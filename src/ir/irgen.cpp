// IRGen: AST -> SSA IR, building SSA form directly during lowering.
//
// Algorithm: M. Braun, S. Buchwald, S. Hack, R. Leißa, C. Mallon, A. Zwinkau,
//   "Simple and Efficient Construction of Static Single Assignment Form",
//   Compiler Construction (CC) 2013, Algorithms 1-3.
//
// Instead of computing dominance frontiers (Cytron et al.), each block keeps a
// map from source variable to its current SSA value:
//
//   writeVariable(v, B, val)  - an assignment to v in B
//   readVariable(v, B)        - the value of v at the end of B so far; if B
//                               has no local definition, look it up in the
//                               predecessors, inserting a phi at joins.
//
// A block is "sealed" once all its predecessors are known. Reading a variable
// in an unsealed block creates an *incomplete* phi whose operands are filled
// in at sealing time. WarpForge has no loops, so every block can be sealed as
// soon as the branches into it exist, but the general mechanism is kept so
// the code reads like the paper (and would extend to loops unchanged).
//
// Phis that turn out to be trivial (all operands are the same value, or the
// phi itself) are removed on the fly (tryRemoveTrivialPhi), so a variable that
// isn't reassigned in either arm of an `if` gets no phi at the join.
//
// Statement lowering:
//   if (c) {T} else {E}   entry: condbr c, if.then, if.else
//                         if.then: T; br if.end      (if it falls through)
//                         if.else: E; br if.end
//                         if.end:  ...               (created only if reachable)
//   return;               ret; the remaining statements of the block are dead
//                         and are not lowered.

#include "ir/irgen.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <set>

namespace wf {

namespace {

using ir::BasicBlock;
using ir::Instruction;
using ir::Value;

ir::Type lowerType(TypeKind t) {
  switch (t) {
  case TypeKind::Int: return ir::Type::I32;
  case TypeKind::Float: return ir::Type::F32;
  case TypeKind::FloatPtr: return ir::Type::Ptr;
  case TypeKind::Bool: return ir::Type::I1;
  case TypeKind::Error: break;
  }
  assert(false && "lowering an ill-typed program");
  return ir::Type::Void;
}

class KernelLowering {
public:
  explicit KernelLowering(const Kernel& k) : kernel_(k), fn_(std::make_unique<ir::Function>(k.name)) {}

  std::unique_ptr<ir::Function> run() {
    BasicBlock* entry = fn_->createBlock("entry");
    sealBlock(entry); // the entry never gains predecessors
    builder_.setInsertPoint(entry);
    for (const VarDecl* p : kernel_.params) {
      ir::Argument* arg = fn_->addArg(lowerType(p->type), p->name);
      writeVariable(p, entry, arg);
    }
    lowerBlock(*kernel_.body);
    if (!terminated()) builder_.ret();
    return std::move(fn_);
  }

private:
  // ------------------------------------------------- SSA construction (Braun)

  void writeVariable(const VarDecl* var, BasicBlock* block, Value* value) {
    currentDef_[var][block] = value;
  }

  Value* readVariable(const VarDecl* var, BasicBlock* block) {
    auto& defs = currentDef_[var];
    if (auto it = defs.find(block); it != defs.end()) return it->second; // local value numbering
    return readVariableRecursive(var, block);                           // global value numbering
  }

  Value* readVariableRecursive(const VarDecl* var, BasicBlock* block) {
    Value* val;
    if (!sealed_.count(block)) {
      // Incomplete CFG: operands are added when the block is sealed.
      Instruction* phi = newPhi(var, block);
      incompletePhis_[block].emplace_back(var, phi);
      val = phi;
    } else {
      std::vector<BasicBlock*> preds = block->predecessors();
      assert(!preds.empty() && "reading a variable in an unreachable block");
      if (preds.size() == 1) {
        val = readVariable(var, preds[0]); // no phi needed
      } else {
        // Break potential cycles with an operandless phi first.
        Instruction* phi = newPhi(var, block);
        writeVariable(var, block, phi);
        val = addPhiOperands(var, phi);
      }
    }
    writeVariable(var, block, val);
    return val;
  }

  Instruction* newPhi(const VarDecl* var, BasicBlock* block) {
    Instruction* phi = ir::IRBuilder().phi(lowerType(var->type), block);
    phi->name = var->name;
    return phi;
  }

  Value* addPhiOperands(const VarDecl* var, Instruction* phi) {
    for (BasicBlock* pred : phi->parent()->predecessors())
      phi->addIncoming(readVariable(var, pred), pred);
    return tryRemoveTrivialPhi(phi);
  }

  Value* tryRemoveTrivialPhi(Instruction* phi) {
    Value* same = nullptr;
    for (Value* op : phi->operands()) {
      if (op == same || op == phi) continue; // unique value or self-reference
      if (same) return phi;                  // the phi merges at least two values
      same = op;
    }
    assert(same && "phi is unreachable or in the start block");

    // Remember the phi's users (excluding itself) before rerouting them.
    // (A vector in use-list order, not a pointer-ordered set: output must be
    // deterministic.)
    std::vector<Instruction*> phiUsers;
    for (Instruction* u : phi->users())
      if (u != phi && u->op() == ir::Op::Phi &&
          std::find(phiUsers.begin(), phiUsers.end(), u) == phiUsers.end())
        phiUsers.push_back(u);

    phi->replaceAllUsesWith(same);
    // The variable maps may still name the phi as a current definition.
    for (auto& [var, defs] : currentDef_)
      for (auto& [block, value] : defs)
        if (value == phi) value = same;
    for (auto& [block, phis] : incompletePhis_)
      for (auto& [var, p] : phis)
        if (p == phi) p = nullptr;
    phi->eraseFromParent();

    // Removing this phi may have made phis that used it trivial too.
    for (Instruction* u : phiUsers)
      if (u->parent()) tryRemoveTrivialPhi(u); // may already have been removed
    return same;
  }

  void sealBlock(BasicBlock* block) {
    for (auto& [var, phi] : incompletePhis_[block])
      if (phi) addPhiOperands(var, phi);
    incompletePhis_.erase(block);
    sealed_.insert(block);
  }

  // ---------------------------------------------------------- statements

  bool terminated() const { return builder_.insertBlock()->terminator() != nullptr; }
  BasicBlock* current() const { return builder_.insertBlock(); }

  void lowerBlock(const BlockStmt& block) {
    for (const StmtPtr& s : block.stmts) {
      if (terminated()) return; // code after `return` is unreachable
      lowerStmt(*s);
    }
  }

  void lowerStmt(const Stmt& s) {
    switch (s.kind) {
    case StmtKind::Decl: {
      const auto& d = static_cast<const DeclStmt&>(s);
      define(d.decl, lowerExpr(*d.init));
      break;
    }
    case StmtKind::Assign: {
      const auto& a = static_cast<const AssignStmt&>(s);
      define(a.target->decl, lowerExpr(*a.value));
      break;
    }
    case StmtKind::Store: {
      const auto& st = static_cast<const StoreStmt&>(s);
      Value* index = lowerExpr(*st.index);
      Value* value = lowerExpr(*st.value);
      Value* address = builder_.addr(pointer(st.base->decl), index);
      builder_.store(value, address);
      break;
    }
    case StmtKind::If: lowerIf(static_cast<const IfStmt&>(s)); break;
    case StmtKind::Return: builder_.ret(); break;
    case StmtKind::Block: lowerBlock(static_cast<const BlockStmt&>(s)); break;
    }
  }

  // Assignment: record the new SSA value; give it the variable's name for
  // readable dumps if it's a fresh, unnamed instruction.
  void define(const VarDecl* var, Value* value) {
    if (value->valueKind() == Value::Kind::Instruction && value->name.empty()) value->name = var->name;
    writeVariable(var, current(), value);
  }

  void lowerIf(const IfStmt& s) {
    Value* cond = lowerExpr(*s.cond);
    BasicBlock* thenBB = fn_->createBlock("if.then");
    BasicBlock* elseBB = s.elseStmt ? fn_->createBlock("if.else") : nullptr;
    // Without an else, the false edge goes straight to the join, so the
    // join is always reachable and must exist now.
    BasicBlock* endBB = elseBB ? nullptr : fn_->createBlock("if.end");
    builder_.condBr(cond, thenBB, elseBB ? elseBB : endBB);

    std::vector<BasicBlock*> fallthrough; // arm exits that branch to the join
    sealBlock(thenBB);
    builder_.setInsertPoint(thenBB);
    lowerBlock(*s.thenBlock);
    if (!terminated()) fallthrough.push_back(current());

    if (elseBB) {
      sealBlock(elseBB);
      builder_.setInsertPoint(elseBB);
      lowerStmt(*s.elseStmt);
      if (!terminated()) fallthrough.push_back(current());
      if (fallthrough.empty()) return; // both arms returned: nothing follows
      endBB = fn_->createBlock("if.end");
    }

    for (BasicBlock* from : fallthrough) {
      builder_.setInsertPoint(from);
      builder_.br(endBB);
    }
    sealBlock(endBB); // all predecessors are now known
    builder_.setInsertPoint(endBB);
  }

  // --------------------------------------------------------- expressions

  Value* pointer(const VarDecl* param) {
    assert(param->type == TypeKind::FloatPtr && param->isParam);
    return fn_->args()[param->paramIndex].get();
  }

  Value* lowerExpr(const Expr& e) {
    switch (e.kind) {
    case ExprKind::IntLit: return fn_->constI32(static_cast<const IntLitExpr&>(e).value);
    case ExprKind::FloatLit: return fn_->constF32(static_cast<const FloatLitExpr&>(e).value);
    case ExprKind::VarRef: return readVariable(static_cast<const VarRefExpr&>(e).decl, current());
    case ExprKind::Index: {
      const auto& x = static_cast<const IndexExpr&>(e);
      Value* index = lowerExpr(*x.index);
      return builder_.load(builder_.addr(pointer(x.base->decl), index));
    }
    case ExprKind::GlobalId: return builder_.globalId();
    case ExprKind::Neg: return builder_.neg(lowerExpr(*static_cast<const NegExpr&>(e).operand));
    case ExprKind::Binary: {
      const auto& b = static_cast<const BinaryExpr&>(e);
      Value* lhs = lowerExpr(*b.lhs);
      Value* rhs = lowerExpr(*b.rhs);
      switch (b.op) {
      case BinaryOp::Add: return builder_.add(lhs, rhs);
      case BinaryOp::Sub: return builder_.sub(lhs, rhs);
      case BinaryOp::Mul: return builder_.mul(lhs, rhs);
      case BinaryOp::Less: return builder_.lt(lhs, rhs);
      }
      break;
    }
    }
    assert(false && "unhandled expression");
    return nullptr;
  }

  const Kernel& kernel_;
  std::unique_ptr<ir::Function> fn_;
  ir::IRBuilder builder_;
  std::map<const VarDecl*, std::map<BasicBlock*, Value*>> currentDef_;
  // Per block, in creation order (iteration order must not depend on pointers).
  std::map<BasicBlock*, std::vector<std::pair<const VarDecl*, Instruction*>>> incompletePhis_;
  std::set<BasicBlock*> sealed_;
};

} // namespace

std::unique_ptr<ir::Module> lowerProgram(const Program& program) {
  auto module = std::make_unique<ir::Module>();
  for (const auto& k : program.kernels) module->functions.push_back(KernelLowering(*k).run());
  return module;
}

} // namespace wf

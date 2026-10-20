// IR verifier.
//
// Checked invariants:
//   1. Every block ends in exactly one terminator, and phis come first.
//   2. Branch targets belong to this function; the entry has no predecessors
//      and therefore no phis.
//   3. Each opcode has the right number and types of operands.
//   4. A phi has exactly one incoming value per predecessor, and none from
//      a block that is not a predecessor.
//   5. SSA dominance: every use of an instruction is dominated by its
//      definition. For a phi, the "use" happens at the end of the incoming
//      block, so the definition must dominate that block instead.
// Uses inside unreachable blocks are not dominance-checked (nothing there
// executes); passes are free to leave such blocks for simplify-cfg.

#include "ir/verifier.hpp"

#include "ir/dominators.hpp"
#include "ir/printer.hpp"

#include <unordered_map>
#include <unordered_set>

namespace wf::ir {

namespace {

class Verifier {
public:
  explicit Verifier(const Function& fn) : fn_(fn) {}

  std::vector<std::string> run() {
    if (fn_.blocks().empty()) {
      fail("function has no blocks (every kernel needs at least an entry block ending in 'ret')");
      return errors_; // every later check assumes an entry block
    }
    for (const auto& bb : fn_.blocks()) blocks_.insert(bb.get());
    for (const auto& bb : fn_.blocks()) {
      size_t pos = 0;
      for (const auto& inst : bb->insts()) position_[inst.get()] = {bb.get(), pos++};
    }
    for (const auto& a : fn_.args()) args_.insert(a.get());

    for (const auto& bb : fn_.blocks()) structure(*bb);
    if (fn_.entry() && !fn_.entry()->predecessors().empty())
      fail("entry block '" + fn_.entry()->name + "' has predecessors");
    if (!fn_.entry()->phis().empty())
      fail("phi in the entry block '" + fn_.entry()->name + "' (it has no predecessors to take values from)");
    if (!errors_.empty()) return errors_; // CFG is malformed; skip deeper checks

    DominatorTree dt(fn_);
    for (const auto& bb : fn_.blocks())
      for (const auto& inst : bb->insts()) {
        types(*bb, *inst);
        if (inst->op() == Op::Phi) phiIncoming(*bb, *inst);
        if (dt.reachable(bb.get())) dominance(dt, *bb, *inst);
      }
    return errors_;
  }

private:
  void fail(std::string msg) { errors_.push_back("in @" + fn_.name + ": " + std::move(msg)); }
  void failAt(const BasicBlock& bb, const Instruction& inst, const std::string& msg) {
    fail("block '" + bb.name + "': '" + printInstruction(inst) + "': " + msg);
  }

  void structure(const BasicBlock& bb) {
    const auto& insts = bb.insts();
    if (insts.empty() || !insts.back()->isTerminator())
      fail("block '" + bb.name + "' does not end in a terminator");
    bool seenNonPhi = false;
    size_t i = 0;
    for (const auto& inst : insts) {
      bool last = ++i == insts.size();
      if (inst->isTerminator() && !last) fail("terminator in the middle of block '" + bb.name + "'");
      if (inst->op() == Op::Phi) {
        if (seenNonPhi) fail("phi is not at the start of block '" + bb.name + "'");
      } else {
        seenNonPhi = true;
      }
      if (inst->isTerminator())
        for (const BasicBlock* t : inst->blocks())
          if (!blocks_.count(t))
            fail("branch target '" + t->name + "' is not in function '@" + fn_.name + "'");
    }
  }

  void types(const BasicBlock& bb, const Instruction& inst) {
    const auto& ops = inst.operands();
    auto want = [&](size_t nOps, size_t nBlocks) {
      if (ops.size() != nOps || inst.blocks().size() != nBlocks) {
        failAt(bb, inst, "wrong number of operands");
        return false;
      }
      return true;
    };
    auto isNum = [](Type t) { return t == Type::I32 || t == Type::F32; };
    switch (inst.op()) {
    case Op::Add: case Op::Sub: case Op::Mul:
      if (!want(2, 0)) return;
      if (ops[0]->type() != ops[1]->type() || inst.type() != ops[0]->type() || !isNum(inst.type()))
        failAt(bb, inst, "operand types do not match");
      break;
    case Op::Neg:
      if (!want(1, 0)) return;
      if (inst.type() != ops[0]->type() || !isNum(inst.type())) failAt(bb, inst, "operand types do not match");
      break;
    case Op::Lt:
      if (!want(2, 0)) return;
      if (ops[0]->type() != ops[1]->type() || !isNum(ops[0]->type()) || inst.type() != Type::I1)
        failAt(bb, inst, "operand types do not match");
      break;
    case Op::GlobalId:
      if (want(0, 0) && inst.type() != Type::I32) failAt(bb, inst, "global_id must be i32");
      break;
    case Op::Addr:
      if (!want(2, 0)) return;
      if (ops[0]->type() != Type::Ptr || ops[1]->type() != Type::I32 || inst.type() != Type::Ptr)
        failAt(bb, inst, "addr expects (ptr, i32) -> ptr");
      break;
    case Op::Load:
      if (!want(1, 0)) return;
      if (ops[0]->type() != Type::Ptr || inst.type() != Type::F32)
        failAt(bb, inst, "load expects ptr -> f32");
      break;
    case Op::Store:
      if (!want(2, 0)) return;
      if (ops[0]->type() != Type::F32 || ops[1]->type() != Type::Ptr)
        failAt(bb, inst, "store expects (f32, ptr)");
      break;
    case Op::Phi:
      if (ops.size() != inst.blocks().size()) {
        failAt(bb, inst, "phi operand/block count mismatch");
        return;
      }
      for (const Value* v : ops)
        if (v->type() != inst.type()) failAt(bb, inst, "phi incoming type does not match");
      break;
    case Op::Br: want(0, 1); break;
    case Op::CondBr:
      if (want(1, 2) && ops[0]->type() != Type::I1) failAt(bb, inst, "condbr condition must be i1");
      break;
    case Op::Ret: want(0, 0); break;
    }
    for (const Value* v : ops) {
      if (v->valueKind() == Value::Kind::Instruction &&
          !position_.count(static_cast<const Instruction*>(v)))
        failAt(bb, inst, "operand is not defined in this function");
      if (v->valueKind() == Value::Kind::Argument && !args_.count(static_cast<const Argument*>(v)))
        failAt(bb, inst, "argument belongs to another function");
    }
  }

  void phiIncoming(const BasicBlock& bb, const Instruction& phi) {
    std::vector<BasicBlock*> preds = bb.predecessors();
    std::unordered_set<const BasicBlock*> predSet(preds.begin(), preds.end());
    std::unordered_map<const BasicBlock*, int> seen;
    for (const BasicBlock* in : phi.blocks()) {
      if (!predSet.count(in))
        failAt(bb, phi, "phi has incoming block '" + in->name + "' that is not a predecessor");
      if (++seen[in] > 1) failAt(bb, phi, "phi lists incoming block '" + in->name + "' twice");
    }
    for (const BasicBlock* p : preds)
      if (!seen.count(p)) failAt(bb, phi, "phi has no incoming value for predecessor '" + p->name + "'");
  }

  void dominance(const DominatorTree& dt, const BasicBlock& bb, const Instruction& inst) {
    for (size_t i = 0; i < inst.operands().size(); ++i) {
      const Value* v = inst.operand(i);
      if (v->valueKind() != Value::Kind::Instruction) continue;
      auto def = position_.find(static_cast<const Instruction*>(v));
      if (def == position_.end()) continue; // reported by types()
      const BasicBlock* defBlock = def->second.first;
      bool ok;
      if (inst.op() == Op::Phi) {
        const BasicBlock* from = inst.blocks()[i];
        ok = !dt.reachable(from) || dt.dominates(defBlock, from);
      } else if (defBlock == &bb) {
        ok = def->second.second < position_.at(&inst).second;
      } else {
        ok = dt.dominates(defBlock, &bb);
      }
      if (!ok)
        failAt(bb, inst, "operand '" + std::string(printInstruction(*static_cast<const Instruction*>(v))) +
                             "' does not dominate its use");
    }
  }

  const Function& fn_;
  std::vector<std::string> errors_;
  std::unordered_set<const BasicBlock*> blocks_;
  std::unordered_set<const Argument*> args_;
  std::unordered_map<const Instruction*, std::pair<const BasicBlock*, size_t>> position_;
};

} // namespace

std::vector<std::string> verifyFunction(const Function& fn) { return Verifier(fn).run(); }

std::vector<std::string> verifyModule(const Module& module) {
  std::vector<std::string> all;
  for (const auto& fn : module.functions) {
    auto errs = verifyFunction(*fn);
    all.insert(all.end(), errs.begin(), errs.end());
  }
  return all;
}

} // namespace wf::ir

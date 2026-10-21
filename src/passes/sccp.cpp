// sccp: Sparse Conditional Constant Propagation.
//
// M. N. Wegman, F. K. Zadeck, "Constant Propagation with Conditional
// Branches", ACM TOPLAS 13(2), 1991.
//
// Every SSA value gets a lattice value:
//
//            Top             "no evidence yet" (optimistic start)
//      c1    c2    c3 ...    "always this constant"
//           Bottom           "not a constant / unknown at compile time"
//
// Values only move down. Two worklists drive the analysis:
//   * CFG edges that just became executable: their target's phis are
//     re-evaluated, and the first time a block is reached all of its
//     instructions are visited.
//   * SSA values whose lattice value just changed: their users are
//     re-evaluated.
// A phi is the meet of its incoming values over *executable* edges only.
// That's the "conditional" part: in
//     x = 1; if (2 < 1) { x = 5; }  ... use(x)
// the edge into `x = 5` never becomes executable, so the phi at the join is
// 1, which neither constant folding nor plain constant propagation can find.
//
// Rewriting afterwards:
//   * every instruction proven constant is replaced by that constant;
//   * `condbr` on a constant becomes `br` to the taken target (the not-taken
//     target loses this block as a predecessor, so its phi entries go too).
// Blocks proven unreachable are left in place for simplify-cfg to delete.

#include "passes/pass.hpp"

#include "ir/const_eval.hpp"

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wf::passes {

namespace {

using ir::BasicBlock;
using ir::Constant;
using ir::Instruction;
using ir::Op;
using ir::Value;

struct Lattice {
  enum State { Top, Const, Bottom } state = Top;
  Constant* value = nullptr;
  bool operator==(const Lattice&) const = default;
};

class SCCPSolver {
public:
  explicit SCCPSolver(ir::Function& fn) : fn_(fn) {}

  void solve() {
    flowWork_.push_back({nullptr, fn_.entry()});
    while (!flowWork_.empty() || !ssaWork_.empty()) {
      while (!flowWork_.empty()) {
        auto [from, to] = flowWork_.back();
        flowWork_.pop_back();
        if (!executableEdges_.insert({from, to}).second) continue;
        for (Instruction* phi : to->phis()) visit(*phi);
        if (executableBlocks_.insert(to).second)
          for (auto& inst : to->insts())
            if (inst->op() != Op::Phi) visit(*inst);
      }
      while (!ssaWork_.empty()) {
        Instruction* inst = ssaWork_.back();
        ssaWork_.pop_back();
        if (executableBlocks_.count(inst->parent())) visit(*inst);
      }
    }
  }

  bool rewrite() {
    bool changed = false;
    for (auto& bb : fn_.blocks()) {
      if (!executableBlocks_.count(bb.get())) continue;
      auto& insts = bb->insts();
      for (auto it = insts.begin(); it != insts.end();) {
        Instruction& inst = **it++;
        if (inst.op() == Op::CondBr) {
          changed |= foldBranch(*bb, inst);
          break; // terminator: end of block
        }
        if (inst.type() == ir::Type::Void || inst.isTerminator()) continue;
        Lattice l = get(&inst);
        if (l.state == Lattice::Const) {
          inst.replaceAllUsesWith(l.value);
          inst.eraseFromParent();
          changed = true;
        }
      }
    }
    return changed;
  }

private:
  Lattice get(const Value* v) {
    if (v->valueKind() == Value::Kind::Constant)
      return {Lattice::Const, const_cast<Constant*>(static_cast<const Constant*>(v))};
    if (v->valueKind() == Value::Kind::Argument) return {Lattice::Bottom, nullptr};
    auto it = values_.find(v);
    return it == values_.end() ? Lattice{} : it->second;
  }

  static Lattice meet(Lattice a, Lattice b) {
    if (a.state == Lattice::Top) return b;
    if (b.state == Lattice::Top) return a;
    if (a.state == Lattice::Bottom || b.state == Lattice::Bottom) return {Lattice::Bottom, nullptr};
    return a.value == b.value ? a : Lattice{Lattice::Bottom, nullptr}; // constants are interned
  }

  void update(Instruction& inst, Lattice l) {
    Lattice old = get(&inst);
    if (old == l) return;
    values_[&inst] = l;
    for (Instruction* user : inst.users()) ssaWork_.push_back(user);
  }

  void visit(Instruction& inst) {
    switch (inst.op()) {
    case Op::Phi: {
      Lattice l;
      for (size_t i = 0; i < inst.operands().size(); ++i)
        if (executableEdges_.count({inst.blocks()[i], inst.parent()}))
          l = meet(l, get(inst.operand(i)));
      update(inst, l);
      break;
    }
    case Op::Add: case Op::Sub: case Op::Mul: case Op::Neg: case Op::Lt: {
      std::vector<Constant*> consts;
      for (Value* v : inst.operands()) {
        Lattice l = get(v);
        if (l.state == Lattice::Bottom) return update(inst, {Lattice::Bottom, nullptr});
        if (l.state == Lattice::Top) return; // wait for more information
        consts.push_back(l.value);
      }
      update(inst, {Lattice::Const, ir::evaluate(fn_, inst.op(), consts)});
      break;
    }
    case Op::GlobalId: case Op::Addr: case Op::Load:
      update(inst, {Lattice::Bottom, nullptr}); // known only at run time
      break;
    case Op::Br: flowWork_.push_back({inst.parent(), inst.blocks()[0]}); break;
    case Op::CondBr: {
      Lattice c = get(inst.operand(0));
      if (c.state == Lattice::Top) break; // no edge is executable yet
      bool both = c.state == Lattice::Bottom;
      if (both || c.value->i1()) flowWork_.push_back({inst.parent(), inst.blocks()[0]});
      if (both || !c.value->i1()) flowWork_.push_back({inst.parent(), inst.blocks()[1]});
      break;
    }
    case Op::Store: case Op::Ret: break;
    }
  }

  bool foldBranch(BasicBlock& bb, Instruction& condbr) {
    Lattice c = get(condbr.operand(0));
    if (c.state != Lattice::Const) return false;
    BasicBlock* taken = condbr.blocks()[c.value->i1() ? 0 : 1];
    BasicBlock* dropped = condbr.blocks()[c.value->i1() ? 1 : 0];
    if (dropped != taken) {
      for (Instruction* phi : dropped->phis())
        for (size_t i = phi->blocks().size(); i-- > 0;)
          if (phi->blocks()[i] == &bb) phi->removeIncoming(i);
    }
    condbr.eraseFromParent();
    ir::IRBuilder(&bb).br(taken);
    return true;
  }

  ir::Function& fn_;
  std::unordered_map<const Value*, Lattice> values_;
  std::set<std::pair<const BasicBlock*, const BasicBlock*>> executableEdges_;
  std::unordered_set<const BasicBlock*> executableBlocks_;
  std::vector<std::pair<BasicBlock*, BasicBlock*>> flowWork_;
  std::vector<Instruction*> ssaWork_;
};

class SCCP final : public Pass {
public:
  const char* name() const override { return "sccp"; }
  bool run(ir::Function& fn) override {
    SCCPSolver solver(fn);
    solver.solve();
    return solver.rewrite();
  }
};

} // namespace

std::unique_ptr<Pass> createSCCP() { return std::make_unique<SCCP>(); }

} // namespace wf::passes

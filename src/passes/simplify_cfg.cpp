// simplify-cfg: clean up the control-flow graph after other passes.
//
// Repeats these rewrites until none applies:
//   1. Delete blocks unreachable from the entry (e.g. the arm SCCP proved is
//      never taken), first removing their entries from successors' phis.
//   2. `condbr %c, B, B`  ->  `br B`  (both edges go to the same place).
//   3. Remove phis whose incoming values are all the same value.
//   4. Merge a block B into its predecessor P when P -> B is P's only exit
//      and B's only entry: B's instructions move to the end of P, and phis
//      in B's successors that named B as incoming block now name P.
//   5. Bypass an empty block B (just `br T`) when T has no phis: B's
//      predecessors branch to T directly. (With phis in T, the incoming
//      values would have to be rewritten per edge; that case is skipped.)
//
// None of these change what the program computes; they shrink the CFG so the
// PTX has fewer labels and branches.

#include "passes/pass.hpp"

#include "ir/dominators.hpp"

#include <unordered_set>

namespace wf::passes {

namespace {

using ir::BasicBlock;
using ir::Instruction;
using ir::Op;

bool removeUnreachable(ir::Function& fn) {
  std::vector<BasicBlock*> rpo = ir::reversePostOrder(fn);
  std::unordered_set<const BasicBlock*> reachable(rpo.begin(), rpo.end());
  std::vector<BasicBlock*> dead;
  for (auto& bb : fn.blocks())
    if (!reachable.count(bb.get())) dead.push_back(bb.get());
  if (dead.empty()) return false;

  for (BasicBlock* bb : dead)
    for (BasicBlock* succ : bb->successors())
      for (Instruction* phi : succ->phis())
        for (size_t i = phi->blocks().size(); i-- > 0;)
          if (phi->blocks()[i] == bb) phi->removeIncoming(i);
  // Dead blocks may reference each other's values; drop everything first.
  for (BasicBlock* bb : dead)
    for (auto& inst : bb->insts()) inst->dropAllReferences();
  for (BasicBlock* bb : dead) fn.eraseBlock(bb);
  return true;
}

bool foldSameTargetBranches(ir::Function& fn) {
  bool changed = false;
  for (auto& bb : fn.blocks()) {
    Instruction* t = bb->terminator();
    if (!t || t->op() != Op::CondBr || t->blocks()[0] != t->blocks()[1]) continue;
    BasicBlock* target = t->blocks()[0];
    t->eraseFromParent();
    ir::IRBuilder(bb.get()).br(target);
    changed = true;
  }
  return changed;
}

bool removeRedundantPhis(ir::Function& fn) {
  bool changed = false;
  for (auto& bb : fn.blocks()) {
    for (Instruction* phi : bb->phis()) {
      ir::Value* same = nullptr;
      bool redundant = true;
      for (ir::Value* v : phi->operands()) {
        if (v == phi || v == same) continue;
        if (same) {
          redundant = false;
          break;
        }
        same = v;
      }
      if (!redundant || !same) continue;
      phi->replaceAllUsesWith(same);
      phi->eraseFromParent();
      changed = true;
    }
  }
  return changed;
}

bool mergeIntoPredecessor(ir::Function& fn) {
  for (auto& bbPtr : fn.blocks()) {
    BasicBlock* bb = bbPtr.get();
    if (bb == fn.entry()) continue;
    std::vector<BasicBlock*> preds = bb->predecessors();
    if (preds.size() != 1) continue;
    BasicBlock* pred = preds[0];
    if (pred == bb || pred->successors().size() != 1) continue;

    // Phis in bb have a single incoming value: forward it.
    for (Instruction* phi : bb->phis()) {
      phi->replaceAllUsesWith(phi->operand(0));
      phi->eraseFromParent();
    }
    pred->terminator()->eraseFromParent();
    while (!bb->insts().empty()) pred->append(bb->remove(bb->insts().front().get()));
    for (BasicBlock* succ : pred->successors())
      for (Instruction* phi : succ->phis())
        for (BasicBlock*& in : phi->blocks())
          if (in == bb) in = pred;
    fn.eraseBlock(bb);
    return true; // block list changed: restart the scan
  }
  return false;
}

bool bypassEmptyBlocks(ir::Function& fn) {
  for (auto& bbPtr : fn.blocks()) {
    BasicBlock* bb = bbPtr.get();
    if (bb == fn.entry() || bb->insts().size() != 1) continue;
    Instruction* t = bb->terminator();
    if (!t || t->op() != Op::Br) continue;
    BasicBlock* target = t->blocks()[0];
    if (target == bb || !target->phis().empty()) continue;
    for (BasicBlock* pred : bb->predecessors())
      for (BasicBlock*& succ : pred->terminator()->blocks())
        if (succ == bb) succ = target;
    fn.eraseBlock(bb); // now unreachable; nothing referenced its values
    return true;
  }
  return false;
}

class SimplifyCFG final : public Pass {
public:
  const char* name() const override { return "simplify-cfg"; }

  bool run(ir::Function& fn) override {
    bool changed = false;
    for (bool again = true; again;) {
      again = removeUnreachable(fn);
      again |= foldSameTargetBranches(fn);
      again |= removeRedundantPhis(fn);
      again |= bypassEmptyBlocks(fn);
      again |= mergeIntoPredecessor(fn);
      changed |= again;
    }
    return changed;
  }
};

} // namespace

std::unique_ptr<Pass> createSimplifyCFG() { return std::make_unique<SimplifyCFG>(); }

} // namespace wf::passes

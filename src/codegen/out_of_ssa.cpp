// Out-of-SSA translation helpers.
//
// PTX has no phi instruction, so before emitting code every
//     %x = phi [%a, P1], [%b, P2]
// becomes a copy "x <- a" at the end of P1 and "x <- b" at the end of P2.
// Two classic pitfalls are avoided, following
//   P. Briggs, K. Cooper, T. Harvey, L. T. Simpson, "Practical Improvements to
//   the Construction and Destruction of Static Single Assignment Form",
//   Software Practice & Experience, 1998.
//
// 1. The lost-copy problem. If P1 has another successor S, a copy placed at
//    the end of P1 also executes on the path P1 -> S, clobbering a value S
//    may still need. Fix: split every *critical edge* (P has several
//    successors and the target several predecessors) with an empty block, so
//    copies live on the edge itself.
//
// 2. The swap problem. All phis at the top of a block read their inputs
//    simultaneously; the copies for one edge therefore form a *parallel
//    copy*. Emitting them one by one can overwrite a register another copy
//    still has to read (think {a <- b, b <- a}). Fix: sequentialize the
//    parallel copy: emit a copy only when its destination is no longer
//    needed as a source; when only cycles remain, save one destination in a
//    temporary and redirect its readers to the temporary. (Boissinot et al.,
//    "Revisiting Out-of-SSA Translation for Correctness, Code Quality, and
//    Efficiency", CGO 2009, give the same construction.)

#include "codegen/out_of_ssa.hpp"

#include <algorithm>

namespace wf::codegen {

int splitCriticalEdges(ir::Function& fn) {
  int split = 0;
  // Snapshot: the block list grows while we iterate.
  std::vector<ir::BasicBlock*> blocks;
  for (auto& bb : fn.blocks()) blocks.push_back(bb.get());

  for (ir::BasicBlock* pred : blocks) {
    std::vector<ir::BasicBlock*> succs = pred->successors();
    if (succs.size() < 2) continue;
    for (ir::BasicBlock* succ : succs) {
      // Only edges into blocks with phis need a home for copies.
      if (succ->predecessors().size() < 2 || succ->phis().empty()) continue;
      ir::BasicBlock* edge = fn.createBlockAfter(pred, pred->name + ".crit");
      ir::IRBuilder(edge).br(succ);
      for (ir::BasicBlock*& target : pred->terminator()->blocks())
        if (target == succ) target = edge;
      for (ir::Instruction* phi : succ->phis())
        for (ir::BasicBlock*& in : phi->blocks())
          if (in == pred) in = edge;
      ++split;
    }
  }
  return split;
}

std::vector<Copy> sequentializeParallelCopy(
    std::vector<Copy> pending, const std::function<std::string(const std::string&)>& newTemp) {
  // A copy onto itself does nothing.
  pending.erase(std::remove_if(pending.begin(), pending.end(),
                               [](const Copy& c) { return c.dst == c.src; }),
                pending.end());

  auto isSource = [&](const std::string& reg) {
    return std::any_of(pending.begin(), pending.end(), [&](const Copy& c) { return c.src == reg; });
  };

  std::vector<Copy> out;
  while (!pending.empty()) {
    // Emit any copy whose destination nobody still needs to read.
    auto ready = std::find_if(pending.begin(), pending.end(),
                              [&](const Copy& c) { return !isSource(c.dst); });
    if (ready != pending.end()) {
      out.push_back(*ready);
      pending.erase(ready);
      continue;
    }
    // Only cycles remain. Save the first copy's destination in a temporary
    // and let its readers read the temporary instead; that frees the
    // destination, so the next iteration can emit the copy.
    const std::string blocked = pending.front().dst;
    std::string temp = newTemp(blocked);
    out.push_back({temp, blocked});
    for (Copy& c : pending)
      if (c.src == blocked) c.src = temp;
  }
  return out;
}

} // namespace wf::codegen

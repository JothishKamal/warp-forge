#pragma once

#include "ir/ir.hpp"

#include <unordered_map>
#include <vector>

namespace wf::ir {

// Dominator tree, computed with the iterative algorithm of
//   K. D. Cooper, T. J. Harvey, K. Kennedy,
//   "A Simple, Fast Dominance Algorithm", Software Practice & Experience, 2001.
//
// Blocks are numbered in reverse post-order (RPO); idom(b) is found by
// repeatedly intersecting the idoms of b's already-processed predecessors,
// walking up the partial tree by RPO number. The iteration converges in a
// couple of passes on reducible CFGs (and WarpForge has no loops at all).
// Unreachable blocks have no idom and are dominated by nothing.
class DominatorTree {
public:
  explicit DominatorTree(const Function& fn);

  bool reachable(const BasicBlock* b) const { return rpoIndex_.count(b) != 0; }
  // nullptr for the entry block and for unreachable blocks.
  const BasicBlock* idom(const BasicBlock* b) const;
  // Reflexive: every reachable block dominates itself.
  bool dominates(const BasicBlock* a, const BasicBlock* b) const;
  const std::vector<BasicBlock*>& reversePostOrder() const { return rpo_; }

private:
  std::vector<BasicBlock*> rpo_;
  std::unordered_map<const BasicBlock*, size_t> rpoIndex_;
  std::vector<size_t> idom_; // by RPO index; idom_[0] == 0 (entry)
};

// Reverse post-order of the blocks reachable from the entry.
std::vector<BasicBlock*> reversePostOrder(const Function& fn);

} // namespace wf::ir

#include "ir/dominators.hpp"

#include <unordered_set>

namespace wf::ir {

std::vector<BasicBlock*> reversePostOrder(const Function& fn) {
  std::vector<BasicBlock*> post;
  if (!fn.entry()) return post;
  // Iterative DFS: (block, index of next successor to visit).
  std::unordered_set<const BasicBlock*> seen;
  std::vector<std::pair<BasicBlock*, size_t>> stack;
  stack.emplace_back(fn.entry(), 0);
  seen.insert(fn.entry());
  while (!stack.empty()) {
    auto& [bb, next] = stack.back();
    std::vector<BasicBlock*> succs = bb->successors();
    if (next < succs.size()) {
      BasicBlock* s = succs[next++];
      if (seen.insert(s).second) stack.emplace_back(s, 0);
    } else {
      post.push_back(bb);
      stack.pop_back();
    }
  }
  return {post.rbegin(), post.rend()};
}

DominatorTree::DominatorTree(const Function& fn) : rpo_(ir::reversePostOrder(fn)) {
  if (rpo_.empty()) return;
  for (size_t i = 0; i < rpo_.size(); ++i) rpoIndex_[rpo_[i]] = i;

  constexpr size_t kUndefined = static_cast<size_t>(-1);
  idom_.assign(rpo_.size(), kUndefined);
  idom_[0] = 0;

  // "intersect" from the paper: walk both fingers up the (partial) tree
  // until they meet. A higher RPO index means deeper in the tree.
  auto intersect = [&](size_t a, size_t b) {
    while (a != b) {
      while (a > b) a = idom_[a];
      while (b > a) b = idom_[b];
    }
    return a;
  };

  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 1; i < rpo_.size(); ++i) {
      size_t newIdom = kUndefined;
      for (BasicBlock* pred : rpo_[i]->predecessors()) {
        auto it = rpoIndex_.find(pred);
        if (it == rpoIndex_.end()) continue; // unreachable predecessor
        size_t p = it->second;
        if (idom_[p] == kUndefined) continue; // not processed yet
        newIdom = newIdom == kUndefined ? p : intersect(p, newIdom);
      }
      if (newIdom != idom_[i]) {
        idom_[i] = newIdom;
        changed = true;
      }
    }
  }
}

const BasicBlock* DominatorTree::idom(const BasicBlock* b) const {
  auto it = rpoIndex_.find(b);
  if (it == rpoIndex_.end() || it->second == 0) return nullptr;
  return rpo_[idom_[it->second]];
}

bool DominatorTree::dominates(const BasicBlock* a, const BasicBlock* b) const {
  auto ia = rpoIndex_.find(a);
  auto ib = rpoIndex_.find(b);
  if (ia == rpoIndex_.end() || ib == rpoIndex_.end()) return false;
  size_t target = ia->second;
  size_t cur = ib->second;
  // Walk up from b; idoms always have smaller RPO numbers.
  while (cur > target) cur = idom_[cur];
  return cur == target;
}

} // namespace wf::ir

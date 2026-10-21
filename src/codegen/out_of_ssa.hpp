#pragma once

#include "ir/ir.hpp"

#include <functional>
#include <string>
#include <vector>

namespace wf::codegen {

// One register-to-register (or immediate-to-register) move.
struct Copy {
  std::string dst;
  std::string src;
  bool operator==(const Copy&) const = default;
};

// Splits every critical edge (from a block with several successors to a
// block with several predecessors) whose target has phis, by inserting an
// empty block on it, so phi copies can be placed on the edge itself.
// Returns the number of edges split.
int splitCriticalEdges(ir::Function& fn);

// Orders a *parallel* copy (all sources read before any destination is
// written) into an equivalent sequence of ordinary moves. Cycles such as
// {a <- b, b <- a} are broken with a temporary obtained from
// newTemp(dst), which must return a fresh register of dst's class.
std::vector<Copy> sequentializeParallelCopy(std::vector<Copy> copies,
                                            const std::function<std::string(const std::string&)>& newTemp);

} // namespace wf::codegen

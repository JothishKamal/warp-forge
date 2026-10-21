#pragma once

#include "ir/ir.hpp"

#include <string>
#include <vector>

namespace wf::ir {

// Structural and SSA checks (docs/ir.md "Verifier"). Returns one message per
// problem found; an empty vector means the IR is well-formed.
std::vector<std::string> verifyFunction(const Function& fn);
std::vector<std::string> verifyModule(const Module& module);

} // namespace wf::ir

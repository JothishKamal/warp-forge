#pragma once

#include "ir/ir.hpp"

#include <memory>
#include <string_view>

namespace wf::passes {

// A function pass. run() must return true iff it changed the IR, and leave
// the IR valid (the pass manager verifies after every pass).
class Pass {
public:
  virtual ~Pass() = default;
  virtual const char* name() const = 0;
  virtual bool run(ir::Function& fn) = 0;
};

std::unique_ptr<Pass> createConstFold();
std::unique_ptr<Pass> createSCCP();
std::unique_ptr<Pass> createDCE();
std::unique_ptr<Pass> createSimplifyCFG();

// By name: "const-fold", "sccp", "dce", "simplify-cfg". nullptr if unknown.
std::unique_ptr<Pass> createPass(std::string_view name);

} // namespace wf::passes

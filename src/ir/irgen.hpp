#pragma once

#include "frontend/ast.hpp"
#include "ir/ir.hpp"

#include <memory>

namespace wf {

// Lowers a type-checked Program to SSA IR, one ir::Function per kernel.
// Precondition: checkProgram() succeeded on `program`.
std::unique_ptr<ir::Module> lowerProgram(const Program& program);

} // namespace wf

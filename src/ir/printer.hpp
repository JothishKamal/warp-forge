#pragma once

#include "ir/ir.hpp"

#include <string>

namespace wf::ir {

// Stable textual form of the IR (see docs/ir.md). Value names are assigned at
// print time, in block/instruction order, so the output depends only on the
// IR's structure: printing the same IR twice is byte-identical.
std::string printFunction(const Function& fn);
std::string printModule(const Module& module);

// The spelling of a single instruction, e.g. "%3 = add.i32 %1, 7", using
// fresh numbering local to that call. Used in verifier messages.
std::string printInstruction(const Instruction& inst);

} // namespace wf::ir

#pragma once

#include "ir/ir.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace wf::ir {

// Parses the textual IR printed by printModule() (docs/ir.md). Lines whose
// first non-blank character is ';' are comments, so `--print-after-all`
// output can be fed straight back in.
//
// Round trip: printModule(*parseIR(printModule(m))) == printModule(m).
//
// Returns nullptr on a syntax error and sets `error` to "line:col: message".
// Semantic problems (types, dominance, phi/predecessor mismatches) are left
// to the verifier, so a test can hand it deliberately broken IR.
std::unique_ptr<Module> parseIR(std::string_view text, std::string& error);

} // namespace wf::ir

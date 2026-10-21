#pragma once

#include "frontend/diagnostics.hpp"
#include "frontend/token.hpp"

namespace wf {

// Tokenizes a whole file. Always ends with an Eof token, even after errors,
// so the parser never needs a bounds check.
std::vector<Token> lex(const SourceFile& file, DiagnosticEngine& diags);

} // namespace wf

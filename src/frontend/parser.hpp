#pragma once

#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"
#include "frontend/token.hpp"

#include <memory>
#include <vector>

namespace wf {

// Recursive-descent parser for the grammar in docs/language.md. On a syntax
// error it reports, then resynchronizes at the next `;` or `}` so several
// independent errors can be reported in one run. The returned Program is
// always non-null but is only meaningful if no errors were reported.
std::unique_ptr<Program> parse(const std::vector<Token>& tokens, DiagnosticEngine& diags);

} // namespace wf

#pragma once

#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"

namespace wf {

// Semantic analysis: resolves every VarRefExpr to its VarDecl, assigns a
// TypeKind to every expression, and enforces the typing rules of
// docs/language.md. Returns true if no errors were reported.
bool checkProgram(Program& program, DiagnosticEngine& diags);

} // namespace wf

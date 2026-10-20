#include "frontend/diagnostics.hpp"

#include <ostream>

namespace wf {

void DiagnosticEngine::error(SourceLoc loc, std::string message) {
  diags_.push_back({loc, std::move(message)});
}

void DiagnosticEngine::print(std::ostream& out) const {
  for (const Diagnostic& d : diags_) {
    out << file_.path() << ':' << d.loc.line << ':' << d.loc.col << ": error: " << d.message
        << '\n';
    std::string_view line = file_.lineText(d.loc.line);
    out << line << '\n';
    // Copy tabs from the source line so the caret lines up in any terminal.
    for (uint32_t i = 1; i < d.loc.col && i - 1 < line.size(); ++i)
      out << (line[i - 1] == '\t' ? '\t' : ' ');
    out << "^\n";
  }
}

} // namespace wf

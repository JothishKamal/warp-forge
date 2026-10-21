#pragma once

#include "frontend/source.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace wf {

struct Diagnostic {
  SourceLoc loc;
  std::string message;
};

// Collects errors and prints them in GCC/Clang style:
//   path:line:col: error: message
//   <source line>
//   <caret under the column>
class DiagnosticEngine {
public:
  static constexpr size_t kMaxErrors = 20;

  explicit DiagnosticEngine(const SourceFile& file) : file_(file) {}

  void error(SourceLoc loc, std::string message);
  size_t errorCount() const { return diags_.size(); }
  bool tooMany() const { return diags_.size() >= kMaxErrors; }
  const std::vector<Diagnostic>& all() const { return diags_; }
  void print(std::ostream& out) const;

private:
  const SourceFile& file_;
  std::vector<Diagnostic> diags_;
};

} // namespace wf

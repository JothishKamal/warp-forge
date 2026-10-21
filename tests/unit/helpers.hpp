#pragma once

// Shared test helpers: run the frontend + IRGen on a source string.

#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/sema.hpp"
#include "ir/irgen.hpp"
#include "ir/printer.hpp"
#include "ir/verifier.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>

namespace wf::test {

inline std::unique_ptr<ir::Module> compileToIR(const std::string& source) {
  SourceFile file("test.wf", source);
  DiagnosticEngine diags(file);
  auto program = parse(lex(file, diags), diags);
  if (diags.errorCount() == 0) checkProgram(*program, diags);
  if (diags.errorCount() != 0) {
    std::ostringstream out;
    diags.print(out);
    ADD_FAILURE() << "frontend errors:\n" << out.str();
    return std::make_unique<ir::Module>();
  }
  auto module = lowerProgram(*program);
  for (const std::string& e : ir::verifyModule(*module)) ADD_FAILURE() << "verifier: " << e;
  return module;
}

inline std::string irFor(const std::string& source) {
  return ir::printModule(*compileToIR(source));
}

} // namespace wf::test

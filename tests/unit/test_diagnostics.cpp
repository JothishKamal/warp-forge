#include "frontend/diagnostics.hpp"
#include "frontend/source.hpp"

#include <gtest/gtest.h>
#include <sstream>

using namespace wf;

TEST(Diagnostics, FormatsWithCaret) {
  SourceFile file("t.wf", "int x\n  y = 1;\n");
  DiagnosticEngine diags(file);
  diags.error({2, 3}, "undefined variable 'y'");

  std::ostringstream out;
  diags.print(out);
  EXPECT_EQ(out.str(), "t.wf:2:3: error: undefined variable 'y'\n"
                       "  y = 1;\n"
                       "  ^\n");
  EXPECT_EQ(diags.errorCount(), 1u);
}

TEST(Diagnostics, LineTextHandlesLastLineWithoutNewline) {
  SourceFile file("t.wf", "a\nbc");
  EXPECT_EQ(file.lineText(1), "a");
  EXPECT_EQ(file.lineText(2), "bc");
  EXPECT_EQ(file.lineText(3), "");
}

TEST(Diagnostics, CaretKeepsTabsForAlignment) {
  SourceFile file("t.wf", "\tx;\n");
  DiagnosticEngine diags(file);
  diags.error({1, 2}, "boom");
  std::ostringstream out;
  diags.print(out);
  EXPECT_EQ(out.str(), "t.wf:1:2: error: boom\n\tx;\n\t^\n");
}

TEST(Diagnostics, TooManyAfterTwenty) {
  SourceFile file("t.wf", "x\n");
  DiagnosticEngine diags(file);
  for (int i = 0; i < 19; ++i) diags.error({1, 1}, "e");
  EXPECT_FALSE(diags.tooMany());
  diags.error({1, 1}, "e");
  EXPECT_TRUE(diags.tooMany());
}

#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <gtest/gtest.h>
#include <sstream>

using namespace wf;

namespace {

struct Parsed {
  SourceFile file;
  DiagnosticEngine diags;
  std::unique_ptr<Program> program;
  explicit Parsed(std::string text) : file("t.wf", std::move(text)), diags(file) {
    program = parse(lex(file, diags), diags);
  }
  std::string dump() const {
    std::ostringstream out;
    dumpAst(*program, out);
    return out.str();
  }
  std::string firstError() const {
    return diags.errorCount() ? diags.all()[0].message : std::string();
  }
};

} // namespace

TEST(Parser, VectorAddAst) {
  Parsed p("kernel vector_add(float* a, float* b, float* c, int n) {\n"
           "  int i = global_id();\n"
           "  if (i < n) {\n"
           "    c[i] = a[i] + b[i];\n"
           "  }\n"
           "}\n");
  ASSERT_EQ(p.diags.errorCount(), 0u) << p.firstError();
  EXPECT_EQ(p.dump(), "Kernel vector_add\n"
                      "  Param float* a\n"
                      "  Param float* b\n"
                      "  Param float* c\n"
                      "  Param int n\n"
                      "  Block\n"
                      "    Decl int i\n"
                      "      GlobalId\n"
                      "    If\n"
                      "      Binary <\n"
                      "        Var i\n"
                      "        Var n\n"
                      "      Block\n"
                      "        Store c\n"
                      "          Var i\n"
                      "          Binary +\n"
                      "            Index a\n"
                      "              Var i\n"
                      "            Index b\n"
                      "              Var i\n");
}

TEST(Parser, Precedence) {
  Parsed p("kernel k(int x) { x = 1 + 2 * 3 - -x; }");
  ASSERT_EQ(p.diags.errorCount(), 0u) << p.firstError();
  EXPECT_EQ(p.dump(), "Kernel k\n"
                      "  Param int x\n"
                      "  Block\n"
                      "    Assign x\n"
                      "      Binary -\n"
                      "        Binary +\n"
                      "          Int 1\n"
                      "          Binary *\n"
                      "            Int 2\n"
                      "            Int 3\n"
                      "        Neg\n"
                      "          Var x\n");
}

TEST(Parser, ElseIfAndReturn) {
  Parsed p("kernel k(int x) { if (x < 1) { return; } else if (x < 2) { x = 0.5; } else { } }");
  ASSERT_EQ(p.diags.errorCount(), 0u) << p.firstError();
  EXPECT_EQ(p.dump(), "Kernel k\n"
                      "  Param int x\n"
                      "  Block\n"
                      "    If\n"
                      "      Binary <\n"
                      "        Var x\n"
                      "        Int 1\n"
                      "      Block\n"
                      "        Return\n"
                      "      Else\n"
                      "        If\n"
                      "          Binary <\n"
                      "            Var x\n"
                      "            Int 2\n"
                      "          Block\n"
                      "            Assign x\n"
                      "              Float 0.5\n"
                      "          Else\n"
                      "            Block\n");
}

TEST(Parser, ParenthesesAndMultipleKernels) {
  Parsed p("kernel a(int x) { x = (x + 1) * 2; }\nkernel b() { }");
  ASSERT_EQ(p.diags.errorCount(), 0u) << p.firstError();
  ASSERT_EQ(p.program->kernels.size(), 2u);
  EXPECT_EQ(p.program->kernels[1]->name, "b");
}

TEST(Parser, UnsupportedWhile) {
  Parsed p("kernel k(int x) { while (x < 1) { } }");
  EXPECT_EQ(p.firstError(), "'while' is not supported in WarpForge");
}

TEST(Parser, UnsupportedOperator) {
  Parsed p("kernel k(int x) { x = x / 2; }");
  EXPECT_EQ(p.firstError(), "operator '/' is not supported in WarpForge");
  EXPECT_EQ(p.diags.all()[0].loc.col, 25u);
}

TEST(Parser, UnsupportedType) {
  Parsed p("kernel k(int* p) { }");
  EXPECT_EQ(p.firstError(), "unsupported type 'int*' (supported: int, float, float*)");
}

TEST(Parser, PointerLocalUnsupported) {
  Parsed p("kernel k(float* p) { float* q = p; }");
  EXPECT_EQ(p.firstError(), "pointer locals are not supported in WarpForge; pointers may only be kernel parameters");
}

TEST(Parser, DeclarationNeedsInitializer) {
  Parsed p("kernel k() { int x; }");
  EXPECT_EQ(p.firstError(), "declaration of 'x' must have an initializer");
}

TEST(Parser, UnsupportedCall) {
  Parsed p("kernel k(int x) { x = foo(); }");
  EXPECT_EQ(p.firstError(), "function calls are not supported in WarpForge (only global_id())");
}

TEST(Parser, RecoversMultipleErrors) {
  Parsed p("kernel k(int x) {\n  x = ;\n  x = x / 1;\n  x = 2;\n}\n");
  ASSERT_EQ(p.diags.errorCount(), 2u);
  EXPECT_EQ(p.diags.all()[0].loc.line, 2u);
  EXPECT_EQ(p.diags.all()[0].message, "expected an expression, found ';'");
  EXPECT_EQ(p.diags.all()[1].loc.line, 3u);
}

TEST(Parser, MissingSemicolon) {
  Parsed p("kernel k(int x) { x = 1 }");
  EXPECT_EQ(p.firstError(), "expected ';' after assignment, found '}'");
}

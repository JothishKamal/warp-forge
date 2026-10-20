#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/sema.hpp"

#include <gtest/gtest.h>
#include <sstream>

using namespace wf;

namespace {

struct Checked {
  SourceFile file;
  DiagnosticEngine diags;
  std::unique_ptr<Program> program;
  bool ok = false;
  explicit Checked(std::string text) : file("t.wf", std::move(text)), diags(file) {
    program = parse(lex(file, diags), diags);
    EXPECT_EQ(diags.errorCount(), 0u) << "parse error: " << firstError();
    ok = checkProgram(*program, diags);
  }
  std::string firstError() const {
    return diags.errorCount() ? diags.all()[0].message : std::string();
  }
  SourceLoc firstLoc() const { return diags.all().at(0).loc; }
};

} // namespace

TEST(Sema, VectorAddTypes) {
  Checked c("kernel v(float* a, float* b, float* c, int n) {\n"
            "  int i = global_id();\n"
            "  if (i < n) { c[i] = a[i] + b[i]; }\n"
            "}\n");
  ASSERT_TRUE(c.ok) << c.firstError();
  std::ostringstream out;
  dumpAst(*c.program, out);
  EXPECT_NE(out.str().find("Binary < : bool"), std::string::npos);
  EXPECT_NE(out.str().find("Index a : float"), std::string::npos);
  EXPECT_NE(out.str().find("GlobalId : int"), std::string::npos);
}

TEST(Sema, ResolvesToDecl) {
  Checked c("kernel k(int x) { int y = x; }");
  ASSERT_TRUE(c.ok);
  auto& decl = static_cast<DeclStmt&>(*c.program->kernels[0]->body->stmts[0]);
  auto& ref = static_cast<VarRefExpr&>(*decl.init);
  EXPECT_EQ(ref.decl, c.program->kernels[0]->params[0]);
}

TEST(Sema, UndefinedVariable) {
  Checked c("kernel k() { int x = y; }");
  EXPECT_FALSE(c.ok);
  EXPECT_EQ(c.firstError(), "undefined variable 'y'");
  EXPECT_EQ(c.firstLoc().col, 22u);
}

TEST(Sema, Redeclaration) {
  Checked c("kernel k() { int x = 1; float x = 2.0; }");
  EXPECT_EQ(c.firstError(), "redeclaration of 'x' in the same scope");
}

TEST(Sema, RedeclaringParamInBodyIsError) {
  Checked c("kernel k(int n) { int n = 1; }");
  EXPECT_EQ(c.firstError(), "redeclaration of 'n' in the same scope");
}

TEST(Sema, ShadowingAllowed) {
  Checked c("kernel k(int n) { if (n < 1) { float n = 2.0; } }");
  EXPECT_TRUE(c.ok) << c.firstError();
}

TEST(Sema, ScopeEndsAtBlock) {
  Checked c("kernel k(int n) { if (n < 1) { int t = 2; } n = t; }");
  EXPECT_EQ(c.firstError(), "undefined variable 't'");
}

TEST(Sema, MixedOperands) {
  Checked c("kernel k(int n) { float f = 1.0; f = f + n; }");
  EXPECT_EQ(c.firstError(), "invalid operands to '+': float and int");
}

TEST(Sema, MixedComparison) {
  Checked c("kernel k(int n) { if (n < 1.0) { } }");
  EXPECT_EQ(c.firstError(), "invalid operands to '<': int and float");
}

TEST(Sema, InitializerTypeMismatch) {
  Checked c("kernel k() { int x = 1.5; }");
  EXPECT_EQ(c.firstError(), "cannot initialize 'x' of type int with a value of type float");
}

TEST(Sema, AssignTypeMismatch) {
  Checked c("kernel k(int x) { x = 0.5; }");
  EXPECT_EQ(c.firstError(), "cannot assign a value of type float to 'x' of type int");
}

TEST(Sema, IndexNonPointer) {
  Checked c("kernel k(int x) { float y = x[0]; }");
  EXPECT_EQ(c.firstError(), "cannot index non-pointer 'x'");
}

TEST(Sema, IndexMustBeInt) {
  Checked c("kernel k(float* a) { float y = a[1.0]; }");
  EXPECT_EQ(c.firstError(), "index must be int, got float");
}

TEST(Sema, ComparisonAsValue) {
  Checked c("kernel k(int x) { x = x < 1; }");
  EXPECT_EQ(c.firstError(), "comparison result can only be used as an if condition");
}

TEST(Sema, IfConditionMustBeComparison) {
  Checked c("kernel k(int x) { if (x) { } }");
  EXPECT_EQ(c.firstError(), "if condition must be a comparison (e.g. 'i < n')");
}

TEST(Sema, AssignToPointerParam) {
  Checked c("kernel k(float* a, float* b) { a = b; }");
  EXPECT_EQ(c.firstError(), "cannot assign to pointer parameter 'a'");
}

TEST(Sema, StoreValueMustBeFloat) {
  Checked c("kernel k(float* a) { a[0] = 1; }");
  EXPECT_EQ(c.firstError(), "stored value must be float, got int");
}

TEST(Sema, PointerAsValue) {
  Checked c("kernel k(float* a) { float x = a; }");
  EXPECT_EQ(c.firstError(), "cannot use pointer 'a' as a value (index it, e.g. 'a[i]')");
}

TEST(Sema, DuplicateKernel) {
  Checked c("kernel k() { }\nkernel k() { }");
  EXPECT_EQ(c.firstError(), "duplicate kernel name 'k'");
  EXPECT_EQ(c.firstLoc().line, 2u);
}

TEST(Sema, NoCascadeAfterError) {
  Checked c("kernel k() { float f = y + 1.0; }");
  EXPECT_EQ(c.diags.errorCount(), 1u);
}

TEST(Sema, NegRequiresNumber) {
  Checked c("kernel k(float* a) { float f = -a; }");
  EXPECT_EQ(c.firstError(), "cannot use pointer 'a' as a value (index it, e.g. 'a[i]')");
}

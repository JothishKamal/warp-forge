#include "helpers.hpp"
#include "passes/pass_manager.hpp"

#include <sstream>

using namespace wf;
using wf::test::compileToIR;

namespace {

// Compiles `source`, runs one pass on its only kernel, and returns the IR.
std::string afterPass(const std::string& source, const char* pass, bool* changed = nullptr) {
  auto m = compileToIR(source);
  auto p = passes::createPass(pass);
  EXPECT_NE(p, nullptr);
  bool c = p->run(*m->functions[0]);
  if (changed) *changed = c;
  for (const auto& e : ir::verifyModule(*m)) ADD_FAILURE() << "verifier after " << pass << ": " << e;
  return ir::printModule(*m);
}

} // namespace

// ------------------------------------------------------------------ const-fold

TEST(ConstFold, IntArith) {
  bool changed = false;
  EXPECT_EQ(afterPass("kernel k(float* o) { int x = 2 + 3 * 4; o[x] = 1.0; }", "const-fold", &changed),
            "kernel @k(ptr %o) {\n"
            "entry:\n"
            "  %0 = addr %o, 14\n"
            "  store.f32 0x3F800000, %0\n"
            "  ret\n"
            "}\n");
  EXPECT_TRUE(changed);
}

TEST(ConstFold, IntIdentities) {
  EXPECT_EQ(afterPass("kernel k(float* o, int n) {\n"
                      "  int a = n * 0; int b = n + 0; int c = n - n; int d = 1 * n;\n"
                      "  o[a + b + c + d] = 1.0;\n"
                      "}\n",
                      "const-fold"),
            "kernel @k(ptr %o, i32 %n) {\n"
            "entry:\n"
            "  %0 = add.i32 %n, %n\n"
            "  %1 = addr %o, %0\n"
            "  store.f32 0x3F800000, %1\n"
            "  ret\n"
            "}\n");
}

TEST(ConstFold, FloatFoldsExactly) {
  std::string ir = afterPass("kernel k(float* o) { o[0] = 0.1 + 0.2; o[1] = -0.0 + 0.0; }", "const-fold");
  EXPECT_NE(ir.find("store.f32 0x3E99999A"), std::string::npos) << ir;
  EXPECT_NE(ir.find("store.f32 0x00000000"), std::string::npos) << ir;
}

TEST(ConstFold, NoFloatIdentities) {
  // x + 0.0 is not x (x = -0.0), x * 0.0 is not 0.0 (NaN, inf, -0.0): untouched.
  const char* src = "kernel k(float* o) { float x = o[0]; o[1] = x + 0.0; o[2] = x * 0.0; o[3] = x * 1.0; }";
  bool changed = true;
  std::string before = test::irFor(src);
  EXPECT_EQ(afterPass(src, "const-fold", &changed), before);
  EXPECT_FALSE(changed);
}

TEST(ConstFold, ComparisonFoldsToI1) {
  std::string ir = afterPass("kernel k(float* o) { if (1 < 2) { o[0] = 1.0; } }", "const-fold");
  EXPECT_NE(ir.find("condbr true, if.then, if.end"), std::string::npos) << ir;
}

TEST(ConstFold, ReportsUnchanged) {
  bool changed = true;
  afterPass("kernel v(float* a, float* b, float* c, int n) {\n"
            "  int i = global_id(); if (i < n) { c[i] = a[i] + b[i]; } }",
            "const-fold", &changed);
  EXPECT_FALSE(changed);
}

// ---------------------------------------------------------------- pass manager

namespace {

class BreakIt final : public passes::Pass {
public:
  const char* name() const override { return "break-it"; }
  bool run(ir::Function& fn) override {
    fn.createBlock("orphan"); // no terminator: invalid
    return true;
  }
};

} // namespace

TEST(PassManager, VerifierCatchesBrokenPass) {
  auto m = compileToIR("kernel k() { }");
  std::vector<std::unique_ptr<passes::Pass>> ps;
  ps.push_back(std::make_unique<BreakIt>());
  std::string error;
  EXPECT_FALSE(passes::runPasses(*m, ps, {}, error));
  EXPECT_NE(error.find("IR verification failed after pass 'break-it' on @k"), std::string::npos) << error;
  EXPECT_NE(error.find("block 'orphan' does not end in a terminator"), std::string::npos) << error;
}

TEST(PassManager, UnknownPass) {
  auto m = compileToIR("kernel k() { }");
  passes::PipelineOptions opts;
  opts.passes = {"const-fold", "licm"};
  std::string error;
  EXPECT_FALSE(passes::runPipeline(*m, opts, error));
  EXPECT_EQ(error, "unknown pass 'licm' (available: const-fold, sccp, dce, simplify-cfg)");
}

TEST(PassManager, PrintAfterAll) {
  auto m = compileToIR("kernel k(float* o) { o[0] = 1.0 + 1.0; }");
  std::ostringstream log;
  passes::PipelineOptions opts;
  opts.passes = {"const-fold"};
  opts.printAfter = true;
  opts.printBefore = true;
  opts.log = &log;
  std::string error;
  ASSERT_TRUE(passes::runPipeline(*m, opts, error)) << error;
  std::string s = log.str();
  EXPECT_NE(s.find("; *** IR before const-fold on @k ***\n"), std::string::npos) << s;
  EXPECT_NE(s.find("; *** IR after const-fold on @k (changed) ***\n"), std::string::npos) << s;
  EXPECT_LT(s.find("before"), s.find("after"));
}

TEST(PassManager, DefaultPipeline) {
  EXPECT_EQ(passes::defaultPipeline(),
            (std::vector<std::string>{"const-fold", "sccp", "dce", "simplify-cfg"}));
}

#include "helpers.hpp"
#include "passes/pass_manager.hpp"

using namespace wf;
using wf::test::compileToIR;

namespace {

// Runs the named passes (once each, in order) through the pass manager, which
// also verifies the IR after each one.
std::string afterPasses(const std::string& source, std::vector<std::string> names,
                        bool* changed = nullptr) {
  auto m = compileToIR(source);
  if (changed) {
    // Report whether the single named pass changed anything.
    *changed = passes::createPass(names.at(0))->run(*m->functions[0]);
    for (const auto& e : ir::verifyModule(*m)) ADD_FAILURE() << "verifier: " << e;
    return ir::printModule(*m);
  }
  passes::PipelineOptions opts;
  opts.passes = std::move(names);
  std::string error;
  EXPECT_TRUE(passes::runPipeline(*m, opts, error)) << error;
  return ir::printModule(*m);
}

} // namespace

// ------------------------------------------------------------------------- dce

TEST(DCE, KeepsStores) {
  bool changed = true;
  afterPasses("kernel v(float* a, float* b, float* c, int n) {\n"
              "  int i = global_id(); if (i < n) { c[i] = a[i] + b[i]; } }",
              {"dce"}, &changed);
  EXPECT_FALSE(changed);
}

TEST(DCE, RemovesDeadChain) {
  EXPECT_EQ(afterPasses("kernel k(float* o) { int t = global_id() * 2; int u = t + 1; o[0] = 1.0; }",
                        {"dce"}),
            "kernel @k(ptr %o) {\n"
            "entry:\n"
            "  %0 = addr %o, 0\n"
            "  store.f32 0x3F800000, %0\n"
            "  ret\n"
            "}\n");
}

TEST(DCE, RemovesUnusedLoad) {
  std::string ir = afterPasses("kernel k(float* o) { float x = o[1]; o[0] = 1.0; }", {"dce"});
  EXPECT_EQ(ir.find("load"), std::string::npos) << ir;
}

TEST(DCE, RemovesDeadPhi) {
  std::string ir = afterPasses("kernel k(float* o, int n) {\n"
                               "  float x = 1.0; if (n < 0) { x = 2.0; } float y = x * 2.0; o[0] = 3.0; }",
                               {"dce"});
  // Reading x creates the phi (Braun's construction is on demand); y is dead,
  // so both the mul and the phi go.
  EXPECT_EQ(ir.find("phi"), std::string::npos) << ir;
  EXPECT_EQ(ir.find("mul"), std::string::npos) << ir;
}

// ---------------------------------------------------------------- simplify-cfg

TEST(SimplifyCFG, RemovesUnreachableAndMerges) {
  EXPECT_EQ(afterPasses("kernel k(float* o) {\n"
                        "  if (1 < 2) { o[0] = 1.0; } else { o[0] = 2.0; }\n"
                        "}\n",
                        {"sccp", "simplify-cfg"}),
            "kernel @k(ptr %o) {\n"
            "entry:\n"
            "  %0 = addr %o, 0\n"
            "  store.f32 0x3F800000, %0\n"
            "  ret\n"
            "}\n");
}

TEST(SimplifyCFG, FixesPhisOfRemovedPredecessor) {
  // After SCCP the `x = 5.0` arm is unreachable; deleting it must also drop
  // its phi entry (the pass manager verifies the result).
  std::string ir = afterPasses("kernel k(float* o, int n) {\n"
                               "  float x = 1.0;\n"
                               "  if (2 < 1) { x = 5.0; }\n"
                               "  if (n < 0) { o[0] = x; }\n"
                               "}\n",
                               {"sccp", "simplify-cfg"});
  EXPECT_EQ(ir.find("0x40A00000"), std::string::npos) << ir; // 5.0 is gone
}

TEST(SimplifyCFG, KeepsRealDiamond) {
  bool changed = true;
  afterPasses("kernel k(float* o, int n) {\n"
              "  int i = global_id(); float x = 1.0;\n"
              "  if (i < n) { x = 2.0; } else { x = 3.0; }\n"
              "  o[i] = x; }",
              {"simplify-cfg"}, &changed);
  EXPECT_FALSE(changed);
}

TEST(SimplifyCFG, EmptyThenArmIsMergedAway) {
  // `if (i < n) {}` leaves a condbr whose two paths both reach if.end.
  std::string ir = afterPasses("kernel k(float* o, int n) {\n"
                               "  int i = global_id(); if (i < n) { } o[i] = 1.0; }",
                               {"simplify-cfg"});
  EXPECT_EQ(ir.find("condbr"), std::string::npos) << ir;
  EXPECT_EQ(ir.find("if."), std::string::npos) << ir;
}

// -------------------------------------------------------------------- pipeline

TEST(Pipeline, ReachesFixpointOnSaxpy) {
  auto m = compileToIR("kernel k(float* x, float* y, int n) {\n"
                       "  int i = global_id(); float s = 2.0 * 1.5; int lanes = 32 * 8;\n"
                       "  if (lanes < 0) { y[i] = 0.0; }\n"
                       "  if (i < n) { y[i] = s * x[i] + y[i]; } }");
  passes::PipelineOptions opts;
  opts.passes = passes::defaultPipeline();
  opts.fixpoint = true;
  std::string error;
  ASSERT_TRUE(passes::runPipeline(*m, opts, error)) << error;
  // Running each pass again must change nothing.
  for (const std::string& name : passes::defaultPipeline())
    EXPECT_FALSE(passes::createPass(name)->run(*m->functions[0])) << name;
  std::string ir = ir::printModule(*m);
  EXPECT_NE(ir.find("mul.f32 0x40400000"), std::string::npos) << ir; // scale folded to 3.0
  EXPECT_EQ(ir.find("0x00000000"), std::string::npos) << ir;        // dead store removed
}

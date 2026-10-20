#include "helpers.hpp"
#include "ir/ir_parser.hpp"
#include "passes/pass_manager.hpp"

#include <filesystem>
#include <fstream>

using namespace wf;

namespace {

std::string roundTrip(const std::string& text) {
  std::string error;
  auto m = ir::parseIR(text, error);
  EXPECT_NE(m, nullptr) << error;
  if (!m) return "";
  for (const auto& e : ir::verifyModule(*m)) ADD_FAILURE() << "verifier: " << e;
  return ir::printModule(*m);
}

std::string parseError(const std::string& text) {
  std::string error;
  auto m = ir::parseIR(text, error);
  EXPECT_EQ(m, nullptr);
  return error;
}

} // namespace

TEST(IRParser, VectorAddRoundTrip) {
  std::string text = test::irFor("kernel vector_add(float* a, float* b, float* c, int n) {\n"
                                 "  int i = global_id(); if (i < n) { c[i] = a[i] + b[i]; } }");
  EXPECT_EQ(roundTrip(text), text);
}

TEST(IRParser, RoundTripsEveryExampleAtO0AndO1) {
  std::filesystem::path dir = std::filesystem::path(WARPFORGE_SOURCE_DIR) / "examples";
  int n = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".wf") continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::string src{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    auto m = test::compileToIR(src);
    std::string o0 = ir::printModule(*m);
    EXPECT_EQ(roundTrip(o0), o0) << entry.path();
    passes::PipelineOptions opts;
    opts.passes = passes::defaultPipeline();
    opts.fixpoint = true;
    std::string error;
    ASSERT_TRUE(passes::runPipeline(*m, opts, error)) << error;
    std::string o1 = ir::printModule(*m);
    EXPECT_EQ(roundTrip(o1), o1) << entry.path();
    ++n;
  }
  EXPECT_GE(n, 6);
}

TEST(IRParser, ForwardReferencesAndComments) {
  // %y is used in `then` before its definition appears in the text.
  const std::string text = "; a comment\n"
                           "kernel @k(i32 %n) {\n"
                           "entry:\n"
                           "  %0 = lt.i32 %n, 0\n"
                           "  condbr %0, later, then\n"
                           "\n"
                           "then:\n"
                           "  %x = phi.i32 [%y, later]\n"
                           "  ret\n"
                           "\n"
                           "later:\n"
                           "  %y = add.i32 %n, 1\n"
                           "  br then\n"
                           "}\n";
  std::string error;
  auto m = ir::parseIR(text, error);
  ASSERT_NE(m, nullptr) << error;
  ir::Function& fn = *m->functions[0];
  ir::BasicBlock* then = fn.findBlock("then");
  ir::BasicBlock* later = fn.findBlock("later");
  ASSERT_TRUE(then && later);
  ir::Instruction* phi = then->phis().at(0);
  EXPECT_EQ(phi->incomingFor(later), &*later->insts().front());
}

TEST(IRParser, ConstantsOfEveryType) {
  const std::string text = "kernel @k(ptr %o) {\n"
                           "entry:\n"
                           "  %0 = add.i32 -3, 2147483647\n"
                           "  %1 = mul.f32 0x80000000, 0x7F800000\n"
                           "  condbr true, a, a\n"
                           "\n"
                           "a:\n"
                           "  %2 = addr %o, %0\n"
                           "  store.f32 %1, %2\n"
                           "  ret\n"
                           "}\n";
  EXPECT_EQ(roundTrip(text), text);
}

TEST(IRParser, Errors) {
  EXPECT_EQ(parseError("kernel @k() {\nentry:\n  %0 = frob.i32 1, 2\n  ret\n}\n"),
            "3:8: unknown instruction 'frob.i32'");
  EXPECT_EQ(parseError("kernel @k() {\nentry:\n  %0 = add.i32 %nope, 2\n  ret\n}\n"),
            "3:16: undefined value '%nope'");
  EXPECT_EQ(parseError("kernel @k() {\nentry:\n  br nowhere\n}\n"), "3:6: undefined block 'nowhere'");
  EXPECT_EQ(parseError("kernel @k() {\nentry:\n  %0 = global_id\n  %0 = global_id\n  ret\n}\n"),
            "4:3: redefinition of '%0'");
  EXPECT_EQ(parseError("kernel @k() {\n  ret\n}\n"), "2:3: instruction outside a block (missing label?)");
  EXPECT_EQ(parseError("kernel @k() {\nentry:\n  ret\n"), "4:1: expected '}' to close kernel @k");
}

TEST(IRParser, LeavesSemanticErrorsToTheVerifier) {
  // Syntactically fine, but %x is used before it's defined: parse succeeds,
  // the verifier reports it.
  std::string error;
  auto m = ir::parseIR("kernel @k() {\nentry:\n  %0 = add.i32 %x, 1\n  %x = global_id\n  ret\n}\n", error);
  ASSERT_NE(m, nullptr) << error;
  auto errs = ir::verifyModule(*m);
  ASSERT_FALSE(errs.empty());
  EXPECT_NE(errs[0].find("does not dominate its use"), std::string::npos) << errs[0];
}

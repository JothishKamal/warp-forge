// Random differential testing of the optimizer on the reference interpreter.
//
// Each generated program (tests/support/program_generator.hpp) must compile,
// pass the verifier after every pass, produce PTX, and produce bit-identical
// output at -O0 and -O1 on the interpreter. A failure prints the seed and the
// program so it can be reproduced.
//
// The interpreter shares its float evaluator with the constant folder, so
// this test cannot see a host-vs-GPU semantic difference (e.g. NaN bits).
// tests/run_gpu_fuzz.py runs the same generator against ptxas and the GPU.

#include "codegen/ptx_emitter.hpp"
#include "helpers.hpp"
#include "ir/interpreter.hpp"
#include "passes/pass_manager.hpp"
#include "support/program_generator.hpp"

#include <bit>
#include <random>

using namespace wf;
using wf::test::ProgramGenerator;

namespace {

constexpr uint32_t kThreads = 64;

std::vector<float> runInterp(ir::Function& fn, uint32_t seed, bool& ok, std::string& error) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-8.0f, 8.0f);
  std::vector<float> in(kThreads), out(kThreads, 0.0f);
  for (float& x : in) x = dist(rng);
  ir::InterpResult r = ir::interpret(fn, {&in, &out, static_cast<int32_t>(kThreads - 5)}, kThreads);
  ok = r.ok;
  error = r.error;
  return out;
}

} // namespace

TEST(RandomPrograms, OptimizerPreservesSemantics) {
  constexpr uint32_t kPrograms = 500;
  for (uint32_t seed = 1; seed <= kPrograms; ++seed) {
    std::string src = ProgramGenerator(seed).generate();
    SCOPED_TRACE("seed " + std::to_string(seed) + ":\n" + src);

    auto o0 = test::compileToIR(src);
    ASSERT_EQ(o0->functions.size(), 1u);
    auto o1 = test::compileToIR(src);
    passes::PipelineOptions opts;
    opts.passes = passes::defaultPipeline();
    opts.fixpoint = true;
    std::string error;
    ASSERT_TRUE(passes::runPipeline(*o1, opts, error)) << error;

    bool ok0 = false, ok1 = false;
    std::string e0, e1;
    std::vector<float> r0 = runInterp(*o0->functions[0], seed, ok0, e0);
    std::vector<float> r1 = runInterp(*o1->functions[0], seed, ok1, e1);
    ASSERT_TRUE(ok0) << e0;
    ASSERT_TRUE(ok1) << e1;
    for (uint32_t t = 0; t < kThreads; ++t)
      ASSERT_EQ(std::bit_cast<uint32_t>(r0[t]), std::bit_cast<uint32_t>(r1[t])) << "thread " << t;

    // The backend must accept both forms too.
    EXPECT_FALSE(codegen::emitPTX(*o0).empty());
    EXPECT_FALSE(codegen::emitPTX(*o1).empty());
  }
}

TEST(RandomPrograms, GeneratorIsDeterministic) {
  EXPECT_EQ(ProgramGenerator(42).generate(), ProgramGenerator(42).generate());
  EXPECT_NE(ProgramGenerator(42).generate(), ProgramGenerator(43).generate());
}

TEST(RandomPrograms, GeneratorCoversEarlyReturn) {
  int withReturn = 0;
  for (uint32_t seed = 1; seed <= 500; ++seed)
    if (ProgramGenerator(seed).generate().find("return;") != std::string::npos) ++withReturn;
  EXPECT_GT(withReturn, 25) << "too few programs exercise `return;`";
}

// Differential testing of the optimizer: every example kernel is run by the
// reference interpreter at -O0 and at -O1 on the same inputs, and the output
// buffers must be bit-identical. This is the operational definition of
// "the optimizations preserve semantics" (docs/passes.md).

#include "helpers.hpp"
#include "ir/interpreter.hpp"
#include "passes/pass_manager.hpp"

#include <bit>
#include <filesystem>
#include <fstream>
#include <random>

using namespace wf;

namespace {

constexpr uint32_t kN = 1000;

std::string readFile(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// One buffer per pointer parameter, seeded so runs are reproducible.
std::vector<std::vector<float>> makeBuffers(const ir::Function& fn, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-4.0f, 4.0f);
  std::vector<std::vector<float>> bufs;
  for (const auto& a : fn.args()) {
    if (a->type() != ir::Type::Ptr) continue;
    std::vector<float> b(kN);
    for (float& x : b) x = dist(rng);
    bufs.push_back(std::move(b));
  }
  return bufs;
}

std::vector<ir::KernelArg> makeArgs(const ir::Function& fn, std::vector<std::vector<float>>& bufs) {
  std::vector<ir::KernelArg> args;
  size_t next = 0;
  for (const auto& a : fn.args()) {
    switch (a->type()) {
    case ir::Type::Ptr: args.emplace_back(&bufs[next++]); break;
    case ir::Type::I32: args.emplace_back(static_cast<int32_t>(kN - 37)); break; // exercise the bound
    case ir::Type::F32: args.emplace_back(1.5f); break;
    default: break;
    }
  }
  return args;
}

void expectBitIdentical(const std::vector<std::vector<float>>& a,
                        const std::vector<std::vector<float>>& b, const std::string& what) {
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i)
    for (size_t j = 0; j < a[i].size(); ++j)
      ASSERT_EQ(std::bit_cast<uint32_t>(a[i][j]), std::bit_cast<uint32_t>(b[i][j]))
          << what << ": buffer " << i << " element " << j;
}

} // namespace

TEST(Differential, ExamplesO0vsO1) {
  std::filesystem::path dir = std::filesystem::path(WARPFORGE_SOURCE_DIR) / "examples";
  int checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".wf") continue;
    std::string name = entry.path().filename().string();
    std::string src = readFile(entry.path());

    auto o0 = test::compileToIR(src);
    auto o1 = test::compileToIR(src);
    passes::PipelineOptions opts;
    opts.passes = passes::defaultPipeline();
    opts.fixpoint = true;
    std::string error;
    ASSERT_TRUE(passes::runPipeline(*o1, opts, error)) << name << ": " << error;

    for (size_t k = 0; k < o0->functions.size(); ++k) {
      for (uint32_t seed : {1u, 2u, 3u}) {
        auto bufs0 = makeBuffers(*o0->functions[k], seed);
        auto bufs1 = bufs0;
        auto args0 = makeArgs(*o0->functions[k], bufs0);
        auto args1 = makeArgs(*o1->functions[k], bufs1);
        auto r0 = ir::interpret(*o0->functions[k], args0, kN);
        auto r1 = ir::interpret(*o1->functions[k], args1, kN);
        ASSERT_TRUE(r0.ok) << name << " -O0: " << r0.error;
        ASSERT_TRUE(r1.ok) << name << " -O1: " << r1.error;
        expectBitIdentical(bufs0, bufs1, name + " seed " + std::to_string(seed));
      }
      ++checked;
    }
  }
  EXPECT_GE(checked, 6);
}

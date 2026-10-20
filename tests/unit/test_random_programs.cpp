// Random differential testing of the optimizer.
//
// A seeded generator writes well-typed WarpForge programs: nested if/else,
// mixed int and float arithmetic, shadowing, constant-heavy expressions
// (INT_MAX, -0.0, subnormals, values near overflow), always-true/false
// conditions and dead code. Every load and store indexes `[i]` under an
// `i < n` guard, so every program is well-defined by construction.
//
// Each program must compile, pass the verifier after every pass, produce
// PTX, and produce bit-identical output at -O0 and -O1 on the interpreter.
// A failure prints the seed and the program so it can be reproduced.

#include "codegen/ptx_emitter.hpp"
#include "helpers.hpp"
#include "ir/interpreter.hpp"
#include "passes/pass_manager.hpp"

#include <bit>
#include <random>

using namespace wf;

namespace {

class ProgramGenerator {
public:
  explicit ProgramGenerator(uint32_t seed) : rng_(seed) {}

  std::string generate() {
    out_ = "kernel fuzz(float* in, float* out, int n) {\n  int i = global_id();\n  if (i < n) {\n";
    scopes_.assign(1, {});
    declare("i", false);
    declare("n", false);
    int count = 2 + pick(6);
    for (int s = 0; s < count; ++s) statement(2, 2);
    out_ += "    out[i] = " + expr(true, 3) + ";\n  }\n}\n";
    return out_;
  }

private:
  struct Var {
    std::string name;
    bool isFloat;
  };

  int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng_); }
  bool chance(int percent) { return pick(100) < percent; }

  void declare(const std::string& name, bool isFloat) { scopes_.back().push_back({name, isFloat}); }

  // Variables of the given type that a name lookup would actually find:
  // innermost scope first, so a shadowed outer variable is not offered.
  std::vector<Var> visible(bool isFloat) const {
    std::vector<Var> vs;
    std::vector<std::string> seen;
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
      for (auto v = scope->rbegin(); v != scope->rend(); ++v) {
        if (std::find(seen.begin(), seen.end(), v->name) != seen.end()) continue;
        seen.push_back(v->name);
        if (v->isFloat == isFloat) vs.push_back(*v);
      }
    return vs;
  }

  std::string indent(int depth) const { return std::string(static_cast<size_t>(depth) * 2, ' '); }

  std::string literal(bool isFloat) {
    static const char* kInts[] = {"0", "1", "2", "3", "7", "65536", "2147483647", "1000"};
    static const char* kFloats[] = {"0.0", "1.0", "0.5", "2.0", "0.1", "3.0e38", "1.0e-45", "16777216.0"};
    return isFloat ? kFloats[pick(8)] : kInts[pick(8)];
  }

  std::string leaf(bool isFloat) {
    std::vector<Var> vs = visible(isFloat);
    int r = pick(10);
    if (isFloat && r < 3) return "in[i]";
    if (!vs.empty() && r < 7) return vs[static_cast<size_t>(pick(static_cast<int>(vs.size())))].name;
    return literal(isFloat);
  }

  std::string expr(bool isFloat, int depth) {
    if (depth == 0 || chance(30)) return leaf(isFloat);
    switch (pick(5)) {
    case 0: return "-(" + expr(isFloat, depth - 1) + ")";
    case 1: return "(" + expr(isFloat, depth - 1) + " + " + expr(isFloat, depth - 1) + ")";
    case 2: return "(" + expr(isFloat, depth - 1) + " - " + expr(isFloat, depth - 1) + ")";
    default: return "(" + expr(isFloat, depth - 1) + " * " + expr(isFloat, depth - 1) + ")";
    }
  }

  std::string condition() {
    bool isFloat = chance(50);
    return expr(isFloat, 2) + " < " + expr(isFloat, 2);
  }

  void statement(int depth, int nesting) {
    std::string ind = indent(depth);
    int r = pick(10);
    if (r < 3) { // declaration (may shadow an outer variable of the other type)
      bool isFloat = chance(50);
      std::string name = "v" + std::to_string(next_++);
      if (chance(15) && !visible(!isFloat).empty()) name = visible(!isFloat)[0].name; // shadowing
      if (name == "i" || name == "n") name = "v" + std::to_string(next_++);
      // Same-scope redeclaration is illegal; only shadow names from outer scopes.
      for (const Var& v : scopes_.back())
        if (v.name == name) name = "v" + std::to_string(next_++);
      out_ += ind + (isFloat ? "float " : "int ") + name + " = " + expr(isFloat, 3) + ";\n";
      declare(name, isFloat);
    } else if (r < 6) { // assignment
      bool isFloat = chance(50);
      std::vector<Var> vs = visible(isFloat);
      std::erase_if(vs, [](const Var& v) { return v.name == "i"; }); // keep the index intact
      if (vs.empty()) return statement(depth, nesting);
      const Var& v = vs[static_cast<size_t>(pick(static_cast<int>(vs.size())))];
      out_ += ind + v.name + " = " + expr(isFloat, 3) + ";\n";
    } else if (r < 8 && nesting > 0) { // if / else
      out_ += ind + "if (" + condition() + ") {\n";
      block(depth + 1, nesting - 1);
      if (chance(50)) {
        out_ += ind + "} else {\n";
        block(depth + 1, nesting - 1);
      }
      out_ += ind + "}\n";
    } else { // store (each thread only touches out[i])
      out_ += ind + "out[i] = " + expr(true, 3) + ";\n";
    }
  }

  void block(int depth, int nesting) {
    scopes_.emplace_back();
    int count = 1 + pick(3);
    for (int s = 0; s < count; ++s) statement(depth, nesting);
    scopes_.pop_back();
  }

  std::mt19937 rng_;
  std::string out_;
  std::vector<std::vector<Var>> scopes_;
  int next_ = 0;
};

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

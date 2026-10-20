#include "helpers.hpp"
#include "ir/const_eval.hpp"
#include "ir/interpreter.hpp"

#include <bit>
#include <cmath>
#include <limits>

using namespace wf::ir;
using wf::test::compileToIR;

namespace {

uint32_t bitsOf(Constant* c) { return c ? c->bits() : 0xDEADBEEFu; }

} // namespace

TEST(ConstEval, IntWrap) {
  Function fn("k");
  Constant* r = evaluate(fn, Op::Add, {fn.constI32(std::numeric_limits<int32_t>::max()), fn.constI32(1)});
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->i32(), std::numeric_limits<int32_t>::min());
  EXPECT_EQ(evaluate(fn, Op::Mul, {fn.constI32(65536), fn.constI32(65536)})->i32(), 0);
  EXPECT_EQ(evaluate(fn, Op::Sub, {fn.constI32(std::numeric_limits<int32_t>::min()), fn.constI32(1)})->i32(),
            std::numeric_limits<int32_t>::max());
  EXPECT_EQ(evaluate(fn, Op::Neg, {fn.constI32(std::numeric_limits<int32_t>::min())})->i32(),
            std::numeric_limits<int32_t>::min());
  EXPECT_EQ(evaluate(fn, Op::Lt, {fn.constI32(-1), fn.constI32(0)}), fn.constI1(true));
}

TEST(ConstEval, FloatEdgeCases) {
  Function fn("k");
  // 0.1f + 0.2f, rounded once to binary32.
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Add, {fn.constF32(0.1f), fn.constF32(0.2f)})), 0x3E99999Au);
  // -0.0 + 0.0 == +0.0 under round-to-nearest.
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Add, {fn.constF32(-0.0f), fn.constF32(0.0f)})), 0x00000000u);
  // neg is a sign flip, not 0 - x: neg(+0) = -0.
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Neg, {fn.constF32(0.0f)})), 0x80000000u);
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Sub, {fn.constF32(0.0f), fn.constF32(0.0f)})), 0x00000000u);
  // NaN results are the PTX canonical NaN 0x7FFFFFFF, as on the GPU
  // (x86 SSE would give its "default NaN" 0xFFC00000). NaN < x is false.
  float nan = std::numeric_limits<float>::quiet_NaN();
  float inf = std::numeric_limits<float>::infinity();
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Mul, {fn.constF32(nan), fn.constF32(2.0f)})), 0x7FFFFFFFu);
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Sub, {fn.constF32(inf), fn.constF32(inf)})), 0x7FFFFFFFu);
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Mul, {fn.constF32(0.0f), fn.constF32(inf)})), 0x7FFFFFFFu);
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Add, {fn.constF32(-inf), fn.constF32(inf)})), 0x7FFFFFFFu);
  // neg.f32 is a pure sign-bit flip on sm_86, NaN included (measured on an
  // RTX 3060: neg(0x7FFFFFFF) == 0xFFFFFFFF; see examples/float_consts.wf).
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Neg, {fn.constF32Bits(0x7FFFFFFFu)})), 0xFFFFFFFFu);
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Neg, {fn.constF32Bits(0xFFC00000u)})), 0x7FC00000u);
  EXPECT_EQ(evaluate(fn, Op::Lt, {fn.constF32(nan), fn.constF32(1.0f)}), fn.constI1(false));
  EXPECT_EQ(evaluate(fn, Op::Lt, {fn.constF32(1.0f), fn.constF32(nan)}), fn.constI1(false));
  // Subnormals are preserved (no flush-to-zero).
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Mul, {fn.constF32Bits(0x00000001u), fn.constF32(1.0f)})), 0x00000001u);
  // Overflow to +inf.
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Mul, {fn.constF32(3e38f), fn.constF32(10.0f)})), 0x7F800000u);
  // x * 0.0 is not 0.0 for x = -1 (it is -0.0).
  EXPECT_EQ(bitsOf(evaluate(fn, Op::Mul, {fn.constF32(-1.0f), fn.constF32(0.0f)})), 0x80000000u);
}

TEST(ConstEval, NotFoldable) {
  Function fn("k");
  EXPECT_EQ(evaluate(fn, Op::Load, {}), nullptr);
  EXPECT_EQ(evaluate(fn, Op::GlobalId, {}), nullptr);
}

TEST(Interp, VectorAdd) {
  auto m = compileToIR("kernel vector_add(float* a, float* b, float* c, int n) {\n"
                       "  int i = global_id();\n"
                       "  if (i < n) { c[i] = a[i] + b[i]; }\n"
                       "}\n");
  std::vector<float> a = {1, 2, 3, 4}, b = {10, 20, 30, 40}, c(4, 0.0f);
  // Grid larger than n: the bounds check must keep threads 4..7 out.
  InterpResult r = interpret(*m->functions[0], {&a, &b, &c, int32_t{4}}, 8);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(c, (std::vector<float>{11, 22, 33, 44}));
}

TEST(Interp, PhiAndNegation) {
  auto m = compileToIR("kernel k(float* o, int n) {\n"
                       "  int i = global_id();\n"
                       "  float v = 1.5;\n"
                       "  if (i < n) { v = -v; } else { v = v * 2.0; }\n"
                       "  o[i] = v;\n"
                       "}\n");
  std::vector<float> o(4, 0.0f);
  InterpResult r = interpret(*m->functions[0], {&o, int32_t{2}}, 4);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(o, (std::vector<float>{-1.5f, -1.5f, 3.0f, 3.0f}));
}

TEST(Interp, OOBDetected) {
  auto m = compileToIR("kernel k(float* o) { int i = global_id(); o[i] = 1.0; }");
  std::vector<float> o(2, 0.0f);
  InterpResult r = interpret(*m->functions[0], {&o}, 3);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "out-of-bounds store at index 2 of %o (size 2) in thread 2");
}

TEST(Interp, NegativeIndexDetected) {
  auto m = compileToIR("kernel k(float* o) { float x = o[-1]; o[0] = x; }");
  std::vector<float> o(2, 0.0f);
  InterpResult r = interpret(*m->functions[0], {&o}, 1);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "out-of-bounds load at index -1 of %o (size 2) in thread 0");
}

TEST(Interp, WriteWriteRaceDetected) {
  auto m = compileToIR("kernel k(float* o, float* a) { int i = global_id(); o[0] = a[i]; }");
  std::vector<float> o(1, 0.0f), a(2, 1.0f);
  InterpResult r = interpret(*m->functions[0], {&o, &a}, 2);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "data race on %o element 0 between threads 0 and 1");
}

TEST(Interp, ReadWriteRaceDetected) {
  // Thread 1 reads o[0] which thread 0 wrote.
  auto m = compileToIR("kernel k(float* o) {\n"
                       "  int i = global_id();\n"
                       "  if (i < 1) { o[0] = 2.0; } else { o[1] = o[0]; }\n"
                       "}\n");
  std::vector<float> o(2, 0.0f);
  InterpResult r = interpret(*m->functions[0], {&o}, 2);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "data race on %o element 0 between threads 0 and 1");
}

TEST(Interp, SameThreadReadWriteIsFine) {
  auto m = compileToIR("kernel k(float* o) { int i = global_id(); o[i] = o[i] + 1.0; }");
  std::vector<float> o = {1, 2};
  InterpResult r = interpret(*m->functions[0], {&o}, 2);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(o, (std::vector<float>{2, 3}));
}

TEST(Interp, ArgumentCountMismatch) {
  auto m = compileToIR("kernel k(float* o, int n) { }");
  std::vector<float> o(1);
  InterpResult r = interpret(*m->functions[0], {&o}, 1);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "kernel @k expects 2 arguments, got 1");
}

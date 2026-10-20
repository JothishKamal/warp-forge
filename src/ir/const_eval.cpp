#include "ir/const_eval.hpp"

#include <bit>

namespace wf::ir {

namespace {

// Integer arithmetic in uint32_t: unsigned overflow is defined (mod 2^32),
// which is exactly two's-complement wrap-around once reinterpreted.
uint32_t intOp(Op op, uint32_t a, uint32_t b) {
  switch (op) {
  case Op::Add: return a + b;
  case Op::Sub: return a - b;
  case Op::Mul: return a * b;
  case Op::Neg: return 0u - a;
  default: return 0;
  }
}

// PTX's canonical NaN. add/sub/mul.rn.f32 flush every NaN result to it (PTX
// ISA, "Floating-Point Instructions"), whereas x86 SSE produces its "default
// NaN" 0xFFC00000 and propagates input payloads. Matching the GPU therefore
// means canonicalizing here, not trusting the host's NaN bits.
constexpr uint32_t kCanonicalNaN = 0x7FFFFFFFu;

// One binary32 operation, returned as its bit pattern. With -ffp-contract=off
// (cmake/Warnings.cmake) and SSE arithmetic, add/sub/mul are each a single
// correctly rounded IEEE operation, the same as add.rn/sub.rn/mul.rn.f32.
uint32_t floatOp(Op op, uint32_t aBits, uint32_t bBits) {
  if (op == Op::Neg) {
    // A pure sign-bit flip, exactly like neg.f32 on sm_86, NaNs included
    // (measured: neg(0x7FFFFFFF) == 0xFFFFFFFF). Also why -(+0) == -0,
    // unlike 0 - (+0).
    return aBits ^ 0x80000000u;
  }
  float a = std::bit_cast<float>(aBits);
  float b = std::bit_cast<float>(bBits);
  float r = 0.0f;
  switch (op) {
  case Op::Add: r = a + b; break;
  case Op::Sub: r = a - b; break;
  case Op::Mul: r = a * b; break;
  default: break;
  }
  return r != r ? kCanonicalNaN : std::bit_cast<uint32_t>(r); // r != r: NaN
}

} // namespace

Constant* evaluate(Function& fn, Op op, const std::vector<Constant*>& ops) {
  switch (op) {
  case Op::Add: case Op::Sub: case Op::Mul: case Op::Neg: {
    size_t arity = op == Op::Neg ? 1 : 2;
    if (ops.size() != arity) return nullptr;
    Type t = ops[0]->type();
    uint32_t b = arity == 2 ? ops[1]->bits() : 0;
    if (t == Type::I32) return fn.constant(Type::I32, intOp(op, ops[0]->bits(), b));
    if (t == Type::F32) return fn.constF32Bits(floatOp(op, ops[0]->bits(), b));
    return nullptr;
  }
  case Op::Lt: {
    if (ops.size() != 2) return nullptr;
    if (ops[0]->type() == Type::I32) return fn.constI1(ops[0]->i32() < ops[1]->i32());
    if (ops[0]->type() == Type::F32) return fn.constI1(ops[0]->f32() < ops[1]->f32()); // false on NaN
    return nullptr;
  }
  default: return nullptr;
  }
}

} // namespace wf::ir

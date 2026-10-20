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

bool evaluateBits(Op op, Type operandType, uint32_t a, uint32_t b, uint32_t& result) {
  switch (op) {
  case Op::Add: case Op::Sub: case Op::Mul: case Op::Neg:
    if (operandType == Type::I32) result = intOp(op, a, b);
    else if (operandType == Type::F32) result = floatOp(op, a, b);
    else return false;
    return true;
  case Op::Lt:
    if (operandType == Type::I32) result = std::bit_cast<int32_t>(a) < std::bit_cast<int32_t>(b);
    else if (operandType == Type::F32) result = std::bit_cast<float>(a) < std::bit_cast<float>(b); // false on NaN
    else return false;
    return true;
  default: return false;
  }
}

Constant* evaluate(Function& fn, Op op, const std::vector<Constant*>& ops) {
  size_t arity = op == Op::Neg ? 1 : 2;
  if (ops.size() != arity) return nullptr;
  uint32_t result = 0;
  if (!evaluateBits(op, ops[0]->type(), ops[0]->bits(), arity == 2 ? ops[1]->bits() : 0, result)) return nullptr;
  return fn.constant(op == Op::Lt ? Type::I1 : ops[0]->type(), result);
}

} // namespace wf::ir

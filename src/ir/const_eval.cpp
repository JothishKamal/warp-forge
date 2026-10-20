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

// One binary32 operation. With -ffp-contract=off (cmake/Warnings.cmake) and
// SSE arithmetic, each of these is a single correctly rounded IEEE operation.
float floatOp(Op op, float a, float b) {
  switch (op) {
  case Op::Add: return a + b;
  case Op::Sub: return a - b;
  case Op::Mul: return a * b;
  case Op::Neg: return -a; // sign-bit flip: -(+0) == -0, unlike 0 - (+0)
  default: return 0.0f;
  }
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
    if (t == Type::F32)
      return fn.constF32(floatOp(op, ops[0]->f32(), std::bit_cast<float>(b)));
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

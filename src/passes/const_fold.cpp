// const-fold: local constant folding and integer algebraic identities.
//
// For each arithmetic/comparison instruction, in block order:
//   1. If every operand is a Constant, compute the result with the shared
//      evaluator (ir/const_eval) and replace the instruction with it.
//   2. Otherwise, for i32 only, apply identities that hold for every value
//      under wrap-around arithmetic:
//        x + 0, 0 + x, x - 0, x * 1, 1 * x   ->  x
//        x * 0, 0 * x, x - x                 ->  0
//
// Why no float identities: under IEEE-754 none of these are safe.
//   x + 0.0 -> x    wrong for x = -0.0  (-0.0 + 0.0 = +0.0)
//   x * 0.0 -> 0.0  wrong for NaN, +-inf (NaN) and negative x (-0.0)
//   x - x   -> 0.0  wrong for NaN and inf
// so floats are only folded when every operand is a known constant.
//
// The pass is local (one instruction at a time); propagating constants
// through phis and across branches is SCCP's job.

#include "passes/pass.hpp"

#include "ir/const_eval.hpp"

namespace wf::passes {

namespace {

using ir::Constant;
using ir::Instruction;
using ir::Op;
using ir::Value;

bool isFoldable(Op op) {
  return op == Op::Add || op == Op::Sub || op == Op::Mul || op == Op::Neg || op == Op::Lt;
}

bool isConstInt(const Value* v, int32_t k) {
  return v->valueKind() == Value::Kind::Constant && v->type() == ir::Type::I32 &&
         static_cast<const Constant*>(v)->i32() == k;
}

// Returns the value `inst` simplifies to, or nullptr.
Value* simplify(ir::Function& fn, Instruction& inst) {
  std::vector<Constant*> consts;
  for (Value* v : inst.operands()) {
    if (v->valueKind() != Value::Kind::Constant) break;
    consts.push_back(static_cast<Constant*>(v));
  }
  if (consts.size() == inst.operands().size()) return ir::evaluate(fn, inst.op(), consts);

  if (inst.type() != ir::Type::I32 || inst.operands().size() != 2) return nullptr;
  Value* a = inst.operand(0);
  Value* b = inst.operand(1);
  switch (inst.op()) {
  case Op::Add:
    if (isConstInt(b, 0)) return a;
    if (isConstInt(a, 0)) return b;
    break;
  case Op::Sub:
    if (isConstInt(b, 0)) return a;
    if (a == b) return fn.constI32(0);
    break;
  case Op::Mul:
    if (isConstInt(b, 1)) return a;
    if (isConstInt(a, 1)) return b;
    if (isConstInt(a, 0) || isConstInt(b, 0)) return fn.constI32(0);
    break;
  default: break;
  }
  return nullptr;
}

class ConstFold final : public Pass {
public:
  const char* name() const override { return "const-fold"; }

  bool run(ir::Function& fn) override {
    bool changed = false;
    for (auto& bb : fn.blocks()) {
      auto& insts = bb->insts();
      for (auto it = insts.begin(); it != insts.end();) {
        Instruction& inst = **it++; // advance first: `inst` may be erased
        if (!isFoldable(inst.op())) continue;
        if (Value* replacement = simplify(fn, inst)) {
          inst.replaceAllUsesWith(replacement);
          inst.eraseFromParent();
          changed = true;
        }
      }
    }
    return changed;
  }
};

} // namespace

std::unique_ptr<Pass> createConstFold() { return std::make_unique<ConstFold>(); }

} // namespace wf::passes

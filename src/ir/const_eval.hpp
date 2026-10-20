#pragma once

#include "ir/ir.hpp"

#include <vector>

namespace wf::ir {

// Evaluates `op` on constant operands with exactly WarpForge's runtime
// semantics (docs/language.md):
//   i32: two's-complement wrap-around (computed in uint32_t: no host UB)
//   f32: one IEEE-754 binary32 operation, round-to-nearest-even, computed in
//        host `float` (the build uses -ffp-contract=off so a*b+c is never
//        fused). This matches PTX add.rn/sub.rn/mul.rn.f32 bit-for-bit.
//   lt : signed for i32; ordered for f32 (false if either side is NaN), like
//        PTX setp.lt.
// Returns nullptr for ops that aren't foldable (loads, phis, ...).
//
// Shared by const-fold and SCCP so the two passes can never disagree.
Constant* evaluate(Function& fn, Op op, const std::vector<Constant*>& operands);

// The same computation on raw bit patterns, for callers that must not create
// IR constants (the interpreter). `b` is ignored for neg. The result of lt is
// 0 or 1. Returns false for ops that aren't foldable.
bool evaluateBits(Op op, Type operandType, uint32_t a, uint32_t b, uint32_t& result);

} // namespace wf::ir

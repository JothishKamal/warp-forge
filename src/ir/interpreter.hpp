#pragma once

#include "ir/ir.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace wf::ir {

// A kernel argument: an i32 or f32 scalar, or a float buffer (for ptr params).
using KernelArg = std::variant<int32_t, float, std::vector<float>*>;

struct InterpResult {
  bool ok = true;
  std::string error;
};

// Reference interpreter: runs `fn` once per thread for global ids
// 0 .. gridSize-1, sequentially, with the same semantics as the GPU code
// (shared evaluator with the constant folder). Buffers are modified in place.
//
// It also enforces the "well-defined execution" rule of docs/language.md and
// reports, instead of silently continuing:
//   * out-of-bounds loads/stores;
//   * cross-thread conflicts: two threads storing to the same element, or one
//     thread reading an element another thread stores.
InterpResult interpret(Function& fn, const std::vector<KernelArg>& args, uint32_t gridSize);

} // namespace wf::ir

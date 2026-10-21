// Reference interpreter for WarpForge IR.
//
// Threads run one after another; within a thread, instructions execute in
// block order following branches. Phis are evaluated together on block entry
// using the values from the edge being taken (parallel-copy semantics).
//
// Arithmetic goes through the same `evaluate` as the constant folder, so the
// interpreter, the optimizer and the GPU (.rn arithmetic) agree bit-for-bit.

#include "ir/interpreter.hpp"

#include "ir/const_eval.hpp"

#include <bit>
#include <unordered_map>

namespace wf::ir {

namespace {

// A pointer at runtime: which buffer argument, and an element offset.
struct RtPtr {
  unsigned buffer = 0;
  int64_t offset = 0;
};

struct RtValue {
  uint32_t bits = 0; // i1 / i32 / f32
  RtPtr ptr;         // ptr
};

constexpr int kNoThread = -1;
constexpr int kManyThreads = -2;

struct ElementState {
  int writer = kNoThread;
  int reader = kNoThread; // a single reader thread, or kManyThreads
};

class Interpreter {
public:
  Interpreter(Function& fn, const std::vector<KernelArg>& args) : fn_(fn), args_(args) {
    for (const KernelArg& a : args)
      if (auto* buf = std::get_if<std::vector<float>*>(&a))
        state_.emplace_back((*buf)->size());
      else
        state_.emplace_back();
  }

  InterpResult run(uint32_t gridSize) {
    if (args_.size() != fn_.args().size())
      return fail("kernel @" + fn_.name + " expects " + std::to_string(fn_.args().size()) +
                  " arguments, got " + std::to_string(args_.size()));
    for (size_t i = 0; i < args_.size(); ++i) {
      Type want = fn_.args()[i]->type();
      bool ok = (want == Type::I32 && std::holds_alternative<int32_t>(args_[i])) ||
                (want == Type::F32 && std::holds_alternative<float>(args_[i])) ||
                (want == Type::Ptr && std::holds_alternative<std::vector<float>*>(args_[i]));
      if (!ok) return fail("argument " + std::to_string(i) + " has the wrong kind");
    }
    for (uint32_t t = 0; t < gridSize; ++t)
      if (!thread(static_cast<int>(t))) return result_;
    return result_;
  }

private:
  InterpResult fail(std::string msg) {
    result_.ok = false;
    result_.error = std::move(msg);
    return result_;
  }

  std::string bufferName(unsigned buffer) const { return "%" + fn_.args()[buffer]->name; }

  RtValue get(const Value* v) {
    switch (v->valueKind()) {
    case Value::Kind::Constant: return {static_cast<const Constant*>(v)->bits(), {}};
    case Value::Kind::Argument: {
      unsigned i = static_cast<const Argument*>(v)->index();
      const KernelArg& a = args_[i];
      if (auto* x = std::get_if<int32_t>(&a)) return {std::bit_cast<uint32_t>(*x), {}};
      if (auto* f = std::get_if<float>(&a)) return {std::bit_cast<uint32_t>(*f), {}};
      return {0, {i, 0}};
    }
    case Value::Kind::Instruction: return env_.at(v);
    }
    return {};
  }

  // Bounds + race check for one access; returns false (and records the error).
  bool access(const RtPtr& p, bool isStore, int tid) {
    std::vector<float>& buf = *std::get<std::vector<float>*>(args_[p.buffer]);
    if (p.offset < 0 || p.offset >= static_cast<int64_t>(buf.size())) {
      fail(std::string("out-of-bounds ") + (isStore ? "store" : "load") + " at index " +
           std::to_string(p.offset) + " of " + bufferName(p.buffer) + " (size " +
           std::to_string(buf.size()) + ") in thread " + std::to_string(tid));
      return false;
    }
    ElementState& e = state_[p.buffer][static_cast<size_t>(p.offset)];
    auto race = [&](int other) {
      fail("data race on " + bufferName(p.buffer) + " element " + std::to_string(p.offset) +
           " between threads " + std::to_string(other) + " and " + std::to_string(tid));
      return false;
    };
    if (isStore) {
      if (e.writer != kNoThread && e.writer != tid) return race(e.writer);
      if (e.reader != kNoThread && e.reader != tid) return race(e.reader == kManyThreads ? 0 : e.reader);
      e.writer = tid;
    } else {
      if (e.writer != kNoThread && e.writer != tid) return race(e.writer);
      if (e.reader == kNoThread) e.reader = tid;
      else if (e.reader != tid) e.reader = kManyThreads;
    }
    return true;
  }

  bool thread(int tid) {
    env_.clear();
    BasicBlock* bb = fn_.entry();
    BasicBlock* from = nullptr;
    for (size_t steps = 0; steps < 1'000'000; ++steps) {
      // Phis read their inputs simultaneously, then all are written.
      std::vector<std::pair<const Instruction*, RtValue>> phiValues;
      for (Instruction* phi : bb->phis()) {
        Value* in = phi->incomingFor(from);
        if (!in) {
          fail("phi in '" + bb->name + "' has no value for edge from '" +
               (from ? from->name : std::string("<entry>")) + "'");
          return false;
        }
        phiValues.emplace_back(phi, get(in));
      }
      for (auto& [phi, val] : phiValues) env_[phi] = val;

      for (const auto& instPtr : bb->insts()) {
        const Instruction& inst = *instPtr;
        switch (inst.op()) {
        case Op::Phi: break;
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Neg: case Op::Lt: {
          // Raw bits, no IR constants: the interpreter must not mutate the IR.
          uint32_t a = get(inst.operand(0)).bits;
          uint32_t b = inst.operands().size() > 1 ? get(inst.operand(1)).bits : 0;
          uint32_t r = 0;
          evaluateBits(inst.op(), inst.operand(0)->type(), a, b, r);
          env_[&inst] = {r, {}};
          break;
        }
        case Op::GlobalId: env_[&inst] = {static_cast<uint32_t>(tid), {}}; break;
        case Op::Addr: {
          RtPtr p = get(inst.operand(0)).ptr;
          p.offset += std::bit_cast<int32_t>(get(inst.operand(1)).bits);
          env_[&inst] = {0, p};
          break;
        }
        case Op::Load: {
          RtPtr p = get(inst.operand(0)).ptr;
          if (!access(p, false, tid)) return false;
          float v = (*std::get<std::vector<float>*>(args_[p.buffer]))[static_cast<size_t>(p.offset)];
          env_[&inst] = {std::bit_cast<uint32_t>(v), {}};
          break;
        }
        case Op::Store: {
          RtPtr p = get(inst.operand(1)).ptr;
          if (!access(p, true, tid)) return false;
          (*std::get<std::vector<float>*>(args_[p.buffer]))[static_cast<size_t>(p.offset)] =
              std::bit_cast<float>(get(inst.operand(0)).bits);
          break;
        }
        case Op::Br:
          from = bb;
          bb = inst.blocks()[0];
          goto next_block;
        case Op::CondBr:
          from = bb;
          bb = inst.blocks()[get(inst.operand(0)).bits ? 0 : 1];
          goto next_block;
        case Op::Ret: return true;
        }
      }
      fail("block '" + bb->name + "' fell off the end without a terminator");
      return false;
    next_block:;
    }
    fail("step limit exceeded");
    return false;
  }

  Function& fn_;
  std::vector<KernelArg> args_;
  std::vector<std::vector<ElementState>> state_; // per argument, per element
  std::unordered_map<const Value*, RtValue> env_;
  InterpResult result_;
};

} // namespace

InterpResult interpret(Function& fn, const std::vector<KernelArg>& args, uint32_t gridSize) {
  return Interpreter(fn, args).run(gridSize);
}

} // namespace wf::ir

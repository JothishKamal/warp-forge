// PTX emitter: SSA IR -> PTX assembly text.
//
// PTX is a virtual ISA: it has an unlimited supply of typed virtual
// registers, and ptxas performs the real register allocation for the target
// GPU. So, like NVIDIA's own front ends, WarpForge does *no* register
// allocation: every SSA value gets its own virtual register, declared up front
// with `.reg .b32 %r<N>;`.
//
// Register classes:
//     i1  -> %p   (.pred)       i32 -> %r   (.b32)
//     f32 -> %f   (.f32)        ptr -> %rd  (.b64, a 64-bit global address)
//
// Instruction selection is a direct mapping (docs/codegen.md has the table).
// Points worth knowing:
//   * Pointer params arrive as generic addresses; `cvta.to.global.u64`
//     converts them once so loads/stores can use the `.global` state space.
//   * global_id() is  %ctaid.x * %ntid.x + %tid.x  in one `mad.lo.s32`.
//   * Float add/sub/mul carry an explicit `.rn` rounding modifier. Without
//     it, ptxas may contract a mul followed by an add into an FMA (one
//     rounding instead of two), and the GPU would no longer agree bit-for-bit
//     with the constant folder and the reference interpreter.
//   * Float immediates are written as exact bit patterns (0f3F800000).
//
// Out of SSA: critical edges are split first, then each phi becomes a copy at
// the end of every predecessor; the copies for one edge are sequentialized as
// a parallel copy (out_of_ssa.cpp).
//
// Layout: reverse post-order with the true successor placed first (see
// computeLayout). Branches to the next block are omitted, and a condbr whose true target is next is inverted
// (`@!%p bra false_target`). Labels follow nvcc's `$L__BB<kernel>_<block>`.

#include "codegen/ptx_emitter.hpp"

#include "codegen/out_of_ssa.hpp"
#include "ir/dominators.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace wf::codegen {

std::string defaultPtxVersion(const std::string& sm) {
  // Lowest PTX ISA version that introduced each target.
  static const std::pair<const char*, const char*> kTable[] = {
      {"sm_50", "4.0"}, {"sm_52", "4.1"}, {"sm_53", "4.2"}, {"sm_60", "5.0"}, {"sm_61", "5.0"},
      {"sm_62", "5.0"}, {"sm_70", "6.0"}, {"sm_72", "6.1"}, {"sm_75", "6.3"}, {"sm_80", "7.0"},
      {"sm_86", "7.1"}, {"sm_87", "7.4"}, {"sm_89", "7.8"}, {"sm_90", "7.8"}};
  for (const auto& [target, version] : kTable)
    if (sm == target) return version;
  return "7.8";
}

namespace {

using ir::BasicBlock;
using ir::Instruction;
using ir::Op;
using ir::Type;
using ir::Value;

enum RegClass { Pred, B32, F32, B64, kNumClasses };

RegClass classOf(Type t) {
  switch (t) {
  case Type::I1: return Pred;
  case Type::I32: return B32;
  case Type::F32: return F32;
  case Type::Ptr: return B64;
  case Type::Void: break;
  }
  assert(false && "no register class for void");
  return B32;
}

constexpr const char* kPrefix[kNumClasses] = {"%p", "%r", "%f", "%rd"};
constexpr const char* kRegDecl[kNumClasses] = {".pred", ".b32", ".f32", ".b64"};
constexpr const char* kMov[kNumClasses] = {"mov.pred", "mov.b32", "mov.f32", "mov.b64"};

std::string immediate(const ir::Constant* c) {
  if (c->type() == Type::F32) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0f%08X", c->bits());
    return buf;
  }
  if (c->type() == Type::I1) return c->i1() ? "1" : "0";
  return std::to_string(c->i32());
}

class FunctionEmitter {
public:
  FunctionEmitter(ir::Function& fn, unsigned index) : fn_(fn), index_(index) {}

  std::string emit() {
    splitCriticalEdges(fn_);
    computeLayout();

    std::ostringstream entry;
    entry << "\t// .globl\t" << fn_.name << "\n.visible .entry " << fn_.name << '(';
    for (size_t i = 0; i < fn_.args().size(); ++i) {
      const char* type = fn_.args()[i]->type() == Type::Ptr   ? ".u64"
                         : fn_.args()[i]->type() == Type::F32 ? ".f32"
                                                              : ".u32";
      entry << (i ? ",\n" : "\n") << "\t.param " << type << ' ' << paramName(i);
    }
    entry << (fn_.args().empty() ? ")\n" : "\n)\n") << "{\n";

    emitParamLoads();
    for (size_t i = 0; i < layout_.size(); ++i) emitBlock(i);

    for (int c = 0; c < kNumClasses; ++c)
      if (counts_[c] > 0)
        entry << "\t.reg " << kRegDecl[c] << " \t" << kPrefix[c] << '<' << counts_[c] + 1 << ">;\n";
    entry << '\n' << body_.str() << "\n}\n";
    return entry.str();
  }

private:
  // ------------------------------------------------------------ registers

  std::string newReg(RegClass c) {
    std::string r = kPrefix[c] + std::to_string(++counts_[c]);
    regClass_[r] = c;
    return r;
  }

  // The register holding `v` (assigned on first reference, so numbering
  // follows emission order and is deterministic).
  std::string reg(const Value* v) {
    assert(v->valueKind() != Value::Kind::Constant);
    auto it = regs_.find(v);
    if (it != regs_.end()) return it->second;
    return regs_[v] = newReg(classOf(v->type()));
  }

  // A source operand that may be an immediate.
  std::string operand(const Value* v) {
    if (v->valueKind() == Value::Kind::Constant) return immediate(static_cast<const ir::Constant*>(v));
    return reg(v);
  }

  // A source operand that must be a register: constants are moved into one.
  std::string regOperand(const Value* v) {
    if (v->valueKind() != Value::Kind::Constant) return reg(v);
    RegClass c = classOf(v->type());
    std::string r = newReg(c);
    inst(kMov[c], r + ", " + operand(v));
    return r;
  }

  // -------------------------------------------------------------- output

  void inst(const std::string& opcode, const std::string& operands) {
    body_ << '\t' << opcode << " \t" << operands << ";\n";
  }

  std::string paramName(size_t i) const { return fn_.name + "_param_" + std::to_string(i); }
  std::string label(const BasicBlock* bb) const {
    return "$L__BB" + std::to_string(index_) + "_" + std::to_string(layoutIndex_.at(bb));
  }
  const BasicBlock* next(size_t i) const { return i + 1 < layout_.size() ? layout_[i + 1] : nullptr; }

  // --------------------------------------------------------------- layout

  // Reverse post-order of a DFS that visits successors last-to-first. RPO is
  // a topological order of the (acyclic) CFG, so a join always comes after
  // both arms; visiting the false successor first makes the *true* successor
  // come right after its branch, so the then-arm falls through.
  void computeLayout() {
    std::vector<const BasicBlock*> post;
    std::unordered_set<const BasicBlock*> seen{fn_.entry()};
    std::vector<std::pair<const BasicBlock*, std::vector<BasicBlock*>>> stack;
    auto push = [&](const BasicBlock* bb) {
      std::vector<BasicBlock*> succs = bb->successors(); // popped from the back: true target last
      stack.emplace_back(bb, std::move(succs));
    };
    push(fn_.entry());
    while (!stack.empty()) {
      auto& [bb, succs] = stack.back();
      if (succs.empty()) {
        post.push_back(bb);
        stack.pop_back();
        continue;
      }
      // Visit the *last* successor first, so the first (true) one finishes
      // last and therefore lands first in reverse post-order.
      BasicBlock* s = succs.back();
      succs.pop_back();
      if (seen.insert(s).second) push(s);
    }
    for (auto it = post.rbegin(); it != post.rend(); ++it) {
      layoutIndex_[*it] = layout_.size();
      layout_.push_back(*it);
    }
    // A block needs a label iff some branch reaches it other than by falling
    // through. emitTerminator() makes the same decisions.
    for (size_t i = 0; i < layout_.size(); ++i) {
      const Instruction* t = layout_[i]->terminator();
      for (const BasicBlock* target : t->blocks())
        if (target != next(i)) labelled_.insert(target);
    }
  }

  // ------------------------------------------------------------- emission

  void emitParamLoads() {
    for (size_t i = 0; i < fn_.args().size(); ++i) {
      const ir::Argument* a = fn_.args()[i].get();
      std::string slot = "[" + paramName(i) + "]";
      switch (a->type()) {
      case Type::Ptr: {
        std::string generic = newReg(B64);
        inst("ld.param.u64", generic + ", " + slot);
        std::string global = newReg(B64);
        inst("cvta.to.global.u64", global + ", " + generic);
        regs_[a] = global;
        break;
      }
      case Type::I32: inst("ld.param.u32", reg(a) + ", " + slot); break;
      case Type::F32: inst("ld.param.f32", reg(a) + ", " + slot); break;
      default: assert(false);
      }
    }
  }

  void emitBlock(size_t i) {
    const BasicBlock* bb = layout_[i];
    if (labelled_.count(bb)) body_ << label(bb) << ":\n";
    for (const auto& ip : bb->insts()) {
      const Instruction& in = *ip;
      if (in.op() == Op::Phi) continue;
      if (in.isTerminator()) {
        emitPhiCopies(*bb);
        emitTerminator(in, i);
      } else {
        emitInstruction(in);
      }
    }
  }

  // For each successor, the phis at its top form one parallel copy on the
  // edge bb -> succ. After critical-edge splitting, a block whose successor
  // has phis either has a single successor, or that successor has a single
  // predecessor (so its phis' registers are only read below it).
  void emitPhiCopies(const BasicBlock& bb) {
    for (BasicBlock* succ : bb.successors()) {
      std::vector<Copy> copies;
      for (Instruction* phi : succ->phis())
        copies.push_back({reg(phi), operand(phi->incomingFor(&bb))});
      if (copies.empty()) continue;
      auto seq = sequentializeParallelCopy(
          std::move(copies), [&](const std::string& dst) { return newReg(regClass_.at(dst)); });
      for (const Copy& c : seq) inst(kMov[regClass_.at(c.dst)], c.dst + ", " + c.src);
    }
  }

  void emitTerminator(const Instruction& t, size_t i) {
    auto branch = [&](const BasicBlock* target) {
      if (target != next(i)) inst("bra.uni", label(target));
    };
    switch (t.op()) {
    case Op::Ret: body_ << "\tret;\n"; break;
    case Op::Br: branch(t.blocks()[0]); break;
    case Op::CondBr: {
      const BasicBlock* ifTrue = t.blocks()[0];
      const BasicBlock* ifFalse = t.blocks()[1];
      const Value* cond = t.operand(0);
      if (cond->valueKind() == Value::Kind::Constant) { // e.g. after a lone const-fold
        branch(static_cast<const ir::Constant*>(cond)->i1() ? ifTrue : ifFalse);
        break;
      }
      if (ifTrue == ifFalse) {
        branch(ifTrue);
      } else if (ifFalse == next(i)) {
        body_ << "\t@" << reg(cond) << " bra \t" << label(ifTrue) << ";\n";
      } else if (ifTrue == next(i)) {
        body_ << "\t@!" << reg(cond) << " bra \t" << label(ifFalse) << ";\n";
      } else {
        body_ << "\t@" << reg(cond) << " bra \t" << label(ifTrue) << ";\n";
        inst("bra.uni", label(ifFalse));
      }
      break;
    }
    default: assert(false);
    }
  }

  void emitInstruction(const Instruction& in) {
    const bool isFloat = in.op() != Op::Addr && !in.operands().empty() && in.operand(0)->type() == Type::F32;
    switch (in.op()) {
    case Op::Add: case Op::Sub: case Op::Mul: {
      static const char* kInt[] = {"add.s32", "sub.s32", "mul.lo.s32"};
      static const char* kFloat[] = {"add.rn.f32", "sub.rn.f32", "mul.rn.f32"};
      size_t k = in.op() == Op::Add ? 0 : in.op() == Op::Sub ? 1 : 2;
      const Value* lhs = in.operand(0);
      const Value* rhs = in.operand(1);
      // add and mul are commutative (exactly, even in IEEE-754), so put a
      // constant second where it can be an immediate instead of a mov.
      if (in.op() != Op::Sub && lhs->valueKind() == Value::Kind::Constant &&
          rhs->valueKind() != Value::Kind::Constant)
        std::swap(lhs, rhs);
      std::string a = regOperand(lhs);
      std::string b = operand(rhs);
      inst(isFloat ? kFloat[k] : kInt[k], reg(&in) + ", " + a + ", " + b);
      break;
    }
    case Op::Neg: {
      std::string a = regOperand(in.operand(0));
      if (isFloat) {
        // Not neg.f32: PTX leaves its result for NaN inputs unspecified, and
        // in practice the hardware returns the canonical NaN unchanged while
        // ptxas's constant folder flips the sign. XOR on the sign bit (.b32
        // works on .f32 registers) is exactly the IR's semantics.
        inst("xor.b32", reg(&in) + ", " + a + ", 0x80000000");
      } else {
        inst("neg.s32", reg(&in) + ", " + a);
      }
      break;
    }
    case Op::Lt: {
      std::string a = regOperand(in.operand(0));
      std::string b = operand(in.operand(1));
      inst(isFloat ? "setp.lt.f32" : "setp.lt.s32", reg(&in) + ", " + a + ", " + b);
      break;
    }
    case Op::GlobalId: {
      std::string ctaid = newReg(B32), ntid = newReg(B32), tid = newReg(B32);
      inst("mov.u32", ctaid + ", %ctaid.x");
      inst("mov.u32", ntid + ", %ntid.x");
      inst("mov.u32", tid + ", %tid.x");
      inst("mad.lo.s32", reg(&in) + ", " + ctaid + ", " + ntid + ", " + tid);
      break;
    }
    case Op::Addr: {
      std::string base = reg(in.operand(0));
      const Value* index = in.operand(1);
      if (index->valueKind() == Value::Kind::Constant) {
        int64_t offset = int64_t{static_cast<const ir::Constant*>(index)->i32()} * 4;
        if (offset == 0) {
          regs_[&in] = base; // same address: no instruction needed
        } else {
          inst("add.s64", reg(&in) + ", " + base + ", " + std::to_string(offset));
        }
        break;
      }
      std::string scaled = newReg(B64);
      inst("mul.wide.s32", scaled + ", " + reg(index) + ", 4"); // sign-extend, times sizeof(float)
      inst("add.s64", reg(&in) + ", " + base + ", " + scaled);
      break;
    }
    case Op::Load: inst("ld.global.f32", reg(&in) + ", [" + reg(in.operand(0)) + "]"); break;
    case Op::Store: {
      std::string value = regOperand(in.operand(0));
      inst("st.global.f32", "[" + reg(in.operand(1)) + "], " + value);
      break;
    }
    default: assert(false && "unexpected instruction");
    }
  }

  ir::Function& fn_;
  unsigned index_;
  std::ostringstream body_;
  int counts_[kNumClasses] = {};
  std::unordered_map<const Value*, std::string> regs_;
  std::unordered_map<std::string, RegClass> regClass_;
  std::vector<const BasicBlock*> layout_;
  std::unordered_map<const BasicBlock*, size_t> layoutIndex_;
  std::unordered_set<const BasicBlock*> labelled_;
};

} // namespace

std::string emitPTX(ir::Module& module, const PTXOptions& options) {
  std::string version = options.ptxVersion.empty() ? defaultPtxVersion(options.sm) : options.ptxVersion;
  std::string out = "//\n// Generated by WarpForge " WARPFORGE_VERSION "\n//\n\n";
  out += ".version " + version + "\n.target " + options.sm + "\n.address_size 64\n";
  for (size_t i = 0; i < module.functions.size(); ++i)
    out += "\n" + FunctionEmitter(*module.functions[i], static_cast<unsigned>(i)).emit();
  return out;
}

} // namespace wf::codegen

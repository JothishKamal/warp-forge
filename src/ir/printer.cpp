// IR printer.
//
// Names are assigned per function at print time:
//   * arguments and instructions with a name hint print as %hint, with
//     duplicates disambiguated as %hint.1, %hint.2 in order of appearance;
//   * other values print as %0, %1, ... in order of appearance.
// Constants print inline: i32 in decimal, f32 as its exact IEEE-754 bit
// pattern in hex (0x3F800000), i1 as true/false. Hex keeps the text exact:
// no decimal rounding can sneak in between print and parse.

#include "ir/printer.hpp"

#include <cstdio>
#include <map>
#include <sstream>
#include <unordered_map>

namespace wf::ir {

namespace {

class Namer {
public:
  explicit Namer(const Function& fn) {
    for (const auto& a : fn.args()) assign(a.get());
    for (const auto& bb : fn.blocks())
      for (const auto& inst : bb->insts())
        if (inst->type() != Type::Void) assign(inst.get());
  }

  std::string ref(const Value* v) const {
    if (v->valueKind() == Value::Kind::Constant) return constant(static_cast<const Constant*>(v));
    auto it = names_.find(v);
    return it != names_.end() ? it->second : "%<unnamed>";
  }

  static std::string constant(const Constant* c) {
    switch (c->type()) {
    case Type::I1: return c->i1() ? "true" : "false";
    case Type::I32: return std::to_string(c->i32());
    case Type::F32: {
      char buf[16];
      std::snprintf(buf, sizeof buf, "0x%08X", c->bits());
      return buf;
    }
    default: return "<const?>";
    }
  }

  void assign(const Value* v) {
    if (!v->name.empty()) {
      unsigned& n = hintCounts_[v->name];
      names_[v] = "%" + (n == 0 ? v->name : v->name + "." + std::to_string(n));
      ++n;
    } else {
      names_[v] = "%" + std::to_string(next_++);
    }
  }

private:
  std::unordered_map<const Value*, std::string> names_;
  std::map<std::string, unsigned> hintCounts_;
  unsigned next_ = 0;
};

void printInst(std::ostream& out, const Instruction& inst, const Namer& names) {
  if (inst.type() != Type::Void) out << names.ref(&inst) << " = ";
  out << opName(inst.op());

  switch (inst.op()) {
  case Op::Add: case Op::Sub: case Op::Mul: case Op::Neg: case Op::Lt:
    // Typed by operand type (lt's result is always i1).
    out << '.' << typeName(inst.operand(0)->type());
    break;
  case Op::Load: case Op::Store: out << ".f32"; break;
  case Op::Phi: out << '.' << typeName(inst.type()); break;
  default: break;
  }

  if (inst.op() == Op::Phi) {
    for (size_t i = 0; i < inst.operands().size(); ++i)
      out << (i ? ", [" : " [") << names.ref(inst.operand(i)) << ", " << inst.blocks()[i]->name
          << ']';
    return;
  }

  bool first = true;
  for (const Value* v : inst.operands()) {
    out << (first ? " " : ", ") << names.ref(v);
    first = false;
  }
  for (const BasicBlock* b : inst.blocks()) {
    out << (first ? " " : ", ") << b->name;
    first = false;
  }
}

} // namespace

std::string printFunction(const Function& fn) {
  Namer names(fn);
  std::ostringstream out;
  out << "kernel @" << fn.name << '(';
  for (size_t i = 0; i < fn.args().size(); ++i) {
    const Argument& a = *fn.args()[i];
    out << (i ? ", " : "") << typeName(a.type()) << ' ' << names.ref(&a);
  }
  out << ") {\n";
  bool firstBlock = true;
  for (const auto& bb : fn.blocks()) {
    if (!firstBlock) out << '\n';
    firstBlock = false;
    out << bb->name << ":\n";
    for (const auto& inst : bb->insts()) {
      out << "  ";
      printInst(out, *inst, names);
      out << '\n';
    }
  }
  out << "}\n";
  return out.str();
}

std::string printModule(const Module& module) {
  std::string out;
  for (size_t i = 0; i < module.functions.size(); ++i) {
    if (i) out += '\n';
    out += printFunction(*module.functions[i]);
  }
  return out;
}

std::string printInstruction(const Instruction& inst) {
  std::ostringstream out;
  if (inst.parent() && inst.parent()->parent()) {
    Namer names(*inst.parent()->parent());
    printInst(out, inst, names);
  } else {
    out << opName(inst.op());
  }
  return out.str();
}

} // namespace wf::ir

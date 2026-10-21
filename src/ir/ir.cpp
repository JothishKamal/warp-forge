#include "ir/ir.hpp"

#include <algorithm>
#include <bit>
#include <cassert>

namespace wf::ir {

const char* typeName(Type t) {
  switch (t) {
  case Type::Void: return "void";
  case Type::I1: return "i1";
  case Type::I32: return "i32";
  case Type::F32: return "f32";
  case Type::Ptr: return "ptr";
  }
  return "?";
}

const char* opName(Op op) {
  switch (op) {
  case Op::Add: return "add";
  case Op::Sub: return "sub";
  case Op::Mul: return "mul";
  case Op::Neg: return "neg";
  case Op::Lt: return "lt";
  case Op::GlobalId: return "global_id";
  case Op::Addr: return "addr";
  case Op::Load: return "load";
  case Op::Store: return "store";
  case Op::Phi: return "phi";
  case Op::Br: return "br";
  case Op::CondBr: return "condbr";
  case Op::Ret: return "ret";
  }
  return "?";
}

// ------------------------------------------------------------------ Value

void Value::removeUser(Instruction* user) {
  auto it = std::find(users_.begin(), users_.end(), user);
  assert(it != users_.end() && "removing a user that isn't registered");
  users_.erase(it);
}

void Value::replaceAllUsesWith(Value* replacement) {
  assert(replacement != this);
  // Copy: setOperand mutates users_.
  std::vector<Instruction*> users = users_;
  for (Instruction* user : users)
    for (size_t i = 0; i < user->operands().size(); ++i)
      if (user->operand(i) == this) user->setOperand(i, replacement);
}

int32_t Constant::i32() const { return std::bit_cast<int32_t>(bits_); }
float Constant::f32() const { return std::bit_cast<float>(bits_); }

// ------------------------------------------------------------ Instruction

Instruction::Instruction(Op op, Type type, std::vector<Value*> operands,
                         std::vector<BasicBlock*> blocks)
    : Value(Kind::Instruction, type), op_(op), operands_(std::move(operands)),
      blocks_(std::move(blocks)) {
  for (Value* v : operands_) v->addUser(this);
}

Instruction::~Instruction() { dropAllReferences(); }

void Instruction::setOperand(size_t i, Value* v) {
  operands_[i]->removeUser(this);
  operands_[i] = v;
  v->addUser(this);
}

void Instruction::addOperand(Value* v) {
  operands_.push_back(v);
  v->addUser(this);
}

void Instruction::removeOperand(size_t i) {
  operands_[i]->removeUser(this);
  operands_.erase(operands_.begin() + static_cast<std::ptrdiff_t>(i));
}

void Instruction::addIncoming(Value* v, BasicBlock* from) {
  assert(op_ == Op::Phi);
  addOperand(v);
  blocks_.push_back(from);
}

void Instruction::removeIncoming(size_t i) {
  assert(op_ == Op::Phi);
  removeOperand(i);
  blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(i));
}

Value* Instruction::incomingFor(const BasicBlock* from) const {
  for (size_t i = 0; i < blocks_.size(); ++i)
    if (blocks_[i] == from) return operands_[i];
  return nullptr;
}

void Instruction::dropAllReferences() {
  for (Value* v : operands_) v->removeUser(this);
  operands_.clear();
}

void Instruction::eraseFromParent() {
  assert(!hasUsers() && "erasing an instruction that still has users");
  BasicBlock* bb = parent_;
  bb->remove(this); // unique_ptr destroyed here, destructor drops references
}

// ------------------------------------------------------------- BasicBlock

Instruction* BasicBlock::append(std::unique_ptr<Instruction> inst) {
  inst->parent_ = this;
  insts_.push_back(std::move(inst));
  return insts_.back().get();
}

Instruction* BasicBlock::insertPhi(std::unique_ptr<Instruction> phi) {
  phi->parent_ = this;
  auto it = insts_.begin();
  while (it != insts_.end() && (*it)->op() == Op::Phi) ++it;
  return insts_.insert(it, std::move(phi))->get();
}

Instruction* BasicBlock::insertBeforeTerminator(std::unique_ptr<Instruction> inst) {
  inst->parent_ = this;
  auto it = insts_.end();
  if (!insts_.empty() && insts_.back()->isTerminator()) --it;
  return insts_.insert(it, std::move(inst))->get();
}

std::unique_ptr<Instruction> BasicBlock::remove(Instruction* inst) {
  for (auto it = insts_.begin(); it != insts_.end(); ++it) {
    if (it->get() == inst) {
      std::unique_ptr<Instruction> owned = std::move(*it);
      insts_.erase(it);
      owned->parent_ = nullptr;
      return owned;
    }
  }
  assert(false && "instruction not in block");
  return nullptr;
}

Instruction* BasicBlock::terminator() const {
  if (insts_.empty() || !insts_.back()->isTerminator()) return nullptr;
  return insts_.back().get();
}

std::vector<BasicBlock*> BasicBlock::successors() const {
  std::vector<BasicBlock*> out;
  if (Instruction* t = terminator())
    for (BasicBlock* b : t->blocks())
      if (std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
  return out;
}

std::vector<BasicBlock*> BasicBlock::predecessors() const {
  std::vector<BasicBlock*> out;
  for (const auto& b : parent_->blocks()) {
    Instruction* t = b->terminator();
    if (!t) continue;
    for (BasicBlock* s : t->blocks()) {
      if (s == this) {
        out.push_back(b.get());
        break;
      }
    }
  }
  return out;
}

std::vector<Instruction*> BasicBlock::phis() const {
  std::vector<Instruction*> out;
  for (const auto& i : insts_) {
    if (i->op() != Op::Phi) break;
    out.push_back(i.get());
  }
  return out;
}

// --------------------------------------------------------------- Function

Function::~Function() {
  for (auto& bb : blocks_)
    for (auto& inst : bb->insts()) inst->dropAllReferences();
}

Argument* Function::addArg(Type type, std::string argName) {
  args_.push_back(std::make_unique<Argument>(type, static_cast<unsigned>(args_.size()),
                                             std::move(argName)));
  return args_.back().get();
}

static std::string uniqueName(std::map<std::string, unsigned>& counts, const std::string& base) {
  unsigned& n = counts[base];
  std::string result = n == 0 ? base : base + "." + std::to_string(n);
  ++n;
  return result;
}

BasicBlock* Function::createBlock(const std::string& blockName) {
  blocks_.push_back(std::make_unique<BasicBlock>(this, uniqueName(blockNameCounts_, blockName)));
  return blocks_.back().get();
}

BasicBlock* Function::createBlockAfter(BasicBlock* after, const std::string& blockName) {
  auto it = std::find_if(blocks_.begin(), blocks_.end(),
                         [&](const auto& b) { return b.get() == after; });
  assert(it != blocks_.end());
  ++it;
  auto inserted = blocks_.insert(
      it, std::make_unique<BasicBlock>(this, uniqueName(blockNameCounts_, blockName)));
  return inserted->get();
}

void Function::eraseBlock(BasicBlock* block) {
  for (auto& inst : block->insts()) inst->dropAllReferences();
  auto it = std::find_if(blocks_.begin(), blocks_.end(),
                         [&](const auto& b) { return b.get() == block; });
  assert(it != blocks_.end());
  blocks_.erase(it);
}

BasicBlock* Function::findBlock(const std::string& blockName) const {
  for (const auto& b : blocks_)
    if (b->name == blockName) return b.get();
  return nullptr;
}

Constant* Function::constant(Type type, uint32_t bits) {
  auto& slot = constants_[{type, bits}];
  if (!slot) slot = std::make_unique<Constant>(type, bits);
  return slot.get();
}

Constant* Function::constI32(int32_t v) { return constant(Type::I32, std::bit_cast<uint32_t>(v)); }
Constant* Function::constF32(float v) { return constant(Type::F32, std::bit_cast<uint32_t>(v)); }
Constant* Function::constF32Bits(uint32_t bits) { return constant(Type::F32, bits); }
Constant* Function::constI1(bool v) { return constant(Type::I1, v ? 1u : 0u); }

// -------------------------------------------------------------- IRBuilder

Instruction* IRBuilder::emit(Op op, Type type, std::vector<Value*> ops,
                             std::vector<BasicBlock*> blocks) {
  assert(block_ && "IRBuilder has no insertion block");
  return block_->append(std::make_unique<Instruction>(op, type, std::move(ops), std::move(blocks)));
}

Instruction* IRBuilder::binary(Op op, Value* a, Value* b) { return emit(op, a->type(), {a, b}); }
Instruction* IRBuilder::neg(Value* a) { return emit(Op::Neg, a->type(), {a}); }
Instruction* IRBuilder::lt(Value* a, Value* b) { return emit(Op::Lt, Type::I1, {a, b}); }
Instruction* IRBuilder::globalId() { return emit(Op::GlobalId, Type::I32, {}); }
Instruction* IRBuilder::addr(Value* ptr, Value* index) {
  return emit(Op::Addr, Type::Ptr, {ptr, index});
}
Instruction* IRBuilder::load(Value* address) { return emit(Op::Load, Type::F32, {address}); }
Instruction* IRBuilder::store(Value* value, Value* address) {
  return emit(Op::Store, Type::Void, {value, address});
}
Instruction* IRBuilder::phi(Type type, BasicBlock* at) {
  return at->insertPhi(std::make_unique<Instruction>(Op::Phi, type, std::vector<Value*>{}));
}
Instruction* IRBuilder::br(BasicBlock* target) { return emit(Op::Br, Type::Void, {}, {target}); }
Instruction* IRBuilder::condBr(Value* cond, BasicBlock* ifTrue, BasicBlock* ifFalse) {
  return emit(Op::CondBr, Type::Void, {cond}, {ifTrue, ifFalse});
}
Instruction* IRBuilder::ret() { return emit(Op::Ret, Type::Void, {}); }

} // namespace wf::ir

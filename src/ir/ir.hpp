#pragma once

// WarpForge IR: a small SSA intermediate representation.
//
//   Module ─┬─ Function (one per kernel)
//           │    ├─ Arguments          (kernel parameters)
//           │    ├─ Constants          (interned per function)
//           │    └─ BasicBlock list    (first block is the entry)
//           │          └─ Instruction list, ending in exactly one terminator
//
// SSA: every Instruction that produces a value is defined exactly once, and
// every use refers directly to its defining Value (no names, no lookups).
// Each Value keeps a list of the Instructions that use it ("use list"), which
// is what makes replaceAllUsesWith, DCE and SCCP's SSA worklist cheap.
//
// Phi operands pair up with incoming blocks: operands()[i] flows in from
// blocks()[i]. For branches, blocks() holds the successor targets.

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wf::ir {

enum class Type { Void, I1, I32, F32, Ptr };

const char* typeName(Type t); // "void", "i1", "i32", "f32", "ptr"

enum class Op {
  Add, Sub, Mul, Neg, // arithmetic on i32 or f32
  Lt,                 // i32/f32 -> i1
  GlobalId,           // -> i32 : ctaid.x * ntid.x + tid.x
  Addr,               // (ptr, i32) -> ptr : base + sext(index) * 4
  Load,               // ptr -> f32
  Store,              // (f32 value, ptr) -> void, side effect
  Phi,
  Br, CondBr, Ret,    // terminators
};

const char* opName(Op op); // "add", "lt", "global_id", ...

class Instruction;
class BasicBlock;
class Function;

class Value {
public:
  enum class Kind { Constant, Argument, Instruction };

  virtual ~Value() = default;
  Value(const Value&) = delete;
  Value& operator=(const Value&) = delete;

  Kind valueKind() const { return kind_; }
  Type type() const { return type_; }

  // One entry per operand slot that refers to this value (a user that uses
  // the value twice appears twice).
  const std::vector<Instruction*>& users() const { return users_; }
  bool hasUsers() const { return !users_.empty(); }

  // Rewrites every use of this value to use `replacement` instead.
  void replaceAllUsesWith(Value* replacement);

  // Optional name hint used by the printer (e.g. the source variable name).
  std::string name;

protected:
  Value(Kind kind, Type type) : kind_(kind), type_(type) {}

private:
  friend class Instruction;
  void addUser(Instruction* user) { users_.push_back(user); }
  void removeUser(Instruction* user);

  Kind kind_;
  Type type_;
  std::vector<Instruction*> users_;
};

class Constant final : public Value {
public:
  Constant(Type type, uint32_t bits) : Value(Kind::Constant, type), bits_(bits) {}

  uint32_t bits() const { return bits_; } // raw bit pattern (f32 as IEEE bits)
  int32_t i32() const;
  float f32() const;
  bool i1() const { return bits_ != 0; }

private:
  uint32_t bits_;
};

class Argument final : public Value {
public:
  Argument(Type type, unsigned index, std::string argName)
      : Value(Kind::Argument, type), index_(index) {
    name = std::move(argName);
  }
  unsigned index() const { return index_; }

private:
  unsigned index_;
};

class Instruction final : public Value {
public:
  Instruction(Op op, Type type, std::vector<Value*> operands, std::vector<BasicBlock*> blocks = {});
  ~Instruction() override;

  Op op() const { return op_; }
  BasicBlock* parent() const { return parent_; }

  const std::vector<Value*>& operands() const { return operands_; }
  Value* operand(size_t i) const { return operands_[i]; }
  void setOperand(size_t i, Value* v);
  void addOperand(Value* v); // appends (used for phi incoming values)
  void removeOperand(size_t i);

  // Phi: incoming block of operand i. Br/CondBr: successor targets.
  const std::vector<BasicBlock*>& blocks() const { return blocks_; }
  std::vector<BasicBlock*>& blocks() { return blocks_; }

  // Phi helpers.
  void addIncoming(Value* v, BasicBlock* from);
  void removeIncoming(size_t i);
  Value* incomingFor(const BasicBlock* from) const; // nullptr if absent

  bool isTerminator() const { return op_ == Op::Br || op_ == Op::CondBr || op_ == Op::Ret; }
  bool hasSideEffects() const { return op_ == Op::Store; }

  // Drops all operand references (so users lists stay correct) and unlinks
  // the instruction from its block, destroying it. Must have no users.
  void eraseFromParent();
  // Drops operand references without erasing (used when deleting whole
  // blocks whose instructions may reference each other).
  void dropAllReferences();

private:
  friend class BasicBlock;
  Op op_;
  std::vector<Value*> operands_;
  std::vector<BasicBlock*> blocks_;
  BasicBlock* parent_ = nullptr;
};

class BasicBlock {
public:
  using InstList = std::list<std::unique_ptr<Instruction>>;

  BasicBlock(Function* parent, std::string blockName)
      : name(std::move(blockName)), parent_(parent) {}

  std::string name;
  Function* parent() const { return parent_; }

  InstList& insts() { return insts_; }
  const InstList& insts() const { return insts_; }

  // Appends before nothing (at end). Returns the raw pointer for convenience.
  Instruction* append(std::unique_ptr<Instruction> inst);
  // Inserts after the last phi at the top of the block.
  Instruction* insertPhi(std::unique_ptr<Instruction> phi);
  // Inserts immediately before the terminator (or at end if none).
  Instruction* insertBeforeTerminator(std::unique_ptr<Instruction> inst);
  // Unlinks without destroying; caller takes ownership.
  std::unique_ptr<Instruction> remove(Instruction* inst);

  Instruction* terminator() const; // nullptr if the block isn't terminated
  std::vector<BasicBlock*> successors() const;   // in terminator order, deduplicated
  std::vector<BasicBlock*> predecessors() const; // in function block order, deduplicated
  std::vector<Instruction*> phis() const;

private:
  Function* parent_;
  InstList insts_;
};

class Function {
public:
  explicit Function(std::string fnName) : name(std::move(fnName)) {}

  std::string name;

  std::vector<std::unique_ptr<Argument>>& args() { return args_; }
  const std::vector<std::unique_ptr<Argument>>& args() const { return args_; }
  Argument* addArg(Type type, std::string argName);

  std::list<std::unique_ptr<BasicBlock>>& blocks() { return blocks_; }
  const std::list<std::unique_ptr<BasicBlock>>& blocks() const { return blocks_; }
  BasicBlock* entry() const { return blocks_.empty() ? nullptr : blocks_.front().get(); }

  // Creates a block with a unique name: `name`, else `name.1`, `name.2`, ...
  BasicBlock* createBlock(const std::string& name);
  // Creates a block right after `after` in layout order.
  BasicBlock* createBlockAfter(BasicBlock* after, const std::string& name);
  // Deletes a block; its instructions must no longer be used outside it, and
  // no terminator may still target it.
  void eraseBlock(BasicBlock* block);
  BasicBlock* findBlock(const std::string& name) const;

  // Interned constants: the same (type, bits) always returns the same object,
  // so pointer equality is value equality.
  Constant* constI32(int32_t v);
  Constant* constF32(float v);
  Constant* constF32Bits(uint32_t bits);
  Constant* constI1(bool v);
  Constant* constant(Type type, uint32_t bits);

private:
  std::vector<std::unique_ptr<Argument>> args_;
  std::list<std::unique_ptr<BasicBlock>> blocks_;
  std::map<std::pair<Type, uint32_t>, std::unique_ptr<Constant>> constants_;
  std::map<std::string, unsigned> blockNameCounts_;
};

struct Module {
  std::vector<std::unique_ptr<Function>> functions;
};

// Convenience builder: appends instructions at the end of a block and infers
// result types from the opcode and operands.
class IRBuilder {
public:
  explicit IRBuilder(BasicBlock* block = nullptr) : block_(block) {}
  void setInsertPoint(BasicBlock* block) { block_ = block; }
  BasicBlock* insertBlock() const { return block_; }

  Instruction* add(Value* a, Value* b) { return binary(Op::Add, a, b); }
  Instruction* sub(Value* a, Value* b) { return binary(Op::Sub, a, b); }
  Instruction* mul(Value* a, Value* b) { return binary(Op::Mul, a, b); }
  Instruction* binary(Op op, Value* a, Value* b);
  Instruction* neg(Value* a);
  Instruction* lt(Value* a, Value* b);
  Instruction* globalId();
  Instruction* addr(Value* ptr, Value* index);
  Instruction* load(Value* address);
  Instruction* store(Value* value, Value* address);
  // Creates an empty phi at the top of `at` (not at the insert point).
  Instruction* phi(Type type, BasicBlock* at);
  Instruction* br(BasicBlock* target);
  Instruction* condBr(Value* cond, BasicBlock* ifTrue, BasicBlock* ifFalse);
  Instruction* ret();

private:
  Instruction* emit(Op op, Type type, std::vector<Value*> ops, std::vector<BasicBlock*> blocks = {});
  BasicBlock* block_;
};

} // namespace wf::ir

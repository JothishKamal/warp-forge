#include "ir/dominators.hpp"
#include "ir/verifier.hpp"

#include <gtest/gtest.h>

using namespace wf::ir;

namespace {

// entry -> (then | else) -> end   (the classic diamond)
struct Diamond {
  Function fn{"k"};
  Argument* n;
  BasicBlock *entry, *then, *els, *end;
  Diamond() {
    n = fn.addArg(Type::I32, "n");
    entry = fn.createBlock("entry");
    then = fn.createBlock("if.then");
    els = fn.createBlock("if.else");
    end = fn.createBlock("if.end");
    IRBuilder ir(entry);
    ir.condBr(ir.lt(n, fn.constI32(0)), then, els);
    ir.setInsertPoint(then);
    ir.br(end);
    ir.setInsertPoint(els);
    ir.br(end);
    ir.setInsertPoint(end);
    ir.ret();
  }
};

bool mentions(const std::vector<std::string>& errs, const std::string& needle) {
  for (const auto& e : errs)
    if (e.find(needle) != std::string::npos) return true;
  return false;
}

} // namespace

TEST(Dominators, Diamond) {
  Diamond d;
  DominatorTree dt(d.fn);
  EXPECT_EQ(dt.idom(d.entry), nullptr);
  EXPECT_EQ(dt.idom(d.then), d.entry);
  EXPECT_EQ(dt.idom(d.els), d.entry);
  EXPECT_EQ(dt.idom(d.end), d.entry); // neither arm dominates the join
  EXPECT_TRUE(dt.dominates(d.entry, d.end));
  EXPECT_FALSE(dt.dominates(d.then, d.end));
  EXPECT_TRUE(dt.dominates(d.then, d.then));
}

TEST(Dominators, UnreachableBlock) {
  Diamond d;
  BasicBlock* dead = d.fn.createBlock("dead");
  IRBuilder(dead).ret();
  DominatorTree dt(d.fn);
  EXPECT_FALSE(dt.reachable(dead));
  EXPECT_EQ(dt.reversePostOrder().size(), 4u);
  EXPECT_EQ(dt.reversePostOrder().front(), d.entry);
}

TEST(Verifier, AcceptsDiamond) {
  Diamond d;
  EXPECT_TRUE(verifyFunction(d.fn).empty());
}

TEST(Verifier, MissingTerminator) {
  Function fn("k");
  IRBuilder(fn.createBlock("entry")).globalId();
  EXPECT_TRUE(mentions(verifyFunction(fn), "block 'entry' does not end in a terminator"));
}

TEST(Verifier, TerminatorMidBlock) {
  Function fn("k");
  BasicBlock* bb = fn.createBlock("entry");
  IRBuilder ir(bb);
  ir.ret();
  ir.globalId();
  ir.ret();
  EXPECT_TRUE(mentions(verifyFunction(fn), "terminator in the middle of block 'entry'"));
}

TEST(Verifier, PhiNotAtStart) {
  Diamond d;
  IRBuilder ir;
  Instruction* phi = ir.phi(Type::I32, d.end);
  phi->addIncoming(d.fn.constI32(1), d.then);
  phi->addIncoming(d.fn.constI32(2), d.els);
  // Move a non-phi ahead of it.
  auto g = std::make_unique<Instruction>(Op::GlobalId, Type::I32, std::vector<Value*>{});
  g->name = "g";
  auto& insts = d.end->insts();
  Instruction* raw = g.get();
  insts.push_front(std::move(g));
  (void)raw;
  EXPECT_TRUE(mentions(verifyFunction(d.fn), "phi is not at the start of block 'if.end'"));
}

TEST(Verifier, PhiIncomingMismatch) {
  Diamond d;
  Instruction* phi = IRBuilder().phi(Type::I32, d.end);
  phi->addIncoming(d.fn.constI32(1), d.then); // missing if.else
  EXPECT_TRUE(mentions(verifyFunction(d.fn), "has no incoming value for predecessor 'if.else'"));
}

TEST(Verifier, PhiFromNonPredecessor) {
  Diamond d;
  Instruction* phi = IRBuilder().phi(Type::I32, d.end);
  phi->addIncoming(d.fn.constI32(1), d.then);
  phi->addIncoming(d.fn.constI32(2), d.els);
  phi->addIncoming(d.fn.constI32(3), d.entry);
  EXPECT_TRUE(mentions(verifyFunction(d.fn), "incoming block 'entry' that is not a predecessor"));
}

TEST(Verifier, TypeMismatch) {
  Function fn("k");
  BasicBlock* bb = fn.createBlock("entry");
  IRBuilder ir(bb);
  bb->append(std::make_unique<Instruction>(Op::Add, Type::I32,
                                           std::vector<Value*>{fn.constI32(1), fn.constF32(1.0f)}));
  ir.ret();
  EXPECT_TRUE(mentions(verifyFunction(fn), "operand types do not match"));
}

TEST(Verifier, UseNotDominated) {
  Diamond d;
  // Define in if.then, use in if.else.
  Instruction* g = d.then->insertBeforeTerminator(
      std::make_unique<Instruction>(Op::GlobalId, Type::I32, std::vector<Value*>{}));
  d.els->insertBeforeTerminator(
      std::make_unique<Instruction>(Op::Add, Type::I32, std::vector<Value*>{g, g}));
  EXPECT_TRUE(mentions(verifyFunction(d.fn), "does not dominate its use"));
}

TEST(Verifier, UseBeforeDefInSameBlock) {
  Function fn("k");
  BasicBlock* bb = fn.createBlock("entry");
  IRBuilder ir(bb);
  Instruction* g = ir.globalId();
  ir.ret();
  auto add = std::make_unique<Instruction>(Op::Add, Type::I32, std::vector<Value*>{g, g});
  bb->insts().push_front(std::move(add));
  EXPECT_TRUE(mentions(verifyFunction(fn), "does not dominate its use"));
}

TEST(Verifier, BranchToForeignBlock) {
  Function a("a"), b("b");
  BasicBlock* other = b.createBlock("other");
  IRBuilder(other).ret();
  IRBuilder(a.createBlock("entry")).br(other);
  EXPECT_TRUE(mentions(verifyFunction(a), "branch target 'other' is not in function"));
}

TEST(Verifier, EntryHasPredecessor) {
  Function fn("k");
  BasicBlock* entry = fn.createBlock("entry");
  IRBuilder(entry).br(entry);
  EXPECT_TRUE(mentions(verifyFunction(fn), "entry block 'entry' has predecessors"));
}

TEST(Verifier, FunctionWithoutBlocks) {
  Function fn("k");
  EXPECT_TRUE(mentions(verifyFunction(fn), "function has no blocks"));
}

TEST(Verifier, PhiInEntryBlock) {
  // The entry has no predecessors, so a phi there could never get a value;
  // the backend would read a register nothing wrote.
  Function fn("k");
  BasicBlock* entry = fn.createBlock("entry");
  IRBuilder().phi(Type::F32, entry);
  IRBuilder(entry).ret();
  EXPECT_TRUE(mentions(verifyFunction(fn), "phi in the entry block 'entry'"));
}

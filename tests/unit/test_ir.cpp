#include "ir/ir.hpp"
#include "ir/printer.hpp"

#include <gtest/gtest.h>

using namespace wf::ir;

namespace {

// Hand-built vector_add, matching what IRGen should produce.
std::unique_ptr<Function> buildVectorAdd() {
  auto fn = std::make_unique<Function>("vector_add");
  Argument* a = fn->addArg(Type::Ptr, "a");
  Argument* b = fn->addArg(Type::Ptr, "b");
  Argument* c = fn->addArg(Type::Ptr, "c");
  Argument* n = fn->addArg(Type::I32, "n");
  BasicBlock* entry = fn->createBlock("entry");
  BasicBlock* then = fn->createBlock("if.then");
  BasicBlock* end = fn->createBlock("if.end");

  IRBuilder ir(entry);
  Instruction* i = ir.globalId();
  i->name = "i";
  ir.condBr(ir.lt(i, n), then, end);

  ir.setInsertPoint(then);
  Value* x = ir.load(ir.addr(a, i));
  Value* y = ir.load(ir.addr(b, i));
  Value* sum = ir.add(x, y); // sequenced: argument evaluation order is unspecified
  ir.store(sum, ir.addr(c, i));
  ir.br(end);

  ir.setInsertPoint(end);
  ir.ret();
  return fn;
}

} // namespace

TEST(IR, BuildAndPrintVectorAdd) {
  auto fn = buildVectorAdd();
  EXPECT_EQ(printFunction(*fn), "kernel @vector_add(ptr %a, ptr %b, ptr %c, i32 %n) {\n"
                                "entry:\n"
                                "  %i = global_id\n"
                                "  %0 = lt.i32 %i, %n\n"
                                "  condbr %0, if.then, if.end\n"
                                "\n"
                                "if.then:\n"
                                "  %1 = addr %a, %i\n"
                                "  %2 = load.f32 %1\n"
                                "  %3 = addr %b, %i\n"
                                "  %4 = load.f32 %3\n"
                                "  %5 = add.f32 %2, %4\n"
                                "  %6 = addr %c, %i\n"
                                "  store.f32 %5, %6\n"
                                "  br if.end\n"
                                "\n"
                                "if.end:\n"
                                "  ret\n"
                                "}\n");
}

TEST(IR, PrintDeterministic) {
  auto fn = buildVectorAdd();
  EXPECT_EQ(printFunction(*fn), printFunction(*fn));
}

TEST(IR, ConstantsInternedAndPrinted) {
  Function fn("k");
  EXPECT_EQ(fn.constI32(7), fn.constI32(7));
  EXPECT_NE(fn.constI32(7), fn.constI32(8));
  EXPECT_EQ(fn.constF32(1.0f), fn.constF32Bits(0x3F800000u));
  EXPECT_NE(fn.constF32(0.0f), fn.constF32(-0.0f)); // distinct bit patterns
  EXPECT_EQ(fn.constI32(-3)->i32(), -3);

  BasicBlock* bb = fn.createBlock("entry");
  IRBuilder ir(bb);
  Instruction* x = ir.add(fn.constI32(-3), fn.constI32(4));
  ir.mul(fn.constF32(1.0f), fn.constF32(-0.0f));
  ir.lt(x, fn.constI32(0));
  ir.ret();
  EXPECT_EQ(printFunction(fn), "kernel @k() {\n"
                               "entry:\n"
                               "  %0 = add.i32 -3, 4\n"
                               "  %1 = mul.f32 0x3F800000, 0x80000000\n"
                               "  %2 = lt.i32 %0, 0\n"
                               "  ret\n"
                               "}\n");
}

TEST(IR, RAUWUpdatesUsers) {
  Function fn("k");
  BasicBlock* bb = fn.createBlock("entry");
  IRBuilder ir(bb);
  Instruction* g = ir.globalId();
  Instruction* s = ir.add(g, g);
  EXPECT_EQ(g->users().size(), 2u);
  Constant* five = fn.constI32(5);
  g->replaceAllUsesWith(five);
  EXPECT_FALSE(g->hasUsers());
  EXPECT_EQ(s->operand(0), five);
  EXPECT_EQ(s->operand(1), five);
  EXPECT_EQ(five->users().size(), 2u);
  g->eraseFromParent();
  EXPECT_EQ(bb->insts().size(), 1u);
}

TEST(IR, PhiPrintsIncomingAndDuplicateNames) {
  Function fn("k");
  Argument* n = fn.addArg(Type::I32, "n");
  BasicBlock* entry = fn.createBlock("entry");
  BasicBlock* then = fn.createBlock("if.then");
  BasicBlock* end = fn.createBlock("if.end");
  IRBuilder ir(entry);
  ir.condBr(ir.lt(n, fn.constI32(1)), then, end);
  ir.setInsertPoint(then);
  Instruction* x1 = ir.add(n, fn.constI32(1));
  x1->name = "x";
  ir.br(end);
  ir.setInsertPoint(end);
  Instruction* phi = ir.phi(Type::I32, end);
  phi->name = "x";
  phi->addIncoming(x1, then);
  phi->addIncoming(fn.constI32(2), entry);
  ir.ret();
  EXPECT_EQ(printFunction(fn), "kernel @k(i32 %n) {\n"
                               "entry:\n"
                               "  %0 = lt.i32 %n, 1\n"
                               "  condbr %0, if.then, if.end\n"
                               "\n"
                               "if.then:\n"
                               "  %x = add.i32 %n, 1\n"
                               "  br if.end\n"
                               "\n"
                               "if.end:\n"
                               "  %x.1 = phi.i32 [%x, if.then], [2, entry]\n"
                               "  ret\n"
                               "}\n");
  EXPECT_EQ(end->predecessors(), (std::vector<BasicBlock*>{entry, then}));
  EXPECT_EQ(phi->incomingFor(entry), fn.constI32(2));
}

TEST(IR, UniqueBlockNames) {
  Function fn("k");
  EXPECT_EQ(fn.createBlock("if.then")->name, "if.then");
  EXPECT_EQ(fn.createBlock("if.then")->name, "if.then.1");
  EXPECT_EQ(fn.createBlock("if.then")->name, "if.then.2");
}

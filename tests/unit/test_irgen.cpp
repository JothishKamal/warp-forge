#include "helpers.hpp"

using wf::test::irFor;

TEST(IRGen, VectorAdd) {
  EXPECT_EQ(irFor("kernel vector_add(float* a, float* b, float* c, int n) {\n"
                  "  int i = global_id();\n"
                  "  if (i < n) {\n"
                  "    c[i] = a[i] + b[i];\n"
                  "  }\n"
                  "}\n"),
            "kernel @vector_add(ptr %a, ptr %b, ptr %c, i32 %n) {\n"
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

TEST(IRGen, ConstantsStayUnfoldedAtO0) {
  EXPECT_EQ(irFor("kernel k(float* o) { int x = 2 + 3; o[x] = 1.0; }"),
            "kernel @k(ptr %o) {\n"
            "entry:\n"
            "  %x = add.i32 2, 3\n"
            "  %0 = addr %o, %x\n"
            "  store.f32 0x3F800000, %0\n"
            "  ret\n"
            "}\n");
}

TEST(IRGen, PhiOnlyOneArm) {
  EXPECT_EQ(irFor("kernel k(float* o, int n) {\n"
                  "  int i = global_id();\n"
                  "  float x = 1.0;\n"
                  "  if (i < n) { x = 2.0; }\n"
                  "  o[i] = x;\n"
                  "}\n"),
            "kernel @k(ptr %o, i32 %n) {\n"
            "entry:\n"
            "  %i = global_id\n"
            "  %0 = lt.i32 %i, %n\n"
            "  condbr %0, if.then, if.end\n"
            "\n"
            "if.then:\n"
            "  br if.end\n"
            "\n"
            "if.end:\n"
            "  %x = phi.f32 [0x3F800000, entry], [0x40000000, if.then]\n"
            "  %1 = addr %o, %i\n"
            "  store.f32 %x, %1\n"
            "  ret\n"
            "}\n");
}

TEST(IRGen, PhiBothArmsWithElse) {
  EXPECT_EQ(irFor("kernel k(float* o, int n) {\n"
                  "  int i = global_id();\n"
                  "  int x = 0;\n"
                  "  if (i < n) { x = i + 1; } else { x = i - 1; }\n"
                  "  o[x] = 0.0;\n"
                  "}\n"),
            "kernel @k(ptr %o, i32 %n) {\n"
            "entry:\n"
            "  %i = global_id\n"
            "  %0 = lt.i32 %i, %n\n"
            "  condbr %0, if.then, if.else\n"
            "\n"
            "if.then:\n"
            "  %x = add.i32 %i, 1\n"
            "  br if.end\n"
            "\n"
            "if.else:\n"
            "  %x.1 = sub.i32 %i, 1\n"
            "  br if.end\n"
            "\n"
            "if.end:\n"
            "  %x.2 = phi.i32 [%x, if.then], [%x.1, if.else]\n"
            "  %1 = addr %o, %x.2\n"
            "  store.f32 0x00000000, %1\n"
            "  ret\n"
            "}\n");
}

TEST(IRGen, TrivialPhiRemoved) {
  // x is not reassigned in either arm, so no phi may appear at the join.
  std::string ir = irFor("kernel k(float* o, int n) {\n"
                         "  int i = global_id();\n"
                         "  float x = 3.0;\n"
                         "  if (i < n) { o[i] = 1.0; } else { o[i] = 2.0; }\n"
                         "  o[i] = x;\n"
                         "}\n");
  EXPECT_EQ(ir.find("phi"), std::string::npos) << ir;
}

TEST(IRGen, ScalarParamReassigned) {
  std::string ir = irFor("kernel k(float* o, int n) {\n"
                         "  if (n < 0) { n = 0; }\n"
                         "  o[n] = 1.0;\n"
                         "}\n");
  EXPECT_NE(ir.find("%n.1 = phi.i32 [%n, entry], [0, if.then]"), std::string::npos) << ir;
}

TEST(IRGen, EarlyReturn) {
  EXPECT_EQ(irFor("kernel k(float* o, int n) {\n"
                  "  int i = global_id();\n"
                  "  if (n < i) { return; o[0] = 5.0; }\n"
                  "  o[i] = 1.0;\n"
                  "}\n"),
            "kernel @k(ptr %o, i32 %n) {\n"
            "entry:\n"
            "  %i = global_id\n"
            "  %0 = lt.i32 %n, %i\n"
            "  condbr %0, if.then, if.end\n"
            "\n"
            "if.then:\n"
            "  ret\n"
            "\n"
            "if.end:\n"
            "  %1 = addr %o, %i\n"
            "  store.f32 0x3F800000, %1\n"
            "  ret\n"
            "}\n");
}

TEST(IRGen, BothArmsReturnNoJoinBlock) {
  std::string ir = irFor("kernel k(int n) {\n"
                         "  if (n < 0) { return; } else { return; }\n"
                         "}\n");
  EXPECT_EQ(ir.find("if.end"), std::string::npos) << ir;
}

TEST(IRGen, NestedIfsAndElseIf) {
  // Mostly a verifier smoke test: helpers.hpp fails the test on verifier errors.
  std::string ir = irFor("kernel k(float* o, int n) {\n"
                         "  int i = global_id();\n"
                         "  float v = 0.0;\n"
                         "  if (i < n) {\n"
                         "    if (i < 10) { v = 1.0; } else if (i < 20) { v = 2.0; } else { v = -v; }\n"
                         "  } else {\n"
                         "    v = v * 2.0;\n"
                         "  }\n"
                         "  o[i] = v;\n"
                         "}\n");
  EXPECT_NE(ir.find("neg.f32"), std::string::npos);
  EXPECT_NE(ir.find("phi.f32"), std::string::npos);
}

TEST(IRGen, ShadowedVariablesAreDistinct) {
  std::string ir = irFor("kernel k(float* o, int n) {\n"
                         "  float x = 1.0;\n"
                         "  if (n < 0) { float x = 2.0; o[0] = x; }\n"
                         "  o[1] = x;\n"
                         "}\n");
  // The inner x never reaches the join, so no phi for it.
  EXPECT_EQ(ir.find("phi"), std::string::npos) << ir;
  EXPECT_NE(ir.find("store.f32 0x40000000"), std::string::npos) << ir;
  EXPECT_NE(ir.find("store.f32 0x3F800000"), std::string::npos) << ir;
}

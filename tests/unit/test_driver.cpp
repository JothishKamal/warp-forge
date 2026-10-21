#include "driver/driver.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wf::driver;

namespace {

bool parse(std::vector<const char*> args, Options& o, std::string& error) {
  args.insert(args.begin(), "warpforge");
  return parseOptions(static_cast<int>(args.size()), args.data(), o, error);
}

} // namespace

TEST(Options, IllustrativePrdCommands) {
  Options o;
  std::string e;
  ASSERT_TRUE(parse({"examples/vector_add.wf", "--emit-ptx", "out/vector_add.ptx"}, o, e)) << e;
  EXPECT_EQ(o.input, "examples/vector_add.wf");
  EXPECT_EQ(o.output, "out/vector_add.ptx");
  EXPECT_TRUE(o.emitPtx());

  Options a;
  ASSERT_TRUE(parse({"examples/vector_add.wf", "--dump-ir", "--opt"}, a, e)) << e;
  EXPECT_TRUE(a.dumpIr);
  EXPECT_EQ(a.optLevel, 1);
  EXPECT_FALSE(a.emitPtx());

  Options b;
  ASSERT_TRUE(parse({"examples/vector_add.wf", "--dump-ir", "--no-opt"}, b, e)) << e;
  EXPECT_EQ(b.optLevel, 0);
}

TEST(Options, PassesAndTarget) {
  Options o;
  std::string e;
  ASSERT_TRUE(parse({"x.wf", "--passes=sccp,dce", "--sm=sm_90", "--ptx-version=8.0", "-O0",
                     "--print-after-all", "--no-verify-each", "-o", "-"},
                    o, e))
      << e;
  EXPECT_TRUE(o.customPasses);
  EXPECT_EQ(o.passes, (std::vector<std::string>{"sccp", "dce"}));
  EXPECT_EQ(o.sm, "sm_90");
  EXPECT_EQ(o.ptxVersion, "8.0");
  EXPECT_TRUE(o.printAfter);
  EXPECT_FALSE(o.verifyEach);
  EXPECT_EQ(o.output, "-");
}

TEST(Options, Errors) {
  Options o;
  std::string e;
  EXPECT_FALSE(parse({}, o, e));
  EXPECT_EQ(e, "no input file");
  EXPECT_FALSE(parse({"a.wf", "b.wf"}, o, e));
  EXPECT_EQ(e, "more than one input file: 'a.wf' and 'b.wf'");
  EXPECT_FALSE(parse({"a.wf", "--frobnicate"}, o, e));
  EXPECT_EQ(e, "unknown option '--frobnicate' (see --help)");
  EXPECT_FALSE(parse({"a.wf", "-o"}, o, e));
  EXPECT_EQ(e, "option '-o' needs a value");
  EXPECT_FALSE(parse({"a.wf", "-O3"}, o, e));
  EXPECT_EQ(e, "unsupported optimization level '-O3' (use -O0 or -O1)");
}

TEST(Options, HelpAndVersionNeedNoInput) {
  Options o;
  std::string e;
  EXPECT_TRUE(parse({"--help"}, o, e));
  EXPECT_TRUE(o.help);
  Options v;
  EXPECT_TRUE(parse({"--version"}, v, e));
  EXPECT_TRUE(v.version);
}

TEST(Driver, MissingFileIsIOError) {
  Options o;
  o.input = "does/not/exist.wf";
  std::ostringstream out, err;
  EXPECT_EQ(run(o, out, err), kIOError);
  EXPECT_EQ(err.str(), "warpforge: error: cannot read 'does/not/exist.wf'\n");
}

TEST(Driver, UsageErrorExitCode) {
  const char* argv[] = {"warpforge"};
  std::ostringstream out, err;
  EXPECT_EQ(wf::driver::main(1, argv, out, err), kUsageError);
  EXPECT_NE(err.str().find("no input file"), std::string::npos);
}

TEST(Options, InputIr) {
  Options o;
  std::string e;
  ASSERT_TRUE(parse({"x.txt", "--input-ir", "--passes=sccp", "--dump-ir"}, o, e)) << e;
  EXPECT_TRUE(o.inputIr);
  Options auto_;
  ASSERT_TRUE(parse({"t/pass.wfir", "--dump-ir"}, auto_, e)) << e;
  EXPECT_TRUE(auto_.inputIr);
  Options bad;
  EXPECT_FALSE(parse({"t/pass.wfir", "--dump-ast"}, bad, e));
  EXPECT_EQ(e, "--dump-tokens, --dump-ast and --check need WarpForge source, not IR input");
}

TEST(Options, RejectsUnknownTarget) {
  Options o;
  std::string e;
  EXPECT_FALSE(parse({"x.wf", "--sm=sm_100"}, o, e));
  EXPECT_EQ(e, "unsupported target 'sm_100' (supported: sm_75 sm_80 sm_86 sm_87 sm_89 sm_90)");
  Options b;
  EXPECT_FALSE(parse({"x.wf", "--sm=banana"}, b, e));
  Options ok;
  EXPECT_TRUE(parse({"x.wf", "--sm=sm_75"}, ok, e)) << e;
}

TEST(Options, DroppedTargetsRejected) {
  Options o;
  std::string e;
  EXPECT_FALSE(parse({"x.wf", "--sm=sm_70"}, o, e)); // not assemblable with CUDA 13 ptxas
}

namespace {

std::string writeTemp(const std::string& name, const std::string& text) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream(path, std::ios::binary) << text;
  return path.string();
}

} // namespace

TEST(Driver, InputWithoutKernelsIsAnError) {
  for (const char* name : {"warpforge_empty_test.wf", "warpforge_empty_test.wfir"}) {
    Options o;
    o.inputIr = std::string(name).ends_with(".wfir");
    o.input = writeTemp(name, o.inputIr ? "; nothing here\n" : "// nothing here\n");
    o.output = "-";
    std::ostringstream out, err;
    EXPECT_EQ(run(o, out, err), kCompileError) << name;
    EXPECT_NE(err.str().find("error: no kernels in input"), std::string::npos) << err.str();
    EXPECT_EQ(out.str(), "") << name;
  }
}

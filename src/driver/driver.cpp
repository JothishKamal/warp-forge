// The compiler driver: source file -> tokens -> AST -> IR -> passes -> PTX,
// with optional dumps at each stage. With --input-ir (or a .wfir file) the
// front end is skipped and the IR is parsed directly, which is how passes are
// tested in isolation.

#include "driver/driver.hpp"

#include "codegen/ptx_emitter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/sema.hpp"
#include "ir/ir_parser.hpp"
#include "ir/irgen.hpp"
#include "ir/printer.hpp"
#include "ir/verifier.hpp"
#include "passes/pass_manager.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace wf::driver {

namespace {

bool readFile(const std::string& path, std::string& text) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  text = ss.str();
  return true;
}

// Source -> verified IR. Returns kOk with `module` null when the run should
// stop early (--check).
int runFrontend(const Options& o, std::string text, std::unique_ptr<ir::Module>& module, std::ostream& out,
                std::ostream& err) {
  SourceFile file(o.input, std::move(text));
  DiagnosticEngine diags(file);
  auto fail = [&] {
    diags.print(err);
    err << diags.errorCount() << (diags.errorCount() == 1 ? " error" : " errors") << " generated.\n";
    return kCompileError;
  };

  std::vector<Token> tokens = lex(file, diags);
  if (o.dumpTokens) dumpTokens(tokens, out);
  if (diags.errorCount()) return fail();
  std::unique_ptr<Program> program = parse(tokens, diags);
  if (diags.errorCount()) return fail();
  if (!checkProgram(*program, diags)) return fail();
  if (o.dumpAst) dumpAst(*program, out);
  if (o.check) return kOk;

  module = lowerProgram(*program);
  if (auto errs = ir::verifyModule(*module); !errs.empty()) {
    err << "warpforge: internal error: IRGen produced invalid IR:\n";
    for (const auto& e : errs) err << "  " << e << '\n';
    return kCompileError;
  }
  return kOk;
}

// Textual IR -> verified IR.
int parseIRInput(const Options& o, const std::string& text, std::unique_ptr<ir::Module>& module,
                 std::ostream& err) {
  std::string error;
  module = ir::parseIR(text, error);
  if (!module) {
    err << o.input << ':' << error << '\n';
    return kCompileError;
  }
  if (auto errs = ir::verifyModule(*module); !errs.empty()) {
    err << o.input << ": error: invalid IR:\n";
    for (const auto& e : errs) err << "  " << e << '\n';
    return kCompileError;
  }
  return kOk;
}

// Passes, IR dump, PTX emission.
int runMiddleAndBackEnd(const Options& o, ir::Module& module, std::ostream& out, std::ostream& err) {
  passes::PipelineOptions pipeline;
  if (o.customPasses) {
    pipeline.passes = o.passes;
  } else if (o.optLevel >= 1) {
    pipeline.passes = passes::defaultPipeline();
    pipeline.fixpoint = true;
  }
  pipeline.verifyEach = o.verifyEach;
  pipeline.printBefore = o.printBefore;
  pipeline.printAfter = o.printAfter;
  pipeline.log = &err;
  std::string error;
  if (!passes::runPipeline(module, pipeline, error)) {
    err << "warpforge: error: " << error << '\n';
    return o.customPasses && error.rfind("unknown pass", 0) == 0 ? kUsageError : kCompileError;
  }
  if (o.dumpIr) out << ir::printModule(module);
  if (!o.emitPtx()) return kOk;

  codegen::PTXOptions target;
  target.sm = o.sm;
  target.ptxVersion = o.ptxVersion;
  std::string ptx = codegen::emitPTX(module, target);
  if (o.output == "-") {
    out << ptx;
    return kOk;
  }
  std::filesystem::path path = o.output.empty()
                                   ? std::filesystem::path(o.input).stem().string() + ".ptx"
                                   : std::filesystem::path(o.output);
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::binary);
  if (!f || !(f << ptx)) {
    err << "warpforge: error: cannot write '" << path.string() << "'\n";
    return kIOError;
  }
  return kOk;
}

} // namespace

int run(const Options& o, std::ostream& out, std::ostream& err) {
  std::string text;
  if (!readFile(o.input, text)) {
    err << "warpforge: error: cannot read '" << o.input << "'\n";
    return kIOError;
  }
  std::unique_ptr<ir::Module> module;
  int status = o.inputIr ? parseIRInput(o, text, module, err) : runFrontend(o, std::move(text), module, out, err);
  if (status != kOk || !module) return status;
  return runMiddleAndBackEnd(o, *module, out, err);
}

int main(int argc, const char* const* argv, std::ostream& out, std::ostream& err) {
  Options o;
  std::string error;
  if (!parseOptions(argc, argv, o, error)) {
    err << "warpforge: error: " << error << '\n' << usage();
    return kUsageError;
  }
  if (o.help) {
    out << usage();
    return kOk;
  }
  if (o.version) {
    out << "warpforge " << WARPFORGE_VERSION << '\n';
    return kOk;
  }
  return run(o, out, err);
}

} // namespace wf::driver

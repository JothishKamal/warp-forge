#include "driver/options.hpp"

#include <string_view>

namespace wf::driver {

const char* usage() {
  return "usage: warpforge <input.wf> [options]\n"
         "\n"
         "Output:\n"
         "  -o, --emit-ptx <file>   write PTX to <file> ('-' for stdout); without -o and\n"
         "                          without a dump flag, writes <input-stem>.ptx\n"
         "  --dump-tokens           print the token stream\n"
         "  --dump-ast              print the type-checked AST\n"
         "  --dump-ir               print the IR after the selected pipeline\n"
         "  --check                 stop after semantic analysis (diagnostics only)\n"
         "\n"
         "Optimization:\n"
         "  -O0, --no-opt           no optimization passes\n"
         "  -O1, --opt              const-fold, sccp, dce, simplify-cfg to a fixpoint (default)\n"
         "  --passes=<a,b,...>      run exactly these passes, once each, in order\n"
         "  --print-before-all      print the IR before every pass (to stderr)\n"
         "  --print-after-all       print the IR after every pass (to stderr)\n"
         "  --no-verify-each        skip the IR verifier between passes\n"
         "\n"
         "Target:\n"
         "  --sm=<arch>             target architecture (default sm_86)\n"
         "  --ptx-version=<x.y>     PTX ISA version (default: lowest supporting --sm)\n"
         "\n"
         "  -h, --help              show this help\n"
         "  --version               show the version\n";
}

bool parseOptions(int argc, const char* const* argv, Options& o, std::string& error) {
  bool haveInput = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    auto value = [&](std::string& dst) {
      if (i + 1 >= argc) {
        error = "option '" + std::string(arg) + "' needs a value";
        return false;
      }
      dst = argv[++i];
      return true;
    };
    auto prefixed = [&](std::string_view prefix, std::string& dst) {
      if (arg.substr(0, prefix.size()) != prefix) return false;
      dst = std::string(arg.substr(prefix.size()));
      return true;
    };

    std::string list;
    if (arg == "-h" || arg == "--help") {
      o.help = true;
    } else if (arg == "--version") {
      o.version = true;
    } else if (arg == "-o" || arg == "--emit-ptx") {
      if (!value(o.output)) return false;
    } else if (arg == "--dump-tokens") {
      o.dumpTokens = true;
    } else if (arg == "--dump-ast") {
      o.dumpAst = true;
    } else if (arg == "--dump-ir") {
      o.dumpIr = true;
    } else if (arg == "--check") {
      o.check = true;
    } else if (arg == "-O0" || arg == "--no-opt") {
      o.optLevel = 0;
    } else if (arg == "-O1" || arg == "--opt") {
      o.optLevel = 1;
    } else if (arg.size() > 2 && arg.substr(0, 2) == "-O") {
      error = "unsupported optimization level '" + std::string(arg) + "' (use -O0 or -O1)";
      return false;
    } else if (prefixed("--passes=", list)) {
      o.customPasses = true;
      o.passes.clear();
      size_t start = 0;
      while (start <= list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        if (comma > start) o.passes.push_back(list.substr(start, comma - start));
        start = comma + 1;
      }
    } else if (arg == "--print-before-all") {
      o.printBefore = true;
    } else if (arg == "--print-after-all") {
      o.printAfter = true;
    } else if (arg == "--no-verify-each") {
      o.verifyEach = false;
    } else if (prefixed("--sm=", o.sm) || prefixed("--ptx-version=", o.ptxVersion)) {
      // value stored by prefixed()
    } else if (arg.size() > 1 && arg[0] == '-') {
      error = "unknown option '" + std::string(arg) + "' (see --help)";
      return false;
    } else if (haveInput) {
      error = "more than one input file: '" + o.input + "' and '" + std::string(arg) + "'";
      return false;
    } else {
      o.input = std::string(arg);
      haveInput = true;
    }
  }
  if (!haveInput && !o.help && !o.version) {
    error = "no input file";
    return false;
  }
  return true;
}

} // namespace wf::driver

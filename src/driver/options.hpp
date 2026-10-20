#pragma once

#include <string>
#include <vector>

namespace wf::driver {

struct Options {
  std::string input;
  std::string output; // empty: default (<stem>.ptx) when emitting; "-" = stdout

  bool dumpTokens = false;
  bool dumpAst = false;
  bool dumpIr = false;
  bool check = false; // stop after semantic analysis

  int optLevel = 1;                 // -O0 / -O1
  bool customPasses = false;        // --passes given
  std::vector<std::string> passes;  // for --passes
  bool printBefore = false;
  bool printAfter = false;
  bool verifyEach = true;

  std::string sm = "sm_86";
  std::string ptxVersion; // empty: default for sm

  bool help = false;
  bool version = false;

  // PTX is written when -o is given, or when no dump/check flag is.
  bool emitPtx() const { return !output.empty() || !(dumpTokens || dumpAst || dumpIr || check); }
};

// Parses argv (argv[0] is the program name). On failure returns false and
// sets `error` to a one-line message.
bool parseOptions(int argc, const char* const* argv, Options& options, std::string& error);

const char* usage();

} // namespace wf::driver

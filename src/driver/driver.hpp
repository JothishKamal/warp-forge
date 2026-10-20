#pragma once

#include "driver/options.hpp"

#include <iosfwd>

namespace wf::driver {

enum ExitCode { kOk = 0, kCompileError = 1, kUsageError = 2, kIOError = 3 };

// Runs the whole pipeline for one input file. Dumps and PTX-to-stdout go to
// `out`; diagnostics and pass logs (--print-*-all) go to `err`.
int run(const Options& options, std::ostream& out, std::ostream& err);

// argv entry point used by tools/warpforge/main.cpp.
int main(int argc, const char* const* argv, std::ostream& out, std::ostream& err);

} // namespace wf::driver

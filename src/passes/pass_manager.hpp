#pragma once

#include "passes/pass.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace wf::passes {

struct PipelineOptions {
  std::vector<std::string> passes; // pass names, in order
  bool fixpoint = false;           // repeat the whole list until nothing changes
  bool verifyEach = true;          // run the IR verifier after every pass
  bool printBefore = false;        // dump IR before each pass to `log`
  bool printAfter = false;         // dump IR after each pass to `log`
  std::ostream* log = nullptr;
};

constexpr int kMaxFixpointRounds = 10;

// -O1: the four passes, iterated to a fixpoint.
std::vector<std::string> defaultPipeline();

// Runs the pipeline on every function. Returns false and sets `error` on an
// unknown pass name or a verifier failure (the message names the pass).
bool runPipeline(ir::Module& module, const PipelineOptions& options, std::string& error);

// Same, with already-constructed passes (used by tests).
bool runPasses(ir::Module& module, std::vector<std::unique_ptr<Pass>>& passes,
               const PipelineOptions& options, std::string& error);

} // namespace wf::passes

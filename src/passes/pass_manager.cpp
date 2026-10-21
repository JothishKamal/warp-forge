#include "passes/pass_manager.hpp"

#include "ir/printer.hpp"
#include "ir/verifier.hpp"

#include <ostream>

namespace wf::passes {

std::unique_ptr<Pass> createPass(std::string_view name) {
  if (name == "const-fold") return createConstFold();
  if (name == "sccp") return createSCCP();
  if (name == "dce") return createDCE();
  if (name == "simplify-cfg") return createSimplifyCFG();
  return nullptr;
}

std::vector<std::string> defaultPipeline() { return {"const-fold", "sccp", "dce", "simplify-cfg"}; }

bool runPipeline(ir::Module& module, const PipelineOptions& options, std::string& error) {
  std::vector<std::unique_ptr<Pass>> passes;
  for (const std::string& name : options.passes) {
    auto p = createPass(name);
    if (!p) {
      error = "unknown pass '" + name + "' (available: const-fold, sccp, dce, simplify-cfg)";
      return false;
    }
    passes.push_back(std::move(p));
  }
  return runPasses(module, passes, options, error);
}

bool runPasses(ir::Module& module, std::vector<std::unique_ptr<Pass>>& passes,
               const PipelineOptions& options, std::string& error) {
  for (auto& fnPtr : module.functions) {
    ir::Function& fn = *fnPtr;
    int rounds = options.fixpoint ? kMaxFixpointRounds : 1;
    bool changedInRound = false;
    for (int round = 0; round < rounds; ++round) {
      changedInRound = false;
      for (auto& pass : passes) {
        if (options.printBefore && options.log)
          *options.log << "; *** IR before " << pass->name() << " on @" << fn.name << " ***\n"
                       << ir::printFunction(fn);
        bool changed = pass->run(fn);
        changedInRound |= changed;
        if (options.verifyEach) {
          std::vector<std::string> errs = ir::verifyFunction(fn);
          if (!errs.empty()) {
            error = "IR verification failed after pass '" + std::string(pass->name()) + "' on @" +
                    fn.name + ":";
            for (const std::string& e : errs) error += "\n  " + e;
            return false;
          }
        }
        if (options.printAfter && options.log)
          *options.log << "; *** IR after " << pass->name() << " on @" << fn.name << " ("
                       << (changed ? "changed" : "unchanged") << ") ***\n"
                       << ir::printFunction(fn);
      }
      if (!changedInRound) break;
    }
    if (options.fixpoint && changedInRound && options.log && (options.printAfter || options.printBefore))
      *options.log << "; pipeline did not reach a fixpoint on @" << fn.name << " after "
                   << kMaxFixpointRounds << " rounds\n";
  }
  return true;
}

} // namespace wf::passes

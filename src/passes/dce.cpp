// dce: dead-code elimination by mark-and-sweep over SSA.
//
//   Mark:  start from the instructions that matter by themselves (roots):
//            * stores        - their effect is visible outside the kernel
//            * terminators   - they decide which code runs
//          then mark, transitively, every instruction a live one uses.
//   Sweep: delete everything left unmarked, including phis that only feed
//          each other.
//
// Because liveness flows backwards from roots (rather than "delete values
// with no users"), a dead cycle such as two phis using each other is removed
// too.
//
// Loads are not roots: an unused load is deleted. WarpForge has no volatile
// memory, and an out-of-bounds load is undefined behaviour, so removing one
// can't change the result of any well-defined execution (docs/language.md).

#include "passes/pass.hpp"

#include <unordered_set>
#include <vector>

namespace wf::passes {

namespace {

using ir::Instruction;
using ir::Value;

class DCE final : public Pass {
public:
  const char* name() const override { return "dce"; }

  bool run(ir::Function& fn) override {
    std::unordered_set<const Instruction*> live;
    std::vector<Instruction*> work;
    for (auto& bb : fn.blocks())
      for (auto& inst : bb->insts())
        if (inst->hasSideEffects() || inst->isTerminator()) {
          live.insert(inst.get());
          work.push_back(inst.get());
        }

    while (!work.empty()) {
      Instruction* inst = work.back();
      work.pop_back();
      for (Value* op : inst->operands()) {
        if (op->valueKind() != Value::Kind::Instruction) continue;
        auto* def = static_cast<Instruction*>(op);
        if (live.insert(def).second) work.push_back(def);
      }
    }

    // Sweep in two phases: dead instructions may use each other, so drop all
    // their operand references first; then none of them has users left.
    std::vector<Instruction*> dead;
    for (auto& bb : fn.blocks())
      for (auto& inst : bb->insts())
        if (!live.count(inst.get())) dead.push_back(inst.get());
    for (Instruction* inst : dead) inst->dropAllReferences();
    for (Instruction* inst : dead) inst->eraseFromParent();
    return !dead.empty();
  }
};

} // namespace

std::unique_ptr<Pass> createDCE() { return std::make_unique<DCE>(); }

} // namespace wf::passes

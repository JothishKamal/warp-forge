#include "passes/pass.hpp"
namespace wf::passes {
namespace { class P final : public Pass { public: const char* name() const override { return "dce"; } bool run(ir::Function&) override { return false; } }; }
std::unique_ptr<Pass> createDCE() { return std::make_unique<P>(); }
}

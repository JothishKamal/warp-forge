#include "passes/pass.hpp"
namespace wf::passes {
namespace { class P final : public Pass { public: const char* name() const override { return "simplify-cfg"; } bool run(ir::Function&) override { return false; } }; }
std::unique_ptr<Pass> createSimplifyCFG() { return std::make_unique<P>(); }
}

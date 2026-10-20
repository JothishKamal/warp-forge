#pragma once

#include "ir/ir.hpp"

#include <string>

namespace wf::codegen {

struct PTXOptions {
  std::string sm = "sm_86";
  std::string ptxVersion; // empty: the lowest ISA version that supports `sm`
};

// The PTX ISA version used when PTXOptions::ptxVersion is empty.
std::string defaultPtxVersion(const std::string& sm);

// Emits a PTX module with one .entry per function.
// Note: splits critical edges in `module` in place (see out_of_ssa.hpp), so
// dump the IR before calling this if you want the pre-codegen form.
std::string emitPTX(ir::Module& module, const PTXOptions& options = {});

} // namespace wf::codegen

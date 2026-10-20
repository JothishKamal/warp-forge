# WarpForge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A C++20 compiler from the frozen WarpForge kernel language to PTX, with SSA IR, four optimization passes, ptxas + GPU validation, and study-grade docs — Tier 1 done by 18:00 today.

**Architecture:** `warpforge_core` static library (frontend → Braun-SSA IRGen → verifier/pass manager → out-of-SSA + PTX emitter), a `warpforge` CLI, a `wf-run` Driver-API harness that loads `nvcuda.dll` dynamically, GoogleTest unit tests plus CTest golden/diagnostic/ptxas/gpu tests.

**Tech Stack:** C++20, CMake ≥ 3.20 + Ninja, MinGW-w64 GCC (primary) / Clang, GoogleTest v1.15.2 via FetchContent, CUDA Driver API (dynamic), ptxas/nvcc from CUDA Toolkit when installed, Python 3 for golden/diagnostic test runners.

**Spec:** `docs/superpowers/specs/2026-10-20-warpforge-design.md`

## Global Constraints

- C++20; warnings `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`, warning-clean; `-ffp-contract=off`, never `-ffast-math`.
- Conventional Commits; **no Co-Authored-By / attribution trailers**; never push.
- Output determinism: no timestamps, paths, pointer- or hash-ordered iteration in IR/PTX.
- Diagnostics format `path:line:col: error: message` + source line + caret; 1-based columns; cap 20 errors; exit codes 0/1/2/3.
- `int` wraps (compute in `uint32_t`); `f32` folded in host `float`, one op at a time; no float algebraic identities.
- PTX float arithmetic always `.rn`; float immediates `0fXXXXXXXX`; target default `sm_86`.
- Every algorithm file begins with a header comment: algorithm, paper citation, invariants.
- All core tests run without a GPU; ptxas/gpu tests skip (CTest `SKIP_RETURN_CODE 77`) when tools/hardware absent.

## Review Focus

1. Variable assigned in only one arm of `if/else` and read after the join → must produce a φ, correct value both ways (IRGen test `PhiOnlyOneArm`, interpreter-checked).
2. `return;` inside an `if` followed by more statements → no IR after the terminator, verifier clean, PTX valid (golden `early_return.wf`).
3. Float folding edge cases (−0.0, NaN, subnormals, `0.1f+0.2f`) → bit-identical to GPU (test `ConstEval.FloatEdgeCases`; GPU check of `float_consts.wf`).
4. φ copies forming a swap cycle across a critical edge → correct values (OutOfSSA tests `LostCopy`, `SwapCycle`, interpreter on the out-of-SSA form is not available, so check via golden PTX + GPU run of `swap.wf`).
5. Unsupported constructs (`while`, `/`, `>`, `int*`, calls) → specific "not supported" diagnostic, not a crash (tests/errors/unsupported_*.wf).

---

## File map

```
CMakeLists.txt, CMakePresets.json, cmake/Warnings.cmake
src/frontend/{source,diagnostics,token,lexer,ast,parser,sema}.{hpp,cpp}
src/ir/{ir,printer,dominators,verifier,irgen,const_eval,interpreter}.{hpp,cpp}
src/passes/{pass,pass_manager,const_fold,sccp,dce,simplify_cfg}.{hpp,cpp}
src/codegen/{out_of_ssa,ptx_emitter}.{hpp,cpp}
src/driver/{options,driver}.{hpp,cpp}
tools/warpforge/main.cpp
tools/wf-run/{main.cpp,cuda_driver.hpp,cuda_driver.cpp}
tests/unit/test_*.cpp
tests/golden/*.wf (+ .tokens .ast .ir .opt.ir .ptx), tests/run_golden.py
tests/errors/*.wf, tests/run_errors.py
examples/{vector_add,saxpy_const,branchy,early_return,swap,float_consts}.wf
bench/, docs/, README.md
```
All C++ in `namespace wf` (sub-namespaces `wf::ir`, `wf::passes`, `wf::codegen`).

---

### Task 1: Scaffold, source locations, diagnostics

**Files:** Create CMake files, `src/frontend/source.*`, `src/frontend/diagnostics.*`, `tests/unit/test_diagnostics.cpp`, `tools/warpforge/main.cpp` (stub printing `--version`).

**Interfaces — Produces:**
- `struct SourceLoc { uint32_t line, col; };`
- `class SourceFile { SourceFile(std::string path, std::string text); const std::string& path() const; std::string_view text() const; std::string_view lineText(uint32_t line) const; }`
- `class DiagnosticEngine { DiagnosticEngine(const SourceFile&); void error(SourceLoc, std::string msg); size_t errorCount() const; bool tooMany() const /* >=20 */; void print(std::ostream&) const; const std::vector<Diagnostic>& all() const; }` with `struct Diagnostic { SourceLoc loc; std::string message; };`

- [ ] Test `Diagnostics.FormatsWithCaret`: file `t.wf` text `"int x\n  y = 1;\n"`, error at {2,3} "undefined variable 'y'" → prints exactly `t.wf:2:3: error: undefined variable 'y'\n  y = 1;\n  ^\n`.
- [ ] Run → fails to compile. Implement. Run → passes. `cmake --preset mingw-gcc && cmake --build build/mingw-gcc && ctest --test-dir build/mingw-gcc`.
- [ ] Commit `build: scaffold CMake project with presets and GoogleTest` and `feat(diag): add source locations and caret diagnostics`.

### Task 2: Lexer

**Files:** `src/frontend/token.hpp`, `lexer.*`, `tests/unit/test_lexer.cpp`.

**Interfaces — Produces:** `enum class TokenKind { Ident, IntLit, FloatLit, KwKernel, KwInt, KwFloat, KwIf, KwElse, KwReturn, KwGlobalId, LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semi, Star, Plus, Minus, Less, Assign, Unsupported, Eof };` `struct Token { TokenKind kind; std::string text; SourceLoc loc; };` `std::vector<Token> lex(const SourceFile&, DiagnosticEngine&);` `const char* tokenKindName(TokenKind);` `void dumpTokens(const std::vector<Token>&, std::ostream&)` — one line per token `L:C KIND 'text'`.
`Unsupported` covers recognizable unsupported operators (`/ > >= <= == != && || ! % ++ --`) and keywords (`while for do`) are lexed as `Ident` and rejected in the parser by name.

- [ ] Tests: `Lexer.VectorAdd` (kinds + locs of first 8 tokens of vector_add), `Lexer.FloatForms` (`1.5`, `1.5f`, `2e3`, `1.0e-2f` → FloatLit), `Lexer.IntOverflow` (`2147483648` → error "integer literal out of range" at its loc), `Lexer.UnterminatedComment` (error at `/*` loc), `Lexer.BadChar` (`@` → error "unexpected character '@'").
- [ ] Red → implement → green → commit `feat(lexer): tokenize WarpForge source`.

### Task 3: AST + parser

**Files:** `ast.*`, `parser.*`, `tests/unit/test_parser.cpp`.

**Interfaces — Produces:**
- `enum class TypeKind { Int, Float, FloatPtr, Bool /* sema-only, comparison */ , Error };`
- Exprs (`struct Expr { SourceLoc loc; TypeKind type = Error; virtual ~Expr(); }`): `IntLit{int32_t}`, `FloatLit{float}`, `VarRef{std::string name; VarDecl* decl=nullptr}`, `Index{VarRef base; ExprPtr index}`, `GlobalId`, `Unary{ExprPtr operand}` (negation), `Binary{char op /* '+','-','*','<' */; ExprPtr lhs, rhs}`.
- Stmts: `DeclStmt{VarDecl* decl /*owned by kernel*/, ExprPtr init}`, `AssignStmt{VarRef target; ExprPtr value}`, `StoreStmt{VarRef base; ExprPtr index, value}`, `IfStmt{ExprPtr cond; BlockPtr then_; StmtPtr else_ /* Block or If or null */}`, `ReturnStmt`, `Block{std::vector<StmtPtr>}`.
- `struct VarDecl { std::string name; TypeKind type; SourceLoc loc; bool isParam; unsigned paramIndex; };` `struct Kernel { std::string name; SourceLoc loc; std::vector<VarDecl*> params; BlockPtr body; std::vector<std::unique_ptr<VarDecl>> decls; };` `struct Program { std::vector<std::unique_ptr<Kernel>> kernels; };`
- Use a `kind` enum + `static_cast` (no RTTI dependence) — `enum class ExprKind`, `enum class StmtKind`.
- `std::unique_ptr<Program> parse(const std::vector<Token>&, DiagnosticEngine&);` recursive descent per spec §4.2; recover by skipping to `;`/`}`.
- `void dumpAst(const Program&, std::ostream&)` — indented tree, 2 spaces per level, e.g. `Kernel vector_add` / `  Param float* a` / `  Block` / `    Decl int i` / `      GlobalId` …; after sema, exprs show `: type`.

- [ ] Tests: `Parser.VectorAddAst` (exact dumpAst text), `Parser.Precedence` (`1 + 2 * 3 - -x` tree), `Parser.ElseIf`, `Parser.Unsupported` (`while (x < 1) {}` → "'while' is not supported in WarpForge"; `a / b` → "operator '/' is not supported in WarpForge"; `int* p` param → "unsupported type 'int*'"), `Parser.RecoversMultipleErrors` (two bad statements → 2 diagnostics).
- [ ] Red → implement → green → commit `feat(parser): parse kernels into an AST`.

### Task 4: Sema

**Files:** `sema.*`, `tests/unit/test_sema.cpp`, `tests/errors/*.wf`, `tests/run_errors.py`.

**Interfaces — Produces:** `bool checkProgram(Program&, DiagnosticEngine&)` — resolves `VarRef::decl`, sets `Expr::type`, enforces §4.3. Messages (substrings tested): `undefined variable 'x'`, `redeclaration of 'x' in the same scope`, `invalid operands to '+': int and float`, `cannot index non-pointer 'x'`, `index must be int, got float`, `comparison result can only be used as an if condition`, `if condition must be a comparison`, `cannot assign to pointer parameter 'a'`, `stored value must be float, got int`, `duplicate kernel name 'k'`, `cannot use pointer 'a' as a value`.
`run_errors.py <warpforge> <dir>`: for each `.wf`, collect `// expect-error: L:C: substring` lines, run `warpforge --check FILE`, assert exit 1 and the set of `L:C` matches exactly and each substring is present.

- [ ] Tests per message (unit) + one `.wf` per message under `tests/errors/` + `ShadowingAllowed` positive test.
- [ ] Red → implement → green → commit `feat(sema): type-check and resolve names` and `test(errors): add diagnostic regression suite`.

### Task 5: IR core + printer

**Files:** `src/ir/ir.*`, `printer.*`, `tests/unit/test_ir.cpp`.

**Interfaces — Produces (namespace `wf::ir`):**
- `enum class Type { I1, I32, F32, Ptr, Void };`
- `enum class Op { Add, Sub, Mul, Neg, Lt, GlobalId, Addr, Load, Store, Phi, Br, CondBr, Ret };`
- `class Value { Type type() const; const std::vector<Instruction*>& users() const; void replaceAllUsesWith(Value*); std::string name; }`; subclasses `Constant` (`static` interned via `Function::constI32(int32_t)`, `constF32(float)` (by bit pattern), `constI1(bool)`; accessors `int32_t i32() const; float f32() const; uint32_t bits() const; bool i1() const;`), `Argument{unsigned index}`, `Instruction`.
- `class Instruction : Value { Op op() const; BasicBlock* parent(); const std::vector<Value*>& operands() const; void setOperand(size_t,Value*); // Phi: operands[i] pairs with incomingBlocks()[i]; Br/CondBr: successors()  std::vector<BasicBlock*>& blocks(); bool isTerminator() const; bool hasSideEffects() const /* Store */; void eraseFromParent(); }`
- `class BasicBlock { std::string name; Function* parent(); std::list<std::unique_ptr<Instruction>>& insts(); Instruction* terminator(); std::vector<BasicBlock*> successors() const; std::vector<BasicBlock*> predecessors() const /* computed from terminators, ordered by block order */; }`
- `class Function { std::string name; std::vector<std::unique_ptr<Argument>>& args(); std::list<std::unique_ptr<BasicBlock>>& blocks(); BasicBlock* entry(); BasicBlock* createBlock(std::string name); void eraseBlock(BasicBlock*); }`; `struct Module { std::vector<std::unique_ptr<Function>> functions; };`
- `class IRBuilder { setInsertPoint(BasicBlock*); Instruction* add/sub/mul(Value*,Value*); neg(Value*); lt(Value*,Value*); globalId(); addr(Value* ptr, Value* idx); load(Value* addr); store(Value* v, Value* addr); Instruction* phi(Type, BasicBlock* at /*inserted at block start*/); void addIncoming(Instruction* phi, Value*, BasicBlock*); br(BasicBlock*); condBr(Value*, BasicBlock*, BasicBlock*); ret(); }`
- `std::string printModule(const Module&); std::string printFunction(const Function&);` — format of spec §6.3: unnamed values numbered `%0,%1,…` in order of appearance; named values `%name`, de-duplicated as `%name.1` in order; float constants `0x3F800000` hex; i32 decimal; i1 `true/false`.

- [ ] Tests: `IR.BuildAndPrintVectorAdd` (exact text), `IR.RAUWUpdatesUsers`, `IR.ConstantsInterned`, `IR.PrintDeterministic` (print twice equal).
- [ ] Red → implement → green → commit `feat(ir): add SSA IR core, builder and printer`.

### Task 6: Dominators + verifier

**Files:** `dominators.*`, `verifier.*`, `tests/unit/test_verifier.cpp`.

**Interfaces:** `class DominatorTree { explicit DominatorTree(const Function&); bool dominates(const BasicBlock*, const BasicBlock*) const; const BasicBlock* idom(const BasicBlock*) const; const std::vector<BasicBlock*>& reversePostOrder() const; bool reachable(const BasicBlock*) const; }` (Cooper–Harvey–Kennedy). `std::vector<std::string> verifyFunction(const Function&); std::vector<std::string> verifyModule(const Module&);` — checks spec §6.4; unreachable blocks are allowed (dominance checked only among reachable).

- [ ] Tests: diamond idoms; verifier rejects: missing terminator, terminator mid-block, phi not at start, phi incoming count ≠ preds, type mismatch on `add.i32` with f32 operand, use not dominated by def; accepts vector_add.
- [ ] Red → implement → green → commit `feat(ir): add dominator tree and IR verifier`.

### Task 7: IRGen (Braun SSA)

**Files:** `irgen.*`, `tests/unit/test_irgen.cpp`.

**Interfaces:** `std::unique_ptr<ir::Module> lowerProgram(const Program&);` (precondition: sema succeeded). Internal: `writeVariable(VarDecl*, BasicBlock*, Value*)`, `readVariable(VarDecl*, BasicBlock*)`, `readVariableRecursive`, `addPhiOperands`, `tryRemoveTrivialPhi`, `sealBlock(BasicBlock*)`, incomplete-phi map — as in Braun et al. 2013 Alg. 1–3. Pointer params read directly as `Argument`. Block names: `entry`, `if.then`, `if.else`, `if.end` with `.N` suffix counter per function. Statements after `return` in a block are skipped. Value names: SSA values defined by a decl/assign take the variable's name.

- [ ] Tests: `IRGen.VectorAdd` (exact IR), `IRGen.PhiOnlyOneArm` (`int x = 1; if (i < n) { x = 2; } c[i] = …` → phi in `if.end` with `[2, if.then], [1, entry]`), `IRGen.TrivialPhiRemoved` (no assignment in arms → no phi), `IRGen.EarlyReturn`, every IRGen output passes `verifyModule`.
- [ ] Red → implement → green → commit `feat(irgen): build SSA with Braun et al. algorithm`.

### Task 8: ConstEval + interpreter

**Files:** `const_eval.*`, `interpreter.*`, `tests/unit/test_const_eval.cpp`, `tests/unit/test_interpreter.cpp`.

**Interfaces:** `std::optional<ir::Constant*> evalBinary(Function&, Op, Constant*, Constant*); std::optional<Constant*> evalUnary(Function&, Op, Constant*);` (shared by const-fold, SCCP). Interpreter: `struct KernelArg { std::variant<int32_t, float, std::vector<float>*> v; }; struct InterpResult { bool ok; std::string error; }; InterpResult interpret(const ir::Function&, std::vector<KernelArg>& args, uint32_t gridSize);` — threads run sequentially; tracks per-element writer thread and readers to report races ("data race on buffer N element E between threads A and B"); OOB → "out-of-bounds load at index E of buffer N (size S) in thread T".

- [ ] Tests: `ConstEval.IntWrap` (`INT_MAX + 1 == INT_MIN`), `ConstEval.FloatEdgeCases` (bits of `0.1f+0.2f` == `0x3E99999A`, `-0.0f + 0.0f` → `+0` bits `0x00000000`, NaN propagation → is NaN, subnormal `1e-45f*1.0f` bits `0x00000001`, `3e38f*10.0f` → `0x7F800000`); `Interp.VectorAdd`, `Interp.OOBDetected`, `Interp.RaceDetected` (`c[0] = a[i];`).
- [ ] Red → implement → green → commit `feat(ir): add constant evaluator and reference interpreter`.

### Task 9: Pass manager + const-fold

**Files:** `src/passes/pass.hpp`, `pass_manager.*`, `const_fold.*`, `tests/unit/test_passes.cpp`.

**Interfaces:** `class Pass { virtual ~Pass(); virtual const char* name() const = 0; virtual bool run(ir::Function&) = 0; };` `std::unique_ptr<Pass> createPass(std::string_view name) /* nullptr if unknown */;` `struct PipelineOptions { std::vector<std::string> passes; bool fixpoint; bool verifyEach = true; bool printBefore = false, printAfter = false; std::ostream* log = nullptr; };` `std::vector<std::string> defaultPipeline() /* {"const-fold","sccp","dce","simplify-cfg"} */;` `bool runPipeline(ir::Module&, const PipelineOptions&, std::string& error);` max 10 rounds when `fixpoint`. Print banner `; *** IR after <pass> on @<fn> (changed|unchanged) ***`.

- [ ] Tests: `ConstFold.IntArith`, `ConstFold.IntIdentities` (`x*0`, `x+0`, `x-x`), `ConstFold.NoFloatIdentities` (`x + 0.0` unchanged, returns false), `ConstFold.ReportsUnchanged`, `PassManager.VerifierCatchesBrokenPass` (a test pass that corrupts IR → error mentions pass name).
- [ ] Red → implement → green → commit `feat(passes): add pass manager and constant folding`.

### Task 10: SCCP

**Files:** `sccp.*`, tests in `test_passes.cpp`.

**Algorithm (Wegman–Zadeck):** lattice `Top | Const(Constant*) | Bottom`; `Argument`, `GlobalId`, `Load` → Bottom; executable-edge set; FlowWorklist of (pred, succ) edges, SSAWorklist of instructions; phi meet over executable incoming edges only; condbr: Const → mark one edge, Bottom → both, Top → none. Rewrite: replace Const-valued instructions with constants; `condbr` on Const → `br` (remove phi incomings from dropped edge); returns true if anything rewritten.

- [ ] Tests: `SCCP.PropagatesThroughPhi` (both arms assign 3 → phi becomes 3), `SCCP.FoldsBranch` (`if (1 < 2)` → `br if.then`), `SCCP.IgnoresUnexecutableArm` (`x=1; if (2<1) {x=5}` → uses of x become 1), `SCCP.NonConstStaysBottom`.
- [ ] Red → implement → green → commit `feat(passes): add sparse conditional constant propagation`.

### Task 11: DCE + simplify-cfg + differential tests

**Files:** `dce.*`, `simplify_cfg.*`, `tests/unit/test_passes.cpp`, `tests/unit/test_differential.cpp`.

- DCE: roots = `Store` + terminators; mark through operands; erase unmarked (incl. phis, loads).
- simplify-cfg: remove unreachable blocks (fix phis in successors), `condbr x,B,B` → `br B`, merge block into sole predecessor with single successor (move phis' values via RAUW), remove phis with all-identical incoming.

- [ ] Tests: `DCE.KeepsStores`, `DCE.RemovesDeadChain`, `DCE.RemovesUnusedLoad`, `SimplifyCFG.RemovesUnreachable`, `SimplifyCFG.MergesStraightLine`; `Differential.ExamplesO0vsO1` — for every `examples/*.wf` with an input harness table (n=1000, seeded buffers) interpret O0 and O1, buffers bit-identical; `Pipeline.ReachesFixpoint`.
- [ ] Red → implement → green → commit `feat(passes): add dead-code elimination`, `feat(passes): add CFG simplification`, `test(passes): add O0/O1 differential tests`.

### Task 12: Out-of-SSA + PTX emitter

**Files:** `src/codegen/out_of_ssa.*`, `ptx_emitter.*`, `tests/unit/test_codegen.cpp`.

**Interfaces:** `struct Copy { ir::Value* dst; ir::Value* src; };` `struct MachineBlock { const ir::BasicBlock* bb; std::string label; std::vector<const ir::Instruction*> insts /* non-phi, non-terminator */; std::vector<Copy> copies /* sequentialized, at end */; const ir::Instruction* term; };` `std::vector<std::vector<Copy>> sequentializeParallelCopy(std::vector<Copy>, std::function<ir::Value*(ir::Type)> newTemp);` `std::string emitPTX(ir::Module&, const PTXOptions&)` with `struct PTXOptions { std::string sm = "sm_86"; std::string ptxVersion = "7.5"; };` Emitter splits critical edges on a clone? → **decision:** `emitPTX` takes the module by non-const ref and splits critical edges in place (adds `edge.N` blocks), documented; driver dumps IR before codegen. Register map: per class counters, assigned in layout order. Labels `$L__BB<fnIndex>_<n>`.
Sequentialization algorithm (Boissinot et al.): repeatedly emit copies whose dst is not a src of any pending copy; when only cycles remain, copy one src to a temp and redirect.

- [ ] Tests: `OutOfSSA.SwapCycle` (`{a←b, b←a}` → 3 copies through temp, correct when simulated), `OutOfSSA.Chain` (`{b←a, c←b}` → `c←b` first), `OutOfSSA.SplitsCriticalEdge`, `PTX.VectorAddGolden` (compare to `tests/golden/vector_add.ptx`), `PTX.FloatImmHex` (`1.0f` → `0f3F800000`), `PTX.Deterministic`.
- [ ] Red → implement → green → commit `feat(codegen): translate out of SSA with parallel copies` and `feat(codegen): emit PTX for kernels`.

### Task 13: Driver/CLI + golden suite + examples

**Files:** `src/driver/*`, `tools/warpforge/main.cpp`, `examples/*.wf`, `tests/golden/*`, `tests/run_golden.py`.

CLI exactly per spec §12 plus `--check` (frontend only). `run_golden.py <warpforge> <dir> [--update]`: for each `X.wf`, compare `--dump-tokens`→`X.tokens`, `--dump-ast`→`X.ast`, `--dump-ir -O0`→`X.ir`, `--dump-ir -O1`→`X.opt.ir`, `-o -`→`X.ptx`. Register as CTest tests `golden`, `errors`.

- [ ] Tests: CTest `golden`, `errors`, `cli_illustrative` (the three PRD commands exit 0).
- [ ] Commit `feat(cli): add warpforge driver`, `test(golden): add golden output suite`.

### Task 14: ptxas + wf-run GPU validation

**Files:** `tools/wf-run/*`, CMake `find_program(PTXAS ptxas HINTS "$ENV{CUDA_PATH}/bin")`, `tests/run_ptxas.py`.

`cuda_driver.hpp`: typedefs for `cuInit, cuDeviceGet, cuDeviceGetName, cuCtxCreate_v2, cuModuleLoadDataEx, cuModuleGetFunction, cuMemAlloc_v2, cuMemcpyHtoD_v2, cuMemcpyDtoH_v2, cuLaunchKernel, cuCtxSynchronize, cuMemFree_v2, cuEventCreate, cuEventRecord, cuEventSynchronize, cuEventElapsedTime, cuGetErrorString, cuDriverGetVersion` loaded from `nvcuda.dll` (`LoadLibraryA`) / `libcuda.so.1` (`dlopen`). Exit 77 if library or device missing.
`wf-run <file.ptx> --kernel vector_add --n N --check` → bit-exact vs C++ reference, prints `PASS vector_add n=N (bit-exact)`. JIT logs printed on failure.

- [ ] CTest `ptxas_*` per golden PTX (skip 77 if no ptxas), `gpu_vector_add` (skip 77).
- [ ] Commit `feat(wf-run): add Driver API harness with JIT diagnostics`, `test: validate PTX with ptxas and on GPU`.

### Task 15: Docs + README (Tier 1 end)

`README.md`, `docs/language.md`, `docs/passes.md`, `docs/codegen.md`, `docs/validation.md` — with real outputs pasted from the CLI. Commit `docs: …`.

### Tier 2 tasks (in order, if time): 16 benchmarks (`wf-run --bench`, nvcc baseline, `docs/benchmarks.md`); 17 `docs/interview-guide.md`; 18 IR parser + round-trip + `.wfir` tests; 19 random program generator differential test.

# WarpForge — Design

Date: 2026-10-20
Source requirements: `WarpForge_PRD.pdf` (v1.0). This document refines the PRD into buildable decisions; where they differ, this document wins.

## 1. Intent

WarpForge is a small, end-to-end compiler from a restricted C-like kernel language to NVIDIA PTX. It is a **portfolio project** whose primary audience is recruiters and interviewers for NVIDIA's **PTX Compiler team (System Software Engineer Intern)**. That team owns PTXAS (PTX front end, optimizer, object files, linkers/loaders, the Driver Compiler Interface); its JD calls out "prior projects in compilers, linkers, debuggers" as the way to stand out.

What the project must make a reader believe: *this person understands real compiler architecture — SSA, dataflow optimization, out-of-SSA, PTX semantics — and validates their work against NVIDIA's own tools.*

The owner wants the interview conversation to centre on **the optimization passes and the PTX code generator**. Depth goes there.

**Who writes the code:** Claude implements everything; the owner studies it to explain it in interviews. Therefore code and docs must be *study material*: clear, cited, explainable at a whiteboard.

## 2. Goals, non-goals, anti-goals

### Goals
- Everything in the PRD's functional requirements FR-01…FR-15 (Must and Should).
- A genuine SSA IR with basic blocks, a verifier, a textual format that can be both printed **and parsed** (so passes are testable in isolation).
- Textbook algorithms, implemented faithfully and cited: Braun et al. SSA construction, Wegman–Zadeck SCCP, mark-sweep DCE, critical-edge splitting + parallel-copy sequentialization for out-of-SSA.
- PTX that is valid under **both** the driver JIT and standalone `ptxas`, and that runs correctly on the owner's GPU.
- Honest, reproducible benchmarks.
- Docs that double as interview prep.

### PRD traceability
| PRD | Covered in |
|---|---|
| FR-01 source input + locations | §4.4 |
| FR-02 lexing (identifiers, keywords, numeric literals, delimiters, operators, EOF) | §4.2 lexical rules, `--dump-tokens` §12 |
| FR-03 parsing (kernels, params, decls, arithmetic, assignment, indexed access, conditionals) | §4.2 grammar, `--dump-ast` §12 |
| FR-04 semantic checks | §4.3, §4.4 |
| FR-05 IR + stable dump / FR-06 IR validation | §6.3 / §6.4 |
| FR-07 passes / FR-08 pass control | §8 / §8.1, §12 |
| FR-09 PTX | §9 |
| FR-10 ptxas / FR-11 execution | §10.2 |
| FR-12 benchmarks | §11 |
| FR-13 CLI | §12 |
| FR-14 tests | §10 |
| FR-15 docs | §14 |
| PRD §9 quality (C++20/CMake, warnings, determinism, diagnostics, portability, honesty) | §3, §13, §9.4, §4.4, §10.1, §11 |
| PRD §11 definition of done | §15 milestones 1–8 together |

### Non-goals (from PRD, unchanged)
- Full C/C++/CUDA, templates, classes, exceptions.
- Linker, debugger, object-file format.
- Beating nvcc.
- GUI or web dashboard.
- Register allocation (deliberately left to `ptxas`, as NVIDIA's own front ends do).

### Anti-goals — failure even if it "works"
- **Grammar creep.** The language is frozen by §4. New syntax is rejected with a clear diagnostic, not added.
- **Unexplainable cleverness.** No template metaprogramming, no clever macro systems, no "magic" the owner can't explain at a whiteboard.
- **Overstated performance claims.** Every number comes with environment, methodology, raw data.
- **PTX that only works because the JIT is lenient.** Standalone `ptxas` must accept it too.
- **A core test suite that needs a GPU.** Frontend, IR, passes and PTX golden tests run on any machine.
- **Optimizations that change observable results** — especially float results.

## 3. Constraints and environment

- **Language/build:** C++20, CMake (≥ 3.20), Ninja. Warning-clean under GCC, Clang and MSVC.
- **Owner's machine:** Windows 11; NVIDIA GeForce RTX 3060 Laptop GPU (**sm_86**), driver 610.62; MinGW-w64 GCC and Clang, CMake 3.27.8, Ninja; git. No CUDA Toolkit, no MSVC yet.
- **Installs (owner approved):** CUDA Toolkit + Visual Studio 2022 Build Tools (C++ workload, needed by nvcc on Windows), installed via `winget` when the backend milestone is reached. Until then everything builds with MinGW.
- **Commits:** Conventional Commits (`feat(scope): …`, `fix:`, `test:`, `docs:`, `build:`, `refactor:`, `chore:`). **No `Co-Authored-By` or tool-attribution trailers.** Small commits, in milestone order, so `git log` reads as a sensible engineering story.
- **No push to any remote** without the owner's explicit go-ahead.

## 4. The WarpForge language (frozen)

Source files use the `.wf` extension.

### 4.1 Example
```c
// examples/vector_add.wf
kernel vector_add(float* a, float* b, float* c, int n) {
    int i = global_id();
    if (i < n) {
        c[i] = a[i] + b[i];
    }
}
```

### 4.2 Grammar (EBNF)
```
program     = { kernel } EOF ;
kernel      = "kernel" IDENT "(" [ param { "," param } ] ")" block ;
param       = type IDENT ;
type        = "int" | "float" | "float" "*" ;
block       = "{" { stmt } "}" ;
stmt        = decl | assign | store | if | return | block ;
decl        = scalar_type IDENT "=" expr ";" ;          (* initializer required *)
scalar_type = "int" | "float" ;
assign      = IDENT "=" expr ";" ;
store       = IDENT "[" expr "]" "=" expr ";" ;
if          = "if" "(" cond ")" block [ "else" ( block | if ) ] ;
return      = "return" ";" ;
cond        = expr "<" expr ;
expr        = term { ( "+" | "-" ) term } ;
term        = unary { "*" unary } ;
unary       = "-" unary | primary ;
primary     = INT_LIT | FLOAT_LIT | IDENT | IDENT "[" expr "]"
            | "global_id" "(" ")" | "(" expr ")" ;
```
Lexical:
- `INT_LIT`: decimal digits, value must be ≤ 2147483647 (so `-2147483648` is not expressible directly; documented).
- `FLOAT_LIT`: `digits "." digits [exponent] ["f"]` or `digits exponent ["f"]`; always binary32 (round-to-nearest from the decimal text).
- Keywords: `kernel int float if else return global_id`.
- Comments: `//…` and `/* … */` (unterminated block comment is a lexical error).
- Any other character is a lexical error with location.

### 4.3 Semantics
- **Types:** `int` = 32-bit signed two's complement; `float` = IEEE-754 binary32; `float*` = pointer to `float` in global memory. Pointers appear **only as kernel parameters**; no pointer locals, no pointer arithmetic, no address-of.
- **Kernels** return nothing. `return;` exits the thread early. Multiple kernels per file are allowed; each becomes a PTX `.entry`.
- **Scopes:** each block is a scope. Duplicate declaration in the same scope is an error; shadowing an outer name is allowed (C rules). Kernel parameters live in the outermost scope.
- **Assignment** to `int`/`float` locals and scalar parameters is allowed; assignment to a pointer parameter is an error.
- **Indexing:** `p[e]` requires `p : float*` and `e : int`; result is `float`. A store's RHS must be `float`.
- **Arithmetic** `+ - *` and unary `-`: both operands must have the same type, `int` or `float`. **No implicit conversions**; `int + float` is an error ("invalid operands to '+': int and float").
- **Integer overflow wraps** (two's complement). This is a deliberate departure from C's UB, documented, and is what the folding pass implements.
- **Comparison** `<` is valid on `int`/`int` or `float`/`float` and its result may **only** be used as an `if` condition (it is not a value). Float `<` is an ordered comparison (false if either side is NaN).
- **`global_id()`** returns `int`: `blockIdx.x * blockDim.x + threadIdx.x` (1-D only).
- **Float semantics:** every `+ - *` is a single IEEE operation rounded to nearest-even. No contraction into FMA, no reassociation, no algebraic simplification (`x + 0.0` is *not* `x` because of `-0.0`; `x * 0.0` is *not* `0.0` because of NaN/∞/−0). Subnormals are preserved (no flush-to-zero).

### 4.4 Diagnostics (FR-01, FR-04)
- Format: `path:line:col: error: message`, followed by the source line and a `^` caret (GCC/Clang style). Columns are 1-based, counted in bytes.
- The compiler **reports multiple errors** per run: the parser recovers at `;` / `}` boundaries; semantic analysis continues after an error. Cap at 20 errors.
- Required semantic diagnostics: undefined variable, duplicate declaration in scope, unsupported type (e.g. `int*`, `float**`), invalid operand/type combinations, indexing a non-pointer, non-int index, comparison used as a value, assignment to pointer parameter, unsupported construct (e.g. `while`, `for`, `/`, `>`, function calls other than `global_id`) — reported as "'while' is not supported in WarpForge" rather than a generic parse error where the token is recognizable.
- Exit codes: 0 success, 1 compile error, 2 usage error, 3 I/O error.

## 5. Architecture

```
 .wf ──► Lexer ──► Parser ──► AST ──► Sema ──► IRGen (Braun SSA) ──► IR
                                                                      │
                     .wfir (textual IR) ──► IR Parser ────────────────┤
                                                                      ▼
                                              Verifier ◄─► PassManager
                                         (const-fold, sccp, dce, simplify-cfg)
                                                                      │
                                                                      ▼
                         PTX backend: critical-edge split → φ → parallel copies
                         → sequentialize → isel → block layout → PTX text
                                                                      │
                          ┌───────────────────────┬───────────────────┤
                          ▼                       ▼                   ▼
                     ptxas -arch=sm_86      wf-run (Driver API JIT,  golden .ptx
                     (static validation)    execute, CPU compare)    tests
```

Repository layout (builder may adjust names, not the separation):
```
CMakeLists.txt  CMakePresets.json
src/frontend/   lexer, parser, ast, sema, diagnostics
src/ir/         ir core, builder, printer, ir-parser, verifier, dominators, interpreter
src/passes/     pass manager, const_fold, sccp, dce, simplify_cfg
src/codegen/    out-of-ssa, ptx emitter
src/driver/     option parsing, pipeline orchestration
tools/warpforge/   CLI main
tools/wf-run/      GPU harness (Driver API, dynamically loaded)
examples/       *.wf demo kernels
tests/unit/     GoogleTest unit tests
tests/golden/   .wf → expected tokens/ast/ir/opt-ir/ptx
tests/errors/   .wf files with expected-diagnostic annotations
tests/ir/       .wfir pass tests (input + expected output)
bench/          benchmark kernels, nvcc baseline .cu, scripts, results
docs/           see §11
```
A static library `warpforge_core` holds everything except the two `main`s; the CLI, `wf-run` and tests link it.

## 6. Intermediate representation

### 6.1 Structure
- `Module` → `Function` (one per kernel) → `BasicBlock` (ordered list) → `Instruction`.
- Values: function `Argument`s, `Instruction` results, interned `Constant`s (`i32`, `f32`).
- Types: `i1` (predicate/condition), `i32`, `f32`, `ptr` (64-bit global address of `f32`).
- Use-def chains: each value knows its users (needed for SCCP worklists, DCE, `replaceAllUsesWith`).
- Every block ends with exactly one terminator; terminators appear only at the end.

### 6.2 Instruction set
| Opcode | Form | Notes |
|---|---|---|
| `add` `sub` `mul` | `%x = add.i32 %a, %b` / `.f32` | typed by operand type |
| `neg` | `%x = neg.f32 %a` / `.i32` | separate op because `0.0 - x ≠ -x` for `x = +0.0` |
| `lt` | `%p = lt.i32 %a, %b` / `.f32` | result `i1` |
| `global_id` | `%x = global_id` | `i32` |
| `addr` | `%q = addr %p, %i` | `ptr + sext(i) * 4`, result `ptr` |
| `load` | `%x = load.f32 %q` | |
| `store` | `store.f32 %v, %q` | side effect, no result |
| `phi` | `%x = phi.i32 [%a, bb1], [%b, bb2]` | only at block start |
| `br` | `br bb` | terminator |
| `condbr` | `condbr %p, bbT, bbF` | terminator |
| `ret` | `ret` | terminator |

### 6.3 Textual format (FR-05) — printed *and* parsed
```
kernel @vector_add(ptr %a, ptr %b, ptr %c, i32 %n) {
entry:
  %i = global_id
  %0 = lt.i32 %i, %n
  condbr %0, if.then, if.end
if.then:
  %1 = addr %a, %i
  %2 = load.f32 %1
  ...
  br if.end
if.end:
  ret
}
```
- **Deterministic:** printing the same IR twice is byte-identical; value names derive from source names where available (`%i`, `%i.1`, …), otherwise sequential numbers assigned in block/instruction order at print time. Block names derive from construct + counter (`if.then`, `if.else`, `if.end`). Float constants print as exact hex (`0x3F800000`) with the decimal as a trailing comment when lossless printing would be ambiguous — builder chooses the exact spelling, but it must round-trip bit-exactly.
- **Round trip:** `parse(print(M))` printed again is identical to `print(M)`. Tested.

### 6.4 Verifier (FR-06, PRD §7)
Checks: operand types match opcode; every operand is defined; **defs dominate uses** (dominator tree via Cooper–Harvey–Kennedy "A Simple, Fast Dominance Algorithm"); φ nodes only at block start, one entry per predecessor, no entries for non-predecessors; exactly one terminator per block; branch targets exist in the function; entry block has no predecessors. Errors are reported with function, block and instruction text. Runs after IRGen and **after every pass** (always on; it's cheap at this scale), and pass manager aborts with the verifier message on failure.

### 6.5 IR interpreter
A CPU interpreter executes a kernel over a simulated 1-D grid with host-provided buffers, implementing exactly the §4.3 semantics (wrapping `i32`, binary32 `f32` with no contraction). It is used for:
1. **CPU reference results** for GPU execution checks (FR-11) — alongside a hand-written C++ reference for `vector_add`.
2. **Differential testing of optimizations:** interpret at `-O0` and `-O1`, require bit-identical buffers.

## 7. Frontend → IR: SSA construction

IRGen walks the type-checked AST and builds SSA **directly**, using Braun, Buchwald, Hack, Leißa, Mallon, Zwinkau, *"Simple and Efficient Construction of Static Single Assignment Form"* (CC 2013): `writeVariable` / `readVariable` per block, sealed blocks, incomplete φs, and on-the-fly removal of trivial φs. Because the language has no loops, every block can be sealed as soon as its predecessors are known, but the implementation should still follow the paper's general algorithm (sealing and incomplete φs) so it is recognizably the textbook method and would extend to loops.

`-O0` output is therefore already SSA, but unoptimized: it still contains `add.i32 2, 3`, constant conditions, dead values. That's the "before" picture for demos.

`return;` inside an `if` produces a branch to a block ending in `ret`; code after it in the same block is unreachable and is not emitted (Sema warns? — builder's call; not required).

## 8. Optimization passes (FR-07, FR-08, PRD §7)

### 8.1 Pass infrastructure
- Interface: each pass is a class with `name()` and `bool run(Function&)` returning **whether the IR changed**.
- `PassManager` runs a pipeline; with `--verify-each` (default on) the verifier runs after each pass. It supports `--print-before-all` / `--print-after-all` (prints `; *** IR after <pass> (changed|unchanged) ***` + IR), and `--passes=a,b,c` for a custom pipeline.
- `-O0`: no passes. `-O1` (default): `const-fold, sccp, dce, simplify-cfg`, **iterated to a fixpoint** (stop when a full round reports no change; hard cap of 10 rounds, after which it stops and notes it in `--print-after-all` output).
- Each pass is testable in isolation from a `.wfir` input via `warpforge --input-ir x.wfir --passes=sccp --dump-ir`.

### 8.2 `const-fold` — constant folding
Local, per instruction. If all operands are constants, replace the instruction with the computed constant:
- `i32`: wrapping arithmetic (compute in `uint32_t` to avoid host UB).
- `f32`: compute in host `float` (not `double`), one operation at a time. The build must use `-ffp-contract=off` (GCC/Clang) / `/fp:precise` (MSVC) and never `-ffast-math`; a unit test asserts bit-exact expected results for edge cases (NaN, ±0, ±∞, subnormals, rounding-boundary values).
- `lt` on constants → `i1` constant.
- **Integer-only algebraic identities:** `x+0`, `0+x`, `x-0`, `x*1`, `1*x` → `x`; `x*0`, `0*x` → `0`; `x-x` → `0`. **None for floats** (documented with the counterexamples from §4.3).

### 8.3 `sccp` — constant propagation
Wegman & Zadeck, *"Constant Propagation with Conditional Branches"* (TOPLAS 1991). Lattice ⊤ (undefined) / constant / ⊥ (overdefined); two worklists (CFG edges and SSA edges); φ meets only over **executable** incoming edges. Afterwards: replace values proven constant, rewrite `condbr` on a constant to `br`, and leave unreachable blocks for `simplify-cfg`/`dce` to delete. Uses the same evaluation functions as `const-fold` (one shared `ConstantEvaluator`) so the two passes can never disagree.

### 8.4 `dce` — dead-code elimination
Mark-and-sweep: roots are instructions with side effects (`store`) and terminators; mark transitively through operands; delete everything unmarked (including dead φs). **Loads are considered removable** when unused (no volatile semantics; documented). Stores are never removed.

### 8.5 `simplify-cfg`
Utility pass: delete blocks unreachable from entry (updating φs), fold `condbr x, B, B` into `br B`, merge a block into its sole predecessor when that predecessor has a single successor, remove φs with a single incoming value or identical incoming values.

### 8.6 What "safe" means (the documented rule)
A transformation is allowed only if, for every **well-defined execution**, the interpreter (§6.5) produces bit-identical memory before and after. The differential tests enforce this on all examples and on randomly generated programs (§10).

**Well-defined execution** *(builder's call, recorded here)*: every executed load/store is in bounds of its buffer, and no two threads store to the same element (and no thread reads an element another thread stores). As in C and CUDA, out-of-bounds access and data races are **undefined behaviour**; the compiler may assume they don't happen. Consequences:
- Deleting an unused load is safe (an OOB load would have been UB anyway).
- The interpreter **traps with a clear error** on any OOB access and **detects cross-thread write/read conflicts** on the same element — a debugging aid, and how the test harness proves an input set is well-defined.
- The random program generator only emits indices of the form `global_id()` (optionally guarded by `if (i < n)`) for loads/stores, so generated programs are well-defined by construction; constant indices appear only in stores executed by a single guarded thread, or not at all.
- `wf-run` GPU-vs-reference comparison applies to well-defined kernels; each kernel's harness config states buffer sizes and scalar args (`--arg n=…`, defaults derived from `--n`).

Duplicate kernel names in one file are a semantic error.

## 9. PTX backend (FR-09)

### 9.1 Out of SSA
1. **Split critical edges** (an edge from a block with >1 successor to a block with >1 predecessor) — prevents the *lost-copy* problem.
2. Replace each φ with a **parallel copy** at the end of each predecessor.
3. **Sequentialize** parallel copies, using a temporary to break cycles — prevents the *swap* problem. Reference: Briggs, Cooper, Harvey, Simpson, *"Practical Improvements to the Construction and Destruction of Static Single Assignment Form"* (SPE 1998); Boissinot et al. 2009 for parallel-copy sequentialization.
These run on a backend-local copy of the function so the IR remains valid SSA for dumping.

### 9.2 Registers
No register allocation. Virtual registers per class, declared up front:
`.reg .pred %p<N>; .reg .b32 %r<N>; .reg .f32 %f<N>; .reg .b64 %rd<N>;`
Numbering is deterministic (order of first definition in layout order).

### 9.3 Instruction selection (direct mapping)
| IR | PTX |
|---|---|
| params | `ld.param.u64 %rdK, [vector_add_param_0];` then `cvta.to.global.u64` for `ptr` params; `ld.param.s32` / `ld.param.f32` for scalars |
| `global_id` | `mov.u32` from `%ctaid.x`, `%ntid.x`, `%tid.x`; `mad.lo.s32` |
| `add/sub/mul.i32` | `add.s32` / `sub.s32` / `mul.lo.s32` |
| `add/sub/mul.f32` | **`add.rn.f32` / `sub.rn.f32` / `mul.rn.f32`** — explicit `.rn` forbids ptxas from contracting into FMA, keeping GPU results identical to the compile-time folder and the interpreter |
| `neg` | `neg.s32` / `neg.f32` |
| `lt` | `setp.lt.s32` / `setp.lt.f32` |
| `addr` | `mul.wide.s32 %rd, %i, 4; add.s64 %rd, %base, %rd;` |
| `load` / `store` | `ld.global.f32` / `st.global.f32` |
| `condbr` | `@%p bra T;` + fallthrough or `bra F;` (or `@!%p bra F;` when T falls through) |
| `br` | `bra.uni` or `bra`; omitted when the target is the next block |
| `ret` | `ret;` |
| copies | `mov.b32` / `mov.f32` / `mov.b64` |
| constants | integer immediates inline; float immediates as exact hex `0fXXXXXXXX` |

### 9.4 Module layout
```
//
// Generated by WarpForge 0.1.0
//
.version 7.x
.target sm_86
.address_size 64

.visible .entry vector_add(
    .param .u64 vector_add_param_0,
    ...
)
{ .reg declarations; body }
```
- Block layout: reverse post-order from entry, fallthrough-aware; labels `$L__BB<fn>_<n>` (matching nvcc's style).
- `--sm=<arch>` selects target (default `sm_86`); the `.version` is the lowest PTX ISA version that supports the target and is accepted by the installed ptxas and driver — builder determines the exact number empirically and records it in docs.
- **Determinism:** no timestamps, paths, or hash-ordered iteration anywhere in output. Same input + options → byte-identical PTX.

## 10. Validation and testing (FR-10, FR-11, FR-14)

### 10.1 CPU-only (always run, no GPU needed)
- **Unit tests** (GoogleTest, fetched via CMake `FetchContent` at a pinned release tag): lexer, parser, sema, IR builder/printer/parser round trip, dominators, verifier (positive and negative cases), each pass, ConstantEvaluator float edge cases, out-of-SSA (lost-copy and swap cases explicitly), PTX emitter pieces.
- **Golden tests** (`tests/golden/`): each `.wf` has expected `.tokens`, `.ast`, `.ir` (O0), `.opt.ir` (O1), `.ptx`. A CTest-driven runner diffs output. A script regenerates goldens (`--update`), and the diff is reviewed in the commit.
- **Diagnostic tests** (`tests/errors/`): `.wf` files annotated with `// expect-error: LINE:COL: message substring`; runner asserts exactly those diagnostics.
- **IR pass tests** (`tests/ir/`): `.wfir` input + `--passes=…` + expected output, in the "given this IR, expect that IR" style, so tests read as worked examples.
- **Differential optimization tests:** every example and golden program interpreted at O0 vs O1 with several input sets → bit-identical. Plus a **random program generator** (seeded, deterministic) producing well-typed straight-line + nested-if programs with constants, mixed int/float, stores; run N (e.g. 500) seeds in CI.
- **Determinism test:** compile each golden twice, require identical output.

### 10.2 With NVIDIA tools (conditional)
- **`ptxas` validation:** CMake `find_program(ptxas)`. If found, CTest assembles every golden `.ptx` and every example with `ptxas -arch=sm_86`; ptxas diagnostics are surfaced verbatim with the PTX file path. If not found, these tests are reported as skipped, not passed.
- **`wf-run` GPU harness:** loads `nvcuda.dll` / `libcuda.so` **dynamically** (own minimal declarations of the ~12 Driver API entry points needed) so it builds without the CUDA Toolkit. It JIT-loads PTX via `cuModuleLoadDataEx` with `CU_JIT_ERROR_LOG_BUFFER` / `CU_JIT_INFO_LOG_BUFFER` so JIT diagnostics are shown (this is the *Driver Compiler Interface* the JD mentions; README should say so). It launches a named kernel with generated inputs and compares to the CPU reference (hand-written C++ for `vector_add`; IR interpreter for any kernel). Comparison is **bit-exact** (expected, given §4.3 + `.rn`); any mismatch prints index, expected, got (hex).
- GPU tests are CTest tests labelled `gpu`, skipped when no driver/device is present.

## 11. Benchmarks (FR-12)

- Tool: `wf-run --bench` using CUDA events; for each variant: warmup runs (e.g. 10), then measured runs (e.g. 100); report min / median / mean / stddev, effective bandwidth (GB/s).
- Variants: WarpForge `-O0`, WarpForge `-O1`, and an **nvcc baseline** (`bench/vector_add.cu` compiled with `nvcc -ptx -arch=sm_86`, loaded through the same harness so launch path is identical).
- Kernels: `vector_add` (memory-bound) and a constant-heavy scalar-arithmetic kernel where `-O1` removes many PTX instructions.
- Sizes: fixed set, e.g. 2^16, 2^20, 2^24 elements; fixed seed for inputs; launch config 256 threads/block.
- Report also **static metrics**: PTX instruction counts per variant, and SASS-level counts if `cuobjdump`/`nvdisasm` are available.
- **Expected and to be stated honestly:** `vector_add` is memory-bound, so all variants should be ~equal; and `ptxas` itself constant-folds, so `-O0` vs `-O1` may differ in PTX size but not in runtime. Explaining *why* is a deliberate interview talking point. Results go in `docs/benchmarks.md` with GPU, driver, CUDA version, OS, power state (laptop; clocks not locked), and raw CSV in `bench/results/`.

## 12. CLI (FR-08, FR-13)

```
warpforge <input> [options]
  -o, --emit-ptx <file>     write PTX to <file> (with no -o and no dump flag:
                            write <input-stem>.ptx in the current directory)
  --dump-tokens             print tokens (line:col KIND 'text')
  --dump-ast                print AST (indented tree)
  --dump-ir                 print IR after the selected pipeline
  -O0 | --no-opt            disable optimization
  -O1 | --opt               enable default pipeline (default)
  --passes=<p1,p2,...>      custom pipeline (overrides -O)
  --print-before-all
  --print-after-all
  --no-verify-each
  --input-ir                treat input as textual IR (.wfir also auto-detected)
  --sm=<arch>               target architecture (default sm_86)
  -h, --help / --version
```
The PRD's illustrative commands must work: `warpforge examples/vector_add.wf --emit-ptx out/vector_add.ptx`, `… --dump-ir --opt`, `… --dump-ir --no-opt`.

`wf-run <file.ptx> --kernel <name> [--n N] [--check] [--bench] [--csv out.csv]`.

## 13. Build

- `CMakePresets.json` presets: `mingw-gcc`, `mingw-clang`, `msvc` (once Build Tools installed), each with debug/release.
- Warnings: GCC/Clang `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`; MSVC `/W4 /permissive-`. Warning-clean; a `ci` preset adds `-Werror` / `/WX`.
- Float flags as in §8.2.
- A GitHub Actions workflow file (Linux GCC + Windows) running the CPU test suite is included in the repo, but the repo is **not pushed** anywhere without the owner's approval.

## 14. Documentation (FR-15) — written as study material

- **Code comments:** every algorithm file starts with a header comment naming the algorithm, its paper, the invariants relied on, and a small worked example. Comments explain *why*, not *what*.
- `README.md`: one-paragraph pitch, architecture diagram, language summary, quickstart commands, before/after IR + PTX example, validation status (ptxas + GPU results), benchmark summary with environment, limitations, test instructions. Skimmable in two minutes.
- `docs/language.md` (grammar, semantics, diagnostics), `docs/ir.md`, `docs/passes/{const-fold,sccp,dce,simplify-cfg}.md` (each with before/after IR), `docs/codegen.md` (IR→PTX mapping table, out-of-SSA walkthrough, why `.rn`, why no regalloc), `docs/validation.md`, `docs/benchmarks.md`.
- `docs/interview-guide.md`: likely interviewer questions with answers grounded in the actual code (e.g. "Why is `x*0 → 0` legal for int but not float?", "What does ptxas do that you don't?", "How do you know your optimizations are correct?", "What's the lost-copy problem?").

## 15. Delivery order and cut line

**Deadline: implementation finished by 18:00 on 2026-10-20** (the owner is putting it on their resume). Work starts ~15:00, so roughly three hours. Order is a **vertical slice first**, then depth, so that at any moment the repo has something demonstrable.

**Start immediately, in the background:** `winget` installs of the CUDA Toolkit and VS 2022 Build Tools (large downloads; the owner approves UAC prompts), so they're ready by step 4.

**Tier 1 — must be done by 18:00 (the resume-worthy core):**
1. Scaffold, CMake presets (MinGW), diagnostics infrastructure, GoogleTest.
2. Lexer, parser, sema with error tests; token/AST dumps.
3. IR core, printer, verifier + dominators, Braun SSA IRGen, interpreter.
4. Pass manager + const-fold, sccp, dce, simplify-cfg, each with unit tests; O0-vs-O1 differential tests on the examples.
5. PTX backend (out-of-SSA with lost-copy/swap tests, isel, emitter); golden PTX; determinism test.
6. ptxas validation + `wf-run` bit-exact check of `vector_add` on the RTX 3060.
7. README with real before/after IR+PTX, validation results, limitations; `docs/codegen.md` and `docs/passes.md` (single file is acceptable at Tier 1).

**Tier 2 — done if time allows, in this order:**
8. Benchmarks (`wf-run --bench`, nvcc baseline) and `docs/benchmarks.md` with honest results.
9. `docs/interview-guide.md`.
10. Textual IR parser + `.wfir` pass tests + round-trip test.
11. Random program generator for differential testing.

**Tier 3 — after the deadline, if at all:** MSVC preset, GitHub Actions workflow file, per-pass doc pages.

If Tier 1 slips, cut from the bottom of Tier 1's docs first, never from tests or validation — an unvalidated claim on a resume is worse than a shorter README.

## 16. Left to the builder

AST node class design; exact diagnostic wording (within §4.4); exact IR value/block naming scheme and float-constant spelling (within §6.3's determinism + round-trip rules); exact PTX `.version`; default output path when only an input is given; internal data structures (e.g. intrusive lists vs vectors); random-generator details; exact benchmark warmup/run counts.

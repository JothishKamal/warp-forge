# Validation

WarpForge's claims are checked at four levels. Only the first three run on every machine; the NVIDIA
levels skip cleanly (CTest exit code 77) when the toolkit or a GPU is missing.

```
ctest --test-dir build/mingw-gcc            # everything
ctest --test-dir build/mingw-gcc -L gpu     # just the GPU runs
ctest --test-dir build/mingw-gcc -L nvidia  # just ptxas
```

## 1. Component tests (GoogleTest, `tests/unit/`)

| Area | File | Examples of what is pinned |
|---|---|---|
| Diagnostics | `test_diagnostics.cpp` | exact `path:line:col` + caret format, tab alignment, 20-error cap |
| Lexer | `test_lexer.cpp` | token kinds and locations, float forms, `INT_MAX` boundary, unterminated comments |
| Parser | `test_parser.cpp` | exact AST dumps, precedence, else-if, error recovery, targeted "not supported" messages |
| Sema | `test_sema.cpp` | every typing rule, scoping/shadowing, no cascading errors |
| IR | `test_ir.cpp`, `test_verifier.cpp` | printer format, constant interning (`+0.0` ≠ `-0.0`), RAUW, dominators, each verifier rule |
| IRGen | `test_irgen.cpp` | exact IR for vector_add, phis for one-arm/both-arm assignment, trivial-phi removal, early return |
| Evaluator + interpreter | `test_interpreter.cpp` | i32 wrap-around, float edge cases (−0, NaN, ∞, subnormals), OOB and race detection |
| Passes | `test_passes.cpp`, `test_dce_cfg.cpp` | see [passes.md](passes.md#tests) |
| Codegen | `test_codegen.cpp` | swap/3-cycle/chain sequentialization, critical-edge splitting, exact PTX for a small kernel, `.rn`, no `fma` |
| Driver | `test_driver.cpp` | the PRD's illustrative commands parse, usage and I/O exit codes |

## 2. Whole-compiler tests (`tests/run_*.py`)

- **Golden files** — `run_golden.py` compiles every `examples/*.wf` in six modes (tokens, AST, IR at
  -O0/-O1, PTX at -O0/-O1) and compares byte-for-byte with `tests/golden/`. Every command runs twice and
  must produce identical bytes (determinism, PRD §9). Regenerate with `--update` and review the diff.
- **Diagnostics** — `run_errors.py` runs `warpforge --check` on 27 invalid programs in `tests/errors/`, each
  annotated with `// expect-error: LINE:COL: substring`; the set of locations must match exactly.
- **CLI** — the three illustrative commands from the PRD run verbatim (`cli_*` tests).

## 3. Semantic preservation (`test_differential.cpp`)

Every example kernel is lowered twice; one copy is optimized with the full -O1 pipeline. Both run on the
reference interpreter (`src/ir/interpreter.cpp`) over 1000 threads with three seeded input sets, and every
output buffer must match **bit-for-bit**. The interpreter also aborts on out-of-bounds accesses and on
cross-thread write/read conflicts, so a passing run also proves the inputs were well-defined.

`test_random_programs.cpp` extends this beyond the hand-written examples. A seeded generator writes 500
well-typed programs, each of which goes through the same -O0-vs-O1 check and through PTX emission. A
failure prints the seed and the full program. The programs contain:

- nested `if`/`else` blocks
- mixed `int` and `float` arithmetic, and shadowing
- edge-case constants: `INT_MAX`, `-0.0`, subnormals, values near overflow
- conditions that are constant true or false, and dead code

Every access is `in[i]` or `out[i]` under `i < n`, so every program is well-defined by construction.

`tests/ir/*.wfir` exercise single passes on hand-written IR, with expected outputs written from the
algorithm rather than captured from the tool. See [ir.md](ir.md#parsing-it-back).

**The limit of this level:** the interpreter shares its float evaluator with the constant folder, so it
can't see where host semantics differ from the GPU's. That is how a NaN bug got through (see below). The
GPU fuzz at level 4 closes that gap.

## 4b. GPU fuzz (`tests/run_gpu_fuzz.py`, CTest `gpu_fuzz`)

- **What runs:** `wf-gen` writes 150 programs from the same generator, now with `return;` and dead code
  after it. Each one is compiled at -O0 and -O1, assembled with `ptxas -arch=sm_86`, and run on the GPU
  with `wf-run --check`. The GPU's output must match the interpreter bit-for-bit, 300 runs in all.
- **Why it exists:** the final code review found that NaN results weren't bit-exact. The host produces
  `0xFFC00000`, but PTX canonicalizes to `0x7FFFFFFF`, and `neg.f32` of a NaN is unspecified in PTX.
- **Proof that it works:** with the pre-fix evaluator and emitter restored, the interpreter-only test still
  passes, while the GPU fuzz fails 6 of 300 runs (seeds 56, 72, 114, …). With the fixes, 0 fail.
- **Without a GPU:** it skips with exit code 77.

## 4. NVIDIA toolchain and hardware

### Static: `ptxas`

`run_ptxas.py` assembles every golden PTX file (both optimization levels) with NVIDIA's standalone
assembler and prints its diagnostics verbatim on failure.

```
using C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/bin/ptxas.exe: Build cuda_13.3.r13.3/compiler.38244171_0
ok   branchy.O0.ptx
...
12 PTX files assembled with ptxas -arch=sm_86, 0 failures
```

### Dynamic: `wf-run` on the GPU

`wf-run` ([`tools/wf-run/`](../tools/wf-run)) needs **no CUDA Toolkit**: it loads the driver library
(`nvcuda.dll` / `libcuda.so.1`) at run time and declares the ~20 Driver API entry points it uses. The PTX is
handed to the driver's JIT compiler with `cuModuleLoadDataEx` — the Driver Compiler Interface an application
uses when it ships PTX — with the JIT error and info logs captured, so a bad module fails with actionable
messages:

```
$ wf-run --source examples/vector_add.wf --ptx broken.ptx --kernel vector_add
ptxas application ptx input, line 40; error   : Unknown modifier '.f33'
ptxas application ptx input, line 40; error   : Unexpected instruction types specified for 'ld'
ptxas fatal   : Ptx assembly aborted due to errors
```

For each run, `wf-run` generates seeded inputs from the kernel's signature (int parameters get `n − 37` so
the bounds check is exercised), launches with 256-thread blocks, copies results back, and compares every
output element **bit-for-bit** with:

1. the IR interpreter running the **-O0** IR of the same source (independent of the PTX's optimization
   level), and
2. for `vector_add`, a hand-written C++ loop.

```
device: NVIDIA GeForce RTX 3060 Laptop GPU (sm_86), CUDA driver API 13.3
PASS vector_add n=1048576 threads=1048576 (bit-exact vs IR interpreter and C++ loop)
```

CTest runs all six examples at both -O0 and -O1 (`gpu_*` tests, 12 total). Mismatches print the element,
expected and actual values in hex; a sabotaged PTX (`add.rn` → `sub.rn`) is reported as 963 mismatching
elements out of 1000.

**Why bit-exact is the right bar:** WarpForge defines each float `+ - *` as one IEEE round-to-nearest
operation (no contraction). The constant folder computes exactly that on the host (`-ffp-contract=off`),
the interpreter uses the same evaluator, and the emitter writes `add.rn`/`mul.rn` so `ptxas` can't fuse
operations. `examples/float_consts.wf` exercises −0.0, ∞, the smallest subnormal and a rounding boundary:
at -O0 the GPU computes them, at -O1 the compiler folded them, and both match the interpreter.

## Test environment (as measured)

| | |
|---|---|
| OS | Windows 11 Pro 10.0.26300 |
| Host compiler | MinGW-w64 GCC 13.2.0 (x86_64-ucrt-posix-seh) |
| Build | CMake 3.27.8, Ninja, Debug |
| GPU | NVIDIA GeForce RTX 3060 Laptop GPU (sm_86), driver 610.62 (CUDA 13.3 driver API) |
| Toolkit | CUDA 13.3 (`ptxas` cuda_13.3.r13.3/compiler.38244171_0) |

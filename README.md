# WarpForge

A small optimizing compiler, written in C++20, from a restricted C-like kernel language to **NVIDIA PTX**.
It has an SSA IR with a verifier, textbook dataflow optimizations, out-of-SSA translation, and a PTX backend.
Its output is validated with NVIDIA's own tools: `ptxas`, the driver JIT, and execution on a real GPU, checked
bit-for-bit against a CPU reference.

```
 .wf ─► lexer ─► parser ─► sema ─► IRGen (Braun SSA) ─► verifier ⇄ pass manager ─► out-of-SSA ─► PTX
                                                          const-fold · sccp · dce · simplify-cfg
```

| | |
|---|---|
| **Front end** | Hand-written lexer and recursive-descent parser. Type checker with `file:line:col` + caret diagnostics. Multiple errors per run. |
| **IR** | SSA with basic blocks, use lists and typed values. Stable text dump. Verifier checks types, CFG, phis and def-dominates-use (Cooper–Harvey–Kennedy dominators). |
| **SSA construction** | Braun et al. 2013 (*Simple and Efficient Construction of SSA Form*): phis placed on the fly, trivial phis removed. |
| **Passes** | Constant folding, **SCCP** (Wegman–Zadeck), mark-sweep DCE, CFG simplification. Pass manager with `--print-after-all` and the verifier between passes. |
| **Backend** | Critical-edge splitting and parallel-copy sequentialization (lost-copy and swap problems), typed virtual registers, `.rn` float ops, nvcc-style labels. |
| **Validation** | 167 CTest tests: unit, golden files, diagnostics, hand-written IR pass tests, O0-vs-O1 differential tests on a reference interpreter (examples + 500 random programs), `ptxas`, GPU runs through the CUDA Driver API, and a GPU fuzz that uses the hardware as the oracle (300 runs). |

## Example

`examples/saxpy_const.wf`:

```c
kernel saxpy_const(float* x, float* y, int n) {
    int i = global_id();
    float scale = 2.0 * 1.5;      // const-fold  -> 3.0
    int unused = i * 4 + 7;       // dce         -> gone
    int lanes = 32 * 8;           // const-fold  -> 256
    if (lanes < 0) {              // sccp        -> branch never taken, block deleted
        y[i] = 0.0;
    }
    if (i < n) {
        y[i] = scale * x[i] + y[i];
    }
}
```

<table><tr><th><code>--dump-ir -O0</code></th><th><code>--dump-ir -O1</code></th></tr><tr><td>

```llvm
entry:
  %i = global_id
  %scale = mul.f32 0x40000000, 0x3FC00000
  %0 = mul.i32 %i, 4
  %unused = add.i32 %0, 7
  %lanes = mul.i32 32, 8
  %1 = lt.i32 %lanes, 0
  condbr %1, if.then, if.end
if.then:
  %2 = addr %y, %i
  store.f32 0x00000000, %2
  br if.end
if.end:
  %3 = lt.i32 %i, %n
  condbr %3, if.then.1, if.end.1
if.then.1:
  ...
  %6 = mul.f32 %scale, %5
  ...
```

</td><td>

```llvm
entry:
  %i = global_id
  %0 = lt.i32 %i, %n
  condbr %0, if.then.1, if.end.1

if.then.1:
  %1 = addr %x, %i
  %2 = load.f32 %1
  %3 = mul.f32 0x40400000, %2
  %4 = addr %y, %i
  %5 = load.f32 %4
  %6 = add.f32 %3, %5
  %7 = addr %y, %i
  store.f32 %6, %7
  br if.end.1

if.end.1:
  ret
```

</td></tr></table>

The generated PTX for `examples/vector_add.wf` (abridged; the full file is [`tests/golden/vector_add.ptx`](tests/golden/vector_add.ptx)):

```ptx
.version 7.1
.target sm_86
.address_size 64

.visible .entry vector_add(
	.param .u64 vector_add_param_0, ..., .param .u32 vector_add_param_3)
{
	ld.param.u64 	%rd1, [vector_add_param_0];
	cvta.to.global.u64 	%rd2, %rd1;
	...
	mov.u32 	%r2, %ctaid.x;
	mov.u32 	%r3, %ntid.x;
	mov.u32 	%r4, %tid.x;
	mad.lo.s32 	%r5, %r2, %r3, %r4;      // global_id()
	setp.lt.s32 	%p1, %r5, %r1;           // i < n
	@!%p1 bra 	$L__BB0_2;
	mul.wide.s32 	%rd7, %r5, 4;
	add.s64 	%rd8, %rd2, %rd7;
	ld.global.f32 	%f1, [%rd8];
	...
	add.rn.f32 	%f3, %f1, %f2;           // explicit .rn: no FMA contraction
	st.global.f32 	[%rd12], %f3;
$L__BB0_2:
	ret;
}
```

Full outputs for every example are in [`tests/golden/`](tests/golden).

## Build and run

Requirements: CMake ≥ 3.20, Ninja, a C++20 compiler (tested with MinGW-w64 GCC 13.2 on Windows 11), and
Python 3 for the golden-file suites. A GPU and the CUDA Toolkit are optional (see [Validation](#validation)).

```sh
cmake --preset mingw-gcc          # or: cmake -B build -G Ninja
cmake --build build/mingw-gcc
ctest --test-dir build/mingw-gcc  # 167 tests; GPU/ptxas tests skip if unavailable
```

```sh
warpforge examples/vector_add.wf --emit-ptx out/vector_add.ptx    # compile
warpforge examples/vector_add.wf --dump-ir --opt                  # IR after -O1
warpforge examples/vector_add.wf --dump-ir --no-opt               # IR straight from IRGen
warpforge examples/saxpy_const.wf --print-after-all --dump-ir     # watch each pass (stderr)
warpforge examples/branchy.wf --passes=sccp,dce --dump-ir         # custom pipeline
warpforge examples/branchy.wf --dump-tokens | --dump-ast          # front-end dumps
wf-run --source examples/vector_add.wf --ptx out/vector_add.ptx --kernel vector_add --check
```

Diagnostics follow GCC/Clang style, and several errors are reported per run:

```
examples/bad.wf:2:20: error: invalid operands to '+': float and int
    float x = o[0] + n;
                   ^
examples/bad.wf:3:12: error: undefined variable 'undefined_thing'
    o[1] = undefined_thing;
           ^
2 errors generated.
```

## The language

`int` (32-bit, wrapping), `float` (IEEE binary32), and `float*` (global memory, kernel parameters only).
Statements are declarations with initializers, assignments, indexed loads and stores, `if`/`else if`/`else`,
and `return;`. Expressions use `+ - *`, unary `-`, `<` (only in `if` conditions), parentheses, and
`global_id()`. There are no implicit conversions and no loops. Anything else is rejected with a specific
message such as `'while' is not supported in WarpForge`. Full grammar and semantics:
[docs/language.md](docs/language.md).

## Validation

| Check | What it proves | Status on the dev machine |
|---|---|---|
| Unit tests (GoogleTest) | Each component in isolation: lexer, parser, sema, IR, verifier, each pass, out-of-SSA, emitter | ✅ |
| Golden files | Tokens, AST, IR at -O0/-O1, and PTX at -O0/-O1 for every example are byte-stable and deterministic | ✅ |
| Diagnostics suite | 27 files of invalid programs, each with exact `line:col` and message | ✅ |
| Differential O0 vs O1 | Optimized and unoptimized IR give bit-identical memory on the reference interpreter | ✅ |
| `ptxas -arch=sm_86` | Every golden PTX file assembles with NVIDIA's standalone assembler (CUDA 13.3) | ✅ 12/12 |
| GPU execution | Every example at -O0 and -O1 is JIT-compiled by the driver, run on an RTX 3060, and matches the CPU reference **bit-for-bit** | ✅ 12/12 |

The GPU path uses no CUDA Toolkit. `wf-run` loads `nvcuda.dll` at run time and hands the PTX to the driver's
JIT compiler through `cuModuleLoadDataEx`, capturing its error log. That is the Driver Compiler Interface a
CUDA application uses when it ships PTX. Details: [docs/validation.md](docs/validation.md).

Bit-exactness is by design, not luck. The constant folder, the interpreter and the GPU all perform each
`f32` operation as one IEEE round-to-nearest operation. The emitter writes `add.rn`/`mul.rn` so that `ptxas`
cannot fuse a multiply and an add into an FMA, and the host build uses `-ffp-contract=off`.
NaN results are canonicalized to PTX's `0x7FFFFFFF`. Float negation is emitted as an exact sign-bit
`xor.b32`, because PTX leaves `neg.f32` of a NaN unspecified, and on sm_86 the hardware and `ptxas`'s folder
disagree. Both rules came out of the final review, which found them by fuzzing against the GPU.
`examples/float_consts.wf` covers −0.0, ∞, subnormals, rounding and NaN: the GPU's results at -O0 match the
compile-time folds at -O1 ([codegen.md](docs/codegen.md)).

## Benchmarks

RTX 3060 Laptop, CUDA 13.3, 200 warmup + 200 timed launches, n = 2²⁴. Every variant passed the
bit-exact GPU check before it was timed. nvcc's PTX was built with `-O3 -fmad=false`, which gives it
WarpForge's no-FMA float semantics so both can pass the same bit-exact check:

| kernel | WarpForge -O0 | WarpForge -O1 | nvcc `-O3 -fmad=false` | PTX instr (O0 / O1 / nvcc) | SASS instr |
|---|---|---|---|---|---|
| `vector_add` | 0.665 ms | 0.665 ms | 0.665 ms | 24 / 24 / 22 | 16 / 16 / 16 |
| `saxpy_const` | 0.670 ms | 0.676 ms | 0.670 ms | 35 / 23 / 20 | 16 / 16 / 16, **identical SASS** |

WarpForge's PTX runs at nvcc's speed: about 303 GB/s, roughly 90% of the GPU's DRAM peak. `ptxas`
re-optimizes whatever it receives, so -O1 shrinks the PTX but compiles to the same machine code as -O0.
For `saxpy_const`, all three variants produce byte-identical SASS. Methodology, environment and the honest
caveats: [docs/benchmarks.md](docs/benchmarks.md).

## Documentation

- [docs/language.md](docs/language.md): grammar, typing rules, semantics and the full list of diagnostics
- [docs/ir.md](docs/ir.md): IR structure, instruction set, text format and verifier
- [docs/passes.md](docs/passes.md): each pass with its algorithm and before/after IR
- [docs/codegen.md](docs/codegen.md): IR→PTX mapping, out-of-SSA, layout, and why there is no register allocator
- [docs/validation.md](docs/validation.md): how correctness is established, and the test inventory
- [docs/benchmarks.md](docs/benchmarks.md): methodology and results
- [docs/interview-guide.md](docs/interview-guide.md): likely questions, with answers tied to the code
- [docs/superpowers/specs/2026-10-20-warpforge-design.md](docs/superpowers/specs/2026-10-20-warpforge-design.md): the design document

## Limitations

These are deliberate, following the PRD's "narrow but correct" principle:

- No loops, function calls, division, `>`/`==`, casts, or pointer arithmetic. 1-D `global_id()` only.
- No register allocation, instruction scheduling, CSE or address-mode folding: `ptxas` does all of these.
  The PTX recomputes `i * 4` for each access; `ptxas` removes the duplicates.
- No shared memory, barriers or atomics, so kernels must be race-free. The interpreter detects races and
  out-of-bounds accesses.
- Targets `sm_75` to `sm_90`, each verified with `ptxas` 13.3. `sm_86` (PTX ISA 7.1) is the default. Other
  targets are rejected rather than guessed.

## Layout

```
src/frontend   lexer, parser, AST, sema, diagnostics
src/ir         IR core, builder, printer, verifier, dominators, IRGen, constant evaluator, interpreter
src/passes     pass manager, const-fold, sccp, dce, simplify-cfg
src/codegen    out-of-SSA, PTX emitter
src/driver     command-line driver
tools/         warpforge (CLI) and wf-run (GPU harness)
tests/         unit tests, golden files, diagnostic files, runners
examples/      demo kernels
```

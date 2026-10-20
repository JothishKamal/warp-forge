# Interview guide

Questions an interviewer is likely to ask about WarpForge, with answers grounded in the code. Each answer
starts with the one-sentence version, then the detail you can go into if asked.

---

## Architecture

**Walk me through the pipeline.**
Source → tokens → AST → typed AST → SSA IR → optimized SSA IR → PTX, with a verifier between every IR stage.
- The lexer (`src/frontend/lexer.cpp`) and the recursive-descent parser produce an AST. Sema resolves names
  and assigns types.
- IRGen builds SSA directly (Braun et al.). The pass manager runs `const-fold, sccp, dce, simplify-cfg` to a
  fixpoint, verifying after each pass.
- The backend splits critical edges, turns phis into sequentialized parallel copies, and emits PTX with
  virtual registers.

**Why build your own IR instead of using LLVM?**
The project's point was to implement the core algorithms myself. A small IR with about 13 opcodes is the
smallest thing that still has real SSA, real CFGs and real dominance. Its shape follows LLVM: values with use
lists, blocks, phis first and one terminator. Moving to LLVM later would therefore be a mapping exercise,
not a redesign.

**How do you know the IR is well-formed?**
`ir::verifyFunction` runs after IRGen and after every pass. It checks terminators, phi placement, operand
types, one phi entry per predecessor, and that definitions dominate uses (using Cooper–Harvey–Kennedy
dominators). If a pass breaks the IR, the pipeline stops with a message naming that pass
(`PassManager.VerifierCatchesBrokenPass`).

---

## SSA

**How do you construct SSA? Why not Cytron et al.?**
I use Braun et al. 2013. It builds SSA on the fly during lowering, with no dominance frontiers.
- `readVariable` looks for a local definition. Otherwise it recurses into the predecessors, and inserts a
  phi when there are several.
- Unsealed blocks get incomplete phis, which are filled in at sealing time.
- Trivial phis (all operands the same) are removed immediately, recursively, through the phis that used
  them.

Compared with Cytron, there is no separate dominator, dominance-frontier or renaming pass, and you get
pruned SSA for free. The paper also shows it is fast in practice. Cytron is the right answer if you
already have an IR in non-SSA form.

**Where do phis appear in WarpForge?**
Only at `if` joins (there are no loops), and only for variables that are both reassigned in an arm and read
after the join. `IRGen.TrivialPhiRemoved` checks that an untouched variable gets no phi.

---

## Optimizations

**What does SCCP do that constant folding plus propagation doesn't?**
It reasons about which CFG edges are executable, so constants flowing along impossible paths are ignored.
In `x = 1; if (2 < 1) { x = 5; } use(x)`, folding learns that the branch is `false`, but the phi still
merges 1 and 5. SCCP never marks the edge into `x = 5` executable, so the phi is just 1. `docs/passes.md`
shows the IR after each pass.

**What is the SCCP lattice and why does it terminate?**
It is ⊤ → constant → ⊥, and values only ever move down. Each value can change at most twice, and each edge
becomes executable at most once, so the worklists drain. The analysis is optimistic, starting at ⊤, which is
why it finds more constants than a pessimistic one.

**Why is `x * 0 → 0` legal for `int` but not `float`?**
The language defines `int` overflow as wrap-around, so the identity holds for every value. For floats it
fails for NaN and ±∞, which give NaN, and for negative `x`, which gives `-0.0`. `x + 0.0 → x` fails for
`x = -0.0`. `const_fold.cpp` has the counterexample table, and `ConstFold.NoFloatIdentities` checks that
nothing changes.

**Why do you compute integer folds in `uint32_t`?**
Signed overflow is undefined behaviour in C++. Unsigned arithmetic is defined modulo 2³², which is exactly
two's-complement wrap-around once reinterpreted. The compiler must never have UB of its own while modelling
the target's semantics.

**How can constant folding produce exactly the GPU's float result?**
Three things line up:
1. The folder does one IEEE binary32 operation in host `float`, built with `-ffp-contract=off` and no
   `-ffast-math`.
2. The emitter writes `add.rn.f32` / `mul.rn.f32`. The explicit rounding mode forbids `ptxas` from fusing
   `mul` and `add` into an FMA. The SASS for `saxpy_const` shows `FMUL` + `FADD` and no `FFMA`.
3. The interpreter uses the same evaluator as the folder.

`examples/float_consts.wf` checks −0.0, ∞, subnormals and rounding: GPU results at -O0 match compile-time
folds at -O1, bit for bit.

**Can DCE delete a load? Isn't that unsafe if the load would fault?**
Yes, an unused load is deleted. WarpForge, like C and CUDA, makes out-of-bounds access undefined behaviour,
so the optimizer may assume it doesn't happen, and there is no `volatile` to protect a load. Stores are
roots and are never removed. The interpreter does trap on OOB access, which is how the tests prove their
inputs are well-defined.

**How do you know your optimizations are correct?**
The operational definition is that every well-defined execution writes bit-identical memory before and after
optimization. `Differential.ExamplesO0vsO1` runs every example through the interpreter at -O0 and at -O1
with three seeded inputs and compares bit-for-bit. Beyond that, the GPU runs -O0 and -O1 PTX and compares
both to the -O0 interpreter. Each pass also has its own unit tests.

**How do you know the pipeline terminates?**
Each pass only shrinks the IR, or moves lattice values one way. The fixpoint loop also has a hard cap of 10
rounds. In practice everything converges in 2 rounds, and the second one confirms that nothing changes
(`Pipeline.ReachesFixpointOnSaxpy`).

---

## Code generation

**Why don't you do register allocation?**
PTX is a virtual ISA with unlimited typed registers, and `ptxas` does the real allocation for the specific
GPU, including occupancy trade-offs a front end can't see. nvcc's front end (NVVM) and LLVM's NVPTX backend
also emit virtual registers. Allocating in the front end would just constrain `ptxas`.

**What's the lost-copy problem?**
A phi copy placed at the end of a predecessor that has another successor also runs on the path to that
other successor, which can clobber a live value. The fix is to split every critical edge into a block with
phis, so the copy sits on the edge itself (`splitCriticalEdges`, `OutOfSSA.SplitsCriticalEdge`).

**What's the swap problem?**
The phis at a block's entry read their inputs simultaneously, so one edge's copies form a parallel copy.
Emitting `a ← b; b ← a` in sequence loses the old `a`. `sequentializeParallelCopy` emits a copy only once
its destination is no longer needed as a source. For cycles it saves one destination in a temporary first:
`t ← a; a ← b; b ← t`. Tested on swaps, 3-cycles, chains and fan-out.

**Can that cycle case actually happen in WarpForge?**
Not from source programs. In an acyclic CFG, a phi's destination can't be a source on the same edge: the phi
result would have to be live in the predecessor, so the join would have to dominate it, which needs a loop.
The general algorithm is implemented and unit-tested anyway, because it becomes necessary as soon as loops
or copy coalescing exist.

**Why `cvta.to.global.u64` on the pointer parameters?**
Kernel pointer parameters arrive as generic addresses. Converting once to the global window lets every
access use `ld.global` / `st.global`, which tells the hardware the state space and avoids generic-address
resolution. nvcc emits the same pattern.

**Why `mul.wide.s32` for indexing?**
The index is a 32-bit `int`, but addresses are 64-bit. `mul.wide.s32 %rd, %r, 4` sign-extends and scales in
one instruction, so negative indices are correct before they are added to the base.

**How is `global_id()` lowered?**
Three `mov.u32` instructions from `%ctaid.x`, `%ntid.x` and `%tid.x`, then `mad.lo.s32`. `ptxas` turns that
into `S2R` and `IMAD`, using the constant bank for `%ntid.x`.

**How do you lay out blocks?**
In reverse post-order of a DFS that visits the false successor first. RPO is a topological order, so each
join comes after both of its arms. Visiting the false successor first puts the then-arm right after the
branch, where it falls through. Labels are emitted only for actual branch targets, with nvcc's
`$L__BB<k>_<n>` names.

---

## Validation and the NVIDIA toolchain

**How do you validate the PTX?**
Two independent NVIDIA consumers check it:
1. Standalone `ptxas -arch=sm_86` assembles every golden PTX file (`ptxas_golden`).
2. `wf-run` hands PTX to the driver's JIT with `cuModuleLoadDataEx`, runs it on the RTX 3060, and compares
   every element bit-for-bit with the CPU reference (`gpu_*`, 12 tests).

**What's the "Driver Compiler Interface" you mention?**
It is the path by which a CUDA application ships PTX and lets the installed driver compile it at load time:
`cuModuleLoadDataEx` with JIT options. `wf-run` passes `CU_JIT_ERROR_LOG_BUFFER` / `CU_JIT_INFO_LOG_BUFFER`,
so a bad module fails with the JIT's own diagnostics (`Unknown modifier '.f33'`). `wf-run` loads
`nvcuda.dll` at run time and declares about 20 entry points itself, so it needs no CUDA Toolkit at all.

**What did the benchmarks show? Is your compiler faster than nvcc?**
No, and it isn't meant to be. Both kernels are memory-bound at about 303 GB/s, roughly 90% of DRAM peak. All
variants are within noise. `ptxas` compiles WarpForge -O0 and -O1 to identical SASS, and for
`saxpy_const`, nvcc's too. The -O1 pipeline shrinks the PTX from 35 to 23 instructions, but `ptxas` does
the same optimizations itself. That is an honest result, and it is the division of labour PTX was designed
for (`docs/benchmarks.md`).

**What was the hardest bug?**
There are several good ones:
- An IR teardown use-after-free. Destroying a function's blocks in order let instructions deregister from
  operands that were already destroyed. The fix was `Function::~Function` dropping all references first.
- Nondeterministic output risk: iterating pointer-keyed sets during trivial-phi removal. The fix was
  ordered vectors.
- Git Bash shadowing MinGW's `libstdc++-6.dll` with an older copy, so binaries that used
  `std::filesystem` silently failed to start. The fix was static linking.

---

## Extensions (what would you do next?)

- **Loops.** Braun's sealing already handles back edges. SCCP and DCE work unchanged, and simplify-cfg needs
  care with self-loops. Out-of-SSA would then hit real copy cycles, which the sequentializer already handles.
  Loop-invariant code motion would be a natural next pass.
- **`__restrict__`-style no-alias information**, so `ptxas` can use `ld.global.nc`. That is a front-end fact
  `ptxas` can't derive, and the first place where WarpForge's own optimizations could beat `-O0` on the GPU.
- **CSE/GVN** on `addr` computations: smaller PTX, although `ptxas` already does it.
- **More targets** through `--sm`: the emitter is target-neutral apart from the `.version` table.

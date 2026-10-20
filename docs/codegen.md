# PTX code generation

[`src/codegen/`](../src/codegen) turns SSA IR into a PTX module: one `.visible .entry` per kernel.

## Why there is no register allocator

PTX is a *virtual* ISA. It offers an unlimited supply of typed virtual registers, and `ptxas`, NVIDIA's
backend, does register allocation, scheduling and most low-level optimization for the actual GPU. NVIDIA's
own front ends (NVVM, and LLVM's NVPTX backend) emit virtual registers too. So WarpForge gives every SSA
value its own register and declares the counts up front:

| IR type | PTX class | Declared as |
|---|---|---|
| `i1` | `%p` | `.reg .pred %p<N>;` |
| `i32` | `%r` | `.reg .b32 %r<N>;` |
| `f32` | `%f` | `.reg .f32 %f<N>;` |
| `ptr` | `%rd` | `.reg .b64 %rd<N>;` |

Numbering starts at 1 and follows emission order, so it is deterministic.

## Instruction selection

| IR | PTX |
|---|---|
| `float*` param | `ld.param.u64 %rd1, [k_param_0];` then `cvta.to.global.u64 %rd2, %rd1;`, once per pointer |
| `int` / `float` param | `ld.param.u32` / `ld.param.f32` |
| `global_id` | `mov.u32` from `%ctaid.x`, `%ntid.x`, `%tid.x`; `mad.lo.s32 %r, %ctaid, %ntid, %tid` |
| `add/sub/mul.i32` | `add.s32` / `sub.s32` / `mul.lo.s32` |
| `add/sub/mul.f32` | **`add.rn.f32` / `sub.rn.f32` / `mul.rn.f32`** |
| `neg` | `neg.s32` / **`xor.b32 %f, %f, 0x80000000`** (not `neg.f32`, see below) |
| `lt` | `setp.lt.s32` / `setp.lt.f32` |
| `addr p, i` | `mul.wide.s32 %rd, %i, 4` + `add.s64 %rd, %p, %rd`. A constant index becomes `add.s64 %rd, %p, 4*i`, or no instruction at all for index 0 |
| `load` / `store` | `ld.global.f32` / `st.global.f32` |
| `condbr` | `@%p bra T;`, or `@!%p bra F;` when T is the next block |
| `br` | `bra.uni L;`, omitted when the target is the next block |
| `ret` | `ret;` |
| phi copies | `mov.b32` / `mov.f32` / `mov.b64` |

Details:

- **`.rn` is a correctness choice, not style.** Without a rounding modifier, `ptxas` may contract
  `mul.f32` followed by `add.f32` into one `fma.rn.f32`. That rounds once instead of twice, so the GPU would
  disagree with the constant folder and the interpreter. The explicit `.rn` forbids contraction.
- **Float negation is a sign-bit XOR, not `neg.f32`.** The PTX ISA leaves `neg.f32` of a NaN input
  unspecified, and on sm_86 the two NVIDIA paths disagree. At run time the RTX 3060 returns the canonical
  NaN `0x7FFFFFFF` unchanged, while `ptxas` constant-folds `neg` of a known NaN by flipping the sign
  (`0xFFFFFFFF`). Neither can be matched in every case, so WarpForge emits an exact sign flip:
  `xor.b32 %f2, %f1, 0x80000000`. `.b32` instructions accept `.f32` registers. The IR defines `neg` as
  exactly this, and the constant folder computes it the same way. The GPU tests cover it (`float_consts`,
  out[8]) along with `PTX.FloatNegationIsSignBitXor`.
- **NaN results are canonical.** `add`/`sub`/`mul.rn.f32` produce `0x7FFFFFFF` for every NaN. The folder
  and interpreter do the same, so a NaN folded at -O1 has exactly the bits the GPU computes at -O0.
  `PTX.FloatArithmeticIsRoundToNearest` checks there is no `fma` in the output.
- **Immediates:** floats are written as exact bit patterns (`0f3F800000`), never as decimal. Constant first
  operands of `add`/`mul` are swapped into the second position, which is exact because IEEE add and mul are
  commutative. Other constants that need a register (stores, `setp` left operands) get a `mov`.
- **No CSE:** `vector_add` computes `mul.wide.s32 %rd, %r5, 4` three times, and `ptxas` removes the
  duplicates. WarpForge doesn't try to beat `ptxas` at its own job (see benchmarks).

## Out of SSA

PTX has no phi. Each `%x = phi [%a, P1], [%b, P2]` becomes the copy `x ← a` at the end of `P1` and `x ← b`
at the end of `P2` ([`src/codegen/out_of_ssa.cpp`](../src/codegen/out_of_ssa.cpp)). There are two classic
traps, described by Briggs, Cooper, Harvey and Simpson (1998).

**The lost-copy problem.** Suppose `P1` has two successors and one of them has phis. A copy at the end of
`P1` then runs on *both* paths and can clobber a value the other path needs. The fix is to split each
**critical edge** (from a block with several successors to a block with several predecessors) into an empty
block that holds the copies. WarpForge splits only critical edges into blocks *with phis*, since no other
edge needs copies (`OutOfSSA.SplitsCriticalEdge`).

**The swap problem.** All phis at the top of a block read their inputs *simultaneously*, so the copies on
one edge are a **parallel copy**. Emitting `{a ← b, b ← a}` one move at a time loses a value.
`sequentializeParallelCopy` does the following:

1. It emits any copy whose destination no other pending copy still reads. In a chain `{b ← a, c ← b}`, it
   emits `c ← b` before `b ← a`.
2. When only cycles remain, it saves one destination in a fresh temporary, redirects that destination's
   readers to the temporary, and continues. A swap therefore becomes `t ← a; a ← b; b ← t`.

This is the construction in Boissinot et al., *Revisiting Out-of-SSA Translation* (CGO 2009). The
sequentializer is unit-tested on swaps, 3-cycles, chains, fan-out and self-copies (`OutOfSSA.*`).
`examples/swap.wf` checks on the GPU that both edges of a conditional swap get the right copies, including
the copies placed on the split critical edge. Its parallel copies contain no cycle, so no temporary is
emitted; see the note below.

```ptx
	setp.lt.s32 	%p2, %r5, %r6;
	@!%p2 bra 	$L__BB0_3;
	mov.f32 	%f3, %f2;        // swapped edge:   a' = b, b' = a
	mov.f32 	%f4, %f1;
	bra.uni 	$L__BB0_4;
$L__BB0_3:                       // split critical edge
	mov.f32 	%f3, %f1;        // unswapped edge: a' = a, b' = b
	mov.f32 	%f4, %f2;
$L__BB0_4:
```

(In an acyclic CFG, phi destinations are never sources on the same edge, so real cycles can't arise from
WarpForge programs today. The sequentializer is still the general algorithm and is unit-tested on cycles.)

## Block layout

Blocks are laid out in **reverse post-order of a DFS that visits the false successor first**. RPO is a
topological order of the acyclic CFG, so a join always comes after both of its arms. Visiting the false
successor first means the true successor lands right after the branch, so the then-arm falls through. Labels
are emitted only for blocks some branch actually jumps to, and they use nvcc's naming, `$L__BB<kernel>_<block>`.

## Module header

```ptx
//
// Generated by WarpForge 0.1.0
//

.version 7.1
.target sm_86
.address_size 64
```

`.version` defaults to the lowest PTX ISA version that introduced the target (`sm_86` → 7.1, `sm_89`/`sm_90`
→ 7.8). `--sm` accepts only targets whose output has been assembled with `ptxas` 13.3: `sm_75`, `sm_80`,
`sm_86`, `sm_87`, `sm_89` and `sm_90`. Anything else is a usage error. Guessing a `.version` for an unknown
target would produce PTX that `ptxas` refuses, and CUDA 13 no longer supports sm_50–sm_72 at all.
`--ptx-version` overrides the version. The output contains no timestamps or paths, so the same input and
flags give byte-identical PTX.

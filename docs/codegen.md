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
| `neg` | `neg.s32` / `neg.f32` |
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

This is the construction in Boissinot et al., *Revisiting Out-of-SSA Translation* (CGO 2009). It is tested
on swaps, 3-cycles, chains, fan-out and self-copies (`OutOfSSA.*`), and end-to-end on the GPU with
`examples/swap.wf`:

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
→ 7.8). `--sm` and `--ptx-version` override it. The output contains no timestamps or paths, so the same
input and flags give byte-identical PTX.

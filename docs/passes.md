# Optimization passes

All passes implement `bool run(ir::Function&)`, which returns **whether the IR changed**
([`src/passes/pass.hpp`](../src/passes/pass.hpp)).

`-O1` runs `const-fold → sccp → dce → simplify-cfg` and repeats the whole sequence until a round changes
nothing (fixpoint), capped at 10 rounds. The verifier runs after every pass. `-O0` runs no passes.
`--passes=a,b` runs exactly those passes, once each.

```
$ warpforge examples/saxpy_const.wf --print-after-all --dump-ir
; *** IR after const-fold on @saxpy_const (changed) ***
; *** IR after sccp on @saxpy_const (changed) ***
; *** IR after dce on @saxpy_const (changed) ***
; *** IR after simplify-cfg on @saxpy_const (changed) ***
; *** IR after const-fold on @saxpy_const (unchanged) ***     <- second round: fixpoint
...
```

## What "safe" means

A transformation is allowed only if, for every *well-defined* execution (in-bounds and race-free, see
[language.md](language.md)), the program writes **bit-identical** memory before and after.
`tests/unit/test_differential.cpp` enforces this: every example kernel runs on the reference interpreter at
-O0 and at -O1 with three seeded input sets, and the output buffers are compared bit-for-bit.

`const-fold` and `sccp` evaluate constants with the same function, `ir::evaluate`
([`src/ir/const_eval.cpp`](../src/ir/const_eval.cpp)), which the interpreter also uses, so they can never
disagree:

- `i32` arithmetic is done in `uint32_t`: wrap-around, with no host undefined behaviour.
- `f32` arithmetic is one host `float` operation, built with `-ffp-contract=off`. That matches PTX
  `add.rn`/`mul.rn` bit-for-bit, including −0.0, NaN, ∞ and subnormals (`ConstEval.FloatEdgeCases`).

## const-fold

Local and per instruction ([`src/passes/const_fold.cpp`](../src/passes/const_fold.cpp)):

1. If all operands are constants, replace the instruction with the computed constant.
2. For `i32` only, apply identities that hold for every value: `x+0`, `0+x`, `x-0`, `x*1` and `1*x` become
   `x`, while `x*0`, `0*x` and `x-x` become `0`.

There are **no float identities**, because none is safe in IEEE-754:

| Rewrite | Counterexample |
|---|---|
| `x + 0.0 → x` | `x = -0.0`: `-0.0 + 0.0 = +0.0` |
| `x * 0.0 → 0.0` | `x = NaN` or `±∞` gives NaN; `x = -1` gives `-0.0` |
| `x - x → 0.0` | `x = NaN` or `∞` gives NaN |

`ConstFold.NoFloatIdentities` checks that the pass leaves these alone and reports "unchanged".

## sccp: sparse conditional constant propagation

Wegman & Zadeck, *Constant Propagation with Conditional Branches*, TOPLAS 1991
([`src/passes/sccp.cpp`](../src/passes/sccp.cpp)).

Each value starts optimistically at **⊤** (no evidence yet) and can only move down to a single constant, then
to **⊥** (not constant). Two worklists drive the analysis:

- **CFG edges** that just became executable. Their target's phis are re-evaluated, and the first time a
  block is reached, all its instructions are visited.
- **SSA values** whose lattice value changed. Their users are re-evaluated.

A phi is the meet of its incoming values over **executable edges only**. A `condbr` on a constant marks only
one edge executable. This is what local folding cannot do:

```c
int x = 1;
if (2 < 1) { x = 5; }
o[i + x] = 1.0;
```

<table><tr><th>-O0</th><th><code>--passes=const-fold</code></th><th><code>--passes=sccp</code></th></tr><tr><td>

```llvm
entry:
  %i = global_id
  %0 = lt.i32 2, 1
  condbr %0, if.then, if.end
if.then:
  br if.end
if.end:
  %x = phi.i32 [1, entry],
               [5, if.then]
  %1 = add.i32 %i, %x
  ...
```

</td><td>

```llvm
entry:
  %i = global_id
  condbr false, if.then, if.end
if.then:
  br if.end
if.end:
  %x = phi.i32 [1, entry],
               [5, if.then]
  %0 = add.i32 %i, %x
  ...
```

</td><td>

```llvm
entry:
  %i = global_id
  br if.end
if.then:
  br if.end
if.end:
  %0 = add.i32 %i, 1
  ...
```

</td></tr></table>

Const-fold learns that the branch is `false` but keeps the phi, because it doesn't reason across edges. SCCP
never marks `entry → if.then` executable, so the phi only sees `1`. After rewriting, constant-valued
instructions are replaced, and constant `condbr`s become `br`. The not-taken successor loses its phi entry
for this block. Unreachable blocks are left for `simplify-cfg`.

## dce: dead-code elimination

Mark-and-sweep ([`src/passes/dce.cpp`](../src/passes/dce.cpp)). Roots are stores and terminators. Marking
follows operands, and everything unmarked is deleted. Because liveness flows backwards from roots, a dead
cycle of phis feeding each other is removed too, which "delete values with no users" would miss.

**Loads are not roots.** An unused load is deleted. That is sound because WarpForge has no `volatile`, and an
out-of-bounds load is undefined behaviour anyway (`DCE.RemovesUnusedLoad`). Stores are never removed
(`DCE.KeepsStores`).

## simplify-cfg

[`src/passes/simplify_cfg.cpp`](../src/passes/simplify_cfg.cpp) applies these rewrites until none matches:

1. Delete blocks unreachable from the entry, after removing their entries from successors' phis.
2. `condbr %c, B, B` → `br B`.
3. Remove phis whose incoming values are all the same.
4. Merge a block into its only predecessor when that predecessor has no other successor.
5. Bypass an empty block (`br T` only) when `T` has no phis. With phis, each edge's incoming value would need
   rewriting, so that case is skipped.

Continuing the SCCP example with `--passes=sccp,simplify-cfg`:

```llvm
entry:
  %i = global_id
  %0 = add.i32 %i, 1
  %1 = addr %o, %0
  store.f32 0x3F800000, %1
  ret
```

## Tests

| Pass | Tests (in `tests/unit/`) |
|---|---|
| const-fold | `ConstFold.*`: arithmetic, integer identities, exact float folds, *no* float identities, `i1` folding, unchanged report |
| sccp | `SCCP.*`: through phis, branch folding, unexecutable arm, nested folded branches, non-constants stay ⊥ |
| dce | `DCE.*`: keeps stores, dead chains, unused loads, dead phis |
| simplify-cfg | `SimplifyCFG.*`: unreachable + merge, phi repair, real diamonds untouched, empty arms |
| pipeline | `Pipeline.ReachesFixpointOnSaxpy`, `PassManager.*` (verifier catches a broken pass, printing, unknown pass) |
| all | `Differential.ExamplesO0vsO1` |

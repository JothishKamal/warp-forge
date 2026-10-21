# Optimization passes

| Pass | One line | Page |
|---|---|---|
| `const-fold` | Fold constant operations, plus `i32`-only identities; no float identities | [passes/const-fold.md](passes/const-fold.md) |
| `sccp` | Wegman–Zadeck sparse conditional constant propagation, which also folds constant branches | [passes/sccp.md](passes/sccp.md) |
| `dce` | Mark-sweep from stores and terminators; unused loads are deleted | [passes/dce.md](passes/dce.md) |
| `simplify-cfg` | Delete unreachable blocks, merge chains, drop redundant phis | [passes/simplify-cfg.md](passes/simplify-cfg.md) |

## Pass manager

Every pass implements `bool run(ir::Function&)` and returns **whether the IR changed**
([`src/passes/pass.hpp`](../src/passes/pass.hpp)).

- `-O1` runs `const-fold → sccp → dce → simplify-cfg` and repeats the sequence until a whole round changes
  nothing (fixpoint), with a cap of 10 rounds.
- `-O0` runs no passes.
- `--passes=a,b` runs exactly those passes, once each.
- The verifier runs after **every** pass, and a failure names the pass that broke the IR.

```
$ warpforge examples/saxpy_const.wf --print-after-all --dump-ir
; *** IR after const-fold on @saxpy_const (changed) ***
; *** IR after sccp on @saxpy_const (changed) ***
; *** IR after dce on @saxpy_const (changed) ***
; *** IR after simplify-cfg on @saxpy_const (changed) ***
; *** IR after const-fold on @saxpy_const (unchanged) ***     <- second round: fixpoint
...
```

To test a pass in isolation, hand it IR directly: `warpforge x.wfir --passes=sccp --dump-ir`
([ir.md](ir.md#parsing-it-back)).

## What "safe" means

A transformation is allowed only if, for every *well-defined* execution (in-bounds and race-free, see
[language.md](language.md)), the program writes **bit-identical** memory before and after.

`const-fold`, `sccp` and the reference interpreter share one definition of arithmetic, `ir::evaluateBits`
([`src/ir/const_eval.cpp`](../src/ir/const_eval.cpp)), so they can never disagree with each other. Agreeing
with the *GPU* also needs every rule below to match the hardware:

- **`i32`:** computed in `uint32_t`, so it wraps around with no host undefined behaviour.
- **`f32`:** one host `float` operation, built with `-ffp-contract=off`, matching `add.rn`/`sub.rn`/
  `mul.rn.f32` bit-for-bit for −0.0, ∞ and subnormals.
- **NaN:** results are canonicalized to PTX's `0x7FFFFFFF`, where x86 SSE would give `0xFFC00000`.
  Negation is an exact sign-bit flip, which the backend emits as `xor.b32`.

The final code review found the NaN rules missing, by fuzzing against the GPU. Before the fix, `inf - inf`
folded at -O1 stored different bits from what the GPU computed at -O0.

## How it's tested

| Level | What |
|---|---|
| Unit tests | `ConstFold.*`, `SCCP.*`, `DCE.*`, `SimplifyCFG.*`, `Pipeline.*`, `PassManager.*` |
| Single pass on hand-written IR | `tests/ir/*.wfir` + `.expected`, with expectations written from the algorithm |
| Differential | `Differential.ExamplesO0vsO1` and `RandomPrograms.OptimizerPreservesSemantics` (500 generated programs) on the interpreter |
| Against the GPU | `gpu_fuzz`: 150 random programs at -O0 and -O1, through `ptxas` and `wf-run --check`. This is the level that can see host-vs-GPU semantic differences |

# const-fold: constant folding

Source: [`src/passes/const_fold.cpp`](../../src/passes/const_fold.cpp). Shared evaluator:
[`src/ir/const_eval.cpp`](../../src/ir/const_eval.cpp).

## What it does

It works locally, one instruction at a time, in block order. For each `add`, `sub`, `mul`, `neg` or `lt`:

1. **All operands constant:** compute the result with `ir::evaluate` and replace the instruction with that
   constant (`replaceAllUsesWith`, then erase).
2. **Otherwise, `i32` only:** apply identities that hold for *every* 32-bit value under wrap-around
   arithmetic.

   | Pattern | Result |
   |---|---|
   | `x + 0`, `0 + x`, `x - 0`, `x * 1`, `1 * x` | `x` |
   | `x * 0`, `0 * x`, `x - x` | `0` |

Because it visits instructions in order and rewrites uses immediately, chains collapse in one pass:
`(2 + 3) * 4` folds `5` first, then `20`.

## Why there are no float identities

None of the integer identities is safe in IEEE-754 binary32:

| Rewrite | Counterexample |
|---|---|
| `x + 0.0 → x` | `x = -0.0`: `-0.0 + 0.0 = +0.0` |
| `x * 0.0 → 0.0` | NaN or ±∞ gives NaN; `x = -1.0` gives `-0.0` |
| `x - x → 0.0` | NaN or ∞ gives NaN |
| `x * 1.0 → x` | `x` = any NaN other than `0x7FFFFFFF`: the GPU's `mul` returns the canonical NaN, while `x` keeps its own bits |

Floats are only folded when every operand is a known constant, and even then the evaluator must reproduce
the GPU exactly:

- **Rounding:** each operation is one host `float` operation, built with `-ffp-contract=off` so `a*b+c` is
  never fused. That matches `add.rn`/`sub.rn`/`mul.rn.f32`.
- **NaN:** results are flushed to PTX's canonical NaN `0x7FFFFFFF`. The host's SSE unit would give
  `0xFFC00000`.
- **Negation:** an exact sign-bit flip, which is also how the backend emits it (`xor.b32`).
- **Integers:** computed in `uint32_t`, so overflow wraps with no host undefined behaviour.

## Worked example

[`tests/ir/const_fold_no_float_identities.wfir`](../../tests/ir/const_fold_no_float_identities.wfir),
run with `warpforge x.wfir --passes=const-fold --dump-ir`:

<table><tr><th>before</th><th>after</th></tr><tr><td>

```llvm
%v = load.f32 %o
%a = add.f32 %v, 0x00000000
%b = mul.f32 %v, 0x00000000
%c = sub.f32 %v, %v
%ia = add.i32 %n, 0
%ib = mul.i32 %n, 0
%ic = sub.i32 %n, %n
%sum = add.i32 %ia, %ib
%idx = add.i32 %sum, %ic
%p = addr %o, %idx
...
```

</td><td>

```llvm
%v = load.f32 %o
%a = add.f32 %v, 0x00000000
%b = mul.f32 %v, 0x00000000
%c = sub.f32 %v, %v
%p = addr %o, %n
...
```

</td></tr></table>

The three float look-alikes survive, while the five integer instructions collapse to `%n`.

## Complexity

O(instructions × operands): a single forward walk. The pass doesn't iterate internally; the -O1 fixpoint
loop reruns it if a later pass exposes more constants.

## Limits (by design)

It doesn't look through phis or across branches. A phi of two equal constants, or a value that is only
constant on the executable path, is SCCP's job. See [sccp.md](sccp.md) for the example `const-fold` can't
handle.

## Tests

- `ConstFold.IntArith`, `IntIdentities`, `FloatFoldsExactly`, `NoFloatIdentities`, `ComparisonFoldsToI1`
  and `ReportsUnchanged`, in `tests/unit/test_passes.cpp`.
- `ConstEval.IntWrap`, `ConstEval.FloatEdgeCases`: exact bits for −0.0, ∞, subnormals and canonical NaN.
- `tests/ir/const_fold_no_float_identities.wfir`.
- On the GPU: `examples/float_consts.wf` at -O1 (folded) and -O0 (computed by the GPU) must match bit for
  bit.

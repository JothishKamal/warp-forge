# sccp: sparse conditional constant propagation

Source: [`src/passes/sccp.cpp`](../../src/passes/sccp.cpp). Algorithm: M. N. Wegman and F. K. Zadeck,
*Constant Propagation with Conditional Branches*, ACM TOPLAS 13(2), 1991.

## The lattice

Every SSA value has a lattice value that only ever moves **down**:

```
           ⊤            no evidence yet (optimistic start)
   c1   c2   c3 ...     always this constant
           ⊥            not a constant / known only at run time
```

- **meet:** `meet(⊤, x) = x`. `meet(c, c) = c`. `meet(c1, c2) = ⊥` when `c1 ≠ c2`. `meet(⊥, x) = ⊥`.
  Constants are interned, so `c1 == c2` is a pointer comparison.
- **Starting values:** arguments, `global_id`, `addr` and `load` start at ⊥, because they are known only at
  run time. Constants are their own value. Everything else starts at ⊤.

## The two worklists

1. **CFG edges.** When an edge `P → B` first becomes executable, re-evaluate `B`'s phis. When `B` itself is
   reached for the first time, visit all its instructions.
2. **SSA values.** When a value's lattice value drops, re-evaluate its users, if their block is executable.

Visiting an instruction works like this:

| Instruction | New value |
|---|---|
| `phi` | meet of incoming values over **executable incoming edges only** |
| arithmetic, `lt` | any operand ⊥ → ⊥; any operand ⊤ → stay ⊤ (wait); all constant → `ir::evaluate` |
| `br T` | mark edge → T executable |
| `condbr c, T, F` | `c` constant → mark only the taken edge; `c` ⊥ → mark both; `c` ⊤ → mark neither yet |

**Termination:** each value drops at most twice (⊤ → c → ⊥), and each edge becomes executable once, so the
worklists drain. The total work is O(edges + SSA uses × height of lattice).

## Rewriting

After the analysis:

- Every instruction whose value is a constant is replaced by that constant and erased.
- Every `condbr` on a constant becomes `br taken`. The not-taken successor loses this block as a
  predecessor, so its phis drop the matching entry, which keeps the verifier happy.
- Blocks that were never reached stay in place. Deleting them is [simplify-cfg](simplify-cfg.md)'s job.

## Why "conditional" matters

[`tests/ir/sccp_unexecutable_arm.wfir`](../../tests/ir/sccp_unexecutable_arm.wfir):

<table><tr><th>before</th><th><code>--passes=sccp</code></th></tr><tr><td>

```llvm
entry:
  %i = global_id
  %c = lt.i32 7, 3
  condbr %c, dead, join
dead:
  br join
join:
  %x = phi.i32 [1, entry], [5, dead]
  %s = add.i32 %i, %x
  ...
```

</td><td>

```llvm
entry:
  %i = global_id
  br join
dead:
  br join
join:
  %s = add.i32 %i, 1
  ...
```

</td></tr></table>

`7 < 3` is false, so the edge into `dead` never becomes executable, and the phi only meets `1`.
Const-fold alone would fold the comparison but keep the phi of 1 and 5. A pessimistic propagator, starting
from ⊥, would also give up on the phi.

The opposite case, [`tests/ir/sccp_both_arms_agree.wfir`](../../tests/ir/sccp_both_arms_agree.wfir), has
both arms executable, but both produce 6 (one of them via `mul.i32 2, 3`). The phi meets `6` and `6` and
stays constant.

## Tests

- `SCCP.PropagatesThroughPhi`, `FoldsBranch`, `IgnoresUnexecutableArm`, `ChainedBranchesFold`,
  `NonConstStaysBottom` and `MixedPhiIsBottom`, in `tests/unit/test_passes.cpp`.
- `tests/ir/sccp_*.wfir`.
- Indirectly, every differential and GPU test, since SCCP runs in every -O1 pipeline.

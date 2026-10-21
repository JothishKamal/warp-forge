# simplify-cfg: CFG cleanup

Source: [`src/passes/simplify_cfg.cpp`](../../src/passes/simplify_cfg.cpp).

SCCP turns constant branches into `br`, but it leaves the dead blocks behind. IRGen makes small blocks for
every `if`. This pass repeats the rewrites below until none applies:

| # | Rewrite | Care needed |
|---|---|---|
| 1 | Delete blocks unreachable from the entry | First remove their entries from successors' phis; drop references across all dead blocks before erasing |
| 2 | `condbr %c, B, B` → `br B` | — |
| 3 | Remove phis whose incoming values are all the same value | RAUW with that value |
| 4 | Merge block B into its only predecessor P, when P's only successor is B | B's phis have one incoming value: forward it. B's successors' phis now name P as the incoming block |
| 5 | Bypass an empty block (just `br T`) when `T` has no phis | With phis, each redirected edge would need its own incoming value, so that case is skipped |

None of these changes what the program computes; they only shrink the CFG. The result is fewer labels and
branches in the PTX, and IR that is easier to read.

## Worked example

[`tests/ir/simplify_cfg_chain.wfir`](../../tests/ir/simplify_cfg_chain.wfir), run with
`--passes=simplify-cfg`:

<table><tr><th>before</th><th>after</th></tr><tr><td>

```llvm
entry:
  br b1
orphan:
  br b2
b1:
  %v = load.f32 %o
  br b2
b2:
  %x = phi.f32 [%v, b1], [0x3F800000, orphan]
  store.f32 %x, %o
  ret
```

</td><td>

```llvm
entry:
  %v = load.f32 %o
  store.f32 %v, %o
  ret
```

</td></tr></table>

1. `orphan` is unreachable. Rule 1 deletes it and drops its phi entry.
2. The phi now has a single incoming value. Rule 3 replaces it with `%v`.
3. Rule 4 merges `b1` and then `b2` into `entry`.

## Interaction with the backend

The PTX emitter lays blocks out in reverse post-order, then-arm first, and omits branches to the next
block. Whatever simplify-cfg removes never reaches the PTX. In `saxpy_const`, the always-false
`if (lanes < 0)` leaves no trace: no label, no branch, no store. Only the real `i < n` bounds check remains.
See [codegen.md](../codegen.md).

## Tests

- `SimplifyCFG.RemovesUnreachableAndMerges`, `FixesPhisOfRemovedPredecessor`, `KeepsRealDiamond` and
  `EmptyThenArmIsMergedAway`, in `tests/unit/test_dce_cfg.cpp`.
- `tests/ir/simplify_cfg_chain.wfir`.

# dce: dead-code elimination

Source: [`src/passes/dce.cpp`](../../src/passes/dce.cpp).

## Algorithm: mark and sweep

1. **Roots** are instructions that matter on their own:
   - `store`, whose effect is visible outside the kernel.
   - Terminators (`br`, `condbr`, `ret`), which decide what runs.
2. **Mark:** starting from the roots, follow operands transitively, marking each defining instruction live.
3. **Sweep:** delete every unmarked instruction, phis included. This happens in two phases. First drop the
   operand references of *all* dead instructions, because they may use each other. After that, none of them
   has a user left, so each can be erased safely.

Liveness flows backwards from the roots. That removes things a naive "delete values with no users" loop
misses, such as two dead phis that keep each other alive. Complexity is O(instructions + uses).

## Why deleting an unused load is sound

Loads are **not** roots, so an unused `load` is deleted. Removing a memory access looks dangerous: what if it
would have faulted? It is sound because of the language's rules ([language.md](../language.md)):

- An out-of-bounds access is **undefined behaviour**, so a program that would fault has no defined result
  to preserve.
- There is no `volatile`, so a load has no observable effect besides its value.

Stores are always roots, so DCE can never remove one.

## Worked example

[`tests/ir/dce_keeps_store_chain.wfir`](../../tests/ir/dce_keeps_store_chain.wfir), run with
`--passes=dce`:

<table><tr><th>before</th><th>after</th></tr><tr><td>

```llvm
%i = global_id
%dead1 = mul.i32 %i, 4
%dead2 = add.i32 %dead1, %n
%p = addr %o, %i
%unused = load.f32 %p
%v = load.f32 %p
%w = mul.f32 %v, 0x40000000
store.f32 %w, %p
ret
```

</td><td>

```llvm
%i = global_id
%p = addr %o, %i
%v = load.f32 %p
%w = mul.f32 %v, 0x40000000
store.f32 %w, %p
ret
```

</td></tr></table>

The store's operand chain `%w → %v → %p → %i` survives. The arithmetic chain and the unused load go.

## Tests

- `DCE.KeepsStores`, `RemovesDeadChain`, `RemovesUnusedLoad` and `RemovesDeadPhi`, in
  `tests/unit/test_dce_cfg.cpp`.
- `tests/ir/dce_keeps_store_chain.wfir`.

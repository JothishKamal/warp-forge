# WarpForge IR

An SSA intermediate representation in the style of LLVM IR, cut down to what the language needs.
Source: [`src/ir/ir.hpp`](../src/ir/ir.hpp).

## Structure

```
Module ── Function (one per kernel)
            ├── Arguments            kernel parameters
            ├── Constants            interned: pointer equality == value equality
            └── BasicBlocks          first block = entry
                  └── Instructions   phis first, exactly one terminator last
```

- **SSA:** each instruction result is defined once, and every operand points directly at its defining
  `Value`. There are no names and no symbol table.
- **Use lists:** every value knows which instructions use it. This keeps `replaceAllUsesWith`, DCE and
  SCCP's SSA worklist cheap.
- **Types:** `i1` (condition), `i32`, `f32`, and `ptr` (64-bit global address of an `f32`).

## Instructions

| Opcode | Example | Notes |
|---|---|---|
| `add` `sub` `mul` | `%x = add.i32 %a, %b` | typed by operand: `.i32` or `.f32` |
| `neg` | `%x = neg.f32 %a` | separate opcode, because `0.0 - x` ≠ `-x` when `x = +0.0` |
| `lt` | `%p = lt.f32 %a, %b` | result `i1`; signed for i32, ordered for f32 |
| `global_id` | `%i = global_id` | `i32` |
| `addr` | `%q = addr %p, %i` | `p + sext(i) * 4`, `ptr` |
| `load` | `%v = load.f32 %q` | |
| `store` | `store.f32 %v, %q` | the only side effect |
| `phi` | `%x = phi.i32 [%a, bb1], [%b, bb2]` | one entry per predecessor |
| `br` | `br bb` | |
| `condbr` | `condbr %p, then, else` | |
| `ret` | `ret` | |

## Text format

```llvm
kernel @vector_add(ptr %a, ptr %b, ptr %c, i32 %n) {
entry:
  %i = global_id
  %0 = lt.i32 %i, %n
  condbr %0, if.then, if.end

if.then:
  %1 = addr %a, %i
  %2 = load.f32 %1
  ...
  store.f32 %5, %6
  br if.end

if.end:
  ret
}
```

- Value names come from source variables where there is one (`%i`), with duplicates numbered (`%x.1`).
  Everything else is numbered in order of appearance.
- Constants print inline: `i32` in decimal, `f32` as its exact bit pattern (`0x3F800000` is 1.0), so the text
  is lossless. `i1` prints as `true`/`false`.
- **Deterministic:** names are assigned at print time from the IR's structure alone. Printing the same IR
  twice, or compiling the same source twice, gives identical bytes. The golden-file suite checks this.

### Parsing it back

The text format is also an input. [`src/ir/ir_parser.cpp`](../src/ir/ir_parser.cpp) reads it, and
`warpforge x.wfir` (or `--input-ir`) runs the pipeline on it directly, skipping the front end:

```sh
warpforge tests/ir/sccp_unexecutable_arm.wfir --passes=sccp --dump-ir
```

- **Two phases.** First every block and instruction is created without operands. Result types are fully
  determined by the spelling (`add.i32`, `lt.f32` → `i1`, `addr` → `ptr`), so each value exists, correctly
  typed, before any operand is read. Then operands and branch targets are resolved by name. Forward
  references, such as a phi naming a value from a later block, need no special handling.
- **Comments:** lines starting with `;` are comments, so `--print-after-all` output can be fed straight back
  in.
- **Errors:** syntax errors are reported as `line:col: message`. Semantic errors (types, dominance, phi
  entries) are left to the verifier, so tests can hand it deliberately broken IR.
- **Round trip:** `print(parse(print(M))) == print(M)` holds for every example at -O0 and -O1
  (`IRParser.RoundTripsEveryExampleAtO0AndO1`).
- **Pass tests:** `tests/ir/*.wfir` hold hand-written IR with a `; passes: …` header. `tests/run_ir_tests.py`
  compares the result with the matching `.expected` file, in a "given this IR, expect that IR" style, like
  LLVM's `opt` tests.

## Verifier

[`src/ir/verifier.cpp`](../src/ir/verifier.cpp) runs after IRGen and after every pass, by default.
It checks:

1. Every block ends in exactly one terminator, and phis come first.
2. Branch targets belong to the function, and the entry block has no predecessors.
3. Operand counts and types are right for each opcode.
4. Each phi has exactly one entry per predecessor, and none for a block that isn't a predecessor.
5. **Definitions dominate uses.** For phis, the definition must dominate the end of the incoming block.
   Dominators are computed with Cooper, Harvey and Kennedy's *A Simple, Fast Dominance Algorithm* (2001):
   iterate over blocks in reverse post-order, and intersect the idoms of each block's processed
   predecessors by walking up the partial tree.

Uses in unreachable blocks are not dominance-checked. A pass may leave such blocks for `simplify-cfg`.
A failure names the pass that broke the IR:

```
IR verification failed after pass 'break-it' on @k:
  in @k: block 'orphan' does not end in a terminator
```

## Construction: Braun et al.

IRGen ([`src/ir/irgen.cpp`](../src/ir/irgen.cpp)) builds SSA directly while it lowers the AST. It follows
Braun, Buchwald, Hack, Leißa, Mallon and Zwinkau, *Simple and Efficient Construction of Static Single
Assignment Form* (CC 2013), rather than Cytron's dominance-frontier method:

- `writeVariable(v, block, value)` records an assignment.
- `readVariable(v, block)` returns the local definition if there is one. Otherwise it asks the
  predecessors: one predecessor needs no phi, while several get a phi whose operands are their answers.
- Unsealed blocks (predecessors not all known yet) get *incomplete* phis, which are filled in when the block
  is sealed. WarpForge has no loops, so blocks are sealed as soon as their predecessors exist. The
  mechanism is kept anyway, so the code matches the paper and would extend to loops.
- `tryRemoveTrivialPhi` removes a phi whose operands are all the same value, or the phi itself. It then
  re-checks phis that used it, so a variable not reassigned in either arm gets no phi at the join.

Phis are created only when a variable is actually read at a join, so construction is on demand.

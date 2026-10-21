# The WarpForge language

WarpForge source files (`.wf`) contain one or more GPU kernels written in a small, deliberately frozen subset
of C. Anything outside this page is rejected with a diagnostic. The language does not grow.

```c
kernel vector_add(float* a, float* b, float* c, int n) {
    int i = global_id();
    if (i < n) {
        c[i] = a[i] + b[i];
    }
}
```

## Grammar

```
program     = { kernel } EOF ;
kernel      = "kernel" IDENT "(" [ param { "," param } ] ")" block ;
param       = type IDENT ;
type        = "int" | "float" | "float" "*" ;
block       = "{" { stmt } "}" ;
stmt        = decl | assign | store | if | return | block ;
decl        = ("int" | "float") IDENT "=" expr ";" ;      (* initializer required *)
assign      = IDENT "=" expr ";" ;
store       = IDENT "[" expr "]" "=" expr ";" ;
if          = "if" "(" expr ")" block [ "else" ( block | if ) ] ;
return      = "return" ";" ;
expr        = additive [ "<" additive ] ;                  (* '<' only valid as an if condition *)
additive    = term { ( "+" | "-" ) term } ;
term        = unary { "*" unary } ;
unary       = "-" unary | primary ;
primary     = INT_LIT | FLOAT_LIT | IDENT | IDENT "[" expr "]"
            | "global_id" "(" ")" | "(" expr ")" ;
```

Lexical rules:

- `INT_LIT` is decimal digits with a value ≤ 2147483647. As in C, `-2147483648` can't be written directly.
- `FLOAT_LIT` is `1.5`, `1.5f`, `2e3`, `1.0e-2f`, …, always binary32, rounded to nearest from the decimal text.
  A literal too large for binary32 (above ~3.4028235e38) is an error rather than a silent `inf`. Tiny literals
  that round to a subnormal or to zero are allowed.
- Keywords: `kernel int float if else return global_id`.
- Comments: `// …` and `/* … */`.

## Types and semantics

| Type | Meaning |
|---|---|
| `int` | 32-bit two's-complement integer. **Overflow wraps.** This deliberately differs from C, where signed overflow is undefined. |
| `float` | IEEE-754 binary32 |
| `float*` | Pointer to `float` in global memory. **Kernel parameters only**: no pointer locals, no pointer arithmetic. |

- **No implicit conversions.** `int + float` is an error. Both operands of `+ - * <` must have the same type.
- **`<`** works on `int`/`int` or `float`/`float`, and may only be used as an `if` condition. Float `<` is
  ordered, so it is false if either side is NaN.
- **`global_id()`** is `blockIdx.x * blockDim.x + threadIdx.x` (1-D).
- **Scopes:** each block is a scope. Shadowing an outer name is allowed. Redeclaring a name in the same scope
  is not, and the kernel's parameters share a scope with the body's top level.
- **`return;`** ends the thread. Statements after it in the same block are never executed or compiled.
- **Float arithmetic:** each `+ - *` is one IEEE operation, rounded to nearest-even. There is no FMA
  contraction, no reassociation, no flush-to-zero, and no algebraic simplification. `x + 0.0` is not `x`
  (consider `x = -0.0`), and `x * 0.0` is not `0.0` (consider NaN, ∞ and `-1.0`).

### Well-defined execution

As in C and CUDA, a kernel has defined behaviour only if:

1. every executed load and store is in bounds, and
2. no element is stored by one thread and stored or loaded by another (no data races).

The optimizer may assume both. The reference interpreter checks both and reports a violation instead of
running on, for example `out-of-bounds store at index 2 of %o (size 2) in thread 2` or
`data race on %o element 0 between threads 0 and 1`.

## Diagnostics

Format: `path:line:col: error: message`, followed by the source line and a caret. Columns are 1-based, in
bytes. The parser recovers at `;` and `}`, so several errors are reported per run, capped at 20. Exit codes:
0 success, 1 compile error, 2 usage error, 3 I/O error.

| Situation | Message |
|---|---|
| Undefined name | `undefined variable 'y'` |
| Same-scope redeclaration | `redeclaration of 'x' in the same scope` |
| Mixed operand types | `invalid operands to '+': float and int` |
| Wrong initializer type | `cannot initialize 'x' of type int with a value of type float` |
| Wrong assignment type | `cannot assign a value of type float to 'x' of type int` |
| Indexing a scalar | `cannot index non-pointer 'n'` |
| Non-int index | `index must be int, got float` |
| Storing an int | `stored value must be float, got int` |
| `<` used as a value | `comparison result can only be used as an if condition` |
| `if (x)` | `if condition must be a comparison (e.g. 'i < n')` |
| Pointer used as value | `cannot use pointer 'a' as a value (index it, e.g. 'a[i]')` |
| Assigning a pointer | `cannot assign to pointer parameter 'a'` |
| Two kernels, one name | `duplicate kernel name 'k'` |
| `while`, `for`, … | `'while' is not supported in WarpForge` |
| `/`, `>`, `==`, … | `operator '/' is not supported in WarpForge` |
| `int*`, `double`, … | `unsupported type 'int*' (supported: int, float, float*)` |
| `float* p = …` | `pointer locals are not supported in WarpForge; pointers may only be kernel parameters` |
| `sqrt(x)` | `function calls are not supported in WarpForge (only global_id())` |
| `int x;` | `declaration of 'x' must have an initializer` |
| `2147483648` | `integer literal '2147483648' is out of range for int` |
| `1e39` | `float literal '1e39' is out of range for float (max ~3.4028235e38)` |
| File with no kernels | `error: no kernels in input` |
| Syntax | `expected ';' after assignment, found '}'`, `expected an expression, found ';'` |

Every row is covered by a file in [`tests/errors/`](../tests/errors).

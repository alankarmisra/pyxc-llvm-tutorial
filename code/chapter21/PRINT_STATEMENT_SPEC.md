# Print Statement Spec (Draft for Next Chapter)

## Goal
Implement a Python-style `print(...)` language feature in Pyxc as a compiler builtin.

This chapter should **not** require general user-defined variadic functions.

## Scope

### In Scope (MVP)
- `print(...)` with zero or more positional arguments.
- Default behavior:
  - `sep = " "`
  - `end = "\n"`
- Works for core scalar types:
  - signed ints: `i8/i16/i32/i64`
  - unsigned ints: `u8/u16/u32/u64`
  - floats: `f32/f64`
- Optionally allow pointers by printing address (or reject with diagnostic; pick one policy and document).

### Out of Scope (MVP)
- General variadic function declarations (`extern def f(...`) and calls.
- Python formatting mini-language.
- f-strings.
- Keyword args other than `sep`/`end`.
- `file=`/`flush=` behavior.

## Language Design

### Syntax
`print` is a reserved builtin statement form:

```py
print()
print(x)
print(x, y, z)
```

MVP may parse it as a special builtin call expression, but semantics are statement-like.

### Future-Compatible Extension (Optional in same chapter)
Allow keyword options:

```py
print(x, y, sep=", ", end="")
```

If implemented now, keep rules strict:
- `sep` and `end` must be final named args.
- Values must be string literals (or char arrays if string type exists).

## Semantics

### Default behavior
- `print()` emits newline only.
- `print(a, b, c)` emits:
  - `a`
  - separator
  - `b`
  - separator
  - `c`
  - end string

### Type mapping to runtime helpers
- `i8/i16/i32/i64` -> `printi8/printi16/printi32/printi64`
- `u8/u16/u32/u64` -> `printu8/printu16/printu32/printu64`
- `f32/f64` -> `printfloat32/printfloat64`

Separator and newline can be emitted via character helper (`printchard` or `putchari*`).

### Return value
- `print` statement returns no language value.
- If internal expression plumbing requires a value, use an ignored sentinel (`0.0`) but keep language semantics as statement.

## Compiler Changes

### Lexer
- Add `print` keyword token (or treat as identifier and detect in parser; keyword preferred).

### Parser
- Add print statement rule under `statement` alternatives.
- Parse comma-separated argument list.
- Build `PrintStmtAST` (recommended) or dedicated builtin AST node.

### AST
Add `PrintStmtAST`:
- `vector<ExprAST*> Args`
- optional `sep` / `end` fields if implemented

### Type Checking
- Resolve each argument type before lowering.
- Emit diagnostic for unsupported types.

### Codegen
For each arg:
1. Codegen arg value.
2. Select print helper symbol based on arg type.
3. Cast/coerce as needed to helper signature.
4. Emit call.
5. Emit separator between args.

After args:
- emit end (`\n` by default).

### Runtime
Reuse existing runtime helpers in Chapter 17.
No new heavy runtime required.

## Diagnostics

Required diagnostics:
- Unsupported print argument type.
- Invalid keyword for print (if keywords implemented).
- Duplicate keyword (`sep` repeated).
- Non-literal `sep/end` where literals are required.

## Testing Plan

### Positive
- `print()` -> newline only.
- `print(1)`
- `print(1, 2, 3)` with exact spacing/newline.
- Mixed types: int, uint, float.
- Narrow signed values (`i8/i16`) to verify ABI path still correct.

### Negative
- Unsupported type argument.
- Malformed syntax (trailing comma policy, bad keyword usage).

### Suggested test file set
- `print_basic_empty.pyxc`
- `print_basic_scalars.pyxc`
- `print_mixed_types.pyxc`
- `print_narrow_signed.pyxc`
- `print_errors.pyxc`

## Implementation Order
1. Add `print` token + parser support.
2. Add `PrintStmtAST` and basic codegen for positional args.
3. Add separator/newline emission.
4. Add diagnostics and tests.
5. (Optional) Add `sep`/`end` keyword support.

## Non-Goals Clarification
This chapter does **not** introduce general variadics.  
`print(...)` is a compiler builtin with variable argument count known at parse time.

## Done Criteria
- `print()` and `print(a, b, c)` work with scalar types.
- Output format matches documented defaults.
- Full lit suite for print passes.
- Existing Chapter 17 tests continue to pass.

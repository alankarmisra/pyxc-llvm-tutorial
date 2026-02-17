# Chapter 23 Design Requirements

## Theme
C-style I/O baseline with string literals and minimal `printf`.

## Goal
Enable practical text I/O for K&R-style programs by adding first-class string literals and direct calls to `putchar`, `getchar`, `puts`, and a constrained `printf`.

## Scope

### In Scope
- String literal token and expression support:
  - `"hello\n"`
- Calling libc-style symbols without explicit `extern` declarations:
  - `putchar(i32) -> i32`
  - `getchar() -> i32`
  - `puts(ptr[i8]) -> i32`
  - `printf(ptr[i8], ...) -> i32`
- Vararg call lowering for `printf`
- `printf` format validation for a minimal subset:
  - `%d`, `%s`, `%c`, `%p`, and `%%`
- String literal to pointer interop (`ptr[i8]`-compatible behavior)

### Out of Scope
- Full C `printf` compatibility (`%f`, width, precision, flags, length modifiers)
- Variadic function declarations in source language syntax
- File I/O (`fopen`, `fgets`, etc.)

## Syntax Requirements

### String literal expression
```py
s: ptr[i8] = "hello"
```

### C-style I/O calls
```py
putchar(65)
puts("hi")
printf("x=%d %c %p %s\n", 42, 65, p, s)
```

## Lexer Requirements
- Add string token:
  - `tok_string`
- Capture and unescape string payload.
- Report diagnostics for unterminated or invalid escape sequences.

## Parser Requirements
- Add string primary parser:
  - `ParseStringExpr()`
- Extend `ParsePrimary()` to accept string literals.

## AST Requirements
- Add node:
  - `StringExprAST(Value)`

## Semantic Requirements
- String literals codegen as pointer values compatible with `ptr[i8]`.
- Unresolved calls to `putchar`, `getchar`, `puts`, `printf` are auto-declared with libc-compatible signatures.
- `printf` checks:
  - first argument must be a string literal
  - specifiers limited to `%d`, `%s`, `%c`, `%p`, `%%`
  - argument count must match non-`%%` specifiers
  - `%d`/`%c` require integer arguments
  - `%s`/`%p` require pointer arguments

## LLVM Lowering Requirements
- String literals lower with global string storage and pointer result.
- `printf` lowers as vararg call with default promotions:
  - small integers to `i32`
  - `f32` to `f64`

## Diagnostics Requirements
- Non-literal first argument to `printf`
- Unsupported format specifier in `printf`
- Format argument count mismatch in `printf`
- Type mismatch for `%d`, `%s`, `%c`, `%p`
- Unterminated string literal
- Invalid string escape sequence

## Tests

### Positive
- `putchar` and `puts` with string literals
- `printf` using `%d/%s/%c/%p` and `%%`
- String literal assignment to `ptr[i8]`

### Negative
- Unsupported `printf` format specifier
- `printf` format count mismatch
- `printf` type mismatch per format code

## Done Criteria
- Chapter 23 lit suite includes I/O baseline coverage and passes.
- Chapter 22 behavior remains green under Chapter 23 compiler.
- `chapter-23.md` documents chapter diff and implementation.

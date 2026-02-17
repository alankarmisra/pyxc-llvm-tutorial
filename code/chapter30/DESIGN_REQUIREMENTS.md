# Chapter 27 Design Requirements

## Theme
Process-level entrypoint arguments and lower-level C/POSIX interop.

## Goal
Expand pyxc executable/runtime interop with:

- C-style `main(argc, argv)` support
- minimal `scanf` format support
- low-level file descriptor I/O primitives (`open/read/write/close` + helpers)
- ctype-style helpers (`isdigit`, `isalpha`, `isspace`, `tolower`, `toupper`)

## Scope

### In Scope
- `main` may be declared as either:
  - `def main() -> i32`
  - `def main(argc: i32, argv: ptr[ptr[i8]]) -> i32`
- Minimal `scanf` support:
  - format subset `%d`, `%f`, `%s`, `%c`, and `%%`
  - format must be a string literal
  - destination argument count must match format placeholders
  - destination type checks enforced
- Auto-declare and type-check low-level APIs:
  - `open(path, flags, mode) -> i32`
  - `creat(path, mode) -> i32`
  - `close(fd) -> i32`
  - `read(fd, buf, count) -> i64`
  - `write(fd, buf, count) -> i64`
  - `unlink(path) -> i32`
- Auto-declare and type-check ctype helpers:
  - `isdigit(ch) -> i32`
  - `isalpha(ch) -> i32`
  - `isspace(ch) -> i32`
  - `tolower(ch) -> i32`
  - `toupper(ch) -> i32`

### Out of Scope
- `switch`/pattern matching syntax
- Full `scanf` grammar/locale handling
- User-defined variadics (`stdarg`-style)
- High-level module/import system
- C preprocessor/macro compatibility

## Behavior Requirements
- Invalid `main` signature in object/executable compile mode produces clear error.
- `scanf` rejects unsupported format specifiers and mismatched destination types.
- Low-level I/O calls reject incorrect argument classes (pointer vs integer).

## Tests

### Positive
- executable with `main(argc, argv)` receives arguments and prints them
- `scanf` integer and string read paths work
- `creat/write/close/open/read/unlink` roundtrip works

### Negative
- invalid `main` signature (wrong arity or types)
- `scanf` destination not pointer
- `scanf` format/type mismatch
- `read` with non-integer fd

## Done Criteria
- Chapter 27 builds cleanly.
- Chapter 27 tests pass.
- Existing Chapter 26 behavior remains green.

# Chapter 23 Design Requirements

## Theme
C-style file I/O baseline using libc stdio file APIs.

## Goal
Add practical file read/write support to Pyxc so programs can open files, write text/binary data, read text/binary data, and close handles explicitly.

## Scope

### In Scope
- Auto-declared libc file APIs:
  - `fopen(path, mode) -> ptr[void]`
  - `fclose(file) -> i32`
  - `fgets(buf, n, file) -> ptr[void]`
  - `fputs(str, file) -> i32`
  - `fread(buf, size, count, file) -> i64`
  - `fwrite(buf, size, count, file) -> i64`
- Opaque file handle representation via pointer type (`ptr[void]`-compatible)
- Type validation for known file APIs at call sites
- Full support for text and block I/O patterns using existing pointers/strings

### Out of Scope
- Structured exceptions or automatic resource cleanup
- High-level file abstractions (streams/classes)
- Buffered-state introspection APIs (`feof`, `ferror`, etc.)
- Filesystem metadata APIs

## Syntax Requirements
- No new statement syntax required.
- File operations use normal function-call syntax.

Example:
```py
f: ptr[void] = fopen("out.txt", "w")
fputs("hello\n", f)
fclose(f)
```

## Lexer Requirements
- No new tokens required for file I/O APIs.

## Parser Requirements
- Reuse existing call-expression parser.
- No new grammar productions required beyond chapter22 baseline.

## AST Requirements
- No new AST node types required.

## Semantic Requirements
- Calls to known file APIs are auto-resolved even without user `extern` declarations.
- Function-specific argument type checks:
  - `fopen`: `(pointer, pointer)`
  - `fclose`: `(pointer)`
  - `fgets`: `(pointer, integer, pointer)`
  - `fputs`: `(pointer, pointer)`
  - `fread`/`fwrite`: `(pointer, integer, integer, pointer)`
- Standard arity checks continue to apply.

## LLVM Lowering Requirements
- Declare file APIs as external functions with fixed signatures.
- Lower calls through existing `CallExprAST` path after validation.

## Diagnostics Requirements
- Incorrect arity
- Non-pointer argument passed where pointer required
- Non-integer argument passed where integer required

## Tests

### Positive
- fopen + fputs + fclose text write
- fopen + fgets + fclose text read
- fwrite + fread roundtrip with explicit byte count

### Negative
- `fopen` with non-pointer mode/path
- `fgets` with non-integer length
- `fread`/`fwrite` with wrong argument types
- `fclose` with non-pointer argument

## Done Criteria
- Chapter 23 lit suite includes file I/O coverage and passes.
- Chapter 22 behavior remains green under Chapter 23 compiler.
- `chapter-23.md` documents chapter diff and implementation in tutorial style.

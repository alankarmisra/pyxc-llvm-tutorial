# 24. File I/O with fopen, fclose, fgets, fputs, fread, fwrite

Chapter 23 added string literals and console-style stdio (`putchar`, `getchar`, `puts`, minimal `printf`).

That gave us interactive text output, but not persistent data.

Chapter 24 extends libc interop to file APIs so Pyxc programs can read and write files directly.

This chapter intentionally stays close to C semantics: explicit open/close, explicit buffer pointers, explicit byte counts.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter24](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter24).

## Grammar (EBNF)

Chapter 24 adds file I/O through normal function calls (`fopen`, `fclose`, `fgets`, `fputs`, `fread`, `fwrite`), so grammar is unchanged from Chapter 23.
The implementation change is semantic/type-checking in call codegen.

Reference: `code/chapter24/pyxc.ebnf`

```ebnf
statement    = ... | expr_stmt ;
expr_stmt    = expression ;
expression   = unary_expr , { binary_op , unary_expr } ;
postfix_expr = primary , { call_suffix | index_suffix | member_suffix } ;
call_suffix  = "(" , [ arg_list ] , ")" ;
```

## Why this chapter matters

Without file I/O, many practical programs are blocked:

- writing logs or reports to disk
- reading configuration files
- binary serialization experiments
- roundtrip tests that need persistent state

With Chapter 24 we can do both text and block I/O:

- text path: `fopen` + `fputs`/`fgets` + `fclose`
- binary/block path: `fopen` + `fwrite`/`fread` + `fclose`

This is a major interop milestone because these APIs are part of the standard C runtime most toolchains already provide.

## Scope and constraints

### In scope

- Auto-declared libc file symbols:
  - `fopen(path, mode) -> ptr`
  - `fclose(file) -> i32`
  - `fgets(buf, n, file) -> ptr`
  - `fputs(str, file) -> i32`
  - `fread(buf, size, count, file) -> i64`
  - `fwrite(buf, size, count, file) -> i64`
- Opaque file-handle usage via pointer types (`ptr[void]` works naturally)
- Call-site type checks for known file APIs
- Regression-safe integration with existing call/codegen pipeline

### Out of scope

- high-level file abstractions/classes
- exception-style cleanup semantics
- extra stdio state APIs (`feof`, `ferror`, etc.)
- path utility helpers

We keep the surface raw and explicit in this chapter.

## What changed from Chapter 23

Primary diff:

- `code/chapter23/pyxc.cpp` -> `code/chapter24/pyxc.cpp`
- `code/chapter23/test` -> `code/chapter24/test`

No new parser grammar was required for file APIs because they are normal function calls.

Main implementation buckets:

1. extend libc auto-declaration table with file APIs
2. add file-API-specific argument type checks in `CallExprAST::codegen()`
3. add positive and negative lit coverage

## Retrospective Note (Boolean/Comparison Semantics Cleanup)

Chapter 24 is also where we finally fixed a long-running semantic debt from early Kaleidoscope-style behavior.

Historically (including Chapter 15 onward), logical/comparison-style operations produced floating values (`0.0` / `1.0`) in several paths, because the original tutorial flow leaned that way for simplicity.

In Chapter 24, we switched this to C-like integer-style truth values and related signed/unsigned correctness:

- `not` / `!` now yields integer truth values (not floating)
- `and` / `or` result values are integer truth values
- comparisons yield integer truth values
- unsigned division/modulo/comparisons use unsigned LLVM ops
- unsigned vararg promotions use zero-extension

Honestly, we should have done this around Chapter 15 when control flow and branching got serious. We noticed it late, fixed it here, and kept momentum instead of stopping to rewrite every earlier chapter immediately. Pragmatic? Yes. A tiny bit lazy? Also yes.

## Design choice: no new syntax

Chapter 24 deliberately introduces no new statement keywords.

All file operations use existing call syntax:

```py
f: ptr[void] = fopen("out.txt", "w")
fputs("hello\n", f)
fclose(f)
```

This keeps language growth small and pushes capability through interop, which is exactly what we want at this stage.

## Extending libc symbol resolution

In Chapter 23, libc auto-declaration supported only console I/O symbols.

In Chapter 24, `GetOrCreateLibcIOFunction(...)` is extended:

```cpp
static Function *GetOrCreateLibcIOFunction(const std::string &Name) {
  if (Function *F = TheModule->getFunction(Name))
    return F;

  Type *I32Ty = Type::getInt32Ty(*TheContext);
  Type *I64Ty = Type::getInt64Ty(*TheContext);
  Type *PtrTy = PointerType::get(*TheContext, 0);
  FunctionType *FT = nullptr;

  if (Name == "putchar")
    FT = FunctionType::get(I32Ty, {I32Ty}, false);
  else if (Name == "getchar")
    FT = FunctionType::get(I32Ty, {}, false);
  else if (Name == "puts")
    FT = FunctionType::get(I32Ty, {PtrTy}, false);
  else if (Name == "printf")
    FT = FunctionType::get(I32Ty, {PtrTy}, true);
  else if (Name == "fopen")
    FT = FunctionType::get(PtrTy, {PtrTy, PtrTy}, false);
  else if (Name == "fclose")
    FT = FunctionType::get(I32Ty, {PtrTy}, false);
  else if (Name == "fgets")
    FT = FunctionType::get(PtrTy, {PtrTy, I32Ty, PtrTy}, false);
  else if (Name == "fputs")
    FT = FunctionType::get(I32Ty, {PtrTy, PtrTy}, false);
  else if (Name == "fread")
    FT = FunctionType::get(I64Ty, {PtrTy, I64Ty, I64Ty, PtrTy}, false);
  else if (Name == "fwrite")
    FT = FunctionType::get(I64Ty, {PtrTy, I64Ty, I64Ty, PtrTy}, false);
  else
    return nullptr;

  return Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());
}
```

### Why this works cleanly

`getFunction(...)` already had a fallback hook for libc-known functions. We simply broadened the allowlist.

No parser changes were needed, and no runtime wrapper stubs were required.

## Semantic checks in CallExprAST::codegen()

Auto-declaration alone is not enough. We also need clear diagnostics when arguments are the wrong shape.

Chapter 24 adds function-specific checks after argument codegen:

```cpp
auto CheckPointerArg = [&](size_t ArgIndex, const char *Err) -> bool { ... };
auto CheckIntegerArg = [&](size_t ArgIndex, const char *Err) -> bool { ... };
```

Then per API:

```cpp
if (Callee == "fopen") {
  if (!CheckPointerArg(0, "fopen expects pointer path argument") ||
      !CheckPointerArg(1, "fopen expects pointer mode argument"))
    return nullptr;
} else if (Callee == "fclose") {
  if (!CheckPointerArg(0, "fclose expects pointer file argument"))
    return nullptr;
} else if (Callee == "fgets") {
  if (!CheckPointerArg(0, "fgets expects pointer buffer argument") ||
      !CheckIntegerArg(1, "fgets expects integer length argument") ||
      !CheckPointerArg(2, "fgets expects pointer file argument"))
    return nullptr;
} else if (Callee == "fputs") {
  if (!CheckPointerArg(0, "fputs expects pointer string argument") ||
      !CheckPointerArg(1, "fputs expects pointer file argument"))
    return nullptr;
} else if (Callee == "fread") {
  if (!CheckPointerArg(0, "fread expects pointer buffer argument") ||
      !CheckIntegerArg(1, "fread expects integer size argument") ||
      !CheckIntegerArg(2, "fread expects integer count argument") ||
      !CheckPointerArg(3, "fread expects pointer file argument"))
    return nullptr;
} else if (Callee == "fwrite") {
  if (!CheckPointerArg(0, "fwrite expects pointer buffer argument") ||
      !CheckIntegerArg(1, "fwrite expects integer size argument") ||
      !CheckIntegerArg(2, "fwrite expects integer count argument") ||
      !CheckPointerArg(3, "fwrite expects pointer file argument"))
    return nullptr;
}
```

These checks are intentionally direct. They catch misuse at compile-time/lowering time and produce clear messages.

## Interaction with existing call lowering

After checks, Chapter 24 still uses existing call machinery:

- fixed-arity arity validation
- per-formal cast via `CastValueTo`
- normal LLVM `CreateCall`

So this chapter extends behavior without forking the call path.

This is important for maintainability: file APIs become “first-class interop calls” rather than special one-off nodes.

## Example usage patterns

### Text write/read roundtrip

```py
def main() -> i32:
    f: ptr[void] = fopen("chapter23_io_a.txt", "w")
    fputs("alpha\n", f)
    fclose(f)

    f = fopen("chapter23_io_a.txt", "r")
    buf: ptr[i8] = malloc[i8](64)
    fgets(buf, 64, f)
    fclose(f)

    printf("%s", buf)
    free(buf)
    return 0

main()
```

### Block I/O roundtrip with byte counts

```py
def main() -> i32:
    f: ptr[void] = fopen("chapter23_io_b.bin", "wb")
    written: i64 = fwrite("HELLO", 1, 5, f)
    fclose(f)

    f = fopen("chapter23_io_b.bin", "rb")
    buf: ptr[i8] = malloc[i8](6)
    read: i64 = fread(buf, 1, 5, f)
    buf[5] = 0
    fclose(f)

    printf("w=%d r=%d s=%s\n", written, read, buf)
    free(buf)
    return 0

main()
```

This pattern demonstrates both the return counts and pointer-buffer usage.

## Tests added in Chapter 24

New tests under `code/chapter24/test`:

### Positive

- `file_fputs_fgets_roundtrip.pyxc`
- `file_fread_fwrite_roundtrip.pyxc`

### Negative

- `file_error_fopen_mode_not_pointer.pyxc`
- `file_error_fgets_len_not_integer.pyxc`
- `file_error_fread_buffer_not_pointer.pyxc`
- `file_error_fclose_non_pointer.pyxc`

These run in addition to inherited Chapter 23 coverage.

## Validation result

Build:

```bash
cd code/chapter24 && ./build.sh
```

Tests:

```bash
lit -sv code/chapter24/test
```

Result:

- 46 tests discovered
- 46 passed

## Notes on typing and handles

Chapter 24 keeps file handles opaque and pointer-shaped (`ptr[void]` works well).

This is a good tradeoff now:

- minimal type-system change
- clear interoperability model
- room to add a named alias (`type file_t = ptr[void]`) at user level

You can add richer handle typing later if you want stricter API boundaries.

## Design takeaways

Chapter 24 keeps language complexity under control by reusing existing machinery:

- existing call syntax
- existing expression/type system
- existing cast and call lowering flow

But capability jumps significantly: Pyxc can now do persistent text and binary I/O with standard libc semantics.

That is enough to support realistic tooling-style programs and sets up future chapters for modules, headers, and broader C interop.

## Compiling

From repository root:

```bash
cd code/chapter24 && ./build.sh
```

## Testing

From repository root:

```bash
lit -sv code/chapter24/test
```

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter24 && ./build.sh
```

Run one sample program:

```bash
code/chapter24/pyxc -i code/chapter24/test/addr_is_keyword.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter24/test
lit -sv .
```

Try editing a test or two and see how quickly you can predict the outcome.

When you're done, clean artifacts:

```bash
cd code/chapter24 && ./build.sh
```


## Need Help?

Stuck on something? Have questions about this chapter? Found an error?

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report bugs, errors, or problems
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- The chapter you're working on
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message or unexpected behavior
- What you've already tried

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!

# 27. Program Arguments, scanf Baseline, Low-Level FD I/O, and ctype Helpers

Chapter 26 gave us separate compilation and multi-file linking.

Chapter 27 extends runtime interop in four practical directions:

- executable entrypoint arguments (`main(argc, argv)`)
- minimal `scanf` support for input parsing
- low-level descriptor-style file APIs (`open/read/write/close` and helpers)
- ctype helper interop (`isdigit`, `isalpha`, `isspace`, `tolower`, `toupper`)

This chapter keeps python-style syntax and adds capability through explicit, typed function calls.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter27](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter27).

## Grammar (EBNF)

Chapter 27 is mostly runtime interop and semantic validation (`scanf`, low-level fd I/O, ctype helpers, `main(argc, argv)` constraints).
Core grammar remains the same shape as Chapter 26.

Reference: `code/chapter27/pyxc.ebnf`

```ebnf
prototype       = identifier , "(" , [ param_list ] , ")" , "->" , type_expr ;
param_list      = param , { "," , param } ;
param           = identifier , ":" , type_expr ;

(* executable main forms are enforced semantically in codegen:
   def main() -> i32
   def main(argc: i32, argv: ptr[ptr[i8]]) -> i32 *)
```

## What this chapter adds

### `main(argc, argv)` support

When compiling executables/objects, `main` is now valid in either form:

- `def main() -> i32`
- `def main(argc: i32, argv: ptr[ptr[i8]]) -> i32`

Anything else is rejected with a clear compile error.

### Minimal `scanf` subset

Added `scanf` support with strict validation.

Supported conversions:

- `%d`
- `%f`
- `%s`
- `%c`
- `%%`

Enforced rules:

- format string must be a literal
- format placeholder count must match destination arg count
- each destination must be pointer-typed
- destination pointee type must match conversion category

### Low-level descriptor APIs

Added interop declarations and checks for:

- `open(path, flags, mode) -> i32`
- `creat(path, mode) -> i32`
- `close(fd) -> i32`
- `read(fd, buf, count) -> i64`
- `write(fd, buf, count) -> i64`
- `unlink(path) -> i32`

### ctype helper interop

Added:

- `isdigit(ch) -> i32`
- `isalpha(ch) -> i32`
- `isspace(ch) -> i32`
- `tolower(ch) -> i32`
- `toupper(ch) -> i32`

All are validated as integer-argument calls.

## How it was implemented (slow walkthrough)

### Step 1: Add libc symbol declarations

In `code/chapter27/pyxc.cpp`, `GetOrCreateLibcIOFunction(...)` was extended.

For ctype:

```cpp
else if (Name == "isdigit")
  FT = FunctionType::get(I32Ty, {I32Ty}, false);
else if (Name == "isalpha")
  FT = FunctionType::get(I32Ty, {I32Ty}, false);
else if (Name == "isspace")
  FT = FunctionType::get(I32Ty, {I32Ty}, false);
else if (Name == "tolower")
  FT = FunctionType::get(I32Ty, {I32Ty}, false);
else if (Name == "toupper")
  FT = FunctionType::get(I32Ty, {I32Ty}, false);
```

For descriptor I/O:

```cpp
else if (Name == "open")
  FT = FunctionType::get(I32Ty, {PtrTy, I32Ty, I32Ty}, false);
else if (Name == "creat")
  FT = FunctionType::get(I32Ty, {PtrTy, I32Ty}, false);
else if (Name == "close")
  FT = FunctionType::get(I32Ty, {I32Ty}, false);
else if (Name == "read")
  FT = FunctionType::get(I64Ty, {I32Ty, PtrTy, I64Ty}, false);
else if (Name == "write")
  FT = FunctionType::get(I64Ty, {I32Ty, PtrTy, I64Ty}, false);
else if (Name == "unlink")
  FT = FunctionType::get(I32Ty, {PtrTy}, false);
```

`scanf` was also added there as vararg declaration.

Why this is nice:

- parser stays simple
- these APIs look like normal function calls in user programs
- symbol creation stays centralized in one place

### Step 2: Add call-site semantic validation

In `CallExprAST::codegen()`, call arguments are validated before emitting LLVM call IR.

For ctype:

```cpp
} else if (Callee == "isdigit") {
  if (!CheckIntegerArg(0, "isdigit expects integer argument"))
    return nullptr;
}
```

For descriptor I/O:

```cpp
} else if (Callee == "read") {
  if (!CheckIntegerArg(0, "read expects integer fd argument") ||
      !CheckPointerArg(1, "read expects pointer buffer argument") ||
      !CheckIntegerArg(2, "read expects integer count argument"))
    return nullptr;
}
```

For `scanf`, we added a format parser and destination checks:

```cpp
if (Callee == "scanf") {
  // format must be string literal
  // subset: %d %f %s %c %%
  // count must match args
  // each destination must be pointer
  // pointee type must match spec
}
```

This is intentionally strict so errors are early and obvious.

### Step 3: Enforce `main` entrypoint signatures

In `PrototypeAST::codegen()`, executable entrypoint ABI checks were added.

Core check shape:

```cpp
if (IsMainEntry) {
  if (!(ParamTypes.empty() || ParamTypes.size() == 2))
    return LogError<Function *>(
        "main must have either 0 arguments or (i32, ptr[ptr[i8]])");
  if (ParamTypes.size() == 2) {
    // enforce exactly (i32, ptr[ptr[i8]])
  }
}
```

This avoids accidental signatures that compile but fail runtime expectations.

## What is possible now (language examples)

### Example 1: Read command-line args

```py

def main(argc: i32, argv: ptr[ptr[i8]]) -> i32:
    printf("argc=%d\n", argc)
    if argc > 2:
        printf("argv1=%s argv2=%s\n", argv[1], argv[2])
    return 0
```

### Example 2: Parse integer input with `scanf`

```py

def main() -> i32:
    x: i32 = 0
    scanf("%d", addr(x))
    printf("x=%d\n", x)
    return 0

main()
```

### Example 3: Descriptor write/read

```py

def main() -> i32:
    fd: i32 = creat("demo.txt", 420)
    write(fd, "HELLO\n", 6)
    close(fd)
    return 0

main()
```

### Example 4: ctype helpers

```py

def main() -> i32:
    printf("%d %d %d\n", isdigit(57), isalpha(65), isspace(32))
    printf("%c %c\n", tolower(65), toupper(122))
    return 0

main()
```

## Compile / Run / Test

### Compile

```bash
cd code/chapter27 && ./build.sh
```

### Run sample programs

`main(argc, argv)` sample:

```bash
code/chapter27/pyxc -o app code/chapter27/test/c26_main_argv_executable.pyxc
./app alpha beta
```

`scanf` sample:

```bash
printf '42\n' | code/chapter27/pyxc -i code/chapter27/test/c26_scanf_int.pyxc
```

### Test

```bash
lit -sv code/chapter27/test
```

Validation result for this implementation pass:

- 117 tests discovered
- 117 passed

## Notes

This chapter intentionally avoids C-syntax sugar. We keep Pythonic syntax and explicit calls, even when code is a little longer.

That tradeoff improves readability and keeps the language direction clear.

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter27 && ./build.sh
```

Run one sample program:

```bash
code/chapter27/pyxc -i code/chapter27/test/c25_mod_add.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter27/test
lit -sv .
```

Have some fun stress-testing the suite with small variations.

When you're done, clean artifacts:

```bash
cd code/chapter27 && ./build.sh
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

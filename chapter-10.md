---
description: "Link object files into standalone executables that can run independently."
---
# 10. Pyxc: Generating Executables

## What We're Building

In Chapter 9, we learned to generate object files (`.o`) with debug information. But object files can't run on their own—they need to be linked into executables.

This chapter adds the final piece: linking object files into standalone executables that can run without the Pyxc compiler.

**Before (Chapter 9):**
```python
# hello.pyxc
extern def putchard(c)

# We don't have blocks or print yet, so we hack a print by 
# adding putchard return values (always 0.0) and get the
# side effect of printing multiple characters.
def printHi(x): return putchard(72.0) + putchard(105.0) + putchard(10.0)

def main(): return printHi(0.0)
```

```bash
$ ./pyxc build hello.pyxc --emit=obj
Wrote hello.o

# blasphemy
$ chmod +x ./hello.o

$ ./hello.o         
zsh: exec format error: ./hello.o
```

**After (Chapter 10):**
```bash
$ ./pyxc build hello.pyxc --emit=exe
Wrote hello.o
Linked executable: hello

$ ./hello
Hi
```

## Source Code

Grab the code: [code/chapter10](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter10)

Or clone the whole repo:
```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter10
```

## From Object File to Executable

In Chapter 9 we produced a `.o` file - machine code, but not yet runnable. To get a runnable executable we need a **linker** to combine our object file with the standard library. We use **LLD** (LLVM's built-in linker) which handles macOS, Linux, and Windows automatically.

```text
hello.pyxc  →  [Pyxc Compiler]  →  hello.o  →  [LLD]  →  hello
```

If you want to understand exactly what the linker is doing under the hood - symbol resolution, relocation, startup code - we cover that in Chapter 11. For now, just know that LLD takes our object file and produces something you can run.

## Implementation

### 1. Add Helper Function for Executable Path

We need to derive the output executable name from the input file:

```cpp
// Returns empty string if the input file has no extension, to avoid
// silently overwriting the input file with the executable output.
static std::string DeriveExecutableOutputPath(const std::string &InputFile) {
  const size_t DotPos = InputFile.find_last_of('.');
  if (DotPos == std::string::npos)
    return "";
  return InputFile.substr(0, DotPos);
}
```

This strips the `.pyxc` extension: `hello.pyxc` → `hello`. If there's no extension we return an empty string rather than silently using the input filename as the output - that would overwrite the source file with an executable.

### 2. Add Linking Function Using LLD

We use LLD (LLVM's linker) directly via the `PyxcLinker` helper class:

```cpp
static bool LinkExecutable(const std::string &ObjectPath, const std::string &RuntimeObj, const std::string &ExePath) {
  // Use LLD (LLVM's linker) to link the object file into an executable.
  // PyxcLinker handles platform-specific linking (ELF, Mach-O, PE/COFF).

  bool success = PyxcLinker::Link(ObjectPath, RuntimeObj, ExePath);
  if (!success) {
    errs() << "Error: failed to link executable.\n";
    return false;
  }

  outs() << "Linked executable: " << ExePath << "\n";
  return true;
}
```

The `PyxcLinker::Link()` function (defined in `include/PyxcLinker.h`) automatically:
- Detects the platform (macOS, Linux, Windows)
- Calls the appropriate LLD driver:
  - `lld::macho::link()` for macOS (Mach-O executables)
  - `lld::elf::link()` for Linux (ELF executables)
  - `lld::coff::link()` for Windows (PE/COFF executables)
- Links the standard library (`-lSystem` on macOS, `-lc` on Linux)
- Handles platform-specific details (SDK paths, dynamic linker, etc.)

### 3. Handle Main Function Return Type

Our `main()` function must return `int` to satisfy the OS, but all Pyxc functions currently return `double`. We fix this by special-casing `main`:

**In `PrototypeAST::codegen()`:**
```cpp
Function *PrototypeAST::codegen() {
  // Special case: main function returns int, everything else returns double
  Type *RetType;
  if (Name == "main") {
    RetType = Type::getInt32Ty(*TheContext);
  } else {
    RetType = Type::getDoubleTy(*TheContext);
  }

  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT = FunctionType::get(RetType, Doubles, false);

  Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());
  // ... set argument names ...
  return F;
}
```

**In `FunctionAST::codegen()`:**
```cpp
if (Value *RetVal = Body->codegen()) {
  // Special handling for main: convert double to i32
  if (P.getName() == "main") {
    RetVal = Builder->CreateFPToSI(RetVal, Type::getInt32Ty(*TheContext), "mainret");
  }

  Builder->CreateRet(RetVal);
  // ... rest of function ...
}
```

This approach:
- Makes `main` have signature `i32 @main()` instead of `double @main()`
- Converts the expression result from `double` to `i32` using `fptosi` instruction

### 4. Set Target Triple and Data Layout

We need to tell LLVM what platform we're generating code for:

```cpp
static bool EmitObjectFile(const std::string &OutputPath) {
  // ... initialization code ...

  // IMPORTANT: Set the module's target triple and data layout
  TheModule->setTargetTriple(Triple(TargetTriple));
  TheModule->setDataLayout(TheTargetMachine->createDataLayout());

  // ... rest of code generation ...
}
```

When pyxc is compiled on different platforms, it will have the target triple for that platform.

**Why this matters:**
- The **target triple** (covered in [Chapter 8](chapter08.md)) identifies the platform: architecture, OS, etc.
- The **data layout** (covered in [Chapter 7](chapter07.md)) specifies sizes and alignments for types
- Together, these ensure LLVM generates correct code for the target platform
- This includes symbol naming conventions (e.g., `_main` on macOS, `main` on Linux)
- Without this, linking will fail because the linker expects platform-specific symbol names

### 5. Update Build Command Handler

In `main()`, handle `--emit=exe`:

```cpp
if (BuildEmit == BuildEmitExe) {
  // Validate output path before doing any work
  const std::string ExePath = DeriveExecutableOutputPath(BuildInputFile);
  if (ExePath.empty()) {
    errs() << "Error: cannot derive executable name from '" << BuildInputFile
           << "': input file has no extension. Rename it with a .pyxc extension.\n";
    return 1;
  }

  // Generate object file
  const std::string ObjectPath = DeriveObjectOutputPath(BuildInputFile);
  bool success = EmitObjectFile(ObjectPath);
  if (!success)
    return 1;

  // Look for runtime.o (provides putchard, printd, etc.)
  std::string RuntimeObj = "runtime.o";
  if (!sys::fs::exists(RuntimeObj)) {
    RuntimeObj = "build/runtime.o";
    if (!sys::fs::exists(RuntimeObj))
      RuntimeObj = ""; // No runtime found, link without it
  }

  // Link object file + runtime into executable
  success = LinkExecutable(ObjectPath, RuntimeObj, ExePath);
  return success ? 0 : 1;
}
```

This two-step process:
1. Generates the object file (`.o`)
2. Links it into an executable

## Compile and Run

```bash
cd code/chapter10
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build
```

Or use the shortcut:
```bash
cd code/chapter10
./build.sh
```

## The Runtime Library

Since Pyxc doesn't support `print` statements yet and all functions currently accept/return `double`, we provide a small runtime library (`runtime.c`) with I/O helper functions:

```c
/// putchard - putchar that takes a double and returns 0.
DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f", X);
  return 0;
}
```

**Why we need this:**
- Pyxc declares all extern functions as `double(double, ...)`
- C's `printf` expects `int printf(const char *, ...)` - type mismatch!
- Our `putchard` bridges the gap by accepting doubles and calling C I/O functions
- Once we add type support in later chapters, we can call `printf` directly

**How it works:**
- The CMakeLists.txt automatically compiles `runtime.c` to `runtime.o`
- The compiler looks for `runtime.o` in the current directory or `build/runtime.o`
- If found, it's automatically linked into your executable
- If not found, linking proceeds without it (you just won't have `putchard`/`printd`)

**Extending the runtime:**
You can add your own functions to `runtime.c`! Just remember they must accept/return `double` to work with the current type system. For example:

```c
DLLEXPORT double add42(double X) {
  return X + 42.0;
}
```

Then in Pyxc: `extern def add42(x)` and call it like any other function.

## Try It Out

If you haven't already, you can create a simple program that prints "Hi" using the `putchard` runtime function:

```python
# hello.pyxc
extern def putchard(c)

def printHi(x): return putchard(72.0) + putchard(105.0) + putchard(10.0)

def main(): return printHi(0.0)
```

Build and run:

```bash
$ ./build/pyxc build hello.pyxc --emit=exe
Wrote hello.o
Linked executable: hello

$ ./hello
Hi
```

The executable is standalone—you can move it to another machine and it will run (assuming same architecture and OS).

**How it works:**
- The `runtime.c` file provides `putchard(double)` which prints a character to stderr
- ASCII codes: 72='H', 105='i', 10=newline
- `putchard` returns 0.0, so adding them gives 0.0, which converts to exit code 0
- The runtime is automatically linked when you build with `--emit=exe` 

## With Optimization

Executables benefit from optimization:

```bash
$ ./build/pyxc build hello.pyxc --emit=exe -O3
Wrote hello.o
Linked executable: hello
```

The `-O3` flag runs LLVM's full optimization pipeline before generating the object file. The resulting executable is faster and often smaller. 

## With Debug Information

Add `-g` for debugging support:

```bash
$ ./build/pyxc build hello.pyxc --emit=exe -g
Wrote hello.o
Linked executable: hello

$ lldb hello
(lldb) breakpoint set -n main
(lldb) run
# Debugger can show source code and step through Pyxc functions!
```

## How It Works: Under the Hood

Let's trace what happens when we build an executable:

1. **Parsing**: Pyxc parses `hello.pyxc` into an AST
2. **Code Generation**: AST → LLVM IR (with `main` returning `i32`)
3. **Optimization**: LLVM optimizes the IR (if `-O` specified)
4. **Target Setup**: Set target triple and data layout for the platform
5. **Object Generation**: LLVM IR → machine code → `hello.o`
6. **Linking**: LLD combines `hello.o` + runtime + std library → `hello`

## Platform-Specific Details

### macOS (Mach-O Executables)

- Symbols have leading underscore: `main` → `_main`
- Standard library: `-lSystem` (includes math functions)
- Uses LLD's Mach-O linker: `ld64.lld`
- Creates Mach-O format executables
- Requires SDK path and platform version

### Linux (ELF Executables)

- Symbols don't need underscore prefix
- Standard library: `-lc` (math included)
- Uses LLD's ELF linker: `ld.lld`
- Creates ELF format executables
- Requires dynamic linker path (`/lib64/ld-linux-x86-64.so.2`)

### Windows (PE Executables)

- Uses LLD's COFF linker: `lld-link`
- Standard library: `libcmt` (C runtime)
- Creates PE/COFF format executables

The `PyxcLinker` class handles all platform differences automatically.

## Exit Codes and Return Values

Pyxc's `main()` returns an `int` exit code, converted from the `double` expression result using `fptosi`. By convention, `0` means success and anything else signals an error to the shell or calling process. So your `main()` should return `0.0`:

```python
extern def putchard(c)

def printHi(x): return putchard(72.0) + putchard(105.0) + putchard(10.0)

def main(): return printHi(0.0)  # putchard returns 0.0, so main returns 0 (success)
```

**Note:** Most Pyxc functions still return `double`. Only `main()` is special-cased to return `int` because that's what the OS expects. Future chapters will add explicit type annotations allowing you to specify return types for all functions.

## Testing Your Implementation

This chapter includes **49 automated tests**. Run them with:

```bash
cd code/chapter10/test
llvm-lit -v .
# or: lit -v .
```

**Pro tip:** The test directory shows exactly what the language can do! Key tests include:
- `cli_build_emit_exe.pyxc` - Verifies executable generation
- `cli_build_emit_obj.pyxc` - Verifies object file creation still works
- `cli_build_debug_info.pyxc` - Tests debug info in executables
- `cli_build_opt_levels.pyxc` - Tests optimization levels

Browse the tests to see what works!

## What We Built

- **Executable generation** - `--emit=exe` creates standalone programs
- **LLD integration** - Uses LLVM's built-in linker for all platforms
- **Platform-aware linking** - Handles macOS (Mach-O), Linux (ELF), Windows (PE) differences
- **Runtime library** - Provides `putchard` and `printd` for I/O (works with double-only types)
- **Automatic runtime linking** - CMake compiles runtime.c and links it automatically
- **Two-stage build** - Object file → executable
- **Main return type handling** - `main()` returns `int` because the OS expects an integer exit code
- **Symbol name correction** - Proper platform-specific symbol naming
- **Exit code support** - Pyxc programs now have meaningful exit codes

Now we have a complete compiler toolchain:
1. Lex and parse Pyxc code
2. Generate LLVM IR
3. Optimize the IR
4. Generate object files with debug info
5. **Link into standalone executables**

## What's Next

**Congratulations!** You've built a complete compiler that generates standalone executables from source code. This is a major milestone!

**Chapters 1-10** form the foundational compiler pipeline. You can now:
- Write Pyxc code
- Compile it to optimized machine code
- Debug it with lldb
- Create standalone programs

**Chapter 11** goes under the hood of the linker - if you've ever wondered what symbol resolution and relocation actually mean, that's the place.

**Chapter 12+** extends the language itself with:
- Comparison and logical operators (`==`, `!=`, `<=`, `and`, `or`)
- Control flow (`if`, `while`)
- Multiple types (`int`, `bool`, `double`)
- Mutable variables and structs

## Need Help?

Stuck? Questions? Errors?

- **Issues:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)
- **Contribute:** Pull requests welcome!

Include:
- Chapter number
- Your OS/platform
- Full error message
- What you tried

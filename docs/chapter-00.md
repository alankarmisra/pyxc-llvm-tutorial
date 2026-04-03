---
title: "Build Your First Language with LLVM"
description: "Learn compilers by building a real programming language from scratch—no experience required."
---
# Pyxc: Build Your First Language with LLVM

**Requirements:** You know C++. That's it. No compiler background needed.

## What You'll Build

A programming language called **Pyxc** (pronounced "Pixie"). It looks like Python, compiles to native code, and runs fast.

You'll learn:
- How lexers and parsers work
- How compilers generate code
- How LLVM optimizes your code
- How to build a real toolchain (compiler, linker, debugger)

By the end, you'll have a working language that:
- Compiles functions and expressions
- Calls C libraries
- Generates optimized native executables
- Includes debug information
- Links using LLVM's built-in linker

## Where We're Headed

**Chapters 1-3** build the front end: lexer, parser, and a polished error-reporting layer.

**Chapters 4-5** set up LLVM and connect the AST to real code generation.

**Chapters 6–11** add language features: JIT evaluation, file input, control flow (`if`/`for`), user-defined operators, mutable variables, and real statement blocks with Python-style indentation.

**Chapters 12–15** turn Pyxc into a real toolchain: a proper CLI with emit modes, object file output, native executable linking, and DWARF debug info for source-level debugging.

**Chapter 16** adds a static type system: `int`, `int8`, `int16`, `int64`, `float32`, `float64`, `bool`, and `None` (void). Parameters, variables, and return types are all explicitly annotated.

**Chapters 17–20** add structs, pointers, C interop, and `while` loops — culminating in the full Mandelbrot renderer from this page's preview.

Here's what Pyxc looks like after chapter 11 — everything below runs today:

```python
extern def printd(x)

@binary(6)
def ^(base, exp):
    var result = 1
    for i = 1, i <= exp, 1:
        result = result * base
    return result

def fib(n):
    if n <= 1: return n
    return fib(n - 1) + fib(n - 2)

def collatz(n):
    var steps = 0
    var x = n
    for i = 1, x != 1, 1:
        var half = x * 0.5
        if half * 2 == x:
            x = half
        else:
            x = x * 3 + 1
        steps = steps + 1
    return steps

printd(fib(10))        # 55
printd(2 ^ 10)         # 1024
printd(collatz(27))    # 111
```

Further ahead: richer types, structs, a type system, and a complete native toolchain.

## Credits

The early chapters are inspired by the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). We start there, then go further with Python-style syntax and a complete toolchain.

If you finish this tutorial, you'll understand how real compilers work.

## How to Use This Tutorial

Each chapter builds on the previous one. You can:
- Follow in order (recommended for beginners)
- Skip ahead if you know lexing/parsing
- Jump to specific topics (optimization, debug info, etc.)

**Code style note:** Early chapters use globals and simple code to focus on concepts. This is intentional—learn the ideas first, polish later.

**Experiment!** Clone the code, break things, add features. That's how you learn.

## Chapter Guide

### The Front End (Start Here)

**[Chapter 1: The Lexer](chapter-01.md)** — Break source code into tokens. The first step of any compiler.

**[Chapter 2: The Parser and AST](chapter-02.md)** — Turn tokens into a tree. Build a recursive descent parser and see "Parsed a function definition." for the first time.

**[Chapter 3: Better Errors](chapter-03.md)** — Fix malformed number detection, replace the keyword if-chain with a table, track source locations, and print caret-style diagnostics.

### Setting Up LLVM

**[Chapter 4: Installing LLVM](chapter-04.md)** — Install LLVM from source with everything you need: clang, lld, lldb, clangd, and lit.

### Code Generation

**[Chapter 5: Code Generation](chapter-05.md)** — Connect the AST to LLVM IR. This is where the compiler starts producing real output.

### Language Features

**[Chapter 6: JIT and Optimisation](chapter-06.md)** — Add LLVM optimisation passes and ORC JIT so expressions evaluate immediately in the REPL.

**[Chapter 7: File Input Mode](chapter-07.md)** — Add file input mode and a `-v` IR flag so pyxc can execute source files through the same JIT pipeline as the REPL.

**[Chapter 8: Control Flow](chapter-08.md)** — Define comparison operators and add `if`/`else` expressions and `for` loops. Render the Mandelbrot set in ASCII.

**[Chapter 9: User-Defined Operators](chapter-09.md)** — Add `@binary(N)` and `@unary` decorators so Pyxc programs can define new operators. Re-render the Mandelbrot with density shading.

**[Chapter 10: Mutable Variables](chapter-10.md)** — Add mutable local variables and assignment using a temporary `var ... :` expression form backed by allocas, loads, and stores.

**[Chapter 11: Statement Blocks](chapter-11.md)** — Replace single-expression bodies with real statement blocks. `if`, `for`, `var`, and `return` become statements. The lexer emits `INDENT`/`DEDENT` tokens and the language becomes indentation-sensitive.

### Toolchain

**[Chapter 12: Global Variables](chapter-12.md)** — Add module-level `var` declarations. Globals are initialized before `main()` runs via a synthetic `__pyxc.global_init` constructor registered with `llvm.global_ctors`.

**[Chapter 13: Object Files and Optimization](chapter-13.md)** — Set up a `TargetMachine`, add `--emit obj`, and honor `-O0`..`-O3` with LLVM's `PassBuilder` pipelines.

**[Chapter 14: Native Executables](chapter-14.md)** — Add `--emit exe` and link `.o` files directly into a native binary using LLD. Add `-o` for the output path and a built-in C runtime for `printd` and `putchard`.

**[Chapter 15: Debug Info and the Optimisation Pipeline](chapter-15.md)** — Add `-g` with `DIBuilder`. Emit DWARF compile units, subprograms, local variables, and source locations. Replace the fixed pass list with `PassBuilder`'s standard O0–O3 pipelines. Add `IRBuilder<NoFolder>` to preserve instruction-level debug locations.

### Types

**[Chapter 16: A Static Type System](chapter-16.md)** — Add eight scalar types: `int`, `int8`, `int16`, `int64`, `float32`, `float64`, `bool`, and `None` (void). Parameters, `var` declarations, `for` loop variables, and return types are all explicitly annotated. Explicit casts (`int32(x)`), type-aware arithmetic, and a strict assignment checker round out the type system.

<!--

### Structs, Pointers, and Full C Interop

**[Chapter 17: Structs and Field Access](chapter-17.md)** — Add `struct` definitions, field layout and offsets, and `.` access for both lvalue and rvalue.

**[Chapter 18: Pointers and Address-Of](chapter-18.md)** — Add `ptr[T]`, `addr(x)` / `&x`, and pointer indexing `p[i]`.

**[Chapter 19: Strings and C Interop](chapter-19.md)** — Add string literals and extern declarations for libc (`printf`, `fopen`, `fputs`, `scanf`). Add `malloc[T]` and `free`.

**[Chapter 20: While Loops and the Full Mandelbrot](chapter-20.md)** — Add `while`. Build the full Mandelbrot renderer using structs, pointers, and I/O — the complete program shown in this tutorial's preview.
-->

## What You'll Learn

By the end of the current chapters:
- **Lexing** - Tokenizing source code
- **Parsing** - Recursive descent, operator precedence
- **AST** - Tree representations of code
- **Code generation** - AST → LLVM IR
- **Optimization** - LLVM's pass system
- **JIT compilation** - Execute code immediately
- **Object files** - Generate relocatable object code
- **Debugging** - DWARF debug info
- **Native executables** - Link object files into real binaries with LLD
- **Linking internals** - Symbol resolution, relocation, object file formats
- **Toolchain** - CLI, build modes, error messages
- **Type systems** - Static types, type checking, explicit casts, void semantics

And you'll have built a real compiler from scratch.

## Need Help?

Stuck? Confused? Found a bug?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)
- **Pull Requests:** [Contribute](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls)

When asking for help, include:
- Chapter number
- Your OS and platform
- Full error message
- What you tried

We're here to help. Let's build this thing.

## Start Building

<!-- Ready? [Start with Chapter 1: The Lexer](chapter-01.md)

Or browse the chapters above and jump to what interests you. -->

Welcome to compiler development. It's not magic—it's just code. Let's build.

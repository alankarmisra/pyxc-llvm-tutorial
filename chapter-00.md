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
- Supports types, pointers, structs, arrays
- Has control flow (if/while/for)
- Generates optimized executables

## Credits

The early chapters are inspired by the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). We start there, then go much further:

- **Python-style syntax** - Indentation, `elif`, `match/case`
- **Real type system** - Scalars, pointers, arrays, structs, `const`
- **Full toolchain** - Build executables, link multiple files, generate debug info
- **C interop** - Call `malloc/free`, `printf`, file I/O, everything

If you finish this tutorial, you'll understand how real compilers work.

## How to Use This Tutorial

Each chapter builds on the previous one. You can:
- Follow in order (recommended for beginners)
- Skip ahead if you know lexing/parsing
- Jump to specific topics (types, control flow, etc.)

**Code style note:** Early chapters use globals and simple code to focus on concepts. Later chapters refactor toward production quality. This is intentional—learn the ideas first, polish later.

**Experiment!** Clone the code, break things, add features. That's how you learn.

## Chapter Guide

### Core Foundation (Start Here)

**[Chapter 1: Lexer](chapter-01.md)** - Break source code into tokens. First step of any compiler.

**[Chapter 2: Parser and AST](chapter-02.md)** - Turn tokens into a tree structure representing your code. Build recursive descent and operator precedence parsers.

**[Chapter 3: Building LLVM](chapter-03.md)** - Install LLVM from source with all the tools you need (clang, lld, lldb, clangd).

**[Chapter 4: Command Line Interface](chapter-04.md)** - Add CLI options so your compiler can build files, not just run a REPL.

**[Chapter 5: Code Generation](chapter-05.md)** - Transform your AST into LLVM IR. This is where you actually generate code.

**[Chapter 6: Understanding LLVM IR](chapter-06.md)** - Learn to read and understand the intermediate representation LLVM uses.

**[Chapter 7: JIT and Optimization](chapter-07.md)** - Run code instantly with JIT compilation. Add LLVM's optimizer passes.

**[Chapter 8: Control Flow](chapter-08.md)** - Add `if` statements and `for` loops. Learn SSA form and phi nodes.

**[Chapter 9: User-Defined Operators](chapter-09.md)** - Let users define custom binary and unary operators with precedence.

**[Chapter 10: Mutable Variables](chapter-10.md)** - Add local variables and assignment. LLVM handles SSA conversion for you.

**[Chapter 11: Object Files](chapter-11.md)** - Generate `.o` files like a real compiler. Link with the system linker.

**[Chapter 12: Debug Information](chapter-12.md)** - Add DWARF debug info so `lldb` can debug your programs.

**[Chapter 13: Forward References](chapter-13.md)** - Support calling functions before they're defined. Handle mutual recursion.

### Advanced Topics

**[Chapter 14: Real Compiler Toolchain](chapter-14.md)** - CLI modes, file parsing, object/executable output. Stop being a toy, become a compiler.

**[Chapter 15: Python-Style Indentation](chapter-15.md)** - Make whitespace significant. Generate INDENT/DEDENT tokens.

**[Chapter 16: Blocks and elif](chapter-16.md)** - Parse statement blocks, support `elif`, make `else` optional.

**[Chapter 17: Clean Operators](chapter-17.md)** - Add `not`, `and`, `or`, `==`, `!=`, `<=`, `>=`. Implement short-circuit logic.

**[Chapter 18: Types and Pointers](chapter-18.md)** - Explicit types, typed function signatures, pointer operations, ABI-correct extern calls.

**[Chapter 19: print() Builtin](chapter-19.md)** - Add a real `print()` statement without general variadics.

**[Chapter 20: Loop Control](chapter-20.md)** - Add `while`, `do`, `break`, `continue`. Complete integer operators.

**[Chapter 21: Structs](chapter-21.md)** - Define struct types, field access, nested fields.

**[Chapter 22: Arrays](chapter-22.md)** - Fixed-size arrays with compile-time sizes.

**[Chapter 23: Dynamic Memory](chapter-23.md)** - Typed `malloc` and `free`.

**[Chapter 24: C-Style I/O](chapter-24.md)** - String literals, `printf`, `putchar`, `getchar`, `puts`.

**[Chapter 25: File I/O](chapter-25.md)** - `fopen`, `fclose`, `fgets`, `fputs`, `fread`, `fwrite`.

**[Chapter 26: const Bindings](chapter-26.md)** - Immutable variables with compile-time checks.

**[Chapter 27: Separate Compilation](chapter-27.md)** - Multi-file builds, linking multiple `.o` files.

**[Chapter 28: Program Arguments and Low-Level I/O](chapter-28.md)** - `main(argc, argv)`, `scanf`, file descriptors.

**[Chapter 29: match/case](chapter-29.md)** - Pattern matching with Python-style syntax.

## What You'll Learn

By the end:
- **Lexing** - Tokenizing source code
- **Parsing** - Recursive descent, operator precedence
- **AST** - Tree representations of code
- **Code generation** - AST → LLVM IR
- **Optimization** - LLVM's pass system
- **Types** - Type checking, type inference basics
- **Control flow** - SSA form, phi nodes
- **Linking** - Object files, executables
- **Debugging** - DWARF debug info
- **Toolchain** - CLI, multi-file builds, error messages

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

Ready? [Start with Chapter 1: The Lexer](chapter-01.md)

Or browse the chapters above and jump to what interests you.

Welcome to compiler development. It's not magic—it's just code. Let's build.

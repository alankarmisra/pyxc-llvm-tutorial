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
- Generates optimized executables
- Includes debug information

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

### Core Foundation (Start Here)

**[Chapter 1: Lexer](chapter-01.md)** - Break source code into tokens. First step of any compiler.

**[Chapter 2: Parser and AST](chapter-02.md)** - Turn tokens into a tree structure representing your code. Build recursive descent and operator precedence parsers.

**[Chapter 3: Building LLVM](chapter-03.md)** - Install LLVM from source with all the tools you need (clang, lld, lldb, clangd).

**[Chapter 4: Command Line Interface](chapter-04.md)** - Add CLI options so your compiler can build files, not just run a REPL.

**[Chapter 5: Code Generation](chapter-05.md)** - Transform your AST into LLVM IR. This is where you actually generate code.

**[Chapter 6: Understanding LLVM IR](chapter-06.md)** - Learn to read and understand the intermediate representation LLVM uses.

**[Chapter 7: Optimization and JIT](chapter-07.md)** - Run code instantly with JIT compilation. Add LLVM's optimizer passes.

**[Chapter 8: Object Files and Optimizations](chapter-08.md)** - Generate `.o` files with proper optimization levels. Call Pyxc code from C++.

**[Chapter 9: Debug Information](chapter-09.md)** - Add DWARF debug info so debuggers like `lldb` can show your source code.

### Coming Soon

More chapters are being prepared and will be added after review:
- Control flow (if/else, loops)
- Mutable variables
- Types and pointers
- Structs and arrays
- And much more...

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
- **Toolchain** - CLI, build modes, error messages

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

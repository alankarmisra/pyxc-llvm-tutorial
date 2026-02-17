---
title: "My First Language Frontend with LLVM Tutorial"
description: "A practical roadmap for building a Python-like language frontend with LLVM, from lexer and parser basics to codegen, tooling, and a full compiler pipeline."
---
# Pyxc: My First Language Frontend with LLVM Tutorial

!!!note Requirements: This tutorial assumes you know C++, but no previous compiler experience is necessary.

## Credits
The Core Foundation chapters are heavily inspired by the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). That tutorial provided the launchpad, but this project has grown far beyond minor syntax tweaks: `Pyxc` now evolves as a Pythonic, C-interoperable systems language frontend with its own parser design, type system, runtime library, and tooling.

In the later chapters, the tutorial extends well past the Kaleidoscope scope with:

- Python-style language structure (indentation blocks, `elif`, `match/case`, and explicit suites)
- A practical typed systems model (scalars, pointers, arrays, structs, `const`, and ABI-conscious interop)
- Real toolchain workflows (object/executable builds, separate compilation, linking, debug info, and tests)
- Runtime and libc interop features used in real programs (`malloc/free`, stdio, file I/O, low-level fd I/O, `scanf` subset, argc/argv)

Many thanks to the LLVM team for their outstanding educational materials and to the open-source community for making compiler development accessible.

## Introduction

Welcome to the *Pyxc: My First Language Frontend with LLVM* tutorial. We'll build a simple programming language from scratch, showing you how compilers work and how to use LLVM to generate real code.

We build `Pyxc` iteratively across several chapters, starting with a lexer and parser, then adding code generation, optimization, control flow, types, and eventually a full compiler toolchain. Each chapter focuses on one concept at a time, keeping things manageable while explaining the "why" behind each decision.

**Note on code style:** Early chapters use globals and keep things simple to focus on compiler concepts and LLVM basics. We'll refactor toward better software engineering practices in later chapters once you're comfortable with the pipeline.

Feel free to experiment—make a copy, hack it up, and break things. That's how you learn.

The tutorial is organized into chapters covering individual topics. You can skip ahead if you're already familiar with certain concepts:

## Core Foundation

[Chapter #1: Pyxc language and Lexer](chapter-01.md) - This shows where we are going and the basic functionality that we want to build. A lexer is also the first part of building a parser for a language, and we use a simple C++ lexer which is easy to understand.

[Chapter #2: Implementing a Parser and AST](chapter-02.md) - With the lexer in place, we can talk about parsing techniques and basic AST construction. This tutorial describes recursive descent parsing and operator precedence parsing.

[Chapter #3: Building LLVM from Source](chapter-03.md) - This chapter focuses on installation and toolchain setup. We build LLVM from source with `clang`, `clangd`, `lld`, and `lldb`, configure `PATH`/`LLVM_DIR`, and wire VS Code to the locally built `clangd` and `compile_commands.json`.

[Chapter #5: Code generation to LLVM IR](chapter-05.md) - With the AST ready, we show how easy it is to generate LLVM IR, and show a simple way to incorporate LLVM into your project.

[Chapter #7: Adding JIT and Optimizer Support](chapter-07.md) - One great thing about LLVM is its support for JIT compilation, so we’ll dive right into it and show you the 3 lines it takes to add JIT support. Later chapters show how to generate .o files.

[Chapter #8: Extending the Language: Control Flow](chapter-08.md) - With the basic language up and running, we show how to extend it with control flow operations (‘if’ statement and a ‘for’ loop). This gives us a chance to talk about SSA construction and control flow.

[Chapter #9: Extending the Language: User-defined Operators](chapter-09.md) - This chapter extends the language to let users define arbitrary unary and binary operators - with assignable precedence! This allows us to build a significant piece of the “language” as library routines.

[Chapter #10: Extending the Language: Mutable Variables](chapter-10.md) - This chapter talks about adding user-defined local variables along with an assignment operator. This shows how easy it is to construct SSA form in LLVM: LLVM does not require your front-end to construct SSA form in order to use it!

[Chapter #11: Compiling to Object Files](chapter-11.md) - This chapter explains how to take LLVM IR and compile it down to object files, like a static compiler does.

[Chapter #12: Debug Information](chapter-12.md) - A real language needs to support debuggers, so we add debug information that allows setting breakpoints in Pyxc functions, print out argument variables, and call functions!

[Chapter #13: Forward Function References and Mutual Recursion](chapter-13.md) - This chapter introduces a two-phase translation-unit pipeline (collect declarations first, codegen second), so file compilation supports forward calls and mutual recursion reliably.

## Intermediate topics

[Chapter #14: From REPL to Real Compiler Toolchain](chapter-14.md) - This chapter turns pyxc into a full compiler frontend. We add CLI modes, file-based parsing, object/executable output, source locations, and DWARF debug info with DIBuilder. It’s the pivot from a JIT demo to a real toolchain.

[Chapter #15: Python-style Indentation and Blocks](chapter-15.md) - Pyxc's syntax uses indentation to denote blocks, just like Python. This chapter shows how to modify the lexer to track indentation levels and automatically generate INDENT/DEDENT tokens, making the parser indentation-aware without explicit braces.

[Chapter #16: Blocks, elif, Optional else, and Benchmarking](chapter-16.md) - Until now, we could *lex* indentation. In this chapter, we use that structure to parse real statement blocks, support `elif`, make `else` optional, and tighten codegen.

[Chapter #17: Clean Operator Rules and Short-Circuit Logic](chapter-17.md) - Before introducing types, we simplify operators. We add `not`, `and`, `or`, `==`, `!=`, `<=`, and `>=`, disable custom operators for now, and implement short-circuit codegen for logical expressions.

[Chapter #18: Typed Interop (Core Types, Pointers, and ABI)](chapter-18.md) - We add explicit scalar and pointer types, typed function signatures, type aliases, typed local declarations, and pointer operations (`addr`, indexing). We also enforce interop-focused typing rules and ABI-correct narrow signed/unsigned extern behavior.

[Chapter #19: A Real `print(...)` Builtin (Without Variadics)](chapter-19.md) - We add a language-level `print(...)` statement builtin without introducing general variadic functions. This chapter covers parser/AST integration, type-directed runtime helper dispatch, and a dedicated lit test suite.

[Chapter #20: Real Loop Control (`while`, `do`, `break`, `continue`) and Finishing Core Integer Operators](chapter-20.md) - We add structured loop control statements and semantic loop-context handling in codegen. We also complete key integer operators (`~`, `%`, `&`, `^`, `|`) with clear diagnostics and tests.

[Chapter #21: Structs and Named Field Access](chapter-21.md) - We add top-level `struct` declarations, struct-typed values, and field access/assignment (`obj.field`) including nested field chains.

[Chapter #22: Fixed-Size Arrays](chapter-22.md) - We add `array[T, N]` types with compile-time sizes and array indexing for reads/writes, including arrays with structs and structs containing arrays.

[Chapter #23: Dynamic Memory with `malloc` and `free`](chapter-23.md) - We add typed heap allocation `malloc[T](count)` and explicit deallocation `free(ptr)` with pointer-focused semantic checks.

[Chapter #24: C-style I/O Baseline (`putchar`, `getchar`, `puts`, minimal `printf`)](chapter-24.md) - We add string literals, libc-style I/O calls, vararg call support for `printf`, and a strict format subset (`%d`, `%s`, `%c`, `%p`, `%%`).

[Chapter #25: File I/O with `fopen`, `fclose`, `fgets`, `fputs`, `fread`, `fwrite`](chapter-25.md) - We extend libc interop to file handles and block/text file operations, with call-site type checks and tests for both positive paths and misuse diagnostics.

[Chapter #26: Immutable Bindings with `const`](chapter-26.md) - We add const declarations with mandatory initialization and enforce reassignment errors at compile time using symbol-binding metadata.

[Chapter #27: Separate Compilation and Multi-File Linking](chapter-27.md) - We add multi-file CLI input handling, per-translation-unit object emission, and list-based linker integration for executable and object workflows.

[Chapter #28: Program Arguments, scanf Baseline, and Low-Level File Descriptor I/O](chapter-28.md) - We add `main(argc, argv)` entrypoint support, a strict `scanf` subset, and descriptor-style interop (`open/read/write/close` + helpers) with compile-time call validation.

[Chapter #29: Python-style match/case](chapter-29.md) - We add integer-pattern `match/case` with optional `case _` default and no fallthrough, using Pythonic syntax only.



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

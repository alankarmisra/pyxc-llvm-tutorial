---
title: "My First Language Frontend with LLVM Tutorial"
---

# Pyxc: My First Language Frontend with LLVM Tutorial

!!!note Requirements: This tutorial assumes you know C++, but no previous compiler experience is necessary.

## Credits
The Core Foundation chapters are heavily inspired by the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html) by Chris Lattner and others. That tutorial provided the launchpad, but this project has grown far beyond minor syntax tweaks: `Pyxc` now evolves as a Pythonic, C-interoperable systems language frontend with its own parser design, type system, runtime library, and tooling.

In the later chapters, the tutorial extends well past the Kaleidoscope scope with:

- Python-style language structure (indentation blocks, `elif`, `match/case`, and explicit suites)
- A practical typed systems model (scalars, pointers, arrays, structs, `const`, and ABI-conscious interop)
- Real toolchain workflows (object/executable builds, separate compilation, linking, debug info, and tests)
- Runtime and libc interop features used in real programs (`malloc/free`, stdio, file I/O, low-level fd I/O, `scanf` subset, argc/argv)

Many thanks to the LLVM team for their outstanding educational materials and to the open-source community for making compiler development accessible.

## Introduction
Welcome to the *Pyxc: My First Language Frontend with LLVM* tutorial. Here we run through the implementation of a simple language, showing how fun and easy it can be. This tutorial will get you up and running fast and show a concrete example of something that uses LLVM to generate code.

This tutorial introduces the simple `Pyxc` language, building it iteratively over the course of several chapters, showing how it is built over time. This lets us cover a range of language design and LLVM-specific ideas, showing and explaining the code for it all along the way, and reduces the overwhelming amount of details up front. We strongly encourage that you work with this code - make a copy and hack it up and experiment.

**Warning:** In order to focus on teaching compiler techniques and LLVM specifically, the first part of the tutorial does not show best practices in software engineering principles. For example, the code uses global variables pervasively, doesn’t use [visitors](http://en.wikipedia.org/wiki/Visitor_pattern), etc… but instead keeps things simple and focuses on the topics at hand. However, we will adapt this to more formal techniques in later chapters, once you've had a chance to familiarize yourself with the LLVM pipeline in an easier setting.

This tutorial is structured into chapters covering individual topics, allowing you to skip ahead as you wish:

## Core Foundation

[Chapter #1: Pyxc language and Lexer](chapter-01.md) - This shows where we are going and the basic functionality that we want to build. A lexer is also the first part of building a parser for a language, and we use a simple C++ lexer which is easy to understand.

[Chapter #2: Implementing a Parser and AST](chapter-02.md) - With the lexer in place, we can talk about parsing techniques and basic AST construction. This tutorial describes recursive descent parsing and operator precedence parsing.

[Chapter #3: Building LLVM from Source](chapter-03.md) - This chapter focuses on installation and toolchain setup. We build LLVM from source with `clang`, `clangd`, `lld`, and `lldb`, configure `PATH`/`LLVM_DIR`, and wire VS Code to the locally built `clangd` and `compile_commands.json`.

[Chapter #5: Code generation to LLVM IR](chapter-05.md) - With the AST ready, we show how easy it is to generate LLVM IR, and show a simple way to incorporate LLVM into your project.

[Chapter #6: Adding JIT and Optimizer Support](chapter-06.md) - One great thing about LLVM is its support for JIT compilation, so we’ll dive right into it and show you the 3 lines it takes to add JIT support. Later chapters show how to generate .o files.

[Chapter #7: Extending the Language: Control Flow](chapter-07.md) - With the basic language up and running, we show how to extend it with control flow operations (‘if’ statement and a ‘for’ loop). This gives us a chance to talk about SSA construction and control flow.

[Chapter #8: Extending the Language: User-defined Operators](chapter-08.md) - This chapter extends the language to let users define arbitrary unary and binary operators - with assignable precedence! This allows us to build a significant piece of the “language” as library routines.

[Chapter #9: Extending the Language: Mutable Variables](chapter-09.md) - This chapter talks about adding user-defined local variables along with an assignment operator. This shows how easy it is to construct SSA form in LLVM: LLVM does not require your front-end to construct SSA form in order to use it!

[Chapter #10: Compiling to Object Files](chapter-10.md) - This chapter explains how to take LLVM IR and compile it down to object files, like a static compiler does.

[Chapter #11: Debug Information](chapter-11.md) - A real language needs to support debuggers, so we add debug information that allows setting breakpoints in Pyxc functions, print out argument variables, and call functions!

[Chapter #12: Forward Function References and Mutual Recursion](chapter-12.md) - This chapter introduces a two-phase translation-unit pipeline (collect declarations first, codegen second), so file compilation supports forward calls and mutual recursion reliably.

## Intermediate topics

[Chapter #13: From REPL to Real Compiler Toolchain](chapter-13.md) - This chapter turns pyxc into a full compiler frontend. We add CLI modes, file-based parsing, object/executable output, source locations, and DWARF debug info with DIBuilder. It’s the pivot from a JIT demo to a real toolchain.

[Chapter #14: Python-style Indentation and Blocks](chapter-14.md) - Pyxc's syntax uses indentation to denote blocks, just like Python. This chapter shows how to modify the lexer to track indentation levels and automatically generate INDENT/DEDENT tokens, making the parser indentation-aware without explicit braces.

[Chapter #15: Blocks, elif, Optional else, and Benchmarking](chapter-15.md) - Until now, we could *lex* indentation. In this chapter, we use that structure to parse real statement blocks, support `elif`, make `else` optional, and tighten codegen.

[Chapter #16: Clean Operator Rules and Short-Circuit Logic](chapter-16.md) - Before introducing types, we simplify operators. We add `not`, `and`, `or`, `==`, `!=`, `<=`, and `>=`, disable custom operators for now, and implement short-circuit codegen for logical expressions.

[Chapter #17: Typed Interop (Core Types, Pointers, and ABI)](chapter-17.md) - We add explicit scalar and pointer types, typed function signatures, type aliases, typed local declarations, and pointer operations (`addr`, indexing). We also enforce interop-focused typing rules and ABI-correct narrow signed/unsigned extern behavior.

[Chapter #18: A Real `print(...)` Builtin (Without Variadics)](chapter-18.md) - We add a language-level `print(...)` statement builtin without introducing general variadic functions. This chapter covers parser/AST integration, type-directed runtime helper dispatch, and a dedicated lit test suite.

[Chapter #19: Real Loop Control (`while`, `do`, `break`, `continue`) and Finishing Core Integer Operators](chapter-19.md) - We add structured loop control statements and semantic loop-context handling in codegen. We also complete key integer operators (`~`, `%`, `&`, `^`, `|`) with clear diagnostics and tests.

[Chapter #20: Structs and Named Field Access](chapter-20.md) - We add top-level `struct` declarations, struct-typed values, and field access/assignment (`obj.field`) including nested field chains.

[Chapter #21: Fixed-Size Arrays](chapter-21.md) - We add `array[T, N]` types with compile-time sizes and array indexing for reads/writes, including arrays with structs and structs containing arrays.

[Chapter #22: Dynamic Memory with `malloc` and `free`](chapter-22.md) - We add typed heap allocation `malloc[T](count)` and explicit deallocation `free(ptr)` with pointer-focused semantic checks.

[Chapter #23: C-style I/O Baseline (`putchar`, `getchar`, `puts`, minimal `printf`)](chapter-23.md) - We add string literals, libc-style I/O calls, vararg call support for `printf`, and a strict format subset (`%d`, `%s`, `%c`, `%p`, `%%`).

[Chapter #24: File I/O with `fopen`, `fclose`, `fgets`, `fputs`, `fread`, `fwrite`](chapter-24.md) - We extend libc interop to file handles and block/text file operations, with call-site type checks and tests for both positive paths and misuse diagnostics.

[Chapter #25: Immutable Bindings with `const`](chapter-25.md) - We add const declarations with mandatory initialization and enforce reassignment errors at compile time using symbol-binding metadata.

[Chapter #26: Separate Compilation and Multi-File Linking](chapter-26.md) - We add multi-file CLI input handling, per-translation-unit object emission, and list-based linker integration for executable and object workflows.

[Chapter #27: Program Arguments, scanf Baseline, and Low-Level File Descriptor I/O](chapter-27.md) - We add `main(argc, argv)` entrypoint support, a strict `scanf` subset, and descriptor-style interop (`open/read/write/close` + helpers) with compile-time call validation.

[Chapter #28: Python-style match/case](chapter-28.md) - We add integer-pattern `match/case` with optional `case _` default and no fallthrough, using Pythonic syntax only.



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

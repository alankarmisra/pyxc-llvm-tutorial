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

**Chapters 6+** add language features: expressions, blocks, comparisons, control flow (`if`/`while`), mutable variables, and eventually optimization, debug info, and native executables.

Here's a preview of what Pyxc will look like with those features:

```python
struct Complex:
    re: double
    im: double

def mandel_escape(c: Complex, max_iter: int) -> int:
    z_re: double = 0.0
    z_im: double = 0.0
    i: int = 0

    while i < max_iter:
        next_re: double = z_re * z_re - z_im * z_im + c.re
        next_im: double = 2.0 * z_re * z_im + c.im
        z_re = next_re
        z_im = next_im

        if z_re * z_re + z_im * z_im > 4.0:
            return i
        i = i + 1

    return max_iter
```

Notice the comparison operators (`<`, `>`), equality check (`==`), and control flow (`while`, `if`)? Those are now introduced early. Richer types (`int`, `Complex`, structs) come later.

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

<!--
**[Chapter 3: Polishing the Lexer](chapter-03.md)** — Fix malformed number handling, promote keywords to a map, and add source locations and token names so error messages are actually useful.

### Setting Up LLVM

**[Chapter 4: Installing LLVM](chapter-04.md)** — Install LLVM from source with everything you need: clang, lld, lldb, clangd, and lit.

### Code Generation

**[Chapter 5: Code Generation](chapter-05.md)** — Connect the AST to LLVM IR. This is where the compiler starts producing real output.

### Language Features

**[Chapter 6: Optimization and JIT](chapter-06.md)** — Add LLVM optimization passes and ORC JIT so expressions evaluate immediately in the REPL.

**[Chapter 7: File Input Mode](chapter-07.md)** — Add `pyxc run script.pyxc` so the compiler can execute source files, not just the REPL.

**[Chapter 8: Comparison Operators](chapter-08.md)** — Add `==`, `!=`, `<=`, `>=` with correct precedence and LLVM IR codegen.

**[Chapter 9: Blocks with `;`](chapter-09.md)** — Add multi-statement function bodies with explicit `;` separators.

**[Chapter 10: Indentation Blocks](chapter-10.md)** — Replace `;` with Python-style indentation using `INDENT`/`DEDENT` tokens.

**[Chapter 11: `if` and `while`](chapter-11.md)** — Add control flow expressions. Write recursive factorial.

**[Chapter 12: Mutable Variables](chapter-12.md)** — Add `let` declarations and assignment. Write iterative factorial with `while`.

### Toolchain

**[Chapter 13: Object Files](chapter-13.md)** — Generate `.o` files with proper optimization levels.

**[Chapter 14: Debug Information](chapter-14.md)** — Add DWARF debug info for source-level debugging with lldb.

**[Chapter 15: Native Executables](chapter-15.md)** — Link object files into native executables using LLVM's linker.

**[Chapter 16: Linking Under the Hood](chapter-16.md)** — Use `nm` and `objdump` to inspect symbol resolution and relocation.
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

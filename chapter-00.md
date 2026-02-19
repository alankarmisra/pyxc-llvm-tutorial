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

**Chapters 1-11** now build language features first: expressions, blocks, comparisons, control flow (`if`/`while`), and mutable variables.

**Toolchain chapters (16+)** then focus on optimization/JIT, object files, debug info, executables, and linker internals.

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

### Core Foundation (Start Here)

**[Chapter 1: Lexer](chapter-01.md)** - Break source code into tokens. First step of any compiler.

**[Chapter 2: Parser and AST](chapter-02.md)** - Turn tokens into a tree structure representing your code. Build recursive descent and operator precedence parsers.

## What You'll Learn

By the end of the current chapters:
- **Lexing** - Tokenizing source code
- **Parsing** - Recursive descent, operator precedence
- **AST** - Tree representations of code

And you'll have the full front-end foundation ready for code generation and execution chapters.

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

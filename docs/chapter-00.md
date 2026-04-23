---
title: "Build Your First Programming Language with LLVM"
description: "Learn compilers by building a real programming language from scratch—no experience required."
---
# pyxc: Build Your First Programming Language with LLVM

## Requirements

You should know some C++. You really don't need to be a master craftsman though. We'll use basic C++ and if we do venture into something complex-y (no, that's not a word), I'll *ELI5* it for you. You don't need to know any compiler theory. We will learn by doing. A lot of the compiler theory you learn elsewhere will automagically make sense once you build a compiler on your own. The theory can then help you structure and expand your thinking to problems we have not considered here, or more excitingly, not considered anywhere else in the world. 

You definitely do not need to know what `LLVM` is, except that it will help you write compilers faster. LLVM has been used to write Rust, Swift, Kotlin/Native, C/C++ compilers (Clang), among others. Using the `IIGEFTIGEFU` principle (*if it's good enough for them, it's good enough for us*), we will use LLVM. You might describe the acronym as gloriously over-engineered. I might ignore you. 

You should know that there are alternatives to LLVM. Regardless of what tool you use, the fundamentals won't change. LLVM works, and works well for our purposes. 

## What We'll Build

We'll invent a programming language called **pyxc** (pronounced "Pixie") that resembles Python syntax. *Pythonic*, if you will. It will run interactively through a REPL using just-in-time compilation (fast), or compile down to a native executable (very fast). I'm not going to expend a paragraph, or two, or three, trying to convince you that doing this is a good idea, and that doing this with *this* tutorial is an even better idea. I'm going to assume, rather naively, that if you are here, building a compiler is something you want to do with me. As you progress through the tutorial, you will be the ultimate arbiter of whether this tutorial is a good fit for your preferred pace and style. It's hard, if not impossible to cater to everyone, but I've tried to keep things simple enough to cater to the hobbyist language designer while not dumbing it down to feel like a toy. 

## Why "pyxc"? 
pyxc is small, nimble, fast, executable, and magical. I made all that up. I only thought of "Py" and "x-cutable" and munged the two. 

## Skip, start building, or keep reading.

The rest of this page is a roadmap and I honestly won't judge you if you just dive into [Chapter 1](chapter-01.md) and get building.  But if you're the sort who needs some structure, read ahead. 

## Where We're Headed

In **Chapters 1-3**, we build the analysis part of the pyxc programming language. The compiler will understand our program's structure and intention, and inform us when it finds something unexpected and/or funky. 

In **Chapter 4** we set up LLVM. It could be smooth. It could be bumpy. If it's the latter, allow yourself a break. But do come back, because the compiler isn't going to build itself.

In **Chapter 5** we will extend our compiler to convert our program's intentions into LLVM's internal representation (IR). The IR looks a lot like assembly, but is specific to LLVM. It is what LLVM converts to machine code. You won't have to write the IR by hand though. LLVM has an easy interface that does all the heavy lifting.

By **Chapter 6 and 7** we will be able to generate and run this IR code in either a python-like interactive REPL interface, or from a source file. At this point, we will be able to write short programs that will outperform similar Python code (do people still say "no cap"?). 

We will text our loved ones who don't quite understand what we actually do and tell them we've invented our own programming language, and that it just printed `1.000000` on the terminal - and that it did it really really fast. They will say something encouraging and hang up on us. We will continue marveling at our first ever output from our first ever programming language. Butterflies and goosebumps galore. 

In **Chapters 8–11** we will add language features such as control flow (`if`/`for`), user-defined operators, mutable variables, and *real statement blocks* with Python-style indentation. People will confuse our code with real python. Facts. 

In **Chapters 12–15** we will add the missing bells and whistles to make the pyxc compiler feel like a production compiler: a proper command line interface with emit modes, object file output, native executable linking, and debug info for source-level debugging. If some of these terms make no sense to you, don't worry about it. You will soon. 

In **Chapter 16** we will add a static type system: `int`, `int8`, `int16`, `int64`, `float32`, `float64`, `bool`, and `None` (void) which will allow us to write programs that rival C/C++/Rust speeds and outperform Python. Again, *no cap*. 

In **Chapters 17–20** we will implement structs and pointers, `while` loops, and some memory management and file I/O interfaces that will bring the language ever closer to being used in your real-world programming projects. 

Here's what pyxc looks like after [chapter 11](chapter-11.md) — everything below runs today:

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

## Credits

The early chapters are inspired by the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). It is brilliant in its pacing and leaves a reader more curious and wanting. I reworked that tutorial to suit a syntax, tone and depth that made more sense to me and hopefully it will make more sense to someone else too. Everything the Kaleidoscope tutorial covers, this one does too. In later chapters, we'll have fun pushing the compiler further in order to support more advanced features. And I hope, that as torch bearers, at least one of you decides to push it further than I have. We have a lot of privilege to be able to learn what we do, and to do what we do. It is only fair that we share and spread this privilege to the far corners of the earth. But, as my mother would often say, "No pressure. Have fun."

## Chapter Guide

### The Front End (Start Here)

**[Chapter 1: The Lexer](chapter-01.md)** — Let's start at the very beginning. A very good place to start.

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

**[Chapter 9: User-Defined Operators](chapter-09.md)** — Add `@binary(N)` and `@unary` decorators so pyxc programs can define new operators. Re-render the Mandelbrot with density shading.

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

Welcome to compiler development. It's not magic—it's just code. Let's build.

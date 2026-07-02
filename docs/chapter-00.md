---
title: "Build Your First Programming Language with LLVM"
description: "Learn compilers by building a real programming language from scratch—no experience required."
---
# pyxc:  Build Your First Programming Language with LLVM

## Requirements

You should know some C++. You really don't need to be a master craftsman though. We'll use basic C++ and if we do venture into something complex-y (no, that's not a word), I'll *ELI5* it for you. You don't need to know any compiler theory. We will learn by doing. A lot of the compiler theory you learn elsewhere will automagically make sense once you build a compiler on your own. The theory can then help you structure and expand your thinking to problems we have not considered here, or more excitingly, not considered anywhere else in the world. 

You definitely do not need to know what `LLVM` is, except that it will help you write compilers faster. LLVM has been used to write Rust, Swift, Kotlin/Native, C/C++ compilers (Clang), among others. Using the `IIGEFTIGEFU` principle (*if it's good enough for them, it's good enough for us*), we will use LLVM. 

You should know that there are alternatives to LLVM. Regardless of what tool you use, the fundamentals won't change. LLVM works, and works well for our purposes. 

## What We'll Build

We'll invent a programming language called **pyxc** (pronounced "Pixie") that resembles Python syntax. *Pythonic*, if you will. It will run interactively through a REPL using just-in-time compilation (fast), or compile down to a native executable (very fast). I'm not going to expend a paragraph, or two, or three, trying to convince you that doing this is a good idea, and that doing this with *this* tutorial is an even better idea. I'm going to assume, rather naively, that if you are here, building a compiler is something you want to do with me. As you progress through the tutorial, you will be the ultimate arbiter of whether this tutorial is a good fit for your preferred pace and style. It's hard, if not impossible to cater to everyone, but I've tried to keep things simple enough to cater to the hobbyist language designer while not dumbing it down to feel like a toy. 

## Why "pyxc"? 
pyxc is a small, nimble, fast, executable, and magical language. Or just something that looks like py-thon and creates xc-utables.

## Skip, start building, or keep reading.
The rest of this page is a roadmap for our tutorial. I honestly won't judge you if you just dive into [Chapter 1](chapter-01.md) and get building.  But if you're the sort who needs some structure, read ahead. 

## Where We're Headed

In **Chapters 1-3**, we build the analysis part of the pyxc programming language. The compiler will understand our program's structure and intention, and inform us when it finds something unexpected and/or funky. 

In **Chapter 4** we set up LLVM. It could be smooth. It could be bumpy. If it's the latter, allow yourself a break. But do come back, because the compiler isn't going to build itself.

In **Chapter 5** we will extend our compiler to convert our program's intentions into LLVM's internal representation (IR). The IR looks a lot like assembly, but is specific to LLVM. It is what LLVM converts to machine code. You won't have to write the IR by hand though. LLVM has an easy interface that does all the heavy lifting.

By **Chapter 6 and 7** we will be able to generate and run this IR code in either a python-like interactive REPL interface, or from a source file. At this point, we will be able to write short programs that will outperform similar Python code (do people still say *no cap*?). 

It is now that we will text our loved ones who don't quite understand what we actually do for a living, and tell them we've invented our own programming language, and that it just printed `1.000000` on the terminal - and that it did it really really fast. They will say something encouraging and hang up on us. We will continue marveling at our first ever output from our first ever programming language. Butterflies and goosebumps galore. 

In **Chapters 8–11** we will add language features such as control flow (`if`/`for`), user-defined operators, mutable variables, and *real statement blocks* with Python-style indentation. People will confuse our code with real python. Facts. 

In **Chapters 12–15** we will add the missing bells and whistles to make the pyxc compiler feel like a production compiler: a proper command line interface with emit modes, object file output, native executable linking, and debug info for source-level debugging. If some of these terms make no sense to you, don't worry about it. You will soon. 

In **Chapter 16** we will add a static type system: `int`, `int8`, `int16`, `int64`, `float32`, `float64`, `bool`, and `None` (void) which will allow us to write programs that rival C/C++/Rust speeds and outperform Python. Again, *no cap*. 

In **Chapters 17–22** we implement the full C-style memory model: structs and field access, pointer types and address-of, pointer arithmetic, heap allocation with `malloc`/`free`/`sizeof`, string literals and C interop, and type aliases. By the end of this phase, pyxc is a serious systems programming language — you can write K&R-style algorithms, call any C library function, and manually manage memory just as you would in C or C++.

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def puts(s: ptr[int8]) -> int
extern def printd(x: float64)

type string = ptr[int8]

struct Point:
  x: int
  y: int

def dot(p: ptr[Point], q: ptr[Point]) -> int:
  return p[0].x * q[0].x + p[0].y * q[0].y

def main() -> int:
  var raw: ptr[int8] = malloc(2 * sizeof(Point))
  var pts: ptr[Point] = ptr[Point](raw)
  pts[0].x = 3
  pts[0].y = 4
  pts[1].x = 1
  pts[1].y = 2
  var next: ptr[Point] = pts + 1
  printd(float64(dot(pts, next)))  # 11.000000
  var msg: string = "done"
  puts(msg)
  free(raw)
  return 0
```

In **Chapters 24–30** we add an object model: `class` declarations, methods with `self`, constructors, visibility rules, traits, and the beginnings of generics.

```pyxc
extern def printd(x: float64)
extern def puts(s: ptr[int8]) -> int

# A trait is a named contract — any class that declares it must satisfy it.
trait Measurable:
  def area() -> int
  def perimeter() -> int

# A class is like a struct with methods, a constructor, and visibility control.
class Rect:
  private w: int
  private h: int

  def __init__(width: int, height: int):
    self.w = width
    self.h = height

  public def scale(factor: int):
    self.w = self.w * factor
    self.h = self.h * factor

# impl adds trait conformance after the class is defined.
# The compiler verifies that Rect actually has area() and perimeter()
# with the right signatures before accepting this.
impl Measurable for Rect:
  def area() -> int:
    return self.w * self.h
  def perimeter() -> int:
    return 2 * (self.w + self.h)

# Generic traits let the same contract apply to different types.
trait Addable[T]:
  def add(x: T, y: T) -> T

class IntAcc:
  public total: int

impl Addable[int] for IntAcc:
  def add(x: int, y: int) -> int:
    self.total = self.total + x + y
    return self.total

def main() -> int:
  var r: Rect = Rect(3, 4)
  printd(float64(r.area()))        # 12.000000
  r.scale(2)
  printd(float64(r.area()))        # 48.000000
  printd(float64(r.perimeter()))   # 28.000000

  var acc: IntAcc = IntAcc()
  printd(float64(acc.add(10, 5)))  # 15.000000
  printd(float64(acc.add(3, 2)))   # 20.000000
  return 0
```

In **Chapters 31–35** we close the K&R compatibility gap: division and remainder, compound assignment, `++`/`--`, logical operators with short-circuit evaluation, `while` and `do/while` loops, `break` and `continue`, bitwise operators, and `switch`. By the end of Chapter 35, pyxc can express everything in the first four chapters of *The C Programming Language* without reaching for a single C library function.

## Credits

The early chapters are inspired by the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). It is brilliant in its pacing and leaves a reader more curious and wanting. I reworked that tutorial to suit a syntax, tone and depth that made more sense to me and hopefully it will make more sense to you too. Everything the Kaleidoscope tutorial covers, this one does too. In later chapters, we'll have fun pushing the compiler further in order to support more advanced features. And I hope, that as torch bearers, at least one of you decides to push it further than I have. We have a lot of privilege to be able to learn what we do, and to do what we do. It is only fair that we share and spread this privilege to the far corners of the earth. But, as my mother would often say, "No pressure. Have fun."

## Chapter Guide

### The Front End 

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

### Structs, Pointers, and the C Memory Model

**[Chapter 17: Structs and Field Access](chapter-17.md)** — Add `struct` definitions, field layout and offsets, and `.` field access for both reads and writes. Structs are passed by value.

**[Chapter 18: Pointers and Address-Of](chapter-18.md)** — Add `ptr[T]`, `addr(x)`, and pointer indexing `p[i]` and `p[i].field`. Functions can now modify the caller's data through a pointer.

**[Chapter 19: Pointer Arithmetic](chapter-19.md)** — Add `p + n`, `p - n`, `p - q` (element-count difference), and pointer comparisons. The building block for K&R-style buffer traversal.

**[Chapter 20: Heap Allocation](chapter-20.md)** — Add `sizeof(T)` as a compile-time constant, `ptr[T](expr)` pointer casts, and the `malloc`/`free` pattern via `extern`. Heap-allocate structs and arrays.

**[Chapter 21: String Literals and C Interop](chapter-21.md)** — Add `"hello"` string literals as `ptr[int8]`, null-terminated global constants, and escape sequences (`\n`, `\t`, `\0`, `\"`, `\\`). Call any C library function with `extern`.

**[Chapter 22: Type Aliases](chapter-22.md)** — Add `type name = type` aliases. Alias chains resolve at definition time. Aliases are transparent in the IR — `type string = ptr[int8]` costs nothing.

**[Chapter 23: Arrays and Array Literals](chapter-23.md)** — Add fixed-size `T[N]` stack arrays, `[1, 2, 3]` initializer literals, index expressions, and array-to-pointer decay when passing to functions.

### OOP Core

**[Chapter 24: Classes](chapter-24.md)** — Add the `class` keyword as a distinct aggregate type. Classes share struct IR layout but carry an `IsClass` flag that unlocks methods, constructors, and visibility in subsequent chapters.

**[Chapter 25: Methods and `self`](chapter-25.md)** — Add methods to classes: define functions inside the class body, call them with `obj.method(args)`, and mutate receiver state through an implicit `self` pointer.

**[Chapter 26: Constructors](chapter-26.md)** — Add `__init__` initializer methods and `ClassName(args)` constructor call syntax. Instances are always zero-initialised before `__init__` runs.

**[Chapter 27: Visibility](chapter-27.md)** — Add `public` and `private` modifiers on class fields and methods. Private members are only accessible from within the class's own method bodies.

**[Chapter 28: Traits](chapter-28.md)** — Add traits: named method-signature contracts that a class declares it satisfies. Conformance is verified at compile time with no runtime overhead.

**[Chapter 29: impl Blocks](chapter-29.md)** — Add `impl TraitName for ClassName:` blocks to implement a trait for an existing class outside the class definition.

**[Chapter 30: Generic Traits](chapter-30.md)** — Add type parameters to traits: `trait Addable[T]` declares a contract over an abstract type, instantiated with a concrete type at each `impl` or `class` site.

### K&R Compatibility (Phase 5)

**[Chapter 31: Arithmetic Completeness](chapter-31.md)** — Add `/` and `%`, five compound assignment operators (`+=`, `-=`, `*=`, `/=`, `%=`), and prefix/postfix `++`/`--` for variables, fields, and array elements. A shared `EmitBuiltInArithmetic` helper unifies all arithmetic paths.

**[Chapter 32: Logical Operators](chapter-32.md)** — Add `&&` and `||` with genuine short-circuit evaluation and `!` (logical not) for `bool`. Both sides must be `bool`; no implicit integer coercion.

**[Chapter 33: Loop Completeness](chapter-33.md)** — Add `while`, `do/while`, `break`, and `continue`. `break` and `continue` correctly target the innermost enclosing loop across arbitrary nesting. `continue` in a `for` loop runs the step expression before re-checking the condition.

**[Chapter 34: Bitwise Operators](chapter-34.md)** — Add `&`, `|`, `^`, `<<`, `>>`, and unary `~` with C-standard precedence. All bitwise operators are integer-only; applying them to floats is a type error.

**[Chapter 35: Switch](chapter-35.md)** — Add `switch` statements with integer case matching, an optional `default`, and `break` support. Cases do not fall through by default — each case exits implicitly.

**[Chapter 36: `elif` Chains](chapter-36.md)** — Add Python-style `elif` so multi-way conditionals don't nest into a pyramid of `else` blocks. Lowered to nested `IfStmtAST` during parsing — no new AST node.

**[Chapter 37: Character Literals](chapter-37.md)** — Add `'a'`, `'\n'`, `'\t'`, `'\\'`, `'\''`, and `'\0'`. A character literal is an integer constant; it reuses `NumberExprAST` and defaults to `int32` to match `getchar()`.

**[Chapter 38: Unsigned Integer Types](chapter-38.md)** — Add `uint8`, `uint16`, `uint32`, and `uint64`. LLVM has no unsigned IR types — signedness lives in instruction selection: `udiv`, `urem`, `lshr`, `icmp u*`, `uitofp`, `fptoui`, and `zext`. Implicit signed/unsigned mixing is rejected.

**[Chapter 39: Assignment as Expression](chapter-39.md)** — Allow `=` and compound-assign operators inside an expression context, right-associative and lowest precedence. Enables `while (c = getchar()) != EOF` and `a = b = 0`. The assigned value flows out of the expression.

**[Chapter 40: Variadic Extern Functions](chapter-40.md)** — Add `extern def f(a: T, ...)` so pyxc can call C functions like `printf` and `scanf`. `...` is only valid in `extern def`; variadic arguments past the fixed params are passed through untyped. Use `%ld` not `%d` for pyxc's 64-bit `int`.

### Program Structure

**[Chapter 41: Module Declarations and Export](chapter-41.md)** — Introduce `module` (names the compilation unit) and `export` (marks public API). Multi-file compilation already works via `pyxc --emit exe a.pyxc b.pyxc` with `extern def` as the glue. `export` draws the public/private line that chapter 42 enforces.

**[Chapter 42: Imports](chapter-42.md)** — `import app.math` finds `app/math.pyxc`, scans its `export` declarations, and injects the prototypes — no `extern def` needed for pyxc-to-pyxc calls. Only exported symbols are importable. Struct, class, trait, and type alias definitions transfer across modules. `--emit exe` auto-includes the full import closure.

**[Chapter 43: Cyclic Imports](chapter-43.md)** — Handle the A→B→A case without infinite recursion. A two-phase scan collects a file's own exports before recursing into its imports. An `InProgress`/`Done` state machine breaks the cycle and doubles as a deduplication cache for large import graphs.

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

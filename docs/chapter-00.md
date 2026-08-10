---
title: "Build Your First Programming Language with LLVM"
description: "Learn compilers by building a real programming language from scratch — no experience required."
---
# pyxc: Build Your First Programming Language with LLVM

## Requirements

You should know some C++. You really don't need to be a master craftsman, though. I'll use basic C++. If I do venture into something complex-y, I'll *ELI5* it for you. You don't need to know any compiler theory. You will learn by doing. A fair bit of the compiler theory you learn elsewhere will automagically make sense to you once you build a compiler on your own. When you do learn the theory, it can then help you structure and expand your thinking to problems we have not considered here, or more excitingly, not considered anywhere else in the world. 

You definitely do not need to know anything about `LLVM`, except that it will help you write compilers faster. LLVM has been used to write Rust, Swift, Kotlin/Native, C/C++ compilers (Clang), among others. Using the `IIGEFTIGEFU` principle (*if it's good enough for them, it's good enough for us*), we will use LLVM. 

You should know that there are alternatives to LLVM. Regardless of what tool you use, the fundamentals won't change. LLVM works, and works well for our purposes. 

## What We'll Build

We'll invent a programming language called **pyxc** (pronounced "Pixie") that resembles Python syntax. *Pythonic*, if you will. We'll be able to run it interactively through a REPL using just-in-time compilation (fast), or compile down to a native executable (very fast). I'm not going to expend a paragraph, or two, or three, trying to convince you that doing this is a good idea, and that doing this with *this* tutorial is an even better idea. I'm going to assume, rather naively, that if you are here, building a compiler is something you want to do with me. As you progress through the tutorial, you will be the ultimate arbiter of whether this tutorial is a good fit for your preferred pace and style. It's hard, if not impossible, to cater to everyone. I've tried to keep things simple enough for the hobbyist language designer without dumbing it down to feel like a toy. 

## About The Tone

I've tried to use an informal tone where possible, and a formal tone only to the extent necessary. Furthermore, I write in the first person so as to make it read less like a textbook, and more like an insight into a fellow software engineer's mind as he tackles a complex task. There are many text books in the compiler construction world. I'm not trying to replace them. I just want to let you witness my conversations with myself, so that you can validate (or invalidate) my approach through your own experiments with building a compiler alongside me. I'm largely an autodidact, and I find that I learn better by mirroring someone else's process. I assume there are others like me. Hence, this blog.

## Why "pyxc"? 
pyxc is a small, nimble, fast, executable, and magical language. Or just something that looks like py-thon and creates x-c-cutables. I didn't dwell on this much. I like it, is what I'm saying. 

## Skip and start building, or keep reading.
The rest of this page is a roadmap for the tutorial. I honestly won't judge you if you just dive into [Chapter 1](chapter-01.md) and start building.  But if you're the sort who needs some structure, read ahead. 

## Where We're Headed

### Foundations

In **Chapters 1–5**, I build the analysis part of pyxc: the lexer that turns source text into tokens, the parser that turns tokens into a syntax tree, the grammar design that encodes operator precedence directly into hand-written grammar tiers instead of a general precedence-climbing algorithm, unary minus and `%` (closing that gap the very next chapter instead of leaving it dangling for dozens of chapters), and better error reporting with real source locations and caret-style diagnostics.

### LLVM and Execution

In **Chapter 6**, I set up LLVM. As you follow along, your experience could be smooth, or reasonably bumpy. If it's the latter, allow yourself a break. But do come back, because the compiler isn't going to build itself. Of course, if you don't succeed, you can [get in touch with me](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) and we can take a crack at it together.

In **Chapters 7 and 8**, I extend the compiler to understand and convert a pyxc program's intentions into LLVM's internal representation (IR). The IR reads a lot like assembly. It's what LLVM converts to machine code for a host of different architectures, giving us a multi-platform language with very little extra work. At this stage, we can run this IR in a REPL and see the output of our programs.

In **Chapter 9**, I add file input mode, so I can run whole source files through the same JIT pipeline instead of typing everything into the REPL one line at a time.

### Statements and Control Flow

In **Chapters 10–14**, I add control flow (`if`/`for`), mutable variables, *real statement blocks* with Python-style indentation, `elif` chains the moment `if` becomes block-bodied, and complete looping: `while`, `do`/`while`, `break`, and `continue`. You might even confuse pyxc code with real Python. I deliberately finish the whole statement language here, before touching the native toolchain or the type system, so I have a comfortable, complete procedural language to build everything else on top of.

### Native Toolchain

In **Chapters 15–17**, I add the missing bells and whistles to make the pyxc compiler feel like a production compiler: global variables, native object-file emission, and one-step executable linking. If some of these terms make no sense to you, don't worry about it. You will soon.

### Types and Typed Operations

In **Chapter 18**, I add a static type system: `int`, `int8`, `int16`, `int32`, `int64`, `float`, `float32`, `float64`, `bool`, and `None` (void), which lets me write programs that rival C/C++/Rust speeds, since I'm using the same LLVM infrastructure.

In **Chapter 19**, unsigned integer types (`uint8` through `uint64`) follow immediately, so signedness exists before I ever need it for bitwise operations or systems-level memory work.

In **Chapter 20**, I add `-g` debug info for real debugger support — now that the type system exists, I can describe real typed values in DWARF metadata instead of pretending everything is a `double`.

In **Chapters 21–23**, I round out the operator set: logical operators (`&&`, `||`, `!`) with real short-circuit evaluation, bitwise operators (`&`, `|`, `^`, `<<`, `>>`, `~`), and `switch` with a genuine LLVM multi-way branch instruction under the hood.

### Data and Memory

In **Chapters 24–33**, I implement the full C-style memory model: structs and field access, pointer types and address-of, pointer arithmetic, fixed-size arrays, heap allocation with `malloc`/`free`/`sizeof`, type aliases, string literals and C interop, character literals, Unicode literals, and variadic `extern` functions for real `printf`/`scanf`-style calls. Arrays come before heap allocation on purpose — I get comfortable with a bounded, automatically-cleaned-up storage model before taking on manual memory lifetime management. By the end of this phase, pyxc is a serious systems programming language: I can write K&R-style algorithms, call any C library function, and manually manage memory just as I would in C or C++.

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

### Expression and Mutation Conveniences

In **Chapters 34 and 35**, I add assignment as an expression and read-modify-write operators: compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`) and prefix/postfix `++`/`--`. These don't let me express anything I couldn't already express with plain assignment — they're pure convenience — which is exactly why they show up this late, once lvalues, types, pointers, and assignment semantics are all already second nature.

```pyxc
extern def getchar() -> int32
extern def printd(x: float64)

var EOF: int32 = -1

def main() -> int:
  var c: int32
  var chars: int = 0
  while (c = getchar()) != EOF:
    chars += 1
  printd(float64(chars))  # count of characters read before EOF
  return 0
```

### Object-Oriented Features

In **Chapters 36–42**, I add an object model: `class` declarations, methods with `self`, constructors, visibility rules, traits, and generics so I can use more object-oriented programming approaches in problem-solving. I'm leaving the C domain behind here and stepping on some C++/Rust toes — deliberately after the procedural, systems-level language is already comfortable to use, not before.

```pyxc
extern def printd(x: float64)

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

### Program Structure

In **Chapters 43–45**, I add a module system: `module` declarations name a compilation unit, `export` marks its public API, and `import` lets one pyxc file use another's exported functions and types without hand-written `extern def` declarations. A two-phase scan handles files that import each other without falling into infinite recursion.

```pyxc
# app/math.pyxc
module app.math

export def add(x: int, y: int) -> int:
  return x + y

export def square(x: int) -> int:
  return x * x
```

```pyxc
# main.pyxc
module app.main
import app.math

extern def printd(x: float64) -> float64

def main() -> int:
  printd(float64(add(2, 3)))     # 5.000000
  printd(float64(square(4)))     # 16.000000
  return 0
```

```bash
pyxc --emit exe -o out main.pyxc
```

### Future Concurrency Track

Closures (Chapter 46) and a concurrency track (Chapters 47–53: ownership rules for shared state, spawning tasks and threads, synchronization, message passing, parallel loops, determinism and race debugging, and eventually parallelizing the compiler itself) are still ahead. There's so much more I want to do beyond these features but I'm evaluating the current chapters for clarity, correctness and consistency before moving further. Once pyxc is stable, feature-rich and reasonably optimized, I might even be able to rewrite the entire tutorial using pyxc as the language to write itself. This is how Rust was eventually bootstrapped too. 

## Credits

For the early chapters, I was inspired by the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). It is brilliant in its pacing and leaves a reader more curious and wanting. I reworked that tutorial to suit a syntax, tone and depth that made more sense to me. Everything the Kaleidoscope tutorial covers, this one does too. In later chapters, as the chapter overview shows, I push the compiler further in order to support more advanced features. And should you so decide, feel free to use this as a template to push the sharing of knowledge even further than I have. We have the incredible privilege of having access to the greatest library known to mankind. It is only fair that we share and spread this privilege to the far corners of the earth. But, as my mother would often say, "No pressure. Remember to have fun."

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

---
title: "Build Your First Programming Language with LLVM"
description: "Learn compilers by building a real programming language from scratch — no experience required."
---
# pyxc: Build Your First Programming Language with LLVM

## Requirements

You should know some C++. You really don't need to be a master craftsman though. I'll use basic C++ and if I do venture into something complex-y, I'll *ELI5* it for you. You don't need to know any compiler theory. You will learn by doing. A fair bit of the compiler theory you learn elsewhere will automagically make sense to you once you build a compiler on your own. When you do learn the theory, it can then help you structure and expand your thinking to problems we have not considered here, or more excitingly, not considered anywhere else in the world. 

You definitely do not need to know anything about `LLVM`, except that it will help you write compilers faster. LLVM has been used to write Rust, Swift, Kotlin/Native, C/C++ compilers (Clang), among others. Using the `IIGEFTIGEFU` principle (*if it's good enough for them, it's good enough for us*), we will use LLVM. 

You should know that there are alternatives to LLVM. Regardless of what tool you use, the fundamentals won't change. LLVM works, and works well for our purposes. 

## What We'll Build

We'll invent a programming language called **pyxc** (pronounced "Pixie") that resembles Python syntax. *Pythonic*, if you will. We'll be able to run it interactively through a REPL using just-in-time compilation (fast), or compile down to a native executable (very fast). I'm not going to expend a paragraph, or two, or three, trying to convince you that doing this is a good idea, and that doing this with *this* tutorial is an even better idea. I'm going to assume, rather naively, that if you are here, building a compiler is something you want to do with me. As you progress through the tutorial, you will be the ultimate arbiter of whether this tutorial is a good fit for your preferred pace and style. It's hard, if not impossible to cater to everyone, but I've tried to keep things simple enough to cater to the hobbyist language designer while not dumbing it down to feel like a toy. 

## About The Tone

I've tried to use an informal tone where possible, and a formal tone only to the extent necessary. Furthermore, I write in the first person so as to make it less pedagogical, and more of an insight into a fellow software engineer's mind as he tackles a complex task. There are plenty of pedagogical references in the compiler construction world. It is not my desire to replace them — I just want to let you witness my conversations with myself, so that you may validate (or invalidate) my approach through your own experiments at building the compiler alongside me. I've personally found this way of mirroring someone else to be a much better way for me to learn things (I'm largely an autodidact) and I figure that there are others like me - which is why this blog exists.

## Why "pyxc"? 
pyxc is a small, nimble, fast, executable, and magical language. Or just something that looks like py-thon and creates x-c-cutables. I didn't dwell on this much. It came to me quite naturally. 

## Skip and start building, or keep reading.
The rest of this page is a roadmap for the tutorial. I honestly won't judge you if you just dive into [Chapter 1](chapter-01.md) and start building.  But if you're the sort who needs some structure, read ahead. 

## Where We're Headed

In **Chapters 1-3**, I build the analysis part of pyxc where it begins to understand my program's structure and intention, and communicates what it can't understand or does not expect. 

In **Chapter 4** I set up LLVM. As you follow along, the process could be smooth, or reasonably bumpy. If it's the latter, allow yourself a break. But do come back, because the compiler isn't going to build itself. Of course if you don't succeed, you can [get in touch with me](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) and we can take a crack at it together. 

In **Chapter 5** I extend the compiler to convert a pyxc program's intentions into LLVM's internal representation (IR). I find that the IR resembles assembly which is understandable. The IR is what LLVM will convert to machine code for a host of different architectures allowing us to have a multi-platform language with very little work. 

By **Chapters 6 and 7** we can generate and run this IR code in either a python-like interactive REPL interface, or from a source file. We will find that, even though we have a limited syntax and language features, we can express short programs that can outperform similar Python code. 

In **Chapters 8–11** I add language features such as control flow (`if`/`for`), user-defined operators, mutable variables, and *real statement blocks* with Python-style indentation. You might even confuse pyxc code with real python.   

In **Chapters 12–15** I add the missing bells and whistles to make the pyxc compiler feel like a production compiler: a proper command line interface that offers different options like object file output, native executable linking, and debug info for source-level debugging. If some of these terms make no sense to you, don't worry about it. You will soon. 

In **Chapter 16** I add a static type system: `int`, `int8`, `int16`, `int64`, `float32`, `float64`, `bool`, and `None` (void) which lets us write programs that rival C/C++/Rust speeds. Not surprising, since we use the same LLVM infrastructure, but super cool nevertheless. 

In **Chapters 17–23** I implement the full C-style memory model: structs and field access, pointer types and address-of, pointer arithmetic, heap allocation with `malloc`/`free`/`sizeof`, string literals and C interop, type aliases, and fixed-size arrays. By the end of this phase, pyxc is a serious systems programming language — I can write K&R-style algorithms (a benchmark that I randomly chose to gauge language completeness), call any C library function, and manually manage memory just as I would in C or C++. Like C/C++, I don't want pyxc to have a garbage collector trading simplicity for raw power. I will, however, want to think about how to extend the language to prevent the same memory corruption horrors that plague C/C++. We don't just want C/C++ features, we want to do better in some respects otherwise we are just reinventing the wheel. I won't be surprised if future C++ versions also address this issue better. Smart pointers in C++ already address some of the issues in C.

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

In **Chapters 24–30** I add an object model: `class` declarations, methods with `self`, constructors, visibility rules, traits, and the beginnings of generics so I can use more object-oriented programming approaches in problem-solving. We are leaving the C domain behind here and stepping on some C++/Rust toes.

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

In **Chapters 31–40** I close the K&R compatibility gap: division and remainder, compound assignment, `++`/`--`, logical operators with short-circuit evaluation, `while` and `do/while` loops, `break` and `continue`, bitwise operators, `switch`, `elif` chains, character literals, unsigned integer types, and assignment as an expression. By the end of this phase, pyxc's control flow and operators cover everything in the first four chapters of *The C Programming Language*.

```pyxc
type string = ptr[int8]
extern def printf(fmt: string, ...) -> int32

# Compound assignment and postfix ++
def factorial(n: int) -> int:
    var result: int = 1
    var i: int = 1
    while i <= n:
        result *= i
        i++
    return result

# Logical and bitwise operators
def is_power_of_two(n: uint32) -> bool:
    return n != uint32(0) && (n & (n - uint32(1))) == uint32(0)

# do/while
def digit_count(n: int) -> int:
    var count: int = 0
    var x: int = n
    do:
        count += 1
        x /= 10
    while x != 0
    return count

# switch on character literals (comma-separated case values), elif for range checks
def classify(c: int8) -> string:
    switch c:
        case 'a', 'e', 'i', 'o', 'u':
            return "vowel"
        default:
            if c >= '0' && c <= '9':
                return "digit"
            elif c >= 'a' && c <= 'z':
                return "consonant"
            else:
                return "other"

# assignment as expression
def count_spaces(s: string) -> int:
    var count: int = 0
    var i: int = 0
    var c: int8
    while (c = s[i]) != 0:
        if c == ' ':
            count++
        i++
    return count

def main() -> int:
    printf("factorial(5) = %ld\n", factorial(5))              # 120
    printf("8 is a power of two: %d\n", is_power_of_two(8))   # 1
    printf("digits in 12345: %ld\n", digit_count(12345))      # 5
    printf("classify('e') = %s\n", classify('e'))             # vowel
    printf("classify('7') = %s\n", classify('7'))             # digit
    printf("spaces in 'a b c': %ld\n", count_spaces("a b c")) # 2
    return 0
```

In **Chapters 41–43** I add a module system: `module` declarations name a compilation unit, `export` marks its public API, and `import` lets one pyxc file use another's exported functions and types without hand-written `extern def` declarations. A two-phase scan handles files that import each other without falling into infinite recursion.

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

There's so much more I want to do beyond these features but I'm evaluating the current chapters for clarity, correctness and consistency before moving further. Once pyxc is stable, feature-rich and reasonably optimized, I might even be able to rewrite the entire tutorial using pyxc as the language to write itself. This is how Rust was eventually bootstrapped too. 

## Credits

For the early chapters, I was inspired by the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). It is brilliant in its pacing and leaves a reader more curious and wanting. I reworked that tutorial to suit a syntax, tone and depth that made more sense to me. Everything the Kaleidoscope tutorial covers, this one does too. In later chapters, as the chapter overview shows, I push the compiler further in order to support more advanced features. And should you so decide, feel free to use this as a template to push the sharing of knowledge even further than I have. We have immense privilege to be able to learn what we do, and to do what we do. It is only fair that we share and spread this privilege to the far corners of the earth. But, as my mother would often say, "No pressure. Have fun."

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

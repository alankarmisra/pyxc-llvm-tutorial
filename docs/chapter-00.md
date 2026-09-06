---
title: "Build Your First Programming Language with LLVM"
description: "Learn compilers by building a real programming language from scratch."
---

# pyxc: Build Your First Programming Language with LLVM

<!-- ![Welcome!](images/TheOfficeGIFbyDAZN.gif) -->

## Keen on building a real programming language?

**You'll learn how to do that here!** 

Don't worry if you don't feel super ready yet. Everybody's got to start somewhere, and I've tried to make this a really great place to start. 

## What You're Building

If you stick around this blog, and life, long enough, you'll build a programming language. 

![Let's Go!](images/YesYou.gif)

The language you'll be building in this tutorial is called **pyxc**, pronounced "Pixie." Its syntax is close to Python, but that's where the imitation stops. Over several chapters, you'll build the language piece by piece. First you'll run some pyxc commands on a Read-Eval-Print Loop (REPL), then you'll graduate to compiling source files, and eventually, you'll produce native executables before extending the language into more advanced areas. And when this tutorial ends, you'll take what you've learned and build bigger and better or just never, ever write a compiler again. I won't judge you either way. 

## What does pyxc look like?

I've got several examples in the notes to follow, but here's something that introduces some of the advanced features. 

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
    self.w *= factor
    self.h *= factor

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
    self.total += x + y
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

Now that we've satisfied your voyeuristic pleasure (*you may see the sample, but you may not run the sample*), let's get moving on.

## Why Bother?

Beyond the obvious bragging rights (you built a *language*), you'll:

- **Understand how your tools work** - No more magic!
- **Think differently about code** - You'll spot inefficiencies in any language
- **Join an elite club** - Compiler writers are rare and valued
- **Have fun** - Seriously, this stuff is addictive

## What Do You Need To Already Know

You do not need compiler theory experience. You *do* need enough C++ to read classes, `unique_ptr`, containers and be able to grasp recursion, pointers/references, and basic data structures. When a compiler term becomes useful, I'll introduce it next to the code that needs it. 

## Why "pyxc"?
pyxc is a small, nimble, fast, executable, and magical language. Or just something that looks like py-thon and creates x-c-cutables. I didn't dwell on this much. *"I like it"*, is what I'm saying. 

## What is LLVM?
So many questions. All you need to know about LLVM right now is that it will help you write production quality compilers faster. Rust, Swift, Kotlin/Native, C/C++ compilers (Clang), Mojo, among others use LLVM under the hood. Using the IIGEFTIGEFU principle (if it's good enough for them, it's good enough for us), we will use LLVM.

If you're into history, and stuff, LLVM was written by [Chris Lattner](https://nondot.org/sabre/) as part of his PHD thesis. 

You should know that there are alternatives to LLVM.

![Chris Lattner!](images/ChrisLattner.gif)

Regardless of what tool you use, the fundamentals won't change. LLVM works, and works well for our purposes.

## About The Tutorial Tone

I learn well by doing. Coding **is** understanding. Making mistakes **is** understanding. Chasing bugs **is** understanding. I like reading tutorials that let me to do just that. Consequently, this tutorial is just that, written in a way that puts you in the implementation seat. **Yes, you.** The one still reading this tutorial, *like a boss*. 

And on that note, it's time you stopped reading and started doing. And I'll lay off the animated gifs. Fire up your terminal and begin by checking if you have the required software installed. 

## Check the Starting Requirements

Make sure these commands exist:

```bash
c++ --version
cmake --version
git --version
```

LLVM is not required yet. You can install it using the instructions in [Chapter 6](chapter-06.md) and strangely enough you'll use it in [Chapter 7](chapter-07.md).

Clone the repository:

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial
```

## A Brief Map Of Your Adventure

You'll give the following abilities to pyxc in stages:

1. Read the characters typed in on the REPL or in your source file
2. Group them into words
3. Arrange the words into a hierarchy to make sense of them
4. Translate what's been understood into a language LLVM will understand
5. Let LLVM translate that into a language the computer understands
6. Run the program
7. Add features to the language that make it a joy to use. This has less to do with LLVM and more to do with designing a language that is general enough to solve a wide range of problems in a way that is amenable to how YOU think. **Yes, you!** This is where the power lies. There are A LOT of languages that can solve a wide range of problems. But you get to pick and choose the features you want, and implement them into a language of your choice. Isn't that exciting?! 
8. Brag/Profit/Do a little jiggy with it. 

## Where You're Headed

This section has more code examples of how the language evolves through the chapters. If you couldn't be bothered with any of this and just want to get building, I respect that. Skip to [Chapter 1](chapter-01.md). 

### Foundations

In **Chapters 1–5**, You teach pyxc to read what you write, make sense of it, and report syntax errors. pyxc is young at this point and understands only one data type, the mighty `double`.

### LLVM and Execution

In **Chapter 6**, You set up LLVM. You could be in for a smooth ride, or on a highway to hell. If it's the latter, allow yourself a break. But do come back, because the compiler isn't going to build itself. Of course, if you do find yourself at the gates of hell and can't find your way back, you can [get in touch with me](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) and we can take a crack at it together.

In **Chapters 7 and 8**, you'll extend pyxc to convert what you understand of the program's intention into something that LLVM understands. LLVM can then emit binary code to execute the intention, and you'll have something you can run. At this stage, we can type pyxc code into a REPL and see the output right away. Goose bumps galore.

In **Chapter 9**, you add file input mode, so you can run source files the same way, instead of typing everything into the REPL one line at a time.

### Statements and Control Flow

In **Chapters 10–14**, you add control flow (`if`, `elif`), mutable variables, *real statement blocks* with Python-style indentation, and looping constructs, `for`, `while`, `do`/`while`, `break`, and `continue`. You might even confuse pyxc code with real Python.

### Native Toolchain

In **Chapters 15–17**, you add the missing bells and whistles to make the pyxc compiler feel like a production compiler: global variables, native object-file emission, and one-step executable linking. If some of these terms make no sense to you, don't worry about it. You will soon.

### Types and Typed Operations

In **Chapter 18**, you add a real static type system: `int`, `int8`, `int16`, `int32`, `int64`, `float`, `float32`, `float64`, `bool`, and `void`. Every variable, parameter, and return type carries an explicit annotation from here on, replacing the single implicit `double` you've used up to this point.

In **Chapters 19–23**, you round things out: unsigned integer types (`uint8` through `uint64`), debugger support for new types, logical operators (`&&`, `||`, `!`), bitwise operators (`&`, `|`, `^`, `<<`, `>>`, `~`), and `switch`.

### Data and Memory

In **Chapters 24–33**, you implement the full C-style memory model: structs and field access, pointer types and address-of, pointer arithmetic, fixed-size arrays, heap allocation with `malloc`/`free`/`sizeof`, type aliases, string literals and C interop, character literals, Unicode literals, and variadic `extern` functions for real `printf`/`scanf`-style calls. By the end of this phase, you'll be sporting a mouthful of jargon, and pyxc will become a serious systems programming language: You'll be able to write K&R-style algorithms, call any C library function, and manually manage memory just as you would in C or C++. 

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

In **Chapters 34 and 35**, you add assignment as an expression (`while (c = getchar()) != EOF:`), compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`) and prefix/postfix `++`/`--`. These don't let you express anything you couldn't already express with plain assignment — they're pure convenience.

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

In **Chapters 36–42**, you add an object model: `class` declarations, methods with `self` (pyxc's version of C++'s implicit `this`), constructors, visibility rules, traits (pyxc's alternative to C++'s pure virtual interfaces), and generics so you can use more object-oriented programming approaches in problem-solving. 

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

In **Chapters 43–45**, you add a module system: `module` declarations name a compilation unit, `export` marks its public API, and `import` lets one pyxc file use another file's exported functions and types directly, without declaring them first. A two-phase scan handles files that import each other without falling into infinite recursion.

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

### Future (Potential) Track

When I first started this blog, I had not expected to get this far. But here I am, standing on the shoulders of giants, potentially pissing them off. 

![Die](images/ShouldersOfGiants.gif)

Beyond the core language, potential future features include closures, enums, generics, operator overloading, inheritance, concurrency, a standard library, compile-time execution, better tooling, testing support, and eventually self-hosting. But that's all aspirational—one chapter at a time.

For now, head on over to [Chapter 1](chapter-01.md) and start building.

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

---
title: "Build Your First Programming Language with LLVM"
description: "Learn compilers by building a real programming language from scratch — no experience required."
---
# pyxc: Build Your First Programming Language with LLVM

## Requirements

You should know some C++. You really don't need to be a master craftsman, though. I'll use basic C++. If I do venture into something complex-y, I'll explain it in simple terms. You don't need to know any compiler theory. You will learn by doing. A fair bit of the compiler theory you learn elsewhere will automagically make sense to you once you build a compiler on your own. When you do learn the theory, it can then help you structure and expand your thinking to problems we have not considered here, or more excitingly, not considered anywhere else in the world. 

You definitely do not need to know anything about `LLVM`, except that it will help you write compilers faster. Compilers for Rust, Swift, Kotlin/Native, C/C++ compilers (Clang), among others use LLVM under the hood. Using the `IIGEFTIGEFU` principle (*if it's good enough for them, it's good enough for us*), we will use LLVM. 

You should know that there are alternatives to LLVM. Regardless of what tool you use, the fundamentals won't change. LLVM works, and works well for our purposes. 

## What We'll Build

We'll invent a programming language called **pyxc** (pronounced "Pixie") that resembles Python syntax. *Pythonic*, if you will. We'll be able to run it interactively through a Read‑Eval‑Print Loop (*REPL*). Type something on the command line, see it run. This will use just-in-time compilation (fast). We'll also be able to compile it down to a native executable (very fast). I'm not going to expend a paragraph, or two, or three, trying to convince you that doing this is a good idea, and that doing this with *this* tutorial is an even better idea. I'm going to assume, rather naively, that if you are here, building a compiler is something you want to do with me. As you progress through the tutorial, you will be the ultimate arbiter of whether this tutorial is a good fit for your preferred pace and style. It's hard, if not impossible, to cater to everyone. I've tried to keep things simple enough for the hobbyist language designer without dumbing it down to feel like a toy. 

## About the Tone

I've tried to use an informal tone where possible, and a formal tone only to the extent necessary. Furthermore, I write in the first person so as to make it read less like a textbook, and more like an insight into a fellow software engineer's mind as he tackles a complex task. There are many text books in the compiler construction world. I'm not trying to replace them. I just want to let you witness my conversations with myself, so that you can validate (or invalidate) my approach through your own experiments with building a compiler alongside me. I'm largely an autodidact, and I find that I learn better by mirroring someone else's process. I assume there are others like me. Hence, this blog.

## Why "pyxc"?
pyxc is a small, nimble, fast, executable, and magical language. Or just something that looks like py-thon and creates x-c-cutables. I didn't dwell on this much. *"I like it"*, is what I'm saying. 

## Start Building, or Keep Reading.
The rest of this page is a roadmap for the tutorial. I honestly won't judge you if you just dive into [Chapter 1](chapter-01.md) and start building. But if you're the sort who needs some structure, read ahead. 

## Where We're Headed

## Foundations

In **Chapters 1–5**, I teach pyxc to read what I write, convert it into its own internal structure, understand how the pieces fit together, and tell me if I get something wrong at the syntax level.

## LLVM and Execution

In **Chapter 6**, I set up LLVM. As you follow along, your experience could be smooth, or bumpy. If it's the latter, allow yourself a break. But do come back, because the compiler isn't going to build itself. Of course, if you don't succeed, you can [get in touch with me](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) and we can take a crack at it together.

In **Chapters 7 and 8**, I extend the compiler to understand and convert a pyxc program's intentions into LLVM's internal representation (IR). The IR reads a lot like assembly. It's what LLVM converts to machine code for a host of different architectures, giving us a multi-platform language with very little extra work. At this stage, we can type pyxc code into a REPL and see the output right away. Goose bumps galore.

In **Chapter 9**, I add file input mode, so I can run whole source files the same way, instead of typing everything into the REPL one line at a time.

## Statements and Control Flow

In **Chapters 10–14**, I add control flow (`if`, `elif`), mutable variables, *real statement blocks* with Python-style indentation, and looping constructs, `for`, `while`, `do`/`while`, `break`, and `continue`. You might even confuse pyxc code with real Python.

## Native Toolchain

In **Chapters 15–17**, I add the missing bells and whistles to make the pyxc compiler feel like a production compiler: global variables, native object-file emission, and one-step executable linking. If some of these terms make no sense to you, don't worry about it. You will soon.

## Types and Typed Operations

In **Chapter 18**, I add a real static type system: `int`, `int8`, `int16`, `int32`, `int64`, `float`, `float32`, `float64`, `bool`, and `void`. Every variable, parameter, and return type carries an explicit annotation from here on, replacing the single implicit `double` I've used up to this point.

In **Chapters 19–23**, I round things out: unsigned integer types (`uint8` through `uint64`), debugger support for new types, logical operators (`&&`, `||`, `!`), bitwise operators (`&`, `|`, `^`, `<<`, `>>`, `~`), and `switch`.

## Data and Memory

In **Chapters 24–33**, I implement the full C-style memory model: structs and field access, pointer types and address-of, pointer arithmetic, fixed-size arrays, heap allocation with `malloc`/`free`/`sizeof`, type aliases, string literals and C interop, character literals, Unicode literals, and variadic `extern` functions for real `printf`/`scanf`-style calls. By the end of this phase, pyxc is a serious systems programming language: I can write K&R-style algorithms, call any C library function, and manually manage memory just as I would in C or C++.

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

## Expression and Mutation Conveniences

In **Chapters 34 and 35**, I add assignment as an expression (`while (c = getchar()) != EOF:`), compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`) and prefix/postfix `++`/`--`. These don't let me express anything I couldn't already express with plain assignment — they're pure convenience.

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

## Object-Oriented Features

In **Chapters 36–42**, I add an object model: `class` declarations, methods with `self` (pyxc's version of C++'s implicit `this`), constructors, visibility rules, traits (pyxc's alternative to C++'s pure virtual interfaces), and generics so I can use more object-oriented programming approaches in problem-solving. 

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

## Program Structure

In **Chapters 43–45**, I add a module system: `module` declarations name a compilation unit, `export` marks its public API, and `import` lets one pyxc file use another file's exported functions and types directly, without declaring them first. A two-phase scan handles files that import each other without falling into infinite recursion.

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

## Future (Potential) Track

None of what follows is committed. Chapter numbers, grouping, and order can all change — this is a first-pass sketch of real, already-written-down design intent, not a promise. I'm giving it a rough sequence anyway, just so the full potential shape of the language is visible in one place, not scattered across a backlog with no order to it. There's so much more I want to do beyond current chapters, but I'm evaluating them for clarity, correctness and consistency before moving further. Once pyxc is stable, feature-rich and reasonably optimized, I might even be able to rewrite the entire tutorial using pyxc as the language to write itself.

## Closures

Lambda syntax and captured variables. The one open question that actually blocks starting: capture semantics. Capture by value is safe under my no-GC model with no extra work; capture by reference means a closed-over variable has to outlive the closure, which is a real lifetime problem I have no borrow-checker to enforce yet. I need to decide this before I write a line of implementation, not while I'm halfway through it.

```pyxc
def make_adder(n: int) -> ptr[def(int) -> int]:
  return \(x: int) -> int: x + n   # captures n — capture semantics still undecided

var add5: ptr[def(int) -> int] = make_adder(5)
printd(float64(add5(10)))  # 15.000000
```

## Self-Hosted Testing and Coverage

Two ideas here, different sizes. The small one: a `test/assert.pyxc` module other `.pyxc` test files can `import`, giving me helpers like `assert.eq_int(actual, expected, label)` that print a `FAIL: ...` line and call `exit(1)` on mismatch. Nothing new needed from the compiler for this — `export`/`import` and variadic `extern def` are already enough — it just replaces the copy-pasted printf-and-compare boilerplate every hand-written test of mine currently repeats.

The bigger one: pyxc-level code coverage, i.e. knowing which lines of a `.pyxc` program actually executed, the same thing `llvm-cov` already does for `pyxc.cpp` itself. This is a real compiler feature, not a library — my own codegen would need to emit profiling counter bumps tied to pyxc source locations, plus a coverage-mapping section. I'm not starting from nothing, though: I already track source locations for diagnostics, so the raw material is half there. I'd want this in place before Concurrency below lands, not after: once concurrent pyxc programs exist, knowing which lines ran and in what order stops being a nice-to-have and becomes the main tool for debugging races and nondeterministic failures.

## Concurrency

Ownership rules for shared state, spawning tasks and threads, synchronization primitives, message passing, parallel loops and work partitioning, determinism and race debugging, and eventually parallelizing the compiler itself. The real blocker is the same shape as closures: I need to decide the safety model before I can design any of the rest concretely, not discover it chapter by chapter.

```pyxc
def worker(ch: Channel[int]):
  ch.send(compute())

def main() -> int:
  var ch: Channel[int] = Channel[int]()
  spawn worker(ch)
  printd(float64(ch.recv()))
  return 0
```

## Enums and Function Pointers

Enums (`enum Color: Red, Green, Blue`) close a real, obvious gap — no implicit int conversion, `switch`-friendly, and a natural place to add exhaustiveness checking so an unhandled variant is a compile error, not a runtime surprise. Function pointers (`ptr[def(int, int) -> int]`) follow right after: callbacks, `qsort`, dispatch tables, and a real prerequisite if I ever get serious about the self-hosting bootstrap plan.

```pyxc
enum Direction:
  North, South, East, West

def opposite(d: Direction) -> Direction:
  switch d:
    case Direction.North: return Direction.South
    case Direction.South: return Direction.North
    case Direction.East:  return Direction.West
    case Direction.West:  return Direction.East
    # no default needed once exhaustiveness checking exists —
    # the compiler already knows every Direction is handled

var callback: ptr[def(int) -> int] = square
```

## Generics Completion

Generic Traits already exist (**Chapter 42**); this extends the same idea to ordinary functions and structs — `def max[T](a: T, b: T) -> T`, `struct Stack[T]` — monomorphised at each instantiation the way C++ templates work, not type-erased the way Java's are. Everything downstream that wants a generic container needs this first.

```pyxc
def max[T](a: T, b: T) -> T:
  return a if a > b else b

struct Stack[T]:
  items: T[64]
  count: int
```

## Standard Library

No new language features here, just wrapping what `extern` already lets me do into real modules — `stdio`, `stdlib`, `string`, `math`, and a built-in `print` so I stop hand-declaring `printf` in every single program. Cheap to build, high payoff for anyone actually writing pyxc programs instead of reading about how the compiler works.

```pyxc
import stdlib.stdio
import stdlib.math

def main() -> int:
  print("sqrt(2) =", sqrt(2.0))
  return 0
```

## Language Ergonomics I — Expressions, Bindings, and Calls

A run of small, independent conveniences on the expression side: ternary expressions, `const` bindings, default parameters, named arguments, multiple return values, a real `NULL`, and `len()`/`in`. None of these change what's *possible* to write in pyxc — I could already express all of it, just more awkwardly. They're worth doing because they're cheap and because a reader coming from Python or C++ will reach for every one of them by instinct.

```pyxc
def clamp(x: int, lo: int = 0, hi: int = 100) -> int:
  return lo if x < lo else (hi if x > hi else x)

var quotient, remainder = divmod(17, 5)
```

## Language Ergonomics II — Statements and Diagnostics

The statement-side equivalent: `assert`, `defer`, `static` locals, `goto`, command-line arguments to `main`, and a lint for accidental assignment in a condition. Same reasoning as above — convenience, not new expressive power.

```pyxc
def read_first_line(path: ptr[int8]) -> int:
  var f: ptr[int8] = fopen(path, "r")
  assert f != NULL, "could not open file"
  defer fclose(f)
  return 0
```

## Unicode Identifiers and String Interpolation

The two heaviest items in the whole ergonomics list, which is why they get their own slot instead of hiding inside the run above. Unicode identifiers (`café`, `变量` as names, not just literal content) need real `XID_Start`/`XID_Continue` tables, a normalization decision, and a homoglyph policy — an actual security question, not just an implementation detail. String interpolation (`f"result: {x}"`) is simpler, but the buffer-size strategy for the `sprintf` it lowers to still needs deciding first.

```pyxc
var café_total: float64 = 4.50
printf(f"Total: {café_total}\n")
```

## Type System Completion

The rest of the type system I've been deferring: optional/nullable types, union types, bit-fields, `const` pointers, multidimensional arrays, pointer-to-array, and a real `void` pointer. Systems-programming completeness — the stuff C gives you that pyxc still makes you work around.

```pyxc
def find(arr: int[10], target: int) -> Option[int]:
  for i in range(10):
    if arr[i] == target: return i
  return None
```

## OOP Refinements

Static class members, operator overloading, a `__str__`/`print` hook, abstract methods on traits, and — the one that actually needs a real decision, not just an afternoon of coding — whether pyxc gets inheritance at all, given that the trait model I already built deliberately has no vtable.

```pyxc
class Vec2:
  public x: float64
  public y: float64

  static def zero() -> Vec2:
    return Vec2(0.0, 0.0)

  def __add__(other: Vec2) -> Vec2:
    return Vec2(self.x + other.x, self.y + other.y)
```

## Import and Module Refinements

Selective imports, import aliasing, and directory-style modules (`__init__.pyxc`) — the ergonomics layer on top of the module system I already built in Chapters 43–45.

```pyxc
from stdlib.io import printf, getchar
import stdlib.math as m

printf("sqrt(2) = %f\n", m.sqrt(2.0))
```

## Generics-Enabled Ecosystem

Once generic structs exist, generators/iterators and real generic collections (`List[T]`, `Dict[K,V]`, `Set[T]`) become possible. This is where pyxc stops making me reimplement a dynamic array by hand every time I need one.

```pyxc
var names: List[ptr[int8]] = List[ptr[int8]]()
names.append("Ada")
names.append("Grace")
for name in names:
  printf("%s\n", name)
```

## Compile-Time Execution

I want Zig-style `comptime` rather than a separate macro language like Rust's `macro_rules!` — arbitrary pyxc code that runs at compile time and produces pyxc values or types, with no second language to learn and no opaque macro-expansion errors. I already have a JIT; running pyxc code at compile time isn't far from what the JIT already does.

```pyxc
comptime def storage_type(n: int) -> type:
  if n <= 32: return int32
  return int64

var x: comptime storage_type(16) = 0   # x: int32 at compile time
```

## Verification

A two-phase program verifier: sequential first (`requires`/`ensures`/`assert`/loop `invariant`, SMT-backed, needs enums to exist first), then a concurrency-aware phase once the concurrency track above actually lands — thread interleavings, synchronization primitives, memory-order rules, real race-freedom checks.

```pyxc
def divide(a: int, b: int) -> int:
  requires b != 0
  ensures result * b == a
  return a / b
```

## Tooling and Quality

Closing the stale source-location bug properly (every codegen error, not just parse errors, pointing at the right line and column), function attributes, escape analysis to promote heap allocations to the stack automatically, REPL quality-of-life improvements, incremental compilation, real packaging, and — if I want pyxc programs to double as MCP servers — a tool-export mechanism. A language server belongs somewhere in here too, though it's IDE tooling built around the compiler, not a compiler feature itself.

```bash
pyxc build          # reads a project manifest, compiles + links
pyxc run main.pyxc  # incremental: only recompiles what changed
```

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

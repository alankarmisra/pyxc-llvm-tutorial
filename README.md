# A word

This tutorial is under active development. A lot has changed in the first two chapters to create a more grounded explanation of the concepts. These changes have not been pushed to the following chapters. I'm working on it. Please write me an email if you prefer to be informed once I'm done. 

# pyxc (Pixie)

`pyxc` is a Pythonic language and compiler built with LLVM as an educational tool.

Prefer HTML over markdown? Read it here:
<https://whereisalan.dev/blog/pyxc-llvm-tutorial>

It is designed to be readable like Python, but much closer to C in behavior and power: pointers are first-class, memory can be manually managed, and you can absolutely shoot yourself in the foot. That is intentional. The project is about learning how languages and compilers work close to the machine, not hiding those edges.

## What this repo is

- A step-by-step compiler construction tutorial (`docs/chapter-XX.md`).
- Full source code per chapter (`code/chapter-XX`), so you can compare progression.
- A language tutorial (in progress) for writing non-trivial programs in `pyxc`.

## Why pyxc exists

- Teach compiler internals with a real codebase.
- Keep syntax approachable (Python-style indentation and control flow).
- Expose low-level behavior directly (types, pointers, allocation, file I/O).
- Make it easy to inspect IR, assembly, and memory effects.

## What You'll Build

The tutorial builds up in stages:

**Chapters 1–11** build a working language with a JIT REPL. By the end, this runs:

```python
extern def printd(x)

@binary(6)
def ^(base, exp):
    var result = 1
    for var i = 1, i <= exp, 1:
        result = result * base
    return result

def fib(n):
    if n <= 1: return n
    return fib(n - 1) + fib(n - 2)

def sum_squares(n):
    var total = 0
    for var i = 1, i <= n, 1:
        total = total + i * i
    return total

printd(fib(10))            # 55
printd(2 ^ 10)             # 1024
printd(sum_squares(10))    # 385
```

**Chapters 12–15** add a real toolchain: `--emit` modes for IR, assembly, object files, and native executables; LLD-based linking; and DWARF debug info with `-g`.

**Chapters 16–23** add a static type system and a C-style memory model — types, structs, pointers, pointer arithmetic, heap allocation, strings, type aliases, and fixed-size arrays. By the end, pyxc can do K&R-style systems programming:

```python
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

**Chapters 24–30** add an object model: `class` declarations, methods with `self`, constructors, visibility, traits, `impl` blocks, and generic traits. By the end, this runs:


```python
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

**Chapters 31–40** close the K&R compatibility gap: division and remainder, compound assignment, `++`/`--`, logical operators with short-circuit evaluation, `while`/`do-while`/`break`/`continue`, bitwise operators, `switch`, `elif`, character literals, unsigned integer types, and assignment-as-expression. By the end, pyxc can express everything in the first four chapters of *The C Programming Language*:

```python
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

**Chapters 41–43** add a module system: `module` declarations, `export` to mark public API, `import` for pyxc-to-pyxc dependencies without `extern def`, and a two-phase scan to handle cyclic imports.

```python
# app/math.pyxc
module app.math

export def add(x: int, y: int) -> int:
  return x + y

export def square(x: int) -> int:
  return x * x
```

```python
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

## Build and Run

Pick any chapter and build it:

```bash
cd code/chapter-11
cmake -S . -B build
cmake --build build
./build/pyxc
```

To run the chapter tests:

```bash
llvm-lit code/chapter-11/test/
```

## Project Layout

```text
.
├── docs/
│   ├── chapter-00.md   # tone, motivation, and a narrative tour of the tutorial
│   ├── chapter-01.md
│   └── ... chapter-43.md
├── code/
│   ├── chapter-01/
│   ├── chapter-02/
│   └── ... chapter-43/
│       ├── pyxc.cpp
│       ├── CMakeLists.txt
│       └── test/
└── README.md
```

## Chapters

All 43 chapters are complete. Each one is a standalone, buildable snapshot of the compiler at that stage — see [Project Layout](#project-layout).

### The Front End

- [Chapter 1: Analyzing program words](docs/chapter-01.md) — Break source text into tokens: keywords, identifiers, numbers, and single characters.
- [Chapter 2: The Parser and AST](docs/chapter-02.md) — Turn tokens into a tree with a recursive descent parser.
- [Chapter 3: Better Errors](docs/chapter-03.md) — Malformed-number detection, source locations, and caret-style diagnostics.

### Setting Up LLVM

- [Chapter 4: Installing LLVM](docs/chapter-04.md) — Install LLVM from source with everything needed: clang, lld, lldb, clangd, and lit.

### Code Generation

- [Chapter 5: Code Generation](docs/chapter-05.md) — Connect the AST to LLVM IR.

### Language Features

- [Chapter 6: JIT and Optimisation](docs/chapter-06.md) — LLVM optimisation passes and ORC JIT so expressions evaluate immediately in the REPL; adds `extern` for calling real C library functions.
- [Chapter 7: File Input Mode](docs/chapter-07.md) — Run source files through the same JIT pipeline as the REPL, plus a `-v` IR flag.
- [Chapter 8: Control Flow](docs/chapter-08.md) — Comparison operators, `if`/`else` expressions, `for` loops, and the Mandelbrot set in ASCII.
- [Chapter 9: User-Defined Operators](docs/chapter-09.md) — `@binary(N)` and `@unary` decorators so pyxc programs can define new operators.
- [Chapter 10: Mutable Variables](docs/chapter-10.md) — Mutable local variables and assignment via a temporary `var ... :` expression form.
- [Chapter 11: Statement Blocks](docs/chapter-11.md) — Real statement blocks and Python-style indentation; `if`, `for`, `var`, and `return` become statements.

### Toolchain

- [Chapter 12: Global Variables](docs/chapter-12.md) — Module-level `var` declarations, initialized before `main()` runs.
- [Chapter 13: Emitting Native Code](docs/chapter-13.md) — Compile straight to a file — object code, assembly, or IR — instead of only running through the JIT.
- [Chapter 14: One-Step Executables](docs/chapter-14.md) — `--emit exe` compiles and links a standalone executable in one command.
- [Chapter 15: Debug Info and the Optimisation Pipeline](docs/chapter-15.md) — `-g` for real debugger support, plus LLVM's full standard optimization levels.

### Types

- [Chapter 16: A Static Type System](docs/chapter-16.md) — Eight real types, explicit casts, type-aware arithmetic, and a strict assignment checker.

### Structs, Pointers, and the C Memory Model

- [Chapter 17: Structs](docs/chapter-17.md) — `struct` definitions and `.` field access, passed by value.
- [Chapter 18: Pointers](docs/chapter-18.md) — Pointer types, `addr(x)`, and `p[i]` indexing, so functions can modify the caller's data.
- [Chapter 19: Pointer Arithmetic](docs/chapter-19.md) — Pointer + integer, pointer distance, and pointer comparisons.
- [Chapter 20: Heap Allocation](docs/chapter-20.md) — `sizeof(T)` and pointer casts so pyxc can call `malloc`/`free` directly.
- [Chapter 21: String Literals and C Interop](docs/chapter-21.md) — String literals, escape sequences, and calling any C standard library function.
- [Chapter 22: Type Aliases](docs/chapter-22.md) — `type name = type`, purely cosmetic and free at runtime.
- [Chapter 23: Arrays](docs/chapter-23.md) — Fixed-size arrays, array literals, indexing, and array-to-pointer decay.

### OOP Core

- [Chapter 24: Classes](docs/chapter-24.md) — The `class` keyword as a new way to bundle data, distinct from `struct`.
- [Chapter 25: Methods and `self`](docs/chapter-25.md) — Methods defined inside a class body, called as `obj.method(args)`.
- [Chapter 26: Constructors](docs/chapter-26.md) — `__init__` and `ClassName(args)` syntax; instances always start zeroed out.
- [Chapter 27: Visibility](docs/chapter-27.md) — `public` and `private` on class fields and methods.
- [Chapter 28: Traits](docs/chapter-28.md) — Named method contracts, checked at compile time with no runtime cost.
- [Chapter 29: impl Blocks](docs/chapter-29.md) — `impl TraitName for ClassName:` to pick up a trait's contract after the fact.
- [Chapter 30: Generic Traits](docs/chapter-30.md) — Type parameters on traits, e.g. `trait Addable[T]`.

### K&R Compatibility

- [Chapter 31: Arithmetic Completeness](docs/chapter-31.md) — `/`, `%`, compound assignment, and prefix/postfix `++`/`--`.
- [Chapter 32: Logical Operators](docs/chapter-32.md) — `&&`, `||`, and `!` with real short-circuit evaluation and a dedicated `bool` type.
- [Chapter 33: Loop Completeness](docs/chapter-33.md) — `while`, `do/while`, `break`, and `continue`, correctly targeting nested loops.
- [Chapter 34: Bitwise Operators](docs/chapter-34.md) — `&`, `|`, `^`, `<<`, `>>`, `~`, integer-only with C-standard precedence.
- [Chapter 35: Switch](docs/chapter-35.md) — `switch` with integer cases, `default`, and no implicit fallthrough.
- [Chapter 36: `elif` Chains](docs/chapter-36.md) — Python-style `elif` so conditionals don't nest into a pyramid.
- [Chapter 37: Character Literals](docs/chapter-37.md) — `'a'`, `'\n'`, `'\t'`, and the rest.
- [Chapter 38: Unsigned Integer Types](docs/chapter-38.md) — `uint8` through `uint64`, with correct unsigned arithmetic and no silent signed/unsigned mixing.
- [Chapter 39: Assignment as Expression](docs/chapter-39.md) — `=` inside an expression, enabling patterns like `while (c = getchar()) != EOF`.
- [Chapter 40: Variadic Extern Functions](docs/chapter-40.md) — `extern def` with a variable number of arguments, so pyxc can call `printf`/`scanf`.

### Program Structure

- [Chapter 41: Module Declarations and Export](docs/chapter-41.md) — `module` names a compilation unit; `export` marks its public API.
- [Chapter 42: Imports](docs/chapter-42.md) — `import` pulls in another file's exported functions and types by name.
- [Chapter 43: Cyclic Imports](docs/chapter-43.md) — Two files that import each other compile correctly, without infinite recursion.

## Credits

This project builds on ideas from the LLVM Kaleidoscope tutorial and extends them into a Pythonic, systems-oriented learning track.

Kaleidoscope: <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html>

## License

MIT

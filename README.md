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

**Chapters 1–17 (Foundations through Native Toolchain)** build a working language with a JIT REPL, `elif`, full loop control, and a real native toolchain. By the end, this runs:

```python
extern def printd(x: float64)

def fib(n: int) -> int:
    if n <= 1: return n
    return fib(n - 1) + fib(n - 2)

def classify(n: int) -> int:
    if n < 0:
        return -1
    elif n == 0:
        return 0
    else:
        return 1

def sum_until(limit: int) -> int:
    var total: int = 0
    var i: int = 1
    while True:
        if i > limit:
            break
        total = total + i
        i = i + 1
    return total

printd(float64(fib(10)))          # 55.000000
printd(float64(classify(-5)))     # -1.000000
printd(float64(sum_until(10)))    # 55.000000
```

**Chapters 18–23 (Types and Typed Operations)** add a static type system, unsigned integers, DWARF debug info keyed to real types, logical and bitwise operators, and `switch`.

**Chapters 24–35 (Data and Memory, Expression and Mutation Conveniences)** add a C-style memory model — structs, pointers, pointer arithmetic, arrays, heap allocation, type aliases, strings, characters, Unicode, variadic `extern` functions, assignment-as-expression, and read-modify-write operators. By the end, pyxc can do K&R-style systems programming:

```python
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printf(fmt: ptr[int8], ...) -> int32

type string = ptr[int8]

struct Point:
  x: int
  y: int

def dot(p: ptr[Point], q: ptr[Point]) -> int:
  return p[0].x * q[0].x + p[0].y * q[0].y

def digit_count(n: int) -> int:
    var count: int = 0
    var x: int = n
    do:
        count += 1
        x /= 10
    while x != 0
    return count

def main() -> int:
  var raw: ptr[int8] = malloc(2 * sizeof(Point))
  var pts: ptr[Point] = ptr[Point](raw)
  pts[0].x = 3
  pts[0].y = 4
  pts[1].x = 1
  pts[1].y = 2
  var next: ptr[Point] = pts + 1
  printf("dot product: %ld\n", dot(pts, next))       # 11
  printf("digits in 12345: %ld\n", digit_count(12345)) # 5
  var msg: string = "done"
  printf("%s\n", msg)
  free(raw)
  return 0
```

**Chapters 36–42 (Object-Oriented Features)** add an object model: `class` declarations, methods with `self`, constructors, visibility, traits, `impl` blocks, and generic traits — built on top of a procedural language that's already comfortable to use. By the end, this runs:


```python
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

**Chapters 43–45 (Program Structure)** add a module system: `module` declarations, `export` to mark public API, `import` for pyxc-to-pyxc dependencies without `extern def`, and a two-phase scan to handle cyclic imports.

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
│   └── ... chapter-45.md
├── code/
│   ├── chapter-01/
│   ├── chapter-02/
│   └── ... chapter-45/
│       ├── pyxc.cpp
│       ├── CMakeLists.txt
│       └── test/
└── README.md
```

## Chapters

Chapters 1–45 are complete. Each one is a standalone, buildable snapshot of the compiler at that stage — see [Project Layout](#project-layout). That's the only committed sequence — Closures, Concurrency, and everything past it are still ahead but not committed to fixed chapter numbers; see [ROADMAP.md](ROADMAP.md).

### Foundations

- [Chapter 1: Analyzing Program Words](docs/chapter-01.md) — Break source text into tokens: keywords, identifiers, numbers, and single characters.
- [Chapter 2: The Parser and Syntax Tree](docs/chapter-02.md) — Turn tokens into a tree with a recursive descent parser.
- [Chapter 3: Encoding Precedence in the Grammar](docs/chapter-03.md) — Fixed grammar-tier functions replace general precedence-climbing; the architectural chapter everything else builds on.
- [Chapter 4: Completing Basic Arithmetic](docs/chapter-04.md) — Unary minus and `%`, closing the cliffhanger from Chapter 3 immediately instead of dozens of chapters later.
- [Chapter 5: Better Errors](docs/chapter-05.md) — Malformed-number detection, source locations, and caret-style diagnostics.

### LLVM and Execution

- [Chapter 6: Installing LLVM](docs/chapter-06.md) — Install LLVM from source with everything needed: clang, lld, lldb, clangd, and lit.
- [Chapter 7: Code Generation](docs/chapter-07.md) — Connect the AST to LLVM IR.
- [Chapter 8: JIT and Optimization](docs/chapter-08.md) — LLVM optimisation passes and ORC JIT so expressions evaluate immediately in the REPL; adds `extern` for calling real C library functions.
- [Chapter 9: File Input Mode](docs/chapter-09.md) — Run source files through the same JIT pipeline as the REPL, plus a `-v` IR flag.

### Statements and Control Flow

- [Chapter 10: Control Flow: `if`, `else`, and `for`](docs/chapter-10.md) — Comparison operators, `if`/`else` expressions, `for` loops, and the Mandelbrot set in ASCII.
- [Chapter 11: Mutable Variables](docs/chapter-11.md) — Mutable local variables and assignment.
- [Chapter 12: Statement Blocks](docs/chapter-12.md) — Real statement blocks and Python-style indentation; `if`, `for`, `var`, and `return` become statements.
- [Chapter 13: `elif` Chains](docs/chapter-13.md) — Python-style `elif` so conditionals don't nest into a pyramid — introduced the moment `if` becomes block-bodied.
- [Chapter 14: Loop Completeness](docs/chapter-14.md) — `while`, `do/while`, `break`, and `continue`, correctly targeting nested loops.

### Native Toolchain

- [Chapter 15: Global Variables](docs/chapter-15.md) — Module-level `var` declarations, initialized before `main()` runs.
- [Chapter 16: Emitting Native Code](docs/chapter-16.md) — Compile straight to a file — object code, assembly, or IR — instead of only running through the JIT.
- [Chapter 17: One-Step Executables](docs/chapter-17.md) — `--emit exe` compiles and links a standalone executable in one command.

### Types and Typed Operations

- [Chapter 18: A Static Type System](docs/chapter-18.md) — Eight real types, explicit casts, type-aware arithmetic, and a strict assignment checker.
- [Chapter 19: Unsigned Integer Types](docs/chapter-19.md) — `uint8` through `uint64`, with correct unsigned arithmetic and no silent signed/unsigned mixing.
- [Chapter 20: Debug Info and the Optimization Pipeline](docs/chapter-20.md) — `-g` for real debugger support with typed DWARF metadata, plus LLVM's full standard optimization levels.
- [Chapter 21: Logical Operators](docs/chapter-21.md) — `&&`, `||`, and `!` with real short-circuit evaluation.
- [Chapter 22: Bitwise Operators](docs/chapter-22.md) — `&`, `|`, `^`, `<<`, `>>`, `~`, integer-only with C-standard precedence.
- [Chapter 23: `switch`](docs/chapter-23.md) — `switch` with integer cases, `default`, comma-separated multi-value cases, and no implicit fallthrough.

### Data and Memory

- [Chapter 24: Structs](docs/chapter-24.md) — `struct` definitions and `.` field access, passed by value.
- [Chapter 25: Pointers](docs/chapter-25.md) — Pointer types, `addr(x)`, and `p[i]` indexing, so functions can modify the caller's data.
- [Chapter 26: Pointer Arithmetic](docs/chapter-26.md) — Pointer + integer, pointer distance, and pointer comparisons.
- [Chapter 27: Arrays](docs/chapter-27.md) — Fixed-size arrays, array literals, indexing, and array-to-pointer decay — before heap allocation, so bounded storage comes first.
- [Chapter 28: Heap Allocation](docs/chapter-28.md) — `sizeof(T)` and pointer casts so pyxc can call `malloc`/`free` directly.
- [Chapter 29: Type Aliases](docs/chapter-29.md) — `type name = type`, purely cosmetic and free at runtime — defined before it's used to name `string`.
- [Chapter 30: String Literals and C Interop](docs/chapter-30.md) — String literals, escape sequences, and calling any C standard library function.
- [Chapter 31: Character Literals](docs/chapter-31.md) — Simple C escapes and strict two-digit hexadecimal escapes for integer-valued characters.
- [Chapter 32: Unicode Literals](docs/chapter-32.md) — Unicode escapes and validated raw UTF-8 in character and string literals.
- [Chapter 33: Variadic Extern Functions](docs/chapter-33.md) — `extern def` with a variable number of arguments, so pyxc can call `printf`/`scanf`.

### Expression and Mutation Conveniences

- [Chapter 34: Assignment as Expression](docs/chapter-34.md) — `=` inside an expression, enabling patterns like `while (c = getchar()) != EOF`.
- [Chapter 35: Read-Modify-Write Operators](docs/chapter-35.md) — Compound assignment and prefix/postfix `++`/`--` — pure convenience, sequenced once lvalues and assignment semantics are established.

### Object-Oriented Features

- [Chapter 36: Classes](docs/chapter-36.md) — The `class` keyword as a new way to bundle data, distinct from `struct`.
- [Chapter 37: Methods and `self`](docs/chapter-37.md) — Methods defined inside a class body, called as `obj.method(args)`.
- [Chapter 38: Constructors](docs/chapter-38.md) — `__init__` and `ClassName(args)` syntax; instances always start zeroed out.
- [Chapter 39: Visibility](docs/chapter-39.md) — `public` and `private` on class fields and methods.
- [Chapter 40: Traits](docs/chapter-40.md) — Named method contracts, checked at compile time with no runtime cost.
- [Chapter 41: `impl` Blocks](docs/chapter-41.md) — `impl TraitName for ClassName:` to pick up a trait's contract after the fact.
- [Chapter 42: Generic Traits](docs/chapter-42.md) — Type parameters on traits, e.g. `trait Addable[T]`.

### Program Structure

- [Chapter 43: Module Declarations and Export](docs/chapter-43.md) — `module` names a compilation unit; `export` marks its public API.
- [Chapter 44: Imports](docs/chapter-44.md) — `import` pulls in another file's exported functions and types by name.
- [Chapter 45: Cyclic Imports](docs/chapter-45.md) — Two files that import each other compile correctly, without infinite recursion.

### Future (Potential) Track

None of this is committed — chapter numbers, grouping, and order can all change. It's a first-pass sequencing of real, already-written-down design intent, not a promise. Given here so the tutorial's full potential scope is visible in one place; see [ROADMAP.md](ROADMAP.md#future-potential-track) for full notes on each item, including dependencies between them.

#### Closures

- Lambda syntax and captured variables. Open question that blocks starting: capture semantics — by value is safe under the no-GC model; by reference means a closed-over variable has to outlive the closure, a real lifetime problem with no borrow-checker yet to enforce it.

```pyxc
def make_adder(n: int) -> ptr[def(int) -> int]:
  return \(x: int) -> int: x + n   # captures n — capture semantics still undecided

var add5: ptr[def(int) -> int] = make_adder(5)
printd(float64(add5(10)))  # 15.000000
```

#### Self-Hosted Testing and Coverage

- A `test/assert.pyxc` module other `.pyxc` tests can `import` (`assert.eq_int(actual, expected, label)` and friends), replacing hand-rolled printf-and-compare boilerplate. Needs nothing new — `export`/`import` and variadic `extern def` already cover it.
- pyxc-level code coverage — which lines of a `.pyxc` program executed, mirroring what `llvm-cov` does for `pyxc.cpp` itself. A real compiler feature (codegen would emit profiling counters tied to pyxc source locations), sequenced ahead of Concurrency since race/nondeterminism debugging is where it pays off first.

#### Concurrency

- Ownership rules for shared state, spawning tasks and threads, synchronization primitives, message passing, parallel loops and work partitioning, determinism/race debugging, and eventually parallelizing the compiler itself. The safety model needs deciding before the rest can be designed concretely.

```pyxc
def worker(ch: Channel[int]):
  ch.send(compute())

def main() -> int:
  var ch: Channel[int] = Channel[int]()
  spawn worker(ch)
  printd(float64(ch.recv()))
  return 0
```

#### Phase 11: Enums and Function Pointers

- Enums — `enum Color: Red, Green, Blue`; `switch`-friendly, no implicit int conversion; bundles `switch` exhaustiveness checking
- Function Pointers — `ptr[def(int, int) -> int]`; callbacks, `qsort`, dispatch tables

```pyxc
enum Direction:
  North, South, East, West

def opposite(d: Direction) -> Direction:
  switch d:
    case Direction.North: return Direction.South
    case Direction.South: return Direction.North
    case Direction.East:  return Direction.West
    case Direction.West:  return Direction.East
```

#### Phase 12: Generics Completion

- Generic Functions and Structs — `def max[T](a: T, b: T) -> T` and `struct Stack[T]`, monomorphised like C++ templates

```pyxc
def max[T](a: T, b: T) -> T:
  return a if a > b else b

struct Stack[T]:
  items: T[64]
  count: int
```

#### Phase 13: Standard Library

- `stdlib/stdio.pyxc`, `stdlib/stdlib.pyxc`, `stdlib/string.pyxc`, `stdlib/math.pyxc` — wraps existing `extern` capability, no new language features
- Built-in `print` — variadic, no `extern def` needed

```pyxc
import stdlib.stdio
import stdlib.math

def main() -> int:
  print("sqrt(2) =", sqrt(2.0))
  return 0
```

#### Phase 14: Language Ergonomics I — Expressions, Bindings, and Calls

- Ternary / Conditional Expression, `const` Bindings, Default Parameter Values, Named Arguments at Call Site, Multiple Return Values, `NULL` / Null Pointer Literal, `len()` and `in`

```pyxc
def clamp(x: int, lo: int = 0, hi: int = 100) -> int:
  return lo if x < lo else (hi if x > hi else x)

var quotient, remainder = divmod(17, 5)
```

#### Phase 15: Language Ergonomics II — Statements and Diagnostics

- `assert` Statement, `defer` Statement, `static` Local Variables, `goto` and Labels, Command-Line Arguments to `main`, Warn on Assignment in Condition

```pyxc
def read_first_line(path: ptr[int8]) -> int:
  var f: ptr[int8] = fopen(path, "r")
  assert f != NULL, "could not open file"
  defer fclose(f)
  return 0
```

#### Phase 16: Unicode Identifiers and String Interpolation

- Unicode Identifiers — non-ASCII names in `name`; needs XID tables, normalization policy, and a homoglyph/security decision
- String Interpolation — `f"result: {x}"` lowering to `sprintf`

```pyxc
var café_total: float64 = 4.50
printf(f"Total: {café_total}\n")
```

#### Phase 17: Type System Completion

- Optional / Nullable Types, Union Types, Bit-Fields, `const` Pointers, Multidimensional Arrays, Pointer to Array, `void` Pointer

```pyxc
def find(arr: int[10], target: int) -> Option[int]:
  for i in range(10):
    if arr[i] == target: return i
  return None
```

#### Phase 18: OOP Refinements

- Static Class Properties and Methods, Operator Overloading, `__str__` and Abstract Methods, Inheritance (pending an explicit design decision)

```pyxc
class Vec2:
  public x: float64
  public y: float64

  static def zero() -> Vec2:
    return Vec2(0.0, 0.0)

  def __add__(other: Vec2) -> Vec2:
    return Vec2(self.x + other.x, self.y + other.y)
```

#### Phase 19: Import and Module Refinements

- Selective Imports and Import Aliases, Directory Modules

```pyxc
from stdlib.io import printf, getchar
import stdlib.math as m

printf("sqrt(2) = %f\n", m.sqrt(2.0))
```

#### Phase 20: Generics-Enabled Ecosystem

- Generators and Iterators, Generic Collections (`List[T]`, `Dict[K,V]`, `Set[T]`)

```pyxc
var names: List[ptr[int8]] = List[ptr[int8]]()
names.append("Ada")
names.append("Grace")
for name in names:
  printf("%s\n", name)
```

#### Phase 21: Compile-Time Execution

- `comptime` Basics, `comptime` and Generics — Zig-style compile-time execution instead of a separate macro language

```pyxc
comptime def storage_type(n: int) -> type:
  if n <= 32: return int32
  return int64

var x: comptime storage_type(16) = 0   # x: int32 at compile time
```

#### Phase 22: Verification

- Verifier Phase 1 (Sequential, SMT-backed) — `requires`/`ensures`/`assert`/loop `invariant`, needs Enums
- Verifier Phase 2 (Concurrency-Aware) — thread interleavings, synchronization, memory-order rules, needs Concurrency

```pyxc
def divide(a: int, b: int) -> int:
  requires b != 0
  ensures result * b == a
  return a / b
```

#### Phase 23: Tooling and Quality

- Real Source Locations in Codegen Diagnostics — properly closes the stale-`CurrentTokenLocation` known bug
- Function Attributes, Escape Analysis and Stack Promotion, REPL Improvements, Incremental Compilation and Packaging, MCP Tool Export

```bash
pyxc build          # reads a project manifest, compiles + links
pyxc run main.pyxc  # incremental: only recompiles what changed
```

Also real but not chapter-shaped: a **Language Server (LSP)** — IDE tooling built around the compiler rather than a compiler feature itself.

## License

MIT

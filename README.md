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

The tutorial runs in three arcs:

**Chapters 1–11** build a working language with a JIT REPL. By the end, this runs:

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
│   ├── chapter-00.md   # overview and chapter guide
│   ├── chapter-01.md
│   └── ... chapter-30.md
├── code/
│   ├── chapter-01/
│   ├── chapter-02/
│   └── ... chapter-30/
│       ├── pyxc.cpp
│       ├── CMakeLists.txt
│       └── test/
└── README.md
```

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the full plan. Summary:

**Phase 1 — Foundations (Ch 1–11)** ✓
- **Ch 1–3** — Lexer, parser, AST, error diagnostics
- **Ch 4** — LLVM setup
- **Ch 5–7** — IR codegen, JIT, file mode
- **Ch 8–9** — Control flow (`if`/`for`), user-defined operators
- **Ch 10–11** — Mutable variables, statement blocks, indentation

**Phase 2 — Native Toolchain (Ch 12–15)** ✓
- **Ch 12** — Global variables (`var` at module scope, `llvm.global_ctors`)
- **Ch 13** — Object file output (`TargetMachine`, `PassBuilder`, `-O0..-O3`)
- **Ch 14** — Native executable linking (`--emit exe`, LLD, built-in runtime)
- **Ch 15** — Debug info (`-g`, `DIBuilder`, DWARF) and optimisation pipelines

**Phase 3 — Types and Memory (Ch 16–23)** ✓
- **Ch 16** — Static type system (`int`, `float64`, `bool`, `None`, typed params, casts) ✓
- **Ch 17** — Structs and field access ✓
- **Ch 18** — Pointers and address-of (`ptr[T]`, `addr`, `p[i]`, `p[i].field`) ✓
- **Ch 19** — Pointer arithmetic (`p + n`, `p - n`, `p - q`, pointer comparisons) ✓
- **Ch 20** — Heap allocation (`malloc`/`free`, `sizeof`, pointer casts) ✓
- **Ch 21** — String literals and C interop (`"hello"` as `ptr[int8]`, escape sequences) ✓
- **Ch 22** — Type aliases (`type string = ptr[int8]`, alias chains) ✓
- **Ch 23** — Fixed-size stack arrays (`T[N]`, `[1,2,3]` literals, indexing, decay) ✓

**Phase 4 — OOP Core (Ch 24–30)** ✓
- **Ch 24** — Class keyword and `IsClass` flag ✓
- **Ch 25** — Methods and implicit `self` pointer ✓
- **Ch 26** — Constructors (`__init__`, `ClassName(args)`, zero-init guarantee) ✓
- **Ch 27** — Visibility (`public`/`private`, `CanAccessClassMember`, `ClassScopeGuard`) ✓
- **Ch 28** — Traits (structural conformance, compile-time check, no vtable) ✓
- **Ch 29** — `impl` blocks (retroactive trait implementation) ✓
- **Ch 30** — Generic traits (`trait Addable[T]`, type substitution at conformance time) ✓

**Phase 5–7 — Modules, Control Flow Pass 2, Concurrency (Ch 31–45)**
See [ROADMAP.md](ROADMAP.md) for details.

## Credits

This project builds on ideas from the LLVM Kaleidoscope tutorial and extends them into a Pythonic, systems-oriented learning track.

Kaleidoscope: <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html>

## License

MIT

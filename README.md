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

**Chapters 12–15** add a real toolchain: subcommands (`repl`, `run`, `build`), object file output, native executable linking, and DWARF debug info.

**Chapters 16–20** add a type system, structs, pointers, C interop, and `while` — culminating in this:

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
│   └── ... chapter-11.md
├── code/
│   ├── chapter-01/
│   ├── chapter-02/
│   └── ... chapter-11/
│       ├── pyxc.cpp
│       ├── CMakeLists.txt
│       └── test/
└── README.md
```

## Roadmap

- **Ch 12** — Driver and modes (`repl`, `run`, `build`, `--emit`)
- **Ch 13** — Object file output (`TargetMachine`, `-O0..-O3`)
- **Ch 14** — Native executable linking (`--emit link`, `-o`)
- **Ch 15** — Debug info and inspection (DWARF, `nm`, `objdump`)
- **Ch 16** — Type system (`int`, `double`, typed params and returns)
- **Ch 17** — Structs and field access
- **Ch 18** — Pointers and address-of
- **Ch 19** — Strings and C interop (`printf`, `fopen`, `malloc`)
- **Ch 20** — `while` loops and the full Mandelbrot renderer

## Credits

This project builds on ideas from the LLVM Kaleidoscope tutorial and extends them into a Pythonic, systems-oriented learning track.

Kaleidoscope: <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html>

## License

MIT

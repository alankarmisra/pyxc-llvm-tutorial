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

**Chapter 16** adds a static type system: `int`, `int8`, `int16`, `int64`, `float32`, `float64`, `bool`, and `None` (void). Every parameter, variable, and return type is explicitly annotated. Explicit casts and a strict type checker are included.

**Chapters 17–20** add structs, pointers, C interop, and `while` — culminating in this:

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

def main() -> i32:
    width: int = 120
    height: int = 48
    max_iter: int = 64

    printf("max_iter (e.g. 64): ")
    scanf("%d", addr(max_iter))
    if max_iter < 1:
        max_iter = 64

    out: ptr[void] = fopen("mandel.pbm", "w")

    # PBM header
    fputs("P1\n", out)
    fputs("120 48\n", out)

    # reusable 3-byte pixel buffer: "1 \0" or "0 \0"
    pix: ptr[i8] = malloc[i8](3)
    pix[1] = 32   # ' '
    pix[2] = 0

    y: int = 0
    while y < height:
        x: int = 0
        while x < width:
            c: Complex
            c.re = -2.2 + 3.2 * x / width
            c.im = -1.2 + 2.4 * y / height
            it: int = mandel_escape(c, max_iter)
            if it == max_iter:
                pix[0] = 49  # '1'
            else:
                pix[0] = 48  # '0'
            fputs(pix, out)
            x = x + 1
        pix[0] = 10  # '\n'
        pix[1] = 0
        fputs(pix, out)
        pix[1] = 32
        y = y + 1

    free(pix)
    fclose(out)
    printf("wrote mandel.pbm\n")
    return 0

main()
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
│   └── ... chapter-16.md
├── code/
│   ├── chapter-01/
│   ├── chapter-02/
│   └── ... chapter-16/
│       ├── pyxc.cpp
│       ├── CMakeLists.txt
│       └── test/
└── README.md
```

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the full plan. Summary:

**Phase 2 — Native Toolchain (Ch 12–15)** ✓
- **Ch 12** — Global variables (`var` at module scope, `llvm.global_ctors`)
- **Ch 13** — Object file output (`TargetMachine`, `PassBuilder`, `-O0..-O3`)
- **Ch 14** — Native executable linking (`--emit exe`, LLD, built-in runtime)
- **Ch 15** — Debug info (`-g`, `DIBuilder`, DWARF) and optimisation pipelines

**Phase 3 — Types and Memory (Ch 16–21)**
- **Ch 16** — Static type system (`int`, `float64`, `bool`, `None`, typed params, casts) ✓
- **Ch 17** — Structs and field access
- **Ch 18** — Pointers and address-of
- **Ch 19** — Arrays
- **Ch 20** — Strings and C interop (`printf`, `fopen`, `malloc`)
- **Ch 21** — `while` loops and the full Mandelbrot renderer

**Phase 4 — Control Flow Extensions (Ch 22–24)**
- **Ch 22–23** — `match`/`case` with guards and defaults
- **Ch 24** — `for`/`in` with `range`

**Phase 5–7 — Modules, Classes, Concurrency (Ch 25–44)**
See [ROADMAP.md](ROADMAP.md) for details.

## Credits

This project builds on ideas from the LLVM Kaleidoscope tutorial and extends them into a Pythonic, systems-oriented learning track.

Kaleidoscope: <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html>

## License

MIT

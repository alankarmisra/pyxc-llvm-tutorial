# pyxc (Pixie)

`pyxc` is a Pythonic language and compiler built with LLVM as an educational tool.

Prefer HTML over markdown walls of text? Read it here:
<https://whereisalan.dev/blog/pyxc-llvm-tutorial>

It is designed to be readable like Python, but much closer to C in behavior and power: pointers are first-class, memory can be manually managed, and you can absolutely shoot yourself in the foot. That is intentional. The project is about learning how languages and compilers work close to the machine, not hiding those edges.

## ⚠️ THIS IS A RAPIDLY EVOLVING REPOSITORY

**Pyxc is under active development.** The codebase and documentation are evolving rapidly. You may encounter:

- Build failures
- Failing tests  
- Incomplete or inconsistent documentation
- Breaking changes between chapters

This warning will be removed once the core tutorial has been stabilized. Thank you for your patience!

**Current Status:** Chapters 1-9 are complete and stable. Chapters 10+ are being reviewed and revised.

## What this repo is

- A step-by-step compiler construction tutorial (`docs/chapter-00.md` ... `docs/chapter-04.md`).
- Full source code per chapter (`code/chapter-XX`), so you can compare progression.
- A language tutorial (in progress) for writing non-trivial programs in `pyxc`.

## Why pyxc exists

- Teach compiler internals with a real codebase.
- Keep syntax approachable (Python-style indentation and control flow).
- Expose low-level behavior directly (types, pointers, allocation, file I/O, syscalls-ish APIs).
- Make it easy to inspect IR, assembly, and memory effects.

## What You'll Build

By the end of this tutorial, you'll implement a compiler for Pyxc, a statically-typed Python-like language. Here's what Pyxc code looks like—a Mandelbrot set renderer with structs, types, control flow, pointers, and file I/O:

```py
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

    # PBM (plain text) header.
    fputs("P1\n", out)
    fputs("120 48\n", out)

    # 2-byte token + terminator: either "1 " or "0 ", reused for each pixel.
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
        pix[2] = 0
        y = y + 1

    free(pix)
    fclose(out)
    printf("wrote mandel.pbm\n")
    return 0

main()
```

This is the vibe of `pyxc`: Pythonic syntax, C-like control.

**Note:** Chapters 1-9 cover the foundational compiler pipeline (lexer through debug info). The features shown above (structs, control flow, types, pointers) are covered in later chapters currently under review. But by Chapter 9, you'll have a complete toolchain that compiles simple mathematical functions to native code!

### What Works in Chapters 1-9

Here's what you can build with the current stable chapters:

```python
# Simple mathematical functions (all values are doubles for now)
def square(x):
    return x * x

def distance(x1, y1, x2, y2):
    return sqrt(square(x1 - x2) + square(y1 - y2))

# Call external C library functions
extern def sqrt(x)
extern def sin(x)
extern def cos(x)

# Compile to object files and call from C++
distance(3.0, 4.0, 0.0, 0.0)  # returns 5.0
```

Simple, but it compiles to optimized native code, includes debug information, and demonstrates the complete compiler pipeline!

## Build and run

Use any chapter directory you want to explore.

```bash
cd code/chapter-04
make
./pyxc -i test/malloc_struct_roundtrip.pyxc
```

To run chapter tests (where available):

```bash
cd code/chapter-04/test
lit -sv .
```

## Project layout

```text
.
├── docs/
│   ├── chapter-00.md ... chapter-28.md   # tutorial text
├── code/
│   ├── chapter-01/
│   ├── chapter-02/
│   └── ... chapter-04/
└── README.md
```

## Roadmap

- Continue expanding the language tutorial.
- Continue hardening the compiler tutorial chapters.
- Self-hosting is a long-term direction: compile `pyxc` with `pyxc`, then document how.

## Credits

This project builds on ideas popularized by the LLVM Kaleidoscope tutorial and extends them into a Pythonic, systems-oriented learning track.

Kaleidoscope: <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html>

## License

MIT

# pyxc (Pixie)

`pyxc` is a Pythonic language and compiler built with LLVM as an educational tool.

Prefer HTML over markdown walls of text? Read it here:
<https://whereisalan.dev/blog/pyxc-llvm-tutorial>

It is designed to be readable like Python, but much closer to C in behavior and power: pointers are first-class, memory can be manually managed, and you can absolutely shoot yourself in the foot. That is intentional. The project is about learning how languages and compilers work close to the machine, not hiding those edges.

!!!warning
    Pyxc is rapidly evolving as is the documentation. Builds might fail, tests might fail too. This message will be removed once I've stabilized the core tutorial set. Thank you for your patience!

## What this repo is

- A step-by-step compiler construction tutorial (`chapter-00.md` ... `chapter-28.md`).
- Full source code per chapter (`code/chapterXX`), so you can compare progression.
- A language tutorial (in progress) for writing non-trivial programs in `pyxc`.

## Why pyxc exists

- Teach compiler internals with a real codebase.
- Keep syntax approachable (Python-style indentation and control flow).
- Expose low-level behavior directly (types, pointers, allocation, file I/O, syscalls-ish APIs).
- Make it easy to inspect IR, assembly, and memory effects.

## Current language snapshot

Larger example from current chapters: typed values, loops, `scanf`, `printf`,
file I/O, structs, pointers, and manual memory management.

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

## Build and run

Use any chapter directory you want to explore.

```bash
cd code/chapter28
make
./pyxc -i test/malloc_struct_roundtrip.pyxc
```

To run chapter tests (where available):

```bash
cd code/chapter28/test
lit -sv .
```

## Project layout

```text
.
├── chapter-00.md ... chapter-28.md   # tutorial text
├── code/
│   ├── chapter13/
│   ├── chapter14/
│   └── ... chapter28/
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

---
description: "What the linker actually does: symbol resolution, relocation, and how a runnable executable is assembled from object files."
---
# 11. Linking: Under the Hood

In Chapter 10 we ran:

```bash
$ ./pyxc build hello.pyxc --emit=exe
Wrote hello.o
Linked executable: hello
```

Two files appeared. This chapter explains what actually happened between those two lines.

There is no new code in this chapter. We're going to use `nm` and `objdump` to inspect the files we already produce and build a concrete mental model of what the linker does. This model will matter later when we add multiple modules, shared libraries, and separate compilation.

## Source Code

We'll use this simple program throughout:

```python
# hello.pyxc
extern def putchard(c)

def main(): return putchard(42.0)
```

Build the object file and executable:

```bash
$ ./build/pyxc build hello.pyxc --emit=obj
Wrote hello.o

$ ./build/pyxc build hello.pyxc --emit=exe
Wrote hello.o
Linked executable: hello
```

## What Is an Object File?

`hello.o` is machine code that is **not yet runnable**. It contains:

- The compiled body of `main`
- A reference to `putchard` that has **no address yet** - just a name

Think of it as a puzzle piece. It has a defined shape (the code it provides) and defined holes (the things it needs from elsewhere). The linker's job is to fit all the pieces together.

## Inspecting Symbols with `nm`

`nm` lists the symbols in an object file. A symbol is simply a name attached to an address.

```nasm
$ nm hello.o
0000000000000000 T _main
                 U _putchard
```

Two columns matter:

- The address (or blank)
- The type letter

**`T`** means the symbol is **defined** in the text (code) section. `_main` is defined here at address `0x0`.

**`U`** means the symbol is **undefined** - it's referenced but not defined here. `_putchard` has no address. The object file just says "I need something called `_putchard`, whoever has it."

Notice that `_main` is at address `0x0`. That's not its real address in memory - object files use placeholder addresses starting at zero. The real address gets assigned by the linker.

## What the Linker Resolves

Now look at the final executable:

```nasm
$ nm hello
00000001000004a0 T _main
00000001000004bc T _putchard
                 U dyld_stub_binder
```

Two things happened:

1. `_main` moved from `0x0` to `0x1000004a0` - the linker assigned it a real address
2. `_putchard` went from **undefined** (`U`) to **defined** (`T`) at `0x1000004bc` - the linker found it in `runtime.o` and pulled it in

This is **symbol resolution**: matching every `U` (undefined) to a `T` (defined) somewhere across all the input object files.

## What Relocation Does

Inside `hello.o`, the call to `putchard` uses a placeholder:

```nasm
$ objdump --disassemble hello.o

Disassembly of section __TEXT,__text:

0000000000000000 <_main>:
       0: stp   x29, x30, [sp, #-0x10]!
       4: mov   x8, #0x4045000000000000    ; the double 42.0
       8: fmov  d0, x8
       c: bl    0xc                         ; <--- placeholder!
      10: fcvtzs w0, d0                    ; convert result to int
      14: ldp   x29, x30, [sp], #0x10
      18: ret
```

The `bl` instruction at offset `0xc` means "branch and link" (i.e., call). But it branches to `0xc` - itself! That's the placeholder. The object file also records a **relocation entry** alongside the code:

```nasm
$ objdump --reloc hello.o

RELOCATION RECORDS FOR [__text]:
OFFSET   TYPE                  VALUE
0xc      ARM64_RELOC_BRANCH26  _putchard
```

This says: **at offset `0xc`, patch the instruction to branch to `_putchard`**.

The linker reads this record, looks up where `_putchard` ended up (`0x1000004bc`), and patches the instruction. In the final executable, the call is correct:

```nasm
$ objdump --disassemble hello

00000001000004a0 <_main>:
  1000004a0: stp   x29, x30, [sp, #-0x10]!
  1000004a4: mov   x8, #0x4045000000000000
  1000004a8: fmov  d0, x8
  1000004ac: bl    0x1000004bc             ; <--- patched! calls _putchard
  1000004b0: fcvtzs w0, d0
  1000004b4: ldp   x29, x30, [sp], #0x10
  1000004b8: ret

00000001000004bc <_putchard>:
  1000004bc: sub   sp, sp, #0x20
  ...
```

This is **relocation**: patching placeholder addresses in machine code with real addresses once everything has been laid out.

## What Gets Linked In

Our executable links three inputs:

```text
hello.o      (our compiled Pyxc code)
runtime.o    (putchard, printd - from runtime.c)
libSystem    (the OS standard library)
```

You can verify the library dependency:

```bash
$ otool -L hello
hello:
    /usr/lib/libSystem.B.dylib
```

`libSystem` on macOS provides the basic OS interface - memory, file I/O, and crucially the program **entry point**. When you run `hello`, the OS doesn't call `main` directly. It calls `_dyld_start` (the dynamic linker startup), which sets up the process, then calls `main`. That's why `libSystem` must always be linked in.

On Linux the equivalent is `libc` with `_start` as the entry point.

## The Full Picture

```text
hello.o          runtime.o         libSystem
  _main (T)        _putchard (T)     _start (entry)
  _putchard (U) ──►                  memory, I/O
       │
       └── relocation patched ──────►
                    ↓
              hello (executable)
                _main at 0x1000004a0
                _putchard at 0x1000004bc
                linked against libSystem
```

The linker:
1. Collects all object files (`hello.o`, `runtime.o`)
2. Resolves all undefined symbols (`U`) by finding matching definitions (`T`)
3. Assigns final addresses to everything
4. Patches all relocation entries with those final addresses
5. Records which shared libraries are needed (`libSystem`)
6. Writes the result in the platform's executable format (Mach-O on macOS, ELF on Linux)

## Platform Differences

The concepts above are universal. The file formats differ:

| Platform | Format  | Entry point  | Std library       | Tool to inspect |
|----------|---------|--------------|-------------------|-----------------|
| macOS    | Mach-O  | `_dyld_start`| `libSystem`       | `otool`, `nm`   |
| Linux    | ELF     | `_start`     | `libc`            | `readelf`, `nm` |
| Windows  | PE/COFF | `mainCRTStartup` | `MSVCRT`     | `dumpbin`       |

This is exactly why `PyxcLinker` has three code paths - one per format. The *what* is identical; only the *how* differs by platform.

## Why This Matters for Pyxc

Right now we link a single object file against a single runtime. As Pyxc grows:

- **Multiple source files** will produce multiple object files, all needing symbol resolution across them
- **`extern def`** declarations are precisely the `U` (undefined) symbols - they're resolved at link time
- **`runtime.c`** is our way of providing `T` (defined) symbols that Pyxc code can reference

Understanding this makes the flow from `extern def putchard(c)` in Pyxc to a working function call in the executable completely transparent: the compiler emits a `U` symbol, the linker finds the `T` symbol in `runtime.o`, patches the call instruction, and the program works.

## What's Next

Now that we have a working executable pipeline, it's time to make the language more expressive. Chapter 12 adds comparison and logical operators (`==`, `!=`, `<=`, `>=`, `and`, `or`, `not`) — the building blocks for control flow and our first real programs: Fibonacci and Mandelbrot.

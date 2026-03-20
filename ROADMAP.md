# Pyxc Roadmap (Planned)

## Chapter 12 — Driver & Modes
- Add subcommands: `repl`, `run`, `build`
- Add `--emit tokens|llvm-ir`
- Keep REPL semantics intact

## Chapter 13 — Object Files
- Add `TargetMachine` setup and host triple
- Emit `.o` files
- Honor `-O0..-O3`

## Chapter 14 — Native Executables
- Link `.o` + runtime object into an executable
- Add `-o` output path
- Default `build` to link

## Chapter 15 — Debug Info + Inspection
- Add `-g` with `DIBuilder`
- Emit DWARF into `.o`/exe
- Introduce `nm`/`objdump` basics

## Chapter 16 — Types & Typed Variables
- Add real types (`int`, `double`, `void`)
- Typed parameters and return types
- Typed local variables
- Type checking + casts

## Chapter 17 — Structs & Field Access
- `struct` declarations
- Layout + field offsets
- `.` field access (lvalue/rvalue)

## Chapter 18 — Pointers & Address‑Of
- `ptr[T]` type
- `addr(x)` / `&x`
- Pointer indexing `p[i]`

## Chapter 19 — Arrays
- Fixed-size arrays `T[N]`
- Stack allocation
- Indexing semantics
- Array-to-pointer decay in calls

## Chapter 20 — Strings & C Interop
- String literals
- `extern` for libc (`printf`, `fopen`, `fputs`, `scanf`)
- `malloc[T]` / `free`

## Chapter 21 — While Loops + Mandel
- `while` loops
- Full Mandelbrot example using structs, pointers, arrays, and I/O

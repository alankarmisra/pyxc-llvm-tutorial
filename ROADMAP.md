# Pyxc Chapter Roadmap

## Scope and Pacing
- One primary concept per chapter, runnable demo, small automated tests
- Each chapter starts from the previous chapter's codebase and adds only its delta
- Every chapter documents: grammar changes, AST changes, semantic rules, IR/codegen changes

---

## Phase 1: Foundations — Complete (Chapters 1–11)

| # | Title | Status |
|---|-------|--------|
| 1 | Lexer Basics | ✅ |
| 2 | Parser and AST | ✅ |
| 3 | Building LLVM from Source | ✅ |
| 4 | Command-Line Interface | ✅ |
| 5 | Code Generation to LLVM IR | ✅ |
| 6 | Optimization and JIT Execution | ✅ |
| 7 | User-Defined Operators | ✅ |
| 8 | Unary Operators | ✅ |
| 9 | Comparison and Logical Operators | ✅ |
| 10 | Mutable Variables and Assignment | ✅ |
| 11 | Statement Blocks and Indentation | ✅ |

---

## Phase 2: Native Toolchain (Chapters 12–15)

| # | Title | Notes |
|---|-------|-------|
| 12 | Driver and Modes | `repl`, `run`, `build` subcommands; `--emit tokens\|llvm-ir` |
| 13 | Object Files | `TargetMachine` setup, host triple, emit `.o`; honour `-O0`..`-O3` |
| 14 | Native Executables | Link `.o` + runtime into an executable; `-o` output path |
| 15 | Debug Info | `-g` with `DIBuilder`; emit DWARF; `nm`/`objdump` basics |

---

## Phase 3: Types and Memory (Chapters 16–21)

| # | Title | Notes |
|---|-------|-------|
| 16 | Types and Typed Variables | `int`, `double`, `void`; typed parameters, return types, type checking, casts |
| 17 | Structs and Field Access | `struct` declarations, layout, field offsets, `.` lvalue/rvalue |
| 18 | Pointers and Address-Of | `ptr[T]` type, `addr(x)` / `&x`, pointer indexing `p[i]` |
| 19 | Arrays | Fixed-size `T[N]`, stack allocation, indexing, array-to-pointer decay |
| 20 | Strings and C Interop | String literals, `extern` for libc (`printf`, `fopen`, `scanf`), `malloc`/`free` |
| 21 | `while` Loops + Mandelbrot | `while` loops; full Mandelbrot demo using structs, pointers, arrays, and I/O |

---

## Phase 4: Control Flow Extensions (Chapters 22–24)

| # | Title | Notes |
|---|-------|-------|
| 22 | `match`/`case` Basics | Pattern matching on scalar values |
| 23 | `match`/`case` Guards and Defaults | Guard expressions, wildcard patterns, exhaustiveness |
| 24 | `for`/`in` with `range` | Pythonic range-based loops, lower to existing `for` IR |

---

## Phase 5: Modules and Code Organization (Chapters 25–30)

| # | Title | Notes |
|---|-------|-------|
| 25 | Module Declarations and Namespaces | Module syntax, symbol visibility |
| 26 | `import` Basics | Single-file imports, name resolution |
| 27 | `export` and Public API | Public/private surfaces |
| 28 | Multi-File Name Resolution | Cross-module lookup and diagnostics |
| 29 | Cyclic Imports | Detection and resolution strategy |
| 30 | Incremental Rebuilds | Module caching, dependency tracking |

---

## Phase 6: Classes and Traits (Chapters 31–37)

| # | Title | Notes |
|---|-------|-------|
| 31 | Class Syntax and Field Layout | Class declarations, field offsets |
| 32 | Methods and `self` | Method dispatch, `self` as implicit first arg |
| 33 | Constructors and Initialization | Construction rules, field init order |
| 34 | Visibility and Encapsulation | Public/private members |
| 35 | Traits and Interfaces | Trait declarations and contracts |
| 36 | Trait Implementations and Dispatch | Impl blocks, static vs dynamic dispatch |
| 37 | Generic Traits and Constraints | Intro to constrained generics |

---

## Phase 7: Concurrency (Chapters 38–44)

| # | Title | Notes |
|---|-------|-------|
| 38 | Concurrency Model and Safety Rules | Overview, ownership rules for shared state |
| 39 | Spawning Tasks and Threads | Task/thread primitives |
| 40 | Shared State and Synchronization | Mutexes, atomics |
| 41 | Message Passing | Channels and queues |
| 42 | Parallel Loops and Work Partitioning | Data-parallel patterns |
| 43 | Determinism, Races, and Debugging | Race detection, deterministic replay |
| 44 | Parallel Compilation Pipeline | Parallelise the Pyxc compiler itself |

---

## Extension Track (No Fixed Chapter)
These can be inserted where they fit best:

- Error reporting with source spans and caret diagnostics
- Function attributes (`readnone`, `nounwind`) for better optimization
- Standard library bootstrap
- Pattern-matching exhaustiveness checks
- Escape analysis and stack-allocation wins
- Packaging and installable CLI workflow
- Multi-file compilation units and separate compilation

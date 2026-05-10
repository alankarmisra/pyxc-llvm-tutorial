# Pyxc Chapter Roadmap

## Scope and Pacing
- One primary concept per chapter, runnable demo, small automated tests
- Each chapter starts from the previous chapter's codebase and adds only its delta
- Every chapter documents: grammar changes, AST changes, semantic rules, IR/codegen changes

## Memory Model
Pyxc is explicitly **no-GC**. Memory is managed like C++/Rust: stack allocation by default, explicit `malloc`/`free` for the heap, and ownership conventions enforced by the programmer (not the runtime). A future ownership/borrow-checker phase may enforce these statically, but there will be no garbage collector.

## Python-Friendly Syntax Goals (Guiding Style)
- Prefer Pythonic surface syntax when it doesn't block clarity or IR goals
- `x: T = ...` (and optionally `x = ...`) instead of mandatory `var` for initialized bindings
- `def f(x: T) -> U:` type annotation style
- `for x in range(a, b, step):` instead of `for x = a, b, step:`
- Array literals like `[1,2,3]` for `T[N]`
- `and` / `or` / `not` keywords for boolean logic

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
| 12 | Driver and Modes | ✅ `repl`, `run`, `build` subcommands; `--emit tokens\|llvm-ir` |
| 13 | Object Files | ✅ `TargetMachine` setup, host triple, emit `.o`; honour `-O0`..`-O3` |
| 14 | Native Executables | ✅ Link `.o` + runtime into an executable; `-o` output path |
| 15 | Debug Info | ✅ `-g` with `DIBuilder`; emit DWARF; `nm`/`objdump` basics |

---

## Phase 3: Types and Memory (Chapters 16–23)

Pointer-first track for C/C++ learners:
- Prioritise pointer arithmetic + heap before arrays/string sugar.
- Keep `p[i]` on `ptr[T]` as the primary memory-access model early.

| # | Title | Notes |
|---|-------|-------|
| 16 | Types and Typed Variables | ✅ `int`, `float64`, `bool`, `void`; typed parameters, return types, type checking, casts |
| 17 | Structs and Field Access | ✅ `struct` declarations, layout, field offsets, `.` lvalue/rvalue |
| 18 | Pointers and Address-Of | ✅ `ptr[T]` type, `addr(x)`, pointer indexing `p[i]`, `p[i].field` |
| 19 | Pointer Arithmetic | ✅ `ptr ± int`, `ptr - ptr`, pointer comparisons, element-size aware offsets |
| 20 | Heap Allocation | ✅ `malloc`/`free` via `extern`, `sizeof(type)` compile-time sizing, pointer casts from raw buffers (`ptr[T](raw)`), and K&R-style `malloc(n * sizeof(T))` patterns |
| 21 | String Literals and C Interop | ✅ `"hello"` as `ptr[int8]`, null-terminated global constants, escape sequences, `extern` C runtime calls |
| 22 | Type Aliasing | ✅ `type name = type`, alias chains, alias/struct name conflict checks |
| 23 | Arrays and Array Literals | ✅ Fixed-size `T[N]`, stack allocation, indexing, decay, `[1,2,3]` initializers |
---

## Phase 4: OOP Core (Chapters 24–30)

| # | Title | Notes |
|---|-------|-------|
| 24 | Class Syntax and Field Layout | ✅ Class declarations, field offsets |
| 25 | Methods and `self` | ✅ Method dispatch, `self` as implicit first arg |
| 26 | Constructors and Initialization | ✅ Construction rules, field init order |
| 27 | Visibility and Encapsulation | ✅ Public/private members |
| 28 | Traits and Interfaces | ✅ Trait declarations and contracts |
| 29 | Trait Implementations and Dispatch | ✅ Impl blocks, static vs dynamic dispatch |
| 30 | Generic Traits and Constraints | ✅ Intro to constrained generics |

---

## Phase 5: Control Flow and Ergonomics (Chapters 31–37)

| # | Title | Notes |
|---|-------|-------|
| 31 | Arithmetic Completeness | ✅ `/`, `%`, and compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`); read-modify-write on lvalues |
| 32 | Logical Operators | ✅ `&&`, `||`, `!`; short-circuit codegen with control flow + PHI nodes |
| 33 | Loop Completeness | `while`, `do/while`, `break`, `continue`; loop header/exit stacks for control transfer |
| 34 | Bitwise Operators | `&`, `|`, `^`, `~`, `<<`, `>>`; integer-only operator semantics and precedence |
| 35 | `switch` | Native LLVM `switch` lowering; case/default dispatch and optimization potential |
| 36 | Increment and Decrement | Pre/post `++` and `--`; old-value preservation for post forms |
| 37 | K&R Payoff | `runtime.c` + `extern def` bridge (`printf`, `scanf`, `getchar`, `putchar`, `strlen`, `strcpy`, `strncpy`) and ported K&R-style programs |

---

## Phase 6: Program Structure (Chapters 38–41)

| # | Title | Notes |
|---|-------|-------|
| 38 | Module Declarations and Imports | `module`, `import`, `export`; public/private symbol visibility; single-file happy path |
| 39 | Multi-File Builds | Cross-module lookup, name resolution, diagnostics |
| 40 | Cyclic Imports and Caching | Detection, resolution strategy, incremental rebuild basics |
| 41 | Closures | Lambda syntax, captured variables, closure struct + function pointer in LLVM IR. **Note:** need to decide capture semantics before implementation — capture by value is safe with no-GC; capture by reference requires closed-over variables to outlive the closure (Rust-style lifetime problem). |
---

## Phase 7: Concurrency (Chapters 42–48)

| # | Title | Notes |
|---|-------|-------|
| 42 | Concurrency Model and Safety Rules | Overview, ownership rules for shared state |
| 43 | Spawning Tasks and Threads | Task/thread primitives |
| 44 | Shared State and Synchronization | Mutexes, atomics |
| 45 | Message Passing | Channels and queues |
| 46 | Parallel Loops and Work Partitioning | Data-parallel patterns |
| 47 | Determinism, Races, and Debugging | Race detection, deterministic replay |
| 48 | Parallel Compilation Pipeline | Parallelise the Pyxc compiler itself |

---

## Extension Track (No Fixed Chapter)
These can be inserted where they fit best:

- Error reporting with source spans and caret diagnostics
- Function attributes (`readnone`, `nounwind`) for better optimization
- Standard library bootstrap
- Pattern-matching exhaustiveness checks
- Escape analysis and stack-allocation wins
- Packaging and installable CLI workflow

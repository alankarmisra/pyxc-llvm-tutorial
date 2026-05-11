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

## Phase 5: Control Flow and Ergonomics (Chapters 31–40)

| # | Title | Notes |
|---|-------|-------|
| 31 | Arithmetic Completeness | ✅ `/`, `%`, compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`), and pre/post `++`/`--`; read-modify-write on lvalues |
| 32 | Logical Operators | ✅ `&&`, `||`, `!`; short-circuit codegen with control flow + PHI nodes |
| 33 | Loop Completeness | ✅ `while`, `do/while`, `break`, `continue`; loop header/exit stacks for control transfer |
| 34 | Bitwise Operators | ✅ `&`, `|`, `^`, `~`, `<<`, `>>`; integer-only operator semantics and precedence |
| 35 | `switch` | ✅ Native LLVM `switch` lowering; case/default dispatch and optimization potential |
| 36 | `if` / `elif` Chains | ✅ Python-style `elif` syntax sugar for conditional chains (`elif` == `else if`) |
| 37 | Character Literals | ✅ `'a'`, escaped chars (`'\n'`, `'\t'`, `'\0'`), and integer interoperability for C-style text processing |
| 38 | Unsigned Integer Types | ✅ `uint8/16/32/64`; signed/unsigned cast+promotion rules; unsigned comparisons and shifts (`lshr` vs `ashr`); `size_t` mapping to `uint64` |
| 39 | Assignment as Expression | ✅ `x = expr` in expression context; `(c = getchar()) != EOF` works; right-associative; lvalue-checked at parse time via `BuildAssignmentExpr` |
| 40 | Variadic Extern Functions | ✅ `extern def f(a: T, ...)` support; variadic call arity checks for fixed parameters; LLVM variadic function types |


---

## Phase 6: Program Structure (Chapters 41–44)

| # | Title | Notes |
|---|-------|-------|
| 41 | Module Declarations and Export | ✅ `module` names the compilation unit; `export` marks public API; multi-file compilation via `extern def` + `--emit exe`; cliffhanger for ch42 |
| 42 | Imports | ✅ `import app.math` resolves file, scans `export` signatures, injects prototypes; only exported symbols importable; struct/class/trait/type transfer; `--emit exe` auto-closure |
| 43 | Cyclic Imports and Caching | ✅ Two-phase scan (own exports first, then recurse); `InProgress`/`Done` state machine; import path cache; A→B→A cycle handled correctly |
| 44 | Closures | Lambda syntax, captured variables, closure struct + function pointer in LLVM IR. **Note:** need to decide capture semantics before implementation — capture by value is safe with no-GC; capture by reference requires closed-over variables to outlive the closure (Rust-style lifetime problem). |
---

## Phase 7: Concurrency (Chapters 45–51)

| # | Title | Notes |
|---|-------|-------|
| 45 | Concurrency Model and Safety Rules | Overview, ownership rules for shared state |
| 46 | Spawning Tasks and Threads | Task/thread primitives |
| 47 | Shared State and Synchronization | Mutexes, atomics |
| 48 | Message Passing | Channels and queues |
| 49 | Parallel Loops and Work Partitioning | Data-parallel patterns |
| 50 | Determinism, Races, and Debugging | Race detection, deterministic replay |
| 51 | Parallel Compilation Pipeline | Parallelise the Pyxc compiler itself |

---

## Known Bugs

- **Stale `CurLoc` in codegen diagnostics (partially fixed):** `CallExprAST` now captures the call-site location at parse time and uses `LogErrorVAt` for "unknown function" and "incorrect argument count" errors — those two now report the right line/column. All other AST node types (binary ops, field access, index expressions, etc.) still use the global `CurLoc`, which has advanced past the node by the time codegen runs. Full fix requires adding a `SourceLoc` field to `ExprAST` base and propagating it through every node constructor.
- `extern def` ABI signatures are trusted without verification; mismatched declared return/argument types vs actual C symbol types are not detected and can cause runtime/ABI bugs.

---

## Extension Track (No Fixed Chapter)
These can be inserted where they fit best:

- Error reporting with source spans and caret diagnostics
- Function attributes (`readnone`, `nounwind`) for better optimization
- Standard library bootstrap
- Generic collections roadmap: `List[T]`, `Dict[K,V]`, `Set[T]`, iteration protocols, and ownership-aware container semantics
- Generators/iterators (`yield`) and lazy sequence APIs; defer `range` builtin until generator model is in place
- Pattern-matching exhaustiveness checks
- Escape analysis and stack-allocation wins
- Packaging and installable CLI workflow
- Warn/error on assignment in conditions (`if x = 10`) unless explicitly parenthesized (lint/strict mode)

---

## Design Decision: `comptime` (Zig-style compile-time execution)

Rather than a separate macro language (Rust's `macro_rules!` / procedural macros), pyxc should pursue **Zig-style `comptime`**: arbitrary pyxc code that runs at compile time and produces pyxc values or types.

**Why not Rust-style macros:**
- `macro_rules!` is a separate pattern language bolted onto the compiler — ugly syntax, cryptic errors, doesn't feel like the rest of the language
- Procedural macros compile as separate dynamic libraries, hurt build times, and produce poor error spans
- Both systems require learning a second language inside the language

**Why `comptime`:**
- No special macro syntax — `comptime` is just a keyword that says "evaluate this now, at compile time"
- The full pyxc language is available at compile-time: loops, functions, conditionals, data structures
- Errors point at real pyxc code, not opaque token streams
- pyxc already has a JIT; running pyxc code at compile time is conceptually very close to what the JIT already does — the infrastructure is mostly there
- Enables: compile-time constants, type-level computation, code generation, generic specialisation, and zero-cost abstractions without a separate template or macro system

**What this looks like (sketch):**
```pyxc
comptime def array_sum_type(n: int) -> type:
  if n <= 32: return int
  return int64

var x: comptime array_sum_type(16) = 0   # x: int at compile time
```

**When to implement:** After modules (Phase 6) and after the type system is stable. `comptime` interacts with generics, modules, and the import graph — it should not be retrofitted into an unstable foundation. Likely Phase 8 or later.

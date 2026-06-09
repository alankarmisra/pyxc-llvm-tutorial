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

## Chapter 44-0 (Pre-Closures Follow-up)

- **Cross-module custom operators in module/import mode**
  - Current import/signature scan wires exported function/type signatures, but does not import operator parser metadata (`@binary`/`@unary`, precedence, arity).
  - Result: custom operators defined in another module are not reliably available as operators in importing modules, even if exported.
  - Needed: import-time operator metadata registration so dependent modules can parse expressions using exported custom operators.

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
- **`self.method()` calls from within a method do not work.** Inside a method body, `self` is typed as `ptr[ClassName]`. Field access (`self.field`) auto-derefs via GEP. Method dispatch (`self.method()`) does not — it sees `ptr[ClassName]` and rejects it with "Method call base must be a class/struct value". Workaround: extract the helper as a free function outside the class. Fix requires auto-deref of `self` in the method call dispatcher when the receiver type is `ptr[ClassName]`.
- **JIT file mode does not compile imported module bodies.** `pyxc file.pyxc` (without `--emit`) signature-scans imports but never compiles their method/function bodies. Calling an imported function produces a JIT link error: "Symbols not found". Workaround: use `--emit exe` for any multi-file program. Fix requires expanding the import closure and JIT-compiling each imported file's bodies before executing the entry file.
- **`extern def` ABI signatures are trusted without verification;** mismatched declared return/argument types vs actual C symbol types are not detected and can cause runtime/ABI bugs.

---

## Extension Track (No Fixed Chapter)
These can be inserted where they fit best, ordered roughly by implementation difficulty within each group.

### Language Ergonomics
- **Ternary / conditional expression** — `x if cond else y` (Pythonic) or `cond ? x : y`; one new AST node, precedence just below assignment. Very common in idiomatic code.
- **`assert` statement** — `assert cond` and `assert cond, "message"`; lowers to a conditional `abort()` call. Trivial to implement.
- **`const` bindings** — `const PI: float64 = 3.14159`; immutable at compile time, stored as an LLVM constant rather than an alloca. Prevents accidental mutation.
- **Default parameter values** — `def f(x: int, y: int = 0) -> int`; caller omits trailing args, compiler fills in constants. No overloading needed.
- **Named arguments at call site** — `f(y=5, x=3)`; reorder or omit args by name. Requires matching names at the call site, not a new type system concept.
- **`defer` statement** — `defer free(p)`; executes at function exit regardless of return path. Essential for clean resource management in no-GC code. Lowers to cleanup blocks at every `return` and fall-through exit.
- **Multiple return values** — `def divmod(a: int, b: int) -> (int, int): return a/b, a%b`; anonymous tuple type, unpacked at call site: `q, r = divmod(10, 3)`. Requires tuple type and lvalue unpacking.
- **Warn on assignment in condition** — `if x = 10` should warn (or error in strict mode) unless explicitly parenthesized: `if (x = 10)`. Low-hanging lint win.
- **String interpolation** — `f"result: {x}"` producing a `ptr[int8]`; lowers to `sprintf` into a stack buffer. Decide on buffer-size strategy before implementing.
- **`static` local variables** — `static counter: int = 0` inside a function; persists between calls. Lowers to a module-level global with a mangled name. Common K&R pattern.
- **`goto` and labels** — `goto cleanup` and `cleanup:` label; useful for K&R-style error unwind and for generated code. Low priority but needed for full C compatibility.
- **`NULL` / null pointer literal** — `null` or `nil` as a typed null pointer constant; currently requires `ptr[T](0)`. Interacts with optional types.
- **Command-line arguments to `main`** — `def main(argc: int32, argv: ptr[ptr[int8]]) -> int`; currently `main` takes no arguments. Required for any CLI tool written in pyxc.
- **`len()` built-in** — `len(arr)` returns the element count of a fixed-size array as a compile-time constant; avoids manual tracking of `N` in `T[N]`.
- **`in` operator** — `x in arr` membership test; lowers to a linear scan for arrays, hook for trait-based custom containers later.

### Types
- **Enums** — `enum Color: Red, Green, Blue` or `enum Status: Ok = 0, Err = 1`; named integral constants with optional explicit values; `switch`-friendly; no implicit int conversion.
- **Function pointers** — `ptr[def(int, int) -> int]`; enables callbacks, `qsort`, dispatch tables, and is a prerequisite for the self-hosting bootstrap plan. Medium complexity: needs a new type encoding and call-site codegen path.
- **Generic functions and structs** — `def max[T](a: T, b: T) -> T` and `struct Stack[T]`; monomorphised at instantiation like C++ templates, not erased like Java generics. Prerequisite for `List[T]`, `Dict[K,V]`, etc.
- **Optional / nullable types** — `T?` or `Option[T]`; `None` as a value, not just a return type. Forces callers to check before use. Interacts with pointer nullability.
- **Union types** — C-style `union`; same memory, multiple interpretations. Needed for systems/protocol programming and certain K&R patterns.
- **Bit-fields** — `struct Flags: bits: uint8[3]` style or `@bitfield` annotation; required for hardware register maps and packed binary formats.
- **`const` pointers** — `ptr[const int8]` for read-only data; catches accidental writes through string literals and read-only memory regions.
- **Multidimensional arrays** — `int[3][3]` as `array(array(int, 3), 3)`; currently blocked with "Nested arrays are not supported". Requires recursive type encoding and multi-level GEP.
- **Pointer to array** — `ptr[int[4]]`; currently blocked with "Pointers to array types are not supported". C's `int (*p)[4]` pattern; useful for passing fixed-size rows to functions.
- **`void` pointer / untyped pointer** — `ptr` without a type argument as a generic handle; equivalent to C's `void *`. Currently requires `ptr[int8]` as a workaround, which loses intent.

### OOP / Classes
- **`self.method()` calls from within a method** — currently broken (see Known Bugs); calling a method on `self` from another method of the same class fails because `self` is typed as `ptr[ClassName]` internally. Fix: auto-deref in the method call dispatcher.
- **Static class properties and methods** — `class Foo: static count: int = 0` and `static def create() -> Foo`; callable as `Foo.count` / `Foo.create()` with no instance. Stored as a module-level global with a mangled name.
- **Operator overloading on class instances** — `impl Add for Vec2` style, or `def __add__(other: Vec2) -> Vec2` inside the class. Currently only global `@binary`/`@unary` operators exist; class-specific dispatch is missing.
- **`__str__` method** — `def __str__() -> ptr[int8]`; called by a built-in `str(x)` or `print(x)` to get a human-readable representation.
- **Abstract methods** — mark a trait method as requiring implementation; compiler errors if `impl` block omits it. (Traits already check conformance, so this may just be a documentation/annotation gap.)
- **Inheritance** — deliberate design decision needed: pyxc currently uses traits for interface polymorphism and composition for code reuse. Single-inheritance with `class B(A)` is possible but conflicts with the no-vtable trait model. Recommend deciding explicitly rather than leaving open.

### Imports / Modules
- **Selective imports** — `from stdlib.io import printf, getchar`; imports named symbols without polluting the top-level namespace. Requires tracking which module a symbol came from.
- **Import alias** — `import stdlib.io as io` and `io.printf(...)`; qualified access prevents name collisions across large import graphs.
- **Directory modules** — `__init__.pyxc` as the entry point for `import mylib`; enables distributing a library as a directory rather than a single file.
- **JIT multi-file execution** — `pyxc file.pyxc` currently only JIT-compiles the entry file; imported module bodies are not compiled (see Known Bugs). Fix: expand import closure and JIT-compile each imported file before executing entry.

### Standard Library
- **`stdlib/stdio.pyxc`** — `export extern def printf`, `scanf`, `getchar`, `putchar`, `fopen`, `fclose`, `fread`, `fwrite`, `fprintf`, `fgets`, `EOF` constant. Eliminates boilerplate `extern def` in every program.
- **`stdlib/stdlib.pyxc`** — `malloc`, `free`, `realloc`, `qsort`, `bsearch`, `exit`, `atoi`, `atof`, `strtol`.
- **`stdlib/string.pyxc`** — `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `memcpy`, `memset`, `memcmp`.
- **`stdlib/math.pyxc`** — `sin`, `cos`, `sqrt`, `pow`, `fabs`, `floor`, `ceil`, `log`, `exp`.
- **Built-in `print`** — `print("hello", x)` as a variadic built-in that calls `printf` internally; no `extern def` needed.

### Tooling
- **Error reporting with source spans** — fix the stale `CurLoc` bug (see Known Bugs); add `SourceLoc` field to `ExprAST` base; propagate through every node constructor.
- **Function attributes** — `@noinline`, `@alwaysinline`, `@nounwind`, `@readnone` as decorators; maps directly to LLVM function attribute enums.
- **Incremental compilation** — cache compiled `.o` files by content hash; skip recompilation when source and imports are unchanged.
- **Packaging and installable CLI** — `pyxc build`, `pyxc run`, `pyxc init`; project manifest file; installable binary distribution.
- **Language server (LSP)** — hover types, go-to-definition, error squiggles in editors.
- **REPL improvements** — multi-line input (continue on trailing `:`), command history, tab completion for defined names.
- **Pattern-matching exhaustiveness checks** — verify `switch` over an `enum` covers all cases; warn on unhandled variants.
- **Escape analysis and stack-allocation wins** — detect heap-allocated objects that don't escape the function; replace `malloc`/`free` with stack allocas automatically.
- **Generic collections** — `List[T]`, `Dict[K,V]`, `Set[T]`; requires generic structs first. Iteration protocols and ownership-aware container semantics.
- **Generators / iterators** — `yield` and lazy sequence APIs; `range` builtin; defer until generator model is decided.
- **Verifier Phase 1 (sequential, SMT-backed)** — after enums and before concurrency: add `requires` / `ensures` / `assert` / loop `invariant`; generate verification conditions for single-threaded code; `pyxc --verify` mode; start with `int`/`bool`/control-flow subset and explicit unsupported diagnostics for heap-alias-heavy proofs.
- **Verifier Phase 2 (concurrency-aware)** — after concurrency lands: extend verification model to thread interleavings, synchronization primitives, and memory-order rules; likely state-machine/temporal reasoning track (TLA+-style specs or model-checker integration) plus race-freedom checks.

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

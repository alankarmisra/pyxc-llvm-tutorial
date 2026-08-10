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

## Sequencing Notes

This roadmap reflects a resequenced chapter order (2026-08). The content of every chapter below has already been written; what's being reordered is *when* each concept is introduced, so that:
- Convenience/sugar features (`elif`, compound assignment, `++`/`--`) sit immediately next to the mechanism they're sugar over, instead of being deferred dozens of chapters past it.
- Real capability gaps (`break`/`continue`, `%`, unsigned types) land close to where a reader would first need them, instead of being bundled into a late "completeness" chapter under a misleading title.
- The object-oriented chapters come after the procedural/systems core is comfortable to use, not while `while`, `&&`, `%`, unsigned integers, character literals, and full `printf` support are all still missing.
- Debug info comes after the static type system, so it teaches real per-type DWARF metadata the first time instead of `double`-only metadata that's silently superseded later without ever being revisited.
- Type aliases come before the string/C-interop chapters, so `string` is a name the reader already knows before it's used in examples.
- Arrays come before Heap Allocation, so readers get comfortable with a bounded, no-manual-cleanup storage model before taking on manual memory lifetime management.

Chapters 1–45 below are the complete, shipped baseline under the new numbering. Chapter 46 (Closures) and Phase 10 (Concurrency, 47–53) are still ahead.

---

## Phase 1: Foundations (Chapters 1–5)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 1 | Analyzing Program Words | ✅ | Lexer: tokens, keywords, identifiers, numbers |
| 2 | The Parser and Syntax Tree | ✅ | Recursive-descent parser, AST node classes |
| 3 | Encoding Precedence in the Grammar | ✅ | Fixed grammar-tier functions (`sum`, `term`, ...); the chapter that replaced general precedence-climbing and any custom/user-defined operator mechanism with hand-written grammar tiers |
| 4 | Completing Basic Arithmetic | ✅ | Unary minus and `%` (remainder); closes the "I'll add unary operators later" cliffhanger from Chapter 3 in the very next chapter |
| 5 | Better Errors | ✅ | Source locations, caret-style diagnostics |

---

## Phase 2: LLVM and Execution (Chapters 6–9)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 6 | Installing LLVM | ✅ | Environment setup; no language changes |
| 7 | Code Generation | ✅ | AST → LLVM IR |
| 8 | JIT and Optimization | ✅ | ORC LLJIT, fixed optimization pass list, `extern` for C library calls |
| 9 | File Input Mode | ✅ | Run source files through the same pipeline as the REPL |

---

## Phase 3: Statements and Control Flow (Chapters 10–14)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 10 | Control Flow: `if`, `else`, and `for` | ✅ | Comparison operators, ternary-style `ifexpr`, `for` loops |
| 11 | Mutable Variables | ✅ | `var` bindings, assignment |
| 12 | Statement Blocks | ✅ | Python-style indentation; `if` becomes a block-bodied statement, not an expression |
| 13 | `elif` Chains | ✅ | Immediately after `if` becomes block-bodied — no reason to make readers manually nest `if`/`else` for 25+ chapters first. Desugars directly to nested `IfStatementNode`s, no new AST node |
| 14 | Loop Completeness | ✅ | `while`, `do`/`while`, `break`, `continue` — closes a real capability gap (no way to exit a loop early) as soon as loops exist, not dozens of chapters later |

---

## Phase 4: Native Toolchain (Chapters 15–17)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 15 | Global Variables | ✅ | Module-level `var`, persists across REPL inputs and compiled files |
| 16 | Emitting Native Code | ✅ | Object-file emission without the JIT |
| 17 | One-Step Executables | ✅ | `--emit exe`, LLD as a library |

---

## Phase 5: Types and Typed Operations (Chapters 18–23)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 18 | A Static Type System | ✅ | `int`, `int8`..`int64`, `float`, `float32`/`float64`, `bool`, `None`; typed parameters, return types, casts |
| 19 | Unsigned Integer Types | ✅ | `uint8/16/32/64`, right after the type system — signedness exists before bitwise operations and systems-memory examples need it |
| 20 | Debug Info and the Optimization Pipeline | ✅ | `-g` DWARF debug info, LLVM's standard `-O0`..`-O3` pipelines. Sequenced after the type system so it teaches real per-type debug metadata (`IntDIType`, `Float32DIType`, ...) from the start, not `double`-only metadata |
| 21 | Logical Operators | ✅ | `&&`, `||`, `!`; short-circuit codegen with control flow + PHI nodes |
| 22 | Bitwise Operators | ✅ | `&`, `|`, `^`, `~`, `<<`, `>>`; integer-only operator semantics and precedence |
| 23 | `switch` | ✅ | Native LLVM `switch` lowering; case/default dispatch. Comma-separated multi-value case lists (`case 'a', 'e', 'i':`) land here |

---

## Phase 6: Data and Memory (Chapters 24–33)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 24 | Structs | ✅ | `struct` declarations, field read/write, nested field access |
| 25 | Pointers | ✅ | `ptr[T]` type, `addr(x)`, pointer indexing `p[i]` |
| 26 | Pointer Arithmetic | ✅ | `ptr + int`, `ptr - int`, `ptr - ptr`, pointer comparisons |
| 27 | Arrays | ✅ | Fixed-size `T[N]`, stack allocation, indexing, `[1,2,3]` initializers, array-to-pointer decay. Deliberately before Heap Allocation — readers get comfortable with bounded, automatically-cleaned-up storage before taking on manual lifetime management |
| 28 | Heap Allocation | ✅ | `sizeof` and pointer casts so pyxc can call `malloc`/`free`. "No heap arrays" in Chapter 27 forward-references this chapter as the tool for dynamically sized data |
| 29 | Type Aliases | ✅ | `type name = type`, alias chains, alias/struct name conflict checks. Before Strings so `type string = ptr[int8]` is a name the reader already knows |
| 30 | String Literals and C Interop | ✅ | String literals; calls `puts`, `printf`, and other C standard library functions directly, using the `string` alias from Chapter 29 |
| 31 | Character Literals | ✅ | `'a'`, simple C escapes, strict two-digit hexadecimal escapes, integer interoperability |
| 32 | Unicode Literals | ✅ | Octal and Unicode escapes, validated raw UTF-8, integer code points for characters, UTF-8 strings |
| 33 | Variadic Extern Functions | ✅ | `extern def f(a: T, ...)`; completes real `printf`/`scanf`-style C interop while the strings/characters motivation is fresh |

---

## Phase 7: Expression and Mutation Conveniences (Chapters 34–35)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 34 | Assignment as an Expression | ✅ | `x = expr` in expression context; `(c = getchar()) != EOF` works; right-associative; lvalue-checked at parse time via `BuildAssignmentExpr` |
| 35 | Read-Modify-Write Operators | ✅ | `+=`, `-=`, `*=`, `/=`, `%=`, and prefix/postfix `++`/`--`. Pure convenience over `x = x + 1` — programs don't need these to express the same operations, so they're sequenced late on purpose, once lvalues, types, pointers, and assignment semantics are all established |

---

## Phase 8: Object-Oriented Features (Chapters 36–42)

Sequenced after the procedural/systems-language core is complete — readers should have a comfortable, fully-featured language before building its object model, not reach generic traits while still missing loops, `%`, unsigned integers, and full C interop.

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 36 | Classes | ✅ | `class` keyword as a second way to declare an aggregate type, sharing `struct`'s parsing/layout machinery |
| 37 | Methods and `self` | ✅ | Methods defined in the class body, called with `obj.method(args)`, implicit `self` pointer |
| 38 | Constructors | ✅ | `__init__`, called via `ClassName(args)`; instances are zero-initialised before `__init__` runs |
| 39 | Visibility | ✅ | Public/private visibility modifiers on fields and methods |
| 40 | Traits | ✅ | Named method-signature contracts; conformance verified at compile time, no runtime overhead |
| 41 | `impl` Blocks | ✅ | Implement a trait for an existing class after the class definition |
| 42 | Generic Traits | ✅ | Type parameters on traits: `trait Addable[T]`; classes instantiate with a concrete type at the `impl` site |

---

## Phase 9: Program Structure (Chapters 43–46)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 43 | Module Declarations and Export | ✅ | `module` names the compilation unit; `export` marks public API; multi-file compilation via `extern def` + `--emit exe` |
| 44 | Imports | ✅ | `import app.math` resolves file, scans `export` signatures, injects prototypes; only exported symbols importable; struct/class/trait/type transfer; `--emit exe` auto-closure |
| 45 | Cyclic Imports | ✅ | Two-phase scan (own exports first, then recurse); `InProgress`/`Done` state machine; import path cache; A→B→A cycle handled correctly |
| 46 | Closures | ⬜ | Lambda syntax, captured variables, closure struct + function pointer in LLVM IR. **Open question:** capture semantics need to be decided before implementation — capture by value is safe with no-GC; capture by reference requires closed-over variables to outlive the closure (Rust-style lifetime problem) |

---

## Phase 10: Future Concurrency Track (Chapters 47–53)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 47 | Concurrency Model and Safety Rules | ⬜ | Overview, ownership rules for shared state |
| 48 | Spawning Tasks and Threads | ⬜ | Task/thread primitives |
| 49 | Shared State and Synchronization | ⬜ | Mutexes, atomics |
| 50 | Message Passing | ⬜ | Channels and queues |
| 51 | Parallel Loops and Work Partitioning | ⬜ | Data-parallel patterns |
| 52 | Determinism, Races, and Debugging | ⬜ | Race detection, deterministic replay |
| 53 | Parallel Compilation Pipeline | ⬜ | Parallelise the Pyxc compiler itself |

---

## Known Bugs

- **Stale `CurLoc` in codegen diagnostics:** Every codegen error (`LogErrorV`) reports the global `CurLoc`, which has already advanced past the node by the time codegen runs — e.g. an "Unknown function referenced" error points at the token after the call's closing paren, not at the callee name. Fix requires adding a `SourceLoc` field to `ExpressionNode` and propagating it through every node constructor.
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
- **Unicode identifiers** — allow non-ASCII names (`café`, `变量`) in `name`, not just string/char literal content. Prerequisite: Chapter 32 (Unicode Literals), which already has UTF-8 decoding but deliberately stops short of this. Moderately hard, medium-sized chapter on its own: needs `XID_Start`/`XID_Continue` Unicode property tables (not a hand-rolled range check), a normalization decision (typically NFC — store normalized or preserve original spelling?), diagnostics/column math that work correctly across multibyte characters, and a policy on visually confusing identifiers (Latin `a` vs. Cyrillic `а`) — a real security question, not just an implementation detail, since this is effectively the IDN-homograph problem applied to source code. A naive "accept any non-ASCII scalar" version is a few hours of work but wrongly accepts combining marks and punctuation in identifier position — not recommended. Proper XID classification with generated tables, normalization, diagnostics, and tests is roughly a full focused day, more if avoiding an external Unicode library. Best inserted immediately after Unicode Literals if picked up, but not committed to a fixed chapter number until the normalization/homoglyph policy is actually decided.

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
- **Operator overloading on class instances** — `impl Add for Vec2` style, or `def __add__(other: Vec2) -> Vec2` inside the class. pyxc currently has no operator-overloading mechanism at all; operators are fixed-precedence built-ins over scalar types only.
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
- **Error reporting with source spans** — fix the stale `CurLoc` bug (see Known Bugs); add `SourceLoc` field to `ExpressionNode` base; propagate through every node constructor.
- **Function attributes** — `@noinline`, `@alwaysinline`, `@nounwind`, `@readnone` as decorators; maps directly to LLVM function attribute enums.
- **Incremental compilation** — cache compiled `.o` files by content hash; skip recompilation when source and imports are unchanged.
- **Packaging and installable CLI** — `pyxc build`, `pyxc run`, `pyxc init`; project manifest file; installable binary distribution.
- **Language server (LSP)** — hover types, go-to-definition, error squiggles in editors.
- **REPL improvements** — multi-line input (continue on trailing `:`), command history, tab completion for defined names.
- **Pattern-matching exhaustiveness checks** — verify `switch` over an `enum` covers all cases; warn on unhandled variants.
- **Escape analysis and stack-allocation wins** — detect heap-allocated objects that don't escape the function; replace `malloc`/`free` with stack allocas automatically.
- **Generic collections** — `List[T]`, `Dict[K,V]`, `Set[T]`; requires generic structs first. Iteration protocols and ownership-aware container semantics.
- **Generators / iterators** — `yield` and lazy sequence APIs; `range` builtin; defer until generator model is decided. Once ranges exist, extend `switch` case matching to accept them too (`case 1...5:`), alongside the comma-separated multi-value case lists added in Chapter 23.
- **MCP (Model Context Protocol) tool export** — mark selected functions as MCP tools so a pyxc program can act as an MCP server; emit an MCP service manifest (JSON) describing each exported tool's name, parameters, and types, generated from the function's own signature. Undecided: a decorator (`@mcp.tool`, requiring a decorator mechanism pyxc doesn't currently have) vs. a dedicated keyword (`mcp def ...`, mirroring `export`/module system from Phase 9). Needs a design decision on annotation style before implementation.
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

**When to implement:** After modules (Phase 9) and after the type system is stable. `comptime` interacts with generics, modules, and the import graph — it should not be retrofitted into an unstable foundation. Likely Phase 11 or later.

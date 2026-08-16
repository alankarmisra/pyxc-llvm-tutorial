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

Chapters 1–45 below are the complete, shipped baseline under the new numbering — the only committed sequence. Closures and Concurrency are still ahead but, like everything past Chapter 45, not committed to fixed chapter numbers; see the Future (Potential) Track below.

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
| 34 | Assignment as an Expression | ✅ | `x = expr` in expression context; `(c = getchar()) != EOF` works; right-associative; lvalue-checked at parse time via `ParseAssignment` |
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

## Phase 9: Program Structure (Chapters 43–45)

| # | Title | Status | Notes |
|---|-------|--------|-------|
| 43 | Module Declarations and Export | ✅ | `module` names the compilation unit; `export` marks public API; multi-file compilation via `extern def` + `--emit exe` |
| 44 | Imports | ✅ | `import app.math` resolves file, scans `export` signatures, injects prototypes; only exported symbols importable; struct/class/trait/type transfer; `--emit exe` auto-closure |
| 45 | Cyclic Imports | ✅ | Two-phase scan (own exports first, then recurse); `InProgress`/`Done` state machine; import path cache; A→B→A cycle handled correctly |

---

## Known Bugs

- **Stale `CurrentTokenLocation` in codegen diagnostics:** Every codegen error (`LogErrorV`) reports the global `CurrentTokenLocation`, which has already advanced past the node by the time codegen runs — e.g. an "Unknown function referenced" error points at the token after the call's closing paren, not at the callee name. Fix requires adding a `SourceLoc` field to `ExpressionNode` and propagating it through every node constructor. (Revisited properly in Chapter 99 below, but the bug itself doesn't need to wait for a chapter slot — fixable any time.)
- **`self.method()` calls from within a method do not work.** Inside a method body, `self` is typed as `ptr[ClassName]`. Field access (`self.field`) auto-derefs via GEP. Method dispatch (`self.method()`) does not — it sees `ptr[ClassName]` and rejects it with "Method call base must be a class/struct value". Workaround: extract the helper as a free function outside the class. Fix requires auto-deref of `self` in the method call dispatcher when the receiver type is `ptr[ClassName]`. Doesn't need a chapter slot — fixable any time.
- **JIT file mode does not compile imported module bodies.** `pyxc file.pyxc` (without `--emit`) signature-scans imports but never compiles their method/function bodies. Calling an imported function produces a JIT link error: "Symbols not found". Workaround: use `--emit exe` for any multi-file program. Fix requires expanding the import closure and JIT-compiling each imported file's bodies before executing the entry file. Doesn't need a chapter slot — fixable any time.
- **`extern def` ABI signatures are trusted without verification;** mismatched declared return/argument types vs actual C symbol types are not detected and can cause runtime/ABI bugs.

---

## Future (Potential) Track

Everything below is real, written-down design intent — none of it is invented for this list. What's new here is only the *ordering*: these were previously an unsequenced backlog (no chapter numbers, no phases), grouped loosely by category. This is a first pass at sequencing them so the tutorial's full scope is visible in one place. **None of this is committed.** Chapter numbers, grouping, and order can all change — treat it as a sketch of how far the language could go, not a plan anyone is locked into. Chapters 1–45 above remain the only committed sequence.

### Closures

Lambda syntax and captured variables. The one open question that actually blocks starting: capture semantics. Capture by value is safe under the no-GC model with no extra work; capture by reference means a closed-over variable has to outlive the closure, which is a real lifetime problem pyxc has no borrow-checker to enforce yet. Needs deciding before implementation starts, not partway through it.

```pyxc
def make_adder(n: int) -> ptr[def(int) -> int]:
  return \(x: int) -> int: x + n   # captures n — capture semantics still undecided

var add5: ptr[def(int) -> int] = make_adder(5)
printd(float64(add5(10)))  # 15.000000
```

### Self-Hosted Testing and Coverage

Two related ideas, different sizes. The small one: a `test/assert.pyxc` module (`assert.eq_int(actual, expected, label)` and friends) that other `.pyxc` test files `import`, printing a `FAIL: ...` line via variadic `printf` and calling `extern def exit(code: int)` (not declared anywhere yet, trivial to add) on mismatch. This needs nothing new from the compiler — `export`/`import` (Chapters 43–45) and variadic `extern def` (Chapter 33) are already enough — it just replaces the copy-pasted printf-and-compare boilerplate every hand-written test currently repeats.

The bigger one: pyxc-level code coverage, i.e. "which lines of *this* `.pyxc` program executed," the same thing `llvm-cov`/Clang's source-based coverage already does for `pyxc.cpp` itself (see the tutorial's own testing docs). This is a real compiler feature, not a library — pyxc's codegen would need to emit `llvm.instrprof.increment` calls tied to pyxc source locations, plus a coverage-mapping section. Not from scratch, though: `SourceLoc`/`CurrentTokenLocation` tracking already exists for diagnostics, so the raw material — "what source location is this AST node at" — is already half there. Worth sequencing ahead of Concurrency below: once concurrent pyxc programs exist, "which lines ran, in what order" turns from a nice-to-have into the main tool for debugging race conditions and nondeterministic failures, so having the instrumentation groundwork in place first pays off the moment concurrency lands.

### Concurrency

Ownership rules for shared state, spawning tasks and threads, synchronization primitives, message passing, parallel loops and work partitioning, determinism/race debugging, and eventually parallelizing the compiler itself. The real blocker is the same shape as closures: the safety model has to be decided before any of the rest can be designed concretely, not discovered chapter by chapter.

```pyxc
def worker(ch: Channel[int]):
  ch.send(compute())

def main() -> int:
  var ch: Channel[int] = Channel[int]()
  spawn worker(ch)
  printd(float64(ch.recv()))
  return 0
```

### Phase 11: Enums and Function Pointers (Chapters 54–55)

| # | Title | Notes |
|---|-------|-------|
| 54 | Enums | `enum Color: Red, Green, Blue` or `enum Status: Ok = 0, Err = 1`; named integral constants, no implicit int conversion, `switch`-friendly. Bundles pattern-matching exhaustiveness checking (warn when a `switch` over an enum misses a variant) as part of the payoff. Unblocks Verifier Phase 1 (Phase 22 below). |
| 55 | Function Pointers | `ptr[def(int, int) -> int]`; enables callbacks, `qsort`, dispatch tables. New type encoding plus a new call-site codegen path. Also a prerequisite for the self-hosting bootstrap plan. |

```pyxc
enum Direction:
  North, South, East, West

def opposite(d: Direction) -> Direction:
  switch d:
    case Direction.North: return Direction.South
    case Direction.South: return Direction.North
    case Direction.East:  return Direction.West
    case Direction.West:  return Direction.East
```

### Phase 12: Generics Completion (Chapter 56)

| # | Title | Notes |
|---|-------|-------|
| 56 | Generic Functions and Structs | `def max[T](a: T, b: T) -> T` and `struct Stack[T]`; monomorphised at instantiation like C++ templates, not erased like Java generics. Extends what Generic Traits (Chapter 42) already started. Unblocks generic collections and `comptime` specialization later. |

```pyxc
def max[T](a: T, b: T) -> T:
  return a if a > b else b

struct Stack[T]:
  items: T[64]
  count: int
```

### Phase 13: Standard Library (Chapters 57–61)

No new language features — this phase just wraps existing `extern` capability into reusable modules. Big concrete reader payoff for relatively little compiler work.

| # | Title | Notes |
|---|-------|-------|
| 57 | `stdlib/stdio.pyxc` | `printf`, `scanf`, `getchar`, `putchar`, `fopen`, `fclose`, `fread`, `fwrite`, `fprintf`, `fgets`, `EOF` constant — eliminates boilerplate `extern def` in every program |
| 58 | `stdlib/stdlib.pyxc` | `malloc`, `free`, `realloc`, `qsort`, `bsearch`, `exit`, `atoi`, `atof`, `strtol` |
| 59 | `stdlib/string.pyxc` | `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `memcpy`, `memset`, `memcmp` |
| 60 | `stdlib/math.pyxc` | `sin`, `cos`, `sqrt`, `pow`, `fabs`, `floor`, `ceil`, `log`, `exp` |
| 61 | Built-in `print` | `print("hello", x)` as a variadic built-in that calls `printf` internally; no `extern def` needed |

```pyxc
import stdlib.stdio
import stdlib.math

def main() -> int:
  print("sqrt(2) =", sqrt(2.0))
  return 0
```

### Phase 14: Language Ergonomics I — Expressions, Bindings, and Calls (Chapters 62–69)

| # | Title | Notes |
|---|-------|-------|
| 62 | Ternary / Conditional Expression | `x if cond else y` (Pythonic) or `cond ? x : y`; one new AST node, precedence just below assignment |
| 63 | `const` Bindings | `const PI: float64 = 3.14159`; immutable at compile time, LLVM constant instead of an alloca |
| 64 | Default Parameter Values | `def f(x: int, y: int = 0) -> int`; caller omits trailing args, compiler fills in constants |
| 65 | Named Arguments at Call Site | `f(y=5, x=3)`; reorder or omit args by name |
| 66 | Multiple Return Values | `def divmod(a: int, b: int) -> (int, int): return a/b, a%b`; anonymous tuple type, unpacked at call site |
| 67 | `NULL` / Null Pointer Literal | `null` or `nil` as a typed null pointer constant, replacing `ptr[T](0)`; interacts with Optional Types (Phase 17) |
| 68 | `len()` and `in` | `len(arr)` as a compile-time constant; `x in arr` membership test (linear scan for arrays, hook for trait-based containers later) |
| 69 | (buffer for scope growth) | — |

```pyxc
def clamp(x: int, lo: int = 0, hi: int = 100) -> int:
  return lo if x < lo else (hi if x > hi else x)

var quotient, remainder = divmod(17, 5)
```

### Phase 15: Language Ergonomics II — Statements and Diagnostics (Chapters 70–75)

| # | Title | Notes |
|---|-------|-------|
| 70 | `assert` Statement | `assert cond` and `assert cond, "message"`; lowers to a conditional `abort()` call |
| 71 | `defer` Statement | `defer free(p)`; executes at function exit regardless of return path — essential for clean resource management in no-GC code; lowers to cleanup blocks at every exit |
| 72 | `static` Local Variables | `static counter: int = 0` inside a function, persists between calls; lowers to a mangled module-level global |
| 73 | `goto` and Labels | `goto cleanup` / `cleanup:`; K&R-style error unwind, needed for full C compatibility |
| 74 | Command-Line Arguments to `main` | `def main(argc: int32, argv: ptr[ptr[int8]]) -> int`; required for any CLI tool written in pyxc |
| 75 | Warn on Assignment in Condition | `if x = 10` warns (or errors in strict mode) unless explicitly parenthesized `if (x = 10)`; lint-only, low-hanging |

```pyxc
def read_first_line(path: ptr[int8]) -> int:
  var f: ptr[int8] = fopen(path, "r")
  assert f != NULL, "could not open file"
  defer fclose(f)
  return 0
```

### Phase 16: Unicode Identifiers and String Interpolation (Chapters 76–77)

| # | Title | Notes |
|---|-------|-------|
| 76 | Unicode Identifiers | Non-ASCII names (`café`, `变量`) in `name`, not just literal content. The heaviest item in this whole speculative list: needs real `XID_Start`/`XID_Continue` Unicode property tables (not a hand-rolled range check), a normalization decision (NFC?), diagnostics/column math correct across multibyte characters, and a homoglyph policy (Latin `a` vs. Cyrillic `а`) — a real security question (IDN-homograph problem applied to source code), not just an implementation detail. Roughly a full focused day of work done properly. |
| 77 | String Interpolation | `f"result: {x}"` producing a `ptr[int8]`; lowers to `sprintf` into a stack buffer — buffer-size strategy needs deciding first |

```pyxc
var café_total: float64 = 4.50
printf(f"Total: {café_total}\n")
```

### Phase 17: Type System Completion (Chapters 78–83)

| # | Title | Notes |
|---|-------|-------|
| 78 | Optional / Nullable Types | `T?` or `Option[T]`; `None` as a value, not just a return type; forces callers to check before use; interacts with pointer nullability |
| 79 | Union Types | C-style `union`; same memory, multiple interpretations — needed for systems/protocol programming |
| 80 | Bit-Fields | `struct Flags: bits: uint8[3]` style or `@bitfield` annotation; hardware register maps, packed binary formats |
| 81 | `const` Pointers | `ptr[const int8]` for read-only data; catches accidental writes through string literals |
| 82 | Multidimensional Arrays | `int[3][3]`; currently blocked with "Nested arrays are not supported" — needs recursive type encoding and multi-level GEP |
| 83 | Pointer to Array, and `void` Pointer | `ptr[int[4]]` (currently blocked) for C's `int (*p)[4]` pattern; plus untyped `ptr` as C's `void *` equivalent, replacing the current `ptr[int8]` workaround |

```pyxc
def find(arr: int[10], target: int) -> Option[int]:
  for i in range(10):
    if arr[i] == target: return i
  return None
```

### Phase 18: OOP Refinements (Chapters 84–87)

| # | Title | Notes |
|---|-------|-------|
| 84 | Static Class Properties and Methods | `class Foo: static count: int = 0` and `static def create() -> Foo`; callable as `Foo.count` / `Foo.create()` with no instance |
| 85 | Operator Overloading | `impl Add for Vec2` style, or `def __add__(other: Vec2) -> Vec2` inside the class; pyxc currently has no operator-overloading mechanism at all |
| 86 | `__str__` and Abstract Methods | `def __str__() -> ptr[int8]` for a built-in `str(x)`/`print(x)`; plus marking a trait method as required, erroring if an `impl` block omits it |
| 87 | Inheritance | Deliberate design decision needed: pyxc currently uses traits for interface polymorphism and composition for reuse. Single-inheritance `class B(A)` is possible but conflicts with the no-vtable trait model — needs an explicit decision, not a default. |

```pyxc
class Vec2:
  public x: float64
  public y: float64

  static def zero() -> Vec2:
    return Vec2(0.0, 0.0)

  def __add__(other: Vec2) -> Vec2:
    return Vec2(self.x + other.x, self.y + other.y)
```

### Phase 19: Import and Module Refinements (Chapters 88–89)

| # | Title | Notes |
|---|-------|-------|
| 88 | Selective Imports and Import Aliases | `from stdlib.io import printf, getchar` (named symbols without polluting the top-level namespace) and `import stdlib.io as io` (qualified access, avoids name collisions) |
| 89 | Directory Modules | `__init__.pyxc` as the entry point for `import mylib`; distribute a library as a directory instead of a single file |

```pyxc
from stdlib.io import printf, getchar
import stdlib.math as m

printf("sqrt(2) = %f\n", m.sqrt(2.0))
```

### Phase 20: Generics-Enabled Ecosystem (Chapters 90–91)

Depends on Phase 12 (Generic Functions and Structs).

| # | Title | Notes |
|---|-------|-------|
| 90 | Generators and Iterators | `yield`, lazy sequence APIs, `range` builtin. Once ranges exist, `switch` case matching extends to accept them too (`case 1...5:`), alongside the comma-separated multi-value case lists from Chapter 23. |
| 91 | Generic Collections | `List[T]`, `Dict[K,V]`, `Set[T]`; iteration protocols and ownership-aware container semantics, built on Phase 12's generics and Chapter 90's iteration model |

```pyxc
var names: List[ptr[int8]] = List[ptr[int8]]()
names.append("Ada")
names.append("Grace")
for name in names:
  printf("%s\n", name)
```

### Phase 21: Compile-Time Execution (Chapters 92–93)

Zig-style `comptime` — see the full design rationale below. Needs modules (done, Phase 9) and a stable type system (done); benefits from generics (Phase 12) existing first for specialization to be worth building.

| # | Title | Notes |
|---|-------|-------|
| 92 | `comptime` Basics | Arbitrary pyxc code that runs at compile time and produces pyxc values or types; `comptime def`, compile-time constants |
| 93 | `comptime` and Generics | Type-level computation, generic specialization without a separate template/macro system |

```pyxc
comptime def storage_type(n: int) -> type:
  if n <= 32: return int32
  return int64

var x: comptime storage_type(16) = 0   # x: int32 at compile time
```

### Phase 22: Verification (Chapters 94–95)

| # | Title | Notes |
|---|-------|-------|
| 94 | Verifier Phase 1 (Sequential) | `requires` / `ensures` / `assert` / loop `invariant`; SMT-backed verification conditions for single-threaded code; `pyxc --verify` mode; starts with `int`/`bool`/control-flow subset, explicit unsupported diagnostics for heap-alias-heavy proofs. Needs Enums (Phase 11). |
| 95 | Verifier Phase 2 (Concurrency-Aware) | Extends the verification model to thread interleavings, synchronization primitives, memory-order rules; likely a state-machine/temporal-reasoning track (TLA+-style specs or model-checker integration) plus race-freedom checks. Needs Concurrency (Phase 10, already committed). |

```pyxc
def divide(a: int, b: int) -> int:
  requires b != 0
  ensures result * b == a
  return a / b
```

### Phase 23: Tooling and Quality (Chapters 96–101)

| # | Title | Notes |
|---|-------|-------|
| 96 | Real Source Locations in Codegen Diagnostics | Properly closes the stale-`CurrentTokenLocation` known bug: `SourceLoc` field on `ExpressionNode`, propagated through every node constructor, so every codegen error — not just parse errors — points at the right line and column |
| 97 | Function Attributes | `@noinline`, `@alwaysinline`, `@nounwind`, `@readnone` as decorators, mapping directly to LLVM function attribute enums |
| 98 | Escape Analysis and Stack Promotion | Detect heap-allocated objects that don't escape the function; replace `malloc`/`free` with stack allocas automatically |
| 99 | REPL Improvements | Multi-line input (continue on trailing `:`), command history, tab completion for defined names |
| 100 | Incremental Compilation and Packaging | Cache compiled `.o` files by content hash, skip recompilation when unchanged; `pyxc build`/`run`/`init`, project manifest, installable binary distribution |
| 101 | MCP Tool Export | Mark selected functions as MCP tools so a pyxc program can act as an MCP server; emit a service manifest (JSON) from the function's own signature. Undecided: a decorator (`@mcp.tool`, needing a decorator mechanism pyxc doesn't have) vs. a dedicated keyword (`mcp def ...`, mirroring `export`). Needs a design decision on annotation style before implementation. |

```bash
pyxc build          # reads a project manifest, compiles + links
pyxc run main.pyxc  # incremental: only recompiles what changed
```

Not chapter-shaped, so not numbered above, but real and ongoing: a **Language Server (LSP)** — hover types, go-to-definition, error squiggles in editors — is IDE tooling built around the compiler rather than a compiler feature itself. It could start any time after there's a stable enough AST/type-info API to query, likely somewhere around Phase 17–20.

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

**When to implement:** After modules (Phase 9) and after the type system is stable. `comptime` interacts with generics, modules, and the import graph — it should not be retrofitted into an unstable foundation. Tentatively sequenced as Phase 21 (Chapters 92–93) above, after Generics Completion (Phase 12) — not committed, same as the rest of that speculative sequence.

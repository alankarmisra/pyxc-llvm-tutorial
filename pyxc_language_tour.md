# pyxc Language Tour

This document gives a practical syntax tour of pyxc as of the chapter-43 codebase, and then lists the biggest missing pieces to reach a full systems-language feature set.

## 1) Program structure

```pyxc
module app.main
import app.math

export def main() -> int:
  return 0
```

- `module` names the unit.
- `import` loads exported signatures from another module.
- `export` marks symbols available to importers.

## 2) Variables and types

```pyxc
var a: int = 42
var b: float64 = 3.14
var ok: bool = True
var p: ptr[int]
var arr: int[4]
```

Supported primitive types:
- `int`, `int8`, `int16`, `int32`, `int64`
- `uint8`, `uint16`, `uint32`, `uint64`
- `float`, `float32`, `float64`
- `bool`, `None`

## 3) Functions

```pyxc
def add(x: int, y: int) -> int:
  return x + y

extern def printf(fmt: ptr[int8], ...) -> int32
```

- Typed params and typed return values.
- Variadic externs are supported.

## 4) Control flow

```pyxc
def f(x: int) -> int:
  if x < 0:
    return -1
  elif x == 0:
    return 0
  else:
    return 1
```

```pyxc
var i: int = 0
while i < 10:
  i += 1
```

```pyxc
do:
  i -= 1
while i > 0
```

```pyxc
switch x:
  case 0:
    return 10
  case 1:
    return 20
  default:
    return 30
```

Current `for` syntax:

```pyxc
for var i: int = 0, i < 10, 1:
  printd(float64(i))
```

## 5) Expressions and operators

```pyxc
x = 1 + 2 * 3
x += 4
x++
--x

if (x = getchar()) != EOF:
  putchar(x)
```

- Assignment is an expression.
- Arithmetic, logical, bitwise, shift operators are present.
- User-defined unary and binary operators are supported.

## 6) Structs, classes, methods, traits

```pyxc
struct Point:
  x: int
  y: int
```

```pyxc
trait Normed:
  def norm2() -> int

class Vec2(Normed):
  x: int
  y: int

  public def norm2() -> int:
    return self.x * self.x + self.y * self.y
```

```pyxc
impl Normed for Vec2:
  def norm2() -> int:
    return self.x * self.x + self.y * self.y
```

- `self` is implicit in method parameter lists.
- Constructors via `__init__`.
- Public/private visibility.

## 7) Memory model

```pyxc
extern def malloc(size: int64) -> ptr[int8]
extern def free(p: ptr[int8])

def alloc_int() -> ptr[int]:
  var raw: ptr[int8] = malloc(sizeof(int))
  return ptr[int](raw)
```

- Explicit heap allocation (`malloc`/`free`).
- Pointer arithmetic and indexing (`p[i]`).
- No GC.

## 8) Literals and casts

```pyxc
var s: ptr[int8] = "hello"
var c: int32 = 'A'
var x: float64 = float64(42)
```

## 9) Modules and imports (chapter 42/43 behavior)

- Imports read exported signatures.
- Non-exported names are private to the module.
- `--emit exe` includes import closure automatically.
- Cyclic imports are handled via scan-state caching.

## 10) What pyxc is still missing for a "full" systems language

Below is the gap list after comparing pyxc with Python grammar shape, C/C++ systems features, and Mojo syntax/capability surface.

### A) Namespacing and symbol qualification

Missing now:
- `import mod as M`
- `M.symbol` qualified lookup

Why it matters:
- Avoid global collisions.
- Scales large programs and stdlib design.

### B) Enums

Missing now:
- Built-in `enum` definitions and typed enum constants.

Why it matters:
- Token kinds, state machines, protocol constants, safer `switch`.

### C) Static class members/methods

Missing now:
- `Type.member` constants and static methods without instance.

Why it matters:
- Better organization of class-level APIs and constants.

### D) Generics beyond trait type-parameter path

Missing now:
- General-purpose generic structs/classes/functions (`Map[K,V]`, `List[T]`).

Why it matters:
- Reusable containers and algorithms.

### E) Containers and iteration protocols

Missing now:
- Native map/dict/set/list abstractions.
- Iterator/generator protocol.

Why it matters:
- Real-world systems code needs basic collections.

### F) Closures and lambdas

Missing now:
- Closure syntax, capture semantics, callable closure values.

Why it matters:
- Composable APIs, callbacks, functional-style control patterns.

### G) Generators (`yield`)

Missing now:
- Generator functions and suspended execution state machine.

Why it matters:
- Streaming pipelines and lazy evaluation.

### H) Error-handling model

Missing now:
- First-class `Result`/error union syntax and ergonomic propagation operator.

Why it matters:
- Predictable systems-level error propagation.

### I) Memory-safety layer above raw pointers

Missing now:
- Ownership/borrow checking or equivalent lifetime discipline.

Why it matters:
- Prevent use-after-free/data-race classes of bugs while staying no-GC.

### J) Concurrency runtime surface

Missing now:
- Standardized thread/task/channel primitives and race-aware tooling.

Why it matters:
- Multi-core systems programming requires a coherent model.

### K) FFI ergonomics and safety

Missing now:
- Better extern ABI checking and diagnostics.

Why it matters:
- Prevent silent signature mismatch bugs at runtime.

## 11) Suggested priority order

1. Qualified module namespaces (`import as`, `M.name`)
2. Enums
3. Static class members/methods
4. Closures (capture-by-value MVP)
5. Generators
6. Generic containers (`List[T]`, `Map[K,V]`)
7. Ownership/lifetime safety layer
8. Concurrency model

---

## Source grammars consulted

- Python full grammar (PEG): https://docs.python.org/3/reference/grammar.html
- C grammar (ANTLR C11-oriented): https://raw.githubusercontent.com/antlr/grammars-v4/master/c/C.g4
- C++ grammar corpus (ANTLR C++): https://github.com/antlr/grammars-v4/tree/master/cpp
- Mojo grammar reference: https://docs.modular.com/mojo/reference/mojo-grammar/

Note: C/C++ and Mojo sources above are large and evolve quickly; this tour uses them for feature-surface comparison, not as a direct parser-generator target for pyxc.

---
description: "Handle cyclic imports: two modules that import each other compile and link correctly using a two-phase signature scan and an InProgress/Done state machine."
---
# 43. pyxc: Cyclic Imports

## Where We Are

[Chapter 42](chapter-42.md) implemented imports. `import app.math` finds the file, scans its exported signatures, and injects them. For a tree-shaped import graph — A imports B, B imports C, nothing cycles back — this works perfectly.

Real programs are not always trees. A and B can be mutually recursive: A calls a function defined in B, B calls a function defined in A. If the signature scanner simply recurses when it sees an `import`, scanning A kicks off scanning B, which kicks off scanning A again — infinite recursion.

After this chapter, that works:

```pyxc
# cycle/a.pyxc
module cycle.a
import cycle.b

export def fa() -> int:
  return fb() + 1
```

```pyxc
# cycle/b.pyxc
module cycle.b
import cycle.a

export def fb() -> int:
  return 41
```

```pyxc
# main.pyxc
module app.main
import cycle.a

extern def printd(x: float64) -> float64

def main() -> int:
  printd(float64(fa()))   # 42.000000
  return 0
```

```bash
pyxc --emit exe -o out main.pyxc
```

```
42.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-43
```

## The Problem

Chapter 42's signature scanner recurses eagerly: when scanning file X and seeing `import Y`, it immediately calls `CollectSignaturesFromFile(Y)`. If Y also imports X, the recursion is infinite.

The solution is a simple insight: **a module's own exports must be collected before recursing into its imports.** When the back-edge arrives, the in-progress module already has its signatures available and the cycle can be broken safely.

## The Two-Phase Algorithm

`CollectSignaturesFromFile` runs in two phases for every file:

**Phase 1 — collect own exports:** Scan the file for `export` declarations. Register every exported function prototype, struct layout, type alias, and trait in the symbol tables. When an `import` line is encountered, record the import name but do not recurse yet.

**Phase 2 — recurse into imports:** Once all of the current file's own exports are registered, process the deferred imports one by one.

The key is phase ordering. By the time we recurse into B from A, A's exports are already in the symbol tables. When B's scan recurses back to A and sees it is `InProgress`, A's signatures are available — the recursion stops and B continues normally.

## The State Machine

Each file has one of three states:

| State | Meaning |
|---|---|
| Not present | File has not been visited |
| `InProgress` | Phase 1 complete, phase 2 in progress |
| `Done` | Both phases complete |

```
CollectSignaturesFromFile(path):
  if state[path] == Done:     return true   # already scanned
  if state[path] == InProgress: return true # cycle — reuse what's already registered

  state[path] = InProgress
  collect own export signatures
  defer import names

  for each deferred import:
    CollectSignaturesFromFile(import)

  state[path] = Done
```

When the cycle-back edge hits (`InProgress`), the function returns `true` immediately — not an error, just "whatever is registered so far is enough." Because phase 1 is already complete for the in-progress file, that is always everything the back-edge caller needs.

## Tracing the A→B→A Cycle

1. main imports `cycle.a` → `CollectSignaturesFromFile("a.pyxc")`
2. A is not visited → mark A as `InProgress`
3. Phase 1: collect `export def fa() -> int` from A, defer `import cycle.b`
4. Phase 2: process deferred imports → `CollectSignaturesFromFile("b.pyxc")`
5. B is not visited → mark B as `InProgress`
6. Phase 1: collect `export def fb() -> int` from B, defer `import cycle.a`
7. Phase 2: process deferred imports → `CollectSignaturesFromFile("a.pyxc")`
8. A is `InProgress` → return true immediately (cycle detected)
9. B's phase 2 complete → mark B as `Done`
10. A's phase 2 complete → mark A as `Done`

Result: both `fa` and `fb` are in the symbol table. `main.pyxc` compiles normally.

## Import Path Caching

Resolving an import path involves a filesystem walk — converting `cycle.a` to `cycle/a.pyxc` and probing up the directory tree. Chapter 43 adds a cache keyed on `(importer_path, import_name)` so each resolution is computed once and reused on repeat encounters. This matters most for large import graphs where the same module is imported from many places.

The cache maps to the canonical resolved path. An empty value means "resolution failed" — a negative cache entry that avoids repeating a failed search.

## Compile-Time vs Run-Time Cycles

The cycle handling here is entirely about **signature scanning** — the compile-time phase where the compiler learns what symbols exist. It has nothing to do with runtime behavior.

Mutual recursion at runtime — `fa()` calling `fb()` and vice versa — was already handled by the LLVM linker. As long as the recursion terminates at runtime (here, `fb()` returns a constant), the program is correct. The compiler does not detect or prevent infinite runtime recursion; that is the programmer's responsibility.

## What Cyclic Imports Cannot Solve

Cyclic imports work when A and B need each other's **function signatures**. They do not help when A and B need each other's **type definitions** in a way that requires one type to embed the other by value. A struct cannot contain itself by value — that would be an infinite-size type. Circular struct layouts are rejected during layout calculation, regardless of imports.

Pointer-based mutual reference is fine:

```pyxc
# node.pyxc
export struct Node:
  value: int
  next: ptr[Node]   # pointer to self — ok, ptr is fixed size
```

Value-based mutual reference is not:

```pyxc
export struct A:
  b: B   # Error: struct layout cycle
export struct B:
  a: A
```

## Things Worth Knowing

**`Done` state prevents redundant scanning.** If three files all import `app.math`, the scanner runs once (marks it `Done`) and skips on the next two encounters. The state machine doubles as a deduplication cache.

**State is reset between compilation runs.** The `InProgress`/`Done` map is cleared at the start of each top-level `PreloadImportedSignatures` call. It does not persist across separate `pyxc` invocations.

**The algorithm is DFS, not BFS.** Imports are resolved depth-first. If A imports B and C, and B imports D, the order is A → B → D → C. This does not affect correctness, but it determines which module's exports get registered first when there are naming conflicts (the first-registered prototype wins).

**Circular `import` without mutual function references is fine.** If A and B import each other but neither calls the other's functions, the cycle is resolved harmlessly — both files scan to completion and no symbols from the other are used.

## What's Next

Phase 6 is complete. [Chapter 44](chapter-44.md) begins Phase 6's final chapter: closures — lambda syntax, captured variables, and the closure struct + function pointer representation in LLVM IR.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

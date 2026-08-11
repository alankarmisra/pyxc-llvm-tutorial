---
description: "Handle cyclic imports: two modules that import each other compile and link correctly using a two-phase signature scan and an InProgress/Done state machine."
---
# 45. pyxc: Cyclic Imports

## What I Am Building

[Chapter 44](chapter-44.md) implemented imports. `import app.math` finds the file, scans its `export` declarations, and injects prototypes. For a tree-shaped import graph this works. For a cycle — A imports B, B imports A — Chapter 44's `SignatureVisitedFiles` set stops the recursion, but it stops it too early: when B tries to scan A it finds A "already visited" and gets nothing. If B calls a function from A, the prototype is missing.

After this chapter, mutual imports work correctly:

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
  printd(float64(fa()))
  return 0
```

```
42.000000
```

There's no grammar change here either — same as Chapter 44, this is entirely about what happens during signature scanning.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-45
```

## The State Machine

I replace Chapter 44's `std::set<string> SignatureVisitedFiles` with a map that distinguishes two completion states:

```cpp
enum class SignatureScanState { InProgress, Done };
static std::map<string, SignatureScanState> SignatureFileStates;
```

| State | Meaning |
|---|---|
| Not present | File has not been visited |
| `InProgress` | Phase 1 complete — own exports collected, phase 2 in progress |
| `Done` | Both phases complete |

`CollectSignaturesFromFile` checks this map at entry:

```cpp
auto StateIt = SignatureFileStates.find(CanonPath);
if (StateIt != SignatureFileStates.end()) {
  if (StateIt->second == SignatureScanState::Done)
    return true;   // fully scanned — reuse
  // Cycle detected. Resolve by allowing the in-progress scan to complete and
  // reusing whatever exported signatures are already known.
  return true;
}
SignatureFileStates[CanonPath] = SignatureScanState::InProgress;
```

Both `Done` and `InProgress` return `true` immediately — an `InProgress` hit isn't an error, it's "OK, use what's already there." The difference between the two states is just what "already there" means: `Done` means every signature this file exports is registered; `InProgress` means only its own exports are registered so far, and it's still off recursing into what it imports. Since Phase 1 always finishes before Phase 2 starts (next section), an `InProgress` file's own exports are always already registered by the time anything can ask for them — that's exactly why returning `true` for a back-edge is safe.

## Two-Phase Signature Collection

The change that actually breaks the cycle: I stop recursing into `import` lines eagerly. Instead I collect them into a `NestedImports` vector first:

```cpp
SignatureFileStates[CanonPath] = SignatureScanState::InProgress;
// ... open file, setup scanner state ...
vector<string> NestedImports;

while (CurrentToken != tok_eof) {
  if (CurrentToken == tok_import) {
    getNextToken(); // eat 'import'
    string ImportName;
    if (!ParseDottedModuleName(ImportName)) { OK = false; break; }
    NestedImports.push_back(ImportName);   // Phase 1: defer, don't recurse
    continue;
  }
  if (CurrentToken == tok_export) {
    if (!ParseExportSignatureOnly()) { OK = false; break; } // Phase 1: collect
    continue;
  }
  SkipSignatureBody();
}
```

Only once the whole file has been scanned — every `export` collected, Phase 1 fully done — do I come back and recurse into the deferred imports:

```cpp
if (OK) {
  for (const auto &ImportName : NestedImports) {
    string ImportPath;
    if (!ResolveImportToPath(CanonPath, ImportName, ImportPath)) {
      OK = false; break;
    }
    if (!CollectSignaturesFromFile(ImportPath)) {
      OK = false; break;
    }
  }
}
// ... restore state ...
SignatureFileStates[CanonPath] =
    OK ? SignatureScanState::Done : SignatureScanState::InProgress;
if (!OK)
  SignatureFileStates.erase(CanonPath);
```

I only set `Done` once both phases succeed.

## Tracing the A→B→A Cycle

This is a depth-first walk of the import graph — imports are resolved depth-first, which is what determines registration order when two files happen to export the same name (first one registered wins):

1. `main` imports `cycle.a` → `CollectSignaturesFromFile("a.pyxc")`
2. A: not present → set `InProgress`
3. A Phase 1: collect `export def fa()`, defer `import cycle.b`
4. A Phase 2: process `cycle.b` → `CollectSignaturesFromFile("b.pyxc")`
5. B: not present → set `InProgress`
6. B Phase 1: collect `export def fb()`, defer `import cycle.a`
7. B Phase 2: process `cycle.a` → `CollectSignaturesFromFile("a.pyxc")`
8. A is `InProgress` → return true (cycle detected, `fa` already registered)
9. B Phase 2 complete → set B to `Done`
10. A Phase 2 complete → set A to `Done`

Both `fa` and `fb` end up in the symbol table, and `main.pyxc` compiles normally.

## Directory Tree Walk and Caching

Chapter 44's resolver only checked one directory. I add two improvements: a search that walks up the directory tree, and a cache so I don't repeat that search.

```cpp
static bool ResolveImportToPath(const string &ImporterPath,
                                const string &Import, string &OutPath) {
  const string CanonImporter = CanonicalizePath(ImporterPath);
  const string CacheKey = CanonImporter + "->" + Import;
  auto CacheIt = ResolvedImportPathCache.find(CacheKey);
  if (CacheIt != ResolvedImportPathCache.end()) {
    if (CacheIt->second.empty())
      return false; // negative cache
    OutPath = CacheIt->second;
    return true;
  }

  string Rel = Import;
  std::replace(Rel.begin(), Rel.end(), '.', '/');
  SmallString<256> Base(CanonImporter);
  path::remove_filename(Base);
  SmallString<256> Probe(Base);
  while (true) {
    SmallString<256> Candidate(Probe);
    path::append(Candidate, Rel + ".pyxc");
    if (fs::exists(Candidate)) {
      OutPath = std::string(Candidate.str());
      ResolvedImportPathCache[CacheKey] = OutPath;
      return true;
    }
    SmallString<256> InputsCandidate(Probe);
    path::append(InputsCandidate, "Inputs");
    path::append(InputsCandidate, Rel + ".pyxc");
    if (fs::exists(InputsCandidate)) {
      OutPath = std::string(InputsCandidate.str());
      ResolvedImportPathCache[CacheKey] = OutPath;
      return true;
    }
    SmallString<256> Parent(Probe);
    path::remove_filename(Parent);
    if (Parent == Probe || Parent.empty())
      break;
    Probe = Parent;
  }
  ResolvedImportPathCache[CacheKey] = ""; // cache negative result
  return false;
}
```

I start the search in the importer's own directory and walk up toward the filesystem root, probing at each level (and its `Inputs/` fallback) until something matches or I run out of parents. `ResolvedImportPathCache` stores both hits and misses, keyed on `"canonicalImporterPath->importName"` — an empty cached value means "I already searched for this and it isn't there," so a repeated failed lookup doesn't repeat the whole directory walk.

## State Reset

`PreloadImportedSignatures` clears both the state machine and the path cache at the start of every top-level compilation, so nothing leaks between separate `pyxc` invocations:

```cpp
static bool PreloadImportedSignatures(const string &Path) {
  SignatureFileStates.clear();
  ResolvedImportPathCache.clear();
  // ... as before ...
}
```

If three files all import `app.math`, I still only scan it once per compilation — the second and third `import app.math` hit `Done` in `SignatureFileStates` and return immediately. But that cache doesn't survive past this one compilation; the next `pyxc` invocation starts clean.

## What Cyclic Imports Cannot Solve

Cyclic imports fix the **signature scanning** problem, not the **type layout** problem. A struct still can't contain itself by value — that's an infinite-size type. But the way pyxc actually stops you from writing one is less clean than a dedicated diagnostic, and it's worth being honest about that rather than inventing a tidier error message than what's really there.

If I try two structs that reference each other by value across the same file:

```pyxc
export struct A:
  b: B
export struct B:
  a: A
```
```
Error (Line 2, Column 6): Unknown type 'B'
  b: B
     ^~~~
```

That's not a cycle detector — it's the ordinary "structs can't forward-reference a type that isn't declared yet" rule from Chapter 28, firing because `B` doesn't exist yet when `A`'s field tries to name it. It would fire the same way for any undeclared type, cycle or not.

A struct referencing *itself* slips past that check, though, since a struct's own name is registered before its body is parsed (Chapter 37). Right now that means `struct A: a: A` doesn't get rejected with a clear error at all — struct-type construction notices it's already partway through building `A` and falls back to an incomplete LLVM type instead of a real one:

```
%struct.A = type opaque
```

That's a gap, not a designed diagnostic. The fix, as always, is pointer-based mutual reference — pointers are fixed size regardless of what they point to, so this works cleanly:

```pyxc
export struct Node:
  value: int
  next: ptr[Node]   # ok — ptr is always 8 bytes
```

## Try It

```bash
mkdir -p cycle
cat > cycle/a.pyxc <<'PYXC'
module cycle.a
import cycle.b

export def fa() -> int:
  return fb() + 1
PYXC
cat > cycle/b.pyxc <<'PYXC'
module cycle.b
import cycle.a

export def fb() -> int:
  return 41
PYXC
cat > main.pyxc <<'PYXC'
module app.main
import cycle.a

extern def printd(x: float64) -> float64

def main() -> int:
  printd(float64(fa()))
  return 0
PYXC
pyxc --emit exe -o out main.pyxc
./out
```
```
42.000000
```

## What's Next

Phase 6 — modules and multi-file builds — is complete: pyxc has K&R-compatible operators and types, a class and trait system, and a module system with automatic dependency resolution and cyclic import support. Closures are still ahead, and concurrency after that — this is as far as the tutorial goes for now, not the end of the language.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

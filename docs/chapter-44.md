---
description: "Handle cyclic imports: two modules that import each other compile and link correctly using a two-phase signature scan and an InProgress/Done state machine."
---
# 44. pyxc: Cyclic Imports

## Where We Are

[Chapter 43](chapter-43.md) implemented imports. `import app.math` finds the file, scans its `export` declarations, and injects prototypes. For a tree-shaped import graph this works. For a cycle — A imports B, B imports A — Chapter 43's `SignatureVisitedFiles` set stops the recursion, but it stops it too early: when B tries to scan A it finds A "already visited" and gets nothing. If B calls a function from A, the prototype is missing.

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
  printd(float64(fa()))   # 42.000000
  return 0
```

```
42.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-44
```

## The State Machine

Chapter 43's `std::set<string> SignatureVisitedFiles` is replaced with a map that distinguishes two completion states:

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
  // InProgress: cycle detected — own exports already registered, return safely
  return true;
}
SignatureFileStates[CanonPath] = SignatureScanState::InProgress;
```

Both `Done` and `InProgress` return `true` immediately. The difference is meaning: `Done` means "all signatures available"; `InProgress` means "own exports available, recursing into nested imports". The back-edge caller gets whatever is already registered — and because Phase 1 is already complete, that is exactly the current file's exports.

## Two-Phase Signature Collection

The key change is that `import` lines are no longer recursed into eagerly. Instead they are collected into a `NestedImports` vector:

```cpp
SignatureFileStates[CanonPath] = SignatureScanState::InProgress;
// ... open file, setup scanner state ...
vector<string> NestedImports;

while (CurTok != tok_eof) {
  if (CurTok == tok_import) {
    getNextToken(); // eat 'import'
    string ImportName;
    if (!ParseDottedModuleName(ImportName)) { OK = false; break; }
    NestedImports.push_back(ImportName);   // Phase 1: defer, don't recurse
    continue;
  }
  if (CurTok == tok_export) {
    if (!ParseExportSignatureOnly()) { OK = false; break; } // Phase 1: collect
    continue;
  }
  SkipSignatureBody();
}
```

Only after the whole file has been scanned does Phase 2 recurse into the deferred imports:

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
SignatureFileStates[CanonPath] = OK ? SignatureScanState::Done
                                   : SignatureScanState::InProgress;
if (!OK)
  SignatureFileStates.erase(CanonPath);
```

The state is set to `Done` only after both phases succeed.

## Tracing the A→B→A Cycle

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

Result: both `fa` and `fb` are in the symbol table. `main.pyxc` compiles normally.

## `ResolveImportToPath` — Directory Tree Walk and Caching

Chapter 43's resolver only checked one directory. This chapter adds two improvements.

**Directory tree walk**: The search starts in the importer's directory and walks up toward the filesystem root, probing at each level:

```cpp
static bool ResolveImportToPath(const string &ImporterPath,
                                const string &Import, string &OutPath) {
  const string CanonImporter = CanonicalizePath(ImporterPath);
  // Check cache first
  const string CacheKey = CanonImporter + "->" + Import;
  auto CacheIt = ResolvedImportPathCache.find(CacheKey);
  if (CacheIt != ResolvedImportPathCache.end()) {
    if (CacheIt->second.empty()) return false; // negative cache
    OutPath = CacheIt->second;
    return true;
  }

  string Rel = Import;
  std::replace(Rel.begin(), Rel.end(), '.', '/');
  SmallString<256> Probe(CanonImporter);
  path::remove_filename(Probe);

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
    // walk up
    SmallString<256> Parent(Probe);
    path::remove_filename(Parent);
    if (Parent == Probe || Parent.empty()) break;
    Probe = Parent;
  }

  ResolvedImportPathCache[CacheKey] = ""; // cache negative result
  return false;
}
```

**Path cache**: Results (both positive and negative) are stored in:

```cpp
static std::map<string, string> ResolvedImportPathCache;
```

The cache is keyed on `"canonicalImporterPath->importName"`. An empty value is a negative cache entry — "we already searched and this doesn't exist." Both the directory walk and the cache are cleared in `PreloadImportedSignatures` at the start of each top-level compilation.

## State Reset

`PreloadImportedSignatures` now clears both the state machine and the path cache:

```cpp
static bool PreloadImportedSignatures(const string &Path) {
  SignatureFileStates.clear();
  ResolvedImportPathCache.clear();
  // ... as before ...
}
```

## What Cyclic Imports Cannot Solve

Cyclic imports resolve the **signature scanning** problem, not the **type layout** problem. A struct cannot contain itself by value — that would be an infinite-size type:

```pyxc
export struct A:
  b: B   # Error: struct layout cycle
export struct B:
  a: A
```

Pointer-based mutual reference is fine because pointers are fixed size:

```pyxc
export struct Node:
  value: int
  next: ptr[Node]   # ok — ptr is always 8 bytes
```

## Things Worth Knowing

**`Done` state prevents redundant scanning.** If three files all import `app.math`, the scanner runs once and skips on the next two encounters.

**State is cleared between compilation runs.** `SignatureFileStates` and `ResolvedImportPathCache` are cleared at the start of each `PreloadImportedSignatures` call. They do not persist across `pyxc` invocations.

**The algorithm is DFS.** Imports are resolved depth-first. This does not affect correctness, but it determines registration order when names conflict (first-registered prototype wins).

**`InProgress` return is not an error.** When a back-edge arrives and finds `InProgress`, the function returns `true` — "OK, use what's already there." The caller proceeds normally with whatever prototypes are already in the symbol tables. Because the in-progress file's Phase 1 is complete, those are always the exports the back-edge caller needs.

## What's Next

This is the final chapter. Phase 6 — modules and multi-file builds — is complete. pyxc is a full systems language: K&R-compatible operators and types, a class and trait system, a module system with automatic dependency resolution, and cyclic import support.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

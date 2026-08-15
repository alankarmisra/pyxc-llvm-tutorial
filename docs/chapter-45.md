---
description: "Handle cyclic imports: two modules that import each other compile and link correctly by deferring import recursion until each file's own exports are registered."
---
# 45. pyxc: Cyclic Imports

## What I Am Building

[Chapter 44](chapter-44.md) implemented imports. `import app.math` finds the file, scans its `export` declarations, and injects prototypes. For a tree-shaped import graph this works. For a cycle — A imports B, B imports A — Chapter 44 recurses into each `import` line the moment it's seen, so scanning A means immediately scanning B, which means immediately scanning A again. `SignatureFileStates` marks A `InProgress` while it's still being scanned, so that re-entry is caught — but Chapter 44 treats it as an error and rejects the compile with `"Cyclic imports are not supported"`, even though A's own exports (registered before it recursed into B) were already sitting in the symbol table.

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

`SignatureFileStates` already exists from Chapter 44 — I'm not introducing it here, I'm changing what happens on an `InProgress` hit:

```cpp
enum class SignatureScanState { InProgress, Done };
static map<string, SignatureScanState> SignatureFileStates;
```

| State | Meaning |
|---|---|
| Not present | File has not been visited |
| `InProgress` | Currently being scanned (own exports may or may not be registered yet, depending on when in the scan the hit occurs) |
| `Done` | Fully scanned — exports registered |

`CollectSignaturesFromFile` checks this map at entry. Chapter 44's version treated an `InProgress` hit as an error; I change that one line to a plain success:

```cppdiff
*static bool CollectSignaturesFromFile(const string &Path) {
*  string CanonicalPath = CanonicalizePath(Path);
*  auto ExistingState = SignatureFileStates.find(CanonicalPath);
*  if (ExistingState != SignatureFileStates.end()) {
*    if (ExistingState->second == SignatureScanState::Done)
*      return true;
-    return LogErrorExpression("Cyclic imports are not supported"), false;
+    return true;
*  }
*  SignatureFileStates[CanonicalPath] = SignatureScanState::InProgress;
*  ...
*}
```

Both `Done` and `InProgress` now return `true` immediately — an `InProgress` hit isn't an error, it's "OK, use what's already there." That's only safe because of the second change this chapter makes, described next: without it, an `InProgress` hit could land before the in-progress file's own exports have even been collected.

## Two-Phase Signature Collection

The change that actually breaks the cycle: I stop recursing into `import` lines eagerly. Instead I collect them into a `NestedImports` vector during the scan, and only recurse into it after the whole file has been scanned:

```cppdiff
*static bool CollectSignaturesFromFile(const string &Path) {
*  ...
*  bool Parsed = true;
+  vector<string> NestedImports;
*
*  ...
*  getNextToken();
*
*  while (CurrentToken != tok_eof) {
*    if (CurrentToken == tok_eol || CurrentToken == tok_indent ||
*        CurrentToken == tok_dedent || CurrentToken == tok_block_end) {
*      getNextToken();
*      continue;
*    }
*    if (CurrentToken == tok_import) {
*      getNextToken(); // eat 'import'
*      string ImportName;
*      if (!ParseModulePath(ImportName)) {
*        Parsed = false;
*        break;
*      }
-      string ImportPath;
-      if (!ResolveImportToPath(CanonicalPath, ImportName, ImportPath)) {
-        LogErrorExpression(
-            ("Could not resolve import '" + ImportName + "'").c_str());
-        Parsed = false;
-        break;
-      }
-      if (!CollectSignaturesFromFile(ImportPath)) {
-        Parsed = false;
-        break;
-      }
+      NestedImports.push_back(std::move(ImportName));
*      continue;
*    }
*    if (CurrentToken == tok_export) {
*      if (!ParseExportedDeclarationSignature()) {
*        Parsed = false;
*        break;
*      }
*      continue;
*    }
*    SkipExportedDefinitionBody();
*  }
*
+  // I collect this module's exports before following its imports. If an
+  // imported module leads back here, those signatures are already available.
+  if (Parsed) {
+    for (const string &ImportName : NestedImports) {
+      string ImportPath;
+      if (!ResolveImportToPath(CanonicalPath, ImportName, ImportPath)) {
+        LogErrorExpression(
+            ("Could not resolve import '" + ImportName + "'").c_str());
+        Parsed = false;
+        break;
+      }
+      if (!CollectSignaturesFromFile(ImportPath)) {
+        Parsed = false;
+        break;
+      }
+    }
+  }
*
*  ...
*}
```

Everything up through the `while` loop is Phase 1 — every top-level `export` in this file gets registered, and every `import` line just gets its name saved for later. The `for` loop over `NestedImports` is Phase 2 — only now do I recurse into what this file imports. At the end, the same success/failure handling from Chapter 44 still applies:

```cppdiff
*static bool CollectSignaturesFromFile(const string &Path) {
*  ...
*  if (!Parsed) {
*    SignatureFileStates.erase(CanonicalPath);
*    return false;
*  }
*  SignatureFileStates[CanonicalPath] = SignatureScanState::Done;
*  return true;
*}
```

I only set `Done` once both phases succeed; a failure at any point erases the entry entirely rather than leaving it `InProgress`.

## Tracing the A→B→A Cycle

This is a depth-first walk of the import graph — imports are resolved depth-first, which is what determines registration order when two files happen to export the same name (first one registered wins):

1. `main` imports `cycle.a` → `CollectSignaturesFromFile("a.pyxc")`
2. A: not present → set `InProgress`
3. A Phase 1: collect `export def fa()`, defer `import cycle.b`
4. A Phase 2: process `cycle.b` → `CollectSignaturesFromFile("b.pyxc")`
5. B: not present → set `InProgress`
6. B Phase 1: collect `export def fb()`, defer `import cycle.a`
7. B Phase 2: process `cycle.a` → `CollectSignaturesFromFile("a.pyxc")`
8. A is `InProgress` → return true (`fa` was already registered in A's Phase 1, before A recursed into B)
9. B Phase 2 complete → set B to `Done`
10. A Phase 2 complete → set A to `Done`

Both `fa` and `fb` end up in the symbol table, and `main.pyxc` compiles normally.

## No Change to Path Resolution

`ResolveImportToPath` itself is untouched this chapter — the directory-tree walk described in [Chapter 44](chapter-44.md) already handles everything the cyclic examples above need, since `cycle/a.pyxc` and `cycle/b.pyxc` sit in the same directory. Fixing cycles is purely a change to *when* `CollectSignaturesFromFile` recurses into imports, not to how a single import name gets turned into a path.

## State Reset

`PreloadImportedSignatures` still just clears `SignatureFileStates` at the start of every top-level compilation, exactly as in Chapter 44:

```cpp
static bool PreloadImportedSignatures(const string &Path) {
  SignatureFileStates.clear();
  for (const string &ImportName : ExtractTopLevelImports(Path)) {
    string ImportPath;
    if (!ResolveImportToPath(Path, ImportName, ImportPath))
      return LogErrorExpression(
                 ("Could not resolve import '" + ImportName + "'").c_str()),
             false;
    if (!CollectSignaturesFromFile(ImportPath))
      return false;
  }
  return true;
}
```

If three files all import `app.math`, I still only scan it once per compilation — the second and third `import app.math` hit `Done` in `SignatureFileStates` and return immediately. That state doesn't survive past this one compilation; the next `pyxc` invocation starts clean.

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

## Build and Run

```bash
cd code/chapter-45
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
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

Phase 9 — program structure — is complete: pyxc has K&R-compatible operators and types, a class and trait system, and a module system with automatic dependency resolution and cyclic import support. Closures are still ahead, and concurrency after that — this is as far as the tutorial goes for now, not the end of the language.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

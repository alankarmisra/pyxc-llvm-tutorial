---
description: "Add import declarations: pyxc finds the source file, scans its exported signatures, and makes them available — no extern def needed for pyxc-to-pyxc calls."
---
# 42. pyxc: Imports

## Where We Are

[Chapter 41](chapter-41.md) introduced `module` and `export`. A module now has a name and a declared public API, but callers still have to write `extern def` to use it. Every file that calls `add` carries a copy of its signature. Change the signature, fix twenty files.

After this chapter, that's gone:

```pyxc
# app/math.pyxc
module app.math

export def add(x: int, y: int) -> int:
  return x + y
```

```pyxc
# main.pyxc
module app.main
import app.math

extern def printd(x: float64) -> float64

def main() -> int:
  printd(float64(add(2, 3)))   # 5.000000
  return 0
```

```bash
pyxc --emit exe -o out main.pyxc
```

No `extern def add`. The compiler finds `app/math.pyxc`, reads its `export` declarations, and injects the prototype. And in `--emit exe` mode, it finds and compiles `app/math.pyxc` automatically — you don't pass it on the command line.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-42
```

## Grammar

```ebnf
importdecl = "import" modulepath ;
modulepath = identifier { "." identifier } ;
```

`import` is file-mode only. It can appear anywhere after the `module` declaration and before the first function definition, though putting all imports at the top is the convention.

## How Resolution Works

`import app.math` resolves to a file by converting dots to path separators and appending `.pyxc`:

```
app.math  →  app/math.pyxc
```

The search starts in the same directory as the importing file and walks up toward the filesystem root:

```
/project/src/main.pyxc imports app.math
  → tries /project/src/app/math.pyxc       ✓ found
```

```
/project/src/ui/view.pyxc imports app.math
  → tries /project/src/ui/app/math.pyxc    ✗ not found
  → tries /project/src/app/math.pyxc       ✓ found
```

If the file is not found after walking to the root, the compiler errors:

```
Error: Could not resolve import 'app.math' from '/project/src/main.pyxc'
```

## Signature Scanning

When `import app.math` is seen, the compiler opens `app/math.pyxc` and runs a signature scan — a fast pass that reads only `export` declarations and skips everything else. Function bodies, non-exported definitions, and initializers are ignored. The scan result is a set of prototypes injected into the current compilation's symbol table.

What transfers:

| Export form | What the importer gets |
|---|---|
| `export def f(x: T) -> U` | Function prototype (name, param types, return type) |
| `export extern def f(...)` | Extern prototype |
| `export struct Foo` | Struct layout (field names, types, offsets) |
| `export class Bar` | Class layout + method prototypes |
| `export trait Named` | Trait method signatures |
| `export type string = ptr[int8]` | Type alias |

What does not transfer:

- Non-exported `def` functions — private, not importable
- Global variables (`var` at top level) — not yet part of the export system
- Function bodies — never transferred; the importer only needs the signature

## Only Exported Symbols Are Importable

```pyxc
# app/math.pyxc
module app.math

def validate(x: int) -> bool:   # private
  return x >= 0

export def add(x: int, y: int) -> int:
  return x + y
```

```pyxc
# main.pyxc
import app.math

def main() -> int:
  validate(5)   # Error: Unknown function referenced
  return add(2, 3)   # ok
```

`validate` is not exported. It does not appear in the import. Calling it is the same as calling a function that was never declared.

## Type Checking Across Modules

Imported signatures carry full type information. The usual type checker applies:

```pyxc
import app.math

def main() -> int:
  return add(1.0, 2.0)   # Error: argument 1 expects int
```

The error message names the expected type, just as it would for a locally declared function.

## Struct Types Across Modules

Importing a module that exports a struct makes that type available for `var` declarations, function parameters, return types, and field access:

```pyxc
# geo/shapes.pyxc
module geo.shapes

export struct Point:
  x: int
  y: int
```

```pyxc
import geo.shapes

def main() -> int:
  var p: Point
  p.x = 7
  return p.x
```

The struct layout — field order, types, and GEP offsets — transfers exactly. A value of type `Point` in the importer has the same memory layout as in the exporter. No marshalling, no copying.

## `--emit exe` Finds Dependencies Automatically

In earlier chapters, every source file had to be listed on the command line:

```bash
pyxc --emit exe -o out math.pyxc main.pyxc   # old way
```

After this chapter, `--emit exe` walks the import graph of the entry file and compiles everything it finds:

```bash
pyxc --emit exe -o out main.pyxc   # new way — math.pyxc found and compiled automatically
```

The closure expansion is depth-first. Each file's imports are resolved, compiled, and linked. A file that is imported by multiple modules is compiled once (deduplication via canonicalized paths).

## Nested Imports

Imports are transitive. If `main.pyxc` imports `app.ui` and `app.ui` imports `app.math`, `main.pyxc` gets `app.math`'s symbols without an explicit import:

```
main  →  app.ui  →  app.math
```

`add` from `app.math` is available in `main` because the signature scan follows the chain. Whether this is good style is a different question — explicit imports are clearer — but the compiler handles the transitive case correctly.

## Note on `self` in Method Signatures

pyxc methods do not write `self` in the parameter list. This catches readers coming from Python:

```pyxc
# Python style — wrong in pyxc
export class Counter:
  value: int
  def increment(self):       # Error: Method parameters cannot be named 'self'
    self.value += 1
```

```pyxc
# pyxc style — correct
export class Counter:
  value: int
  def increment():
    self.value += 1           # self is implicit
```

`self` is the implicit receiver of every method. It is injected by the compiler as a pointer to the class instance and is always in scope inside the method body, but it is not written in the signature.

## Error Cases

**Import not found:**
```pyxc
import does.not.exist   # Error: Could not resolve import 'does.not.exist' from '...'
```

**Calling a private function:**
```pyxc
import app.math
validate(5)   # Error: Unknown function referenced
```

**Wrong argument type from imported function:**
```pyxc
import app.math
add(1.0, 2)   # Error: argument 1 expects int
```

## Things Worth Knowing

**`import` in the REPL is not supported.** The import system is file-mode only. The REPL compiles expressions in a single-module context with no file to resolve against.

**Module names and file paths must agree.** `import app.math` looks for `app/math.pyxc`. If that file declares `module geo.shapes` at the top, the compiler does not complain — module names are not verified against file paths in this chapter. Keeping them consistent is your responsibility and a strong convention.

**Circular imports work.** If `a.pyxc` imports `b.pyxc` and `b.pyxc` imports `a.pyxc`, the compiler does not loop forever. It resolves the cycle by scanning each file's own exports before recursing into its imports. See [Chapter 43](chapter-43.md) for details.

**`--emit llvm-ir` does not auto-include dependencies.** The auto-closure expansion is specific to `--emit exe`. When emitting IR for a single file you still pass all input files explicitly. This is intentional — IR output is one-file-in, one-file-out by design.

## What's Next

[Chapter 43](chapter-43.md) looks at what happens when two modules import each other and explains how the compiler resolves the cycle without infinite recursion.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

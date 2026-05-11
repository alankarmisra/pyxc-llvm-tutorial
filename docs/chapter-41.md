---
description: "Introduce module declarations and export: name your compilation unit, mark your public API, and split a pyxc project across multiple files."
---
# 41. pyxc: Module Declarations and Export

## Where We Are

[Chapter 40](chapter-40.md) completed Phase 5. pyxc is now a complete systems language — you can write K&R-style programs, call any C library function, and express everything in the first four chapters of *The C Programming Language*.

What we haven't addressed is scale. Every non-trivial program lives in more than one file. pyxc can already compile multiple files into a single executable — it has since [Chapter 14](chapter-14.md) — but there's no way to say which functions are public and which are internal. This chapter introduces `module` and `export` to fix that.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-41
```

## Multi-File Compilation Already Works

Before adding anything new, it's worth showing what already works. Two pyxc files, compiled together:

```pyxc
# math.pyxc
def add(x: int, y: int) -> int:
  return x + y
```

```pyxc
# main.pyxc
extern def add(x: int, y: int) -> int

def main() -> int:
  return add(3, 4)
```

```bash
pyxc --emit exe -o out math.pyxc main.pyxc
```

The linker connects them. No new features needed. The `extern def` in `main.pyxc` tells the compiler "this function exists somewhere — I'll deal with the details at link time."

This works, but it has a problem: every file that calls `add` must repeat its `extern def`. In a project with twenty files that all call `add`, you repeat the signature twenty times. If the signature changes, twenty files break.

## `module` and `export`

This chapter adds two keywords to address that:

**`module`** names the compilation unit. It must be the first non-comment line in the file.

```pyxc
module app.math
```

The name is a dotted path — `app.math`, `geo.point`, `stdlib.io`. It doesn't affect the compiled output in this chapter; it's documentation about what this file contains and a prerequisite for the import system in [Chapter 42](chapter-42.md).

**`export`** marks a top-level declaration as part of the module's public API. Anything without `export` is private to that file.

```pyxc
module app.math

def validate(x: int) -> bool:   # private — not exported
  return x >= 0

export def add(x: int, y: int) -> int:   # public
  return x + y
```

`export` is a prefix, not a separate declaration. You can export functions, structs, classes, traits, type aliases, and extern declarations:

```pyxc
export struct Point:
  x: int
  y: int

export type string = ptr[int8]

export def distance(a: Point, b: Point) -> float64:
  ...
```

## Grammar

```ebnf
moduledecl  = "module" modulepath ;
exportdecl  = "export" ( definition | external | structdef | classdef
                        | typealias | traitdef | impldef ) ;
modulepath  = identifier { "." identifier } ;
```

`module` must appear before any other top-level form. A file can have at most one `module` declaration. Both are file-mode only — `module` and `export` in the REPL are errors.

## What `export` Does Not Do Yet

In this chapter, `export` is parsed and checked syntactically, but it does not restrict what other files can see. That enforcement comes in [Chapter 42](chapter-42.md), when the import system scans a file's exported declarations to build the set of available symbols.

Think of this chapter as drawing the line. Chapter 42 is what enforces it.

## Cliffhanger

The `extern def` repetition problem is still unsolved. In the program above, `main.pyxc` still needs:

```pyxc
extern def add(x: int, y: int) -> int
```

Chapter 42 fixes this: `import app.math` finds `app/math.pyxc`, scans its `export` declarations, and injects them as prototypes — no `extern def` required.

## Error Cases

**`module` after a definition:**
```pyxc
def a() -> int:
  return 0

module late.name   # Error: module declaration must appear before other top-level forms
```

**Duplicate `module`:**
```pyxc
module app.a
module app.b   # Error: Only one module declaration is allowed per file
```

**`export` on a non-declaration:**
```pyxc
module app.bad
export 1 + 2   # Error: 'export' must be followed by a top-level declaration
```

**`module` or `export` in the REPL:**
```
>>> module foo
Error: 'module' is only supported in file mode
```

## Things Worth Knowing

**The module name is not the file path.** `module app.math` does not tell the compiler to look for `app/math.pyxc`. That mapping is the import resolver's job (Chapter 42). In this chapter, the name is purely informational.

**`export` on a struct exports the layout.** Importing a module that exports a struct gives you the field names, field types, and field offsets — everything needed to construct and use values of that type across files.

**`module` paths use dots, not slashes.** `module app.math` corresponds to a file at `app/math.pyxc` relative to the project root, but you write dots in the source and the toolchain handles the conversion. This is the same convention Python, Go, and Java use.

## What's Next

[Chapter 42](chapter-42.md) implements `import`: the compiler finds the source file, scans its `export` declarations, and makes them available — no `extern def` needed for pyxc-to-pyxc calls.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

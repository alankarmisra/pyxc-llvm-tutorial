---
title: "Build Your First Programming Language with LLVM"
description: "Learn compilers by building a real programming language from scratch."
---

# pyxc: Build Your First Programming Language with LLVM

Start with a small promise: by the end of this tutorial, you will have built a programming language.

It is called **pyxc**, pronounced “Pixie.” Its syntax begins close to Python, but its implementation is a C++ compiler built on LLVM. You will use it interactively, compile source files, and eventually produce native executables.

You do not need compiler-theory experience. You do need enough C++ to read classes, `unique_ptr`, containers, and ordinary functions. When a compiler term becomes useful, I will introduce it next to the code that gives it a concrete meaning.

## 1. Check the Starting Requirements

Before Chapter 1, make sure these commands exist:

```bash
c++ --version
cmake --version
git --version
```

LLVM is not required yet. Chapter 6 installs it. Chapters 1–5 deliberately build the frontend without LLVM so the lexer, parser, and diagnostics remain understandable on their own.

Clone the repository:

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial
```

## 2. Know the Compiler Boundary

A compiler can feel like one enormous mechanism. We will instead build a sequence of narrow transformations:

```text
source characters
    -> tokens
    -> syntax tree
    -> LLVM IR
    -> machine code
    -> running program
```

Each chapter leaves one new boundary working before the next begins. The basic unit is:

```text
one source fragment -> one observable compiler result
```

That result might be a token, a parsed node, an error with a caret, a line of LLVM IR, or a value printed by the JIT.

## 3. Build the Frontend First

Chapters 1–5 teach pyxc to read and understand source text:

```text
Chapter 1 -> lexer and tokens
Chapter 2 -> parser and abstract syntax tree
Chapter 3 -> operator precedence
Chapter 4 -> unary minus and remainder
Chapter 5 -> source locations and recoverable diagnostics
```

At the end of this phase, pyxc recognizes code such as:

```pyxc
def add(a, b): a + b
add(2, 3)
```

It does not execute the program yet. That separation is intentional:

```text
valid syntax first -> executable meaning second
```

## 4. Add LLVM and Execution

Chapter 6 installs LLVM and verifies that CMake can find it. Chapters 7–9 then add:

```text
AST codegen -> LLVM IR
LLVM IR     -> optimized JIT execution
stdin/file  -> two input modes using one compiler
```

The REPL boundary becomes:

```pyxc
ready> 1 + 2 * 3
```

```text
Parsed a top-level expression.
Evaluated to 7.000000
```

LLVM gives pyxc a portable backend. The frontend describes the program in LLVM IR; LLVM handles optimization and target-specific machine code.

## 5. Grow from Expressions into a Language

Chapters 10–14 add control flow and state:

```text
comparisons and if/for
mutable variables and assignment
indentation-based statement blocks
while, do/while, break, and continue
elif and unreachable-code handling
```

By then, pyxc begins to look like a small Python-shaped systems language:

```pyxc
def sum_to(n):
    var total = 0
    for var i = 1, i <= n, i = i + 1:
        total = total + i
    return total
```

## 6. Build a Native Toolchain

Chapters 15–17 add global variables, object-file and assembly emission, and one-command executable linking:

```bash
pyxc --emit exe -o program program.pyxc
```

## 7. Add Types, Data, and Memory

Chapters 18–23 replace the single implicit `double` with signed and unsigned integers, floating-point types, `bool`, `void`, casts, typed signatures, logical and bitwise operators, and `switch`.

```pyxc
def add(x: int32, y: int32) -> int32:
    return x + y
```

Chapters 24–35 build the C-style data model:

```text
structs and fields
pointers and pointer arithmetic
fixed-size arrays
malloc, free, and sizeof
type aliases
strings, characters, and Unicode literals
variadic extern calls
compound assignment and increment/decrement
```

At that point pyxc can express manual memory management and call ordinary C libraries.

## 8. Add Objects and Modules

Chapters 36–42 add classes, methods, constructors, visibility, traits, and generic trait conformance.

Chapters 43–45 add modules, exports, imports, and multi-file compilation:

```pyxc
# app/math.pyxc
module app.math

export def square(x: int) -> int:
    return x * x
```

```pyxc
# main.pyxc
module app.main
import app.math

def main() -> int:
    return square(4)
```

## 9. Keep the Future Track Separate

The implemented tutorial ends with the module system. Possible later work includes:

```text
closures and captured-variable lifetime rules
self-hosted tests and source-level coverage
concurrency and a shared-state safety model
enums and function pointers
generic functions and structs
a standard library
default and named arguments
multiple return values
assert, defer, and static locals
Unicode identifiers and string interpolation
type inference and stronger exhaustiveness checks
package discovery and module aliases
compile-time execution
formal verification and SMT-backed checks
formatter, language server, package manager, and debugger tooling
```

These are design directions, not promises about chapter numbering. They should become chapters only when their semantics are clear enough to teach as narrow, testable steps.

## 10. Use the Tutorial as a Build Loop

For every chapter:

1. Start from the previous chapter's working code.
2. Make one described change at a time.
3. Build after each meaningful boundary.
4. Run the immediate example.
5. Run the full chapter test suite before moving on.

When an example fails, keep the full diagnostic and exact input. Compiler bugs are much easier to reason about when reduced to one source fragment and one observed result.

You are ready to begin.

Next: [Chapter 1](chapter-01.md) turns raw characters into tokens.

## Need Help?

Build issues? Questions?

- [Report a problem with GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- [Ask a question in GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your operating system and version
- The chapter number
- The exact command you ran
- The complete error message
- The output of `c++ --version` and `cmake --version`
- The output of `llvm-config --version` for Chapter 6 and later

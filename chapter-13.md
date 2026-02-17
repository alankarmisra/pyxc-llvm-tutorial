---
description: "Fix forward references and mutual recursion by moving to a translation-unit style pipeline that decouples function use from source order."
---
# 12. Pyxc: Forward Function References and Mutual Recursion

This chapter addresses a foundational frontend issue: function calls should not depend on source order in file compilation mode.

We fix this with a translation-unit pipeline:

- parse and collect declarations first
- codegen function bodies second

That gives reliable forward references and mutual recursion.

## Source Code
To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter14](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter14).

## Grammar (EBNF)

This is a compiler pipeline chapter, not a syntax chapter. Grammar shape is unchanged.

Reference: `code/chapter14/pyxc.ebnf`

```ebnf
program      = { top_item } , eof ;
top_item     = newline | type_alias_decl | struct_decl
             | function_def | extern_decl | statement ;

function_def = "def" , prototype , ":" , suite ;
extern_decl  = "extern" , "def" , prototype ;
```

## Problem to solve

Forward call example:

```py

def main() -> i32:
    printf("%d\n", add(20, 22))
    return 0

def add(a: i32, b: i32) -> i32:
    return a + b

main()
```

Mutual recursion example:

```py

def is_even(n: i32) -> i32:
    if n == 0:
        return 1
    return is_odd(n - 1)

def is_odd(n: i32) -> i32:
    if n == 0:
        return 0
    return is_even(n - 1)
```

Both should compile and run without requiring manual reordering.

## Design

Two explicit phases per file:

1. Parse and collect
- parse defs/externs/top-level expressions
- pre-register prototypes for lookup

2. Codegen
- emit extern declarations
- emit function bodies
- emit top-level expressions

This removes declaration-order fragility.

## Translation unit container

```cpp
struct ParsedTranslationUnit {
  std::vector<std::unique_ptr<FunctionAST>> Definitions;
  std::vector<std::unique_ptr<PrototypeAST>> Externs;
  std::vector<std::unique_ptr<FunctionAST>> TopLevelExprs;
};
```

## Prototype registration and compatibility

We register prototypes as soon as parsed, with compatibility checks:

- `TypeExprEqual(...)`
- `PrototypeCompatible(...)`
- `ClonePrototype(...)`
- `RegisterPrototypeForLookup(...)`

If a name is redeclared with a different signature, emit:

```text
Function redeclared with incompatible signature
```

## Parse phase

`ParseTranslationUnit(...)` collects nodes and registers prototypes early.

Conceptually:

```cpp
if (CurTok == tok_def) {
  auto FnAST = ParseDefinition();
  RegisterPrototypeForLookup(FnAST->getProto());
  TU.Definitions.push_back(std::move(FnAST));
}
```

## Codegen phase

`CodegenTranslationUnit(...)` runs in this order:

1. externs
2. definitions
3. top-level expressions

This is the key ordering guarantee.

## Paths updated

In `code/chapter14/pyxc.cpp`:

- `InterpretFile(...)` now uses parse-then-codegen
- `CompileToObjectFile(...)` now uses parse-then-codegen

`FunctionAST` exposes:

```cpp
const PrototypeAST &getProto() const { return *Proto; }
```

so registration can happen before body codegen.

## Tests

Added tests:

- `code/chapter14/test/c28_forward_call.pyxc`
- `code/chapter14/test/c28_mutual_recursion.pyxc`
- `code/chapter14/test/c28_signature_mismatch.pyxc`

## Compile / Run / Test

Build:

```bash
cd code/chapter14 && ./build.sh
```

Run one sample:

```bash
code/chapter14/pyxc -i code/chapter14/test/c28_forward_call.pyxc
```

Run tests:

```bash
cd code/chapter14/test
lit -sv .
```

Explore by reordering functions or introducing a mismatched `extern` signature and checking diagnostics.

Clean artifacts:

```bash
cd code/chapter14 && ./build.sh
```


## Need Help?

Stuck on something? Have questions about this chapter? Found an error?

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report bugs, errors, or problems
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- The chapter you're working on
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message or unexpected behavior
- What you've already tried

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!

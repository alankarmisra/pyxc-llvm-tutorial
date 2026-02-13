# 28. Forward Function References and Mutual Recursion

Chapter 27 still had a sharp edge: in file mode, function calls were effectively sensitive to declaration order in some paths.

Chapter 28 fixes that by introducing a proper translation-unit pipeline:

- parse and collect declarations first
- codegen bodies second

That gives us stable support for forward calls and mutual recursion without requiring users to reorder source files unnaturally.

!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter28](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter28).

## Grammar (EBNF)

Chapter 28 is a compiler pipeline chapter, not a syntax chapter. The grammar shape from Chapter 27 remains unchanged.

Reference: `code/chapter28/pyxc.ebnf`

```ebnf
program      = { top_item } , eof ;
top_item     = newline | type_alias_decl | struct_decl
             | function_def | extern_decl | statement ;

function_def = "def" , prototype , ":" , suite ;
extern_decl  = "extern" , "def" , prototype ;

statement    = if_stmt | match_stmt | for_stmt | while_stmt | do_while_stmt
             | break_stmt | continue_stmt | free_stmt | print_stmt
             | return_stmt | const_decl_stmt
             | typed_assign_stmt | assign_stmt | expr_stmt ;
```

## The user-visible problem

Before this chapter, code like this could fail depending on compilation path:

```py

def main() -> i32:
    printf("%d\n", add(20, 22))
    return 0

def add(a: i32, b: i32) -> i32:
    return a + b

main()
```

Even though the function is defined in the same file, the call appears before `add` is seen by some codegen paths.

Mutual recursion had the same issue:

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

## Design in Chapter 28

We now treat each input file as a translation unit with two explicit phases.

Phase 1: parse and collect

- type aliases and structs are parsed as before
- function definitions are parsed and stored
- extern prototypes are parsed and stored
- top-level expressions are parsed and stored
- function prototypes are pre-registered in `FunctionProtos` so later bodies can resolve calls

Phase 2: codegen

- extern prototypes emit declarations
- function definitions emit IR bodies
- top-level expressions emit and execute normally (in interpreter mode)

This means name resolution no longer depends on whether a call appears textually before or after a function body.

## New translation-unit container

In `code/chapter28/pyxc.cpp`, we add a small holder:

```cpp
struct ParsedTranslationUnit {
  std::vector<std::unique_ptr<FunctionAST>> Definitions;
  std::vector<std::unique_ptr<PrototypeAST>> Externs;
  std::vector<std::unique_ptr<FunctionAST>> TopLevelExprs;
};
```

This keeps parse products separate and makes phase ordering explicit.

## Prototype pre-registration and signature checks

To make forward resolution safe, we register prototypes as soon as they are parsed.

Key helpers added in `code/chapter28/pyxc.cpp`:

- `TypeExprEqual(...)`
- `PrototypeCompatible(...)`
- `ClonePrototype(...)`
- `RegisterPrototypeForLookup(...)`

Registration behavior:

- if name is new: store prototype in `FunctionProtos`
- if name already exists and signature matches: accept
- if name already exists and signature differs: error

Error message:

```text
Function redeclared with incompatible signature
```

That prevents silent ABI/type mismatches across `extern` declarations and function definitions.

## Parsing phase

`ParseTranslationUnit(...)` replaces the old direct parse+codegen walk in file pipelines.

Conceptually:

```cpp
if (CurTok == tok_def) {
  auto FnAST = ParseDefinition();
  RegisterPrototypeForLookup(FnAST->getProto());
  TU.Definitions.push_back(std::move(FnAST));
}
```

and similarly for `extern` and top-level expressions.

Important detail:

- this phase does not emit function bodies yet
- it only ensures declarations are known and AST is collected

## Codegen phase

`CodegenTranslationUnit(...)` executes in a deterministic order:

1. emit extern declarations
2. emit function definitions
3. emit top-level expressions

That ordering is what makes forward/mutual calls robust.

## Interpreter and compiler paths now use the same model

In `code/chapter28/pyxc.cpp`:

- `InterpretFile(...)` now does parse-then-codegen using the new translation-unit helpers
- `CompileToObjectFile(...)` also does parse-then-codegen using the same helpers

This removes behavior drift between modes and avoids having one mode support a pattern while another mode breaks.

## Small AST utility added

`FunctionAST` now exposes:

```cpp
const PrototypeAST &getProto() const { return *Proto; }
```

This is used during parse-time prototype registration without prematurely consuming ownership needed for later body codegen.

## What works now

Forward call in same file:

```py

def main() -> i32:
    printf("%d\n", add(20, 22))
    return 0

def add(a: i32, b: i32) -> i32:
    return a + b

main()
```

Mutual recursion:

```py

def is_even(n: i32) -> i32:
    if n == 0:
        return 1
    return is_odd(n - 1)

def is_odd(n: i32) -> i32:
    if n == 0:
        return 0
    return is_even(n - 1)

def main() -> i32:
    printf("%d %d\n", is_even(10), is_odd(10))
    return 0

main()
```

## New Chapter 28 tests

Added in `code/chapter28/test`:

- `c28_forward_call.pyxc`
- `c28_mutual_recursion.pyxc`
- `c28_signature_mismatch.pyxc`

They validate:

- forward definition calls succeed
- mutual recursion succeeds
- incompatible redeclaration fails with the expected diagnostic

## Compile / Run / Test

Build chapter 28:

```bash
make -C code/chapter28 clean all
```

Run a forward-call sample:

```bash
code/chapter28/pyxc -i code/chapter28/test/c28_forward_call.pyxc
```

Run the chapter tests:

```bash
cd code/chapter28/test
lit -sv .
```

Try editing the new tests and intentionally break a signature or reorder functions to see which diagnostics trigger.

When you're done:

```bash
make -C code/chapter28 clean
```

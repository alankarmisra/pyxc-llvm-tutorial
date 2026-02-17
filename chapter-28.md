---
description: "Add const local bindings and compile-time reassignment checks to preserve immutability intent and reduce accidental state changes."
---
# 26. Pyxc: Immutable Bindings with const

Chapter 26 introduces `const` local declarations with compile-time reassignment protection.

This chapter is small in syntax but important in semantics: we are teaching the compiler to remember mutability intent for each binding.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter26](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter26).

## What we are building

A declaration like this:

```py
const max_count: i32 = 64
```

should:

- parse as a declaration statement
- require an initializer
- create a symbol-table binding marked immutable
- reject later assignment to `max_count`

## Grammar changes

In `code/chapter26/pyxc.ebnf`, we add a dedicated statement production:

```ebnf
const_decl_stmt = "const" , identifier , ":" , type_expr , "=" , expression ;
```

and include it in `statement`:

```ebnf
statement =
    print_stmt
  | if_stmt
  | for_stmt
  | while_stmt
  | do_while_stmt
  | return_stmt
  | var_assign_stmt
  | const_decl_stmt
  | free_stmt
  | expr_stmt ;
```

Why this matters:

- `const` is not “just assignment with a flag.”
- It is a declaration form with stricter rules (initializer required).

## Lexer keyword support

In `code/chapter26/pyxc.cpp`, add a token kind and keyword mapping.

Token enum gets:

```cpp
tok_const = -37,
```

Keyword map gets:

```cpp
{"const", tok_const}
```

Without this, parser logic would never see a distinct `const` token.

## Parser: dedicated const declaration function

Parser entry in `ParseStmt()`:

```cpp
case tok_const:
  return ParseConstDeclStmt();
```

The new parser function (`ParseConstDeclStmt`) enforces the exact shape:

```cpp
// const name: Type = expr
if (CurTok != tok_const) ...
if (CurTok != tok_identifier) ...
if (CurTok != ':') ...
auto DeclTy = ParseTypeExpr();
if (CurTok != '=') ...
auto Init = ParseExpression();
```

Key behavior: missing initializer errors immediately, instead of silently defaulting.

## AST and symbol metadata

Chapter 26 adds a separate AST node for const declaration and extends binding metadata.

`VarBinding` in `code/chapter26/pyxc.cpp` now has:

```cpp
struct VarBinding {
  AllocaInst *Alloca = nullptr;
  Type *Ty = nullptr;
  Type *PointeeTy = nullptr;
  std::string BuiltinLeafTy;
  std::string PointeeBuiltinLeafTy;
  bool IsConst = false;
};
```

This is the core decision: mutability is tracked in symbol-table state.

## Codegen for const declaration

Const declaration lowering mirrors regular typed declaration flow:

- resolve declared type
- allocate storage
- codegen initializer
- cast initializer to declared type
- store value
- insert `NamedValues[name]` binding with `IsConst = true`

Representative insertion shape:

```cpp
NamedValues[Name] = {Alloca, DeclTy, ResolvePointeeTypeExpr(DeclType),
                     BuiltinLeaf, PointeeBuiltinLeaf, true};
```

So the generated IR is normal; policy is enforced by semantic checks around stores.

## Reassignment protection

The assignment path checks mutability before emitting a store.

In assignment codegen, Chapter 26 adds:

```cpp
auto It = NamedValues.find(*Name);
if (It != NamedValues.end() && It->second.IsConst)
  return LogError<Value *>("Cannot assign to const variable");
```

That ensures:

- reassignment to mutable locals still works
- reassignment to const locals fails at compile/lowering time

No runtime check needed.

## Test coverage

Chapter 26 adds four focused tests in `code/chapter26/test`.

Positive:

```py
# const_basic_scalar.pyxc
const base: i32 = 40
print(base + 5)
```

```py
# const_pointer_string.pyxc
const msg: ptr[i8] = "hello const"
puts(msg)
```

Negative:

```py
# const_error_reassign.pyxc
const x: i32 = 7
x = 8
```

Expected diagnostic includes:

```text
Cannot assign to const variable
```

```py
# const_error_missing_initializer.pyxc
const y: i32
```

Expected diagnostic includes:

```text
Const declaration requires initializer
```

## End-to-end behavior

With Chapter 26 complete:

- the language has explicit immutable local bindings
- parser enforces strict declaration form
- codegen carries immutability in symbol state
- assignment path respects immutability
- tests lock both happy paths and failures

This gives us safer local semantics before we scale into multi-file workflows in Chapter 27.

## Compiling

From repository root:

```bash
cd code/chapter26 && ./build.sh
```

## Testing

From repository root:

```bash
lit -sv code/chapter26/test
```

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter26 && ./build.sh
```

Run one sample program:

```bash
code/chapter26/pyxc -i code/chapter26/test/addr_is_keyword.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter26/test
lit -sv .
```

Explore the test folder a bit and add one tiny edge case of your own.

When you're done, clean artifacts:

```bash
cd code/chapter26 && ./build.sh
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

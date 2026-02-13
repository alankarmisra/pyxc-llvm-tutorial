# 24. Immutable Bindings with const

Chapter 24 introduces `const` local declarations with compile-time reassignment protection.

This chapter is small in syntax but important in semantics: we are teaching the compiler to remember mutability intent for each binding.

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

In `code/chapter24/pyxc.ebnf`, we add a dedicated statement production:

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

In `code/chapter24/pyxc.cpp`, add a token kind and keyword mapping.

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

Chapter 24 adds a separate AST node for const declaration and extends binding metadata.

`VarBinding` in `code/chapter24/pyxc.cpp` now has:

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

In assignment codegen, Chapter 24 adds:

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

Chapter 24 adds four focused tests in `code/chapter24/test`.

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

With Chapter 24 complete:

- the language has explicit immutable local bindings
- parser enforces strict declaration form
- codegen carries immutability in symbol state
- assignment path respects immutability
- tests lock both happy paths and failures

This gives us safer local semantics before we scale into multi-file workflows in Chapter 25.

## Full Source Code

Chapter 24 implementation lives in:

- `code/chapter24/pyxc.ebnf`
- `code/chapter24/pyxc.cpp`
- `code/chapter24/runtime.c`
- `code/chapter24/Makefile`
- `code/chapter24/test/`

## Compiling

From repository root:

```bash
make -C code/chapter24 clean all
```

## Testing

From repository root:

```bash
lit -sv code/chapter24/test
```

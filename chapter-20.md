# 20. Fixed-Size Arrays

Chapter 19 gave us named aggregate data with structs.

That solved *shape*, but not *collections*. We still could not represent “N elements of T” as one typed value.

Chapter 20 adds fixed-size arrays so Pyxc can model contiguous collections with compile-time length.

This is a major usability milestone because many real programs need both:

- records (`struct`)
- collections (`array`)

and the ability to combine them.

## Why this chapter matters

Before Chapter 20, we could index pointers, but we had no first-class array type.

That made local contiguous storage awkward. With Chapter 20 we can write:

```py
a: array[i32, 4]
a[0] = 10
a[1] = 20
print(a[0], a[1])
```

Now the language can model stack-allocated fixed buffers, array fields inside structs, and arrays of structs.

## Scope and constraints

Chapter 20 supports:

- array type syntax: `array[T, N]`
- compile-time positive integer size `N`
- array indexing for load/store
- arrays mixed with structs (both directions)

Chapter 20 does not support yet:

- dynamic arrays
- array literals
- direct initializer expressions for struct/array locals

The static-only scope keeps codegen and diagnostics clean.

## What changed from Chapter 19

Primary diff:

- `code/chapter19/pyxc.cpp` -> `code/chapter20/pyxc.cpp`
- `code/chapter19/pyxc.ebnf` -> `code/chapter20/pyxc.ebnf`

Implementation buckets:

1. type-expression model extended with `Array`
2. parser support for `array[ElemType, Size]`
3. LLVM type resolution for arrays
4. indexing codegen support for array bases
5. type-hint propagation through arrays
6. array-focused tests and diagnostics

## Grammar updates

`code/chapter20/pyxc.ebnf` extends `type_expr`:

```ebnf
type_expr       = pointer_type
                | array_type
                | base_type ;

array_type      = "array" , "[" , type_expr , "," , array_size , "]" ;
array_size      = number ;
```

No expression-level syntax was needed for indexing because `[...]` already existed in postfix grammar from earlier chapters.

## Type model changes (TypeExpr)

Chapter 20 extends type kinds:

```cpp
enum class TypeExprKind { Builtin, AliasRef, Pointer, Array };
```

and adds size storage on the node:

```cpp
uint64_t ArraySize = 0;
```

plus a constructor helper:

```cpp
static std::shared_ptr<TypeExpr> Array(std::shared_ptr<TypeExpr> Elem,
                                       uint64_t Size)
```

This keeps arrays in the same recursive type-expression system as pointers and aliases.

## Parsing array[...]

`ParseTypeExpr()` gets an `array` branch that enforces exact shape and size constraints:

- must see `array`
- must see `[`
- parse element type recursively
- must see `,`
- size must be positive integer literal
- must see closing `]`

Core size validation:

```cpp
if (CurTok != tok_number || !NumIsIntegerLiteral || NumIntVal <= 0)
  return LogError<TypeExprPtr>(
      "Expected positive integer literal array size");
```

The “literal only” rule is deliberate. It keeps array layout known at compile time.

## LLVM lowering for array types

`ResolveTypeExpr(...)` adds array lowering:

```cpp
if (Ty->Kind == TypeExprKind::Array) {
  Type *ElemTy = ResolveTypeExpr(Ty->Elem, Visited);
  ...
  return ArrayType::get(ElemTy, Ty->ArraySize);
}
```

That means arrays now work in every typed location that calls `ResolveTypeExpr`:

- local declarations
- struct fields
- aliases
- function signatures (where permitted by existing rules)

## Indexing behavior: pointer base vs array base

This is the most important runtime/codegen change in Chapter 20.

`IndexExprAST::codegenAddress()` now supports two distinct base categories:

1. pointer base (existing path)
2. array base (new path)

### Array-base path

When base value type is an LLVM array, address computation uses a two-index GEP:

```cpp
Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
return Builder->CreateGEP(BaseValTy, BaseAddr, {Zero, IdxV}, "arr.idx.addr");
```

Why `{0, idx}`?

- first index steps from the alloca pointer to the array object
- second index steps to the element inside that array object

This is standard LLVM aggregate indexing semantics.

### Shared index diagnostics

Both pointer and array index paths require integer index types:

- `Array index must be an integer type`

This prevents silent float-to-int indexing mistakes.

## Hint propagation and type inference adjustments

To keep downstream behavior consistent (printing, assignments, chained access), Chapter 20 updates helper logic so array element type info flows through:

- `ResolvePointeeTypeExpr` recurses through arrays
- builtin-leaf-name helpers recurse through arrays
- `IndexExprAST::getValueTypeHint()` returns array element type when base is array

Without these “small” changes, indexing would compile but later type-dependent features would degrade.

## Initialization policy remains conservative

Direct initializer expressions for struct/array locals are still blocked in Chapter 20:

```cpp
"Struct/array variables do not support direct initializer expressions"
```

This is an intentional tradeoff. It keeps aggregate initialization semantics out of this chapter so we can focus on layout and indexing correctness first.

## Test coverage added in Chapter 20

Chapter 20 inherits Chapter 19 tests and adds:

- `code/chapter20/test/array_basic_local.pyxc`
- `code/chapter20/test/array_alias_type.pyxc`
- `code/chapter20/test/array_struct_field.pyxc`
- `code/chapter20/test/array_of_structs.pyxc`
- `code/chapter20/test/array_error_non_integer_index.pyxc`
- `code/chapter20/test/array_error_invalid_size.pyxc`

### Representative positive scenario

```py
def main() -> i32:
    a: array[i32, 4]
    a[0] = 10
    a[1] = 20
    a[2] = 30
    a[3] = 40
    print(a[0], a[1], a[2], a[3])
    return 0

main()
```

### Struct + array composition

```py
struct Bucket:
    vals: array[i32, 3]

b: Bucket
b.vals[1] = 8
print(b.vals[1])
```

### Negative scenario

```py
def main() -> i32:
    a: array[i32, 2]
    idx: f64 = 1.0
    print(a[idx])
    return 0

main()
```

This ensures index typing rules are actually enforced.

## Build and test for this chapter

From repository root:

```bash
cd code/chapter20
make
lit -sv test
```

Chapter 20 was also regression-checked against Chapter 19 test behavior.

## Design takeaways

Chapter 20 establishes a clean split:

- `array[T, N]` for fixed compile-time contiguous storage
- `ptr[T]` for pointer-based access and indirection

Because both integrate with shared postfix/index infrastructure, the language now handles:

- scalar operations
- struct member access
- array indexing
- combinations like `arr[i].field`

That sets up Chapter 21 naturally: once arrays and struct layout are stable, dynamic allocation (`malloc/free`) becomes much more valuable.

## Full Source Code

Chapter 20 implementation lives in:

- `code/chapter20/pyxc.ebnf`
- `code/chapter20/pyxc.cpp`
- `code/chapter20/runtime.c`
- `code/chapter20/Makefile`
- `code/chapter20/test/`

## Compiling

From repository root:

```bash
make -C code/chapter20 clean all
```

## Testing

From repository root:

```bash
lit -sv code/chapter20/test
```

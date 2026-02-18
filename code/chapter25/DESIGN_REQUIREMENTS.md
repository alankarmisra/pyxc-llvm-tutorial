# Chapter 21 Design Requirements

## Theme
Fixed-size arrays as first-class typed aggregates.

## Goal
Add static arrays to Pyxc with explicit type-level sizes and element indexing while preserving Chapter 20 struct behavior.

## Scope

### In Scope
- Array type syntax in type positions:
  - `array[ElemType, N]`
- Typed locals and fields using array types
- Array indexing for load/store:
  - `a[i]`
  - `s.arr[i]`
- Nested composition:
  - arrays of structs
  - structs containing arrays
- Existing pointer indexing remains supported

### Out of Scope
- Dynamic arrays
- Array literals/initializer lists
- Slices/views
- Runtime bounds checking (this chapter)

## Syntax Requirements

### Array type
```py
a: array[i32, 4]
```

### Indexing
```py
a[0] = 10
print(a[0])
```

## Lexer Requirements
- No new keyword required.
- `array` is recognized via type parser in type contexts.

## Parser Requirements
- Extend `type_expr` parser to recognize `array[ type_expr , <int-literal> ]`.
- Enforce compile-time integer literal size.
- Reuse existing indexing expression syntax.

## AST Requirements
- Extend `TypeExpr` to represent array type with:
  - element type
  - constant length

## Semantic Requirements
- Array size must be a positive integer literal.
- Array indexing base must be either:
  - pointer type (existing behavior), or
  - array type (new behavior)
- Index expression must be integer-like.

## LLVM Lowering Requirements
- Lower `array[T, N]` to `llvm::ArrayType::get(T, N)`.
- For array indexing, lower address as GEP:
  - `[0, idx]` on the array alloca/field address.
- Pointer indexing continues to use pointer GEP behavior.

## Diagnostics Requirements
- Invalid array size (missing/non-integer/non-positive)
- Indexing non-pointer/non-array base
- Non-integer index expression

## Tests

### Positive
- Basic local array load/store
- Array alias type
- Struct field array indexing
- Array of structs with field access through indexed element

### Negative
- Float/non-integer index
- Invalid array size literal
- Indexing non-array/non-pointer

## Done Criteria
- Chapter 21 lit suite includes array coverage and is green
- Chapter 20 suite behavior remains intact under Chapter 21 compiler
- `chapter-21.md` documents chapter diff and implementation

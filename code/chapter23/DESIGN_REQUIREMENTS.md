# Chapter 20 Design Requirements

## Theme
Structured data with named fields.

## Goal
Add first-class `struct` support to Pyxc with typed field access and assignment while preserving all Chapter 19 behavior.

## Scope

### In Scope
- Top-level struct declarations:
  - `struct Name:` followed by indented typed field declarations
- Struct types usable in typed locals and function signatures
- Field read access: `obj.field`
- Field assignment: `obj.field = expr`
- Nested field access (e.g. `outer.inner.x`)
- Struct type aliases via existing `type Alias = StructName`

### Out of Scope
- Struct literals/constructors
- Methods/member functions
- Inheritance
- Packed/bitfield layout controls
- Generic structs

## Syntax Requirements

### Struct declaration
```py
struct Point:
    x: i32
    y: i32
```

### Local declaration and field assignment
```py
p: Point
p.x = 10
p.y = 20
```

### Field read
```py
print(p.x, p.y)
```

## Lexer Requirements
- Add keyword token:
  - `tok_struct`
- Ensure `.` can be tokenized for member access (and not consumed as a malformed number).

## Parser Requirements
- Add top-level parser:
  - `ParseStructDecl()`
- Extend top-level dispatch to accept `struct` declarations.
- Extend postfix parsing to handle member access:
  - `postfix_expr '.' identifier`
- Preserve existing call/index postfix chaining behavior.

## AST Requirements
- Add member access expression node:
  - `MemberExprAST(BaseExpr, FieldName)`
- Member expression must support:
  - value codegen (field load)
  - address codegen (for assignment lvalue path)
  - type hints for downstream operations (`print`, casts, etc.)

## Semantic Requirements
- Maintain a struct declaration table with:
  - declaration order
  - field names
  - field type expressions
  - per-struct LLVM type cache
- Reject duplicate struct names.
- Reject duplicate field names inside a struct.
- Reject unknown field names in member access.
- Reject member access on non-struct types.
- Keep current assignment/lvalue rules: field assignment requires addressable base.

## LLVM Lowering Requirements
- Lower each struct declaration to an LLVM `StructType`.
- Resolve field types in declaration order.
- Lower `obj.field` address as struct GEP to field index.
- Lower field read as load from computed field address.
- Lower field assignment through existing assignment codepath (`codegenAddress` + store).

## Interaction Requirements
- Existing Chapter 19 loop/control/operator behavior remains unchanged.
- Existing type aliases continue to work and may alias structs.
- Existing pointer/indexing behavior remains unchanged.

## Diagnostics Requirements
- Duplicate struct declaration
- Duplicate field in struct
- Unknown struct type in typed declaration/signature
- Unknown field name on struct
- Member access on non-struct value
- Missing/invalid struct declaration syntax (`:`, indent, field type)

## Tests

### Positive
- Basic struct with two scalar fields
- Nested structs and chained field access
- Struct alias usage (`type P = Point`)
- Field assignment + readback in loops/conditionals

### Negative
- Unknown field access
- Member access on non-struct value
- Duplicate field in struct declaration
- Duplicate struct declaration name

## Done Criteria
- Chapter 20 lit suite includes struct coverage and passes
- Chapter 19 behavior remains green under Chapter 20 compiler
- `chapter-20.md` documents the feature and code diffs from Chapter 19

## Implementation Sequencing Notes
1. Copy Chapter 19 baseline into `code/chapter20/`
2. Add struct token + declaration parser
3. Add member access AST + codegen
4. Add struct type resolution/cache
5. Add tests and harden diagnostics

# Chapter 22 Design Requirements

## Theme
Dynamic heap allocation with explicit `malloc`/`free` language builtins.

## Goal
Add practical dynamic memory support to Pyxc using typed allocation and explicit deallocation, while preserving Chapter 21 structs/arrays behavior.

## Scope

### In Scope
- Typed allocation expression:
  - `malloc[T](count)`
- Deallocation statement:
  - `free(ptr_expr)`
- Works with scalar, struct, and array element types
- Interacts with existing indexing/member access:
  - `p[0].x = ...`

### Out of Scope
- Garbage collection
- Smart pointers / ownership model
- `realloc`/`calloc`
- Lifetime analysis / leak diagnostics

## Syntax Requirements

### Malloc expression
```py
p: ptr[i32] = malloc[i32](4)
```

### Free statement
```py
free(p)
```

## Lexer Requirements
- Add keywords/tokens:
  - `tok_malloc`
  - `tok_free`

## Parser Requirements
- Add malloc parser:
  - `ParseMallocExpr()`
- Add free statement parser:
  - `ParseFreeStmt()`
- Extend `ParsePrimary()` to accept `malloc` expression form.
- Extend `ParseStmt()` dispatch with `free` statement.

## AST Requirements
- Add nodes:
  - `MallocExprAST(ElemType, CountExpr)`
  - `FreeStmtAST(PtrExpr)`

## Semantic Requirements
- `malloc[T](count)`:
  - `T` must resolve to a non-void type.
  - `count` must be integer-like.
  - expression result is pointer-typed and carries pointee type hint `T`.
- `free(ptr_expr)`:
  - operand must be pointer-typed.

## LLVM Lowering Requirements
- `malloc` lowers to runtime/extern `malloc` call with byte count:
  - `bytes = count * sizeof(T)`
- `free` lowers to runtime/extern `free` call.
- Use module data layout for `sizeof(T)`.

## Diagnostics Requirements
- Non-integer malloc count
- Unknown/void malloc element type
- `free` called with non-pointer expression
- Syntax errors in `malloc[T](count)` and `free(expr)` forms

## Tests

### Positive
- malloc/free for scalar pointer + indexed writes/reads
- malloc/free for struct pointer + field writes/reads
- malloc/free for array element types where indexing/member chains work

### Negative
- malloc with float count
- free with non-pointer argument

## Done Criteria
- Chapter 22 lit suite includes malloc/free coverage and passes
- Chapter 21 behavior remains green under Chapter 22 compiler
- `chapter-22.md` documents chapter diff and implementation

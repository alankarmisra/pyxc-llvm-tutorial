# 21. Dynamic Memory with malloc and free

Chapter 20 gave us fixed-size arrays and stronger aggregate modeling.

But everything was still fundamentally static in storage duration unless values lived in existing stack locals or passed pointers from outside.

Chapter 21 adds the missing runtime allocation primitive:

- allocate memory on heap with `malloc[T](count)`
- release it with `free(ptr_expr)`

This is the first chapter where memory lifetime becomes an explicit programming concern in Pyxc.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter21](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter21).

## Why this chapter matters

With only fixed-size stack values, many common patterns are blocked:

- runtime-sized buffers
- long-lived dynamically allocated structures
- explicit memory ownership patterns

Chapter 21 does not try to solve lifetime ergonomics. It gives manual control, C-style, which is exactly what we need for systems-oriented interop and later language work.

## Scope and constraints

Chapter 21 supports:

- typed allocation expression: `malloc[T](count)`
- explicit deallocation statement: `free(ptr_expr)`
- scalar/struct/array-element allocation targets

Chapter 21 intentionally does not support:

- garbage collection
- ownership checker/borrow model
- `calloc`/`realloc`
- leak diagnostics

This is deliberate. The focus is “correct lowering + clear diagnostics”, not full memory management policy.

## What changed from Chapter 20

Primary diff:

- `code/chapter20/pyxc.cpp` -> `code/chapter21/pyxc.cpp`
- `code/chapter20/pyxc.ebnf` -> `code/chapter21/pyxc.ebnf`

Implementation buckets:

1. new tokens/keywords (`malloc`, `free`)
2. parser support for malloc expression and free statement
3. AST nodes (`MallocExprAST`, `FreeStmtAST`)
4. LLVM lowering helpers for external `malloc`/`free`
5. semantic checks on element type, count, and free operand type
6. dedicated tests

## Grammar and syntax additions

In `code/chapter21/pyxc.ebnf`, Chapter 21 adds:

```ebnf
statement       = ...
                | free_stmt
                | ... ;

free_stmt       = "free" , "(" , expression , ")" ;

primary         = number
                | identifier
                | malloc_expr
                | addr_expr
                | paren_expr
                | var_expr ;

malloc_expr     = "malloc" , "[" , type_expr , "]" , "(" , expression , ")" ;
```

Design choice:

- `malloc` is an expression (can appear in assignment/initialization)
- `free` is a statement (effectful operation, no meaningful value)

That closely matches programmer intuition.

## Lexer changes

New tokens in `Token` enum:

```cpp
tok_malloc = -33,
tok_free = -34,
```

Keyword map entries:

```cpp
{"malloc", tok_malloc},
{"free", tok_free}
```

Token debug names were also added:

```cpp
case tok_malloc: return "<malloc>";
case tok_free: return "<free>";
```

## Parser changes

### ParseMallocExpr()

`malloc` parsing is strict on delimiters and shape:

```cpp
malloc[TypeExpr](CountExpr)
```

It validates:

- `malloc`
- `[` element type `]`
- `(` count expression `)`

and returns `MallocExprAST`.

### ParseFreeStmt()

`free` is parsed as statement syntax:

```cpp
free(expr)
```

and returns `FreeStmtAST`.

### Dispatch integration

- `ParsePrimary()` adds `tok_malloc`
- `ParseStmt()` adds `tok_free`

This keeps syntax orthogonal and avoids ambiguity.

## AST additions

Chapter 21 introduces two nodes:

```cpp
class MallocExprAST : public ExprAST {
  TypeExprPtr ElemType;
  std::unique_ptr<ExprAST> CountExpr;
  ...
};

class FreeStmtAST : public StmtAST {
  std::unique_ptr<ExprAST> PtrExpr;
  ...
};
```

`MallocExprAST` also implements type-hint methods so existing indexing/member machinery can use pointee information:

- result type is pointer-like
- pointee type resolves from `ElemType`

That is why patterns like `p[0].x` continue to work when `p` comes from `malloc[Point](1)`.

## LLVM lowering helpers for libc allocation APIs

Chapter 21 introduces helper functions to declare or reuse external symbols:

```cpp
static Function *GetOrCreateMallocHelper() {
  if (Function *F = TheModule->getFunction("malloc"))
    return F;
  FunctionType *FT = FunctionType::get(
      PointerType::get(*TheContext, 0), {Type::getInt64Ty(*TheContext)}, false);
  return Function::Create(FT, Function::ExternalLinkage, "malloc",
                          TheModule.get());
}

static Function *GetOrCreateFreeHelper() {
  if (Function *F = TheModule->getFunction("free"))
    return F;
  FunctionType *FT = FunctionType::get(
      Type::getVoidTy(*TheContext), {PointerType::get(*TheContext, 0)}, false);
  return Function::Create(FT, Function::ExternalLinkage, "free", TheModule.get());
}
```

This pattern mirrors how other external helpers are handled across chapters.

## malloc codegen details

`MallocExprAST::codegen()` performs these steps:

1. Resolve element type `T`
2. Reject `void` element type
3. Codegen count expression
4. Require integer count type
5. Cast count to `i64`
6. Compute byte size: `count * sizeof(T)`
7. Emit call to external `malloc`

Representative code:

```cpp
Type *ElemTy = ResolveTypeExpr(ElemType);
if (!ElemTy)
  return nullptr;
if (ElemTy->isVoidTy())
  return LogError<Value *>("malloc element type cannot be void");

Value *CountV = CountExpr->codegen();
if (!CountV)
  return nullptr;
if (!IsIntegerLike(CountV->getType()))
  return LogError<Value *>("malloc count must be an integer type");
CountV = CastValueTo(CountV, Type::getInt64Ty(*TheContext));

uint64_t ElemSize = TheModule->getDataLayout().getTypeAllocSize(ElemTy);
Value *ElemSizeV = ConstantInt::get(Type::getInt64Ty(*TheContext), ElemSize, false);
Value *BytesV = Builder->CreateMul(CountV, ElemSizeV, "malloc.bytes");

return Builder->CreateCall(GetOrCreateMallocHelper(), {BytesV}, "malloc.ptr");
```

### Why DataLayout-based size matters

Using `TheModule->getDataLayout().getTypeAllocSize(ElemTy)` makes size computation target-aware. This is essential for correctness across platforms/ABIs.

## free codegen details

`FreeStmtAST::codegen()` is intentionally simple and strict:

```cpp
Value *PtrV = PtrExpr->codegen();
if (!PtrV)
  return nullptr;
if (!PtrV->getType()->isPointerTy())
  return LogError<Value *>("free expects a pointer argument");

Builder->CreateCall(GetOrCreateFreeHelper(), {PtrV});
```

The key semantic rule is clear: only pointer-typed expressions can be freed.

## Semantics and diagnostics in this chapter

Chapter 21 introduces high-signal errors for common misuse:

- `malloc element type cannot be void`
- `malloc count must be an integer type`
- `free expects a pointer argument`

These diagnostics are intentionally direct so mistakes are easy to fix.

## Language examples

### Scalar allocation

```py
def main() -> i32:
    p: ptr[i32] = malloc[i32](4)
    p[0] = 10
    p[1] = 20
    print(p[0], p[1])
    free(p)
    return 0

main()
```

### Struct allocation

```py
struct Point:
    x: i32
    y: i32

def main() -> i32:
    p: ptr[Point] = malloc[Point](1)
    p[0].x = 7
    p[0].y = 11
    print(p[0].x, p[0].y)
    free(p)
    return 0

main()
```

### Invalid free usage

```py
def main() -> i32:
    x: i32 = 42
    free(x)   # error: free expects a pointer argument
    return 0

main()
```

## Tests added in Chapter 21

Chapter 21 inherits Chapter 20 tests and adds:

- `code/chapter21/test/malloc_basic_i32.pyxc`
- `code/chapter21/test/malloc_struct_roundtrip.pyxc`
- `code/chapter21/test/malloc_array_element_type.pyxc`
- `code/chapter21/test/malloc_error_float_count.pyxc`
- `code/chapter21/test/free_error_non_pointer.pyxc`

These tests validate both positive flows (allocation + access + deallocation) and semantic rejection paths.

## Build and test for this chapter

From repository root:

```bash
cd code/chapter21 && ./build.sh
lit -sv test
```

Chapter 21 was also regression-checked against Chapter 20 behavior.

## Design takeaways

Chapter 21 is intentionally small but strategic:

- It introduces explicit heap lifetime control.
- It composes with existing struct/array/indexing features.
- It builds the bridge to practical C interop patterns.

This chapter also sets up Chapter 22 naturally, where we add C-style I/O (`putchar`, `puts`, `printf`) and string literals, making it possible to build more complete small programs.

## Compiling

From repository root:

```bash
cd code/chapter21 && ./build.sh
```

## Testing

From repository root:

```bash
lit -sv code/chapter21/test
```

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter21 && ./build.sh
```

Run one sample program:

```bash
code/chapter21/pyxc -i code/chapter21/test/array_alias_type.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter21/test
lit -sv .
```

Have some fun stress-testing the suite with small variations.

When you're done, clean artifacts:

```bash
cd code/chapter21 && ./build.sh
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

---
description: "Introduce a practical type system with C interop: typed values, pointers, ABI-correct signatures, and stronger compile-time checks."
---
# 21. Pyxc: Typed Interop: Core Types, Pointers, and ABI Correctness

Chapter 17 cleaned up operators and short-circuit behavior.  
In Chapter 18, we take the next major step: a practical type system that can interoperate with C APIs safely.

The focus of this chapter is not “advanced type theory.” It is interop-first typing:

- explicit scalar types
- pointer types and pointer indexing
- typed function signatures
- typed local declarations
- aliasing for C-like names (`int`, `size_t`, etc.)
- ABI-correct extern behavior for narrow signed/unsigned integers

The implementation for this chapter lives in:

- `code/chapter18/pyxc.cpp`
- `code/chapter18/runtime.c`


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter18](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter18).

## Grammar (EBNF)

Chapter 18 introduces the typed grammar surface: `type` aliases, typed params/returns, typed local declarations, and `ptr[...]`.

Reference: `code/chapter18/pyxc.ebnf`

```ebnf
top_item        = newline | type_alias_decl | function_def | extern_decl | statement ;
type_alias_decl = "type" , identifier , "=" , type_expr ;

prototype       = identifier , "(" , [ param_list ] , ")" , "->" , type_expr ;
param_list      = param , { "," , param } ;
param           = identifier , ":" , type_expr ;

typed_assign_stmt = identifier , ":" , type_expr , [ "=" , expression ] ;
type_expr       = pointer_type | base_type ;
pointer_type    = "ptr" , "[" , type_expr , "]" ;
```

## Why This Chapter Exists

Until Chapter 17, Pyxc effectively treated most values like doubles. That keeps early chapters simple, but it is not enough if you want real C interop.

For example, calling a C function that expects `i16`, `u8`, or `ptr[T]` requires:

1. precise type information in the AST
2. correct LLVM type lowering
3. correct ABI attributes at call boundaries

That is exactly what Chapter 18 introduces.

## Language Additions

### Core scalar types

Builtins in this chapter:

- signed: `i8`, `i16`, `i32`, `i64`
- unsigned: `u8`, `u16`, `u32`, `u64`
- floats: `f32`, `f64`
- `void`

### Pointer type constructor

```py
ptr[i32]
ptr[void]
ptr[ptr[i8]]
```

### Typed function prototypes

```py
extern def printd(x: f64) -> f64

def add(x: i32, y: i32) -> i32:
    return x + y
```

### Typed local declarations

```py
x: i32 = 10
p: ptr[i32] = addr(x)
```

### Type aliases

```py
type int = i32
type char = i8
type size = u64
```

## Pointer Operations in This Syntax

Chapter 18 uses Python-flavored pointer forms:

- `addr(x)` for address-of
- `p[n]` for dereference/indexing

Example:

```py
def main() -> i32:
    x: i32 = 10
    p: ptr[i32] = addr(x)
    p[0] = p[0] + 1
    return x
```

Type rules enforced:

1. base of `p[n]` must be pointer
2. index must be integer type
3. `ptr[void]` cannot be indexed

## Lexer and Token Updates

Chapter 18 adds key tokens and keywords:

- `type` -> `tok_type`
- `->` -> `tok_arrow`

Snippet:

```cpp
enum Token {
  tok_type = -25,
  tok_arrow = -26,
  // ...
};
```

The keyword map also includes `type`, and identifier lexing was updated to allow `_`, so aliases like `size_t` parse correctly.

## Type Representation in the Frontend

A recursive type-expression model is introduced:

```cpp
enum class TypeExprKind { Builtin, AliasRef, Pointer };

struct TypeExpr {
  TypeExprKind Kind;
  std::string Name;
  std::shared_ptr<TypeExpr> Elem;
};
```

This is important because parser-level type syntax (`ptr[ptr[i32]]`, aliases, etc.) needs structured representation before LLVM lowering.

## Parser Changes

### Parsing typed prototypes

`ParsePrototype()` now expects typed params and return types:

```text
name '(' param ':' type_expr {',' ...} ')' '->' type_expr
```

### Parsing top-level aliases

`ParseTypeAliasDecl()` handles:

```py
type Name = TypeExpr
```

### Parsing typed declarations and assignments

Identifier-leading statements are disambiguated into:

- typed declaration: `x: T` or `x: T = expr`
- assignment: `lhs = rhs`
- ordinary expression statement

### Parsing pointer/address constructs

- `addr(expr)` becomes `AddrExprAST`
- `p[n]` becomes `IndexExprAST`

## AST Additions

Key AST nodes added in this chapter:

- `TypedAssignStmtAST`
- `AssignStmtAST` (explicit statement form)
- `AddrExprAST`
- `IndexExprAST`
- typed `PrototypeAST` (arg types + return type)

`ExprAST` also gains hooks for lvalue/address/type hints used by assignment and pointer lowering:

```cpp
virtual Value *codegenAddress() { return nullptr; }
virtual Type *getValueTypeHint() const { return nullptr; }
virtual Type *getPointeeTypeHint() const { return nullptr; }
```

## Type Resolution and Alias Policy

Type lowering pipeline:

1. parse into `TypeExpr`
2. resolve alias chains
3. reject alias cycles
4. map to LLVM type

Chapter 18 also introduces default C-like aliases initialized from target data layout:

- `int`, `char`, `float`, `double`
- `long`, `size_t` derived from pointer width

This keeps interop target-aware instead of hardcoding one platform.

## Typed Codegen: Main Ideas

### Typed variable bindings

Chapter 17 tracked mostly allocas. Chapter 18 stores richer per-variable metadata (alloca + type hints) so pointer operations and print/type dispatch can work correctly.

### Conversion helper

A central cast helper is used across assignment/calls/returns:

```cpp
static Value *CastValueTo(Value *V, Type *DstTy) {
  // fp<->int, int<->int, ptr<->ptr, ptr<->int, etc.
}
```

### Boolean conversion helper

Conditions and logical ops now route through a generic truthiness converter:

```cpp
static Value *ToBoolI1(Value *V, const Twine &Name)
```

This supports int/float/pointer conditions consistently.

### Return correctness

Chapter 18 enforces:

- `return` with no expression only for `-> void`
- returning a value from `void` function is an error
- missing value in non-void return is an error

## ABI Correctness for Narrow Integer Externs

A key fix in this chapter is preserving signedness at ABI boundaries for narrow integers.

Without this, extern calls involving `i8/i16` can behave like zero-extended values.

Chapter 18 applies extension attributes in prototype codegen:

- `i8/i16` -> `signext`
- `u8/u16` -> `zeroext`

Conceptual snippet:

```cpp
if (narrowSigned)
  F->addParamAttr(i, Attribute::SExt);
if (narrowUnsigned)
  F->addParamAttr(i, Attribute::ZExt);
```

This applies to params and return types where appropriate.

## Runtime Helper Surface

`code/chapter18/runtime.c` now exports typed print/char helpers for scalar widths.

Examples:

```c
DLLEXPORT int8_t printi8(int8_t X) { fprintf(stderr, "%d", (int)X); return 0; }
DLLEXPORT uint64_t printu64(uint64_t X) { fprintf(stderr, "%llu", (unsigned long long)X); return 0; }
DLLEXPORT double printfloat64(double X) { fprintf(stderr, "%f", X); return 0; }
DLLEXPORT double printchard(double X) { fputc((unsigned char)X, stderr); return 0; }
```

This runtime surface is used by language features and tests in later chapters.

## Diagnostics Behavior

To reduce cascading noise in file mode, Chapter 18 adds a stop-after-first-semantic-error gate:

- `HadError` is set in `LogError(...)`
- file processing loops stop once it is set

This keeps negative test output focused on primary failures.

## Tests for This Chapter

Chapter 18 test coverage lives under `code/chapter18/test/`.

Core areas:

- typed aliases and target aliasing (`size_t`)
- address-of and pointer indexing
- invalid pointer index type
- `ptr[void]` indexing rejection
- runtime print helper matrix (`runtime_print_all_types.pyxc`)
- narrow signed extern behavior (`i8/i16` negative-value paths)

Representative snippets from tests:

```py
# pointer invalid index type
n: f64 = 2.5
x = p[n]   # error
```

```py
# narrow signed ABI path
x: i8 = -5
y: i16 = -300
print(x, y)
```

## Summary

Chapter 18 turns Pyxc from a mostly-untyped toy frontend into an interop-capable typed frontend:

- explicit scalar/pointer typing
- typed signatures and declarations
- alias resolution with target awareness
- pointer semantics with type checks
- ABI-correct narrow integer extern behavior

With this foundation in place, Chapter 19 can add language-level printing ergonomics (`print(...)`) without re-solving basic type and ABI correctness.

## Build and Test

Now compile Chapter 18 and run the test suite.

```bash
cd code/chapter18 && ./build.sh
llvm-lit test -sv
```

You can also run individual test programs directly:

```bash
./pyxc -i test/type_alias_core.pyxc
./pyxc -i test/pointer_addr_basic.pyxc
./pyxc -i test/runtime_print_all_types.pyxc
```

### Please Write Your Own Tests

The best way to check your understanding is to add tests yourself.

Try creating a few new `.pyxc` files under `code/chapter18/test/` that cover:

- alias chains and alias-cycle errors
- pointer indexing with integer and non-integer indices
- `void` return correctness
- extern calls with narrow signed/unsigned integer parameters

If your mental model matches the compiler behavior, your tests should pass on first try. If not, those mismatches are exactly what this chapter is meant to surface.

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter18 && ./build.sh
```

Run one sample program:

```bash
code/chapter18/pyxc -i code/chapter18/test/c_alias_size_t_target.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter18/test
lit -sv .
```

Have some fun stress-testing the suite with small variations.

When you're done, clean artifacts:

```bash
cd code/chapter18 && ./build.sh
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

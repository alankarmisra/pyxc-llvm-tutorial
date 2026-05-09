---
description: "Add array literals [e1, e2, ..., eN] so arrays can be initialized inline instead of element by element."
---
# 20. pyxc: Array Literals

## Where We Are

[Chapter 19](chapter-19.md) added fixed-size arrays — `int[4]`, `Point[10]`, indexing, decay — but you had to populate them one slot at a time:

```python
var a: int[3]
a[0] = 1
a[1] = 2
a[2] = 3
```

This chapter adds the obvious shortcut:

```python
var a: int[3] = [1, 2, 3]
```

That's it. One new syntax form, one new AST node, one new codegen path. Everything else in this chapter is plumbing to make the context available in every place a literal can legally appear.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-20
```

## Grammar

```ebnf
arrayliteral ::= '[' [ expression { ',' expression } ] ']'

primary ::= ... | arrayliteral | ...
```

An array literal is just a bracketed, comma-separated list of expressions. It can appear anywhere a primary expression can — including function arguments, return values, and the right-hand side of a `var` binding. The catch: it requires context. Without a declared array type to look at, the parser has no idea what element type to expect or how many elements are required, and rejects the literal immediately.

## The Context Problem

Most literals are self-describing. `42` is an integer. `3.14` is a float. `[1, 2, 3]` is… what? An array of three ints? Three floats? The parser cannot answer that without external information.

The solution is an ambient expected-type mechanism that already existed for numeric literals. Since chapter 16, `ExpectedLiteralType` carries the type the surrounding context expects. Chapter 18 extended it with `ExpectedLiteralStructName` to carry struct and pointer metadata. This chapter extends it again to carry array metadata:

```cpp
static ValueType ExpectedLiteralType = ValueType::Error;
static string ExpectedLiteralStructName;
```

When the parser enters a context with a known target type — `var` init, return, assignment, function argument — it pushes the expected type onto a guard:

```cpp
struct ExpectedLiteralTypeGuard {
  ValueType Saved;
  string SavedStruct;
  ExpectedLiteralTypeGuard(ValueType Type, const string &StructName = "")
      : Saved(ExpectedLiteralType), SavedStruct(ExpectedLiteralStructName) {
    ExpectedLiteralType = Type;
    ExpectedLiteralStructName = StructName;
  }
  ~ExpectedLiteralTypeGuard() {
    ExpectedLiteralType = Saved;
    ExpectedLiteralStructName = SavedStruct;
  }
};
```

The guard is RAII — it restores the previous state when it goes out of scope, so nested contexts work correctly. Chapter 20 updates every `ExpectedLiteralTypeGuard` call site to also pass the struct name (which carries the array encoding), and updates `ReturnTypeGuard` the same way.

## Parsing `[e1, e2, ..., eN]`

`ParseArrayLiteralExpr` is called when the primary parser sees `[`:

```cpp
static unique_ptr<ExprAST> ParseArrayLiteralExpr() {
  if (ExpectedLiteralType != ValueType::Array)
    return LogError("Array literal requires an expected array type");

  ValueType ElemType; string ElemStructName; uint64_t Count;
  DecodeArrayType(ExpectedLiteralStructName, ElemType, ElemStructName, Count);

  getNextToken(); // eat '['
  vector<unique_ptr<ExprAST>> Elements;
  while (CurTok != ']') {
    ExpectedLiteralTypeGuard Guard(ElemType, ElemStructName);
    auto Elem = ParseExpression();
    if (!IsAssignable(ElemType, Elem->getType()))
      return LogError("Array literal element type mismatch");
    Elements.push_back(std::move(Elem));
    if (CurTok == ']') break;
    // expect ','
    getNextToken(); // eat ','
  }
  getNextToken(); // eat ']'
  if (Elements.size() != Count)
    return LogError("Array literal element count does not match array size");
  return make_unique<ArrayLiteralExprAST>(std::move(Elements),
                                          ExpectedLiteralStructName);
}
```

The element count is checked against the declared array size *after* parsing all elements, not before. This gives you a cleaner error: you find out the count is wrong after seeing all the elements, not after the first token.

Each element is parsed with its own guard set to `ElemType` — so if the elements are themselves typed (say, struct values in a future chapter), the context propagates correctly.

The resulting `ArrayLiteralExprAST` records both the elements and the full array type encoding:

```cpp
class ArrayLiteralExprAST : public ExprAST {
  vector<unique_ptr<ExprAST>> Elements;
public:
  ArrayLiteralExprAST(vector<unique_ptr<ExprAST>> Elements,
                      const string &ArrayTypeInfo)
      : Elements(std::move(Elements)) {
    setType(ValueType::Array, ArrayTypeInfo);
  }
};
```

## Codegen: `InsertValue`

LLVM does not have a "build array" instruction. Instead, you start with an undefined value of the array type and insert elements one at a time:

```cpp
Value *ArrayLiteralExprAST::codegen() {
  ValueType ElemType; string ElemStructName; uint64_t Count;
  DecodeArrayType(getStructName(), ElemType, ElemStructName, Count);

  llvm::Type *ArrTy = LLVMTypeFor(ValueType::Array, getStructName());
  Value *Agg = UndefValue::get(ArrTy);

  for (size_t I = 0; I < Elements.size(); ++I) {
    Value *ElemVal = Elements[I]->codegen();
    ElemVal = EmitImplicitCast(ElemVal, Elements[I]->getType(), ElemType);
    Agg = Builder->CreateInsertValue(Agg, ElemVal, {(unsigned)I}, "arr.ins");
  }
  return Agg;
}
```

`UndefValue::get([3 x i64])` produces an aggregate of unspecified bits. `insertvalue` slots a value into one index of that aggregate without touching the others. After three inserts, you have a fully defined `[3 x i64]` value that gets stored into the alloca or global.

For `var a: int[3] = [1, 2, 3]`, the IR looks like:

```llvm
%a = alloca [3 x i64]
%arr.ins0 = insertvalue [3 x i64] undef,  i64 1, 0
%arr.ins1 = insertvalue [3 x i64] %arr.ins0, i64 2, 1
%arr.ins2 = insertvalue [3 x i64] %arr.ins1, i64 3, 2
store [3 x i64] %arr.ins2, ptr %a
```

When all elements are constants (the common case), LLVM's constant folder collapses the chain into a single `ConstantDataArray` before emitting anything. The `insertvalue` sequence only appears in the IR when elements are non-constant expressions.

## The `ExactArrayInit` Exception

`IsAssignable` now returns `false` whenever either side is an `Array`:

```cpp
static bool IsAssignable(ValueType Dest, ValueType Src) {
  if (Dest == ValueType::Array || Src == ValueType::Array)
    return false;
  if (Dest == Src)
    return true;
  // ...
}
```

Arrays have no copy semantics. `var b: int[3] = a` is a type error. `a = [1, 2, 3]` to an already-declared array is also a type error.

But `var a: int[3] = [1, 2, 3]` must work — and it would be caught by `IsAssignable` too, since both sides are `Array`. So `ParseVarStmt` checks for an exact literal initializer and bypasses the `IsAssignable` check:

```cpp
bool ExactArrayInit =
    (DeclType == ValueType::Array &&
     Init->getType() == ValueType::Array &&
     DeclStructName == Init->getStructName() &&
     dynamic_cast<ArrayLiteralExprAST *>(Init.get()) != nullptr);
if (!ExactArrayInit && !IsAssignable(DeclType, Init->getType()))
  return LogError("Type mismatch in variable initialization");
```

All four conditions must hold: declared type is array, init is an array, they have the same encoding (same element type and same count), and the init is specifically an `ArrayLiteralExprAST` — not some other array-typed expression. The last check is the important one: it blocks `var b: int[3] = a` (where `a` is another array variable) while allowing `var b: int[3] = [1, 2, 3]`.

## Context at Every Call Site

For a literal to work as a function argument, the argument parser has to set up the expected type from the prototype:

```cpp
string ExpectedStructName;
Expected = Proto->getArgType(ArgIndex);
ExpectedStructName = Proto->getArgStructName(ArgIndex);
// ...
ExpectedLiteralTypeGuard Guard(Expected, ExpectedStructName);
auto ArgExpr = ParseExpression();
```

If `Expected` is `Array`, the guard fires and `ParseArrayLiteralExpr` gets the array encoding from `ExpectedStructName`. Same mechanism handles return statements, index-element assignments, and indexed-field assignments — every place the struct name guard needed updating got it.

## Build and Run

```bash
cd code/chapter-20
cmake -S . -B build && cmake --build build
```

## Try It

### Initialize inline

```python
extern def printd(x: float64)

def main() -> int:
  var a: int[4] = [10, 20, 30, 40]
  printd(float64(a[2]))
  return 0
```

```
30.000000
```

### Pass a literal to a function

The array is initialized from the literal first, then decays to a pointer at the call site:

```python
extern def printd(x: float64)

def first(p: ptr[int]) -> int:
  return p[0]

def main() -> int:
  var a: int[3] = [7, 8, 9]
  printd(float64(first(a)))
  return 0
```

```
7.000000
```

### Global array with literal

```python
extern def printd(x: float64)

var g: int[3] = [4, 5, 6]

def main() -> int:
  printd(float64(g[1]))
  return 0
```

```
5.000000
```

The global is initialized by the ctor function at startup, same as all other global state.

### Non-constant elements

Elements can be arbitrary expressions:

```python
extern def printd(x: float64)

def main() -> int:
  var a: int[3] = [1 + 2, 3 * 2, 7]
  printd(float64(a[0] + a[1] + a[2]))
  return 0
```

```
16.000000
```

## Known Limitations

**Context required.** A bare `[1, 2, 3]` with no surrounding type annotation is a parse error. The parser cannot infer element type or count from the literal alone.

**No literal-to-literal assignment.** `a = [1, 2, 3]` where `a` is a declared array is a type error. The literal form is only allowed as the initializer of a `var` binding.

**No struct element literals.** `var pts: Point[2] = [{x=1,y=2}, {x=3,y=4}]` is not possible — pyxc has no struct initializer syntax yet.

**Element type must match exactly.** Implicit widening between element types is applied, but mixing unrelated types (`int` and `float64` in the same literal) is an error.

## What's Next

[Chapter 21](chapter-21.md) adds string literals: `"hello"` as a `ptr[int8]` value, escape sequences, and the ability to call C functions like `printf` directly from Pyxc.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

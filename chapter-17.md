# 17. A Real print(...) Builtin (Without Variadics)

Chapter 16 gave us typed values, typed locals, pointer syntax, and ABI-correct extern signatures.

In Chapter 17, we use that typed foundation to add something user-facing and practical: a language-level `print(...)` builtin.

The important constraint for this chapter is: **no general variadic functions yet**.
`print(...)` is implemented as a compiler builtin statement, not as user-declared `extern def foo(...)` machinery.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter17](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter17).

## What Changed from Chapter 16

At a high level, Chapter 17 adds four things:

- a new `print` keyword/token
- parser support for `print(...)` as a statement
- a dedicated `PrintStmtAST` lowering path
- a new lit suite under `code/chapter17/test/`

There is also one subtle type-system plumbing update: we carry extra signed/unsigned type hints so codegen can choose `printi32` vs `printu32` reliably.

## Language Surface in This Chapter

Supported forms:

```py
print()
print(x)
print(x, y, z)
```

Default formatting behavior:

- separator between args: a single space (`" "`)
- end of statement: newline (`"\n"`)

Supported argument families in this implementation:

- signed ints: `i8/i16/i32/i64`
- unsigned ints: `u8/u16/u32/u64`
- floats: `f32/f64`

Current policy for pointers: rejected with a diagnostic.

## Grammar (EBNF) Update

Compared to Chapter 16, `statement` gained a `print_stmt` branch.

```ebnf
statement       = if_stmt
                | for_stmt
                | print_stmt
                | return_stmt
                | typed_assign_stmt
                | assign_stmt
                | expr_stmt ;

print_stmt      = "print" , "(" , [ arg_list ] , ")" ;
```

This keeps `print` explicit in the grammar and avoids overloading generic call parsing for statement semantics.

## Tests First: New Chapter 17 lit Suite

Before implementation, we added a dedicated test harness and print-focused coverage.

- `code/chapter17/test/lit.cfg.py`
- `code/chapter17/test/print_basic_empty.pyxc`
- `code/chapter17/test/print_basic_scalars.pyxc`
- `code/chapter17/test/print_mixed_types.pyxc`
- `code/chapter17/test/print_narrow_signed.pyxc`
- `code/chapter17/test/print_unsigned_widths.pyxc`
- `code/chapter17/test/print_error_unsupported_pointer.pyxc`
- `code/chapter17/test/print_error_keyword_args.pyxc`
- `code/chapter17/test/print_error_trailing_comma.pyxc`

Example positive test:

```py
# RUN: %pyxc -i %s > %t 2>&1
# RUN: grep -x -- '-4 42 3.500000 7.250000' %t
# RUN: ! grep -q "Error (Line:" %t

def main() -> i32:
    si: i32 = -4
    ui: u64 = 42
    f1: f32 = 3.5
    f2: f64 = 7.25
    print(si, ui, f1, f2)
    return 0

main()
```

Example negative test:

```py
# RUN: %pyxc -i %s > %t 2>&1
# RUN: grep -q "Error (Line:" %t

def main() -> i32:
    x: i32 = 7
    p: ptr[i32] = addr(x)
    print(p)
    return 0

main()
```

## Lexer Changes

`print` becomes a real token in Chapter 17.

```cpp
enum Token {
  // ...
  tok_var = -15,
  tok_print = -27,
  // ...
};
```

Keyword table update:

```cpp
{"and", tok_and}, {"print", tok_print}, {"or", tok_or}
```

Token debug name support was also added:

```cpp
case tok_print:
  return "<print>";
```

This is small, but it keeps the parser branch clean and diagnostics readable.

## AST Changes: A Dedicated PrintStmtAST

Instead of pretending `print` is a normal function call expression, Chapter 17 adds an explicit statement node:

```cpp
class PrintStmtAST : public StmtAST {
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  PrintStmtAST(SourceLocation Loc, std::vector<std::unique_ptr<ExprAST>> Args)
      : StmtAST(Loc), Args(std::move(Args)) {}

  Value *codegen() override;
};
```

This keeps the semantics straightforward:

- parse as statement
- lower with statement behavior
- return an ignored sentinel in IR plumbing (`0.0`), same style as other statement nodes

## Parser Changes

We added a `ParsePrintStmt()` function and wired it into `ParseStmt()`.

Core parse shape:

```cpp
static std::unique_ptr<StmtAST> ParsePrintStmt() {
  auto PrintLoc = CurLoc;
  getNextToken(); // eat `print`
  if (CurTok != '(')
    return LogError<StmtPtr>("Expected '(' after print");
  getNextToken(); // eat '('

  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      auto Arg = ParseExpression();
      if (!Arg)
        return nullptr;
      Args.push_back(std::move(Arg));

      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogError<StmtPtr>("Expected ')' or ',' in print argument list");
      getNextToken();
      if (CurTok == ')')
        return LogError<StmtPtr>("Trailing comma is not allowed in print");
    }
  }

  getNextToken(); // eat ')'
  return std::make_unique<PrintStmtAST>(PrintLoc, std::move(Args));
}
```

And dispatch in statement parsing:

```cpp
case tok_print:
  return ParsePrintStmt();
```

## Why Extra Type Hints Were Needed

In LLVM IR, both `i32` and `u32` are just `i32` values. Width alone does not preserve signedness intent.

For print helper selection, that matters:

- signed 32-bit should call `printi32`
- unsigned 32-bit should call `printu32`

So Chapter 17 extends expression/type hint plumbing and variable bindings with leaf-type metadata.

### New expression hooks

```cpp
virtual std::string getBuiltinLeafTypeHint() const { return ""; }
virtual std::string getPointeeBuiltinLeafTypeHint() const { return ""; }
```

### Extended variable binding

```cpp
struct VarBinding {
  AllocaInst *Alloca = nullptr;
  Type *Ty = nullptr;
  Type *PointeeTy = nullptr;
  std::string BuiltinLeafTy;
  std::string PointeeBuiltinLeafTy;
};
```

### Typed assignment now stores these hints

```cpp
NamedValues[Name] = {Alloca, DeclTy, ResolvePointeeTypeExpr(DeclType),
                     ResolveBuiltinLeafName(DeclType),
                     ResolvePointeeBuiltinLeafName(DeclType)};
```

This is what lets print codegen preserve signed/unsigned helper choice for typed values and pointer-indexed values.

## Print Codegen: Helper Dispatch Strategy

The codegen path does not use user-level variadics. It does this instead:

1. codegen each argument expression
2. infer helper symbol by LLVM type + leaf hint
3. emit `call print*` for the arg
4. emit separator (`printchard(32)`) between args
5. emit newline (`printchard(10)`) at end

### Helper resolution

```cpp
static Function *GetPrintHelperForArg(Type *ArgTy, const std::string &LeafHint) {
  if (ArgTy->isFloatTy())
    return GetOrCreatePrintHelper("printfloat32", ArgTy, false);
  if (ArgTy->isDoubleTy())
    return GetOrCreatePrintHelper("printfloat64", ArgTy, false);
  if (!ArgTy->isIntegerTy())
    return nullptr;

  bool IsUnsigned = !LeafHint.empty() && LeafHint[0] == 'u';
  unsigned W = ArgTy->getIntegerBitWidth();
  switch (W) {
  case 8:  return GetOrCreatePrintHelper(IsUnsigned ? "printu8"  : "printi8",  ArgTy, IsUnsigned);
  case 16: return GetOrCreatePrintHelper(IsUnsigned ? "printu16" : "printi16", ArgTy, IsUnsigned);
  case 32: return GetOrCreatePrintHelper(IsUnsigned ? "printu32" : "printi32", ArgTy, IsUnsigned);
  case 64: return GetOrCreatePrintHelper(IsUnsigned ? "printu64" : "printi64", ArgTy, IsUnsigned);
  default: return nullptr;
  }
}
```

### Statement lowering loop

```cpp
for (size_t I = 0; I < Args.size(); ++I) {
  Value *ArgV = Args[I]->codegen();
  Type *ArgTy = ArgV->getType();

  if (ArgTy->isPointerTy())
    return LogError<Value *>("Unsupported print argument type: pointer");

  Function *PrintF = GetPrintHelperForArg(ArgTy, Args[I]->getBuiltinLeafTypeHint());
  if (!PrintF)
    return LogError<Value *>("Unsupported print argument type");

  Value *CastArg = CastValueTo(ArgV, PrintF->getFunctionType()->getParamType(0));
  Builder->CreateCall(PrintF, {CastArg});

  if (I + 1 < Args.size())
    Builder->CreateCall(PrintCharF, {ConstantFP::get(*TheContext, APFloat(32.0))});
}

Builder->CreateCall(PrintCharF, {ConstantFP::get(*TheContext, APFloat(10.0))});
```

## Diagnostics in This Chapter

The print implementation now reports meaningful errors for:

- malformed syntax (for example, bad separator usage via unsupported keyword syntax)
- trailing comma in this MVP parser path
- unsupported argument kinds (notably pointers)

Representative messages include:

- `Expected ')' or ',' in print argument list`
- `Trailing comma is not allowed in print`
- `Unsupported print argument type: pointer`
- `Unsupported print argument type`

## Chapter 17 Test Outcomes

The new chapter17 suite validates:

- `print()` newline-only behavior
- basic scalar spacing/newline behavior
- mixed signed/unsigned/float dispatch
- narrow signed integer paths (`i8`, `i16`)
- wide unsigned paths (`u32`, `u64`)
- key negative behavior (pointer args, unsupported keyword usage, trailing comma)

Status after implementation: all Chapter 17 lit tests pass.

## Recap

Chapter 17 intentionally avoids general variadics while still delivering ergonomic output.

The key design choices were:

- treat `print` as a language builtin statement
- lower each argument with type-directed helper dispatch
- preserve signedness intent with lightweight leaf-type hints
- lock behavior with a dedicated lit suite before codegen work

This keeps the compiler architecture clean and gives us a practical builtin today, while leaving room to add true user-defined variadics later.

## Repository Link

You can browse the full project directly here:

- [pyxc-llvm-tutorial on GitHub](https://github.com/alankarmisra/pyxc-llvm-tutorial)

## Build and Test (Chapter 17)

From the repository root:

```bash
cd code/chapter17
make
```

Run the chapter test suite:

```bash
llvm-lit -sv test
```

If you want to sanity-check compatibility with the previous chapter as well:

```bash
cd ../chapter16
make
llvm-lit -sv test
```

Try writing a few of your own `print` tests in `code/chapter17/test/` too. A good way to check your understanding is to add both:

- positive cases (mixed types, nested expressions, computed values)
- negative cases (unsupported types, malformed argument lists)

When your own tests pass and fail exactly where you expect, the implementation model usually clicks.


## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
make -C code/chapter17 clean all
```

Run one sample program:

```bash
code/chapter17/pyxc -i code/chapter17/test/print_basic_empty.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter17/test
lit -sv .
```

Poke around the tests and tweak a few cases to see what breaks first.

When you're done, clean artifacts:

```bash
make -C code/chapter17 clean
```

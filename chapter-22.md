# 22. C-style I/O Baseline (putchar, getchar, puts, minimal printf)

Chapter 21 gave us dynamic heap memory with `malloc` and `free`.

That was important, but it still left us in a place where writing small, practical programs felt awkward, because we had no direct string literals and no C-style I/O entry points.

In this chapter we focus on exactly that gap.

The goal is not to implement all of C stdio. The goal is to establish a *clean baseline* that unlocks many K&R-style programs quickly:

- string literals
- `putchar`
- `getchar`
- `puts`
- a minimal, well-defined `printf`

We intentionally keep this chapter narrow so behavior stays predictable and testable.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter22](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter22).

## Why this chapter matters

Without this chapter, even simple tasks like printing formatted text need custom language helpers.

With this chapter, we can directly write:

```py
msg: ptr[i8] = "hello"
puts(msg)
printf("x=%d\n", 42)
```

That one improvement changes the feel of the language from “compiler exercise” to “actually usable for small systems-style experiments”.

## Scope and constraints

What we support in Chapter 22:

- String literals as first-class expressions.
- Auto-declared libc calls for:
  - `putchar(i32) -> i32`
  - `getchar() -> i32`
  - `puts(ptr[i8]) -> i32`
  - `printf(ptr[i8], ...) -> i32`
- Vararg call lowering required for `printf`.
- Strict format subset for `printf`:
  - `%d`, `%s`, `%c`, `%p`, `%%`

What we explicitly do *not* support yet:

- Full `printf` flags/width/precision/length modifiers.
- `%f` formatting.
- User-defined variadic prototypes in source syntax.

This is a deliberate “minimum viable stdio” milestone.

## What changed from Chapter 21

The key file diffs are:

- `code/chapter21/pyxc.cpp` -> `code/chapter22/pyxc.cpp`
- `code/chapter21/pyxc.ebnf` -> `code/chapter22/pyxc.ebnf`

Conceptually, changes fall into five buckets:

1. lexer support for string literals
2. parser + AST support for string expressions
3. codegen for string literals
4. libc function resolution for stdio symbols
5. vararg call support + `printf` semantic checks

We will walk through each bucket in order.

## Grammar and language surface

### EBNF updates

In `code/chapter22/pyxc.ebnf`, we add `string_literal` and allow it as a primary expression:

```ebnf
string_literal = "\"" , { ? any char except " or newline; escapes allowed ? } , "\"" ;

primary         = number
                | string_literal
                | identifier
                | malloc_expr
                | addr_expr
                | paren_expr
                | var_expr ;
```

This is a small grammar change, but it has major downstream effects because literals can now participate in assignment and function calls naturally.

### Resulting source syntax

```py
s: ptr[i8] = "text"
puts(s)
printf("v=%d\n", 7)
```

## Lexer changes: tokenizing string literals safely

### New token and lexer storage

In `code/chapter22/pyxc.cpp` we add:

```cpp
tok_string = -35,
```

and:

```cpp
static std::string StringVal;     // Filled in if tok_string
```

This matches the existing `NumVal`/`IdentifierStr` pattern.

### String scanning path in gettok()

The lexer adds a new branch when it sees `"`:

```cpp
if (LastChar == '"') {
  StringVal.clear();
  while (true) {
    LastChar = advance();
    if (LastChar == EOF || LastChar == '\n' || LastChar == '\r') {
      LogError("Unterminated string literal");
      return tok_error;
    }
    if (LastChar == '"') {
      LastChar = advance();
      return tok_string;
    }
    if (LastChar == '\\') {
      LastChar = advance();
      switch (LastChar) {
      case 'n': StringVal.push_back('\n'); break;
      case 't': StringVal.push_back('\t'); break;
      case 'r': StringVal.push_back('\r'); break;
      case '0': StringVal.push_back('\0'); break;
      case '\\': StringVal.push_back('\\'); break;
      case '"': StringVal.push_back('"'); break;
      default:
        LogError("Invalid escape sequence in string literal");
        return tok_error;
      }
    } else {
      StringVal.push_back(static_cast<char>(LastChar));
    }
  }
}
```

### Why this design

- We unescape during lexing so parser/AST see normalized data.
- We reject malformed strings early with precise diagnostics.
- We allow common escapes only. This keeps behavior explicit and avoids silent surprises.

### Token-name plumbing

`TokenName(...)` also gets:

```cpp
case tok_string:
  return "<string>";
```

This improves debug token dumps and parser debugging.

## AST and parser: representing string literals explicitly

### New AST node: StringExprAST

In `code/chapter22/pyxc.cpp`, Chapter 22 adds:

```cpp
class StringExprAST : public ExprAST {
  std::string Val;

public:
  StringExprAST(SourceLocation Loc, std::string Val)
      : ExprAST(Loc), Val(std::move(Val)) {}
  const std::string &getValue() const { return Val; }
  Value *codegen() override;
  Type *getValueTypeHint() const override;
  Type *getPointeeTypeHint() const override;
  std::string getPointeeBuiltinLeafTypeHint() const override;
  bool getStringLiteralValue(std::string &Out) const override {
    Out = Val;
    return true;
  }
};
```

Two important details here:

1. It is a dedicated node, not a special case in calls.
2. It overrides `getStringLiteralValue(...)`, which we use later for `printf` validation *without RTTI*.

### Why the virtual helper (getStringLiteralValue) matters

This codebase builds with `-fno-rtti`. So using `dynamic_cast` for “is this node a string literal?” is not valid.

Instead, the base `ExprAST` provides:

```cpp
virtual bool getStringLiteralValue(std::string &Out) const { return false; }
```

and `StringExprAST` overrides it to return true with payload.

That gives us safe, low-overhead type query behavior compatible with current compile flags.

### Parser entry point: ParseStringExpr()

```cpp
static std::unique_ptr<ExprAST> ParseStringExpr() {
  auto StrLoc = CurLoc;
  auto Result = std::make_unique<StringExprAST>(StrLoc, StringVal);
  getNextToken(); // consume string literal
  return std::move(Result);
}
```

Then `ParsePrimary()` dispatch adds:

```cpp
case tok_string:
  return ParseStringExpr();
```

This is the exact same parser architecture already used for numbers/identifiers, so complexity stays low.

## Codegen for string literals

`StringExprAST::codegen()` lowers a literal into a global string pointer:

```cpp
Value *StringExprAST::codegen() {
  emitLocation(this);
  return Builder->CreateGlobalStringPtr(Val, "strlit");
}
```

And type hints are:

```cpp
Type *StringExprAST::getValueTypeHint() const {
  return PointerType::get(*TheContext, 0);
}

Type *StringExprAST::getPointeeTypeHint() const {
  return Type::getInt8Ty(*TheContext);
}

std::string StringExprAST::getPointeeBuiltinLeafTypeHint() const {
  return "i8";
}
```

### Why this is enough for interop

Existing assignment/call casting logic already handles pointer-compatible values. So once string literals lower to pointer values and advertise pointee hints (`i8`), they naturally work with:

- variable assignment to `ptr[i8]`
- call arguments where pointer is expected (e.g., `puts`, `printf` format)

No separate “string type system” is introduced in this chapter.

## Auto-declaring libc stdio functions

Before Chapter 22, unresolved functions were looked up only in:

- module-defined functions
- previously seen prototypes

Chapter 22 adds a libc fallback for known stdio symbols.

### Helper: GetOrCreateLibcIOFunction

```cpp
static Function *GetOrCreateLibcIOFunction(const std::string &Name) {
  if (Function *F = TheModule->getFunction(Name))
    return F;

  Type *I32Ty = Type::getInt32Ty(*TheContext);
  Type *PtrTy = PointerType::get(*TheContext, 0);
  FunctionType *FT = nullptr;

  if (Name == "putchar")
    FT = FunctionType::get(I32Ty, {I32Ty}, false);
  else if (Name == "getchar")
    FT = FunctionType::get(I32Ty, {}, false);
  else if (Name == "puts")
    FT = FunctionType::get(I32Ty, {PtrTy}, false);
  else if (Name == "printf")
    FT = FunctionType::get(I32Ty, {PtrTy}, true);
  else
    return nullptr;

  return Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());
}
```

### Integration in getFunction

```cpp
if (Function *LibCF = GetOrCreateLibcIOFunction(Name))
  return LibCF;
```

### Why this design

- No new source syntax is required for these common calls.
- We stay explicit: only a tiny allowlist is auto-declared.
- We avoid surprising behavior from auto-creating arbitrary unresolved symbols.

## Vararg call lowering in CallExprAST::codegen()

This is the most important backend change in the chapter.

Before Chapter 22, call lowering assumed fixed-arity calls only:

- exact argument count
- formal-by-formal cast loop

That breaks for `printf`, because `printf` has one fixed arg (format string) and then variadic arguments.

### Updated arity checks

```cpp
size_t FixedArgCount = CalleeF->arg_size();
if ((!CalleeF->isVarArg() && FixedArgCount != Args.size()) ||
    (CalleeF->isVarArg() && Args.size() < FixedArgCount))
  return LogError<Value *>("Incorrect # arguments passed");
```

For varargs, we now require “at least fixed args”.

### Two-phase argument handling

We first codegen all arguments into `RawArgs`:

```cpp
std::vector<Value *> RawArgs;
RawArgs.reserve(Args.size());
for (auto &Arg : Args) {
  Value *ArgV = Arg->codegen();
  if (!ArgV)
    return nullptr;
  RawArgs.push_back(ArgV);
}
```

Then we do:

1. fixed-parameter casts to formal types
2. vararg-specific promotion for trailing args

### Default promotions for varargs

```cpp
while (I < RawArgs.size()) {
  Value *ArgV = RawArgs[I++];
  Type *ArgTy = ArgV->getType();
  if (ArgTy->isFloatTy()) {
    ArgV = Builder->CreateFPExt(ArgV, Type::getDoubleTy(*TheContext),
                                "vararg.fpext");
  } else if (ArgTy->isIntegerTy() && ArgTy->getIntegerBitWidth() < 32) {
    ArgV = Builder->CreateSExt(ArgV, Type::getInt32Ty(*TheContext),
                               "vararg.sext");
  }
  ArgsV.push_back(ArgV);
}
```

This implements the key ABI-relevant C default promotions for our use case.

## printf semantic validation (minimal subset)

If we only lower varargs and skip semantic checks, users get confusing runtime output or undefined behavior. So Chapter 22 adds compile-time checks specifically for `printf`.

### Rule 1: format must be a string literal

```cpp
std::string Fmt;
if (!Args[0]->getStringLiteralValue(Fmt))
  return LogError<Value *>("printf format must be a string literal");
```

We intentionally require a literal (not variable/expr) in this chapter so we can validate format shape statically.

### Rule 2: only %d, %s, %c, %p, %%

```cpp
for (size_t I = 0; I < Fmt.size(); ++I) {
  if (Fmt[I] != '%')
    continue;
  if (I + 1 >= Fmt.size())
    return LogError<Value *>("Unsupported printf format specifier '%'");
  char Spec = Fmt[++I];
  if (Spec == '%')
    continue;
  if (Spec != 'd' && Spec != 's' && Spec != 'c' && Spec != 'p')
    return LogError<Value *>("Unsupported printf format specifier");
  Specs.push_back(Spec);
}
```

### Rule 3: placeholder count must match arg count

```cpp
if (Specs.size() != Args.size() - 1)
  return LogError<Value *>("printf format/argument count mismatch");
```

### Rule 4: type check each placeholder

```cpp
for (size_t I = 0; I < Specs.size(); ++I) {
  Type *Ty = RawArgs[I + 1]->getType();
  char Spec = Specs[I];
  if ((Spec == 'd' || Spec == 'c') && !Ty->isIntegerTy())
    return LogError<Value *>("printf type mismatch for integer format");
  if ((Spec == 's' || Spec == 'p') && !Ty->isPointerTy())
    return LogError<Value *>("printf type mismatch for pointer format");
}
```

### Why this conservative approach is good here

- Users get actionable compile-time errors.
- Behavior is deterministic.
- We avoid half-implemented format parsing that would be hard to reason about.

We can loosen these constraints in a later chapter once we have richer type/format infrastructure.

## Chapter 22 test suite

Chapter 22 keeps all inherited Chapter 21 tests and adds dedicated I/O tests in `code/chapter22/test`.

### Positive tests

- `io_basic_putchar_puts.pyxc`
  - checks `putchar` + `puts`
- `io_printf_subset.pyxc`
  - checks `%d/%s/%c/%p/%%`
- `io_string_ptr_assign.pyxc`
  - checks literal -> `ptr[i8]` assignment
- `io_getchar_decl_only.pyxc`
  - checks symbol resolution and typing path for `getchar`

### Negative tests

- `io_error_printf_bad_spec.pyxc`
  - `%f` rejected
- `io_error_printf_count_mismatch.pyxc`
  - placeholder/arg mismatch rejected
- `io_error_printf_type_mismatch.pyxc`
  - `%d` with pointer rejected

These tests enforce that our strict subset is not just documented, but actually implemented.

## End-to-end behavior examples

### Example 1: direct calls

```py
def main() -> i32:
    putchar(65)
    putchar(32)
    puts("hi")
    return 0

main()
```

Output:

```text
A hi
```

### Example 2: minimal printf

```py
def main() -> i32:
    s: ptr[i8] = "ok"
    printf("value=%d char=%c ptr=%p text=%s percent=%%\n", 42, 66, s, s)
    return 0

main()
```

This exercises integer, char, pointer, string, and escaped percent paths together.

## Validation run for this chapter

Chapter 22 was validated with:

```bash
make -C code/chapter22 clean all
lit -sv code/chapter22/test
```

Result:

- 40 tests discovered
- 40 passed

## Design takeaways

### What we gained

- Practical text I/O capability with minimal language surface growth.
- String literals integrated into the existing expression/type pipeline.
- First variadic call path in backend codegen.
- Strong early diagnostics for common `printf` mistakes.

### What we intentionally postponed

- full C format grammar
- runtime formatting helpers
- user-defined variadics

This keeps Chapter 22 focused and stable while still unlocking a large amount of real program space.

## Where to go next

With Chapter 22 complete, the next high-leverage topics are:

- pointer dereference (`*`) and address-of operator parity with C syntax
- header/translation-unit workflow for separate compilation
- richer standard-library interop (`strlen`, `strcmp`, etc.)

Those build directly on the I/O and pointer groundwork laid here.

## Compiling

From repository root:

```bash
make -C code/chapter22 clean all
```

## Testing

From repository root:

```bash
lit -sv code/chapter22/test
```

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
make -C code/chapter22 clean all
```

Run one sample program:

```bash
code/chapter22/pyxc -i code/chapter22/test/array_alias_type.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter22/test
lit -sv .
```

Poke around the tests and tweak a few cases to see what breaks first.

When you're done, clean artifacts:

```bash
make -C code/chapter22 clean
```

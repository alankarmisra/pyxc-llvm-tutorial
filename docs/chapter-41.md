---
description: "Add variadic extern declarations so pyxc code can call C functions like printf and scanf that take a variable number of arguments."
---
# 41. pyxc: Variadic Extern Functions

## Where We Are

[Chapter 40](chapter-40.md) completed the K&R toolbox. pyxc can call C functions via `extern def`, but only functions with a fixed number of typed parameters. `printf`, `scanf`, `sprintf`, and most other C I/O functions take a variable number of arguments — the `...` in their C signatures. Trying to declare them currently produces an error:

```
Error: Expected parameter name in prototype
```

After this chapter, variadic `extern` declarations work:

```pyxc
type string = ptr[int8]
extern def printf(fmt: string, ...) -> int32

def main() -> int:
  printf("hello world\n")
  printf("answer: %ld\n", 42)
  return 0
```

```
hello world
answer: 42
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-41
```

## `IsVarArg` on `PrototypeAST`

The only structural change is a new `bool IsVarArg` field on `PrototypeAST`:

```cpp
class PrototypeAST {
  ...
  bool IsVarArg;
public:
  PrototypeAST(string Name, vector<ArgInfo> Args, SourceLocation Loc,
               ValueType ReturnType = ValueType::Float64,
               bool IsOperator = false, bool IsVarArg = false,
               unsigned Prec = 0, string ReturnStructName = "")
      : ..., IsVarArg(IsVarArg), ... {}

  bool isVarArg() const { return IsVarArg; }
};
```

`IsVarArg` defaults to `false`. All existing callers pass it implicitly or explicitly as `false`. Only the `extern def` path sets it to `true`.

## `ParsePrototype` — `AllowVarArgs` Parameter

`ParsePrototype` gains an `AllowVarArgs` parameter that defaults to `false`:

```cpp
static unique_ptr<PrototypeAST> ParsePrototype(bool AllowVarArgs = false) {
  ...
  bool IsVarArg = false;
  while (CurTok != ')') {
    // parse normal typed parameters...

    if (AllowVarArgs && CurTok == '.') {
      getNextToken();
      if (CurTok != '.')
        return LogErrorP("Expected '...' in variadic prototype");
      getNextToken();
      if (CurTok != '.')
        return LogErrorP("Expected '...' in variadic prototype");
      getNextToken();
      IsVarArg = true;
      if (CurTok != ')')
        return LogErrorP("Variadic marker must be last in parameter list");
      break;
    }
    ...
  }
  return make_unique<PrototypeAST>(FnName, std::move(ArgNames), ProtoLoc,
                                   ValueType::Float64, false, IsVarArg);
}
```

`...` is not a lexer token — the lexer returns three consecutive `.` characters. The parser consumes them one at a time. If any of the three dots is missing, `LogErrorP` fires immediately. `...` must be the last item before `)` — parameters after it are a parse error.

## Only `extern def` Passes `AllowVarArgs = true`

Regular `def` and `decorateddef` call `ParsePrototype()` with the default `false`. Only `ParseExtern` passes `true`:

```cpp
// ParseExtern:
auto Proto = ParsePrototype(true);  // variadic allowed here
```

All other `ParsePrototype` call sites pass the default (`false`) or explicitly pass `/*IsVarArg=*/false`:

```cpp
// ParseDefinition, ParseBinaryDef, ParseUnaryDef:
return make_unique<PrototypeAST>(..., /*IsVarArg=*/false, Precedence);
```

Attempting `...` in a user-defined function body fails because `AllowVarArgs` is `false` and the parser sees the first `.` as an unexpected token.

## Arity Check Updated at Call Sites

The call-site arity check was previously an exact match. For variadic functions it becomes "at least the fixed count":

```cpp
// In ParseCallArgs / call codegen:
if ((!Proto->isVarArg() && Proto->getNumArgs() != Args.size()) ||
    (Proto->isVarArg() && Args.size() < Proto->getNumArgs()))
  return LogErrorExpression("Incorrect # arguments passed");
```

Type-checking the arguments only iterates over the fixed parameters:

```cpp
for (size_t i = 0; i < Args.size() && i < Proto->getNumArgs(); ++i) {
  // check Args[i] against Proto->getArgType(i)
}
```

Arguments beyond the fixed count are passed through as-is. Their types are whatever the caller provides. The same guard is applied in `CallExprAST::codegen`:

```cpp
if ((!CalleeF->isVarArg() && CalleeF->arg_size() != Args.size()) ||
    (CalleeF->isVarArg() && Args.size() < CalleeF->arg_size()))
  return LogErrorV("Incorrect # arguments passed");
```

## Codegen — `FunctionType::get` with `IsVarArg`

The function type construction in `PrototypeAST::codegen` passes `IsVarArg` to `FunctionType::get` instead of the previous hardcoded `false`:

```cpp
FunctionType *FT = FunctionType::get(
    LLVMTypeFor(ReturnType, ReturnStructName), ArgTys, IsVarArg);
```

LLVM emits the IR declaration with `...`:

```
declare i32 @printf(ptr, ...)
```

At the call site, LLVM handles the variadic ABI automatically. Arguments past the declared fixed parameters are passed using the platform's default variadic calling convention.

## Grammar

```ebnf
external        = "extern" "def" externprototype [ "->" type ] ;  -- changed
externprototype = identifier "(" [ typedparam { "," typedparam } [ "," "..." ] | "..." ] ")" ; -- new
```

Regular `prototype` (used by `def`) is unchanged — `...` is not valid there.

## Error Cases

**Incomplete `...`:**
```pyxc
extern def bad(fmt: ptr[int8], ..) -> int32
# Error: Expected '...' in variadic prototype
```

**Too few fixed arguments:**
```pyxc
extern def printf(fmt: ptr[int8], ...) -> int32
printf()   # Error: Incorrect # arguments passed
```

**`...` in a user-defined function:**
```pyxc
def bad(x: int, ...) -> int:  # Error: Expected parameter name in prototype
  return x
```

## Things Worth Knowing

**`%ld` not `%d` for pyxc's `int`.** pyxc's `int` is 64-bit. `printf`'s `%d` expects a 32-bit `int`. Use `%ld` or `%lld`; use `%d` only when you have explicitly cast to `int32`.

**Variadic arguments are not type-checked.** Fixed parameters are checked at compile time. Anything past `...` is your responsibility.

**You cannot implement a variadic function in pyxc.** `...` is only valid in `extern def`. There is no `va_list`, `va_start`, or `va_arg` in pyxc. If you need a variadic-style interface, write an overloaded wrapper or accept a fixed-size array.

**Pure `...` with no fixed parameters is valid syntax.** `extern def f(...)` compiles and generates `declare ... @f(...)`. However, any real C function declared this way would have ABI issues because `va_start` needs at least one named parameter.

## What's Next

Phase 5 is complete. pyxc now has the full K&R toolbox: signed and unsigned integer types, character literals, the complete set of operators, `if`/`elif`/`else`, `switch`, `while`, `do/while`, `break`, `continue`, assignment as expression, and direct C library interop via `extern` (including variadic functions). [Chapter 42](chapter-42.md) begins Phase 6: module declarations and imports, giving pyxc programs a way to split across multiple files.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

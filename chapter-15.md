# 15. Clean Operator Rules and Short-Circuit Logic

In Chapter 14, we had custom operators and decorator syntax.

For Chapter 15, we simplify the language before adding types:
- add Python-like logical/comparison operators
- remove custom operator definitions for now
- add real short-circuit behavior for `and` and `or`

This keeps the codebase easier to reason about before Chapter 16 (types).


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter15](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter15).

## What We Added

- logical keywords: `not`, `and`, `or`
- comparison operators: `==`, `!=`, `<=`, `>=`
- short-circuit codegen for `and` and `or`

## What We Removed (for now)

- `@unary` / `@binary` custom operator definitions

## Why Disable Custom Operators Before Types?

Simple reason: typed custom operators are ambiguous without more language features.

If we keep custom operators while introducing types, we must answer:
- how many typed versions of the same operator are allowed?
- how does the compiler pick the correct one?

That requires one of:
- overloading (same operator name, many signatures)
- generics (one generic operator that works for a type family)

We do not have overloading or generics yet.

So Chapter 15 disables custom operators on purpose. We will bring them back later, after overloading/generics exist.

## Grammar Target (Chapter 15)

From [`code/chapter15/pyxc.ebnf`](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter15/pyxc.ebnf):

```ebnf
function_def   = "def" , prototype , ":" , suite ;

unary_op       = "+" | "-" | "!" | "~" | "not" ;

binary_op      = "<" | "<=" | ">" | ">="
               | "==" | "!="
               | "and" | "or"
               | "+" | "-" | "*" | "/"
               | "=" ;
```

## Lexer Changes

### New tokens

```cpp
tok_not = -18,
tok_and = -19,
tok_or = -20,
tok_eq = -21, // ==
tok_ne = -22, // !=
tok_le = -23, // <=
tok_ge = -24, // >=
```

Keyword table includes:

```cpp
{"not", tok_not}, {"and", tok_and}, {"or", tok_or}
```

### Multi-char operator lexing

Lexer now recognizes:
- `==`
- `!=`
- `<=`
- `>=`

instead of trying to parse them as two separate chars.

## Parser Changes

### Precedence table now uses token IDs

Because `tok_and`, `tok_eq`, etc are token IDs (not plain chars), precedence lookup moved to `std::map<int, int>`:

```cpp
static std::map<int, int> BinopPrecedence = {
    {'=', 2},       {tok_or, 5},   {tok_and, 6}, {tok_eq, 10}, {tok_ne, 10},
    {'<', 12},      {'>', 12},     {tok_le, 12}, {tok_ge, 12},
    {'+', 20},      {'-', 20},     {'*', 40},    {'/', 40}};
```

### Builtin-only unary parsing

```cpp
if (CurTok != '+' && CurTok != '-' && CurTok != '!' && CurTok != '~' &&
    CurTok != tok_not)
  return ParsePrimary();
```

### Decorator/custom-operator parse path removed

`ParseDefinition()` and `ParsePrototype()` now parse only normal `def` functions.

If the parser sees `@...`, it emits a direct Chapter 15 error:

```cpp
LogError("Decorators/custom operators are disabled in Chapter 15");
```

## Short-Circuit Logic: What It Means

Without short-circuit:
- `a and b` evaluates both `a` and `b`
- `a or b` evaluates both `a` and `b`

With short-circuit:
- `a and b`: if `a` is false, skip `b`
- `a or b`: if `a` is true, skip `b`

This matters for side effects and performance.

## LLVM IR Shape We Want

For `a and b`, we want control flow like this:

```llvm
entry:
  %a = ...
  %a_bool = fcmp one double %a, 0.0
  br i1 %a_bool, label %rhs, label %merge

rhs:
  %b = ...
  %b_bool = fcmp one double %b, 0.0
  br label %merge

merge:
  %res = phi i1 [ false, %entry ], [ %b_bool, %rhs ]
  %res_f = uitofp i1 %res to double
```

For `a or b`, branch directions and first PHI value flip:

```llvm
entry:
  %a = ...
  %a_bool = fcmp one double %a, 0.0
  br i1 %a_bool, label %merge, label %rhs

rhs:
  %b = ...
  %b_bool = fcmp one double %b, 0.0
  br label %merge

merge:
  %res = phi i1 [ true, %entry ], [ %b_bool, %rhs ]
  %res_f = uitofp i1 %res to double
```

That is exactly what we implemented in Chapter 15 codegen.

## Codegen Changes

### Unary

`not` and `!` now map to builtin boolean inversion (still returned as `0.0/1.0`):

```cpp
case '!':
case tok_not: {
  Value *AsBool = Builder->CreateFCmpONE(
      OperandV, ConstantFP::get(*TheContext, APFloat(0.0)), "nottmp.bool");
  Value *NegBool = Builder->CreateNot(AsBool, "nottmp.inv");
  return Builder->CreateUIToFP(NegBool, Type::getDoubleTy(*TheContext),
                               "nottmp");
}
```

### Binary

`and` / `or` now use branch-based short-circuit codegen with PHI merge.

We still keep Chapter 15 result style:
- boolean-like result as `double` (`0.0` or `1.0`)
- not Python operand-return semantics yet

## Tests in This Chapter

Operator-focused tests:
- [`code/chapter15/test/logic_ops.pyxc`](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter15/test/logic_ops.pyxc)
- [`code/chapter15/test/logic_short_circuit.pyxc`](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter15/test/logic_short_circuit.pyxc)
- [`code/chapter15/test/custom_ops_disabled.pyxc`](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter15/test/custom_ops_disabled.pyxc)

Type/pointer tests moved to Chapter 16.

## Compile and Run

```bash
cd code/chapter15
make
```

Run sample programs:

```bash
./pyxc -t test/logic_ops.pyxc
./pyxc -i test/logic_ops.pyxc
./pyxc -i test/logic_short_circuit.pyxc
./pyxc -i test/custom_ops_disabled.pyxc
```

Run Chapter 15 test suite:

```bash
llvm-lit test
```

Repo:

- [https://github.com/alankarmisra/pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial)


## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
make -C code/chapter15 clean all
```

Run one sample program:

```bash
code/chapter15/pyxc -i code/chapter15/test/custom_ops_disabled.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter15/test
lit -sv .
```

Pick a couple of tests, mutate the inputs, and watch how diagnostics respond.

When you're done, clean artifacts:

```bash
make -C code/chapter15 clean
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

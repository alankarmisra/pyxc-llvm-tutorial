---
description: "Add user-defined operators via Python-style decorators — @binary(N) and @unary — backed by dedicated parser functions with compile-time validation."
---
# 9. Pyxc: User-Defined Operators

## Where We Are

[Chapter 8](chapter-08.md) added comparison operators, `if`/`else`, and `for` loops, but every operator Pyxc knows is still hardwired into the compiler. This chapter adds user-defined operators — a detour into some interesting parsing techniques that pays off with a surprisingly clean syntax.

By the end, you'll be able to define new operators directly in Pyxc using Python-style decorators. The decorator line sets the type and precedence; the `def` line gives it a body:

<!-- code-merge:start -->
```python
ready> @binary(5) # an operator precedence of 5
def |(x, y): return if x != 0: 1 else: if y != 0: 1 else: 0
```
```bash
Parsed a user-defined operator.
```
```python
ready> 1 | 0
```
```bash
Parsed a top-level expression.
Evaluated to 1.000000
```
<!-- code-merge:end -->

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-09
```

## The Design

In Pyxc, user-defined operators are just functions with funny names. A user-defined binary operator `|` is stored as an ordinary function named `binary|` — LLVM knows nothing special about the name. A unary operator `!` is stored as `unary!`. When the parser encounters `a | b`, it looks up `binary|` and generates a call. Built-in operators like `+` and `*` are handled directly in `BinaryExprAST::codegen` with `CreateFAdd` and `CreateFMul` — no function call involved.

This means:
- The JIT treats user-defined operators exactly like regular functions.
- The parser needs to know about new operators *at parse time* so it can apply precedence rules. Binary operators register their precedence in `BinopPrecedence` when codegen runs. This works because each definition is parsed and codegenned before the next one is processed — in both REPL and file mode. An operator is available to the parser from the line after it is defined. For example, if `|` has precedence `5` and `+` has precedence `20`, then `a + b | 1` parses as `(a + b) | 1`.

Unary operators are a different case: they bind tighter than any binary operator by design, so `-x + 1` always means `(-x) + 1`. They are parsed in a dedicated step before any binary expression is evaluated.

## Grammar

`code/chapter-09/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = definition | decorateddef | external | toplevelexpr ;
definition      = "def" prototype ":" [ eols ] "return" expression ;
decorateddef      = binarydecorator eols "def" binaryopprototype ":" [ eols ] "return" expression
                  | unarydecorator  eols "def" unaryopprototype  ":" [ eols ] "return" expression ;
binarydecorator   = "@" "binary" "(" integer ")" ;
unarydecorator    = "@" "unary" ;
binaryopprototype = customopchar "(" identifier "," identifier ")" ;
unaryopprototype  = customopchar "(" identifier ")" ;
external        = "extern" "def" prototype ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ identifier { "," identifier } ] ")" ;
ifexpr          = "if" expression ":" [ eols ] expression [ eols ] "else" ":" [ eols ] expression ;
forexpr         = "for" identifier "=" expression "," expression "," expression ":" [ eols ] expression ;
expression      = unaryexpr binoprhs ;
binoprhs        = { binaryop unaryexpr } ;
unaryexpr       = unaryop unaryexpr | primary ;
unaryop         = "-" | userdefunaryop ;
primary         = identifierexpr | numberexpr | parenexpr
                | ifexpr | forexpr ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
numberexpr      = number ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
builtinbinaryop = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
integer         = digit { digit } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
```

### What Changed

Chapter 9 adds three things: `decorateddef` for `@binary`/`@unary` definitions; `unaryexpr` inserted between `expression` and `primary` (every operand slot that previously called `primary` now calls `unaryexpr`, which either applies a unary op and recurses, or delegates to `primary`); and `integer` as a distinct terminal to reject fractional precedence values.

```ebnf
-- Chapter 8
expression = primary binoprhs ;
binoprhs   = { binaryop primary } ;

-- Chapter 9
expression = unaryexpr binoprhs ;
binoprhs   = { binaryop unaryexpr } ;
unaryexpr  = unaryop unaryexpr | primary ;
```

`integer` appears only in `binarydecorator`. It is a subset of `number` with no decimal point — `@binary(5)` is valid, `@binary(1.5)` is not.

```ebnf
...
binarydecorator   = "@" "binary" "(" integer ")" ;
....
integer         = digit { digit } ;
```


`customopchar`  is any ASCII punctuation character except `@`, except built-in operator characters (including reserved unary `-`), and except any character already defined as a custom operator (unary or binary).

```ebnf
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
```

## New Tokens

Two new keywords: `binary` and `unary`. They appear only in decorator lines, never in expressions.

```cpp
enum Token {
  // ...existing tokens...

  // user-defined operators
  tok_binary = -16,
  tok_unary  = -17,
};
```

They are added to the `Keywords` map alongside the other keywords:

```cpp
{"binary", tok_binary}, {"unary", tok_unary}
```

The lexer returns `tok_binary` or `tok_unary` when it reads the corresponding word.

## Extending PrototypeAST

A prototype needs to know whether it describes a regular function or an operator, and for binary operators it needs the precedence:

```cpp
class PrototypeAST {
  // ...existing Name, Args...
  bool IsOperator;
  unsigned Precedence; // binary only; 0 for all others

  bool isUnaryOp()  const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  // Last character of the encoded name: "binary|" → '|', "unary!" → '!'
  char getOperatorName() const { return Name.back(); }
};
```

Regular function prototypes keep the defaults `IsOperator=false, Prec=0` and are unaffected.

## A New AST Node: UnaryExprAST

Binary operators already have `BinaryExprAST`. A new node handles unary operator applications:

```cpp
class UnaryExprAST : public ExprAST {
  char Opcode; // char suffices — unary operators are always a single ASCII character.
  unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(char Opcode, unique_ptr<ExprAST> Operand)
      : Opcode(Opcode), Operand(std::move(Operand)) {}

  Value *codegen() override;
};
```

## Defining Operators

### Parsing `@binary(5) def |(x, y): ...`

`ParseDecoratedDef` manages both binary and unary parsing:

```cpp
/// decorateddef
///   = binarydecorator eols "def" binaryopprototype ":" [ eols ] "return" expression
///   | unarydecorator  eols "def" unaryopprototype  ":" [ eols ] "return" expression
///
/// binarydecorator   = "@" "binary" "(" integer ")" ;
/// unarydecorator    = "@" "unary" ;
static unique_ptr<FunctionAST> ParseDecoratedDef() {
  getNextToken(); // eat '@'

  unique_ptr<PrototypeAST> Proto;
  if (CurTok == tok_binary) { // "binary"
    unsigned Prec = ParseBinaryDecorator(); // delegate: processes "binary" "(" integer ")" 
    if (CurTok != tok_eol) // ensure we get a newline after the decorator
      return LogErrorF("Expected newline after '@binary(...)' decorator");
    consumeNewlines(); 
    if (CurTok != tok_def) ... // check for errors (snipped)
    getNextToken(); // eat 'def' and parse the rest of the prototype
    Proto = ParseBinaryOpPrototype(Prec); // will install op's precedence in BinaryOpPrecedence
  } else if (CurTok == tok_unary) {
    ParseUnaryDecorator(); // delegate: processes "unary"
    // ... same newline enforcement, eat 'def' ...
    Proto = ParseUnaryOpPrototype(); // will install the operator as unary<op> 
  } else {
    return LogErrorF("Expected 'binary' or 'unary' after '@'");
  }

  // Shared body tail — same as ParseDefinition, where `return` and expression are parsed.
  ...
}
```

**`ParseBinaryDecorator`** consumes `binary(5)` and returns `5`. The lexer has no `tok_integer` — it emits `tok_number` for both `5` and `1.5` — so the decimal check inspects `NumLiteralStr`, the raw source text:

```cpp
/// binarydecorator
///   = "binary" "(" integer ")"
///
/// Called after '@' has been consumed. CurTok is on 'binary'.
/// Returns the parsed precedence (>= 1), or 0 on error.
/// 0 is a safe sentinel because valid precedences must be >= 1.
static unsigned ParseBinaryDecorator() {
  getNextToken(); // eat 'binary'
  // ...eat '('...
  if (CurTok != tok_number)
    return LogErrorF("Expected precedence after '@binary('");
  // Check for a decimal. We don't have an integer type. 
  // Lexer will happily send decimal numbers back.
  if (NumLiteralStr.find('.') != string::npos)
    return LogErrorF("Operator precedence must be an integer");
  unsigned Prec = (unsigned)NumVal;
  if (Prec == 0)
    return LogErrorF("Operator precedence must be >= 1");
  getNextToken(); // eat number
  // ...eat ')'...
  return Prec;
}
```

Zero is rejected because it is the sentinel/marker value `GetTokPrecedence` returns for unknown operators.

**`ParseBinaryOpPrototype`** reads `|`, encodes it as `"binary|"`, runs three redefinition checks, then reads `x` and `y`:

```cpp
/// binaryopprototype
///   = customopchar "(" identifier "," identifier ")"
///
/// CurTok is on the operator character.
/// The function is stored internally as "binary<opchar>" (e.g. "binary%"),
/// which is how BinaryExprAST::codegen() looks it up at call sites.
static unique_ptr<PrototypeAST> ParseBinaryOpPrototype(unsigned Precedence) {
    if (!IsCustomOpChar(CurTok))
        return LogErrorP("Expected operator character in binary operator prototype");
    char OpChar = (char)CurTok;
    string FnName = string("binary") + OpChar;  // → "binary|"

    if (IsKnownBinaryOperatorToken(CurTok)) ...  // already a binary op?
    if (IsKnownUnaryOperatorToken(CurTok))  ...  // already a unary op?
    if (FunctionProtos.count(FnName))       ...  // encoded name collision?

    getNextToken(); // eat operator char
    // ... read (x, y) — same as ParsePrototype, expect exactly 2 args ...
    return make_unique<PrototypeAST>(FnName, ArgNames, true, Precedence);
}
```

`IsCustomOpChar` checks `isascii(Tok) && ispunct(Tok) && Tok != '@'`. The third check is defensive — `binary|` is not a legal Pyxc identifier — but guards against future parser changes.

### Parsing `@unary def !(v): ...`

The unary path follows a similar scheme. `ParseUnaryDecorator` simply eats `unary` — no precedence argument, since unary operators always bind tighter than any binary operator by design. Among unary operators themselves, there is no precedence either — `-!x` parses as `-(! x)` because `ParseUnary` recurses into itself, applying operators from the outside in and resolving them inside out. `ParseUnaryOpPrototype` encodes the name as `"unary!"` and runs the same two redefinition checks:

```cpp
if (!IsCustomOpChar(CurTok))
    return LogErrorP("Expected operator character in unary operator prototype");

string FnName = string("unary") + OpChar;  // → "unary!"

if (IsKnownUnaryOperatorToken(CurTok))  ...  // already a unary op?
if (IsKnownBinaryOperatorToken(CurTok)) ...  // already a binary op?
```

Since unary operators have no precedence, `PrototypeAST` is created with `Precedence = 0`. Body parsing is identical to the binary path.

## Parsing Unary Expressions

`ParseUnary` is called wherever the grammar expects a `unaryexpr` — as the operand on either side of a binary operator, so `!x + 1` and `f(x) + !y` both work. 

```cpp
static unique_ptr<ExprAST> ParseUnary() {
  // Primary starters — hand off to ParsePrimary.
  if (!isascii(CurTok) || CurTok == '(' || isalpha(CurTok) || isdigit(CurTok))
    return ParsePrimary();
  // Built-in unary minus.
  if (CurTok == '-')
    return ParseUnaryMinus();
  // ASCII punctuation — treat as a user-defined unary prefix.
  int Opc = CurTok;
  getNextToken(); // eat the operator character
  if (auto Operand = ParseUnary())
    return make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}
```

`ParseUnaryMinus` eats `-`, recurses into `ParseUnary` for the operand, and builds a `UnaryExprAST` with opcode `'-'`:

```cpp
static unique_ptr<ExprAST> ParseUnaryMinus() {
  getNextToken(); // eat '-'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  return make_unique<UnaryExprAST>('-', std::move(Operand));
}
```

During codegen, `UnaryExprAST` treats `-` as a built-in and emits `fneg`; all other opcodes are resolved as `unary<opchar>` function calls.

## Code Generation

### PrototypeAST::codegen — Registering Operators

When a binary operator prototype is compiled, its precedence is installed in `BinopPrecedence`. When a unary operator prototype is compiled, its token is inserted into `KnownUnaryOperators`. Both happen at JIT time — inside `codegen` — so the operator is immediately usable in subsequent REPL lines or file definitions:

```cpp
Function *PrototypeAST::codegen() {
  // ...create the LLVM function as before...

  // Register binary operator precedence so the parser recognises it in
  // subsequent expressions.
  if (isBinaryOp())
    BinopPrecedence[getOperatorName()] = Precedence;

  // Register unary operator so ParseUnaryOpPrototype can detect redefinitions.
  if (isUnaryOp())
    KnownUnaryOperators.insert(getOperatorName());

  return F;
}
```

The `BinopPrecedence` side-effect is what makes `GetTokPrecedence()` return the right value for new operators. The `KnownUnaryOperators` side-effect is what lets `ParseUnaryOpPrototype` detect and reject redefinition attempts.

### BinaryExprAST::codegen — User-Defined Fallthrough

The existing `switch` handles built-in operators. Everything else falls through to a function lookup:

```cpp
Value *BinaryExprAST::codegen() {
  // ...codegen L and R...

  switch (Op) {
  // ...built-in operators...
  default: break;
  }

  // User-defined: look up "binary<op>" and emit a call.
  Function *F = getFunction(std::string("binary") + (char)Op);
  if (!F)
    return LogErrorV("invalid binary operator");
  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}
```

Because user-defined operators lower to regular function calls, operands are evaluated before the call is emitted. As a consequence, user-defined operators cannot have short-circuit functionality.

### UnaryExprAST::codegen

```cpp
Value *UnaryExprAST::codegen() {
  Value *Op = Operand->codegen();
  if (!Op)
    return nullptr;

  // Built-in unary minus.
  if (Opcode == '-')
    return Builder->CreateFNeg(Op, "negtmp");

  // User-defined unary operator.
  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return Builder->CreateCall(F, Op, "unop");
}
```

The generated IR for `-x` results in a call to LLVM's fneg:
```llvm
%negtmp = fneg double %x
```

The generated IR for `!x` is a regular function call:

```llvm
%unop = call double @unary!(double %x)
```

And if you mix the two:
Example `-!x`:

```llvm
%unop = call double @unary!(double %x)
%negtmp = fneg double %unop
```

## How It All Fits Together

Here is the complete path for:
```python
@binary(5)
def |(x, y): return if x != 0: 1 else: if y != 0: 1 else: 0
```

1. **MainLoop** sees `@`. Eats it → CurTok is `tok_binary`. Calls `HandleDecorator`.

2. **HandleDecorator** calls `ParseDecoratedDef`.

3. **ParseDecoratedDef** sees `tok_binary` → `IsBinary = true`. Calls `ParseBinaryDecorator`.

4. **ParseBinaryDecorator**: eats `binary` → eats `(` → sees `tok_number`: `NumLiteralStr = "5"`, `NumVal = 5.0`. No `.` in literal. `NumVal ≥ 1`. `Prec = 5`. Eats `5` → eats `)`. Returns `5`.

5. **ParseDecoratedDef**: `Prec = 5`. Checks `CurTok == tok_eol` (end of decorator line) — yes. Calls `consumeNewlines()`, which eats one or more consecutive `tok_eol` tokens. CurTok is now `tok_def`. Eats `def` → CurTok is `|`. Calls `ParseBinaryOpPrototype(5)`.

6. **ParseBinaryOpPrototype**: `IsCustomOpChar('|')` → true. `OpChar = '|'`, `FnName = "binary|"`. `IsKnownBinaryOperatorToken('|')` → false. `FunctionProtos.count("binary|")` → 0. Eats `|` → eats `(` → reads `x`, `,`, `y` → eats `)`. `ArgNames = {"x", "y"}`, size = 2 → ok. Returns `PrototypeAST("binary|", {"x","y"}, true, 5)`.

7. **ParseDecoratedDef**: eats `:` → `consumeNewlines()` (body is inline, no eols) → eats `return` → calls `ParseExpression()`. The expression `if x != 0: 1 else: if y != 0: 1 else: 0` is parsed into a nested `IfExprAST`. Returns `FunctionAST("binary|", body)`.

8. **HandleDecorator** calls `FnAST->codegen()`.

9. **FunctionAST::codegen** moves the prototype into `FunctionProtos["binary|"]`. Calls `getFunction("binary|")`, which calls `PrototypeAST::codegen()`:
   - Creates `double @binary|(double %x, double %y)` in `TheModule`.
   - `isBinaryOp()` is true → installs `BinopPrecedence['|'] = 5`.

10. **FunctionAST::codegen** creates the entry block, populates `NamedValues` with `{x, y}`, codegens the body, emits `ret`, runs the optimiser. Returns the compiled `Function*`.

11. **HandleDecorator** prints `"Parsed a user-defined operator."`. Hands the module to the JIT. Calls `InitializeModuleAndManagers`.

Now `|` is a live binary operator. When the parser next sees `1 | 0`, `GetTokPrecedence` returns 5, `ParseBinOpRHS` builds `BinaryExprAST('|', 1, 0)`, and codegen looks up `binary|` in the JIT and emits a call.

## Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/pyxc
```

The binary runs as an interactive REPL when given no file argument. Press `Ctrl-D` to exit.

## Try It

### Defining a binary operator

The decorator line ends at the newline. The REPL waits silently for the `def` line — no second `ready>` prompt appears between the two lines.

<!-- code-merge:start -->
```python
ready> @binary(5)
def |(x, y): return if x != 0: 1 else: if y != 0: 1 else: 0
```
```bash
Parsed a user-defined operator.
```
```python
ready> 1 | 0
```
```bash
Parsed a top-level expression.
Evaluated to 1.000000
```
```python
ready> 0 | 0
```
```bash
Parsed a top-level expression.
Evaluated to 0.000000
```
```python
ready>
```
<!-- code-merge:end -->

### Defining a unary operator
<!-- code-merge:start -->
```python
ready> @unary
def !(x): return if x == 0: 1 else: 0
```
```bash
Parsed a user-defined operator.
```
```python
ready> !0
```
```bash
Parsed a top-level expression.
Evaluated to 1.000000
```
```python
ready> !5
```
```bash
Parsed a top-level expression.
Evaluated to 0.000000
```
```python
ready>
```
<!-- code-merge:end -->

### Composing unary minus and a user-defined unary operator

`ParseUnaryMinus` recurses into `ParseUnary` for its operand, so `-!x` parses as unary-minus applied to `!x`:

<!-- code-merge:start -->
```python
ready> @unary
def !(x): return if x == 0: 1 else: 0
```
```bash
Parsed a user-defined operator.
```
```python
ready> -!0
```
```bash
Parsed a top-level expression.
Evaluated to -1.000000
```
```python
ready> -!5
```
```bash
Parsed a top-level expression.
Evaluated to -0.000000
```
```python
ready>
```
<!-- code-merge:end -->

`-!5` evaluates to `-0.000000` because `!5` is `0.0` and `fneg 0.0` is IEEE 754 negative zero. Negative zero compares equal to `0.0` in any subsequent expression, so this is harmless.

Running `./build/pyxc -v` shows the generated IR for `-!5`:

<!-- code-merge:start -->
```llvm
define double @__anon_expr() {
entry:
  %unop = call double @"unary!"(double 5.000000e+00)
  %negtmp = fneg double %unop
  ret double %negtmp
}
```
```bash
Evaluated to -0.000000
```
<!-- code-merge:end -->

### A low-precedence sequencing operator

Setting precedence to 1 — lower than all built-ins — lets `;` act as a sequencer: `a ; b` evaluates `a` for its side effects and returns `b`:

<!-- code-merge:start -->
```python
ready> extern def printd(x)
```
```bash
Parsed an extern.
```
```python
ready> @binary(1)
def ;(lhs, rhs): return rhs
```
```bash
Parsed a user-defined operator.
```
```python
ready> printd(1) ; printd(2) ; 99
```
```bash
Parsed a top-level expression.
1.000000
2.000000
Evaluated to 99.000000
```
```python
ready>
```
<!-- code-merge:end -->

### Validation errors

Attempting to redefine a built-in binary operator:

<!-- code-merge:start -->
```python
ready> @binary(5)
def +(x, y): return x + y
```
```bash
Error (Line 2, Column 5): Binary operator '+' is already defined
def +(
    ^~~~
ready>
```
<!-- code-merge:end -->

Decimal precedence:

<!-- code-merge:start -->
```python
ready> @binary(1.5)
def %(x, y): return x - y
```
```bash
Error (Line 1, Column 9): Precedence must be an integer, not a decimal literal
@binary(1.5)
        ^~~~
ready>
```
<!-- code-merge:end -->

Unary/Binary conflict — once `|` is binary, it cannot also become unary (and vice-versa):

<!-- code-merge:start -->
```python
ready> @binary(5)
def |(x, y): return if x != 0: 1 else: if y != 0: 1 else: 0
```
```bash
Parsed a user-defined operator.
```
```python
ready> @unary
def |(x): return if x != 0: 0 else: 1
```
```bash
Error (Line 4, Column 5): Unary operator '|' conflicts with an existing binary operator
def |(
    ^~~~
ready>
```
<!-- code-merge:end -->

## The Payoff: Density-Shaded Mandelbrot

[Chapter 8](chapter-08.md) already rendered the Mandelbrot set — but every point was either `*` (outside the set) or space (inside). The fractal boundary was a hard edge:

```
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************   *********************************
******************************************    ********************************
*******************************************  *********************************
************************************ **          *****************************
************************************                 *************************
***********************************                 **************************
**********************************                   *************************
*********************************                     ************************
*********************** *  *****                      ************************
***********************       **                      ************************
**********************         *                      ************************
*******************  *         *                     *************************
*******************  *         *                     *************************
**********************         *                      ************************
***********************       **                      ************************
*********************** *   ****                      ************************
*********************************                     ************************
**********************************                   *************************
***********************************                 **************************
*************************************                *************************
************************************ *           *****************************
*******************************************  *********************************
******************************************    ********************************
******************************************    ********************************
******************************************** *********************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
```

Now that we have user-defined operators, we can rewrite the renderer to shade by density — mapping how quickly each point escapes to a different character. The boundary dissolves into gradients of `*`, `+`, `.`, and space.

Four things change from the chapter 8 version:

- **Unary minus.** Built-in unary minus is now parsed directly (`ParseUnaryMinus`) and lowered by `UnaryExprAST::codegen` to LLVM `fneg`, so `-2.3` works without the `0 - 2.3` workaround from chapter 8.
- **`;` for sequencing.** Chapter 8 wrote `mandelrow(...) + putchard(10)` to chain two side-effect calls — adding two `0.0` return values happens to work, but is misleading. The new `@binary(1) def ;(x, y)` makes intent explicit: evaluate left for its side effect, return right.
- **`|` to combine exit conditions.** Chapter 8's `mandelconverge` checked the iteration limit and the escape radius with nested `if`. Chapter 9 tests `iters > 255 | (real * real + imag * imag > 4)` in one expression using `@binary(5) def |`.
- **`printdensity` for shading.** Instead of mapping each point to just inside/outside, the iteration count at escape determines the shade character.

```python
# test/mandel.pyxc
extern def putchard(x)

# Logical not: 0 -> 1, non-zero -> 0.
@unary
def !(v):
    return if v == 0: 1 else: 0

# Sequencing operator: evaluate lhs for side effects, then return rhs.
@binary(1)
def ;(x, y):
    return y

# Logical OR (no short-circuit).
@binary(5)
def |(lhs, rhs):
    return if lhs: 1 else: if rhs: 1 else: 0

# Logical AND (no short-circuit).
# !!rhs normalises rhs to 0.0 or 1.0 — rhs might be any double, not just a boolean.
@binary(6)
def &(lhs, rhs):
    return if !lhs: 0 else: !!rhs

# printdensity - map iteration count to an ASCII shade.
def printdensity(d):
    return if d > 8: putchard(32) else: if d > 4: putchard(46) else: if d > 2: putchard(43) else: putchard(42)

# Determine whether z = z^2 + c diverges for the given point.
def mandelconverger(real, imag, iters, creal, cimag):
    return if iters > 255 | (real * real + imag * imag > 4): iters else: mandelconverger(real * real - imag * imag + creal, 2 * real * imag + cimag, iters + 1, creal, cimag)

# Return number of iterations required for escape.
def mandelconverge(real, imag):
    return mandelconverger(real, imag, 0, real, imag)

# Render one row.
def mandelrow(xmin, xmax, xstep, y):
    return for x = xmin, x < xmax, xstep:
               printdensity(mandelconverge(x, y))

# Render full 2D region.
def mandelhelp(xmin, xmax, xstep, ymin, ymax, ystep):
    return for y = ymin, y < ymax, ystep:
               mandelrow(xmin, xmax, xstep, y) ; putchard(10)

# Top-level helper.
def mandel(realstart, imagstart, realmag, imagmag):
    return mandelhelp(realstart, realstart + realmag * 78, realmag, imagstart, imagstart + imagmag * 40, imagmag)

mandel(-2.3, -1.3, 0.05, 0.07)
mandel(-2, -1, 0.02, 0.04)
mandel(-0.9, -1.4, 0.02, 0.03)
```

The four custom operators:

- `@unary def !(v)` — logical NOT: returns `1` if `v == 0`, else `0`.
- `@binary(1) def ;(x, y)` — sequencing: evaluates `x` for side effects, returns `y`. Used as `mandelrow(...) ; putchard(10)` to print a newline after each row.
- `@binary(5) def |(lhs, rhs)` — logical OR (no short-circuit): `if lhs: 1 else: if rhs: 1 else: 0`.
- `@binary(6) def &(lhs, rhs)` — logical AND (no short-circuit): `if !lhs: 0 else: !!rhs`. Its body uses the already-defined `!` — operators become available immediately after their prototype is JIT-compiled.

The precedences are chosen carefully: `|` (5) and `&` (6) are both lower than comparisons (10), so `iters > 255 | (real * real + imag * imag > 4)` parses as `(iters > 255) | (...)` as intended. If `|` had higher precedence than `>`, the condition would parse wrong.

`printdensity(d)` maps an iteration count to an ASCII shade character:

| count | char | meaning |
|-------|------|---------|
| > 8   | ` ` (space) | deep inside — survived 9+ iterations |
| > 4   | `.`  | boundary zone — survived 5–8 iterations |
| > 2   | `+`  | near boundary — survived 3–4 iterations |
| ≤ 2   | `*`  | fast escape — outside the set |

`mandelconverger` combines the two exit conditions with `|`:

```python
if iters > 255 | (real * real + imag * imag > 4): iters else: ...
```

Run it directly:

```bash
./build/pyxc test/mandel.pyxc
```

The same view as chapter 8 (`mandel(-2.3, -1.3, 0.05, 0.07)`) now produces:

```
******************************************************************************
******************************************************************************
****************************************++++++********************************
************************************+++++...++++++****************************
*********************************++++++++.. ...+++++**************************
*******************************++++++++++..   ..+++++*************************
******************************++++++++++.     ..++++++************************
****************************+++++++++....      ..++++++***********************
**************************++++++++.......      .....++++**********************
*************************++++++++.   .            ... .++*********************
***********************++++++++...                     ++*********************
*********************+++++++++....                    .+++********************
******************+++..+++++....                      ..+++*******************
**************++++++. ..........                        +++*******************
***********++++++++..        ..                         .++*******************
*********++++++++++...                                 .++++******************
********++++++++++..                                   .++++******************
*******++++++.....                                    ..++++******************
*******+........                                     ...++++******************
*******+... ....                                     ...++++******************
*******+++++......                                    ..++++******************
*******++++++++++...                                   .++++******************
*********++++++++++...                                  ++++******************
**********+++++++++..        ..                        ..++*******************
*************++++++.. ..........                        +++*******************
******************+++...+++.....                      ..+++*******************
*********************+++++++++....                    ..++********************
***********************++++++++...                     +++********************
*************************+++++++..   .            ... .++*********************
**************************++++++++.......      ......+++**********************
****************************+++++++++....      ..++++++***********************
*****************************++++++++++..     ..++++++************************
*******************************++++++++++..  ...+++++*************************
*********************************++++++++.. ...+++++**************************
***********************************++++++....+++++****************************
***************************************++++++++*******************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
```

The file then calls `mandel(...)` two more times, zooming into different regions of the complex plane, with the density shading revealing finer boundary detail at each zoom level.

## Things Worth Knowing

- **An operator is either unary or binary, not both.** Once `|` is defined as binary, it cannot also be defined as unary (and vice-versa). This is enforced at parse time.

- **Operators cannot be removed or redefined within a session.** Once a custom operator is registered, there is no mechanism to remove or reassign it. Restart the REPL to get a clean slate.

## What's Next

[Chapter 10](chapter-10.md) adds mutable local variables and assignment using a temporary `var ... :` expression form. This keeps Pyxc expression-oriented for one more chapter before real statement blocks arrive.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

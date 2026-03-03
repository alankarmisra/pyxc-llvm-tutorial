---
description: "Add user-defined operators via Python-style decorators — @binary(N) and @unary — and use them to render a density-shaded Mandelbrot set."
---
# 9. User-Defined Operators

## Where We Are

[Chapter 8](chapter-08.md) added control flow — `if`/`else` and `for` — and used it to render the Mandelbrot set in ASCII. Every operator Pyxc understands is built into the compiler: `+`, `-`, `*`, comparisons. If a user wants a new operator, they're out of luck.

This chapter changes that. After it, Pyxc programs can define new operators using Python-style decorators:

```python
@binary(1)
def ;(lhs, rhs):
    return rhs

@unary
def !(x):
    return if x == 0: 1 else: 0
```

The operators are stored as regular LLVM functions — `binary;` and `unary!` — so the compiler generates a call to them just like any other function. The only thing that makes them special is that you can write `a ; b` instead of `binary;(a, b)`.

As a payoff, we re-render the Mandelbrot set with density shading: instead of a single `*` character, points are mapped to `@`, `#`, `*`, `.`, and ` ` based on how quickly they diverge.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-09
```

## The Design

The key insight: operators are just functions with funny names. A user-defined binary operator `|` is stored in LLVM as a function called `binary|`. A unary operator `!` is stored as `unary!`. When the parser encounters `a | b`, it looks up `binary|` and generates a call.

This means:
- No special IR for user-defined operators. The JIT compiles them exactly like regular functions.
- The parser needs to know about new operators *at parse time* so it can handle precedence. Binary operators register their precedence in `BinopPrecedence` when codegen runs.
- Unary operators bind tighter than any binary operator — they're applied before any binary expression is evaluated.

## New Tokens

Two new keywords: `binary` and `unary`. They appear in decorator lines, not in expressions.

```cpp
enum Token {
  // ...existing tokens...

  // user-defined operators
  tok_binary = -16,
  tok_unary  = -17,
};
```

They're added to the keyword table and the `TokenNames` map alongside the other keywords.

## Extending PrototypeAST

A prototype needs to know whether it describes a regular function or an operator, and for binary operators it needs the precedence:

```cpp
class PrototypeAST {
  string Name;
  vector<string> Args;
  bool IsOperator;
  unsigned Precedence; // binary operators only

public:
  PrototypeAST(const string &Name, vector<string> Args,
               bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), Args(std::move(Args)), IsOperator(IsOperator),
        Precedence(Prec) {}

  const string &getName() const { return Name; }

  bool isUnaryOp()  const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  char getOperatorName() const {
    assert((isUnaryOp() || isBinaryOp()) && "Not an operator prototype");
    return Name.back(); // "binary+" -> '+', "unary!" -> '!'
  }

  unsigned getBinaryPrecedence() const { return Precedence; }

  Function *codegen();
};
```

`isUnaryOp()` and `isBinaryOp()` determine the type from the argument count — one argument means unary, two means binary. `getOperatorName()` returns the last character of the encoded name.

## A New AST Node: UnaryExprAST

Binary operators already have `BinaryExprAST`. We need a new node for unary operator applications:

```cpp
class UnaryExprAST : public ExprAST {
  char Opcode;
  unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(char Opcode, unique_ptr<ExprAST> Operand)
      : Opcode(Opcode), Operand(std::move(Operand)) {}

  Value *codegen() override;
};
```

## Decorator Syntax: @binary and @unary

Pyxc uses Python-style decorators to mark operator definitions:

```python
@binary(5)
def +(lhs, rhs):    # overload +  (precedence 5)
    ...

@unary
def !(x):           # define logical not
    ...
```

The decorator is a *separate line* above the `def`. The `@` is not an operator character — it's the decorator marker. The compiler parses the decorator line, records the operator type and precedence, then parses the `def` that follows.

### Parsing the Decorator Line

When `MainLoop` sees `@`, it eats the `@` and calls `HandleDecorator`. The handler reads `binary(N)` or `unary`, eats the newline, checks for `def`, then calls `ParseDefinition(IsOperator=true, IsBinary, Precedence)`:

```cpp
case '@':
  getNextToken(); // eat '@', now on 'binary' or 'unary'
  HandleDecorator();
  break;
```

```cpp
static void HandleDecorator() {
  bool IsBinary = (CurTok == tok_binary);
  if (CurTok != tok_binary && CurTok != tok_unary) {
    LogError("Expected 'binary' or 'unary' after '@'");
    SynchronizeToLineBoundary();
    return;
  }
  getNextToken(); // eat 'binary'/'unary'

  unsigned Precedence = 30; // default
  if (IsBinary) {
    // Expect '(' number ')'.
    // ...parse '(' NumVal ')' and store in Precedence...
  }

  // Eat the newline that ends the decorator line.
  while (CurTok == tok_eol) getNextToken();

  if (CurTok != tok_def) { /* error */ return; }

  auto FnAST = ParseDefinition(/*IsOperator=*/true, IsBinary, Precedence);
  // ...codegen and JIT...
}
```

### Parsing the Operator Prototype

`ParsePrototype` receives `IsOperator=true` and `IsBinary`. After `ParseDefinition` has eaten `def`, `CurTok` is sitting on the operator character itself — `+`, `|`, `!`, `;`, whatever the user wrote. The prototype reads that character and encodes it into the function name:

```cpp
if (IsOperator) {
  if (!isascii(CurTok) || isalnum(CurTok) || CurTok == '@')
    return LogErrorP("Expected operator character after 'def'");
  char OpChar = (char)CurTok;
  FnName = string(IsBinary ? "binary" : "unary") + OpChar;
  getNextToken(); // eat operator char
}
```

After parsing arguments, the arity is validated: unary must have exactly one argument, binary exactly two.

## Parsing Unary Operator Applications

The grammar now has a new level between `primary` and `binoprhs`:

```
expression = unary binoprhs
unary      = opchar unary
           | primary
```

`ParseUnary` checks whether the current token looks like a user-defined unary operator character. If it does, it eats the character, recursively parses the operand with another `ParseUnary` call, and builds a `UnaryExprAST`. Otherwise it falls through to `ParsePrimary`:

```cpp
static unique_ptr<ExprAST> ParseUnary() {
  // If CurTok starts a primary (letter, digit, '-', '(', or keyword token),
  // defer to ParsePrimary.
  if (!isascii(CurTok) || CurTok == '(' || CurTok == '-' ||
      isalpha(CurTok) || isdigit(CurTok))
    return ParsePrimary();

  // Otherwise treat it as a user-defined unary operator.
  int Opc = CurTok;
  getNextToken(); // eat the operator character
  if (auto Operand = ParseUnary())
    return make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}
```

`ParseExpression` and `ParseBinOpRHS` are both updated to call `ParseUnary` instead of `ParsePrimary` directly. This means unary operators apply in all positions — `!x + 1`, `f(!y)`, `a * !b` all work.

## Code Generation

### PrototypeAST::codegen — Registering Precedence

When a binary operator prototype is compiled, its precedence is installed in `BinopPrecedence` so the parser will recognize the new operator in subsequent expressions:

```cpp
Function *PrototypeAST::codegen() {
  // ...create function as before...

  // Register binary operator precedence so the parser recognises it.
  if (isBinaryOp())
    BinopPrecedence[getOperatorName()] = Precedence;

  return F;
}
```

This happens inside `codegen`, which runs at JIT time. After this line executes, the operator is immediately usable in the next REPL input or the next definition in the file.

### BinaryExprAST::codegen — User-Defined Fallthrough

The existing `switch` handles built-in operators. Everything else falls through to a function lookup:

```cpp
Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();

  switch (Op) {
  case '+': return Builder->CreateFAdd(L, R, "addtmp");
  // ...other built-ins...
  default:
    break; // fall through to user-defined lookup
  }

  // Look for a user-defined binary operator function.
  Function *F = getFunction(std::string("binary") + (char)Op);
  if (!F)
    return LogErrorV("invalid binary operator");

  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}
```

### UnaryExprAST::codegen

```cpp
Value *UnaryExprAST::codegen() {
  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  Value *Op = Operand->codegen();
  if (!Op) return nullptr;

  return Builder->CreateCall(F, Op, "unop");
}
```

The generated IR for `!x` is just:

```llvm
%unop = call double @unary!(double %x)
```

No special instruction. It's a regular function call.

## The Payoff: Density-Shaded Mandelbrot

With `@binary` and `@unary` working, we can write a richer Mandelbrot renderer. The new version uses:

- `@binary(1) def ;(lhs, rhs): return rhs` — a *sequencing* operator with the lowest possible precedence (1). `a ; b` evaluates `a` for its side effects, discards the result, and returns `b`. This sequences the `mandelrow` call with the `putchard(10)` newline call without needing a separate wrapper function.

- `@unary def !(x): return if x == 0: 1 else: 0` — logical not. Included to demonstrate `@unary`; the Mandelbrot code doesn't use it directly, but you can build on it.

- `printdensity(d)` — maps an escape-time count to a density character:

```python
#   d == 0   -> inside the set              -> '@'
#   d < 3    -> boundary region             -> '#'
#   d < 7    -> inner halo                  -> '*'
#   d < 17   -> outer halo                  -> '.'
#   d >= 17  -> open space                  -> ' '
def printdensity(d):
    return if d == 0: putchard(64)
    else: if d < 3:  putchard(35)
    else: if d < 7:  putchard(42)
    else: if d < 17: putchard(46)
    else: putchard(32)
```

`mandelconverge` returns the number of iterations *remaining* when the point escaped (or 0 if it never escaped). A low non-zero `d` means it almost ran out of iterations before escaping — it's right on the boundary. A high `d` means it escaped quickly — it's deep in the open space. We use 32 max iterations so the gradient spreads across a visible range.

The `for` loop body now uses `;` to sequence:

```python
def mandelhelp(xmin, xmax, xstep, ymin, ymax, ystep):
    return for y = ymin, y < ymax, ystep:
               mandelrow(xmin, xmax, xstep, y) ; putchard(10)
```

`mandelrow(...) ; putchard(10)` calls `mandelrow` (which prints a full row of density characters), then calls `putchard(10)` (newline), and returns the newline result. Without `;`, we'd need a helper function to sequence the two calls — which is how the chapter-8 version was written.

### Full Source: `test/mandel2.pyxc`

```python
extern def putchard(x)

@binary(1)
def ;(lhs, rhs):
    return rhs

@unary
def !(x):
    return if x == 0: 1 else: 0

def printdensity(d):
    return if d == 0: putchard(64) else: if d < 3: putchard(35) else: if d < 7: putchard(42) else: if d < 17: putchard(46) else: putchard(32)

def mandelconverge(real, imag, iters, creal, cimag):
    return if iters == 0: 0 else: if (real * real + imag * imag) > 4: iters else: mandelconverge(real * real - imag * imag + creal, 2 * real * imag + cimag, iters - 1, creal, cimag)

def mandelrow(xmin, xmax, xstep, y):
    return for x = xmin, x < xmax, xstep:
               printdensity(mandelconverge(x, y, 32, x, y))

def mandelhelp(xmin, xmax, xstep, ymin, ymax, ystep):
    return for y = ymin, y < ymax, ystep:
               mandelrow(xmin, xmax, xstep, y) ; putchard(10)

def mandel2(realstart, imagstart, realmag, imagmag):
    return mandelhelp(realstart, realstart + realmag * 78, realmag, imagstart, imagstart + imagmag * 40, imagmag)

mandel2(-2.3, -1.3, 0.05, 0.07)
```

### Output

```
                                            .
                                           .@.
                                         .@@@@@
                                          @@@@.
                                          .@@. *
                                   @@ .@@@@@@@@@@.
                                   @@@@@@@@@@@@@@@@@@
                                 ..@@@@@@@@@@@@@@@@@@
                     .           *@@@@@@@@@@@@@@@@@@@*
                      .@        .@@@@@@@@@@@@@@@@@@@@@@
                       @.@@@.   @@@@@@@@@@@@@@@@@@@@@@
                      .@@@@@@@#.@@@@@@@@@@@@@@@@@@@@@@
                     @@@@@@@@@@*@@@@@@@@@@@@@@@@@@@@@@
                   @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
                  .@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
                     .@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
                     @*@@@@@@@@.@@@@@@@@@@@@@@@@@@@@@@
                      .@#@@@#   @@@@@@@@@@@@@@@@@@@@@@.
                      .@  .     .@@@@@@@@@@@@@@@@@@@@@@
                     .          #@@@@@@@@@@@@@@@@@@@@@..
                                .@.@@@@@@@@@@@@@@@@@@
                                   .@@@@@@@@@@@@@@@@@@
                                   *@.@@@@@@@@@@@@ #..
                                  @.   @@..@@..@ .   .
                                         .@@@@
                                          @@@@.
                                           @@
                                            .
```

The `@` characters form the Mandelbrot set body. The `.`, `*`, and `#` characters show the boundary region — points that are *barely* outside the set. The empty space is where points diverge immediately.

## How It All Fits Together

Here's what happens when Pyxc processes `@binary(1)\ndef ;(lhs, rhs):\n    return rhs`:

1. **MainLoop** sees `@`, eats it, calls `HandleDecorator`.
2. **HandleDecorator** reads `binary`, `(`, `1`, `)`, then the newline. Records `IsBinary=true, Precedence=1`. Calls `ParseDefinition(true, true, 1)`.
3. **ParseDefinition** eats `def`. CurTok is now `;`. Calls `ParsePrototype(true, true, 1)`.
4. **ParsePrototype** sees `;`, builds `FnName = "binary;"`. Parses `(lhs, rhs)`. Returns `PrototypeAST("binary;", ["lhs","rhs"], true, 1)`.
5. **ParseDefinition** parses the body (`return rhs`). Returns a `FunctionAST`.
6. **FunctionAST::codegen** calls `PrototypeAST::codegen` which:
   - Creates an LLVM function `double @binary;(double %lhs, double %rhs)`.
   - Installs `BinopPrecedence[';'] = 1`.
7. The JIT compiles and links the function.

Now `;` is a live binary operator. When the parser next sees `a ; b`, `GetTokPrecedence` returns 1, `BinaryExprAST(';', a, b)` is built, and codegen calls `@binary;`.

## What Changed

Compared to chapter 8:

| What | Change |
|------|--------|
| Tokens | Added `tok_binary`, `tok_unary` |
| AST | `PrototypeAST` extended; new `UnaryExprAST` |
| Parser | `ParseUnary` added; `ParseExpression`/`ParseBinOpRHS` use it; `ParsePrototype` handles operator forms; `HandleDecorator` added; MainLoop dispatches `@` |
| Codegen | `PrototypeAST::codegen` registers binary precedence; `BinaryExprAST::codegen` falls through to user-defined lookup; `UnaryExprAST::codegen` added |
| Tests | `test/mandel2.pyxc` — density-shaded Mandelbrot using `@binary(1) def ;` |

## Experiment

Try defining your own operators:

```python
# Average of two numbers
@binary(20)
def %(lhs, rhs):
    return (lhs + rhs) * 0.5

3 % 7    # -> 5.0
```

```python
# Absolute value
@unary
def |(x):
    return if x < 0: 0 - x else: x

|-3.0    # -> 3.0
```

Notice that `|` as a unary operator works even though `|` is also a potential binary operator character — the parser checks whether `|` appears in `BinopPrecedence` to decide. Since we haven't registered it as binary, it parses as unary.

## What's Next

Chapter 10 replaces `for var = start, cond, step:` with Python-style indentation blocks, adding `INDENT` and `DEDENT` tokens to the lexer so multi-statement function bodies can be written naturally.

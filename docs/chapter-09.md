---
description: "Add user-defined operators via Python-style decorators — @binary(N) and @unary — backed by dedicated parser functions with compile-time validation."
---
# 9. Pyxc: User-Defined Operators

## Where We Are

[Chapter 8](chapter-08.md) added comparison operators, `if`/`else` and `for` loops. Every operator Pyxc understands is still wired into the compiler: `+`, `-`, `*`, the six comparisons. This chapter of the tutorial takes a wild digression into adding user-defined operators to the simple and beautiful pyxc language. We won't debate whether this is a good idea. One of the great things about creating your own language is that you get to decide what is good or bad. In this tutorial we'll use it as a vehicle for some interesting parsing techniques.

Pyxc programs can now declare new operators using Python-style decorators. The decorator line gives the operator its type and precedence; the `def` line that follows gives it its body:

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

The key insight: operators are just functions with funny names. A user-defined binary operator `|` is stored in LLVM as a function called `binary|`. A unary operator `!` is stored as `unary!`. When the parser encounters `a | b`, it looks up `binary|` and generates a call.

This means:
- The JIT treats user-defined functions exactly like regular functions.
- The parser needs to know about new operators *at parse time* so it can apply precedence rules. Binary operators register their precedence in `BinopPrecedence` when codegen runs. For example, if `|` has precedence `5` and `+` has precedence `20`, then `a + b | 1` parses as `(a + b) | 1`.
- Unary operators bind tighter than any binary operator — they are parsed in a dedicated step before any binary expression is evaluated.

## Grammar

Chapter 9 extends the grammar in three places: a new `decorateddef` production for `@binary`/`@unary` definitions, a new `unaryexpr` level between `expression` and `primary`, and an `integer` terminal to restrict decorator precedences to whole numbers.

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
conditionalexpr = "if" expression ":" [ eols ] expression [ eols ] "else" ":" [ eols ] expression ;
forexpr         = "for" identifier "=" expression "," expression "," expression ":" [ eols ] expression ;
expression      = unaryexpr binoprhs ;
binoprhs        = { binaryop unaryexpr } ;
unaryexpr       = unaryop unaryexpr | primary ;
unaryop         = "-" | userdefunaryop ;
primary         = identifierexpr | numberexpr | parenexpr
                | conditionalexpr | forexpr ;
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

`integer` appears only in `binarydecorator`. It is a subset of `number` with no decimal point — `@binary(5)` is valid, `@binary(1.5)` is not.

`customopchar`  is any ASCII punctuation character except `@`, except built-in operator characters (including reserved unary `-`), and except any character already defined as a custom operator (unary or binary).

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

They are added to the keyword table alongside the other keywords, so the lexer returns `tok_binary` or `tok_unary` when it reads the corresponding word.

## New Global State

Three global variables change in this chapter.

**`BinopPrecedence` gains new entries at runtime.** It is initialised at startup with the nine built-in operators. When `PrototypeAST::codegen` runs for a user-defined binary operator, it installs the operator's token and precedence into this map. From that point on, `GetTokPrecedence()` returns the correct value for the new operator and `ParseBinOpRHS` handles it correctly.

**`KnownUnaryOperators` is a new `std::set<int>`.** It tracks every unary operator token that is reserved or defined. It is seeded with `'-'` at startup to block users from defining `@unary def -(v)` — unary minus is a built-in handled by `ParseUnaryMinus`, and a user-defined `unary-` prototype would shadow it silently. When `PrototypeAST::codegen` runs for a user-defined unary operator, it inserts the operator's token into this set. `ParseUnaryOpPrototype` checks the set to reject redefinitions.

```cpp
static std::set<int> KnownUnaryOperators = {'-'};
```

**`FunctionProtos` is moved earlier.** In earlier chapters `FunctionProtos` lived near the codegen functions. This chapter's new parser functions (`ParseBinaryOpPrototype`, `ParseUnaryOpPrototype`) need to check it for name collisions, so it is declared before the parser section:

```cpp
// FunctionProtos - Persistent prototype registry used by the parser to detect
// redefinition of operators. Also used by codegen to re-emit declarations into
// fresh modules. Declared here so parser functions can access it.
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
```

## Extending PrototypeAST

A prototype needs to know whether it describes a regular function or an operator, and for binary operators it needs the precedence:

```cpp
class PrototypeAST {
  string Name;
  vector<string> Args;
  bool IsOperator;
  unsigned Precedence; // binary operators only; 0 for all others

public:
  PrototypeAST(const string &Name, vector<string> Args,
               bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), Args(std::move(Args)), IsOperator(IsOperator),
        Precedence(Prec) {}

  const string &getName() const { return Name; }

  bool isUnaryOp()  const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  // The operator character is the last character of the encoded name.
  // e.g. "binary+" -> '+', "unary!" -> '!'
  char getOperatorName() const {
    assert((isUnaryOp() || isBinaryOp()) && "Not an operator prototype");
    return Name.back();
  }

  unsigned getBinaryPrecedence() const { return Precedence; }

  Function *codegen();
};
```

`isUnaryOp()` and `isBinaryOp()` derive the arity from the argument count — one argument means unary, two means binary. `getOperatorName()` returns the last character of the encoded name.

Regular function prototypes keep the defaults `IsOperator=false, Prec=0` and are unaffected.

## A New AST Node: UnaryExprAST

Binary operators already have `BinaryExprAST`. A new node handles unary operator applications:

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

## Parsing: Decorator and Operator Prototype

This is where most of the new code lives. The implementation is split into five tightly-scoped functions, each responsible for one grammar rule. This makes each function independently testable and keeps the validation logic close to the grammar it enforces.

### Parsing the Binary Decorator

`ParseBinaryDecorator` reads `binary(N)` after `@` has been consumed. It returns the precedence as an `unsigned`, using `0` as a safe error sentinel (valid precedences are ≥ 1):

```cpp
/// binarydecorator
///   = "binary" "(" integer ")"
///
/// Called after '@' has been consumed. CurTok is on 'binary'.
/// Returns the parsed precedence (>= 1), or 0 on error.
/// 0 is a safe sentinel because valid precedences must be >= 1.
static unsigned ParseBinaryDecorator() {
  getNextToken(); // eat 'binary'

  if (CurTok != '(') {
    LogError("Expected '(' after '@binary'");
    return 0;
  }
  getNextToken(); // eat '('

  if (CurTok != tok_number) {
    LogError("Expected precedence number in '@binary(...)'");
    return 0;
  }
  // The lexer has no separate tok_integer — it emits tok_number for both
  // integer and decimal literals. Reject decimals by checking the raw source.
  if (NumLiteralStr.find('.') != string::npos) {
    LogError("Precedence must be an integer, not a decimal literal");
    return 0;
  }
  if (NumVal < 1) {
    LogError("Precedence must be a positive integer");
    return 0;
  }
  unsigned Prec = static_cast<unsigned>(NumVal);
  getNextToken(); // eat number

  if (CurTok != ')') {
    LogError("Expected ')' after precedence in '@binary(...)'");
    return 0;
  }
  getNextToken(); // eat ')'
  return Prec;
}
```

Two details worth noting. First, the lexer has no `tok_integer` — it emits `tok_number` for both `5` and `1.5`. The decimal check uses `NumLiteralStr`, the raw source text of the number token, to detect a `.`. Second, zero is rejected because `0` is the sentinel used to signal that this function failed.

`ParseUnaryDecorator` is trivial — it just eats the `unary` token:

```cpp
static void ParseUnaryDecorator() {
  getNextToken(); // eat 'unary'
}
```

### Parsing the Operator Prototype

`ParseBinaryOpPrototype` takes the already-parsed precedence, reads the operator character and its two parameter names, and returns a `PrototypeAST` with the encoded function name:

```cpp
/// binaryopprototype
///   = customopchar "(" identifier "," identifier ")"
static unique_ptr<PrototypeAST> ParseBinaryOpPrototype(unsigned Precedence) {
  if (!IsCustomOpChar(CurTok))
    return LogErrorP("Expected operator character in binary operator prototype");

  char OpChar = (char)CurTok;
  string FnName = string("binary") + OpChar;

  // Reject redefining any binary operator already known to the parser
  // (covers both built-ins and previously defined custom operators).
  if (IsKnownBinaryOperatorToken(CurTok))
    return LogErrorP(
        (string("Binary operator '") + OpChar + "' is already defined").c_str());

  // Reject cross-arity reuse: if the token is already a unary operator,
  // it cannot also become binary.
  if (IsKnownUnaryOperatorToken(CurTok))
    return LogErrorP((string("Binary operator '") + OpChar +
                      "' conflicts with an existing unary operator").c_str());

  // Reject silent JIT shadowing via a same-named prototype entry.
  if (FunctionProtos.count(FnName))
    return LogErrorP(
        (string("Binary operator '") + OpChar + "' is already defined").c_str());

  getNextToken(); // eat operator char

  // ... parse "(" identifier "," identifier ")" ...

  if (ArgNames.size() != 2)
    return LogErrorP("Binary operator must have exactly two arguments");

  return make_unique<PrototypeAST>(FnName, std::move(ArgNames),
                                   /*IsOperator=*/true, Precedence);
}
```

`IsCustomOpChar` checks `isascii(Tok) && ispunct(Tok) && Tok != '@'` — ASCII punctuation excluding the decorator marker. In the binary path we enforce three checks: binary redefinition (`IsKnownBinaryOperatorToken`), unary/binary conflict (`IsKnownUnaryOperatorToken`), and symbol-name collision (`FunctionProtos`).

`ParseUnaryOpPrototype` follows the same pattern with two additional guards:

```cpp
/// unaryopprototype
///   = customopchar "(" identifier ")"
static unique_ptr<PrototypeAST> ParseUnaryOpPrototype() {
  // ...
  char OpChar = (char)CurTok;
  string FnName = string("unary") + OpChar;

  // Reject redefining any unary operator that is already known to the parser.
  if (IsKnownUnaryOperatorToken(CurTok))
    return LogErrorP(
        (string("Unary operator '") + OpChar + "' is already defined").c_str());

  // Reject cross-arity reuse: if '|' is already binary, it cannot become unary.
  if (IsKnownBinaryOperatorToken(CurTok))
    return LogErrorP((string("Unary operator '") + OpChar +
                      "' conflicts with an existing binary operator").c_str());

  // ... parse "(" identifier ")" ...

  if (ArgNames.size() != 1)
    return LogErrorP("Unary operator must have exactly one argument");

  // Unary operators bind tighter than any binary op — no precedence needed.
  return make_unique<PrototypeAST>(FnName, std::move(ArgNames),
                                   /*IsOperator=*/true, /*Precedence=*/0);
}
```

There is no separate hard-coded "built-in unary operator list" in this parser. Built-in unary `-` is blocked because `KnownUnaryOperators` is seeded with `'-'`, and built-in binary operators are blocked from unary reuse by `IsKnownBinaryOperatorToken`.

### ParseDecoratedDef: Tying It Together

`ParseDecoratedDef` orchestrates the two branches. It reads the decorator, enforces the mandatory newline between decorator and `def`, calls the appropriate prototype parser, then parses the shared `:` / `return` / body structure:

```cpp
/// decorateddef
///   = binarydecorator eols "def" binaryopprototype ":" [ eols ] "return" expression
///   | unarydecorator  eols "def" unaryopprototype  ":" [ eols ] "return" expression
///
/// Called after '@' has been consumed. CurTok is on 'binary' or 'unary'.
static unique_ptr<FunctionAST> ParseDecoratedDef() {
  if (CurTok != tok_binary && CurTok != tok_unary)
    return LogErrorF("Expected 'binary' or 'unary' after '@'");

  bool IsBinary = (CurTok == tok_binary);
  unique_ptr<PrototypeAST> Proto;

  if (IsBinary) {
    unsigned Prec = ParseBinaryDecorator(); // consumes "binary(N)"
    if (!Prec)
      return nullptr;
    if (CurTok != tok_eol)
      return LogErrorF("Expected newline after '@binary(...)' decorator");
    consumeNewlines();
    if (CurTok != tok_def)
      return LogErrorF("Expected 'def' after decorator");
    getNextToken(); // eat 'def'
    Proto = ParseBinaryOpPrototype(Prec);
  } else {
    ParseUnaryDecorator();          // consumes "unary"
    if (CurTok != tok_eol)
      return LogErrorF("Expected newline after '@unary' decorator");
    consumeNewlines();
    if (CurTok != tok_def)
      return LogErrorF("Expected 'def' after decorator");
    getNextToken(); // eat 'def'
    Proto = ParseUnaryOpPrototype();
  }

  if (!Proto)
    return nullptr;

  // Shared body structure — identical to ParseDefinition.
  if (CurTok != ':')
    return LogErrorF("Expected ':' in operator definition");
  getNextToken(); // eat ':'
  consumeNewlines();

  if (CurTok != tok_return)
    return LogErrorF("Expected 'return' in operator body");
  getNextToken(); // eat 'return'

  if (auto E = ParseExpression())
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}
```

The mandatory `eols` between decorator and `def` is a grammar requirement — `@binary(5) def |(x,y): ...` on a single line is rejected. `consumeNewlines()` eats one or more consecutive `tok_eol` tokens, so blank lines between the decorator and the `def` are allowed.

`HandleDecorator` is now a thin wrapper — it calls `ParseDecoratedDef`, checks for trailing tokens, and codegens on success:

```cpp
/// The '@' has already been consumed by MainLoop before calling here.
/// CurTok is on 'binary' or 'unary'. Delegates to ParseDecoratedDef.
static void HandleDecorator() {
  auto FnAST = ParseDecoratedDef();
  if (!FnAST || (CurTok != tok_eol && CurTok != tok_eof)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FnIR = FnAST->codegen()) {
    Log("Parsed a user-defined operator.\n");
    ExitOnErr(TheJIT->addModule(
        ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
    InitializeModuleAndManagers();
  }
}
```

**`ParsePrototype` is unchanged.** Regular function prototypes still parse as `identifier "(" args ")"`. None of the `@binary`/`@unary` logic bleeds into `ParsePrototype` — the separation is clean because `ParseDecoratedDef` calls the dedicated operator prototype parsers directly.

## Parsing Unary Operator Applications

### Unary Minus: A Built-In Special Case

Before dispatching to user-defined unary operators, the parser handles `-` as a dedicated built-in unary operator. `ParseUnaryMinus` eats `-`, parses a full `unaryexpr` operand (so `-!x` works), and builds a `UnaryExprAST` with opcode '-':

```cpp
/// unaryminus
///   = "-" unaryexpr ;
static unique_ptr<ExprAST> ParseUnaryMinus() {
  getNextToken(); // eat '-'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  return make_unique<UnaryExprAST>('-', std::move(Operand));
}
```

During codegen, `UnaryExprAST` treats `-` as a built-in and emits the LLVM IR instruction `fneg`; all other unary opcodes are resolved as user-defined unary<opchar> functions.

### User-Defined Unary Operators

`ParseUnary` is the parser step that decides whether the next thing is a unary operator (like `-x` or `!x`) or a normal primary expression before binary operators are processed.

```cpp
/// unaryexpr
///   = unaryop unaryexpr | primary
///
/// Parsing strategy:
/// 1) Primary starters ('(', letter, digit, multi-character tokens) -> ParsePrimary
/// 2) '-' -> ParseUnaryMinus (built-in)
/// 3) Everything else -> treat as a user-defined unary prefix operator
static unique_ptr<ExprAST> ParseUnary() {
  // Primary starters: fall through to ParsePrimary.
  if (!isascii(CurTok) /* multi-character tokens */ || CurTok == '(' || isalpha(CurTok) || isdigit(CurTok))
    return ParsePrimary();

  // Built-in unary minus.
  if (CurTok == '-')
    return ParseUnaryMinus();

  // ASCII punctuation — treat it as a user-defined unary prefix operator.
  int Opc = CurTok;
  getNextToken(); // eat the operator character
  if (auto Operand = ParseUnary())
    return make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}
```

The recursion in the third branch (`ParseUnary()` calling itself) makes user-defined unary operators right-associative and nestable: `!!x` parses as `!(!(x))`, and `-!x` parses as unary-minus applied to `!x`.

`ParseBinOpRHS` calls `ParseUnary` rather than `ParsePrimary` directly to parse the right-hand side of every binary operator. This makes user-defined unary operators work in all positions: `!a + 1`, `f(!x)`, and `a * !b` all work.

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
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+': return Builder->CreateFAdd(L, R, "addtmp");
  case '-': return Builder->CreateFSub(L, R, "subtmp");
  case '*': return Builder->CreateFMul(L, R, "multmp");
  // ...comparisons...
  default:
    break;
  }

  // Look for a user-defined binary operator function named "binary<op>".
  Function *F = getFunction(std::string("binary") + (char)Op);
  if (!F)
    return LogErrorV("invalid binary operator");

  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}
```

Because user-defined operators lower to regular function calls, operands are evaluated eagerly before the call is emitted. There is no short-circuit behavior unless you encode it explicitly in control flow.

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

The generated IR for `!x` is a regular function call:

```llvm
%unop = call double @unary!(double %x)
```

No special instruction. The JIT compiles the user-supplied body of `unary!` and calls it here.

For built-in unary minus, codegen emits LLVM’s native negate instruction.
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

## What We Built

| What | Change |
|---|---|
| `tok_binary`, `tok_unary` | New lexer tokens for the `binary` and `unary` keywords |
| `KnownUnaryOperators` | New `std::set<int>` tracking reserved and defined unary operators; seeded with `'-'` |
| `FunctionProtos` | Moved earlier (before the parser section) so prototype lookup is available during parse-time validation |
| `PrototypeAST::IsOperator`, `Precedence` | New fields; `isUnaryOp()`, `isBinaryOp()`, `getOperatorName()`, `getBinaryPrecedence()` added |
| `UnaryExprAST` | New AST node for unary operator applications (built-in `-` and user-defined unary ops) |
| `ParseBinaryDecorator()` | Parses `binary(N)`; validates integer precedence ≥ 1; returns `unsigned` (0 = error) |
| `ParseUnaryDecorator()` | Eats `unary`; trivial |
| `IsCustomOpChar()` | Helper: ASCII punctuation, excluding `@` |
| `IsKnownBinaryOperatorToken()` | Helper: looks up token in `BinopPrecedence` |
| `IsKnownUnaryOperatorToken()` | Helper: looks up token in `KnownUnaryOperators` |
| `ParseBinaryOpPrototype()` | Parses binary operator prototype; validates `customopchar`, rejects redefinitions and wrong arity |
| `ParseUnaryOpPrototype()` | Parses unary operator prototype; blocks built-in ops, rejects redefinitions and cross-arity conflicts, checks arity |
| `ParseDecoratedDef()` | Orchestrates the two `decorateddef` branches; enforces mandatory newline between decorator and `def` |
| `HandleDecorator()` | Simplified to delegate to `ParseDecoratedDef()` + error recovery |
| `ParseUnaryMinus()` | Dedicated handler for '-' ; emits `UnaryExprAST('-', operand)` (built-in unary minus), which codegen lowers to LLVM `fneg` |
| `ParseUnary()` | New dispatch point: routes `-` to `ParseUnaryMinus`, primary starters to `ParsePrimary`, everything else to user-defined unary |
| `ParseBinOpRHS()` | Updated to call `ParseUnary()` instead of `ParsePrimary()` for the RHS |
| `ParseExpression()` | Updated to seed with `ParseUnary()` instead of `ParsePrimary()` |
| `PrototypeAST::codegen()` | Registers `BinopPrecedence` for binary ops; inserts into `KnownUnaryOperators` for unary ops |
| `BinaryExprAST::codegen()` | Default case now looks up `binary<op>` via `getFunction` and emits a call |
| `UnaryExprAST::codegen()` | Emits `fneg` for built-in `-`; otherwise looks up `unary<op>` and emits a call |
| `test/mandel.pyxc` | Density-shaded Mandelbrot using four custom operators: `@unary def !`, `@binary(1) def ;`, `@binary(5) def \|`, `@binary(6) def &` |

## Known Limitations

- **Unary/Binary distinction is enforced.** A punctuation character can be used for only one custom operator definition: either unary or binary, not both. `ParseUnaryOpPrototype` rejects symbols already known as binary operators, and `ParseBinaryOpPrototype` rejects symbols already known as unary operators.

- **No operator removal or redefinition.** Once a custom operator is registered in `BinopPrecedence` or `KnownUnaryOperators`, there is no mechanism to remove or reassign it within a session.

- **Precedence is fixed at definition time.** `BinopPrecedence` maps the operator character to a single precedence value. There is no way to define two operators sharing the same character with different precedences in different contexts.

- **All values are `double`.** No integer or boolean types. Comparisons return `1.0` for true and `0.0` for false. User-defined operators work within this constraint.

## What's Next

Chapter 10 replaces the single-expression function body with a statement block, allowing multi-line function bodies without the sequencing operator workaround.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

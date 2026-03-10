---
description: "Add comparison operators, if/else expressions, and for loops — then use them to render the Mandelbrot set in ASCII."
---
# 8. Pyxc: Control Flow: if, else, and for

## Where We Are

[Chapter 7](chapter-07.md) added file input mode. The language itself still has only arithmetic and function calls — no way to branch, no way to loop, no way to compare two values. This chapter adds all three.

Comparison operators produce `1.0` for true and `0.0` for false:

<!-- code-merge:start -->
```python
ready> 1 < 2
```
```bash
Parsed a top-level expression.
Evaluated to 1.000000
```
```python
ready> 3 != 3
```
```bash
Parsed a top-level expression.
Evaluated to 0.000000
```
<!-- code-merge:end -->

`if` is an expression that produces a value:

<!-- code-merge:start -->
```python
ready> def maxval(a, b): return if a > b: a else: b
```
```bash
Parsed a function definition.
```
```python
ready> maxval(10, 5)
```
```bash
Parsed a top-level expression.
Evaluated to 10.000000
```
<!-- code-merge:end -->

`for` is an expression that repeats a body expression, always producing `0.0`:

<!-- code-merge:start -->
```python
ready> extern def printd(x)
```
```bash
Parsed an extern.
```
```python
ready> for i = 1, i <= 3, 1: printd(i)
```
```bash
1.000000
2.000000
3.000000
Parsed a top-level expression.
Evaluated to 0.000000
```
<!-- code-merge:end -->

As a payoff, the Mandelbrot set rendered in ASCII — written entirely in Pyxc.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-08
```

## Grammar

Chapter 8 adds two new primary expression forms (`conditionalexpr` and `forexpr`) and six binary comparison operators to the grammar.

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = definition | external | toplevelexpr ;
definition      = "def" prototype ":" [ eols ] "return" expression ;
external        = "extern" "def" prototype ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ identifier { "," identifier } ] ")" ;
conditionalexpr = "if" expression ":" [ eols ] expression [ eols ] "else" ":" [ eols ] expression ;
forexpr         = "for" identifier "=" expression "," expression "," expression ":" [ eols ] expression ;
expression      = primary binoprhs ;
binoprhs        = { binaryop primary } ;
primary         = identifierexpr | numberexpr | parenexpr
                | conditionalexpr | forexpr ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
numberexpr      = number ;
parenexpr       = "(" expression ")" ;
binaryop        = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
identifier      = (letter | "_") { letter | digit | "_" } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
```

## New Tokens

Six new token enums:

```cpp
// comparison operators
tok_eq  = -8,   // ==
tok_neq = -9,   // !=
tok_leq = -10,  // <=
tok_geq = -11,  // >=

// control flow
tok_if   = -12,
tok_else = -13,

// loops
tok_for = -15,
```

`tok_if`, `tok_else`, and `tok_for` are keywords added to the keyword table. The comparison tokens are returned by the lexer when it sees two-character sequences.

## Comparison Operators

### Lexer: Two-Character Tokens

The lexer reads one character at a time. Recognizing `==` means seeing `=` first, then deciding whether the next character is also `=`. A `peek()` helper reads one character and immediately unreads it — it looks ahead without consuming:

```cpp
static int peek() {
  int c = fgetc(Input);
  if (c != EOF)
    ungetc(c, Input);
  return c;
}
```

Each two-character operator follows the same pattern in `gettok()`:

```cpp
if (LastChar == '=') {
  int Tok = (peek() == '=') ? (advance(), tok_eq) : '=';
  LastChar = advance();
  return Tok;
}
```

If the next character is also `=`, consume it with `advance()` and return `tok_eq`. Otherwise return the bare `=`. Either way, call `advance()` at the end to load the next `LastChar`. The same pattern handles `!`, `<`, and `>`.

### Parser: BinopPrecedence Keyed on int

In earlier chapters `BinopPrecedence` used `char` keys. Named token enums are negative integers, which don't fit in a `char`. The key type is now `int`:

```cpp
static map<int, int> BinopPrecedence = {
    {tok_eq, 10},  // ==
    {tok_neq, 10}, // !=
    {tok_leq, 10}, // <=
    {tok_geq, 10}, // >=
    {'<', 10},     // <
    {'>', 10},     // >
    {'+', 20},     // +
    {'-', 20},     // -
    {'*', 40},     // *
};
```

All six comparison operators share precedence 10 — they bind equally tightly and are left-associative. `GetTokPrecedence` is a simple map lookup:

```cpp
static int GetTokPrecedence() {
  auto It = BinopPrecedence.find(CurTok);
  if (It == BinopPrecedence.end() || It->second <= 0)
    return -1;
  return It->second;
}
```

`BinaryExprAST::Op` also changes from `char` to `int` so it can store negative token values without truncation.

### Codegen: FCmpO* + UIToFP

All six comparison operators follow the same two-step pattern in `BinaryExprAST::codegen`:

```cpp
case tok_eq:
  L = Builder->CreateFCmpOEQ(L, R, "cmptmp");
  return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
```

`CreateFCmpOEQ` produces an `i1` — LLVM's one-bit boolean. Since Pyxc represents everything as `double`, `CreateUIToFP` widens it: `false → 0.0`, `true → 1.0`. This double boolean is what flows into `if` conditions and arithmetic expressions.

The `O` prefix selects **ordered** floating-point comparison: if either operand is NaN, the comparison returns false (including `!=`). Unordered variants (`U*`) would return true for many NaN comparisons, which is rarely what you want.

## if/else Expressions

`if` in Pyxc is an **expression** — it produces a value, not just a side effect:

```python
if condition: then_expr else: else_expr
```

Because `if` is a primary, it can appear anywhere an expression can — as a function return value, a loop body, a function argument, or nested inside another `if`.

Both branches are mandatory. Without an `else`, what value would the expression produce when the condition is false?

One syntactic note: every function definition currently requires the `return` keyword — it is part of the grammar (`def name(args): return expression`), not a statement. A later chapter replaces the single-expression body with a block of statements where `return` can appear anywhere.

### Parsing

`ParseIfExpr` eats `if`, parses the condition, expects `:`, allows newlines, then parses the then-branch. It then allows newlines before `else`, expects `else:`, allows more newlines, and parses the else-branch:

```cpp
static unique_ptr<ExprAST> ParseIfExpr() {
  getNextToken(); // eat 'if'

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError("Expected ':' after if condition");
  getNextToken(); // eat ':'

  consumeNewlines(); // allow body on next line

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  consumeNewlines(); // allow 'else' on next line

  if (CurTok != tok_else)
    return LogError("Expected 'else' in if expression");
  getNextToken(); // eat 'else'

  if (CurTok != ':')
    return LogError("Expected ':' after else");
  getNextToken(); // eat ':'

  consumeNewlines(); // allow body on next line

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return make_unique<IfExprAST>(std::move(Cond), std::move(Then),
                                std::move(Else));
}
```

`consumeNewlines()` eats one or more consecutive `tok_eol` tokens, so both inline and multi-line forms are accepted:

```python
if a > b: a else: b                    # all on one line

if a > b:                              # multi-line
    a
else:
    b
```

### Codegen: Building the then / else / join Blocks

Codegen for an `if` expression has three jobs:

1. Evaluate the condition.
2. Run exactly one of the two branches.
3. Continue afterward with the value produced by the branch that ran.

LLVM builds this out of **basic blocks**: straight-line chunks of instructions
that end in a jump. For an `if`, we need one block for the `then` path, one for
the `else` path, and one final block where both paths meet again.

For:

```python
if a > b: a else: b
```

the generated block layout looks like this:

```text
         entry
           │
      if (a > b)?
      ┌────┴────┐
 true ▼         ▼ false
     then      else
       └────┬────┘
            ▼
          ifcont
```

`ifcont` is the block where execution continues after either branch.

There is one extra problem: both branches produce a value, and after they
rejoin we need one name for "the value from the branch that actually ran." LLVM
writes that choice with a **PHI node**:

```llvm
ifcont:
  %iftmp = phi double [ %a, %then ], [ %b, %else ]
```

Read that as: "if we arrived here from `then`, use `%a`; if we arrived here
from `else`, use `%b`."

`IfExprAST::codegen` builds this shape in five steps. We'll trace through
`if a > b: a else: b` (`maxval`).

**Step 1 — Generate the condition in the current block.**

First we generate code for the condition expression:

```cpp
Value *CondV = Cond->codegen();
```

In Pyxc, condition expressions produce a `double`, not an LLVM boolean.
Before we can branch, we must turn that `double` into an `i1` value that LLVM
can use for `br i1 ...`.

We do that by comparing the condition value against `0.0`:

```cpp
CondV = Builder->CreateFCmpONE(
    CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");
```

This means: treat the condition as true if it is not equal to `0.0`.

At this point the builder is still inserting instructions into the current
block, which is the block that was already active before the `if`.

**Step 2 — Create the `then`, `else`, and join blocks.**

```cpp
BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);
Builder->CreateCondBr(CondV, ThenBB, ElseBB);
```

All three blocks are attached to the function immediately. `CreateCondBr` does
not fill either branch block; it only finishes the current block with a
conditional jump to `then` or `else`.

So after this line:

- the current block (`entry`) now ends with `br i1 ...`
- `then`, `else`, and `ifcont` exist
- those three blocks still contain no instructions

IR so far — `entry` is complete, new blocks are placeholders:

```llvm
define double @maxval(double %a, double %b) {
entry:
  %cmptmp  = fcmp ogt double %a, %b         ; a > b → i1
  %booltmp = uitofp i1 %cmptmp to double    ; widen to double (0.0 or 1.0)
  %ifcond  = fcmp one double %booltmp, 0.0  ; back to i1: non-zero?
  br i1 %ifcond, label %then, label %else

then:    ; (empty)
else:    ; (empty)
ifcont:  ; (empty)
}
```

**Step 3 — Move the builder into `then` and generate that branch.**

```cpp
Builder->SetInsertPoint(ThenBB);
Value *ThenV = Then->codegen();
Builder->CreateBr(MergeBB);
ThenBB = Builder->GetInsertBlock(); // re-capture: nested ifs shift the cursor
```

`SetInsertPoint` is the important move here: it tells LLVM, "append the next
instructions into the `then` block."

After `Then->codegen()` finishes, we emit an unconditional branch to `ifcont`
so the `then` path rejoins the `else` path.

The re-capture on the last line matters because nested control flow can create
more blocks and move the builder. We want the block where the `then` path
actually ended, not just the block where it started.

```llvm
then:                           ; preds = %entry
  br label %ifcont              ; then-value is %a (a parameter — no instruction needed)
```

**Step 4 — Do the same for `else`.**

The pattern is the same:

- move the builder there
- generate the `else` expression
- branch to `ifcont`
- re-capture the block where the `else` path actually ended

```llvm
else:                           ; preds = %entry
  br label %ifcont              ; else-value is %b
```

**Step 5 — Fill the join block and choose the final value.**

```cpp
Builder->SetInsertPoint(MergeBB);
PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
PN->addIncoming(ThenV, ThenBB);
PN->addIncoming(ElseV, ElseBB);
```

Now we are in the block where both branches meet again. The PHI node gives the
whole `if` expression one result value:

- if control arrived from the `then` side, use `ThenV`
- if control arrived from the `else` side, use `ElseV`

That is why the re-captures in steps 3 and 4 matter: the PHI node needs the
actual block each branch jumped from.

#### Why Re-Capture `ThenBB` and `ElseBB`?

This only matters when one branch contains nested control flow.

Suppose the outer `then` branch is itself another `if`:

```python
if outer_cond:
    if inner_cond:
        a
    else:
        b
else:
    c
```

The inner `if` produces its own result first, and then the outer `if` uses
that result as the value of its `then` branch.

A simplified IR shape looks like this:

```llvm
entry:
  br i1 %outer_cond, label %outer_then, label %outer_else

outer_then:
  br i1 %inner_cond, label %inner_then, label %inner_else

inner_then:
  br label %inner_join

inner_else:
  br label %inner_join

inner_join:
  %inner = phi double [ %a, %inner_then ], [ %b, %inner_else ]
  br label %outer_join

outer_else:
  br label %outer_join

outer_join:
  %outer = phi double [ %inner, %inner_join ], [ %c, %outer_else ]
```

Notice what happened:

- the outer `then` branch started in `outer_then`
- but after generating the nested `if`, it actually ends in `inner_join`

So the outer PHI must use `inner_join`, not `outer_then`:

```llvm
%outer = phi double [ %inner, %inner_join ], [ %c, %outer_else ]
```

That is why we re-capture with:

```cpp
ThenBB = Builder->GetInsertBlock();
```

After nested codegen, `ThenBB` must mean "the block where the outer `then`
path actually finished," because that is the predecessor block the outer PHI
needs.

**Full unoptimized IR for `maxval`:**

```llvm
define double @maxval(double %a, double %b) {
entry:
  %cmptmp  = fcmp ogt double %a, %b
  %booltmp = uitofp i1 %cmptmp to double
  %ifcond  = fcmp one double %booltmp, 0.0
  br i1 %ifcond, label %then, label %else

then:                                         ; preds = %entry
  br label %ifcont

else:                                         ; preds = %entry
  br label %ifcont

ifcont:                                       ; preds = %then, %else
  %iftmp = phi double [ %a, %then ], [ %b, %else ]
  ret double %iftmp
}
```

### The Optimizer Collapses Simple ifs to select

When both branches are side-effect-free, the optimizer sees that no code actually runs in `then` or `else` — they are just routing. It replaces the whole three-block structure with a single `select` instruction:

```llvm
define double @maxval(double %a, double %b) {
entry:
  %cmptmp = fcmp ogt double %a, %b
  %a.b = select i1 %cmptmp, double %a, double %b
  ret double %a.b
}
```

`select i1 cond, T true_val, T false_val` is LLVM's ternary — no branches, no blocks. The optimizer also removes the redundant `uitofp` / `fcmp one` round-trip (comparison → double → back to `i1`) and uses `%cmptmp` directly.

Functions where the branches make calls (`printd`, `putchard`) keep the full three-block structure because those calls must actually run in one branch and not the other.

## for Loop Expressions

The `for` expression repeats a body expression while a condition holds:

```python
for var = start, condition, step: body
```

The loop runs while `condition` is non-zero. `var` is introduced by the `for` and is in scope for `condition`, `step`, and `body`. The body's return value is discarded each iteration. The expression as a whole always produces `0.0` — useful when you want to call a function repeatedly and don't care what it returns.

### Parsing

`ParseForExpr` reads the variable name, `=`, start, `,`, condition, `,`, step, `:`, then the body. Newlines between `:` and the body are consumed by `consumeNewlines()`, allowing the body on the next line:

```cpp
static unique_ptr<ExprAST> ParseForExpr() {
  getNextToken(); // eat 'for'

  if (CurTok != tok_identifier)
    return LogError("Expected identifier after 'for'");
  string VarName = IdentifierStr;
  getNextToken(); // eat identifier

  if (CurTok != '=')
    return LogError("Expected '=' after for variable");
  getNextToken(); // eat '='

  auto Start = ParseExpression();
  if (!Start) return nullptr;

  if (CurTok != ',') return LogError("Expected ',' after for start value");
  getNextToken(); // eat ','

  auto Cond = ParseExpression();
  if (!Cond) return nullptr;

  if (CurTok != ',') return LogError("Expected ',' after for condition");
  getNextToken(); // eat ','

  auto Step = ParseExpression();
  if (!Step) return nullptr;

  if (CurTok != ':') return LogError("Expected ':' after for step");
  getNextToken(); // eat ':'

  consumeNewlines(); // allow body on next line

  auto Body = ParseExpression();
  if (!Body) return nullptr;

  return make_unique<ForExprAST>(VarName, std::move(Start), std::move(Cond),
                                 std::move(Step), std::move(Body));
}
```

### Codegen: Check-at-Top Loop

The loop compiles to four blocks. We'll trace through `for i = 1, i <= 3, 1: printd(i)`.

**Step 1 — Evaluate start in the preheader and jump to the condition block.**

```cpp
Value *StartVal = Start->codegen();           // 1.0
BasicBlock *PreheaderBB = Builder->GetInsertBlock();
BasicBlock *CondBB  = BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
BasicBlock *BodyBB  = BasicBlock::Create(*TheContext, "loop_body");
BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "after_loop");
Builder->CreateBr(CondBB);
```

**Step 2 — Build the condition block: PHI node + branch.**

```cpp
Builder->SetInsertPoint(CondBB);
PHINode *Variable = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
Variable->addIncoming(StartVal, PreheaderBB);   // first-iteration value
```

The PHI node is created with only one incoming for now — the preheader. The back-edge (from the loop body) is added after body codegen. Then evaluate the condition and branch:

```cpp
Value *CondV = Cond->codegen();
Value *LoopCond = Builder->CreateFCmpONE(
    CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
Builder->CreateCondBr(LoopCond, BodyBB, AfterBB);
```

**Step 3 — Fill the body block.**

```cpp
TheFunction->insert(TheFunction->end(), BodyBB);
Builder->SetInsertPoint(BodyBB);
Body->codegen();                                // return value discarded
Value *StepVal = Step->codegen();               // 1.0
Value *NextVar  = Builder->CreateFAdd(Variable, StepVal, "nextvar");
```

Then re-capture the insert block (nested ifs inside the body can shift the cursor) and close the back-edge:

```cpp
BasicBlock *BodyEndBB = Builder->GetInsertBlock();
Variable->addIncoming(NextVar, BodyEndBB);      // complete the PHI
Builder->CreateBr(CondBB);
```

**Step 4 — After-loop block returns `0.0`.**

```cpp
TheFunction->insert(TheFunction->end(), AfterBB);
Builder->SetInsertPoint(AfterBB);
return ConstantFP::get(*TheContext, APFloat(0.0));
```

**Full IR for `for i = 1, i <= 3, 1: printd(i)` as a top-level expression:**

```llvm
define double @__anon_expr() {
entry:
  br label %loop_cond

loop_cond:                                    ; preds = %entry, %loop_body
  %i = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop_body ]
  %cmptmp  = fcmp ole double %i, 3.000000e+00
  %booltmp = uitofp i1 %cmptmp to double
  %loopcond = fcmp one double %booltmp, 0.000000e+00
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:                                    ; preds = %loop_cond
  %calltmp = call double @printd(double %i)
  %nextvar = fadd double %i, 1.000000e+00
  br label %loop_cond

after_loop:                                   ; preds = %loop_cond
  ret double 0.000000e+00
}
```

CFG:

```
      entry
        │
        ▼
    loop_cond ◄──────────┐
        │                │
   ┌────┴────┐           │
   ▼         ▼           │
loop_body  after_loop    │
   │        ret 0.0      │
   └─────────────────────┘
   (i = i + step)
```

The condition is checked **before** the first iteration. If false on entry, the body never runs. The `loop_cond` block has two predecessors — `entry` (on the first pass) and `loop_body` (on subsequent passes) — which is exactly what the PHI node encodes.

### Variable Shadowing

If an outer function parameter has the same name as the loop variable, the loop variable takes precedence inside the loop. The outer binding is saved before the loop and restored in `after_loop`:

```cpp
Value *OldVal = NamedValues[VarName];
NamedValues[VarName] = Variable;
// ... condition, step, body codegen ...
if (OldVal)
  NamedValues[VarName] = OldVal;
else
  NamedValues.erase(VarName);
```

## Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/pyxc
```

The binary runs as an interactive REPL when given no file argument. Press `Ctrl-D` to exit.

## Try It

### Comparison operators

<!-- code-merge:start -->
```python
ready> 1 < 2
```
```bash
Parsed a top-level expression.
Evaluated to 1.000000
```
```python
ready> 3 != 3
```
```bash
Parsed a top-level expression.
Evaluated to 0.000000
```
```python
ready> 2 <= 2
```
```bash
Parsed a top-level expression.
Evaluated to 1.000000
```
```python
ready>
```
<!-- code-merge:end -->

### if/else expression

<!-- code-merge:start -->
```python
ready> def maxval(a, b): return if a > b: a else: b
```
```bash
Parsed a function definition.
```
```python
ready> maxval(10, 5)
```
```bash
Parsed a top-level expression.
Evaluated to 10.000000
```
```python
ready> maxval(3, 8)
```
```bash
Parsed a top-level expression.
Evaluated to 8.000000
```
```python
ready>
```
<!-- code-merge:end -->

### for loop

The loop always produces `0.0`. The body runs once per iteration and its return value is thrown away — here the body is a `printd` call, so the observable result is the printing.

<!-- code-merge:start -->
```python
ready> extern def printd(x)
```
```bash
Parsed an extern.
```
```python
ready> for i = 1, i <= 3, 1: printd(i)
```
```bash
1.000000
2.000000
3.000000
Parsed a top-level expression.
Evaluated to 0.000000
```
```python
ready>
```
<!-- code-merge:end -->

## The Mandelbrot Set

With comparisons, `if`/`else`, and `for`, Pyxc is expressive enough to render the Mandelbrot set. The Mandelbrot set is the set of complex numbers `c` for which the iteration `z = z² + c` (starting from `z = 0`) does not diverge to infinity.

```python
# test/mandel.pyxc
extern def putchard(x)

def mandelconverger(real, imag, iters, creal, cimag):
    return if iters > 255: iters
           else: if (real * real + imag * imag) > 4: iters
                 else: mandelconverger(real * real - imag * imag + creal, 2 * real * imag + cimag, iters + 1, creal, cimag)

def mandelconverge(real, imag):
    return mandelconverger(real, imag, 0, real, imag)

def mandelrow(xmin, xmax, xstep, y):
    return for x = xmin, x < xmax, xstep:
               putchard(if mandelconverge(x, y) > 255: 32 else: 42)

def mandelhelp(xmin, xmax, xstep, ymin, ymax, ystep):
    return for y = ymin, y < ymax, ystep:
               mandelrow(xmin, xmax, xstep, y) + putchard(10)

def mandel(realstart, imagstart, realmag, imagmag):
    return mandelhelp(realstart, realstart + realmag * 78, realmag, imagstart, imagstart + imagmag * 40, imagmag)

mandel(0 - 2.3, 0 - 1.3, 0.05, 0.07)
```

**Line breaks.** The parser only allows newlines in specific positions — after `:` in `def`, `if`, and `for` bodies. A newline anywhere else (inside a function argument list, mid-expression) is a parse error. This is why `mandelconverge`'s nested `if`/`else` chain can span lines (each `else:` starts a new allowed position) but `mandel`'s long argument list must stay on a single line.

**Unary minus.** Pyxc has no unary minus yet — `-2.3` would be parsed as the binary operator `-` applied to nothing, which is an error. The workaround is `0 - 2.3`: a fully-formed binary subtraction that the optimizer collapses to the literal `-2.3` with no extra instructions emitted. Chapter 9 adds unary-expression parsing and built-in unary minus support.

**`mandelconverger`** counts iterations upward from `iters = 0`. It stops when `iters` exceeds 255 (the iteration limit — point is likely inside the set) or when `real² + imag²` exceeds 4 (magnitude exceeded 2 — point is diverging). It returns the iteration count at which it stopped. **`mandelconverge`** is a thin wrapper that starts the recursion at `iters = 0`.

**`mandelrow`** drives the x-axis for a single row. For each point it calls `mandelconverge`; a return value `> 255` means the iteration limit was reached without diverging — point is inside the set, print space (ASCII 32). Any smaller value means it escaped — print `*` (ASCII 42).

**`mandelhelp`** drives the y-axis. The outer `for y` body is `mandelrow(...) + putchard(10)` — the `+` sequences both calls, printing the row then a newline (ASCII 10). The return value `0.0 + 0.0` is discarded.

**`mandel`** maps row and column counts to complex-plane coordinates.

Run it:

```bash
./build/pyxc test/mandel.pyxc
```

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

The entire renderer — iteration, branching, output — is Pyxc code. The only runtime function provided by the host is `putchard`, which writes one ASCII character to `stderr`.

## What We Built

| What | Change |
|---|---|
| `tok_eq`, `tok_neq`, `tok_leq`, `tok_geq` | New token enums for `==`, `!=`, `<=`, `>=` |
| `tok_if`, `tok_else`, `tok_for` | Keyword tokens; added to the keyword table |
| `peek()` | Reads one character and unreads it; used by two-character operator branches in `gettok()` |
| `BinopPrecedence` key type `int` | Accommodates both ASCII char operators and named token enums |
| `GetTokPrecedence` map lookup | Replaces old `!isascii` guard so named tokens participate in binary expressions |
| `BinaryExprAST::Op` type `int` | Stores negative token values without truncation |
| `FCmpO*` + `UIToFP` | Ordered float comparison → `i1`, widened to `double`; NaN always evaluates false |
| `IfExprAST` / `ParseIfExpr` | `if condition: then else: else` expression with mandatory else |
| Three blocks + PHI | `then`, `else`, `ifcont`; PHI merges the two branch values |
| `ForExprAST` / `ParseForExpr` | `for var = start, cond, step: body` expression producing `0.0` |
| `loop_cond` / `loop_body` / `after_loop` | Check-at-top loop; PHI merges start and back-edge values |
| Variable shadowing + restore | Loop variable overrides outer binding; outer is restored in `after_loop` |
| `consumeNewlines()` | Eats consecutive `tok_eol` tokens; used after `:` to allow bodies on the next line |

## Known Limitations

- **No block bodies.** Function bodies and loop bodies are single expressions. Multi-statement sequencing currently uses an arithmetic workaround (`a + b`) only when both subexpressions are side-effect calls that return `0.0`; otherwise `+` changes the value. Chapter 9 adds a dedicated sequencing operator.
- **No mutable local variables.** `NamedValues` holds only function parameters. `alloca`/`store`/`load` and `mem2reg` come in a later chapter.
- **No unary minus.** `-x` is not valid syntax; write `0 - x` instead. Chapter 9 adds unary-expression parsing and built-in unary minus support.
- **No short-circuit operators.** There are no built-in `&&`/`||` operators in this chapter. Any boolean composition must be expressed with nested `if` expressions.
- **No `break` or `continue`.** Early exit from loops requires a different codegen strategy not yet implemented.

## What's Next

Chapter 9 adds user-defined operators via Python-style decorators — `@binary(precedence)` and `@unary` — and also introduces unary-expression parsing with built-in unary minus, so `-x` finally works. The chapter payoff is a richer Mandelbrot renderer with density shading and a clean sequencing operator.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

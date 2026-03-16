---
description: "Add comparison operators, if/else expressions, and for loops — then use them to render the Mandelbrot set in ASCII."
---
# 8. Pyxc: Control Flow: if, else, and for

## Where We Are

[Chapter 7](chapter-07.md) added file input mode. The language itself still has only basic arithmetic, and function calls — no way to branch, no way to loop, no way to compare two values. This chapter adds all three.

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
ready> def absdiff(a, b): return if a > b: a - b else: b - a
```
```bash
Parsed a function definition.
```
```python
ready> absdiff(10, 5)
```
```bash
Parsed a top-level expression.
Evaluated to 5.000000
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
Parsed a top-level expression.
1.000000
2.000000
3.000000
Evaluated to 0.000000
```
<!-- code-merge:end -->

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-08
```

## Grammar

Chapter 8 adds two new primary expression forms (`ifexpr` and `forexpr`) and six binary comparison operators to the grammar.

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = definition | external | toplevelexpr ;
definition      = "def" prototype ":" [ eols ] "return" expression ;
external        = "extern" "def" prototype ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ identifier { "," identifier } ] ")" ;
ifexpr          = "if" expression ":" [ eols ] expression [ eols ] "else" ":" [ eols ] expression ;
forexpr         = "for" identifier "=" expression "," expression "," expression ":" [ eols ] expression ;
expression      = primary binoprhs ;
binoprhs        = { binaryop primary } ;
primary         = identifierexpr | numberexpr | parenexpr
                | ifexpr | forexpr ;
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
enum Token {
...
// comparison operators
tok_eq  = -8,   // ==
tok_neq = -9,   // !=
tok_leq = -10,  // <=
tok_geq = -11,  // >=

// control flow
tok_if   = -12,
tok_else = -13,

...

// loops
tok_for = -15,
```

`tok_if`, `tok_else`, and `tok_for` are keywords added to the `Keywords` map. The comparison tokens are returned by the lexer when it sees two-character sequences.

## Comparison Operators

### Lexer: Two-Character Tokens

The lexer reads one character at a time. Recognizing `==` means seeing `=` first, then deciding whether the next character is also `=` and needs to be consumed. A `peek()` helper reads one character and immediately unreads it — reading ahead without consuming:

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

If the next character is also `=`, consume it with `advance()` and return `tok_eq`. Otherwise return the bare `=`. Either way, call `advance()` at the end to preload `LastChar` for the next `gettok()` call. The same pattern handles `!`, `<`, and `>`.

### Parser: BinopPrecedence Keyed on int

In earlier chapters `BinopPrecedence` used `char` keys. Named token enums are negative integers (`tok_eq` = -10 for example), which don't fit in a `char`. We extend the keytype to `int`:

```cpp
static map<int /* changed from char to int */, int> BinopPrecedence = {
    {tok_eq, 10}, {tok_neq, 10}, {tok_leq, 10}, {tok_geq, 10},  // new
    {'<', 10}, {'>', 10},
    // ... plus '+', '-', '*' from before ...
};
```

All six comparison operators share precedence `10` — they bind equally tightly. 

`BinaryExprAST::Op` also changes from `char` to `int` so it can store negative token values without truncation.

```cpp
class BinaryExprAST : public ExprAST {
  int /* used to be char */ Op; 
  ...
```

### Comparison Codegen

Pyxc comparison operators like `==`, `!=`, `<`, and `>` lower to LLVM's [fcmp](https://llvm.org/docs/LangRef.html#fcmp-instruction)
instruction.

For example, the `==` case in `BinaryExprAST::codegen` is:

```cpp
case tok_eq:
  L = Builder->CreateFCmpOEQ(L, R, "cmptmp");  
```

which produces:

```llvm
%cmptmp = fcmp oeq double %L, %R
```

The predicate — `oeq` here — encodes the comparison (`eq`, `lt`, `le`, …) and one more thing: how to handle `NaN`. That is the only difference between the `o*` and `u*` families. You could write it by hand (pseudocode — `and` arrives in [chapter 9 : User-Defined operators](chapter-09.md) as `&`):

```python
extern def is_nan(x)
def cmp_lt(a, b, ordered):
    if !is_nan(a) and !is_nan(b):
        return a < b
    else:
        return !ordered   # ordered -> false, unordered -> true
```

LLVM bakes that choice into the predicate, so you get a single instruction that already knows how to treat NaN.

The names come from numeric order. Real numbers can be placed on a number line — they are *ordered*. NaN cannot, so any comparison involving NaN is *unordered*. An ordered predicate (`o*`) returns `false` in that case; an unordered predicate (`u*`) returns `true`.

Pyxc follows C's behaviour:

- `==`, `<`, `<=`, `>`, and `>=` use ordered predicates — NaN comparisons return `false`
- `!=` uses unordered not-equal (`une`) — so `x != NaN` is `true`, matching IEEE 754

```cpp
case tok_neq:
  L = Builder->CreateFCmpUNE(L, R, "cmptmp");
```

which produces:

```llvm
%cmptmp = fcmp une double %L, %R
```

If you want an explicit NaN test in IR, you can use `fcmp uno`. It returns true if either operand is NaN:

```llvm
%has_nan = fcmp uno double %L, %R
```

#### Converting `i1` Back to `double`

`fcmp` produces an `i1` — LLVM's one-bit boolean (`false` or `true`). But Pyxc does not have a separate boolean type. Comparison results are ordinary numbers in the language, so we widen that `i1` back to `double`:

```cpp
// CreateUIToFP (Unsigned Int -> Floating Point)
return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
```

which produces:

```llvm
%booltmp = uitofp i1 %cmptmp to double
```

This gives Pyxc its usual comparison result convention: `false → 0.0`, `true → 1.0`. That value is what later flows into `if` conditions and arithmetic expressions. 

## if/else Expressions

In Pyxc, `if` is an expression: it evaluates to a value.

```python
if condition: then_expr else: else_expr
```

That means you can use an `if` anywhere an expression is allowed: as part of a
larger expression, as a function argument, as a loop body, or nested inside
another `if`.

An `if` expression must always have both branches, because it needs to produce
a value whether the condition is true or false.

This can look a little unusual in function bodies. Since functions still
consist of a single expression in this chapter, you write things like:

```python
def absdiff(a, b): return if a > b: a - b else: b - a
```

Later chapters introduce statement blocks, which gives `if` a more familiar
statement-like role inside function bodies.

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
if a > b: a - b else: b - a            # all on one line

if a > b:                              # multi-line
    a - b
else:
    b - a
```

### Codegen: Building the then / else / join Blocks

Codegen for an `if` expression has three jobs:

1. Evaluate the condition.
2. Run exactly one of the two branches.
3. Continue afterward with the value produced by the branch that ran.

For an `if`, we need one block for the `then` path, one for
the `else` path, and one final block where both paths meet again.

We will keep using the same example function from above:

```python
def absdiff(a, b): return if a > b: a - b else: b - a
```

Inside that function, the `if` expression is:

```python
if a > b: a - b else: b - a
```

The generated block layout looks like this:

```text
                entry
                  │
             if (a > b)?
          ┌───────┴────────┐
     true ▼                ▼ false
 then: %subtmp = a-b   else: %subtmp1 = b-a
          └───────┬────────┘
                  ▼
                ifcont
```

Here `entry` is the current block, `then` and `else` are two branch blocks, and `ifcont` is the block where execution continues after either branch.

`IfExprAST::codegen` builds this shape in five steps. We will trace the body of
`absdiff`.

At the LLVM level, we are filling in this function body:

```llvm
define double @absdiff(double %a, double %b) {
entry:
  ...
}
```

**Step 1 — Generate the condition in the current block.**

First we generate code for the condition expression:

```cpp
Value *CondV = Cond->codegen();
```

For `absdiff`, `Cond->codegen()` generates code for `a > b`. 

```cpp
case '>':
  L = Builder->CreateFCmpOGT(L, R, "cmptmp");
  return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
```

That produces:

```llvm
define double @absdiff(double %a, double %b) {
entry:
  %cmptmp  = fcmp ogt double %a, %b
  %booltmp = uitofp i1 %cmptmp to double
}
```

`Cond->codegen()` gives us a `double`, because Pyxc represents booleans as
`0.0` or `1.0`. LLVM branches need an `i1`, so before we can branch we must
turn that `double` back into an `i1`.

We do that by comparing the condition value against `0.0`:

```cpp
CondV = Builder->CreateFCmpONE(
    CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");
```

This means: treat the condition as true if it is not equal to `0.0`.

The current block now looks like this:

```llvm
define double @absdiff(double %a, double %b) {
entry:
  %cmptmp  = fcmp ogt double %a, %b
  %booltmp = uitofp i1 %cmptmp to double
  %ifcond  = fcmp one double %booltmp, 0.0
}
```

At this point the builder is still inserting instructions into the current
block, which is the block that was already active before the `if`.

**Step 2 — Create the `then`, `else`, and join blocks.**

```cpp
BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);
Builder->CreateCondBr(CondV, ThenBB, ElseBB);
```

All three blocks are attached to the function immediately. `CreateCondBr` finishes the current block with a conditional jump — in LLVM, every basic block must end with a *terminator instruction* (LLVM will reject IR where a block has no terminator or has instructions after one). In IR that looks like:

```llvm
br i1 %ifcond, label %then, label %else
```

Check `%ifcond`; jump to `%then` if true, `%else` if false.

We now have:

```llvm
define double @absdiff(double %a, double %b) {
entry:
  %cmptmp  = fcmp ogt double %a, %b
  %booltmp = uitofp i1 %cmptmp to double
  %ifcond  = fcmp one double %booltmp, 0.0
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
```

`SetInsertPoint` is the important move here: it tells LLVM, "append the next
instructions into the `then` block."

After `Then->codegen()` finishes, we emit an unconditional branch to `ifcont`
so the `then` path rejoins the `else` path. 

```llvm
then:                           ; reached when the condition is true
  %subtmp = fsub double %a, %b
  br label %ifcont
```

Finally, we update `ThenBB` so it points to the block where the `then` path
actually finished.

```cpp
// Update ThenBB to the block where the then-path actually ended.
// This matters for nested control flow; explained just below.
ThenBB = Builder->GetInsertBlock();
```

This matters because nested control flow can create more blocks and move the
builder cursor. We want the block where the `then` path ended, not the block where it
started. This only matters for nested `if` expressions; we’ll look at that
case a little later in this chapter.

**Step 4 — Do the same for `else`.**

```cpp
Builder->SetInsertPoint(ElseBB);
Value *ElseV = Else->codegen();
Builder->CreateBr(MergeBB);
ElseBB = Builder->GetInsertBlock();
```

Step 4 is the same idea for `else`: move the builder into `else`, generate the
expression, branch to `ifcont`, and update `ElseBB` to the block where that
path ended.

```llvm
else:                           ; reached when the condition is false
  %subtmp1 = fsub double %b, %a
  br label %ifcont
```

**Step 5 — Fill the join block and choose the final value.**

Both branches have produced a value, but the join block needs one name for "the value from whichever branch actually ran." LLVM solves this with a **PHI node** — each bracket pairs a value with the block it came from:

```llvm
%iftmp = phi double [ %subtmp, %then ], [ %subtmp1, %else ]
```

Read it as: "if we arrived here from `then`, use `%subtmp`; if from `else`, use `%subtmp1`." The name comes from the φ-function notation in the SSA papers of the late 1980s — exactly the piecewise-function idea of "this value if condition A, that value if condition B."

```cpp
Builder->SetInsertPoint(MergeBB);
PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
PN->addIncoming(ThenV, ThenBB);
PN->addIncoming(ElseV, ElseBB);
return PN;
```

That is why the `ThenBB` and `ElseBB` updates in steps 3 and 4 matter: the PHI node needs the actual blocks that flow into `ifcont`, not the blocks where those paths started.

> **Note:** LLVM requires PHI nodes to appear before any non-PHI instructions in a basic block. `CreatePHI` inserts into `MergeBB` immediately after `SetInsertPoint`, so this is satisfied here — but if you ever generate other instructions into a merge block before creating the PHI, LLVM will reject the IR with a verifier error.

**Full unoptimized IR for `absdiff`:**

```llvm
define double @absdiff(double %a, double %b) {
entry:
  %cmptmp  = fcmp ogt double %a, %b
  %booltmp = uitofp i1 %cmptmp to double
  %ifcond  = fcmp one double %booltmp, 0.0
  br i1 %ifcond, label %then, label %else

then:                                         ; reached when the condition is true
  %subtmp = fsub double %a, %b
  br label %ifcont

else:                                         ; reached when the condition is false
  %subtmp1 = fsub double %b, %a
  br label %ifcont

ifcont:                                       ; both branches rejoin here
  %iftmp = phi double [ %subtmp, %then ], [ %subtmp1, %else ]
  ret double %iftmp
}
```

### What `-v` Shows

`build/pyxc -v` uses the default optimization level (`-O2`), so the IR you see is already optimized. For `absdiff`, the optimizer notices that the branches only compute values with no side effects, and replaces the entire three-block `if` shape with a single `select` instruction:

```llvm
define double @absdiff(double %a, double %b) {
entry:
  %cmptmp = fcmp ogt double %a, %b
  %subtmp = fsub double %a, %b
  %subtmp1 = fsub double %b, %a
  %iftmp = select i1 %cmptmp, double %subtmp, double %subtmp1
  ret double %iftmp
}
```

LLVM writes `select` like this:

```llvm
%result = select i1 <condition>, <type> <true-value>, <type> <false-value>
```

`select` is LLVM's ternary operator: choose one value if the condition is true, otherwise the other. No extra branch blocks are needed.

The optimizer also removes the `i1` → `double` → `i1` round-trip and uses `%cmptmp` directly as the branch condition.

Functions where the branches make calls (`printd`, `putchard`) keep the full three-block structure because those calls must actually run in one branch and not the other.

To see the unoptimized IR shown above, run `build/pyxc -v -O0`.

### Why Nested ifs Change the End Block

Consider this Pyxc code:

```python
def xor(a, b):
    return if a == 1:         # %a1
        if b == 1: 0          # %a1_b1
        else: 1               # %a1_b0
        # these join at %a1_merge
    else:                     # %a0
        if b == 1: 1          # %a0_b1
        else: 0               # %a0_b0
        # these join at %a0_merge
    
    # the final result is chosen at %merge
```    

The IR for the `a == 1` branch:

```llvm
a1:                                      ; a == 1
  ...

a1_b1:                                   ; a == 1, b == 1 → 0
  ...

a1_b0:                                   ; a == 1, b == 0 → 1
  ...

a1_merge:
  %a1_result = phi double [ 0.0, %a1_b1 ],
                          [ 1.0, %a1_b0 ]
  ...
```

Once execution enters `a1`, the nested `if` sends it through either `a1_b1` or
`a1_b0`, and both of those rejoin at `a1_merge`.

So if control later reaches the final PHI from the `a == 1` side, it is
arriving from `a1_merge`, not from `a1`.

The `a == 0` side works the same way:

```llvm
a0:                                      ; a == 0
  ...

a0_b1:                                   ; a == 0, b == 1 → 1
  ...

a0_b0:                                   ; a == 0, b == 0 → 0
  ...

a0_merge:
  %a0_result = phi double [ 1.0, %a0_b1 ],
                          [ 0.0, %a0_b0 ]
  ...
```

Again, execution does not go straight from `a0` to the final PHI. Once it
enters `a0`, the nested `if` sends it through `a0_b1` or `a0_b0`, and both of
those rejoin at `a0_merge`.

So if control reaches the final PHI from the `a == 0` side, it is arriving
from `a0_merge`, not from `a0`.

Now the final PHI makes sense:

```llvm
merge:
  %xor_result = phi double [ %a1_result, %a1_merge ],
                           [ %a0_result, %a0_merge ]
  ret double %xor_result
```

The final PHI uses the exit points, not the entry points:

- if execution arrives from `a1_merge`, use `%a1_result`
- if execution arrives from `a0_merge`, use `%a0_result`

That is why these updates matter in the code generator:

```cpp
ThenBB = Builder->GetInsertBlock();
...
ElseBB = Builder->GetInsertBlock();
```

In the actual emitted IR, LLVM names these blocks `then`, `else`, and `ifcont`. The XOR example uses descriptive names like `a1`, `a1_merge`, and `merge` for clarity of exposition.

## for Loop Expressions

The `for` expression repeats a body expression while a condition holds:

```python
for var = start, condition, step: body
```

The loop runs while `condition` is non-zero. `var` is introduced by the `for` and is in scope for `condition`, `step`, and `body`. The body's return value is discarded each iteration.

### Parsing

`ParseForExpr` reads the variable name, `=`, start, `,`, condition, `,`, step, `:`, then the body — the same eat/parse/eat pattern as `ParseIfExpr`. `consumeNewlines()` before the body allows it on the next line:

```cpp
static unique_ptr<ExprAST> ParseForExpr() {
  getNextToken(); // eat 'for'

  string VarName = IdentifierStr;
  getNextToken(); // eat identifier

  getNextToken(); // eat '='

  auto Start = ParseExpression();
  // ... eat ',', parse Cond, eat ',', parse Step, eat ':' ...
  consumeNewlines();

  auto Body = ParseExpression();
  return make_unique<ForExprAST>(VarName, std::move(Start), std::move(Cond),
                                 std::move(Step), std::move(Body));
}
```

### Codegen


```
      entry
        │
        ▼
    loop_cond ◄─────────────────┐
        │                       │
   ┌────┴────┐                  │
   ▼         ▼                  │
loop_body  after_loop           │
   │        ret 0.0             │
   └── (i = i + step) ──────────┘
```

We check the condition before the first iteration. If `false` on entry, the body never runs. We'll trace through `for i = 1, i <= 3, 1: printd(i)` to see how each block is built.

**Step 1 — Evaluate start in the preheader and jump to the condition block.**

```cpp
Value *StartVal = Start->codegen();           // 1.0
BasicBlock *PreheaderBB = Builder->GetInsertBlock();
BasicBlock *CondBB  = BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
BasicBlock *BodyBB  = BasicBlock::Create(*TheContext, "loop_body", TheFunction);
BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "after_loop", TheFunction);
Builder->CreateBr(CondBB);
```

All three loop blocks are attached to the function immediately. `CreateBr`
finishes the preheader by jumping into the loop condition block.

IR so far — the preheader is finished, and the other loop blocks exist but are
still empty:

```llvm
define double @__anon_expr() {
entry:
  br label %loop_cond

loop_cond:   ; (empty)
loop_body:   ; (empty)
after_loop:  ; (empty)
}
```

**Step 2 — Build the condition block: PHI node + branch.**

```cpp
Builder->SetInsertPoint(CondBB);
PHINode *Variable = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
Variable->addIncoming(StartVal, PreheaderBB);   // first-iteration value
```

The PHI node is created with only one incoming for now — the preheader. The
back-edge from the loop body is added later, once we know where the body ends.

The condition block starts to look like this:

```llvm
loop_cond:
  %i = phi double [ 1.000000e+00, %entry ]
```

Next we generate the loop condition expression:

```cpp
Value *CondV = Cond->codegen();
```

For `i <= 3`, that adds:

```llvm
loop_cond:
  %i = phi double [ 1.000000e+00, %entry ]
  %cmptmp  = fcmp ole double %i, 3.000000e+00
  %booltmp = uitofp i1 %cmptmp to double
```

Then convert that `double` condition into an `i1` for branching:

```cpp
Value *LoopCond = Builder->CreateFCmpONE(
    CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
```

which adds:

```llvm
  %loopcond = fcmp one double %booltmp, 0.000000e+00
```

Finally, branch to the loop body or the after-loop block:

```cpp
Builder->CreateCondBr(LoopCond, BodyBB, AfterBB);
```

which completes the block:

```llvm
define double @__anon_expr() {
entry:
  br label %loop_cond

loop_cond:
  %i = phi double [ 1.000000e+00, %entry ]
  %cmptmp  = fcmp ole double %i, 3.000000e+00
  %booltmp = uitofp i1 %cmptmp to double
  %loopcond = fcmp one double %booltmp, 0.000000e+00
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:   ; (empty)
after_loop:  ; (empty)
}
```

**Step 3 — Fill the body block.**

```cpp
Builder->SetInsertPoint(BodyBB);
Body->codegen();                                // return value discarded
```

For `printd(i)`, that adds:

```llvm
loop_body:
  %calltmp = call double @printd(double %i)
```

Next generate the step value and the next loop variable:

```cpp
Value *StepVal = Step->codegen();               // 1.0
Value *NextVar  = Builder->CreateFAdd(Variable, StepVal, "nextvar");
```

which adds:

```llvm
  %nextvar = fadd double %i, 1.000000e+00
```

Finally, update the PHI with the back-edge value and branch back to
`loop_cond`:

```cpp
BasicBlock *BodyEndBB = Builder->GetInsertBlock();
Variable->addIncoming(NextVar, BodyEndBB);      // complete the PHI
Builder->CreateBr(CondBB);
```

That completes the loop body and gives the PHI its second incoming value:

```llvm
define double @__anon_expr() {
entry:
  br label %loop_cond

loop_cond:
  %i = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop_body ]
  %cmptmp  = fcmp ole double %i, 3.000000e+00
  %booltmp = uitofp i1 %cmptmp to double
  %loopcond = fcmp one double %booltmp, 0.000000e+00
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:
  %calltmp = call double @printd(double %i)
  %nextvar = fadd double %i, 1.000000e+00
  br label %loop_cond

after_loop:  ; (empty)
}
```

**Step 4 — After-loop block returns `0.0`.**

```cpp
Builder->SetInsertPoint(AfterBB);
return ConstantFP::get(*TheContext, APFloat(0.0));
```

Now the last block is filled in too, so the loop is complete.

**Full unoptimized IR for `for i = 1, i <= 3, 1: printd(i)` as a top-level expression:**

```llvm
define double @__anon_expr() {
entry:
  br label %loop_cond

loop_cond:                                    ; entered first from entry, later from loop_body
  %i = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop_body ]
  %cmptmp  = fcmp ole double %i, 3.000000e+00
  %booltmp = uitofp i1 %cmptmp to double
  %loopcond = fcmp one double %booltmp, 0.000000e+00
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:                                    ; runs while the loop condition is true
  %calltmp = call double @printd(double %i)
  %nextvar = fadd double %i, 1.000000e+00
  br label %loop_cond

after_loop:                                   ; reached when the loop condition becomes false
  ret double 0.000000e+00
}
```

### What `-v` Shows After Optimization

The optimizer recognises that widening the `i1` result to `double` just to compare it against `0.0` again to convert it back to `i1` is unnecessary — the `i1` from the first `fcmp` is all that's needed to drive the branch. It removes the roundtrip entirely:

```llvm
; unoptimized
%cmptmp  = fcmp ole double %i, 3.000000e+00
%booltmp = uitofp i1 %cmptmp to double
%loopcond = fcmp one double %booltmp, 0.000000e+00
br i1 %loopcond, label %loop_body, label %after_loop
```

```llvm
; optimized
%cmptmp = fcmp ugt double %i, 3.000000e+00
br i1 %cmptmp, label %after_loop, label %loop_body
```

Even though Pyxc has no special boolean type — comparisons produce `double` like everything else — the optimizer recovers the efficient `i1` branch condition automatically. The simplicity costs nothing at runtime. 

Notice also that LLVM rewrote `ole` (ordered less-than-or-equal) as `ugt` (unordered greater than), and flipped the branch destinations to match. This doesn't change the behaviour of your code and is more of an LLVM implementation detail that we need not concern ourselves with. 

The full optimized function:

```llvm
define double @__anon_expr() {
entry:
  br label %loop_cond
loop_cond:
  %i = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop_body ]
  %cmptmp = fcmp ugt double %i, 3.000000e+00
  br i1 %cmptmp, label %after_loop, label %loop_body
loop_body:
  %calltmp = call double @printd(double %i)
  %nextvar = fadd double %i, 1.000000e+00
  br label %loop_cond
after_loop:
  ret double 0.000000e+00
}
```

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

# Try these too
# mandel(0 - 2, 0 - 1, 0.02, 0.04)
# mandel(0 - 0.9, 0 - 1.4, 0.02, 0.03)
```

**Line breaks.** The parser only allows newlines in specific positions — after `:` in `def`, `if`, and `for` bodies. A newline anywhere else (inside a function argument list, mid-expression) is a parse error. This is why `mandelconverge`'s nested `if`/`else` chain can span lines (each `else:` starts a new allowed position) but `mandel`'s long argument list must stay on a single line.

**Unary minus.** Pyxc has no unary minus yet — `-2.3` would be parsed as the binary operator `-` applied to nothing, which is an error. The workaround is `0 - 2.3`: a fully-formed binary subtraction that the optimizer collapses to the literal `-2.3` with no extra instructions emitted. Chapter 9 adds unary-expression parsing and built-in unary minus support.

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
ready> def absdiff(a, b): return if a > b: a - b else: b - a
```
```bash
Parsed a function definition.
```
```python
ready> absdiff(10, 5)
```
```bash
Parsed a top-level expression.
Evaluated to 5.000000
```
```python
ready> absdiff(3, 8)
```
```bash
Parsed a top-level expression.
Evaluated to 5.000000
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
Parsed a top-level expression.
1.000000
2.000000
3.000000
Evaluated to 0.000000
```
```python
ready>
```
<!-- code-merge:end -->

## What's Next

[Chapter 9](chapter-09.md) adds user-defined operators via Python-style decorators — `@binary(precedence)` and `@unary` — and also introduces unary-expression parsing with built-in unary minus, so `-x` finally works. The chapter payoff is a richer Mandelbrot renderer with density shading and a clean sequencing operator.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

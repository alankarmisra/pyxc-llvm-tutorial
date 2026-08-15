---
section: "Statements and Control Flow"
description: "Add comparison operators, if/else expressions, and for loops — then use them to render the Mandelbrot set in ASCII."
---
# 10. pyxc: Control Flow: If, Else, and For

## What I Am Building

[Chapter 9](chapter-09.md) added file input. The language still only supports arithmetic and function calls. I now add comparisons, conditional branches, and loops.

I continue to represent every pyxc value as a `double`, so I make comparisons produce `1.0` for true and `0.0` for false. A comparison can then appear anywhere that another expression can appear.

<!-- code-merge:start -->
```pyxc
ready> 1 < 2
```
```bash
Parsed a top-level expression.
Evaluated to 1.000000
```
```pyxc
ready> 3 != 3
```
```bash
Parsed a top-level expression.
Evaluated to 0.000000
```
<!-- code-merge:end -->

I first implement `if` as an expression. I require both branches because the complete `if` must produce a value. I will add statement-style control flow when I introduce multi-statement blocks later.

<!-- code-merge:start -->
```pyxc
ready> def absdiff(a, b): if a > b: a - b else: b - a
```
```bash
Parsed a function definition.
```
```pyxc
ready> absdiff(10, 5)
```
```bash
Parsed a top-level expression.
Evaluated to 5.000000
```
<!-- code-merge:end -->

I also implement `for` as an expression. It repeats its body and produces `0.0` when it finishes. The value is only a placeholder until pyxc has statements and a way to represent no value.

<!-- code-merge:start -->
```pyxc
ready> extern def printd(x)
```
```bash
Parsed an extern.
```
```pyxc
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
cd pyxc-llvm-tutorial/code/chapter-10
```

## Grammar

I add `if-expression` and `for-expression` to `primary`, and replace the single `<` alternative in `comparison` with a `comparison-operator` rule containing six operators:

`code/chapter-10/pyxc.ebnf`

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | external
                                     | top-level-expression ;
 function-definition               = "def" function-signature ":"
                                     [ end-of-lines ] expression ;
 external                          = "extern" "def" function-signature ;
 top-level-expression              = expression ;
 function-signature                = name "(" [ parameters ] ")" ;
 parameters                        = parameter { "," parameter } ;
 parameter                         = name ;
 expression                        = comparison ;
-comparison                        = sum { "<" sum } ;
+comparison                        = sum { comparison-operator sum } ;
+comparison-operator               = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum                               = term { ("+" | "-") term } ;
 term                              = factor { ("*" | "/" | "%") factor } ;
 factor                            = "-" factor | primary ;
 primary                           = name-expression
                                     | number-expression
-                                    | parenthesized-expression ;
+                                    | parenthesized-expression
+                                    | if-expression
+                                    | for-expression ;
+if-expression                     = "if" expression ":"
+                                    [ end-of-lines ] expression
+                                    [ end-of-lines ] "else" ":"
+                                    [ end-of-lines ] expression ;
+for-expression                    = "for" name "=" expression ","
+                                    expression "," expression ":"
+                                    [ end-of-lines ] expression ;
 name-expression                   = name
                                     | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 number                            = digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 whitespace                        = " " | "\t" | "\v" | "\f" ;
```

## New Tokens

I add tokens for control-flow keywords and multi-character comparisons:

```cppdiff
*enum Token {
*  tok_eof = -1,
*  tok_eol = -2,
*  tok_error = -3,
*
*  // commands
*  tok_def = -4,
*  tok_extern = -5,
*
*  // primary
*  tok_name = -6,
*  tok_number = -7,
*
+  // comparison operators
+  // Only multi-character operators use explicit tokens;
+  // I give single-character operators named tokens whose values match their
+  // corresponding characters.
+  tok_eq = -8,   // ==
+  tok_neq = -9,  // !=
+  tok_leq = -10, // <=
+  tok_geq = -11, // >=
+
+  // control
+  tok_if = -12,
+  tok_else = -13,
+
+  // loops
+  tok_for = -15,
+
*  // punctuation and operators
*  tok_lparen = '(',
*  tok_rparen = ')',
*  tok_comma = ',',
*  tok_colon = ':',
*  tok_plus = '+',
*  tok_minus = '-',
*  tok_star = '*',
*  tok_slash = '/',
*  tok_percent = '%',
*  tok_less = '<',
+  tok_greater = '>',
+  tok_equal = '=',
*};
```

I add `if`, `else`, and `for` to `Keywords`. The lexer returns the four negative tokens when it recognizes two-character operators. I use named character tokens for `<`, `>`, and `=`.

## Comparison Operators

### Lexer: Two-Character Tokens

To distinguish `=` from `==`, I inspect the next character without consuming it permanently. I add `peek()` for that lookahead:

```cpp
static int peek() {
  int c = fgetc(Input);
  if (c != EOF)
    ungetc(c, Input);
  return c;
}
```

In `getToken()`, I consume the second `=` only when `peek()` finds one:

```cpp
if (LastChar == '=') {
  int Tok = (peek() == '=') ? (advance(), tok_eq) : tok_equal;
  LastChar = advance();
  return Tok;
}
```

The expression `(advance(), tok_eq)` consumes the second character and then produces `tok_eq`. Otherwise, I return `tok_equal`. I finish either path by loading the character after the operator into `LastChar`. I use the same pattern for `!=`, `<=`, and `>=`.

### Parser: Extending the Comparison Layer

The grammar already gives comparisons their own parser layer. I extend its loop to accept all six comparison tokens:

```cpp
static unique_ptr<ExpressionNode> ParseComparison() {
  auto Left = ParseSum();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_eq || CurrentToken == tok_neq ||
         CurrentToken == tok_leq || CurrentToken == tok_geq ||
         CurrentToken == tok_less || CurrentToken == tok_greater) {
    int Operator = CurrentToken;
    getNextToken();

    auto Right = ParseSum();
    if (!Right)
      return nullptr;

    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}
```

I parse every comparison at the same grammar tier and group repeated comparisons from left to right. Therefore, `a < b == c` becomes `(a < b) == c`. pyxc does not implement Python's special chained-comparison behavior yet.

`BinaryExpressionNode` stores `Operator` as an `int`. I need that range because tokens such as `tok_eq` use negative values, while named single-character tokens use their character values.

### Comparison Codegen

I generate LLVM [`fcmp`](https://llvm.org/docs/LangRef.html#fcmp-instruction) instructions for pyxc comparisons.

For example, I implement `==` in `BinaryExpressionNode::codegen()` with an ordered equal comparison:

```cppdiff
*Value *BinaryExpressionNode::codegen() {
*  Value *L = Left->codegen();
*  if (!L)
*    return nullptr;
*
*  Value *R = Right->codegen();
*  if (!R)
*    return nullptr;
*
*  switch (Operator) {
*  case tok_plus:
*    return TheBuilder->CreateFAdd(L, R, "addtmp");
*  case tok_minus:
*    return TheBuilder->CreateFSub(L, R, "subtmp");
*  case tok_star:
*    return TheBuilder->CreateFMul(L, R, "multmp");
*  case tok_slash:
*    return TheBuilder->CreateFDiv(L, R, "divtmp");
*  case tok_percent:
*    return TheBuilder->CreateFRem(L, R, "remtmp");
*  case tok_less:
-    L = TheBuilder->CreateFCmpULT(L, R, "cmptmp");
+    L = TheBuilder->CreateFCmpOLT(L, R, "cmptmp");
*    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
+  case tok_greater:
+    L = TheBuilder->CreateFCmpOGT(L, R, "cmptmp");
+    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
+  case tok_eq:
+    L = TheBuilder->CreateFCmpOEQ(L, R, "cmptmp");
+    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
+  case tok_neq:
+    L = TheBuilder->CreateFCmpUNE(L, R, "cmptmp");
+    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
+  case tok_leq:
+    L = TheBuilder->CreateFCmpOLE(L, R, "cmptmp");
+    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
+  case tok_geq:
+    L = TheBuilder->CreateFCmpOGE(L, R, "cmptmp");
+    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
*  default:
*    return LogErrorV("invalid binary operator");
*  }
*}
```

which produces:

```llvm
%cmptmp = fcmp oeq double %L, %R
```

LLVM provides ordered and unordered floating-point predicates. `oeq` returns false when either operand is `NaN`, while `ueq` returns true in that case. Other comparison pairs follow the same naming pattern, such as `olt`/`ult` and `one`/`une`.

The distinction comes from numeric order. Regular numbers are ordered; `NaN` is unordered. I use ordered predicates for `==`, `<`, `<=`, `>`, and `>=`, and unordered not-equal for `!=`. This matches C and IEEE 754 behavior.

My choices are:

- I use ordered predicates for `==`, `<`, `<=`, `>`, and `>=`, so comparisons with `NaN` return false.
- I use unordered not-equal (`une`) for `!=`, so `x != NaN` returns true.

```cpp
case tok_neq:
  L = TheBuilder->CreateFCmpUNE(L, R, "cmptmp");
```

which produces:

```llvm
%cmptmp = fcmp une double %L, %R
```

LLVM also provides `fcmp uno` for testing whether either operand is `NaN`:

```cpp
TheBuilder->CreateFCmpUNO(L, R, "has_nan");
```
```llvm
%has_nan = fcmp uno double %L, %R
```

#### Converting `i1` Back to `double`

`fcmp` produces an `i1`, LLVM's one-bit Boolean type. pyxc does not have a separate Boolean type, so I convert that result to `double`:

```cpp
// CreateUIToFP (Unsigned Int -> Floating Point)
return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
```

which produces:

```llvm
%booltmp = uitofp i1 %cmptmp to double
```

This gives pyxc its usual comparison result convention: `false → 0.0`, `true → 1.0`. That value is what later flows into `if` conditions and arithmetic expressions. 

## If/Else Expressions

I make `if` an expression, so I can use it inside another expression, as a function argument, as a loop body, or inside another `if`.

```pyxc
if condition: then_expr else: else_expr
```

I treat any nonzero value as true. The condition can be any expression; it does not need to be a comparison.

### Parsing

I parse the condition, the required `then` expression, and the required `else` expression. I accept newlines after each colon and before `else`, but I do not process indentation yet:

```cpp
static unique_ptr<ExpressionNode> ParseIfExpression() {
  getNextToken(); // eat 'if'

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after if condition");
  getNextToken(); // eat ':'

  consumeNewlines(); // allow body on next line

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  consumeNewlines(); // allow 'else' on next line

  if (CurrentToken != tok_else)
    return LogErrorExpression("Expected 'else' in if expression");
  getNextToken(); // eat 'else'

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after else");
  getNextToken(); // eat ':'

  consumeNewlines(); // allow body on next line

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return make_unique<IfExpressionNode>(
      std::move(Cond), std::move(Then), std::move(Else));
}
```

`consumeNewlines()` consumes consecutive `tok_eol` tokens, so I accept both inline and multiline forms:

```pyxc
if a > b: a - b else: b - a            # all on one line

if a > b:                              # multi-line
    a - b
else:
    b - a
```

### Codegen: Building the Then / Else / Join Blocks

To generate an `if`, I need to:

1. Evaluate the condition.
2. Run exactly one of the two branches.
3. Continue afterward with the value produced by the branch that ran.

For an `if`, I need one block for the `then` path, one for
the `else` path, and one final block where both paths meet again.

I use the same example function:

```pyxc
def absdiff(a, b): if a > b: a - b else: b - a
```

Inside that function, the `if` expression is:

```pyxc
if a > b: a - b else: b - a
```

The generated block layout looks like this:

```diagram
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

LLVM requires every basic block to end with a terminator such as `br` or `ret`. I therefore create explicit branches from `entry` to `then` or `else`, and from each branch to `ifcont`.

I build this shape in `IfExpressionNode::codegen()` and trace it through `absdiff`.

At the LLVM level, I'm filling in this function body:

```llvm
define double @absdiff(double %a, double %b) {
entry:
  ...
}
```

**Step 1 — Generate the condition in the current block.**

First I generate code for the condition expression:

```cpp
Value *CondV = Cond->codegen();
```

For `absdiff`, `Cond->codegen()` generates code for `a > b`. 

```cpp
case tok_greater:
  L = TheBuilder->CreateFCmpOGT(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
```

That produces:

```llvm
define double @absdiff(double %a, double %b) {
entry:
  %cmptmp  = fcmp ogt double %a, %b
  %booltmp = uitofp i1 %cmptmp to double
}
```

`Cond->codegen()` gives me a `double`, because pyxc represents booleans as
`0.0` or `1.0`. LLVM branches need an `i1`, so before I can branch I must
turn that `double` back into an `i1`. 

I do that by comparing the condition value against `0.0`:

```cpp
CondV = TheBuilder->CreateFCmpONE(
    CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");
```

This means: treat the condition as true if it is not equal to `0.0`.

A comparison condition makes an `i1 → double → i1` round trip. I first convert the comparison to `double` because every pyxc expression has that type. Here I must convert any condition—including a number such as `2.0`—back to the `i1` required by `CreateCondBr`. LLVM's optimizer removes the round trip when it can.

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
TheBuilder->CreateCondBr(CondV, ThenBB, ElseBB);
```

I attach all three blocks to the function and finish the current block with `CreateCondBr`:

```llvm
br i1 %ifcond, label %then, label %else
```

Check `%ifcond`; jump to `%then` if true, `%else` if false.

Now I have:

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

**Step 3 — Move the builder cursor into `then` and generate that branch.**

```cpp
TheBuilder->SetInsertPoint(ThenBB);
Value *ThenV = Then->codegen();
if (!ThenV)
  return nullptr;
TheBuilder->CreateBr(MergeBB);
```

I call `SetInsertPoint` so the builder appends subsequent instructions to the `then` block.

After `Then->codegen()` finishes, I emit an unconditional branch to `ifcont` so execution continues in the join block after the then-branch completes.

```llvm
then:                           ; reached when the condition is true
  %subtmp = fsub double %a, %b
  br label %ifcont
```

Finally, I update `ThenBB` so it points to the block where the `then` path
actually finished.

```cpp
// Update ThenBB to the block where the then-path actually ended.
// This matters for nested control flow; explained just below.
ThenBB = TheBuilder->GetInsertBlock();
```

This matters because nested control flow can create more blocks and move the
builder cursor. I want the block where the `then` path ended, not the block where it
started. This only matters for nested `if` expressions; I'll look at that
case a little later in this chapter.

**Step 4 — Do the same for `else`.**

```cpp
TheBuilder->SetInsertPoint(ElseBB);
Value *ElseV = Else->codegen();
if (!ElseV)
  return nullptr;
TheBuilder->CreateBr(MergeBB);
ElseBB = TheBuilder->GetInsertBlock();
```

Step 4 is the same idea for `else`: move the builder cursor into `else`, generate the
expression, branch to `ifcont`, and update `ElseBB` to the block where that
path ended.

```llvm
else:                           ; reached when the condition is false
  %subtmp1 = fsub double %b, %a
  br label %ifcont
```

**Step 5 — Fill the join block and choose the final value.**

Both branches produce a value, but I need one value after they rejoin. LLVM requires me to represent that choice with a **PHI node**. Each incoming entry pairs a value with the block that produced it:

```llvm
%iftmp = phi double [ %subtmp, %then ], [ %subtmp1, %else ]
```

Read it as: "if execution arrived here from `then`, use `%subtmp`; if from `else`, use `%subtmp1`." The name **phi** comes from the φ-function notation in the SSA papers of the late 1980s — exactly the piecewise-function idea of "this value if condition A, that value if condition B."

```cpp
TheBuilder->SetInsertPoint(MergeBB);
PHINode *PN = TheBuilder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
PN->addIncoming(ThenV, ThenBB);
PN->addIncoming(ElseV, ElseBB);
return PN;
```

> LLVM requires PHI nodes to appear before non-PHI instructions in a block. I create the PHI immediately after moving the insertion point to `MergeBB`.

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

Because execution jumps directly to either `then` or `else` and never enters the other block, `if`/`else` short-circuits — the branch not taken is never evaluated.

### What `-v` Shows

By default I print optimized IR. My pass pipeline is only three fixed passes — `InstCombinePass`, `ReassociatePass`, `GVNPass` — not LLVM's full `-O2`, so it cleans up redundant instructions but doesn't restructure control flow. The `then`/`else`/`ifcont` blocks and the `phi` stay exactly as I built them:

```llvm
define double @absdiff(double %a, double %b) {
entry:
  %cmptmp = fcmp ogt double %a, %b
  br i1 %cmptmp, label %then, label %else

then:                                             ; preds = %entry
  %subtmp = fsub double %a, %b
  br label %ifcont

else:                                             ; preds = %entry
  %subtmp1 = fsub double %b, %a
  br label %ifcont

ifcont:                                           ; preds = %else, %then
  %iftmp = phi double [ %subtmp, %then ], [ %subtmp1, %else ]
  ret double %iftmp
}
```

The only thing that changes from the unoptimized IR is the condition: `InstCombinePass` recognizes `fcmp one (uitofp i1 %cmptmp to double), 0.0` as a round trip back to `%cmptmp` itself, and folds it away — so `br` branches on `%cmptmp` directly instead of the reconstructed `%ifcond`. I keep the simple all-`double` language model in the code I write; the optimizer is what notices the `i1` was there all along.

Turning branches into a branchless `select` is a real LLVM transformation, but it needs passes I haven't added yet (`SimplifyCFGPass` in particular). Until I do, `if`/`else` always keeps its block structure, at every `-O` level pyxc accepts, including `-O0`. `-O0` skips even the three passes I do have, which is why the unoptimized IR above still shows the full `i1 → double → i1` round trip.

### Why Nested Ifs Change the End Block

Consider this pyxc code:

```pyxc
def xor(a, b):
    if a == 1:                # %a1
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
ThenBB = TheBuilder->GetInsertBlock();
...
ElseBB = TheBuilder->GetInsertBlock();
```

In the actual emitted IR, LLVM names these blocks `then`, `else`, and `ifcont`. The XOR example uses descriptive names like `a1`, `a1_merge`, and `merge` for clarity of exposition.

## For Loop Expressions

I use a `for` expression to repeat one body expression while a condition remains nonzero:

```pyxc
for var = start, condition, step: body
```

I introduce `var` for the loop and make it available to the condition, step, and body. I discard the body's value on each iteration. I require the step expression explicitly.

### Parsing

I parse the variable name, start value, condition, step, and body in their grammar order. I allow newlines before the body:

```cpp
static unique_ptr<ExpressionNode> ParseForExpression() {
  getNextToken(); // eat 'for'

  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected name after 'for'");
  string VarName = Name;
  getNextToken(); // eat name

  if (CurrentToken != tok_equal)
    return LogErrorExpression("Expected '=' after for variable");
  getNextToken(); // eat '='

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;

  if (CurrentToken != tok_comma)
    return LogErrorExpression("Expected ',' after for start value");
  getNextToken(); // eat ','

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurrentToken != tok_comma)
    return LogErrorExpression("Expected ',' after for condition");
  getNextToken(); // eat ','

  auto Step = ParseExpression();
  if (!Step)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after for step");
  getNextToken(); // eat ':'

  // Allow body on next line.
  consumeNewlines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return make_unique<ForExpressionNode>(
      VarName, std::move(Start), std::move(Cond),
      std::move(Step), std::move(Body));
}
```

### Codegen

```diagram
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

I evaluate the parts in this order: `start → condition → body → step → condition → …`. Because I check the condition before the body, the body may run zero times. I trace `for i = 1, i <= 3, 1: printd(i)` through the blocks.

**Step 1 — Evaluate start in the preheader and jump to the condition block.**

```cpp
Value *StartVal = Start->codegen();           // 1.0
BasicBlock *PreheaderBB = TheBuilder->GetInsertBlock();
BasicBlock *CondBB  = BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
BasicBlock *BodyBB  = BasicBlock::Create(*TheContext, "loop_body", TheFunction);
BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "after_loop", TheFunction);
TheBuilder->CreateBr(CondBB);
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
TheBuilder->SetInsertPoint(CondBB);
PHINode *Variable = TheBuilder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
Variable->addIncoming(StartVal, PreheaderBB);   // first-iteration value
```

I create the PHI node with only one incoming value for now — the preheader. The
back-edge from the loop body is added later, once I know where the body ends.

Right after creating the PHI, I bind `VarName` to it in `NamedValues`, shadowing any outer variable of the same name (I cover the shadow/restore mechanics in full further down):

```cpp
NamedValues[VarName] = Variable;
```

I do this before generating the condition, not just before the body, because the condition can reference the loop variable too — `i <= 3` needs to find `i`.

The condition block starts to look like this:

```llvm
loop_cond:
  %i = phi double [ 1.000000e+00, %entry ]
```

Next I generate the loop condition expression:

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
Value *LoopCond = TheBuilder->CreateFCmpONE(
    CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
```

which adds:

```llvm
  %loopcond = fcmp one double %booltmp, 0.000000e+00
```

Finally, branch to the loop body or the after-loop block:

```cpp
TheBuilder->CreateCondBr(LoopCond, BodyBB, AfterBB);
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
TheBuilder->SetInsertPoint(BodyBB);
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
Value *NextVar  = TheBuilder->CreateFAdd(Variable, StepVal, "nextvar");
```

which adds:

```llvm
  %nextvar = fadd double %i, 1.000000e+00
```

Finally, update the PHI with the back-edge value and branch back to
`loop_cond`:

```cpp
BasicBlock *BodyEndBB = TheBuilder->GetInsertBlock();
Variable->addIncoming(NextVar, BodyEndBB);      // complete the PHI
TheBuilder->CreateBr(CondBB);
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
TheBuilder->SetInsertPoint(AfterBB);
return ConstantFP::get(*TheContext, APFloat(0.0));
```

The loop has no natural return value, so the after-loop block returns `0.0`.

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

### What `-v` Shows after Optimization

As with `if/else`, the optimizer removes the `i1` → `double` → `i1` roundtrip:

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

LLVM also rewrites `ole` (ordered less-than-or-equal) as its complement, `ugt` (unordered greater-than), and swaps the branch destinations. These two changes preserve the same control flow.

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

With comparisons, `if`/`else`, and `for`, I can render the Mandelbrot set. For each complex number `c`, I repeatedly calculate `z = z² + c`, beginning with `z = 0`, and test whether the result escapes. I use recursion in `mandelconverger` because pyxc does not have mutable variables yet; I pass the updated values as parameters instead.

```pyxc
# test/mandel.pyxc
extern def putchard(x)

def mandelconverger(real, imag, iters, creal, cimag):
    if iters > 255: iters
    else: if (real * real + imag * imag) > 4: iters
          else: mandelconverger(real * real - imag * imag + creal, 2 * real * imag + cimag, iters + 1, creal, cimag)

def mandelconverge(real, imag):
    mandelconverger(real, imag, 0, real, imag)

def mandelrow(xmin, xmax, xstep, y):
    for x = xmin, x < xmax, xstep:
        putchard(if mandelconverge(x, y) > 255: 32 else: 42)

def mandelhelp(xmin, xmax, xstep, ymin, ymax, ystep):
    for y = ymin, y < ymax, ystep:
        mandelrow(xmin, xmax, xstep, y) + putchard(10)

def mandel(realstart, imagstart, realmag, imagmag):
    mandelhelp(realstart, realstart + realmag * 78, realmag, imagstart, imagstart + imagmag * 40, imagmag)

mandel(0 - 2.3, 0 - 1.3, 0.05, 0.07)

# Try these too
# mandel(0 - 2, 0 - 1, 0.02, 0.04)
# mandel(0 - 0.9, 0 - 1.4, 0.02, 0.03)
```

**Line breaks.** I only consume newlines after `:` in `def`, `if`, and `for` forms, and before `else`. I reject a newline in an argument list or in the middle of another expression. The nested `if` can therefore span lines, while the long call in `mandel` must remain on one line.

**Sequencing with `+`.** I write `mandelrow(...) + putchard(10)` to perform two calls in one expression. Both return `0.0`, so the addition only gives me a way to sequence their side effects. Starting in Chapter 11's version of this file, I wrap this same pattern in a small `sequence(x, y)` helper for readability — the trick itself doesn't go away until pyxc's function bodies can hold more than one expression.

**Unary minus.** I write `0 - 2.3` instead of `-2.3`, even though Chapter 4 already added unary minus by this point. Either form compiles to the same negative constant; I keep the subtraction form here because it's how this file has always been written, and rewriting it isn't the point of this section.

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

I write the iteration and branching in pyxc. The host only provides `putchard` for writing one character to `stdout`.

## Build and Run

```bash
cd code/chapter-10
cmake -S . -B build && cmake --build build
./build/pyxc
```

With no filename, I start the interactive REPL. I press `Ctrl-D` to exit.

To run the Mandelbrot renderer directly:

```bash
./build/pyxc test/mandel.pyxc
```

```bash
llvm-lit -v test/
```

## Try It

Since I parse every comparison at the same tier and group left to right, `a < b == c` becomes `(a < b) == c`, not Python-style chained comparison:

<!-- code-merge:start -->
```pyxc
ready> 1 < 2 == 1
```
```text
Parsed a top-level expression.
Evaluated to 1.000000
```
<!-- code-merge:end -->

`1 < 2` evaluates to `1.0`, then `1.0 == 1` evaluates to `1.0` — true. It isn't testing whether `1 < 2 < 1` in the mathematical sense.

## What's Next

[Chapter 11](chapter-11.md) adds mutable variables.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

---
section: "Statements and Control Flow"
description: "Add comparison operators, if/else expressions, and for loops, one working experiment at a time."
---

# 10. pyxc: Control Flow: If, Else, and For

Right now pyxc can evaluate arithmetic:

```pyxc
ready> 1 + 2 * 3
```

```text
Parsed a top-level expression.
Evaluated to 7.000000
```

That is useful, but a program also needs to ask questions and repeat work. By the end of this chapter, these should work:

```pyxc
ready> 1 < 2
ready> if 10 > 5: 10 else: 5
ready> for i = 1, i <= 3, 1: printd(i)
```

We're still keeping every pyxc value a `double`, so a comparison can't return a bare true/false, you'll need to produce `1.0` for true and `0.0` for false instead.

As an expression, we have to make `if` produce a value no matter which branch runs, so you'll have to have both branches complete. Statement-style control flow (where a branch can be empty) waits until pyxc has multi-statement blocks.

`for` is also an expression here. You'll have it repeat its body and produce `0.0` when it finishes, that result is only a placeholder until pyxc has statements and a way to represent no value at all.

The implementation order matters:

```text
comparison tokens -> comparison parser -> comparison IR
                  -> if AST/parser/IR
                  -> for AST/parser/IR
```

Work in:

```bash
cd code/chapter-10
```

## 1. Add the Control-Flow Grammar

First, replace the old comparison rule:

```ebnf
comparison = sum { "<" sum } ;
```

with:

```ebnf
comparison          = sum { comparison-operator sum } ;
comparison-operator = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
```

Then extend `primary`:

```ebnf
primary = name-expression
        | number-expression
        | parenthesized-expression
        | if-expression
        | for-expression ;
```

Add the two new expression rules:

```ebnf
if-expression = "if" expression ":"
                [ end-of-lines ] expression
                [ end-of-lines ] "else" ":"
                [ end-of-lines ] expression ;

for-expression = "for" name "=" expression ","
                 expression "," expression ":"
                 [ end-of-lines ] expression ;
```

This gives us three clean units:

```text
one comparison -> one double boolean
one if         -> one selected value
one for        -> one repeated side effect
```

## 2. Teach the Lexer the New Tokens

The lexer currently returns character values for one-character operators. Keep that behavior for `<`, `>`, and `=`, but add named tokens for the multi-character operators and keywords.

After `tok_number`, add:

```cpp
// comparison operators
tok_eq = -8,   // ==
tok_neq = -9,  // !=
tok_leq = -10, // <=
tok_geq = -11, // >=

// control
tok_if = -12,
tok_else = -13,

// loops
tok_for = -15,
```

Then add these character-token aliases near the other punctuation:

```cpp
tok_less = '<',
tok_greater = '>',
tok_assign = '=',
```

Update `Keywords` from:

```cpp
static map<string, Token> Keywords = {{"def", tok_def},
                                      {"extern", tok_extern}};
```

to:

```cpp
static map<string, Token> Keywords = {{"def", tok_def},
                                      {"extern", tok_extern},
                                      {"if", tok_if},
                                      {"else", tok_else},
                                      {"for", tok_for}};
```

Add one-character lookahead after `advance()`:

```cpp
static int peek() {
  int c = fgetc(Input);
  if (c != EOF)
    ungetc(c, Input);
  return c;
}
```

`peek()` reads the next character and puts it back. It does not update the source location because the lexer has not committed to consuming that character yet.

In `getToken()`, add these branches after comment handling and before the `EOF` check:

```cpp
if (LastChar == '=') {
  int Tok = (peek() == '=') ? (advance(), tok_eq) : tok_assign;
  LastChar = advance();
  return Tok;
}

if (LastChar == '!') {
  int Tok = (peek() == '=') ? (advance(), tok_neq) : '!';
  LastChar = advance();
  return Tok;
}

if (LastChar == '<') {
  int Tok = (peek() == '=') ? (advance(), tok_leq) : tok_less;
  LastChar = advance();
  return Tok;
}

if (LastChar == '>') {
  int Tok = (peek() == '=') ? (advance(), tok_geq) : tok_greater;
  LastChar = advance();
  return Tok;
}
```

The comma expression in `(advance(), tok_eq)` consumes the second `=` and then produces `tok_eq`. If there is no second `=`, the lexer returns the one-character token.

Build now:

```bash
cmake -S . -B build
cmake --build build
```

At this point the lexer understands the spelling, but the parser still cannot build the new expressions. That separation is intentional.

## 3. Parse All Six Comparisons

Find `ParseComparison()`. It currently loops only while the current token is `<`.

Replace that condition with:

```cpp
while (CurrentToken == tok_eq || CurrentToken == tok_neq ||
       CurrentToken == tok_leq || CurrentToken == tok_geq ||
       CurrentToken == tok_less || CurrentToken == tok_greater) {
```

Leave the body of the loop unchanged: save the operator, consume it, parse the right-hand `sum`, and create a new `BinaryExpressionNode`.

`BinaryExpressionNode::Operator` must now be an `int`, not a `char`. Tokens such as `tok_eq` have negative enum values and do not fit the one-character-token model:

```cpp
class BinaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Left, Right;

public:
  BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left,
                       unique_ptr<ExpressionNode> Right)
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {}
  Value *codegen() override;
};
```

## 4. Emit LLVM IR for Comparisons

Arithmetic already returns `double`, so comparisons should do the same.

LLVM's floating-point comparisons return `i1`. Convert that result back to `double` before returning it:

```text
fcmp -> i1 -> uitofp -> double
```

Add these cases to `BinaryExpressionNode::codegen()`:

```cpp
case tok_less:
  L = TheBuilder->CreateFCmpOLT(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(
      L, Type::getDoubleTy(*TheContext), "booltmp");
case tok_greater:
  L = TheBuilder->CreateFCmpOGT(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(
      L, Type::getDoubleTy(*TheContext), "booltmp");
case tok_eq:
  L = TheBuilder->CreateFCmpOEQ(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(
      L, Type::getDoubleTy(*TheContext), "booltmp");
case tok_neq:
  L = TheBuilder->CreateFCmpUNE(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(
      L, Type::getDoubleTy(*TheContext), "booltmp");
case tok_leq:
  L = TheBuilder->CreateFCmpOLE(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(
      L, Type::getDoubleTy(*TheContext), "booltmp");
case tok_geq:
  L = TheBuilder->CreateFCmpOGE(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(
      L, Type::getDoubleTy(*TheContext), "booltmp");
```

Use ordered comparisons for `==`, `<`, `<=`, `>`, and `>=`. Use unordered-not-equal for `!=`, so a comparison with NaN behaves like normal floating-point inequality.

Rebuild and run the first experiment:

```bash
cmake --build build
./build/pyxc
```

Enter:

```pyxc
ready> 1 < 2
ready> 3 != 3
ready> 4 >= 4
```

Expected:

```text
Parsed a top-level expression.
Evaluated to 1.000000
Parsed a top-level expression.
Evaluated to 0.000000
Parsed a top-level expression.
Evaluated to 1.000000
```

Now pyxc can ask a question. Next, make it choose between two answers.

## 5. Add and Parse the `if` Expression

Add an expression node that owns three child expressions:

```cpp
class IfExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Condition, Then, Else;

public:
  IfExpressionNode(unique_ptr<ExpressionNode> Condition,
                   unique_ptr<ExpressionNode> Then,
                   unique_ptr<ExpressionNode> Else)
      : Condition(std::move(Condition)), Then(std::move(Then)),
        Else(std::move(Else)) {}
  Value *codegen() override;
};
```

Both branches are required because this is an expression. No matter which path runs, the whole `if` must produce a value.

Add `ParseIfExpression()` before `ParsePrimary()`:

```cpp
static unique_ptr<ExpressionNode> ParseIfExpression() {
  getNextToken(); // eat 'if'

  auto Condition = ParseExpression();
  if (!Condition)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after 'if' condition");
  getNextToken(); // eat ':'
  consumeNewlines();

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  consumeNewlines();
  if (CurrentToken != tok_else)
    return LogErrorExpression("Expected 'else' in 'if' expression");
  getNextToken(); // eat 'else'

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after 'else'");
  getNextToken(); // eat ':'
  consumeNewlines();

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return make_unique<IfExpressionNode>(
      std::move(Condition), std::move(Then), std::move(Else));
}
```

Then add this case to `ParsePrimary()`:

```cpp
case tok_if:
  return ParseIfExpression();
```

Without that case, the lexer returns `tok_if`, but `ParsePrimary()` reports it as unexpected and never calls the new parser.

## 6. Lower `if` to Basic Blocks and a PHI

An `if` expression needs four pieces of IR:

```text
current block -> then block --+
             \-> else block --+-> merge block -> PHI result
```

Implement `IfExpressionNode::codegen()`:

```cpp
Value *IfExpressionNode::codegen() {
  Value *CondV = Condition->codegen();
  if (!CondV)
    return nullptr;

  CondV = TheBuilder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);
  TheBuilder->CreateCondBr(CondV, ThenBB, ElseBB);

  TheBuilder->SetInsertPoint(ThenBB);
  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;
  TheBuilder->CreateBr(MergeBB);
  ThenBB = TheBuilder->GetInsertBlock();

  TheBuilder->SetInsertPoint(ElseBB);
  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;
  TheBuilder->CreateBr(MergeBB);
  ElseBB = TheBuilder->GetInsertBlock();

  TheBuilder->SetInsertPoint(MergeBB);
  PHINode *PN = TheBuilder->CreatePHI(
      Type::getDoubleTy(*TheContext), 2, "iftmp");
  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}
```

The condition arrives as a `double`, so compare it with `0.0` to recover an `i1` branch condition. The PHI selects the value produced by the block that reached the merge.

Re-read `ThenBB` and `ElseBB` after generating each branch. A nested `if` can move the builder's insertion point to a new end block.

Build and try it:

```bash
cmake --build build
./build/pyxc
```

```pyxc
ready> def absdiff(a, b): if a > b: a - b else: b - a
ready> absdiff(10, 5)
ready> absdiff(3, 8)
```

Expected:

```text
Parsed a function definition.
Parsed a top-level expression.
Evaluated to 5.000000
Parsed a top-level expression.
Evaluated to 5.000000
```

That completes one decision:

```text
one condition -> one branch -> one PHI value
```

## 7. Add and Parse the `for` Expression

Next, add a loop expression with a start value, condition, step, and body:

```cpp
class ForExpressionNode : public ExpressionNode {
  string VariableName;
  unique_ptr<ExpressionNode> Start, Condition, Step, Body;

public:
  ForExpressionNode(const string &VariableName,
                    unique_ptr<ExpressionNode> Start,
                    unique_ptr<ExpressionNode> Condition,
                    unique_ptr<ExpressionNode> Step,
                    unique_ptr<ExpressionNode> Body)
      : VariableName(VariableName), Start(std::move(Start)),
        Condition(std::move(Condition)), Step(std::move(Step)),
        Body(std::move(Body)) {}
  Value *codegen() override;
};
```

For this chapter, a loop has this form:

```pyxc
for i = start, condition, step: body
```

Add `ParseForExpression()` before `ParsePrimary()`:

```cpp
static unique_ptr<ExpressionNode> ParseForExpression() {
  getNextToken(); // eat 'for'

  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected variable name after 'for'");
  string VariableName = Name;
  getNextToken(); // eat name

  if (CurrentToken != tok_assign)
    return LogErrorExpression("Expected '=' after 'for' variable");
  getNextToken(); // eat '='

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;
  if (CurrentToken != tok_comma)
    return LogErrorExpression("Expected ',' after 'for' start value");
  getNextToken();

  auto Condition = ParseExpression();
  if (!Condition)
    return nullptr;
  if (CurrentToken != tok_comma)
    return LogErrorExpression("Expected ',' after 'for' condition");
  getNextToken();

  auto Step = ParseExpression();
  if (!Step)
    return nullptr;
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after 'for' step");
  getNextToken();
  consumeNewlines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return make_unique<ForExpressionNode>(
      VariableName, std::move(Start), std::move(Condition),
      std::move(Step), std::move(Body));
}
```

Then add the dispatch case:

```cpp
case tok_for:
  return ParseForExpression();
```

## 8. Lower the Loop to a CFG

Use three new blocks:

```text
preheader -> loop_cond -> loop_body --+
                 |          ^        |
                 +-> after  +--------+
```

The loop variable is a PHI node. On the first visit it receives `StartVal`; on later visits it receives `NextVar` from the body back edge.

Implement `ForExpressionNode::codegen()`:

```cpp
Value *ForExpressionNode::codegen() {
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;
  BasicBlock *PreheaderBB = TheBuilder->GetInsertBlock();

  BasicBlock *CondBB = BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
  BasicBlock *BodyBB = BasicBlock::Create(*TheContext, "loop_body", TheFunction);
  BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "after_loop", TheFunction);

  TheBuilder->CreateBr(CondBB);
  TheBuilder->SetInsertPoint(CondBB);

  PHINode *Variable = TheBuilder->CreatePHI(
      Type::getDoubleTy(*TheContext), 2, VariableName);
  Variable->addIncoming(StartVal, PreheaderBB);

  Value *OldVal = NamedValues[VariableName];
  NamedValues[VariableName] = Variable;

  Value *CondVal = Condition->codegen();
  if (!CondVal)
    return nullptr;
  CondVal = TheBuilder->CreateFCmpONE(
      CondVal, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
  TheBuilder->CreateCondBr(CondVal, BodyBB, AfterBB);

  TheBuilder->SetInsertPoint(BodyBB);
  if (!Body->codegen())
    return nullptr;

  Value *StepVal = Step->codegen();
  if (!StepVal)
    return nullptr;
  Value *NextVar = TheBuilder->CreateFAdd(Variable, StepVal, "nextvar");

  BasicBlock *BodyEndBB = TheBuilder->GetInsertBlock();
  Variable->addIncoming(NextVar, BodyEndBB);
  TheBuilder->CreateBr(CondBB);

  TheBuilder->SetInsertPoint(AfterBB);
  if (OldVal)
    NamedValues[VariableName] = OldVal;
  else
    NamedValues.erase(VariableName);

  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

Saving and restoring `NamedValues[VariableName]` makes loop variables shadow outer variables without destroying the outer binding.

As with `if`, capture the actual body-end block after generating the body. A nested control-flow expression may have changed it.

Now run the immediate experiment:

```bash
cmake --build build
./build/pyxc
```

```pyxc
ready> extern def printd(x)
ready> for i = 1, i <= 3, 1: printd(i)
```

Expected:

```text
Parsed an extern.
Parsed a top-level expression.
1.000000
2.000000
3.000000
Evaluated to 0.000000
```

This is the complete loop boundary:

```text
one start value -> repeated condition/body/step -> one 0.0 result
```

## 9. Run the Mandelbrot Program

Comparisons, branches, and loops are enough to run a small Mandelbrot renderer. Use the existing program:

```bash
./build/pyxc test/mandel.pyxc
```

You should see a field of `*` characters with the Mandelbrot shape cut into it.

The program uses recursion in `mandelconverger` because pyxc does not have mutable variables yet. It uses `for` for the rows and columns, and an `if` expression to choose between a space and `*`:

```pyxc
putchard(if mandelconverge(x, y) > 255: 32 else: 42)
```

The host only supplies `putchard`. The iteration and branching are now compiled by pyxc itself.

## 10. Run the Chapter Tests

Finally, run the complete suite:

```bash
llvm-lit -v test/
```

Pay particular attention to:

```text
binary_ops_comparison.pyxc
if_else_inline.pyxc
if_else_multiline.pyxc
if_else_nested.pyxc
for_loop.pyxc
for_loop_nested.pyxc
for_loop_var_shadow.pyxc
mandel.pyxc
```

One subtle behavior is worth checking explicitly:

```pyxc
ready> 1 < 2 == 1
```

Expected:

```text
Parsed a top-level expression.
Evaluated to 1.000000
```

All comparisons share one precedence level and associate left to right, so pyxc parses that as `(1 < 2) == 1`. It does not implement Python-style chained comparisons.

## What You Built

Chapter 10 now has three independently testable pieces:

```text
comparison -> double boolean
if         -> PHI-selected double
for        -> loop CFG and 0.0
```

That is enough control flow to move beyond a calculator and start compiling real programs.

Next: [Chapter 11](chapter-11.md) adds mutable variables.

## Need Help?

Build issues? Questions?

- [Report a problem with GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- [Ask a question in GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your operating system and version
- The chapter number
- The exact command you ran
- The complete error message
- The output of `c++ --version` and `cmake --version`
- The output of `llvm-config --version` for Chapter 6 and later

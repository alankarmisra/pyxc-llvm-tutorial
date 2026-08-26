---
description: "Complete pyxc's loop story: while, do/while, break, and continue, with correct targets for nested loops and for loops."
---
# 14. pyxc: Loop Completeness

## What I Am Building

pyxc has had `for` loops since [Chapter 10](chapter-10.md), but that's the only loop form, and there's no way to leave one early. After this chapter, `while` and `do`/`while` join the language, and `break`/`continue` work correctly, including inside nested loops:

<!-- code-merge:start -->
```pyxc
ready> def stop_at_three():
  var i = 0
  while 1:
    if i == 3:
      break
    i = i + 1
  return i
```
```text
Parsed a function definition.
```
```pyxc
ready> stop_at_three()
```
```text
Parsed a top-level expression.
Evaluated to 3.000000
```
<!-- code-merge:end -->

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-14
```

## Grammar

Four new productions: `while-statement`, `do-while-statement`, `break-statement`, and `continue-statement`. `compound-statement` gains the first two; `simple-statement` gains the last two:

`code/chapter-14/pyxc.ebnf`

```grammardiff
*...
*for-statement                     = "for" [ "var" ] name "=" expression ","
*                                    expression "," expression ":" suite ;
+while-statement                   = "while" expression ":" suite ;
+do-while-statement                = "do" ":" suite [ end-of-lines ]
+                                    "while" expression ;
*variable-statement                = "var" variable-binding
*                                    { "," variable-binding } ;
*assignment-statement              = lvalue "=" expression ;
*simple-statement                  = return-statement
+                                    | break-statement
+                                    | continue-statement
*                                    | variable-statement
*                                    | assignment-statement
*                                    | expression ;
-compound-statement                = if-statement | for-statement ;
+compound-statement                = if-statement
+                                    | for-statement
+                                    | while-statement
+                                    | do-while-statement ;
*statement                         = simple-statement | compound-statement ;
*suite                             = simple-statement
*                                    | compound-statement
*                                    | end-of-lines block ;
*return-statement                  = "return" expression ;
+break-statement                   = "break" ;
+continue-statement                = "continue" ;
*statement-separator               = end-of-lines | BLOCK_END ;
*block                             = indent statement
*...
```

Note the `do`/`while` shape: the body comes first under `do:`, and the condition appears after `while` on its own line with no trailing colon — `do: ... while cond`, not `do: ... while cond:`.

## New Tokens and Keywords

Four new tokens:

```cppdiff
*enum Token {
*  ...
*  // loops
*  tok_for = -15,
+  tok_while = -21,
+  tok_do = -22,
+  tok_break = -23,
+  tok_continue = -24,
*
*  // mutable variables
*  ...
*};
```

Added to the keyword table alongside `for`:

```cppdiff
*static map<string, Token> Keywords = {
*    {"def", tok_def},       {"extern", tok_extern}, {"return", tok_return},
*    {"if", tok_if},         {"elif", tok_elif},     {"else", tok_else},
-    {"for", tok_for},
+    {"for", tok_for},       {"while", tok_while},   {"do", tok_do},
+    {"break", tok_break},   {"continue", tok_continue},
*    {"var", tok_var}};
```

## New AST Nodes

`WhileStatementNode` covers both `while` and `do`/`while` — an `IsDoWhile` flag tells codegen which block to branch to first:

```cpp
/// WhileStatementNode - Statement class for while and do/while loops.
class WhileStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Condition, Body;
  bool IsDoWhile;

public:
  WhileStatementNode(unique_ptr<ExpressionNode> Condition,
                     unique_ptr<ExpressionNode> Body, bool IsDoWhile)
      : Condition(std::move(Condition)), Body(std::move(Body)), IsDoWhile(IsDoWhile) {}
  Value *codegen() override;
};
```

`BreakStatementNode` and `ContinueStatementNode` carry no data at all — each just emits an unconditional branch at codegen time:

```cpp
class BreakStatementNode : public ExpressionNode {
public:
  Value *codegen() override;
};

class ContinueStatementNode : public ExpressionNode {
public:
  Value *codegen() override;
};
```

## Parse-Time Depth Tracking

A counter gates `break` and `continue` outside any loop, guarded automatically by RAII — the same pattern [Chapter 12](chapter-12.md) already used for variable scopes:

```cpp
static int ParseLoopDepth = 0;

struct ParseLoopGuard {
  ParseLoopGuard() { ++ParseLoopDepth; }
  ~ParseLoopGuard() { --ParseLoopDepth; }
};
```

`ParseBreakStatement` and `ParseContinueStatement` check the counter before accepting the keyword:

```cpp
static unique_ptr<ExpressionNode> ParseBreakStatement() {
  if (ParseLoopDepth <= 0)
    return LogErrorExpression("'break' used outside of a loop");
  getNextToken(); // eat 'break'
  return make_unique<BreakStatementNode>();
}

static unique_ptr<ExpressionNode> ParseContinueStatement() {
  if (ParseLoopDepth <= 0)
    return LogErrorExpression("'continue' used outside of a loop");
  getNextToken(); // eat 'continue'
  return make_unique<ContinueStatementNode>();
}
```

`ParseForStatement` now installs a `ParseLoopGuard` too, right alongside the scope guard it already had:

```cpp
unique_ptr<ExpressionNode> Start, Condition, Step, Body;
ParseLoopGuard ParseLoop;

unique_ptr<LoopScopeGuard> LoopScope;
if (IsVarDecl)
  LoopScope = make_unique<LoopScopeGuard>(VarName);
```

## Parsing `while` and `do`/`while`

`ParseWhileStatement` reads the condition first:

```cpp
/// while-statement
///   = "while" expression ":" suite ;
static unique_ptr<ExpressionNode> ParseWhileStatement() {
  getNextToken(); // eat 'while'
  auto Condition = ParseExpression();
  if (!Condition)
    return nullptr;
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after while condition");
  getNextToken(); // eat ':'

  ParseLoopGuard Loop;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  return make_unique<WhileStatementNode>(std::move(Condition), std::move(Body),
                                         false);
}
```

`ParseDoWhileStatement` reads the body first, then the condition after `while`:

```cpp
/// do-while-statement
///   = "do" ":" suite [ end-of-lines ] "while" expression ;
static unique_ptr<ExpressionNode> ParseDoWhileStatement() {
  getNextToken(); // eat 'do'
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after 'do'");
  getNextToken(); // eat ':'

  ParseLoopGuard Loop;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;

  if (CurrentToken == tok_block_end)
    getNextToken();
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_while)
    return LogErrorExpression("Expected 'while' after do body");
  getNextToken(); // eat 'while'

  auto Condition = ParseExpression();
  if (!Condition)
    return nullptr;
  return make_unique<WhileStatementNode>(std::move(Condition), std::move(Body), true);
}
```

Both parsers install a `ParseLoopGuard` around the body, so `break`/`continue` inside are accepted; the guard's destructor decrements `ParseLoopDepth` automatically when the function returns, whichever path it returns through. `ParseStatement` dispatches to both alongside `tok_if` and `tok_for`, and `ParseSimpleStatement` dispatches `tok_break`/`tok_continue` to their parsers:

```cpp
static unique_ptr<ExpressionNode> ParseSimpleStatement() {
  if (CurrentToken == tok_return)
    return ParseReturnStatement();
  if (CurrentToken == tok_break)
    return ParseBreakStatement();
  if (CurrentToken == tok_continue)
    return ParseContinueStatement();
  if (CurrentToken == tok_var)
    return ParseVarStatement();
  if (CurrentToken == tok_name)
    return ParseLeadingNameSimpleStatement();
  return ParseNonLeadingNameSimpleStatement();
}
```

## Codegen Targets for Loop Control

A single stack tracks break and continue targets for every loop type. Each entry holds two blocks:

```cpp
struct LoopControlTargets {
  BasicBlock *BreakTarget = nullptr;
  BasicBlock *ContinueTarget = nullptr;
};
static vector<LoopControlTargets> LoopControlStack;
```

Every loop's codegen pushes an entry on the way in and pops it on the way out, so the innermost active loop is always on top. `break` branches to `LoopControlStack.back().BreakTarget`; `continue` branches to `.ContinueTarget`. Nesting falls out of this for free: a `break` inside a nested loop only ever sees the innermost loop's targets, so it can only exit that loop.

`FunctionDefinitionNode::codegen` clears `LoopControlStack` alongside `NamedValues` at the start of every function, the same way it already reset `NamedValues` in [Chapter 11](chapter-11.md). Without it, a stack left non-empty by a codegen failure partway through a loop (e.g. in the REPL, after one bad top-level input) could leak stale targets into the next function compiled.

## While-Loop Codegen

Three basic blocks: `while_cond`, `while_body`, `while_after`. Only the entry branch and where the first condition check happens differ between `while` and `do`/`while`:

```cpp
Value *WhileStatementNode::codegen() {
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();
  BasicBlock *CondBB =
      BasicBlock::Create(*TheContext, "while_cond", TheFunction);
  BasicBlock *BodyBB =
      BasicBlock::Create(*TheContext, "while_body", TheFunction);
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "while_after", TheFunction);

  TheBuilder->CreateBr(IsDoWhile ? BodyBB : CondBB);

  if (!IsDoWhile) {
    TheBuilder->SetInsertPoint(CondBB);
    Value *ConditionValue = Condition->codegen();
    if (!ConditionValue)
      return nullptr;
    ConditionValue = TheBuilder->CreateFCmpONE(
        ConditionValue, ConstantFP::get(*TheContext, APFloat(0.0)),
        "whilecond");
    TheBuilder->CreateCondBr(ConditionValue, BodyBB, AfterBB);
  }

  TheBuilder->SetInsertPoint(BodyBB);
  LoopControlStack.push_back({AfterBB, CondBB});
  if (!Body->codegen()) {
    LoopControlStack.pop_back();
    return nullptr;
  }
  LoopControlStack.pop_back();
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateBr(CondBB);

  TheBuilder->SetInsertPoint(CondBB);
  if (IsDoWhile) {
    Value *ConditionValue = Condition->codegen();
    if (!ConditionValue)
      return nullptr;
    ConditionValue = TheBuilder->CreateFCmpONE(
        ConditionValue, ConstantFP::get(*TheContext, APFloat(0.0)),
        "dowhilecond");
    TheBuilder->CreateCondBr(ConditionValue, BodyBB, AfterBB);
  }

  TheBuilder->SetInsertPoint(AfterBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

For a plain `while`, the entry branch goes straight to `CondBB`, so the condition is checked before the body ever runs. For `do`/`while`, the entry branch goes to `BodyBB` directly, skipping `CondBB` entirely the first time through — `CondBB` still gets filled in afterward (that's the second `if (IsDoWhile)` block), so every iteration *after* the first one still checks the condition the normal way, before looping back.

The `ConstantFP::get(*TheContext, APFloat(0.0))` return at the end isn't special to `while` — it's the same "statements produce a dummy `0.0`" convention every statement-shaped node has used since [Chapter 11](chapter-11.md). pyxc still has no boolean type at the language level; `while 1:` and `while i < 5:` both work because every condition, however it's spelled, resolves to a `double` that's compared against `0.0`.

## `for` Loops Get a Dedicated Step Block

`ForStatementNode`'s codegen changes too. Before this chapter, the step expression ran inline at the end of the body block. Now it gets its own basic block, so `continue` has somewhere correct to jump to:

```cppdiff
*  BasicBlock *CondBB =
*      BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
*  BasicBlock *BodyBB =
*      BasicBlock::Create(*TheContext, "loop_body", TheFunction);
+  BasicBlock *StepBB =
+      BasicBlock::Create(*TheContext, "loop_step", TheFunction);
*  BasicBlock *AfterBB =
*      BasicBlock::Create(*TheContext, "after_loop", TheFunction);
```

The body's implicit fallthrough branch now targets `StepBB` instead of the condition block directly, and `StepBB` itself evaluates the step expression, stores the updated loop variable, and only then branches to the condition block:

```cpp
TheBuilder->SetInsertPoint(BodyBB);
LoopControlStack.push_back({AfterBB, StepBB});

if (!Body->codegen()) {
  LoopControlStack.pop_back();
  return nullptr;
}
LoopControlStack.pop_back();

if (!TheBuilder->GetInsertBlock()->getTerminator())
  TheBuilder->CreateBr(StepBB);

TheBuilder->SetInsertPoint(StepBB);

Value *StepVal = Step->codegen();
if (!StepVal)
  return nullptr;
Value *CurVar =
    TheBuilder->CreateLoad(Type::getDoubleTy(*TheContext), LoopVariableSlot,
                           VarName);
Value *NextVar = TheBuilder->CreateFAdd(CurVar, StepVal, "nextvar");
TheBuilder->CreateStore(NextVar, LoopVariableSlot);
TheBuilder->CreateBr(CondBB);
```

The `LoopControlTargets` pushed for a `for` loop sets `ContinueTarget = StepBB`, not the condition block. That's what makes `continue` inside a `for` loop run the step before re-checking the condition, matching C semantics, rather than skipping straight to the condition check the way `continue` in a `while` loop does — a `while` loop has no step to run, so its `ContinueTarget` is `CondBB` directly.

## `break` and `continue` Codegen

Both emit a single unconditional branch to whichever target is on top of `LoopControlStack`:

```cpp
Value *BreakStatementNode::codegen() {
  if (LoopControlStack.empty())
    return LogErrorValue("'break' used outside of a loop");
  TheBuilder->CreateBr(LoopControlStack.back().BreakTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *ContinueStatementNode::codegen() {
  if (LoopControlStack.empty())
    return LogErrorValue("'continue' used outside of a loop");
  TheBuilder->CreateBr(LoopControlStack.back().ContinueTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

The `CreateBr` terminates the current block; any code that would otherwise follow `break` or `continue` in the same source block never actually gets appended to it, since a well-formed basic block can only have one terminator. This is the same LLVM-level guarantee `return` already relies on, from [Chapter 12](chapter-12.md).

`LoopControlStack.empty()` at codegen time is a belt-and-suspenders check — `ParseLoopDepth` already rejects `break`/`continue` outside a loop at parse time, so codegen should never actually see one with an empty stack. The check stays because codegen makes no assumption about what the parser already caught.

## Build and Run

```bash
cd code/chapter-14
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

**`while` loop:**

<!-- code-merge:start -->
```pyxc
ready> def sum_to_four():
  var i = 0
  var sum = 0
  while i < 5:
    sum = sum + i
    i = i + 1
  return sum
```
```text
Parsed a function definition.
```
```pyxc
ready> sum_to_four()
```
```text
Parsed a top-level expression.
Evaluated to 10.000000
```
<!-- code-merge:end -->

**`do`/`while` always runs the body once**, even when the condition is false from the start:

<!-- code-merge:start -->
```pyxc
ready> def once():
  var i = 0
  do:
    i = i + 1
  while i < 1
  return i
```
```text
Parsed a function definition.
```
```pyxc
ready> once()
```
```text
Parsed a top-level expression.
Evaluated to 1.000000
```
<!-- code-merge:end -->

**`continue` in a `for` loop still runs the step** — it doesn't get stuck re-checking the same `i`:

<!-- code-merge:start -->
```pyxc
ready> def skip_two():
    var sum = 0
    for var i = 0, i < 5, 1:
        if i == 2:
            continue
        sum = sum + i
    return sum
```
```text
Parsed a function definition.
```
```pyxc
ready> skip_two()
```
```text
Parsed a top-level expression.
Evaluated to 8.000000
```
<!-- code-merge:end -->

`sum` skips `i == 2` (0 + 1 + 3 + 4 = 8).

**`break` targets only the innermost loop:**

<!-- code-merge:start -->
```pyxc
ready> def nested_break():
    var count = 0
    for var i = 0, i < 3, 1:
        for var j = 0, j < 3, 1:
            if j == 1:
                break
            count = count + 1
    return count
```
```text
Parsed a function definition.
```
```pyxc
ready> nested_break()
```
```text
Parsed a top-level expression.
Evaluated to 3.000000
```
<!-- code-merge:end -->

The inner loop's `break` fires when `j == 1`, so each of the 3 outer iterations only ever counts `j == 0` — 3 total, not 9. The outer loop is untouched.

**`break` outside a loop is a parse-time error:**

<!-- code-merge:start -->
```pyxc
ready> def bad():
  break
  return 0
```
```text
Error (Line 2, Column 3): 'break' used outside of a loop
  break
  ^~~~
Error (Line 3, Column 3): unknown token when expecting an expression
  return 
  ^~~~
```
<!-- code-merge:end -->

The first line is the real error — `ParseLoopDepth` is `0` at the top level, so `ParseBreakStatement` rejects it immediately. The second error is the parser trying to recover from the token stream that's left over, the same cascading behavior [Chapter 12](chapter-12.md) and [Chapter 13](chapter-13.md) already showed for other parse failures.

## What's Next

[Chapter 15](chapter-15.md) adds global variables.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

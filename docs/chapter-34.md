---
description: "Complete pyxc's loop story: while, do/while, break, and continue, with correct targets for nested loops and for loops."
---
# 34. pyxc: Loop Completeness

## Where We Are

[Chapter 33](chapter-33.md) added logical operators. pyxc has had `for` loops since [Chapter 9](chapter-09.md), but that is the only loop form. After this chapter, `while` and `do/while` join the language, and `break` and `continue` work correctly across nested loops:

```pyxc
extern def printd(x: float64)

def collatz(n: int) -> int:
  var x: int = n
  var steps: int = 0
  while x != 1:
    if x % 2 == 0:
      x /= 2
    else:
      x = x * 3 + 1
    steps++
  return steps

def main() -> int:
  printd(float64(collatz(27)))
  return 0
```

```
111.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-34
```

## New Tokens and Keywords

Four new tokens:

```cpp
tok_while    = -52,
tok_do       = -53,
tok_break    = -54,
tok_continue = -55,
```

They are added to the keyword table alongside existing keywords:

```cpp
{"while", tok_while}, {"do", tok_do},
{"break", tok_break}, {"continue", tok_continue},
```

## New AST Nodes

Three nodes handle the new constructs.

**`WhileExprAST`** covers both `while` and `do/while`. An `IsDoWhile` flag tells codegen which block to branch to first:

```cpp
class WhileExprAST : public ExprAST {
  unique_ptr<ExprAST> Cond;
  unique_ptr<ExprAST> Body;
  bool IsDoWhile;
public:
  WhileExprAST(unique_ptr<ExprAST> Cond, unique_ptr<ExprAST> Body,
               bool IsDoWhile)
      : Cond(std::move(Cond)), Body(std::move(Body)), IsDoWhile(IsDoWhile) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};
```

**`BreakExprAST`** and **`ContinueExprAST`** carry no data — they emit an unconditional branch at codegen time:

```cpp
class BreakExprAST : public ExprAST {
public:
  BreakExprAST() { setType(ValueType::None); }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class ContinueExprAST : public ExprAST {
public:
  ContinueExprAST() { setType(ValueType::None); }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};
```

## Parse-Time Depth Tracking

A counter gates `break` and `continue` outside any loop. An RAII guard increments and decrements it automatically:

```cpp
static int ParseLoopDepth = 0;

struct ParseLoopGuard {
  ParseLoopGuard()  { ++ParseLoopDepth; }
  ~ParseLoopGuard() { --ParseLoopDepth; }
};
```

`ParseBreakStmt` and `ParseContinueStmt` check the counter before accepting the keyword:

```cpp
static unique_ptr<ExprAST> ParseBreakStmt() {
  if (ParseLoopDepth <= 0)
    return LogError("'break' used outside of a loop");
  getNextToken(); // eat 'break'
  return make_unique<BreakExprAST>();
}

static unique_ptr<ExprAST> ParseContinueStmt() {
  if (ParseLoopDepth <= 0)
    return LogError("'continue' used outside of a loop");
  getNextToken(); // eat 'continue'
  return make_unique<ContinueExprAST>();
}
```

## `ParseWhileStmt` and `ParseDoWhileStmt`

`ParseWhileStmt` reads the condition first:

```cpp
static unique_ptr<ExprAST> ParseWhileStmt() {
  getNextToken(); // eat 'while'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogError("While loop condition must be bool");
  if (CurTok != ':')
    return LogError("Expected ':' after while condition");
  getNextToken(); // eat ':'
  ParseLoopGuard LoopGuard;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  return make_unique<WhileExprAST>(std::move(Cond), std::move(Body),
                                   /*IsDoWhile=*/false);
}
```

`ParseDoWhileStmt` reads the body first, then the condition after `while`:

```cpp
static unique_ptr<ExprAST> ParseDoWhileStmt() {
  getNextToken(); // eat 'do'
  if (CurTok != ':')
    return LogError("Expected ':' after 'do'");
  getNextToken(); // eat ':'
  ParseLoopGuard LoopGuard;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  if (CurTok == tok_block_end)
    getNextToken();
  if (CurTok == tok_eol)
    consumeNewlines();
  if (CurTok != tok_while)
    return LogError("Expected 'while' after do-body");
  getNextToken(); // eat 'while'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogError("Do-while condition must be bool");
  return make_unique<WhileExprAST>(std::move(Cond), std::move(Body),
                                   /*IsDoWhile=*/true);
}
```

Both parsers install a `ParseLoopGuard` around the body so `break`/`continue` inside are accepted. The guard is destroyed automatically when the function returns, decrementing `ParseLoopDepth`.

Both `ParseWhileStmt` and `ParseDoWhileStmt` are wired into the compound statement dispatcher alongside `tok_if`, `tok_for`.

## `LoopControlStack` — Codegen Targets

A single stack tracks break and continue targets for all loop types. Each entry holds two blocks:

```cpp
struct LoopControlTargets {
  BasicBlock *BreakTarget    = nullptr;  // where 'break' jumps
  BasicBlock *ContinueTarget = nullptr;  // where 'continue' jumps
};
static std::vector<LoopControlTargets> LoopControlStack;
```

Every loop codegen pushes on entry and pops on exit. The innermost loop is always on top. `break` branches to `back().BreakTarget`; `continue` branches to `back().ContinueTarget`.

## `WhileExprAST::codegen`

Three basic blocks are created: `while_cond`, `while_body`, `while_after`. The entry branch and condition evaluation differ between `while` and `do/while`:

```cpp
Value *WhileExprAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  BasicBlock *CondBB  = BasicBlock::Create(*TheContext, "while_cond", TheFunction);
  BasicBlock *BodyBB  = BasicBlock::Create(*TheContext, "while_body", TheFunction);
  BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "while_after", TheFunction);

  if (IsDoWhile) {
    Builder->CreateBr(BodyBB);   // do/while: enter body unconditionally
  } else {
    Builder->CreateBr(CondBB);   // while: check condition first
  }

  if (!IsDoWhile) {
    Builder->SetInsertPoint(CondBB);
    Value *CondVal = Cond->codegen();
    CondVal = ToBool(CondVal, Cond->getType());
    Builder->CreateCondBr(CondVal, BodyBB, AfterBB);
  }

  Builder->SetInsertPoint(BodyBB);
  LoopControlStack.push_back({AfterBB, CondBB});
  if (!Body->codegen()) {
    LoopControlStack.pop_back();
    return nullptr;
  }
  LoopControlStack.pop_back();
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(CondBB);

  Builder->SetInsertPoint(CondBB);
  if (IsDoWhile || !CondBB->getTerminator()) {
    Value *CondVal = Cond->codegen();
    CondVal = ToBool(CondVal, Cond->getType());
    Builder->CreateCondBr(CondVal, BodyBB, AfterBB);
  }

  Builder->SetInsertPoint(AfterBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

For a regular `while`, the body falls back to `CondBB`; for `do/while`, the body also falls to `CondBB`, but the initial branch skips `CondBB` entirely and lands in `BodyBB`.

## `for` Loop Gets a `StepBB`

The existing `for` loop codegen is updated. Previously, the step expression was evaluated inline at the end of the body block. Now it gets its own dedicated basic block so `continue` can jump to it correctly:

```cpp
BasicBlock *StepBB = BasicBlock::Create(*TheContext, "loop_step", TheFunction);
```

The body's implicit branch now goes to `StepBB` instead of the condition block. `StepBB` evaluates the step expression and then branches to the condition. The `LoopControlTargets` pushed for the `for` loop sets `ContinueTarget = StepBB`, matching C semantics: `continue` in a `for` loop runs the step.

```cpp
LoopControlStack.push_back({AfterBB, StepBB});
```

## `BreakExprAST::codegen` and `ContinueExprAST::codegen`

Both emit a single unconditional branch and then stop generating code for the current block (the caller sees the terminator and skips further statements):

```cpp
Value *BreakExprAST::codegen() {
  if (LoopControlStack.empty())
    return LogErrorV("'break' used outside of a loop");
  Builder->CreateBr(LoopControlStack.back().BreakTarget);
  // caller checks for terminator; no further instructions emitted
}

Value *ContinueExprAST::codegen() {
  if (LoopControlStack.empty())
    return LogErrorV("'continue' used outside of a loop");
  Builder->CreateBr(LoopControlStack.back().ContinueTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

## Block Parser Fix

The block parser loop condition is tightened. Previously it stopped on `tok_block_end` or `tok_dedent`; now it stops only on `tok_dedent` (or `tok_eof`) and handles `tok_block_end` inline:

```cpp
while (CurTok != tok_dedent && CurTok != tok_eof) {
  if (...) {
    continue;
  }
  if (CurTok == tok_block_end) {
    getNextToken();
    continue;
  }
  ...
}
```

This prevents `tok_block_end` from accidentally terminating a block mid-parse in nested constructs.

## Grammar

```ebnf
whilestmt    = "while" expression ":" suite ;                    -- new
dowhilestmt  = "do" ":" suite eols "while" expression ;          -- new
compoundstmt = ifstmt | forstmt | whilestmt | dowhilestmt ;      -- changed
simplestmt   = returnstmt | breakstmt | continuestmt
             | varstmt | assignstmt | expression ;               -- changed
breakstmt    = "break" ;                                          -- new
continuestmt = "continue" ;                                       -- new
```

Note the `do/while` form: the body comes first under `do:`, the condition appears after `while` on a separate line with no trailing colon.

## Error Cases

**`break` outside a loop:**
```pyxc
def main() -> int:
  break  # Error: 'break' used outside of a loop
  return 0
```

**While condition is not bool:**
```pyxc
var n: int = 5
while n:     # Error: While loop condition must be bool
  n -= 1
```

## Things Worth Knowing

**`do/while` uses the same AST node as `while`.** The `IsDoWhile` flag is the only structural difference. Codegen for both lives in `WhileExprAST::codegen`.

**`continue` target differs between loop types.** In a `while` loop, `continue` goes to the condition block. In a `for` loop, `continue` goes to `StepBB` — the step expression always runs. The `LoopControlStack` stores the right target per loop.

**The loop condition must be `bool`.** There is no implicit `int → bool` coercion. Use an explicit comparison: `while n != 0:` not `while n:`.

**Nesting is automatic.** The stack means the innermost loop's targets are always on top. `break` inside a nested loop exits only the inner loop.

## What's Next

[Chapter 35](chapter-35.md) adds bitwise operators: `&`, `|`, `^`, `<<`, `>>`, and `~`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

# 18. Real Loop Control (while, do, break, continue) and Finishing Core Integer Operators

Chapter 17 gave us a typed language with practical output (`print(...)`) and a solid test workflow.

Chapter 18 moves the language forward in two ways:

- control-flow expressiveness (`while`, `do-while`, `break`, `continue`)
- operator completeness for common integer work (`~`, `%`, `&`, `^`, `|`)

The key theme is still the same as Chapter 17: make targeted compiler changes, lock behavior with `lit` tests, and keep each feature small and understandable.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter18](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter18).

## What Changed from Chapter 17

If you compare:

- `code/chapter17/pyxc.cpp`
- `code/chapter18/pyxc.cpp`

you will see Chapter 18 adds four layers:

1. new lexer tokens/keywords for loop-control statements
2. new AST statement nodes + parser paths for those statements
3. loop-context plumbing in codegen so `break` and `continue` always know where to branch
4. integer operator support in expression codegen for `%`, `&`, `^`, `|`, and unary `~`

We also expanded the Chapter 18 test suite in `code/chapter18/test/` to cover both positive and negative cases.

## Language Surface in This Chapter

New statement forms:

```py
while cond:
    body
```

```py
do:
    body
while cond
```

```py
while cond:
    if should_exit:
        break
    continue
```

Additional operators supported in this chapter:

```py
~x
x % y
x & y
x ^ y
x | y
```

## Tests First: Chapter 18 lit Suite

Before discussing implementation details, it helps to look at the behavior we pinned down in tests.

Representative loop/control tests:

- `code/chapter18/test/while_counting.pyxc`
- `code/chapter18/test/do_while_runs_once.pyxc`
- `code/chapter18/test/nested_break_outer_continue.pyxc`
- `code/chapter18/test/continue_skips_body.pyxc`
- `code/chapter18/test/break_outside_loop.pyxc`
- `code/chapter18/test/continue_outside_loop.pyxc`
- `code/chapter18/test/malformed_do_while.pyxc`

Representative operator tests:

- `code/chapter18/test/operator_modulo_signed.pyxc`
- `code/chapter18/test/operator_bitwise_basic.pyxc`
- `code/chapter18/test/operator_bitwise_precedence.pyxc`
- `code/chapter18/test/operator_unary_bitnot.pyxc`
- `code/chapter18/test/operator_error_modulo_float.pyxc`
- `code/chapter18/test/operator_error_bitwise_float.pyxc`
- `code/chapter18/test/operator_error_unary_bitnot_float.pyxc`

Example positive loop test:

```py
# RUN: %pyxc -i %s > %t 2>&1
# RUN: grep -x '0' %t
# RUN: grep -x '1' %t
# RUN: grep -x '2' %t
# RUN: ! grep -q "Error (Line:" %t

def main() -> i32:
    i: i32 = 0
    while i < 3:
        print(i)
        i = i + 1
    return 0

main()
```

Example negative loop-control test:

```py
# RUN: %pyxc -i %s > %t 2>&1
# RUN: grep -q "break" %t
# RUN: grep -q "outside of a loop" %t

def main() -> i32:
    break
    return 0

main()
```

Example positive operator test:

```py
# RUN: %pyxc -i %s > %t 2>&1
# RUN: grep -x '2 7 5' %t
# RUN: ! grep -q "Error (Line:" %t

def main() -> i32:
    a: i32 = 6
    b: i32 = 3
    print(a & b, a | b, a ^ b)
    return 0

main()
```

## Lexer and Token Updates

Chapter 18 adds dedicated loop-control tokens to the existing `Token` enum:

```cpp
tok_while = -28,
tok_do = -29,
tok_break = -30,
tok_continue = -31,
```

Then it wires the corresponding keywords into the keyword map:

```cpp
{"and", tok_and}, {"print", tok_print},   {"while", tok_while},
{"do", tok_do},   {"break", tok_break},   {"continue", tok_continue},
{"or", tok_or}
```

The practical benefit is that parser dispatch stays explicit and straightforward: no special identifier heuristics are needed for these statements.

## AST Additions for New Statements

Chapter 17 already had dedicated statement nodes (`IfStmtAST`, `ForStmtAST`, `PrintStmtAST`, etc.).

Chapter 18 continues that pattern with:

```cpp
class WhileStmtAST : public StmtAST { ... };
class DoWhileStmtAST : public StmtAST { ... };
class BreakStmtAST : public StmtAST { ... };
class ContinueStmtAST : public StmtAST { ... };
```

Two subtle details are important:

- `BreakStmtAST` and `ContinueStmtAST` return `true` from `isTerminator()`
- they are statements, not expressions, so they slot naturally into suites/blocks

That terminator flag keeps later codegen logic from accidentally adding extra fallthrough branches after a `break` or `continue`.

## Parser Additions (Step by Step)

`ParseStmt()` in Chapter 18 now dispatches loop-control forms directly:

```cpp
case tok_while:
  return ParseWhileStmt();
case tok_do:
  return ParseDoWhileStmt();
case tok_break:
  return ParseBreakStmt();
case tok_continue:
  return ParseContinueStmt();
```

### Parsing while

`while` follows the same design style as `if` and `for`:

```cpp
// while_stmt = "while" , expression , ":" , suite ;
```

The parser:

1. consumes `while`
2. parses the condition expression
3. requires `:`
4. parses a suite (inline or block)

No surprises, and that consistency helps readability.

### Parsing do-while

The Chapter 18 grammar shape is:

```cpp
// do_while_stmt = "do" , ":" , suite , "while" , expression ;
```

The ordering matters here and is intentional:

1. parse `do:`
2. parse body suite first
3. require trailing `while`
4. parse condition expression

That parse order directly matches post-test loop semantics.

### Parsing break / continue

Parse-time behavior is intentionally simple:

```cpp
getNextToken(); // eat break or continue
return std::make_unique<BreakStmtAST>(Loc);
```

Syntactically this accepts the statement where it appears. Semantic validation (inside loop or not) is delayed to codegen, where loop nesting context is available.

## Semantic Backbone: Loop Context Stack

`break` and `continue` need target blocks, and those targets depend on which loop we are currently inside.

Chapter 18 introduces:

```cpp
struct LoopContext {
  BasicBlock *BreakTarget = nullptr;
  BasicBlock *ContinueTarget = nullptr;
};
static std::vector<LoopContext> LoopContextStack;
```

Then it uses a small RAII helper:

```cpp
class LoopContextGuard {
public:
  LoopContextGuard(BasicBlock *BreakTarget, BasicBlock *ContinueTarget) {
    LoopContextStack.push_back({BreakTarget, ContinueTarget});
  }
  ~LoopContextGuard() {
    LoopContextStack.pop_back();
  }
};
```

This gives us clean behavior for nested loops:

- inner loop pushes its context
- `break`/`continue` bind to the nearest loop (top of stack)
- context is restored automatically when leaving the loop body

## LLVM Lowering for Loop Control

### break and continue

Codegen first verifies loop context exists:

```cpp
if (LoopContextStack.empty())
  return LogError<Value *>("`break` used outside of a loop");
Builder->CreateBr(LoopContextStack.back().BreakTarget);
```

`continue` is the same shape, branching to `ContinueTarget`.

This gives clear diagnostics for invalid usage and clean branches for valid usage.

### while lowering

The block layout is:

- `while.cond`
- `while.body`
- `while.exit`

Core structure:

```cpp
Builder->CreateBr(CondBB);
Builder->SetInsertPoint(CondBB);
CondV = ToBoolI1(Cond->codegen(), "whilecond");
Builder->CreateCondBr(CondV, BodyBB, ExitBB);

LoopContextGuard Guard(ExitBB, CondBB);
Body->codegen();
if (!Builder->GetInsertBlock()->getTerminator())
  Builder->CreateBr(CondBB);
```

A useful consequence is that `continue` inside `while` naturally returns to condition evaluation.

### do-while lowering

The block layout is:

- `do.body`
- `do.cond`
- `do.exit`

Core structure:

```cpp
Builder->CreateBr(BodyBB); // enter body first

LoopContextGuard Guard(ExitBB, CondBB);
Body->codegen();
if (!Builder->GetInsertBlock()->getTerminator())
  Builder->CreateBr(CondBB);

CondV = ToBoolI1(Cond->codegen(), "docond");
Builder->CreateCondBr(CondV, BodyBB, ExitBB);
```

The initial unconditional branch into `BodyBB` is the defining property: the loop body executes at least once.

### Existing for behavior, now with continue correctness

`for range(...)` already existed, but Chapter 18 adapts its loop context so `continue` targets the step block:

```cpp
LoopContextGuard Guard(EndLoopBB, StepBB);
```

So `continue` in a `for` body does what users expect:

1. jump to step
2. update induction variable
3. re-check loop condition

## Operator Coverage: %, &, ^, |, ~

This chapter line also closes a practical operator gap.

### Precedence table extension

`BinopPrecedence` now includes:

```cpp
{'|', 7}, {'^', 8}, {'&', 9}, ... {'%', 40}
```

This preserves sensible C-style relative precedence among bitwise operators and keeps `%` at multiplicative precedence.

### Unary ~

Unary bit-not is now implemented for integers:

```cpp
case '~':
  if (!OperandV->getType()->isIntegerTy())
    return LogError<Value *>("Unary '~' requires integer operand");
  return Builder->CreateNot(OperandV, "bnottmp");
```

If the operand is floating-point, the user gets a direct diagnostic.

### Binary %, &, ^, |

These are intentionally integer-only in this chapter.

First, codegen detects operators that require integer operands:

```cpp
bool RequiresIntOnly = (Op == '%' || Op == '&' || Op == '^' || Op == '|');
```

Then it enforces type rules with explicit diagnostics:

```cpp
if (!(L->getType()->isIntegerTy() && R->getType()->isIntegerTy())) {
  if (Op == '%')
    return LogError<Value *>("Modulo operator '%' requires integer operands");
  return LogError<Value *>("Bitwise operators require integer operands");
}
```

After harmonizing integer widths, lowering is direct:

```cpp
case '%': return Builder->CreateSRem(L, R, "modtmp");
case '&': return Builder->CreateAnd(L, R, "andtmp");
case '^': return Builder->CreateXor(L, R, "xortmp");
case '|': return Builder->CreateOr(L, R, "ortmp");
```

## Validation Commands

Run the Chapter 18 suite:

```bash
lit -sv code/chapter18/test
```

Optional: verify Chapter 17 behavior in the same environment:

```bash
lit -sv -j 1 code/chapter17/test
```

(`-j 1` can be useful if your local setup shows parallel-test flakiness.)

## Closing Thoughts

Chapter 18 is a good example of incremental compiler growth.

We did not redesign the compiler. We extended the existing architecture in place:

- lexer: a few new tokens
- parser: a few new statement branches
- AST: a few new statement nodes
- codegen: explicit loop context and branch targets
- tests: concrete behavior first, including failure paths

By the end of the chapter, control flow is much closer to what users expect in day-to-day code, and integer expression support is meaningfully more complete.

## Build and Test (Chapter 18)

From the repository root:

```bash
cd code/chapter18
make
```

Run the chapter test suite:

```bash
lit -sv test
```

If you also want to sanity-check the previous chapter in the same environment:

```bash
cd ../chapter17
make
lit -sv -j 1 test
```


## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
make -C code/chapter18 clean all
```

Run one sample program:

```bash
code/chapter18/pyxc -i code/chapter18/test/break_outside_loop.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter18/test
lit -sv .
```

Try editing a test or two and see how quickly you can predict the outcome.

When you're done, clean artifacts:

```bash
make -C code/chapter18 clean
```

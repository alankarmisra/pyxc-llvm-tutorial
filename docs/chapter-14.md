---
description: "Complete pyxc's loop story: while, do/while, break, and continue, with correct targets for nested loops and for loops."
---
# 14. pyxc: Loop Completeness

## What I Am Building

[Chapter 21](chapter-21.md) added logical operators. pyxc has had `for` loops since [Chapter 10](chapter-10.md), but that's the only loop form. After this chapter, `while` and `do`/`while` join the language, and `break` and `continue` work correctly across nested loops:

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

```text
111.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-34
```

## Grammar

Four new productions: `while-statement`, `do-while-statement`, `break-statement`, and `continue-statement`. `compound-statement` gains the first two; `simple-statement` gains the last two:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
 trait-definition        = "trait" name [ "[" name "]" ] ":" end-of-lines trait-block ;
 trait-block      = indent trait-method-signature { end-of-lines trait-method-signature } dedent ;
 trait-method-signature  = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 class-definition        = "class" name [ "(" trait-reference { "," trait-reference } ")" ] ":" end-of-lines struct-block ;
 trait-reference        = name [ "[" type "]" ] ;
 implementation-definition         = "impl" trait-reference "for" name ":" end-of-lines implementation-block ;
 implementation-block       = indent implementation-method { end-of-lines implementation-method } dedent ;
 implementation-method      = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 struct-block     = indent class-member { end-of-lines class-member } dedent ;
 class-member     = [ visibility ] ( field-declaration | method-definition ) ;
 visibility      = "public" | "private" ;
 method-definition       = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")"
                   [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 field-declaration       = name ":" type ;
 function-definition      = "def" function-signature [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 (* If the return type is omitted, it defaults to None. *)
 external        = "extern" "def" function-signature [ "->" type ] ;
 top-level-expression    = expression ;
 function-signature       = name "(" [ typed-parameter { "," typed-parameter } ] ")" ;
 typed-parameter      = name ":" type ;
 if-statement          = "if" expression ":" suite
                 [ end-of-lines "else" ":" suite ] ;
 for-statement         = "for"
                   ( "var" name ":" type | name )
                   "=" expression "," expression "," expression ":" suite ;
+while-statement       = "while" expression ":" suite ;
+do-while-statement     = "do" ":" suite end-of-lines "while" expression ;
 variable-statement         = "var" variable-binding { "," variable-binding } ;
 assignment-statement      = lvalue assignment-operator expression ; (* assignment is a statement here *)
-simple-statement      = return-statement | variable-statement | assignment-statement | expression ;
-compound-statement    = if-statement | for-statement ;
+simple-statement      = return-statement | break-statement | continue-statement | variable-statement | assignment-statement | expression ;
+compound-statement    = if-statement | for-statement | while-statement | do-while-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
+break-statement       = "break" ;
+continue-statement    = "continue" ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
 expression      = logical-or ;
 logical-or      = logical-and { "||" logical-and } ;
 logical-and     = comparison { "&&" comparison } ;
 comparison      = sum { comparison-operator sum } ;
 comparison-operator = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum             = term { ("+" | "-") term } ;
 term            = unary-expression { ("*" | "/" | "%") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = ("-" | "!" | "++" | "--") unary-expression | postfix-expression ;
 postfix-expression     = primary [ postfix-operator ] ;
 postfix-operator       = "++" | "--" ;
 primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
 cast-expression        = cast-type "(" expression ")" ;
 sizeof-expression      = "sizeof" "(" type ")" ;
 address-expression        = "addr" "(" lvalue ")" ;
 name-expression  = name | call-expression | method-call-expression | constructor-call-expression ;
 call-expression        = name "(" [ expression { "," expression } ] ")" ;
 method-call-expression  = name "." name "(" [ expression { "," expression } ] ")" ;
 constructor-call-expression    = name "(" [ expression { "," expression } ] ")" ;
 field-access     = name "." name { "." name } ;
 index-expression       = name "[" expression "]" ;
 number-expression      = number ;
 array-literal    = "[" [ expression { "," expression } ] "]" ;
 string-literal   = "\"" { ? any char except " and newline ? | escape } "\"" ;
 escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
 parenthesized-expression       = "(" expression ")" ;
 indent          = INDENT ;
 dedent          = DEDENT ;
 
 assignment-operator        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 name      = (letter | "_") { letter | digit | "_" } ;
 builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
                 | "float" | "float32" | "float64"
                 | "bool" | "None" ;
 alias-type       = name ;
 struct-type      = name ;
 pointer-type     = "ptr" "[" type "]" ;
 type            = base-type [ array-suffix ] ;
 base-type        = builtin-type | alias-type | struct-type | pointer-type ;
 array-suffix     = "[" integer "]" ;
 cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
                 | "float" | "float32" | "float64"
                 | "bool" | pointer-type ;
 integer         = digit { digit } ;
 number          = ( digit { digit } [ "." { digit } ]
                   | "." digit { digit } ) [ exponent ] ;
 exponent        = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
 boolean-literal    = "True" | "False" ;
 letter          = "A".."Z" | "a".."z" ;
 digit           = "0".."9" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
 INDENT          = ? synthetic token emitted by lexer ? ;
 DEDENT          = ? synthetic token emitted by lexer ? ;
 
 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
```

Note the `do`/`while` shape: the body comes first under `do:`, and the condition appears after `while` on its own line with no trailing colon — `do: ... while cond`, not `do: ... while cond:`.

## New Tokens and Keywords

Four new tokens:

```cpp
tok_while    = -52,
tok_do       = -53,
tok_break    = -54,
tok_continue = -55,
```

Registered in the keyword table alongside every other keyword.

## New AST Nodes

Three nodes handle the new constructs. `WhileExpressionNode` covers both `while` and `do`/`while`; an `IsDoWhile` flag tells codegen which block to branch to first:

```cpp
class WhileExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Cond;
  unique_ptr<ExpressionNode> Body;
  bool IsDoWhile;

public:
  WhileExpressionNode(unique_ptr<ExpressionNode> Cond, unique_ptr<ExpressionNode> Body,
               bool IsDoWhile)
      : Cond(std::move(Cond)), Body(std::move(Body)), IsDoWhile(IsDoWhile) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};
```

`BreakExpressionNode` and `ContinueExpressionNode` carry no data at all; each just emits an unconditional branch at codegen time:

```cpp
class BreakExpressionNode : public ExpressionNode {
public:
  BreakExpressionNode() { setType(ValueType::None); }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class ContinueExpressionNode : public ExpressionNode {
public:
  ContinueExpressionNode() { setType(ValueType::None); }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};
```

## Parse-Time Depth Tracking

A counter gates `break` and `continue` outside any loop, guarded automatically by RAII:

```cpp
static int ParseLoopDepth = 0;

struct ParseLoopGuard {
  ParseLoopGuard()  { ++ParseLoopDepth; }
  ~ParseLoopGuard() { --ParseLoopDepth; }
};
```

`ParseBreakStatement` and `ParseContinueStatement` check the counter before accepting the keyword:

```cpp
static unique_ptr<ExpressionNode> ParseBreakStatement() {
  if (ParseLoopDepth <= 0)
    return LogErrorExpression("'break' used outside of a loop");
  getNextToken(); // eat 'break'
  return make_unique<BreakExpressionNode>();
}

static unique_ptr<ExpressionNode> ParseContinueStatement() {
  if (ParseLoopDepth <= 0)
    return LogErrorExpression("'continue' used outside of a loop");
  getNextToken(); // eat 'continue'
  return make_unique<ContinueExpressionNode>();
}
```

## Parsing `while` and `do`/`while`

`ParseWhileStatement` reads the condition first:

```cpp
static unique_ptr<ExpressionNode> ParseWhileStatement() {
  getNextToken(); // eat 'while'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogErrorExpression("While loop condition must be bool");
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after while condition");
  getNextToken(); // eat ':'
  ParseLoopGuard LoopGuard;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  return make_unique<WhileExpressionNode>(std::move(Cond), std::move(Body),
                                   /*IsDoWhile=*/false);
}
```

`ParseDoWhileStatement` reads the body first, then the condition after `while`:

```cpp
static unique_ptr<ExpressionNode> ParseDoWhileStatement() {
  getNextToken(); // eat 'do'
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after 'do'");
  getNextToken(); // eat ':'
  ParseLoopGuard LoopGuard;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  if (CurrentToken == tok_block_end)
    getNextToken();
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_while)
    return LogErrorExpression("Expected 'while' after do-body");
  getNextToken(); // eat 'while'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogErrorExpression("Do-while condition must be bool");
  return make_unique<WhileExpressionNode>(std::move(Cond), std::move(Body),
                                   /*IsDoWhile=*/true);
}
```

Both parsers install a `ParseLoopGuard` around the body, so `break`/`continue` inside are accepted; the guard's destructor decrements `ParseLoopDepth` automatically when the function returns, whichever path it returns through. Both functions are wired into the compound-statement dispatcher alongside `tok_if` and `tok_for`.

## Codegen Targets for Loop Control

A single stack tracks break and continue targets for every loop type. Each entry holds two blocks:

```cpp
struct LoopControlTargets {
  BasicBlock *BreakTarget = nullptr;
  BasicBlock *ContinueTarget = nullptr;
};
static std::vector<LoopControlTargets> LoopControlStack;
```

Every loop's codegen pushes an entry on the way in and pops it on the way out, so the innermost active loop is always on top. `break` branches to `LoopControlStack.back().BreakTarget`; `continue` branches to `.ContinueTarget`. Nesting falls out of this for free: a `break` inside a nested loop only ever sees the innermost loop's targets, so it can only exit that loop.

## While-Loop Codegen

Three basic blocks: `while_cond`, `while_body`, `while_after`. Only the entry branch and where the first condition check happens differ between `while` and `do`/`while`:

```cpp
Value *WhileExpressionNode::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  BasicBlock *CondBB =
      BasicBlock::Create(*TheContext, "while_cond", TheFunction);
  BasicBlock *BodyBB =
      BasicBlock::Create(*TheContext, "while_body", TheFunction);
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "while_after", TheFunction);

  if (IsDoWhile) {
    Builder->CreateBr(BodyBB);
  } else {
    Builder->CreateBr(CondBB);
  }

  if (!IsDoWhile) {
    Builder->SetInsertPoint(CondBB);
    Value *CondVal = Cond->codegen();
    if (!CondVal)
      return nullptr;
    CondVal = ToBool(CondVal, Cond->getType());
    if (!CondVal)
      return LogErrorV("Invalid loop condition type");
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
    if (!CondVal)
      return nullptr;
    CondVal = ToBool(CondVal, Cond->getType());
    if (!CondVal)
      return LogErrorV("Invalid loop condition type");
    Builder->CreateCondBr(CondVal, BodyBB, AfterBB);
  }

  Builder->SetInsertPoint(AfterBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

For a plain `while`, the entry branch goes straight to `CondBB`, so the condition is checked before the body ever runs. For `do`/`while`, the entry branch goes to `BodyBB` directly, skipping `CondBB` entirely the first time through; `CondBB` still gets filled in afterward for every subsequent iteration, which is why the function only guards the *first* `CreateCondBr` on `!IsDoWhile` and lets the second one run unconditionally through `IsDoWhile || !CondBB->getTerminator()`.

The `ConstantFP::get(*TheContext, APFloat(0.0))` return at the end isn't special to `while`: it's the same "statements always return a dummy `0.0` at the LLVM level" convention every statement-shaped node has used since [Chapter 11](chapter-11.md), even now that the type system tracks `ValueType::None` separately for what the value actually *means* at the pyxc level.

## `for` Loops Get a Dedicated Step Block

The existing `for`-loop codegen changes too. Before this chapter, the step expression ran inline at the end of the body block. Now it gets its own basic block, so `continue` has somewhere correct to jump to:

```cpp
BasicBlock *StepBB = BasicBlock::Create(*TheContext, "loop_step", TheFunction);
```

The body's implicit fallthrough branch now targets `StepBB` instead of the condition block directly. `StepBB` evaluates the step expression and then branches to the condition block itself. The `LoopControlTargets` pushed for a `for` loop sets `ContinueTarget = StepBB`:

```cpp
LoopControlStack.push_back({AfterBB, StepBB});
```

That's what makes `continue` inside a `for` loop run the step before re-checking the condition, matching C semantics, rather than skipping straight to the condition check the way `continue` in a `while` loop does. I confirmed this distinction is real by writing a `for` loop that sums every value except one skipped with `continue`, and checking the skipped iteration's contribution really is missing from the total (see Try It).

## `break` and `continue` Codegen

Both emit a single unconditional branch to whichever target is on top of `LoopControlStack`:

```cpp
Value *BreakExpressionNode::codegen() {
  if (LoopControlStack.empty())
    return LogErrorV("'break' used outside of a loop");
  Builder->CreateBr(LoopControlStack.back().BreakTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *ContinueExpressionNode::codegen() {
  if (LoopControlStack.empty())
    return LogErrorV("'continue' used outside of a loop");
  Builder->CreateBr(LoopControlStack.back().ContinueTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

The `CreateBr` makes the current block terminated; any code that would otherwise follow `break` or `continue` in the same block never actually gets appended to it, since a well-formed basic block can only have one terminator.

## Known Limitations

**The loop condition must be `bool`.** There's no implicit `int → bool` coercion. `while n:` doesn't work; `while n != 0:` does.

**`break` and `continue` outside any loop are parse-time errors**, caught by `ParseLoopDepth` before codegen ever runs.

## Try It

**`break` outside a loop**

```pyxc
def main() -> int:
  break
  return 0
```

```text
Error (Line 2, Column 3): 'break' used outside of a loop
```

**`while` condition must be `bool`**

```pyxc
def main() -> int:
  var n: int = 5
  while n:
    n -= 1
  return 0
```

```text
Error (Line 3, Column 10): While loop condition must be bool
```

**`continue` in a `for` loop still runs the step**

```pyxc
extern def printd(x: float64)
def main() -> int:
  var sum: int = 0
  for var i: int = 0, i < 5, 1:
    if i == 2:
      continue
    sum += i
  printd(float64(sum))
  return 0
```

```text
8.000000
```

`sum` skips `i == 2` (0 + 1 + 3 + 4 = 8), and the loop still terminates normally: `continue` reaching `StepBB` means `i` keeps incrementing instead of looping forever on the same value.

**`do`/`while` always runs the body once**

```pyxc
extern def printd(x: float64)
def main() -> int:
  var n: int = 10
  var count: int = 0
  do:
    count++
  while n < 5
  printd(float64(count))
  return 0
```

```text
1.000000
```

The condition `n < 5` is false from the start, but the body still ran exactly once before it was ever checked.

## Build and Run

```bash
cd code/chapter-34
cmake -S . -B build && cmake --build build
```

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

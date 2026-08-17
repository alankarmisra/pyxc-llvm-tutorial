---
description: "Add switch statements with integer case matching, an optional default, break support, and no implicit fallthrough."
---
# 23. pyxc: Switch

## What I Am Building

[Chapter 22](chapter-22.md) added bitwise operators. Right now, multi-way branching on an integer value means a chain of `if`/`elif`. I'm adding `switch`:

```pyxc
extern def printd(x: float64)

def day_type(d: int) -> int:
  var result: int = 0
  switch d:
    case 0, 6:
      result = 2   # Sunday or Saturday
    default:
      result = 1   # weekday
  return result

def main() -> int:
  printd(float64(day_type(0) + day_type(3) + day_type(6)))
  return 0
```

```
5.000000
```

`switch` runs the matching `case` and stops — no fallthrough. A `case` can list more than one value, so `0` and `6` share a body without two separate `case` lines.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-23
```

## Grammar

I add `switch-statement` and its three sub-productions, and add it as a `compound-statement` alternative:

`code/chapter-23/pyxc.ebnf`

```grammardiff
*...
*do-while-statement                = "do" ":" suite [ end-of-lines ]
*                                    "while" expression ;
+switch-statement                  = "switch" expression ":" end-of-lines
+                                    indent switch-body dedent ;
+switch-body                       = switch-case
+                                    { end-of-lines switch-case }
+                                    [ end-of-lines default-case ] ;
+switch-case                       = "case" switch-integer
+                                    { "," switch-integer } ":" suite ;
+default-case                      = "default" ":" suite ;
*for-statement                     = "for" ( "var" name ":" type | name )
*                                    "=" expression ","
*...
*                                    | assignment-statement
*                                    | expression ;
-compound-statement                = if-statement
-                                    | for-statement
-                                    | while-statement
-                                    | do-while-statement ;
+compound-statement                = if-statement
+                                    | for-statement
+                                    | while-statement
+                                    | do-while-statement
+                                    | switch-statement ;
*statement                         = simple-statement | compound-statement ;
*suite                             = simple-statement
*...
*                                    | "float" | "float32"
*                                    | "float64" | "bool" ;
+switch-integer                    = [ "-" ] digit { digit } ;
*number                            = ( digit { digit } [ "." { digit } ]
*                                    | "." digit { digit } ) [ exponent ] ;
*...
```

## New Tokens and Keywords

I add three new tokens:

```cppdiff
*enum Token {
*  ...
*  tok_shift_left = -45,  // <<
*  tok_shift_right = -46, // >>
+  tok_switch = -47,
+  tok_case = -48,
+  tok_default = -49,
*
*  // punctuation and operators
*  ...
*};
```

And add them to the keyword table:

```cppdiff
*static map<string, Token> Keywords = {
*    ...
*    {"uint32", tok_uint32},   {"uint64", tok_uint64},
+    {"switch", tok_switch},   {"case", tok_case},
+    {"default", tok_default},
*    {"float", tok_float},
*    ...
*};
```

## Representing `switch` in the AST

The node stores the condition, a list of (values, body) pairs, and an optional default body. A case can list more than one value — `case 'a', 'e', 'i', 'o', 'u':` — so I store a vector of values per case, not just one:

```cpp
class SwitchStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Condition;
  vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases;
  unique_ptr<ExpressionNode> DefaultCase;

public:
  SwitchStatementNode(
      unique_ptr<ExpressionNode> Condition,
      vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases,
      unique_ptr<ExpressionNode> DefaultCase)
      : Condition(std::move(Condition)), Cases(std::move(Cases)),
        DefaultCase(std::move(DefaultCase)) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};
```

I store case values as `int64_t` — signed integer literals I parse at compile time.

## Parse-Time Switch Depth

Same pattern as loop depth for `break`/`continue`: a counter and an RAII guard track whether I'm inside a switch:

```cpp
static int ParseSwitchDepth = 0;

struct ParseSwitchGuard {
  ParseSwitchGuard()  { ++ParseSwitchDepth; }
  ~ParseSwitchGuard() { --ParseSwitchDepth; }
};
```

I update `ParseBreakStatement` to accept `break` inside a switch as well as a loop:

```cpp
static unique_ptr<ExpressionNode> ParseBreakStatement() {
  if (ParseLoopDepth <= 0 && ParseSwitchDepth <= 0)
    return LogErrorExpression("'break' used outside of a loop or switch");
  getNextToken();
  return make_unique<BreakStatementNode>();
}
```

## Parsing Case Literals

Case values are signed integer literals. I handle an optional leading `-` explicitly, before reading the number:

```cpp
static bool ParseSwitchCaseValue(int64_t &Value) {
  bool Negative = false;
  if (CurrentToken == tok_minus) {
    Negative = true;
    getNextToken();
  }
  if (CurrentToken != tok_number || NumberIsFloat) {
    LogErrorExpression("Switch case value must be an integer literal");
    return false;
  }

  uint64_t Magnitude = 0;
  for (char Digit : NumberLiteral) {
    unsigned ValueOfDigit = static_cast<unsigned>(Digit - '0');
    if (Magnitude >
        (std::numeric_limits<uint64_t>::max() - ValueOfDigit) / 10) {
      LogErrorExpression("Switch case value out of range");
      return false;
    }
    Magnitude = Magnitude * 10 + ValueOfDigit;
  }
  getNextToken(); // eat integer

  uint64_t NegativeLimit =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;
  if (Negative) {
    if (Magnitude > NegativeLimit) {
      LogErrorExpression("Switch case value out of range");
      return false;
    }
    Value = Magnitude == NegativeLimit
                ? std::numeric_limits<int64_t>::min()
                : -static_cast<int64_t>(Magnitude);
  } else {
    if (Magnitude >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      LogErrorExpression("Switch case value out of range");
      return false;
    }
    Value = static_cast<int64_t>(Magnitude);
  }
  return true;
}
```

I accumulate the magnitude digit by digit rather than parsing the whole literal and checking after the fact, so overflow is caught mid-parse instead of after a `uint64_t` has already wrapped around silently. `case -9223372036854775808:` — the minimum `int64_t` — still parses correctly: its magnitude is exactly `NegativeLimit` (`INT64_MAX + 1`), one past what a positive `int64_t` can hold, which is exactly the boundary `Magnitude == NegativeLimit` exists to allow. This also means negative case values just work: `case -1:` is valid, no separate rule needed.

## Parsing the `switch` Statement

I eat `switch`, check the condition is an integer type, then read an indented block of `case` and `default` clauses. A `case` reads one value, then keeps reading more as long as a `,` follows — that's how `case 'a', 'e', 'i', 'o', 'u':` ends up sharing one body across five values:

```cpp
static unique_ptr<ExpressionNode> ParseSwitchStatement() {
  getNextToken(); // eat 'switch'
  auto Condition = ParseExpression();
  if (!Condition)
    return nullptr;
  if (!IsIntType(Condition->getType()))
    return LogErrorExpression("Switch condition must be an integer type");
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after switch expression");
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented switch body");
  getNextToken(); // eat INDENT

  ParseSwitchGuard Switch;
  vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases;
  set<int64_t> SeenValues;
  unique_ptr<ExpressionNode> DefaultCase;

  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_case) {
      if (DefaultCase)
        return LogErrorExpression("Case cannot follow default");
      getNextToken(); // eat 'case'
      vector<int64_t> Values;
      while (true) {
        int64_t Value = 0;
        if (!ParseSwitchCaseValue(Value))
          return nullptr;
        if (!SeenValues.insert(Value).second)
          return LogErrorExpression("Duplicate switch case value");
        Values.push_back(Value);
        if (CurrentToken != tok_comma)
          break;
        getNextToken(); // eat ','
      }
      if (CurrentToken != tok_colon)
        return LogErrorExpression("Expected ':' after case value");
      getNextToken(); // eat ':'
      auto Body = ParseSuite();
      if (!Body)
        return nullptr;
      Cases.emplace_back(std::move(Values), std::move(Body));
    } else if (CurrentToken == tok_default) {
      if (DefaultCase)
        return LogErrorExpression("Duplicate default case");
      getNextToken(); // eat 'default'
      if (CurrentToken != tok_colon)
        return LogErrorExpression("Expected ':' after default");
      getNextToken(); // eat ':'
      DefaultCase = ParseSuite();
      if (!DefaultCase)
        return nullptr;
    } else {
      return LogErrorExpression("Expected 'case' or 'default' in switch body");
    }

    if (CurrentToken == tok_block_end)
      getNextToken();
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }

  if (CurrentToken != tok_dedent)
    return LogErrorExpression("Expected dedent after switch body");
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface block-end
  return make_unique<SwitchStatementNode>(
      std::move(Condition), std::move(Cases), std::move(DefaultCase));
}
```

I reject duplicate case values at parse time with a `set<int64_t>`, checked as each value is read — so a repeat within one comma-separated list (`case 1, 2, 1:`) is caught the same way as a repeat across two separate `case` lines. I reject multiple `default` clauses the same way, and reject a `case` that comes after `default` at all — `default` has to be the last clause in the switch body. If nothing matches and there's no `default`, `DefaultCase` just stays null — execution falls through to after the switch with no action.

**Non-integer switch condition:**
```pyxc
switch x:
```
```
Error (Line 3, Column 11): Switch condition must be an integer type
  switch x:
          ^~~~
```

**Duplicate case value:**
```pyxc
case 1:
  return 1
case 1:
  return 2
```
```
Error (Line 5, Column 11): Duplicate switch case value
    case 1:
          ^~~~
```

## Refactoring Break Targets

Chapter 14's `LoopControlStack` carries `BreakTarget` and `ContinueTarget` together. A switch needs to push a break target without disturbing `continue`, which still has to reach the enclosing loop — so I add a separate stack just for break:

```cpp
static vector<BasicBlock *> BreakTargetStack;
```

I update the `for` and `while` codegens to push and pop `BreakTargetStack` alongside `LoopControlStack`:

```cppdiff
*Value *ForStatementNode::codegen() {
*  ...
*  TheBuilder->SetInsertPoint(BodyBB);
*
*  LoopControlStack.push_back({AfterBB, StepBB});
+  BreakTargetStack.push_back(AfterBB);
*  if (!Body->codegen()) {
+    BreakTargetStack.pop_back();
*    LoopControlStack.pop_back();
*    return nullptr;
*  }
+  BreakTargetStack.pop_back();
*  LoopControlStack.pop_back();
*  ...
*}

*Value *WhileStatementNode::codegen() {
*  ...
*  TheBuilder->SetInsertPoint(BodyBlock);
*  LoopControlStack.push_back({AfterBlock, ConditionBlock});
+  BreakTargetStack.push_back(AfterBlock);
*  if (!Body->codegen()) {
+    BreakTargetStack.pop_back();
*    LoopControlStack.pop_back();
*    return nullptr;
*  }
+  BreakTargetStack.pop_back();
*  LoopControlStack.pop_back();
*  ...
*}
```

And I switch `BreakStatementNode::codegen` from `LoopControlStack.back().BreakTarget` to `BreakTargetStack`:

```cpp
Value *BreakStatementNode::codegen() {
  if (BreakTargetStack.empty())
    return LogErrorValue("'break' used outside of a loop or switch");
  TheBuilder->CreateBr(BreakTargetStack.back());
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

`continue` doesn't need any of this — it still reads `LoopControlStack.back().ContinueTarget` directly, and a switch never touches that stack. `continue` inside a switch keeps meaning "continue the enclosing loop," while `break` inside a switch now means "exit the switch," not the loop.

## `switch` Codegen

I use LLVM's own `switch` instruction — a real multi-way branch, not a chain of comparisons. The backend picks a jump table, binary search, or comparison chain depending on how many cases there are and how dense the values are; I don't have to choose. Each case gets one basic block, and since LLVM's `switch` already supports many values pointing at the same block, giving a case several values is just one `addCase` call per value, all targeting that case's block:

```cpp
Value *SwitchStatementNode::codegen() {
  Value *ConditionValue = Condition->codegen();
  if (!ConditionValue)
    return nullptr;

  auto *ConditionType = dyn_cast<IntegerType>(LLVMTypeFor(Condition->getType()));
  if (!ConditionType)
    return LogErrorValue("Switch condition must be an integer type");

  Function *FunctionIR = TheBuilder->GetInsertBlock()->getParent();
  BasicBlock *AfterBlock =
      BasicBlock::Create(*TheContext, "switch.after", FunctionIR);
  BasicBlock *DefaultBlock =
      DefaultCase
          ? BasicBlock::Create(*TheContext, "switch.default", FunctionIR)
          : AfterBlock;

  unsigned CaseCount = 0;
  for (const auto &Case : Cases)
    CaseCount += Case.first.size();
  auto *SwitchIR =
      TheBuilder->CreateSwitch(ConditionValue, DefaultBlock, CaseCount);

  vector<BasicBlock *> CaseBlocks;
  for (const auto &Case : Cases) {
    BasicBlock *CaseBlock =
        BasicBlock::Create(*TheContext, "switch.case", FunctionIR);
    CaseBlocks.push_back(CaseBlock);
    for (int64_t Value : Case.first) {
      auto *Constant = ConstantInt::get(ConditionType,
                                        static_cast<uint64_t>(Value), true);
      SwitchIR->addCase(Constant, CaseBlock);
    }
  }

  BreakTargetStack.push_back(AfterBlock);
  for (size_t Index = 0; Index < Cases.size(); ++Index) {
    TheBuilder->SetInsertPoint(CaseBlocks[Index]);
    if (!Cases[Index].second->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!TheBuilder->GetInsertBlock()->getTerminator())
      TheBuilder->CreateBr(AfterBlock);
  }

  if (DefaultCase) {
    TheBuilder->SetInsertPoint(DefaultBlock);
    if (!DefaultCase->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!TheBuilder->GetInsertBlock()->getTerminator())
      TheBuilder->CreateBr(AfterBlock);
  }
  BreakTargetStack.pop_back();

  TheBuilder->SetInsertPoint(AfterBlock);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

`TheBuilder->CreateSwitch(ConditionValue, DefaultBlock, CaseCount)` emits the `switch` instruction itself, with the default destination and a hint for how many cases to expect — `CaseCount` counts individual values, not `case` clauses, so `case 0, 6:` contributes two to the hint even though it's one clause with one body. When there's no `default` in the source, `DefaultBlock` is just `AfterBlock` — no matching value falls straight through to after the switch, same as a real `default` that does nothing. `SwitchIR->addCase(Constant, CaseBlock)` registers each value.

If a case body doesn't end in a terminator, I add an unconditional branch to `switch.after` myself. That's the whole no-fallthrough guarantee — every case exits to `switch.after` unless it already returned or broke somewhere else. There's no way to stack empty `case`s to share a body the way C does; if two values need the same code, list them on one `case` line instead.

I only allow compile-time integer literals as case values, not variables or expressions — that's what lets LLVM build a real jump table or binary search instead of a comparison chain.

## Known Limitations

**Case values must be compile-time integer literals.** `case x:` or `case a + 1:` aren't accepted; only literal integers (optionally negative) are.

**No fallthrough, and no way to opt into it.** Every case implicitly branches to `switch.after` unless it already returns or breaks. There's no C-style `case 1: case 2:` stacking to share a body; comma-separated values on one `case` line are the only way to match several values with one body.

**Only one `default`.** A second `default:` clause is a parse-time error, same as a duplicate `case` value.

## Build and Run

```bash
cd code/chapter-23
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## Try It

```pyxc
ready> def day_type(d: int) -> int:
   switch d:
     case 0, 6:
       return 2
     default:
       return 1

ready> day_type(0)
2
ready> day_type(3)
1
ready> day_type(6)
2
ready>
```

## What's Next

[Chapter 24](chapter-24.md) adds `struct` declarations.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

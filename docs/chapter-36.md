---
description: "Add switch statements with integer case matching, an optional default, break support, and no implicit fallthrough."
---
# 36. pyxc: Switch

## Where We Are

[Chapter 35](chapter-35.md) added bitwise operators. Multi-way branching on an integer value is currently done with chains of `if`/`else if`. After this chapter, `switch` is available:

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

`switch` dispatches to the matching `case` and stops there. There is no fallthrough — but a single `case` can list more than one value, so `0` and `6` sharing a body doesn't need two separate `case` lines.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-36
```

## New Tokens and Keywords

Three new token values:

```cpp
tok_switch  = -60,
tok_case    = -61,
tok_default = -62,
```

Added to the keyword table:

```cpp
{"switch", tok_switch}, {"case", tok_case}, {"default", tok_default},
```

## `SwitchExprAST`

The AST node stores the condition, a vector of (value list, body) pairs, and an optional default body. Each case can list more than one value — `case 'a', 'e', 'i', 'o', 'u':` — so all of them share the same body:

```cpp
class SwitchExprAST : public ExprAST {
  unique_ptr<ExprAST> Cond;
  vector<pair<vector<int64_t>, unique_ptr<ExprAST>>> Cases;
  unique_ptr<ExprAST> DefaultCase;
public:
  SwitchExprAST(unique_ptr<ExprAST> Cond,
                vector<pair<vector<int64_t>, unique_ptr<ExprAST>>> Cases,
                unique_ptr<ExprAST> DefaultCase)
      : Cond(std::move(Cond)), Cases(std::move(Cases)),
        DefaultCase(std::move(DefaultCase)) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};
```

Case values are `int64_t` — signed integer literals parsed at compile time.

## Parse-Time Switch Depth

Like loop depth for `break`/`continue`, a counter and RAII guard track whether the parser is inside a switch:

```cpp
static int ParseSwitchDepth = 0;

struct ParseSwitchGuard {
  ParseSwitchGuard()  { ++ParseSwitchDepth; }
  ~ParseSwitchGuard() { --ParseSwitchDepth; }
};
```

`ParseBreakStmt` is updated to accept `break` inside a switch as well as a loop:

```cpp
static unique_ptr<ExprAST> ParseBreakStmt() {
  if (ParseLoopDepth <= 0 && ParseSwitchDepth <= 0)
    return LogErrorExpression("'break' used outside of a loop or switch");
  getNextToken(); // eat 'break'
  return make_unique<BreakExprAST>();
}
```

## `ParseSwitchCaseValue` — Parsing Case Literals

Case values are signed integer literals. An optional leading `-` is handled explicitly before the number:

```cpp
static bool ParseSwitchCaseValue(int64_t &Out) {
  bool Neg = false;
  if (CurTok == '-') {
    Neg = true;
    getNextToken();
  }
  if (CurTok != tok_number || NumIsFloat)
    return LogErrorExpression("Switch case value must be an integer literal"), false;
  uint64_t Raw = 0;
  if (!ParseUnsignedDecimal(NumLiteralStr, Raw))
    return LogErrorExpression("Invalid switch case value"), false;
  getNextToken(); // eat number
  if (Neg) {
    if (Raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ULL)
      return LogErrorExpression("Switch case value out of range"), false;
    Out = static_cast<int64_t>(0) - static_cast<int64_t>(Raw);
  } else {
    if (Raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return LogErrorExpression("Switch case value out of range"), false;
    Out = static_cast<int64_t>(Raw);
  }
  return true;
}
```

Overflow is checked explicitly for both positive and negative cases. `case -9223372036854775808:` (the minimum `int64_t`) is handled correctly by the `Raw > max + 1` path.

## `ParseSwitchStmt`

The parser eats `switch`, verifies the condition is an integer type, then reads an indented block of `case` and `default` clauses. A `case` reads one value, then keeps reading more as long as a `,` follows — that's what lets `case 'a', 'e', 'i', 'o', 'u':` share one body across five values:

```cpp
static unique_ptr<ExprAST> ParseSwitchStmt() {
  getNextToken(); // eat 'switch'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (!IsIntType(Cond->getType()))
    return LogErrorExpression("Switch condition must be an integer type");
  if (CurTok != ':')
    return LogErrorExpression("Expected ':' after switch expression");
  getNextToken(); // eat ':'
  if (CurTok == tok_eol)
    consumeNewlines();
  if (CurTok != tok_indent)
    return LogErrorExpression("Expected an indented switch body");
  getNextToken(); // eat INDENT

  ParseSwitchGuard SwitchGuard;
  vector<pair<vector<int64_t>, unique_ptr<ExprAST>>> Cases;
  std::set<int64_t> SeenCaseValues;
  unique_ptr<ExprAST> DefaultCase;

  while (CurTok != tok_dedent && CurTok != tok_eof) {
    if (CurTok == tok_case) {
      getNextToken(); // eat 'case'
      vector<int64_t> CaseVals;
      while (true) {
        int64_t CaseVal = 0;
        if (!ParseSwitchCaseValue(CaseVal))
          return nullptr;
        if (!SeenCaseValues.insert(CaseVal).second)
          return LogErrorExpression("Duplicate switch case value");
        CaseVals.push_back(CaseVal);
        if (CurTok != ',')
          break;
        getNextToken(); // eat ',' and parse the next case value
      }
      if (CurTok != ':')
        return LogErrorExpression("Expected ':' after case value");
      getNextToken(); // eat ':'
      auto Body = ParseSuite();
      if (!Body)
        return nullptr;
      Cases.emplace_back(std::move(CaseVals), std::move(Body));
    } else if (CurTok == tok_default) {
      if (DefaultCase)
        return LogErrorExpression("Duplicate default case");
      getNextToken(); // eat 'default'
      if (CurTok != ':')
        return LogErrorExpression("Expected ':' after default");
      getNextToken(); // eat ':'
      DefaultCase = ParseSuite();
      if (!DefaultCase)
        return nullptr;
    } else {
      return LogErrorExpression("Expected 'case' or 'default' in switch body");
    }
    if (CurTok == tok_block_end)
      getNextToken();
    if (CurTok == tok_eol)
      consumeNewlines();
  }
  if (CurTok != tok_dedent)
    return LogErrorExpression("Expected dedent after switch body");
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT
  return make_unique<SwitchExprAST>(std::move(Cond), std::move(Cases),
                                    std::move(DefaultCase));
}
```

Duplicate case values are rejected at parse time using a `std::set<int64_t>` — checked as each value is read, so a repeat within one comma-separated list (`case 1, 2, 1:`) is caught exactly the same way as a repeat across two separate `case` lines. Multiple `default` clauses are also rejected at parse time. The `ParseSwitchGuard` is installed around the body loop so `break` inside any case is legal.

## `BreakTargetStack` — Refactoring Break Targets

Chapter 34's `LoopControlStack` carried both `BreakTarget` and `ContinueTarget` together. Switches need to push a break target without disturbing `continue` targets (which still refer to the enclosing loop). So this chapter introduces a separate stack for break:

```cpp
static std::vector<BasicBlock *> BreakTargetStack;
```

Both the `for` and `while` loop codegens are updated to push and pop `BreakTargetStack` in addition to `LoopControlStack`:

```cpp
// for loop:
BreakTargetStack.push_back(AfterBB);
if (!Body->codegen()) {
  BreakTargetStack.pop_back();
  return nullptr;
}
BreakTargetStack.pop_back();

// while loop:
BreakTargetStack.push_back(AfterBB);
// ...
BreakTargetStack.pop_back();
```

`BreakExprAST::codegen` is updated to use `BreakTargetStack` instead of `LoopControlStack.back().BreakTarget`:

```cpp
Value *BreakExprAST::codegen() {
  if (BreakTargetStack.empty())
    return LogErrorV("'break' used outside of a loop or switch");
  Builder->CreateBr(BreakTargetStack.back());
}
```

`continue` is unaffected — it still reads from `LoopControlStack.back().ContinueTarget`, which the switch never touches.

## `SwitchExprAST::codegen`

Codegen uses LLVM's `switch` instruction, which maps directly to a machine-level multi-way branch. Each case gets its own basic block — and since LLVM's `switch` instruction natively supports many values pointing at the same destination block, giving a case several values is just a loop over `C.first` calling `addCase` once per value, all targeting the one `CaseBB` created for that case:

```cpp
Value *SwitchExprAST::codegen() {
  Value *CondVal = Cond->codegen();
  // ...type checks...
  Function *F = Builder->GetInsertBlock()->getParent();
  BasicBlock *AfterBB  = BasicBlock::Create(*TheContext, "switch.after", F);
  BasicBlock *DefaultBB =
      DefaultCase ? BasicBlock::Create(*TheContext, "switch.default", F)
                  : AfterBB;
  auto *SwitchI = Builder->CreateSwitch(CondVal, DefaultBB, Cases.size());

  vector<BasicBlock *> CaseBBs;
  CaseBBs.reserve(Cases.size());
  for (const auto &C : Cases) {
    BasicBlock *CaseBB = BasicBlock::Create(*TheContext, "switch.case", F);
    CaseBBs.push_back(CaseBB);
    for (int64_t Val : C.first) {
      auto *CaseConst = ConstantInt::get(cast<IntegerType>(CondLLVMType),
                                         static_cast<uint64_t>(Val),
                                         /*isSigned=*/true);
      SwitchI->addCase(CaseConst, CaseBB);
    }
  }

  BreakTargetStack.push_back(AfterBB);
  for (size_t I = 0; I < Cases.size(); ++I) {
    Builder->SetInsertPoint(CaseBBs[I]);
    if (!Cases[I].second->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!Builder->GetInsertBlock()->getTerminator())
      Builder->CreateBr(AfterBB);    // implicit no-fallthrough
  }

  if (DefaultCase) {
    Builder->SetInsertPoint(DefaultBB);
    if (!DefaultCase->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!Builder->GetInsertBlock()->getTerminator())
      Builder->CreateBr(AfterBB);
  }
  BreakTargetStack.pop_back();

  Builder->SetInsertPoint(AfterBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

`Builder->CreateSwitch(CondVal, DefaultBB, Cases.size())` emits the IR `switch` instruction with the default destination and a hint for how many cases to expect. `SwitchI->addCase(CaseConst, CaseBB)` registers each case. If no case body ends with a terminator, the implicit `br switch.after` provides the no-fallthrough semantics.

## Grammar

```ebnf
switchstmt   = "switch" expression ":" eols indent switchbody dedent ;  -- new
switchbody   = switchcase { eols switchcase } [ eols defaultcase ] ;     -- new
switchcase   = "case" switchint { "," switchint } ":" suite ;            -- new
defaultcase  = "default" ":" suite ;                                     -- new
switchint    = [ "-" ] integer ;                                          -- new
compoundstmt = ifstmt | forstmt | whilestmt | dowhilestmt | switchstmt ; -- changed
```

## Error Cases

**Non-integer switch condition:**
```pyxc
var x: float64 = 1.0
switch x:           # Error: Switch condition must be an integer type
  case 1:
    return 1
```

**Duplicate case value:**
```pyxc
switch x:
  case 1:
    return 1
  case 1:           # Error: Duplicate switch case value
    return 2
```

Both are caught at parse time.

## Things Worth Knowing

**Case values are compile-time integer literals only.** You cannot use a variable or expression as a case value. This restriction allows LLVM to emit an efficient branch table or binary search.

**A case can list more than one value.** `case 0, 6:` matches either `0` or `6` and runs the one shared body — no need to repeat the body under two separate `case` lines, and no C-style fallthrough involved in getting there.

**Negative case values are supported.** `case -1:` is valid. `switchint` accepts a leading `-`.

**LLVM emits a real `switch` instruction.** The IR contains `switch i64`, not a chain of comparisons. The backend lowers it to a jump table, binary search, or comparison chain depending on case density and count.

**`default` is optional.** If no case matches and there is no `default`, execution continues after the switch with no action.

**`continue` inside a switch refers to the enclosing loop.** The `LoopControlStack` is not touched by the switch. `break` inside a switch exits only the switch, not any enclosing loop.

**No fallthrough — by design.** Each case implicitly branches to `switch.after`. The C idiom of stacking empty cases to share a body does not work; extract the shared logic into a function instead.

## What's Next

Phase 5 — K&R compatibility — is now complete. pyxc has `while`, `do/while`, `break`, `continue`, `switch`, the full set of arithmetic and bitwise operators, `++`/`--`, compound assignment, logical operators, and the C memory model from Chapters 17–22. The next phase brings modules, imports, and the standard library.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

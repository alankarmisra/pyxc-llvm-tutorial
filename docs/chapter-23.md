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
cd pyxc-llvm-tutorial/code/chapter-36
```

## Grammar

I add `switch-statement` and its three sub-productions, and add it as a `compound-statement` alternative:

`code/chapter-36/pyxc.ebnf`

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
 while-statement       = "while" expression ":" suite ;
 do-while-statement     = "do" ":" suite end-of-lines "while" expression ;
+switch-statement      = "switch" expression ":" end-of-lines indent switch-body dedent ;
+switch-body      = switch-case { end-of-lines switch-case } [ end-of-lines default-case ] ;
+switch-case      = "case" switch-integer { "," switch-integer } ":" suite ;
+default-case     = "default" ":" suite ;
 for-statement         = "for"
                   ( "var" name ":" type | name )
                   "=" expression "," expression "," expression ":" suite ;
 variable-statement         = "var" variable-binding { "," variable-binding } ;
 assignment-statement      = lvalue assignment-operator expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | break-statement | continue-statement | variable-statement | assignment-statement | expression ;
-compound-statement    = if-statement | for-statement | while-statement | do-while-statement ;
+compound-statement    = if-statement | for-statement | while-statement | do-while-statement | switch-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
 break-statement       = "break" ;
 continue-statement    = "continue" ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
 expression      = logical-or ;
 logical-or      = logical-and { "||" logical-and } ;
 logical-and     = bitwise-or { "&&" bitwise-or } ;
 bitwise-or      = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor     = bitwise-and { "^" bitwise-and } ;
 bitwise-and     = equality { "&" equality } ;
 equality        = relational { ("==" | "!=") relational } ;
 relational      = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift           = sum { ("<<" | ">>") sum } ;
 sum             = term { ("+" | "-") term } ;
 term            = unary-expression { ("*" | "/" | "%") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = ("-" | "!" | "~" | "++" | "--") unary-expression | postfix-expression ;
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
+switch-integer       = [ "-" ] integer ;
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

## New Tokens and Keywords

I add three new tokens:

```cpp
tok_switch  = -60,
tok_case    = -61,
tok_default = -62,
```

And add them to the keyword table:

```cpp
{"switch", tok_switch}, {"case", tok_case}, {"default", tok_default},
```

## Representing `switch` in the AST

The node stores the condition, a list of (values, body) pairs, and an optional default body. A case can list more than one value — `case 'a', 'e', 'i', 'o', 'u':` — so I store a vector of values per case, not just one:

```cpp
class SwitchExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Cond;
  vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases;
  unique_ptr<ExpressionNode> DefaultCase;
public:
  SwitchExpressionNode(unique_ptr<ExpressionNode> Cond,
                vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases,
                unique_ptr<ExpressionNode> DefaultCase)
      : Cond(std::move(Cond)), Cases(std::move(Cases)),
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
  getNextToken(); // eat 'break'
  return make_unique<BreakExpressionNode>();
}
```

## Parsing Case Literals

Case values are signed integer literals. I handle an optional leading `-` explicitly, before reading the number:

```cpp
static bool ParseSwitchCaseValue(int64_t &Out) {
  bool Neg = false;
  if (CurrentToken == tok_minus) {
    Neg = true;
    getNextToken();
  }
  if (CurrentToken != tok_number || NumberIsFloat)
    return LogErrorExpression("Switch case value must be an integer literal"), false;
  uint64_t Raw = 0;
  if (!ParseUnsignedDecimal(NumberLiteral, Raw))
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

I check overflow explicitly on both branches, so `case -9223372036854775808:` — the minimum `int64_t` — still parses correctly: it overflows a bare positive `int64_t` by one, which is exactly what the `Raw > max + 1` branch is there to allow. This also means negative case values just work: `case -1:` is valid, no separate rule needed.

## Parsing the `switch` Statement

I eat `switch`, check the condition is an integer type, then read an indented block of `case` and `default` clauses. A `case` reads one value, then keeps reading more as long as a `,` follows — that's how `case 'a', 'e', 'i', 'o', 'u':` ends up sharing one body across five values:

```cpp
static unique_ptr<ExpressionNode> ParseSwitchStatement() {
  getNextToken(); // eat 'switch'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (!IsIntType(Cond->getType()))
    return LogErrorExpression("Switch condition must be an integer type");
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after switch expression");
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented switch body");
  getNextToken(); // eat INDENT

  ParseSwitchGuard SwitchGuard;
  vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases;
  std::set<int64_t> SeenCaseValues;
  unique_ptr<ExpressionNode> DefaultCase;

  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_case) {
      getNextToken(); // eat 'case'
      vector<int64_t> CaseVals;
      while (true) {
        int64_t CaseVal = 0;
        if (!ParseSwitchCaseValue(CaseVal))
          return nullptr;
        if (!SeenCaseValues.insert(CaseVal).second)
          return LogErrorExpression("Duplicate switch case value");
        CaseVals.push_back(CaseVal);
        if (CurrentToken != tok_comma)
          break;
        getNextToken(); // eat ',' and parse the next case value
      }
      if (CurrentToken != tok_colon)
        return LogErrorExpression("Expected ':' after case value");
      getNextToken(); // eat ':'
      auto Body = ParseSuite();
      if (!Body)
        return nullptr;
      Cases.emplace_back(std::move(CaseVals), std::move(Body));
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
  getNextToken(); // eat DEDENT, then surface tok_block_end
  return make_unique<SwitchExpressionNode>(std::move(Cond), std::move(Cases),
                                    std::move(DefaultCase));
}
```

I reject duplicate case values at parse time with a `std::set<int64_t>`, checked as each value is read — so a repeat within one comma-separated list (`case 1, 2, 1:`) is caught the same way as a repeat across two separate `case` lines. I reject multiple `default` clauses the same way. If nothing matches and there's no `default`, `DefaultCase` just stays null — execution falls through to after the switch with no action.

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
static std::vector<BasicBlock *> BreakTargetStack;
```

I update the `for` and `while` codegens to push and pop `BreakTargetStack` alongside `LoopControlStack`:

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

And I switch `BreakExpressionNode::codegen` from `LoopControlStack.back().BreakTarget` to `BreakTargetStack`:

```cpp
Value *BreakExpressionNode::codegen() {
  if (BreakTargetStack.empty())
    return LogErrorV("'break' used outside of a loop or switch");
  Builder->CreateBr(BreakTargetStack.back());
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

`continue` doesn't need any of this — it still reads `LoopControlStack.back().ContinueTarget` directly, and a switch never touches that stack. `continue` inside a switch keeps meaning "continue the enclosing loop," while `break` inside a switch now means "exit the switch," not the loop.

## `switch` Codegen

I use LLVM's own `switch` instruction — a real multi-way branch, not a chain of comparisons. The backend picks a jump table, binary search, or comparison chain depending on how many cases there are and how dense the values are; I don't have to choose. Each case gets one basic block, and since LLVM's `switch` already supports many values pointing at the same block, giving a case several values is just one `addCase` call per value, all targeting that case's block:

```cpp
Value *SwitchExpressionNode::codegen() {
  Value *CondVal = Cond->codegen();
  if (!CondVal)
    return nullptr;

  ValueType CondType = Cond->getType();
  llvm::Type *CondLLVMType = LLVMTypeFor(CondType);
  if (!CondLLVMType || !CondLLVMType->isIntegerTy())
    return LogErrorV("Switch condition must be an integer type");
  CondVal = EmitImplicitCast(CondVal, CondType, CondType);
  if (!CondVal)
    return LogErrorV("Invalid switch condition type");

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

`Builder->CreateSwitch(CondVal, DefaultBB, Cases.size())` emits the `switch` instruction itself, with the default destination and a hint for how many cases to expect. When there's no `default` in the source, `DefaultBB` is just `AfterBB` — no matching case falls straight through to after the switch, same as a real `default` that does nothing. `SwitchI->addCase(CaseConst, CaseBB)` registers each value.

If a case body doesn't end in a terminator, I add an unconditional branch to `switch.after` myself. That's the whole no-fallthrough guarantee — every case exits to `switch.after` unless it already returned or broke somewhere else. There's no way to stack empty `case`s to share a body the way C does; if two values need the same code, list them on one `case` line instead.

I only allow compile-time integer literals as case values, not variables or expressions — that's what lets LLVM build a real jump table or binary search instead of a comparison chain.

## Known Limitations

**Case values must be compile-time integer literals.** `case x:` or `case a + 1:` aren't accepted; only literal integers (optionally negative) are.

**No fallthrough, and no way to opt into it.** Every case implicitly branches to `switch.after` unless it already returns or breaks. There's no C-style `case 1: case 2:` stacking to share a body; comma-separated values on one `case` line are the only way to match several values with one body.

**Only one `default`.** A second `default:` clause is a parse-time error, same as a duplicate `case` value.

## Build and Run

```bash
cd code/chapter-36
cmake -S . -B build && cmake --build build
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

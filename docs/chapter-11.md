---
section: "Statements and Control Flow"
description: "Add mutable variables and assignment with entry-block allocas, loads, stores, and mem2reg."
---

# 11. pyxc: Mutable Variables

Next: let a name change its value.

Right now `NamedValues` maps each name directly to an LLVM value. That works while every name is immutable, but assignment needs a stable place to write:

```pyxc
x = x + 1
```

The clean first model is:

```text
name -> stack slot
read -> load from slot
write -> store to slot
```

Do not build PHI nodes for mutable variables by hand. Emit simple memory operations first, then let LLVM's `mem2reg` optimization recover SSA form.

By the end of this chapter, this should work:

```pyxc
ready> var x = 1: x = x + 1
```

Expected:

```text
Parsed a top-level expression.
Evaluated to 2.000000
```

Work in:

```bash
cd code/chapter-11
```

## 1. Extend the Grammar

Start with the language boundary.

Replace:

```ebnf
expression = comparison ;
```

with:

```ebnf
expression = variable-expression
           | comparison [ "=" expression ] ;

variable-expression = "var" variable-binding
                      { "," variable-binding } ":"
                      [ end-of-lines ] expression ;

variable-binding = name [ "=" expression ] ;
```

Then change the loop rule from:

```ebnf
for-expression = "for" name "=" expression ","
                 expression "," expression ":"
                 [ end-of-lines ] expression ;
```

to:

```ebnf
for-expression = "for" [ "var" ] name "=" expression ","
                 expression "," expression ":"
                 [ end-of-lines ] expression ;
```

The last loop expression is now the complete update:

```pyxc
for var i = 1, i <= 3, i = i + 1: printd(i)
```

Chapter 10 treated it as a numeric step and added it automatically. Chapter 11 evaluates `i = i + 1` like any other expression.

Assignment belongs at the loosest precedence level. This makes it right-associative:

```pyxc
a = b = 4
```

parses as:

```text
a = (b = 4)
```

## 2. Add the `var` Token

Add one token after `tok_for`:

```cpp
// mutable variables
tok_var = -16,
```

Then add it to `Keywords`:

```cpp
{"var", tok_var}
```

Also add it to the debug token-name table:

```cpp
{tok_var, "'var'"}
```

No lexer branch is needed. `var` follows the same identifier-or-keyword path as `def`, `if`, and `for`.

Build now:

```bash
cmake -S . -B build
cmake --build build
```

The lexer now recognizes `var`; the parser does not yet know what to construct from it.

## 3. Give Expressions an Lvalue Boundary

Assignment needs to distinguish a location from a computed value:

```text
lvalue = rvalue
```

For now, only a plain variable name is an lvalue. Add this virtual query to `ExpressionNode`:

```cpp
class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;

  virtual const string *getLValueName() const { return nullptr; }
  virtual Value *codegen() = 0;
};
```

The default says that numbers, calls, binary expressions, and control-flow expressions are not assignable.

Override it in `NameExpressionNode`:

```cpp
class NameExpressionNode : public ExpressionNode {
  string Name;

public:
  NameExpressionNode(const string &Name) : Name(Name) {}

  const string *getLValueName() const override { return &Name; }
  Value *codegen() override;
};
```

This keeps the parser independent of concrete AST casts. It asks the parsed left side whether it denotes an assignable name.

## 4. Add the Assignment AST Node

Add a node containing the destination name and right-hand expression:

```cpp
class AssignmentExpressionNode : public ExpressionNode {
  string VariableName;
  unique_ptr<ExpressionNode> Expression;

public:
  AssignmentExpressionNode(const string &VariableName,
                           unique_ptr<ExpressionNode> Expression)
      : VariableName(VariableName), Expression(std::move(Expression)) {}

  Value *codegen() override;
};
```

An assignment is itself an expression. After storing the new value, it returns that value. That is what makes chained assignment possible.

## 5. Parse Assignment

`ParseExpression()` currently returns `ParseComparison()` directly.

Replace it with:

```cpp
static unique_ptr<ExpressionNode> ParseExpression() {
  if (CurrentToken == tok_var)
    return ParseVariableExpression();

  auto Expr = ParseComparison();
  if (!Expr)
    return nullptr;

  if (CurrentToken != tok_assign)
    return Expr;

  const string *AssignedName = Expr->getLValueName();
  if (!AssignedName)
    return LogErrorExpression("Destination of '=' must be a variable");

  string Name = *AssignedName;
  getNextToken(); // eat '='

  auto Right = ParseExpression();
  if (!Right)
    return nullptr;

  return make_unique<AssignmentExpressionNode>(Name, std::move(Right));
}
```

Add a forward declaration for `ParseVariableExpression()` with the other parser declarations:

```cpp
static unique_ptr<ExpressionNode> ParseVariableExpression();
```

Notice the recursive call on the right side. That is the small detail that makes assignment associate right to left.

The lvalue check rejects this immediately:

```pyxc
(1 + 2) = 3
```

Expected diagnostic:

```text
Destination of '=' must be a variable
```

## 6. Change `NamedValues` from Values to Slots

This is the central implementation change.

Replace:

```cpp
static std::map<string, Value *> NamedValues;
```

with:

```cpp
static std::map<string, AllocaInst *> NamedValues;
```

Before this change:

```text
NamedValues["x"] -> current LLVM value
```

After this change:

```text
NamedValues["x"] -> memory slot containing current value
```

Every variable kind will use the same representation:

```text
parameter     -> alloca
var local     -> alloca
loop variable -> alloca
```

## 7. Create Slots in the Entry Block

Add this helper near the code-generation globals:

```cpp
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VariableName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr,
                           VariableName);
}
```

Use a temporary builder so the allocation always goes at the start of the function's entry block, even when the current builder is generating a nested branch or loop.

This produces deliberately simple IR:

```llvm
%x = alloca double
store double 1.000000e+00, ptr %x
%x1 = load double, ptr %x
```

The optimization pipeline will clean it up later.

## 8. Make Variable Reads Load

`NameExpressionNode::codegen()` used to return the value stored directly in `NamedValues`.

Replace it with:

```cpp
Value *NameExpressionNode::codegen() {
  auto VariableBinding = NamedValues.find(Name);
  if (VariableBinding == NamedValues.end() || !VariableBinding->second)
    return LogErrorValue("Unknown variable name: '" + Name + "'");

  return TheBuilder->CreateLoad(Type::getDoubleTy(*TheContext),
                                VariableBinding->second, Name.c_str());
}
```

A name expression is an rvalue use, so it loads the current value from the slot.

## 9. Make Assignment Store

Implement the new node:

```cpp
Value *AssignmentExpressionNode::codegen() {
  Value *Value = Expression->codegen();
  if (!Value)
    return nullptr;

  auto VariableBinding = NamedValues.find(VariableName);
  if (VariableBinding == NamedValues.end() || !VariableBinding->second)
    return LogErrorValue("Unknown variable name: '" + VariableName + "'");

  TheBuilder->CreateStore(Value, VariableBinding->second);
  return Value;
}
```

The order is important:

```text
evaluate right side -> find destination slot -> store -> return stored value
```

Do not return the slot. The value of `x = 5` is `5`, not the address of `x`.

## 10. Add the `var` AST Node

A `var` expression owns a list of bindings and one body:

```cpp
class VariableExpressionNode : public ExpressionNode {
  vector<pair<string, unique_ptr<ExpressionNode>>> VariableBindings;
  unique_ptr<ExpressionNode> Body;

public:
  VariableExpressionNode(
      vector<pair<string, unique_ptr<ExpressionNode>>> VariableBindings,
      unique_ptr<ExpressionNode> Body)
      : VariableBindings(std::move(VariableBindings)),
        Body(std::move(Body)) {}

  Value *codegen() override;
};
```

This form:

```pyxc
var x = 1, y = x + 1: y
```

has two bindings. The second initializer must see the first binding, so parse and generate them in source order.

## 11. Parse `var`

Add `ParseVariableExpression()`:

```cpp
static unique_ptr<ExpressionNode> ParseVariableExpression() {
  getNextToken(); // eat 'var'

  vector<pair<string, unique_ptr<ExpressionNode>>> VariableBindings;

  while (true) {
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected name after 'var'");

    string ParsedName = Name;
    getNextToken(); // eat name

    unique_ptr<ExpressionNode> Init;
    if (CurrentToken == tok_assign) {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init)
        return nullptr;
    } else {
      Init = make_unique<NumberExpressionNode>(0.0);
    }

    VariableBindings.push_back({ParsedName, std::move(Init)});

    if (CurrentToken != tok_comma)
      break;
    getNextToken(); // eat ','
  }

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after var bindings");
  getNextToken(); // eat ':'
  consumeNewlines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return make_unique<VariableExpressionNode>(
      std::move(VariableBindings), std::move(Body));
}
```

A missing initializer defaults to `0.0`:

```pyxc
var x: x
```

## 12. Generate Slots for `var`

Implement `VariableExpressionNode::codegen()`:

```cpp
Value *VariableExpressionNode::codegen() {
  vector<pair<string, AllocaInst *>> OldBindings;
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  for (auto &Var : VariableBindings) {
    const string &VariableName = Var.first;
    ExpressionNode *Init = Var.second.get();

    Value *InitVal = Init->codegen();
    if (!InitVal)
      return nullptr;

    AllocaInst *Alloca =
        CreateEntryBlockAlloca(TheFunction, VariableName);
    TheBuilder->CreateStore(InitVal, Alloca);

    OldBindings.push_back({VariableName, NamedValues[VariableName]});
    NamedValues[VariableName] = Alloca;
  }

  Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;

  for (auto SavedBinding = OldBindings.rbegin();
       SavedBinding != OldBindings.rend(); ++SavedBinding) {
    const string &Name = SavedBinding->first;
    AllocaInst *PreviousValue = SavedBinding->second;

    if (PreviousValue)
      NamedValues[Name] = PreviousValue;
    else
      NamedValues.erase(Name);
  }

  return BodyVal;
}
```

Each initializer is generated before installing its own binding. Then the new binding is visible to later initializers and the body.

Restore bindings in reverse order. That correctly handles repeated shadowing, including repeated names in one binding list.

Build and run the first complete experiment:

```bash
cmake --build build
./build/pyxc
```

```pyxc
ready> var x = 1: x = x + 1
ready> var x = 1, y = x + 1: y
ready> var x: x
```

Expected:

```text
Parsed a top-level expression.
Evaluated to 2.000000
Parsed a top-level expression.
Evaluated to 2.000000
Parsed a top-level expression.
Evaluated to 0.000000
```

## 13. Put Parameters in Slots Too

After changing `NamedValues`, function parameters can no longer map directly to incoming `Argument` values.

In `FunctionDefinitionNode::codegen()`, replace the parameter setup with:

```cpp
NamedValues.clear();

for (auto &Argument : TheFunction->args()) {
  AllocaInst *Alloca =
      CreateEntryBlockAlloca(TheFunction, string(Argument.getName()));
  TheBuilder->CreateStore(&Argument, Alloca);
  NamedValues[string(Argument.getName())] = Alloca;
}
```

Now parameters use the same read and write path as locals:

```pyxc
ready> def increment(n): n = n + 1
ready> increment(5)
```

Expected:

```text
Parsed a function definition.
Parsed a top-level expression.
Evaluated to 6.000000
```

## 14. Change `for` from Step to Update

Update `ForExpressionNode` so it stores a complete `Update` expression and whether the loop declares a new variable:

```cpp
class ForExpressionNode : public ExpressionNode {
  string VariableName;
  bool DeclaresVariable;
  unique_ptr<ExpressionNode> Start, Condition, Update, Body;

public:
  ForExpressionNode(const string &VariableName, bool DeclaresVariable,
                    unique_ptr<ExpressionNode> Start,
                    unique_ptr<ExpressionNode> Condition,
                    unique_ptr<ExpressionNode> Update,
                    unique_ptr<ExpressionNode> Body)
      : VariableName(VariableName), DeclaresVariable(DeclaresVariable),
        Start(std::move(Start)), Condition(std::move(Condition)),
        Update(std::move(Update)), Body(std::move(Body)) {}

  Value *codegen() override;
};
```

At the start of `ParseForExpression()`, consume the optional `var`:

```cpp
bool DeclaresVariable = false;
if (CurrentToken == tok_var) {
  DeclaresVariable = true;
  getNextToken(); // eat 'var'
}
```

Rename the parsed `Step` expression to `Update`, then pass `DeclaresVariable` and `Update` into the AST node.

The semantic distinction is:

```pyxc
for var i = ...  # create a new loop-local slot
for i = ...      # reuse an existing slot
```

## 15. Generate the Mutable Loop

In `ForExpressionNode::codegen()`, evaluate the start value first. Then either create a new slot or find the existing one:

```cpp
AllocaInst *LoopVariableSlot = nullptr;
AllocaInst *PreviousVariableSlot = nullptr;

if (DeclaresVariable) {
  auto PreviousBinding = NamedValues.find(VariableName);
  if (PreviousBinding != NamedValues.end())
    PreviousVariableSlot = PreviousBinding->second;

  LoopVariableSlot = CreateEntryBlockAlloca(TheFunction, VariableName);
  TheBuilder->CreateStore(StartVal, LoopVariableSlot);
  NamedValues[VariableName] = LoopVariableSlot;
} else {
  auto ExistingBinding = NamedValues.find(VariableName);
  if (ExistingBinding == NamedValues.end() || !ExistingBinding->second)
    return LogErrorValue("Unknown variable name: '" + VariableName + "'");

  LoopVariableSlot = ExistingBinding->second;
  TheBuilder->CreateStore(StartVal, LoopVariableSlot);
}
```

Keep the condition, body, and after blocks from Chapter 10. Remove the manual PHI and numeric-step addition. At the end of the body, generate the complete update expression:

```cpp
if (!Update->codegen())
  return nullptr;

TheBuilder->CreateBr(CondBB);
```

After the loop, restore the shadowed binding only when the loop declared a new variable:

```cpp
if (DeclaresVariable) {
  if (PreviousVariableSlot)
    NamedValues[VariableName] = PreviousVariableSlot;
  else
    NamedValues.erase(VariableName);
}
```

Run the immediate experiment:

```bash
cmake --build build
./build/pyxc
```

```pyxc
ready> extern def printd(x)
ready> for var i = 1, i <= 3, i = i + 1: printd(i)
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

## 16. Build an Accumulator

Function bodies still contain only one expression, so use the loop's `0.0` result to sequence the loop before the final value:

```pyxc
ready> def sum_to(n): var acc = 0: (for var i = 1, i <= n, i = i + 1: acc = acc + i) + acc
ready> sum_to(5)
```

Expected:

```text
Parsed a function definition.
Parsed a top-level expression.
Evaluated to 15.000000
```

The `+ acc` is temporary glue. The loop runs for its side effects and returns `0.0`, so the addition returns the accumulated value. Chapter 12 will replace this with real statement blocks.

## 17. Let `mem2reg` Recover SSA

The source model uses memory because it is simple and correct:

```text
alloca -> load -> store
```

LLVM's promotion pass can turn eligible entry-block allocas back into SSA values and insert PHI nodes where control flow merges.

Without promotion, an update looks roughly like:

```llvm
%x = alloca double
store double 1.000000e+00, ptr %x
%x1 = load double, ptr %x
%addtmp = fadd double %x1, 1.000000e+00
store double %addtmp, ptr %x
```

After promotion, it can become just:

```llvm
%addtmp = fadd double 1.000000e+00, 1.000000e+00
```

For loops and branches, the pass inserts the necessary PHI nodes across the whole control-flow graph. That is precisely the global bookkeeping we do not want each AST node to reproduce.

Use `-v` to inspect the optimized IR:

```bash
./build/pyxc -v
```

Then enter a mutable-variable example. The final IR should contain SSA values and PHI nodes rather than a load and store for every source-level operation.

## 18. Run the Chapter Tests

Run the complete suite:

```bash
llvm-lit -v test/
```

Pay particular attention to tests covering:

```text
variable declarations
default initialization
multiple bindings
assignment values
right-associative assignment
invalid assignment destinations
parameter mutation
for var declarations
reuse of existing loop variables
variable shadowing
```

Also try the error boundary directly:

```pyxc
ready> (1 + 2) = 3
```

Expected:

```text
Error (Line 1, Column 9): Destination of '=' must be a variable
```

## What You Built

Chapter 11 now has one consistent mutable-variable model:

```text
declaration -> entry-block alloca + initial store
read        -> load
assignment  -> store and return assigned value
scope exit  -> restore previous name binding
optimization -> promote memory back to SSA
```

That is the useful boundary:

```text
simple frontend memory model -> LLVM optimization -> clean SSA
```

Next: [Chapter 12](chapter-12.md) adds statement blocks, so accumulators no longer need the `for(...) + result` sequencing trick.

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

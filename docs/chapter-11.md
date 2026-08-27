---
description: "Add mutable local variables and assignment using a temporary var ... : expression form, backed by memory slots, loads, and stores."
---
# 11. pyxc: Mutable Variables

## What I Am Building

Until now, names in pyxc haven't really been variables. I could give something a
name and use it later, but I couldn't change it. This chapter adds the missing
pieces: `var` declares mutable storage, and `=` writes a new value into it.

The `for` syntax changes along with it. Its last field used to be a number that
the loop added automatically. Now it's the whole update expression:

```pyxc
for i = 1, i <= 3, i = i + 1: ...
```

Eventually I want to write an accumulator like this:

```pyxc
def sum_to(n):
    var acc = 0
    for var i = 1, i <= n, i = i + 1:
        acc = acc + i
    return acc
```

That version has to wait for statement blocks in [Chapter 12](chapter-12.md).
Right now a function body is still one expression, so the Chapter 11 version is
a little cramped:

<!-- code-merge:start -->
```pyxc
ready> def sum_to(n): var acc = 0: (for var i = 1, i <= n, i = i + 1: acc = acc + i) + acc
```
```bash
Parsed a function definition.
```
```pyxc
ready> sum_to(5)
```
```bash
Parsed a top-level expression.
Evaluated to 15.000000
```
<!-- code-merge:end -->

The odd-looking `(for ...) + acc` is just temporary glue. `for` evaluates to
`0.0`, so the loop runs for its side effects and the final `+ acc` gives me the
accumulated value. I still can't put two statements next to each other, which is
why this needs a trick at all.

If I need this more than once, I can at least give the trick a name:

```pyxc
def sequence(x, y): y
```

```pyxc
sequence(for var i = 1, i <= n, i = i + 1: acc = acc + i, acc)
```

pyxc evaluates the arguments from left to right, so the loop runs first and
`sequence` returns the updated `acc`. It isn't pretty language design; it's a
bridge to the next chapter. The useful part here is the machinery underneath:
names now point to storage, reads load from it, and assignments store into it.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-11
```

## Grammar

I add `variable-expression` as a second alternative for `expression`, and give `comparison` an optional `"=" expression` tail. `for-expression` gains an optional leading `"var"`:

`code/chapter-11/pyxc.ebnf`

```grammardiff
*...
*parameters                        = parameter { "," parameter } ;
*parameter                         = name ;
-expression                        = comparison ;
+expression                        = variable-expression
+                                    | comparison [ "=" expression ] ;
+(* Constraint: If "=" is present, comparison must resolve to a variable lvalue.*)
+variable-expression               = "var" variable-binding
+                                    { "," variable-binding } ":"
+                                    [ end-of-lines ] expression ;
+variable-binding                  = name [ "=" expression ] ;
*comparison                        = sum { comparison-operator sum } ;
*comparison-operator               = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
*...
*                                    [ end-of-lines ] "else" ":"
*                                    [ end-of-lines ] expression ;
-for-expression                    = "for" name "=" expression ","
+for-expression                    = "for" [ "var" ] name "=" expression ","
*                                    expression "," expression ":"
*                                    [ end-of-lines ] expression ;
+(* The final expression performs the complete loop update; its value is discarded. *)
*...
```

Assignment needs a destination — somewhere in memory to write a value to. Using the two sides of `=`, I'll borrow a couple of terms:

```
lvalue = rvalue
```

**lvalue** — a memory location (like a variable). **rvalue** — a value (like `5`, `x`, `x + y`, or a function result).

`x` could be either, depending on which side of `=` it's on: I write into the left side, I read the right side to produce the value being written. I put `=` at the loosest level in the grammar — not part of `comparison`, `sum`, or `term`, but a tail on the whole `expression` production — so `a + b = c` parses as `(a + b) = c`. That fails, because `a + b` isn't a variable name, and I only accept a plain name as the left side of `=`.

`var` introduces one or more mutable locals and evaluates to the body's value. Later bindings can see earlier ones:

```pyxc
var x = 1, y = x + 1: y   # evaluates to 2
```

## New Token and AST Nodes

The lexer gains one new keyword token:

```cppdiff
*enum Token {
*  ...
*  // control
*  tok_if = -12,
*  tok_else = -13,
*
*  // loops
*  tok_for = -15,
*
+  // mutable variables
+  tok_var = -16,
*
*  // punctuation and operators
*  ...
*};
```

Added to the keyword table like every other reserved word:

```cppdiff
*static map<string, Token> Keywords = {
*    {"def", tok_def}, {"extern", tok_extern}, {"if", tok_if},
*    {"else", tok_else}, {"for", tok_for},
+    {"var", tok_var},
*};
```

Two new AST nodes do the real work.

`AssignmentExpressionNode` represents `x = x + 1`. It stores the destination name (the **lvalue**) and the right-hand side expression (the **rvalue**):

```cpp
/// AssignmentExpressionNode - Expression class for assignment to an existing variable.
/// The expression stores Expr into the named variable and produces the assigned
/// value.
class AssignmentExpressionNode : public ExpressionNode {
  string Name;
  unique_ptr<ExpressionNode> Expr;

public:
  AssignmentExpressionNode(const string &Name, unique_ptr<ExpressionNode> Expr)
      : Name(Name), Expr(std::move(Expr)) {}
  Value *codegen() override;
};
```

`VariableExpressionNode` represents `var a = 1, b = 2: body`. It stores the list of bindings plus the body:

```cpp
/// VariableExpressionNode - Expression class for mutable local variable bindings.
///   var a = <init>, b = <init> : <body>
/// Each binding allocates stack storage in the current function's entry block,
/// stores its initializer, shadows any outer binding of the same name for the
/// duration of the body, then restores the old binding afterward.
class VariableExpressionNode : public ExpressionNode {
  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;
  unique_ptr<ExpressionNode> Body;

public:
  VariableExpressionNode(
    vector<pair<string, unique_ptr<ExpressionNode>>> VarNames,
    unique_ptr<ExpressionNode> Body)
      : VarNames(std::move(VarNames)), Body(std::move(Body)) {}
  Value *codegen() override;
};
```

`getLValueName` is a new virtual on `ExpressionNode` itself, defaulting to `nullptr`. Only a plain variable reference overrides it to return its own name — that's how the assignment parser below decides whether the left-hand side of `=` is a legal destination:

```cppdiff
*class ExpressionNode {
*public:
*  virtual ~ExpressionNode() = default;
+  // getLValueName - If this node is a plain assignable variable, return its
+  // name; otherwise return nullptr.
+  virtual const string *getLValueName() const { return nullptr; }
*  virtual Value *codegen() = 0;
*};
```

`NameExpressionNode` is the only override — a plain name is the only expression shape that's ever a legal assignment target:

```cppdiff
*class NameExpressionNode : public ExpressionNode {
*  string Name;
*
*public:
*  NameExpressionNode(const string &Name) : Name(Name) {}
+  const string *getLValueName() const override { return &Name; }
*  Value *codegen() override;
*};
```

## Parsing `var`

`ParseVariableExpression` reads four things in sequence: the `var` keyword, one or more `name [= initializer]` bindings, a mandatory `:`, and then the body expression.

**Step 1: Eat `var` and prepare the binding list.**

```cpp
static unique_ptr<ExpressionNode> ParseVariableExpression() {
  getNextToken(); // eat 'var'

  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;
```

**Step 2: Parse each binding — a name, then an optional initializer.**

```cppdiff
*  getNextToken(); // eat 'var'
*  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;
+
+  while (true) {
+    if (CurrentToken != tok_name)
+      return LogErrorExpression("Expected name after 'var'");
+
+    string ParsedName = Name;
+    getNextToken(); // eat name
+
+    unique_ptr<ExpressionNode> Init;
+    if (CurrentToken == tok_assign) {
+      getNextToken(); // eat '='
+      Init = ParseExpression();
+      if (!Init)
+        return nullptr;
+    } else {
+      // No '=': default the variable to 0.0.
+      Init = make_unique<NumberExpressionNode>(0.0);
+    }
+
+    VarNames.push_back({ParsedName, std::move(Init)});
```

If there's no `=`, the variable defaults to `0.0`. The binding always produces a value, so what follows never has to special-case a missing initializer.

**Step 3: `,` means another binding; anything else ends the list.**

```cppdiff
*    }
*    VarNames.push_back({ParsedName, std::move(Init)});
+
+    if (CurrentToken != tok_comma)
+      break;
+    getNextToken(); // eat ','
+  }
```

**Step 4: Expect `:`, allow the body on the next line, parse the body.**

```cppdiff
*    getNextToken(); // eat ','
*  }
+
+  if (CurrentToken != tok_colon)
+    return LogErrorExpression("Expected ':' after var bindings");
+  getNextToken(); // eat ':'
+
+  consumeNewlines();
+
+  auto Body = ParseExpression();
+  if (!Body)
+    return nullptr;
+
+  return make_unique<VariableExpressionNode>(std::move(VarNames), std::move(Body));
+}
```

`ParseExpression` routes to `ParseVariableExpression` before anything else, if the current token is `var`:

```cppdiff
*static unique_ptr<ExpressionNode> ParseExpression() {
-  return ParseComparison();
-}
+  if (CurrentToken == tok_var)
+    return ParseVariableExpression();
+
+  auto Expr = ParseComparison();
+   if (!Expr)
+    return nullptr;
+  // ... assignment handling, shown in "Parsing Assignment" below ...
```

## Parsing Assignment

I deliberately let the grammar allow any `comparison` before the optional `=`, so it mirrors how I structure the parser: I parse a complete comparison first, then check whether `=` follows. That broad syntactic shape needs a constraint: when `=` is present, I require the parsed comparison to resolve to a variable lvalue. So an input like `1 < 2 = 10` matches the production, but I reject it in the lvalue check below.

Once `ParseComparison` returns — the same top-of-the-chain call chapter 10 already parses every expression through — I check whether `=` follows. If not, I hand back the expression unchanged. If so, I require the left-hand side to be a plain variable name, or I report a parse error:

```cppdiff
~  if (!Expr)
~   return nullptr;
*
+  if (CurrentToken != tok_assign)
+    return Expr; // no assignment — return the expression as-is
+
+  // The left-hand side must be a plain variable name (an lvalue).
+  const string *AssignedName = Expr->getLValueName();
+  if (!AssignedName)
+    return LogErrorExpression("Destination of '=' must be a variable");
+
+  string Name = *AssignedName;
+  getNextToken(); // eat '='
+
+  auto Right = ParseExpression(); // right-recursive, so a = b = 1 parses as a = (b = 1)
+  if (!Right)
+    return nullptr;
+
+  return make_unique<AssignmentExpressionNode>(Name, std::move(Right));
+}
```

This makes assignment:

- lower precedence than everything else — the entire left-hand expression is parsed before I even check for `=`
- right-associative — `a = b = 1` parses as `a = (b = 1)`, since the right side recurses into `ParseExpression` rather than stopping at `ParseComparison`

```pyxc
(1 + 2) = 3
```
```
Error (Line 1, Column 9): Destination of '=' must be a variable
(1 + 2) = 
        ^~~~
```

`(1 + 2)` parses fine as an expression, but its `getLValueName()` returns `nullptr` — only a bare name overrides that — so I reject the assignment before I ever look at the right-hand side.

## Memory Slots: From Values to Storage

Until this chapter, every name in pyxc referred to a value that could never change. `n` in `def increment(n): n + 1` means the same double for the entire call — I can think of `n` as shorthand for whatever got passed in, and nothing later in the function can invalidate that substitution.

That's exactly how `NamedValues` worked through chapter 4: it mapped a name straight to the LLVM value it stood for.

```cpp
// Before: the name maps directly to the incoming argument — fixed, immutable.
NamedValues[string(Argument.getName())] = &Argument;
```

Looking up `n` handed codegen the value to use wherever `n` appeared. Done — no further bookkeeping needed, because the value behind the name never moves.

Mutable variables break that. Take `def increment(n): var x = n: x = x + 1`. If `x` started out just *meaning* the value `5`, then `x = x + 1` would have to mean `5 = 5 + 1` — which isn't even a sentence. `5` can't become `6`; a value simply is what it is. For `x = x + 1` to mean anything, `x` can't stand for one fixed value. It has to stand for a *place*: somewhere a value lives that can hold something different a moment later.

```diagram
x ──▶ memory slot [ 5 ]
```

Reading `x` becomes "go get whatever's in the slot right now":

```diagram
x + 1
  │
  ▼
load(slot) + 1
  │
  ▼
5 + 1
```

Assigning to `x` becomes "put a new value in the slot" — the name itself doesn't move, only what it points at changes:

```diagram
x = x + 1
  │
  ▼
store(6, slot)

x ──▶ memory slot [ 6 ]
```

So `NamedValues` has to stop mapping names to values and start mapping names to slots:

```cppdiff
-static std::map<string, Value *> NamedValues;
+static std::map<string, AllocaInst *> NamedValues;
```

`AllocaInst` is LLVM's slot: a chunk of stack memory reserved in the current function. If I reach for a C++ analogy, `alloca` is `&x` — the address of a local variable — and `NamedValues` becomes something close to `map<string, double*>` instead of `map<string, double>`:

```cpp
double x = 5.0;    // reserve a slot, initialize it — CreateEntryBlockAlloca + CreateStore
double *slot = &x; // slot is what AllocaInst* is: the address of that slot

*slot = *slot + 1; // read through the pointer, write through the pointer — load and store
```

One difference: in C++, `&x` assumes `x` already exists as a variable, and you're just asking for its address. LLVM has no separate "declare `x`, then take its address" step — `alloca` itself is what creates the memory, and it hands back the pointer to it in one motion. The picture still holds: a name really does mean "a pointer to a slot" — there's just no earlier step where `x` existed on its own.

### Creating a Slot

`CreateEntryBlockAlloca` is the helper that actually reserves one:

```cpp
/// CreateEntryBlockAlloca - Create a memory slot in the current function's
/// entry block for a mutable variable.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
}
```

I use a temporary `IRBuilder` (`TmpB`) instead of the main `TheBuilder` because codegen might be deep inside a branch or loop body when this runs, but allocas for local variables belong at the very start of the function's entry block — not wherever the main builder happens to be pointing. Placing every alloca at the start of the entry block is a requirement for `mem2reg` to work correctly, further down this chapter.

Because `TmpB` is always reset to `begin()` of the entry block, each new alloca lands *before* all the previous ones. Declaring `var x, y` produces allocas in reverse order:

```pyxc
def foo(): var x, y: 0
```

```llvm
entry:
  %y = alloca double, align 8   ; inserted second, lands first
  %x = alloca double, align 8   ; inserted first, lands last
```

The reversal is harmless — allocas are just slot reservations with no ordering dependency between them. But it's worth checking that claim rather than trusting it, because a later binding can reference an earlier one: `var a = 1, b = a + 1` needs `b`'s initializer to read the value already stored in `a`. Here's the actual IR for `def foo(): var a = 1, b = a + 1: b`:

```llvm
define double @foo() {
entry:
  %b = alloca double, align 8
  %a = alloca double, align 8
  store double 1.000000e+00, ptr %a, align 8
  %a1 = load double, ptr %a, align 8
  %addtmp = fadd double %a1, 1.000000e+00
  store double %addtmp, ptr %b, align 8
  %b2 = load double, ptr %b, align 8
  ret double %b2
}
```

The allocas are still reversed (`%b` before `%a`), but the store/load/compute sequence underneath is in the right order: store `1.0` into `%a`, then load `%a` and add `1.0` for `b`'s initializer, then store into `%b`. `a + 1` reads correctly.

The reason is that two different builders are doing two different jobs. `TmpB` — reset to `begin()` on every call — only ever places the `alloca` instructions themselves, always at the very front of the block. Every store, load, and computation goes through the main `TheBuilder`, whose cursor only moves forward, in the order codegen actually runs: `a`'s initializer, then `a`'s alloca and store, then `b`'s initializer (which reads `a`), then `b`'s alloca and store. The two builders never interleave in a way that matters, because `alloca` doesn't read or compute anything — it has no dependency on other instructions for reordering to break. It only needs to exist *before its first use*, and `TmpB` guarantees that unconditionally by always inserting at the very top of the block, ahead of everything real work `TheBuilder` has emitted or will emit.

`mem2reg` doesn't care about alloca order either, for the same underlying reason — it's a pure stack reservation with no state to get out of sync.

With `CreateEntryBlockAlloca` in hand, creating a slot, storing into it, and recording it in `NamedValues` looks like this for a parameter entering a function:

```cppdiff
*Function *FunctionDefinitionNode::codegen() {
*  const string FunctionName = Signature->getName();
*  ...
*  // Step 2: create the entry block and point the builder at it.
*  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
*  TheBuilder->SetInsertPoint(BB);
*
-  // Step 3: I map each parameter name to its LLVM value. When I generate the
-  // body, I resolve each parameter reference through this table in
-  // NameExpressionNode::codegen().
-  NamedValues.clear();
-  for (auto &Argument : TheFunction->args())
-    NamedValues[string(Argument.getName())] = &Argument;
+  // Step 3: I store each argument in an entry-block stack slot and map its
+  // parameter name to that slot. When I generate the body, I resolve each
+  // parameter reference through this table in NameExpressionNode::codegen().
+  NamedValues.clear();
+  for (auto &Argument : TheFunction->args()) {
+    // After: the name maps to a memory slot that holds the current value.
+    AllocaInst *Alloca =
+        CreateEntryBlockAlloca(TheFunction, string(Argument.getName()));
+    TheBuilder->CreateStore(&Argument, Alloca);            // copy the incoming value into the slot
+    NamedValues[string(Argument.getName())] = Alloca; // the name now points at the slot
+  }
*
*  // Step 4: codegen the body, optimise, verify, or erase on failure.
*  ...
*}
```

From here on, every variable reference is a load from its slot, and every assignment is a store into it. That's the entire core implementation change — everything else in this chapter is wiring load/store into the right places.

## Loading and Storing Variables

Once names map to memory slots, I turn reading and writing a variable into an explicit load and store.

A variable reference loads the current value:

```cppdiff
-/// NameExpressionNode::codegen - A variable reference looks up the name in
-/// NamedValues and returns the Value* for the corresponding function argument.
-///
-/// For now NamedValues only contains the current function's parameters; any
-/// other name is an error. Mutable local variables (alloca/store/load) come
-/// in a later chapter.
+/// NameExpressionNode::codegen - A variable reference loads the current value
+/// from the variable's memory slot.
*Value *NameExpressionNode::codegen() {
*  auto VariableBinding = NamedValues.find(Name);
*  if (VariableBinding == NamedValues.end() || !VariableBinding->second)
*    return LogErrorValue("Unknown variable name: '" + Name + "'");
-  return VariableBinding->second;
+  return TheBuilder->CreateLoad(Type::getDoubleTy(*TheContext), VariableBinding->second,
+                                Name.c_str());
*}
```

For `def f(x): x`, that produces:

```llvm
%x2 = load double, ptr %x1, align 8
```

(`%x1` is the parameter's own alloca from Step 3 above; `%x2` is this load's result — LLVM numbers both off the hint `"x"` since the incoming argument itself is already called `%x`.)

An assignment evaluates the right-hand side, stores it into the memory slot, and returns the assigned value — that return is what makes `a = b = 1` work, since the inner `b = 1` has to produce `1.0` for the outer `a = ...` to store:

```cpp
/// AssignmentExpressionNode::codegen - Evaluate the Right, store it into the variable's
/// memory slot, and produce the assigned value.
Value *AssignmentExpressionNode::codegen() {
  Value *Value = Expr->codegen();
  if (!Value)
    return nullptr;

  auto VariableBinding = NamedValues.find(Name);
  if (VariableBinding == NamedValues.end() || !VariableBinding->second)
    return LogErrorValue("Unknown variable name: '" + Name + "'");

  TheBuilder->CreateStore(Value, VariableBinding->second);
  return Value;
}
```

For `x = x + 1`, where `x` is a `var` local rather than a parameter (so its own alloca is just `%x`, with no `%x1`-style renumbering), that produces:

```llvm
store double %addtmp, ptr %x, align 8
```

`%addtmp` is the `fadd` I'm storing — the same name hint `BinaryExpressionNode::codegen` already passes to `CreateFAdd` since [Chapter 7](chapter-07.md). `AssignmentExpressionNode::codegen` doesn't create that name; it just stores whatever `Value*` `Expr->codegen()` handed back. I'll show the full instruction sequence for this exact program, `def increment(n): var x = n: x = x + 1`, further below.

## Codegen for `var`

**Step 1: Evaluate initializers and allocate memory slots.**

```cpp
Value *VariableExpressionNode::codegen() {
  vector<pair<string, AllocaInst *>> OldBindings;
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.first;
    ExpressionNode *Init = Var.second.get();

    Value *InitVal = Init->codegen(); // evaluate before installing the binding
    if (!InitVal)
      return nullptr;

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    TheBuilder->CreateStore(InitVal, Alloca);
```

**Step 2: Install the new binding, saving any shadowed outer binding.**

```cppdiff
*    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
*    TheBuilder->CreateStore(InitVal, Alloca);
+
+    OldBindings.push_back({VarName, NamedValues[VarName]});
+    NamedValues[VarName] = Alloca; // shadow any outer binding
+  }
```

After steps 1 and 2, `var x = 1: x = x + 1` has emitted:

```llvm
%x = alloca double, align 8
store double 1.000000e+00, ptr %x, align 8
```

**Step 3: Codegen the body under the new bindings.**

The body `x = x + 1` loads `x`, adds 1, stores back, and returns the result:

```llvm
%x3 = load double, ptr %x, align 8
%addtmp = fadd double %x3, 1.000000e+00
store double %addtmp, ptr %x, align 8
```

```cppdiff
*    NamedValues[VarName] = Alloca; // shadow any outer binding
*  }
+
+  Value *BodyVal = Body->codegen();
+  if (!BodyVal)
+    return nullptr;
```

**Step 4: Restore outer bindings after the body.**

```cppdiff
*  if (!BodyVal)
*    return nullptr;
+
+  for (auto SavedBinding = OldBindings.rbegin();
+       SavedBinding != OldBindings.rend(); ++SavedBinding) {
+    const string &Name = SavedBinding->first;
+    AllocaInst *PreviousValue = SavedBinding->second;
+    if (PreviousValue)
+      NamedValues[Name] = PreviousValue; // restore saved binding
+    else
+      NamedValues.erase(Name); // name was not in scope before — remove it
+  }
+
+  return BodyVal;
+}
```

If an outer variable had the same name, it's visible again after the `var` body exits — normal lexical shadowing.

## Parameters Become Mutable Too

Once `NamedValues` holds allocas, function parameters have to use the same representation. That's the `FunctionDefinitionNode::codegen` loop I already walked through above, in [Memory Slots: From Values to Storage](#memory-slots-from-values-to-storage) — parameters go through the exact same alloca-and-store as any other variable, nothing extra to add here.

This unifies the whole language: parameters, `var` locals, and loop variables all live in memory slots. Variable references always load; assignments always store. One model everywhere.

## `for` Loops Switch to the Same Model

The old `for` codegen bound the loop variable directly to the incoming `Value*`, using a PHI node to merge the start value with each iteration's next value. That no longer fits now that every mutable local uses an alloca. `for var i` needs a fresh slot; plain `for i` needs to reuse the slot of an `i` already in scope — so I first record which case I'm in with a new `IsVarDecl` field on `ForExpressionNode` itself:

The constructor's parameter list and initializer list are reflowed below, one entry per line, so the one real addition to each is easy to spot — the real source wraps these lines differently:

```cppdiff
*class ForExpressionNode : public ExpressionNode {
*  string VarName;
+  bool IsVarDecl;
*  unique_ptr<ExpressionNode> Start, Condition, Step, Body;
*
*public:
*  ForExpressionNode(const string &VarName,
+                    bool IsVarDecl,
*                    unique_ptr<ExpressionNode> Start,
*                    unique_ptr<ExpressionNode> Condition,
*                    unique_ptr<ExpressionNode> Step,
*                    unique_ptr<ExpressionNode> Body)
*      : VarName(VarName),
+        IsVarDecl(IsVarDecl),
*        Start(std::move(Start)),
*        Condition(std::move(Condition)),
*        Step(std::move(Step)),
*        Body(std::move(Body)) {}
-
*  Value *codegen() override;
*};
```

`ParseForExpression` sets that field through the updated constructor:

```cppdiff
*static unique_ptr<ExpressionNode> ParseForExpression() {
*  getNextToken(); // eat 'for'
*
+  bool IsVarDecl = false;
+  if (CurrentToken == tok_var) {
+    IsVarDecl = true;
+    getNextToken(); // optional 'var'
+  }
+
*  if (CurrentToken != tok_name)
*    return LogErrorExpression("Expected variable name after 'for'");
*  string VarName = Name;
*  getNextToken(); // eat name
*
* ...
*
*  auto Body = ParseExpression();
*  if (!Body)
*    return nullptr;
*
-  return make_unique<ForExpressionNode>(VarName, std::move(Start),
-                                        std::move(Condition), std::move(Step),
-                                        std::move(Body));
+  return make_unique<ForExpressionNode>(VarName, IsVarDecl, std::move(Start),
+                                        std::move(Condition), std::move(Step),
+                                        std::move(Body));
*}
```

Then I switch `ForExpressionNode::codegen` to a memory slot for the loop variable, using `IsVarDecl` to decide whether to allocate a fresh slot or reuse an existing one:

```cppdiff
*Value *ForExpressionNode::codegen() {
*  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();
*
*  // Emit start value in the preheader (current block before the loop).
*  Value *StartVal = Start->codegen();
*  if (!StartVal)
*    return nullptr;
*
-  BasicBlock *PreheaderBB = TheBuilder->GetInsertBlock();
+  AllocaInst *LoopVariableSlot = nullptr;
+  AllocaInst *PreviousVariableSlot = nullptr;
+  if (IsVarDecl) {
+    // With `for var`, I declare a new loop variable. I save any binding it
+    // shadows so I can restore that binding after the loop, then I create the
+    // new slot.
+    auto PreviousBinding = NamedValues.find(VarName);
+    if (PreviousBinding != NamedValues.end())
+      PreviousVariableSlot = PreviousBinding->second;
+    LoopVariableSlot = CreateEntryBlockAlloca(TheFunction, VarName);
+    TheBuilder->CreateStore(StartVal, LoopVariableSlot);
+    NamedValues[VarName] = LoopVariableSlot;
+  } else {
+    // Without `var`, I reuse a variable that already exists. If the lookup
+    // fails, I report an unknown-variable error.
+    auto ExistingBinding = NamedValues.find(VarName);
+    if (ExistingBinding == NamedValues.end() || !ExistingBinding->second)
+      return LogErrorValue("Unknown variable name: '" + VarName + "'");
+    LoopVariableSlot = ExistingBinding->second;
+    TheBuilder->CreateStore(StartVal, LoopVariableSlot);
+  }
*
*  // Create all three blocks up front so we can reference them in branches.
*  BasicBlock *CondBB =
*      BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
*  BasicBlock *BodyBB =
*      BasicBlock::Create(*TheContext, "loop_body", TheFunction);
*  BasicBlock *AfterBB =
*      BasicBlock::Create(*TheContext, "after_loop", TheFunction);
*
*  // Unconditional jump from preheader into the condition check.
*  TheBuilder->CreateBr(CondBB);
*
*  // ---- loop_cond ----
*  TheBuilder->SetInsertPoint(CondBB);
*
-  // PHI picks start_val on the first iteration, next_i on subsequent ones.
-  // The back-edge incoming value is added below once we know BodyEndBB.
-  PHINode *Variable =
-      TheBuilder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
-  Variable->addIncoming(StartVal, PreheaderBB);
-
-  // Shadow any outer variable of the same name so the body sees the loop var.
-  Value *OldVal = NamedValues[VarName];
-  NamedValues[VarName] = Variable;
-
*  // Evaluate the condition; treat 0.0 as false, anything else as true.
*  Value *CondVal = Condition->codegen();
*  if (!CondVal)
*    return nullptr;
*  CondVal = TheBuilder->CreateFCmpONE(
*      CondVal, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
*  TheBuilder->CreateCondBr(CondVal, BodyBB, AfterBB);
*
*  // ---- loop_body ----
*  TheBuilder->SetInsertPoint(BodyBB);
*
*  // Body is evaluated for side effects; its value is discarded.
*  if (!Body->codegen())
*    return nullptr;
*
-  // Step: advance the loop variable automatically.
-  Value *StepVal = Step->codegen();
-  if (!StepVal)
-    return nullptr;
-  Value *NextVar = TheBuilder->CreateFAdd(Variable, StepVal, "nextvar");
-
-  // Body codegen may have changed the insert block (e.g. nested ifs added
-  // blocks). Capture where the body actually ended for the PHI back-edge.
-  BasicBlock *BodyEndBB = TheBuilder->GetInsertBlock();
-  Variable->addIncoming(NextVar, BodyEndBB);
+  // Execute the complete update expression; its value is discarded.
+  if (!Step->codegen())
+    return nullptr;
*  TheBuilder->CreateBr(CondBB);
*
*  // ---- after_loop ----
*  TheBuilder->SetInsertPoint(AfterBB);
*
*  // Restore the shadowed variable (if any) now that the loop is done.
-  if (OldVal)
-    NamedValues[VarName] = OldVal;
-  else
-    NamedValues.erase(VarName);
+  if (IsVarDecl) {
+    if (PreviousVariableSlot)
+      NamedValues[VarName] = PreviousVariableSlot;
+    else
+      NamedValues.erase(VarName);
+  }
*
*  // The for expression always produces 0.0.
*  return ConstantFP::get(*TheContext, APFloat(0.0));
*}
```

`Step` is now the complete update operation. For `i = i + 1`, assignment codegen
loads `i`, adds `1`, and stores the result. The loop itself only evaluates that
expression and branches back to the condition. This also permits updates such as
`i = i * 2` or function calls with side effects. An update that never changes
anything is legal, but may naturally leave the loop running forever.

`IsVarDecl` also controls teardown: when `var` was used, I remove the loop variable from `NamedValues` (or restore the shadowed outer binding) after the loop exits; when it wasn't, I leave the existing slot untouched.

Here is `def count(n): for var i = 1, i < n, i = i + 1: i` with `-O0 -v`:

```llvm
define double @count(double %n) {
entry:
  %i = alloca double, align 8        ; slot for loop variable i
  %n1 = alloca double, align 8       ; slot for parameter n
  store double %n, ptr %n1, align 8  ; store incoming n into its slot
  store double 1.000000e+00, ptr %i, align 8 ; i = 1 (start value)
  br label %loop_cond

loop_cond:
  %i2 = load double, ptr %i, align 8         ; load i
  %n3 = load double, ptr %n1, align 8        ; load n
  %cmptmp = fcmp olt double %i2, %n3         ; i < n
  %booltmp = uitofp i1 %cmptmp to double     ; bool → double (1.0 or 0.0)
  %loopcond = fcmp one double %booltmp, 0.000000e+00 ; non-zero?
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:
  %i4 = load double, ptr %i, align 8         ; body: i (result unused)
  %i5 = load double, ptr %i, align 8         ; load i for step computation
  %addtmp = fadd double %i5, 1.000000e+00    ; assignment RHS: i + 1
  store double %addtmp, ptr %i, align 8      ; assignment stores into i
  br label %loop_cond

after_loop:
  ret double 0.000000e+00  ; for always returns 0.0 (established in chapter 9)
}
```

`%i4` and `%i5` are two separate loads of `i`: `%i4` evaluates the body expression (`: i`) whose result goes unused, and `%i5` loads `i` again for the step computation. The `uitofp`/`fcmp one` pair converts the boolean comparison to a double and back — with optimizations on, `InstCombinePass` folds this away, and `mem2reg` removes the slots entirely.

### The Optional `var` in `for`

```
for [var] name = start, condition, update: body
```

`var` is optional, and I give the two forms different meanings:

- **`for var i = ...`** — I declare a new alloca slot named `i` in the current scope. If `i` already exists in the enclosing scope, this shadows it.
- **`for i = ...`** — I reuse an existing variable `i` that must already be in scope. If it doesn't exist, I report an error.

The distinction is mostly academic right now, because a function body is still a single expression, not a sequence of statements. The one place it surfaces is a nested loop reusing the outer loop variable:

```pyxc
for var i = 0, i < 10, i = i + 1:
   for i = 5, i < 11, i = i + 1:
    printd(i)
```

The outer `for var i` introduces `i` into scope. The inner `for i` finds that same slot and reuses it — the inner loop overwrites `i` on every outer iteration, then the outer condition re-evaluates with whatever `i` was left at once the inner loop finished. That's almost never useful deliberately; I include it here to make the semantics concrete.

The more natural use of `for i = ...` (without `var`) becomes clear once `var` statements exist independently of expressions, next chapter:

```pyxc
var x = 0.0
for x = 1, x < 10, x = x + 1:   # reuses x declared above
    printd(x)
```

Until then, `var` in `for` is the safe default.

## mem2reg: Cleaning Up the Memory Slots

I add `PromotePass` — commonly called `mem2reg` — to the optimization pipeline, ahead of the three passes I already had:

```cppdiff
+#include "llvm/Transforms/Utils/Mem2Reg.h"
*...
*
* static void InitializeModuleAndManagers() {
*  ...
*  if (OptLevel != 0) {
+    FunctionPasses->addPass(PromotePass());     // mem2reg: memory slots -> SSA regs
*    FunctionPasses->addPass(InstCombinePass()); // peephole rewrites
*    FunctionPasses->addPass(ReassociatePass()); // canonicalise commutative ops
*    FunctionPasses->addPass(GVNPass());         // eliminate common sub-expressions
*  }
* ...
*}
```

Every parameter and local variable now gets a memory slot. Without optimizations, `def increment(n): var x = n: x = x + 1` produces:

```llvm
define double @increment(double %n) {
entry:
  %x = alloca double, align 8         ; slot for local x
  %n1 = alloca double, align 8        ; slot for parameter n
  store double %n, ptr %n1, align 8   ; store incoming n
  %n2 = load double, ptr %n1, align 8 ; load n to initialise x
  store double %n2, ptr %x, align 8   ; x = n
  %x3 = load double, ptr %x, align 8  ; load x for addition
  %addtmp = fadd double %x3, 1.000000e+00 ; x + 1
  store double %addtmp, ptr %x, align 8   ; x = x + 1
  ret double %addtmp
}
```

Two slots, four loads, three stores — just to add 1 to a parameter. `mem2reg` looks at each `alloca`, traces every store and load, and replaces the whole pattern with plain values. With optimizations on:

```llvm
define double @increment(double %n) {
entry:
  %addtmp = fadd double %n, 1.000000e+00
  ret double %addtmp
}
```

Nine instructions down to two.

> Without `mem2reg` collapsing needless memory operations first, `GVNPass` and `InstCombinePass` would have to stay conservative around memory operations and would miss optimizations they'd otherwise catch.

### A More Complex Example

When control flow is involved, `mem2reg` has more work to do. Here's the raw form of `sum_to` from the top of this chapter — the `+`-as-sequencer trick that `sequence` wraps for readability:

```pyxc
def sum_to(n): var acc = 0: (for var i = 1, i <= n, i = i + 1: acc = acc + i) + acc
```

Without optimizations (`-O0 -v`), three slots and repeated loads/stores on every iteration:

```llvm
define double @sum_to(double %n) {
entry:
  %i = alloca double, align 8            ; slot for loop variable i
  %acc = alloca double, align 8          ; slot for accumulator
  %n1 = alloca double, align 8           ; slot for parameter n
  store double %n, ptr %n1, align 8      ; store n
  store double 0.000000e+00, ptr %acc, align 8 ; acc = 0
  store double 1.000000e+00, ptr %i, align 8   ; i = 1
  br label %loop_cond

loop_cond:
  %i2 = load double, ptr %i, align 8    ; load i
  %n3 = load double, ptr %n1, align 8   ; load n
  %cmptmp = fcmp ole double %i2, %n3    ; i <= n
  %booltmp = uitofp i1 %cmptmp to double
  %loopcond = fcmp one double %booltmp, 0.000000e+00
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:
  %acc4 = load double, ptr %acc, align 8 ; load acc
  %i5 = load double, ptr %i, align 8     ; load i
  %addtmp = fadd double %acc4, %i5       ; acc + i
  store double %addtmp, ptr %acc, align 8 ; acc = acc + i
  %i6 = load double, ptr %i, align 8     ; load i for step
  %addtmp7 = fadd double %i6, 1.000000e+00 ; i + 1
  store double %addtmp7, ptr %i, align 8   ; i = i + 1
  br label %loop_cond

after_loop:
  %acc8 = load double, ptr %acc, align 8   ; load final acc
  %addtmp9 = fadd double 0.000000e+00, %acc8 ; 0.0 + acc — the sequencing trick
  ret double %addtmp9
}
```

With optimizations on, all three slots and every load/store disappear. In their place, two PHI nodes at the top of `loop_cond` — one per mutable variable:

```llvm
define double @sum_to(double %n) {
entry:
  br label %loop_cond  ; jump straight to condition

loop_cond:
  ; acc: 0.0 on first iteration, acc+i on subsequent ones
  %acc.0 = phi double [ 0.000000e+00, %entry ], [ %addtmp, %loop_body ]
  ; i: 1.0 on first iteration, i+1 on subsequent ones
  %i.0 = phi double [ 1.000000e+00, %entry ], [ %addtmp7, %loop_body ]
  %cmptmp = fcmp ugt double %i.0, %n  ; i > n — the exit test, inverted
  br i1 %cmptmp, label %after_loop, label %loop_body

loop_body:
  %addtmp = fadd double %acc.0, %i.0       ; acc + i
  %addtmp7 = fadd double %i.0, 1.000000e+00 ; i + 1
  br label %loop_cond

after_loop:
  %addtmp9 = fadd double %acc.0, 0.000000e+00 ; the sequencing add survives — no fast-math
  ret double %addtmp9
}
```

Two things are worth noticing here. First, the optimizer canonicalized `i <= n` (branch to the body when true) into its negation, `i > n` (branch to `after_loop` when true), with the two branch targets swapped to match — same loop, an inverted way of asking the same question.

Second, the trailing `fadd ... 0.0` doesn't disappear, even after optimization. None of my three passes fold "add zero" away, and without `-ffast-math` LLVM wouldn't do it unconditionally anyway — `x + 0.0` isn't always exactly `x` in IEEE 754 (`-0.0 + 0.0` is `0.0`, not `-0.0`), so removing it is only safe if LLVM knows the sign of zero never matters here. It doesn't know that, so the instruction stays.

Each PHI node says: "on the first iteration, take the initial value (from `%entry`); on every subsequent iteration, take the updated value (from `%loop_body`)." Two mutable variables, two PHI nodes — one per slot that `mem2reg` promoted.

## Build and Run

```bash
cd code/chapter-11
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

Simple local update:

<!-- code-merge:start -->
```pyxc
ready> var x = 1: x = x + 1
```
```bash
Parsed a top-level expression.
Evaluated to 2.000000
```
<!-- code-merge:end -->

Multiple bindings — later initializers see earlier ones:

<!-- code-merge:start -->
```pyxc
ready> var x = 1, y = x + 1: y
```
```bash
Parsed a top-level expression.
Evaluated to 2.000000
```
<!-- code-merge:end -->

Local variable inside a function:

<!-- code-merge:start -->
```pyxc
ready> def increment(n): var x = n: x = x + 1
```
```bash
Parsed a function definition.
```
```pyxc
ready> increment(5)
```
```bash
Parsed a top-level expression.
Evaluated to 6.000000
```
<!-- code-merge:end -->

Accumulator with a loop, using the `+`-as-sequencer trick:

<!-- code-merge:start -->
```pyxc
ready> def sum_to(n): var acc = 0: (for var i = 1, i <= n, i = i + 1: acc = acc + i) + acc
```
```bash
Parsed a function definition.
```
```pyxc
ready> sum_to(5)
```
```bash
Parsed a top-level expression.
Evaluated to 15.000000
```
<!-- code-merge:end -->

Invalid assignment target:

<!-- code-merge:start -->
```pyxc
ready> (1 + 2) = 3
```
```bash
Error (Line 1, Column 9): Destination of '=' must be a variable
(1 + 2) = 
        ^~~~
```
<!-- code-merge:end -->

## What's Next

[Chapter 12](chapter-12.md) adds real statement blocks with Python-style indentation.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

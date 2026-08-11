---
description: "Add mutable local variables and assignment using a temporary var ... : expression form, backed by memory slots, loads, and stores."
---
# 11. pyxc: Mutable Variables

## What I Am Building

[Chapter 4](chapter-04.md) treated every variable as read-only. Function parameters were read-only. `for` loops introduced a variable and could update it internally, but I had no way to declare my own local variable and change it:

<!-- code-merge:start -->
```pyxc
ready> def bump(n): var x = n: x = x + 1
```
```bash
Error (Line 1, Column 18): Unexpected name 'x'
def bump(n): var x 
                 ^~~~
```
<!-- code-merge:end -->

I add two things:

- `var` — creates a new variable I can modify
- `=` — updates an existing variable

<!-- code-merge:start -->
```pyxc
ready> def bump(n): var x = n: x = x + 1
```
```bash
Parsed a function definition.
```
```pyxc
ready> bump(5)
```
```bash
Parsed a top-level expression.
Evaluated to 6.000000
```
<!-- code-merge:end -->

**A note on style:** `var x = n: x = x + 1` isn't code I'd choose to write. Pyxc still only supports single-expression bodies — everything after `:` has to be one expression — so multi-step mutation feels forced because it *is* forced right now. This chapter isn't about that syntax; it's about what happens underneath it. [Chapter 12](chapter-12.md) replaces expression bodies with real statement blocks, and the same machinery starts looking natural:

```pyxc
var x = n
...
x = x + 1
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-11
```

## Grammar

I add `variable-expression` as a second alternative for `expression`, and give `comparison` an optional `"=" expression` tail. `for-expression` gains an optional leading `"var"`:

`code/chapter-11/pyxc.ebnf`

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | external
                                     | top-level-expression ;
 function-definition               = "def" function-signature ":"
                                     [ end-of-lines ] expression ;
 external                          = "extern" "def" function-signature ;
 top-level-expression              = expression ;
 function-signature                = name "(" [ parameters ] ")" ;
 parameters                        = parameter { "," parameter } ;
 parameter                         = name ;
-expression                        = comparison ;
+expression                        = variable-expression
+                                    | comparison [ "=" expression ] ;
+variable-expression               = "var" variable-binding
+                                    { "," variable-binding } ":"
+                                    [ end-of-lines ] expression ;
+variable-binding                  = name [ "=" expression ] ;
 comparison                        = sum { comparison-operator sum } ;
 comparison-operator               = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum                               = term { ("+" | "-") term } ;
 term                              = factor { ("*" | "/" | "%") factor } ;
 factor                            = "-" factor | primary ;
 primary                           = name-expression
                                     | number-expression
                                     | parenthesized-expression
                                     | if-expression
                                     | for-expression ;
 if-expression                     = "if" expression ":"
                                     [ end-of-lines ] expression
                                     [ end-of-lines ] "else" ":"
                                     [ end-of-lines ] expression ;
-for-expression                    = "for" name "=" expression ","
+for-expression                    = "for" [ "var" ] name "=" expression ","
                                     expression "," expression ":"
                                     [ end-of-lines ] expression ;
 name-expression                   = name
                                     | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 number                            = digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 whitespace                        = " " | "\t" | "\v" | "\f" ;
```

Assignment needs a destination — somewhere in memory to write a value to. Using the two sides of `=`, I'll borrow a couple of terms:

```
lvalue = rvalue
```

**lvalue** — a memory location (like a variable). **rvalue** — a value (like `5`, `x`, `x + y`, or a function result).

`x` could be either, depending on which side of `=` it's on: the left side names a destination to write into, the right side is read to produce the value being written. `=` sits at the loosest level in the grammar — it's not part of `comparison`, `sum`, or `term`, it's a tail on the whole `expression` production — so `a + b = c` parses as `(a + b) = c`. That fails, because `a + b` isn't a variable name, and the parser only accepts a plain name as the left side of `=`.

`var` introduces one or more mutable locals and evaluates to the body's value. Later bindings can see earlier ones:

```pyxc
var x = 1, y = x + 1: y   # evaluates to 2
```

## New Token and AST Nodes

The lexer gains one new keyword token:

```cpp
tok_var = -18,
```

Added to the keyword table like every other reserved word:

```cpp
{"if", tok_if}, {"else", tok_else}, {"for", tok_for}, {"var", tok_var}
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
  VariableExpressionNode(vector<pair<string, unique_ptr<ExpressionNode>>> VarNames,
             unique_ptr<ExpressionNode> Body)
      : VarNames(std::move(VarNames)), Body(std::move(Body)) {}
  Value *codegen() override;
};
```

`getLValueName` is a new virtual on `ExpressionNode` itself, defaulting to `nullptr`. Only a plain variable reference overrides it to return its own name — that's how the assignment parser below decides whether the left-hand side of `=` is a legal destination:

```cpp
// getLValueName - If this node is a plain assignable variable, return its
// name. Every other expression kind returns nullptr.
virtual const string *getLValueName() const { return nullptr; }
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

```cpp
  while (true) {
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected name after 'var'");

    string ParsedName = Name;
    getNextToken(); // eat name

    unique_ptr<ExpressionNode> Init;
    if (CurrentToken == tok_equal) {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init)
        return nullptr;
    } else {
      Init = make_unique<NumberExpressionNode>(0.0);
    }

    VarNames.push_back({ParsedName, std::move(Init)});
```

If there's no `=`, the variable defaults to `0.0`. The binding always produces a value, so what follows never has to special-case a missing initializer.

**Step 3: `,` means another binding; anything else ends the list.**

```cpp
    if (CurrentToken != tok_comma)
      break;
    getNextToken(); // eat ','
  }
```

**Step 4: Expect `:`, allow the body on the next line, parse the body.**

```cpp
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after var bindings");
  getNextToken(); // eat ':'

  consumeNewlines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return make_unique<VariableExpressionNode>(std::move(VarNames), std::move(Body));
}
```

`ParseExpression` routes to `ParseVariableExpression` before anything else, if the current token is `var`:

```cpp
static unique_ptr<ExpressionNode> ParseExpression() {
  if (CurrentToken == tok_var)
    return ParseVariableExpression();

  auto Expr = ParseComparison();
  // ...
}
```

## Parsing Assignment

Once `ParseComparison` returns — the same top-of-the-chain call chapter 10 already parses every expression through — `ParseExpression` checks whether `=` follows. If not, it hands back the expression unchanged. If so, the left-hand side has to be a plain variable name, or it's a parse error:

```cpp
  if (CurrentToken != tok_equal)
    return Expr; // no assignment — return the expression as-is

  // The left-hand side must be a plain variable name (an lvalue).
  const string *AssignedName = Expr->getLValueName();
  if (!AssignedName)
    return LogErrorExpression("Destination of '=' must be a variable");

  string Name = *AssignedName;
  getNextToken(); // eat '='

  auto Right = ParseExpression(); // right-recursive, so a = b = 1 parses as a = (b = 1)
  if (!Right)
    return nullptr;

  return make_unique<AssignmentExpressionNode>(Name, std::move(Right));
}
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

`(1 + 2)` parses fine as an expression, but its `getLValueName()` returns `nullptr` — only a bare name overrides that — so the assignment is rejected before I ever look at the right-hand side.

## Memory Slots: From Values to Storage

Through chapter 4, `NamedValues` mapped variable names directly to LLVM `Value*` — the incoming argument value, fixed at the point the function was called. That worked only because variables were immutable: a parameter name could always refer to the same value forever.

```cpp
// Before: the name maps directly to the incoming argument — fixed, immutable.
NamedValues[Arg.getName()] = &Arg;
```

Mutable variables break that model. Once `x` can be reassigned, the name `x` can't mean "this one fixed value" anymore. It has to mean "the place where the current value of `x` lives":

```cpp
// After: the name maps to a memory slot that holds the current value.
AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName()); // reserve a slot
TheBuilder->CreateStore(&Arg, Alloca);       // copy the incoming value into it
NamedValues[Arg.getName()] = Alloca;         // name now points to the slot, not the value
```

So `NamedValues` changes type, from:

```cpp
static map<string, Value *> NamedValues;
```

to:

```cpp
static map<string, AllocaInst *> NamedValues;
```

Every variable name now maps to an `AllocaInst` — a memory slot in the current function's entry block. That's the entire core implementation change; everything else in this chapter follows from it.

## Creating the Memory Slots

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

The reversal is harmless — allocas are just slot reservations with no ordering dependency between them. The stores that write the initial values happen afterward, in declaration order, and `mem2reg` doesn't care about alloca order either.

## Loading and Storing Variables

Once names map to memory slots, reading and writing a variable becomes an explicit load and store.

A variable reference loads the current value:

```cpp
/// NameExpressionNode::codegen - A variable reference loads the current value
/// from the variable's memory slot.
Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end() || !It->second)
    return LogErrorV("Unknown variable name");
  return TheBuilder->CreateLoad(Type::getDoubleTy(*TheContext), It->second,
                                Name.c_str());
}
```

```llvm
%x2 = load double, ptr %x, align 8
```

An assignment evaluates the right-hand side, stores it into the memory slot, and returns the assigned value — that return is what makes `a = b = 1` work, since the inner `b = 1` has to produce `1.0` for the outer `a = ...` to store:

```cpp
/// AssignmentExpressionNode::codegen - Evaluate the Right, store it into the variable's
/// memory slot, and produce the assigned value.
Value *AssignmentExpressionNode::codegen() {
  Value *Value = Expr->codegen();
  if (!Value)
    return nullptr;

  auto It = NamedValues.find(Name);
  if (It == NamedValues.end() || !It->second)
    return LogErrorV("Unknown variable name");

  TheBuilder->CreateStore(Value, It->second);
  return Value;
}
```

```llvm
store double %addtmp, ptr %x, align 8
```

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

```cpp
    OldBindings.push_back({VarName, NamedValues[VarName]});
    NamedValues[VarName] = Alloca; // shadow any outer binding
  }
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

```cpp
  Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;
```

**Step 4: Restore outer bindings after the body.**

```cpp
  for (auto I = OldBindings.rbegin(), E = OldBindings.rend(); I != E; ++I) {
    if (I->second)
      NamedValues[I->first] = I->second; // restore saved binding
    else
      NamedValues.erase(I->first);        // name was not in scope before — remove it
  }

  return BodyVal;
}
```

If an outer variable had the same name, it's visible again after the `var` body exits — normal lexical shadowing.

## Parameters Become Mutable Too

Once `NamedValues` holds allocas, function parameters have to use the same representation. `FunctionDefinitionNode::codegen` now creates an entry-block alloca for each argument and stores the incoming LLVM argument value into it:

```cpp
NamedValues.clear();
for (auto &Arg : TheFunction->args()) {
  AllocaInst *Alloca =
      CreateEntryBlockAlloca(TheFunction, std::string(Arg.getName()));
  TheBuilder->CreateStore(&Arg, Alloca);
  NamedValues[std::string(Arg.getName())] = Alloca;
}
```

This unifies the whole language: parameters, `var` locals, and loop variables all live in memory slots. Variable references always load; assignments always store. One model everywhere.

## `for` Loops Switch to the Same Model

The old `for` codegen bound the loop variable directly to the incoming `Value*`. That no longer fits now that every mutable local uses an alloca, so `ForExpressionNode::codegen` switches to a memory slot for the loop variable too:

```cpp
// Allocate a memory slot for the loop variable and store the start value.
AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
TheBuilder->CreateStore(StartVal, Alloca);

// ...

// In the loop body, load the current value, add the step, store back.
Value *CurVar =
    TheBuilder->CreateLoad(Type::getDoubleTy(*TheContext), Alloca, VarName);
Value *NextVar = TheBuilder->CreateFAdd(CurVar, StepVal, "nextvar");
TheBuilder->CreateStore(NextVar, Alloca);
```

The parser records whether `var` was present by setting an `IsVarDecl` flag on `ForExpressionNode`:

```cpp
bool IsVarDecl = false;
if (CurrentToken == tok_var)
  IsVarDecl = true, getNextToken(); // optional 'var'
```

Codegen uses that flag to decide whether to allocate a fresh slot or reuse an existing one:

```cpp
if (IsVarDecl) {
  // 'for var i': allocate a new slot and store the start value.
  Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
  TheBuilder->CreateStore(StartVal, Alloca);
} else {
  // 'for i': look up the existing alloca — error if i is not in scope.
  auto It = NamedValues.find(VarName);
  if (It == NamedValues.end() || !It->second)
    return LogErrorV("Unknown variable name");
  Alloca = It->second;
  TheBuilder->CreateStore(StartVal, Alloca);
}
```

`IsVarDecl` also controls teardown: when `var` was used, the loop variable is removed from `NamedValues` (or the shadowed outer binding is restored) after the loop exits; when it wasn't, the existing slot is left untouched.

Here is `def count(n): for var i = 1, i < n, 1: i` with `-O0 -v`:

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
  %nextvar = fadd double %i5, 1.000000e+00   ; i + step (1.0)
  store double %nextvar, ptr %i, align 8     ; write new i back
  br label %loop_cond

after_loop:
  ret double 0.000000e+00  ; for always returns 0.0 (established in chapter 9)
}
```

`%i4` and `%i5` are two separate loads of `i`: `%i4` evaluates the body expression (`: i`) whose result goes unused, and `%i5` loads `i` again for the step computation. The `uitofp`/`fcmp one` pair converts the boolean comparison to a double and back — with optimizations on, `InstCombinePass` folds this away, and `mem2reg` removes the slots entirely.

### The Optional `var` in `for`

```
for [var] name = start, condition, step: body
```

`var` is optional and follows C++ semantics:

- **`for var i = ...`** — declares a new alloca slot named `i` in the current scope. If `i` already exists in the enclosing scope, this shadows it.
- **`for i = ...`** — reuses an existing variable `i` that must already be in scope. If it doesn't exist, this is an error.

The distinction is mostly academic right now, because a function body is still a single expression, not a sequence of statements. The one place it surfaces is a nested loop reusing the outer loop variable:

```pyxc
for var i = 0, i < 10, 1:
   for i = 5, i < 11, 1:
    printd(i)
```

The outer `for var i` introduces `i` into scope. The inner `for i` finds that same slot and reuses it — the inner loop overwrites `i` on every outer iteration, then the outer condition re-evaluates with whatever `i` was left at once the inner loop finished. That's almost never useful deliberately; it's here to make the semantics concrete.

The more natural use of `for i = ...` (without `var`) becomes clear once `var` statements exist independently of expressions, next chapter:

```pyxc
var x = 0.0
for x = 1, x < 10, 1:   # reuses x declared above
    printd(x)
```

Until then, `var` in `for` is the safe default.

## mem2reg: Cleaning Up the Memory Slots

This chapter adds `PromotePass` — commonly called `mem2reg` — to the optimization pipeline, ahead of the three passes I already had:

```cpp
TheFPM->addPass(PromotePass());     // mem2reg: memory slots -> SSA registers
TheFPM->addPass(InstCombinePass()); // peephole rewrites
TheFPM->addPass(ReassociatePass()); // canonicalise commutative ops
TheFPM->addPass(GVNPass());         // eliminate common sub-expressions
```

Every parameter and local variable now gets a memory slot. Without optimizations, `def bump(n): var x = n: x = x + 1` produces:

```llvm
define double @bump(double %n) {
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
define double @bump(double %n) {
entry:
  %addtmp = fadd double %n, 1.000000e+00
  ret double %addtmp
}
```

Nine instructions down to two.

> Without `mem2reg` collapsing needless memory operations first, `GVNPass` and `InstCombinePass` would have to stay conservative around memory operations and would miss optimizations they'd otherwise catch.

### A More Complex Example

When control flow is involved, `mem2reg` has more work to do. I still don't have a way to sequence two side-effecting expressions cleanly — that's still chapters away — so I reuse the same `+`-as-sequencer trick chapter 10's Mandelbrot renderer used: `for` always returns `0.0`, and `0.0 + acc` is just `acc`.

```pyxc
def acc_loop(n): var acc = 0: (for var i = 1, i < n, 1: acc = acc + i) + acc
```

Without optimizations (`-O0 -v`), three slots and repeated loads/stores on every iteration:

```llvm
define double @acc_loop(double %n) {
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
  %cmptmp = fcmp olt double %i2, %n3    ; i < n
  %booltmp = uitofp i1 %cmptmp to double
  %loopcond = fcmp one double %booltmp, 0.000000e+00
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:
  %acc4 = load double, ptr %acc, align 8 ; load acc
  %i5 = load double, ptr %i, align 8     ; load i
  %addtmp = fadd double %acc4, %i5       ; acc + i
  store double %addtmp, ptr %acc, align 8 ; acc = acc + i
  %i6 = load double, ptr %i, align 8     ; load i for step
  %nextvar = fadd double %i6, 1.000000e+00 ; i + 1
  store double %nextvar, ptr %i, align 8   ; i = i + 1
  br label %loop_cond

after_loop:
  %acc7 = load double, ptr %acc, align 8   ; load final acc
  %addtmp8 = fadd double 0.000000e+00, %acc7 ; 0.0 + acc — the sequencing trick
  ret double %addtmp8
}
```

With optimizations on, all three slots and every load/store disappear. In their place, two PHI nodes at the top of `loop_cond` — one per mutable variable:

```llvm
define double @acc_loop(double %n) {
entry:
  br label %loop_cond  ; jump straight to condition

loop_cond:
  ; acc: 0.0 on first iteration, acc+i on subsequent ones
  %acc.0 = phi double [ 0.000000e+00, %entry ], [ %addtmp, %loop_body ]
  ; i: 1.0 on first iteration, i+1 on subsequent ones
  %i.0 = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop_body ]
  %cmptmp = fcmp olt double %i.0, %n  ; i < n
  br i1 %cmptmp, label %loop_body, label %after_loop

loop_body:
  %addtmp = fadd double %acc.0, %i.0       ; acc + i
  %nextvar = fadd double %i.0, 1.000000e+00 ; i + 1
  br label %loop_cond

after_loop:
  %addtmp8 = fadd double %acc.0, 0.000000e+00 ; the sequencing add survives — no fast-math
  ret double %addtmp8
}
```

The trailing `fadd ... 0.0` doesn't disappear, even after optimization. None of my three passes fold "add zero" away, and without `-ffast-math` LLVM wouldn't do it unconditionally anyway — `x + 0.0` isn't always exactly `x` in IEEE 754 (`-0.0 + 0.0` is `0.0`, not `-0.0`), so removing it is only safe if LLVM knows the sign of zero never matters here. It doesn't know that, so the instruction stays.

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
ready> def bump(n): var x = n: x = x + 1
```
```bash
Parsed a function definition.
```
```pyxc
ready> bump(5)
```
```bash
Parsed a top-level expression.
Evaluated to 6.000000
```
<!-- code-merge:end -->

Accumulator with a loop, using the `+`-as-sequencer trick:

<!-- code-merge:start -->
```pyxc
ready> def sum_to(n): var acc = 0: (for var i = 1, i < n + 1, 1: acc = acc + i) + acc
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

---
description: "Add mutable local variables and assignment using a temporary var ... : expression form, backed by memory slots, loads, and stores."
---
# 10. Pyxc: Mutable Variables

## Where We Are

[Chapter 9](chapter-09.md) treated every variable as read-only. Function parameters were read-only. `for` loops introduced variables, and could  update them internally. However, you, the mighty programmer, couldn't create your own local variables and update them. That changes now. This chapter adds two things:

- `var` — creates a new variable we can modify
- `=` — updates existing variables

Nothing you aren't already familiar with. But the way LLVM handles this internally is super interesting. 

<!-- code-merge:start -->
```python
ready> def bump(n): return var x = n: x = x + 1
```
```bash
Parsed a function definition.
```
```python
ready> bump(5)
```
```bash
Parsed a top-level expression.
Evaluated to 6.000000
```
<!-- code-merge:end -->

**A note on style:** The examples look clunky. `var x = n: x = x + 1` isn't code we would write if we have any self-respect. That's intentional. Pyxc still only supports single expression bodies: everything after `:` must be a single expression. Multi-step mutation feels forced because it *is* forced.

We're keeping it this way because this chapter isn't about syntax. It's about what happens underneath. The next chapter replaces expression bodies with real statement blocks. There, the same machinery will look natural.

```python
var x = n
...
x = x + 1
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-10
```

## Grammar
Two new productions:

```ebnf
expression      = varexpr | unaryexpr binoprhs [ "=" expression ] ;
varexpr         = "var" varbinding { "," varbinding } ":" [ eols ] expression ;
varbinding      = identifier [ "=" expression ] ;
```

Assignment requires a destination — somewhere in memory to write a value to. Using the two sides of `=`, we make up a couple of terms:
```
lvalue = rvalue
```
**lvalue** — a memory location (like a variable)
**rvalue** — a value (like 5, x, x + y, or a function result)

If you're thinking, `x` could be an `lvalue` or an `rvalue`, you're right. The parser will treat the left as a memory destination to put the value into, and the right is be where you read the value from. 

`=` has the lowest precedence of any operator. `a + b = c` parses as `(a + b) = c`, which fails because `a + b` isn't a valid *lvalue*. The parser enforces that the left side of = must be a plain variable name.

`var` introduces one or more mutable locals and evaluates to the body's value. Later bindings can reference earlier ones:

```python
var x = 1, y = x + 1: y   # evaluates to 2
```

### Full Grammar
[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-10/pyxc.ebnf)

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = definition | decorateddef | external | toplevelexpr ;
definition      = "def" prototype ":" [ eols ] "return" expression ;
decorateddef    = binarydecorator eols "def" binaryopprototype ":" [ eols ] "return" expression
                | unarydecorator  eols "def" unaryopprototype  ":" [ eols ] "return" expression ;
binarydecorator = "@" "binary" "(" integer ")" ;
unarydecorator  = "@" "unary" ;
binaryopprototype = customopchar "(" identifier "," identifier ")" ;
unaryopprototype  = customopchar "(" identifier ")" ;
external        = "extern" "def" prototype ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ identifier { "," identifier } ] ")" ;
ifexpr          = "if" expression ":" [ eols ] expression [ eols ] "else" ":" [ eols ] expression ;
forexpr         = "for" [ "var" ] identifier "=" expression "," expression "," expression ":" [ eols ] expression ;
expression      = varexpr | unaryexpr binoprhs [ "=" expression ] ;
binoprhs        = { binaryop unaryexpr } ;
varexpr         = "var" varbinding { "," varbinding } ":" [ eols ] expression ;
varbinding      = identifier [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | primary ;
unaryop         = "-" | userdefunaryop ;
primary         = identifierexpr | numberexpr | parenexpr
                | ifexpr | forexpr ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
numberexpr      = number ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
builtinbinaryop = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
integer         = digit { digit } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
```


## New Token and AST Nodes

The lexer gains one new keyword token:

```cpp
tok_var = -18,
```

Added to the keyword table like every other reserved word:

```cpp
{"binary", tok_binary}, {"unary", tok_unary}, {"var", tok_var}
```

Two new AST nodes do the real work.

`AssignmentExprAST` represents `x = x + 1`. It stores the destination name (the **lvalue**) and the right-hand side expression (the **rvalue**):

```cpp
class AssignmentExprAST : public ExprAST {
  string Name; // lvalue
  unique_ptr<ExprAST> Expr; // rvalue

public:
  AssignmentExprAST(const string &Name, unique_ptr<ExprAST> Expr)
      : Name(Name), Expr(std::move(Expr)) {}
  Value *codegen() override;
};
```

`VarExprAST` represents `var a = 1, b = 2: body`. It stores the list of bindings plus the body:

```cpp
class VarExprAST : public ExprAST {
  vector<pair<string, unique_ptr<ExprAST>>> VarNames;
  unique_ptr<ExprAST> Body;

public:
  VarExprAST(vector<pair<string, unique_ptr<ExprAST>>> VarNames,
             unique_ptr<ExprAST> Body)
      : VarNames(std::move(VarNames)), Body(std::move(Body)) {}
  Value *codegen() override;
};
```

## Parsing var

`ParseVarExpr` reads four things in sequence: the `var` keyword, one or more `name [= initializer]` bindings, a mandatory `:`, and then the body expression.

**Step 1: Eat `var` and prepare the binding list.**

```cpp
static unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat 'var'
  vector<pair<string, unique_ptr<ExprAST>>> VarNames;
```

**Step 2: Parse each binding — a name, then an optional initializer.**

```cpp
  while (true) {
    if (CurTok != tok_identifier)
      return LogError("Expected identifier after 'var'");

    string Name = IdentifierStr;
    getNextToken(); // eat identifier

    unique_ptr<ExprAST> Init;
    if (CurTok == '=') {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init) return nullptr;
    } else {
      Init = make_unique<NumberExprAST>(0.0); // no initializer → default to 0.0
    }

    VarNames.push_back({Name, std::move(Init)});
```

If there is no `=`, the variable defaults to `0.0`. The binding always produces a value, so the code that follows never has to special-case an empty initializer.

**Step 3: `,` means another binding; anything else ends the list.**

```cpp
    if (CurTok != ',') break;
    getNextToken(); // eat ',' and loop for the next binding
  }
```

**Step 4: Expect `:`, allow the body on the next line, parse the body.**

```cpp
  if (CurTok != ':')
    return LogError("Expected ':' after var bindings");
  getNextToken(); // eat ':'

  consumeNewlines(); // body may start on the next line

  auto Body = ParseExpression();
  if (!Body) return nullptr;

  return make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}
```

If the current token is `var`, `ParseExpression` routes to `ParseVarExpr` before attempting the usual binary-expression path:

```cpp
static unique_ptr<ExprAST> ParseExpression() {
  if (CurTok == tok_var)
    return ParseVarExpr();

  auto LHS = ParseUnary();
  // ...
}
```

## Parsing Assignment

After `ParseBinOpRHS` returns, `ParseExpression` checks whether the next token is `=`. If not, the expression is returned as-is. If yes, the left-hand side must be a plain variable name — anything else is a parse error:

```cpp
  if (CurTok != '=')
    return Expr; // no assignment — return the binary expression

  // The left-hand side must be a plain variable name (an lvalue).
  const string *AssignedName = Expr->getLValueName();
  if (!AssignedName)
    return LogError("Destination of '=' must be a variable");

  string Name = *AssignedName;
  getNextToken(); // eat '='

  auto RHS = ParseExpression(); // right-recursive, so a = b = 1 parses as a = (b = 1)
  if (!RHS) return nullptr;

  return make_unique<AssignmentExprAST>(Name, std::move(RHS));
```

This makes assignment:

- lower precedence than all binary operators — the entire left-hand binary expression is parsed before `=` is checked
- right-associative — `a = b = 1` parses as `a = (b = 1)`

The parser enforces that the left-hand side is a plain variable name, not an arbitrary expression. `(1 + 2) = 3` is a parse error.

## Memory Slots: From Values to Storage

Until chapter 9, `NamedValues` mapped variable names directly to LLVM `Value*` — the incoming argument value, fixed at the point the function was called. That worked only because variables were immutable: a parameter name could always refer to the same value forever.

```cpp
// Before: the name maps directly to the incoming argument — fixed, immutable.
NamedValues[Arg.getName()] = &Arg;
```

Mutable variables break that model. Once `x` can be reassigned, the name `x` can no longer mean "this one fixed value". It has to mean "the place where the current value of `x` lives".

```cpp
// After: the name maps to a memory slot that holds the current value.
AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName()); // reserve a slot
Builder->CreateStore(&Arg, Alloca);          // copy the incoming value into it
NamedValues[Arg.getName()] = Alloca;         // name now points to the slot, not the value
```

So `NamedValues` changes from:

```cpp
static map<string, Value *> NamedValues;
```

to:

```cpp
static map<string, AllocaInst *> NamedValues;
```

Each variable name now maps to an `AllocaInst` — a memory slot in the current function's entry block. That is the entire core implementation change.

## CreateEntryBlockAlloca

This helper creates the memory slots:

```cpp
/// CreateEntryBlockAlloca - Create a memory slot in the current function's
/// entry block for a mutable variable.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VarName) {
  IRBuilder<> TmpB(
      &TheFunction->getEntryBlock(),     // insert into the entry block
      TheFunction->getEntryBlock().begin()); // at the very start, before any instructions
  return TmpB.CreateAlloca(
      Type::getDoubleTy(*TheContext), // type: a single double
      nullptr,                        // no array size (scalar slot)
      VarName);                       // name for the IR printout
}
```

A temporary `IRBuilder` (`TmpB`) is used instead of the main `Builder` because we may be codegenning deep inside a branch or loop body, but allocas for local variables belong in the function entry block — not wherever the main builder happens to be pointing. Placing all allocas at the start of the entry block is a requirement for `mem2reg` to work correctly.

## Loading and Storing Variables

Once names map to memory slots, reading and writing a variable becomes explicit load and store instructions.

A variable reference loads the current value:

```cpp
Value *VariableExprAST::codegen() {
  AllocaInst *A = NamedValues[Name];
  if (!A)
    return LogErrorV("Unknown variable name");
  return Builder->CreateLoad(Type::getDoubleTy(*TheContext), A, Name.c_str());
  //   → %x2 = load double, ptr %x, align 8
}
```

```llvm
%x2 = load double, ptr %x, align 8
```

An assignment evaluates the right-hand side, stores it into the memory slot, and returns the assigned value:

```cpp
Value *AssignmentExprAST::codegen() {
  Value *Val = Expr->codegen(); // evaluate the right-hand side first
  if (!Val) return nullptr;

  AllocaInst *A = NamedValues[Name];
  if (!A)
    return LogErrorV("Unknown variable name");

  Builder->CreateStore(Val, A); // → store double %val, ptr %x, align 8
  return Val;                   // return the assigned value (makes a = b = 1 work)
}
```

```llvm
store double %addtmp, ptr %x, align 8
```


## VarExprAST::codegen

**Step 1: Evaluate initializers and allocate memory slots.**

```cpp
Value *VarExprAST::codegen() {
  vector<pair<string, AllocaInst *>> OldBindings;
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.first;
    ExprAST *Init = Var.second.get();

    Value *InitVal = Init->codegen(); // evaluate before installing the binding
    if (!InitVal) return nullptr;

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    // → %x = alloca double, align 8
    Builder->CreateStore(InitVal, Alloca);
    // → store double %initval, ptr %x, align 8
```

**Step 2: Install the new binding, saving any shadowed outer binding.**

If a variable with the same name already exists, its alloca is saved so it can be restored later:

```cpp
    OldBindings.push_back({VarName, NamedValues[VarName]});
    NamedValues[VarName] = Alloca; // shadow any outer binding
  }
```

After steps 1 and 2, `var x = 1: x = x + 1` has emitted:

```llvm
%x    = alloca double, align 8
store double 1.000000e+00, ptr %x, align 8
```

**Step 3: Codegen the body under the new bindings.**

The body `x = x + 1` loads `x`, adds 1, stores back, and returns the result:

```llvm
%x1     = load double, ptr %x, align 8
%addtmp = fadd double %x1, 1.000000e+00
store double %addtmp, ptr %x, align 8
```

```cpp
  Value *BodyVal = Body->codegen();
  if (!BodyVal) return nullptr;
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

If an outer variable had the same name, it's visible again after the `var` body exits. This gives `var` normal lexical shadowing behavior.

## Parameters Become Mutable Too

Once `NamedValues` holds allocas, function parameters must use the same representation. `FunctionAST::codegen` now creates an entry-block alloca for each argument and stores the incoming LLVM argument value into it:

```cpp
NamedValues.clear();
for (auto &Arg : TheFunction->args()) {
  // Create a memory slot for each parameter.
  AllocaInst *Alloca =
      CreateEntryBlockAlloca(TheFunction, string(Arg.getName()));
  // Copy the incoming argument value into the slot.
  Builder->CreateStore(&Arg, Alloca);
  NamedValues[string(Arg.getName())] = Alloca;
}
```

This unifies the whole language: parameters, `var` locals, and loop variables all live in memory slots. Variable references always load; assignments always store. One model everywhere.

## for Loops Switch to the Same Model

The old `for` implementation bound the loop variable directly to the incoming `Value*`. That no longer fits now that all mutable locals use allocas. So we change `ForExprAST::codegen` to use a memory slot for the loop variable too:

```cpp
// Allocate a memory slot for the loop variable and store the start value.
AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
Builder->CreateStore(StartVal, Alloca);

// ...

// In the loop body, load the current value, add the step, store back.
Value *CurVar =
    Builder->CreateLoad(Type::getDoubleTy(*TheContext), Alloca, VarName);
Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
Builder->CreateStore(NextVar, Alloca);
```

The loop variable name is installed in `NamedValues` as an alloca for the duration of the loop, then restored (or removed) afterward — the same pattern as `VarExprAST::codegen`.

Here is `def count(n): return for var i = 1, i < n, 1: i` with `-O0 -v`:

```llvm
; def count(n): return for var i = 1, i < n, 1: i

define double @count(double %n) {
entry:
  %i  = alloca double, align 8        ; slot for loop variable i
  %n1 = alloca double, align 8        ; slot for parameter n
  store double %n, ptr %n1, align 8   ; store incoming n into its slot
  store double 1.000000e+00, ptr %i, align 8 ; i = 1 (start value)
  br label %loop_cond

loop_cond:
  %i2  = load double, ptr %i, align 8         ; load i
  %n3  = load double, ptr %n1, align 8        ; load n
  %cmptmp   = fcmp olt double %i2, %n3        ; i < n
  %booltmp  = uitofp i1 %cmptmp to double     ; bool → double (1.0 or 0.0)
  %loopcond = fcmp one double %booltmp, 0.000000e+00 ; non-zero?
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:
  %i4 = load double, ptr %i, align 8          ; body: i (result unused)
  %i5 = load double, ptr %i, align 8          ; load i for step computation
  %nextvar = fadd double %i5, 1.000000e+00    ; i + step (1.0)
  store double %nextvar, ptr %i, align 8      ; write new i back
  br label %loop_cond

after_loop:
  ret double 0.000000e+00  ; for always returns 0.0 (established in chapter 8)
}
```

`%i4` and `%i5` are two separate loads of `i` — `%i4` evaluates the body expression (`: i`) whose result is unused, and `%i5` loads `i` again for the step computation. The `uitofp`/`fcmp one` pair converts the boolean comparison to a double and back — with optimizations on, `InstCombinePass` folds this away and `mem2reg` removes the slots entirely.

### The Optional `var` in `for`

The grammar for `for` in this chapter is:

```
for [var] identifier = start, condition, step: body
```

The `var` keyword is optional and follows C++ semantics:

- **`for var i = ...`** — declares a new alloca slot named `i` in the current scope. If `i` already exists in the enclosing scope, this is an error.
- **`for i = ...`** — reuses an existing variable `i` that must already be in scope. If it does not exist, this is an error.

When `for var i` is used, the parser allocates a fresh alloca slot for `i`, stores the start value into it, and tears it down when the loop exits. When `for i` is used, the parser looks up the existing alloca for `i` and stores the start value into that — no new slot is created. The difference is not just scope rules: `for i = ...` will fail if `i` has not already been declared.

The distinction is mostly academic at this stage because a function body is still a single expression, not a sequence of statements. The one case where it surfaces is a nested loop reusing the outer loop variable:

```python
for var i = 0, i < 10, 1:
   for i = 5, i < 11, 1:
    printd(i)
```

Here the outer `for var i` introduces `i` into scope. The inner `for i` finds that same slot and reuses it — the inner loop overwrites `i` on every outer iteration, then the outer condition re-evaluates with whatever `i` was left at after the inner loop finished. That is almost never useful deliberately; it is shown here to make the semantics concrete.

The more natural use of `for i = ...` (without `var`) becomes clear in the next chapter once `var` statements exist independently:

```python
var x = 0.0
for x = 1, x < 10, 1:   # reuses x declared above
    printd(x)
```

Until then, `var` in `for` is the safe default.

## mem2reg: Cleaning Up the Memory Slots

This chapter adds `PromotePass` (commonly called `mem2reg`) to the optimization pipeline:

```cpp
TheFPM->addPass(PromotePass()); // mem2reg: replace alloca/load/store with plain values
```

Every parameter and local variable gets a memory slot. Without optimizations, `def bump(n): return var x = n: x = x + 1` produces:

```llvm
define double @bump(double %n) {
entry:
  %x  = alloca double, align 8        ; slot for local x
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

> **Note:** Without `mem2reg` collapsing needless memory operations, `GVNPass` and `InstCombinePass` will have to be conservative around memory operations and will miss optimizations they'd otherwise catch.

### A More Complex Example

When control flow is involved, `mem2reg` has more work to do. Define `;` as a sequencing operator and an accumulator loop that returns its result:

```python
@binary(1)
def ;(x, y): return y

def acc_loop(n): return var acc = 0: (for var i = 1, i < n, 1: acc = acc + i) ; acc
```

Without optimizations (`-O0 -v`), three slots and repeated loads/stores on every iteration:

```llvm
define double @acc_loop(double %n) {
entry:
  %i   = alloca double, align 8          ; slot for loop variable i
  %acc = alloca double, align 8          ; slot for accumulator
  %n1  = alloca double, align 8          ; slot for parameter n
  store double %n, ptr %n1, align 8      ; store n
  store double 0.000000e+00, ptr %acc, align 8 ; acc = 0
  store double 1.000000e+00, ptr %i, align 8   ; i = 1
  br label %loop_cond

loop_cond:
  %i2  = load double, ptr %i, align 8    ; load i
  %n3  = load double, ptr %n1, align 8   ; load n
  %cmptmp  = fcmp olt double %i2, %n3   ; i < n
  %booltmp = uitofp i1 %cmptmp to double
  %loopcond = fcmp one double %booltmp, 0.000000e+00
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:
  %acc4   = load double, ptr %acc, align 8 ; load acc
  %i5     = load double, ptr %i, align 8   ; load i
  %addtmp = fadd double %acc4, %i5         ; acc + i
  store double %addtmp, ptr %acc, align 8  ; acc = acc + i
  %i6     = load double, ptr %i, align 8   ; load i for step
  %nextvar = fadd double %i6, 1.000000e+00 ; i + 1
  store double %nextvar, ptr %i, align 8   ; i = i + 1
  br label %loop_cond

after_loop:
  %acc7  = load double, ptr %acc, align 8  ; load final acc
  %binop = call double @"binary;"(double 0.000000e+00, double %acc7)
  ret double %binop
}
```

With optimizations on, all three slots and every load/store disappear. In their place, two phi nodes at the top of `loop_cond` — one for each mutable variable:

```llvm
define double @acc_loop(double %n) {
entry:
  br label %loop_cond  ; jump straight to condition

loop_cond:
  ; acc: 0.0 on first iteration, acc+i on subsequent ones
  %acc.0 = phi double [ 0.000000e+00, %entry ], [ %addtmp, %loop_body ]
  ; i: 1.0 on first iteration, i+1 on subsequent ones
  %i.0   = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop_body ]
  %cmptmp = fcmp olt double %i.0, %n  ; i < n
  br i1 %cmptmp, label %loop_body, label %after_loop

loop_body:
  %addtmp  = fadd double %acc.0, %i.0       ; acc + i
  %nextvar = fadd double %i.0, 1.000000e+00 ; i + 1
  br label %loop_cond

after_loop:
  ; binary; discards 0.0 (for's return value) and returns acc.
  ; the call is still present because binary; has no readnone attribute —
  ; the optimizer can't prove it has no side effects, so it keeps the call.
  %binop = call double @"binary;"(double 0.000000e+00, double %acc.0)
  ret double %binop
}
```

Each phi node says: "on the first iteration take the initial value (from `%entry`); on every subsequent iteration take the updated value (from `%loop_body`)." Two mutable variables, two phi nodes — one per slot that `mem2reg` promoted.

## Build and Run

```bash
cd code/chapter-10
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

Simple local update:

<!-- code-merge:start -->
```python
ready> var x = 1: x = x + 1
```
```bash
Parsed a top-level expression.
Evaluated to 2.000000
```
<!-- code-merge:end -->

Multiple bindings — later initializers see earlier ones:

<!-- code-merge:start -->
```python
ready> var x = 1, y = x + 1: y
```
```bash
Parsed a top-level expression.
Evaluated to 2.000000
```
<!-- code-merge:end -->

Local variable inside a function:

<!-- code-merge:start -->
```python
ready> def bump(n): return var x = n: x = x + 1  # returns n+1
```
```bash
Parsed a function definition.
```
```python
ready> bump(5)
```
```bash
Parsed a top-level expression.
Evaluated to 6.000000
```
<!-- code-merge:end -->

Accumulator with a loop:

<!-- code-merge:start -->
```python
ready> @binary(1)
def ;(x, y): return y
```
```bash
Parsed a user-defined operator.
```
```python
ready> def sum_to(n): return var acc = 0:
    (for var i = 1, i < n + 1, 1: acc = acc + i) ; acc
```
```bash
Parsed a function definition.
```
```python
ready> sum_to(5)
```
```bash
Parsed a top-level expression.
Evaluated to 15.000000
```
<!-- code-merge:end -->

Invalid assignment target:

<!-- code-merge:start -->
```python
ready> (1 + 2) = 3
```
```bash
Error (Line 1, Column 9): Destination of '=' must be a variable
(1 + 2) =
        ^~~~
```
<!-- code-merge:end -->

## What's Next

[Chapter 11](chapter-11.md) replaces the single-expression function body with real statement blocks. That makes mutable variables much more natural to use: assignment can stand on its own line, `return` can appear anywhere in a function body, and examples stop needing expression-level workarounds like `var acc = 0: (for ...) ; acc`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

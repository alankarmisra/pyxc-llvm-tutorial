---
description: "Add mutable local variables and assignment using a temporary var ... : expression form, backed by stack slots, loads, and stores."
---
# 10. Pyxc: Mutable Variables

## Where We Are

[Chapter 9](chapter-09.md) added user-defined operators, but every variable in Pyxc is still immutable. Function parameters can be read, loop variables can be introduced by `for`, but there is no way to create a local variable and update it. This chapter adds `var` — a scoped mutable binding — and `=` assignment:

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

One caveat up front: the new syntax is intentionally transitional. `var x = ... : expression` is not especially Pythonic. It exists because Pyxc still has expression bodies only. The next two chapters replace this temporary syntax with real statement blocks and indentation-sensitive syntax.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-10
```

## Grammar
This chapter extends the grammar in two places: a new `varexpr` production for local bindings, and assignment as the loosest expression form.

```ebnf
expression      = varexpr | unaryexpr binoprhs [ "=" expression ] ;  
varexpr         = "var" varbinding { "," varbinding } ":" [ eols ] expression ; 
varbinding      = identifier [ "=" expression ] ;                     
```

Two forms are new:

- `var x = 1, y = 2: expression` — introduces one or more mutable locals, evaluates the body under those bindings, and returns the body's value. Later initializers can reference earlier ones: `var x = 1, y = x + 1: y` evaluates to `2`.
- `x = x + 1` — assigns to an existing mutable local (or a function parameter); evaluates to the new value.

`var` must come first in the expression, and the `:` is mandatory. The body after `:` can stay on the same line or move to the next line because `consumeNewlines()` is already part of the expression forms.

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
forexpr         = "for" identifier "=" expression "," expression "," expression ":" [ eols ] expression ;
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

`AssignmentExprAST` represents `x = x + 1`. It stores the destination name and the right-hand side:

```cpp
class AssignmentExprAST : public ExprAST {
  string Name;
  unique_ptr<ExprAST> Expr;

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

`ParseVarExpr` reads the `var` keyword, one or more `name [= initializer]` bindings separated by commas, the mandatory `:`, then the body expression:

```cpp
static unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat 'var'

  vector<pair<string, unique_ptr<ExprAST>>> VarNames;

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

    if (CurTok != ',') break; // no more bindings
    getNextToken();            // eat ',' and loop for the next binding
  }

  if (CurTok != ':')
    return LogError("Expected ':' after var bindings");
  getNextToken(); // eat ':'

  // Allow the body to start on the next line:
  //   var x = 1:
  //     x + 2
  consumeNewlines();

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

Assignment is parsed after the binary expression has been built. If the result of `ParseBinOpRHS` is a plain variable reference and the next token is `=`, we consume the `=` and parse the right-hand side recursively:

```cpp
/// expression
///   = varexpr | unaryexpr binoprhs [ "=" expression ] ;
static unique_ptr<ExprAST> ParseExpression() {
  if (CurTok == tok_var)
    return ParseVarExpr();

  auto LHS = ParseUnary();
  if (!LHS) return nullptr;

  auto Expr = ParseBinOpRHS(0, std::move(LHS));
  if (!Expr) return nullptr;

  if (CurTok != '=')
    return Expr; // no assignment — return the binary expression

  // The left-hand side must be a plain variable name.
  const string *AssignedName = Expr->getVariableName();
  if (!AssignedName)
    return LogError("Destination of '=' must be a variable");

  string Name = *AssignedName;
  getNextToken(); // eat '='

  auto RHS = ParseExpression(); // right-recursive, so chains right-to-left
  if (!RHS) return nullptr;

  return make_unique<AssignmentExprAST>(Name, std::move(RHS));
}
```

This makes assignment:

- lower precedence than all binary operators — the entire left-hand binary expression is parsed before `=` is checked
- right-associative — `a = b = 1` parses as `a = (b = 1)`

The parser enforces that the left-hand side is a plain variable name, not an arbitrary expression. `(1 + 2) = 3` is a parse error.

## Stack Slots: From Values to Storage

Until chapter 9, `NamedValues` mapped variable names directly to LLVM `Value*` — the SSA values produced by the function's incoming arguments. That worked only because variables were immutable: a parameter name could always refer to the same SSA value forever.

Mutable variables break that model. Once `x` can be reassigned, the name `x` can no longer mean "this one fixed SSA value". It has to mean "the place where the current value of `x` lives".

So `NamedValues` changes from:

```cpp
static map<string, Value *> NamedValues;
```

to:

```cpp
static map<string, AllocaInst *> NamedValues;
```

Each variable name now maps to an `AllocaInst` — a stack slot in the current function's entry block. That is the entire core implementation change.

## CreateEntryBlockAlloca

This helper creates the stack slots:

```cpp
/// CreateEntryBlockAlloca - Create a stack slot in the current function's
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

A temporary `IRBuilder` (`TmpB`) is used instead of the main `Builder` because we may be codegenning deep inside a branch or loop body, but allocas for local variables belong in the function entry block — not wherever the main builder happens to be pointing. Placing all allocas at the start of the entry block is a convention LLVM's `mem2reg` pass depends on when promoting allocas to SSA registers.

## Loading and Storing Variables

Once names map to stack slots, reading and writing a variable becomes explicit load and store instructions.

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

An assignment evaluates the right-hand side, stores it into the stack slot, and returns the assigned value:

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

Returning the assigned value is what makes assignment fit naturally into an expression language.

## VarExprAST::codegen

**Step 1: Evaluate initializers and allocate stack slots.**

The initializer is evaluated *before* the new name is installed, so `var x = x: ...` looks up the outer `x`, not the one being declared:

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
  // Create a stack slot for each parameter.
  AllocaInst *Alloca =
      CreateEntryBlockAlloca(TheFunction, string(Arg.getName()));
  // Copy the incoming argument value into the slot.
  Builder->CreateStore(&Arg, Alloca);
  NamedValues[string(Arg.getName())] = Alloca;
}
```

This unifies the whole language: parameters, `var` locals, and loop variables all live in stack slots. Variable references always load; assignments always store. One model everywhere.

## for Loops Switch to the Same Model

The old `for` implementation bound the loop variable directly to an SSA `Value*`. That no longer fits now that all mutable locals use allocas. So we change `ForExprAST::codegen` to use a stack slot for the loop variable too:

```cpp
// Allocate a stack slot for the loop variable and store the start value.
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

Here is `def count(n): return for i = 1, i < n, 1: i` with `-O0 -v`:

```llvm
define double @count(double %n) {
entry:
  %i  = alloca double, align 8
  %n1 = alloca double, align 8
  store double %n,           ptr %n1, align 8
  store double 1.000000e+00, ptr %i,  align 8
  br label %loop_cond

loop_cond:
  %i2       = load double, ptr %i,  align 8
  %n3       = load double, ptr %n1, align 8
  %cmptmp   = fcmp olt double %i2, %n3
  %booltmp  = uitofp i1 %cmptmp to double
  %loopcond = fcmp one double %booltmp, 0.000000e+00
  br i1 %loopcond, label %loop_body, label %after_loop

loop_body:
  %i4      = load double, ptr %i, align 8
  %i5      = load double, ptr %i, align 8
  %nextvar = fadd double %i5, 1.000000e+00
  store double %nextvar, ptr %i, align 8
  br label %loop_cond

after_loop:
  ret double 0.000000e+00
}
```

The loop variable `i` gets its own alloca, loaded and stored on every iteration. The `uitofp`/`fcmp one` pair is LLVM's way of converting the boolean comparison result back to a double for the branch — we'll see `mem2reg` clean all of this up in [mem2reg: Cleaning Up the Stack Slots](#mem2reg-cleaning-up-the-stack-slots).

## What the IR Looks Like

The alloca-based approach is deliberate — the frontend puts every parameter and mutable variable into a stack slot to avoid having to compute SSA form itself. Every read is a `load`, every write is a `store`. Even a function as simple as `def bump(n): return n + 1` shows this with `-O0 -v`:

```llvm
define double @bump(double %n) {
entry:
  %n1     = alloca double, align 8
  store double %n, ptr %n1, align 8
  %n2     = load double, ptr %n1, align 8
  %addtmp = fadd double %n2, 1.000000e+00
  ret double %addtmp
}
```

A stack slot for `n`, a store, a load — just to add 1.

## mem2reg: Cleaning Up the Stack Slots

This chapter adds `PromotePass` (commonly called `mem2reg`) to the optimization pipeline:

```cpp
TheFPM->addPass(PromotePass()); // mem2reg: stack slots → SSA registers
```

`mem2reg` looks at each `alloca` and traces every value stored into and loaded from it. If the slot never escapes (no pointer to it is passed anywhere), it replaces the whole `alloca`/`load`/`store` pattern with plain values. The same `bump` with optimizations on:

```llvm
define double @bump(double %n) {
entry:
  %addtmp = fadd double %n, 1.000000e+00
  ret double %addtmp
}
```

Five instructions down to two. This is what LLVM's other passes — `GVNPass`, `InstCombinePass` — expect to see. They are designed to work on values, not memory operations, and `mem2reg` gives them exactly that.

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
Evaluated to 3.000000
```
<!-- code-merge:end -->

Multiple bindings — later initializers see earlier ones:

<!-- code-merge:start -->
```python
ready> var x = 1, y = x + 1: y
```
```bash
Parsed a top-level expression.
Evaluated to 3.000000
```
<!-- code-merge:end -->

Local variable inside a function:

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
    (for i = 1, i < n + 1, 1: acc = acc + i) ; acc
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

Chapter 11 replaces the single-expression function body with real statement blocks. That makes mutable variables much more natural to use: assignment can stand on its own line, `return` can appear anywhere in a function body, and examples stop needing expression-level workarounds like `var acc = 0: (for ...) ; acc`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

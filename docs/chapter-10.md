---
description: "Add mutable local variables and assignment using a temporary var ... : expression form, backed by stack slots, loads, and stores."
---
# 10. Pyxc: Mutable Variables

## Where We Are

[Chapter 9](chapter-09.md) added user-defined operators, but every variable in Pyxc is still immutable. Function parameters can be read, loop variables can be introduced by `for`, but there is still no way to create a local variable and update it.

Before this chapter, even a tiny local update fails at parse time:

<!-- code-merge:start -->
```python
ready> def bump(n): return var x = n: x = x + 1
```
```bash
Error (Line 1, Column 25): Unexpected identifier 'x'
def bump(n): return var x = n
                        ^~~~
```
<!-- code-merge:end -->

After this chapter, the same function works:

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

One caveat up front: the new syntax is intentionally transitional. `var x = ... : expression` is not especially Pythonic. It exists because Pyxc still has expression bodies only. The next two chapters replace this temporary shape with real statement blocks and indentation-sensitive syntax.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-10
```

## Grammar

Chapter 10 extends the grammar in two places: a new `varexpr` production for local bindings, and assignment as the loosest expression form.

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

Two forms are new:

- `var x = 1, y = 2: expression`
- `x = x + 1`

`var` must come first in the expression, and the `:` is mandatory. The body after `:` can stay on the same line or move to the next line because `consumeNewlines()` is already part of the chapter 8/9 expression forms.

## Mutable Variables Are Still Expressions

Pyxc still has no statement blocks. So chapter 10 adds mutable variables in expression form:

```python
var x = 1: x = x + 2
```

This means:

- `var` introduces one or more local variables
- each variable gets its own mutable storage
- the body expression runs with those bindings in scope
- the whole `var` expression evaluates to the value of the body

Multiple bindings are allowed:

```python
var x = 1, y = x + 2: y
```

In the current implementation, bindings are initialized from left to right. So later initializers can refer to earlier bindings in the same `var`.

## New Token and AST Nodes

The lexer gains one new keyword token:

```cpp
// mutable variables
tok_var = -18,
```

It is added to the keyword table like every other reserved word:

```cpp
{"binary", tok_binary}, {"unary", tok_unary}, {"var", tok_var}
```

Two new AST nodes do the real work.

`AssignmentExprAST` represents:

```python
x = x + 1
```

It stores the destination name and the right-hand side expression:

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

`VarExprAST` represents:

```python
var a = 1, b = 2: body
```

It stores the list of bindings plus the body:

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

`ParseVarExpr` reads the `var` keyword, one or more bindings, the mandatory `:`, then the body expression:

```cpp
static unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat 'var'
  vector<pair<string, unique_ptr<ExprAST>>> VarNames;

  while (true) {
    string Name = IdentifierStr;
    getNextToken(); // eat identifier

    unique_ptr<ExprAST> Init;
    if (CurTok == '=') {
      getNextToken(); // eat '='
      Init = ParseExpression();
    } else {
      Init = make_unique<NumberExprAST>(0.0); // default to 0.0
    }
    VarNames.push_back({Name, std::move(Init)});

    if (CurTok != ',') break;
    getNextToken(); // eat ','
  }

  // ... eat ':', consumeNewlines(), parse body ...
  return make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}
```

Two small design choices are worth noting:

- initializers are optional; `var x:` means `x` starts at `0.0`
- the body is still a single expression, so `var` fits the current expression-only language

`ParseExpression` simply gives `var` first refusal:

```cpp
if (CurTok == tok_var)
  return ParseVarExpr();
```

## Parsing Assignment

Assignment is parsed after the usual binary expression has been built:

```cpp
/// expression
///   = varexpr | unaryexpr binoprhs [ "=" expression ] ;
static unique_ptr<ExprAST> ParseExpression() {
  if (CurTok == tok_var)
    return ParseVarExpr();

  auto LHS = ParseUnary();
  if (!LHS)
    return nullptr;

  auto Expr = ParseBinOpRHS(0, std::move(LHS));
  if (!Expr)
    return nullptr;

  if (CurTok != '=')
    return Expr;

  const string *AssignedName = Expr->getVariableName();
  if (!AssignedName)
    return LogError("Destination of '=' must be a variable");

  string Name = *AssignedName;
  getNextToken(); // eat '='

  auto RHS = ParseExpression();
  if (!RHS)
    return nullptr;

  return make_unique<AssignmentExprAST>(Name, std::move(RHS));
}
```

This makes assignment:

- lower precedence than all binary operators
- right-associative, because the RHS is parsed with `ParseExpression()`

The parser also enforces a useful rule: the left-hand side must be a plain variable name, not an arbitrary expression. So this is valid:

```python
x = x + 1
```

but this is not:

```python
(1 + 2) = 3
```

## Stack Slots: From Values to Storage

Until chapter 9, `NamedValues` mapped variable names directly to LLVM values. That worked only because variables were immutable: a parameter name could always refer to the same SSA value forever.

Mutable variables break that model. Once `x` can be reassigned, the name `x` can no longer mean "this one fixed SSA value". It has to mean "the place where the current value of `x` lives".

So `NamedValues` changes from:

```cpp
static std::map<std::string, Value *> NamedValues;
```

to:

```cpp
static std::map<std::string, AllocaInst *> NamedValues;
```

That is the core implementation change in this chapter.

Each variable name now maps to an `alloca`: a stack slot in the current function's entry block.

This helper creates those stack slots:

```cpp
/// CreateEntryBlockAlloca - Create a stack slot in the current function's
/// entry block for a mutable variable.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
}
```

The temporary `IRBuilder` is important. We may be code-generating deep inside a branch or loop, but allocas for local variables belong in the function entry block. This helper inserts them there regardless of where the main builder is currently pointing.

## Loading and Storing Variables

Once names map to stack slots, variable access becomes load/store.

A variable reference now loads:

```cpp
Value *VariableExprAST::codegen() {
  AllocaInst *A = NamedValues[Name];
  if (!A)
    return LogErrorV("Unknown variable name");
  return Builder->CreateLoad(Type::getDoubleTy(*TheContext), A, Name.c_str());
}
```

An assignment evaluates the right-hand side, stores it, and returns the assigned value:

```cpp
Value *AssignmentExprAST::codegen() {
  Value *Val = Expr->codegen();
  if (!Val)
    return nullptr;

  AllocaInst *A = NamedValues[Name];
  if (!A)
    return LogErrorV("Unknown variable name");

  Builder->CreateStore(Val, A);
  return Val;
}
```

Returning the assigned value is what makes assignment fit naturally into an expression language.

## `VarExprAST::codegen`

`VarExprAST::codegen` does four jobs:

1. evaluate each initializer
2. allocate storage for each local
3. bind the variable names in `NamedValues`
4. restore any shadowed outer bindings after the body finishes

```cpp
Value *VarExprAST::codegen() {
  vector<pair<string, AllocaInst *>> OldBindings;
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.first;
    Value *InitVal = Var.second->codegen();

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);

    OldBindings.push_back({VarName, NamedValues[VarName]});
    NamedValues[VarName] = Alloca;
  }

  Value *BodyVal = Body->codegen();

  for (auto I = OldBindings.rbegin(), E = OldBindings.rend(); I != E; ++I) {
    if (I->second) NamedValues[I->first] = I->second;
    else           NamedValues.erase(I->first);
  }

  return BodyVal;
}
```

This gives `var` normal lexical shadowing behavior. If an outer variable already has the same name, the inner `var` temporarily replaces it, then the old binding is restored after the body.

## Parameters Become Mutable Too

Once `NamedValues` holds allocas, function parameters must use the same representation. So `FunctionAST::codegen` now creates an entry-block alloca for each argument and stores the incoming LLVM argument into it:

```cpp
NamedValues.clear();
for (auto &Arg : TheFunction->args()) {
  AllocaInst *Alloca =
      CreateEntryBlockAlloca(TheFunction, std::string(Arg.getName()));
  Builder->CreateStore(&Arg, Alloca);
  NamedValues[std::string(Arg.getName())] = Alloca;
}
```

This unifies the whole language:

- parameters live in stack slots
- `var` locals live in stack slots
- assignment stores into stack slots
- variable references load from stack slots

One model everywhere.

## for Loops Switch to the Same Model

The old `for` implementation rebound the loop variable name directly to an SSA value produced by a PHI node. That no longer fits a mutable-variable language.

So chapter 10 changes the loop variable to use a stack slot too:

```cpp
AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
Builder->CreateStore(StartVal, Alloca);
...
NamedValues[VarName] = Alloca;
...
Value *CurVar =
    Builder->CreateLoad(Type::getDoubleTy(*TheContext), Alloca, VarName);
Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
Builder->CreateStore(NextVar, Alloca);
```

That means loop variables, like every other mutable local, now have storage and can participate naturally in assignment-heavy code.

## What the IR Looks Like

A tiny update like:

```python
var x = 1: x = x + 2
```

often optimizes all the way down to a single constant result. So to see the mutable-variable machinery clearly, it helps to use a slightly larger example. Here is a sum written with an accumulator:

```python
@binary(1)
def ;(x, y): return y

def sum_to(n): return var acc = 0:
    (for i = 1, i < n + 1, 1: acc = acc + i) ; acc
```

With `-v`, Pyxc prints:

```llvm
define double @sum_to(double %n) {
entry:
  %i = alloca double, align 8
  %acc = alloca double, align 8
  %n1 = alloca double, align 8
  store double %n, ptr %n1, align 8
  store double 0.000000e+00, ptr %acc, align 8
  br label %loop_cond

loop_cond:                                        ; preds = %loop_body, %entry
  %acc4 = phi double [ 0.000000e+00, %entry ], [ %addtmp6, %loop_body ]
  %i5 = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop_body ]
  store double %i5, ptr %i, align 8
  %addtmp = fadd double %n, 1.000000e+00
  %cmptmp = fcmp olt double %i5, %addtmp
  br i1 %cmptmp, label %loop_body, label %after_loop

loop_body:                                        ; preds = %loop_cond
  %addtmp6 = fadd double %acc4, %i5
  store double %addtmp6, ptr %acc, align 8
  %nextvar = fadd double %i5, 1.000000e+00
  br label %loop_cond

after_loop:                                       ; preds = %loop_cond
  %binop = call double @"binary;"(double 0.000000e+00, double %acc4)
  ret double %binop
}
```

Three things to notice:

- `%n1`, `%acc`, and `%i` are stack slots created by `alloca`
- the current loop-carried values still appear in SSA form as `%acc4` and `%i5`
- stores write those values back into the stack slots so ordinary variable lookups can load them later

So the chapter 10 model is not "SSA or mutable variables". It is both: stack storage at the source-language level, SSA values inside the optimized IR.

One thing you will notice: the IR still contains explicit `alloca`, `load`, and `store` instructions. LLVM's `mem2reg` pass would promote most of these to pure SSA form, eliminating the stack traffic entirely. Chapter 10 does not run that pass — the raw IR is left visible so the storage model is easy to follow.

## Build and Run

```bash
cd code/chapter-10
./build.sh
./build/pyxc
```

## Try It

Simple local update:

<!-- code-merge:start -->
```python
ready> var x = 1: x = x + 2
```
```bash
Parsed a top-level expression.
Evaluated to 3.000000
```
<!-- code-merge:end -->

Multiple bindings:

<!-- code-merge:start -->
```python
ready> var x = 1, y = x + 2: y
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

Chapter 11 replaces the single-expression function body with real statement blocks. That makes mutable variables much more natural to use: assignment can stand on its own line, `return` can appear anywhere in a function body, and examples stop needing expression-level workarounds.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

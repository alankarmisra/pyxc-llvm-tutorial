---
description: "Connect the AST to LLVM IR: add codegen() to every node and see real machine-level instructions for the first time."
---
# 7. pyxc: Code Generation

## What I Am Building

In [Chapter 5](chapter-05.md), I wrote a parser that builds a syntax tree and reports errors. I now want to turn that tree into LLVM intermediate representation (IR). In a later chapter, I will ask LLVM to compile and run this IR. For now, I focus on generating and printing it.

If I enter a function definition:

```pyxc
ready> def sum(a, b): a + b
```

I print the function as LLVM IR:

<!-- code-merge:start -->
```text
Parsed a function definition.
```
```llvm
define double @sum(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```
<!-- code-merge:end -->

LLVM can later compile this IR for x86, ARM, or another supported target. I could write IR by hand, but I will generate it from the syntax tree with LLVM's C++ API.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-07
```

## The Three LLVM Objects

I add the LLVM headers these codegen types come from, and pull in the `llvm` namespace alongside `std`:

```cpp
#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;
```

I use three LLVM objects during code generation and keep them as globals:

```cpp
static unique_ptr<LLVMContext> TheContext;
static unique_ptr<Module> TheModule;
static unique_ptr<IRBuilder<>> TheBuilder;
```

LLVM assigns a specific role to each object. To generate IR, I use them as LLVM expects:

- I keep shared LLVM state, such as types and constants, in `LLVMContext`.
- I store the functions and global variables I generate in `Module`.
- I use `IRBuilder` to create LLVM instructions inside those functions.

```diagram
                 ┌───────────────────────┐
                 │      LLVMContext      │
                 │-----------------------│
                 │ Shared LLVM state     │
                 │ Global Constants, etc.│
                 └──────────┬────────────┘
                            │
          ┌─────────────────┴─────────────────┐
          │                                   │
          ▼                                   ▼
 ┌───────────────────┐               ┌───────────────────┐
 │      Module       │               │      Module       │
 │-------------------│               │-------------------│
 │ Source file IR    │               │ Source file IR    │
 │ Functions         │               │ Functions         │
 │ Globals           │               │ Globals           │
 └─────────▲─────────┘               └─────────▲─────────┘
           │                                   │
           │      emits instructions into      │
           │                                   │
           └──────────────┬────────────────────┘
                          │
                ┌─────────┴─────────┐
                │     IRBuilder     │
                │-------------------│
                │ Generates LLVM IR │
                └───────────────────┘
```

I create all three objects in one function:

```cpp
static void InitializeModuleAndManagers() {
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  TheBuilder = std::make_unique<IRBuilder<>>(*TheContext);
}
```

`"PyxcJIT"` is the module identifier. I can choose any name here. When I add file mode later, I will use the source filename instead.

I call the function `InitializeModuleAndManagers` because I will also create optimization managers in [Chapter 8](chapter-08.md).

## Adding Code Generation to the AST

In Chapters [2](chapter-02.md) and [3](chapter-03.md), I created the syntax tree without generating LLVM IR. I now add a pure virtual `codegen()` method to the base expression class:

```cpp
class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;
  virtual Value *codegen() = 0;
};
```

I implement this method in every derived expression class. Each implementation returns an LLVM `Value*`. LLVM uses `Value` as the base class for values such as constants, instructions, and function arguments.

A function signature and a function definition are not expressions, so they do not derive from `ExpressionNode`. I give them `codegen()` methods that return an LLVM `Function*` instead:

```cppdiff
*class FunctionSignatureNode {
*  string Name;
*  vector<string> Parameters;
*
*public:
*  FunctionSignatureNode(const string &Name, vector<string> Parameters)
*      : Name(Name), Parameters(std::move(Parameters)) {}
*
+  const string &getName() const { return Name; }
+  Function *codegen();
*};
*
*class FunctionDefinitionNode {
*  unique_ptr<FunctionSignatureNode> Signature;
*  unique_ptr<ExpressionNode> Body;
*
*public:
-  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature, unique_ptr<ExpressionNode> Body)
+  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature,
+                         unique_ptr<ExpressionNode> Body)
*      : Signature(std::move(Signature)), Body(std::move(Body)) {}
+  Function *codegen();
*};
```

## Generating Expressions

### Number Literals

For a number literal, I create an LLVM floating-point constant:

```cpp
Value *NumberExpressionNode::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Value));
}
```

`APFloat` holds the floating-point value in LLVM's format. I pass it to `ConstantFP::get` to create a constant in `TheContext`. I do not need to emit an instruction because LLVM constants can be used directly as instruction operands.

### Name References

Code generation needs an error helper with the same return type as an expression's `codegen()` method. I add `LogErrorV`, which reports the error and returns `nullptr`:

```cpp
Value *LogErrorV(const char *Str) {
  LogErrorExpression(Str);
  return nullptr;
}
```

This lets me report an error directly from a `Value*` function:

```cpp
if (SomeErrorCondition)
  return LogErrorV("Error specifics");
```

For a name, I need to find the LLVM value that the name represents. I keep those values in `NamedValues`:

```cpp
static map<std::string, Value *> NamedValues;

Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end() || !It->second)
    return LogErrorV("Unknown variable name");
  return It->second;
}
```

For now, I only add function parameters to this map. When I add local variables later, I will store them here too.

### Unary Minus

Chapter 4 added `-` as a prefix operator to the grammar and the parser, but there was no codegen for it yet — every valid line just reported that it parsed. I close that gap now:

```cpp
Value *UnaryExpressionNode::codegen() {
  Value *OperandValue = Operand->codegen();
  if (!OperandValue)
    return nullptr;

  if (Operator == tok_minus)
    return TheBuilder->CreateFNeg(OperandValue, "negtmp");
  return LogErrorV("invalid unary operator");
}
```

I generate the operand first, then negate it with `CreateFNeg`. For a variable operand, that produces an `fneg` instruction:

```llvm
%negtmp = fneg double %x
```

For a constant operand, `IRBuilder` folds the negation into the constant itself instead of emitting an instruction, the same constant folding I see with binary operators below. `-5` becomes the constant `-5.0` directly, with no `fneg` in the IR at all.

### Binary Expressions

For a binary expression, I first generate the left and right values. I then use the operator to choose an LLVM instruction:

```cpp
Value *BinaryExpressionNode::codegen() {
  Value *L = Left->codegen();
  if (!L)
    return nullptr;

  Value *R = Right->codegen();
  if (!R)
    return nullptr;

  switch (Operator) {
  case tok_plus:
    return TheBuilder->CreateFAdd(L, R, "addtmp");
  case tok_minus:
    return TheBuilder->CreateFSub(L, R, "subtmp");
  case tok_star:
    return TheBuilder->CreateFMul(L, R, "multmp");
  case tok_slash:
    return TheBuilder->CreateFDiv(L, R, "divtmp");
  case tok_percent:
    return TheBuilder->CreateFRem(L, R, "remtmp");
  case tok_less:
    L = TheBuilder->CreateFCmpULT(L, R, "cmptmp");
    return TheBuilder->CreateUIToFP(
        L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}
```

If either operand fails, I return `nullptr` and stop generating this expression. Its parent will do the same, so an error can travel back through the tree.

For `+`, I call `CreateFAdd`:

```cpp
case tok_plus:
  return TheBuilder->CreateFAdd(L, R, "addtmp");
```

That call can generate an instruction like this:

```llvm
%addtmp = fadd double %x, %y
```

`fadd` is LLVM's floating-point addition instruction. `double` is the type of both operands. `%x` and `%y` are the operands, and `%addtmp` names the result.

I pass `"addtmp"` as a name hint. If I use the same hint again in the same function, LLVM adds a number to keep the names unique:

```llvm
%addtmp = fadd double %x, %y
%addtmp1 = fadd double %a, %b
```

I add subtraction, multiplication, and division in the same way:

```cpp
case tok_minus:
  return TheBuilder->CreateFSub(L, R, "subtmp");
case tok_star:
  return TheBuilder->CreateFMul(L, R, "multmp");
case tok_slash:
  return TheBuilder->CreateFDiv(L, R, "divtmp");
```

These calls generate `fsub`, `fmul`, and `fdiv` instructions:

```llvm
%subtmp = fsub double %x, %y
%multmp = fmul double %x, %y
%divtmp = fdiv double %x, %y
```

`%` gets the same treatment, joining the `switch` as a sibling of `*` and `/`:

```cpp
case tok_percent:
  return TheBuilder->CreateFRem(L, R, "remtmp");
```

```llvm
%remtmp = frem double %x, %y
```

I need two instructions for `<`:

```cpp
case tok_less:
  L = TheBuilder->CreateFCmpULT(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(
      L, Type::getDoubleTy(*TheContext), "booltmp");
```

The first instruction compares the two doubles and produces an `i1`, LLVM's one-bit boolean type. `ult` means unordered or less than: the result is true if either operand is a NaN or the left operand is less than the right operand.

```llvm
%cmptmp = fcmp ult double %x, %y
```

pyxc currently represents every value as a `double`, so I cannot return the `i1` directly. I use `uitofp` to convert false to `0.0` and true to `1.0`:

```llvm
%booltmp = uitofp i1 %cmptmp to double
```

The `default` case is a safety net, not a reachable path: the parser only ever builds a `BinaryExpressionNode` from an operator this `switch` already handles, so `LogErrorV("invalid binary operator")` never actually fires right now.

### Function Calls

For a function call, I perform four actions:

1. I find the function in the module.
2. I check the number of arguments.
3. I generate a value for each argument.
4. I emit the call.

The syntax tree node stores the function name and its arguments:

```cppdiff
*class CallExpressionNode : public ExpressionNode {
*  string Callee;
*  vector<unique_ptr<ExpressionNode>> Arguments;
*
*public:
-  CallExpressionNode(const string &Callee, vector<unique_ptr<ExpressionNode>> Arguments)
+  CallExpressionNode(const string &Callee,
+                     vector<unique_ptr<ExpressionNode>> Arguments)
*      : Callee(Callee), Arguments(std::move(Arguments)) {}
+  Value *codegen() override;
*};
```

I use those fields in `codegen()`:

```cpp
Value *CallExpressionNode::codegen() {
  Function *CalleeF = TheModule->getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Arguments.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Arguments.size(); i != e; ++i) {
    ArgsV.push_back(Arguments[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return TheBuilder->CreateCall(CalleeF, ArgsV, "calltmp");
}
```

`TheModule->getFunction` finds a function that I already defined in this session. I reject an unknown function or the wrong number of arguments before emitting a call. I then generate each argument from left to right and store the resulting `Value*` objects in `ArgsV`.

After I define `sum`, the call `sum(10, 20)` produces:

```llvm
%calltmp = call double @sum(double 1.000000e+01, double 2.000000e+01)
```

## Generating Functions

### Function Signatures

For a function signature, I add an LLVM function declaration to the module. I need the function name, its return type, and its parameter types and names.

```cpp
Function *FunctionSignatureNode::codegen() {
  // I use double for every parameter and for the return value.
  std::vector<Type *> Doubles(
      Parameters.size(), Type::getDoubleTy(*TheContext));

  FunctionType *FT = FunctionType::get(
      Type::getDoubleTy(*TheContext), Doubles, false /* not variadic */);

  Function *F = Function::Create(
      FT, Function::ExternalLinkage, Name, TheModule.get());

  // I name the arguments so the printed IR is easier to read.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Parameters[Idx++]);

  return F;
}
```

I use `double` for every parameter and for the return value. I pass those types to `FunctionType::get`, along with `false` because pyxc functions are not variadic.

I then call `Function::Create` to add the declaration to `TheModule`. I use `ExternalLinkage` so the function can be found outside this module. I will rely on that linkage when I add the JIT in [Chapter 8](chapter-08.md).

I also copy each parameter name into the LLVM arguments. This step only makes the printed IR easier to read:

```llvm
define double @foo(double %x, double %y) {
```

Without those names, LLVM would print numbered values instead:

```llvm
define double @foo(double %0, double %1) {
```

The names do not change the function's behavior.

### Function Definitions

A function definition contains a signature and a body:

```cppdiff
*class FunctionDefinitionNode {
*  unique_ptr<FunctionSignatureNode> Signature;
*  unique_ptr<ExpressionNode> Body;
*
*public:
-  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature, unique_ptr<ExpressionNode> Body)
+  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature,
+                         unique_ptr<ExpressionNode> Body)
*      : Signature(std::move(Signature)), Body(std::move(Body)) {}
+  Function *codegen();
*};
```

I generate the complete function in four steps:

```cpp
Function *FunctionDefinitionNode::codegen() {
  // Step 1: I get an existing declaration or create a new one.
  Function *TheFunction = TheModule->getFunction(Signature->getName());

  if (TheFunction && !TheFunction->empty()) {
    LogErrorExpression("Function cannot be redefined.");
    return nullptr;
  }

  if (!TheFunction)
    TheFunction = Signature->codegen();

  if (!TheFunction)
    return nullptr;

  // Step 2: I create the entry block and insert new instructions there.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  TheBuilder->SetInsertPoint(BB);

  // Step 3: I make the parameters available to the body.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  // Step 4: I generate the body, return its value, and verify the function.
  if (Value *RetVal = Body->codegen()) {
    TheBuilder->CreateRet(RetVal);
    verifyFunction(*TheFunction);
    return TheFunction;
  }

  // Reaching here means Body->codegen() failed. I remove the incomplete
  // function so no broken declaration is left in the module.
  TheFunction->eraseFromParent();
  return nullptr;
}
```

First, I look for the function name in the module. If I find a function that already has a body, I report a redefinition. If I do not find a declaration, I generate one from the signature.

Next, I create the function's `entry` basic block. A basic block is a straight-line sequence of instructions with one entry and one exit. `SetInsertPoint` tells `TheBuilder` where I want to add the next instruction.

```llvm
define double @foo(double %x, double %y) {
entry:
  ; I insert the next instruction here.
}
```

I then clear `NamedValues` and add the new function's parameters. This prevents names from the previous function from leaking into the new one. When I generate the body, each `NameExpressionNode` can now find its parameter.

Finally, I generate the body and pass its result to `CreateRet`. Although pyxc source does not use a `return` keyword yet, every LLVM function needs a terminating instruction. I create an LLVM `ret` instruction because the body expression is the function's result.

```llvm
define double @foo(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  ret double %addtmp
}
```

I call `verifyFunction` to ask LLVM to check the structure of the function. If body generation fails, I remove the incomplete function from the module.

## Printing IR as I Type

After code generation succeeds, I print the IR for the current input:

```cpp
static void HandleFunctionDefinition() {
  auto FunctionDefinition = ParseFunctionDefinition();
  if (!FunctionDefinition ||
      (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (FunctionDefinition)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FunctionIR = FunctionDefinition->codegen()) {
    fprintf(stderr, "Parsed a function definition.\n");
    FunctionIR->print(errs());
    fprintf(stderr, "\n");
  }
}
```

```cpp
static void HandleTopLevelExpression() {
  auto FunctionDefinition = ParseTopLevelExpression();
  if (!FunctionDefinition ||
      (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (FunctionDefinition)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FunctionIR = FunctionDefinition->codegen()) {
    fprintf(stderr, "Parsed a top-level expression.\n");
    FunctionIR->print(errs());
    fprintf(stderr, "\n");

    // Erase after printing — anonymous expressions don't belong in the final
    // module dump.
    FunctionIR->eraseFromParent();
  }
}
```

`errs()` is LLVM's wrapper around `stderr`. I pass it to `FunctionIR->print` to print the function in LLVM's text format. The extra `fprintf(stderr, "\n")` afterward is just spacing, so the next `ready>` prompt doesn't run up against the last line of IR.

For a top-level expression such as `1 + 2`, I create a temporary function named `__anon_expr`. LLVM instructions must belong to a function, so this wrapper gives me a place to generate the expression. After I print its IR, I remove it from the module. In the next chapter, I will execute it before removing it.

## Printing the Module at Session End

At the end of the session, I print the complete module:

```cppdiff
*int main() {
*  // I print the first prompt and load the first token before entering the loop.
*  // I load CurrentToken before I call any parse function.
*  fprintf(stderr, "ready> ");
*  getNextToken();
*
+  // Make the module, which holds all the code.
+  InitializeModuleAndManagers();
*
*  MainLoop();
*
+  // Print out all of the generated code.
+  TheModule->print(errs(), nullptr);
*
*  return 0;
*}
```

This shows every function that remains in the module. It does not show the temporary `__anon_expr` functions because I already removed them.

## Build and Run

```bash
cd code/chapter-07
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

I can enter a bare expression:

<!-- code-merge:start -->
```pyxc
ready> 4 + 5
```
```llvm
Parsed a top-level expression.
define double @__anon_expr() {
entry:
  ret double 9.000000e+00
}
```
<!-- code-merge:end -->

`IRBuilder` sees that both operands are constants, so it calculates `4 + 5` while constructing the IR. It returns the constant `9.0` instead of emitting an `fadd` instruction. This is **constant folding**.

I can also define and call a function:

<!-- code-merge:start -->
```pyxc
ready> def sum(a, b): a + b
```
```text
Parsed a function definition.
```
```llvm
define double @sum(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```
```pyxc
ready> sum(10, 20)
```
```text
Parsed a top-level expression.
```
```llvm
define double @__anon_expr() {
entry:
  %calltmp = call double @sum(double 1.000000e+01, double 2.000000e+01)
  ret double %calltmp
}
```
<!-- code-merge:end -->

Unlike `4 + 5`, a variable operand can't be folded to a constant, so I see the actual `fneg` and `frem` instructions:

<!-- code-merge:start -->
```pyxc
ready> def neg(x): -x
```
```text
Parsed a function definition.
```
```llvm
define double @neg(double %x) {
entry:
  %negtmp = fneg double %x
  ret double %negtmp
}
```
```pyxc
ready> def rem(x, y): x % y
```
```text
Parsed a function definition.
```
```llvm
define double @rem(double %x, double %y) {
entry:
  %remtmp = frem double %x, %y
  ret double %remtmp
}
```
<!-- code-merge:end -->

When I press `Ctrl-D`, I print the full module:

<!-- code-merge:start -->
```text
ready> ^D
```
```llvm
; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"

define double @sum(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}

define double @neg(double %x) {
entry:
  %negtmp = fneg double %x
  ret double %negtmp
}

define double @rem(double %x, double %y) {
entry:
  %remtmp = frem double %x, %y
  ret double %remtmp
}
```
<!-- code-merge:end -->

`sum`, `neg`, and `rem` all remain. I removed each temporary `__anon_expr` after printing it.

## What's Next

[Chapter 8](chapter-08.md) adds JIT execution, optimization passes, and `extern` for calling real C functions.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

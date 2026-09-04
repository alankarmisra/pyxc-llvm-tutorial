---
section: "LLVM and Execution"
description: "Give AST nodes codegen methods and emit the first pyxc LLVM IR."
---

# 7. pyxc: Code Generation

Next: translate the AST into LLVM IR.

The frontend already performs:

```text
source -> tokens -> AST
```

Add one more boundary:

```text
AST node -> LLVM Value or Function
```

This chapter prints IR but does not execute it. Keeping emission and execution separate makes the first backend easier to inspect.

Work in:

```bash
cd code/chapter-07
```

## 1. Link the LLVM Components

After `find_package(LLVM REQUIRED CONFIG)`, map the components pyxc needs:

```cmake
llvm_map_components_to_libnames(LLVM_LIBS
  Core
  Support
)

target_link_libraries(pyxc PRIVATE ${LLVM_LIBS})
```

Add the LLVM headers used by code generation:

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
#include "llvm/Support/raw_ostream.h"
```

## 2. Create the Three LLVM Objects

Add these code-generation globals:

```cpp
static unique_ptr<LLVMContext> TheContext;
static unique_ptr<Module> TheModule;
static unique_ptr<IRBuilder<>> TheBuilder;
static map<string, Value *> NamedValues;
```

They have distinct jobs:

```text
LLVMContext -> owns LLVM's shared type and constant data
Module      -> owns functions and global IR
IRBuilder   -> inserts instructions at the current position
NamedValues -> maps current parameter names to LLVM values
```

Initialize them once:

```cpp
static void InitializeModuleAndManagers() {
  TheContext = make_unique<LLVMContext>();
  TheModule = make_unique<Module>("PyxcJIT", *TheContext);
  TheBuilder = make_unique<IRBuilder<>>(*TheContext);
}
```

Call this from `main()` before parsing any input.

## 3. Add the Codegen Contract to the AST

Change the expression base class to:

```cpp
class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;
  virtual Value *codegen() = 0;
};
```

Add `codegen() override` to every expression node. Function nodes return `Function *`:

```cpp
Function *codegen();
```

Use `nullptr` as the failure result. Add:

```cpp
Value *LogErrorValue(const string &Message) {
  LogErrorExpression(Message);
  return nullptr;
}
```

## 4. Generate Number Constants

Implement:

```cpp
Value *NumberExpressionNode::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Value));
}
```

All values are still `double`, so every numeric literal becomes an LLVM floating-point constant.

No instruction is emitted for a constant by itself. It becomes an operand of whatever uses it.

## 5. Resolve Name References

Implement:

```cpp
Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end() || !It->second)
    return LogErrorValue("Unknown variable name: '" + Name + "'");

  return It->second;
}
```

At this stage, `NamedValues` contains function parameters only. Mutable locals arrive in Chapter 11.

## 6. Generate Unary Minus

Implement:

```cpp
Value *UnaryExpressionNode::codegen() {
  Value *OperandValue = Operand->codegen();
  if (!OperandValue)
    return nullptr;

  if (Operator != tok_minus)
    return LogErrorValue("Invalid unary operator");

  return TheBuilder->CreateFNeg(OperandValue, "negtmp");
}
```

`CreateFNeg` appends a floating-point negation at the builder's current insertion point.

## 7. Generate Binary Arithmetic

Generate both children first:

```cpp
Value *L = Left->codegen();
if (!L)
  return nullptr;

Value *R = Right->codegen();
if (!R)
  return nullptr;
```

Then select an instruction:

```cpp
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
```

The hint names make printed IR readable. LLVM may append numbers when a hint repeats.

## 8. Generate Less-Than as a pyxc Number

LLVM comparisons return `i1`, but pyxc currently represents every value as `double`.

Add:

```cpp
case tok_less:
  L = TheBuilder->CreateFCmpOLT(L, R, "cmptmp");
  return TheBuilder->CreateUIToFP(
      L, Type::getDoubleTy(*TheContext), "booltmp");
```

The conversion boundary is:

```text
fcmp olt -> i1 -> uitofp -> double 0.0 or 1.0
```

Finish the switch with an invalid-operator error.

## 9. Generate Calls

Look up the callee in the current module, validate its arity, generate every argument, and emit the call:

```cpp
Value *CallExpressionNode::codegen() {
  Function *CalleeF = TheModule->getFunction(Callee);
  if (!CalleeF)
    return LogErrorValue("Unknown function: '" + Callee + "'");

  if (CalleeF->arg_size() != Arguments.size())
    return LogErrorValue("Incorrect number of arguments in call to '" +
                         Callee + "'");

  vector<Value *> ArgsV;
  for (auto &Argument : Arguments) {
    ArgsV.push_back(Argument->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return TheBuilder->CreateCall(CalleeF, ArgsV, "calltmp");
}
```

Calls can target only functions already declared in the module. Chapter 8 adds persistent cross-module signature lookup.

## 10. Generate Function Signatures

Every parameter and return value is a `double`:

```cpp
Function *FunctionSignatureNode::codegen() {
  vector<Type *> ParameterTypes(
      Parameters.size(), Type::getDoubleTy(*TheContext));

  FunctionType *FT = FunctionType::get(
      Type::getDoubleTy(*TheContext), ParameterTypes, false);

  Function *F = Function::Create(
      FT, Function::ExternalLinkage, Name, TheModule.get());

  unsigned Index = 0;
  for (auto &Argument : F->args())
    Argument.setName(Parameters[Index++]);

  return F;
}
```

A signature creates a declaration. It has a name and type but no basic blocks yet.

## 11. Generate Function Bodies

Implement the definition in four steps:

```cpp
Function *FunctionDefinitionNode::codegen() {
  const string FunctionName = Signature->getName();

  Function *TheFunction = TheModule->getFunction(FunctionName);
  if (TheFunction && !TheFunction->empty())
    return nullptr; // redefinition

  if (!TheFunction)
    TheFunction = Signature->codegen();
  if (!TheFunction)
    return nullptr;

  BasicBlock *BB =
      BasicBlock::Create(*TheContext, "entry", TheFunction);
  TheBuilder->SetInsertPoint(BB);

  NamedValues.clear();
  for (auto &Argument : TheFunction->args())
    NamedValues[string(Argument.getName())] = &Argument;

  if (Value *RetVal = Body->codegen()) {
    TheBuilder->CreateRet(RetVal);
    verifyFunction(*TheFunction);
    return TheFunction;
  }

  TheFunction->eraseFromParent();
  return nullptr;
}
```

The entry block gives the builder somewhere to insert instructions. `NamedValues` connects source parameter names to LLVM arguments. Verification checks the finished function's IR invariants.

Erase a partially generated function on failure so broken IR does not remain in the module.

## 12. Generate Anonymous Top-Level Functions

Top-level expressions need a function body too. Wrap each one in an internal zero-argument function such as `__anon_expr`, then call the same definition codegen path.

This keeps the backend boundary uniform:

```text
named definition      -> LLVM function
top-level expression  -> anonymous LLVM function
```

In the top-level handler, print successful IR:

```cpp
if (Function *FunctionIR = Definition->codegen()) {
  FunctionIR->print(errs());
  fprintf(stderr, "\n");
}
```

## 13. Build and Inspect IR

```bash
cmake -S . -B build \
  -DLLVM_DIR="$(llvm-config --cmakedir)"
cmake --build build
./build/pyxc
```

Enter:

```pyxc
ready> def add(a, b): a + b
```

Expected IR shape:

```llvm
define double @add(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```

Try:

```pyxc
ready> add(2, 3)
```

The anonymous function should contain a call to `@add`.

Run the suite:

```bash
llvm-lit -v test/
```

What you built is the first backend:

```text
AST expressions -> LLVM Value*
AST signatures  -> LLVM declarations
AST definitions -> verified LLVM functions
```

Next: [Chapter 8](chapter-08.md) hands modules to ORC JIT, executes anonymous functions, and adds optimization.

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

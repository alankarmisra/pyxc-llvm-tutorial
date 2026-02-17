---
description: "Generate LLVM IR from the AST: turn expressions, operators, and functions into executable code."
---
# 5. Pyxc: Code Generation to LLVM IR

## What We're Building

We have a lexer (Chapter 1) and a parser (Chapter 2). They turn source code into an AST—a tree structure representing the code's meaning.

But an AST doesn't run. It's just a data structure.

This chapter converts the AST into LLVM IR (Intermediate Representation)—the format LLVM uses to generate machine code.

Here's what happens:

**Pyxc code:**
```python
def add(a, b):
    return a + b
```

**LLVM IR:**
```llvm
define double @add(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```

Don't worry about the syntax yet. The key point: LLVM IR is a readable, platform-independent format that LLVM can optimize and compile to machine code for any supported architecture.

## Source Code

Grab the code: [code/chapter05](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter05)

Or clone the whole repo:
```bash
git clone https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter05
```

## Why IR?

If we only cared about one platform (say, x86-64 macOS), we could go straight from AST to machine code.

But real compilers need to support multiple platforms: macOS/Linux/Windows, x86/ARM, different calling conventions, different linkers. Writing separate backends for each would be painful.

IR solves this:

- **Frontend** - Parse language into AST
- **Middle** - Lower AST to platform-independent IR
- **Backend** - Turn IR into machine code for any supported platform

We write the frontend once. LLVM handles everything else.

## The Setup

Add a `codegen()` method to each AST class:

```cpp
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  Value *codegen() override;
};
```

`codegen()` means "generate IR for this node and return the resulting value."

**What's a `Value*`?** In LLVM, `Value*` represents a value in the IR. It might be:
- A constant like `3.14`
- A function parameter like `%a`
- The result of an instruction like `%addtmp`

We're not computing values—we're building IR that *describes* how to compute them.

Global state for code generation:

```cpp
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder;
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;
```

- **`TheContext`** - LLVM's environment object. Most LLVM APIs need it.
- **`Builder`** - Creates IR instructions. Has methods like `CreateFAdd()` and `CreateFMul()`.
- **`TheModule`** - Container for all our functions and their IR.
- **`NamedValues`** - Maps variable names to their IR values. For now, only function parameters go here.

## Generating Expressions

### Numbers

```cpp
Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}
```

Create a floating-point constant. `ConstantFP::get()` gives us a `Value*` representing that constant in the IR.

### Variables

```cpp
Value *VariableExprAST::codegen() {
  Value *V = NamedValues[Name];
  if (!V)
    return LogError<Value *>("Unknown variable name");
  return V;
}
```

Look up the variable name in `NamedValues`. Right now, only function parameters are in there.

### Binary Operators

```cpp
Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '/':
    return Builder->CreateFDiv(L, R, "divtmp");
  case '%':
    return Builder->CreateFRem(L, R, "remtmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case '>':
    L = Builder->CreateFCmpUGT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogError<Value *>("invalid binary operator");
  }
}
```

Generate IR for both operands, then create an instruction for the operation.

The `Builder` makes this easy: `CreateFAdd(L, R, "addtmp")` creates an add instruction. The string `"addtmp"` is just a hint for readability—LLVM will automatically append numbers if you reuse names (`addtmp`, `addtmp1`, `addtmp2`).

**Why the extra step for comparisons?**

Arithmetic operators (`+`, `-`, `*`, `/`, `%`) take doubles and return doubles. Simple.

Comparison operators (`<`, `>`) return a 1-bit integer (0 or 1), but Pyxc expects all expressions to return doubles. So we use `CreateUIToFP` to convert the boolean result to a floating-point 0.0 or 1.0.

### Function Calls

```cpp
Value *CallExprAST::codegen() {
  Function *CalleeF = TheModule->getFunction(Callee);
  if (!CalleeF)
    return LogError<Value *>("Unknown function referenced");

  if (CalleeF->arg_size() != Args.size())
    return LogError<Value *>("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}
```

Look up the function by name, check the argument count, generate IR for each argument, then create a call instruction.

This works for both our own functions and standard library functions like `sin` and `cos`.

## Generating Functions

Functions are more involved than expressions. We need to handle both prototypes (signatures) and bodies.

### Prototypes

```cpp
Function *PrototypeAST::codegen() {
  std::vector<Type*> Doubles(Args.size(),
                             Type::getDoubleTy(*TheContext));
  FunctionType *FT =
    FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
    Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}
```

Create a function signature: N doubles as parameters, one double as return type. Then create the `Function` object with external linkage (can be called from outside this module) and add it to `TheModule`.

Setting argument names isn't required, but it makes the IR easier to read.

This gives us a function declaration with no body—exactly what `extern` needs.

### Function Bodies

```cpp
Function *FunctionAST::codegen() {
  Function *TheFunction = TheModule->getFunction(Proto->getName());

  if (!TheFunction)
    TheFunction = Proto->codegen();

  if (!TheFunction)
    return nullptr;

  if (!TheFunction->empty())
    return (Function*)LogError<Value *>("Function cannot be redefined.");
```

Check if the function already exists (from an earlier `extern` declaration). If not, create it from the prototype. Verify it has no body yet.

```cpp
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;
```

Create a basic block called "entry" and tell the `Builder` to insert instructions there.

**What's a basic block?** A sequence of instructions with no branches. Since we don't have `if` or loops yet, our functions only need one block.

Populate `NamedValues` with the function's parameters so variable expressions can find them.

```cpp
  if (Value *RetVal = Body->codegen()) {
    Builder->CreateRet(RetVal);
    verifyFunction(*TheFunction);
    return TheFunction;
  }
```

Generate IR for the function body. If that succeeds, create a `ret` instruction to return the result, then call `verifyFunction()` to check that the IR is valid.

```cpp
  TheFunction->eraseFromParent();
  return nullptr;
}
```

If there's an error, delete the function with `eraseFromParent()`. This lets users redefine functions after making a typo.

**Known bug:** This code doesn't check that the function signature matches the prototype. If you declare `extern def foo(a)` and then define `def foo(b): return b`, the extern's signature takes precedence and you get "Unknown variable name" because the body expects `b` but the function has parameter `a`.

Try to fix this yourself—it's good practice!

## Using the Code Generator

In Chapter 4, we added `--emit-llvm` to the REPL. Now we can implement it.

When `--emit-llvm` is set, we call `codegen()` on each parsed construct and print the resulting IR:

```cpp
static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read function definition:\n");
      FnIR->print(errs());
      fprintf(stderr, "\n");
    }
  } else {
    SynchronizeToLineBoundary();
  }
}
```

Similar changes for `HandleExtern()` and `HandleTopLevelExpression()`.

## Compile and Run

```bash
cd code/chapter05
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build
./build/pyxc repl --emit-llvm
```

Or use the shortcut:
```bash
cd code/chapter05
./build.sh
./build/pyxc repl --emit-llvm
```

## Sample Session

Default mode (parser only):
```
$ ./build/pyxc repl
ready> 4+5
Parsed a top-level expr
ready> def foo(a,b): return a*a + 2*a*b + b*b
Parsed a function definition
ready> extern def cos(x)
Parsed an extern
ready> ^D
```

LLVM mode:
```
$ ./build/pyxc repl --emit-llvm
ready> def foo(a,b): return a*a + 2*a*b + b*b
define double @foo(double %a, double %b) {
entry:
  %multmp = fmul double %a, %a
  %multmp1 = fmul double 2.000000e+00, %a
  %multmp2 = fmul double %multmp1, %b
  %addtmp = fadd double %multmp, %multmp2
  %multmp3 = fmul double %b, %b
  %addtmp4 = fadd double %addtmp, %multmp3
  ret double %addtmp4
}

ready> extern def cos(x)
declare double @cos(double)

ready> ^D
```

Look at that generated IR! Each operation becomes an instruction, temporary values get names like `%multmp` and `%addtmp`, and the final result gets returned.

## What We Built

- **Code generation** - Convert AST to LLVM IR
- **Expression IR** - Numbers, variables, operators, calls
- **Function IR** - Prototypes and definitions
- **REPL modes** - Default (parser feedback) and `--emit-llvm` (show IR)

With about 100 lines of code, we can now generate valid LLVM IR from Pyxc source code.

## What's Next

We can generate IR, but we can't run it yet. The REPL just prints the IR and moves on.

In Chapter 7, we'll add a JIT (Just-In-Time compiler) that actually executes the generated IR, turning our toy language into something that can compute real results.

## Need Help?

Stuck? Questions? Errors?

- **Issues:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)
- **Contribute:** Pull requests welcome!

Include:
- Chapter number
- Your OS/platform
- Full error message
- What you tried

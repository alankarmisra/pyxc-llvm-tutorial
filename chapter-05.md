---
description: "Generate LLVM IR from the AST: lower literals, variables, binary expressions, calls, prototypes, and functions into verified IR using IRBuilder."
---
# 5. Pyxc: Code generation to LLVM IR

## Introduction

Welcome to Chapter 5 of the [Pyxc: My First Language Frontend with LLVM](chapter-00.md) tutorial. This chapter shows you how to transform the Abstract Syntax Tree built in [Chapter 2](chapter-02.md) into LLVM IR. You'll see how straightforward it is to generate IR—it's actually easier than building the lexer and parser.

## Source Code

To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter05](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter05).

## Why do we need IR at all?

If we were building a compiler for exactly one machine, one OS, and one calling convention forever, we could go straight from AST to machine code and stop there.

Real life is messier than that. We have macOS/Linux/Windows, x86/ARM, different linkers, different object formats, and different optimization needs. An intermediate representation gives us a stable middle layer:

- Frontend: parse language constructs into a semantic graph (our AST).
- Middle: lower AST into a target-independent form (LLVM IR).
- Backend: turn that IR into object code for many supported targets.

So IR is not “extra ceremony.” It is the layer that lets one frontend work across many platforms and optimization pipelines.

## What is Internal Representation (IR)?

A simple add function in `pyxc`
```python
def add(a, b):
    return a + b
```

converts to an equally simple LLVM IR like so:
```llvm
define double @add(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```

Don't worry about the syntax details yet — we'll break down what all these pieces mean in a later chapter. For now, just notice how clean and readable it is.

## Code Generation Setup
In order to generate LLVM IR, we want some simple setup to get started. First we define virtual code generation (codegen) methods in each AST class:

```cpp
/// ExprAST - Base class for all expression nodes.
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  Value *codegen() override;
};
...
```

The `codegen()` method means: "emit LLVM IR for this AST node and return the resulting LLVM `Value*`." `Value*` is the handle for whatever value was produced by the node — whether that's a constant, a function argument, or the result of an instruction.

```cpp
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder;
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;
```

These globals handle code generation:

- **`TheContext`**: LLVM's environment object. Many LLVM APIs need it, so we create one and pass it around.
- **`Builder`**: Creates LLVM instructions. Provides methods like `CreateFAdd()` and tracks where to insert them.
- **`TheModule`**: Container for all our functions and their IR.
- **`NamedValues`**: Maps variable names (like `"a"` or `"x"`) to their IR values (the `Value*` representing them). For now, only function parameters go in here.

With this setup, we can generate code for each AST node.

## Expression Code Generation
Generating LLVM code for expression nodes is straightforward. First we’ll do numeric literals:

```cpp
Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}
```

This creates a floating-point constant in LLVM IR. We call `ConstantFP::get()` with our `double` value, and LLVM gives us back a `Value*` representing that constant.

**Why `Value*` instead of `double`?** We're not computing values—we're building IR that *represents* a computation. A `Value*` is LLVM's way of saying "here's a reference to a value in the IR." It might be a constant like `3.14`, or a function parameter like `%a`, or the result of an instruction like `%addtmp`. The actual number isn't computed until the code runs; right now we're just building the structure that describes how to compute it.

```cpp
Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  Value *V = NamedValues[Name];
  if (!V)
    return LogError<Value *>("Unknown variable name");
  return V;
}
```

Variable references are straightforward: look up the name in `NamedValues` and return the IR value we stored there. Right now, only function parameters are in that map, so we're looking up things like `"a"` or `"b"` from `def foo(a, b)`.

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
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case '>':
    L = Builder->CreateFCmpUGT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogError<Value *>("invalid binary operator");
  }
}
```

Binary operators are straightforward: recursively generate IR for the left side, then the right side, then create an instruction for the operation. The switch statement picks the right LLVM instruction based on the operator.

The `Builder` makes this easy—just tell it which instruction to create (`CreateFAdd`), pass the operands (`L` and `R`), and optionally give it a name (`"addtmp"`). The name is just a hint for readability; if you create multiple values with the same name, LLVM automatically adds numeric suffixes (`addtmp`, `addtmp1`, `addtmp2`).

Since all values in Pyxc are doubles, the arithmetic operators (`+`, `-`, `*`, `/`, `%`) are simple—both operands are doubles, the result is a double.

Comparison operators (`<`, `>`) need an extra step. LLVM's comparison instructions return a 1-bit integer (0 or 1), but Pyxc wants a double (0.0 or 1.0) because all expressions use double right now. So we use `CreateUIToFP` to convert the integer result to a floating-point value.

```cpp
Value *CallExprAST::codegen() {
  // Look up the name in the global module table.
  Function *CalleeF = TheModule->getFunction(Callee);
  if (!CalleeF)
    return LogError<Value *>("Unknown function referenced");

  // If argument mismatch error.
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

Function calls: look up the function by name in `TheModule`, check that the argument count matches, generate IR for each argument, then create a call instruction. We can call standard library functions like `sin` and `cos` just as easily as our own functions.

That covers the four expression types: literals, variables, binary operators, and function calls.

## Function Code Generation

Generating IR for functions is a bit more involved than expressions, but still straightforward. We need to handle both prototypes (function signatures) and function bodies.

```cpp
Function *PrototypeAST::codegen() {
  // Make the function type:  double(double,double) etc.
  std::vector<Type*> Doubles(Args.size(),
                             Type::getDoubleTy(*TheContext));
  FunctionType *FT =
    FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
    Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());
```

This creates the function signature. First, we build a `FunctionType` describing what the function looks like: it takes N doubles as parameters and returns a double. Then we create the actual `Function` object with external linkage (meaning it can be called from outside this module) and add it to `TheModule`.

```cpp
// Set names for all arguments.
unsigned Idx = 0;
for (auto &Arg : F->args())
  Arg.setName(Args[Idx++]);

return F;
```

We set the name of each argument to match the names in the prototype. This isn't required, but it makes the IR easier to read.

At this point we have a function signature with no body—this is what `extern` declarations need. For actual function definitions, we need to generate the body.

```cpp
Function *FunctionAST::codegen() {
    // First, check for an existing function from a previous 'extern' declaration.
  Function *TheFunction = TheModule->getFunction(Proto->getName());

  if (!TheFunction)
    TheFunction = Proto->codegen();

  if (!TheFunction)
    return nullptr;

  if (!TheFunction->empty())
    return (Function*)LogError<Value *>("Function cannot be redefined.");
```

For function definitions, we first check if the function already exists (maybe from an earlier `extern` declaration). If not, we create it from the prototype. Either way, we verify that the function has no body yet before adding one.

```cpp
// Create a new basic block to start insertion into.
BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
Builder->SetInsertPoint(BB);

// Record the function arguments in the NamedValues map.
NamedValues.clear();
for (auto &Arg : TheFunction->args())
  NamedValues[std::string(Arg.getName())] = &Arg;
```

We create a basic block called "entry" and tell the `Builder` to insert instructions there. A basic block is a sequence of instructions with no branching—since we don't have `if` or loops yet, our functions only need one block.

Then we populate `NamedValues` with the function's parameters so that `VariableExprAST` can look them up.

```cpp
if (Value *RetVal = Body->codegen()) {
  // Finish off the function.
  Builder->CreateRet(RetVal);

  // Validate the generated code, checking for consistency.
  verifyFunction(*TheFunction);

  return TheFunction;
}
```

Now we generate IR for the function body by calling `Body->codegen()`. If that succeeds, we create a `ret` instruction to return the computed value, then call `verifyFunction()` to check that the IR we generated is valid.

```cpp
  // Error reading body, remove function.
  TheFunction->eraseFromParent();
  return nullptr;
}
```

If there's an error generating the body, we delete the function with `eraseFromParent()`. This lets users redefine functions after making a typo.

This code does have a bug, though: If the FunctionAST::codegen() method finds an existing IR Function, it does not validate its signature against the definition’s own prototype. This means that an earlier ‘extern’ declaration will take precedence over the function definition’s signature, which can cause codegen to fail, for instance if the function arguments are named differently. There are a number of ways to fix this bug, see what you can come up with! Here is a testcase:

```python
extern def foo(a) # ok, defines foo.
def foo(b): return b # Error: Unknown variable name. (decl using 'a' takes precedence).
```

## Driver Changes and Closing Thoughts
At this point, Chapter 5 has two useful REPL modes:

- default `repl`: parser-focused feedback (`Parsed a ...`)
- `repl --emit-llvm`: print generated IR as you define things

That split is helpful while learning, because sometimes you just want parser confirmation, and sometimes you want to inspect the generated IR in detail.

For example, default mode:

```text
$ ./build/pyxc repl
ready> 4+5
Parsed a top-level expr
ready> def foo(a,b): return a*a + 2*a*b + b*b
Parsed a function definition
ready> extern def cos(x)
Parsed an extern
```

And LLVM mode:

```text
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
```

In LLVM mode, pressing EOF (`Ctrl+D` on Linux/macOS, `Ctrl+Z` then Enter on Windows) prints the full module, which is useful when you want the complete picture of all definitions emitted in the session.

This also makes the chapter progression cleaner: Chapter 5 is where we can *see* IR clearly, and Chapter 7 is where we make that IR executable through JIT plus optimization passes.

## Compiling

```bash
cd code/chapter05 && \
    cmake -S . -B build && \
    cmake --build build
```

### macOS / Linux shortcut

```bash
cd code/chapter05 && ./build.sh
```

## Need Help?

Stuck on something? Have questions about this chapter? Found an error?

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report bugs, errors, or problems
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- The chapter you're working on
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message or unexpected behavior
- What you've already tried

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!

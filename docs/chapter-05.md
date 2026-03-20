---
description: "Connect the AST to LLVM IR: add codegen() to every node and see real machine-level instructions for the first time."
---
# 5. Pyxc: Code Generation

## Where We Are

We have a parser that builds an AST and a diagnostics layer that gives clear error messages. But the AST just sits in memory. Nothing runs.

By the end of this chapter, typing a function into the REPL produces LLVM IR — the intermediate representation that LLVM compiles to native machine code:

<!-- code-merge:start -->
```python
ready> def sum(a, b): return a + b
```
```llvm
Parsed a function definition.
define double @sum(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```
<!-- code-merge:end -->

That's real output. LLVM can take that IR and compile it to x86, ARM, or any other target it supports. In the next chapter, we'll run that same code using a Just In Time Compiler (JIT). 

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-05
```

## The Three LLVM Objects

Code generation in LLVM revolves around three objects. We keep them as globals:

```cpp
static unique_ptr<LLVMContext> TheContext;
static unique_ptr<Module>      TheModule;
static unique_ptr<IRBuilder<>> Builder;
```

**`LLVMContext`** is the root object of our compiled code. Types and constants live here and are shared across everything below. You pass it to almost every LLVM API call.

**`Module`** belongs to the context and represents one source file's worth of compiled output — the functions and global variables defined in it.

**`IRBuilder`** is what we use to emit instructions.

Initialization bundles the three objects:

```cpp
static void InitializeModule() {
  TheContext = make_unique<LLVMContext>();
  TheModule  = make_unique<Module>("Pyxc JIT", *TheContext);
  Builder    = make_unique<IRBuilder<>>(*TheContext);
}
```

`"Pyxc JIT"` is the module identifier — it appears in the `ModuleID` line of the printed IR. We use a placeholder here; in later chapters when we add file mode, this becomes the source filename.

## Adding codegen() to the AST

In chapters [2](chapter-02.md) and [3](chapter-03.md), the AST nodes had no methods beyond their constructors. Now we add a pure virtual `codegen()` to the base class:

```cpp
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};
```

`Value` is LLVM's base class for anything that produces a value — constants, instructions, function arguments. Every expression node returns a `Value*` from its `codegen()`. 

`PrototypeAST` and `FunctionAST` produce `Function*` instead of `Value*` — a function is not an expression, so they don't inherit from `ExprAST`:

```cpp
class PrototypeAST {
  ...
  Function *codegen();
};

class FunctionAST {
  ...
  Function *codegen();
};
```

## Generating Expressions

### Number Literals

A number literal becomes a floating-point constant:

```cpp
Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}
```

`APFloat` is LLVM's floating-point value type. `ConstantFP::get` creates a constant node and stores it in the context — which is why we pass `TheContext`. No instruction is emitted; constants are values that get folded into whichever instruction uses them.

### Variable References

A variable reference looks up the name in `NamedValues`:

```cpp
Value *VariableExprAST::codegen() {
  Value *V = NamedValues[Name];
  if (!V)
    return LogErrorV("Unknown variable name");
  return V;
}
```

For now `NamedValues` only contains function parameters. Referencing any other name is an error. Mutable local variables come in a later chapter.

`LogErrorV` is a fourth error helper that returns `Value*`:

```cpp
Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}
```

### Binary Expressions

`BinaryExprAST::codegen()` recurses into both sides first, then emits a single instruction for the operator:

```cpp
Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+': return Builder->CreateFAdd(L, R, "addtmp");
  case '-': return Builder->CreateFSub(L, R, "subtmp");
  case '*': return Builder->CreateFMul(L, R, "multmp");
  case '<': ...
  }
}
```

Each case and the IR it produces:

**`+`**
```cpp
case '+': return Builder->CreateFAdd(L, R, "addtmp");
```
```llvm
%addtmp = fadd double %x, %y
```

**`-`**
```cpp
case '-': return Builder->CreateFSub(L, R, "subtmp");
```
```llvm
%subtmp = fsub double %x, %y
```

**`*`**
```cpp
case '*': return Builder->CreateFMul(L, R, "multmp");
```
```llvm
%multmp = fmul double %x, %y
```

**`<`**
```cpp
case '<':
  L = Builder->CreateFCmpULT(L, R, "cmptmp");
  return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
```
```llvm
%cmptmp = fcmp ult double %x, %y
%booltmp = uitofp i1 %cmptmp to double
```

`<` needs two steps — `fcmp ult` produces a 1-bit integer (`i1`). Since Pyxc treats everything as `double`, we widen it with `uitofp`: `false` → `0.0`, `true` → `1.0`.

If either side fails codegen, we return `nullptr` immediately. The parent node does the same — a failure anywhere in the tree bubbles up and aborts the whole codegen.

The string arguments (`"addtmp"`, `"subtmp"`, etc.) are hint names — LLVM uses them when printing IR, appending a number if the name collides.

### Function Calls

A call looks up the callee in the module, checks the argument count, codegens each argument, and emits a call instruction:

```cpp
Value *CallExprAST::codegen() {
  Function *CalleeF = TheModule->getFunction(Callee); // Callee is the function name string from CallExprAST
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())  // codegen failed — bail out
      return nullptr;
  }

  return Builder->CreateCall(CalleeF,    /* function to call */
                             ArgsV,      /* arguments */
                             "calltmp"); /* hint name for the result */
}
```

`TheModule->getFunction` searches the module for a function with that name. If it finds one — whether from a previous `extern` or a previous `def` — we use it. The argument count check catches mismatches that the type system would catch in a typed language.

For example, `extern def sin(x)` followed by `sin(10)` produces:

```llvm
%calltmp = call double @sin(double 1.000000e+01)  ; calling sin(10)
```

## Generating Functions

### Prototypes

A prototype creates the function signature in the module — name, return type, parameter types and parameters names:

```cpp
Function *PrototypeAST::codegen() {
  // All parameters are double — build a vector of N double types
  vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));

  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), /* return type */
                        Doubles,                        /* parameter types */
                        false);                         /* not variadic */

  Function *F =
      Function::Create(FT,                        /* signature */
                       Function::ExternalLinkage,  /* visible outside module */
                       Name,                       /* function name */
                       TheModule.get());            /* module to add it to */

  // Name each argument — optional, but makes the printed IR readable
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}
```

Everything in Pyxc is a `double` for now — parameters and return value alike. `FunctionType::get` takes the return type, a list of parameter types, and a boolean for variadic functions (false here).

`ExternalLinkage` means the function is visible outside this module. That's what will allow `extern def sin(x)` to link against the C library's `sin` at runtime — once we add JIT execution in Chapter 6 — and what allows `def foo(...)` to be called from later expressions in the same session.

Setting argument names via `setName` is optional — it only affects the printed IR. But it makes the output readable:

```llvm
define double @foo(double %a, double %b) {
```

instead of:

```llvm
define double @foo(double %0, double %1) {
```

### Function Definitions

A function definition first checks whether the module already has a declaration for this name (from a previous `extern`), then creates the body:

```cpp
Function *FunctionAST::codegen() {
  // Step 1: reuse an existing extern declaration ...
  Function *TheFunction = TheModule->getFunction(Proto->getName());
  // ... or create a fresh one
  if (!TheFunction)
    TheFunction = Proto->codegen();
  // Bail if both options fail.
  if (!TheFunction)
    return nullptr;

  // Step 2: create the entry basic block and point the builder at it
  // A basic block is a straight-line sequence of instructions that ends
  // with a branch or return. Every function body has at least one.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Step 3: populate NamedValues so the body can resolve parameter names
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[string(Arg.getName())] = &Arg;

  // Step 4: codegen the body expression, wrap its result in a ret instruction,
  // verify the function — or erase it from the module if codegen failed
  if (Value *RetVal = Body->codegen()) {
    Builder->CreateRet(RetVal);
    verifyFunction(*TheFunction);
    return TheFunction;
  }

  TheFunction->eraseFromParent();
  return nullptr;
}
```

Four steps:

1. **Get or create the function declaration.** If `extern def foo(x)` was seen earlier, `getFunction` finds it. Otherwise `Proto->codegen()` creates a `Function*` object in the module — which at this point is just a signature with no body, equivalent to a `declare`:

   ```llvm
   declare double @foo(double %x, double %y)
   ```

   The remaining steps then add basic blocks to that same object, promoting it to a full definition:

   ```llvm
   define double @foo(double %x, double %y) {
   entry:
     %addtmp = fadd double %x, %y
     ret double %addtmp
   }
   ```

   It's the same `Function*` being completed in place — not two separate objects.

2. **Create the entry basic block.** A basic block is a straight-line sequence of instructions that ends with a branch or return. Every function starts with one. `SetInsertPoint` tells the builder to append new instructions here.

3. **Populate `NamedValues`.** Clear the table (parameters from the last function are irrelevant) and add each argument. Now when the body's `VariableExprAST` nodes look up parameter names, they find the `Value*` representing the incoming argument.

4. **Codegen the body.** If it succeeds, `CreateRet` wraps the resulting value in an LLVM `ret` instruction — that is how LLVM functions return a value. Then `verifyFunction` runs LLVM's internal consistency checks — it catches structural IR problems like type mismatches or a basic block with no instruction to end it (a branch or return). If codegen fails, the partially-built function is erased from the module so it doesn't leave a broken declaration behind.

For example, `def add(x, y): return x + y` produces:

```llvm
define double @add(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  ret double %addtmp
}
```

## Printing IR as You Type

Each `Handle*` function prints the IR for that input immediately after codegen succeeds:

```cpp
// HandleDefinition
if (auto *FnIR = FnAST->codegen()) {
  fprintf(stderr, "Parsed a function definition.\n");
  FnIR->print(errs());
}

// HandleExtern
if (auto *FnIR = ProtoAST->codegen()) {
  fprintf(stderr, "Parsed an extern.\n");
  FnIR->print(errs());
}

// HandleTopLevelExpression
if (auto *FnIR = FnAST->codegen()) {
  fprintf(stderr, "Parsed a top-level expression.\n");
  FnIR->print(errs());
  FnIR->eraseFromParent();
}
```

`errs()` is LLVM's wrapper around `stderr`. `FnIR->print(errs())` dumps the function's IR in human-readable form.

Anonymous top-level expressions call `eraseFromParent()` after printing. The expression was only needed to show the IR; it shouldn't accumulate in the module and shouldn't appear in the end-of-session dump.

This unconditional IR printing is intentional for chapter 5 — IR inspection is the whole point of the chapter. [Chapter 7](chapter-07.md) moves it behind a `-v` flag so running a source file doesn't flood the terminal.

## The Module at Session End

At the end of the session, `main()` prints the full module:

```cpp
TheModule->print(errs(), nullptr);
```

This dumps every function that was defined or declared, in one block. It's how you see the accumulated result of the whole session. Anonymous expressions don't appear here because `eraseFromParent()` already removed them.

## Build and Run

```bash
cd code/chapter-05
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

A bare expression — constant folding kicks in immediately:

<!-- code-merge:start -->
```python
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

Defining and calling a function:

<!-- code-merge:start -->
```python
ready> def sum(a, b): return a + b
```
```llvm
Parsed a function definition.
define double @sum(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```
```python
ready> sum(10, 20)
```
```llvm
Parsed a top-level expression.
define double @__anon_expr() {
entry:
  %calltmp = call double @sum(double 1.000000e+01, double 2.000000e+01)
  ret double %calltmp
}
```
<!-- code-merge:end -->

Declaring and calling an external function:

<!-- code-merge:start -->
```python
ready> extern def cos(x)
```
```llvm
Parsed an extern.
declare double @cos(double)
```
```python
ready> cos(1.234)
```
```llvm
Parsed a top-level expression.
define double @__anon_expr() {
entry:
  %calltmp = call double @cos(double 1.234000e+00)
  ret double %calltmp
}
```
<!-- code-merge:end -->

Press `^D` to end the session — the full module dumps:

<!-- code-merge:start -->
```bash
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
declare double @cos(double)
```
<!-- code-merge:end -->

A few things to notice:

- `4 + 5` folds to `9.0` at IR construction time — `IRBuilder` recognizes two constants and returns a single value rather than emitting a `fadd`. This is **constant folding** and happens by default.
- `extern def cos(x)` emits a `declare` — a signature with no body. At link time this resolves to the C library's `cos`.
- The end-of-session dump shows only `sum` and the `cos` declaration. The `__anon_expr` functions are absent because `HandleTopLevelExpression` calls `eraseFromParent()` after printing — they were only useful for display.

## What's Next

The IR is correct — but it just prints and does nothing. In [Chapter 6](chapter-06.md) we plug in an ORC JIT layer so that top-level expressions actually execute and print their results. We also add an optimization pass manager so the IR that runs is clean and fast. This is the chapter where the compiler comes alive.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

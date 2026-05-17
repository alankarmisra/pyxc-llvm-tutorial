---
description: "Connect the AST to LLVM IR: add codegen() to every node and see real machine-level instructions for the first time."
---
# 5. pyxc: Code Generation

## Where We Are

In [Chapter 3](chapter-03.md) we wrote a parser that builds a syntax tree and reports error messages, if any. The next step is to generate intermediate code (IR) and pass that on to the LLVM tooling, either to compile and run immediately, or to compile to machine code to be run later. This chapter focuses on generating the IR. If you type something like:

```pyxc
ready> def sum(a, b): return a + b
```

You should see...

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

That's the internal representation LLVM needs to be able to run code. It can take that IR and compile it to x86, ARM, or any other target it supports. If you're wondering, and even if you're not, yes it's possible to just write the IR out by hand in a text file and run it through LLVM. We won't be doing that here. Instead we will use the LLVM utility functions to help us translate the pyxc programming language code into something LLVM can run.

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

**`LLVMContext`** is at the root of our compiled code. Items shared across modules like global constants land up here. You pass the context to almost every LLVM API call.

**`Module`** belongs to the context and represents one source file's worth of compiled output — the functions and global variables defined in it. A single LLVMContext can contain multiple modules.

**`IRBuilder`** is what we use to emit LLVM instructions as you'll see soon.

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

Initialization bundles the three objects:

```cpp
static void InitializeModule() {
  TheContext = make_unique<LLVMContext>();
  // `PyxcJIT` is the module identifier — it can be anything you want. 
  // In later chapters when we add file mode, we use the source filename as the module id to keep things sensible.
  TheModule  = make_unique<Module>("PyxcJIT", *TheContext);
  Builder    = make_unique<IRBuilder<>>(*TheContext);
}
```

## Adding codegen() to the AST

In chapters [2](chapter-02.md) and [3](chapter-03.md), the AST nodes had no methods beyond their constructors. Now we add a pure virtual `codegen()` to the base class and then every derived class can decide on what it wants to emit:

```cpp
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};
```

`Value` is LLVM's base class for anything that produces a value — constants, instructions, function arguments. Every expression node returns a `Value*` from its `codegen()`. However, `PrototypeAST` and `FunctionAST` produce `Function*` instead of `Value*` — a function and its prototype are not expressions, so they don't inherit from `ExprAST`

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

`APFloat` is LLVM's floating-point value type. `ConstantFP::get` creates a constant and stores it in the context — which is why we pass `TheContext`. No instruction is emitted; constants are values that get copied into whichever instruction uses them. 

### Variable References

A variable reference looks up the name in `NamedValues` and returns the referenced value:

```cpp
static map<std::string, Value *> NamedValues;
...

Value *VariableExprAST::codegen() {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end() || !It->second)
    return LogErrorV("Unknown variable name");
  return It->second;
}
```

For now `NamedValues` only contains function parameters which are `Value*` objects unlike the functions themselves which are `Function*`. When we introduce local variables in a later chapter, we will stuff them into this map as well.

`LogErrorV` is a new error helper that returns nullptr cast as a `Value*`:

```cpp
Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}
```

so we can now do:

```cpp
if(some_error_condition) return LogErrorV("Error specifics");

// No error if we are here, continue processing.
...

```

### Binary Expressions

`BinaryExprAST::codegen()` will recurse into the left and right sides allowing them to generate their `Value*`s, and will then emit a single instruction for the operator combining the two:

```cpp
Value *BinaryExprAST::codegen() {
  // Generate the Value* from the LHS expression
  Value *L = LHS->codegen();
  if (!L)
    return nullptr;
  // Generate the Value* from the RHS expression
  Value *R = RHS->codegen();
  if (!R)
    return nullptr;

  switch (Op) { // Based on the operator, emit the appropriate LLVM instruction
  case '+': return Builder->CreateFAdd(L, R, "addtmp"); 
```

This generates the following LLVM instruction:

```llvm
%addtmp = fadd double %x, %y
```

`fadd` is the actual LLVM instruction. `double` specifies the operand types (LLVM is strongly typed). The result of `fadd double %x, %y`  is then called `%addtmp` - which is the variable name *prefix* we supplied. If we didn't supply a *prefix*, LLVM would just invent one. In this tutorial we supply our variable name prefix so the output is sensible to our human eyes. Why do I keep saying *prefix*? Because we could do something like this

```cpp
Builder->CreateFAdd(L, R, "addtmp"); 
....
// Elsewhere in the code in a different expression
Builder->CreateFAdd(L, R, "addtmp"); 
```

LLVM suffixes the second hinted variable name with a number to make it unique and so *prefix* makes sense. Notice how only the second one has a suffix. In most programs we will have multiple add statements, so we just call them prefix. Some literature calls these *hints* which is fine too. 

```llvm
%addtmp = fadd double %x, %y
...
%addtmp1 = fadd double %x, %y
```

Now let's do all the other operators within the same switch statement.


Each case and the IR it produces:

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

`<` needs two steps — `fcmp ult` does an unsigned less than (u-l-t) comparison and produces a 1-bit integer (`i1`). Since Pyxc treats everything as `double`, we widen it with unsigned-integer-to-floating-point (u-i-to-f-p) `uitofp`: `false` → `0.0`, `true` → `1.0`. This way expressions up the chain that are expecting a double get a double.

If either side fails codegen, we return `nullptr` immediately. The parent node does the same — a failure anywhere in the tree bubbles up and aborts the whole codegen.

### Function Calls

Here's what we want to do for a function call:
1. Find the function being called
2. Check the argument count
3. Codegen each argument
4. Call the function.

Remember `CallExprAST` properties look like so:
```cpp
class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;
...  
```

Let's *codegen* it.

```cpp
Value *CallExprAST::codegen() {
  // Let's see if we can find the function first. This would be a function 
  // that was previously declared with `extern` or defined with `def`.
  Function *CalleeF = TheModule->getFunction(Callee /* the function name is in the Callee property*/); 

  // Uh-oh
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  // Got the function. Let's do an argument count check for the call

  // Geez Luis. 
  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");
  
  // Checks out. Let's codegen the arguments first so the values are
  // available to the function
  vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    // Aikaramba
    if (!ArgsV.back())  // codegen failed — bail out
      return nullptr;
  }
  
  // Function exists, has the right parameters, and they evaluate to something 
  // meaningful, let's call it. 
  return Builder->CreateCall(CalleeF,    /* function to call */
                             ArgsV,      /* arguments */
                             "calltmp"); /* prefix for the result */
}
```

For example, `extern def sin(x)` followed by `sin(10)` produces:

```llvm
%calltmp = call double @sin(double 1.000000e+01)  
```

## Function Codegen

### Prototypes

A prototype creates the function signature in the module: 
- name
- return type
- parameter types and 
- parameters names.

We just need to repackage our existing node information into something LLVM can consume. 

```cpp
Function *PrototypeAST::codegen() {    
  // All parameters are double — build a vector of N double types
  vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  

  // Now ask LLVM to generate a function prototype stub
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), /* return type */
                        Doubles,                        /* parameter types */
                        false);                         /* not variadic */
  
  // And create a function prototype from that stub, and add it to the module.
  Function *F =
      Function::Create(FT,                         /* signature */
                       Function::ExternalLinkage,  /* visible outside module */
                       Name,                       /* function name */
                       TheModule.get());           /* module to add it to */

  // Name each argument — optional, but makes the printed IR readable
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}
```

Everything in Pyxc is a `double` for now — parameters and return value alike. 

`ExternalLinkage` means the function is visible outside this module. We do this for all functions, even functions we don't intend to call outside the module. The specifics of why regular functions need this will make more sense in [Chapter 6](chapter-06.md) when we set up the JIT. Right now, let's run with "*Just trust me bro*". And yes, there exists `InternalLinkage` just as you suspected. 

Setting argument names via `setName` is optional — it only affects the printed IR. But it makes the output readable:

```llvm
define double @foo(double %x, double %y) {
```

instead of what LLVM would generate if you didn't use setName:

```llvm
define double @foo(double %0, double %1) {
```

`%0` and `%1` are just names LLVM prints in the IR. The underlying objects are identical — `%x` and `%0` refer to the same thing. The code works either way; only the string output differs.

### Function Definitions

A function definition first checks whether the module already has a declaration for this name, then creates the body. First let's recap what `PrototypeAST` and `FunctionAST` nodes looks like:

```cpp
class PrototypeAST {
  string Name;
  vector<string> Args;
...
public:
  const string &getName() const { return Name; }
  Function *codegen();
```

```cpp
/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;
...
```

Now let's use these properties and methods to codegen.

```cpp
Function *FunctionAST::codegen() {
  // Step 1: reuse an existing `extern` declaration if one exists.
  Function *TheFunction = TheModule->getFunction(Proto->getName());

  // Bail if the function is already fully defined — redefinition is an error.
  if (TheFunction && !TheFunction->empty()) {
    LogError("Function cannot be redefined.");
    return nullptr;
  }

  // The function was neither declared nor defined — create a fresh declaration.
  if (!TheFunction)
    TheFunction = Proto->codegen();

  // Proto codegen failed.
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

  // Step 4: codegen the body expression, wrap its result in a `ret` instruction,
  // which is what LLVM needs to see to return a value from the function.
  // Verify the function — or erase it from the module if codegen failed.
  if (Value *RetVal = Body->codegen()) {
    Builder->CreateRet(RetVal);
    verifyFunction(*TheFunction);
    return TheFunction;
  }

  TheFunction->eraseFromParent();
  return nullptr;
}
```

Five steps:

1. **Get or create the function declaration.** If `extern def foo(x, y)` was seen earlier, `TheModule->getFunction` finds it. Otherwise `Proto->codegen()` creates a `Function*` object in the module — which at this point is just a signature with no body, equivalent to a `declare`:

   ```llvm
   declare double @foo(double %x, double %y)
   ```
2. **Reject redefinitions.** If the function already has a body — someone defined `def foo` twice — we error out here before touching anything. We check this before calling `Proto->codegen()` to avoid unnecessary work.

3. **Create the entry basic block.** A basic block is a straight-line sequence of instructions that ends with a branch or return. Every function starts with one. `SetInsertPoint` tells the builder to append new instructions here.

   ```llvm
   define double @foo(double %x, double %y) {
   entry:
    ; <-- insert point is here
   }
   ```



4. **Populate `NamedValues`.** Clear the table (parameters from the last function are irrelevant) and add each argument. Now when the body's `VariableExprAST` nodes look up parameter names, they find the `Value*` representing the incoming argument.

5. **Codegen the body.** If it succeeds, `CreateRet` wraps the resulting value in an LLVM `ret` instruction — that is how LLVM functions return a value. Then `verifyFunction` runs LLVM's internal consistency checks — it catches structural IR problems like type mismatches or a basic block with no instruction to end it (a branch or return). If codegen fails, the partially-built function is erased from the module so it doesn't leave a broken declaration behind.

   ```llvm
   define double @foo(double %x, double %y) {
   entry:
     ; body codegenned
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

`HandleTopLevelExpression` does a bit extra. Recall from [Chapter 3](chapter-03.md) that top-level expressions like `1 + 2` are wrapped in a synthetic anonymous function (`__anon_expr`) so the parser always has a `FunctionAST` to work with. That anonymous function is what gets codegenned here. It calls `eraseFromParent()` after printing because it was only needed to show the IR — it shouldn't accumulate in the module and shouldn't appear in the end-of-session dump. This doesn't mean we are ignoring it. In the next chapter, our JIT will run the top-level expression as you would expect it to, and then discard it before defining another anonymous function for the next top-level expression and so on.

## The Module at Session End

At the end of the session, `main()` prints the full module:

```cpp
TheModule->print(errs(), nullptr);
```

This dumps every function that was defined or declared, in one block. It's how you see the accumulated result of the whole session. As mentioned earlier, anonymous expressions don't appear here because `eraseFromParent()` already removed them.

## Build and Run

```bash
cd code/chapter-05
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

A **bare expression**:

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

Note how `4 + 5` folds to `9.0` at IR construction time — `IRBuilder` recognizes two constants and returns a single value rather than emitting a `fadd`. This is **constant folding** and happens by default. 

**Defining and calling a function:**
<!-- code-merge:start -->
```pyxc
ready> def sum(a, b): return a + b
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

**Declaring and calling an external function:**

<!-- code-merge:start -->
```pyxc
ready> extern def cos(x)
```
```text
Parsed an extern.
```
```llvm
declare double @cos(double)
```
<!-- code-merge:end -->

`extern def cos(x)` emits a `declare` — a signature with no body. At link time this resolves to the C library's `cos`.

<!-- code-merge:start -->
```pyxc
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
declare double @cos(double)
```
<!-- code-merge:end -->

The end-of-session dump shows only `sum` and the `cos` declaration. The `__anon_expr` functions are absent because `HandleTopLevelExpression` calls `eraseFromParent()` after printing — they were only useful for display.

## What's Next

The IR is correct — but it just prints and does nothing. In [Chapter 6](chapter-06.md) we plug in an **On Request Compilation (ORC)** JIT layer so that top-level expressions actually execute and print their results. We also add an optimization pass manager so the IR that runs is clean and fast. This is the chapter where the compiler comes alive. 

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

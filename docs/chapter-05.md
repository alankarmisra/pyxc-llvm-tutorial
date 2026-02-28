---
description: "Connect the AST to LLVM IR: add codegen() to every node and see real machine-level instructions for the first time."
---
# 5. Pyxc: Code Generation

## Where We Are

We have a parser that builds an AST and a diagnostics layer that gives clear error messages. But the AST just sits in memory. Nothing runs.

By the end of this chapter, typing a function into the REPL produces LLVM IR — the intermediate representation that LLVM compiles to native machine code:

```llvm
ready> def foo(a, b):
return a * a + 2 * a * b + b * b
Parsed a function definition.
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
```

That's real output. LLVM can take that IR and compile it to x86, ARM, or any other target it supports.

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

**`LLVMContext`** owns all LLVM data structures — types, constants, the interning tables that ensure two references to `double` are the same object. Everything else is attached to a context. You normally have one per thread.

**`Module`** is the unit of compilation — the container for all functions and global variables. At the end of the session we print the whole module to see everything that was defined.

**`IRBuilder`** is a cursor into the IR being built. You point it at a `BasicBlock` and call methods like `CreateFAdd`, `CreateFMul`, `CreateCall`. It inserts instructions at the current insertion point and returns the `Value*` representing the result.

A fourth global maps parameter names to their `Value*`:

```cpp
static map<string, Value *> NamedValues;
```

This is the only symbol table for now. Every time we enter a function body we clear it and repopulate it from the function's parameter list. Local variables and closures come in later chapters.

Initialization bundles the three objects:

```cpp
static void InitializeModule() {
  TheContext = make_unique<LLVMContext>();
  TheModule  = make_unique<Module>("my cool jit", *TheContext);
  Builder    = make_unique<IRBuilder<>>(*TheContext);
}
```

`"my cool jit"` is the module identifier — it appears in the `ModuleID` line of the printed IR. The name doesn't affect compilation.

## Adding codegen() to the AST

In chapters 2 and 3, the AST nodes had no methods beyond their constructors. Now we add a pure virtual `codegen()` to the base class:

```cpp
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};
```

`Value` is LLVM's base class for anything that produces a value — constants, instructions, function arguments. Every expression node returns a `Value*` from its `codegen()`. `nullptr` means an error occurred.

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

`APFloat` is LLVM's arbitrary-precision floating-point wrapper. `ConstantFP::get` returns a constant node that can be used directly as an operand. No instruction is emitted — constants aren't instructions, they're values that get folded into the instruction that uses them.

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

Each operator maps to an `IRBuilder` method:

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
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}
```

The string arguments (`"addtmp"`, `"multmp"`, etc.) are hint names for the SSA value. LLVM uses them when printing IR, appending a number if the name would collide. They have no effect on correctness.

The `<` operator needs two steps. `CreateFCmpULT` produces a 1-bit integer (i1) — LLVM's boolean. Since Pyxc treats everything as `double`, we convert it with `CreateUIToFP`: `false` → `0.0`, `true` → `1.0`.

### Function Calls

A call looks up the callee in the module, checks the argument count, codegens each argument, and emits a call instruction:

```cpp
Value *CallExprAST::codegen() {
  Function *CalleeF = TheModule->getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}
```

`TheModule->getFunction` searches the module for a function with that name. If it finds one — whether from a previous `extern` or a previous `def` — we use it. The argument count check catches mismatches that the type system would catch in a typed language.

## Generating Functions

### Prototypes

A prototype creates the function signature in the module — name, return type, parameter types and names:

```cpp
Function *PrototypeAST::codegen() {
  vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
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

Everything in Pyxc is a `double` for now — parameters and return value alike. `FunctionType::get` takes the return type, a list of parameter types, and a boolean for variadic functions (false here).

`ExternalLinkage` means the function is visible outside this module. That's what allows `extern def sin(x)` to link against the C library's `sin` at runtime, and what allows `def foo(...)` to be called from later expressions in the same session.

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
  Function *TheFunction = TheModule->getFunction(Proto->getName());

  if (!TheFunction)
    TheFunction = Proto->codegen();

  if (!TheFunction)
    return nullptr;

  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[string(Arg.getName())] = &Arg;

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

1. **Get or create the function declaration.** If `extern def foo(x)` was seen earlier, `getFunction` finds it. Otherwise `Proto->codegen()` creates a fresh declaration.

2. **Create the entry basic block.** A basic block is a sequence of instructions with one entry and one exit. Every function starts with one. `SetInsertPoint` tells the builder to append new instructions here.

3. **Populate `NamedValues`.** Clear the table (parameters from the last function are irrelevant) and add each argument. Now when the body's `VariableExprAST` nodes look up parameter names, they find the `Value*` representing the incoming argument.

4. **Codegen the body.** If it succeeds, emit `ret` and verify. `verifyFunction` runs LLVM's internal consistency checks — it catches bugs like using a value defined in a different function, or a block with no terminator. If the body fails, erase the partially-built function from the module so it doesn't leave a broken declaration behind.

### SSA Form

The IR you see is in **Static Single Assignment** form — every value has exactly one definition and every use refers to that definition by name. There are no mutable variables in the IR itself. LLVM enforces this and uses it heavily for optimization.

When you write `a * a + 2 * a * b + b * b`, the compiler doesn't think in terms of registers being overwritten. It thinks in terms of values:

```llvm
%multmp  = fmul double %a, %a             ; a*a
%multmp1 = fmul double 2.0, %a            ; 2*a
%multmp2 = fmul double %multmp1, %b       ; (2*a)*b
%addtmp  = fadd double %multmp, %multmp2  ; a*a + 2*a*b
%multmp3 = fmul double %b, %b             ; b*b
%addtmp4 = fadd double %addtmp, %multmp3  ; a*a + 2*a*b + b*b
ret double %addtmp4
```

Each `%name` is defined once and can be used any number of times. The suffixed numbers (`%multmp1`, `%multmp2`) are added automatically when the same hint name would repeat.

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

`errs()` is LLVM's wrapper around `stderr`. `FnIR->print(errs())` dumps the function's IR in human-readable form — the same text you'd get from `llvm-dis`.

Anonymous top-level expressions call `eraseFromParent()` after printing. The expression was only needed to show the IR; it shouldn't accumulate in the module and shouldn't appear in the end-of-session dump.

This unconditional IR printing is intentional for chapter 5 — IR inspection is the whole point of the chapter. Chapter 7 moves it behind a `-v` flag so running a source file doesn't flood the terminal.

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

```llvm
ready> 4 + 5
Parsed a top-level expression.
define double @__anon_expr() {
entry:
  ret double 9.000000e+00
}
```

The constant expression `4 + 5` is folded at IR construction time — `IRBuilder` recognizes two constants and returns a single `ConstantFP` for `9.0` rather than emitting a `fadd` instruction. This is **constant folding**, and it happens for free because `IRBuilder` checks operand types before emitting instructions.

```llvm
ready> def foo(a, b):
return a * a + 2 * a * b + b * b
Parsed a function definition.
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
```

Six instructions for `(a+b)²`. No redundancy — each subexpression is computed once. Notice `2 * a` isn't folded because `a` is a parameter, not a constant.

```llvm
ready> extern def cos(x)
Parsed an extern.
declare double @cos(double)
ready> cos(1.234)
Parsed a top-level expression.
define double @__anon_expr() {
entry:
  %calltmp = call double @cos(double 1.234000e+00)
  ret double %calltmp
}
```

`extern def cos(x)` emits a `declare` — a signature with no body. When `cos(1.234)` is parsed, `CallExprAST::codegen` finds `@cos` in the module and emits a `call` instruction against it. At link time, this resolves to the C library's `cos`.

At the end of the session:

```llvm
; ModuleID = 'PyxcJit'
source_filename = "PyxcJit"

define double @foo(double %a, double %b) {
entry:
  ...
}

define double @bar(double %a) {
entry:
  ...
}

declare double @cos(double)
```

Every `def` and `extern` from the session appears here. The `__anon_expr` functions are absent because `HandleTopLevelExpression` calls `eraseFromParent()` after printing them — they were only useful for display and don't belong in the final module.

## What We Built

| Piece | What it does |
|---|---|
| `LLVMContext` | Owns all LLVM types and constants; one per compilation unit |
| `Module` | Container for all functions and globals; printed in full at session end |
| `FnIR->print(errs())` | Dumps each function's IR to stderr immediately after codegen; `errs()` is LLVM's stderr wrapper |
| `IRBuilder<>` | Cursor that inserts instructions into a basic block |
| `NamedValues` | Symbol table mapping parameter names to their `Value*` |
| `InitializeModule()` | Creates the three globals before the REPL starts |
| `codegen()` on `ExprAST` | Pure virtual; every expression node must implement it |
| `NumberExprAST::codegen` | Returns a `ConstantFP`; no instruction emitted |
| `VariableExprAST::codegen` | Looks up parameter in `NamedValues` |
| `BinaryExprAST::codegen` | Emits `fadd`/`fsub`/`fmul`/`fcmp`+`uitofp` |
| `CallExprAST::codegen` | Looks up callee in module, emits `call` |
| `PrototypeAST::codegen` | Creates a `Function` declaration with typed signature |
| `FunctionAST::codegen` | Creates the entry block, populates `NamedValues`, codegens body, emits `ret`, verifies |
| `LogErrorV` | Returns `Value*` nullptr after printing a diagnostic |
| `verifyFunction` | LLVM's internal consistency check; catches codegen bugs early |
| `eraseFromParent` | Removes a broken or used-up function from the module |

## Known Limitations

- **No optimization.** The IR is correct but unoptimized. `2 * a * b` emits two multiplications even though associativity might allow combining them. Chapter 6 adds LLVM's pass manager.
- **No JIT execution.** Expressions are compiled to IR and printed, but not run. The result of `4 + 5` appears as `ret double 9.0` — you have to read it, you can't evaluate it. Chapter 6 adds ORC JIT so top-level expressions run immediately.
- **Only function parameters as variables.** `NamedValues` only holds the current function's arguments. There are no local variables yet.

## What's Next

The IR is correct. Chapter 6 adds two things on top of it: an optimization pass manager that cleans up the IR, and an ORC JIT layer that executes top-level expressions immediately so you can use the REPL interactively.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

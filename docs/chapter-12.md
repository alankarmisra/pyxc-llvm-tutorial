---
description: "Add global variables so top-level var declarations and assignments persist across REPL inputs and work naturally in compiled files."
---
# 12. Pyxc: Global Variables

## Where We Are

[Chapter 11](chapter-11.md) introduced statement blocks, indentation, and `var` as a proper statement. But `var` only worked inside function bodies. At the top level — both in the REPL and in file mode — there was no way to declare a variable that outlived a single expression:

```python
# Chapter 11 — neither of these works at top level:
var x = 10     # parse error: var is not an expression
x = x + 1     # parse error: x is undeclared
```

This chapter fixes that. After it, the REPL works the way you'd expect, and file mode has a proper entry point:

```python
ready> var x = 10
ready> x = x + 7
ready> extern def printd(n)
ready> printd(x)
17.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-12
```

## The Problem in Detail

In chapter 11, `ParseTopLevelExpr` called `ParseExpression`, which only accepted expressions — not statements. And even if `var x = 10` had somehow parsed, it would have been compiled into a fresh `__anon_expr` function that was immediately freed after execution. The variable and its storage would both be gone before the next REPL line was read.

The root cause is architectural: the REPL compiles each top-level input into a new module, then hands that module to the JIT and (for expressions) frees it. A local `alloca` inside a freed module is unreachable. Global mutable state needs storage that survives across module boundaries.

The solution has two parts:

1. **`GlobalVariable` instead of `alloca`** — LLVM global variables live at a fixed address in the JIT's address space. Any module can declare one as `extern` and the JIT resolves all references to the same storage.

2. **`__pyxc.global_init`** — top-level statements need to run in order. Both the REPL and file mode collect top-level statements into an internal function called `__pyxc.global_init` and call it as an entry point.

## Grammar

The grammar itself is unchanged from chapter 11. What changes is what the top-level dispatch accepts. Previously `top` only dispatched statements as expressions; now it handles the full statement set:

```ebnf
(* unchanged from chapter 11 — the grammar already supported this *)
top        = definition | decorateddef | external | toplevelstmt ;
toplevelstmt = statement ;  (* was: toplevelexpr = expression *)
```

`toplevelstmt` covers everything `statement` covers: `var`, assignment, `if`, `for`, `return`, and plain expressions. The grammar rule is a one-word change; the work is in the parser and codegen.

## A Side Effect Worth Noting

In chapter 11, `var` was a statement but its scope was always a function body — the variable and the code using it were always in the same compilation unit. A top-level `var` breaks that: the declaration is one REPL input (one module), and the code that reads the variable is a different input (a different module). Sharing state across modules requires a different storage mechanism than `alloca`, which is what this chapter introduces.

## Parse-Time Tracking

Chapter 11 tracked declared variables in `VarScopes` — a stack of sets, one per active scope. Chapter 12 adds a parallel set for globals:

```cpp
static vector<set<string>> VarScopes;    // locals and block scopes
static set<string> GlobalVarNames;       // top-level globals (persist forever)
static bool ParsingTopLevel = false;     // true while parsing a top-level statement
```

`ParsingTopLevel` is set by a scope guard whenever the top-level dispatch is active:

```cpp
struct TopLevelParseGuard {
  TopLevelParseGuard()  { ParsingTopLevel = true; }
  ~TopLevelParseGuard() { ParsingTopLevel = false; }
};
```

`ParseVarStmt` checks this flag and routes to the right tracking set:

```cpp
static unique_ptr<ExprAST> ParseVarStmt() {
  getNextToken(); // eat 'var'
  bool IsGlobalDecl = ParsingTopLevel;
  // ...
  if (IsGlobalDecl) {
    if (GlobalVarNames.count(Name))
      return LogError("Variable '...' already declared in this scope");
    // ...
    GlobalVarNames.insert(Name);
  } else {
    if (IsDeclaredInCurrentScope(Name))
      return LogError("Variable '...' already declared in this scope");
    // ...
    DeclareVar(Name);
  }
}
```

`IsDeclaredVar` now checks both sets — so inside a function body, a name resolves as declared if it was declared locally or globally:

```cpp
static bool IsDeclaredVar(const string &Name) {
  for (auto It = VarScopes.rbegin(); It != VarScopes.rend(); ++It)
    if (It->count(Name)) return true;
  return GlobalVarNames.count(Name) > 0;
}
```

## Top-Level Parsing

`ParseTopLevelStatement` wraps the existing `ParseStatement` with the top-level guard:

```cpp
static unique_ptr<ExprAST> ParseTopLevelStatement() {
  LastTopLevelEndedWithBlock = false;
  TopLevelParseGuard Guard;
  auto Stmt = ParseStatement();
  if (!Stmt) return nullptr;
  LastTopLevelShouldPrint = Stmt->shouldPrintValue();
  LastTopLevelEndedWithBlock = LastStatementWasBlock;
  return Stmt;
}
```

`shouldPrintValue()` is a virtual method on `ExprAST`. Statements like `var`, `if`, and `for` return `false` — their result (always `0.0`) is noise, not a value the user asked to see. Plain expressions return `true`. This is how the REPL suppresses the unwanted `0.000000` that would otherwise appear after every `var` declaration.

This flag exists because the AST has a single `ExprAST` hierarchy for both statements and expressions. If the two were split into separate base classes — `StmtAST` producing no value, `ExprAST` producing one — the distinction would be structural and `shouldPrintValue()` wouldn't be needed at all. For now, adding a virtual boolean is the least-invasive fix without a full AST refactor.

`ParseTopLevelExpr` wraps the parsed statement in a uniquely-named function so it goes through the same `FunctionAST` codegen path as everything else:

```cpp
static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  auto Stmt = ParseTopLevelStatement();
  if (!Stmt) return nullptr;

  if (!Stmt->isReturnExpr())
    Stmt = make_unique<ReturnExprAST>(std::move(Stmt));

  string FnName = "__pyxc.toplevel." + to_string(TopLevelExprCounter++);
  auto Proto = make_unique<PrototypeAST>(FnName, vector<string>());
  return make_unique<FunctionAST>(std::move(Proto), std::move(Stmt));
}
```

Each top-level input gets a unique name (`__pyxc.toplevel.0`, `__pyxc.toplevel.1`, …) so the JIT can look them up individually after adding the module.

## Codegen: GlobalVariable

When `VarStmtAST::codegen` runs inside a `__pyxc.global_init` context, it emits a `GlobalVariable` instead of an `alloca`:

```cpp
Value *VarStmtAST::codegen() {
  if (InGlobalInit) {
    for (auto &Var : VarNames) {
      const string &VarName = Var.first;

      // Create the global with a constant zero initializer.
      auto *Ty = Type::getDoubleTy(*TheContext);
      auto *GV = new GlobalVariable(*TheModule, Ty,
                                    /*isConstant=*/false,
                                    GlobalValue::ExternalLinkage,
                                    ConstantFP::get(*TheContext, APFloat(0.0)),
                                    VarName);
      ModuleHasGlobals = true;

      // Run the initializer at runtime and store the result.
      Value *InitVal = Var.second->codegen();
      if (!InitVal) return nullptr;
      Builder->CreateStore(InitVal, GV);
    }
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  // Inside a function: alloca path, unchanged from chapter 11.
  // ...
}
```

Two things to note:

- **Constant zero initializer, then runtime store.** LLVM global variables require a *constant* initializer in the IR — you can't write `@x = global double sin(1.0)`. So every global starts as `0.0`. The actual initializer expression is evaluated at runtime inside `__pyxc.global_init` and stored into the global. This means initializers run in source order, and each one can read the already-initialized value of any earlier global.

- **`ExternalLinkage`.** This makes the symbol visible across module boundaries. Any later module that declares `@x` as `extern` will have its reference resolved by the JIT to the same storage.

`GetGlobalVariable` handles the cross-module visibility. When a later module references a global that was defined in an earlier one, it emits a declaration in the current module and lets the JIT resolve it:

```cpp
static GlobalVariable *GetGlobalVariable(const string &Name) {
  // Fast path: already defined or declared in this module.
  if (auto *GV = TheModule->getNamedGlobal(Name))
    return GV;

  // Not in this module — emit an extern declaration so the JIT can link it.
  if (!GlobalVarNames.count(Name))
    return nullptr;

  auto *Ty = Type::getDoubleTy(*TheContext);
  return new GlobalVariable(*TheModule, Ty,
                            /*isConstant=*/false,
                            GlobalValue::ExternalLinkage,
                            /*Initializer=*/nullptr,  // declaration, not definition
                            Name);
}
```

A `GlobalVariable` with a null initializer is a *declaration* — it says "this symbol exists somewhere, find it at link time." The JIT resolves declarations to their definitions when the module is added.

`VariableExprAST::codegen` and `AssignmentExprAST::codegen` both try the local `NamedValues` table first, then fall back to `GetGlobalVariable`:

```cpp
Value *VariableExprAST::codegen() {
  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second)
    return Builder->CreateLoad(Type::getDoubleTy(*TheContext), It->second, Name);

  if (auto *GV = GetGlobalVariable(Name))
    return Builder->CreateLoad(Type::getDoubleTy(*TheContext), GV, Name);

  return LogErrorV("Unknown variable name");
}

Value *AssignmentExprAST::codegen() {
  Value *Val = Expr->codegen();
  if (!Val) return nullptr;

  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second) {
    Builder->CreateStore(Val, It->second);
    return Val;
  }

  if (auto *GV = GetGlobalVariable(Name)) {
    Builder->CreateStore(Val, GV);
    return Val;
  }

  return LogErrorV("Unknown variable name");
}
```

A local variable always shadows a global of the same name. Inside a function, if you declare `var x`, the alloca goes into `NamedValues` and the `NamedValues` check wins. After the function returns and `NamedValues` is cleared, the global is visible again.

## REPL Mode: HandleTopLevelExpression

In the REPL, each top-level input is still compiled into its own fresh module. The presence of globals changes what happens after codegen:

```cpp
static void HandleTopLevelExpression() {
  auto FnAST = ParseTopLevelExpr();
  // ... error handling ...

  InGlobalInit = true;
  if (auto *FnIR = FnAST->codegen()) {
    InGlobalInit = false;

    bool KeepModule = ModuleHasGlobals;

    if (KeepModule) {
      // Module contains GlobalVariable definitions — add it permanently.
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM)));
      InitializeModuleAndManagers();
    } else {
      // No globals — use a ResourceTracker to free the module after the call.
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();
    }

    auto ExprSymbol = ExitOnErr(TheJIT->lookup(FnName));
    double (*FP)() = ExprSymbol.toPtr<double (*)()>();
    double result = FP();
    if (IsRepl && LastTopLevelShouldPrint)
      fprintf(stderr, "%f\n", result);

    if (!KeepModule)
      ExitOnErr(RT->remove());
  }
}
```

`ModuleHasGlobals` is set by `VarStmtAST::codegen` when it emits a `GlobalVariable`. If the flag is set, the module must be kept permanently — freeing it would destroy the global's storage. If not, the old ResourceTracker path applies and the module is freed after execution.

`InGlobalInit = true` tells `VarStmtAST::codegen` to emit globals rather than allocas for top-level `var` statements.

## File Mode: FileModeLoop and RunFileMode

File mode handles globals differently. Rather than compiling and executing each statement as it's parsed, it collects all top-level statements first:

```cpp
static vector<unique_ptr<ExprAST>> FileTopLevelStmts;

static void FileModeLoop() {
  while (true) {
    // ...
    switch (CurTok) {
    case tok_def:    HandleDefinition(); break;
    case tok_extern: HandleExtern();     break;
    case '@':        /* ... */           break;
    default:
      HandleTopLevelStatementFileMode(); // collect, don't execute
      break;
    }
  }
}
```

`HandleTopLevelStatementFileMode` just parses and appends to `FileTopLevelStmts`. After the entire file is parsed, `RunFileMode` wraps the collected statements into `__pyxc.global_init` and runs it:

```cpp
static void RunFileMode() {
  if (!FileTopLevelStmts.empty()) {
    // Wrap all top-level statements into __pyxc.global_init.
    auto Block = make_unique<BlockExprAST>(std::move(FileTopLevelStmts));
    auto Proto = make_unique<PrototypeAST>("__pyxc.global_init", vector<string>());
    auto FnAST = make_unique<FunctionAST>(std::move(Proto), std::move(Block));

    InGlobalInit = true;
    if (auto *FnIR = FnAST->codegen()) {
      InGlobalInit = false;
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM)));
      InitializeModuleAndManagers();

      // Call the initializer.
      auto InitSymbol = ExitOnErr(TheJIT->lookup("__pyxc.global_init"));
      double (*InitFn)() = InitSymbol.toPtr<double (*)()>();
      InitFn();
    }
  }

  // If the user defined main(), call it after globals are initialized.
  auto MainIt = FunctionProtos.find("main");
  if (MainIt == FunctionProtos.end()) return;

  auto MainSymbol = ExitOnErr(TheJIT->lookup("main"));
  double (*MainFn)() = MainSymbol.toPtr<double (*)()>();
  MainFn();
}
```

The ordering guarantee: `def` and `extern` statements are compiled as they're encountered during `FileModeLoop` (same as before). Top-level `var` and assignment statements are deferred until `RunFileMode`. At the point `__pyxc.global_init` runs, all functions are already compiled and in the JIT — so initializer expressions can call user-defined functions.

If the user defines `main`, it runs after `__pyxc.global_init`, so all globals are fully initialized before `main` executes.

## Scoping Rules

With globals in place, Pyxc now has three scopes:

| Scope | Declared by | Storage | Lifetime |
|---|---|---|---|
| Block | `var` inside an indented block | alloca | Until block exits |
| Function | `var` inside a function body | alloca | Until function returns |
| Global | `var` at top level | `GlobalVariable` | Entire session |

Lookup always goes inner-to-outer: block → function → global. A `var x` inside a function shadows a global `x` for the duration of that function call. The global is unaffected.

## Known Limitations

**`main` takes no arguments.** `RunFileMode` checks that `main()` has zero parameters. There is no way to pass command-line arguments to a Pyxc program yet.

**No global-to-global forward references in initializers.** Initializers run in source order. `var b = a * 2` sees `a`'s initialized value only if `var a = ...` appeared earlier in the file. Referencing a global before it has been initialized reads `0.0` (the constant default).

## Try It

**REPL: persistent counter**

```python
ready> extern def printd(x)
ready> var count = 0
ready> def tick(): count = count + 1
ready> tick()
ready> tick()
ready> tick()
ready> printd(count)
3.000000
```

**File mode: globals + main**

```python
extern def printd(x)

var total = 0

def add(n):
    total = total + n

def main():
    add(10)
    add(5)
    printd(total)
```

```
15.000000
```

**Initialization order**

```python
extern def printd(x)

var a = 3
var b = a * 4   # sees a = 3, not 0
printd(b)       # 12.000000
```

## Build and Run

```bash
cd code/chapter-12
cmake -S . -B build && cmake --build build
./build/pyxc
```

## What's Next

Chapter 13 adds object file emission. Instead of JIT-compiling everything at runtime, Pyxc will be able to write a `.o` file that a system linker can combine with other objects into a standalone native binary — no JIT required.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

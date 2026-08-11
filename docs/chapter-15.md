---
description: "Add global variables so top-level var declarations and assignments persist across REPL inputs and work naturally in compiled files."
---
# 15. pyxc: Global Variables

## What I Am Building

[Chapter 12](chapter-12.md) introduced statement blocks, indentation, and `var` as a proper statement. But `var` only worked inside function bodies. At the top level — both in the REPL and in file mode — there was no way to declare a variable that outlived a single statement:

```pyxc
# Chapter 12 — neither of these works at top level:
var x = 10     # parse error: var is not an expression at the top level
x = x + 1      # parse error: x is undeclared
```

I fix that this chapter. Once I do, the REPL works the way you'd expect, and file mode has a proper entry point:

<!-- code-merge:start -->
```pyxc
ready> var x = 10
ready> x = x + 7
ready> extern def printd(n)
ready> printd(x)
```
```bash
17.000000
```
<!-- code-merge:end -->

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-13
```

## The Problem in Detail

In chapter 12, top-level input was compiled via `ParseTopLevelExpression`, wrapped into a fresh anonymous function, run through the JIT, then — for a plain expression — freed right after execution. Even once `var` became a valid statement, a `var` at the top level would allocate its `alloca` inside that same freed module. The variable and its storage would both be gone before the next REPL line was read.

The root cause is architectural: the REPL compiles each top-level input into a new module, hands it to the JIT, and frees it once the call returns (unless something needs it to stick around). A local `alloca` inside a freed module is unreachable from anywhere else. I need storage for global mutable state that survives across module boundaries.

I solve this in two parts:

1. **`GlobalVariable` instead of `alloca`.** LLVM global variables live at a fixed address in the JIT's address space. Any module can declare one as `extern` and the JIT resolves all references to the same storage.

2. **`__pyxc.global_init`.** Top-level statements need to run in order. Both the REPL and file mode collect top-level statements into an internal function called `__pyxc.global_init` and call it as an entry point.

## Grammar

There's no grammar change this chapter — `code/chapter-13/pyxc.ebnf` is identical to chapter 12's, aside from the header comment. `top-level-expression = expression` still reads exactly the same in the `.ebnf` file. What actually changes is which function the parser calls at the top level: `ParseTopLevelStatement` now routes through `ParseStatement`, the same entry point a function body uses, instead of `ParseExpression` directly — so `var`, assignment, `if`, `for`, and `return` are all valid at the top level, even though the grammar file's own `top-level-expression` production doesn't spell that out explicitly. The real work this chapter is entirely in the parser and codegen, not the grammar.

## A Side Effect Worth Noting

In chapter 12, `var` was a statement, but its scope was always a function body — the variable and the code using it were always in the same compilation unit. A top-level `var` breaks that: the declaration is one REPL input (one module), and the code that reads the variable is a different input (a different module). Sharing state across modules needs a different storage mechanism than `alloca`, which is what this chapter introduces.

## Parse-Time Tracking

Chapter 12 tracked declared variables in `VarScopes` — a stack of sets, one per active scope. I add a parallel set for globals, and a flag for whether I'm currently parsing top-level input:

```cpp
static vector<set<string>> VarScopes;    // locals and block scopes
static set<string> GlobalVarNames;       // top-level globals (persist forever)
static bool ParsingTopLevel = false;     // true while parsing a top-level statement
```

I set `ParsingTopLevel` with a scope guard whenever the top-level dispatch is active:

```cpp
struct TopLevelParseGuard {
  TopLevelParseGuard()  { ParsingTopLevel = true; }
  ~TopLevelParseGuard() { ParsingTopLevel = false; }
};
```

`ParseVarStatement` checks this flag and routes to the right tracking set:

```cpp
static unique_ptr<ExpressionNode> ParseVarStatement() {
  getNextToken(); // eat 'var'
  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;
  bool IsGlobalDecl = ParsingTopLevel;

  while (true) {
    // ... parse name ...
    if (IsGlobalDecl) {
      if (GlobalVarNames.count(ParsedName))
        return LogErrorExpression(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    } else {
      if (IsDeclaredInCurrentScope(ParsedName))
        return LogErrorExpression(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    }
    // ... parse optional initializer ...
    VarNames.push_back({ParsedName, std::move(Init)});
    if (IsGlobalDecl)
      GlobalVarNames.insert(ParsedName);
    else
      DeclareVar(ParsedName);
    // ...
  }
  return make_unique<VarStatementNode>(std::move(VarNames));
}
```

`IsDeclaredVar` checks both sets now — inside a function body, a name resolves as declared if it was declared locally or globally:

```cpp
static bool IsDeclaredVar(const string &Name) {
  for (auto It = VarScopes.rbegin(); It != VarScopes.rend(); ++It)
    if (It->count(Name))
      return true;
  return GlobalVarNames.count(Name) > 0;
}
```

There's one more wrinkle `ParsingTopLevel` fixes: a top-level `if` or `for` still opens a block or loop scope via `BeginBlockScope`/`BeginLoopScope`, same as inside a function — but at the top level there's no enclosing `FunctionScopeGuard` to eventually clear `VarScopes` when everything's done. `EndBlockScope` and `EndLoopScope` both special-case that: once the last scope on the stack belongs to a top-level block or loop rather than a function, they pop it too, instead of leaving it stranded:

```cpp
static void EndBlockScope() {
  if (VarScopes.size() > 1)
    VarScopes.pop_back();
  else if (ParsingTopLevel && VarScopes.size() == 1)
    VarScopes.pop_back();
}
```

## Top-Level Parsing

`ParseTopLevelStatement` wraps `ParseStatement` with the top-level guard, and records whether the REPL should print this statement's result:

```cpp
static unique_ptr<ExpressionNode> ParseTopLevelStatement() {
  TopLevelParseGuard Guard;
  auto Stmt = ParseStatement();
  if (!Stmt)
    return nullptr;
  LastTopLevelShouldPrint = Stmt->shouldPrintValue();
  return Stmt;
}
```

`shouldPrintValue()` is a virtual method on `ExpressionNode`, defaulting to `true`. Statement nodes — `var`, `if`, `for`, `return` — override it to return `false`; their result (always `0.0`) is noise, not a value the user asked to see. Plain expressions keep the default `true`. This is how the REPL suppresses the unwanted `0.000000` that would otherwise appear after every `var` declaration.

I need this flag because the AST still has a single `ExpressionNode` hierarchy for both statements and expressions. If I'd split the two into separate base classes — one producing no value, one producing one — the distinction would be structural and `shouldPrintValue()` wouldn't be needed at all. For now, a virtual boolean is the least-invasive fix, without a full AST refactor.

`ParseTopLevelExpression` wraps the parsed statement in a uniquely-named function so it goes through the same `FunctionDefinitionNode` codegen path as everything else:

```cpp
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  auto Stmt = ParseTopLevelStatement();
  if (!Stmt)
    return nullptr;

  if (!Stmt->isReturnExpr())
    Stmt = make_unique<ReturnExpressionNode>(std::move(Stmt));

  string FnName = "__pyxc.toplevel." + to_string(TopLevelExprCounter++);
  auto Signature = make_unique<FunctionSignatureNode>(FnName, vector<string>());
  return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Stmt));
}
```

Each top-level input gets a unique name (`__pyxc.toplevel.0`, `__pyxc.toplevel.1`, …) so the JIT can look them up individually after adding the module. Wrapping in `ReturnExpressionNode` when the statement isn't already a `return` is what lets `FunctionDefinitionNode::codegen`'s ordinary path emit a real `ret` for it.

## Codegen: Emitting a Global Instead of an Alloca

I want `VarStatementNode::codegen` to emit a `GlobalVariable` instead of an `alloca` when it's running inside `__pyxc.global_init`. There's one wrinkle: by the time a `var` statement codegens, `GetGlobalVariable` (below) may already have emitted a bare *declaration* for this name in the current module — some earlier statement in the same file might have referenced it before its `var` line was reached. So I can't just unconditionally create a new global; I have to check whether one already exists in this module and, if it's only a declaration, promote it to a real definition instead of creating a second, colliding global:

```cpp
Value *VarStatementNode::codegen() {
  if (InGlobalInit) {
    for (auto &Var : VarNames) {
      const string &VarName = Var.first;
      ExpressionNode *Init = Var.second.get();

      auto *GV = TheModule->getNamedGlobal(VarName);
      if (GV && !GV->isDeclaration())
        return LogErrorV("Global variable already defined");

      if (!GV) {
        // No global by this name yet in this module — create one with a
        // constant zero initializer.
        auto *Ty = Type::getDoubleTy(*TheContext);
        GV = new GlobalVariable(
            *TheModule, Ty, false, GlobalValue::ExternalLinkage,
            ConstantFP::get(*TheContext, APFloat(0.0)), VarName);
      } else if (GV->isDeclaration()) {
        // A bare 'extern'-style declaration already exists for this name —
        // turn it into a real definition instead of creating a duplicate.
        GV->setInitializer(ConstantFP::get(*TheContext, APFloat(0.0)));
        GV->setLinkage(GlobalValue::ExternalLinkage);
      }

      ModuleHasGlobals = true;

      Value *InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
      Builder->CreateStore(InitVal, GV);
    }
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  // Inside a function: alloca path, unchanged from chapter 12.
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  for (auto &Var : VarNames) {
    // ...
  }
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

A few things worth noting:

- **Constant zero initializer, then runtime store.** LLVM global variables require a *constant* initializer in the IR — I can't write `@x = global double sin(1.0)`. So every global starts as `0.0`. The actual initializer expression is evaluated at runtime inside `__pyxc.global_init` and stored into the global. Initializers run in source order, and each one can read the already-initialized value of any earlier global.

- **`ExternalLinkage`.** This makes the symbol visible across module boundaries. Any later module that declares `@x` as `extern` will have its reference resolved by the JIT to the same storage.

- **Reusing an existing declaration.** Without the `GV->isDeclaration()` branch, defining a global whose name was already declared elsewhere in this same module would leave two distinct `GlobalVariable` objects fighting over one name — LLVM would silently rename the second one rather than error, and the two objects would no longer refer to the same storage. Checking first and promoting the existing declaration in place avoids that.

`GetGlobalVariable` is what creates those bare declarations, and handles cross-module visibility generally. When a later module references a global that was defined in an earlier one, it emits a declaration in the current module and lets the JIT resolve it:

```cpp
static GlobalVariable *GetGlobalVariable(const string &Name) {
  // Fast path: already defined or declared in this module.
  if (auto *GV = TheModule->getNamedGlobal(Name))
    return GV;

  // Not in this module — emit an extern declaration so the JIT can link it.
  if (!GlobalVarNames.count(Name))
    return nullptr;

  auto *Ty = Type::getDoubleTy(*TheContext);
  return new GlobalVariable(*TheModule, Ty, false, GlobalValue::ExternalLinkage,
                            nullptr, Name); // nullptr initializer = declaration, not definition
}
```

A `GlobalVariable` with a null initializer is a *declaration* — it says "this symbol exists somewhere, find it at link time." The JIT resolves declarations to their definitions when the module is added.

`NameExpressionNode::codegen` and `AssignmentExpressionNode::codegen` both try the local `NamedValues` table first, then fall back to `GetGlobalVariable`:

```cpp
Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second)
    return Builder->CreateLoad(Type::getDoubleTy(*TheContext), It->second, Name.c_str());

  if (auto *GV = GetGlobalVariable(Name))
    return Builder->CreateLoad(Type::getDoubleTy(*TheContext), GV, Name.c_str());

  return LogErrorV("Unknown variable name");
}

Value *AssignmentExpressionNode::codegen() {
  Value *Val = Expr->codegen();
  if (!Val)
    return nullptr;

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

A local variable always shadows a global of the same name. Inside a function, if you declare `var x`, the alloca goes into `NamedValues` and that check wins. After the function returns and `NamedValues` is cleared, the global is visible again.

## REPL Mode: Deciding Whether to Keep the Module

In the REPL, each top-level input still compiles into its own fresh module. The presence of globals changes what happens after codegen:

```cpp
static void HandleTopLevelExpression() {
  auto FnAST = ParseTopLevelExpression();
  // ... error handling ...

  string FnName = FnAST->getName();
  bool SavedInGlobalInit = InGlobalInit;
  InGlobalInit = true;
  if (auto *FnIR = FnAST->codegen()) {
    InGlobalInit = SavedInGlobalInit;
    Log("Parsed a top-level expression.\n");
    if (VerboseIR)
      FnIR->print(errs());

    bool KeepModule = ModuleHasGlobals;

    if (KeepModule) {
      // Module contains GlobalVariable definitions — add it permanently.
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM)));
      InitializeModuleAndManagers();

      auto ExprSymbol = ExitOnErr(TheJIT->lookup(FnName));
      double (*FP)() = ExprSymbol.toPtr<double (*)()>();
      double result = FP();
      if (IsRepl && LastTopLevelShouldPrint)
        fprintf(stderr, "%f\n", result);
    } else {
      // No globals — use a ResourceTracker to free the module after the call.
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      auto ExprSymbol = ExitOnErr(TheJIT->lookup(FnName));
      double (*FP)() = ExprSymbol.toPtr<double (*)()>();
      double result = FP();
      if (IsRepl && LastTopLevelShouldPrint)
        fprintf(stderr, "Evaluated to %f\n", result);

      ExitOnErr(RT->remove());
    }
  } else {
    InGlobalInit = SavedInGlobalInit;
  }
}
```

`ModuleHasGlobals` is set by `VarStatementNode::codegen` when it emits a `GlobalVariable`. If it's set, I keep the module permanently — freeing it would destroy the global's storage. If not, the old ResourceTracker path from chapter 7 applies and the module is freed after execution.

The two branches print with different formats — `"%f\n"` when the module sticks around, `"Evaluated to %f\n"` when it gets freed. That's not a deliberate stylistic choice, it's just what falls out of keeping each branch's own `printf` call where chapter 7 originally put it. I'm noting it here because it's the kind of small inconsistency I'd otherwise forget I introduced, and a reader diffing REPL output against expectations deserves to know it's real, not a typo.

I save and restore `InGlobalInit` rather than hard-resetting it to `false` after codegen, in case `HandleTopLevelExpression` is ever called while something else already has it set. It isn't today, but restoring the old value instead of assuming what it was is a habit worth keeping. Setting it to `true` before codegen is what tells `VarStatementNode::codegen` to emit globals rather than allocas for top-level `var` statements.

## File Mode: Collecting Statements, Then Running Them

File mode needs to handle globals differently. Rather than compiling and executing each statement as it's parsed, I collect all top-level statements first:

```cpp
static vector<unique_ptr<ExpressionNode>> FileTopLevelStmts;

static void FileModeLoop() {
  while (true) {
    // ... skip blank lines, indentation errors, tok_block_end, tok_error ...
    switch (CurrentToken) {
    case tok_def:    HandleFunctionDefinition(); break;
    case tok_extern: HandleExtern();             break;
    default:
      HandleTopLevelStatementFileMode(); // collect, don't execute
      break;
    }
  }
}
```

`HandleTopLevelStatementFileMode` just parses and appends to `FileTopLevelStmts`. Once the entire file is parsed, `RunFileMode` wraps the collected statements into `__pyxc.global_init` and runs it:

```cpp
static void RunFileMode() {
  if (!FileTopLevelStmts.empty()) {
    auto Block = make_unique<BlockExpressionNode>(std::move(FileTopLevelStmts));
    auto Signature =
        make_unique<FunctionSignatureNode>("__pyxc.global_init", vector<string>());
    auto FnAST = make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Block));

    InGlobalInit = true;
    if (auto *FnIR = FnAST->codegen()) {
      InGlobalInit = false;
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM)));
      InitializeModuleAndManagers();

      auto InitSymbol = ExitOnErr(TheJIT->lookup("__pyxc.global_init"));
      double (*InitFn)() = InitSymbol.toPtr<double (*)()>();
      InitFn();
    } else {
      InGlobalInit = false;
      return;
    }
  }

  auto MainIt = FunctionSignatures.find("main");
  if (MainIt == FunctionSignatures.end())
    return;

  if (MainIt->second->getNumParameters() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    return;
  }

  auto MainSymbol = ExitOnErr(TheJIT->lookup("main"));
  double (*MainFn)() = MainSymbol.toPtr<double (*)()>();
  MainFn();
}
```

The ordering guarantee I'm relying on: `def` and `extern` statements are compiled as they're encountered during `FileModeLoop`, same as before. Top-level `var` and assignment statements are deferred until `RunFileMode`. By the time `__pyxc.global_init` runs, all functions are already compiled and in the JIT — so initializer expressions can call user-defined functions.

If the user defines `main`, it runs after `__pyxc.global_init`, so all globals are fully initialized before `main` executes.

## Scoping Rules

With globals in place, pyxc now has three scopes:

| Scope | Declared by | Storage | Lifetime |
|---|---|---|---|
| Block | `var` inside an indented block | alloca | Until block exits |
| Function | `var` inside a function body | alloca | Until function returns |
| Global | `var` at top level | `GlobalVariable` | Entire session |

Lookup always goes inner-to-outer: block → function → global. A `var x` inside a function shadows a global `x` for the duration of that function call. The global is unaffected.

## A Quiet Change: Implicit Return Is Always `0.0`

While working on this chapter I ran into something that isn't really *about* globals, but that I ended up fixing at the same time because I hit it directly while wiring up `tick()`-style examples. In [chapter 12](chapter-12.md), a function with no explicit `return` implicitly returned whatever its last statement's `codegen()` happened to produce. For something like `def tick(): count = count + 1`, that meant the function's return value was the newly-assigned value — a side effect of how `AssignmentExpressionNode::codegen` happens to be written, not something I ever deliberately decided a function's "result" should be.

Once `var`, assignment, `if`, and `for` are all first-class statements that can be the last thing in a body, I don't want a function's implicit return value to quietly depend on which kind of statement happened to be last. So `FunctionDefinitionNode::codegen` ignores the body's codegen result for the purposes of the implicit return, and always returns `0.0` when a function falls off the end without hitting a `return`:

```cpp
if (Value *BodyVal = Body->codegen()) {
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateRet(ConstantFP::get(*TheContext, APFloat(0.0)));
  verifyFunction(*TheFunction);
  TheFPM->run(*TheFunction, *TheFAM);
  return TheFunction;
}
```

Concretely: `def tick(): count = count + 1` now always returns `0.0` when called, no matter what `count` becomes. The global still updates correctly — I verified that separately — it's only the *return value* of a body-with-no-`return` function that's now fixed at `0.0`. If I want a function to hand back a value, I write `return` explicitly. This is why the REPL transcript below prints `Evaluated to 0.000000` after every `tick()` call rather than the incrementing count.

## Known Limitations

**`main` takes no arguments.** `RunFileMode` checks that `main()` has zero parameters. There's no way to pass command-line arguments to a pyxc program yet.

**No global-to-global forward references in initializers.** Initializers run in source order. `var b = a * 2` sees `a`'s initialized value only if `var a = ...` appeared earlier in the file. Referencing a global before it's been initialized reads `0.0`, the constant default.

## Try It

**REPL: persistent counter**

<!-- code-merge:start -->
```pyxc
ready> extern def printd(x)
```
```bash
Parsed an extern.
```
```pyxc
ready> var count = 0
```
```bash
Parsed a top-level expression.
```
```pyxc
ready> def tick(): count = count + 1
```
```bash
Parsed a function definition.
```
```pyxc
ready> tick()
```
```bash
Parsed a top-level expression.
Evaluated to 0.000000
```
```pyxc
ready> tick()
```
```bash
Parsed a top-level expression.
Evaluated to 0.000000
```
```pyxc
ready> tick()
```
```bash
Parsed a top-level expression.
Evaluated to 0.000000
```
```pyxc
ready> printd(count)
```
```bash
Parsed a top-level expression.
3.000000
Evaluated to 0.000000
```
<!-- code-merge:end -->

The `Evaluated to 0.000000` after each `tick()` is the JIT reporting the return value of that line's own top-level wrapper function. `ParseTopLevelExpression` always wraps a bare expression in an explicit `return`, so for `tick()` the wrapper is really `return tick();` — and `tick`'s own body (`count = count + 1`) has no explicit `return`, so `tick()` itself always evaluates to `0.0` now, by the implicit-return rule above. `count` is updating correctly underneath the whole time — `printd(count)` prints the real `3.000000`; the `Evaluated to 0.000000` right after it is a separate thing entirely, just `printd`'s own C-level return value.

**File mode: globals + `main`**

```pyxc
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

```pyxc
extern def printd(x)

var a = 3
var b = a * 4   # sees a = 3, not 0
printd(b)       # 12.000000
```

## Build and Run

```bash
cd code/chapter-13
cmake -S . -B build && cmake --build build
./build/pyxc
```

## What's Next

[Chapter 16](chapter-16.md) compiles straight to native object files instead of only running through the JIT.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

I'll help you figure it out.

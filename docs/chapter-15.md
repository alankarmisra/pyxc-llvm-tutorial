---
section: "Native Toolchain"
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
```
```text
Parsed an extern.
```
```pyxc
ready> printd(x)
```
```text
Parsed a top-level expression.
17.000000
Evaluated to 0.000000
```
<!-- code-merge:end -->

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-15
```

## The Problem in Detail

In chapter 12, top-level input was compiled via `ParseTopLevelExpression`, wrapped into a fresh anonymous function, run through the JIT, then — for a plain expression — freed right after execution. Even once `var` became a valid statement, a `var` at the top level would allocate its `alloca` inside that same freed module. The variable and its storage would both be gone before the next REPL line was read.

The root cause is architectural: the REPL compiles each top-level input into a new module, hands it to the JIT, and frees it once the call returns (unless something needs it to stick around). A local `alloca` inside a freed module is unreachable from anywhere else. I need storage for global mutable state that survives across module boundaries.

I solve this in two parts:

1. **`GlobalVariable` instead of `alloca`.** LLVM global variables live at a fixed address in the JIT's address space. Any module can declare one as `extern` and the JIT resolves all references to the same storage.

2. **`__pyxc.global_init`.** Top-level statements need to run in order. Both the REPL and file mode collect top-level statements into an internal function called `__pyxc.global_init` and call it as an entry point.

## Grammar

One production changes: `top-level-item` used to accept a bare `expression` at the top level. Now it accepts a full `statement` — the same production a function body already uses — so `var`, assignment, `if`, `for`, `while`, `return`, and everything else `ParseStatement` understands are all valid at the top level too, not just expressions:

`code/chapter-15/pyxc.ebnf`

```grammardiff
*...
*top-level-item                    = function-definition
*                                    | external
-                                    | top-level-expression ;
+                                    | top-level-statement ;
*function-definition               = "def" function-signature ":"
*                                    ( simple-statement
*                                      | end-of-lines block ) ;
*external                          = "extern" "def" function-signature ;
-top-level-expression              = expression ;
+top-level-statement               = statement ;
*function-signature                = name "(" [ parameters ] ")" ;
```

Everything past `function-signature` is unchanged from [Chapter 14](chapter-14.md). The real work this chapter is in the parser and codegen — deciding *which* statements get global storage and which get an ordinary stack slot — not in the grammar itself.

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

```cppdiff
*static unique_ptr<ExpressionNode> ParseVarStatement() {
*  getNextToken(); // eat 'var'
*
*  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;
+  bool IsGlobalDeclaration = ParsingTopLevel;
*
*  while (true) {
*    if (CurrentToken != tok_name)
*      return LogErrorExpression("Expected name after 'var'");
*
*    string ParsedName = Name;
*    getNextToken(); // eat name
*
-    if (IsDeclaredInCurrentScope(ParsedName))
-      return LogErrorExpression(
-          ("Variable '" + ParsedName + "' already declared in this scope").c_str());
+    if (IsGlobalDeclaration) {
+      if (GlobalVarNames.count(ParsedName))
+        return LogErrorExpression(
+            ("Variable '" + ParsedName + "' already declared in this scope")
+                .c_str());
+    } else if (IsDeclaredInCurrentScope(ParsedName)) {
+      return LogErrorExpression(
+          ("Variable '" + ParsedName + "' already declared in this scope")
+              .c_str());
+    }
*
*    unique_ptr<ExpressionNode> Init;
*    if (CurrentToken == tok_equal) {
*      getNextToken(); // eat '='
*      Init = ParseExpression();
*      if (!Init)
*        return nullptr;
*    } else {
*      Init = make_unique<NumberExpressionNode>(0.0);
*    }
*
*    VarNames.push_back({ParsedName, std::move(Init)});
-    DeclareVar(ParsedName);
+    if (IsGlobalDeclaration)
+      GlobalVarNames.insert(ParsedName);
+    else
+      DeclareVar(ParsedName);
*
*    if (CurrentToken != tok_comma)
*      break;
*    getNextToken(); // eat ','
*  }
*
*  return make_unique<VarStatementNode>(std::move(VarNames));
*}
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
  auto Statement = ParseStatement();
  if (!Statement)
    return nullptr;
  LastTopLevelShouldPrint = Statement->shouldPrintValue();
  return Statement;
}
```

`shouldPrintValue()` is a virtual method on `ExpressionNode`, defaulting to `true`. Statement nodes — `var`, assignment, a `{...}` block, `if`, `for`, `while`, `return`, `break`, `continue` — override it to return `false`; their result (always `0.0`) is noise, not a value the user asked to see. Plain expressions keep the default `true`. This is how the REPL suppresses the unwanted `Parsed a top-level expression.` / `Evaluated to 0.000000` noise that would otherwise appear after every `var` declaration or assignment.

I need this flag because the AST still has a single `ExpressionNode` hierarchy for both statements and expressions. If I'd split the two into separate base classes — one producing no value, one producing one — the distinction would be structural and `shouldPrintValue()` wouldn't be needed at all. For now, a virtual boolean is the least-invasive fix, without a full AST refactor.

`ParseTopLevelStatementFunction` wraps the parsed statement in a uniquely-named zero-parameter function so it goes through the same `FunctionDefinitionNode` codegen path as everything else:

```cpp
static unique_ptr<FunctionDefinitionNode> ParseTopLevelStatementFunction() {
  auto Statement = ParseTopLevelStatement();
  if (!Statement)
    return nullptr;

  if (!Statement->isReturnStatement())
    Statement = make_unique<ReturnStatementNode>(std::move(Statement));

  string FunctionName =
      "__pyxc.toplevel." + to_string(TopLevelStatementCounter++);
  auto Signature =
      make_unique<FunctionSignatureNode>(FunctionName, vector<string>());
  return make_unique<FunctionDefinitionNode>(std::move(Signature),
                                             std::move(Statement));
}
```

Each top-level input gets a unique name (`__pyxc.toplevel.0`, `__pyxc.toplevel.1`, …) so the JIT can look them up individually after adding the module. Wrapping in `ReturnStatementNode` when the statement isn't already a `return` is what lets `FunctionDefinitionNode::codegen`'s ordinary path emit a real `ret` for it.

## Codegen: Emitting a Global Instead of an Alloca

I want `VarStatementNode::codegen` to emit a `GlobalVariable` instead of an `alloca` when it's running inside `__pyxc.global_init`. There's one wrinkle: by the time a `var` statement codegens, `GetGlobalVariable` (below) may already have emitted a bare *declaration* for this name in the current module — some earlier statement in the same file might have referenced it before its `var` line was reached. So I can't just unconditionally create a new global; I have to check whether one already exists in this module and, if it's only a declaration, promote it to a real definition instead of creating a second, colliding global:

```cpp
Value *VarStatementNode::codegen() {
  if (InGlobalInit) {
    for (auto &Var : VarNames) {
      const string &VarName = Var.first;
      ExpressionNode *Initializer = Var.second.get();

      auto *Global = TheModule->getNamedGlobal(VarName);
      if (Global && !Global->isDeclaration())
        return LogErrorValue("Global variable already defined");

      if (!Global) {
        // No global by this name yet in this module — create one with a
        // constant zero initializer.
        Global = new GlobalVariable(
            *TheModule, Type::getDoubleTy(*TheContext), false,
            GlobalValue::ExternalLinkage,
            ConstantFP::get(*TheContext, APFloat(0.0)), VarName);
      } else {
        // A bare 'extern'-style declaration already exists for this name —
        // turn it into a real definition instead of creating a duplicate.
        Global->setInitializer(
            ConstantFP::get(*TheContext, APFloat(0.0)));
        Global->setLinkage(GlobalValue::ExternalLinkage);
      }

      ModuleHasGlobals = true;

      Value *InitialValue = Initializer->codegen();
      if (!InitialValue)
        return nullptr;
      TheBuilder->CreateStore(InitialValue, Global);
    }

    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  // Inside a function: alloca path, unchanged from chapter 12.
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();
  for (auto &Var : VarNames) {
    // ...
  }
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

A few things worth noting:

- **Constant zero initializer, then runtime store.** LLVM global variables require a *constant* initializer in the IR — I can't write `@x = global double sin(1.0)`. So every global starts as `0.0`. The actual initializer expression is evaluated at runtime inside `__pyxc.global_init` and stored into the global. Initializers run in source order, and each one can read the already-initialized value of any earlier global.

- **`ExternalLinkage`.** This makes the symbol visible across module boundaries. Any later module that declares `@x` as `extern` will have its reference resolved by the JIT to the same storage.

- **Reusing an existing declaration.** The `if (Global && !Global->isDeclaration())` guard above already rules out the case where a real definition exists; by the time execution reaches the `else` branch, any `Global` that exists must be a bare declaration. Promoting it in place, instead of creating a second `GlobalVariable` with the same name, avoids LLVM silently renaming the newcomer — which would leave two distinct objects that no longer refer to the same storage.

`GetGlobalVariable` is what creates those bare declarations, and handles cross-module visibility generally. When a later module references a global that was defined in an earlier one, it emits a declaration in the current module and lets the JIT resolve it:

```cpp
static GlobalVariable *GetGlobalVariable(const string &Name) {
  if (auto *Global = TheModule->getNamedGlobal(Name))
    return Global;

  if (!GlobalVarNames.count(Name))
    return nullptr;

  return new GlobalVariable(*TheModule, Type::getDoubleTy(*TheContext), false,
                            GlobalValue::ExternalLinkage, nullptr, Name);
}
```

A `GlobalVariable` with a null initializer is a *declaration* — it says "this symbol exists somewhere, find it at link time." The JIT resolves declarations to their definitions when the module is added.

`NameExpressionNode::codegen` and `AssignmentStatementNode::codegen` both try the local `NamedValues` table first, then fall back to `GetGlobalVariable`:

```cpp
Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second)
    return TheBuilder->CreateLoad(Type::getDoubleTy(*TheContext), It->second,
                               Name.c_str());

  if (auto *Global = GetGlobalVariable(Name))
    return TheBuilder->CreateLoad(Type::getDoubleTy(*TheContext), Global,
                               Name.c_str());

  return LogErrorValue("Unknown variable name");
}

Value *AssignmentStatementNode::codegen() {
  Value *Value = Expr->codegen();
  if (!Value)
    return nullptr;

  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second) {
    TheBuilder->CreateStore(Value, It->second);
    return Value;
  }

  if (auto *Global = GetGlobalVariable(Name)) {
    TheBuilder->CreateStore(Value, Global);
    return Value;
  }

  return LogErrorValue("Unknown variable name");
}
```

A local variable always shadows a global of the same name. Inside a function, if you declare `var x`, the alloca goes into `NamedValues` and that check wins. After the function returns and `NamedValues` is cleared, the global is visible again.

`ForStatementNode::codegen` gets the same fallback. A `for x = start, cond, step: ...` loop that reuses an already-declared name (no `var`) used to look `VarName` up in `NamedValues` only, and fail if it wasn't a local. Now it tries `NamedValues` first and falls back to `GetGlobalVariable`, storing whichever pointer it finds in a new `VariablePointer` local that the rest of the function (the initial store, the per-iteration load, and the step store) uses in place of the old `Alloca`:

```cpp
Value *VariablePointer = nullptr;
AllocaInst *Alloca = nullptr;
AllocaInst *OldVal = nullptr;
if (IsVarDecl) {
  auto OldIt = NamedValues.find(VarName);
  OldVal = (OldIt != NamedValues.end()) ? OldIt->second : nullptr;
  Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
  VariablePointer = Alloca;
  NamedValues[VarName] = Alloca;
} else {
  auto It = NamedValues.find(VarName);
  if (It != NamedValues.end() && It->second)
    VariablePointer = It->second;
  else if (auto *Global = GetGlobalVariable(VarName))
    VariablePointer = Global;
  else
    return LogErrorValue("Unknown variable name");
}
```

This means `for count = 0, count < 10, count + 1: ...` can now drive a global counter directly, without a local shadow, as long as `count` was declared with a top-level `var` first.

## REPL Mode: Deciding Whether to Keep the Module

In the REPL, each top-level input still compiles into its own fresh module. The presence of globals changes what happens after codegen:

```cpp
/// HandleTopLevelStatement - Compile and execute one REPL statement.
/// I keep a module when it defines global storage. Otherwise I attach a
/// ResourceTracker and remove the temporary module after execution.
static void HandleTopLevelStatement() {
  auto FunctionDefinition = ParseTopLevelStatementFunction();
  // ... error handling ...

  string FunctionName = FunctionDefinition->getName();
  bool SavedInGlobalInit = InGlobalInit;
  InGlobalInit = true;
  if (auto *FunctionIR = FunctionDefinition->codegen()) {
    InGlobalInit = SavedInGlobalInit;
    if (LastTopLevelShouldPrint)
      Log("Parsed a top-level expression.\n");
    if (VerboseIR)
      FunctionIR->print(errs());

    if (ModuleHasGlobals) {
      // Module contains GlobalVariable definitions — add it permanently.
      ExitOnErr(JIT->addModule(
          ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
      InitializeModuleAndManagers();

      auto Symbol = ExitOnErr(JIT->lookup(FunctionName));
      double (*FunctionPointer)() = Symbol.toPtr<double (*)()>();
      double Result = FunctionPointer();
      if (IsRepl && LastTopLevelShouldPrint)
        fprintf(stderr, "Evaluated to %f\n", Result);
      return;
    }

    // No globals — use a ResourceTracker to free the module after the call.
    auto ResourceTracker =
        JIT->getMainJITDylib().createResourceTracker();
    ExitOnErr(JIT->addModule(
        ThreadSafeModule(std::move(TheModule), std::move(TheContext)),
        ResourceTracker));
    InitializeModuleAndManagers();

    auto Symbol = ExitOnErr(JIT->lookup(FunctionName));
    double (*FunctionPointer)() = Symbol.toPtr<double (*)()>();
    double Result = FunctionPointer();
    if (IsRepl && LastTopLevelShouldPrint)
      fprintf(stderr, "Evaluated to %f\n", Result);

    ExitOnErr(ResourceTracker->remove());
  } else {
    InGlobalInit = SavedInGlobalInit;
  }
}
```

`ModuleHasGlobals` is set by `VarStatementNode::codegen` when it emits a `GlobalVariable`. If it's set, I keep the module permanently — freeing it would destroy the global's storage. If not, the old ResourceTracker path from chapter 8 applies and the module is freed after execution. Both branches print `"Evaluated to %f\n"` — keeping a module doesn't change how its result gets reported, only what happens to the module afterward.

`LastTopLevelShouldPrint` gates both the `"Parsed a top-level expression."` line and the final result print. That's why `var count = 0` produces no REPL output at all: `VarStatementNode::shouldPrintValue()` returns `false`, so neither message fires.

I save and restore `InGlobalInit` rather than hard-resetting it to `false` after codegen, in case `HandleTopLevelStatement` is ever called while something else already has it set. It isn't today, but restoring the old value instead of assuming what it was is a habit worth keeping. Setting it to `true` before codegen is what tells `VarStatementNode::codegen` to emit globals rather than allocas for top-level `var` statements.

## File Mode: Collecting Statements, Then Running Them

File mode needs to handle globals differently. Rather than compiling and executing each statement as it's parsed, I collect all top-level statements first:

```cpp
static vector<unique_ptr<ExpressionNode>> FileTopLevelStatements;

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

`HandleTopLevelStatementFileMode` just parses and appends to `FileTopLevelStatements`. Once the entire file is parsed, `RunFileMode` wraps the collected statements into `__pyxc.global_init` and runs it:

```cpp
/// I emit queued file statements into __pyxc.global_init, execute them in
/// source order, and then call a zero-parameter main function when one exists.
static void RunFileMode() {
  if (!FileTopLevelStatements.empty()) {
    auto Block =
        make_unique<BlockStatementNode>(std::move(FileTopLevelStatements));
    auto Signature = make_unique<FunctionSignatureNode>(
        "__pyxc.global_init", vector<string>());
    auto FunctionDefinition = make_unique<FunctionDefinitionNode>(
        std::move(Signature), std::move(Block));

    bool SavedInGlobalInit = InGlobalInit;
    InGlobalInit = true;
    if (auto *FunctionIR = FunctionDefinition->codegen()) {
      InGlobalInit = SavedInGlobalInit;
      if (VerboseIR)
        FunctionIR->print(errs());

      ExitOnErr(JIT->addModule(
          ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
      InitializeModuleAndManagers();

      auto InitSymbol = ExitOnErr(JIT->lookup("__pyxc.global_init"));
      double (*InitializeGlobals)() = InitSymbol.toPtr<double (*)()>();
      InitializeGlobals();
    } else {
      InGlobalInit = SavedInGlobalInit;
      return;
    }
  }

  auto Main = FunctionSignatures.find("main");
  if (Main == FunctionSignatures.end())
    return;

  if (Main->second->getNumParameters() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    return;
  }

  auto MainSymbol = ExitOnErr(JIT->lookup("main"));
  double (*MainFunction)() = MainSymbol.toPtr<double (*)()>();
  MainFunction();
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

## A Reminder: Implicit Return Is Always `0.0`

`def tick(): count = count + 1` calls out a rule that's easy to forget once globals are in the picture, even though it isn't new here — [Chapter 12](chapter-12.md) already made `FunctionDefinitionNode::codegen` ignore the body's own codegen result for the purposes of an implicit return, and emit a plain `0.0` whenever a function falls off the end without hitting `return`:

```cpp
if (Value *BodyVal = Body->codegen()) {
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateRet(ConstantFP::get(*TheContext, APFloat(0.0)));
  verifyFunction(*TheFunction);
  FunctionPasses->run(*TheFunction, *FunctionAnalyses);
  return TheFunction;
}
```

`tick()` never writes `return`, so it always evaluates to `0.0`, no matter what `count` becomes — the assignment inside it isn't the function's "result" just because it happened to be the last statement. The global still updates correctly; only the *return value* of a body-with-no-`return` function is fixed at `0.0`. This is why the REPL transcript below prints `Evaluated to 0.000000` after every `tick()` call rather than the incrementing count.

## Known Limitations

**`main` takes no arguments.** `RunFileMode` checks that `main()` has zero parameters. There's no way to pass command-line arguments to a pyxc program yet.

**No global-to-global forward references in initializers.** Initializers run in source order. `var b = a * 2` sees `a`'s initialized value only if `var a = ...` appeared earlier in the file. Referencing a global before it's been initialized reads `0.0`, the constant default.

## Try It

**REPL: persistent counter**

<!-- code-merge:start -->
```pyxc
ready> extern def printd(x)
```
```text
Parsed an extern.
```
```pyxc
ready> var count = 0
ready> def tick(): count = count + 1
```
```text
Parsed a function definition.
```
```pyxc
ready> tick()
```
```text
Parsed a top-level expression.
Evaluated to 0.000000
```
```pyxc
ready> tick()
```
```text
Parsed a top-level expression.
Evaluated to 0.000000
```
```pyxc
ready> tick()
```
```text
Parsed a top-level expression.
Evaluated to 0.000000
```
```pyxc
ready> printd(count)
```
```text
Parsed a top-level expression.
3.000000
Evaluated to 0.000000
```
<!-- code-merge:end -->

The `Evaluated to 0.000000` after each `tick()` is the JIT reporting the return value of that line's own top-level wrapper function. `ParseTopLevelStatementFunction` always wraps a bare expression in an explicit `return`, so for `tick()` the wrapper is really `return tick();` — and `tick`'s own body (`count = count + 1`) has no explicit `return`, so `tick()` itself always evaluates to `0.0`, by the implicit-return rule above. `count` is updating correctly underneath the whole time — `printd(count)` prints the real `3.000000`; the `Evaluated to 0.000000` right after it is a separate thing entirely, just `printd`'s own C-level return value.

**File mode: globals + `main`**

<!-- code-merge:start -->
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
```text
$ ./build/pyxc program.pyxc
15.000000
```
<!-- code-merge:end -->

**Initialization order**

<!-- code-merge:start -->
```pyxc
extern def printd(x)

var a = 3
var b = a * 4   # sees a = 3, not 0
printd(b)       # 12.000000
```
```text
$ ./build/pyxc program.pyxc
12.000000
```
<!-- code-merge:end -->

## Build and Run

```bash
cd code/chapter-15
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

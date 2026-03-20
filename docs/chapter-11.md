---
description: "Switch from expression-only bodies to statement blocks with indentation, and make if/for/var/return statements."
---
# 11. Pyxc: Statement Blocks

## Where We Are

[Chapter 10](chapter-10.md) added mutable variables, but the function body was still a single expression. The `var` form needed a `:` and a body expression, and `for` loops were expressions that produced `0.0`:

<!-- code-merge:start -->
```python
ready> def sum_to(n): return var acc = 0: for i = 1, i <= n, 1: acc = acc + i
```
```bash
Parsed a function definition.
```
<!-- code-merge:end -->

That works, but it is not how Python reads. This chapter introduces real statement blocks and indentation-sensitive syntax. After this chapter:

<!-- code-merge:start -->
```python
ready> def sum_to(n):
    var acc = 0
    for i = 1, i <= n, 1:
        acc = acc + i
    return acc
```
```bash
Parsed a function definition.
```
<!-- code-merge:end -->

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-11
```

## Grammar

The central shift: `if`, `for`, `var`, and `return` move out of the expression grammar and become statements. Expressions are now purely value-producing.

`code/chapter-11/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = definition | decorateddef | external | toplevelexpr ;
definition      = "def" prototype ":" ( simplestmt | eols block ) ;  -- changed
decorateddef    = binarydecorator eols "def" binaryopprototype ":" ( simplestmt | eols block )
                | unarydecorator  eols "def" unaryopprototype  ":" ( simplestmt | eols block ) ;
binarydecorator = "@" "binary" "(" integer ")" ;
unarydecorator  = "@" "unary" ;
binaryopprototype = customopchar "(" identifier "," identifier ")" ;
unaryopprototype  = customopchar "(" identifier ")" ;
external        = "extern" "def" prototype ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ identifier { "," identifier } ] ")" ;
ifstmt          = "if" expression ":" suite                           
                [ eols "else" ":" suite ] ;
forstmt         = "for" identifier "=" expression "," expression "," expression ":" suite ; 
varstmt         = "var" varbinding { "," varbinding } ;               (no body)
assignstmt      = identifier "=" expression ;                         
simplestmt      = returnstmt | varstmt | assignstmt | expression ;   
compoundstmt    = ifstmt | forstmt ;                                  
statement       = simplestmt | compoundstmt ;                        
suite           = simplestmt | compoundstmt | eols block ;           
returnstmt      = "return" expression ;                               
block           = indent statement { eols statement } dedent ;        
expression      = unaryexpr binoprhs ;                               -- simplified (no var/=)
binoprhs        = { binaryop unaryexpr } ;
varbinding      = identifier [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | primary ;
unaryop         = "-" | userdefunaryop ;
primary         = identifierexpr | numberexpr | parenexpr ;          -- simplified (no if/for)
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
numberexpr      = number ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;                                            
dedent          = DEDENT ;                                            
builtinbinaryop = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
integer         = digit { digit } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
INDENT          = ? synthetic token emitted by lexer ? ;              
DEDENT          = ? synthetic token emitted by lexer ? ;              
```

Key new rules:

- **`suite`** — what follows a `:`. Either a single statement on the same line, or a newline followed by an indented block.
- **`simplestmt`** — statements that fit on one line: `return`, `var`, assignment, or a bare expression.
- **`compoundstmt`** — statements that introduce a new suite: `if` and `for`.
- **`block`** — an `INDENT` token, one or more statements separated by newlines, a `DEDENT` token.
- **`INDENT` / `DEDENT`** — synthetic tokens emitted by the lexer; the parser never sees raw whitespace.

## Statements vs Expressions

Before this chapter, `if`, `for`, and `var` were expressions — they produced a value and could be nested:

```python
var acc = 0: for i = 1, ...: acc = acc + i
```

Statements don't produce values — they *do* things. Once `if`, `for`, `var`, and `return` are statements, a function body becomes a flat list of them:

```python
var acc = 0
for i = 1, ...:
    acc = acc + i
return acc
```

`ParseExpression` no longer handles `var`, `if`, `for`, or assignment `=`. Those are all in `ParseStatement` and `ParseSimpleStmt`. Expressions are now purely value-producing — operators, calls, variable reads.

## New Tokens and AST Nodes

Two new token values are added to the lexer's enum:

```cpp
tok_indent = -19,  // synthetic: start of an indented block
tok_dedent = -20,  // synthetic: end of an indented block
```

And three new AST node classes:

**`ReturnExprAST`** — a `return` statement. Emits a real LLVM terminator:

```cpp
class ReturnExprAST : public ExprAST {
  unique_ptr<ExprAST> Expr;
public:
  ReturnExprAST(unique_ptr<ExprAST> Expr) : Expr(std::move(Expr)) {}
  Value *codegen() override;
};
```

**`BlockExprAST`** — a sequence of statements evaluated in order. The block's value is the value of its last statement:

```cpp
class BlockExprAST : public ExprAST {
  vector<unique_ptr<ExprAST>> Stmts;
public:
  BlockExprAST(vector<unique_ptr<ExprAST>> Stmts)
      : Stmts(std::move(Stmts)) {}
  Value *codegen() override;
};
```

**`VarStmtAST`** — the statement form of `var`. Unlike `VarExprAST` from chapter 10, it has no body. Variables declared here persist for the rest of the function:

```cpp
class VarStmtAST : public ExprAST {
  vector<pair<string, unique_ptr<ExprAST>>> VarNames;
public:
  VarStmtAST(vector<pair<string, unique_ptr<ExprAST>>> VarNames)
      : VarNames(std::move(VarNames)) {}
  Value *codegen() override;
};
```

`IfStmtAST` also exists — the statement form of `if`. It differs from `IfExprAST` in that it doesn't need to produce a value, so it has no PHI node and the `else` branch is optional.

## INDENT and DEDENT

The lexer keeps an indentation stack and a pending-token queue. At the start of each line it measures the column, compares it to the top of the stack, and pushes `INDENT` or `DEDENT` tokens into the queue for the parser to drain one at a time:

```cpp
static vector<int> IndentStack = {0}; // starts at column 0
static deque<int>  PendingTokens;     // buffered tokens the parser hasn't seen yet
static bool AtLineStart = true;       // true right after a newline
```

Inside `gettok()`, before any normal token logic, the indentation is processed:

```cpp
if (AtLineStart) {
  // Measure the indent level of this line.
  int IndentCol = 0;
  while (LastChar == ' ' || LastChar == '\t') {
    IndentCol += (LastChar == ' ') ? 1 : (8 - IndentCol % 8); // tabs to cols
    LastChar = advance();
  }

  // More indented than current level → emit one INDENT.
  if (IndentCol > IndentStack.back()) {
    IndentStack.push_back(IndentCol);
    PendingTokens.push_back(tok_indent);
  }
  // Less indented → emit one DEDENT per level closed.
  else if (IndentCol < IndentStack.back()) {
    while (IndentStack.size() > 1 && IndentCol < IndentStack.back()) {
      IndentStack.pop_back();
      PendingTokens.push_back(tok_dedent);
    }
    // Dedenting to a level that was never opened is an error.
    if (IndentCol != IndentStack.back()) {
      fprintf(stderr, "Error (...): inconsistent indentation\n");
      return tok_error;
    }
  }

  AtLineStart = false;
  // Return the first pending token if any were just pushed.
  if (!PendingTokens.empty()) {
    int Tok = PendingTokens.front();
    PendingTokens.pop_front();
    return Tok;
  }
}
```

A single dedent can produce multiple `DEDENT` tokens — one for each level that closed. The queue holds them until the parser asks. `PendingTokens` is a `deque` so `pop_front()` is O(1).

At EOF, the lexer flushes one `DEDENT` per still-open block:

```cpp
if (LastChar == EOF) {
  if (IndentStack.size() > 1) {
    IndentStack.pop_back();
    return tok_dedent; // gettok is called again for the next one
  }
  return tok_eof;
}
```

In REPL mode, a blank line ends the current indented block immediately — the same behavior as the Python REPL.

## Indentation Rules

- Indentation is measured in columns.
- Tabs advance to the next multiple of 8 columns.
- Do not mix tabs and spaces — it is an error regardless of whether they appear to line up.
- Blank lines and comment-only lines do not affect indentation.
- A block opens after `:` followed by a newline and a deeper indent level.

## Parse-Time Variable Tracking

Assignment to an undeclared variable is a parse-time error in chapter 11:

```python
ready> x = 1
Error: Assignment to undeclared variable
```

To detect this, the parser maintains a scope stack of declared variable names. `var` declarations register names; `for` loops introduce a loop variable into a temporary inner scope:

```cpp
static vector<set<string>> VarScopes;

static void BeginFunctionScope(const vector<string> &Args) {
  VarScopes.clear();
  VarScopes.emplace_back();
  for (const auto &Arg : Args)
    VarScopes.front().insert(Arg); // parameters are pre-declared
}

static void EndFunctionScope() { VarScopes.clear(); }

static void DeclareVar(const string &Name) {
  if (!VarScopes.empty())
    VarScopes.front().insert(Name); // declare in the current function scope
}

static bool IsDeclaredVar(const string &Name) {
  for (auto It = VarScopes.rbegin(); It != VarScopes.rend(); ++It)
    if (It->count(Name)) return true;
  return false;
}
```

RAII guards manage scope lifetime automatically:

```cpp
struct FunctionScopeGuard {
  FunctionScopeGuard(const vector<string> &Args) { BeginFunctionScope(Args); }
  ~FunctionScopeGuard() { EndFunctionScope(); }
};

struct LoopScopeGuard {
  LoopScopeGuard(const string &Name) { EnterLoopScope(Name); }
  ~LoopScopeGuard() { ExitLoopScope(); }
};
```

`ParseDefinition` creates a `FunctionScopeGuard` immediately after parsing the prototype — before parsing the body:

```cpp
static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto) return nullptr;

  FunctionScopeGuard Scope(Proto->getArgs()); // parameters enter scope here

  // ... parse ':' and body ...
}
```

`ParseForStmt` creates a `LoopScopeGuard` for the loop variable:

```cpp
string VarName = IdentifierStr;
getNextToken(); // eat identifier
LoopScopeGuard LoopScope(VarName); // loop var in scope for cond, step, body
```

## Parsing a Suite

After every `:`, the parser calls `ParseSuite`. A suite is either an inline statement or an indented block:

```cpp
/// suite = simplestmt | compoundstmt | eols block ;
static unique_ptr<ExprAST> ParseSuite(bool *EndedWithBlock) {
  if (CurTok == tok_eol) {
    // Newline after ':' → expect an indented block.
    consumeNewlines();
    if (CurTok != tok_indent)
      return LogError("Expected an indented block");
    *EndedWithBlock = true;
    return ParseBlock();
  }
  // Same line after ':' → parse an inline statement.
  auto Stmt = ParseStatement();
  if (!Stmt) return nullptr;
  *EndedWithBlock = LastStatementWasBlock;
  return Stmt;
}
```

`ParseIfStmt` and `ParseForStmt` both call `ParseSuite` after eating `:`. A `def` body works slightly differently — the inline form only accepts a `simplestmt`, not a compound statement. You cannot write `def f(x): if x > 0: return 1` on one line.

## Parsing a Block

`ParseBlock` consumes `INDENT`, reads statements separated by newlines until `DEDENT`, then consumes `DEDENT`:

```cpp
/// block = INDENT statement { eols statement } DEDENT ;
static unique_ptr<ExprAST> ParseBlock() {
  if (CurTok != tok_indent)
    return LogError("Expected an indented block");
  getNextToken(); // eat INDENT
  consumeNewlines();

  if (CurTok == tok_dedent)
    return LogError("Expected at least one statement in block");

  vector<unique_ptr<ExprAST>> Stmts;
  while (true) {
    if (CurTok == tok_dedent) break;

    auto Stmt = ParseStatement();
    if (!Stmt) return nullptr;
    Stmts.push_back(std::move(Stmt));

    if (CurTok == tok_eol) { consumeNewlines(); continue; }
    if (CurTok == tok_dedent) break;
    // A nested compound statement (if/for) leaves the parser just after
    // its suite — no newline is required before the next statement.
    if (LastStatementWasBlock) continue;

    return LogError("Expected newline or end of block");
  }

  getNextToken(); // eat DEDENT
  return make_unique<BlockExprAST>(std::move(Stmts));
}
```

`LastStatementWasBlock` is a global flag set by `ParseIfStmt` / `ParseForStmt` when their suite ended with an indented block. It lets the enclosing block keep parsing without requiring a newline first.

## Parsing Statements

`ParseStatement` dispatches to compound or simple statement parsers:

```cpp
/// statement = simplestmt | compoundstmt ;
static unique_ptr<ExprAST> ParseStatement() {
  LastStatementWasBlock = false;
  if (CurTok == tok_if)  return ParseIfStmt();
  if (CurTok == tok_for) return ParseForStmt();
  return ParseSimpleStmt();
}
```

`ParseSimpleStmt` handles `return`, `var`, assignment, and bare expressions:

```cpp
/// simplestmt = returnstmt | varstmt | assignstmt | expression ;
static unique_ptr<ExprAST> ParseSimpleStmt() {
  if (CurTok == tok_return) return ParseReturnStmt();
  if (CurTok == tok_var)    return ParseVarStmt();

  // Parse an expression. If it is immediately followed by '=', it's an
  // assignment. The left-hand side must be a plain variable name.
  auto Expr = ParseExpression();
  if (!Expr) return nullptr;

  if (CurTok != '=')
    return Expr; // bare expression statement

  const string *AssignedName = Expr->getVariableName();
  if (!AssignedName)
    return LogError("Destination of '=' must be a variable");

  string Name = *AssignedName;
  if (!IsDeclaredVar(Name))
    return LogError("Assignment to undeclared variable");

  getNextToken(); // eat '='
  auto RHS = ParseExpression();
  if (!RHS) return nullptr;
  return make_unique<AssignmentExprAST>(Name, std::move(RHS));
}
```

Note the addition of `IsDeclaredVar(Name)` — chapter 11 rejects assignments to undeclared names at parse time rather than deferring the check to codegen.

## Parsing Var as a Statement

`var` in chapter 11 has no body. It declares one or more names that persist for the rest of the function:

```cpp
/// varstmt = "var" varbinding { "," varbinding } ;
static unique_ptr<ExprAST> ParseVarStmt() {
  getNextToken(); // eat 'var'
  vector<pair<string, unique_ptr<ExprAST>>> VarNames;

  while (true) {
    if (CurTok != tok_identifier)
      return LogError("Expected identifier after 'var'");

    string Name = IdentifierStr;
    getNextToken(); // eat identifier

    unique_ptr<ExprAST> Init;
    if (CurTok == '=') {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init) return nullptr;
    } else {
      Init = make_unique<NumberExprAST>(0.0); // default to 0.0
    }

    DeclareVar(Name); // register name in the current function scope
    VarNames.push_back({Name, std::move(Init)});

    if (CurTok != ',') break;
    getNextToken(); // eat ','
  }

  return make_unique<VarStmtAST>(std::move(VarNames));
}
```

The critical difference from chapter 10: no `:` and no body. `DeclareVar(Name)` tells the parser that `Name` is now in scope for the rest of the function — so later assignments to it will pass the `IsDeclaredVar` check.

## Return

`ParseReturnStmt` is straightforward:

```cpp
/// returnstmt = "return" expression ;
static unique_ptr<ExprAST> ParseReturnStmt() {
  getNextToken(); // eat 'return'
  auto Expr = ParseExpression();
  if (!Expr) return nullptr;
  return make_unique<ReturnExprAST>(std::move(Expr));
}
```

`ReturnExprAST::codegen` emits a real LLVM terminator — a `ret` instruction that ends the current basic block:

```cpp
Value *ReturnExprAST::codegen() {
  Value *RetVal = Expr->codegen();
  if (!RetVal) return nullptr;
  Builder->CreateRet(RetVal); // terminates the current basic block
  return RetVal;
}
```

## Block Codegen

`BlockExprAST::codegen` evaluates statements in order and returns the last value. It stops early if a `return` has already terminated the current block — statements after a `return` are unreachable:

```cpp
Value *BlockExprAST::codegen() {
  Value *Last = nullptr;
  for (auto &Stmt : Stmts) {
    // If a previous statement already emitted a terminator (e.g. 'return'),
    // skip the rest — we'd be emitting into a block with no successor.
    if (Builder->GetInsertBlock()->getTerminator())
      break;
    Last = Stmt->codegen();
    if (!Last) return nullptr;
  }
  if (!Last)
    return LogErrorV("Empty block");
  return Last;
}
```

## Var and Assignment Codegen

`VarStmtAST::codegen` allocates stack slots and initializes them, but does not restore any shadowed bindings. Variables declared with `var` live for the whole function:

```cpp
Value *VarStmtAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.first;
    ExprAST *Init = Var.second.get();

    // Reject duplicate declarations (caught at parse time too, but double-check).
    if (NamedValues.count(VarName))
      return LogErrorV(("Variable '" + VarName + "' already defined").c_str());

    Value *InitVal = Init->codegen();
    if (!InitVal) return nullptr;

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);
    NamedValues[VarName] = Alloca; // binding persists for the rest of the function
  }

  return ConstantFP::get(*TheContext, APFloat(0.0)); // var statement produces 0.0
}
```

`AssignmentExprAST::codegen` is unchanged from chapter 10 — it loads the alloca from `NamedValues`, stores the new value, and returns it.

## if as a Statement

Chapter 10 had `IfExprAST`, which always produced a value via a PHI node and required both `then` and `else` branches. Chapter 11 adds `IfStmtAST` — the statement form. It has two key differences:

1. **`else` is optional.** If there is no `else`, control falls through to the merge block.
2. **No PHI node.** The statement doesn't produce a value — it just controls which branch runs.

```cpp
Value *IfStmtAST::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV) return nullptr;

  // Convert condition double to i1: true if != 0.0.
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  BasicBlock *ThenBB  = BasicBlock::Create(*TheContext, "then",    TheFunction);
  BasicBlock *ElseBB  = BasicBlock::Create(*TheContext, "else",    TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont",  TheFunction);

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit the then branch.
  Builder->SetInsertPoint(ThenBB);
  if (!Then->codegen()) return nullptr;
  // If the then branch didn't already terminate (e.g. via 'return'), fall through.
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(MergeBB);

  // Emit the else branch (or just fall through if there is none).
  Builder->SetInsertPoint(ElseBB);
  if (Else) {
    if (!Else->codegen()) return nullptr;
  }
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(MergeBB);
  return ConstantFP::get(*TheContext, APFloat(0.0)); // statement produces 0.0
}
```

The `getTerminator()` check before each `CreateBr` is what makes `return` inside an `if` work correctly. If the `then` block already has a `ret`, we don't emit a second branch — that would be ill-formed IR.

## Implicit Return

In chapter 10, `FunctionAST::codegen` always emitted `CreateRet(RetVal)` unconditionally after `Body->codegen()` returned. That breaks now that `return` statements emit their own `ret` instructions.

Chapter 11 checks whether the current block already has a terminator before deciding whether to add one:

```cpp
// Step 4: codegen the body, verify, optimize — or erase on failure.
if (Value *RetVal = Body->codegen()) {
  // Only emit a return if the body didn't already terminate the block.
  // A 'return' statement in the body emits its own 'ret', so we don't
  // want to emit a second one.
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateRet(RetVal);
  verifyFunction(*TheFunction);
  TheFPM->run(*TheFunction, *TheFAM);
  return TheFunction;
}
```

This is what makes the following valid — the `if` path returns explicitly; the fall-through path gets an implicit `return 0.0`:

```python
def threshold(x):
    if x > 10: return x
    # no explicit return — implicit return 0.0 inserted by codegen
```

## After a Top-Level Block

When a `def` body ends with an indented block, the parser lands on the next top-level token — not a newline. Without special handling, `HandleDefinition` would treat that as a parse error. A flag tracks this:

```cpp
static bool LastTopLevelEndedWithBlock = false;

static void HandleDefinition() {
  auto FnAST = ParseDefinition();
  bool HasTrailing = (CurTok != tok_eol && CurTok != tok_eof);
  if (!FnAST || (HasTrailing && !LastTopLevelEndedWithBlock)) {
    // error recovery
  }
  // ...
}
```

This is what lets a file contain two top-level definitions back to back without a blank line between them.

## Things Worth Knowing

- `var` without an initializer defaults to `0.0`.
- Declaring the same variable twice in the same function is a codegen error.
- Assignment only works on variables that were declared with `var` or are function parameters. Undeclared assignments are rejected at parse time.
- `for` introduces a loop variable scoped to the loop body only. `var` bindings persist for the rest of the function.
- The inline body of a `def` accepts only a `simplestmt`. Compound statements (`if`, `for`) require an indented block.

## Try It

Simple function with multiple statements:

<!-- code-merge:start -->
```python
ready> def f(x):
    if x > 10: return 20
    return 10
```
```bash
Parsed a function definition.
```
```python
ready> f(5)
```
```bash
Parsed a top-level expression.
Evaluated to 10.000000
```
```python
ready> f(20)
```
```bash
Parsed a top-level expression.
Evaluated to 20.000000
```
<!-- code-merge:end -->

Accumulator loop — the chapter 10 workaround, now written naturally:

<!-- code-merge:start -->
```python
ready> def sum_to(n):
    var acc = 0
    for i = 1, i <= n, 1:
        acc = acc + i
    return acc
```
```bash
Parsed a function definition.
```
```python
ready> sum_to(5)
```
```bash
Parsed a top-level expression.
Evaluated to 15.000000
```
<!-- code-merge:end -->

## Build and Run

```bash
cd code/chapter-11
cmake -S . -B build && cmake --build build
./build/pyxc
```

## What's Next

Statement blocks unlock more control flow. In the next chapter we can add `while`, `break`, and `continue`, and build idiomatic examples without expression-level workarounds.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.

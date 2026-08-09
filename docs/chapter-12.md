---
description: "Switch from expression-only bodies to statement blocks with indentation, and make if/for/var/return statements."
---
# 12. pyxc: Statement Blocks

## Where We Are

[Chapter 11](chapter-11.md) added mutable variables, but the function body was still a single expression. The `var` form needed a `:` and a body expression, and `for` loops were expressions that produced `0.0`. This chapter introduces real statement blocks and indentation-sensitive syntax. After this chapter you'll be able to write code more naturally:

<!-- code-merge:start -->
```pyxc
ready> def sum_to(n):
    var acc = 0
    for var i = 1, i <= n, 1:
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
cd pyxc-llvm-tutorial/code/chapter-12
```

## Grammar

The central shift: `if`, `for`, `var`, and `return` move out of the expression grammar and become statements. Expressions are now purely value-producing.

**What changed or is new:**

```ebnf
(* changed: body is now a statement or indented block, not an expression *)
definition   = "def" prototype ":" ( simplestmt | eols block ) ;
decorateddef = binarydecorator eols "def" binaryopprototype ":" ( simplestmt | eols block )
             | unarydecorator  eols "def" unaryopprototype  ":" ( simplestmt | eols block ) ;

(* new: statement forms *)
ifstmt       = "if" expression ":" suite [ eols "else" ":" suite ] ;
forstmt      = "for" [ "var" ] identifier "=" expression "," expression "," expression ":" suite ;
varstmt      = "var" varbinding { "," varbinding } ;  (* no body — var is now a statement *)
assignstmt   = identifier "=" expression ;
returnstmt   = "return" expression ;
simplestmt   = returnstmt | varstmt | assignstmt | expression ;
compoundstmt = ifstmt | forstmt ;
statement    = simplestmt | compoundstmt ;
suite        = simplestmt | compoundstmt | eols block ;
stmtsep      = eols | BLOCK_END ;           (* new: what separates statements inside a block *)
block        = indent statement { stmtsep statement } dedent ;
indent       = INDENT ;
dedent       = DEDENT ;
INDENT       = ? synthetic token emitted by the lexer when indentation increases ? ;
DEDENT       = ? synthetic token emitted by the lexer when indentation decreases ? ;
BLOCK_END    = ? synthetic token injected by ParseBlock after it consumes DEDENT ? ;

(* simplified: var and assignment removed; if/for removed from primary *)
expression   = unaryexpr binoprhs ;
primary      = identifierexpr | numberexpr | parenexpr ;
```

- **`suite`** — what follows a `:`. Either a single statement on the same line, or a newline followed by an indented block.
- **`simplestmt`** — statements that fit on one line: `return`, `var`, assignment, or a bare expression.
- **`compoundstmt`** — statements that introduce a new suite: `if` and `for`.
- **`stmtsep`** — what separates two statements inside a block. Normally that's one or more newlines (`eols`). But when the first statement was itself a block (an `if` or `for` with an indented body), no newline follows — the `DEDENT` already consumed the line break. `BLOCK_END` covers that case; see below.
- **`block`** — an `INDENT` token, one or more statements separated by `stmtsep`, a `DEDENT` token.
- **`INDENT` / `DEDENT`** — tokens emitted by the lexer when indentation increases or decreases. One `INDENT` is emitted when a block opens, one `DEDENT` when it closes — not one per line. The parser sees them like matched parentheses:
- **`BLOCK_END`** — a synthetic token injected by `ParseBlock` into the token stream just before it returns. It signals "a nested block just closed here". The enclosing `ParseBlock` loop consumes it instead of expecting a newline, and any outer caller (like `HandleDefinition`) can check for it too. See the Parsing a Block section for details.

```pyxc
def f():
    var x = 5      # ← INDENT emitted here (indentation increased)
    x = x + 1      # ← nothing (same level)
    return x       # ← nothing (same level)
                   # ← DEDENT emitted here (indentation decreased)
```

**Full grammar** — [pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-12/pyxc.ebnf):

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = definition | decorateddef | external | toplevelexpr ;
definition      = "def" prototype ":" ( simplestmt | eols block ) ;
decorateddef    = binarydecorator eols "def" binaryopprototype ":" ( simplestmt | eols block )
                | unarydecorator  eols "def" unaryopprototype  ":" ( simplestmt | eols block ) ;
binarydecorator = "@" "binary" "(" integer ")" ;
unarydecorator  = "@" "unary" ;
binaryopprototype = customopchar "(" identifier "," identifier ")" ;
unaryopprototype  = customopchar "(" identifier ")" ;
external        = "extern" "def" prototype ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ identifier { "," identifier } ] ")" ;
ifstmt          = "if" expression ":" suite [ eols "else" ":" suite ] ;
forstmt         = "for" [ "var" ] identifier "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = identifier "=" expression ;
simplestmt      = returnstmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" expression ;
stmtsep         = eols | BLOCK_END ;
block           = indent statement { stmtsep statement } dedent ;
expression      = unaryexpr binoprhs ;
binoprhs        = { binaryop unaryexpr } ;
varbinding      = identifier [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | primary ;
unaryop         = "-" | userdefunaryop ;
primary         = identifierexpr | numberexpr | parenexpr ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
numberexpr      = number ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;
dedent          = DEDENT ;
INDENT          = ? synthetic token emitted by lexer when indentation increases ? ;
DEDENT          = ? synthetic token emitted by lexer when indentation decreases ? ;
BLOCK_END       = ? synthetic token injected into the stream by ParseBlock
                    immediately after it consumes DEDENT ? ;
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
```

**A side effect of this grammar change.** In [chapter 10](chapter-11.md), `var` was an expression with a body — `var x = 5 in x + 1`. The variable and the code that used it were a single syntactic unit, so the variable's lifetime was self-contained. Now that `var` is a free-standing statement, a variable declared in one statement could in principle be referenced in any later statement — including one compiled in a completely separate module.

That last part is the problem. In the REPL, each top-level input is compiled into its own throw-away module and immediately freed after evaluation. A `var` at the top level would need its storage to survive across module boundaries, which the current JIT design doesn't support. Chapter 13 fixes this properly — both for the REPL and for compiled executables.

## Statements vs Expressions

Before this chapter, `if`, `for`, and `var` were expressions — they produced a value and could be nested:

```pyxc
var acc = 0: for var i = 1, ...: acc = acc + i
```

Statements don't produce values — they *do* things. Once `if`, `for`, `var`, and `return` are statements, a function body becomes a flat list of them:

```pyxc
var acc = 0
for var i = 1, ...:
    acc = acc + i
return acc
```

`ParseExpression` no longer handles `var`, `if`, `for`, or assignment `=`. Those are all in `ParseStatement` and `ParseSimpleStmt`. Expressions are now purely value-producing — operators, calls, variable reads.

## New Tokens and AST Nodes

Three new token values are added to the lexer's enum:

```cpp
tok_indent    = -19, // synthetic: emitted by lexer when indentation increases
tok_dedent    = -20, // synthetic: emitted by lexer when indentation decreases
tok_block_end = -21, // synthetic: injected by ParseBlock after eating DEDENT
```

`tok_indent` and `tok_dedent` come from the lexer — they are pushed into `PendingTokens` when the lexer detects a change in indentation. `tok_block_end` never comes from the lexer. It is injected by `ParseBlock` into `PendingTokens` just before it returns, so the calling parser sees it as `CurTok`. It is a signal in the token stream, not a character in the source.

And three new AST node classes:

**`ReturnExprAST`** — a `return` statement:

```cpp
class ReturnExprAST : public ExprAST {
  unique_ptr<ExprAST> Expr;
public:
  ReturnExprAST(unique_ptr<ExprAST> Expr) : Expr(std::move(Expr)) {}
  Value *codegen() override;
};
```

**`BlockExprAST`** — a sequence of statements evaluated in order. If execution reaches the end without a `return`, the function implicitly returns `0.0`. Use an explicit `return` if you need a specific value:

```cpp
class BlockExprAST : public ExprAST {
  vector<unique_ptr<ExprAST>> Stmts;
public:
  BlockExprAST(vector<unique_ptr<ExprAST>> Stmts)
      : Stmts(std::move(Stmts)) {}
  Value *codegen() override;
};
```

**`VarStmtAST`** — the statement form of `var`. Unlike `VarExprAST` from [chapter 10](chapter-11.md), it has no body. Variables declared here persist for the rest of the function:

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

A single counter isn't enough to track indentation — nested blocks need to remember every level that was opened. When indentation drops, the lexer needs to know which level it's returning to, and how many blocks it's closing at once. That's why the lexer keeps an `IndentStack` and a pending-token queue.

At the start of each line it finds the indentation level, compares it to the top of the stack, and pushes `INDENT` or `DEDENT` tokens into the queue. When indentation drops by multiple levels in one step, one `DEDENT` is queued per level closed and the parser drains them one at a time:

```pyxc
def f(x):            # stack: [0]
    if x > 0:        # stack: [0, 4]        → INDENT
        if x > 10:   # stack: [0, 4, 8]     → INDENT
            return x # stack: [0, 4, 8, 12] → INDENT
    return 0         # col 4: three levels closed → DEDENT, DEDENT, DEDENT queued
                     # stack drains back to [0, 4]; parser sees them one at a time
```

Blocks are also automatically closed at end of file — no trailing blank line needed:

```pyxc
def f():
    var x = 5        # stack: [0, 4] → INDENT
    return x         # stack: [0, 4]   nothing
# EOF                # col 0: stack has [0, 4] → DEDENT pushed into PendingTokens
                     # parser drains it on the next getNextToken() call
```

```cpp
static vector<int> IndentStack = {0}; // starts at column 0
static deque<int>  PendingTokens;     // buffered tokens the parser hasn't seen yet
static bool AtLineStart = true;       // true right after a newline
```

Inside `gettok()`, before any normal token logic, the indentation is processed in three steps.

**Step 1: Find the indentation level of the current line.**

```cpp
if (AtLineStart) {
  int IndentCol = 0;
  while (LastChar == ' ' || LastChar == '\t') {
    IndentCol += (LastChar == ' ') ? 1 : (8 - IndentCol % 8); // tabs → columns
    LastChar = advance();
  }
```

Spaces contribute 1 column each. Tabs advance to the next multiple of 8 — the delta is `8 - (IndentCol % 8)`:

| IndentCol before tab | IndentCol % 8 | delta | IndentCol after tab |
|---|---|---|---|
| 0 | 0 | 8 | 8 |
| 1 | 1 | 7 | 8 |
| 7 | 7 | 1 | 8 |
| 8 | 0 | 8 | 16 |
| 11 | 3 | 5 | 16 |

A tab always snaps forward to the next 8-column boundary, never backward and never past it.

**Step 2: Compare to the top of the stack and queue INDENT or DEDENT tokens.**

```cpp
  if (IndentCol > IndentStack.back()) {
    // More indented → push one INDENT.
    IndentStack.push_back(IndentCol);
    PendingTokens.push_back(tok_indent);
  } else if (IndentCol < IndentStack.back()) {
    // Less indented → push one DEDENT per level closed.
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
```

A single dedent can push multiple `DEDENT` tokens — one for each level that closed. Each time the parser calls `gettok()`, `PendingTokens` is drained first; only when it is empty does the lexer go looking for the next real token.

**Step 3: Drain the queue — return the first pending token if any.**

```cpp
  AtLineStart = false;
  if (!PendingTokens.empty()) {
    int Tok = PendingTokens.front();
    PendingTokens.pop_front();
    return Tok;
  }
}
```

`gettok()` is called again for each subsequent token, draining the queue one entry at a time before returning to normal lexing.

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

## Pyxc Indentation Rules

These are similar to Python's indentation rules, with one difference: Pyxc allows mixing tabs and spaces (Python 3 disallows it).

- Each space advances one column; each tab advances to the next multiple of 8.
- Mixing tabs and spaces is allowed — the column count is what matters.
- Dedenting to a column that was never opened is an error.
- Blank lines and comment-only lines do not affect indentation in file mode. In REPL mode, a blank line closes the current block immediately.
- A block opens after `:` followed by a newline and a deeper indentation level.

## Parse-Time Variable Tracking

Assignment to an undeclared variable is a parse-time error:

```pyxc
ready> x = 1
Error: Assignment to undeclared variable
```

To detect this, the parser maintains a scope stack of declared variable names. `var` declarations register names; `for var` loops introduce a loop variable into a temporary inner scope:

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
    VarScopes.back().insert(Name); // declare in the innermost (current) scope
}

static bool IsDeclaredInCurrentScope(const string &Name) {
  if (VarScopes.empty()) return false;
  return VarScopes.back().count(Name) > 0;
}

static void BeginBlockScope() { VarScopes.emplace_back(); }
static void EndBlockScope() {
  if (VarScopes.size() > 1) VarScopes.pop_back();
}

static bool IsDeclaredVar(const string &Name) {
  for (auto It = VarScopes.rbegin(); It != VarScopes.rend(); ++It)
    if (It->count(Name)) return true;
  return false;
}
```

Each scope guard is a small C++ struct. The constructor opens the scope; the destructor closes it. When the guard variable goes out of scope — at the end of a block, or when an early return is hit — the scope closes automatically without any explicit cleanup calls:

```cpp
struct FunctionScopeGuard {
  FunctionScopeGuard(const vector<string> &Args) { BeginFunctionScope(Args); }
  ~FunctionScopeGuard() { EndFunctionScope(); }
};

struct LoopScopeGuard {
  LoopScopeGuard(const string &Name) { EnterLoopScope(Name); }
  ~LoopScopeGuard() { ExitLoopScope(); }
};

struct BlockScopeGuard {
  BlockScopeGuard()  { BeginBlockScope(); }
  ~BlockScopeGuard() { EndBlockScope(); }
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

`ParseForStmt` creates a `LoopScopeGuard` only when the loop introduces a new
variable with `var`. Without `var`, the loop reuses an existing variable and
errors if it is undeclared.

```cpp
string VarName = IdentifierStr;
getNextToken(); // eat identifier
LoopScopeGuard LoopScope(VarName); // only when "var" is present
```

## Parsing a Suite

After every `:`, the parser calls `ParseSuite`. A suite is either an inline statement or an indented block:

```cpp
/// suite = simplestmt | compoundstmt | eols block ;
static unique_ptr<ExprAST> ParseSuite() {
  if (CurTok == tok_eol) {
    // Newline after ':' → expect an indented block.
    consumeNewlines();
    if (CurTok != tok_indent)
      return LogErrorExpression("Expected an indented block");
    return ParseBlock(); // CurTok = tok_block_end on return
  }
  if (CurTok == tok_indent)
    return ParseBlock(); // CurTok = tok_block_end on return
  // Same line after ':' → parse an inline statement.
  return ParseStatement();
}
```

When `ParseSuite` delegates to `ParseBlock`, it returns exactly what `ParseBlock` returns, with `CurTok = tok_block_end`. The caller can inspect `CurTok` to know whether the suite ended with a block.

`ParseIfStmt` and `ParseForStmt` both call `ParseSuite` after eating `:`. A `def` body works slightly differently — the inline form only accepts a `simplestmt`, not a compound statement. You cannot write `def f(x): if x > 0: return 1` on one line.

## Parsing a Block

`ParseBlock` consumes `INDENT`, reads statements separated by `stmtsep` until `DEDENT`, injects `tok_block_end`, and returns:

```cpp
/// block = INDENT statement { stmtsep statement } DEDENT ;
static unique_ptr<ExprAST> ParseBlock() {
  if (CurTok != tok_indent)
    return LogErrorExpression("Expected an indented block");
  getNextToken(); // eat INDENT

  BlockScopeGuard Scope; // each block gets its own var scope

  // Parse the first statement (required — empty blocks are not allowed).
  auto First = ParseStatement();
  if (!First) return nullptr;
  vector<unique_ptr<ExprAST>> Stmts;
  Stmts.push_back(std::move(First));

  while (true) {
    if (CurTok == tok_eol) {
      consumeNewlines();        // stmtsep = eols
      if (CurTok == tok_dedent) break;
    } else if (CurTok == tok_block_end) {
      // stmtsep = BLOCK_END: a nested block just closed.
      // No tok_eol follows — consume the marker and continue.
      getNextToken();
      if (CurTok == tok_dedent) break;
    } else if (CurTok == tok_dedent) {
      break;
    } else {
      return LogErrorExpression("Expected newline or end of block");
    }

    auto Stmt = ParseStatement();
    if (!Stmt) return nullptr;
    Stmts.push_back(std::move(Stmt));
  }

  // Inject tok_block_end before advancing past DEDENT so callers see it
  // as CurTok on return, removing the need for any boolean flag.
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // → CurTok = tok_block_end (DEDENT is overwritten and consumed)
  return make_unique<BlockExprAST>(std::move(Stmts));
}
```

The last three lines are the key. When the loop breaks on `tok_dedent`, `CurTok` holds the DEDENT token. I push `tok_block_end` to the front of `PendingTokens` and call `getNextToken()`. That call pops `tok_block_end` from `PendingTokens` and overwrites `CurTok` — the DEDENT is quietly consumed in the process, and `ParseBlock` returns with `CurTok = tok_block_end`.

Every caller that previously needed a boolean "did this suite end with a block?" can now just check `CurTok == tok_block_end`.

## BLOCK_END and the else Problem

`tok_block_end` flows cleanly through most of the parser — `ParseBlock`'s loop consumes it and keeps going, `HandleDefinition` checks for it instead of checking for `tok_eol`. One case is trickier: `if` with an optional `else`.

After `ParseSuite` returns the then-branch, `CurTok` might be `tok_block_end` (if the then was a block) or `tok_eol` (if the then was inline, e.g. `if cond: return 1`). Either way, `else` — if present — lives on the very next line at the same indentation level, right where that separator token is sitting. `ParseIfStmt` needs to look past it to check.

The approach: consume the separator temporarily to peek at what follows. If it's `else`, great — parse the else branch normally. If it's not, re-inject the separator so the enclosing `ParseBlock` loop still sees it.

```cpp
unique_ptr<ExprAST> Then = ParseSuite();
if (!Then) return nullptr;

bool ThenWasBlock = (CurTok == tok_block_end);
if (ThenWasBlock)
  getNextToken(); // consume tok_block_end → CurTok = next real token

// If Then was inline, CurTok is tok_eol right now (unless there was no
// trailing newline at all). Remember that so it can be restored below.
bool ThenHadTrailingEol = (CurTok == tok_eol);

consumeNewlines(); // skip any blank lines before 'else'

unique_ptr<ExprAST> Else;
if (CurTok == tok_else) {
  getNextToken(); // eat 'else'
  if (CurTok != ':') return LogErrorExpression("Expected ':' after else");
  getNextToken(); // eat ':'
  Else = ParseSuite();
  if (!Else) return nullptr;
} else if (ThenWasBlock) {
  // No else. Re-inject tok_block_end so the enclosing block sees it.
  // Save the token we already advanced to — it must not be lost.
  PendingTokens.push_front(CurTok); // push current lookahead back
  CurTok = tok_block_end;           // restore the signal directly
} else if (ThenHadTrailingEol) {
  // No else, and Then was inline. consumeNewlines() above swallowed the
  // newline that separates this statement from the next one — restore it.
  PendingTokens.push_front(CurTok); // push current lookahead back
  CurTok = tok_eol;                 // restore the separator directly
}
```

The critical detail is the last three lines of each `else if` branch. After `getNextToken()` consumed `tok_block_end` (or after `consumeNewlines()` consumed a real `tok_eol`), a real token — say, `tok_return`, or the next `tok_dedent` — landed in `CurTok`. If I naively pushed the separator to `PendingTokens` and called `getNextToken()` again, that new call would pop the separator right back out — and the token already sitting in `CurTok` would be **overwritten and lost**. The statement after the `if` would parse incorrectly, or the outer block would close at the wrong point.

Instead: push the current `CurTok` to `PendingTokens`, then set `CurTok` to the separator directly without calling `getNextToken()`. The saved token is now first in `PendingTokens`; the next `getNextToken()` call anywhere upstream will retrieve it correctly.

I found this the hard way: my first pass only handled the `ThenWasBlock` case, on the assumption that an inline `then` always ends cleanly at a `tok_eol` that nothing downstream would touch. That's true in isolation — but `consumeNewlines()` a few lines later doesn't know or care whether it's looking at a "real" separator or one it's about to strand a statement without. It consumes any `tok_eol` in front of it while probing for `else`, block or no block. Miss the inline case and `if x > 10: return 20` parses fine as the *last* statement in a block, but breaks the moment another statement follows it at the same indentation — the newline that `ParseBlock`'s loop needed as a separator is gone, and it sees the next statement's leading token where it expected `tok_eol`, `tok_dedent`, or `tok_block_end`. `ThenHadTrailingEol` closes that gap the same way `ThenWasBlock` already did for the block case.

## Parsing Statements

`ParseStatement` dispatches to compound or simple statement parsers:

```cpp
/// statement = simplestmt | compoundstmt ;
static unique_ptr<ExprAST> ParseStatement() {
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

  // Fast path: if the current token is an identifier, peek at what follows
  // before committing to a full expression parse. This lets me detect
  // "x = expr" (assignment) without going through ParseExpression first.
  if (CurTok == tok_identifier) {
    string Name = IdentifierStr;
    getNextToken(); // eat identifier

    if (CurTok == '=') {
      if (!IsDeclaredVar(Name))
        return LogErrorExpression("Assignment to undeclared variable");
      getNextToken(); // eat '='
      auto RHS = ParseExpression();
      if (!RHS) return nullptr;
      return make_unique<AssignmentExprAST>(Name, std::move(RHS));
    }

    // Not an assignment — parse the rest as an expression.
    auto Expr = ParseIdentifierExprWithName(std::move(Name));
    if (!Expr) return nullptr;
    return ParseBinOpRHS(0, std::move(Expr));
  }

  // Non-identifier start: parse a full expression, then check for '='.
  auto Expr = ParseExpression();
  if (!Expr) return nullptr;

  if (CurTok != '=')
    return Expr; // bare expression statement

  const string *AssignedName = Expr->getLValueName();
  if (!AssignedName)
    return LogErrorExpression("Destination of '=' must be a variable");

  string Name = *AssignedName;
  if (!IsDeclaredVar(Name))
    return LogErrorExpression("Assignment to undeclared variable");

  getNextToken(); // eat '='
  auto RHS = ParseExpression();
  if (!RHS) return nullptr;
  return make_unique<AssignmentExprAST>(Name, std::move(RHS));
}
```

Assignment to an undeclared variable is rejected at parse time via `IsDeclaredVar` — no codegen is needed to catch it.

## Parsing Var as a Statement

`var` in chapter 11 has no body. It declares one or more names that persist for the rest of the function:

```cpp
/// varstmt = "var" varbinding { "," varbinding } ;
static unique_ptr<ExprAST> ParseVarStmt() {
  getNextToken(); // eat 'var'
  vector<pair<string, unique_ptr<ExprAST>>> VarNames;

  while (true) {
    if (CurTok != tok_identifier)
      return LogErrorExpression("Expected identifier after 'var'");

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

    DeclareVar(Name); // register name in the current block scope
    VarNames.push_back({Name, std::move(Init)});

    if (CurTok != ',') break;
    getNextToken(); // eat ','
  }

  return make_unique<VarStmtAST>(std::move(VarNames));
}
```

The critical difference from [chapter 10](chapter-11.md): no `:` and no body. `DeclareVar(Name)` registers `Name` in the current block scope — so later assignments to it will pass the `IsDeclaredVar` check. If the `var` is inside an `if` or `for` block, that name is only visible inside that block.

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

`BlockExprAST::codegen` evaluates statements in order. It stops early if a `return` has already terminated the current block — statements after a `return` are unreachable. It also saves and restores `NamedValues` around the block body so that variables declared inside the block with `var` don't leak to the outer scope:

```cpp
Value *BlockExprAST::codegen() {
  auto SavedBindings = NamedValues; // snapshot outer bindings
  Value *Last = nullptr;
  for (auto &Stmt : Stmts) {
    // If a previous statement already emitted a terminator (e.g. 'return'),
    // skip the rest — I'd be emitting into a block with no successor.
    if (Builder->GetInsertBlock()->getTerminator()) break;
    Last = Stmt->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      return nullptr;
    }
  }
  NamedValues = SavedBindings; // restore outer bindings when block exits
  if (!Last)
    return LogErrorV("Empty block");
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

## Var and Assignment Codegen

`VarStmtAST::codegen` allocates stack slots and initializes them. Duplicate declarations in the same scope are caught at parse time, so codegen just sets up the alloca and records the binding:

```cpp
Value *VarStmtAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.first;
    ExprAST *Init = Var.second.get();

    Value *InitVal = Init->codegen();
    if (!InitVal) return nullptr;

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);
    NamedValues[VarName] = Alloca;
  }

  return ConstantFP::get(*TheContext, APFloat(0.0)); // var statement produces 0.0
}
```

`AssignmentExprAST::codegen` is unchanged from [chapter 10](chapter-11.md) — it loads the alloca from `NamedValues`, stores the new value, and returns it.

## if as a Statement

[Chapter 11](chapter-11.md) had `IfExprAST`, which always produced a value via a PHI node and required both `then` and `else` branches. Chapter 12 adds `IfStmtAST` — the statement form. The condition check, basic block creation, and branch structure are identical to [chapter 10](chapter-11.md). Two things change:

1. **`else` is optional.** If there is no `else`, the else block just falls through to merge.
2. **No PHI node.** The statement doesn't produce a value.

```cpp
  // Emit the then branch.
  Builder->SetInsertPoint(ThenBB);
  if (!Then->codegen()) return nullptr;
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(MergeBB);

  // Emit the else branch — skipped entirely if there is no else.
  Builder->SetInsertPoint(ElseBB);
  if (Else) {
    if (!Else->codegen()) return nullptr;
  }
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(MergeBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
  // No PHI node — statements don't produce values.
```

The `getTerminator()` check before each `CreateBr` is what makes `return` inside an `if` work correctly. If the `then` block already has a `ret`, I don't emit a second branch — that would be ill-formed IR.

## Implicit Return

In [chapter 10](chapter-11.md), `FunctionAST::codegen` always emitted `CreateRet(RetVal)` unconditionally after `Body->codegen()` returned. That breaks now that `return` statements emit their own `ret` instructions.

Chapter 12 checks whether the current block already has a terminator before deciding whether to add one:

```cpp
// Step 4: codegen the body, verify, optimize — or erase on failure.
if (Value *RetVal = Body->codegen()) {
  // Only emit a return if the body didn't already terminate the block.
  // A 'return' statement in the body emits its own 'ret', so I don't
  // want to emit a second one.
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateRet(RetVal);
  verifyFunction(*TheFunction);
  TheFPM->run(*TheFunction, *TheFAM);
  return TheFunction;
}
```

This is what makes the following valid — the `if` path returns explicitly; the fall-through path gets an implicit `return 0.0`:

```pyxc
def threshold(x):
    if x > 10: return x
    # no explicit return — implicit return 0.0 inserted by codegen
```

## After a Top-Level Block

When a `def` body ends with an indented block, `ParseDefinition` returns with `CurTok = tok_block_end`. The `MainLoop` and `HandleDefinition` check for it explicitly:

```cpp
// In MainLoop:
if (CurTok == tok_block_end) {
  getNextToken(); // consume the marker; next token starts the next definition
  continue;
}
```

```cpp
// In HandleDefinition:
bool HasTrailing = (CurTok != tok_eol && CurTok != tok_eof &&
                    CurTok != tok_block_end);
if (!FnAST || HasTrailing) {
  // error recovery
}
```

No boolean flag needed. `tok_block_end` in `CurTok` is the signal — it's the same mechanism that `ParseBlock`'s own loop uses. Two definitions back to back with no blank line between them work correctly because `MainLoop` eats the `tok_block_end` before dispatching the next top-level form.

## Things Worth Knowing

- `var` without an initializer defaults to `0.0`.
- `var` is block-scoped. A variable declared inside an `if` or `for` block is not visible after that block exits. An outer variable with the same name is shadowed inside the block and restored when the block exits.
- Declaring the same variable twice in the same block is a parse-time error.
- Assignment only works on variables that were declared with `var` or are function parameters. Undeclared assignments are rejected at parse time.
- `for var` introduces a loop variable scoped to the loop body only.
- The inline body of a `def` accepts only a `simplestmt`. Compound statements (`if`, `for`) require an indented block.

## Known Limitations

**No global variables.** `var` is only valid inside a function body. `ParseTopLevelExpr` calls `ParseExpression`, so `var x = 10` at the top level is a parse error. Each top-level expression also gets its own fresh function scope, so there is no way to declare a variable on one REPL line and reference it on the next.

This is the main practical limitation of the current chapter. In the REPL it means you cannot build up state across lines:

```pyxc
# Does not work in the REPL:
var x = 10      # parse error — var is not an expression
x = x + 10     # x is undeclared in this expression's scope
printd(x)
```

For now, keep mutable state inside a function:

```pyxc
def f():
    var x = 10
    x = x + 10
    return x

printd(f())   # prints 20.000000
```

Chapter 13 addresses this properly. When compiling to an executable, all top-level statements are collected into a synthesized `main()`, so `var` declarations and assignments at the top level work naturally. Full REPL support for global state requires additional runtime infrastructure and is also covered in chapter 12.

## Try It

Simple function with multiple statements:

<!-- code-merge:start -->
```pyxc
ready> def f(x):
    if x > 10: return 20
    return 10
```
```bash
Parsed a function definition.
```
```pyxc
ready> f(5)
```
```bash
Parsed a top-level expression.
Evaluated to 10.000000
```
```pyxc
ready> f(20)
```
```bash
Parsed a top-level expression.
Evaluated to 20.000000
```
<!-- code-merge:end -->

Accumulator loop — the [chapter 10](chapter-11.md) workaround, now written naturally:

<!-- code-merge:start -->
```pyxc
ready> def sum_to(n):
    var acc = 0
    for var i = 1, i <= n, 1:
        acc = acc + i
    return acc
```
```bash
Parsed a function definition.
```
```pyxc
ready> sum_to(5)
```
```bash
Parsed a top-level expression.
Evaluated to 15.000000
```
<!-- code-merge:end -->

## Build and Run

```bash
cd code/chapter-12
cmake -S . -B build && cmake --build build
./build/pyxc
```

## What's Next

Chapter 13 resolves the global variable limitation described in Known Limitations. For compiled programs, all top-level statements are collected into a synthesized `main()` so `var` declarations and assignments work naturally. For the REPL, a persistent variable store backed by a runtime helper lets state survive across JIT module boundaries. Once globals work in both modes, chapter 13 emits object files and chapter 14 links them into native executables — turning Pyxc from a JIT toy into a real compiler.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.

---
description: "Switch from expression-only bodies to statement blocks with indentation, and make if/for/var/return statements."
---
# 12. pyxc: Statement Blocks

## What I Am Building

[Chapter 11](chapter-11.md) added mutable variables, but a function body was still a single expression. `var` needed a `:` and a body expression, and `for` loops were expressions that produced `0.0`. I introduce real statement blocks and indentation-sensitive syntax, so I can write code the way I actually want to:

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

The central shift: `if`, `for`, `var`, and (new) `return` move out of the expression grammar and become statements. Expressions go back to being purely value-producing — I add `statement`, `simple-statement`, `compound-statement`, `suite`, and `block` to hold them, plus `indent`/`dedent`/`INDENT`/`DEDENT`/`BLOCK_END` for the indentation machinery:

`code/chapter-12/pyxc.ebnf`

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | external
                                     | top-level-expression ;
-function-definition               = "def" function-signature ":"
-                                    [ end-of-lines ] expression ;
+function-definition               = "def" function-signature ":"
+                                    ( simple-statement
+                                      | end-of-lines block ) ;
 external                          = "extern" "def" function-signature ;
 top-level-expression              = expression ;
 function-signature                = name "(" [ parameters ] ")" ;
 parameters                        = parameter { "," parameter } ;
 parameter                         = name ;
-expression                        = variable-expression
-                                    | comparison [ "=" expression ] ;
-variable-expression               = "var" variable-binding
-                                    { "," variable-binding } ":"
-                                    [ end-of-lines ] expression ;
-variable-binding                  = name [ "=" expression ] ;
+if-statement                      = "if" expression ":" suite
+                                    [ [ end-of-lines ] "else" ":" suite ] ;
+for-statement                     = "for" [ "var" ] name "=" expression ","
+                                    expression "," expression ":" suite ;
+variable-statement                = "var" variable-binding
+                                    { "," variable-binding } ;
+assignment-statement              = lvalue "=" expression ;
+simple-statement                  = return-statement
+                                    | variable-statement
+                                    | assignment-statement
+                                    | expression ;
+compound-statement                = if-statement | for-statement ;
+statement                         = simple-statement | compound-statement ;
+suite                             = simple-statement
+                                    | compound-statement
+                                    | end-of-lines block ;
+return-statement                  = "return" expression ;
+statement-separator               = end-of-lines | BLOCK_END ;
+block                             = indent statement
+                                    { statement-separator statement } dedent ;
+expression                        = comparison ;
 comparison                        = sum { comparison-operator sum } ;
 comparison-operator               = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum                               = term { ("+" | "-") term } ;
 term                              = factor { ("*" | "/" | "%") factor } ;
+lvalue                            = name ;
+variable-binding                  = name [ "=" expression ] ;
 factor                            = "-" factor | primary ;
 primary                           = name-expression
                                     | number-expression
-                                    | parenthesized-expression
-                                    | if-expression
-                                    | for-expression ;
-if-expression                     = "if" expression ":"
-                                    [ end-of-lines ] expression
-                                    [ end-of-lines ] "else" ":"
-                                    [ end-of-lines ] expression ;
-for-expression                    = "for" [ "var" ] name "=" expression ","
-                                    expression "," expression ":"
-                                    [ end-of-lines ] expression ;
-name-expression                   = name
-                                    | call-expression ;
+                                    | parenthesized-expression ;
+name-expression                   = name | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
+indent                            = INDENT ;
+dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 number                            = digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 whitespace                        = " " | "\t" | "\v" | "\f" ;
+INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
+DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
+BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
+                                      immediately after it consumes DEDENT ? ;
```

- **`suite`** — what follows a `:`. Either a single statement on the same line, or a newline followed by an indented block.
- **`simple-statement`** — statements that fit on one line: `return`, `var`, assignment, or a bare expression.
- **`compound-statement`** — statements that introduce a new suite: `if` and `for`.
- **`statement-separator`** — what separates two statements inside a block. Normally that's one or more newlines. But when the first statement was itself a block (an `if` or `for` with an indented body), no newline follows — the `DEDENT` already consumed the line break. `BLOCK_END` covers that case; see below.
- **`block`** — an `INDENT` token, one or more statements separated by `statement-separator`, a `DEDENT` token.
- **`INDENT` / `DEDENT`** — tokens emitted by the lexer when indentation increases or decreases. One `INDENT` is emitted when a block opens, one `DEDENT` when it closes — not one per line.
- **`BLOCK_END`** — a synthetic token `ParseBlock` injects into the token stream just before it returns. It signals "a nested block just closed here." The enclosing `ParseBlock` loop consumes it instead of expecting a newline, and any outer caller can check for it too.

```pyxc
def f():
    var x = 5      # ← INDENT emitted here (indentation increased)
    x = x + 1      # ← nothing (same level)
    return x       # ← nothing (same level)
                   # ← DEDENT emitted here (indentation decreased)
```

**A side effect of this grammar change.** In [chapter 11](chapter-11.md), `var` was an expression with a body — `var x = 5: x + 1`. The variable and the code that used it were one syntactic unit, so its lifetime was self-contained. Now that `var` is a free-standing statement, a variable declared in one statement could in principle be referenced in any later statement — including one compiled in a completely separate module.

That's the problem. In the REPL, each top-level input compiles into its own throw-away module, freed right after evaluation. A `var` at the top level would need its storage to survive across module boundaries, which the current JIT design doesn't support yet. [Chapter 15](chapter-15.md) fixes this properly, for both the REPL and compiled executables.

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

`ParseExpression` no longer handles `var`, `if`, `for`, or assignment. Those all live in `ParseStatement` and `ParseSimpleStatement` now. Expressions are purely value-producing again — operators, calls, variable reads.

## New Tokens and AST Nodes

Three new token values:

```cppdiff
*enum Token {
*  ...
*  // mutable variables
*  tok_var = -18,
*
+  // indentation
+  tok_indent    = -19,
+  tok_dedent    = -20,
+  tok_block_end = -21, // synthetic: injected by ParseBlock after eating DEDENT
*
*  // punctuation and operators
*  ...
*};
```

`tok_indent` and `tok_dedent` come from the lexer — I push them into `PendingTokens` when I detect a change in indentation. `tok_block_end` never comes from the lexer; `ParseBlock` injects it into `PendingTokens` just before returning, so the calling parser sees it as `CurrentToken`. It's a signal in the token stream, not a character in the source.

Three new AST node classes:

`ReturnStatementNode` — a `return` statement:

```cpp
/// ReturnStatementNode - Statement-like expression for return.
/// Emits a function return and produces the returned value.
class ReturnStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Expr;

public:
  ReturnStatementNode(unique_ptr<ExpressionNode> Expr) : Expr(std::move(Expr)) {}
  Value *codegen() override;
};
```

`BlockStatementNode` — a sequence of statements evaluated in order:

```cpp
/// BlockStatementNode - A sequence of statements evaluated in order.
/// The block's value is the value of the last statement executed.
class BlockStatementNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Stmts;

public:
  BlockStatementNode(vector<unique_ptr<ExpressionNode>> Stmts) : Stmts(std::move(Stmts)) {}
  Value *codegen() override;
};
```

`VarStatementNode` replaces [chapter 11](chapter-11.md)'s `VariableExpressionNode` and drops the body entirely — no more `: expression` tail. Variables declared here persist for the rest of the function, not just one expression:

```cpp
/// VarStatementNode - Statement form of mutable local variable bindings.
///   var a = <init>, b = <init>
/// Each binding allocates stack storage in the current function's entry block
/// and stores its initializer. Bindings persist for the rest of the function.
class VarStatementNode : public ExpressionNode {
  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;

public:
  VarStatementNode(vector<pair<string, unique_ptr<ExpressionNode>>> VarNames)
      : VarNames(std::move(VarNames)) {}
  Value *codegen() override;
};
```

`IfStatementNode` replaces [chapter 10](chapter-10.md)'s `IfExpressionNode`. Since a statement doesn't need to produce a value, the new version drops the PHI node entirely, and `else` becomes optional — a real behavior change, not just a rename.

## INDENT and DEDENT

A single counter isn't enough to track indentation — nested blocks need to remember every level that was opened. When indentation drops, the lexer needs to know which level it's returning to, and how many blocks it's closing at once. That's why the lexer keeps an `IndentStack` and a pending-token queue.

At the start of each line, I find the indentation level, compare it to the top of the stack, and push `INDENT` or `DEDENT` tokens into the queue. When indentation drops by multiple levels in one step, one `DEDENT` is queued per level closed and the parser drains them one at a time:

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

Inside `getToken()`, before any normal token logic, indentation is processed at the start of every line.

**Step 1: Find the indentation level of the current line.**

```cpp
if (AtLineStart) {
  // Prime sentinel space once so indentation scans real input.
  if (LastChar == ' ')
    LastChar = advance();
  int CurrentIndentRead = 0;
  while (LastChar == ' ' || LastChar == '\t') {
    CurrentIndentRead +=
        (LastChar == ' ')
            ? 1
            : (IndentTabWidth - CurrentIndentRead % IndentTabWidth);
    LastChar = advance();
  }
```

`IndentTabWidth` is `8`. Spaces contribute 1 column each. Tabs advance to the next multiple of `IndentTabWidth` — the delta is `IndentTabWidth - (CurrentIndentRead % IndentTabWidth)`:

| column before tab | column % 8 | delta | column after tab |
|---|---|---|---|
| 0 | 0 | 8 | 8 |
| 1 | 1 | 7 | 8 |
| 7 | 7 | 1 | 8 |
| 8 | 0 | 8 | 16 |
| 11 | 3 | 5 | 16 |

A tab always snaps forward to the next 8-column boundary, never backward and never past it.

Blank lines and comment-only lines return `tok_eol` here without touching the indent stack (a blank line closes the current block immediately in the REPL instead), and EOF falls through to the flush logic further below. Real source content instead falls through to Step 2.

**Step 2: Compare to the top of the stack and either return `tok_indent` directly or queue `DEDENT` tokens.**

```cpp
  CurrentTokenLocation = LexerLocation;
  int CurrentIndentOnStack = IndentStack.back();
  if (CurrentIndentRead > CurrentIndentOnStack) {
    IndentStack.push_back(CurrentIndentRead);
    AtLineStart = false;
    return tok_indent;
  }
  if (CurrentIndentRead < CurrentIndentOnStack) {
    while (IndentStack.size() > 1 && CurrentIndentRead < IndentStack.back()) {
      IndentStack.pop_back();
      PendingTokens.push_back(tok_dedent);
    }
    if (CurrentIndentRead != IndentStack.back()) {
      LogErrorAtLoc("inconsistent indentation", CurrentTokenLocation);
      PrintErrorSourceContext(CurrentTokenLocation);
      return tok_error;
    }
    AtLineStart = false;
    int Tok = PendingTokens.front();
    PendingTokens.pop_front();
    return Tok;
  }
  // Same indentation level — no indent/dedent token needed.
  AtLineStart = false;
```

An increased indentation never goes through `PendingTokens` at all — it returns `tok_indent` immediately. A decreased indentation can close several levels at once; each closed level pushes one `tok_dedent` onto `PendingTokens`, and this call returns the first one. The rest drain on later calls to `getToken()`, through the check at the very top of the function (see Step 3 below). A single dedent can therefore produce multiple `DEDENT` tokens — one for each level that closed:

<!-- code-merge:start -->
```pyxc
ready> def f():
    var x = 1
   var y = 2
```
```text
Error (Line 3, Column 4): inconsistent indentation
   v
   ^~~~
   v
   ^~~~
Error (Line 3, Column 4): unknown token when expecting an expression
   v
   ^~~~
Error (Line 3, Column 4): unknown token when expecting an expression
   var 
   ^~~~
```
<!-- code-merge:end -->

Dedenting to column 3 has no match on the stack (`[0, 4]`) — it's neither the current indentation nor an outer one, so there's no consistent level to return to. `getToken()` reports it and returns `tok_error`, but the lexer state doesn't stop there — the parser keeps trying to recover from the malformed token stream, which is why one bad indent produces several cascading error lines instead of just the first one.

**Step 3: On the next call, drain the rest of the queue before doing anything else.**

Any `DEDENT` tokens left over from a multi-level dedent, or from Step 2's queuing, sit in `PendingTokens` for the next call to consume — a check right at the top of `getToken()`, before the line-start logic even runs:

```cpp
static int getToken() {
  static int LastChar = ' ';

  // Drain tokens queued by a multi-level dedent on the previous line.
  if (!PendingTokens.empty()) {
    int Tok = PendingTokens.front();
    PendingTokens.pop_front();
    return Tok;
  }
```

`getToken()` is called again for each subsequent token, draining the queue one entry at a time before returning to normal lexing.

At EOF, the lexer flushes one `DEDENT` per still-open block (still inside the `if (AtLineStart)` block from Step 1):

```cpp
if (LastChar == EOF) {
  if (IndentStack.size() > 1) {
    IndentStack.pop_back();
    return tok_dedent;
  }
  return tok_eof;
}
```

In REPL mode, a blank line ends the current indented block immediately — the same behavior as the Python REPL.

## Pyxc Indentation Rules

Similar to Python's, with one difference: pyxc allows mixing tabs and spaces (Python 3 disallows it).

- Each space advances one column; each tab advances to the next multiple of 8.
- Mixing tabs and spaces is allowed — the column count is what matters.
- Dedenting to a column that was never opened is an error.
- Blank lines and comment-only lines don't affect indentation in file mode. In REPL mode, a blank line closes the current block immediately.
- A block opens after `:` followed by a newline and a deeper indentation level.

## Parse-Time Variable Tracking

Assignment to an undeclared variable is a parse-time error:

<!-- code-merge:start -->
```pyxc
ready> x = 1
```
```text
Error (Line 1, Column 3): Assignment to undeclared variable
x = 
  ^~~~
```
<!-- code-merge:end -->

To catch this, the parser keeps a scope stack of declared variable names — a function scope at the bottom, plus one nested scope per block:

```cpp
static vector<set<string>> VarScopes;

static void BeginFunctionScope(const vector<string> &Parameters) {
  VarScopes.clear();
  VarScopes.emplace_back();
  for (const auto &Parameter : Parameters)
    VarScopes.front().insert(Parameter); // parameters are pre-declared
}

static void EndFunctionScope() { VarScopes.clear(); }

static void DeclareVar(const string &Name) {
  if (VarScopes.empty())
    return;
  VarScopes.back().insert(Name); // declare in the innermost (current) scope
}

static void BeginBlockScope() { VarScopes.emplace_back(); }
static void EndBlockScope() {
  if (VarScopes.size() > 1)
    VarScopes.pop_back();
}

static void BeginLoopScope(const string &Name) {
  VarScopes.emplace_back();
  VarScopes.back().insert(Name);
}
static void EndLoopScope() {
  if (VarScopes.size() > 1)
    VarScopes.pop_back();
}

// Check only the innermost scope (used for redeclaration checks).
static bool IsDeclaredInCurrentScope(const string &Name) {
  if (VarScopes.empty())
    return false;
  return VarScopes.back().count(Name) > 0;
}

// Check all local scopes from innermost to outermost.
static bool IsDeclaredVar(const string &Name) {
  for (auto It = VarScopes.rbegin(); It != VarScopes.rend(); ++It)
    if (It->count(Name))
      return true;
  return false;
}
```

`IsDeclaredInCurrentScope` and `IsDeclaredVar` answer two different questions: the first catches redeclaring the same name in the same block, the second catches referencing a name that was never declared anywhere visible. I use the first one for `var`'s own redeclaration check:

<!-- code-merge:start -->
```pyxc
ready> def f():
    var x = 1
    var x = 2
```
```text
Error (Line 3, Column 11): Variable 'x' already declared in this scope
    var x = 
          ^~~~
```
<!-- code-merge:end -->

Each scope guard is a small C++ struct. The constructor opens the scope; the destructor closes it. When the guard variable goes out of scope — at the end of a block, or when an early return is hit — the scope closes automatically, no explicit cleanup calls needed:

```cpp
struct FunctionScopeGuard {
  FunctionScopeGuard(const vector<string> &Parameters) { BeginFunctionScope(Parameters); }
  ~FunctionScopeGuard() { EndFunctionScope(); }
};

struct BlockScopeGuard {
  BlockScopeGuard() { BeginBlockScope(); }
  ~BlockScopeGuard() { EndBlockScope(); }
};

struct LoopScopeGuard {
  LoopScopeGuard(const string &Name) { BeginLoopScope(Name); }
  ~LoopScopeGuard() { EndLoopScope(); }
};
```

I create a `FunctionScopeGuard` right after parsing the signature, before the body — parameters enter scope there. `ParseForStatement` only creates a `LoopScopeGuard` when the loop introduces a new variable with `var`; without `var`, the loop reuses an existing variable and errors if it isn't declared:

```cpp
static unique_ptr<ExpressionNode> ParseForStatement() {
  getNextToken(); // eat 'for'
  // ... parse optional 'var', the loop variable name, and its scope check ...

  unique_ptr<ExpressionNode> Start, Cond, Step, Body;

  unique_ptr<LoopScopeGuard> LoopScope;
  if (IsVarDecl)
    LoopScope = make_unique<LoopScopeGuard>(VarName);

  if (!ParseForParts(Start, Cond, Step, Body))
    return nullptr;
  // ...
}
```

## Parsing a Suite

After every `:`, the parser calls `ParseSuite`. A suite is either an inline statement or an indented block:

```cpp
static unique_ptr<ExpressionNode> ParseSuite() {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken != tok_indent)
      return LogErrorExpression("Expected an indented block");
    return ParseBlock(); // CurrentToken = tok_block_end on return
  }
  if (CurrentToken == tok_indent)
    return ParseBlock(); // CurrentToken = tok_block_end on return
  return ParseStatement();
}
```

When `ParseSuite` delegates to `ParseBlock`, it returns exactly what `ParseBlock` returns, with `CurrentToken = tok_block_end`. The caller can inspect `CurrentToken` to know whether the suite ended with a block.

`ParseIfStatement` and `ParseForStatement` both call `ParseSuite` after eating `:`. A `def` body is slightly different — its inline form only accepts a `simple-statement`, not a compound one. You can't write `def f(x): if x > 0: return 1` on one line.

## Parsing a Block

`ParseBlock` consumes `INDENT`, requires at least one statement, reads more statements separated by `statement-separator` until `DEDENT`, injects `tok_block_end`, and returns:

```cpp
static unique_ptr<ExpressionNode> ParseBlock() {
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented block");
  getNextToken(); // eat INDENT

  BlockScopeGuard Scope; // each block gets its own var scope

  consumeNewlines();
  if (CurrentToken == tok_dedent)
    return LogErrorExpression("Expected at least one statement in block");

  vector<unique_ptr<ExpressionNode>> Stmts;

  while (true) {
    if (CurrentToken == tok_dedent)
      break;

    auto Stmt = ParseStatement();
    if (!Stmt)
      return nullptr;
    Stmts.push_back(std::move(Stmt));

    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken == tok_dedent)
      break;
    if (CurrentToken == tok_block_end) {
      // A nested block just closed. No tok_eol separates it from the next
      // statement; consume the marker and continue.
      getNextToken();
      continue;
    }
    return LogErrorExpression("Expected newline or end of block");
  }

  if (CurrentToken != tok_dedent)
    return LogErrorExpression("Expected end of block");

  // Inject tok_block_end before advancing past DEDENT so that callers see it
  // as CurrentToken on return, removing the need for any boolean flag.
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // → CurrentToken = tok_block_end

  return make_unique<BlockStatementNode>(std::move(Stmts));
}
```

The last two lines are the key. When the loop breaks on `tok_dedent`, `CurrentToken` holds the DEDENT token. I push `tok_block_end` to the front of `PendingTokens` and call `getNextToken()`. That pops `tok_block_end` back out and overwrites `CurrentToken` — the DEDENT is quietly consumed in the process, and `ParseBlock` returns with `CurrentToken = tok_block_end`.

Every caller that previously needed a boolean "did this suite end with a block?" can now just check `CurrentToken == tok_block_end`.

## `BLOCK_END` and the `else` Problem

`tok_block_end` flows cleanly through most of the parser — `ParseBlock`'s own loop consumes it and keeps going. One case is trickier: `if` with an optional `else`.

After `ParseSuite` returns the then-branch, `CurrentToken` might be `tok_block_end` (if the then was a block) or `tok_eol` (if the then was inline, e.g. `if cond: return 1`). Either way, `else` — if present — lives on the very next line at the same indentation level, right where that separator token is sitting. `ParseIfStatement` needs to look past it to check.

The approach: consume the separator temporarily to peek at what follows. If it's `else`, parse the else branch normally. If it's not, re-inject the separator so the enclosing `ParseBlock` loop still sees it.

```cpp
unique_ptr<ExpressionNode> Then = ParseSuite();
if (!Then)
  return nullptr;

bool ThenWasBlock = (CurrentToken == tok_block_end);
if (ThenWasBlock)
  getNextToken(); // consume tok_block_end → CurrentToken = next real token

// If Then was inline, CurrentToken is tok_eol right now (unless there was no
// trailing newline at all). Remember that so it can be restored below.
bool ThenHadTrailingEol = (CurrentToken == tok_eol);

consumeNewlines(); // skip any blank lines before 'else'

unique_ptr<ExpressionNode> Else;
if (CurrentToken == tok_else) {
  getNextToken(); // eat 'else'
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after else");
  getNextToken(); // eat ':'
  Else = ParseSuite();
  if (!Else)
    return nullptr;
} else if (ThenWasBlock) {
  // No else. Re-inject tok_block_end so the enclosing block sees it.
  PendingTokens.push_front(CurrentToken); // push current lookahead back
  CurrentToken = tok_block_end;           // restore the signal directly
} else if (ThenHadTrailingEol) {
  // No else, and Then was inline. consumeNewlines() above swallowed the
  // newline that separates this statement from the next one — restore it.
  PendingTokens.push_front(CurrentToken);
  CurrentToken = tok_eol;
}
```

The critical detail is the last two lines of each `else if` branch. After `getNextToken()` consumed `tok_block_end` (or `consumeNewlines()` consumed a real `tok_eol`), a real token — say `tok_return`, or the next `tok_dedent` — landed in `CurrentToken`. If I naively pushed the separator to `PendingTokens` and called `getNextToken()` again, that call would pop the separator right back out, and the token already sitting in `CurrentToken` would be **overwritten and lost**. The statement after the `if` would parse incorrectly, or the outer block would close at the wrong point.

Instead: push the current `CurrentToken` to `PendingTokens`, then set `CurrentToken` to the separator directly, without calling `getNextToken()`. The saved token is now first in `PendingTokens`; the next `getNextToken()` call anywhere upstream retrieves it correctly.

I found this the hard way: my first pass only handled the `ThenWasBlock` case, on the assumption that an inline `then` always ends cleanly at a `tok_eol` nothing downstream would touch. That's true in isolation — but `consumeNewlines()` a few lines later doesn't know or care whether it's looking at a "real" separator or one it's about to strand a statement without. It consumes any `tok_eol` in front of it while probing for `else`, block or no block. Miss the inline case, and `if x > 10: return 20` parses fine as the *last* statement in a block, but breaks the moment another statement follows it at the same indentation — the newline `ParseBlock`'s loop needed as a separator is gone. `ThenHadTrailingEol` closes that gap the same way `ThenWasBlock` already did for the block case.

## Parsing Statements

`ParseStatement` dispatches to compound or simple statement parsers:

```cpp
static unique_ptr<ExpressionNode> ParseStatement() {
  if (CurrentToken == tok_if)
    return ParseIfStatement();
  if (CurrentToken == tok_for)
    return ParseForStatement();
  return ParseSimpleStatement();
}
```

`ParseSimpleStatement` handles `return`, `var`, and everything else — assignment and bare expressions both start by parsing a full expression, then check whether `=` follows:

```cpp
static unique_ptr<ExpressionNode> ParseSimpleStatement() {
  if (CurrentToken == tok_return)
    return ParseReturnStatement();
  if (CurrentToken == tok_var)
    return ParseVarStatement();
  if (CurrentToken == tok_name)
    return ParseLeadingNameSimpleStatement();
  return ParseNonLeadingNameSimpleStatement();
}
```

I split the expression-or-assignment path into two helpers so the "not a variable" error stays specific to assignment, not a generic parse failure. Both call `ParseExpression` first; the only difference is what happens if `=` follows:

```cpp
// Parse a name-led expression, then recognize an assignment statement if '=' follows.
static unique_ptr<ExpressionNode> ParseLeadingNameSimpleStatement() {
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;

  if (CurrentToken != tok_equal)
    return Expr;

  const string *AssignedName = Expr->getLValueName();
  if (!AssignedName)
    return LogErrorExpression("Destination of '=' must be a variable");

  return ParseAssignmentRight(*AssignedName);
}

// Parse non-name-leading expression forms for simple-statement and reject a
// trailing '=' so assignment diagnostics stay local and specific.
static unique_ptr<ExpressionNode> ParseNonLeadingNameSimpleStatement() {
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;

  if (CurrentToken != tok_equal)
    return Expr;

  return LogErrorExpression("Destination of '=' must be a variable");
}
```

`ParseAssignmentRight` is where the undeclared-variable check actually happens:

```cpp
static unique_ptr<ExpressionNode> ParseAssignmentRight(const string &Name) {
  if (!IsDeclaredVar(Name))
    return LogErrorExpression("Assignment to undeclared variable");
  getNextToken(); // eat '='

  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  return make_unique<AssignmentStatementNode>(Name, std::move(Right));
}
```

Assignment to an undeclared variable is rejected here, at parse time — no codegen needed to catch it.

## Parsing `var` as a Statement

`var` in this chapter has no body. It declares one or more names that persist for the rest of the function:

```cpp
static unique_ptr<ExpressionNode> ParseVarStatement() {
  getNextToken(); // eat 'var'

  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;

  while (true) {
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected name after 'var'");

    string ParsedName = Name;
    getNextToken(); // eat name

    if (IsDeclaredInCurrentScope(ParsedName))
      return LogErrorExpression(
          ("Variable '" + ParsedName + "' already declared in this scope").c_str());

    unique_ptr<ExpressionNode> Init;
    if (CurrentToken == tok_equal) {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init)
        return nullptr;
    } else {
      Init = make_unique<NumberExpressionNode>(0.0);
    }

    VarNames.push_back({ParsedName, std::move(Init)});
    DeclareVar(ParsedName);

    if (CurrentToken != tok_comma)
      break;
    getNextToken(); // eat ','
  }

  return make_unique<VarStatementNode>(std::move(VarNames));
}
```

The critical difference from [chapter 11](chapter-11.md): no `:` and no body. `DeclareVar` registers each name in the current block scope, so later assignments to it pass `IsDeclaredVar`. If the `var` is inside an `if` or `for` block, that name is only visible inside that block.

## Return

```cpp
static unique_ptr<ExpressionNode> ParseReturnStatement() {
  getNextToken(); // eat 'return'
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  return make_unique<ReturnStatementNode>(std::move(Expr));
}
```

`ReturnStatementNode::codegen` emits a real LLVM terminator — a `ret` instruction that ends the current basic block:

```cpp
Value *ReturnStatementNode::codegen() {
  Value *RetVal = Expr->codegen();
  if (!RetVal)
    return nullptr;

  TheBuilder->CreateRet(RetVal);
  return RetVal;
}
```

## Block Codegen

`BlockStatementNode::codegen` evaluates statements in order. It stops early if a `return` already terminated the current block — statements after a `return` are unreachable. It also saves and restores `NamedValues` around the block body, so variables declared inside the block with `var` don't leak to the outer scope:

```cpp
Value *BlockStatementNode::codegen() {
  auto SavedBindings = NamedValues;

  Value *Last = nullptr;
  for (auto &Stmt : Stmts) {
    if (TheBuilder->GetInsertBlock()->getTerminator())
      break;
    Last = Stmt->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      return nullptr;
    }
  }

  NamedValues = SavedBindings;

  if (!Last)
    return LogErrorV("Empty block");

  // Blocks are statement sequences. If control reaches the end without an
  // explicit return, the block's implicit value is always 0.0.
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

## `var` and Assignment Codegen

`VarStatementNode::codegen` allocates stack slots and initializes them. Duplicate declarations in the same scope are already caught at parse time, so codegen just sets up the alloca and records the binding:

```cpp
Value *VarStatementNode::codegen() {
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.first;
    ExpressionNode *Init = Var.second.get();

    Value *InitVal = Init->codegen();
    if (!InitVal)
      return nullptr;

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    TheBuilder->CreateStore(InitVal, Alloca);
    NamedValues[VarName] = Alloca;
  }

  return ConstantFP::get(*TheContext, APFloat(0.0)); // var statement produces 0.0
}
```

Assignment codegen is unchanged in substance from [chapter 11](chapter-11.md) — it loads the alloca from `NamedValues`, stores the new value, and returns it. Only the class name changed, from `AssignmentExpressionNode` to `AssignmentStatementNode`, matching the grammar's new `assignment-statement` rule.

## `if` as a Statement

[Chapter 4](chapter-04.md) had `if` producing a value through a PHI node, with both `then` and `else` required. This chapter's `IfStatementNode` doesn't need to produce a value, so it has no PHI node, and `else` is optional — if it's missing, the else block just falls through to the merge block:

```cpp
Value *IfStatementNode::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  CondV = TheBuilder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);

  TheBuilder->CreateCondBr(CondV, ThenBB, ElseBB);

  TheBuilder->SetInsertPoint(ThenBB);
  if (!Then->codegen())
    return nullptr;
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateBr(MergeBB);

  TheBuilder->SetInsertPoint(ElseBB);
  if (Else) {
    if (!Else->codegen())
      return nullptr;
  }
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateBr(MergeBB);

  TheBuilder->SetInsertPoint(MergeBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

The `getTerminator()` check before each `CreateBr` is what makes `return` inside an `if` work correctly. If the `then` block already has a `ret`, I don't emit a second branch — that would be ill-formed IR.

## Implicit Return

In [chapter 11](chapter-11.md), `FunctionDefinitionNode::codegen` always emitted `CreateRet(RetVal)` unconditionally after `Body->codegen()` returned. That breaks now that `return` statements emit their own `ret` instructions — I'd emit a second, unreachable one.

I check whether the current block already has a terminator before adding one, and when I do add one, it's always the constant `0.0` — never the body's own return value, since a block that falls through with no explicit `return` has, by definition, nothing meaningful to return:

```cpp
if (Value *BodyVal = Body->codegen()) {
  // If the body didn't already terminate the current block (e.g. via
  // return), return 0.0. Implicit returns never use the last expression.
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateRet(ConstantFP::get(*TheContext, APFloat(0.0)));
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

When a `def` body ends with an indented block, parsing it returns with `CurrentToken = tok_block_end`. `MainLoop` handles it explicitly in its switch, so two definitions back to back with no blank line between them still work. Two more cases sit alongside it: a stray `tok_indent` at the top level (indentation where none was expected) is reported and skipped, and a stray `tok_dedent` (an unmatched close, which can happen in REPL mode when a block is torn down early) is silently consumed:

```cpp
static void MainLoop() {
  while (CurrentToken != tok_eof) {
    switch (CurrentToken) {
    case tok_indent:
      LogErrorExpression("Unexpected indentation");
      SynchronizeToLineBoundary();
      break;
    // Stray dedent at top level (can occur in REPL mode): skip it.
    case tok_dedent:
      getNextToken();
      break;
    // Block-end marker left in the stream after a block-bodied definition.
    case tok_block_end:
      getNextToken();
      break;
    // ...
    }
  }
}
```

`SynchronizeToLineBoundary` and `HandleFunctionDefinition` both need to know about these new tokens too. `SynchronizeToLineBoundary` used to stop only at `tok_eol` or `tok_eof`; now it also stops at `tok_dedent` and `tok_block_end`, since those tokens mark the true end of a malformed block, not just a line:

```cpp
static void SynchronizeToLineBoundary() {
  while (CurrentToken != tok_eol && CurrentToken != tok_eof &&
         CurrentToken != tok_dedent && CurrentToken != tok_block_end)
    getNextToken();
}
```

And `HandleFunctionDefinition`'s trailing-token check now accepts `tok_block_end` as a valid way for a definition to end, alongside `tok_eol` and `tok_eof` — a block-bodied `def` doesn't leave a newline behind, it leaves `tok_block_end`:

```cpp
static void HandleFunctionDefinition() {
  auto FunctionDefinition = ParseFunctionDefinition();
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof &&
                      CurrentToken != tok_block_end);
  if (!FunctionDefinition || HasTrailing) {
    if (FunctionDefinition)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  // ...
}
```

## Top-Level Assignment

`ParseExpression` no longer understands `=` at all — assignment moved to `ParseSimpleStatement` this chapter. But a top-level REPL line still goes through `ParseExpression`, by way of `ParseTopLevelExpression`, so it needs its own handling to recognize `x = 1` and produce the right error instead of a generic parse failure. `ParseTopLevelExpression` opens a fresh, empty function scope (top-level input has no parameters), and checks for a trailing `=` itself:

```cpp
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  FunctionScopeGuard Scope({});
  auto Expression = ParseExpression();
  if (!Expression)
    return nullptr;

  if (CurrentToken == tok_equal) {
    const string *AssignedName = Expression->getLValueName();
    if (!AssignedName)
      return LogErrorFunction("Destination of '=' must be a variable");

    string Name = *AssignedName;
    if (!IsDeclaredVar(Name))
      return LogErrorFunction("Assignment to undeclared variable");

    getNextToken(); // eat '='
    auto Right = ParseExpression();
    if (!Right)
      return nullptr;
    Expression = make_unique<AssignmentStatementNode>(Name, std::move(Right));
  }

  auto Signature = make_unique<FunctionSignatureNode>("__anon_expr", vector<string>());
  auto Body = make_unique<ReturnStatementNode>(std::move(Expression));
  return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
}
```

Since the scope is fresh and empty every time, `IsDeclaredVar` can never succeed for a top-level assignment — that's exactly the "Assignment to undeclared variable" limitation shown below, and this is the code path that produces it. The whole expression, assignment or not, gets wrapped in a `ReturnStatementNode` before it's handed to `FunctionDefinitionNode`, so a top-level expression's value now reaches the caller through an explicit `return` rather than the old unconditional `CreateRet` at the end of `FunctionDefinitionNode::codegen`.

## Known Limitations

**No global variables.** `var` is only valid inside a function body — a top-level `var x = 10` is a parse error, since top-level input still goes through `ParseExpression`, which no longer understands `var` at all. Each top-level input also gets its own fresh function scope, so there's no way to declare a variable on one REPL line and reference it on the next:

```pyxc
# Does not work in the REPL:
var x = 10      # parse error — var is not an expression at the top level
x = x + 10      # x is undeclared in this expression's scope
printd(x)
```

For now, keep mutable state inside a function:

```pyxc
extern def printd(x)

def f():
    var x = 10
    x = x + 10
    return x

printd(f())   # prints 20.000000
```

[Chapter 15](chapter-15.md) addresses this properly: for compiled programs, all top-level statements are collected into a synthesized `main()`, so `var` declarations and assignments at the top level work naturally. Full REPL support for global state needs additional runtime infrastructure, and is covered there too.

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

Accumulator loop — the [chapter 11](chapter-11.md) workaround, now written naturally:

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

[Chapter 13](chapter-13.md) adds `elif` chains.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

I'll help you figure it out.

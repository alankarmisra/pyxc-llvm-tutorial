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
    for var i = 1, i <= n, i = i + 1:
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

The central shift: I move `if`, `for`, `var`, and (new) `return` out of the expression grammar and make them statements. Expressions go back to being purely value-producing — I add `statement`, `simple-statement`, `compound-statement`, `suite`, and `block` to hold them, plus `indent`/`dedent`/`INDENT`/`DEDENT`/`BLOCK_END` for the indentation machinery:

`code/chapter-12/pyxc.ebnf`

```grammardiff
*...
*                                    | external
*                                    | top-level-expression ;
-function-definition               = "def" function-signature ":"
-                                    [ end-of-lines ] expression ;
+function-definition               = "def" function-signature ":"
+                                    ( simple-statement
+                                      | end-of-lines block ) ;
*external                          = "extern" "def" function-signature ;
*top-level-expression              = expression ;
*function-signature                = name "(" [ parameters ] ")" ;
*parameters                        = parameter { "," parameter } ;
*parameter                         = name ;
-expression                        = variable-expression
-                                    | comparison [ "=" expression ] ;
-(* Constraint: If "=" is present, comparison must resolve to a variable lvalue.*)
-variable-expression               = "var" variable-binding
-                                    { "," variable-binding } ":"
-                                    [ end-of-lines ] expression ;
-variable-binding                  = name [ "=" expression ] ;
+if-statement                      = "if" expression ":" suite
+                                    [ [ end-of-lines ] "else" ":" suite ] ;
+for-statement                     = "for" [ "var" ] name "=" expression ","
+                                    expression "," for-update ":" suite ;
+(* The final expression performs the complete loop update; its value is discarded. *)
+for-update                         = assignment-statement | expression ;
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
*comparison                        = sum { comparison-operator sum } ;
*comparison-operator               = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
*sum                               = term { ("+" | "-") term } ;
*term                              = factor { ("*" | "/" | "%") factor } ;
+lvalue                            = name ;
+variable-binding                  = name [ "=" expression ] ;
*factor                            = "-" factor | primary ;
*primary                           = name-expression
*                                    | number-expression
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
*call-expression                   = name "(" [ arguments ] ")" ;
*arguments                         = expression { "," expression } ;
*number-expression                 = number ;
*parenthesized-expression          = "(" expression ")" ;
+indent                            = INDENT ;
+dedent                            = DEDENT ;
*name                              = (letter | "_")
*                                    { letter | digit | "_" } ;
*...
*comment-character                 = ? any character except "\r" and "\n" ? ;
*whitespace                        = " " | "\t" | "\v" | "\f" ;
+INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
+DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
+BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
+                                      immediately after it consumes DEDENT ? ;
```

Assignment is no longer a general expression in this statement-oriented
grammar, but a `for` loop still needs an assignment in its update field. I make
that role explicit with `for-update`: it accepts either an assignment statement
or an ordinary expression. In both cases the loop evaluates it after the body
and discards its value.

- **`suite`** — what follows a `:`. Either a single statement on the same line, or a newline followed by an indented block.
- **`simple-statement`** — statements that fit on one line: `return`, `var`, assignment, or a bare expression.
- **`compound-statement`** — statements that introduce a new suite: `if` and `for`.
- **`statement-separator`** — what separates two statements inside a block. Normally that's one or more newlines. But when the first statement was itself a block (an `if` or `for` with an indented body), no newline follows — I already consumed the line break as part of the `DEDENT`. I use `BLOCK_END` to cover that case; see below.
- **`block`** — an `INDENT` token, one or more statements separated by `statement-separator`, a `DEDENT` token.
- **`INDENT` / `DEDENT`** — tokens I emit from the lexer when indentation increases or decreases. I emit one `INDENT` when a block opens, one `DEDENT` when it closes — not one per line.
- **`BLOCK_END`** — a synthetic token I have `ParseBlock` inject into the token stream just before it returns. It signals "a nested block just closed here." The enclosing `ParseBlock` loop consumes it instead of expecting a newline, and any outer caller can check for it too.

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

I no longer handle `var`, `if`, `for`, or assignment in `ParseExpression`. I moved them all into `ParseStatement` and `ParseSimpleStatement`. Expressions are purely value-producing again — operators, calls, variable reads.

## New Tokens and AST Nodes

Three new token values:

```cppdiff
*enum Token {
*  ...
*  // mutable variables
*  tok_var = -16,
*
+  // indentation
+  tok_indent    = -17,
+  tok_dedent    = -18,
+  tok_block_end = -19, // synthetic: injected by ParseBlock after eating DEDENT
*
*  // punctuation and operators
*  ...
*};
```

`tok_indent` and `tok_dedent` come from the lexer — I push them into `PendingTokens` when I detect a change in indentation. I never produce `tok_block_end` from the lexer; I have `ParseBlock` inject it into `PendingTokens` just before returning, so the calling parser sees it as `CurrentToken`. It's a signal I add to the token stream, not a character in the source.

New AST node classes:

### `return`

`ReturnStatementNode` — a `return` statement:

```cpp
/// ReturnStatementNode - Statement-like expression for return.
/// Emits a function return and produces the returned value.
class ReturnStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Expression;

public:
  ReturnStatementNode(unique_ptr<ExpressionNode> Expression)
      : Expression(std::move(Expression)) {}
  Value *codegen() override;
};
```

### Blocks

`BlockStatementNode` — a sequence of statements evaluated in order:

```cpp
/// BlockStatementNode - A sequence of statements evaluated in order.
/// The block's value is the value of the last statement executed.
class BlockStatementNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Statements;

public:
  BlockStatementNode(vector<unique_ptr<ExpressionNode>> Statements)
      : Statements(std::move(Statements)) {}
  Value *codegen() override;
};
```

### `var`

`VariableStatementNode` replaces [chapter 11](chapter-11.md)'s `VariableExpressionNode` and drops the body entirely — no more `: expression` tail. Variables declared here persist for the rest of the function, not just one expression:

```cppdiff
-/// VariableExpressionNode - Expression class for mutable local variable
-/// bindings.
-class VariableExpressionNode : public ExpressionNode {
-  vector<pair<string, unique_ptr<ExpressionNode>>> VariableBindings;
-  unique_ptr<ExpressionNode> Body;
-
-public:
-  VariableExpressionNode(
-      vector<pair<string, unique_ptr<ExpressionNode>>> VariableBindings,
-      unique_ptr<ExpressionNode> Body)
-      : VariableBindings(std::move(VariableBindings)), Body(std::move(Body)) {}
-  Value *codegen() override;
-};
```

```cpp
/// VariableStatementNode - Statement form of mutable local variable bindings.
///   var a = <init>, b = <init>
/// Each binding allocates stack storage in the current function's entry block
/// and stores its initializer. Bindings persist for the rest of the function.
class VariableStatementNode : public ExpressionNode {
  vector<pair<string, unique_ptr<ExpressionNode>>> VariableBindings;

public:
  VariableStatementNode(
      vector<pair<string, unique_ptr<ExpressionNode>>> VariableBindings)
      : VariableBindings(std::move(VariableBindings)) {}
  Value *codegen() override;
};
```

### `if`

`IfStatementNode` replaces [chapter 10](chapter-10.md)'s `IfExpressionNode`. The field layout barely changes — `Else` was already a plain field, and stays one — but its meaning does: `Else` is now `nullptr` whenever the source omits an `else` clause, something chapter 10 never allowed:

```cppdiff
-/// IfExpressionNode - Expression class for if/else.
-class IfExpressionNode : public ExpressionNode {
-  unique_ptr<ExpressionNode> Condition, Then, Else;
-
-public:
-  IfExpressionNode(unique_ptr<ExpressionNode> Condition,
-                   unique_ptr<ExpressionNode> Then,
-                   unique_ptr<ExpressionNode> Else)
-      : Condition(std::move(Condition)), Then(std::move(Then)),
-        Else(std::move(Else)) {}
-  Value *codegen() override;
-};
```

```cpp
/// IfStatementNode - Statement form of if/else.
/// Produces 0.0 and does not return a value.
class IfStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Condition, Then, Else;

public:
  IfStatementNode(unique_ptr<ExpressionNode> Condition, unique_ptr<ExpressionNode> Then,
            unique_ptr<ExpressionNode> Else)
      : Condition(std::move(Condition)), Then(std::move(Then)), Else(std::move(Else)) {}
  Value *codegen() override;
};
```

## pyxc Indentation Rules

Similar to Python's, with one difference: pyxc allows mixing tabs and spaces (Python 3 disallows it).

- Each space advances one column; each tab advances to the next multiple of 8.
- Mixing tabs and spaces is allowed — the column count is what matters.
- Dedenting to a column that was never opened is an error.
- Blank lines and comment-only lines don't affect indentation in file mode. In REPL mode, a blank line closes the current block immediately.
- A block opens after `:` followed by a newline and a deeper indentation level.

## INDENT and DEDENT

A single counter isn't enough to track indentation — I need to remember every level I opened, for nested blocks. When indentation drops, I need to know which level I'm returning to, and how many blocks I'm closing at once. That's why I keep an `IndentStack` and a pending-token queue.

At the start of each line, I find the indentation level and compare it to the top of the stack. An increase returns `INDENT` right away — no queue involved. A decrease is different: I queue one `DEDENT` per level closed and return only the first one, so a single line that closes several levels at once still gets drained by the parser one `DEDENT` at a time:

```pyxc
def f(x):            # Stack: [0]         Pending: []
    if x > 0:        # Stack: [0, 4]      Pending: []        → INDENT
        if x > 10:   # Stack: [0, 4, 8]   Pending: []        → INDENT
            return x # Stack: [0, 4, 8, 12]  Pending: []     → INDENT
    return 0         # col 4 is less than both 12 and 8: two levels closed, two DEDENTs queued
                     # Stack: [0, 4]      Pending: [DEDENT]  → returns the first DEDENT now
                     # the second DEDENT stays queued; the next getToken() call drains it
                     #   before it even looks at whatever line comes after "return 0"
```

Blocks are also automatically closed at end of file — no trailing blank line needed:

```pyxc
def f():
    var x = 5        # Stack: [0, 4]  Pending: []  → INDENT
    return x         # Stack: [0, 4]  Pending: []    (same level, nothing happens)
# EOF                # col 0: stack still holds [0, 4] → one DEDENT pushed into the pending queue
                     # the parser drains it on the next getNextToken() call
```

Let's start by defining the structures we need to track indents/dedents.

```cpp
static vector<int> IndentStack = {0}; // starts at column 0
static deque<int>  PendingTokens;     // buffered tokens the parser hasn't seen yet
static bool AtLineStart = true;       // true right after a newline
static constexpr int IndentTabWidth = 8; // tab width
```

Inside `getToken()`, I process indentation at the start of every line, before any normal token logic.

**Step 1: Find the indentation level of the current line.**

This is the start of `getToken()` itself — the indentation check comes before any other token logic, right after the multi-level-dedent drain from `PendingTokens`:

```cppdiff
~static int getToken() {
~  static int LastChar = ' ';

+// ── Line-start: count indentation, emit INDENT / DEDENT ──────────────
+  if (AtLineStart) {
+    // Prime sentinel space once so indentation scans real input.
+    if (LastChar == ' ')
+      LastChar = advance();
+    int CurrentIndentRead = 0;
+    while (LastChar == ' ' || LastChar == '\t') {
+      CurrentIndentRead += (LastChar == ' ')
+              ? 1 : (IndentTabWidth - CurrentIndentRead % IndentTabWidth);
+      LastChar = advance();
+    }
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

For a blank line or a comment-only line, I return `tok_eol` here without touching the indent stack (in the REPL, I close the current block immediately on a blank line instead). For EOF, I fall through to the flush logic further below. For real source content, I fall through to Step 2.

**Step 2: Compare to the top of the stack and either return `tok_indent` directly or queue `DEDENT` tokens.**

```cppdiff
~      CurrentIndentRead ~= (LastChar == ' ')
~              ? 1 : (IndentTabWidth - CurrentIndentRead % IndentTabWidth);
~      LastChar = advance();
~    }
~
+  CurrentTokenLocation = LexerLocation;
+  int CurrentIndentOnStack = IndentStack.back();
+
+  // I see an indent: I remember the new level and open a block.
+  if (CurrentIndentRead > CurrentIndentOnStack) {
+    IndentStack.push_back(CurrentIndentRead);
+    AtLineStart = false;
+    return tok_indent;
+  }
+
+  // I see a dedent: I close blocks until I reach the new level.
+  if (CurrentIndentRead < CurrentIndentOnStack) {
+    while (IndentStack.size() > 1 && CurrentIndentRead < IndentStack.back()) {
+      IndentStack.pop_back();
+      PendingTokens.push_back(tok_dedent);
+    }
+    if (CurrentIndentRead != IndentStack.back()) {
+      LogErrorAtLoc("inconsistent indentation", CurrentTokenLocation);
+      PrintErrorSourceContext(CurrentTokenLocation);
+      PendingTokens.clear();
+      AtLineStart = false;
+      return tok_error;
+    }
+    AtLineStart = false;
+    int Tok = PendingTokens.front();
+    PendingTokens.pop_front();
+    return Tok;
+  }
+
+  // Same indentation level — no indent/dedent token needed.
+  AtLineStart = false;
```

**Step 3: On the next call, drain the rest of the queue before doing anything else.**

I leave any `DEDENT` tokens from a multi-level dedent, or from Step 2's queuing, sitting in `PendingTokens` for the next call to consume. I check for them right at the top of `getToken()`, before the line-start logic even runs:

```cppdiff
~static int getToken() {
~  static int LastChar = ' ';
~
+  // Drain tokens queued by a multi-level dedent on the previous line.
+  if (!PendingTokens.empty()) {
+    int Tok = PendingTokens.front();
+    PendingTokens.pop_front();
+    return Tok;
+  }
~
~  CurrentTokenLocation = LexerLocation;
~  int CurrentIndentOnStack = IndentStack.back();
```

I call `getToken()` again for each subsequent token, and each call drains one more entry from the queue before I return to normal lexing.

At EOF, the lexer flushes one `DEDENT` per still-open block:

```cppdiff
~static int getToken() {
~  ...
~  if (AtLineStart) {
~    ...
+    // EOF (with or without trailing newline): flush open blocks one at a time.
+    if (LastChar == EOF) {
+      if (IndentStack.size() > 1) {
+        IndentStack.pop_back();
+        return tok_dedent;
+      }
+      return tok_eof;
+    }
~    ...
~  }
~  ...
~}
```

Two more checks happen before that one, inside the same block — a blank line, and a comment-only line:

```cppdiff
~static int getToken() {
~  ...
~  if (AtLineStart) {
~    ...
+    // Blank line: ignore in file mode; close the block immediately in REPL.
+    if (LastChar == '\n') {
+      if (IsRepl && IndentStack.size() > 1) {
+        IndentStack.pop_back();
+        return tok_dedent;
+      }
+      CurrentTokenLocation = LexerLocation;
+      LastChar = ' ';
+      return tok_eol;
+    }
+
+    // Comment-only line: consume and return a newline.
+    if (LastChar == '#') {
+      do {
+        LastChar = advance();
+      } while (LastChar != '\n' && LastChar != EOF);
+      if (LastChar == '\n') {
+        CurrentTokenLocation = LexerLocation;
+        LastChar = ' ';
+        return tok_eol;
+      }
+      // else fall through to EOF handling below
+    }
~    ...
~  }
~  ...
~}
```

A blank line in file mode is just `tok_eol` — I don't touch the indent stack, so nothing about the surrounding blocks changes. In the REPL, a blank line ends the current block immediately instead: I can't count on an actual dedented line ever showing up, since the user might just hit Enter twice to say "I'm done with this block," so I treat the blank line itself as that signal — the same behavior as the Python REPL.

A comment-only line is simpler still: I consume everything up to the newline and return `tok_eol`, same as a blank line, indent stack untouched. If the comment runs to the end of the file with no trailing newline, `LastChar` is now `EOF` — that's the fallthrough the comment refers to, and it's exactly the EOF handling I covered above.

### Inconsistent indentation

When indentation increases, I never go through `PendingTokens` at all — I return `tok_indent` immediately. When it decreases, I can close several levels at once: for each closed level, I push one `tok_dedent` onto `PendingTokens`, and I return the first one from this call. I drain the rest on later calls to `getToken()`, through the check at the very top of the function (see Step 3 below). A single dedent can therefore produce multiple `DEDENT` tokens — one for each level I close:

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
Error (Line 3, Column 4): Unexpected error
   v
   ^~~~
```
<!-- code-merge:end -->

Dedenting to column 3 has no match on the stack (`[0, 4]`) — it's neither the current indentation nor an outer one, so there's no consistent level to return to. By the time I discover that, I may already have queued one or more `DEDENT` tokens while searching the stack. Those tokens do not describe a valid transition, so I clear them before returning `tok_error`. I also set `AtLineStart` to `false` because the lexer has already consumed the line's leading whitespace. Without that reset, recovery can mistake the remaining text for the beginning of another line and lex it a second time.

The parser still reports `Unexpected error` after the lexer reports the inconsistent indentation. That is the normal boundary between the two layers: the lexer returns `tok_error`, and the parser reports that the token cannot begin the statement it was parsing. The important part is that recovery now consumes the rest of the malformed line instead of exposing its remaining tokens as a bogus new statement.

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

To catch this, I keep a scope stack of declared variable names — a function scope at the bottom, plus one nested scope per block:

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

I use `IsDeclaredInCurrentScope` and `IsDeclaredVar` to answer two different questions: whether I'm redeclaring the same name in the same block, and whether I'm referencing a name that was never declared anywhere visible. I use the first one for `var`'s own redeclaration check:

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

I write each scope guard as a small C++ struct: the constructor opens the scope, the destructor closes it. When the guard variable goes out of scope — at the end of a block, or when an early return is hit — the scope closes automatically, no explicit cleanup calls needed:

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

I create a `FunctionScopeGuard` right after parsing the signature, before the body — parameters enter scope there. In `ParseForStatement`, I only create a `LoopScopeGuard` when the loop introduces a new variable with `var`. Without `var`, I reuse an existing variable instead, and I error if it isn't declared:

```cpp
static unique_ptr<ExpressionNode> ParseForStatement() {
  getNextToken(); // eat 'for'
  // ... parse optional 'var', the loop variable name, and its scope check ...

  unique_ptr<ExpressionNode> Start, Condition, Update, Body;

  unique_ptr<LoopScopeGuard> LoopScope;
  if (DeclaresVariable)
    LoopScope = make_unique<LoopScopeGuard>(VariableName);

  if (!ParseForParts(Start, Condition, Update, Body))
    return nullptr;
  // ...
}
```

## Parsing a Suite

After every `:`, I call `ParseSuite`. A suite is either an inline statement or an indented block:

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

When I delegate from `ParseSuite` to `ParseBlock`, I return exactly what `ParseBlock` returns, with `CurrentToken = tok_block_end`. Any caller can inspect `CurrentToken` to know whether the suite ended with a block.

I call `ParseSuite` from both `ParseIfStatement` and `ParseForStatement`, after eating `:`. I handle a `def` body slightly differently — its inline form only accepts a `simple-statement`, not a compound one, so I don't accept `def f(x): if x > 0: return 1` on one line.

## Parsing a Block

In `ParseBlock`, I consume `INDENT`, require at least one statement, read more statements separated by `statement-separator` until `DEDENT`, inject `tok_block_end`, and return:

```cpp
static unique_ptr<ExpressionNode> ParseBlock() {
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented block");
  getNextToken(); // eat INDENT

  BlockScopeGuard Scope; // each block gets its own var scope

  consumeNewlines();
  if (CurrentToken == tok_dedent)
    return LogErrorExpression("Expected at least one statement in block");

  vector<unique_ptr<ExpressionNode>> Statements;

  while (true) {
    if (CurrentToken == tok_dedent)
      break;

    auto Stmt = ParseStatement();
    if (!Stmt)
      return nullptr;
    Statements.push_back(std::move(Stmt));

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

  return make_unique<BlockStatementNode>(std::move(Statements));
}
```

The last two lines are the key. When the loop breaks on `tok_dedent`, `CurrentToken` holds the DEDENT token. I push `tok_block_end` to the front of `PendingTokens` and call `getNextToken()`. That pops `tok_block_end` back out and overwrites `CurrentToken` — I quietly consume the DEDENT in the process, and I return from `ParseBlock` with `CurrentToken = tok_block_end`.

I no longer need any caller to track a boolean "did this suite end with a block?" — I just check `CurrentToken == tok_block_end`.

## `BLOCK_END` and the `else` Problem

`tok_block_end` flows cleanly through most of the parser — I consume it in `ParseBlock`'s own loop and keep going. One case is trickier: `if` with an optional `else`.

After `ParseSuite` returns the then-branch, `CurrentToken` might be `tok_block_end` (if the then was a block) or `tok_eol` (if the then was inline, e.g. `if cond: return 1`). Either way, `else` — if present — lives on the very next line at the same indentation level, right where that separator token is sitting. In `ParseIfStatement`, I need to look past that separator to check for `else`.

My approach: I consume the separator temporarily to peek at what follows. If it's `else`, I parse the else branch normally. If it's not, I re-inject the separator so the enclosing `ParseBlock` loop still sees it.

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
    return LogErrorExpression("Expected ':' after 'else'");
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

The critical detail is the last two lines of each `else if` branch. After I call `getNextToken()` to consume `tok_block_end` (or `consumeNewlines()` to consume a real `tok_eol`), a real token — say `tok_return`, or the next `tok_dedent` — ends up in `CurrentToken`. If I naively pushed the separator to `PendingTokens` and called `getNextToken()` again, that call would pop the separator right back out, and I'd **overwrite and lose** the token already sitting in `CurrentToken`. The statement after the `if` would then parse incorrectly, or the outer block would close at the wrong point.

Instead, I push the current `CurrentToken` to `PendingTokens`, then set `CurrentToken` to the separator directly, without calling `getNextToken()`. The saved token is now first in `PendingTokens`; the next `getNextToken()` call anywhere upstream retrieves it correctly.

I found this the hard way: my first pass only handled the `ThenWasBlock` case, on the assumption that an inline `then` always ends cleanly at a `tok_eol` nothing downstream would touch. That's true in isolation — but a few lines later, when I call `consumeNewlines()` to probe for `else`, I can't tell there whether I'm looking at a "real" separator or one I'm about to strand a statement without — it consumes any `tok_eol` in front of it regardless, block or no block. Miss the inline case, and `if x > 10: return 20` parses fine as the *last* statement in a block, but breaks the moment another statement follows it at the same indentation — the newline `ParseBlock`'s loop needed as a separator is gone. `ThenHadTrailingEol` closes that gap the same way `ThenWasBlock` already did for the block case.

## Parsing Statements

In `ParseStatement`, I dispatch to compound or simple statement parsers:

```cpp
static unique_ptr<ExpressionNode> ParseStatement() {
  if (CurrentToken == tok_if)
    return ParseIfStatement();
  if (CurrentToken == tok_for)
    return ParseForStatement();
  return ParseSimpleStatement();
}
```

In `ParseSimpleStatement`, I handle `return`, `var`, and everything else. For assignment and bare expressions, I start by parsing a full expression, then check whether `=` follows:

```cpp
static unique_ptr<ExpressionNode> ParseSimpleStatement() {
  if (CurrentToken == tok_return)
    return ParseReturnStatement();
  if (CurrentToken == tok_var)
    return ParseVariableStatement();
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

  if (CurrentToken != tok_assign)
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

  if (CurrentToken != tok_assign)
    return Expr;

  return LogErrorExpression("Destination of '=' must be a variable");
}
```

In `ParseAssignmentRight`, I actually run the undeclared-variable check:

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

I reject assignment to an undeclared variable right here, at parse time — no codegen needed to catch it.

## Parsing `var` as a Statement

`var` in this chapter has no body. It declares one or more names that persist for the rest of the function:

```cpp
static unique_ptr<ExpressionNode> ParseVariableStatement() {
  getNextToken(); // eat 'var'

  vector<pair<string, unique_ptr<ExpressionNode>>> VariableBindings;

  while (true) {
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected name after 'var'");

    string ParsedName = Name;
    getNextToken(); // eat name

    if (IsDeclaredInCurrentScope(ParsedName))
      return LogErrorExpression(
          ("Variable '" + ParsedName + "' already declared in this scope").c_str());

    unique_ptr<ExpressionNode> Init;
    if (CurrentToken == tok_assign) {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init)
        return nullptr;
    } else {
      Init = make_unique<NumberExpressionNode>(0.0);
    }

    VariableBindings.push_back({ParsedName, std::move(Init)});
    DeclareVar(ParsedName);

    if (CurrentToken != tok_comma)
      break;
    getNextToken(); // eat ','
  }

  return make_unique<VariableStatementNode>(std::move(VariableBindings));
}
```

The critical difference from [chapter 11](chapter-11.md): no `:` and no body. I register each name in the current block scope via `DeclareVar`, so later assignments to it pass `IsDeclaredVar`. If the `var` is inside an `if` or `for` block, that name is only visible inside that block.

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

In `ReturnStatementNode::codegen`, I emit a real LLVM terminator — a `ret` instruction that ends the current basic block:

```cpp
Value *ReturnStatementNode::codegen() {
  Value *RetVal = Expression->codegen();
  if (!RetVal)
    return nullptr;

  TheBuilder->CreateRet(RetVal);
  return RetVal;
}
```

## Block Codegen

In `BlockStatementNode::codegen`, I evaluate statements in order. I stop early if a `return` already terminated the current block, since statements after a `return` are unreachable. I also save and restore `NamedValues` around the block body, so variables declared inside the block with `var` don't leak to the outer scope:

```cpp
Value *BlockStatementNode::codegen() {
  auto SavedBindings = NamedValues;

  Value *Last = nullptr;
  for (auto &Statement : Statements) {
    if (TheBuilder->GetInsertBlock()->getTerminator())
      break;
    Last = Statement->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      return nullptr;
    }
  }

  NamedValues = SavedBindings;

  if (!Last)
    return LogErrorValue("Empty block");

  // Blocks are statement sequences. If control reaches the end without an
  // explicit return, the block's implicit value is always 0.0.
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

## `var` and Assignment Codegen

In `VariableStatementNode::codegen`, I allocate stack slots and initialize them. Since I already catch duplicate declarations in the same scope at parse time, codegen here just sets up the alloca and records the binding:

```cpp
Value *VariableStatementNode::codegen() {
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  for (auto &Var : VariableBindings) {
    const string &VariableName = Var.first;
    ExpressionNode *Init = Var.second.get();

    Value *InitVal = Init->codegen();
    if (!InitVal)
      return nullptr;

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VariableName);
    TheBuilder->CreateStore(InitVal, Alloca);
    NamedValues[VariableName] = Alloca;
  }

  return ConstantFP::get(*TheContext, APFloat(0.0)); // var statement produces 0.0
}
```

Assignment codegen is unchanged in substance from [chapter 11](chapter-11.md) — I load the alloca from `NamedValues`, store the new value, and return it. I only changed the class name, from `AssignmentExpressionNode` to `AssignmentStatementNode`, to match the grammar's new `assignment-statement` rule.

## `if` as a Statement

[Chapter 10](chapter-10.md) had `if` producing a value through a PHI node, with both `then` and `else` required. `IfStatementNode` doesn't need to produce a value, so I drop the PHI node and make `else` optional — if it's missing, the else block just falls through to the merge block:

```cpp
Value *IfStatementNode::codegen() {
  Value *CondV = Condition->codegen();
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
  FunctionPasses->run(*TheFunction, *FunctionAnalyses);
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

When a `def` body ends with an indented block, I return from parsing it with `CurrentToken = tok_block_end`. I handle that explicitly in `MainLoop`'s switch, so two definitions back to back with no blank line between them still work. I handle two more cases alongside it: a stray `tok_indent` at the top level (indentation where none was expected), which I report and skip, and a stray `tok_dedent` (an unmatched close, which can happen in REPL mode when a block is torn down early), which I silently consume:

```cpp
static void MainLoop() {
  while (CurrentToken != tok_eof) {
    switch (CurrentToken) {
    case tok_indent:
      LogErrorExpression("Unexpected indentation");
      DiscardRestOfLine();
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

I need `DiscardRestOfLine` and `HandleFunctionDefinition` to know about these new tokens too. `DiscardRestOfLine` used to stop only at `tok_eol` or `tok_eof`; I make it also stop at `tok_dedent` and `tok_block_end` now, since those tokens mark the true end of a malformed block, not just a line:

```cpp
static void DiscardRestOfLine() {
  // I stop before consuming tok_eol, tok_eof, tok_dedent, or
  // tok_block_end so MainLoop() can handle it.
  while (CurrentToken != tok_eol && CurrentToken != tok_eof &&
         CurrentToken != tok_dedent && CurrentToken != tok_block_end)
    getNextToken();
}
```

And in `HandleFunctionDefinition`'s trailing-token check, I now accept `tok_block_end` as a valid way for a definition to end, alongside `tok_eol` and `tok_eof` — a block-bodied `def` doesn't leave a newline behind, it leaves `tok_block_end`:

```cpp
static void HandleFunctionDefinition() {
  auto FunctionDefinition = ParseFunctionDefinition();
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof &&
                      CurrentToken != tok_block_end);
  if (!FunctionDefinition || HasTrailing) {
    if (FunctionDefinition)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)));
    DiscardRestOfLine();
    return;
  }
  // ...
}
```

## Top-Level Assignment

I no longer let `ParseExpression` understand `=` at all — I moved assignment to `ParseSimpleStatement` this chapter. But a top-level REPL line still goes through `ParseExpression`, by way of `ParseTopLevelExpression`, so I need that function to have its own handling to recognize `x = 1` and produce the right error instead of a generic parse failure. In `ParseTopLevelExpression`, I open a fresh, empty function scope (top-level input has no parameters), and check for a trailing `=` myself:

```cpp
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  FunctionScopeGuard Scope({});
  auto Expression = ParseExpression();
  if (!Expression)
    return nullptr;

  if (CurrentToken == tok_assign) {
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

  auto Signature = make_unique<FunctionSignatureNode>(
      AnonymousExpressionFunctionName, vector<string>());
  auto Body = make_unique<ReturnStatementNode>(std::move(Expression));
  return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
}
```

Since the scope is fresh and empty every time, `IsDeclaredVar` can never succeed for a top-level assignment — that's exactly the "Assignment to undeclared variable" limitation shown below, and this is the code path that produces it. I wrap the whole expression, assignment or not, in a `ReturnStatementNode` before handing it to `FunctionDefinitionNode`, so a top-level expression's value now reaches the caller through an explicit `return` rather than the old unconditional `CreateRet` at the end of `FunctionDefinitionNode::codegen`.

## Known Limitations

**No global variables.** `var` is only valid inside a function body — a top-level `var x = 10` is a parse error, since top-level input still goes through `ParseExpression`, which no longer understands `var` at all. Each top-level input also gets its own fresh function scope, so there's no way to declare a variable on one REPL line and reference it on the next:

```pyxc
# Does not work in the REPL:
var x = 10      # parse error — var is not an expression at the top level
x = x + 10      # x is undeclared in this expression's scope
printd(x)
```

For now, I keep mutable state inside a function:

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
    for var i = 1, i <= n, i = i + 1:
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

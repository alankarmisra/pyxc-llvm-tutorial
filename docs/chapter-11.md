---
description: "Switch from expression-only bodies to statement blocks with indentation, and make if/for/var/return statements."
---
# 11. Pyxc: Statement Blocks

## Where We Are

[Chapter 10](chapter-10.md) added mutable variables, but the function body was still a single expression:

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
ifstmt          = "if" expression ":" suite
                [ eols "else" ":" suite ] ;
forstmt         = "for" identifier "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = identifier "=" expression ;
simplestmt      = returnstmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" expression ;
block           = indent statement { eols statement } dedent ;
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

The key new rules:

- `suite` — what follows a `:`. Either a single statement on the same line, or a newline and an indented block.
- `simplestmt` — statements that fit on one line: `return`, `var`, assignment, or a bare expression.
- `compoundstmt` — statements that introduce a new suite: `if` and `for`.
- `block` — an `INDENT` token, one or more statements, a `DEDENT` token.

`INDENT` and `DEDENT` are synthetic — the lexer emits them, the parser never sees raw whitespace.

## Statements vs Expressions

Before this chapter, `if`, `for`, and `var` were expressions — they produced a value. That made function bodies a chain of nested expressions:

```python
var acc = 0: for i = 1, ...: acc = acc + i
```

The `:` after `var` was "evaluate the body under these bindings". The `:` after `for` was "evaluate this expression for each iteration". Both produced values. Both required nesting.

Statements don't produce values — they *do* things. Once `if`, `for`, `var`, and `return` are statements, a function body is a flat list of them rather than a deeply nested expression. The `:` after each one now introduces a `suite` — a block of further statements.

`ParseExpression` no longer handles `var`, `if`, `for`, or `=`. Those are all in `ParseStatement` and `ParseSimpleStmt` now, which keeps expression parsing clean.

## Indentation Rules

The lexer tracks indentation and emits `INDENT`/`DEDENT` tokens. The rules:

- Indentation is measured in columns.
- Tabs advance to the next multiple of 8 columns.
- Do not mix tabs and spaces — it is an error regardless of whether they line up.
- Blank lines and comment-only lines do not affect indentation.
- A block opens after `:` followed by a newline and a deeper indent level.

At end-of-file, the lexer flushes `DEDENT` tokens for every open block. In REPL mode, a blank line ends the current indented block — the same behavior as the Python REPL.

## Suites

`suite` is the key new concept. After every `:`, the parser calls `ParseSuite`:

```cpp
/// suite = simplestmt | compoundstmt | eols block ;
static unique_ptr<ExprAST> ParseSuite(bool *EndedWithBlock) {
  if (CurTok == tok_eol) {
    consumeNewlines();
    if (CurTok != tok_indent)
      return LogError("Expected an indented block");
    *EndedWithBlock = true;
    return ParseBlock();
  }
  auto Stmt = ParseStatement();
  if (!Stmt) return nullptr;
  *EndedWithBlock = LastStatementWasBlock;
  return Stmt;
}
```

If the next token is a newline, it expects an indented block. Otherwise it parses a statement on the same line. `ParseIfStmt` and `ParseForStmt` both call `ParseSuite` after eating `:`.

A `def` body works slightly differently — the inline form only accepts a `simplestmt`, not a compound statement. You cannot write `def f(x): if x > 0: return 1` on one line. The indented form takes a full block:

```cpp
/// definition = "def" prototype ":" ( simplestmt | eols block ) ;
static unique_ptr<FunctionAST> ParseDefinition() {
  // ...
  bool BodyIsBlock = false;
  unique_ptr<ExprAST> Body;
  if (CurTok == tok_eol) {
    consumeNewlines();
    BodyIsBlock = true;
    Body = ParseBlock();
  } else {
    Body = ParseSimpleStmt();
  }
  if (Body) {
    LastTopLevelEndedWithBlock = BodyIsBlock;
    return make_unique<FunctionAST>(std::move(Proto), std::move(Body));
  }
  return nullptr;
}
```

## INDENT and DEDENT

The lexer keeps an indentation stack and a pending-token queue. At the start of each line it measures the column, compares it to the stack, and pushes `INDENT` or `DEDENT` tokens into the queue:

```cpp
static vector<int> IndentStack = {0};
static deque<int> PendingTokens;
static bool AtLineStart = true;

static int gettok() {
  static int LastChar = ' ';

  if (!PendingTokens.empty()) {
    int Tok = PendingTokens.front();
    PendingTokens.pop_front();
    return Tok;
  }

  if (AtLineStart && LastChar == ' ')
    LastChar = advance();

  if (AtLineStart) {
    int IndentCol = 0;
    while (LastChar == ' ' || LastChar == '\t') {
      IndentCol += (LastChar == ' ') ? 1 : (8 - IndentCol % 8);
      LastChar = advance();
    }

    if (IndentCol > IndentStack.back()) {
      IndentStack.push_back(IndentCol);
      PendingTokens.push_back(tok_indent);
    } else if (IndentCol < IndentStack.back()) {
      while (IndentStack.size() > 1 && IndentCol < IndentStack.back()) {
        IndentStack.pop_back();
        PendingTokens.push_back(tok_dedent);
      }
    }

    AtLineStart = false;
    if (!PendingTokens.empty()) {
      int Tok = PendingTokens.front();
      PendingTokens.pop_front();
      return Tok;
    }
  }
  // ... normal token logic ...
}
```

A dedent can produce multiple `DEDENT` tokens — one for each level that closed. The queue holds them until the parser asks. `PendingTokens` is a `deque` so `pop_front()` is O(1).

## Blocks

`ParseBlock` consumes `INDENT`, then reads statements separated by newlines, then consumes `DEDENT`:

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
    if (LastStatementWasBlock) continue;
    return LogError("Expected newline or end of block");
  }

  getNextToken(); // eat DEDENT
  return make_unique<BlockExprAST>(std::move(Stmts));
}
```

`LastStatementWasBlock` handles the case where a nested compound statement ends without a newline — the enclosing block can keep parsing without one.

## Var and Assignment

`var` no longer wraps a body expression. It simply declares and initializes one or more mutable locals, which persist for the rest of the function:

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
    DeclareVar(Name);
    VarNames.push_back({Name, std::move(Init)});
    if (CurTok != ',') break;
    getNextToken(); // eat ','
  }
  return make_unique<VarStmtAST>(std::move(VarNames));
}
```

Assignment is parsed as part of `ParseSimpleStmt`. The parser checks at parse time that the target was declared — undeclared assignments are rejected before codegen:

```cpp
if (!IsDeclaredVar(Name))
  return LogError("Assignment to undeclared variable");
```

## Return and Implicit Return

`return` now emits a real LLVM terminator:

```cpp
Value *ReturnExprAST::codegen() {
  Value *RetVal = Expr->codegen();
  if (!RetVal) return nullptr;
  Builder->CreateRet(RetVal);
  return RetVal;
}
```

If the function body ends without a terminator — for example, an `if` with no `else` — codegen inserts an implicit `return 0.0`:

```cpp
Value *FunctionAST::codegen() {
  // ...
  Body->codegen();
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateRet(ConstantFP::get(*TheContext, APFloat(0.0)));
  // ...
}
```

That means this is valid:

```python
def threshold(x):
    if x > 10: return x
    # no explicit return on the else path — implicit return 0.0
```

## After a Top-Level Block

When a `def` ends with an indented block, the parser lands on the next top-level token — not a newline. Without special handling, `HandleDefinition` would treat that as a parse error. We track this with a flag:

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
- Assignment only works on existing variables; it does not create new ones. Undeclared assignments are rejected at parse time.
- `for` introduces a loop variable scoped to the loop body. `var` bindings persist for the rest of the function.
- The inline body of a `def` accepts only a `simplestmt`. Compound statements (`if`, `for`) require an indented block.
- The lexer's pending-token queue is a `deque`, so `pop_front()` is O(1).

## Try It

```python
ready> def f(x):
    if x > 10: return 20
    return 10
Parsed a function definition.
ready> f(5)
Parsed a top-level expression.
Evaluated to 10.000000
ready> f(20)
Parsed a top-level expression.
Evaluated to 20.000000
ready> def sum_to(n):
    var acc = 0
    for i = 1, i <= n, 1:
        acc = acc + i
    return acc
Parsed a function definition.
ready> sum_to(5)
Parsed a top-level expression.
Evaluated to 15.000000
```

Peek into `code/chapter-11/test/` for more examples, including nested blocks, implicit return, and multi-binding `var` statements.

## Build and Run

```bash
cmake -S . -B build
cmake --build build
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

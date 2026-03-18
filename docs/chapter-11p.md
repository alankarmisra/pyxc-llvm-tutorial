---
description: "Switch from expression-only bodies to statement blocks with indentation, and make if/for/var/return statements."
---
# 11. Pyxc: Statement Blocks

## Where We Are

[Chapter 10](chapter-10.md) added mutable variables, but only in expression form:

```python
var x = 1: x = x + 2
```

That was a pragmatic bridge, but it is not how Python reads. In this chapter we introduce real statement blocks and indentation-sensitive syntax. That unlocks `if` and `for` as statements, `return` as a proper statement, and a cleaner `var` statement.

Before this chapter, you were stuck writing bodies like this:

<!-- code-merge:start -->
```python
ready> def sum_to(n): return var acc = 0: for i = 1, i <= n, 1: acc = acc + i
```
```bash
Parsed a function definition.
```
<!-- code-merge:end -->

After this chapter, it becomes readable and Python-shaped:

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

Chapter 11 replaces expression bodies with statement blocks. `if`, `for`, `var`, and `return` are now statements. Assignment is a statement too, and expressions can still appear as top-level statements.

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

Key shifts:

- `if` and `for` are statements, not expressions.
- `var` is a statement and no longer accepts `: expression`.
- `return` is a statement.
- `=` is only allowed as a statement.

Expressions are now purely value-producing.

## Indentation Rules

Pyxc now treats indentation like Python. The lexer tracks indentation and emits synthetic `INDENT` and `DEDENT` tokens. The policy is intentionally strict:

- Indentation is measured in columns.
- Tabs advance to the next multiple of 8 columns.
- A single file must not mix tabs and spaces for indentation.
- Blank lines and comment-only lines do not affect indentation.
- A block begins after `:` followed by a newline and an increased indent.

The lexer emits `DEDENT` tokens as needed when indentation decreases, and flushes any remaining `DEDENT`s at end-of-file.

In REPL mode, a blank line ends the current indented block. This mirrors the Python REPL and makes it easy to finish a block without adding another statement.

## Statement Blocks

Blocks can be written on the same line or with indentation:

```python
# single-line suite
if x > 10: return 20

# indented suite
if x > 10:
    return 20
```

The same rule applies to `for` and function bodies. A `def` can take either a block or a single statement after the `:`.

## Return and Implicit Return

A `return` now emits a real terminator. If the function body does not end with a terminator, we insert an implicit `return 0.0` during codegen. That means this is valid:

```python
def f(x):
    if x > 10: return 20
    # no explicit return on the else path
```

The `if` does not need an `else`, and the function still returns a value.

## Implementation Tour

This section walks through the concrete C++ changes that power statement blocks.

### 1. Lexer: INDENT/DEDENT tokens

We add two synthetic tokens and keep an indentation stack. At the start of each line, we count spaces and tabs, compare indentation depth to the stack, and emit `INDENT` or `DEDENT` as needed.

```cpp
// Tokens
tok_indent = -19,
tok_dedent = -20,

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

  // If we're at the start of a line and LastChar is still the sentinel,
  // advance once so indentation logic sees the real character.
  if (AtLineStart && LastChar == ' ')
    LastChar = advance();

  if (AtLineStart) {
    int IndentCol = 0;
    while (LastChar == ' ' || LastChar == '\t') {
      if (LastChar == ' ')
        IndentCol += 1;
      else
        IndentCol += 8 - (IndentCol % 8);
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

Blank lines and comment-only lines are ignored for indentation. In REPL mode, a blank line emits `DEDENT` to end the current block.

### 2. Parsing statements vs expressions

Expressions no longer contain `if`, `for`, `var`, or assignment. Statements handle those forms explicitly, and `simplestmt` keeps single-line suites clean. Assignment to an undeclared variable is now rejected at parse time.

```cpp
/// statement = simplestmt | compoundstmt ;
static unique_ptr<ExprAST> ParseStatement() {
  LastStatementWasBlock = false;
  if (CurTok == tok_if)     return ParseIfStmt();
  if (CurTok == tok_for)    return ParseForStmt();
  return ParseSimpleStmt();
}

/// simplestmt = returnstmt | varstmt | assignstmt | expression ;
static unique_ptr<ExprAST> ParseSimpleStmt() {
  if (CurTok == tok_return) return ParseReturnStmt();
  if (CurTok == tok_var)    return ParseVarStmt();
  // ... expression / assignment parsing ...
}
```

This keeps `ParseExpression()` clean and avoids value-producing `if`/`for` hacks.

### 3. Parsing blocks

Blocks are `INDENT` ... `DEDENT`, with statements separated by `eols`. We also allow a statement to be followed immediately by another statement if the first one ended with a nested block.

```cpp
/// block = INDENT statement { eols statement } DEDENT ;
static unique_ptr<ExprAST> ParseBlock() {
  if (CurTok != tok_indent)
    return LogError("Expected an indented block");
  getNextToken(); // eat INDENT
  consumeNewlines();

  vector<unique_ptr<ExprAST>> Stmts;
  while (CurTok != tok_dedent) {
    auto Stmt = ParseStatement();
    if (!Stmt) return nullptr;
    Stmts.push_back(std::move(Stmt));

    if (CurTok == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurTok == tok_dedent) break;
    if (LastStatementWasBlock) continue;
    return LogError("Expected newline or end of block");
  }

  getNextToken(); // eat DEDENT
  return make_unique<BlockExprAST>(std::move(Stmts));
}
```

### 4. If and for as statements

`if` and `for` now parse a `suite` after `:`. A suite can be a single statement or an indented block.

```cpp
/// ifstmt = "if" expression ":" suite [ eols "else" ":" suite ] ;
static unique_ptr<ExprAST> ParseIfStmt() {
  getNextToken(); // eat 'if'
  auto Cond = ParseExpression();
  if (!Cond) return nullptr;
  if (CurTok != ':') return LogError("Expected ':' after if condition");
  getNextToken(); // eat ':'
  bool ThenIsBlock = false;
  auto Then = ParseSuite(&ThenIsBlock);
  if (!Then) return nullptr;

  consumeNewlines();

  unique_ptr<ExprAST> Else;
  bool ElseIsBlock = false;
  if (CurTok == tok_else) {
    getNextToken(); // eat 'else'
    if (CurTok != ':') return LogError("Expected ':' after else");
    getNextToken(); // eat ':'
    Else = ParseSuite(&ElseIsBlock);
    if (!Else) return nullptr;
  }

  LastStatementWasBlock = ThenIsBlock || ElseIsBlock;
  return make_unique<IfStmtAST>(std::move(Cond), std::move(Then), std::move(Else));
}
```

```cpp
/// forstmt = "for" identifier "=" expression "," expression "," expression ":" suite ;
static unique_ptr<ExprAST> ParseForStmt() {
  getNextToken(); // eat 'for'
  // ... parse loop header ...
  if (CurTok != ':') return LogError("Expected ':' after for step");
  getNextToken(); // eat ':'
  bool BodyIsBlock = false;
  auto Body = ParseSuite(&BodyIsBlock);
  if (!Body) return nullptr;

  LastStatementWasBlock = BodyIsBlock;
  return make_unique<ForExprAST>(VarName, std::move(Start), std::move(Cond),
                                 std::move(Step), std::move(Body));
}
```

### 5. Var statements and assignment

`var` introduces mutable locals but no longer wraps an expression body. Assignment is parsed as a statement and lowered into a store.
If a `var` binding omits an initializer, it defaults to `0.0`.

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
      Init = make_unique<NumberExprAST>(0.0);
    }
    VarNames.push_back({Name, std::move(Init)});
    if (CurTok != ',') break;
    getNextToken(); // eat ','
  }
  return make_unique<VarStmtAST>(std::move(VarNames));
}
```

### 6. Return and implicit return

Return is now a real terminator, and the function body inserts a default return if needed.

```cpp
Value *ReturnExprAST::codegen() {
  Value *RetVal = Expr->codegen();
  if (!RetVal) return nullptr;
  Builder->CreateRet(RetVal);
  return RetVal;
}
```

```cpp
Value *FunctionAST::codegen() {
  // ...
  Body->codegen();
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateRet(ConstantFP::get(*TheContext, APFloat(0.0)));
  // ...
}
```

### 7. Top-level parsing after blocks

When a `def` ends with an indented block, the parser may be sitting on the next top-level token (not a newline). We track this and relax the “must end on tok_eol/tok_eof” rule.

The inline body of a `def` is a `simplestmt` — not a full `statement`. You cannot write `def f(x): if x > 0: return 1` on one line; compound statements require an indented block.

`ParseSuite` is the glue that chooses between the two forms after every `:`:

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

`ParseDefinition` uses `ParseSimpleStmt` for the inline case and `ParseBlock` for the indented case:

```cpp
static bool LastTopLevelEndedWithBlock = false;

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

static void HandleDefinition() {
  auto FnAST = ParseDefinition();
  bool HasTrailing = (CurTok != tok_eol && CurTok != tok_eof);
  if (!FnAST || (HasTrailing && !LastTopLevelEndedWithBlock)) {
    // error recovery
  }
  // ...
}
```

This is what lets a file contain:

```python
def sum_to(n):
    var acc = 0
    for i = 1, i <= n, 1:
        acc = acc + i
    return acc

printd(sum_to(3))
```

## Parser Changes at a Glance

The parser now distinguishes between statements and expressions:

- `ParseStatement` handles `return`, `if`, `for`, `var`, assignment, and expression statements.
- `ParseBlock` consumes `INDENT`, then a list of statements separated by `eols`, then `DEDENT`.
- `ParseExpression` no longer parses `var`, `if`, `for`, or assignment.

This keeps expression parsing clean and shifts control flow into statement parsing.

## AST and Codegen Changes

New AST nodes:

- **ReturnExprAST** emits `ret` directly.
- **BlockExprAST** evaluates statements in order and stops after the first terminator.
- **IfStmtAST** builds control flow without a PHI node.
- **VarStmtAST** allocates and initializes mutable locals; bindings persist for the rest of the function.

Assignment is now handled as a statement that stores into an existing alloca.
In chapter 8, `if` was an expression and needed a PHI to produce a value. Now it's a statement, so there is no value to merge.

## Things Worth Knowing

- `var` without an initializer defaults to `0.0`.
- Declaring the same variable twice in the same function is a codegen error.
- Assignment only works on existing variables; it does not create new ones. Undeclared assignments are rejected at parse time.
- `for` introduces a loop variable that is scoped to the loop body.
- The lexer’s pending-token queue is a `deque`, so `pop_front()` is O(1).

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

## Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/pyxc
```

## What's Next

Statement blocks unlock more control-flow features. In the next chapter we can add `while`, `break`, and `continue`, and build more idiomatic examples without expression-level workarounds.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.

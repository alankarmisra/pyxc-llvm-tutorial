---
description: "Build a recursive-descent parser and AST: turn tokens into structure and see 'Parsed a function definition' for the first time."
---
# 2. Pyxc: The Parser and AST

## What We're Building

In Chapter 1 we built a lexer that turns raw text into tokens. Tokens are still just a flat stream — `'def' identifier '(' identifier ')' ':' 'return' identifier '+' identifier`. The parser's job is to understand what those tokens *mean* and build a tree structure that captures it.

By the end of this chapter, typing a function definition into the REPL gives you:

```
ready> def add(x, y):
ready>   return x + y
Parsed a function definition.
ready> extern def sin(x)
Parsed an extern.
ready> 1 + 2 * 3
Parsed a top-level expression.
```

No code runs yet. But the structure has been understood and validated. That's the foundation everything else builds on.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-02
```

## The Grammar

Before writing a parser, we need to define what we're parsing. Here's the grammar for Pyxc at this stage, in EBNF:

```ebnf
program        = [ top { eols top } ] [ eols ] ;
eols           = eol { eol } ;

top            = definition
                 | external
                 | expression;

definition     = "def" prototype ":" "return" expression;
external       = "extern" "def" prototype;
toplevelexpr   = expression;

prototype      = identifier "(" [ identifier { "," identifier } ] ")" ;

expression     = primary binoprhs ;

binoprhs       = { binaryop primary } ;

primary        = identifierexpr
                 | numberexpr
                 | parenexpr ;

identifierexpr = identifier
                 | identifier "(" [ expression { "," expression } ] ")" ;

numberexpr     = number ;
parenexpr      = "(" expression ")" ;

binaryop       = "<" | ">" | "+" | "-" | "*" | "/" | "%" ;

identifier     = (letter | "_") { letter | digit | "_" } ;
number         = digit { digit } [ "." { digit } ]
                 | "." digit { digit } ;

letter         = "A".."Z" | "a".."z" ;
digit          = "0".."9" ;
eol            = "\r\n"
                 | "\r" 
                 | "\n";

(*  
    `ws` may appear between any two tokens 
     and is ignored by the lexer.  
*)
ws             = " " | "\t" ;

```

A few things to notice:

- **`program`** allows blank lines between top-level items. `eols` is one or more `eol` tokens — the grammar explicitly accounts for them because our lexer emits `tok_eol` and the REPL loop handles them.
- A **`definition`** requires `return` in the body. Multi-statement bodies and indentation come later.
- An **`external`** is just a prototype with no body — a declaration of something that lives in a C library.
- **`binoprhs`** is named explicitly because it maps directly to `ParseBinOpRHS()`. It means "zero or more (operator, primary) pairs following the first primary."
- **`identifierexpr`** handles both plain variable references (`x`) and function calls (`foo(a, b)`) — the presence of `(` is the disambiguation.
- **`binaryop`** lists `>`, `/`, and `%` in the grammar even though they're not in `BinopPrecedence` yet. They're valid tokens that the lexer will return as ASCII values; adding them to the precedence table in a later chapter is all that's needed to activate them.
- The grammar spells out `identifier`, `number`, `letter`, `digit`, `eol`, and `ws` at the bottom — this is the part of the grammar owned by the *lexer*, not the parser. The parser never sees whitespace or raw characters; it only sees the tokens the lexer produces from them.

## The Abstract Syntax Tree

A parser could just validate syntax and discard everything. But we need to *do* something with the code later — generate IR, optimize it, run it. So we build an **Abstract Syntax Tree (AST)**: a tree of objects that represents the structure of the program.

"Abstract" means we drop syntax-only details. Parentheses in `(x + y)` are needed to parse correctly, but once we've built the tree the grouping is captured by structure — we don't need the `(` and `)` nodes anymore.

### The Node Classes

Every expression node inherits from `ExprAST`:

```cpp
class ExprAST {
public:
  virtual ~ExprAST() = default;
};
```

A number literal:

```cpp
class NumberExprAST : public ExprAST {
  double Val;
public:
  NumberExprAST(double Val) : Val(Val) {}
};
```

A variable reference:

```cpp
class VariableExprAST : public ExprAST {
  string Name;
public:
  VariableExprAST(const string &Name) : Name(Name) {}
};
```

A binary operation — stores the operator and its two operands:

```cpp
class BinaryExprAST : public ExprAST {
  char Op;
  unique_ptr<ExprAST> LHS, RHS;
public:
  BinaryExprAST(char Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};
```

A function call — stores the callee name and argument list:

```cpp
class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;
public:
  CallExprAST(const string &Callee, vector<unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};
```

Functions are represented by two classes. `PrototypeAST` captures the signature — name and parameter names:

```cpp
class PrototypeAST {
  string Name;
  vector<string> Args;
public:
  PrototypeAST(const string &Name, vector<string> Args)
      : Name(Name), Args(std::move(Args)) {}
  const string &getName() const { return Name; }
};
```

`FunctionAST` pairs a prototype with a body expression:

```cpp
class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;
public:
  FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
};
```

Note that `PrototypeAST` is not an `ExprAST` — a function signature isn't an expression. It stands alone, and `FunctionAST` owns it.

For `def add(x, y): return (x + y) * 2`, the tree looks like this:

```
FunctionAST
├── PrototypeAST  name="add"  args=["x", "y"]
└── BinaryExprAST  op='*'
    ├── BinaryExprAST  op='+'
    │   ├── VariableExprAST  name="x"
    │   └── VariableExprAST  name="y"
    └── NumberExprAST  val=2.0
```

## Parser Setup

The parser needs one token of lookahead — it always has the *current* token loaded and ready to inspect:

```cpp
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }
```

Every parse function assumes `CurTok` is already loaded when it is called, and leaves `CurTok` pointing at the first token it did not consume. This is the invariant that makes the whole parser work — once you know it, the code reads naturally.

### Operator Precedence

Binary expressions need precedence to parse correctly. Does `x + y * z` mean `(x+y)*z` or `x+(y*z)`? We want the second, so `*` must bind more tightly than `+`.

We store precedences in a map and look them up as we parse:

```cpp
static map<char, int> BinopPrecedence;

static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}
```

`-1` means "not a binary operator — stop here." The map is populated in `main()`:

```cpp
BinopPrecedence['<'] = 10;
BinopPrecedence['+'] = 20;
BinopPrecedence['-'] = 20;
BinopPrecedence['*'] = 40;  // highest
```

The `isascii` guard rejects our named token enums (which are negative integers) so they can never be mistaken for operators.

### Error Reporting

When parsing fails we return `nullptr` and print a message. Three overloads, one per return type:

```cpp
unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "ready> Error: %s (token: %d)\nready> ", Str, CurTok);
  return nullptr;
}
unique_ptr<PrototypeAST> LogErrorP(const char *Str) { LogError(Str); return nullptr; }
unique_ptr<FunctionAST>  LogErrorF(const char *Str) { LogError(Str); return nullptr; }
```

The `ready>` prefix and suffix keep the REPL prompt in sync. Without them, an error on a non-printing token (like `tok_eol`) would leave the cursor waiting with no prompt. We print the raw token number for now — Chapter 3 replaces this with readable names and source locations.

## Parsing Expressions

### Numbers

```cpp
static unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}
```

`NumVal` was set by the lexer when it returned `tok_number`. We snapshot it into a node, advance past the number, and return.

### Parentheses

```cpp
static unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat '('
  auto V = ParseExpression();
  if (!V)
    return nullptr;
  if (CurTok != ')')
    return LogError("expected ')'");
  getNextToken(); // eat ')'
  return V;
}
```

We parse whatever is inside, check for the closing `)`, and return the inner expression directly — no `ParenExprAST` node because the tree structure already captures the grouping.

### Identifiers and Calls

```cpp
static unique_ptr<ExprAST> ParseIdentifierExpr() {
  string IdName = IdentifierStr;
  getNextToken(); // eat identifier

  if (CurTok != '(')
    return make_unique<VariableExprAST>(IdName); // plain variable

  // Function call
  getNextToken(); // eat '('
  vector<unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')') break;

      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken(); // eat ','
    }
  }
  getNextToken(); // eat ')'
  return make_unique<CallExprAST>(IdName, std::move(Args));
}
```

After reading the identifier, we peek at the next token. No `(` means plain variable. A `(` means function call — we parse a comma-separated list of expressions until `)`.

### Primary

`ParsePrimary` dispatches to the right parser based on what token we're looking at:

```cpp
static unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  case tok_identifier: return ParseIdentifierExpr();
  case tok_number:     return ParseNumberExpr();
  case '(':            return ParseParenExpr();
  default:
    return LogError("unknown token when expecting an expression");
  }
}
```

## Parsing Binary Expressions

### The Precedence Climbing Algorithm

`ParseBinOpRHS` is the most subtle function in the parser. It handles chains of binary operators with correct precedence.

```cpp
static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();

    // Current operator doesn't bind tightly enough — return what we have.
    if (TokPrec < ExprPrec)
      return LHS;

    int BinOp = CurTok;
    getNextToken(); // eat operator

    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    // If the next operator binds more tightly than this one, let it take
    // our RHS first (right-recursive call).
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}
```

Walk through `a + b * c + d`:

1. `LHS = a`, operator is `+` (prec 20)
2. Parse `b` as `RHS`. Next operator is `*` (prec 40)
3. `*` binds tighter than `+` → recurse: `ParseBinOpRHS(21, b)`
4. Inside recursion: `LHS = b`, operator is `*`, parse `c`. Next is `+` (prec 20), which is < 21 → return `b*c`
5. Back in outer call: `RHS = b*c`, build `a + (b*c)`
6. Loop continues: next operator is `+` (prec 20 >= 0) → parse `d`, build `(a+(b*c)) + d`

The key insight: the `ExprPrec` parameter is a threshold. We only consume operators that bind at least that tightly. When a higher-precedence operator appears to our right, we recurse to let it grab its operands first.

`ParseExpression` kicks it off with threshold 0 (accept any operator):

```cpp
static unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}
```

## Parsing Function Definitions

### Prototype

```cpp
static unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");

  string FnName = IdentifierStr;
  getNextToken(); // eat function name

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");

  vector<string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);

    if (getNextToken() == ')')  // eat identifier, check what follows
      break;

    if (CurTok != ',')
      return LogErrorP("Expected ')' or ',' in parameter list");
    // loop continues: getNextToken() at the top eats the ','
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype");

  getNextToken(); // eat ')'
  return make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}
```

The argument loop calls `getNextToken()` at the top (eating `(` on the first pass, `,` on subsequent ones) and again inside the body (eating the identifier). The two advances per iteration let us check what comes after each parameter cleanly.

### Definition

```cpp
static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // Skip newlines so the body can be on the next line:
  //   def foo(x):
  //     return x + 1
  while (CurTok == tok_eol)
    getNextToken();

  if (CurTok != tok_return)
    return LogErrorF("Expected 'return' in function body");
  getNextToken(); // eat 'return'

  if (auto E = ParseExpression())
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}
```

The newline skip after `:` is important — it's what lets you split the definition across lines. Without it, `def foo(x):` on one line followed by `return x` on the next would fail because `CurTok` would be `tok_eol` where we expect `tok_return`.

### Extern and Top-Level Expressions

```cpp
static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat 'extern'
  if (CurTok != tok_def)
    return LogErrorP("Expected 'def' after extern");
  getNextToken(); // eat 'def'
  return ParsePrototype();
}
```

An `extern` is just a prototype with no body. We require `def` after `extern` to keep the syntax consistent with function definitions.

```cpp
static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = make_unique<PrototypeAST>("__anon_expr", vector<string>());
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}
```

A top-level expression like `1 + 2` gets wrapped in an anonymous function named `__anon_expr`. This isn't just a convenience — when we add JIT execution in a later chapter, we'll compile and call this function by name to get the result. The uniform `FunctionAST` representation means the rest of the pipeline doesn't need a special case for top-level expressions.

## The Driver

`MainLoop` dispatches on the leading token:

```cpp
static void MainLoop() {
  while (true) {
    if (CurTok == tok_eof)
      return;

    if (CurTok == tok_eol) {
      fprintf(stderr, "ready> ");
      getNextToken();
      continue;
    }

    switch (CurTok) {
    case tok_def:    HandleDefinition();         break;
    case tok_extern: HandleExtern();             break;
    default:         HandleTopLevelExpression(); break;
    }
  }
}
```

On success each handler prints a confirmation. On failure it skips one token — crude, but enough to keep the REPL alive after bad input without getting stuck on the same bad token forever.

`main()` sets up operator precedences, prints the first prompt, loads the first token, then hands off:

```cpp
int main() {
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  fprintf(stderr, "ready> ");
  getNextToken(); // prime CurTok before MainLoop

  MainLoop();
  return 0;
}
```

## Build and Run

```bash
cd code/chapter-02
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

```
ready> def add(x, y):
ready>   return x + y
Parsed a function definition.
ready> def fib(n):
ready>   return fib(n-1) + fib(n-2)
Parsed a function definition.
ready> extern def sin(x)
Parsed an extern.
ready> 1 + 2 * 3
Parsed a top-level expression.
ready> sin(1.0) + cos(2.0)
Parsed a top-level expression.
ready> def bad(x) return x
Error: Expected ':' in function definition (token: -7)
ready>
```

The parser understands structure. It accepts valid definitions and rejects malformed ones.

## What We Built

| Piece | What it does |
|---|---|
| AST node classes | Represent each kind of expression and declaration as an object |
| `CurTok` / `getNextToken()` | One-token lookahead buffer between lexer and parser |
| `BinopPrecedence` / `GetTokPrecedence()` | Drive operator precedence during expression parsing |
| `ParseBinOpRHS()` | Precedence-climbing algorithm for binary expressions |
| `ParsePrototype()` / `ParseDefinition()` | Parse function signatures and bodies |
| `ParseTopLevelExpr()` | Wrap bare expressions in `__anon_expr` for uniform handling |
| `MainLoop()` | REPL dispatch loop |

## Known Limitations

Two TODOs carried forward from chapter 1 are still present:

- `1.2.3` still lexes silently as `1.2` — the `.3` becomes a surprise for the parser
- Error messages show raw token numbers instead of names and source locations

Both get fixed in Chapter 3 when we polish the lexer and add proper diagnostics. Now that you've seen what parse errors look like, the motivation for `Error (line 3, col 7): Expected ')' near '+'` over `Error: Expected ')' (token: 20)` should be obvious.

## What's Next

The parser understands the structure of Pyxc code. But before we connect it to LLVM and generate actual IR, Chapter 3 revisits the lexer: better error messages, source locations, and the keyword map. The foundation we have works — Chapter 3 makes it pleasant to work with.

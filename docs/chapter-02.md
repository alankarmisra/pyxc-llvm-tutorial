---
description: "Build a recursive-descent parser and AST: turn tokens into structure and see 'Parsed a function definition' for the first time."
---
# 2. Pyxc: The Parser and AST

## Where We Are

In [Chapter 1](chapter-01.md) we built a lexer that turns raw source text into a stream of tokens. Given this:

```python
def add(x, y):
    return x + y
```

The lexer produces:

```
'def'  'add'  '('  'x'  ','  'y'  ')'  ':'  newline
'return'  'x'  '+'  'y'  newline
```

That's progress — the noise (whitespace, comments) is gone. But we still just have a flat list. We don't know that `add` is a function name, that `x` and `y` are its parameters, or that `x + y` is its return value.

By the end of this chapter, typing that same function into the REPL gives you:

```python
ready> def add(x, y):
ready>   return x + y
Parsed a function definition.
```

The structure has been understood and validated. That's what we're building.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-02
```

## Writing Down the Rules

Before we can write a parser, we need to write down the rules of the language. What's a valid program? What's a valid function? What's a valid expression?

Let's start informally. A function definition looks like:

```text
the word "def", then a function name, then a parameter list in parentheses,
then a colon, then "return", then an expression
```

A function call looks like:

```text
a name, then "(" then zero or more expressions separated by commas, then ")"
```

An expression is one of:
- a number
- a variable name
- a parenthesized expression
- two of the above joined by an operator like `+`, `-`, `*`, `<`

That's the informal version. Now let's tighten it up.

### A Compact Notation for Grammar Rules

It gets tedious to write grammar rules in plain English. Let's invent a shorthand. We'll use:

- Quoted text like `"def"` for exact keywords and punctuation
- Unquoted words like `identifier` or `expression` for "fill in a valid thing of this kind here"
- `|` to mean "or" — either this or that
- `[ ... ]` to mean "optionally — zero or one of these"
- `{ ... }` to mean "zero or more of these, repeated"

With that shorthand, a function definition becomes:

```ebnf
definition = "def" prototype ":" "return" expression
```

And a prototype (just the signature — name and parameters):

```ebnf
prototype = identifier "(" [ identifier { "," identifier } ] ")"
```

And an expression:

```ebnf
expression = primary { binaryop primary }
```

Where `primary` is the building block — a number, a variable, or a parenthesized expression:

```ebnf
primary = identifier [ "(" [ expression { "," expression } ] ")" ]
        | number
        | "(" expression ")"
```

This notation — named rules, `|`, `[ ]`, `{ }` — has a formal name: **EBNF**, short for Extended Backus-Naur Form. It's the standard way grammars are written in programming language textbooks. Now you've seen why it looks the way it does: it's just a compact version of the English description we started with.

### The Full Grammar

Here's the complete grammar for Pyxc at this stage:

```ebnf
program        = [ top { eols top } ] [ eols ] ;
eols           = eol { eol } ;

top            = definition
               | external
               | expression ;

definition     = "def" prototype ":" "return" expression ;
external       = "extern" "def" prototype ;

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

binaryop       = "<" | "+" | "-" | "*" ;

identifier     = (letter | "_") { letter | digit | "_" } ;
number         = digit { digit } [ "." { digit } ]
               | "." digit { digit } ;

letter         = "A".."Z" | "a".."z" ;
digit          = "0".."9" ;
eol            = "\r\n" | "\r" | "\n" ;

(* ws may appear between any two tokens and is ignored by the lexer *)
ws             = " " | "\t" ;
```

A few things to notice:

- The grammar has two layers. The bottom rules — `identifier`, `number`, `letter`, `digit`, `eol`, `ws` — describe what the *lexer* understands: raw characters. The top rules — `expression`, `definition`, `prototype`, etc. — describe what the *parser* understands: tokens. The parser never sees individual characters; it only sees the tokens the lexer already produced.

- `definition` requires `return`. Multi-statement bodies and indentation come in later chapters.

- `external` is just a prototype with no body — a declaration for something that lives in a C library (like `sin` or `cos`). No body means we're just telling the compiler the name and parameter count.

- `binoprhs` is named explicitly because it maps directly to a function called `ParseBinOpRHS`. It means "zero or more (operator, primary) pairs following the first primary." The name will make sense when we see the code.

- `identifierexpr` handles both plain variable references (`x`) and function calls (`foo(a, b)`). The presence of `(` is the only difference — and the parser can tell them apart by looking at the very next token.

### How the Parser Chooses

Look at the rule for `top`:

```ebnf
top = definition | external | expression
```

How does the parser know which branch to take? It looks at the current token:

- `def` → must be a `definition`
- `extern` → must be an `external`
- anything else → try `expression`

Each option starts with a different token, so the parser can decide immediately, with just one token of lookahead. This style — where you can always pick the right branch by looking at the next token — is called **top-down, one-token-lookahead** parsing (or LL(1) if you want the textbook name).

### Avoiding a Pitfall: Left Recursion

There's a rule you can't write for a top-down parser: a rule that starts with itself.

```ebnf
(* DON'T DO THIS *)
expression = expression binaryop primary
```

The parser would try to parse `expression`, which requires parsing `expression`, which requires parsing `expression`... infinite recursion, immediate crash.

The fix is to use `{ ... }` (iteration) instead of recursion:

```ebnf
expression = primary { binaryop primary }
```

"Parse one primary, then loop and grab (operator, primary) pairs until there are no more." Same language, no recursion. Our grammar above always does this.

## Representing Structure

A parser could just *validate* syntax — accept or reject the input — and throw everything away. But we need to *do* something with the code: generate machine instructions, optimize it, run it. So instead of discarding what we parse, we build up a structured representation we can work with later.

Think about a file system. A directory can contain files and other directories. You can navigate it, count items in it, search it, copy subtrees of it. It's a *tree* — a root node with children, each child potentially having more children, with leaves (files) at the bottom.

Code has the same shape. The expression `(x + y) * 2` breaks down like this:

```
multiply (*)
├── add (+)
│   ├── variable "x"
│   └── variable "y"
└── number 2.0
```

The multiply node has two children: an add node (which itself has two children) and a number node. The leaves are variables and numbers. The structure — which operation is nested inside which — captures the meaning of the expression without needing the parentheses anymore.

For a full function definition `def add(x, y): return (x + y) * 2`, the structure is:

```
FunctionAST
├── PrototypeAST  name="add"  args=["x", "y"]
└── BinaryExprAST  op='*'
    ├── BinaryExprAST  op='+'
    │   ├── VariableExprAST  name="x"
    │   └── VariableExprAST  name="y"
    └── NumberExprAST  val=2.0
```

We call this an **Abstract Syntax Tree** — "abstract" because we've stripped away the syntax details that were only needed for parsing (like the parentheses and the colon). What remains captures the *meaning* without the noise.

### The Node Classes

We represent each kind of node as a C++ class. All expression nodes inherit from a common base:

```cpp
class ExprAST {
public:
  virtual ~ExprAST() = default;
};
```

The virtual destructor is all we need for now. Later chapters will add virtual methods for code generation.

A number literal stores its value:

```cpp
class NumberExprAST : public ExprAST {
  double Val;
public:
  NumberExprAST(double Val) : Val(Val) {}
};
```

A variable reference stores its name:

```cpp
class VariableExprAST : public ExprAST {
  string Name;
public:
  VariableExprAST(const string &Name) : Name(Name) {}
};
```

A binary operation stores the operator character and its two operands. The operands are owned by the node — when the node is destroyed, the operands are too:

```cpp
class BinaryExprAST : public ExprAST {
  char Op;
  unique_ptr<ExprAST> LHS, RHS;
public:
  BinaryExprAST(char Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};
```

A function call stores the callee name and a list of argument expressions:

```cpp
class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;
public:
  CallExprAST(const string &Callee, vector<unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};
```

Functions are split into two classes. The prototype captures just the signature — name and parameter names. We need it separately because `extern` declarations have a prototype but no body:

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

Notice `PrototypeAST` doesn't inherit from `ExprAST` — a function signature is not an expression.

The full function definition pairs a prototype with a body expression:

```cpp
class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;
public:
  FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
};
```

## The Parser

### The Lookahead Invariant

The parser needs to look at the current token to decide what to do. We keep one token of lookahead in a global:

```cpp
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }
```

Every parse function operates by this invariant: **`CurTok` is already loaded when the function is called, and when the function returns, `CurTok` is pointing at the first token it did not consume.**

In other words: the function that calls you is responsible for loading `CurTok` before the call. You eat what you need, and leave the next thing for whoever called you.

Once you internalize this rule, all the `getNextToken()` calls in the parser make immediate sense.

### Error Reporting

When parsing fails, we return `nullptr` and print a message. We need three overloads — one per return type — because C++ can't overload on return type:

```cpp
unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s (token: %d)\nready> ", Str, CurTok);
  return nullptr;
}
unique_ptr<PrototypeAST> LogErrorP(const char *Str) { LogError(Str); return nullptr; }
unique_ptr<FunctionAST>  LogErrorF(const char *Str) { LogError(Str); return nullptr; }
```

The `ready>` at the end keeps the REPL prompt in sync. If a parse error occurs on a non-printing token (like a bare newline), the main loop won't print a new prompt — so we print it here. Chapter 3 replaces the raw token number with a readable token name and source location.

### Operator Precedence

Binary expressions can be ambiguous. Does `x + y * z` mean `(x+y)*z` or `x+(y*z)`? We want the second — `*` should bind more tightly than `+`.

We store precedences in a map. Higher number means tighter binding:

```cpp
static map<char, int> BinopPrecedence;
```

Populated in `main()`:

```cpp
BinopPrecedence['<'] = 10;
BinopPrecedence['+'] = 20;
BinopPrecedence['-'] = 20;
BinopPrecedence['*'] = 40;  // highest
```

A helper returns the precedence of whatever is in `CurTok`, or `-1` if it's not a known binary operator:

```cpp
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}
```

The `isascii` guard rejects our named token enums (which are negative integers) so they can never be mistaken for operators.

## Parsing Expressions

### Numbers

When the lexer returns `tok_number`, it has already set the global `NumVal`. We snapshot it into a node and advance:

```cpp
/// numberexpr
///   = number ;
static unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}
```

### Parentheses

Parse whatever is inside, verify the closing `)`, and return the inner expression directly. We don't create a parentheses node — the tree structure already captures the grouping:

```cpp
/// parenexpr
///   = "(" expression ")" ;
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

### Identifiers and Calls

After reading an identifier, we peek at the next token. No `(` means it's a plain variable. A `(` means it's a function call:

```cpp
/// identifierexpr
///   = identifier
///   | identifier "("[expression{"," expression}]")" ;
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

### Dispatching to the Right Parser

`ParsePrimary` looks at `CurTok` and delegates:

```cpp
/// primary
///   = identifierexpr
///   | numberexpr
///   | parenexpr ;
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

### Binary Expressions: Precedence Climbing

The most subtle function in the parser is `ParseBinOpRHS`. It handles chains of binary operators with correct precedence.

The key idea: when we're parsing `a + b * c + d`, we need to figure out which operators go together. The `*` between `b` and `c` binds more tightly than the `+` around it, so `b * c` should be grouped first.

We solve this with a **threshold**: `ParseBinOpRHS` takes a minimum precedence level and only consumes operators at or above that level. When it sees a higher-precedence operator to its right, it recurses to let that operator grab its operands first.

```cpp
/// binoprhs
///   = { binaryop primary } ;
static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If CurTok is not an operator, or binds less tightly than our threshold,
    // we're done — return what we have.
    if (TokPrec < ExprPrec)
      return LHS;

    int BinOp = CurTok;
    getNextToken(); // eat operator

    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    // If the next operator binds more tightly than the current one, recurse
    // to let it take our RHS as its LHS first.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    // Merge LHS and RHS under the current operator.
    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}
```

Let's trace `a + b * c + d` step by step:

1. Called with threshold 0, `LHS = a`. Current operator is `+` (prec 20).
2. 20 ≥ 0 — consume `+`. Parse `b` as `RHS`. Next operator is `*` (prec 40).
3. 40 > 20 — recurse: `ParseBinOpRHS(21, b)`.
4. Inside recursion: current operator is `*` (prec 40). 40 ≥ 21 — consume `*`. Parse `c`. Next is `+` (prec 20). 20 < 21 — stop. Return `b*c`.
5. Back in outer call: `RHS = b*c`. Build `a + (b*c)`.
6. Loop continues. Next operator is `+` (prec 20). 20 ≥ 0 — consume `+`. Parse `d`. Next token is not an operator — return. Build `(a+(b*c)) + d`.

The `TokPrec + 1` in the recursive call enforces left-associativity. For operators at the same level — like `a - b - c` — we want `(a-b)-c`, not `a-(b-c)`. The `+1` threshold means the recursive call will stop when it sees an operator at the same level, leaving it for the outer loop to consume.

`ParseExpression` kicks everything off with threshold 0 (accept any operator):

```cpp
/// expression
///   = primary binoprhs ;
static unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}
```

## Parsing Function Definitions

### Prototype

A prototype is the function's signature: name and parameter names (no types yet — everything is `double` for now).

```cpp
/// prototype
///   = identifier "(" [identifier {"," identifier}] ")" ;
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

The argument loop uses two `getNextToken()` calls per iteration. The one at the top of the `while` eats `(` on the first iteration and `,` on later ones. The one inside the body eats the identifier and lands on what follows — either `)` to break or `,` to loop again.

### Definition

```cpp
/// definition
///   = "def" prototype ":" ["newline"] "return" expression ;
static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // Skip newlines between ':' and 'return'. This lets you write:
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

The newline skip after `:` is what makes multi-line definitions work. Without it, the REPL would print `ready>` for the second line, the user types `return x + 1`, but `CurTok` would be `tok_eol` — not `tok_return` — and the parse would fail.

### Extern Declarations

```cpp
/// external
///   = "extern" "def" prototype
static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat 'extern'
  if (CurTok != tok_def)
    return LogErrorP("Expected 'def' after extern.");
  getNextToken(); // eat 'def'
  return ParsePrototype();
}
```

An `extern` is just a prototype — we're declaring a name and its parameter count so the compiler knows how to call it. The actual implementation lives elsewhere (a C library, another object file).

### Top-Level Expressions

A bare expression typed at the REPL — like `1 + 2 * 3` — gets wrapped in an anonymous function:

```cpp
/// toplevelexpr
///   = expression
static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = make_unique<PrototypeAST>("__anon_expr", vector<string>());
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}
```

The name `__anon_expr` is a placeholder. In a later chapter when we add JIT execution, we'll look up this function by name and call it to evaluate the expression immediately. Wrapping it in `FunctionAST` now means the rest of the pipeline — code generation, optimization, JIT — doesn't need any special cases for top-level expressions.

## The Driver

Three handler functions, one for each top-level construct. Each calls the appropriate parser, prints a success message, or skips one bad token to keep the REPL alive:

```cpp
static void HandleDefinition() {
  if (ParseDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // skip bad token
}

static void HandleExtern() {
  if (ParseExtern())
    fprintf(stderr, "Parsed an extern.\n");
  else
    getNextToken(); // skip bad token
}

static void HandleTopLevelExpression() {
  if (ParseTopLevelExpr())
    fprintf(stderr, "Parsed a top-level expression.\n");
  else
    getNextToken(); // skip bad token
}
```

`MainLoop` dispatches on the leading token:

```cpp
static void MainLoop() {
  while (true) {
    if (CurTok == tok_eof)
      return;

    // A bare newline: print a fresh prompt and advance.
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

`main()` sets up operator precedences, prints the first prompt, loads the first token, then hands off to the loop:

```cpp
int main() {
  // Register binary operators. Higher number = tighter binding.
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  // Print the first prompt and load the first token before entering the loop.
  // Every parse function expects CurTok to already be loaded when it is called.
  fprintf(stderr, "ready> ");
  getNextToken();

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

```python
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

The parser accepts valid syntax and rejects invalid syntax with an error message. The REPL keeps running after errors.

## What We Built

| Piece | What it does |
|---|---|
| Grammar (EBNF) | Defines exactly what valid Pyxc looks like |
| AST node classes | Represent each kind of expression as a C++ object |
| `CurTok` / `getNextToken()` | One-token lookahead between lexer and parser |
| `BinopPrecedence` / `GetTokPrecedence()` | Control how operators bind |
| `ParseBinOpRHS()` | Precedence-climbing algorithm for binary expressions |
| `ParsePrototype()` / `ParseDefinition()` | Parse function signatures and bodies |
| `ParseTopLevelExpr()` | Wrap bare expressions in `__anon_expr` for uniform handling |
| `MainLoop()` | REPL dispatch loop |

## Known Limitations

The TODOs from Chapter 1 are still present:

- `1.2.3` still lexes as `1.2` — the lexer silently drops `.3`, which will confuse the parser
- Error messages show raw token numbers (`token: -7`) instead of readable names and source locations

Both get fixed in Chapter 3, when we polish the lexer and add proper diagnostics. Now that you've written a few function definitions and hit a few errors, you'll understand exactly *why* `Error (line 3, col 15): Expected ':' near 'return'` is worth the effort.

## What's Next

The parser understands the structure of Pyxc code and builds a tree of objects representing it. But before we hook this up to LLVM and generate real machine code, Chapter 3 revisits the lexer: readable error messages, source locations, and the keyword map. The parser you have works — Chapter 3 makes it pleasant to use.

---
description: "Build a recursive-descent parser and AST: turn tokens into structure and see 'Parsed a function definition' for the first time."
---
# 2. pyxc: The Parser and AST

## Where We Are

In [Chapter 1](chapter-01.md) I built a lexer that turns raw source text into a stream of tokens. Given this:

```pyxc
# adds two numbers
def add(x, y):    
    return x + y
```

The lexer produces:

```
keyword:'def'  identifier:'add'  '('  identifier:'x'  ','  identifier:'y'  ')'  ':'  newline
keyword:'return'  identifier:'x'  '+'  identifier:'y'  newline
```

That's progress — the whitespace and comments are gone, and I can focus on the essentials. But it's still just a flat list of tokens. I don't know that `add` is a function name, that `x` and `y` are its parameters, or that `x + y` is the return value.

By the end of this chapter, typing that same function into the REPL gives me:

```pyxc
ready> def add(x, y):
return x + y
Parsed a function definition.
```

That's my compiler saying, "The structure has been understood and validated." That's what I'm building this chapter.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-02
```

## Writing Down the Rules

Before I can write a parser — that bit of the compiler that verifies the syntax of a program — I need to write down the rules of the language. These rules are called the *grammar* of the language just like they are with human languages. What's a valid program? What's a valid function? What's a valid expression? Grammars are meant to give more structure to implementation efforts, but if you've never written a parser, it will just feel like a lot of cognitive load. As a consequence, my recommendation is to skim through the grammar section, have a vague language structure in your head, write the parser to fortify your understanding of the mechanics, and then come back to the grammar section to polish off your theoretical concepts. You might find yourself referencing the grammar more than once as you proceed through the parser implementation. I've put grammar snippets everywhere to reduce scroll fatigue. Once you've had enough experience, it will become second nature to write the grammar first before moving on to the implementation.

Let's start with one rule in plain English. A function definition looks like:

```text
the word "def", followed by a function name, a parameter list in parentheses,  
a colon, optional newlines, "return", and finally, an expression.
```

Yeah, I don't want to write a language spec like that. So let's tighten it up and invent a shorthand.

### A Compact Notation for Grammar Rules

I've invented the following notation for myself. The notation isn't set in stone. Nothing is, except maybe the physical laws. And don't let them fool you into believing that taxes are inevitable either. Now, you could invent your own notation and that's totally fine. But I'll use my notation for the rest of the tutorial, so maybe just familiarize yourself with this one. You can discard it from your human memory once you're done with the tutorial and replace it with something you prefer.

- Quoted text like `"def"` for exact keywords and punctuation
- Unquoted words like `identifier` or `expression` for "fill in a valid thing of this kind here"
- `|` to mean "or" — either this or that
- `[ ... ]` to mean "optionally — zero or one of these"
- `{ ... }` to mean "zero or more of these, repeated"

With that shorthand, a function definition becomes:

```ebnf
definition = "def" prototype ":" [ eols ] "return" expression
```

In plain English: *the keyword `def`, then a prototype, then `:`, then optionally one or more newlines, then `return`, then an expression* — exactly what I wrote above, just more compact. Notice that `prototype` and `expression` are without quotes — they are themselves rules, not literal text. The grammar is written top-down: I expand on `prototype` and `expression` below, and the full grammar appears at the end of this section.

Here's `prototype` which is just the function signature.

```ebnf
prototype = identifier "(" [ identifier { "," identifier } ] ")"
```

In plain English: *a function name (an identifier), then `(`, then optionally one or more comma-separated parameter names (also identifiers), then `)`*.

And `expression`:

```ebnf
expression = primary { binaryop primary }
```

Where `primary` is the building block — a variable, a number, or a parenthesized expression:

```ebnf
primary = identifierexpr | numberexpr | parenexpr ;
```

I totally lied about inventing this notation. It is used ubiquitously and has a formal name: **EBNF**, short for *Extended Backus-Naur Form*. But if I said that in the beginning, you'd think it's complicated. So I went the re-invention route. It's the standard way grammars are written in programming language textbooks with a few customizations based on author preferences. You can do this too. There is nothing magical about any of this. I've always had issues with theoretical foundations being written like they were invented by aliens with massive brains. Sure, the inventions were incredibly forward looking at the time, and all credit to the inventors. But placing things on a pedestal makes them opaque to scrutiny and recognizing that they aren't as complicated as one was made to believe. Meet your Gods. Recognize they are human. Respect them anyway.

### The Full Grammar

Here's the complete grammar for Pyxc at this stage.  

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-02/pyxc.ebnf)

```ebnf
(* parser territory *)
program        = [ eols ] [ top { eols top } ] [ eols ] ;
eols           = eol { eol } ;
top            = definition | external | toplevelexpr ;
definition     = "def" prototype ":" [ eols ] "return" expression ;
external       = "extern" "def" prototype ;
toplevelexpr   = expression ;
prototype      = identifier "(" [ identifier { "," identifier } ] ")" ;
expression     = primary binoprhs ;
binoprhs       = { binaryop primary } ;
primary        = identifierexpr | numberexpr | parenexpr ;
identifierexpr = identifier
                 | identifier "(" [ expression { "," expression } ] ")" ;
numberexpr     = number ;
parenexpr      = "(" expression ")" ;

(* lexer territory *)
binaryop       = "+" | "-" | "*" | "<" ;
identifier     = (letter | "_") { letter | digit | "_" } ;
number         = digit { digit } [ "." { digit } ]
                 | "." digit { digit } ;
letter         = "A".."Z" | "a".."z" ;
digit          = "0".."9" ;
eol            = "\r\n" | "\r" | "\n" ;
(*  
    `ws` may appear between any two tokens 
     and is ignored by the lexer.  
*)
ws             = " " | "\t" ;
```

The grammar has two layers. The bottom rules — `identifier`, `number`, `letter`, `digit`, `eol`, `ws` — describe what the *lexer* understands: raw characters and how they combine to form tokens. The top rules — `expression`, `definition`, `prototype`, etc. — describe what the *parser* understands: the syntax of things. What token follows what other token and so on.

## Representing Structure

Look at the expression `(x + y) * 2`. You and I both know that the correct thing to do is to first add, then multiply. So I put all the items I want to add in one *bucket* or *folder* with an instruction to add the contents. 

```
├── add (+)
│   ├── variable "x"
│   └── variable "y"
```

And then I put the result of that into a multiply bucket with the parameters that need to be multiplied with the result.

```
multiply (*)
├── add (+)
│   ├── variable "x"
│   └── variable "y"
└── number 2.0
```

Historically, such constructions have been represented as:

```
        (*)
       /   \
     (+)   2.0
    /   \
  "x"   "y"
```  

Looks like an upside-down tree no? With the root at the (\*), tiny branches, and the parameters/function arguments/constants at the leaves. I'm not being creative. Computational literature uses exactly this analogy. This whole construction is called an **Abstract Syntax Tree** — "abstract" because I've stripped away the syntax details that were only needed for parsing (like the parentheses and the colon). What remains captures the *meaning* without the noise.

### The Node Classes

I represent each kind of node as an *expression* class:

```cpp
class ExprAST {
public:
  virtual ~ExprAST() = default;
};
```

The virtual destructor is all I need in the base class for now. 

A number literal stores its value as a double, because I only support doubles for now:

```cpp
class NumberExprAST : public ExprAST {
  double Val;
public:
  NumberExprAST(double Val) : Val(Val) {}
};
```

A variable reference stores only its name:

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

Functions are split into two classes. The prototype captures just the signature — name and parameter names. I need it separately because `extern` declarations have a prototype but no function body:

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

In terms of AST, for a function definition like `def add(x, y): return (x + y) * 2`, the structure is:

```
FunctionAST
├── PrototypeAST  name="add"  args=["x", "y"]
└── BinaryExprAST  op='*'
    ├── BinaryExprAST  op='+'
    │   ├── VariableExprAST  name="x"
    │   └── VariableExprAST  name="y"
    └── NumberExprAST  val=2.0
```

## The Parser

### The Lookahead Invariant

The parser needs to look at the current token to decide what to do. I keep one token of lookahead in a global. There are parsing strategies that use more than one token to determine future action, but I've designed the grammar in a way that one token is sufficient:

```cpp
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }
```

A value that doesn't change is called a constant (or immutable, depending on the language).
A *fact about the program* that doesn't change is called an **invariant**.

Every parse function operates by this invariant: *`CurTok` is already loaded when the function is called, and when the function returns, `CurTok` is pointing at the first token it did not consume.* This is always true in my parser. 

In other words: the function that calls you is responsible for loading `CurTok` before the call. You eat what you need, and leave the next thing for whoever called you.

Once you internalize this rule, all the `getNextToken()` calls in the parser make immediate sense.

### Error Reporting

When parsing fails, I return `nullptr` and print a message. I need three overloads — one per return type — because C++ can't overload on return type:

```cpp
unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s (token: %d)\nready> ", Str, CurTok);
  return nullptr;
}
unique_ptr<PrototypeAST> LogErrorP(const char *Str) { LogError(Str); return nullptr; }
unique_ptr<FunctionAST>  LogErrorF(const char *Str) { LogError(Str); return nullptr; }
```

You haven't seen the main loop yet, but if a parse error occurs at the end of a line, the main loop won't print a new prompt — so I print it in LogError. This is one of those 'Trust me bro' moments. But I will explain it later, I promise.

[Chapter 3](chapter-03.md) replaces the raw token number with a readable token name and source location.

### Operator Precedence

Since binary expressions can be ambiguous (does `x+y*z` mean `(x+y)*z` or `x+(y*z)` ?) I have to tell the compiler that `*` should *bind* more tightly than `+`. *Bind more tightly* is a fancy way of saying *compute before others*. I only use *binding* because compiler literature uses it. I use numbers to decide the binding order, and the number is called **precedence**. If you know C++, you know about operator precedence tables, so accuse me of *obvious-splaining* or *o-splaining* if you will, another term I just invented. It's easy to invent things. 

I store precedences in a map. Higher precedence means tighter binding:

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

    auto It = BinopPrecedence.find(CurTok);
    if (It == BinopPrecedence.end() || It->second <= 0)
        return -1;
    return It->second;
}
```

The `isascii` guard rejects my named `Token` enums (which are negative integers) so they can never be mistaken for operators.

## Parsing Expressions

### Numbers

When the lexer returns `tok_number`, it has already set the global `NumVal`. I copy its current value into a node and advance:

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

Parse whatever is inside, verify the closing `)`, and return the inner expression directly. I don't create a parentheses node — the tree structure already captures the grouping:

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

After reading an identifier, I peek at the next token. No `(` means it's a plain variable. A `(` means it's a function call.

```pyxc
x     # variable
foo() # function call
```

Here's the parsing code.

```cpp
/// identifierexpr
///   = identifier
///   | identifier "("[expression{"," expression}]")" ;
static unique_ptr<ExprAST> ParseIdentifierExpr() {
  string IdName = IdentifierStr;

  getNextToken(); // eat identifier.

  if (CurTok != '(') // Simple variable ref.
    return make_unique<VariableExprAST>(IdName);

  // Call.
  getNextToken(); // eat (
  vector<unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return make_unique<CallExprAST>(IdName, std::move(Args));
}
```

### Calling the Right (mini) Parser

I have all my mini-parsers ready for different token types. `ParsePrimary` looks at `CurTok` and, based on what it sees, invokes the relevant mini-parser:

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

The most subtle function in the parser is `ParseBinOpRHS`. It handles a sequence of binary operators with correct precedence.

The key idea: when I'm parsing `a + b * c + d`, I need to figure out which operators go together. The `*` between `b` and `c` binds more tightly than the `+` around it, so `b * c` should be grouped first.

I solve this by setting a minimum precedence. `ParseBinOpRHS` is told: only deal with operators at this precedence level or higher. If it sees a higher-precedence operator on the right, it steps aside (recurses) and lets that operator take its operands first.

```cpp
/// binoprhs
///   = { binaryop primary } ;
static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                         unique_ptr<ExprAST> LHS) {
  // If this is a binop, find its precedence.
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < ExprPrec)
      return LHS;

    // Okay, we know this is a binop.
    int BinOp = CurTok;
    getNextToken(); // eat binop

    // Parse the primary expression after the binary operator.
    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    // Merge LHS/RHS.
    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}
```

Let's trace `a + b * c + d` step by step:

1. Called with precedence level of 0, `LHS = a`. Current operator is `+` (prec 20).
2. 20 ≥ 0 — consume `+`. Parse `b` as `RHS`. Next operator is `*` (prec 40).
3. 40 > 20 — recurse: `ParseBinOpRHS(21, b)`.
4. Inside recursion: current operator is `*` (prec 40). 40 ≥ 21 — consume `*`. Parse `c`. Next is `+` (prec 20). 20 < 21 — stop. Return `b*c`.
5. Back in outer call: `RHS = b*c`. Build `a + (b*c)`.
6. Loop continues. Next operator is `+` (prec 20). 20 ≥ 0 — consume `+`. Parse `d`. Next token is not an operator — return. Build `(a+(b*c)) + d`.

The `TokPrec + 1` makes sure operators group from left to right.

For operators at the same level — like `a - b - c` — I want `(a - b) - c`, not `a - (b - c)`.

The `+1` means the recursive call stops when it sees another operator at the same level, leaving it for the outer loop to handle.

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

Do you see how straightforward this is? I just look at the grammar and sequence out the commands to parse those bits. Could writing this parser be automated? Absolutely, and several such tools exist. In fact, in a few more chapters, you'll know enough to write such a tool yourself, but I'll leave that for the more free-spirited to pursue — I won't discuss such tools any further in this tutorial. Writing parsers by hand is an extremely good exercise for really driving home core concepts, and I'm not going to cheat myself out of that experience. I don't strap my Garmin onto my pets and make them run around the block, do I? Do I?? Don't trust my Strava.

### Definition

Let's read function definitions now.

```cpp
/// definition
///   = "def" prototype ":" [ eols ] "return" expression ;
static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'
```

After I've read the signature and the following `:`, I call `consumeNewlines()` which lets the body go on the next line. 

```cpp
  // Skip any newlines between ':' and 'return'. This allows the body to be
  // written on the next line:
  //   def foo(x):
  //     return x + 1
  consumeNewlines();
```

In the REPL this will look like:

```pyxc
ready> def add(x, y): 
  return x + y
Parsed a function definition.
```

Next: `return`.

```cpp
  if (CurTok != tok_return)
    return LogErrorF("Expected 'return' in function body");
  getNextToken(); // eat 'return' — not stored in the AST

  if (auto E = ParseExpression())
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}
```

`getNextToken()` eats the `return` keyword but nothing stores it in the AST. For now every function body is a single expression, so `return` is just syntax that says "this expression is the result." In a later chapter, when functions can have multiple statements and multiple return points, `return` becomes a first-class AST node.

`consumeNewlines()` is trivial to implement.
```cpp
static void consumeNewlines() {
  while (CurTok == tok_eol)
    getNextToken();
}
```

### Extern Declarations

```cpp
/// external
///   = "extern" "def" prototype
static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern.

  if (CurTok != tok_def)
    return LogErrorP("Expected `def` after extern.");

  getNextToken(); // eat def
  return ParsePrototype();
}
```

An `extern` is just a prototype — I'm declaring a name and its parameter count so the compiler knows how to call it. The actual implementation lives elsewhere (a C library, or, once I add multi-file support, a different object file). I use `def` after `extern` to keep the syntax consistent — `extern def` reads as "this is an external definition," parallel to `def` for local ones. This is just a personal preference. There's no universal law. It's your language, design it as you please. I know I say this a lot, but I cannot iterate this enough. YOU are in the drivers seat. Feel free to experiment, break things, or accept defaults and move along — there's no right way. What is shown here is one way.

### Top-Level Expressions

So far I have the infrastructure to read function definitions and call them. But what happens to bare expressions like `1 + 2 * 3`? I just wrap them in a function with an internal name and reuse the existing infrastructure to read and run it:

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

The name `__anon_expr` is a placeholder I invented — it could be any valid identifier. In a later chapter when I add JIT execution, I'll look up this function by name and call it to evaluate the expression immediately. Wrapping it in `FunctionAST` now means the rest of the pipeline — code generation, optimization, JIT — doesn't need any special cases for top-level expressions. They can be treated as ordinary functions.

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

## Don’t trust me no more bro

Now that you've seen the main loop, you'll see why I printed `ready>` inside LogError. It's because if the error happens at the end of a line, I don't go back to the main loop right away. Here's an example:

```pyxc
ready> def add   
Error: Expected '(' in prototype (token: -2)
ready>   
```        

When I hit the error at the end of `add`, `HandleDefinition` tries to skip the bad token:

```cpp
getNextToken();
```

That call blocks waiting for input. So nothing else runs.

Which means:
- MainLoop doesn’t run
- `ready>` doesn’t get printed

So if I don't print the prompt inside `LogError`, the program would just sit there silently and appear frozen.

## The final touches

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

The `test/` directory has lit tests covering each grammar rule — one file per rule. Browse them for more input examples, or run the suite:

```bash
llvm-lit code/chapter-02/test/
```

## Try It

```pyxc
ready> def add(x, y):
return x + y
Parsed a function definition.
ready> def fib(n):
return fib(n-1) + fib(n-2)
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

## Things Worth Knowing

- **`1.2.3` silently lexes as `1.2`.** The lexer reads the `1.2.3` as a number but `strtod` quietly drops `.3` without explicitly saying so. I fix this in [Chapter 3](chapter-03.md).

- **Error messages show raw token numbers.** `token: -7` means `tok_return`. [Chapter 3](chapter-03.md) replaces this with readable names and source locations.

### Avoiding a Grammar Pitfall: Left Recursion

There's a rule you can't write for a top-down parser: a rule that starts with itself.

```ebnf
(* DON'T DO THIS *)
expression = expression binaryop primary | primary
```

The parser would try to parse `expression`, which requires parsing `expression`, which requires parsing `expression`... infinite recursion, immediate crash.

The fix is to use iteration instead of recursion:

```ebnf
expression = primary { binaryop primary }
```

"Parse one primary, then loop and grab (operator, primary) pairs until there are no more." Same language, no recursion. Pyxc grammar always does this.

## What's Next

I now have a parser that understands the structure of pyxc code and builds a tree of objects representing it. But before I hook this up to LLVM and generate real machine code, [Chapter 3](chapter-03.md) revisits the lexer: readable error messages, source locations, and the keyword map. The parser works but [Chapter 3](chapter-03.md) makes it pleasant to use.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.

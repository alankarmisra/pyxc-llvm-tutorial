---
section: "Foundations"
description: "Consume lexer tokens with a recursive-descent parser and build the first pyxc syntax tree."
---

# 2. pyxc: The Parser and Syntax Tree

Next: turn the token stream into structure.

Chapter 1 can recognize the pieces of:

```pyxc
def add(a, b): a + b
```

but it does not know that `add` is a function, `a` and `b` are parameters, or `a + b` is the function body.

Add the next compiler boundary:

```text
tokens -> abstract syntax tree
```

Work in:

```bash
cd code/chapter-02
```

## 1. Write the Grammar Before the Parser

Use this expression grammar:

```ebnf
program              = [ end-of-lines ]
                       [ top-level-item { end-of-lines top-level-item } ]
                       [ end-of-lines ] ;
top-level-item       = function-definition | top-level-expression ;
function-definition  = "def" function-signature ":"
                       [ end-of-lines ] expression ;
function-signature   = name "(" [ parameters ] ")" ;
parameters           = name { "," name } ;
top-level-expression = expression ;
expression           = sum ;
sum                  = term { "+" term } ;
term                 = primary ;
primary              = name-expression
                     | number-expression
                     | parenthesized-expression ;
name-expression      = name | call-expression ;
call-expression      = name "(" [ arguments ] ")" ;
arguments            = expression { "," expression } ;
parenthesized-expression = "(" expression ")" ;
```

Each parser function will implement one rule. This is a **recursive-descent parser** because grammar rules call the functions for their child rules directly.

## 2. Add the AST Base Class

Add:

```cpp
class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;
};
```

Use `unique_ptr` for ownership. Each parent node owns its children, so deleting the root deletes the complete tree.

## 3. Add Leaf Expression Nodes

A number stores its value:

```cpp
class NumberExpressionNode : public ExpressionNode {
  double Value;

public:
  NumberExpressionNode(double Value) : Value(Value) {}
};
```

A name stores its spelling:

```cpp
class NameExpressionNode : public ExpressionNode {
  string Name;

public:
  NameExpressionNode(const string &Name) : Name(Name) {}
};
```

The AST keeps semantic structure and discards punctuation. A `NumberExpressionNode` does not need to remember which source characters surrounded it.

## 4. Add Compound Expression Nodes

Represent `a + b` with an operator and two owned operands:

```cpp
class BinaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Left, Right;

public:
  BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left,
                       unique_ptr<ExpressionNode> Right)
      : Operator(Operator), Left(std::move(Left)),
        Right(std::move(Right)) {}
};
```

Represent a call with a callee name and zero or more arguments:

```cpp
class CallExpressionNode : public ExpressionNode {
  string Callee;
  vector<unique_ptr<ExpressionNode>> Arguments;

public:
  CallExpressionNode(const string &Callee,
                     vector<unique_ptr<ExpressionNode>> Arguments)
      : Callee(Callee), Arguments(std::move(Arguments)) {}
};
```

For example:

```pyxc
add(1, 2)
```

becomes:

```text
CallExpression "add"
├── NumberExpression 1
└── NumberExpression 2
```

## 5. Add Function Nodes

Keep the signature separate from the body:

```cpp
class FunctionSignatureNode {
  string Name;
  vector<string> Parameters;

public:
  FunctionSignatureNode(const string &Name, vector<string> Parameters)
      : Name(Name), Parameters(std::move(Parameters)) {}
};
```

Then combine a signature and expression body:

```cpp
class FunctionDefinitionNode {
  unique_ptr<FunctionSignatureNode> Signature;
  unique_ptr<ExpressionNode> Body;

public:
  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature,
                         unique_ptr<ExpressionNode> Body)
      : Signature(std::move(Signature)), Body(std::move(Body)) {}
};
```

This creates one clear unit:

```text
function definition -> signature + body expression
```

## 6. Add One Token of Parser Lookahead

The lexer already provides `getToken()`. Add:

```cpp
static int CurrentToken;

static int getNextToken() {
  return CurrentToken = getToken();
}
```

`CurrentToken` is the token the parser is currently deciding how to use. Parser functions consume it only when they have confirmed it belongs to their grammar rule.

## 7. Add Parser Error Helpers

Add helpers that report an error and return `nullptr`:

```cpp
static unique_ptr<ExpressionNode> LogErrorExpression(const char *Message) {
  fprintf(stderr, "Error: %s\n", Message);
  return nullptr;
}

static unique_ptr<FunctionSignatureNode>
LogErrorSignature(const char *Message) {
  LogErrorExpression(Message);
  return nullptr;
}
```

This lets parser code use the normal failure pattern:

```cpp
if (CurrentToken != tok_rparen)
  return LogErrorExpression("Expected ')'");
```

## 8. Parse Numbers

Add:

```cpp
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  auto Result = make_unique<NumberExpressionNode>(NumberValue);
  getNextToken(); // eat number
  return std::move(Result);
}
```

The lexer has already placed the value in `NumberValue`. Copy it into the AST before consuming the next token.

## 9. Parse Names and Calls Together

A leading name is ambiguous until you inspect the following token:

```text
x       -> name expression
add(...) -> call expression
```

Implement:

```cpp
static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;
  getNextToken(); // eat name

  if (CurrentToken != tok_lparen)
    return make_unique<NameExpressionNode>(ParsedName);

  getNextToken(); // eat '('
  vector<unique_ptr<ExpressionNode>> Arguments;

  if (CurrentToken != tok_rparen) {
    while (true) {
      auto Argument = ParseExpression();
      if (!Argument)
        return nullptr;
      Arguments.push_back(std::move(Argument));

      if (CurrentToken == tok_rparen)
        break;

      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken(); // eat ','
    }
  }

  getNextToken(); // eat ')'
  return make_unique<CallExpressionNode>(ParsedName,
                                         std::move(Arguments));
}
```

Parse each argument as a complete expression. That allows nested calls and arithmetic inside arguments without special call-parser logic.

## 10. Parse Parentheses

Add:

```cpp
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat '('

  auto Expression = ParseExpression();
  if (!Expression)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')'");

  getNextToken(); // eat ')'
  return Expression;
}
```

The parentheses affect grouping but do not need their own AST node. Return the expression they contain.

## 11. Dispatch Primary Expressions

Implement the grammar alternatives with a switch:

```cpp
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  case tok_name:
    return ParseNameExpression();
  case tok_number:
    return ParseNumberExpression();
  case tok_lparen:
    return ParseParenthesizedExpression();
  default:
    return LogErrorExpression("Unexpected token in expression");
  }
}
```

This function answers one question:

```text
which primary begins with CurrentToken?
```

## 12. Parse Addition Left to Right

For this chapter, `term` is just `primary`:

```cpp
static unique_ptr<ExpressionNode> ParseTerm() {
  return ParsePrimary();
}
```

Build `sum` with a loop:

```cpp
static unique_ptr<ExpressionNode> ParseSum() {
  auto Left = ParseTerm();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_plus) {
    getNextToken(); // eat '+'

    auto Right = ParseTerm();
    if (!Right)
      return nullptr;

    Left = make_unique<BinaryExpressionNode>(
        tok_plus, std::move(Left), std::move(Right));
  }

  return Left;
}
```

Then make expression delegate to sum:

```cpp
static unique_ptr<ExpressionNode> ParseExpression() {
  return ParseSum();
}
```

Repeated addition folds left:

```text
1 + 2 + 3 -> ((1 + 2) + 3)
```

## 13. Parse Function Signatures

After the driver consumes `def`, parse the name, parentheses, and comma-separated parameters:

```cpp
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name");

  string FunctionName = Name;
  getNextToken(); // eat name

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");
  getNextToken(); // eat '('

  vector<string> Parameters;
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (CurrentToken != tok_name)
        return LogErrorSignature("Expected parameter name");

      Parameters.push_back(Name);
      getNextToken(); // eat parameter

      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorSignature("Expected ')' or ',' in signature");
      getNextToken(); // eat ','
    }
  }

  getNextToken(); // eat ')'
  return make_unique<FunctionSignatureNode>(FunctionName,
                                             std::move(Parameters));
}
```

## 14. Parse Function Definitions

Combine the signature with its body:

```cpp
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // eat 'def'

  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;

  if (CurrentToken != tok_colon)
    return nullptr;
  getNextToken(); // eat ':'

  while (CurrentToken == tok_eol)
    getNextToken();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return make_unique<FunctionDefinitionNode>(
      std::move(Signature), std::move(Body));
}
```

Allow the body on the same line or after one or more newlines.

## 15. Add the Mini Driver

The top-level driver has three jobs:

```text
newline -> consume it
def     -> parse a function definition
other   -> parse a top-level expression
```

Use handlers that report successful parsing:

```cpp
static void HandleFunctionDefinition() {
  if (ParseFunctionDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken();
}

static void HandleTopLevelExpression() {
  if (ParseExpression())
    fprintf(stderr, "Parsed a top-level expression.\n");
  else
    getNextToken();
}
```

Then loop:

```cpp
static void MainLoop() {
  while (true) {
    fprintf(stderr, "ready> ");

    switch (CurrentToken) {
    case tok_eof:
      return;
    case tok_eol:
      getNextToken();
      break;
    case tok_def:
      HandleFunctionDefinition();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}
```

Prime the lookahead once in `main()` before entering `MainLoop()`.

The recovery step consumes at least one token after failure. Without it, the driver would retry the same invalid token forever.

## 16. Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/pyxc
```

Try:

```pyxc
ready> def add(a, b): a + b
ready> add(1, 2)
```

Expected:

```text
Parsed a function definition.
Parsed a top-level expression.
```

The parser does not execute either tree yet. Success means the token sequence matched the grammar and produced owned AST nodes.

Run the suite:

```bash
llvm-lit -v test/
```

What you built is the next clean boundary:

```text
one grammar rule -> one parser function -> one AST node or error
```

Next: [Chapter 3](chapter-03.md) splits expressions into precedence tiers.

## Need Help?

Build issues? Questions?

- [Report a problem with GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- [Ask a question in GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your operating system and version
- The chapter number
- The exact command you ran
- The complete error message
- The output of `c++ --version` and `cmake --version`
- The output of `llvm-config --version` for Chapter 6 and later

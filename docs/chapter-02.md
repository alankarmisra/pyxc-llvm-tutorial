---
description: "Build a recursive-descent parser and syntax-tree nodes: turn tokens into structure and see 'Parsed a function definition' for the first time."
---
# 2. pyxc: The Parser and Syntax Tree

## What I Am Building

I'll continue working with the `add` function example from [Chapter 1](chapter-01.md).

```pyxc
# adds two numbers
def add(x, y):    
    x + y

print(add(1, 2)) # call the add function and print its value    
```

In the last chapter, I stripped out all the comments and converted the code into a stream of tokens. I now want to arrange these tokens into a hierarchical structure so I can make the relationships between different items clear.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-02
```

!!!note
    For a beginner compiler writer, the following can be a lot of information to grok. Take your time with it. And don't be disheartened if it seems slow going. Once you have your fundamentals down, the later chapters get much easier to wrap your head around. I'm not saying this merely to encourage you. It has been true in my own experience.

## Representing Structure

I went through several iterations before I arrived at the structures in the following sections. I made omissions, mistakes, sub-optimal decisions, and some lasting good decisions (beginner's luck). 

### Function definitions 

For something like `def add(x, y): x + y`, I start with a function definition as the outermost node, and store the signature and body underneath it. I isolate the signature into its own structure since it's the function's interface, separate from its body — something I'll want on its own later, for instance to check a call's argument count against it. The following shows instances of the classes and subclasses I'll define soon, built in response to the tokens I see for the function definition.

```ast
FunctionDefinition
├──  Signature -> FunctionSignature
│                 ├──  Name      = "add"
│                 └──  Parameters = ["x", "y"]
└──  Body -> BinaryExpression
             ├──  Operator='+'
             ├──  Left  -> NameExpression  Name="x"
             └──  Right -> NameExpression  Name="y"
```

### Function calls 

I follow a similar approach for function calls. The call expression is the parent, with its callee name and arguments as two branches beneath it. 

`add(1, 2)` becomes:

```ast
CallExpression
├──  Callee = "add"
└──  Arguments
     ├──  NumberExpression  Value=1
     └──  NumberExpression  Value=2
```

and `print(...)` becomes:

```ast
CallExpression
├──  Callee = "print"
└──  Arguments
     └──  ...
```

When I merge both calls, I get the full hierarchy for `print(add(1, 2))`:

```ast
CallExpression
├──  Callee = "print"
└──  Arguments
     └──  CallExpression
          ├──  Callee = "add"
          └──  Arguments
               ├──  NumberExpression  Value=1
               └──  NumberExpression  Value=2
```

Compiler writers call this an **Abstract Syntax Tree**, or **AST**. *Abstract*, because I leave out punctuation a hierarchical structure doesn't need, like the parentheses in `add(1, 2)`; I already record which arguments belong to which call without them. *Syntax tree*, because I capture only how the pieces fit together, not whether they make sense together. At this stage, I can attach three arguments to `add` even though it accepts only two because I am not checking that relationship yet. Checking whether the pieces actually make sense is called a **semantic** check, as opposed to a syntax check, and it's a separate pass I'll add later. For now, I'm just focusing on building the nodes.

## Coding Trees

Now I need to turn these trees into code. Compiler writers call each piece of the tree a **node**, so I'll use that name too.

### The Base Class

Most of my nodes derive from a base expression class. Any node in pyxc that reduces to a single value derives from this base class:

```cpp
class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;
};
```

I'll create a virtual destructor for the base. Without it, deleting a derived class node through a `unique_ptr<ExpressionNode>` is undefined behavior. 

### Numbers

Next, I'll deal with data types. pyxc's only data type is a number, represented by a `double`. That's easy to model with a number expression that stores a double value:

```cpp
class NumberExpressionNode : public ExpressionNode {
  double Value;
public:
  NumberExpressionNode(double Value) : Value(Value) {}
};
```

### Names

Right now a **name expression** can hold any identifier I see — a function parameter, or just a bare name with nothing behind it yet. I use it to store whichever name appears. I haven't figured out how to bind values to a name in function calls just yet. That's a later problem.

```cpp
class NameExpressionNode : public ExpressionNode {
  string Name;
public:
  NameExpressionNode(const string &Name) : Name(Name) {}
};
```

### Binary Expressions

I store a binary expression, like `1 + 2`, as the operator (`tok_plus` here) and the two operands on either side, `1` and `2`.

```cpp
class BinaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Left, Right;
public:
  BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left,
                       unique_ptr<ExpressionNode> Right)
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {}
};
```

How will I handle `1 + 2 + 3`? I'll build the tree iteratively and group it as `((1 + 2) + 3)`. Compiler writers call this *left associativity*, so I'll use that name too.

```ast
BinaryExpressionNode
├──  Operator='+'
├──  Left  -> BinaryExpressionNode
│             ├──  Operator='+'
│             ├──  Left  -> NumberExpressionNode  Value=1
│             └──  Right -> NumberExpressionNode  Value=2
└──  Right -> NumberExpressionNode  Value=3
```

In the [next chapter](chapter-03.md), I add more operators and use grammar layers to decide how I group an expression such as `1 + 2 * 3 + 4`.

### Function Calls

A function call stores the name of the function to call and a list of argument expressions being passed in. I model each argument as an expression node so I can model different kinds of function calls like `add(x, y)`, `add(1, 2)`, `add(1+2, 3+4)`:

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

`add(x, y)`:

```ast
CallExpressionNode
├──  Callee = "add"
└──  Arguments
     ├──  NameExpressionNode  Name="x"
     └──  NameExpressionNode  Name="y"
```

`add(1, 2)`:

```ast
CallExpressionNode
├──  Callee = "add"
└──  Arguments
     ├──  NumberExpressionNode  Value=1
     └──  NumberExpressionNode  Value=2
```

`add(1+2, 3+4)`:

```ast
CallExpressionNode
├──  Callee = "add"
└──  Arguments
     ├──  BinaryExpressionNode
     │    ├──  Operator='+'
     │    ├──  Left  -> NumberExpressionNode  Value=1
     │    └──  Right -> NumberExpressionNode  Value=2
     └──  BinaryExpressionNode
          ├──  Operator='+'
          ├──  Left  -> NumberExpressionNode  Value=3
          └──  Right -> NumberExpressionNode  Value=4
```

### Function Signatures

As I mentioned earlier, I split functions into two classes. The first is the function signature where I capture the function name and parameter names. Since a function signature is not an expression, I don't derive it from `ExpressionNode`.

```cpp
class FunctionSignatureNode {
  string Name;
  vector<string> Parameters;
public:
  FunctionSignatureNode(const string &Name, vector<string> Parameters)
      : Name(Name), Parameters(std::move(Parameters)) {}
};
```

The second is the body, which I can model with an existing `ExpressionNode`.

### Function Definitions

I create a function definition class where I pair the function signature with the body expression. Again, since a function *definition* is not an expression (as opposed to a function *call*), I don't derive it from `ExpressionNode`.

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

With this structure in place, for a function definition like `def add(x, y): x + y`, I will build something like:

```ast
FunctionDefinitionNode
├──  Signature -> FunctionSignatureNode
│                 ├──  Name      = "add"
│                 └──  Parameters = ["x", "y"]
└──  Body -> BinaryExpressionNode
             ├──  Operator='+'
             ├──  Left  -> NameExpressionNode  Name="x"
             └──  Right -> NameExpressionNode  Name="y"
```

## The Parser

Before I write the parsing functions themselves, I need two supporting pieces in place: a way to read one token at a time and a way to report errors. I'll set both up first, then move on to parsing itself.

### Reading Ahead 

I read one token at a time and store it in `CurrentToken`, a global variable:

```cpp
static int CurrentToken;
static int getNextToken() { return CurrentToken = getToken(); }
```

Looking ahead one token turns out to be enough: I can always tell what I'm parsing from the next token. A number is just a number, nothing more comes after it. A name is either a variable or the start of a function call, and whether a `(` follows right after is all I need to tell which. A `(` that wasn't preceded by a name is a parenthesized expression.

### Error Reporting

I make every parsing function return a `unique_ptr` to one of three node types, `ExpressionNode` (or a subclass of it), `FunctionSignatureNode`, or `FunctionDefinitionNode`, depending on the node I am parsing. If parsing fails, I return `nullptr` instead and print an error message. Since C++ can't overload on return type, I need three separate helpers to do that:

```cpp
unique_ptr<ExpressionNode> LogErrorExpression(const char *Str) {
  fprintf(stderr, "Error: %s (token: %s)\n", Str,
          TokenNames.at(CurrentToken).c_str());
  return nullptr;
}
unique_ptr<FunctionSignatureNode> LogErrorSignature(const char *Str) {
  LogErrorExpression(Str);
  return nullptr;
}
unique_ptr<FunctionDefinitionNode> LogErrorFunction(const char *Str) {
  LogErrorExpression(Str);
  return nullptr;
}
```

I could implement this with a template, but I don't want to call `LogError<ExpressionNode>()`, `LogError<FunctionSignatureNode>()`, and so on. I prefer three explicitly named helpers here.

In [Chapter 5](chapter-05.md), I add source locations—line and column—to these diagnostics.

## Parsing Expressions

Above each parsing function below, I write a short comment describing the construct it parses. Compiler writers call a rule like this a **grammar** rule, written in a compact notation:

1. Quoted text, like `"("`, means an exact character or keyword.
2. An unquoted word, like `expression`, refers to another rule.
3. `|` means "or".
4. `[ ... ]` means optional, zero or one.
5. `{ ... }` means repeated, zero or more.

### Numbers

When I receive `tok_number` from the lexer, I already have its value in the global `NumberValue`. I copy that value into a node and advance:

```cpp
/// number-expression
///   = number ;
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  auto Result = make_unique<NumberExpressionNode>(NumberValue);
  getNextToken(); // I consume the number.
  return Result;
}
```

I may use numbers in different ways in pyxc later, so I call this one specifically a `number-expression`: a number used as part of an expression.

### Names and Calls

After reading a name, I peek at the next token. No `(` means it's a plain variable. A `(` means it's a function call.

```pyxc
a     # variable
add() # function call
add(p1) # function call with a parameter
add(p1, p2) # function call with parameters
```

For function calls, I need to parse arguments, if any, which are expressions. The expression parser will come later, so let me forward-declare it. 

```cpp
static unique_ptr<ExpressionNode> ParseExpression();
```

And now I parse both forms.

```cpp
/// name-expression
///   = name
///   | call-expression ;
/// call-expression
///   = name "(" [ arguments ] ")" ;
/// arguments
///   = expression { "," expression } ;
static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;

  getNextToken(); // I eat the name.

  if (CurrentToken != tok_lparen) // I return a name, not a call.
    return make_unique<NameExpressionNode>(ParsedName);

  // I parse a call.
  getNextToken(); // I eat '('.
  vector<unique_ptr<ExpressionNode>> Arguments;
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (auto Arg = ParseExpression())
        Arguments.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurrentToken == tok_rparen)
        break;

      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // I eat ')'.
  getNextToken();

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments));
}
```

Since I packed a lot of notation into the `call-expression` and `arguments` lines, let me walk through them: a name, followed by `"("`, followed by optional arguments where each argument is an expression and additional arguments are separated by `","`, followed by `")"`.

### Parentheses

I skip over the `(`, parse whatever is inside the parentheses, verify the closing `)`, and return the inner expression. I don't need a parentheses node, the tree structure I'm building already captures the grouping:

```cpp
/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // I eat '('.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("expected ')'");
  getNextToken(); // I eat ')'.
  return V;
}
```

### Calling the Right Primary Parser

I now have parsing functions for three basic building blocks: numbers, names, and parenthesized expressions. I use the name parser for both plain variables and function calls. I call these building blocks **primary** items because I parse them before any operator. I use the current token to choose which primary parser to call:

```cpp
/// primary
///   = name-expression
///   | number-expression
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  case tok_number:
    return ParseNumberExpression(); // I parse a number such as 3.14.
  case tok_name:
    return ParseNameExpression(); // I parse `a` or `add(...)`.
  case tok_lparen:
    return ParseParenthesizedExpression(); // I parse `( ... )`.
  default:
    return LogErrorExpression("unknown token when expecting an expression");
  }
}
```

### Binary Expressions

If I call `ParsePrimary()` alone, I stop after one name, number, or parenthesized group. To parse `x + y`, I add one more layer. In `ParseSum()`, I parse a term, then loop: as long as `CurrentToken` is `+`, I eat the `+` and parse another term, folding the result into a `BinaryExpressionNode`. I define a term as a primary for now, and I make `ParseExpression()` return the sum.

```cpp
/// term
///   = primary ;
static unique_ptr<ExpressionNode> ParseTerm() { return ParsePrimary(); }

/// sum
///   = term { "+" term } ;
static unique_ptr<ExpressionNode> ParseSum() {
  auto Left = ParseTerm();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_plus) {
    getNextToken(); // I eat '+'.
    auto Right = ParseTerm();
    if (!Right)
      return nullptr;
    Left = make_unique<BinaryExpressionNode>(tok_plus, std::move(Left),
                                             std::move(Right));
  }

  return Left;
}

/// expression
///   = sum ;
static unique_ptr<ExpressionNode> ParseExpression() {
  return ParseSum();
}
```

`{ "+" term }` is the `{ }` repetition symbol again: zero or more `+ term` pairs, which is exactly what I encode with the `while` loop. I support only `+` for now; I add more operators and arithmetic precedence in the next chapter.

## Parsing Function Definitions

### Function Signature

I represent a function signature with a name and parameter names (no types yet, everything is `double` for now).

```cpp
/// function-signature
///   = name "(" [ parameters ] ")" ;
/// parameters
///   = parameter { "," parameter } ;
/// parameter
///   = name ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");

  string FnName = Name;
  getNextToken(); // I eat the function name.

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  // I parse parameter names. I call getNextToken() at the top to advance past
  // '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body I call getNextToken() again to move past the name
  // I just stored, then check whether ')' or ',' follows.

  vector<string> ParameterNames;
  while (getNextToken() == tok_name) {
    ParameterNames.push_back(Name);

    if (getNextToken() == tok_rparen) // I eat the name and check what follows.
      break;

    if (CurrentToken != tok_comma)
      return LogErrorSignature("Expected ')' or ',' in parameter list");
    // I continue the loop so getNextToken() at the top eats the ','.
  }

  if (CurrentToken != tok_rparen)
    return LogErrorSignature("Expected ')' in function signature");

  getNextToken(); // I eat ')'.

  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames));
}
```

### Function Definition

I'll read function definitions now.

```cpp
/// function-definition
///   = "def" function-signature ":" [ end-of-lines ] expression ;
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // I eat 'def'.
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorFunction("Expected ':' in function definition");
  getNextToken(); // I eat ':'.
```

After I read the signature and the following `:`, I call `consumeNewlines()` so I can put the body on the next line.

```cpp
  // I allow the body expression to start on the next line:
  //   def foo(x):
  //     x + 1
  consumeNewlines();
```

Now I read the expression.

```cpp
  if (auto E = ParseExpression())
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(E));
  return nullptr;
}
```

I implement `consumeNewlines()` with a short loop:

```cpp
static void consumeNewlines() {
  while (CurrentToken == tok_eol)
    getNextToken();
}
```

## Parsing Top-Level Expressions

So far I can parse function definitions and function-call expressions. I also need to parse expressions outside a function, such as `1 + 2 + 3`, because I will often enter them directly in the REPL. In LLVM, I cannot represent an instruction outside a function. I therefore wrap each top-level expression in a function with an internal name, letting me reuse the same representation as a regular function definition:

```cpp
/// top-level-expression
///   = expression ;
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  if (auto E = ParseExpression()) {
    auto Signature = make_unique<FunctionSignatureNode>("__anon_expr", vector<string>());
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(E));
  }
  return nullptr;
}
```

I invented the placeholder name `__anon_expr`, but I could use any valid name. When I add JIT execution in a later chapter, I look up this function by name and call it to evaluate the expression immediately. I then discard it and reuse the same name for the next top-level expression, so I do not need to keep inventing unique names.

!!!note
    Tools such as GNU Bison, ANTLR, and JavaCC can generate a parser from a grammar. They can save a lot of work, especially as a language grows. I want to write this parser by hand first because it will help me understand how the grammar becomes code. A hand-written parser also gives me direct control over how I build the syntax tree, report errors, and handle the unusual parts of my language. Once I understand those decisions, I can judge whether a parser generator would help me later.


## Mini Driver

I write two handler functions, one for each top-level construct, that call the appropriate parser and either print a success message or skip one bad token to keep the REPL alive:

```cpp
static void HandleFunctionDefinition() {
  if (ParseFunctionDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // I skip the bad token.
}

static void HandleTopLevelExpression() {
  if (ParseTopLevelExpression())
    fprintf(stderr, "Parsed a top-level expression.\n");
  else
    getNextToken(); // I skip the bad token.
}
```

I'll then write `MainLoop` to call a function based on the leading token, similar to what I do in `ParsePrimary()`:

```cpp
static void MainLoop() {
  while (true) {
    if (CurrentToken == tok_eof)
      return;

    // For a bare newline, I print a fresh prompt and read the next token.
    if (CurrentToken == tok_eol) {
      fprintf(stderr, "ready> ");
      getNextToken();
      continue;
    }

    switch (CurrentToken) {
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

## The Final Touches

In `main()`, I print the first prompt, load the first token, and then enter the loop:

```cpp
int main() {
  // I print the first prompt and load the first token before entering the loop.
  // I load CurrentToken before I call any parse function.
  fprintf(stderr, "ready> ");
  getNextToken();

  MainLoop();
  return 0;
}
```

## Bug Hunting

I didn't catch the following bugs until I actually ran the REPL against `MainLoop`.

### Bug 1: The REPL Doesn't Respond Until I Type More

I expect this:

<!-- code-merge:start -->
```pyxc
ready> 1 + 2
```
```text
Parsed a top-level expression.
ready> 
```
<!-- code-merge:end -->

But if I actually type `1 + 2` and press *enter*, it seems to wait for another keypress. 

<!-- code-merge:start -->
```pyxc
ready> 1 + 2
```
```text
...
```
<!-- code-merge:end -->

Here's why. When I read the `2`, I leave `LastChar` sitting on the `\n` right after it. At this point my `getToken()` code reaches this branch:

```cpp
// Newline
if (LastChar == '\n') {
  LastChar = advance(); // <-- BUG
  return tok_eol;
}
```

That `LastChar = advance()` is the bug. Since I haven't typed anything past that newline yet, `advance()` blocks right there, before `tok_eol` is ever returned. I'm expecting output, but the REPL just looks frozen. If I hit *enter* again, it unblocks, and I finally see the expected `Parsed a top-level expression.`

I fix this by not trying to read another character once I see a newline. I set `LastChar` to a space instead:

```cpp
// Newline
if (LastChar == '\n') {
  LastChar = ' ';
  return tok_eol;
}
```

On the *next* call, I skip that space in `getToken()`'s whitespace loop and call `advance()` to read the next token. This is the one place I deliberately break my own `LastChar` rule: right after this code snippet, `LastChar` does *not* hold the next input value to process.

I made the same mistake in the comment branch:

<!-- code-merge:start -->
```pyxc
ready> 1 + 2 # this comment will stall getToken() too
```
```text
... 
```
<!-- code-merge:end -->

Here's the offending code:

```cpp
// Comment
  if (LastChar == '#') {
    // I consume the comment through the end of the line.
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar != EOF) {
      LastChar = advance(); // <-- BUG
      return tok_eol;
    }
  }
```

I similarly replace the offending snippet in comment parsing with:
```cpp
// ...
    if (LastChar != EOF) {
        LastChar = ' '; // <-- FIX
        return tok_eol;
    }
// ...
```

With that fix in place I now get the expected output:

<!-- code-merge:start -->
```pyxc
ready> 1 + 2
```
```text
Parsed a top-level expression.
ready> 
```
<!-- code-merge:end -->

### Bug 2: The Prompt Disappears After an Error

I found a second bug hiding behind the first one. I typed a broken function definition to check my error handling:

<!-- code-merge:start -->
```pyxc
ready> def add
```
```text
Error: Expected '(' in function signature (token: newline)
```
<!-- code-merge:end -->

The error prints, but no fresh `ready> ` follows it. The REPL looks frozen again.

Here's why. Once I report the error in `ParseFunctionSignature()`, I return `nullptr`. In `HandleFunctionDefinition()`'s recovery path, I then try to skip the bad token so I do not retry it forever:

```cpp
static void HandleFunctionDefinition() {
  if (ParseFunctionDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // I skip the bad token.
}
```

I stall in that `getNextToken()` call. I never return to `MainLoop()`, so I never print a fresh `ready> `.

I fix this by printing the prompt from inside `LogErrorExpression()` immediately after the error message, before I reach the blocking `getNextToken()` call:

```cpp
fprintf(stderr, "Error: %s (token: %s)\nready> ", Str,
        TokenNames.at(CurrentToken).c_str());
```

With both fixes in place:

<!-- code-merge:start -->
```pyxc
ready> def add
```
```text
Error: Expected '(' in function signature (token: newline)
ready> 
```
<!-- code-merge:end -->

### Bug 3: Prompt Prints Twice

By adding the prompt to `LogErrorExpression()`, I created a new issue.

<!-- code-merge:start -->
```pyxc
ready> def bad(x) x
```
```text
Error: Expected ':' in function definition (token: name)
ready> ready>
```
<!-- code-merge:end -->

I print the first prompt from `LogErrorExpression()`'s baked-in `\nready> `. The recovery skip in `HandleFunctionDefinition()` then lands exactly on the line's trailing `tok_eol`. When I return to `MainLoop()`, I print a second `ready> ` immediately afterward.

I'm not chasing this down here. In [Chapter 5](chapter-05.md), I send lexer errors (`tok_error`) and parser failures (`nullptr`) through `SynchronizeToLineBoundary()`, then print one prompt from the main loop.

## Build and Run

```bash
cd code/chapter-02
cmake -S . -B build && cmake --build build
./build/pyxc
```

The `test/` directory has lit tests covering the grammar rules. I run the suite with:

```bash
llvm-lit test/
```

## Try It

<!-- code-merge:start -->
```pyxc
ready> def add(x, y):
x + y
```
```text
Parsed a function definition.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> def sumThree(a, b, c):
add(a, b) + c
```
```text
Parsed a function definition.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> 1 + 2 + 3
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> sin(1.0) + cos(2.0) # sin/cos are never defined; I don't check that yet, that's a semantic check
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> 1 2 # legal here; Chapter 5 disallows two things on one line
```
```text
Parsed a top-level expression.
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> def a(): 1 def b(): 2 # same deal for function definitions
```
```text
Parsed a function definition.
Parsed a function definition.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> def bad(x) x
```
```text
Error: Expected ':' in function definition (token: name)
ready> ready>
```
<!-- code-merge:end -->

With this parser, I accept valid syntax, report invalid syntax, and keep the REPL running after an error.

## Grammar

I've collected all the grammar rules I write above each parsing function in this chapter, and put them here. This makes up the complete grammar for pyxc at this stage.

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-02/pyxc.ebnf)

```ebnf
(*
   pyxc.ebnf
   Baseline grammar for chapter 2.
*)

(*
   { } = zero or more (any number of...)
   [ ] = zero or one (optional)
*)
program                    = [ end-of-lines ]
                             [ top-level-item { end-of-lines top-level-item } ]
                             [ end-of-lines ] ;
end-of-lines               = end-of-line { end-of-line } ;
top-level-item             = function-definition | top-level-expression ;
function-definition        = "def" function-signature ":"
                             [ end-of-lines ] expression ;
top-level-expression       = expression ;
function-signature         = name "(" [ parameters ] ")" ;
parameters                 = parameter { "," parameter } ;
parameter                  = name ;
expression                 = sum ;
sum                        = term { "+" term } ;
term                       = primary ;
primary                    = name-expression
                             | number-expression
                             | parenthesized-expression ;
name-expression            = name
                             | call-expression ;
call-expression            = name "(" [ arguments ] ")" ;
arguments                  = expression { "," expression } ;
number-expression          = number ;
parenthesized-expression   = "(" expression ")" ;
name                       = (letter | "_") { letter | digit | "_" } ;
number                     = digit { digit } [ "." { digit } ]
                             | "." digit { digit } ;
letter                     = "A".."Z" | "a".."z" ;
digit                      = "0".."9" ;
end-of-line                = "\r\n" | "\r" | "\n" ;
(*
    A `comment` begins with "#" and continues to the end of the line. The lexer
     ignores its text and returns an end-of-line token when one follows it.
*)
comment                    = "#" { comment-character } ;
comment-character          = ? any character except "\r" and "\n" ? ;
(*
    `whitespace` may appear before or between tokens
     and is ignored by the lexer.
*)
whitespace                 = " " | "\t" | "\v" | "\f" ;
```

I wrote the grammar in two layers.

- I use the bottom rules — `name`, `number`, `letter`, `digit`, `end-of-line`, `comment`, `comment-character`, and `whitespace` — to turn raw characters into tokens.
- I use the top rules — `expression`, `function-definition`, `function-signature`, and so on — to arrange those tokens into syntax and specify which token may follow another.

## What's Next

[Chapter 3](chapter-03.md) encodes operator precedence into the grammar, adding `-`, `*`, `/`, and `<`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`

I'll help you figure it out.

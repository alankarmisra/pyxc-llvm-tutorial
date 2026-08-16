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

### Function Definitions

For something like `def add(x, y): x + y`, I start by representing the whole definition with an instance of a class I call `FunctionDefinition`. I store the signature and body within it. I will write these classes shortly. For now, I'm just working on the structure I want.

```ast
FunctionDefinition
├──  Signature -> FunctionSignature
│                 ├──  Name       = "add"
│                 └──  Parameters = ["x", "y"]
└──  Body -> BinaryExpression
             ├──  Operator='+'
             ├──  Left  -> NameExpression  Name = "x"
             └──  Right -> NameExpression  Name = "y"
```

### Function Calls

I follow a similar approach for function calls. The call expression is the parent, with the callee name and arguments as two branches beneath it. 

`add(1, 2)` becomes:

```ast
CallExpression
├──  Callee = "add"
└──  Arguments
     ├──  NumberExpression  Value = 1
     └──  NumberExpression  Value = 2
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
               ├──  NumberExpression  Value = 1
               └──  NumberExpression  Value = 2
```

Compiler writers call this an **Abstract Syntax Tree**, or **AST**. It is *abstract* because I keep the structure expressed by the source while leaving out details I no longer need. For example, the parentheses and commas in `add(1, 2)` tell me how to group the call and its arguments, but once I record that structure in a `CallExpression`, I do not need the punctuation itself and throw it away.

It is a *syntax tree* because it represents how the program's grammatical pieces fit together. The word **syntax** comes from the Greek *sýntaxis*, meaning “arrangement.” Successfully creating a syntax tree doesn't guarantee those pieces necessarily make sense together. At this stage, I can attach three arguments to `add` even though it accepts only two because I have not checked that relationship yet.

Checking whether the pieces make sense is called **semantic analysis**. The word **semantic** comes from the Greek *sēmantikós*, meaning “significant” or “having meaning.” I add semantic analysis later.

## Coding Trees

Now I need to turn these trees into code. Compiler writers call each piece of a tree a **node**. The word comes from the Latin *nodus*, meaning “knot,” which fits because a node is a point where parts of the tree connect.

### The Base Class

I wrap every node class in an anonymous namespace. That gives each class internal linkage, so only code inside `pyxc.cpp` can reference them by name. Since pyxc is a single-file compiler, that's every line I write, but the anonymous namespace keeps these classes from accidentally colliding with an identically named class if I ever split the compiler across multiple files.

Most of my nodes reduce to a single value, so I derive them from a common expression class:

```cpp
namespace {

class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;
};
```

I give the base class a virtual destructor. Without it, deleting a derived object through a `unique_ptr<ExpressionNode>` is undefined behavior.

### Numbers

For now, pyxc represents every number as a `double`, so I create a number expression that stores one:

```cpp
class NumberExpressionNode : public ExpressionNode {
  double Value;
public:
  NumberExpressionNode(double Value) : Value(Value) {}
};
```

### Names

Right now, I use a **name expression** to store to store any name used as a value, such as a function parameter or a bare name. I will figure out how to bind values to these names in function calls later.

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

How will I handle `1 + 2 + 3`? I'll build the tree iteratively and group it as `((1 + 2) + 3)`. Compiler writers call this left-to-right grouping **left associativity**, which sounds reasonable enough, so I will too.

```ast
BinaryExpression
├──  Operator = '+'
├──  Left  -> BinaryExpression
│             ├──  Operator = '+'
│             ├──  Left  -> NumberExpression  Value = 1
│             └──  Right -> NumberExpression  Value = 2
└──  Right -> NumberExpression  Value = 3
```

### Function Calls

A function call stores the name of the function to call and a list of arguments. I store each argument as an expression node so it can be a name, a number, or a larger expression, as in `add(x, y)`, `add(1, 2)`, `add(1 + 2, 3 + 4)`:

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
CallExpression
├──  Callee = "add"
└──  Arguments
     ├──  NameExpression  Name = "x"
     └──  NameExpression  Name = "y"
```

`add(1, 2)`:

```ast
CallExpression
├──  Callee = "add"
└──  Arguments
     ├──  NumberExpression  Value = 1
     └──  NumberExpression  Value = 2
```

`add(1+2, 3+4)`:

```ast
CallExpression
├──  Callee = "add"
└──  Arguments
     ├──  BinaryExpression
     │    ├──  Operator = '+'
     │    ├──  Left  -> NumberExpression  Value = 1
     │    └──  Right -> NumberExpression  Value = 2
     └──  BinaryExpression
          ├──  Operator = '+'
          ├──  Left  -> NumberExpression  Value = 3
          └──  Right -> NumberExpression  Value = 4
```

### Function Signatures

As I mentioned earlier, I keep a function's signature separate from its body. The signature stores the function name and parameter names. Since a signature does not produce a value, it is not an *expression*, so I don't derive `FunctionSignatureNode` from `ExpressionNode`.

```cpp
class FunctionSignatureNode {
  string Name;
  vector<string> Parameters;
public:
  FunctionSignatureNode(const string &Name, vector<string> Parameters)
      : Name(Name), Parameters(std::move(Parameters)) {}
};
```

### Function Bodies

Since I currently restrict each function body to one expression, I can represent the body with an `ExpressionNode`.

### Function Definitions

I create a function definition class where I pair the function signature with the body expression. Again, since a function *definition* is not an expression, I don't derive it from `ExpressionNode`.

```cpp
class FunctionDefinitionNode {
  unique_ptr<FunctionSignatureNode> Signature;
  unique_ptr<ExpressionNode> Body;
public:
  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature,
                 unique_ptr<ExpressionNode> Body)
      : Signature(std::move(Signature)), Body(std::move(Body)) {}
};

} // end anonymous namespace
```

With this structure in place, for a function definition like `def add(x, y): x + y`, I will build something like:

```ast
FunctionDefinition
├──  Signature -> FunctionSignature
│                 ├──  Name       = "add"
│                 └──  Parameters = ["x", "y"]
└──  Body -> BinaryExpression
             ├──  Operator = '+'
             ├──  Left  -> NameExpression  Name = "x"
             └──  Right -> NameExpression  Name = "y"
```

## The Parser

The word *parse* ultimately comes from the Latin *pars*, meaning “part.” I use a parser to work out which parts of the program the tokens represent and how those parts fit together.

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
unique_ptr<ExpressionNode> LogErrorExpression(const char *ErrorMessage) {
  fprintf(stderr, "Error: %s (token: %s)\n", ErrorMessage,
          TokenNames.at(CurrentToken).c_str());
  return nullptr;
}
unique_ptr<FunctionSignatureNode> LogErrorSignature(const char *ErrorMessage) {
  LogErrorExpression(ErrorMessage);
  return nullptr;
}
unique_ptr<FunctionDefinitionNode> LogErrorFunction(const char *ErrorMessage) {
  LogErrorExpression(ErrorMessage);
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
      if (auto Argument = ParseExpression())
        Arguments.push_back(std::move(Argument));
      else
        return nullptr;

      // ParseExpression() has already consumed the argument and left
      // CurrentToken at the token after it.
      if (CurrentToken == tok_rparen)
        break;

      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken(); // I eat ','.
    }
  }

  // I only reach here after parsing `a()` or `a(<arguments>)`, so I eat ')'.
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
  auto Expression = ParseExpression();
  if (!Expression)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')'");
  getNextToken(); // I eat ')'.
  return Expression;
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
    return LogErrorExpression(
        ("Unexpected " + TokenNames.at(CurrentToken)).c_str());
  }
}
```

### Binary Expressions

If I call `ParsePrimary()` alone, I stop after one name, number, or parenthesized group. To parse `x + y`, I add one more layer. In `ParseSum()`, I parse a term, then loop: as long as `CurrentToken` is `+`, I eat the `+` and parse another term, folding the result into a `BinaryExpressionNode`. I define a term as a primary for now, and I make `ParseExpression()` return the sum.

The separate `term` layer may look redundant here, but it is a placeholder for a grammar rule that expands later. In Chapter 3, a term will include multiplication and division, which I want to group together and process before addition. Keeping the layer now lets me extend its rule without restructuring `ParseSum()`.

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

I represent a function signature with a name and parameter names (no types yet, everything is `double` for now). `ParseFunctionDefinition()` is dispatched only when `CurrentToken` is `tok_def`, so it can consume `def` immediately. It then calls `ParseFunctionSignature()` without first checking the next token; the signature parser itself must verify that this token is the required function name.

```cpp
/// function-signature
///   = name "(" [ parameters ] ")" ;
/// parameters
///   = parameter { "," parameter } ;
/// parameter
///   = name ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  // Callers consume the leading 'def', so the current token must be the
  // function name.
  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");

  string FunctionName = Name;
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

  return make_unique<FunctionSignatureNode>(FunctionName, std::move(ParameterNames));
}
```

### Function Definition

I'll read function definitions now.

I want to let the function body start on the next line, so I need a small helper that skips over any newlines sitting between the `:` and the body expression. I implement `consumeNewlines()` with a short loop:

```cpp
static void consumeNewlines() {
  while (CurrentToken == tok_eol)
    getNextToken();
}
```

After I read the signature and the following `:`, I call `consumeNewlines()` so I can put the body on the next line. Then I read the expression.

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

  // I allow the body expression to start on the next line:
  //   def foo(x):
  //     x + 1
  consumeNewlines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return std::make_unique<FunctionDefinitionNode>(std::move(Signature),
                                                  std::move(Body));
}
```

## Parsing Top-Level Expressions

So far I can parse function definitions and function-call expressions. I also need to parse expressions outside a function, such as `1 + 2 + 3`, because I will often enter them directly in the REPL. In LLVM, I cannot represent an instruction outside a function. I therefore wrap each top-level expression in a function with an internal name, letting me reuse the same representation as a regular function definition:

```cpp
/// top-level-expression
///   = expression ;
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  // I invent a function signature with an internal name and no parameters
  auto Signature =
      make_unique<FunctionSignatureNode>("__anon_expr", vector<string>());

  return std::make_unique<FunctionDefinitionNode>(std::move(Signature),
                                                  std::move(Body));
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
  while (CurrentToken != tok_eof) {
    switch (CurrentToken) {
    case tok_eol:
      // For a bare newline, I print a fresh prompt and read the next token.
      fprintf(stderr, "ready> ");
      getNextToken();
      break;
    case tok_error:
      LogErrorExpression("invalid character");
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

### Bug 1: The REPL Doesn't Respond until I Type More

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

```cppdiff
*int getToken() {
*  ...
*  // I recognize a newline.
*  if (LastChar == '\n') {
-    LastChar = advance(); // <-- BUG
+    LastChar = ' ';
*    return tok_eol;
*  }
*  ...
*}
```

That `LastChar = advance()` is the bug. Since I haven't typed anything past that newline yet, `advance()` blocks right there, before `tok_eol` is ever returned. I'm expecting output, but the REPL just looks frozen. If I hit *enter* again, it unblocks, and I finally see the expected `Parsed a top-level expression.`

I fix this by not trying to read another character once I see a newline. I set `LastChar` to a space instead, as shown above. On the *next* call, I skip that space in `getToken()`'s whitespace loop and call `advance()` to read the next token. This is the one place I deliberately break my own `LastChar` rule: right after this code snippet, `LastChar` does *not* hold the next input value to process.

I made the same mistake in the comment branch:

<!-- code-merge:start -->
```pyxc
ready> 1 + 2 # this comment will stall getToken() too
```
```text
... 
```
<!-- code-merge:end -->

Here's the offending code, and the same fix:

```cppdiff
*  // I discard a comment.
*  if (LastChar == '#') {
*    // I consume characters through the end of the line.
*    do {
*      LastChar = advance();
*    } while (LastChar != '\n' && LastChar != EOF);
*
*    if (LastChar == '\n') {
-      LastChar = advance(); // <-- BUG
+      LastChar = ' ';
*      return tok_eol;
*    }
*  }
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

### Bug 2: The Prompt Disappears after an Error

After I fixed the first bug and rebuilt `pyxc`, a second bug showed up in the form below. I typed a broken function definition to check my error handling:

<!-- code-merge:start -->
```pyxc
ready> def add
```
```text
Error: Expected '(' in function signature (token: newline)
```
<!-- code-merge:end -->

The error prints, but no fresh `ready> ` follows it. The REPL looks frozen again.

This bug already existed before the first fix, but the broken newline handling masked it with different behavior: the REPL printed a fresh `ready> ` without first acknowledging the error, then reported the error without printing another prompt. Fixing the newline handling made the missing prompt appear directly after the diagnostic, exposing the second bug clearly.

Here's why. `ParseFunctionSignature()` reports the missing `(` through `LogErrorSignature()`, which delegates to `LogErrorExpression()`. `LogErrorExpression()` prints the diagnostic, then the parser returns `nullptr` through `ParseFunctionDefinition()` to `HandleFunctionDefinition()`. The handler then enters its recovery path and tries to skip the bad token so I do not retry it forever:

```cpp
static void HandleFunctionDefinition() {
  if (ParseFunctionDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // I skip the bad token.
}
```

By the time I reach `getNextToken()`, the error has already been printed by `LogErrorExpression()`; this call is only trying to recover. I stall while attempting that skip. I never return to `MainLoop()`, so I never print a fresh `ready> `.

I fix this by printing the prompt from inside `LogErrorExpression()` immediately after the error message, before I reach the blocking `getNextToken()` call:

```cppdiff
*unique_ptr<ExpressionNode> LogErrorExpression(const char *ErrorMessage) {
-  fprintf(stderr, "Error: %s (token: %s)\n", ErrorMessage,
+  fprintf(stderr, "Error: %s (token: %s)\nready> ", ErrorMessage,
*          TokenNames.at(CurrentToken).c_str());
*  return nullptr;
*}
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
llvm-lit -v test/
```

!!!note
    Until [Chapter 7](chapter-07.md) adds code generation, these tests can only confirm that parsing succeeds, not which operator or value actually ended up in the tree because I have no way of inspecting them outside the compiler. A bug that silently swapped `-` for `+`, for example, would still print *Parsed a top-level expression.* and pass every test here. 

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

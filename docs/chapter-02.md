---
description: "Build a recursive-descent parser and syntax-tree nodes: turn tokens into structure and see 'Parsed a function definition' for the first time."
---
# 2. pyxc: The Parser and Syntax Tree

## Where We Are

I'll continue working with the `add` function example from [Chapter 1](chapter-01.md).

```pyxc
# adds two numbers
def add(x, y):    
    x + y

print(add(1, 2)) # call the add function and print its value    
```

In the last chapter I managed to strip out all the comments and convert the code into a stream of tokens. I now want to arrange these tokens into some sort of a hierarchical structure so the relationships between different items are clear. Let me do that next.  

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-02
```

## Representing Structure

A fair number of iterations went into coming up with the structures in the following sections. I made omissions, mistakes, sub-optimal decisions, and some lasting good decisions too (beginner's luck) and what you see here is a result of that. Even so, you might discover better ways to do this, and that wouldn't surprise me at all. 

### Function definitions 

For something like `def add(x, y): x + y`, I start with a function definition as the primary group, and store the signature and body underneath it. I've isolated the signature so that I can compare the function signature with function calls to ensure argument counts match. The following shows instances of the classes and subclasses I'll define soon, built in response to the tokens I see for the function definition.

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

Merging both we get the full hierarchy for `print(add(1, 2))`:

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

Compiler writers call this an **Abstract Syntax Tree**, or **AST**. *Abstract*, because I leave out punctuation a hierarchical structure doesn't need, like the parentheses in `add(1, 2)`; I already record which arguments belong to which call without them. *Syntax tree*, because it only captures syntax, how the pieces fit together, not whether they make sense together. It would happily let me attach three arguments to `add` even though it only takes two; the tree is just a data structure and can't reason about that. Checking whether the pieces actually make sense is called a **semantic** check, as opposed to a syntax check, and it's a separate pass I'll add later. For now, I'm just focusing on building the nodes.

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

The only **name values** I have right now are function parameters. I use a name expression to store the name of each parameter. I haven't figured out how to bind values to a name in function calls just yet. That's a later problem.

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

How will I handle `1 + 2 + 3`? I'll build a tree iteratively that groups them like so `((1 + 2) + 3)`. Them compiler writers call it *Left associativity* so I'll call it that too. 

```ast
BinaryExpressionNode
├──  Operator='+'
├──  Left  -> BinaryExpressionNode
│             ├──  Operator='+'
│             ├──  Left  -> NumberExpressionNode  Value=1
│             └──  Right -> NumberExpressionNode  Value=2
└──  Right -> NumberExpressionNode  Value=3
```

This will be a whole new adventure once I introduce multiplication in the [next chapter](chapter-03.md). Think about how you would group `1 + 2 * 3 + 4` and what the code would look like. 

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
  const string &getName() const { return Name; }
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

Now I get to turn the token stream into trees, a process compiler writers call **parsing**. Before I write the parsing functions themselves, I need two supporting pieces in place: error reporting, since every parsing function will call into it, and a way to read one token at a time. I'll set both up first, then move on to parsing itself.

### Error Reporting

I make every parsing function return a `unique_ptr` to one of three node types, `ExpressionNode` (or a subclass of it), `FunctionSignatureNode`, or `FunctionDefinitionNode`, depending on what it's parsing. If parsing fails, I return `nullptr` instead and print an error message. Since C++ can't overload on return type, I need three separate helpers to do that:

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

I didn't use a template because I don't want to call `LogError<ExpressionNode>()`, `LogError<FunctionSignatureNode>()`, etc. but you can choose to implement it as a template if you want.

[Chapter 4](chapter-04.md) adds source location, line and column, to these diagnostics.

### Reading Ahead 

Next, I read one token at a time and store it in `CurrentToken`, a global variable:

```cpp
static int CurrentToken;
static int getNextToken() { return CurrentToken = getToken(); }
```

Looking ahead one token turns out to be enough: I can always tell what I'm parsing from the next token. A number is just a number, nothing more comes after it. A name is either a variable or the start of a function call, and whether a `(` follows right after is all I need to tell which. A `(` that wasn't preceded by a name is a parenthesized expression.

## Parsing Expressions

Above each parsing function below, I write a short comment describing the construct it parses. Compiler writers call a rule like this a **grammar** rule, written in a compact notation:
1. Quoted text, like `"("`, means an exact character or keyword.
2. An unquoted word, like `expression`, refers to another rule.
3. `|` means "or".
4. `[ ... ]` means optional, zero or one.
5. `{ ... }` means repeated, zero or more.

### Numbers

When the lexer returns `tok_number`, it has already set the global `NumberValue`. I copy its current value into a node and advance:

```cpp
/// number-expression
///   = number ;
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  auto Result = make_unique<NumberExpressionNode>(NumberValue);
  getNextToken(); // consume the number
  return std::move(Result);
}
```

Numbers might get used in different ways in pyxc later on, so I call this one specifically a `number-expression`: a number used as part of an expression.

### Names and Calls

After reading a name, I peek at the next token. No `(` means it's a plain variable. A `(` means it's a function call.

```pyxc
a     # variable
add() # function call
```

This is what I've come up with. 

```cpp
/// name-expression
///   = name
///   | name "(" [ expression { "," expression } ] ")" ;
static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;

  getNextToken(); // eat name.

  if (CurrentToken != tok_lparen) // Simple name, not a call.
    return make_unique<NameExpressionNode>(ParsedName);

  // Call.
  getNextToken(); // eat (
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

  // Eat the ')'.
  getNextToken();

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments));
}
```

Since I packed a lot of notation into the `name "(" [ expression { "," expression } ] ")"` line, let me walk through it: a name, followed by `"("`, followed by an optional expression that, if present, can be followed by any number of additional `","`-separated expressions, followed by `")"`.

### Parentheses

I skip over the `(`, parse whatever is inside the parentheses, verify the closing `)`, and return the inner expression. I don't need a parentheses node, the tree structure I'm building already captures the grouping:

```cpp
/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat '('
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("expected ')'");
  getNextToken(); // eat ')'
  return V;
}
```

### Calling the Right Primary Parser

I now have parsing functions for three basic building blocks: numbers, names, which cover both plain variables and function calls, and parenthesized expressions. These three are what I'll call **primary** items: the pieces that show up first, before any operator, whether they end up inside a function body or as a bare expression typed at the REPL, the same way you'd write one in a plain Python script. Based on the token I read, I can tell which of the three I'm looking at:

```cpp
/// primary
///   = name-expression
///   | number-expression
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  case tok_name:       return ParseNameExpression(); // handles names like `a` or function calls like `add(...)`
  case tok_number:     return ParseNumberExpression(); // handles singular numbers like 3.14
  case tok_lparen:     return ParseParenthesizedExpression(); // handles parenthesized expressions like `(` ... `)`
  default:
    return LogErrorExpression("unknown token when expecting an expression");
  }
}
```

### Binary Expressions

`ParsePrimary` alone stops after one name, number, or parenthesized group, so `x + y` needs one more layer. In `ParseExpression`, I parse a primary, then loop: as long as `CurrentToken` is `+`, I eat the `+` and parse another primary, folding the result into a `BinaryExpressionNode`.

```cpp
/// expression
///   = primary { "+" primary } ;
static unique_ptr<ExpressionNode> ParseExpression() {
  auto Left = ParsePrimary();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_plus) {
    getNextToken(); // eat '+'
    auto Right = ParsePrimary();
    if (!Right)
      return nullptr;
    Left = make_unique<BinaryExpressionNode>(tok_plus, std::move(Left),
                                             std::move(Right));
  }

  return Left;
}
```

`{ "+" primary }` is the `{ }` repetition symbol again: zero or more `+ primary` pairs, which is exactly what I encode with the `while` loop. pyxc only understands `+` for now; more operators and real operator precedence arrive in a later chapter.

## Parsing Function Definitions

### Function Signature

A function signature has a name and parameter names (no types yet, everything is `double` for now).

```cpp
/// function-signature
///   = name "(" [ name { "," name } ] ")" ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");

  string FnName = Name;
  getNextToken(); // eat function name

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  // Parse parameter names. The loop calls getNextToken() at the top to advance
  // past '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body I call getNextToken() again to move past the name
  // I just stored, then check whether ')' or ',' follows.

  vector<string> ParameterNames;
  while (getNextToken() == tok_name) {
    ParameterNames.push_back(Name);

    if (getNextToken() == tok_rparen) // eat name, check what follows
      break;

    if (CurrentToken != tok_comma)
      return LogErrorSignature("Expected ')' or ',' in parameter list");
    // loop continues: getNextToken() at the top eats the ','
  }

  if (CurrentToken != tok_rparen)
    return LogErrorSignature("Expected ')' in function signature");

  getNextToken(); // eat ')'

  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames));
}
```

If you're thinking there could be a way to convert these grammar rules into code automatically, you are right. But writing one by hand is too good an exercise to skip, so that's what I'm doing here.

### Function Definition

I'll read function definitions now.

```cpp
/// function-definition
///   = "def" function-signature ":" [ end-of-lines ] expression ;
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // eat 'def'
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorFunction("Expected ':' in function definition");
  getNextToken(); // eat ':'
```

After I've read the signature and the following `:`, I call `consumeNewlines()`, so the body can go on the next line. 

```cpp
  // Allow the body expression to start on the next line:
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

`consumeNewlines()` is trivial to implement.
```cpp
static void consumeNewlines() {
  while (CurrentToken == tok_eol)
    getNextToken();
}
```

## Parsing Top-Level Expressions

So far I have the code to read function definitions and call them. But what happens to expressions outside a function, like `1 + 2 + 3`? In the REPL, I'll almost always be typing expressions outside a function. Unfortunately, LLVM has no way to represent an instruction that exists outside a function, everything has to live inside one. So I wrap these expressions in a function with an internal name, reusing the same code I already have for reading and running real functions:

```cpp
/// top-level-expression
///   = expression
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  if (auto E = ParseExpression()) {
    auto Signature = make_unique<FunctionSignatureNode>("__anon_expr", vector<string>());
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(E));
  }
  return nullptr;
}
```

The name `__anon_expr` is a placeholder I invented, it could be any valid name. In a later chapter when I add JIT execution, I'll look up this function by name, call it to evaluate the expression immediately, then discard it and create a fresh one with the same name for the next top-level expression, so I don't need to keep inventing new unique names.

## Mini Driver

I write two handler functions, one for each top-level construct, that call the appropriate parser and either print a success message or skip one bad token to keep the REPL alive:

```cpp
static void HandleFunctionDefinition() {
  if (ParseFunctionDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // skip bad token
}

static void HandleTopLevelExpression() {
  if (ParseTopLevelExpression())
    fprintf(stderr, "Parsed a top-level expression.\n");
  else
    getNextToken(); // skip bad token
}
```

I'll then write `MainLoop` to call a function based on the leading token, similar to what I do in `ParsePrimary()`:

```cpp
static void MainLoop() {
  while (true) {
    if (CurrentToken == tok_eof)
      return;

    // A bare newline: just print a fresh prompt and read the next token.
    if (CurrentToken == tok_eol) {
      fprintf(stderr, "ready> ");
      getNextToken();
      continue;
    }

    switch (CurrentToken) {
    case tok_def:    HandleFunctionDefinition();  break;
    default:         HandleTopLevelExpression(); break;
    }
  }
}
```

## The Final Touches

`main()` prints the first prompt, loads the first token, then hands off to the loop:

```cpp
int main() {
  // Print the first prompt and load the first token before entering the loop.
  // Every parse function expects CurrentToken to already be loaded when it is called.
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

```pyxc
ready> 1 + 2
Parsed a top-level expression.
ready> 
```

But if I actually type `1 + 2` and press *enter*, it seems to wait for another keypress. 

```pyxc
ready> 1 + 2
...
```

Here's why. Reading the `2` leaves `LastChar` sitting on the `\n` right after it. At this point `getToken()` does this:

```cpp
// Newline
if (LastChar == '\n') {
  LastChar = advance(); // <-- BUG
  return tok_eol;
}
```

That `LastChar = advance()` is the bug. Since I haven't typed anything past that newline yet, `advance()` blocks right there, before `tok_eol` is ever returned. I'm expecting output, but the REPL just looks frozen. If I hit *enter* again, it unblocks, and I finally see the expected `Parsed a top-level expression.`

The fix is to not try to read another character once I see a newline. I set LastChar to a space instead:

```cpp
// Newline
if (LastChar == '\n') {
  LastChar = ' ';
  return tok_eol;
}
```

On the *next* call, `getToken()`'s whitespace-skipping loop sees that space, skips over it, and calls `advance()` reading in the next token. This is the one place I deliberately break my own `LastChar` rule: right after this code snippet, `LastChar` does *not* hold the next input value to process.

The exact same bug is in the comment branch, for the same reason:

```pyxc
ready> 1 + 2 # this comment will stall getToken() too
... 
```

Here's the offending code:

```cpp
// Comment
  if (LastChar == '#') {
    // Comment until the end of the line
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

```pyxc
ready> 1 + 2
Parsed a top-level expression.
ready> 
```

### Bug 2: The Prompt Disappears After an Error

I found a second bug hiding behind the first one. I typed a broken function definition to check my error handling:

```pyxc
ready> def add
Error: Expected '(' in function signature (token: newline)
```

The error prints, but no fresh `ready> ` follows it. The REPL looks frozen again.

Here's why. Once I report the error in `ParseFunctionSignature`, it returns `nullptr`, and in `HandleFunctionDefinition`'s recovery path, I try to skip the bad token so the REPL doesn't get stuck retrying it forever:

```cpp
static void HandleFunctionDefinition() {
  if (ParseFunctionDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // skip bad token
}
```

That `getNextToken()` call is where it actually stalls: control never makes it back to `MainLoop`, so no fresh `ready> ` gets printed.

The fix: print the prompt from inside `LogErrorExpression` itself, right after the error message, so it's already on screen before that blocking `getNextToken()` call ever happens:

```cpp
fprintf(stderr, "Error: %s (token: %s)\nready> ", Str,
        TokenNames.at(CurrentToken).c_str());
```

With both fixes in place:

```pyxc
ready> def add
Error: Expected '(' in function signature (token: newline)
ready> 
```

### Bug 3: Prompt Prints Twice

The earlier fix created a new issue.  

```pyxc
ready> def bad(x) x
Error: Expected ':' in function definition (token: name)
ready> ready>
```

`LogErrorExpression`'s baked-in `\nready> ` already prints the first one. The recovery skip in `HandleFunctionDefinition` then lands exactly on the line's trailing `tok_eol`, so `MainLoop` prints a second `ready> ` right after.

I'm not chasing this down here. [Chapter 4](chapter-04.md) replaces both mechanisms with a single `tok_error`-driven `SynchronizeToLineBoundary()` resync, so only one prompt ever prints.

## Build and Run

```bash
cd code/chapter-02
cmake -S . -B build && cmake --build build
./build/pyxc
```

The `test/` directory has lit tests covering the grammar rules. Browse them for more input examples, or run the suite:

```bash
llvm-lit test/
```

## Try It

```pyxc
ready> def add(x, y):
x + y
Parsed a function definition.
ready> def sumThree(a, b, c):
add(a, b) + c
Parsed a function definition.
ready> 1 + 2 + 3
Parsed a top-level expression.
ready> sin(1.0) + cos(2.0) # sin/cos are never defined; I don't check that yet, that's a semantic check
Parsed a top-level expression.
ready> 1 2 # legal here; Chapter 4 disallows two things on one line
Parsed a top-level expression.
Parsed a top-level expression.
ready> def a(): 1 def b(): 2 # same deal for function definitions
Parsed a function definition.
Parsed a function definition.
ready> def bad(x) x
Error: Expected ':' in function definition (token: name)
ready> ready>
```

The parser accepts valid syntax and rejects invalid syntax with an error message. The REPL keeps running after errors.

## The Full Grammar

I've collected all the grammar rules I've been writing above each parsing function, and put them here. This makes up the complete grammar for pyxc at this stage:

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-02/pyxc.ebnf)

```ebnf
(* parser territory *)
program                    = [ end-of-lines ]
                             [ top-level-item { end-of-lines top-level-item } ]
                             [ end-of-lines ] ;
end-of-lines               = end-of-line { end-of-line } ;
top-level-item             = function-definition | top-level-expression ;
function-definition        = "def" function-signature ":"
                             [ end-of-lines ] expression ;
top-level-expression       = expression ;
function-signature         = name "(" [ name { "," name } ] ")" ;
expression                 = primary { "+" primary } ;
primary                    = name-expression
                             | number-expression
                             | parenthesized-expression ;
name-expression            = name
                             | name "(" [ expression { "," expression } ] ")" ;
number-expression          = number ;
parenthesized-expression   = "(" expression ")" ;

(* lexer territory *)
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
- The bottom rules — `name`, `number`, `letter`, `digit`, `end-of-line`, `comment`, `comment-character`, `whitespace` describe what the *lexer* understands: raw characters and how they combine to form tokens. 
- The top rules — `expression`, `function-definition`, `function-signature`, etc. — describe what the *parser* understands: the syntax of things. What token follows what other token and so on.

## What's Next

I now have a parser that understands the structure of pyxc code and builds a tree of objects representing it. But it only understands one operator, `+`. [Chapter 3](chapter-03.md) adds `-`, `*`, and `<`, along with the logic needed to group them correctly.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.

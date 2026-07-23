---
description: "Build a recursive-descent parser and syntax-tree nodes: turn tokens into structure and see 'Parsed a function definition' for the first time."
---
# 2. pyxc: The Parser and Syntax Tree

## Where We Are

I'll continue working with the `add` function example from [Chapter 1](chapter-01.md).

```pyxc
# adds two numbers
def add(x, y):    
    return x + y

print(add(1, 2)) # call the add function and print its value    
```

In the last chapter I managed to strip out all the comments and convert the code into a stream of tokens. I now want to arrange these tokens into some sort of a hierarchical structure so the relationships between different items are clear. Let me do that next.  

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-02
```

## Representing Structure

### Function definitions 

For a function definition like  `def add(x, y): return x + y`, I start with the function name as the parent, and then group `parameters` and `body` underneath. This way I can look up a function by name and get all its components. Mind you, `parameters` and `body` are simple grouping mechanisms (classes and pointers). They aren't additional tokens. This will become clearer once you see the code to implement these structures.

```text
(def, "add")
├── parameters
│   ├── (name, "x")
│   └── (name, "y")
└── body
    └── return
        └── +
            ├── (name, "x")
            └── (name, "y")
```

I will do this for every function definition in the source file.

### Function calls 

I follow a similar approach for function calls. The `call` is the parent, with the function name and the call arguments as two branches beneath it. Again `call` is not a token here, it's a grouping mechanism.

`add(1, 2)` becomes:

```text
call
├── (name, "add")
└── arguments
    ├── (number, 1)
    └── (number, 2)
```

and `print(...)` becomes:

```text
call
├── (name, "print")
└── argument
    └── ...
```

Merging both we get the full tree for `print(add(1, 2))`:

```text
call
├── (name, "print")
└── argument
    └── call
        ├── (name, "add")
        └── arguments
            ├── (number, 1)
            └── (number, 2)
```

Compiler writers call this an **Abstract Syntax Tree**, or **AST**. *Abstract* means I leave out punctuation that a hierarchical structure doesn't need, such as the parentheses in `add(1, 2)`. The tree already records which arguments belong to which call. *Syntax tree*, because it represents, well, the syntax: how the pieces fit together, not whether they make sense together. For example, the tree would happily record a call to `add` with three arguments even though `add` only takes two. Catching a mismatch like that is a semantic check, and it needs a separate pass over the tree, one I'll add later once I can look up what `add` actually expects.

## Coding Trees

Now I need to turn these trees into code. Compiler writers call each piece of the tree a **node**, so I'll use that name too.

### The Base Class

Most of my nodes derive from a base `ExpressionNode` class, since they represent things that have or return a value:

```cpp
class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;
};
```

I'll create a virtual destructor for the base. Without it, deleting one of these nodes through a unique_ptr<ExpressionNode> would only run `ExpressionNode`'s destructor, skipping the derived class's. Marking it virtual fixes that. 

### Numbers

Next, I'll deal with data types. pyxc's only data type is a number, represented by a `double`. That's easy to model with a `NumberExpressionNode` that stores a double value:

```cpp
class NumberExpressionNode : public ExpressionNode {
  double Val;
public:
  NumberExpressionNode(double Val) : Val(Val) {}
};
```

### Names

The only **name values** I have right now are function parameters.  I use`NameExpressionNode` to store the name of a parameter. I haven't figured out how I'll bind values to a name in function calls just yet. 

```cpp
class NameExpressionNode : public ExpressionNode {
  string Name;
public:
  NameExpressionNode(const string &Name) : Name(Name) {}
};
```

### Binary Expressions

A `BinaryExpressionNode` stores the operator token and its two operands. 

```cpp
class BinaryExpressionNode : public ExpressionNode {
  int Op;
  unique_ptr<ExpressionNode> LHS, RHS;
public:
  BinaryExpressionNode(int Op, unique_ptr<ExpressionNode> LHS, unique_ptr<ExpressionNode> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};
```

### Function Calls

A function call stores the callee name and a list of argument expressions. I model each argument as an `ExpressionNode` so I can model `add(x, y)`, `add(1, 2)`, `add(1+2, 3+4)`:

```cpp
class CallExpressionNode : public ExpressionNode {
  string Callee;
  vector<unique_ptr<ExpressionNode>> Args;
public:
  CallExpressionNode(const string &Callee, vector<unique_ptr<ExpressionNode>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};
```

`add(x, y)`:

```
CallExpressionNode
├── Callee = "add"
└── Args
    ├── NameExpressionNode  name="x"
    └── NameExpressionNode  name="y"
```

`add(1, 2)`:

```
CallExpressionNode
├── Callee = "add"
└── Args
    ├── NumberExpressionNode  val=1
    └── NumberExpressionNode  val=2
```

`add(1+2, 3+4)`:

```
CallExpressionNode
├── Callee = "add"
└── Args
    ├── BinaryExpressionNode  op='+'
    │   ├── LHS -> NumberExpressionNode  val=1
    │   └── RHS -> NumberExpressionNode  val=2
    └── BinaryExpressionNode  op='+'
        ├── LHS -> NumberExpressionNode  val=3
        └── RHS -> NumberExpressionNode  val=4
```

### Function Signatures

I split functions into two classes. The function signature captures the name and parameter names. Since a function signature is not an expression, I don't derive it from `ExpressionNode`.

```cpp
class FunctionSignatureNode {
  string Name;
  vector<string> Args;
public:
  FunctionSignatureNode(const string &Name, vector<string> Args)
      : Name(Name), Args(std::move(Args)) {}
  const string &getName() const { return Name; }
};
```

### Function Definitions

The full function definition pairs a function signature with a body expression. Again, since a function definition is not an expression, I don't derive it from `ExpressionNode`.

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

With this structure in place, for a function definition like `def add(x, y): return x + y`, I need to build something like:

```
FunctionDefinitionNode
├── Signature -> FunctionSignatureNode  name="add"  args=["x", "y"]
└── Body -> BinaryExpressionNode  op='+'
    ├── NameExpressionNode  name="x"
    └── NameExpressionNode  name="y"
```

## The Parser

### Looking Ahead 

The parser keeps one token of lookahead in a global `CurTok`: every parse function assumes it's already loaded when called, and leaves it pointing at the first unconsumed token when it returns.

```cpp
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }
```

When parsing fails, I return `nullptr` and print a message. I use three clearly
named helpers—one per return type—because C++ can't overload on return type:

```cpp
unique_ptr<ExpressionNode> LogErrorExpression(const char *Str) {
  fprintf(stderr, "Error: %s (token: %d)\nready> ", Str, CurTok);
  return nullptr;
}
unique_ptr<FunctionSignatureNode> LogErrorSignature(const char *Str) { LogErrorExpression(Str); return nullptr; }
unique_ptr<FunctionDefinitionNode>  LogErrorFunction(const char *Str) { LogErrorExpression(Str); return nullptr; }
```

[Chapter 4](chapter-04.md) replaces the raw token number with a readable token name and source location.

## Parsing Expressions

### Numbers

Above each parsing function I write the grammar rule it implements, as a short comment directly above it. I'll build up the notation one symbol at a time. Here's the simplest form: `number-expression` is just a `number`, nothing more. The word `number` is unquoted, which means it's a placeholder: fill in a valid thing of that kind here, not the literal text `number`.

When the lexer returns `tok_number`, it has already set the global `NumVal`. I copy its current value into a node and advance:

```cpp
/// number-expression
///   = number ;
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  auto Result = make_unique<NumberExpressionNode>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}
```

### Parentheses

`parenthesized-expression` introduces one more symbol: quoted text, like `"("` and `")"`, means an exact character or keyword the parser expects to see there, not a placeholder.

Parse whatever is inside, verify the closing `)`, and return the inner expression directly. I don't create a parentheses node, the tree structure already captures the grouping:

```cpp
/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat '('
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != tok_rparen)
    return LogErrorExpression("expected ')'");
  getNextToken(); // eat ')'
  return V;
}
```

### Names and Calls

After reading a name, I peek at the next token. No `(` means it's a plain variable. A `(` means it's a function call.

```pyxc
x     # variable
foo() # function call
```

That's two forms for one rule, so the grammar needs a few more symbols: `|` means "or" (either this form or that one), `[ ... ]` means "optional, zero or one of these," and `{ ... }` means "zero or more of these, repeated." Here's the parsing code.

```cpp
/// name-expression
///   = name
///   | name "("[expression{"," expression}]")" ;
static unique_ptr<ExpressionNode> ParseNameExpression() {
  string Name = NameStr;

  getNextToken(); // eat name.

  if (CurTok != tok_lparen) // Simple variable ref.
    return make_unique<NameExpressionNode>(Name);

  // Call.
  getNextToken(); // eat (
  vector<unique_ptr<ExpressionNode>> Args;
  if (CurTok != tok_rparen) {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == tok_rparen)
        break;

      if (CurTok != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return make_unique<CallExpressionNode>(Name, std::move(Args));
}
```

### Calling the Right (mini) Parser

I have all my mini-parsers ready for different token types. `ParsePrimary` looks at `CurTok` and, based on what it sees, invokes the relevant mini-parser:

```cpp
/// primary
///   = name-expression
///   | number-expression
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurTok) {
  case tok_name: return ParseNameExpression();
  case tok_number:     return ParseNumberExpression();
  case tok_lparen:     return ParseParenthesizedExpression();
  default:
    return LogErrorExpression("unknown token when expecting an expression");
  }
}
```

### Binary Expressions

`ParsePrimary` alone stops after one name, number, or parenthesized group, so `x + y` needs one more layer. `ParseExpression` parses a primary, then loops: as long as `CurTok` is `+`, it eats the `+` and parses another primary, folding the result into a `BinaryExpressionNode`.

```cpp
/// expression
///   = primary { "+" primary } ;
static unique_ptr<ExpressionNode> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  while (CurTok == tok_plus) {
    getNextToken(); // eat '+'
    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;
    LHS = make_unique<BinaryExpressionNode>(tok_plus, std::move(LHS),
                                     std::move(RHS));
  }

  return LHS;
}
```

`{ "+" primary }` is the `{ }` repetition symbol again: zero or more `+ primary` pairs, which is exactly what the `while` loop encodes. pyxc only understands `+` for now; more operators and real operator precedence arrive in a later chapter.

## Parsing Function Definitions

### Function Signature

A function signature has a name and parameter names (no types yet — everything is `double` for now).

```cpp
/// function-signature
///   = name "(" [name {"," name}] ")" ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  if (CurTok != tok_name)
    return LogErrorSignature("Expected function name in function signature");

  string FnName = NameStr;
  getNextToken(); // eat function name

  if (CurTok != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  vector<string> ArgNames;
  while (getNextToken() == tok_name) {
    ArgNames.push_back(NameStr);

    if (getNextToken() == tok_rparen)  // eat name, check what follows
      break;

    if (CurTok != tok_comma)
      return LogErrorSignature("Expected ')' or ',' in parameter list");
    // loop continues: getNextToken() at the top eats the ','
  }

  if (CurTok != tok_rparen)
    return LogErrorSignature("Expected ')' in function signature");

  getNextToken(); // eat ')'
  return make_unique<FunctionSignatureNode>(FnName, std::move(ArgNames));
}
```

Do you see how straightforward this is? I just look at the grammar and sequence out the commands to parse those bits. Tools exist to generate parsers automatically, but writing one by hand is too good an exercise to skip, so that's what I'm doing here.

### Function Definition

Let's read function definitions now.

```cpp
/// function-definition
///   = "def" function-signature ":" [ end-of-lines ] "return" expression ;
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // eat 'def'
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;

  if (CurTok != tok_colon)
    return LogErrorFunction("Expected ':' in function definition");
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
    return LogErrorFunction("Expected 'return' in function body");
  getNextToken(); // eat 'return' — not stored in the tree

  if (auto E = ParseExpression())
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(E));
  return nullptr;
}
```

`getNextToken()` eats the `return` keyword but nothing stores it in the tree. For now every function body is a single expression, so `return` is just syntax that says "this expression is the result." In a later chapter, when functions can have multiple statements and multiple return points, `return` becomes a first-class node.

`consumeNewlines()` is trivial to implement.
```cpp
static void consumeNewlines() {
  while (CurTok == tok_eol)
    getNextToken();
}
```

## Parsing Top-Level Expressions

So far I have the infrastructure to read function definitions and call them. But what happens to bare expressions like `1 + 2 * 3`? I just wrap them in a function with an internal name and reuse the existing infrastructure to read and run it:

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

The name `__anon_expr` is a placeholder I invented — it could be any valid name. In a later chapter when I add JIT execution, I'll look up this function by name and call it to evaluate the expression immediately. Wrapping it in `FunctionDefinitionNode` now means the rest of the pipeline — code generation, optimization, JIT — doesn't need any special cases for top-level expressions. They can be treated as ordinary functions.

## The Driver

Two handler functions, one for each top-level construct. Each calls the appropriate parser, prints a success message, or skips one bad token to keep the REPL alive:

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
    case tok_def:    HandleFunctionDefinition();  break;
    default:         HandleTopLevelExpression(); break;
    }
  }
}
```

## A Bug I Found After Wiring Up MainLoop

I didn't catch this until I actually ran the REPL against MainLoop. Make an innocent error at the end of a line, and the prompt after the error never showed up. The REPL just sat there, looking frozen:

```pyxc
ready> def add   
Error: Expected '(' in function signature (token: -2)
ready>   
```

Here's why. When I hit the error at the end of `add`, `HandleFunctionDefinition` tries to skip the bad token:

```cpp
getNextToken();
```

That call blocks waiting for input. So nothing else runs:
- MainLoop doesn't run
- `ready>` doesn't get printed

The fix: print the prompt from inside `LogErrorExpression` itself, right after the error message, so it's already on screen before that blocking `getNextToken()` call ever happens. That's why the `LogErrorExpression` code back in [Error Reporting](#error-reporting) has a `\nready> ` baked into its `fprintf`.

## The final touches

`main()` prints the first prompt, loads the first token, then hands off to the loop:

```cpp
int main() {
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
ready> 1 + 2 * 3
Parsed a top-level expression.
ready> sin(1.0) + cos(2.0)
Parsed a top-level expression.
ready> def bad(x) return x
Error: Expected ':' in function definition (token: -6)
ready>
```

The parser accepts valid syntax and rejects invalid syntax with an error message. The REPL keeps running after errors.

## Things Worth Knowing

- **`1.2.3` silently lexes as `1.2`.** The lexer reads the `1.2.3` as a number but `strtod` quietly drops `.3` without explicitly saying so. I fix this in [Chapter 4](chapter-04.md).

- **Error messages show raw token numbers.** `token: -6` means `tok_return`. [Chapter 4](chapter-04.md) replaces this with readable names and source locations.

### The Full Grammar

Here's the complete grammar for Pyxc at this stage, using the shorthand I built up above:

- Quoted text, like `"def"`, for exact keywords and punctuation
- Unquoted words, like `name` or `expression`, meaning "fill in a valid thing of this kind here" (a reference to another rule, not literal text)
- `|` for "or": either this or that
- `[ ... ]` for "optional": zero or one of these
- `{ ... }` for "repeated": zero or more of these

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-02/pyxc.ebnf)

```ebnf
(* parser territory *)
program                    = [ end-of-lines ]
                             [ top-level-item { end-of-lines top-level-item } ]
                             [ end-of-lines ] ;
end-of-lines               = end-of-line { end-of-line } ;
top-level-item             = function-definition | top-level-expression ;
function-definition        = "def" function-signature ":"
                             [ end-of-lines ] "return" expression ;
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
    `whitespace` may appear between any two tokens
     and is ignored by the lexer.
*)
whitespace                 = " " | "\t" ;
```

The grammar has two layers. The bottom rules — `name`, `number`, `letter`, `digit`, `end-of-line`, `whitespace` — describe what the *lexer* understands: raw characters and how they combine to form tokens. The top rules — `expression`, `function-definition`, `function-signature`, etc. — describe what the *parser* understands: the syntax of things. What token follows what other token and so on.

### Avoiding a Grammar Pitfall: Left Recursion

There's a rule you can't write for a top-down parser: a rule that starts with itself.

```ebnf
(* DON'T DO THIS *)
expression = expression "+" primary | primary
```

The parser would try to parse `expression`, which requires parsing `expression`, which requires parsing `expression`... infinite recursion, immediate crash.

The fix is to use iteration instead of recursion:

```ebnf
expression = primary { "+" primary }
```

"Parse one primary, then loop and grab (operator, primary) pairs until there are no more." Same language, no recursion. Pyxc grammar always does this.

## What's Next

I now have a parser that understands the structure of pyxc code and builds a tree of objects representing it. But before I hook this up to LLVM and generate real machine code, [Chapter 4](chapter-04.md) revisits the lexer: readable error messages, source locations, and the keyword map. The parser works but [Chapter 4](chapter-04.md) makes it pleasant to use.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.

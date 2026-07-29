---
description: "Add subtraction, multiplication, and comparison to the lexer, then give the parser a precedence table so `1 + 2 * 3` groups the way everyone expects."
---
# 3. pyxc: Operator Precedence

## Where We Are

In this chapter, I'll expand the binary operators to include *, -, and <, and add precedence so they group the way arithmetic already does: `k < a + b * c + d` becomes `k < ((a + (b * c)) + d)`.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-03
```

## Teaching the Lexer New Operators

Before I can parse `-`, `*`, and `<`, I first have to teach my lexer to recognize them as tokens at all. For each I'll add a named enum value, a readable name for error messages, and a case in `getToken()`'s switch statement.

```cpp
enum Token {
  // ...
  tok_plus,
  tok_minus,
  tok_star,
  tok_less,
};
```

```cpp
static map<int, string> TokenNames = {
    // ...
    {tok_plus, "'+'"},
    {tok_minus, "'-'"},
    {tok_star, "'*'"},
    {tok_less, "'<'"},
};
```

```cpp
  switch (ThisChar) {
  // ...
  case '+':
    return tok_plus;
  case '-':
    return tok_minus;
  case '*':
    return tok_star;
  case '<':
    return tok_less;
  // ...    
  }
```

## Operator Precedence

I give each operator a number, and the number decides binding order: higher number, tighter binding. I keep them in a map:

```cpp
static const map<int, int> OperatorPrecedence = {
    {tok_less, 10},
    {tok_plus, 20},
    {tok_minus, 20},
    {tok_star, 40},
};
```

`+` and `-` share a precedence, since neither should bind tighter than the other. `*` outranks both. `<` binds loosest, so `k < a + b * c + d` groups the addition and the multiplication first, then compares the two results.

I write a small helper that reads that table for whatever token I'm currently looking at:

```cpp
static int GetTokenPrecedence() {
  auto It = OperatorPrecedence.find(CurrentToken);
  if (It == OperatorPrecedence.end() || It->second <= 0)
    return -1;
  return It->second;
}
```

If `CurrentToken` isn't in the table (`)`, a newline, `tok_eof`, anything that isn't one of my four operators), I return `-1`. That's not just "not found," it's a sentinel I chose so it always loses: every real precedence I hand out is 10 or higher, so wherever I ask "is there an operator here?" I get an unambiguous no.

## Binary Expressions: Precedence Climbing

In Chapter 2, I wrote `ParseExpression` as a loop that only ever looked for `+`. That doesn't survive operators with different precedence, so I split the work into two functions: `ParseBinaryOperatorRight` does the actual work, and `ParseExpression` just kicks it off.

The idea: parse a primary, then look at what follows. If it's a binary operator at least as tight as whatever minimum I'm currently allowed to accept, I consume it and parse the next primary. If the operator *after that* is tighter still, I don't grab it myself, I hand off to a fresh call of `ParseBinaryOperatorRight` with a stricter minimum, letting that call claim the tighter operator's operands before handing control back to me.

```cpp
static unique_ptr<ExpressionNode>
ParseBinaryOperatorRight(int ExpressionPrecedence,
                         unique_ptr<ExpressionNode> Left) {
  // If this is a binary operator, find its precedence.
  while (true) {
    int TokenPrecedence = GetTokenPrecedence();

    // If this binary operator binds at least as tightly as the current
    // expression, consume it; otherwise, I'm done.
    if (TokenPrecedence < ExpressionPrecedence)
      return Left;

    // Okay, this is a binary operator.
    int Operator = CurrentToken;
    getNextToken(); // eat binary operator

    // Parse the primary expression after the binary operator.
    auto Right = ParsePrimary();
    if (!Right)
      return nullptr;

    // If Operator binds less tightly with Right than the operator after Right,
    // let the pending operator take Right as its Left.
    int NextTokenPrecedence = GetTokenPrecedence();
    if (TokenPrecedence < NextTokenPrecedence) {
      Right = ParseBinaryOperatorRight(TokenPrecedence + 1, std::move(Right));
      if (!Right)
        return nullptr;
    }

    // Merge Left/Right.
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }
}
```

Let me trace `k < a + b * c + d`:

1. Called with a minimum precedence of 0 and `Left = k`. Current operator is `<` (10).
2. 10 ≥ 0: consume `<`. Parse `a` as `Right`. Next operator is `+` (20).
3. 20 > 10: recurse: `ParseBinaryOperatorRight(11, a)`.
4. Inside that call: current operator is `+` (20). 20 ≥ 11: consume `+`. Parse `b` as `Right`. Next operator is `*` (40).
5. 40 > 20: recurse again: `ParseBinaryOperatorRight(21, b)`.
6. Inside the deeper call: current operator is `*` (40). 40 ≥ 21: consume `*`. Parse `c`. Next operator is `+` (20). 20 < 21: stop, return `b * c`.
7. Back in the `min = 11` call: `Right = b * c`. Build `a + (b * c)`. Loop again: next operator is `+` (20). 20 ≥ 11: consume `+`. Parse `d`. Nothing follows: return `(a + (b * c)) + d`.
8. Back in the outermost call: `Right = (a + (b * c)) + d`. Build `k < ((a + (b * c)) + d)`. Nothing follows: return. Final tree: `k < ((a + (b * c)) + d)`.

In step 5, adding 1 to `TokenPrecedence` serves a specific purpose: once I've recursed into a tighter operator, it stops that recursive call from also swallowing an operator back at the *original* level. `a - b - c` doesn't actually exercise this, since both `-`s are equal precedence and the recursion never triggers for them at all. `a - b * c - d` does: the recursive call handling `b * c` gets a minimum of 21, not 20, so when it loops back and sees the second `-` at precedence 20, `20 < 21` correctly kicks control back to the outer frame instead of letting the inner call consume it too. Without the `+1`, that inner call would grab the second `-` as well, building `a - ((b * c) - d)` instead of `(a - (b * c)) - d`. Compiler writers call the correct grouping left-associativity, the same grouping I built in Chapter 2 for chains of `+`. That `+1` is the one detail responsible for it: I only let a recursive call claim an operator strictly tighter than the one that triggered it, so same-level operators always fall through to the *outer* loop, which is how I keep building leftward.

With `ParseBinaryOperatorRight` doing the looping, `ParseExpression` simplifies to:

```cpp
static unique_ptr<ExpressionNode> ParseExpression() {
  auto Left = ParsePrimary();
  if (!Left)
    return nullptr;

  return ParseBinaryOperatorRight(0, std::move(Left));
}
```

Passing `0` as the minimum precedence means "accept any operator I know about," which is exactly the right starting condition for a fresh expression.

## Build and Run

```bash
cd code/chapter-03
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit test/
```

## Try It

```pyxc
ready> def add(x, y):
x + y
Parsed a function definition.
ready> def fib(n):
fib(n-1) + fib(n-2)
Parsed a function definition.
ready> 1 + 2 * 3
Parsed a top-level expression.
ready> sin(1.0) + cos(2.0)
Parsed a top-level expression.
ready> -5
Error: unknown token when expecting an expression (token: '-')
ready> Parsed a top-level expression. # the leftover `5` parses as its own top-level expression once recovery skips the bad `-`
ready>
```

That last line shows a real gap: `-` only works as a binary operator right now, not a unary one. I wired `tok_minus` into `OperatorPrecedence` and `ParseBinaryOperatorRight`, but `ParsePrimary` still has no case for a leading `-`, so it can't start an expression on its own. `x - -3` fails the same way. Negative literals and unary negation are a later chapter's problem.

### The Full Grammar

Here's the complete grammar for pyxc at this stage. The two rules Chapter 2's grammar didn't have are marked.

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-03/pyxc.ebnf)

```ebnf
program                           = [ end-of-lines ]
                                    [ top-level-item
                                      { end-of-lines top-level-item } ]
                                    [ end-of-lines ] ;
end-of-lines                      = end-of-line { end-of-line } ;
top-level-item                    = function-definition
                                    | top-level-expression ;
function-definition               = "def" function-signature ":"
                                    [ end-of-lines ] expression ;
top-level-expression              = expression ;
function-signature                = name "(" [ name { "," name } ] ")" ;
expression                        = primary binary-operator-right ;  (* changed *)
binary-operator-right             = { binary-operator primary } ;  (* new *)
primary                           = name-expression
                                    | number-expression
                                    | parenthesized-expression ;
name-expression                   = name
                                    | name "("
                                      [ expression { "," expression } ] ")" ;
number-expression                 = number ;
parenthesized-expression          = "(" expression ")" ;
binary-operator                   = "+" | "-" | "*" | "<" ;  (* new *)
name                              = (letter | "_")
                                    { letter | digit | "_" } ;
number                            = digit { digit } [ "." { digit } ]
                                    | "." digit { digit } ;
letter                            = "A".."Z" | "a".."z" ;
digit                             = "0".."9" ;
end-of-line                       = "\r\n" | "\r" | "\n" ;
(*
    A `comment` begins with "#" and continues to the end of the line. The lexer
     ignores its text and returns an end-of-line token when one follows it.
*)
comment                           = "#" { comment-character } ;
comment-character                 = ? any character except "\r" and "\n" ? ;
(* 
    `whitespace` may appear before or between tokens
     and is ignored by the lexer.
*)
whitespace                        = " " | "\t" | "\v" | "\f" ;
```

I changed `expression` to route through `binary-operator-right` instead of hardcoding `"+"`, and I added `binary-operator-right` and `binary-operator`, neither of which needed to exist when `+` was the only operator in the language. Everything else, the whole shape of a function definition, a call, a name, a number, is exactly what it was in Chapter 2.

## What's Next

I now have a parser that understands operator precedence, but its error messages still show only a single readable token name, with no idea which line or column it came from. [Chapter 4](chapter-04.md) fixes that: source locations, caret diagnostics, and a proper keyword table.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.

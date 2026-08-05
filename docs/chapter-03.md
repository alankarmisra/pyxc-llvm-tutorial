---
description: "Add subtraction, multiplication, and comparison to the lexer, then give the parser a precedence table so `1 + 2 * 3` groups the way everyone expects."
---
# 3. pyxc: Operator Precedence

## Where We Are

In this chapter, I'll expand the binary operators to include `*`, `-`, and `<`, and add operator precedence rules so they group the way arithmetic already does: `k < a + b * c + d` becomes `k < ((a + (b * c)) + d)`.

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

## Defining Operator Precedence

I'm going to introduce some terminology here so I have some vocabulary to use later on in my thinking process.

Consider `a * b + c * d`. By standard arithmetic convention, it is grouped as:

```pyxc
(a * b) + (c * d)
```

- In terms of operators, `*` **binds tighter** than `+`. Each multiplication takes its operands before `+` takes the results.
- In terms of precedence, the multiplications belong to a **higher-precedence tier**, while the addition belongs to a **lower-precedence tier**.

Conceptually, the grouped expression looks like this:

```pyxc
# Higher precedence
r1 = a * b
r2 = c * d

# Lower precedence
result = r1 + r2
```

I assign each precedence tier a number and store the operators in a map:

```cpp
static const map<int, int> OperatorPrecedence = {
    {tok_less, 10},
    {tok_plus, 20},
    {tok_minus, 20},
    {tok_star, 40},
};
```

- `*` has the highest precedence and binds the tightest.
- `+` and `-` form the middle precedence tier.
- `<` has the lowest precedence and binds the loosest.

I write a small helper that reads the precedence of the token I'm currently looking at:

```cpp
static int GetTokenPrecedence() {
  auto It = OperatorPrecedence.find(CurrentToken);
  if (It == OperatorPrecedence.end() || It->second <= 0)
    return -1;
  return It->second;
}
```

If `CurrentToken` isn't an operator, and consequently not in the table, I return `-1`. This tells my expression parsing function that there is nothing more to process.

## Parsing Binary Expressions

### Parsing Operators Explicitly

I'm going to start thinking and iterating in code. The first thing I'd reach for: one function per precedence tier, each parsing the operators assigned to that tier.

```cpp
// I parse the tightest binary tier: multiplication.
// Because no binary operator binds tighter than '*', I parse each operand
// directly as a primary.
static unique_ptr<ExpressionNode> ParseStar() {
  auto Left = ParsePrimary();

  while (CurrentToken == tok_star) {
    getNextToken(); // eat '*'
    auto Right = ParsePrimary();

    // I fold repeated multiplications into the left side:
    // a * b * c becomes (a * b) * c.
    Left = make_unique<BinaryExpressionNode>(
        tok_star, std::move(Left), std::move(Right));
  }

  return Left;
}

// I parse the additive tier. I ask ParseStar() for each operand so any
// multiplication is grouped before I consume '+' or '-'.
// If I find no additive operator, I return the subtree ParseStar() built.
static unique_ptr<ExpressionNode> ParsePlusMinus() {
  auto Left = ParseStar();

  while (CurrentToken == tok_plus
      || CurrentToken == tok_minus) {
    int Operator = CurrentToken;
    getNextToken(); // eat '+' or '-'
    auto Right = ParseStar();

    // I fold operators at the same tier into the left side.
    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}

// I parse the loosest tier. I ask ParsePlusMinus() for each operand so
// arithmetic is grouped before I consume '<'.
// If I find no '<', I return the subtree ParsePlusMinus() built.
static unique_ptr<ExpressionNode> ParseLess() {
  auto Left = ParsePlusMinus();

  while (CurrentToken == tok_less) {
    getNextToken(); // eat '<'
    auto Right = ParsePlusMinus();

    Left = make_unique<BinaryExpressionNode>(
        tok_less, std::move(Left), std::move(Right));
  }

  return Left;
}
```

### Passing the Precedence Tier

The functions have the same overall shape. Each one parses `Left`, consumes the operators in its tier, parses `Right`, and merges them. For now, I still check for each tier's operator tokens directly. In `ParseStar()` and `ParseLess()`  I check for one operator token:

```cpp
static unique_ptr<ExpressionNode> ParseStar() {
    ...
    while (CurrentToken == tok_star) // <-- one token
    ...
}

static unique_ptr<ExpressionNode> ParseLess() {
    ...
    while (CurrentToken == tok_less) // <-- one token
    ...
}
```

and in `ParsePlusMinus()` I check for two operator tokens.

```cpp
static unique_ptr<ExpressionNode> ParsePlusMinus() {
    ...
    while (CurrentToken == tok_plus
        || CurrentToken == tok_minus) // <-- two tokens
    ...
}
```

But if I used the precedence tiers, I could reduce the checking to the current precedence tier instead of the operator tokens.

```cppdiff
// I pass the tier into ParseStar so I can replace the tok_star check.
-static unique_ptr<ExpressionNode> ParseStar() {
+static unique_ptr<ExpressionNode> ParseStar(int PrecedenceTier) {
  auto Left = ParsePrimary();

  // I compare precedence tiers instead of checking specifically for '*'.
-  while (CurrentToken == tok_star) {
+  while (GetTokenPrecedence() == PrecedenceTier) {
+    // I save the operator because any token assigned to this tier can match.
+    int Operator = CurrentToken;
    getNextToken();

    auto Right = ParsePrimary();

    // I use the operator I saved instead of hard-coding tok_star.
    Left = make_unique<BinaryExpressionNode>(
-        tok_star, std::move(Left), std::move(Right));
+        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}

// I pass the tier into ParsePlusMinus so I can replace the '+' and '-' checks.
-static unique_ptr<ExpressionNode> ParsePlusMinus() {
+static unique_ptr<ExpressionNode> ParsePlusMinus(int PrecedenceTier) {
  // I pass the multiplication tier to ParseStar.
-  auto Left = ParseStar();
+  auto Left = ParseStar(OperatorPrecedence.at(tok_star));

  // I compare one precedence tier instead of checking both '+' and '-'.
-  while (CurrentToken == tok_plus || CurrentToken == tok_minus) {
+  while (GetTokenPrecedence() == PrecedenceTier) {
    int Operator = CurrentToken;
    getNextToken();

    // I pass the multiplication tier to ParseStar for Right too.
-    auto Right = ParseStar();
+    auto Right = ParseStar(OperatorPrecedence.at(tok_star));

    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}

// I pass the tier into ParseLess so I can replace the tok_less check.
-static unique_ptr<ExpressionNode> ParseLess() {
+static unique_ptr<ExpressionNode> ParseLess(int PrecedenceTier) {
  // I use tok_plus to look up the tier shared by '+' and '-'.
-  auto Left = ParsePlusMinus();
+  auto Left = ParsePlusMinus(OperatorPrecedence.at(tok_plus));

  // I compare precedence tiers instead of checking specifically for '<'.
-  while (CurrentToken == tok_less) {
+  while (GetTokenPrecedence() == PrecedenceTier) {
+    // I save the operator because any token assigned to this tier can match.
+    int Operator = CurrentToken;
    getNextToken();

    // I pass the additive tier to ParsePlusMinus for Right too.
-    auto Right = ParsePlusMinus();
+    auto Right = ParsePlusMinus(OperatorPrecedence.at(tok_plus));

    // I use the operator I saved instead of hard-coding tok_less.
    Left = make_unique<BinaryExpressionNode>(
-        tok_less, std::move(Left), std::move(Right));
+        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}
```

I start parsing at the loosest tier:

```cpp
auto Expression =
    ParseLess(OperatorPrecedence.at(tok_less));
```

### Merging the Tier Parsers

Now the loop and merge logic look identical. The only meaningful difference I have left is how I parse `Left` and `Right`. In the tightest tier, I call `ParsePrimary()`. In every other tier, I call the next tighter parser and pass it the corresponding precedence from the map.

I call the next higher precedence first so I collect its operands before I return to the current tier. So let me write a helper that I use to find the next precedence tier, and then I can merge all three functions into one. I have to be careful, though, because `ParseStar()` doesn't have a next precedence; I use `ParsePrimary()` for `Left` and `Right` there.

```cpp
static int GetNextPrecedenceTier(int CurrentPrecedenceTier) {
  int NextPrecedenceTier = -1;

  for (const auto &[Operator, PrecedenceTier] : OperatorPrecedence) {
    if (PrecedenceTier <= CurrentPrecedenceTier)
      continue;

    // I already know PrecedenceTier is higher than the current tier.
    // I keep it if it is my first candidate, or if it is lower than the
    // best candidate I have found so far, which makes it the closer tier.
    if (NextPrecedenceTier == -1 ||
        PrecedenceTier < NextPrecedenceTier)
      NextPrecedenceTier = PrecedenceTier;
  }

  return NextPrecedenceTier;
}
```

When my recursion reaches the highest precedence tier, `GetNextPrecedenceTier()` returns `-1` because I have no higher tier in the map. I use that as my cue to call `ParsePrimary()`. The merged function looks like this:

```cpp
static unique_ptr<ExpressionNode>
ParsePrecedenceTier(int PrecedenceTier) {
  int NextPrecedenceTier =
      GetNextPrecedenceTier(PrecedenceTier);

  unique_ptr<ExpressionNode> Left;

  if (NextPrecedenceTier == -1)
    Left = ParsePrimary();
  else
    Left = ParsePrecedenceTier(NextPrecedenceTier);

  while (GetTokenPrecedence() == PrecedenceTier) {
    int Operator = CurrentToken;
    getNextToken();

    unique_ptr<ExpressionNode> Right;

    if (NextPrecedenceTier == -1)
      Right = ParsePrimary();
    else
      Right = ParsePrecedenceTier(NextPrecedenceTier);

    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}
```

I start the merged parser at the lowest precedence tier:

```cpp
auto Expression =
    ParsePrecedenceTier(OperatorPrecedence.at(tok_less));
```

### Precedence Climbing

I no longer need to find the next registered tier. Instead, I can make each recursive call accept any operator whose precedence is above a minimum. This changes my function from walking exact tiers to climbing by minimum precedence.

```cpp
static unique_ptr<ExpressionNode>
ParseBinaryExpression(int MinimumPrecedence) {
  auto Left = ParsePrimary();

  while (true) {
    int TokenPrecedence = GetTokenPrecedence();

    // I leave this operator for an earlier call if it binds more loosely
    // than the minimum precedence I require here.
    if (TokenPrecedence < MinimumPrecedence)
      return Left;

    int Operator = CurrentToken;
    getNextToken();

    // I use + 1 to accept only operators that bind more tightly than
    // the current operator.
    auto Right =
        ParseBinaryExpression(TokenPrecedence + 1);

    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }
}

// start the whole thing
auto Expression =
    ParseBinaryExpression(OperatorPrecedence.at(tok_less));
```

`TokenPrecedence + 1` does not mean that the next registered tier is numerically one higher. It means that I accept only operators that bind more tightly than the current operator. With tiers `10`, `20`, and `40`, passing `21` still accepts `40`.

This works. But now I recurse even when I don't need to. I'll optimize it one more time and add a check to see whether I need to recurse.

```diff
 static unique_ptr<ExpressionNode>
-ParseBinaryExpression(int MinimumPrecedence) {
-  auto Left = ParsePrimary();
-
+ParseBinaryOperatorRight(int MinimumPrecedence,
+                         unique_ptr<ExpressionNode> Left) {
+  // I find the precedence of the binary operator in front of me.
   while (true) {
     int TokenPrecedence = GetTokenPrecedence();

-    // I leave this operator for an earlier call if it binds more loosely
-    // than the minimum precedence I require here.
+    // I stop if this operator binds more loosely than the minimum
+    // precedence accepted by this call.
     if (TokenPrecedence < MinimumPrecedence)
       return Left;

+    // I save the operator before I consume it.
     int Operator = CurrentToken;
-    getNextToken();
+    getNextToken(); // eat binary operator

-    // I use + 1 to accept only operators that bind more tightly than
-    // the current operator.
-    auto Right =
-        ParseBinaryExpression(TokenPrecedence + 1);
+    // I parse the primary expression after the binary operator.
+    auto Right = ParsePrimary();
+    if (!Right)
+      return nullptr;
+
+    // If the operator after Right binds tighter than Operator, I let it
+    // consume Right before I merge Operator with Left.
+    int NextTokenPrecedence = GetTokenPrecedence();
+    if (TokenPrecedence < NextTokenPrecedence) {
+      Right = ParseBinaryOperatorRight(TokenPrecedence + 1, std::move(Right));
+      if (!Right)
+        return nullptr;
+    }

-    Left = make_unique<BinaryExpressionNode>(
-        Operator, std::move(Left), std::move(Right));
+    // I merge Left and Right under Operator.
+    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
+                                             std::move(Right));
   }
 }
```

This is the actual function in `pyxc.cpp`. I never ask "what's the next tier?" I use `+ 1` and the minimum-precedence comparison to accept any tighter tier without keeping another lookup table that says what comes next.

Here's the tree I build for `a + b * c`. I tag each row with the precedence of the operator that owns it:

```ast
20 │ BinaryExpression '+'
20 │ ├── Left  -> a
40 │ └── Right -> BinaryExpression '*'
40 │              ├── Left  -> b
40 │              └── Right -> c
```

I produce the `20` rows and the `40` rows in two different calls to `ParseBinaryOperatorRight`. When I see `*` waiting on the right of `+`, I know that `*` binds tighter (`40 > 20`), so I start a new call and pass `Right` to it before I merge anything. In that call, I build `b * c`.

I handle the full expression, `k < a + b * c + d`, the same way, just with a third tier:

```ast
10 │ BinaryExpression '<'
10 │ ├── Left  -> k
20 │ └── Right -> BinaryExpression '+'
20 │              ├── Left  -> BinaryExpression '+'
20 │              │            ├── Left  -> a
40 │              │            └── Right -> BinaryExpression '*'
40 │              │                         ├── Left  -> b
40 │              │                         └── Right -> c
20 │              └── Right -> d
```

I build both `+` nodes in the same call, so both are tagged `20`. Because the two `+` operators have equal precedence, I don't start another recursive call for the second one. I stay in the same call, loop around, and merge again. That is what makes the chain nest left instead of right: `(a + (b * c)) + d`, not `a + ((b * c) + d)`. I handle `<` (`10`) and `*` (`40`) in separate calls because they are strictly looser or tighter than their neighbors.

Because I put the loop in `ParseBinaryOperatorRight`, I can simplify `ParseExpression` to:

```cpp
static unique_ptr<ExpressionNode> ParseExpression() {
  auto Left = ParsePrimary();
  if (!Left)
    return nullptr;

  return ParseBinaryOperatorRight(0, std::move(Left));
}
```

I pass `0` as the minimum precedence so the first call accepts any operator I know about. That gives me the right starting condition for a fresh expression.

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

## The Full Grammar

Here's the full grammar, shown as a diff against Chapter 2's. I reflowed Chapter 2's rules to the same column width as Chapter 3's so only the real grammar changes show up as `+`/`-`, not the column shift caused by `binary-operator-right` being a longer name than anything Chapter 2 had.

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-03/pyxc.ebnf)

```grammardiff
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
-expression                        = primary { "+" primary } ;
+expression                        = primary binary-operator-right ;
+binary-operator-right             = { binary-operator primary } ;
 primary                           = name-expression
                                     | number-expression
                                     | parenthesized-expression ;
 name-expression                   = name
                                     | name "("
                                       [ expression { "," expression } ] ")" ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
+binary-operator                   = "+" | "-" | "*" | "<" ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 number                            = digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
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

---
description: "Encode operator precedence in the grammar, then parse subtraction, multiplication, division, and comparison with one function for each grammar layer."
---
# 3. pyxc: Encoding Precedence in the Grammar

## What I Am Building

In this chapter, I introduce the `-`, `*`, `/`, and `<` operators.  

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-03
```

## Grammar

Here is the grammar as a diff against [Chapter 2](chapter-02.md). 

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-03/pyxc.ebnf)

```grammardiff
*...
*parameters                        = parameter { "," parameter } ;
*parameter                         = name ;
-expression                        = sum ;
+expression                        = comparison ;
+comparison                        = sum { "<" sum } ;
-sum                               = term { "+" term } ;
+sum                               = term { ("+" | "-") term } ;
-term                              = primary ;
+term                              = primary { ("*" | "/") primary } ;
*primary                           = name-expression
*                                    | number-expression
*...
```

I changed only the expression hierarchy. I route `expression` through `comparison`, add `<` at the comparison tier, add `-` at the sum tier, and add `*` and `/` at the term tier. The grammar for functions, calls, names, numbers, comments, and whitespace stays the same.

## Encoding the Tiers in the Grammar

I first build the `*` and `/` groups. I use those groups to build the `+` and `-` groups, then I use the completed `+` and `-` groups as the operands of `<`.

For example:

```pyxc
k < a * b + c * d
```

becomes:

```pyxc
k < ((a * b) + (c * d))
```

I need a few names for the pieces of this expression:

- A **primary** is one basic operand: a name such as `a`, a number such as `2`, or a parenthesized expression such as `(a + b)`. The expression inside parentheses may be larger, but I treat the complete parenthesized group as one operand.
- A **term** is one multiplication/division group. It may be a single primary such as `k`, or several primaries joined by `*` or `/`, such as `a * b`.
- A **sum** is one addition/subtraction group. It may be a single term, or several terms joined by `+` or `-`, such as `(a * b) + (c * d)`.
- A **comparison** combines sums with `<`. In this example, I compare the sum `k` with the sum `(a * b) + (c * d)`.

With that vocabulary, I can sketch the parser as the following functions:

```cpp
// Pseudocode
Expression ParseComparison() {
  Expression Left = ParseSum();   // I parse k as a sum containing one term.
  getNextToken();                 // I eat '<'.
  Expression Right = ParseSum();  // I parse (a * b) + (c * d).
  return Left < Right;            // I combine the two sums.
}
```

```cpp
// Pseudocode
Expression ParseSum() {
  Expression Left = ParseTerm();   // I parse a * b as the first term.
  int Operator = CurrentToken;     // I remember whether I saw '+' or '-'.
  getNextToken();                  // I eat the operator.
  Expression Right = ParseTerm();  // I parse c * d as the next term.
  return Operator == '+' ? Left + Right : Left - Right; // I build the sum.
}
```

```cpp
// Pseudocode
Expression ParseTerm() {
  Expression Left = ParsePrimary();  // I parse one name, number, or parenthesized expression.
  int Operator = CurrentToken;       // I remember whether I saw '*' or '/'.
  getNextToken();                    // I eat the operator.
  Expression Right = ParsePrimary(); // I parse the next primary.
  return Operator == '*' ? Left * Right : Left / Right; // I build the term.
}
```

These are deliberately small sketches of the example above. In the real parser, I let each function return its first operand when it does not find an operator from its own group. I also use a loop so it can parse more than one operator from that group.

I build primaries into terms, terms into sums, and sums into comparisons. Using precedence terminology, I say that `*` and `/` **bind tighter** than `+` and `-`, which bind tighter than `<`.

I can express the same chain of calls in the grammar:

```ebnf
expression = comparison ;
comparison = sum { "<" sum } ;
sum        = term { ("+" | "-") term } ;
term       = primary { ("*" | "/") primary } ;
primary    = name-expression
           | number-expression
           | parenthesized-expression ;
```

I place the loosest-binding rule at the top and the tightest-binding rule at the bottom. I place `primary` below them because it gives me the individual operands. When I parse an expression, I work down to `primary`, then use the completed tighter operands as I return through the looser rules.

## Teaching the Lexer New Operators

Before I can use these grammar rules, I have to teach the lexer to recognize the new operators. Chapter 2 already had `+`. I add named tokens for `-`, `*`, `/`, and `<` beside it:

```cppdiff
*enum Token {
*  ...
*  tok_plus,
+  tok_minus,
+  tok_star,
+  tok_slash,
+  tok_less,
*};
```

I give each token a readable name for error messages:

```cppdiff
*static map<int, string> TokenNames = {
*    ...
-    {tok_colon, "':'"},        {tok_plus, "'+'"},
+    {tok_colon, "':'"},        {tok_plus, "'+'"},    {tok_minus, "'-'"},
+    {tok_star, "'*'"},         {tok_slash, "'/'"},   {tok_less, "'<'"},
*};
```

Finally, I return the corresponding token when the lexer reads each character:

```cppdiff
*  switch (ThisChar) {
*  ...
*  case '+':
*    return tok_plus;
+  case '-':
+    return tok_minus;
+  case '*':
+    return tok_star;
+  case '/':
+    return tok_slash;
+  case '<':
+    return tok_less;
*  default:
*    return tok_error;
*  }
```

These are all single-character operators. Multi-character operators such as `==` and `<=` need a little more lexer logic, so I leave those for a later chapter.

## Writing One Parser for Each Grammar Layer

I already have `ParsePrimary()`, `ParseTerm()`, and `ParseSum()` from [Chapter 2](chapter-02.md). I update `ParseTerm()` and `ParseSum()` to match their expanded grammar rules, then add `ParseComparison()` for the new comparison layer.

```cpp
/// term
///   = primary { ("*" | "/") primary } ;
static unique_ptr<ExpressionNode> ParseTerm() {
  // I start the term by parsing one primary.
  auto Left = ParsePrimary();
  if (!Left)
    return nullptr;

  // I consume only the operators that belong to this tier.
  while (CurrentToken == tok_star || CurrentToken == tok_slash) {
    int Operator = CurrentToken;
    getNextToken(); // I eat '*' or '/'.
    auto Right = ParsePrimary();
    if (!Right)
      return nullptr;

    // I fold each new operation into the tree on my left.
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }

  return Left;
}
```

And here's how I changed `ParseSum()`.

```cppdiff
*/// sum
-///   = term { "+" term } ;
+///   = term { ("+" | "-") term } ;
*static unique_ptr<ExpressionNode> ParseSum() {
+  // I call ParseTerm() so I finish every tighter * or / operation first.
*  auto Left = ParseTerm();
*  if (!Left)
*    return nullptr;
*
-  while (CurrentToken == tok_plus) {
-    getNextToken(); // I eat '+'.
+  while (CurrentToken == tok_plus || CurrentToken == tok_minus) {
+    int Operator = CurrentToken;
+    getNextToken(); // I eat '+' or '-'.
*    auto Right = ParseTerm();
*    if (!Right)
*      return nullptr;
-    Left = make_unique<BinaryExpressionNode>(tok_plus, std::move(Left),
+    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
*                                             std::move(Right));
*  }
*
*  return Left;
*}
```

```cpp
/// comparison
///   = sum { "<" sum } ;
static unique_ptr<ExpressionNode> ParseComparison() {
  // I call ParseSum() so I finish both sums before I build the comparison.
  auto Left = ParseSum();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_less) {
    int Operator = CurrentToken;
    getNextToken(); // I eat '<'.
    auto Right = ParseSum();
    if (!Right)
      return nullptr;
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }

  return Left;
}
```

```cpp
/// expression
///   = comparison ;
static unique_ptr<ExpressionNode> ParseExpression() {
  // I start at the loosest tier so the expression can contain every tier.
  return ParseComparison();
}
```

I implement the grammar's `{ ... }` repetition with `while` loops. If I find no operator from the current tier, I return the first operand unchanged. If I find one or more, I keep adding binary nodes to `Left`.

I make `ParseTerm()` get its operands from `ParsePrimary()`. I use `ParseExpression()` as the entry point and call `ParseComparison()` first so the expression can contain every layer.

## Left Associativity

I group operators from different tiers through the order in which I call my parsing functions. I make `ParseSum()` call `ParseTerm()`, so I finish `*` and `/` expressions before I use them as operands for `+` or `-`. That is how I make `*` and `/` bind tighter than `+` and `-`.

I still need to choose how I group repeated operators from the same tier.

For example, I could group `8 / 2 / 2` in two ways:

- If I group from the left, I get `(8 / 2) / 2`, which produces `2`.
- If I group from the right, I get `8 / (2 / 2)`, which produces `8`.

Unless I find parentheses that require the second form, I choose the first one. I group the division operations from left to right. This choice is **left associativity**.

I implement that choice in the loop inside `ParseTerm()`:

```cpp
auto Left = ParsePrimary();
if (!Left)
  return nullptr;

while (CurrentToken == tok_star || CurrentToken == tok_slash) {
  int Operator = CurrentToken;
  getNextToken(); // I eat '*' or '/'.
  auto Right = ParsePrimary();
  if (!Right)
    return nullptr;

  // I fold each new operation into the tree on my left.
  Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                           std::move(Right));
}
```

I begin by parsing `8` into `Left`. When I see the first `/`, I parse `2` into `Right` and replace `Left` with this tree:

`(8 / 2)`

```ast
BinaryExpression '/'
├── Left  -> 8
└── Right -> 2
```

When I see the second `/`, `Left` already contains the entire first tree. I parse the final `2` into `Right` and replace `Left` again:

`((8 / 2) / 2)`

```ast
BinaryExpression '/'
├── Left  -> BinaryExpression '/'
│            ├── Left  -> 8
│            └── Right -> 2
└── Right -> 2
```

For each new operator, I use the entire tree I have already built as its `Left` operand. I use the same loop shape for `+`, `-`, and `<`.

I create precedence by choosing which parsing function I call for each operand. I create left associativity within each tier by replacing `Left` inside its loop.

## Build and Run

```bash
cd code/chapter-03
cmake -S . -B build && cmake --build build
./build/pyxc
```

I run the chapter tests with:

```bash
llvm-lit -v test/
```

## Try It

<!-- code-merge:start -->
```pyxc
ready> def scale_and_add(x, y, scale):
x + y * scale
```
```text
Parsed a function definition.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> def fib(n):
fib(n-1) + fib(n-2)
```
```text
Parsed a function definition.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> 1 + 2 * 3
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> 8 / 2 + 1
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> 1 < 2 + 3 * 4
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> -5
```
```text
Error: Unexpected '-' (token: '-')
ready> Parsed a top-level expression.
```
<!-- code-merge:end -->

The final example shows two separate limitations. First, `-` works only as a binary operator. I put `tok_minus` in `ParseSum()`, but `ParsePrimary()` still has no case for a leading `-`. That means `-5` and `x - -3` both fail. I will add unary operators later.

Second, Chapter 3 has only crude error recovery. After the parse fails, I skip the bad `-` token and continue. That leaves `5` to be parsed as a separate top-level expression, which produces the second message.

## What's Next

I now enforce operator precedence by structuring the parser around grammar layers. `-5` still doesn't parse, since `ParsePrimary()` has no case for a leading `-`. In [Chapter 4](chapter-04.md), I close that gap and add `%` alongside it. My error messages still show only a token name, without the source line or column — [Chapter 5](chapter-05.md) fixes that with real source locations and caret-style diagnostics.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`

I'll help you figure it out.

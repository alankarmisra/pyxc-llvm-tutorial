---
description: "Add unary minus and the remainder operator, closing the gap left by Chapter 3, by giving multiplication and division a new operand tier."
---
# 4. pyxc: Completing Basic Arithmetic

## What I Am Building

In Chapter 3, `-` only ever showed up as subtraction. A leading `-` doesn't parse at all:

<!-- code-merge:start -->
```pyxc
ready> -5
```
```text
Error: Unexpected '-' (token: '-')
```
<!-- code-merge:end -->

`%` doesn't exist either — there's no way to ask for a remainder. I close both gaps in this chapter.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-04
```

## Grammar

I insert `factor` between `term` and `primary`. `term` now loops over `factor` instead of `primary` directly, and `factor` is where `-` and `%` both live:

`code/chapter-04/pyxc.ebnf`

```grammardiff
*...
*comparison                        = sum { "<" sum } ;
*sum                               = term { ("+" | "-") term } ;
-term                              = primary { ("*" | "/") primary } ;
+term                              = factor { ("*" | "/" | "%") factor } ;
+factor                            = "-" factor
+                                    | primary ;
*primary                           = name-expression
*                                    | number-expression
*...
```

I could have special-cased `-` inside `ParsePrimary` instead of giving it its own tier, but then `--5` and `-(x + 1)` wouldn't fall out naturally — I'd have to handle chaining by hand at the call site. Giving `factor` its own self-recursive rule means `-` in front of *anything* that can start a `factor`, including another `-`, just works. `%` doesn't need any of that; it's a plain sibling of `*` and `/` at the same tier.

## A New Token

I add one token for `%`:

```cppdiff
*enum Token {
*  ...
*  tok_star,
*  tok_slash,
+  tok_percent,
*  tok_less,
*};
```

And give it a readable name for error messages, right alongside `%`'s siblings:

```cppdiff
*static map<int, string> TokenNames = {
*    ...
-    {tok_star, "'*'"},         {tok_slash, "'/'"},   {tok_less, "'<'"},
+    {tok_star, "'*'"},         {tok_slash, "'/'"},   {tok_percent, "'%'"},
+    {tok_less, "'<'"},
*};
```

And return it from the lexer:

```cppdiff
*  switch (ThisChar) {
*  ...
*  case '/':
*    return tok_slash;
+  case '%':
+    return tok_percent;
*  case '<':
*    return tok_less;
*  default:
*    return tok_error;
*  }
```

## A New AST Node

Unary minus needs a node shaped differently from `BinaryExpressionNode` — one operand, not two:

```cpp
/// UnaryExpressionNode - Expression class for applying a unary operator.
class UnaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Operand;

public:
  UnaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Operand)
      : Operator(Operator), Operand(std::move(Operand)) {}
};
```

I store `Operator` as an `int`, the same type `CurrentToken` already is, even though `-` is the only unary operator I have right now. That leaves room to add `!` or `~` later without changing the node's shape.

## Parsing `factor`

`ParseTerm` calls `ParseFactor` for each operand instead of `ParsePrimary`:

`ParseFactor` needs a forward declaration above `ParseUnaryMinus` because the two functions call each other: `ParseUnaryMinus` needs `ParseFactor` to parse its operand, and `ParseFactor` needs `ParseUnaryMinus` to handle the `-` case. Whichever one I define first has to declare the other ahead of its own body.

```cpp
static unique_ptr<ExpressionNode> ParseFactor();

/// I parse the unary-minus branch of factor.
static unique_ptr<ExpressionNode> ParseUnaryMinus() {
  getNextToken(); // I eat '-'.
  auto Operand = ParseFactor();
  if (!Operand)
    return nullptr;
  return make_unique<UnaryExpressionNode>(tok_minus, std::move(Operand));
}

/// factor
///   = "-" factor
///   | primary ;
static unique_ptr<ExpressionNode> ParseFactor() {
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();
  return ParsePrimary();
}
```

`ParseUnaryMinus` calls `ParseFactor` for its own operand, not `ParsePrimary` — that's what lets it recurse into itself. `--5` works because the first `-` calls `ParseFactor`, which sees the second `-` and calls `ParseUnaryMinus` again before either call has produced a value.

```cppdiff
*/// term
-///   = primary { ("*" | "/") primary } ;
+///   = factor { ("*" | "/" | "%") factor } ;
*static unique_ptr<ExpressionNode> ParseTerm() {
-  // I start the term by parsing one primary.
-  auto Left = ParsePrimary();
+  // I start the term by parsing one factor.
+  auto Left = ParseFactor();
*  if (!Left)
*    return nullptr;
*
*  // I consume only the operators that belong to this tier.
-  while (CurrentToken == tok_star || CurrentToken == tok_slash) {
+  while (CurrentToken == tok_star || CurrentToken == tok_slash ||
+         CurrentToken == tok_percent) {
*    int Operator = CurrentToken;
-    getNextToken(); // I eat '*' or '/'.
-    auto Right = ParsePrimary();
+    getNextToken(); // I eat '*', '/', or '%'.
+    auto Right = ParseFactor();
*    if (!Right)
*      return nullptr;
*
*    // I fold each new operation into the tree on my left.
*    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
*                                             std::move(Right));
*  }
*
*  return Left;
*}
```

That single change — `ParsePrimary()` to `ParseFactor()`, twice, in `ParseTerm` — is what makes `-2 * 3` parse as `(-2) * 3` rather than `-(2 * 3)`. `ParseFactor` grabs the `-2` as a complete unit before `ParseTerm`'s `while` loop ever sees the `*`. `%` needed no equivalent change anywhere else — it just joins `*` and `/` in the same `while` condition, since it belongs at exactly their precedence.

There's no new error path for a bad operand after `-`: `ParseUnaryMinus` just propagates the `Unexpected ...` diagnostic produced by `ParsePrimary()`:

<!-- code-merge:start -->
```pyxc
ready> -)
```
```text
Error: Unexpected ')' (token: ')')
```
<!-- code-merge:end -->

## Try It

<!-- code-merge:start -->
```pyxc
ready> def wrap(x):
-x % 10
```
```text
Parsed a function definition.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> -5
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> 7 % 3
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> -2 * 3
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->
<!-- code-merge:start -->
```pyxc
ready> --5
```
```text
Parsed a top-level expression.
```
<!-- code-merge:end -->

`-2 * 3` is `(-2) * 3`, not `-(2 * 3)` written differently — unary minus binds tighter than `*`, same as it does in every C-family language. `--5` is double negation, not decrement — pyxc has no `--` token yet, so this is just `-` applied twice, and `ParseFactor` handles both `-` characters through the same recursive call. There's still no codegen at this stage, so every valid line just reports that it parsed; I don't see real arithmetic results until I connect the AST to LLVM IR.

## Build and Run

```bash
cd code/chapter-04
cmake -S . -B build && cmake --build build
./build/pyxc
```

I run the chapter tests with:

```bash
llvm-lit -v test/
```

## What's Next

[Chapter 5](chapter-05.md) adds real source locations and caret-style error messages.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`

I'll help you figure it out.

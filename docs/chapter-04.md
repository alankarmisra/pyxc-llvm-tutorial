---
section: "Foundations"
description: "Complete basic arithmetic with unary negation and remainder."
---

# 4. pyxc: Completing Basic Arithmetic

Next: make these expressions parse:

```pyxc
-5
x * -2
17 % 5
```

Chapter 3 recognizes binary `-`, but a leading `-` appears where the parser expects an operand. Add a new grammar layer for prefix operators, then add remainder beside multiplication and division.

Work in:

```bash
cd code/chapter-04
```

## 1. Replace the Term Grammar

Replace:

```ebnf
term = primary { ("*" | "/") primary } ;
```

with:

```ebnf
term   = factor { ("*" | "/" | "%") factor } ;
factor = "-" factor | primary ;
```

This puts unary minus between `primary` and multiplication:

```text
primary -> factor -> term -> sum
```

Calling `factor` recursively allows repeated negation:

```pyxc
---x
```

## 2. Add the Remainder Token

Add:

```cpp
tok_percent,
```

Add its readable token name:

```cpp
{tok_percent, "'%'"},
```

Then recognize the character in `getToken()`:

```cpp
case '%':
  return tok_percent;
```

No new token is needed for unary minus. The lexer already returns `tok_minus`; the parser decides whether its position is unary or binary.

## 3. Add a Unary AST Node

Add:

```cpp
class UnaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Operand;

public:
  UnaryExpressionNode(int Operator,
                      unique_ptr<ExpressionNode> Operand)
      : Operator(Operator), Operand(std::move(Operand)) {}
};
```

A binary node owns two operands. A unary node owns one.

For:

```pyxc
-x
```

the tree is:

```text
UnaryExpression '-'
└── NameExpression x
```

## 4. Parse Unary Minus

Forward-declare `ParseFactor()` because unary parsing calls back into it:

```cpp
static unique_ptr<ExpressionNode> ParseFactor();
```

Then add:

```cpp
static unique_ptr<ExpressionNode> ParseUnaryMinus() {
  getNextToken(); // eat '-'

  auto Operand = ParseFactor();
  if (!Operand)
    return nullptr;

  return make_unique<UnaryExpressionNode>(
      tok_minus, std::move(Operand));
}
```

Implement the grammar choice:

```cpp
static unique_ptr<ExpressionNode> ParseFactor() {
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();

  return ParsePrimary();
}
```

The recursive `ParseFactor()` call makes `--x` become `-(-x)`. Calling `ParsePrimary()` after the recursion stops gives unary minus tighter precedence than `*`, `/`, `+`, and `-`.

## 5. Make Terms Consume Factors

In `ParseTerm()`, replace both calls to `ParsePrimary()` with `ParseFactor()`.

Then extend the operator condition:

```cpp
while (CurrentToken == tok_star || CurrentToken == tok_slash ||
       CurrentToken == tok_percent) {
```

The complete shape is:

```cpp
static unique_ptr<ExpressionNode> ParseTerm() {
  auto Left = ParseFactor();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_star || CurrentToken == tok_slash ||
         CurrentToken == tok_percent) {
    int Operator = CurrentToken;
    getNextToken();

    auto Right = ParseFactor();
    if (!Right)
      return nullptr;

    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}
```

## 6. Check the Resulting Grouping

These expressions now group as:

```text
-2 * 3      -> (-2) * 3
x * -2      -> x * (-2)
-x % 4 + 1  -> ((-x) % 4) + 1
--x          -> -(-x)
```

Unary minus binds tighter because `ParseTerm()` asks `ParseFactor()` for each operand. Remainder shares the term tier with multiplication and division.

## 7. Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/pyxc
```

Try:

```pyxc
ready> -5
ready> 17 % 5
ready> x * -2
ready> --3
```

Expected after each valid input:

```text
Parsed a top-level expression.
```

The frontend still does not evaluate. These experiments verify that the new token positions produce the intended AST rather than an unexpected-token error.

Run all tests:

```bash
llvm-lit -v test/
```

What you built is one reusable prefix boundary:

```text
leading '-' -> unary node over another factor
infix '-'   -> binary node in the sum tier
```

Next: [Chapter 5](chapter-05.md) turns crude parser failures into source-located diagnostics with carets and recovery.

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

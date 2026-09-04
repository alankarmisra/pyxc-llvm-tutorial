---
section: "Foundations"
description: "Encode operator precedence in the grammar and parser."
---

# 3. pyxc: Encoding Precedence in the Grammar

Next: make `2 + 3 * 4` mean `14`, not `20`.

Chapter 2 has one useful expression loop, but a single loop cannot distinguish tighter and looser operators. Add one parser layer per precedence tier:

```text
primary -> term -> sum -> comparison -> expression
```

Work in:

```bash
cd code/chapter-03
```

## 1. Replace the Expression Grammar

Replace:

```ebnf
expression = sum ;
sum        = term { "+" term } ;
term       = primary ;
```

with:

```ebnf
expression = comparison ;
comparison = sum { "<" sum } ;
sum        = term { ("+" | "-") term } ;
term       = primary { ("*" | "/") primary } ;
```

Read the grammar from bottom to top:

```text
primary    -> one operand
term       -> multiplication/division group
sum        -> addition/subtraction group
comparison -> less-than group
```

The tighter rule is called first, so its tree is complete before the looser rule combines it.

## 2. Add the Operator Tokens

Chapter 2 already has `tok_plus`. Add:

```cpp
tok_minus,
tok_star,
tok_slash,
tok_less,
```

Add readable names:

```cpp
{tok_minus, "'-'"},
{tok_star, "'*'"},
{tok_slash, "'/'"},
{tok_less, "'<'"},
```

Then extend the single-character switch in `getToken()`:

```cpp
case '-':
  return tok_minus;
case '*':
  return tok_star;
case '/':
  return tok_slash;
case '<':
  return tok_less;
```

Build now:

```bash
cmake -S . -B build
cmake --build build
```

The lexer can now name the operators. Next, give each operator group its own parser.

## 3. Expand `ParseTerm()`

Replace the Chapter 2 pass-through implementation with:

```cpp
static unique_ptr<ExpressionNode> ParseTerm() {
  auto Left = ParsePrimary();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_star || CurrentToken == tok_slash) {
    int Operator = CurrentToken;
    getNextToken(); // eat '*' or '/'

    auto Right = ParsePrimary();
    if (!Right)
      return nullptr;

    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}
```

This function consumes only `*` and `/`. It stops as soon as it sees a token belonging to another tier.

## 4. Expand `ParseSum()`

Change the loop condition from only `tok_plus` to both sum operators, and preserve the actual operator:

```cpp
static unique_ptr<ExpressionNode> ParseSum() {
  auto Left = ParseTerm();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_plus || CurrentToken == tok_minus) {
    int Operator = CurrentToken;
    getNextToken(); // eat '+' or '-'

    auto Right = ParseTerm();
    if (!Right)
      return nullptr;

    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}
```

The important call is `ParseTerm()`. It finishes every multiplication or division group before `ParseSum()` constructs `+` or `-`.

## 5. Add `ParseComparison()`

Add the new loosest tier:

```cpp
static unique_ptr<ExpressionNode> ParseComparison() {
  auto Left = ParseSum();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_less) {
    int Operator = CurrentToken;
    getNextToken(); // eat '<'

    auto Right = ParseSum();
    if (!Right)
      return nullptr;

    Left = make_unique<BinaryExpressionNode>(
        Operator, std::move(Left), std::move(Right));
  }

  return Left;
}
```

Then replace:

```cpp
return ParseSum();
```

in `ParseExpression()` with:

```cpp
return ParseComparison();
```

Starting at the loosest tier allows one expression to contain every tighter tier beneath it.

## 6. Verify the Tree Shape

For:

```pyxc
k < a * b + c * d
```

the parser calls form this structure:

```text
comparison '<'
├── sum: k
└── sum '+'
    ├── term '*': a, b
    └── term '*': c, d
```

Or with parentheses made explicit:

```text
k < ((a * b) + (c * d))
```

Precedence comes from which function parses each operand—not from a table applied after parsing.

## 7. Keep Operators Left-Associative

Each tier repeatedly replaces `Left`:

```cpp
Left = make_unique<BinaryExpressionNode>(
    Operator, std::move(Left), std::move(Right));
```

Therefore:

```pyxc
8 / 2 / 2
```

becomes:

```text
((8 / 2) / 2)
```

not:

```text
(8 / (2 / 2))
```

The `while` loop provides repetition and left associativity at the same time.

## 8. Build and Run

```bash
cmake --build build
./build/pyxc
```

Try:

```pyxc
ready> 1 + 2 * 3
ready> 8 / 2 + 1
ready> 1 < 2 + 3 * 4
```

Expected after each line:

```text
Parsed a top-level expression.
```

This chapter still parses but does not evaluate. The important result is that each input produces an AST with the intended grouping.

Try the current limitation:

```pyxc
ready> -5
```

The lexer recognizes `-`, but no primary begins with it, so parsing fails. Binary subtraction and unary negation are different grammar positions.

Run the suite:

```bash
llvm-lit -v test/
```

What you built is the precedence boundary:

```text
one operator tier -> one parser loop
tighter tier      -> parsed before looser tier
same tier         -> folded left
```

Next: [Chapter 4](chapter-04.md) adds unary minus and remainder.

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

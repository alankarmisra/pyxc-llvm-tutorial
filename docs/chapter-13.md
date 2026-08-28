---
description: "Add Python-style elif chains so multi-way conditionals don't nest into a pyramid of else blocks."
---
# 13. pyxc: `elif` Chains

## What I Am Building

[Chapter 12](chapter-12.md) gave pyxc `if` and `else`, and nothing else — which is fine right up until a function needs to choose between three things instead of two, at which point I'm reduced to stacking `else`/`if` pairs like Russian nesting dolls, each one a little further from the left margin than the last:

```pyxc
def sign(x):
    if x > 0:
        return 1.0
    else:
        if x == 0:
            return 0.0
        else:
            return -1.0
```

After this chapter, I can write the same logic flat:

<!-- code-merge:start -->
```pyxc
ready> def sign(x):
    if x > 0:
        return 1.0
    elif x == 0:
        return 0.0
    else:
        return -1.0
```
```text
Parsed a function definition.
```
<!-- code-merge:end -->

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-13
```

## Grammar

I add one alternative to `if-statement`: zero or more `elif` clauses between the `if` clause and the optional `else`:

`code/chapter-13/pyxc.ebnf`

```grammardiff
*...
*parameter                         = name ;
*if-statement                      = "if" expression ":" suite
+                                    { [ end-of-lines ] "elif" expression ":" suite }
*                                    [ [ end-of-lines ] "else" ":" suite ] ;
*for-statement                     = "for" [ "var" ] name "=" expression ","
*...
```

## New Token and Keyword

One new token:

```cppdiff
*enum Token {
*  ...
*  // control
*  tok_if = -12,
*  tok_else = -13,
*  tok_return = -14,
+  tok_elif = -20,
*
*  // loops
*  ...
*};
```

Added to the keyword table, right alongside `if` and `else`:

```cppdiff
*static map<string, Token> Keywords = {
-    {"def", tok_def},       {"extern", tok_extern}, {"return", tok_return},
-    {"if", tok_if},         {"else", tok_else},     {"for", tok_for},
-    {"var", tok_var}};
+    {"def", tok_def},       {"extern", tok_extern}, {"return", tok_return},
+    {"if", tok_if},         {"elif", tok_elif},     {"else", tok_else},
+    {"for", tok_for},
+    {"var", tok_var}};
```

And to the token-name map used in error messages:

```cppdiff
*static map<int, string> TokenNames = [] {
*  // Unprintable character tokens, and multi-character tokens.
*  static map<int, string> Names = {
*      ...
-      {tok_if, "'if'"},          {tok_else, "'else'"},
-      {tok_for, "'for'"},        {tok_var, "'var'"},
+      {tok_if, "'if'"},          {tok_elif, "'elif'"},
+      {tok_else, "'else'"},
+      {tok_for, "'for'"},        {tok_var, "'var'"},
*      {tok_indent, "indent"},    {tok_dedent, "dedent"},
*      {tok_block_end, "block-end"}};
*  ...
*};
```

## Refactoring If/Elif Parsing to Collect Branches

Before this chapter, `ParseIfStatement` parsed exactly one condition and one body. Now I collect an arbitrary number of `(condition, body)` pairs in a loop before I even know whether an `else` follows:

```cpp
static unique_ptr<ExpressionNode> ParseIfStatement() {
  getNextToken(); // eat 'if'
  vector<pair<unique_ptr<ExpressionNode>, unique_ptr<ExpressionNode>>> Branches;
  bool LastBranchWasBlock = false;
  bool LastBranchHadTrailingEol = false;

  while (true) {
    auto Condition = ParseExpression();
    if (!Condition)
      return nullptr;

    if (CurrentToken != tok_colon)
      return LogErrorExpression("Expected ':' after if/elif condition");
    getNextToken(); // eat ':'

    auto Body = ParseSuite();
    if (!Body)
      return nullptr;

    LastBranchWasBlock = (CurrentToken == tok_block_end);
    if (LastBranchWasBlock)
      getNextToken();
    LastBranchHadTrailingEol = (CurrentToken == tok_eol);

    Branches.push_back({std::move(Condition), std::move(Body)});
    consumeNewlines();

    if (CurrentToken != tok_elif)
      break;
    getNextToken(); // eat 'elif'
  }
  // ...
}
```

Each pass through the loop parses one `if` or `elif` branch — I don't distinguish between them; the first iteration happens to follow `if`, and every iteration after that follows `elif`. After each body, I call `consumeNewlines()` and check whether `elif` comes next. If it does, I eat it and loop again. Anything else — `else`, a dedent, end of file — and I break out.

`LastBranchWasBlock` and `LastBranchHadTrailingEol` are the same bookkeeping [Chapter 12](chapter-12.md) already needed for a bare `if`/`else`, just tracked per-branch now instead of once.

**Missing colon after an `elif` condition:**

<!-- code-merge:start -->
```pyxc
ready> def bad(x):
    if x > 0:
        return 1
    elif x == 0
        return 0
    else:
        return -1
```
```text
Error (Line 4, Column 16): Expected ':' after if/elif condition
    elif x == 0
               ^~~~
Error (Line 5, Column 9): Unexpected indentation
        r
        ^~~~
Error (Line 6, Column 5): unknown token when expecting an expression
    else:
    ^~~~
Error (Line 7, Column 9): Unexpected indentation
        r
        ^~~~
```
<!-- code-merge:end -->

The first line is the real error — the parser bails out of `ParseIfStatement` the moment the colon check fails. Everything after that is the parser trying to recover from a token stream that no longer makes sense, the same cascading-error behavior [Chapter 12](chapter-12.md) already showed for bad indentation.

## Lowering to a Nested If Tree

I don't introduce a new AST node for `elif`. Once the loop above exits, I check for a trailing `else` — this part is unchanged from Chapter 12, just renamed from `Then`/`ThenWasBlock` to `LastBranchWasBlock` since there can now be more than one branch before it:

```cpp
unique_ptr<ExpressionNode> Else;
if (CurrentToken == tok_else) {
  getNextToken(); // eat 'else'
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after 'else'");
  getNextToken(); // eat ':'
  Else = ParseSuite();
  if (!Else)
    return nullptr;
} else if (LastBranchWasBlock) {
  PendingTokens.push_front(CurrentToken);
  CurrentToken = tok_block_end;
} else if (LastBranchHadTrailingEol) {
  PendingTokens.push_front(CurrentToken);
  CurrentToken = tok_eol;
}
```

Then I lower the whole chain to a right-nested `IfStatementNode` tree: the (possibly null) `else` body becomes the initial innermost node, and I walk `Branches` in reverse, wrapping one more `IfStatementNode` around it per branch:

```cpp
// I lower the chain to nested IfStatementNodes in the else branch.
unique_ptr<ExpressionNode> Tree = std::move(Else);
for (auto It = Branches.rbegin(); It != Branches.rend(); ++It) {
  Tree = make_unique<IfStatementNode>(std::move(It->first),
                                      std::move(It->second), std::move(Tree));
}
return Tree;
```

Given:

```pyxc
if a:    body_a
elif b:  body_b
elif c:  body_c
else:    body_d
```

I build:

```ast
IfStatementNode(a, body_a,
  IfStatementNode(b, body_b,
    IfStatementNode(c, body_c,
      body_d)))
```

If there's no `else` at all, `Else` starts as `nullptr`, so the innermost `IfStatementNode`'s `Else` is null too — exactly what a bare `if` without `else` already produces. `IfStatementNode::codegen()` doesn't change at all; it has no idea whether it came from a literal `if`/`else` or from one link in an `elif` chain.

My codegen sees exactly what it would see for hand-written nested `if`/`else` blocks, so conditions are evaluated top to bottom, one at a time, same as nested `if`/`else` would be. `elif` buys me flatter source, not a different runtime shape — `switch`, which dispatches on a value directly instead of testing a chain of conditions, comes in [Chapter 23](chapter-23.md).

## Build and Run

```bash
cd code/chapter-13
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

<!-- code-merge:start -->
```pyxc
ready> def sign(x):
    if x > 0:
        return 1.0
    elif x == 0:
        return 0.0
    else:
        return -1.0
```
```text
Parsed a function definition.
```
```pyxc
ready> sign(5)
```
```text
Parsed a top-level expression.
Evaluated to 1.000000
```
```pyxc
ready> sign(0)
```
```text
Parsed a top-level expression.
Evaluated to 0.000000
```
```pyxc
ready> sign(-2)
```
```text
Parsed a top-level expression.
Evaluated to -1.000000
```
<!-- code-merge:end -->

More than one `elif` — the first matching branch wins, and later ones are never even evaluated:

<!-- code-merge:start -->
```pyxc
ready> def mapv(x):
    if x == 1:
        return 10.0
    elif x == 2:
        return 20.0
    elif x == 3:
        return 30.0
    elif x == 4:
        return 40.0
    else:
        return 99.0
```
```text
Parsed a function definition.
```
```pyxc
ready> mapv(3)
```
```text
Parsed a top-level expression.
Evaluated to 30.000000
```
```pyxc
ready> mapv(4)
```
```text
Parsed a top-level expression.
Evaluated to 40.000000
```
```pyxc
ready> mapv(7)
```
```text
Parsed a top-level expression.
Evaluated to 99.000000
```
<!-- code-merge:end -->

An `elif` chain with no `else` at all — same fall-through behavior a bare `if` has always had:

<!-- code-merge:start -->
```pyxc
ready> def classify(x):
    var result = 0
    if x == 1:
        result = 1
    elif x == 2:
        result = 2
    return result
```
```text
Parsed a function definition.
```
```pyxc
ready> classify(1)
```
```text
Parsed a top-level expression.
Evaluated to 1.000000
```
```pyxc
ready> classify(2)
```
```text
Parsed a top-level expression.
Evaluated to 2.000000
```
```pyxc
ready> classify(99)
```
```text
Parsed a top-level expression.
Evaluated to 0.000000
```
<!-- code-merge:end -->

`classify(99)` matches neither `x == 1` nor `x == 2`, so the `if` falls through without touching `result` — it stays `0`.

## What's Next

[Chapter 14](chapter-14.md) completes looping: `while`, `do`/`while`, `break`, and `continue`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

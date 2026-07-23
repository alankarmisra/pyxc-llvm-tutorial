---
description: "Add Python-style elif chains so multi-way conditionals don't nest into a pyramid of else blocks."
---
# 37. pyxc: `elif` Chains

## Where We Are

[Chapter 36](chapter-36.md) added `switch`. Multi-way conditionals on non-integer values — booleans, comparisons, method results — are still written as nested `if`/`else` blocks, which stack up fast:

```pyxc
def classify(x: int) -> int:
  if x < 0:
    return -1
  else:
    if x == 0:
      return 0
    else:
      return 1
```

After this chapter, the same logic reads cleanly:

```pyxc
def classify(x: int) -> int:
  if x < 0:
    return -1
  elif x == 0:
    return 0
  else:
    return 1
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-37
```

## New Token and Keyword

One new token:

```cpp
tok_elif = -63,
```

Added to the keyword table:

```cpp
{"elif", tok_elif},
```

And to the token name map for error messages:

```cpp
{tok_elif, "'elif'"},
```

## `ParseIfStmt` Refactored to Collect Branches

Previously `ParseIfStmt` parsed a single condition and body. Now it collects an arbitrary number of `(condition, body)` pairs in a loop:

```cpp
vector<pair<unique_ptr<ExprAST>, unique_ptr<ExprAST>>> Branches;
bool LastBranchWasBlock = false;

// eat 'if'
getNextToken();

while (true) {
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogError("If condition must be bool");

  if (CurTok != ':')
    return LogError("Expected ':' after if/elif condition");
  getNextToken(); // eat ':'

  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  LastBranchWasBlock = (CurTok == tok_block_end);
  if (LastBranchWasBlock)
    getNextToken();
  Branches.push_back({std::move(Cond), std::move(Body)});

  consumeNewlines();
  if (CurTok != tok_elif)
    break;
  getNextToken(); // eat 'elif'
}
```

After each body, `consumeNewlines()` skips the line ending. If the next token is `tok_elif`, the loop continues — eating `elif` and parsing another condition and body. Any other token (including `tok_else` or anything else) exits the loop.

## Lowering to a Nested `IfStmtAST` Tree

No new AST node is introduced. The `elif` chain is lowered directly to a right-nested `IfStmtAST` tree during parsing. The optional `else` body becomes the initial innermost node, and the `Branches` vector is walked in reverse:

```cpp
// Lower if/elif chain to nested IfStmtAST in else branch.
unique_ptr<ExprAST> Tree = std::move(Else);
for (auto It = Branches.rbegin(); It != Branches.rend(); ++It) {
  Tree = make_unique<IfStmtAST>(std::move(It->first), std::move(It->second),
                                std::move(Tree));
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

The parser builds:

```
IfStmtAST(a, body_a,
  IfStmtAST(b, body_b,
    IfStmtAST(c, body_c,
      body_d)))
```

Codegen sees exactly what it would see for hand-written nested `if`/`else` blocks. The IR is identical.

## Grammar

```ebnf
ifstmt = "if" expression ":" suite
       { eols "elif" expression ":" suite }   -- new
       [ eols "else" ":" suite ] ;            -- changed
```

## Error Cases

**Missing colon after `elif` condition:**
```pyxc
if x > 0:
  return 1
elif x == 0
  return 0   # Error: Expected ':' after if/elif condition
```

**Non-bool `elif` condition:**
```pyxc
if x > 0:
  return 1
elif x + 1:   # Error: If condition must be bool
  return 0
```

## Things Worth Knowing

**`elif` without `else` is fine.** If no branch matches and there is no `else`, execution continues after the chain. The same is true for a plain `if` without `else`.

**`elif` uses the same condition type rule as `if`.** Every condition must be `bool`. There is no implicit integer-to-bool coercion.

**Use `switch` for integer dispatch on constants; use `elif` for everything else.** `switch` is limited to compile-time integer literals and allows LLVM to emit a real branch table. `elif` works on any `bool` expression but generates a linear chain of comparisons.

## What's Next

[Chapter 38](chapter-38.md) adds character literals: `'a'`, `'\n'`, `'\t'`, and friends.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

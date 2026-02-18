---
description: "Implement Python-style match/case to express multi-way branching clearly with indentation-based suites and readable pattern-driven flow."
---
# 32. Pyxc: Python-Style match/case

Chapter 29 adds `match/case` in a Python-style form.

The goal is simple: make multi-way branching readable without introducing C syntax. This is our language, so the control flow should stay consistent with indentation-based blocks and explicit suites.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter29](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter29).

## Feature summary

New statement form:

```py
match expr:
    case 1:
        ...
    case 2:
        ...
    case _:
        ...
```

Supported in this chapter:

- integer `match` expression
- integer `case` expressions
- optional wildcard default with `case _`
- first matching case runs
- no fallthrough
- single-quoted character literals as integer values (`' '`, `'\n'`, `'\t'`, etc.)

Not included in this chapter:

- pattern destructuring
- `case ... if ...` guards
- string and struct pattern forms

## Grammar design

The grammar update in `code/chapter29/pyxc.ebnf` introduces two non-terminals:

```ebnf
statement       = if_stmt
                | match_stmt
                | for_stmt
                | ... ;

match_stmt      = "match" , expression , ":" , newline , indent ,
                  case_clause , { newline , case_clause } , [ newline ] ,
                  dedent ;
case_clause     = "case" , ( "_" | expression ) , ":" , suite ;
```

Design notes:

- `match_stmt` is a statement, not an expression. This keeps it aligned with `if`, `for`, and `while`.
- We require at least one `case_clause`.
- We treat `_` as a dedicated wildcard token at parse time for default handling.

## Lexer changes

In `code/chapter29/pyxc.cpp`, the lexer/token layer adds:

- `tok_match`
- `tok_case`
- keyword mapping for `"match"` and `"case"`
- token display names in `TokenName(...)`

Code sample:

```cpp
tok_match = -38,
tok_case = -39,
```

```cpp
{"match", tok_match}, {"case", tok_case}
```

Why this matters:

- parser logic can branch directly on dedicated token kinds
- debug token dumps remain readable when diagnosing parser errors

## Parser changes

We add `ParseMatchStmt()` and hook it into `ParseStmt()`.

Code sample:

```cpp
static std::unique_ptr<StmtAST> ParseMatchStmt();
```

```cpp
case tok_match:
  return ParseMatchStmt();
```

`ParseMatchStmt()` flow:

1. consume `match`
2. parse the match expression
3. require `:`
4. require newline + indented block
5. parse one or more `case` clauses
6. allow at most one wildcard default (`case _`)
7. require final dedent

Code sample for duplicate default rejection:

```cpp
if (!CaseExpr) {
  if (DefaultCase)
    return LogError<StmtPtr>("Duplicate default case '_' in match");
  DefaultCase = std::move(CaseBody);
}
```

This parser shape keeps the behavior explicit and prevents ambiguous or accidental defaults.

## AST model

A new statement node stores:

- `MatchExpr`
- ordered case list: `(case-expression, case-suite)` pairs
- optional `DefaultCase`

Code sample:

```cpp
class MatchStmtAST : public StmtAST {
  std::unique_ptr<ExprAST> MatchExpr;
  std::vector<std::pair<std::unique_ptr<ExprAST>, std::unique_ptr<BlockSuiteAST>>> Cases;
  std::unique_ptr<BlockSuiteAST> DefaultCase;
  ...
};
```

Important detail:

- case bodies are ordinary suites (`BlockSuiteAST`), so we reuse existing statement codegen and block semantics instead of inventing a special body representation.

## Code generation strategy

`MatchStmtAST::codegen()` lowers to a chain of integer comparisons and branches.

The generated control-flow shape is:

1. evaluate `MatchExpr` once
2. for each case:
   - evaluate case expression
   - compare with `icmp eq`
   - conditional branch to case body or next check
3. if none match:
   - jump to default block if present
   - otherwise jump to end block
4. merge at `match.end`

Code sample:

```cpp
Value *CmpV = Builder->CreateICmpEQ(MatchV, CaseV, "match.cmp");
Builder->CreateCondBr(CmpV, CaseBB, NextCheckBB);
```

Type rules enforced in codegen:

- `match` expression must be integer-typed
- each `case` expression must be integer-typed

Code sample:

```cpp
if (!MatchV->getType()->isIntegerTy())
  return LogError<Value *>("match expression must be an integer type");
```

```cpp
if (!CaseV->getType()->isIntegerTy())
  return LogError<Value *>("match case expression must be an integer type");
```

Why enforce this now:

- first implementation stays deterministic and small
- avoids introducing mixed-mode comparison semantics in this chapter
- keeps future extension path open for strings/struct patterns

## What you can write now

Basic branch selection:

```py
x: i32 = 2
match x:
    case 1:
        printf("one\n")
    case 2:
        printf("two\n")
    case _:
        printf("other\n")
```

Nested control flow inside a case:

```py
total: i32 = 0
match x:
    case 3:
        for i in range(1, 4, 1):
            total = total + i
    case _:
        total = -1
printf("%d\n", total)
```

No default case:

```py
match x:
    case 10:
        printf("ten\n")
    case 20:
        printf("twenty\n")
printf("done\n")
```

If there is no match and no default, execution continues after the `match` block.

Character literal comparisons (C-style ergonomics, Python syntax context):

```py
def is_space(c: i32) -> i32:
    if c == ' ' or c == '\t' or c == '\n':
        return 1
    return 0
```

Character literals are lowered as integer literals during lexing, so they work directly in integer expressions and comparisons.

## Tests added

New tests in `code/chapter29/test`:

- `c27_match_basic_default.pyxc`
- `c27_match_no_default.pyxc`
- `c27_match_nested_suite.pyxc`
- `c27_match_error_non_integer_match.pyxc`
- `c27_match_error_non_integer_case.pyxc`
- `c27_match_error_duplicate_default.pyxc`
- `c27_char_literal_basic.pyxc`
- `c27_char_literal_escapes.pyxc`
- `c27_char_literal_error_multi_char.pyxc`

These cover:

- normal execution with and without default
- nested suite codegen
- semantic errors for non-integer match/case values
- parser-level duplicate-default validation

## Compile / Run / Test

Compile:

```bash
cd code/chapter29 && ./build.sh
```

Run one sample:

```bash
code/chapter29/pyxc -i code/chapter29/test/c27_match_basic_default.pyxc
```

Run all chapter tests:

```bash
lit -sv code/chapter29/test
```

Validation for this implementation pass:

- 123 tests discovered
- 123 passed

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter29 && ./build.sh
```

Run one sample program:

```bash
code/chapter29/pyxc -i code/chapter29/test/c25_mod_add.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter29/test
lit -sv .
```

Poke around the tests and tweak a few cases to see what breaks first.

When you're done, clean artifacts:

```bash
cd code/chapter29 && ./build.sh
```


## Need Help?

Stuck on something? Have questions about this chapter? Found an error?

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report bugs, errors, or problems
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- The chapter you're working on
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message or unexpected behavior
- What you've already tried

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!

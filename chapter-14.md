# 14. Blocks, `elif`, Optional `else`, and Benchmarking

This chapter extends Chapter 13’s indentation model into full statement suites and richer control flow.  
The focus is:

- block-shaped statements (`suite`)
- `if / elif / else` chains
- optional `else`
- parser/codegen cleanup needed to make these reliable
- baseline benchmarking against Python

Below, changes are grouped by semantics (not just raw diff).

## What We Are Adding (Short)

- Statement-level AST (`StmtAST`) and block suites (`BlockSuiteAST`)
- `return` as a real statement node (`ReturnStmtAST`)
- `elif` token + parser support
- Optional `else` in `if`
- Safer `if` codegen for terminated branches
- Runtime mode fixes for interpreter vs executable `main` behavior

---

## Target Grammar (EBNF)

This is the syntax shape we want.  
We write it in EBNF so parser decisions are explicit and consistent.

```ebnf
program         = { top_level , newline } ;
top_level       = definition | extern | expression ;

definition      = { decorator } , "def" , prototype , ":" , suite ;
decorator       = "@" , identifier , [ "(" , [ "precedence" , "=" , number ] , ")" ] ;
extern          = "extern" , "def" , prototype ;

suite           = inline_suite | block_suite ;
inline_suite    = statement ;
block_suite     = newline , indent , statement_list , dedent ;
statement_list  = statement , { newline , statement } , [ newline ] ;

statement       = if_stmt | for_stmt | return_stmt | expr_stmt ;
if_stmt         = "if" , expression , ":" , suite ,
                  { "elif" , expression , ":" , suite } ,
                  [ "else" , ":" , suite ] ;
for_stmt        = "for" , identifier , "in" , "range" , "(" ,
                  expression , "," , expression , [ "," , expression ] , ")" ,
                  ":" , suite ;
return_stmt     = "return" , expression ;
expr_stmt       = expression ;
```

---

## Chapter 13 -> 14 by Semantic Change

Reference:
- `code/chapter13/pyxc.cpp`
- `code/chapter14/pyxc.cpp`

High-level diff:
- `451 insertions`, `266 deletions`

### A) Block semantics (statement suites)

**What changed**
- Added statement hierarchy:
  - `StmtAST`
  - `ExprStmtAST`
  - `ReturnStmtAST`
  - `BlockSuiteAST`
- Added parser entry points:
  - `ParseStmt`
  - `ParseStatementList`
  - `ParseSuite`
  - `ParseBlockSuite`

**Where**
- `code/chapter14/pyxc.cpp` around AST declarations and parser functions (`ParseSuite`, `ParseBlockSuite`, `ParseStmt`).

**Why**
- In Chapter 13, indentation tokens existed, but we still needed statement-aware parsing to use those tokens as real block structure.
- This change makes `if`, `for`, and `def` bodies parse as suites instead of expression-only fragments.

### B) `elif` + optional `else`

**What changed**
- Added token: `tok_elif`
- Added keyword mapping for `"elif"`
- Updated `TokenName()` for debug printing
- Reworked `ParseIfStmt()` to support:
  - `if ...`
  - `if ... elif ...`
  - `if ... elif ... else ...`
  - `if ...` with no else
- Added explicit errors for stray `elif` / `else` outside a valid `if` chain.

**Where**
- Token/keyword sections in `code/chapter14/pyxc.cpp`
- `ParseIfStmt` and `ParseStmt` in parser section

**Why**
- This gives Python-like branch chains without forcing nested `else: if`.
- Optional `else` removes earlier parser workaround patterns.

### C) Codegen stability for branch-heavy code

**What changed**
- `IfStmtAST::codegen()` now handles terminated branches correctly:
  - avoids invalid extra branches after `ret`
  - handles both-branches-terminated cases safely
- `ReturnStmtAST::codegen()` now normalizes return type (e.g., double/i32 conversion when needed)
- `FunctionAST::codegen()` avoids emitting duplicate terminal `ret` when already terminated

**Where**
- Codegen section in `code/chapter14/pyxc.cpp` (`IfStmtAST::codegen`, `ReturnStmtAST::codegen`, `FunctionAST::codegen`)

**Why**
- Prevent malformed IR and runtime instability on complex control flow (especially nested conditionals and early returns).

### D) Runtime mode behavior (interpreter vs executable)

**What changed**
- Added `UseCMainSignature` mode switch
- Interpreter mode keeps language-level functions returning `double`
- Executable/object mode can still emit native-style `int main` behavior

**Where**
- `PrototypeAST::codegen`, `FunctionAST::codegen`
- mode setup in `InterpretFile`, `CompileToObjectFile`, `REPL`

**Why**
- Removes mismatches between interpreter/JIT call expectations and native executable entrypoint requirements.

### E) Tests and examples

**What changed**
- Added/updated tests:
  - blocks + nested control flow
  - bad indentation
  - `if / elif / optional else`
  - showcase program
  - mandelbrot reference output alignment

**Where**
- `code/chapter14/test/`

**Why**
- Prevent regressions while parser/codegen semantics are expanding.

---

## Benchmarking (Bonus)

Repo:
- [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial)

Benchmark runner:
- [`code/chapter14/bench/run_suite.sh`](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter14/bench/run_suite.sh)

Current 3-run averages (seconds, rounded to 2 decimals):
- `fib(41)`: Python `11.66`, `pyxc -i` `0.46`, `pyxc exe` `0.44`
- `loopsum(10000,10000)`: Python `3.39`, `pyxc -i` `0.15`, `pyxc exe` `0.10`
- `primecount(1900) x 10`: Python `1.22`, `pyxc -i` `0.17`, `pyxc exe` `0.15`

Overall average across cases:
- Python: `5.42s`
- `pyxc -i`: `0.26s`
- `pyxc executable`: `0.23s`

Run locally:

```bash
cd code/chapter14
bench/run_suite.sh 3
```

If setup fails on your machine, please open an issue (or message me) with your OS, LLVM toolchain details, and full command output.

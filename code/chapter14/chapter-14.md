# Chapter 14 - Blocks, `elif`, Optional `else`, and Benchmarks

## 1. What Changed (Terse Summary)

- Added statement blocks (`suite`) with indentation-aware parsing.
- Added `return` as a first-class statement inside block suites.
- Added `elif`.
- Made `else` optional.
- Made `elif` optional.
- Updated `if` codegen to correctly handle terminated branches (`return` paths).
- Tightened function return codegen to avoid duplicate/invalid terminators.
- Added file interpreter/object/executable/token flows and expanded test coverage.
- Added benchmark suite (Python vs `pyxc -i` vs compiled `pyxc` executable).

## 2. EBNF (Target Shape)

This is the target syntax shape we want. Writing it as EBNF helps us decide parser structure and parse order.

```ebnf
program         = { top_level , newline } ;
top_level       = definition | extern | expression ;

definition      = { decorator } , "def" , prototype , ":" , suite ;
decorator       = "@" , identifier , [ "(" , [ "precedence" , "=" , number ] , ")" ] ;
extern          = "extern" , "def" , prototype ;
prototype       = ( identifier | operator_name ) ,
                  "(" , [ identifier , { "," , identifier } ] , ")" ;

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

expression      = unary , { binop , unary } ;
unary           = primary | unary_op , unary ;
primary         = number | identifier | call_expr | "(" , expression , ")" | var_expr ;
call_expr       = identifier , "(" , [ expression , { "," , expression } ] , ")" ;
var_expr        = "var" , identifier , [ "=" , expression ] ,
                  { "," , identifier , [ "=" , expression ] } ,
                  "in" , expression ;
```

## 3. Chapter 13 -> Chapter 14 Code Comparison (What + Why)

Reference files:
- `code/chapter13/pyxc.cpp`
- `code/chapter14/pyxc.cpp`

Diff headline:
- `451 insertions`, `266 deletions` (`git diff --no-index --stat code/chapter13/pyxc.cpp code/chapter14/pyxc.cpp`)

Key additions and intent:

- Lexer/tokens:
  - Added `tok_elif`.
  - Reflowed token ids into grouped contiguous negatives.
  - Why: cleaner token model and direct support for `elif`.

- AST split into statements vs expressions:
  - Added `StmtAST`, `ExprStmtAST`, `ReturnStmtAST`, `BlockSuiteAST`.
  - `IfStmtAST` and `ForStmtAST` now operate as statement nodes with suites.
  - Why: blocks and control flow are statement-level concerns.

- Parser upgrades:
  - Added `ParseSuite`, `ParseBlockSuite`, statement-list parsing.
  - `ParseIfStmt` now supports `if` + `{elif}` + optional `else`.
  - Added explicit errors for stray `elif`/`else`.
  - Why: model indentation-driven block structure and optional branches directly.

- Codegen fixes:
  - `if` codegen now handles terminated branches safely.
  - Return codegen performs return-type conversion where required.
  - Function codegen now avoids adding duplicate terminal `ret`.
  - Why: prevent malformed IR and runtime crashes/hangs.

- Driver/mode updates:
  - Better split for interpret/object/executable/token flows.
  - `UseCMainSignature` toggles native `main` behavior by mode.
  - Why: keep interpreter semantics stable while preserving executable generation needs.

- Tests/benchmarks:
  - Added lit tests for nested blocks, invalid indentation, `elif`/optional `else`, showcase, mandel.
  - Added benchmark suite under `code/chapter14/bench/`.
  - Why: verify language behavior and measure runtime characteristics.

## 4. Bonus: Benchmarks + Links

Repository:
- [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial)

Benchmark runner:
- [`code/chapter14/bench/run_suite.sh`](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter14/bench/run_suite.sh)

Latest 3-run averages (seconds):
- `fib(41)`: Python `11.66`, `pyxc -i` `0.46`, `pyxc exe` `0.44`
- `loopsum(10000,10000)`: Python `3.39`, `pyxc -i` `0.15`, `pyxc exe` `0.10`
- `primecount(1900) x 10`: Python `1.22`, `pyxc -i` `0.17`, `pyxc exe` `0.15`

Overall average across cases:
- Python: `5.42s`
- `pyxc -i`: `0.26s`
- `pyxc executable`: `0.23s`

Please run these on your own machine and compare with your environment:

```bash
cd code/chapter14
bench/run_suite.sh 3
```

If anything does not run as expected, open an issue or message me and include your OS, compiler/LLVM setup, and command output.

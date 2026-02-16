# Chapter 18 Design Requirements

## Theme
Control-flow parity with C-style loops plus practical core integer operator completeness.

## Goal
Extend Pyxc with:

- `while`, `do-while`, `break`, `continue`
- integer operators: unary `~`, binary `%`, `&`, `^`, `|`

while preserving Chapter 16/17 behavior.

## Scope

### In Scope
- `while <expr>:` with inline and block suites
- `do:` `<suite>` `while <expr>` (post-test loop)
- `break` statement (nearest enclosing loop)
- `continue` statement (nearest enclosing loop)
- Correct nesting behavior with `if` inside loops and loops inside loops
- Integer operator support:
  - unary `~`
  - binary `%`, `&`, `^`, `|`
- Integer-only diagnostics for `%` and bitwise operators

### Out of Scope
- Labels and `goto`
- `switch`
- `for (;;)` C-form rewrite (existing `for range` remains as-is)
- Shift operators and compound assignment (deferred)

## Syntax Requirements

### While
```py
while cond:
    body
```

### Do-while
```py
do:
    body
while cond
```

### Break/Continue
```py
while cond:
    if x:
        break
    continue
```

### Operators
```py
~x
x % y
x & y
x ^ y
x | y
```

## Lexer Requirements
- Add keywords/tokens:
  - `tok_while`
  - `tok_do`
  - `tok_break`
  - `tok_continue`
- Existing single-character operator tokens are used for `%`, `&`, `^`, `|`, `~`.

## Parser Requirements
- Add statement parsers:
  - `ParseWhileStmt()`
  - `ParseDoWhileStmt()`
  - `ParseBreakStmt()`
  - `ParseContinueStmt()`
- Extend `ParseStmt()` dispatch accordingly.
- `break`/`continue` must be parse-valid anywhere syntactically but become semantic errors outside loops.
- Extend binary precedence table to include `%`, `&`, `^`, and `|` with stable precedence.

## AST Requirements
- Add nodes:
  - `WhileStmtAST`
  - `DoWhileStmtAST`
  - `BreakStmtAST`
  - `ContinueStmtAST`
- Keep existing `StmtAST` hierarchy style and location tracking.

## Semantic Requirements
- Maintain a loop-context stack in codegen containing:
  - break target block
  - continue target block
- Emit diagnostic when `break`/`continue` are used outside loops.
- Condition truthiness must use current Chapter 16 boolean conversion rules (`ToBoolI1`).
- `%`, `&`, `^`, `|`, `~` are integer-only in this chapter.

## LLVM Lowering Requirements

### while
Blocks:
1. condition block
2. body block
3. exit block

Flow:
- branch to condition
- condition true -> body
- condition false -> exit
- body tail -> condition

### do-while
Blocks:
1. body block
2. condition block
3. exit block

Flow:
- enter body first
- body tail -> condition
- condition true -> body
- condition false -> exit

### break
- branch to nearest loop exit block

### continue
- branch to nearest loop continue target:
  - `while`: condition block
  - `do-while`: condition block
  - existing `for range`: step/condition path per current for lowering

### Operators
- `%` lowers to integer remainder (`srem` in current signed arithmetic path)
- `&`, `^`, `|` lower to LLVM `and`, `xor`, `or`
- unary `~` lowers to LLVM integer bitwise not

## Interaction Requirements
- Works with typed locals and pointer expressions from Chapter 16.
- Existing `for range` behavior must continue to pass tests.
- `return` inside loop still behaves as terminator.

## Diagnostics Requirements
- `break` outside loop: clear error
- `continue` outside loop: clear error
- malformed do-while syntax: clear parse error
- `%` with non-integer operands: clear error
- bitwise op with non-integer operands: clear error
- unary `~` with non-integer operand: clear error

## Tests

### Positive
- simple while counting
- do-while runs at least once
- nested loops with inner break and outer continue
- continue skips remainder of body
- `%` with signed integer values
- `&`, `|`, `^` basic correctness and precedence
- unary `~` on integer values

### Negative
- break outside loop
- continue outside loop
- malformed do-while syntax
- `%` with float operand
- bitwise op with float operand
- unary `~` with float operand

## Done Criteria
- Chapter 18 lit suite added/updated and green
- Previous chapter lit suite remains green
- No regressions in function/type/pointer behavior

## Implementation Sequencing Notes
- After Chapter 17 completes, copy compiler/runtime baseline to `code/chapter18/`.
- Implement feature in small increments:
  1. while
  2. break/continue with loop context
  3. do-while
  4. operator `%`, `&`, `^`, `|`, `~`
  5. diagnostics + test hardening

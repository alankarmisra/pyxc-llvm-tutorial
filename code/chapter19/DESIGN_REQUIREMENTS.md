# Chapter 19 Design Requirements

## Theme
C-like integer semantics and core operator completeness.

## Goal
Close the semantic gap to K&R-style integer-heavy code by adding missing integer operators and C-like conversion/promotion rules.

## Scope

### In Scope
- Integer arithmetic/operator support:
  - `%`
  - bitwise: `&`, `|`, `^`, `~`
  - shifts: `<<`, `>>`
- Compound assignment:
  - `+=`, `-=`, `*=`, `/=`, `%=`
  - `&=`, `|=`, `^=`, `<<=`, `>>=`
- Increment/decrement:
  - prefix `++x`, `--x`
  - postfix `x++`, `x--` (if parser complexity allows in same chapter)
- Integer conversion behavior aligned with C-style intent

### Out of Scope
- Full C usual arithmetic conversions for every edge case across all integer ranks and floats (can stage)
- Sequence-point undefined behavior modeling
- Volatile/atomic semantics

## Design Policy
Implement in staged strictness:
1. Practical C-like behavior for common K&R cases
2. Explicitly document deviations from full C standard where deferred

## Lexer Requirements
Add tokens for multi-char operators:
- `tok_shl` (`<<`), `tok_shr` (`>>`)
- compound assignments (`+=`, `-=`, etc.)
- increment/decrement (`++`, `--`)

## Parser Requirements
- Extend precedence table for bitwise and shifts.
- Parse compound assignment forms.
- Parse prefix inc/dec in unary position.
- Parse postfix inc/dec in postfix-expression position (if included this chapter).

## AST Requirements
- Either:
  - expand `BinaryExprAST` operator set, plus unary/postfix nodes
- Or:
  - introduce dedicated nodes for compound assign and inc/dec

Must preserve lvalue constraints for mutating operators.

## Semantic Requirements

### Type Categories
- Distinguish signed/unsigned integer intent (`i*` vs `u*`), not just bit width.

### Promotion Rules (Chapter 19 target)
- Integer literals default to signed integer type (currently i64 unless suffixed later).
- Small integer types (`i8/i16/u8/u16`) promote before arithmetic.
- Mixed signed/unsigned behavior must be deterministic and documented.

### Shift Rules
- Shift count must be integer.
- Left operand must be integer.
- Choose arithmetic vs logical right shift based on signedness policy.

### Compound Assign
- `x op= y` equivalent to `x = x op y` with single lvalue evaluation.

### Inc/Dec
- Prefix returns updated value.
- Postfix returns original value (if implemented).

## LLVM Lowering Requirements
- `%` -> `srem` or `urem` based on signedness
- shifts -> `shl`, `ashr`/`lshr`
- bitwise ops -> `and`, `or`, `xor`
- compound assign emits load-op-store with correct cast path

## Diagnostics Requirements
- Invalid operand types for integer-only ops
- Invalid lvalue for mutating operators
- Shift by non-integer
- Unsupported combination diagnostics should mention operator and operand types

## ABI / Interop Requirements
- Preserve Chapter 16 signext/zext behavior at extern boundaries.
- New operator semantics should not regress runtime helper tests.

## Tests

### Positive
- `%`, bitwise, shifts across i/u widths
- compound assignments on locals and indexed lvalues
- inc/dec behavior checks
- mixed signed/unsigned practical cases

### Negative
- invalid operand types
- compound assignment on non-lvalue
- bad shift operand/count types

## Done Criteria
- Chapter 19 operator semantics tests pass
- Chapter 18 and earlier test suites remain green
- Known deviations from full ISO C semantics documented explicitly

## Implementation Sequencing Notes
- After Chapter 18 completion, copy baseline to `code/chapter19/`.
- Suggested order:
  1. `%`, bitwise, shifts
  2. compound assignment
  3. prefix inc/dec
  4. postfix inc/dec (if time)
  5. promotion/signedness hardening and tests

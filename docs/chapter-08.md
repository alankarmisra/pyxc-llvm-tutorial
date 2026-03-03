---
description: "Add comparison operators, if/else expressions, and for loops — then use them to render the Mandelbrot set in ASCII."
---
# 8. Control Flow: if, else, and for

## Where We Are

[Chapter 7](chapter-07.md) added file input mode so Pyxc can execute source files. But the language itself is still just arithmetic and function calls — there's no branching, no looping, no way to make decisions at runtime.

After this chapter Pyxc has:

- Comparison operators: `==`, `!=`, `<=`, `>=`, `<`, `>`
- `if`/`else` expressions that produce a value
- `for` loop expressions that drive side effects
- Unary minus so `-2.3` works as a literal

And as a payoff, the complete Mandelbrot set rendered in ASCII — written entirely in Pyxc.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-08
```

## Comparison Operators

Pyxc already had `<` and `>` from earlier chapters. This chapter adds four more: `==`, `!=`, `<=`, `>=`. Each is a two-character token.

### Lexer: two-character tokens

The lexer reads one character at a time. For `==`, it sees `=` first, then needs to peek at the next character to decide whether to return `tok_eq` or a bare `=`. We add a `peek()` helper that reads one character and immediately unreads it — it looks ahead without consuming:

```cpp
static int peek() {
  int C = fgetc(Input);
  ungetc(C, Input);
  return C;
}
```

Each two-character operator follows the same pattern in `gettok()`:

```cpp
if (LastChar == '=') {
  int Tok = (peek() == '=') ? (advance(), tok_eq) : '=';
  LastChar = advance();
  return Tok;
}
```

Read: if the next character is also `=`, consume it with `advance()` and return `tok_eq`. Otherwise return the bare `=`. Either way, call `advance()` at the end to load the next `LastChar` for the following token. The same pattern handles `!`, `<`, and `>`.

### Parser: BinopPrecedence uses int keys

In earlier chapters, `BinopPrecedence` was a `map<char, int>`. Extending it to named token enums (which are negative ints) required changing the key type to `int`:

```cpp
static map<int, int> BinopPrecedence = {
    {tok_eq, 10},  // ==
    {tok_neq, 10}, // !=
    {tok_leq, 10}, // <=
    {tok_geq, 10}, // >=
    {'<', 10},     // <
    {'>', 10},     // >
    {'+', 20},     // +
    {'-', 20},     // -
    {'*', 40},     // *
};
```

`GetTokPrecedence` was also updated: the old guard `if (!isascii(CurTok)) return -1` blocked named tokens. Now it does a simple map lookup:

```cpp
static int GetTokPrecedence() {
  auto It = BinopPrecedence.find(CurTok);
  if (It == BinopPrecedence.end() || It->second <= 0)
    return -1;
  return It->second;
}
```

Likewise, `BinaryExprAST::Op` changed from `char` to `int` so it can store negative token values without truncation.

### Codegen: FCmp + UIToFP

All comparison operators follow the same two-step pattern in `BinaryExprAST::codegen`:

```cpp
case tok_eq:
  L = Builder->CreateFCmpUEQ(L, R, "cmptmp");
  return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
```

`CreateFCmpUEQ` produces an `i1` — LLVM's one-bit boolean. Since Pyxc represents everything as `double`, `CreateUIToFP` widens it: `false → 0.0`, `true → 1.0`. This double boolean is what the parser sees as the expression's value.

## if/else Expressions

`if` in Pyxc is an **expression** — it produces a value, not just a side effect. The syntax:

```python
if condition: then_expr
else: else_expr
```

Because `if` is a primary expression, it can appear anywhere an expression can — including as the body of a `for` loop, as a function argument, or nested inside another `if`.

Both branches are mandatory for now. Without an `else`, what value would the expression produce if the condition is false? Mandatory `else` keeps codegen simple and is consistent with expression-oriented languages like Rust and Scala.

### Parsing

`ParseIfExpr` handles the `if` keyword. After parsing the condition it skips any newlines before checking for `:`, which allows the condition to span lines:

```cpp
auto Cond = ParseExpression();

// Allow condition to end on the previous line.
while (CurTok == tok_eol)
  getNextToken();

if (CurTok != ':')
  return LogError("Expected ':' after if condition");
```

Similarly, newlines are skipped before the `else` keyword and after the `else:` colon, allowing multi-line formatting.

### Codegen: three basic blocks and a PHI node

`IfExprAST::codegen` emits three LLVM basic blocks:

```
entry:
  %ifcond = fcmp one double %cond, 0.0
  br i1 %ifcond, label %then, label %else

then:
  <then_expr codegen>
  br label %ifcont

else:
  <else_expr codegen>
  br label %ifcont

ifcont:
  %iftmp = phi double [ %then_val, %then ], [ %else_val, %else ]
```

The condition is converted to `i1` with `fcmp one` — ordered not-equal to `0.0`. "Ordered" means NaN inputs produce `false`, so a NaN condition silently takes the else branch rather than firing unpredictably.

The PHI node in `ifcont` is SSA's way of saying "this value comes from two different paths." After the branches merge, `%iftmp` holds whichever value was computed.

One subtlety: after codegenning the then-block, the current insert block may have changed — nested ifs add their own blocks. The actual predecessor of the merge block is wherever the builder ended up, not the original `ThenBB`. So we re-capture it:

```cpp
Builder->CreateBr(MergeBB);
ThenBB = Builder->GetInsertBlock(); // may differ from original ThenBB
```

The same applies to `ElseBB`.

### The optimizer collapses trivial ifs to select

When both branches just pass through an existing value (no new computation), the optimizer replaces the entire three-block structure with a single `select` instruction:

```llvm
define double @higher(double %x, double %y) {
entry:
  %cmptmp = fcmp ugt double %x, %y
  %x.y = select i1 %cmptmp, double %x, double %y
  ret double %x.y
}
```

`select i1 cond, T true_val, T false_val` is LLVM's ternary — no branching needed. This only happens when both branches are side-effect-free. Functions with real work (`printd`, `putchard`) keep the full block structure.

## for Loop Expressions

The `for` loop drives iteration for side effects. The syntax:

```python
for var = start, condition, step:
    body
```

The loop runs while `condition` is true. `var` is introduced by the `for` and is in scope for `condition`, `step`, and `body`. The expression always produces `0.0` — loops are used for what they do, not what they return.

### Codegen: three blocks, check at the top

The loop compiles to three basic blocks:

```
preheader:
  start_val = <start>
  br loop_cond

loop_cond:
  i = phi [ start_val, preheader ], [ next_i, loop_body ]
  cond = <condition> fcmp one 0.0
  br cond loop_body, afterloop

loop_body:
  <body>           ; side effects
  next_i = i + step
  br loop_cond

afterloop:
  ret 0.0
```

The condition is checked **before** the first iteration. If the condition is false on entry, the body never runs.

The PHI node at the top of `loop_cond` merges the initial value (from the preheader) with the updated value (from the end of `loop_body`). The back-edge incoming block is captured after body codegen — nested ifs inside the body may add blocks, so the builder may not be in `BodyBB` anymore when the body finishes:

```cpp
BasicBlock *BodyEndBB = Builder->GetInsertBlock();
Variable->addIncoming(NextVar, BodyEndBB);
Builder->CreateBr(CondBB);
```

### Variable shadowing

If an outer function parameter has the same name as the loop variable, the loop variable takes precedence inside the loop. The outer value is saved before the loop and restored in `afterloop`:

```cpp
Value *OldVal = NamedValues[VarName];
NamedValues[VarName] = Variable;
// ... loop body ...
if (OldVal)
  NamedValues[VarName] = OldVal;
else
  NamedValues.erase(VarName);
```

## Unary Minus

`-2.3` would otherwise lex as the binary operator `-` followed by `2.3`, which fails because there's no left operand. `ParseUnaryMinus` handles the `-` case in `ParsePrimary` by desugaring it to `0.0 - operand`:

```cpp
static unique_ptr<ExprAST> ParseUnaryMinus() {
  getNextToken(); // eat '-'
  auto Operand = ParsePrimary();
  auto Zero = make_unique<NumberExprAST>(0.0);
  return make_unique<BinaryExprAST>('-', std::move(Zero), std::move(Operand));
}
```

The optimizer constant-folds `0.0 - 2.3` to `-2.3` immediately.

## The Mandelbrot Set

With comparisons, `if`/`else`, and `for`, Pyxc is expressive enough to render the Mandelbrot set. The Mandelbrot set is the set of complex numbers `c` for which the iteration `z = z² + c` (starting from `z = 0`) does not diverge to infinity.

```python
extern def putchard(x)

def mandelconverge(real, imag, iters, creal, cimag):
    return if iters == 0: iters
           else: if (real * real + imag * imag) > 4: iters
                 else: mandelconverge(real * real - imag * imag + creal,
                                      2 * real * imag + cimag,
                                      iters - 1, creal, cimag)

def mandelrow(xmin, xmax, xstep, y):
    return for x = xmin, x < xmax, xstep:
               putchard(if mandelconverge(x, y, 255, x, y) == 0: 32
                        else: 42)

def mandelhelp(xmin, xmax, xstep, ymin, ymax, ystep):
    return for y = ymin, y < ymax, ystep:
               mandelrow(xmin, xmax, xstep, y) + putchard(10)

def mandel(realstart, imagstart, realmag, imagmag):
    return mandelhelp(realstart, realstart + realmag * 78,
                      realmag,
                      imagstart, imagstart + imagmag * 40,
                      imagmag)
```

**`mandelconverge`** is recursive. It takes the current complex value `(real, imag)`, the remaining iteration count, and the original point `(creal, cimag)`. It returns the remaining count when the iteration terminates:

- `iters == 0` — hit the limit: the point is likely inside the set
- `real² + imag² > 4` — magnitude exceeded 2: the point is diverging

Otherwise it recurses with the next iteration: `z = z² + c`, where `z² = (real² - imag², 2·real·imag)`.

**`mandelrow`** drives the x-axis for a single row, calling `putchard` with space (32) for in-set points and `*` (42) for out-of-set points.

**`mandelhelp`** drives the y-axis. The outer `for y` body is `mandelrow(...) + putchard(10)` — the `+` sequences both calls, printing the row then a newline (ASCII 10). The result `0.0 + 0.0` is discarded.

**`mandel`** is the entry point, mapping row/column counts to complex-plane coordinates.

### Output

```bash
$ build/pyxc test/mandel.pyxc
```

```
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************   *********************************
******************************************    ********************************
*******************************************  *********************************
************************************ **          *****************************
************************************                 *************************
***********************************                 **************************
**********************************                   *************************
*********************************                     ************************
*********************** *  *****                      ************************
***********************       **                      ************************
**********************         *                      ************************
*******************  *         *                     *************************
*******************  *         *                     *************************
**********************         *                      ************************
***********************       **                      ************************
*********************** *   ****                      ************************
*********************************                     ************************
**********************************                   *************************
***********************************                 **************************
*************************************                *************************
************************************ *           *****************************
*******************************************  *********************************
******************************************    ********************************
******************************************    ********************************
******************************************** *********************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
```

Other views:

```python
mandel(-2, -1, 0.02, 0.04)    # wider
mandel(-0.9, -1.4, 0.02, 0.03) # zoomed to a tendril
```

```
# mandel(-2, -1, 0.02, 0.04)
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
************************************************************************* * **
*************************************************************************    *
****************************************************************************  
************************************************************************      
********************************************************************  *       
**********************************************************************        
********************************************************************          
*******************************************************************           
************************************************** **************             
******************************************   *         *********              
*******************************************                *****              
*************************************** *                   ****              
***************************************                      **               
***************************************                       *               
*********************************    *                                        
***********  ************* **                                                 
*********************************    *                                        
***************************************                       *               
***************************************                      **               
*************************************** *                   ****              
*******************************************                *****              
******************************************   *         *********              
************************************************** **************             
*******************************************************************           
********************************************************************          
**********************************************************************        
********************************************************************  *       
************************************************************************      
****************************************************************************  
*************************************************************************    *
```

```
# mandel(-0.9, -1.4, 0.02, 0.03)
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
***************************************  *************************************
*************************************    *************************************
**********************************         ***********************************
***********************************         **********************************
***********************************          *********************************
***********************************         **********************************
************************************       ***********************************
************************************* * ** ***********************************
*************************** *                   *** **************************
*******************   ***                           **************************
******************    *                               ****    ****************
*******************                                           ****************
*******************                                          *****************
*****************                                          *******************
*************  *                                             *****************
**************                                                ****************
**************                                                 ***************
*********** *                                                  *  ************
************                                                   * *************
**********                                                      **************
***********                                                     **************
**********                                                      * ************
```

The entire renderer — iteration, branching, output — is Pyxc code. The only runtime function provided by the host is `putchard`, which writes one ASCII character to stderr.

## What We Built

| Piece | What it does |
|---|---|
| `tok_eq / tok_neq / tok_leq / tok_geq` | New token enums for `==`, `!=`, `<=`, `>=` |
| `peek()` | Reads one character and unreads it; used by two-char token branches in `gettok()` |
| `BinopPrecedence` key type `int` | Accommodates both ASCII char operators and named token enums |
| `GetTokPrecedence` map lookup | Replaces `!isascii` guard so named tokens participate in binary expressions |
| `BinaryExprAST::Op` type `int` | Stores negative token values without truncation |
| `FCmpU*` + `UIToFP` | Comparison result as `i1`, widened to `double` for uniformity |
| `IfExprAST` / `ParseIfExpr` | `if condition: then else: else` expression with mandatory else |
| Three blocks + PHI | `then`, `else`, `ifcont`; PHI merges the two branch values |
| `ForExprAST` / `ParseForExpr` | `for var = start, cond, step: body` expression producing `0.0` |
| `loop_cond` / `loop_body` / `afterloop` | Check-at-top loop; PHI merges start and back-edge values |
| Variable shadowing + restore | Loop variable overrides outer binding; outer is restored in afterloop |
| `ParseUnaryMinus` | `-expr` desugared to `0.0 - expr`; optimizer constant-folds literals |
| Newline skipping in `ParseIfExpr` | Allows condition and bodies to span lines |

## Known Limitations

- **No block bodies.** Function bodies and loop bodies are single expressions. Multi-statement blocks require a new AST node and a sequencing operator — a later chapter adds these.
- **No mutable local variables.** `NamedValues` only holds function parameters. `alloca`/`store`/`load` and `mem2reg` come later.
- **Expression `if` only.** The current `if` always produces a value. A statement `if` (where only one branch runs for side effects) will replace this when blocks arrive.
- **No `break` or `continue`.** Early exit from loops requires blocks and a different codegen strategy.

## What's Next

Chapter 9 adds user-defined operators via Python-style decorators — `@binary(precedence)` and `@unary` — letting Pyxc programs define new operators in the language itself. The payoff is a richer Mandelbrot renderer with density shading and a clean sequencing operator.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

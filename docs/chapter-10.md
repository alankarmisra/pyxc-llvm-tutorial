---
description: "Add built-in unary minus so -x parses directly, replacing the 0 - x workaround, and use it to shade the Mandelbrot renderer by density."
---
# 10. pyxc: Unary Minus

## What I Am Building

[Chapter 9](chapter-09.md) added comparisons, `if`/`else`, and `for`. pyxc still has no way to negate a value directly:

<!-- code-merge:start -->
```pyxc
ready> -5
```
```bash
Error (Line 1, Column 1): unknown token when expecting an expression
-5
 ^~~~
```
<!-- code-merge:end -->

`-` only exists as subtraction, so I've been writing `0 - 5` to get a negative number. I add unary minus:

<!-- code-merge:start -->
```pyxc
ready> -5
```
```bash
Parsed a top-level expression.
Evaluated to -5.000000
```
```pyxc
ready> -2 * 3
```
```bash
Parsed a top-level expression.
Evaluated to -6.000000
```
```pyxc
ready> --5
```
```bash
Parsed a top-level expression.
Evaluated to 5.000000
```
<!-- code-merge:end -->

`-2 * 3` is `-6`, not `-(2 * 3)` written differently — unary minus binds tighter than `*`, same as it does in every C-family language. `--5` is double negation, not decrement — pyxc has no `--` token yet, so this is just `-` applied twice.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-10
```

## Grammar

I insert `unary-expression` between `term` and `primary`. Every operand slot that used to call `primary` directly now goes through `unary-expression` first:

`code/chapter-10/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = function-definition | external | top-level-expression ;
 function-definition      = "def" function-signature ":" [ end-of-lines ] expression ;
 external        = "extern" "def" function-signature ;
 top-level-expression    = expression ;
 function-signature       = name "(" [ parameters ] ")" ;
 parameters               = parameter { "," parameter } ;
 parameter                = name ;
 ifexpr          = "if" expression ":" [ end-of-lines ] expression [ end-of-lines ] "else" ":" [ end-of-lines ] expression ;
 forexpr         = "for" name "=" expression "," expression "," expression ":" [ end-of-lines ] expression ;
 expression               = comparison ;
 comparison               = sum { comparison-operator sum } ;
 comparison-operator      = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum                      = term { ("+" | "-") term } ;
-term                     = primary { ("*" | "/") primary } ;
+term                     = unary-expression { ("*" | "/") unary-expression } ;
+unary-expression       = "-" unary-expression | primary ;
 primary         = name-expression | number-expression | parenthesized-expression
                 | ifexpr | forexpr ;
 name-expression  = name | call-expression ;
 call-expression         = name "(" [ arguments ] ")" ;
 arguments               = expression { "," expression } ;
 number-expression      = number ;
 parenthesized-expression       = "(" expression ")" ;
 name      = (letter | "_") { letter | digit | "_" } ;
 number          = digit { digit } [ "." { digit } ]
                 | "." digit { digit } ;
 letter          = "A".."Z" | "a".."z" ;
 digit           = "0".."9" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
```

I could have special-cased `-` inside `ParsePrimary` instead of adding a new tier, but then `--x` and `-(x + 1)` wouldn't fall out naturally — I'd need to handle chaining and recursion by hand at the call site instead of getting it for free from the grammar. Giving unary minus its own self-recursive tier means `-` in front of *anything* that can start a `unary-expression`, including another `-`, just works.

## A New AST Node

I add one node for unary operator application:

```cpp
/// UnaryExpressionNode - Expression class for a unary operator application.
/// Built-in unary minus is represented with opcode '-' and lowered directly to
/// LLVM `fneg`.
class UnaryExpressionNode : public ExpressionNode {
  char Opcode;
  unique_ptr<ExpressionNode> Operand;

public:
  UnaryExpressionNode(char Opcode, unique_ptr<ExpressionNode> Operand)
      : Opcode(Opcode), Operand(std::move(Operand)) {}
  Value *codegen() override;
};
```

`Opcode` is a `char` because unary minus is the only unary operator I have — one character is all I need to distinguish it, and it leaves room to add more (`!`, `~`) later without changing the shape of the node.

## Parsing Unary Minus

`ParseTerm` now calls `ParseUnary` for each operand instead of `ParsePrimary`:

```cpp
static unique_ptr<ExpressionNode>
ParseUnary(); // forward declaration for ParseUnaryMinus

/// unaryminus
///   = "-" unaryexpr ;
/// Parse built-in unary minus into a UnaryExpressionNode with opcode '-'.
/// The operand is a full unaryexpr so unary chains work naturally
/// (e.g. --x and -(x+1)).
static unique_ptr<ExpressionNode> ParseUnaryMinus() {
  getNextToken(); // eat '-'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  return make_unique<UnaryExpressionNode>(tok_minus, std::move(Operand));
}

/// unaryexpr
///   = "-" unaryexpr
///   | primary ;
static unique_ptr<ExpressionNode> ParseUnary() {
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();
  return ParsePrimary();
}
```

`ParseUnaryMinus` calls `ParseUnary` for its own operand, not `ParsePrimary` — that's what lets it recurse into itself. `--5` works because the first `-` calls `ParseUnary`, which sees the second `-` and calls `ParseUnaryMinus` again before either call has produced a value.

`ParseUnary` needs a forward declaration above `ParseUnaryMinus` because the two functions call each other: `ParseUnaryMinus` needs `ParseUnary` to parse its operand, and `ParseUnary` needs `ParseUnaryMinus` to handle the `-` case. Whichever one I define first has to declare the other ahead of its own body.

```cpp
/// term
///   = unary-expression { ("*" | "/") unary-expression } ;
static unique_ptr<ExpressionNode> ParseTerm() {
  auto Left = ParseUnary();
  if (!Left)
    return nullptr;

  while (CurrentToken == tok_star || CurrentToken == tok_slash) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseUnary();
    if (!Right)
      return nullptr;
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }
  return Left;
}
```

That single change — `ParsePrimary()` to `ParseUnary()`, twice, in `ParseTerm` — is what makes `-2 * 3` parse as `(-2) * 3` rather than failing or parsing as `-(2 * 3)`. `ParseUnary` grabs the `-2` as a complete unit before `ParseTerm`'s `while` loop ever sees the `*`.

There's no new error path here — an invalid operand after `-` still fails with the same "unknown token when expecting an expression" `ParsePrimary` has always produced, since `ParseUnaryMinus` just propagates whatever `ParseUnary` returns:

<!-- code-merge:start -->
```pyxc
ready> -)
```
```bash
Error (Line 1, Column 2): unknown token when expecting an expression
-)
 ^~~~
```
<!-- code-merge:end -->

## Codegen

`UnaryExpressionNode::codegen` lowers `-` directly to LLVM's [`fneg`](https://llvm.org/docs/LangRef.html#fneg-instruction):

```cpp
/// UnaryExpressionNode::codegen - Emit built-in unary minus directly.
Value *UnaryExpressionNode::codegen() {
  Value *Operator = Operand->codegen();
  if (!Operator)
    return nullptr;

  if (Opcode == tok_minus)
    return TheBuilder->CreateFNeg(Operator, "negtmp");
  return LogErrorV("Unknown unary operator");
}
```

For `-5`, this produces:

```llvm
%negtmp = fneg double 5.000000e+00
```

The `Opcode == tok_minus` check and its `LogErrorV` fallback look unreachable right now — `Opcode` is only ever `'-'`, since that's the only unary operator that exists. It's there for the same reason the node stores a `char` instead of hardcoding `-`: it's the shape a second unary operator would need later, without pretending one exists yet.

## The Payoff: Density-Shaded Mandelbrot

[Chapter 9](chapter-09.md) rendered the Mandelbrot set with a hard edge — every point was either `*` or a space. With unary minus, I can drop the `0 - 2.3` workaround, and I replace the `mandelrow(...) + putchard(10)` sequencing hack with a small helper function. Both changes let me focus the renderer on shading by density instead of the workarounds:

```pyxc
# test/mandel.pyxc
extern def putchard(x)

# Evaluate x before returning y.
def sequence(x, y):
    y

# printdensity - map iteration count to an ASCII shade.
def printdensity(d):
    if d > 8: putchard(32) else: if d > 4: putchard(46) else: if d > 2: putchard(43) else: putchard(42)

# Determine whether z = z^2 + c diverges for the given point.
def mandelconverger(real, imag, iters, creal, cimag):
    if iters > 255: iters else: if real * real + imag * imag > 4: iters else: mandelconverger(real * real - imag * imag + creal, 2 * real * imag + cimag, iters + 1, creal, cimag)

# Return number of iterations required for escape.
def mandelconverge(real, imag):
    mandelconverger(real, imag, 0, real, imag)

# Render one row.
def mandelrow(xmin, xmax, xstep, y):
    for x = xmin, x < xmax, xstep:
               printdensity(mandelconverge(x, y))

# Render full 2D region.
def mandelhelp(xmin, xmax, xstep, ymin, ymax, ystep):
    for y = ymin, y < ymax, ystep:
               sequence(mandelrow(xmin, xmax, xstep, y), putchard(10))

# Top-level helper.
def mandel(realstart, imagstart, realmag, imagmag):
    mandelhelp(realstart, realstart + realmag * 78, realmag, imagstart, imagstart + imagmag * 40, imagmag)

mandel(-2.3, -1.3, 0.05, 0.07)
mandel(-2, -1, 0.02, 0.04)
mandel(-0.9, -1.4, 0.02, 0.03)
```

`sequence(x, y)` isn't a new language feature — function calls already evaluate their arguments left to right, so `sequence(mandelrow(...), putchard(10))` runs the row, then the newline, then returns whatever `putchard` returned. Chapter 9 got the same effect by adding two `0.0` return values together, which worked by accident; `sequence` says what I actually mean.

`printdensity` maps an iteration count to a shade instead of just inside/outside:

| count | char | meaning |
|-------|------|---------|
| > 8   | ` ` (space) | deep inside — survived 9+ iterations |
| > 4   | `.`  | boundary zone — survived 5–8 iterations |
| > 2   | `+`  | near boundary — survived 3–4 iterations |
| ≤ 2   | `*`  | fast escape — outside the set |

Run it directly:

```bash
./build/pyxc test/mandel.pyxc
```

The same view as chapter 9 (`mandel(-2.3, -1.3, 0.05, 0.07)`) now produces:

```
******************************************************************************
******************************************************************************
****************************************++++++********************************
************************************+++++...++++++****************************
*********************************++++++++.. ...+++++**************************
*******************************++++++++++..   ..+++++*************************
******************************++++++++++.     ..++++++************************
****************************+++++++++....      ..++++++***********************
**************************++++++++.......      .....++++**********************
*************************++++++++.   .            ... .++*********************
***********************++++++++...                     ++*********************
*********************+++++++++....                    .+++********************
******************+++..+++++....                      ..+++*******************
**************++++++. ..........                        +++*******************
***********++++++++..        ..                         .++*******************
*********++++++++++...                                 .++++******************
********++++++++++..                                   .++++******************
*******++++++.....                                    ..++++******************
*******+........                                     ...++++******************
*******+... ....                                     ...++++******************
*******+++++......                                    ..++++******************
*******++++++++++...                                   .++++******************
*********++++++++++...                                  ++++******************
**********+++++++++..        ..                        ..++*******************
*************++++++.. ..........                        +++*******************
******************+++...+++.....                      ..+++*******************
*********************+++++++++....                    ..++********************
***********************++++++++...                     +++********************
*************************+++++++..   .            ... .++*********************
**************************++++++++.......      ......+++**********************
****************************+++++++++....      ..++++++***********************
*****************************++++++++++..     ..++++++************************
*******************************++++++++++..  ...+++++*************************
*********************************++++++++.. ...+++++**************************
***********************************++++++....+++++****************************
***************************************++++++++*******************************
******************************************************************************
******************************************************************************
******************************************************************************
******************************************************************************
```

The boundary is now a gradient instead of a hard edge. The file calls `mandel(...)` two more times, zooming into different regions of the complex plane.

## Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/pyxc
```

With no filename, I start the interactive REPL. I press `Ctrl-D` to exit.

To run the Mandelbrot renderer directly:

```bash
./build/pyxc test/mandel.pyxc
```

## What's Next

In [Chapter 11](chapter-11.md), I add mutable local variables and assignment using a temporary `var ... :` expression form. This keeps pyxc expression-oriented for one more chapter before real statement blocks arrive.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

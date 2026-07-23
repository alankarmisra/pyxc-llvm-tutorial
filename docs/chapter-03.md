---
description: "Build a recursive-descent parser and AST: turn tokens into structure and see 'Parsed a function definition' for the first time."
---
# 3. pyxc: Operator Precedence Parsing

## Where We Are


## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-03
```

## Operator Precedence

Since binary expressions can be ambiguous (does `x+y*z` mean `(x+y)*z` or `x+(y*z)` ?) I have to tell the compiler that `*` should *bind* more tightly than `+`. *Bind more tightly* is a fancy way of saying *compute before others*. I only use *binding* because compiler literature uses it. I use numbers to decide the binding order, and the number is called **precedence**. If you know C++, you know about operator precedence tables, so accuse me of *obvious-splaining* or *o-splaining* if you will, another term I just invented. It's easy to invent things. 

I store precedences in a map. Higher precedence means tighter binding:

```cpp
/// BinopPrecedence - Maps each binary operator token to its precedence.
/// Higher numbers bind more tightly: '*' (40) > '+'/'-' (20) > '<' (10).
/// Operators not in this map return -1 from GetTokPrecedence(), which tells
/// ParseBinOpRHS to stop consuming operators and return what it has so far.
static const map<int, int> BinopPrecedence = {
    {tok_less, 10},
    {tok_plus, 20},
    {tok_minus, 20},
    {tok_star, 40},
};
```

A helper returns the precedence of whatever is in `CurTok`, or `-1` if it's not a known binary operator:

```cpp
static int GetTokPrecedence() {
    if (!isascii(CurTok))
        return -1;

    auto It = BinopPrecedence.find(CurTok);
    if (It == BinopPrecedence.end() || It->second <= 0)
        return -1;
    return It->second;
}
```

The `isascii` guard rejects my named `Token` enums (which are negative integers) so they can never be mistaken for operators.

## Binary Expressions: Precedence Climbing

The most subtle function in the parser is `ParseBinOpRHS`. It handles a sequence of binary operators with correct precedence.

The key idea: when I'm parsing `a + b * c + d`, I need to figure out which operators go together. The `*` between `b` and `c` binds more tightly than the `+` around it, so `b * c` should be grouped first.

I solve this by setting a minimum precedence. `ParseBinOpRHS` is told: only deal with operators at this precedence level or higher. If it sees a higher-precedence operator on the right, it steps aside (recurses) and lets that operator take its operands first.

```cpp
/// binoprhs
///   = { binaryop primary } ;
static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                         unique_ptr<ExprAST> LHS) {
  // If this is a binop, find its precedence.
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < ExprPrec)
      return LHS;

    // Okay, we know this is a binop.
    int BinOp = CurTok;
    getNextToken(); // eat binop

    // Parse the primary expression after the binary operator.
    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    // Merge LHS/RHS.
    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}
```

Let's trace `a + b * c + d` step by step:

1. Called with precedence level of 0, `LHS = a`. Current operator is `+` (prec 20).
2. 20 ≥ 0 — consume `+`. Parse `b` as `RHS`. Next operator is `*` (prec 40).
3. 40 > 20 — recurse: `ParseBinOpRHS(21, b)`.
4. Inside recursion: current operator is `*` (prec 40). 40 ≥ 21 — consume `*`. Parse `c`. Next is `+` (prec 20). 20 < 21 — stop. Return `b*c`.
5. Back in outer call: `RHS = b*c`. Build `a + (b*c)`.
6. Loop continues. Next operator is `+` (prec 20). 20 ≥ 0 — consume `+`. Parse `d`. Next token is not an operator — return. Build `(a+(b*c)) + d`.

The `TokPrec + 1` makes sure operators group from left to right.

For operators at the same level — like `a - b - c` — I want `(a - b) - c`, not `a - (b - c)`.

The `+1` means the recursive call stops when it sees another operator at the same level, leaving it for the outer loop to handle.

`ParseExpression` kicks everything off with threshold 0 (accept any operator):

```cpp
/// expression
///   = primary binoprhs ;
static unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}
```

## Build and Run

```bash
cd code/chapter-02
cmake -S . -B build && cmake --build build
./build/pyxc
```

The `test/` directory has lit tests covering each grammar rule — one file per rule. Browse them for more input examples, or run the suite:

```bash
llvm-lit code/chapter-02/test/
```

## Try It

```pyxc
ready> def add(x, y):
return x + y
Parsed a function definition.
ready> def fib(n):
return fib(n-1) + fib(n-2)
Parsed a function definition.
ready> 1 + 2 * 3
Parsed a top-level expression.
ready> sin(1.0) + cos(2.0)
Parsed a top-level expression.
ready> def bad(x) return x
Error: Expected ':' in function definition (token: -6)
ready>
```

The parser accepts valid syntax and rejects invalid syntax with an error message. The REPL keeps running after errors.

### The Full Grammar

Here's the complete grammar for Pyxc at this stage.  

[pyxc.ebnf](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/chapter-02/pyxc.ebnf)

```ebnf
(* parser territory *)
program        = [ eols ] [ top { eols top } ] [ eols ] ;
eols           = eol { eol } ;
top            = functiondef | toplevelexpr ;
functiondef    = "def" functionsignature ":" [ eols ] "return" expression ;
toplevelexpr   = expression ;
functionsignature = name "(" [ name { "," name } ] ")" ;
expression     = primary binoprhs ;
binoprhs       = { binaryop primary } ;
primary        = nameexpr | numberexpr | parenexpr ;
nameexpr       = name
                 | name "(" [ expression { "," expression } ] ")" ;
numberexpr     = number ;
parenexpr      = "(" expression ")" ;

(* lexer territory *)
binaryop       = "+" | "-" | "*" | "<" ;
name           = (letter | "_") { letter | digit | "_" } ;
number         = digit { digit } [ "." { digit } ]
                 | "." digit { digit } ;
letter         = "A".."Z" | "a".."z" ;
digit          = "0".."9" ;
eol            = "\r\n" | "\r" | "\n" ;
(*  
    `ws` may appear between any two tokens 
     and is ignored by the lexer.  
*)
ws             = " " | "\t" ;
```

## What's Next

I now have a parser that understands the structure of pyxc code and builds a tree of objects representing it. But before I hook this up to LLVM and generate real machine code, [Chapter 4](chapter-04.md) revisits the lexer: readable error messages, source locations, and the keyword map. The parser works but [Chapter 4](chapter-04.md) makes it pleasant to use.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version` and `ninja --version`

We'll figure it out.

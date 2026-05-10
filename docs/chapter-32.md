---
description: "Add &&, ||, and ! — logical operators with short-circuit evaluation and a dedicated bool result type."
---
# 32. pyxc: Logical Operators

## Where We Are

[Chapter 31](chapter-31.md) completed arithmetic. Conditions in `if` and `while` can now involve complex expressions, but there is still no way to combine two boolean checks or negate one. After this chapter:

```pyxc
extern def printd(x: float64)

def is_between(x: int, lo: int, hi: int) -> bool:
  return x >= lo && x <= hi

def main() -> int:
  var a: bool = True
  var b: bool = !a
  if b || is_between(5, 1, 10):
    printd(1.0)
  return 0
```

```
1.000000
```

`&&` and `||` short-circuit: the right-hand side is not evaluated if the result is already determined by the left.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-32
```

## Grammar

`!` is added to `unaryop`. `&&` and `||` join `builtinbinaryop`.

```ebnf
unaryop         = "-" | "!" | "++" | "--" | userdefunaryop ;  -- changed
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!="
                | "&&" | "||" ;                               -- changed
```

### Full Grammar

`code/chapter-32/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | traitdef | structdef | classdef | impldef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
traitdef        = "trait" identifier [ "[" identifier "]" ] ":" eols traitblock ;
traitblock      = indent traitmethodsig { eols traitmethodsig } dedent ;
traitmethodsig  = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ;
structdef       = "struct" identifier ":" eols structblock ;
classdef        = "class" identifier [ "(" traitref { "," traitref } ")" ] ":" eols structblock ;
traitref        = identifier [ "[" type "]" ] ;
impldef         = "impl" traitref "for" identifier ":" eols implblock ;
implblock       = indent implmethod { eols implmethod } dedent ;
implmethod      = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ":" ( simplestmt | eols block ) ;
structblock     = indent classmember { eols classmember } dedent ;
classmember     = [ visibility ] ( fielddecl | methoddef ) ;
visibility      = "public" | "private" ;
methoddef       = "def" identifier "(" [ typedparam { "," typedparam } ] ")"
                  [ "->" type ] ":" ( simplestmt | eols block ) ;
fielddecl       = identifier ":" type ;
definition      = "def" prototype [ "->" type ] ":" ( simplestmt | eols block ) ;
decorateddef    = binarydecorator eols "def" binaryopprototype [ "->" type ] ":" ( simplestmt | eols block )
                | unarydecorator  eols "def" unaryopprototype  [ "->" type ] ":" ( simplestmt | eols block ) ;
binarydecorator = "@" "binary" "(" integer ")" ;
unarydecorator  = "@" "unary" ;
binaryopprototype = customopchar "(" typedparam "," typedparam ")" ;
unaryopprototype  = customopchar "(" typedparam ")" ;
external        = "extern" "def" prototype [ "->" type ] ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ typedparam { "," typedparam } ] ")" ;
typedparam      = identifier ":" type ;
ifstmt          = "if" expression ":" suite
                [ eols "else" ":" suite ] ;
forstmt         = "for"
                  ( "var" identifier ":" type | identifier )
                  "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = lvalue assignop expression ;
simplestmt      = returnstmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" [ expression ] ;
block           = indent statement { eols statement } dedent ;
expression      = unaryexpr binoprhs ;
binoprhs        = { binaryop unaryexpr } ;
lvalue          = identifier | fieldaccess | indexexpr ;
varbinding      = identifier ":" type [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | postfixexpr ;
unaryop         = "-" | "!" | "++" | "--" | userdefunaryop ;
postfixexpr     = primary [ postfixop ] ;
postfixop       = "++" | "--" ;
primary         = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
castexpr        = casttype "(" expression ")" ;
sizeofexpr      = "sizeof" "(" type ")" ;
addrexpr        = "addr" "(" lvalue ")" ;
identifierexpr  = identifier | callexpr | methodcallexpr | ctorcallexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
methodcallexpr  = identifier "." identifier "(" [ expression { "," expression } ] ")" ;
ctorcallexpr    = identifier "(" [ expression { "," expression } ] ")" ;
fieldaccess     = identifier "." identifier { "." identifier } ;
indexexpr       = identifier "[" expression "]" ;
numberexpr      = number ;
arrayliteral    = "[" [ expression { "," expression } ] "]" ;
stringliteral   = "\"" { ? any char except " and newline ? | escape } "\"" ;
escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;
dedent          = DEDENT ;

assignop        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!="
                | "&&" | "||" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
builtintype     = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | "None" ;
aliastype       = identifier ;
structtype      = identifier ;
pointertype     = "ptr" "[" type "]" ;
type            = basetype [ arraysuffix ] ;
basetype        = builtintype | aliastype | structtype | pointertype ;
arraysuffix     = "[" integer "]" ;
casttype        = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | pointertype ;
integer         = digit { digit } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
bool_literal    = "True" | "False" ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
INDENT          = ? synthetic token emitted by lexer ? ;
DEDENT          = ? synthetic token emitted by lexer ? ;
```

## Logical Not: `!`

`!` on a `bool` operand is handled by a dedicated AST node:

```cpp
class LogicalNotExprAST : public ExprAST {
  unique_ptr<ExprAST> Operand;
  ...
};
```

Codegen emits a single `CreateNot` on an `i1` value. The result type is always `bool`.

If the operand is not `bool`, the parser falls through to the user-defined `unary!` lookup — so existing programs that define a custom `unary!` for other types still work.

## Short-Circuit Evaluation

`&&` and `||` do not use the standard binary expression path. Instead, they get early codegen that emits a conditional branch before evaluating the right-hand side:

**`a && b`:**

```
  evaluate a
  if a == false: jump to merge (result = false)
  evaluate b
merge:
  phi [ false from lhs_block, b from rhs_block ]
```

**`a || b`:**

```
  evaluate a
  if a == true: jump to merge (result = true)
  evaluate b
merge:
  phi [ true from lhs_block, b from rhs_block ]
```

The basic blocks are named `logic.rhs` and `logic.end` in the IR. The PHI node resolves to the correct boolean result regardless of which path was taken. If `b` is a function call with side effects, it will not be called if the short-circuit fires.

Both operands must be `bool`. Mixing `int` or any other type is a type error caught at parse time.

## Precedence

`||` and `&&` sit below all comparison and arithmetic operators:

| Operator | Precedence |
|---|---|
| `\|\|` | 5 |
| `&&` | 7 |
| comparisons (`==`, `<`, etc.) | 10–14 |
| arithmetic (`+`, `*`, etc.) | 20–40 |

`&&` binds more tightly than `||`, so `a || b && c` parses as `a || (b && c)`.

## Error Cases

**Non-bool operand:**
```pyxc
var x: int = 1
var y: bool = True
if x && y:  # Error: Type mismatch in binary operator
  return 1
```

Both sides must be `bool`. There is no implicit conversion from `int` to `bool`.

## Things Worth Knowing

**Short-circuit is real, not just an optimisation.** The right-hand side is structurally placed behind a conditional branch in the IR. A function call on the right of `&&` or `||` will genuinely not execute if the left determines the result.

**`!` on a non-bool falls through to user-defined `unary!`.** If you have defined a custom `unary!` for some other type, it continues to work. The built-in path only activates for `bool`.

**`&&` and `||` are not bitwise.** For bitwise AND and OR on integers, see [Chapter 34](chapter-34.md).

## What's Next

[Chapter 33](chapter-33.md) adds `while`, `do/while`, `break`, and `continue`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

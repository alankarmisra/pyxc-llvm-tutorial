---
description: "Allow assignment inside an expression so patterns like while (c = getchar()) != EOF work without a separate priming read."
---
# 39. pyxc: Assignment as Expression

## Where We Are

[Chapter 38](chapter-38.md) added unsigned integer types. pyxc can call `getchar()`, but the canonical K&R idiom for reading until EOF still doesn't compile:

```pyxc
# What we want to write:
while (c = getchar()) != EOF:
    ...
```

```
Error: Assignment target must be assignable
```

The problem is that `=` is a statement in pyxc — it cannot appear inside an expression like a while condition. After this chapter it can:

```pyxc
extern def getchar() -> int32
extern def printd(x: float64)

var EOF: int32 = -1

def main() -> int:
  var c: int32
  var blanks: int
  while (c = getchar()) != EOF:
    if c == ' ':
      blanks += 1
  printd(float64(blanks))
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-39
```

## Grammar

`expression` gains an optional assignment tail. The tail is right-recursive, which makes chained assignment right-associative.

```ebnf
expression = unaryexpr binoprhs [ assignop expression ] ; -- changed
```

The `assignstmt` rule is unchanged — assignment-as-statement still works exactly as before.

### Full Grammar

`code/chapter-39/pyxc.ebnf`

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
                { eols "elif" expression ":" suite }
                [ eols "else" ":" suite ] ;
whilestmt       = "while" expression ":" suite ;
dowhilestmt     = "do" ":" suite eols "while" expression ;
switchstmt      = "switch" expression ":" eols indent switchbody dedent ;
switchbody      = switchcase { eols switchcase } [ eols defaultcase ] ;
switchcase      = "case" switchint ":" suite ;
defaultcase     = "default" ":" suite ;
forstmt         = "for"
                  ( "var" identifier ":" type | identifier )
                  "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = lvalue assignop expression ; (* assignment is a statement here *)
simplestmt      = returnstmt | breakstmt | continuestmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt | whilestmt | dowhilestmt | switchstmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" [ expression ] ;
breakstmt       = "break" ;
continuestmt    = "continue" ;
block           = indent statement { eols statement } dedent ;
expression      = unaryexpr binoprhs [ assignop expression ] ;
binoprhs        = { binaryop unaryexpr } ;
lvalue          = identifier | fieldaccess | indexexpr ;
varbinding      = identifier ":" type [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | postfixexpr ;
unaryop         = "-" | "!" | "~" | "++" | "--" | userdefunaryop ;
postfixexpr     = primary [ postfixop ] ;
postfixop       = "++" | "--" ;
primary         = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral | charliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
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
charliteral     = "'" ( ? any char except ' and newline ? | charescape ) "'" ;
escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
charescape      = "\\" ( "\\" | "'" | "n" | "t" | "0" ) ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;
dedent          = DEDENT ;

assignop        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!="
                | "&&" | "||"
                | "&" | "|" | "^" | "<<" | ">>" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
builtintype     = "int" | "int8" | "int16" | "int32" | "int64"
                | "uint8" | "uint16" | "uint32" | "uint64"
                | "float" | "float32" | "float64"
                | "bool" | "None" ;
aliastype       = identifier ;
structtype      = identifier ;
pointertype     = "ptr" "[" type "]" ;
type            = basetype [ arraysuffix ] ;
basetype        = builtintype | aliastype | structtype | pointertype ;
arraysuffix     = "[" integer "]" ;
casttype        = "int" | "int8" | "int16" | "int32" | "int64"
                | "uint8" | "uint16" | "uint32" | "uint64"
                | "float" | "float32" | "float64"
                | "bool" | pointertype ;
integer         = digit { digit } ;
switchint       = [ "-" ] integer ;
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

## Parsing

`ParseExpression` already parsed `unaryexpr binoprhs`. The change is a tail check after `binoprhs` resolves: if the next token is `=` or a compound-assign operator, consume it and recurse to parse the right-hand side. Because the right-hand side is parsed via a recursive call to `ParseExpression`, chained assignment is automatically right-associative.

Lvalue validation has always been done at parse time. It still is. When the tail fires, a `BuildAssignmentExpr` helper pattern-matches on the LHS node type — `VariableExprAST`, `FieldExprAST`, `IndexExprAST`, or `IndexedFieldExprAST` — and returns the appropriate assignment AST node. Any other node type is rejected:

```
(x + 1) = 3   # Error: Assignment target must be assignable
```

No new AST nodes are introduced. The existing `AssignmentExprAST`, `CompoundAssignmentExprAST`, and their field/index variants are reused unchanged.

## The Value of an Assignment

In C, `c = getchar()` is an expression whose value is the value stored into `c`. pyxc matches this: `AssignmentExprAST::codegen` stores the value and then *returns it*, so it can be used in any larger expression context.

```pyxc
var result: int = (c = 5) + 1   # result is 6; c is 5
```

Compound assignment works the same way — `(c += 3)` returns the new value of `c`.

## Right Associativity

The assignment tail recurses on `ParseExpression`, not on `ParseBinOpRHS`. This means `=` binds more loosely than every binary operator and chains right:

```pyxc
a = b = 4   # parsed as: a = (b = 4)
```

`b = 4` is evaluated first (storing 4 into `b` and producing 4), then `a = 4` stores that value into `a`. Both variables end up holding 4.

## Parentheses Are Transparent

Assignment in an expression context often needs parentheses to separate it from the surrounding expression. The parens do not change the lvalue — `(x)` resolves to the same `VariableExprAST` as `x`, so assignment through it works:

```pyxc
(x) = 2.0       # valid: same as x = 2.0
(p[i]) = val    # valid: same as p[i] = val
```

## Error Cases

**Non-lvalue target:**
```pyxc
(x + 1) = 3   # Error: Assignment target must be assignable
```

## Things Worth Knowing

**Parentheses are required when the assignment is not the top-level expression.** Without them the operator precedence is ambiguous to the reader even if the parser handles it:

```pyxc
while (c = getchar()) != EOF:   # clear: assign then compare
while c = getchar() != EOF:     # parses as: c = (getchar() != EOF) — probably not what you want
```

The second form assigns a `bool` to `c`. If `c` is `int32`, this is a type error. If `c` is `bool`, it compiles but reads `EOF` once and stops immediately. Always parenthesise the assignment when combining it with a comparison.

**Assignment expressions do not print in the REPL.** Assignments at the top level are side-effecting statements, not values to display. `x = 5` at the REPL prompt sets `x` silently, exactly as before.

**Compound assignment also works as an expression.** `(n -= 1)` produces the new value of `n`. This enables idioms like `while (n -= 1) > 0:` where the decrement and condition check fold into one expression.

## What's Next

Phase 5 is complete. pyxc now has the full K&R toolbox: signed and unsigned integer types, character literals, the complete set of operators, `if`/`elif`/`else`, `switch`, `while`, `do/while`, `break`, `continue`, assignment as expression, and direct C library interop via `extern`. The next phase adds modules and multi-file builds.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

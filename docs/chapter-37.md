---
description: "Add character literals so single characters can be written as 'a', '\n', '\t', and '\\' instead of their numeric ASCII values."
---
# 37. pyxc: Character Literals

## Where We Are

[Chapter 36](chapter-36.md) added `elif`. pyxc can call C library functions like `getchar()`, but comparing the result to a space or newline requires knowing the ASCII value off the top of your head:

```pyxc
if c == 32:   # space
if c == 10:   # newline — or was it 13?
```

After this chapter, you can write what you mean:

```pyxc
if c == ' ':
if c == '\n':
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-37
```

## Grammar

`charliteral` joins `primary`. A new `charescape` production lists the five recognised escape sequences.

```ebnf
primary     = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral
            | charliteral | identifierexpr | fieldaccess | indexexpr
            | numberexpr | bool_literal | parenexpr ;             -- changed
charliteral = "'" ( ? any char except ' and newline ? | charescape ) "'" ; -- new
charescape  = "\\" ( "\\" | "'" | "n" | "t" | "0" ) ;             -- new
```

### Full Grammar

`code/chapter-37/pyxc.ebnf`

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
assignstmt      = lvalue assignop expression ;
simplestmt      = returnstmt | breakstmt | continuestmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt | whilestmt | dowhilestmt | switchstmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" [ expression ] ;
breakstmt       = "break" ;
continuestmt    = "continue" ;
block           = indent statement { eols statement } dedent ;
expression      = unaryexpr binoprhs ;
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

## Lexer

The lexer recognises `'` as the start of a character literal. It reads the next character (or escape sequence), verifies the closing `'`, stores the integer value in `CharLiteralValue`, and returns `tok_char`.

The five supported escape sequences:

| Written | Value | Meaning |
|---|---|---|
| `'\\'` | 92 | backslash |
| `'\''` | 39 | single quote |
| `'\n'` | 10 | newline |
| `'\t'` | 9 | horizontal tab |
| `'\0'` | 0 | null byte |

Anything else after `\` is an error. An empty literal `''` is also an error.

## Type

A character literal is an integer constant, not a distinct type. It reuses `NumberExprAST` and defaults to `int32`, matching `getchar()`'s return type and C's `int`. If the surrounding context expects a narrower integer — `var c: int8 = 'A'` — the literal adopts that type, with a range check at parse time.

This means `'a'` and `97` are exactly the same thing to the compiler once parsed. Character literals are pure convenience — there is nothing to do in codegen that wasn't already there for integer literals.

## K&R Payoff

The classic "count blanks" loop from *The C Programming Language* now writes naturally in pyxc:

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

The `c == ' '` comparison uses the character literal added in this chapter. The `(c = getchar()) != EOF` pattern uses assignment-as-expression from [Chapter 39](chapter-39.md).

## Error Cases

**Invalid escape sequence:**
```pyxc
var x: int32 = '\x'  # Error: invalid character escape
```

**Empty literal:**
```pyxc
var x: int32 = ''    # Error: empty character literal
```

**Unterminated literal:**
```pyxc
var x: int32 = 'a    # Error: unterminated character literal
```

## Things Worth Knowing

**A character literal is just an integer.** `'a' + 1` is `98`. `'z' - 'a'` is `25`. Arithmetic on character values works exactly as it does in C.

**The default type is `int32`, not `int8`.** This matches `getchar()`, which returns `int32` to distinguish `EOF` (−1) from a valid byte (0–255). If you store the result of `getchar()` in an `int8`, values above 127 will be negative, which may not be what you want.

**No multi-character literals.** `'ab'` is not valid. Use string literals for strings.

## What's Next

[Chapter 38](chapter-38.md) adds unsigned integer types: `uint8`, `uint16`, `uint32`, and `uint64`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

---
description: "Add bitwise operators &, |, ^, <<, >> and unary ~ with C-standard precedence and integer-only type checking."
---
# 34. pyxc: Bitwise Operators

## Where We Are

[Chapter 33](chapter-33.md) completed the loop story. The last major gap before K&R-style systems programming is bitwise manipulation. After this chapter, flags, masks, and bit-shifting all work:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var flags: int = 0
  flags = flags | 1        # set bit 0
  flags = flags | 4        # set bit 2
  flags = flags & ~2       # clear bit 1 (already clear, but pattern works)

  var shifted: int = 1 << 3   # 8
  var masked: int = shifted & 0xFF

  printd(float64(flags + masked))
  return 0
```

```
13.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-34
```

## Grammar

`~` joins `unaryop`. The five bitwise binary operators join `builtinbinaryop`.

```ebnf
unaryop         = "-" | "!" | "~" | "++" | "--" | userdefunaryop ;  -- changed
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!="
                | "&&" | "||"
                | "&" | "|" | "^" | "<<" | ">>" ;                  -- changed
```

### Full Grammar

`code/chapter-34/pyxc.ebnf`

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
whilestmt       = "while" expression ":" suite ;
dowhilestmt     = "do" ":" suite eols "while" expression ;
forstmt         = "for"
                  ( "var" identifier ":" type | identifier )
                  "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = lvalue assignop expression ;
simplestmt      = returnstmt | breakstmt | continuestmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt | whilestmt | dowhilestmt ;
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

## Bitwise Binary Operators

All bitwise binary operators require integer operands on both sides. Float operands are a type error.

| Op | Name | LLVM instruction |
|---|---|---|
| `&` | bitwise AND | `and` |
| `\|` | bitwise OR | `or` |
| `^` | bitwise XOR | `xor` |
| `<<` | left shift | `shl` |
| `>>` | right shift | `ashr` (arithmetic) |

`<<` and `>>` are recognised by a lexer peek-ahead. When the lexer sees `<`, it checks the next character: `<=` becomes the comparison token, `<<` becomes `tok_shl`, anything else stays `<`. The same two-character disambiguation applies to `>`.

Right shift uses arithmetic (`ashr`) rather than logical (`lshr`). On x86 this sign-extends the value, which is the standard behaviour for signed integers in C.

## Unary Bitwise Not: `~`

`~x` flips every bit in an integer. The operand must be an integer type; applying `~` to a float or bool is a type error caught at parse time.

```pyxc
var x: int = 9        # ...0001001
var y: int = ~x       # ...1110110 (two's complement, so -10 as int64)
var z: int = y & 7    # 6
```

Codegen emits `CreateNot` on the integer value.

## Precedence

The complete binary operator precedence table, from lowest to highest:

| Operator | Precedence |
|---|---|
| `\|\|` | 5 |
| `&&` | 7 |
| `\|` | 10 |
| `^` | 11 |
| `&` | 12 |
| `==`, `!=` | 13 |
| `<`, `<=`, `>`, `>=` | 14 |
| `<<`, `>>` | 15 |
| `+`, `-` | 20 |
| `*`, `/`, `%` | 40 |

This matches C exactly. In particular, `&` sits below `==` and `!=` — so `a & b == 0` parses as `a & (b == 0)`, not `(a & b) == 0`. If you want the latter (and you usually do), add parentheses: `(a & b) == 0`.

## Error Cases

**Bitwise op on float:**
```pyxc
var x: float64 = 1.0
var y: float64 = 2.0
var z: float64 = x & y  # Error: Type mismatch in binary operator
```

**`~` on non-integer:**
```pyxc
var x: float64 = 1.0
var y: float64 = ~x  # Error: Unary '~' requires an integer operand
```

## Things Worth Knowing

**`&` and `|` are not `&&` and `||`.** The single-character forms are bitwise and operate on integers. The double-character forms are logical and operate on `bool` with short-circuit evaluation.

**Compound assignment works with bitwise operators.** `x &= mask`, `flags |= bit`, `x ^= pattern`, `x <<= 2` and `x >>= 1` are all valid.

**Right shift is arithmetic (sign-extending).** For a negative `int`, `x >> 1` fills the high bit with the sign bit. pyxc does not have unsigned integer types, so logical shift right is not directly available.

**The C precedence gotcha.** `a & b == 0` means `a & (b == 0)` in both C and pyxc. Put parentheses around the operands you want grouped: `(a & b) == 0`.

## What's Next

[Chapter 35](chapter-35.md) adds `switch` statements.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

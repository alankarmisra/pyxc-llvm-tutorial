---
description: "Add unsigned integer types uint8, uint16, uint32, and uint64 with correct unsigned arithmetic, comparisons, and casts throughout."
---
# 38. pyxc: Unsigned Integer Types

## Where We Are

[Chapter 37](chapter-37.md) added character literals. pyxc has had signed integers since [Chapter 16](chapter-16.md), but all of them — `int8`, `int16`, `int32`, `int64`, and `int` — interpret their top bit as a sign. Sizes, counts, and bit masks are commonly stored as unsigned values in systems code, and without unsigned types the compiler has no way to generate the right instructions for them.

After this chapter, `uint8`, `uint16`, `uint32`, and `uint64` are available:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var flags: uint32 = 0
  flags |= uint32(1) << uint32(3)   # set bit 3
  flags |= uint32(1) << uint32(7)   # set bit 7

  var mask: uint32 = uint32(0xFF)
  printd(float64(flags & mask))     # 136.000000
  return 0
```

```
136.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-38
```

## Grammar

The four unsigned keywords join `builtintype` and `casttype`.

```ebnf
builtintype = "int" | "int8" | "int16" | "int32" | "int64"
            | "uint8" | "uint16" | "uint32" | "uint64"   -- changed
            | "float" | "float32" | "float64"
            | "bool" | "None" ;
casttype    = "int" | "int8" | "int16" | "int32" | "int64"
            | "uint8" | "uint16" | "uint32" | "uint64"   -- changed
            | "float" | "float32" | "float64"
            | "bool" | pointertype ;
```

### Full Grammar

`code/chapter-38/pyxc.ebnf`

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

## No New IR Types

LLVM has no separate "unsigned integer" types. `uint32` and `int32` are both `i32` in the IR. The signedness lives entirely in the *instructions* the compiler chooses. This chapter adds no new LLVM types — it adds new rules for which instruction to emit.

The four unsigned types and the instructions they change:

| Operation | Signed instruction | Unsigned instruction |
|---|---|---|
| Division `/` | `sdiv` | `udiv` |
| Modulo `%` | `srem` | `urem` |
| Right shift `>>` | `ashr` (arithmetic, sign-extends) | `lshr` (logical, zero-fills) |
| Comparison `<` `<=` `>` `>=` | `icmp slt/sle/sgt/sge` | `icmp ult/ule/ugt/uge` |
| Integer → float | `sitofp` | `uitofp` |
| Float → integer | `fptosi` | `fptoui` |
| Integer widening | `sext` | `zext` |

`==` and `!=` are signedness-agnostic (`icmp eq` / `icmp ne`); the same instruction is used for both.

## Signedness and Comparisons

The difference between `icmp sgt` and `icmp ugt` matters whenever the top bit is set. Consider `uint32(-1)` — the bit pattern `0xFFFFFFFF`, which is 4294967295 as an unsigned value. Compared to 0:

```pyxc
var a: uint32 = uint32(-1)   # 4294967295
var b: int32  = -1

# unsigned comparison: 4294967295 > 0 → true
if a > uint32(0): printd(1.0)    # prints 1.000000

# signed comparison: -1 > 0 → false
if b > int32(0): printd(1.0)     # does not print
```

The compiler dispatches to `icmp ugt` for `a > uint32(0)` and `icmp sgt` for `b > int32(0)` based on the operand types.

## Implicit Widening Rules

Implicit integer widening is allowed only between types of the same signedness:

- `uint8` → `uint16` → `uint32` → `uint64` ✓
- `int8` → `int16` → `int32` → `int64` ✓
- `uint32` → `int64` ✗ (implicit signed/unsigned mix rejected)
- `int32` → `uint32` ✗

Mixing signed and unsigned requires an explicit cast. This is deliberate — implicit conversion between signed and unsigned is a well-known source of bugs in C, and pyxc won't do it silently.

## Explicit Casts

Explicit casts between signed and unsigned types are always allowed:

```pyxc
var x: int32  = -1
var y: uint32 = uint32(x)   # reinterprets the bit pattern: 4294967295
var z: int32  = int32(y)    # back to -1
```

Same bit width: the bits are unchanged, only the interpretation changes. Narrowing (e.g. `uint64` → `uint32`) truncates to the low bits.

## Error Cases

**Implicit signed/unsigned mix:**
```pyxc
var a: uint32 = 1
var b: int32  = 2
a = a + b   # Error: Type mismatch
```

Cast explicitly: `a = a + uint32(b)`.

## Things Worth Knowing

**`uint64(-1)` is `18446744073709551615`.** Converting it to `float64` rounds up to `2^64` because `float64` can only represent integers exactly up to `2^53`. If you need to print a large `uint64` value, stay in integer arithmetic rather than converting to float.

**Right shift is always logical for unsigned types.** `uint32(-1) >> 1` fills the vacated high bit with zero, giving `2147483647`. For signed types, `>>` is arithmetic (sign-extending). This is why the type matters for shift operations even though `>>` is a single operator.

**`size_t` maps to `uint64` on 64-bit targets.** When calling C functions that take or return `size_t`, declare the parameter as `uint64` (or `uint32` on 32-bit targets). The convention follows C's ABI.

## What's Next

[Chapter 39](chapter-39.md) allows assignment to appear inside an expression — enabling the `while (c = getchar()) != EOF` pattern from K&R.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

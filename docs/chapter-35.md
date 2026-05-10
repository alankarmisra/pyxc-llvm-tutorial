---
description: "Add switch statements with integer case matching, an optional default, break support, and no implicit fallthrough."
---
# 35. pyxc: Switch

## Where We Are

[Chapter 34](chapter-34.md) added bitwise operators. Multi-way branching on an integer value is currently done with chains of `if`/`else if`. After this chapter, `switch` is available:

```pyxc
extern def printd(x: float64)

def day_type(d: int) -> int:
  var result: int = 0
  switch d:
    case 0:
      result = 2   # Sunday
    case 6:
      result = 2   # Saturday
    default:
      result = 1   # weekday
  return result

def main() -> int:
  printd(float64(day_type(0) + day_type(3) + day_type(6)))
  return 0
```

```
5.000000
```

`switch` dispatches to the matching `case` and stops there. There is no fallthrough.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-35
```

## Grammar

`switchstmt` is a new compound statement. The case value is a signed integer literal.

```ebnf
switchstmt   = "switch" expression ":" eols indent switchbody dedent ;  -- new
switchbody   = switchcase { eols switchcase } [ eols defaultcase ] ;     -- new
switchcase   = "case" switchint ":" suite ;                              -- new
defaultcase  = "default" ":" suite ;                                     -- new
switchint    = [ "-" ] integer ;                                          -- new
compoundstmt = ifstmt | forstmt | whilestmt | dowhilestmt | switchstmt ; -- changed
```

`switchint` accepts an optional leading `-`, so negative case values are valid literals.

### Full Grammar

`code/chapter-35/pyxc.ebnf`

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

Three new keywords: `switch`, `case`, `default`.

`ParseSwitchStmt` reads the switch expression, verifies it is an integer type, then enters an `INDENT`/`DEDENT` block. Inside, it loops consuming `case` and `default` branches until the closing `DEDENT`.

Duplicate case values are rejected at parse time using a `std::set<int64_t>` of seen values. Multiple `default` clauses are also rejected at parse time.

Case values are parsed by `ParseSwitchCaseValue`, which accepts an optional leading `-` and checks that the literal fits in `int64_t`.

## Codegen

`SwitchExprAST` stores the condition, a vector of `(int64_t, body)` pairs, and an optional default body.

Codegen uses LLVM's `CreateSwitch` instruction, which maps directly to the machine's multi-way branch. Each case gets a dedicated basic block (`switch.case`). The default block, if present, is `switch.default`; if absent, the switch falls through to `switch.after`.

```
switch.case.0:  body for case 0 → branch to switch.after
switch.case.1:  body for case 1 → branch to switch.after
switch.default: default body    → branch to switch.after
switch.after:   execution continues here
```

Each case body is codegen'd into its own block. If the body does not end with a terminator (`return`, `break`), an implicit branch to `switch.after` is appended.

## No Fallthrough

In C, switch cases fall through to the next case unless `break` is written. In pyxc, each case implicitly breaks. There is no way to fall through to the next case.

This removes a well-known class of C bugs and makes `switch` safer by default. The trade-off is that you cannot use the C idiom of stacking empty cases to share a body:

```c
/* C only — not valid in pyxc */
switch (x) {
  case 1:
  case 2:
    handle_both();
    break;
}
```

In pyxc, write two cases with the same body, or extract the shared logic into a function.

## `break` in a Switch

`break` inside a `switch` exits the switch, exactly as in C. The `BreakTargetStack` introduced in [Chapter 33](chapter-33.md) handles this: the switch pushes its `switch.after` block as the break target. `break` inside a case branches there.

`break` outside any loop or switch is a parse-time error.

`continue` has no meaning inside a switch. If the switch is nested inside a loop, `continue` still refers to the enclosing loop — the `LoopControlStack` is not touched by the switch.

## Error Cases

**Non-integer switch condition:**
```pyxc
var x: float64 = 1.0
switch x:           # Error: Switch condition must be an integer type
  case 1:
    return 1
```

**Duplicate case value:**
```pyxc
switch x:
  case 1:
    return 1
  case 1:           # Error: Duplicate switch case value
    return 2
```

## Things Worth Knowing

**Case values are compile-time integer literals only.** You cannot use a variable or expression as a case value. This matches C's `switch` restriction and allows LLVM to emit an efficient branch table.

**Negative case values are supported.** `case -1:` is valid. `switchint` accepts a leading minus sign.

**LLVM emits a real `switch` instruction.** The IR contains `switch i64`, not a chain of comparisons. The backend is free to lower it to a jump table, a binary search, or a comparison chain depending on the density and count of cases.

**`default` is optional.** If no case matches and there is no `default`, execution continues after the switch with no action taken.

## What's Next

Phase 5 — K&R compatibility — is now complete. pyxc has `while`, `do/while`, `break`, `continue`, `switch`, the full set of arithmetic and bitwise operators, `++`/`--`, compound assignment, logical operators, and the C memory model from Chapters 17–22. The next phase brings modules, imports, and the standard library.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

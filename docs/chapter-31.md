---
description: "Complete pyxc's arithmetic: add / and %, five compound assignment operators, and prefix/postfix ++/-- for all lvalue shapes."
---
# 31. pyxc: Arithmetic Completeness

## Where We Are

[Chapter 30](chapter-30.md) finished the object model. Before moving further, there is a gap worth closing: pyxc has `+`, `-`, and `*` but not `/` or `%`. Compound assignment (`+=`, `*=` etc.) does not exist. Neither do `++` and `--`. After this chapter, all of that works:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var a: int = 17
  var b: int = 4
  var q: int = a / b
  var r: int = a % b

  var x: int = 10
  x += 5
  x -= 3
  x *= 2
  x /= 4
  x %= 10

  var i: int = 0
  i++
  ++i

  printd(float64(q + r + x + i))
  return 0
```

```
14.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-31
```

## Grammar

Three areas of the grammar change.

`assignop` replaces the bare `=` in `assignstmt`, now accepting any of the six assignment operators. `postfixexpr` is inserted between `unaryexpr` and `primary` to capture postfix `++`/`--`. `builtinbinaryop` gains `/` and `%`.

```ebnf
assignstmt      = lvalue assignop expression ;           -- changed
assignop        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;  -- new
unaryexpr       = unaryop unaryexpr | postfixexpr ;      -- changed
unaryop         = "-" | "++" | "--" | userdefunaryop ;   -- changed
postfixexpr     = primary [ postfixop ] ;                -- new
postfixop       = "++" | "--" ;                          -- new
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!=" ;  -- changed
```

### Full Grammar

`code/chapter-31/pyxc.ebnf`

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
unaryop         = "-" | "++" | "--" | userdefunaryop ;
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
                | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
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

## Division and Remainder

`/` and `%` share precedence 40 with `*` — they bind as tightly as multiplication. The LLVM instructions differ by type:

| Op | Int | Float |
|---|---|---|
| `/` | `sdiv` | `fdiv` |
| `%` | `srem` | error |

`%` on float operands is a type error. There is no floating-point remainder operator in pyxc.

All five arithmetic operators route through a shared helper, `EmitBuiltInArithmetic`, which is called from both `BinaryExprAST::codegen` and every compound assignment node. The helper handles integer widening, float promotion, and pointer arithmetic in one place.

## Compound Assignment

Five new tokens (`+=`, `-=`, `*=`, `/=`, `%=`) are recognised by a lexer peek-ahead on `+`, `-`, `*`, `/`, and `%`. Each compound assignment desugars at codegen time into a load, an `EmitBuiltInArithmetic` call, and a store — sharing the same arithmetic path as the binary expression form.

There are four AST node types, one for each lvalue shape:

| Node | Lvalue shape |
|---|---|
| `CompoundAssignmentExprAST` | plain variable |
| `FieldCompoundAssignmentExprAST` | struct field (`p.x += 2`) |
| `IndexCompoundAssignmentExprAST` | array index (`arr[i] *= 3`) |
| `IndexedFieldCompoundAssignmentExprAST` | indexed struct field |

The four nodes share the same codegen pattern: resolve the lvalue to a pointer, load, compute, store.

## Increment and Decrement

`++` and `--` work on any assignable lvalue: variables, struct fields, array elements, and indexed struct fields. The operand type must be numeric or a pointer.

A single `IncDecExprAST` handles all four variants (prefix/postfix × increment/decrement) via two flags:

```cpp
class IncDecExprAST : public ExprAST {
  unique_ptr<ExprAST> Operand;
  bool IsIncrement;
  bool IsPrefix;
  ...
};
```

Codegen: load the old value, compute `old ± 1` via `EmitBuiltInArithmetic`, store the new value, then return `IsPrefix ? new : old`. The postfix form returns the value that existed *before* the mutation — the same semantics as C.

`ResolveIncDecLValuePtr` resolves the operand to an LLVM pointer for all four lvalue shapes, exactly like compound assignment.

## Error Cases

**`%` on float:**
```pyxc
var x: float64 = 5.5
x %= 2.0  # Error: Type mismatch in assignment
```

**`++` on a non-lvalue:**
```pyxc
++(1 + 2)  # Error: Increment/decrement target must be assignable
```

## Things Worth Knowing

**`EmitBuiltInArithmetic` is the single implementation path.** Both `BinaryExprAST` and every compound assignment node call it. Adding a new arithmetic operator in the future means touching one function.

**Postfix `++` returns the old value.** `var y: int = x++` captures the value before the increment. This is identical to C.

**`++`/`--` work on pointers.** `p++` advances by one element, the same as `p += 1`. Pointer arithmetic rules apply.

## What's Next

[Chapter 32](chapter-32.md) adds `&&`, `||`, and `!` — logical operators with short-circuit evaluation.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

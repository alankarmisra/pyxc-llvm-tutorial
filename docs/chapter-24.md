---
description: "Add the class keyword as a distinct aggregate type. Classes share struct IR layout but carry an IsClass flag that unlocks methods, constructors, and visibility in subsequent chapters."
---
# 24. pyxc: Classes

## Where We Are

[Chapter 23](chapter-23.md) added arrays. We now have a decent type system, but the only aggregate type is `struct`. After this chapter, the `class` keyword is available:

```pyxc
class Point:
  x: int
  y: int

def main() -> int:
  var p: Point
  p.x = 3
  p.y = 4
  return 0
```

On its own, `class` behaves identically to `struct` — same field layout, same IR, same field access syntax. The difference lives inside the compiler: a class sets `IsClass = true` in `StructTypeInfo`. That flag is what the next three chapters gate everything on.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-24
```

## Grammar

This chapter adds one new production and extends `top`.

```ebnf
top      = typealias | structdef | classdef | definition | decorateddef | external | toplevelexpr ;  -- changed
classdef = "class" identifier ":" eols structblock ;  -- new
```

`classdef` and `structdef` share the same body grammar (`structblock`). The only syntactic difference is the keyword.

### Full Grammar

`code/chapter-24/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | structdef | classdef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
structdef       = "struct" identifier ":" eols structblock ;
classdef        = "class" identifier ":" eols structblock ;
structblock     = indent fielddecl { eols fielddecl } dedent ;
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
assignstmt      = lvalue "=" expression ;
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
unaryexpr       = unaryop unaryexpr | primary ;
unaryop         = "-" | userdefunaryop ;
primary         = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
castexpr        = casttype "(" expression ")" ;
sizeofexpr      = "sizeof" "(" type ")" ;
addrexpr        = "addr" "(" lvalue ")" ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
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

builtinbinaryop = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
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

## New Keyword: `class`

```cpp
tok_class = -40,
```

Registered in the keyword table:

```cpp
{"class", tok_class}
```

## One Parser, Two Keywords

Both `struct` and `class` share the same parser function: `ParseAggregateDefinition`. The caller passes `"struct"` or `"class"` as a string; the parser uses it only for error messages and to set the `IsClass` flag:

```cpp
static bool ParseAggregateDefinition(const char *KindName) {
  // CurTok is tok_struct or tok_class
  getNextToken(); // eat keyword
  // ... parse name, ':', INDENT, fields, DEDENT ...
  Info.IsClass = (strcmp(KindName, "class") == 0);
  StructTypes[StructName] = std::move(Info);
  return true;
}
```

`HandleStructDef` calls it with `"struct"`, `HandleClassDef` with `"class"`. The dispatch loop calls the right handler based on the token.

## The `IsClass` Flag

`StructTypeInfo` gains a boolean field:

```cpp
struct StructTypeInfo {
  string Name;
  bool IsClass = false;      // new
  vector<FieldInfo> Fields;
  // ...
};
```

This flag is checked wherever a feature is class-specific — methods in chapter 25, constructors in chapter 26, visibility in chapter 27, trait conformance in chapter 28. A `struct` hitting those paths produces an error.

## IR Layout

A class has exactly the same IR layout as a struct with the same fields. There is nothing in the generated LLVM IR that distinguishes a `class Point` from a `struct Point`. The distinction is purely a parser-level concept.

```pyxc
class Vec2:
  x: float64
  y: float64
```

```llvm
%Vec2 = type { double, double }
```

## Conflict Rules

Class names and struct names share the same namespace (`StructTypes`). You cannot define a class and a struct with the same name, in either order:

```pyxc
struct Foo:
  x: int

class Foo:   # Error: Aggregate 'Foo' is already defined
  y: int
```

Type alias names also conflict: a class name that collides with an existing type alias, or vice versa, is rejected.

## Build and Run

```bash
cd code/chapter-24
cmake -S . -B build && cmake --build build
```

## What's Next

[Chapter 25](chapter-25.md) adds methods — functions defined inside a class body and called with `obj.method(args)`. The `IsClass` flag gates all of this: structs do not get methods.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

---
description: "Add the class keyword as a distinct aggregate type. Classes share struct IR layout but carry an IsClass flag that unlocks methods, constructors, and visibility in subsequent chapters."
---
# 25. pyxc: Classes

## Where We Are

[Chapter 24](chapter-24.md) added arrays. We now have a decent type system, but the only aggregate type is `struct`. After this chapter, the `class` keyword is available:

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
cd pyxc-llvm-tutorial/code/chapter-25
```

## Grammar

This chapter adds one new production and extends `top`.

```ebnf
top      = typealias | structdef | classdef | definition | decorateddef | external | toplevelexpr ;  -- changed
classdef = "class" identifier ":" eols structblock ;  -- new
```

`classdef` and `structdef` share the same body grammar (`structblock`). The only syntactic difference is the keyword.

### Full Grammar

`code/chapter-25/pyxc.ebnf`

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

## New Token

```cpp
tok_class = -40,
```

Registered in the keyword table alongside `tok_struct`:

```cpp
{"struct", tok_struct},
{"class",  tok_class},
```

`tok_class` is also added to the token name map so error messages print `'class'` rather than a raw integer.

## One Parser, Two Keywords — `ParseAggregateDefinition`

Previously the struct parser was `ParseStructDefinition()`. This chapter replaces it with a single function that handles both keywords. The caller passes `"struct"` or `"class"` as a string, and the parser uses it only to:

1. Produce readable error messages (`"Expected struct name"` vs `"Expected class name"`)
2. Set the `IsClass` flag (see Group 3)

```cpp
static bool ParseAggregateDefinition(const char *KindName) {
  // CurTok is tok_struct or tok_class
  getNextToken(); // eat keyword
  if (CurTok != tok_identifier)
    return LogError((string("Expected ") + KindName + " name").c_str());
  string StructName = IdentifierStr;

  // Check for redefinition
  if (StructTypes.count(StructName))
    return LogError(("Aggregate '" + StructName + "' is already defined").c_str());

  getNextToken(); // eat aggregate name
  // ... parse ':', INDENT, fields, DEDENT ...

  Info.IsClass = (strcmp(KindName, "class") == 0);
  StructTypes[StructName] = std::move(Info);
  return true;
}
```

All error strings are parameterised on `KindName`, so a mis-formed class body reports `"Expected dedent after class body"` instead of the generic struct message.

`HandleStructDef` and `HandleClassDef` each call this with `"struct"` or `"class"`:

```cpp
static void HandleStructDef() {
  bool Ok = ParseAggregateDefinition("struct");
  if (!Ok) { SynchronizeToLineBoundary(); return; }
  // check for trailing tokens on the same line
}

static void HandleClassDef() {
  bool Ok = ParseAggregateDefinition("class");
  // same error recovery
}
```

`HandleStructDef` now also includes improved error recovery — if parsing succeeds but there are unexpected tokens on the same line, it logs an error and synchronises to the line boundary.

The dispatch loops in `MainLoop` and `FileModeLoop` add a `tok_class` case:

```cpp
case tok_class:
  HandleClassDef();
  break;
```

## The `IsClass` Flag

`StructTypeInfo` gains one boolean field:

```cpp
struct StructTypeInfo {
  string Name;
  bool IsClass = false;      // new
  vector<FieldInfo> Fields;
  // ...
};
```

`ParseAggregateDefinition` sets `IsClass = true` when `KindName == "class"`, false otherwise. This flag is the sole distinction between structs and classes in `StructTypes`. It does nothing yet in chapter 24 — but chapter 25 checks it before parsing methods, chapter 26 checks it for constructors, and so on.

## IR Layout

A class has exactly the same IR layout as a struct with the same fields. The LLVM type system uses a `"struct."` prefix for the named aggregate type for both source-level `struct` and `class` — a comment in the code makes this explicit:

```cpp
// LLVM named aggregate types use a conventional "struct." prefix here for
// both source-level 'struct' and 'class' in chapter 24. They are layout-
// equivalent at this stage; chapter 25 can layer semantic distinctions.
```

```pyxc
class Vec2:
  x: float64
  y: float64
```

```llvm
%Vec2 = type { double, double }
```

There is nothing in the generated LLVM IR that distinguishes a `class Vec2` from a `struct Vec2`. The distinction is a compile-time concept only.

## Conflict Rules

Class names and struct names share the same namespace (`StructTypes`). Defining a class and a struct with the same name, in either order, is rejected:

```pyxc
struct Foo:
  x: int

class Foo:   # Error: Aggregate 'Foo' is already defined
  y: int
```

Type alias names also conflict: a class name that collides with an existing type alias, or vice versa, is rejected with `"Name '...' is already defined as an aggregate type"`.

## Build and Run

```bash
cd code/chapter-25
cmake -S . -B build && cmake --build build
```

## What's Next

[Chapter 26](chapter-26.md) adds methods — functions defined inside a class body and called with `obj.method(args)`. The `IsClass` flag gates all of this: structs do not get methods.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

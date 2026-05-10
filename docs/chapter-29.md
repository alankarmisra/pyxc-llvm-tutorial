---
description: "Add impl blocks: implement a trait for an existing class after the class definition, separating the class layout from its trait conformance."
---
# 29. pyxc: impl Blocks

## Where We Are

[Chapter 28](chapter-28.md) added traits. A class declares the traits it implements in its header, and the compiler verifies conformance when the class body closes. That works well when you write both the trait and the class together, but what if you want to implement a standard trait on a class that was already written — or implement two different traits on the same class in separate places?

After this chapter, trait conformance can be declared outside the class body entirely:

```pyxc
extern def printd(x: float64)

trait Adder:
  def add(x: int, y: int) -> int

class Calc:
  public bias: int

impl Adder for Calc:
  def add(x: int, y: int) -> int:
    return x + y + self.bias


def main() -> int:
  var c: Calc = Calc()
  c.bias = 5
  printd(float64(c.add(3, 4)))
  return 0
```

```
12.000000
```

The methods defined in the `impl` block (`add` here) become regular methods on `Calc`. They are callable with `c.add(...)` just like any other method.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-29
```

## Grammar

This chapter adds one new production and extends `top`.

```ebnf
top       = typealias | traitdef | structdef | classdef | impldef | definition | ...  -- changed
impldef   = "impl" identifier "for" identifier ":" eols implblock ;  -- new
implblock = indent implmethod { eols implmethod } dedent ;           -- new
implmethod = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ":" ( simplestmt | eols block ) ;  -- new
```

`implmethod` has a full body, unlike `traitmethodsig` which does not. Methods in an `impl` block are fully defined here.

### Full Grammar

`code/chapter-29/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | traitdef | structdef | classdef | impldef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
traitdef        = "trait" identifier ":" eols traitblock ;
traitblock      = indent traitmethodsig { eols traitmethodsig } dedent ;
traitmethodsig  = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ;
structdef       = "struct" identifier ":" eols structblock ;
classdef        = "class" identifier [ "(" identifier { "," identifier } ")" ] ":" eols structblock ;
impldef         = "impl" identifier "for" identifier ":" eols implblock ;
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

## New Keyword: `impl`

```cpp
tok_impl = -44,
```

## How `impl` Works

`ParseImplDefinition` validates and executes the block in three stages:

**1. Validate the header.** The trait name must already exist in `Traits`. The class name must already exist in `StructTypes` and must be a class (not a struct). Implementing a trait that is already implemented for this class is rejected.

**2. Parse and compile the methods.** Each `def` in the body is parsed with `ParseMethodDefinitionInClass(ClassName, /*IsPublic=*/true)`. The `IsPublic=true` argument is not optional — all methods in an `impl` block are public, because satisfying a trait contract is a public commitment.

**3. Verify conformance.** After the body closes, `VerifyTraitConformance` checks the same three things as in chapter 28: the method exists, is public, and has the right signature.

## `VerifyTraitConformance` as a Shared Function

In chapter 28, the conformance check lived inside `ParseAggregateDefinition`. This chapter extracts it into a standalone function so both paths can call it:

```cpp
static bool VerifyTraitConformance(const string &ClassName,
                                   const string &TraitName);
```

`ParseAggregateDefinition` calls it at the end of the class body. `ParseImplDefinition` calls it at the end of the impl body. The logic is identical; the function is just shared.

## Methods Defined in `impl` Are Regular Methods

There is no runtime distinction between a method defined in the class body and one defined in an `impl` block. Both are stored in `FunctionProtos` under the mangled name `ClassName.MethodName` and emitted as `@ClassName.MethodName` in the IR. A caller cannot tell where the method was defined.

## Things Worth Knowing

**The trait must be defined before the `impl`.** `impl Adder for Calc:` requires that `Adder` is already in scope. Forward references are not supported.

**The class must be defined before the `impl`.** `impl Adder for Ghost:` where `Ghost` does not yet exist is an error.

**Implementing a trait on a struct is rejected.** Traits can only be implemented on classes. A struct named `S` gives: `'S' is a struct, not a class; traits can only be implemented on classes`.

**`impl` cannot be used twice for the same trait/class pair.** `impl Adder for Calc:` a second time is rejected with "Trait 'Adder' is already implemented for class 'Calc'".

## What's Next

[Chapter 30](chapter-30.md) adds type parameters to traits — `trait Addable[T]:` — so the same contract can be expressed for different element types.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

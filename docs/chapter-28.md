---
description: "Add traits: named method-signature contracts that a class declares it satisfies. Conformance is verified at compile time with no runtime overhead."
---
# 28. pyxc: Traits

## Where We Are

[Chapter 27](chapter-27.md) added visibility. Classes can now hide implementation details. But there is no way to say "this class promises to have these methods" — no interface contract, no way to write code that works against any class satisfying a given shape.

After this chapter:

```python
extern def printd(x: float64)

trait Measurable:
  def area() -> int

class Rect(Measurable):
  public w: int
  public h: int

  def __init__(w: int, h: int):
    self.w = w
    self.h = h

  public def area() -> int:
    return self.w * self.h


def main() -> int:
  var r: Rect = Rect(3, 4)
  printd(float64(r.area()))
  return 0
```

```
12.000000
```

If `Rect` does not implement `area`, or implements it with the wrong signature, the compiler reports an error before any code is generated.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-28
```

## Grammar

This chapter adds two new productions (`traitdef`, `traitblock`, `traitmethodsig`) and extends `top` and `classdef`.

```ebnf
top            = typealias | traitdef | structdef | classdef | ...  -- changed
traitdef       = "trait" identifier ":" eols traitblock ;           -- new
traitblock     = indent traitmethodsig { eols traitmethodsig } dedent ;  -- new
traitmethodsig = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ;  -- new
classdef       = "class" identifier [ "(" identifier { "," identifier } ")" ] ":" eols structblock ;  -- changed
```

`traitmethodsig` looks like a method definition but has no body and no `self` parameter. The `classdef` gains an optional parenthesised list of trait names after the class name.

### Full Grammar

`code/chapter-28/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | traitdef | structdef | classdef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
traitdef        = "trait" identifier ":" eols traitblock ;
traitblock      = indent traitmethodsig { eols traitmethodsig } dedent ;
traitmethodsig  = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ;
structdef       = "struct" identifier ":" eols structblock ;
classdef        = "class" identifier [ "(" identifier { "," identifier } ")" ] ":" eols structblock ;
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

## New Keyword: `trait`

```cpp
tok_trait = -43,
```

Registered in the keyword table. `trait` definitions appear at the top level, before any class that implements them.

## Defining a Trait

A trait is a named list of method signatures. No bodies, no fields, no `self` — just names, parameter types, and return types:

```python
trait Adder:
  def add(x: int, y: int) -> int

trait Printable:
  def label() -> ptr[int8]
```

`ParseTraitDefinition` reads the trait name, validates it does not clash with existing traits, struct types, or type aliases, then parses the body. Each signature in the body is stored as a `TraitMethodSig`:

```cpp
struct TraitMethodSig {
  string Name;
  vector<PrototypeAST::ArgInfo> Args;
  ValueType ReturnType;
  string ReturnStructName;
};
```

The `self` parameter is not listed in a trait signature. It is always implied — every implementing method will have `self` injected at position 0.

## Declaring Conformance

A class declares which traits it implements in the class header:

```python
class Calc(Adder):
  public def add(x: int, y: int) -> int:
    return x + y
```

Multiple traits:

```python
class Calc(Adder, Scaler):
  public def add(x: int, y: int) -> int: ...
  public def scale(x: int, factor: int) -> int: ...
```

The trait names in the parentheses must refer to already-defined traits. Forward references are not allowed.

## Conformance Verification

When the class body ends (at the closing DEDENT), the compiler walks each declared trait and verifies that the class satisfies it. For each method signature in the trait:

1. **The method must exist.** `ClassName.MethodName` must be in `FunctionProtos`.
2. **The method must be public.** Private trait methods are rejected — a trait contract is a public interface.
3. **The signature must match.** Parameter types and return type must agree exactly, accounting for the implicit `self` at position 0.

```
class Bad(Adder):
  public def add(x: int, y: float64) -> int:  # wrong: y should be int
    return x

# Error: Method 'add' on class 'Bad' does not match trait signature
```

If any check fails, the compiler reports an error. No code is generated for that class.

## What Traits Are Not

There is no dynamic dispatch. There is no vtable. The trait check is purely structural: it verifies that the method exists with the right signature and is public. The generated IR is identical to what you would get without the trait — trait methods are just regular LLVM functions.

There is no way in this chapter to pass a `Measurable` to a function without knowing the concrete type. Traits are a documentation and enforcement mechanism, not a polymorphism mechanism. Dynamic dispatch comes later.

## Things Worth Knowing

**Traits must be defined before the classes that implement them.** The trait name lookup happens at parse time when the class header is read; if the trait does not exist yet, it is an error.

**A class can implement multiple traits.** The list in the class header is comma-separated. Each trait is checked independently. Listing the same trait twice is an error.

**Trait methods cannot have bodies.** Writing `:` after the signature to begin a body gives a parse error: "Trait methods cannot have a body".

**Structs cannot implement traits.** The `(Trait)` syntax is only valid on `class` definitions.

## What's Next

[Chapter 29](chapter-29.md) adds `impl` blocks — a way to implement a trait for a class outside the class definition, after the fact.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

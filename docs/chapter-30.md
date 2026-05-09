---
description: "Add type parameters to traits: trait Addable[T] declares a contract over an abstract type, and classes instantiate it with a concrete type at the impl site."
---
# 30. pyxc: Generic Traits

## Where We Are

[Chapter 29](chapter-29.md) added `impl` blocks. A trait is still limited to concrete types — `trait Adder` specifies `int` parameters explicitly. After this chapter, a trait can name an abstract type parameter and leave the concrete type to be supplied by each implementor:

```python
extern def printd(x: float64)

trait Addable[T]:
  def add(x: T, y: T) -> T

class Calc:
  public bias: int

impl Addable[int] for Calc:
  def add(x: int, y: int) -> int:
    return x + y + self.bias


def main() -> int:
  var c: Calc = Calc()
  c.bias = 2
  printd(float64(c.add(4, 5)))
  return 0
```

```
11.000000
```

`Addable[int]` and `Addable[float64]` are separate contracts. A class can implement both — with separate `impl` blocks for each instantiation.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-30
```

## Grammar

`traitdef` gains an optional type parameter. `classdef` and `impldef` use `traitref` (a trait name plus an optional type argument) wherever they previously used a bare identifier.

```ebnf
traitdef  = "trait" identifier [ "[" identifier "]" ] ":" eols traitblock ;  -- changed
classdef  = "class" identifier [ "(" traitref { "," traitref } ")" ] ":" eols structblock ;  -- changed
traitref  = identifier [ "[" type "]" ] ;  -- new
impldef   = "impl" traitref "for" identifier ":" eols implblock ;  -- changed
```

`traitref` is the common notation used everywhere a trait is referenced with a possible type argument: in the class header, in the `impl` header.

### Full Grammar

`code/chapter-30/pyxc.ebnf`

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

## Defining a Generic Trait

A trait with a type parameter uses `[T]` after the trait name:

```python
trait Addable[T]:
  def add(x: T, y: T) -> T
```

`T` is a placeholder. Inside the trait body, every occurrence of `T` as a type name is recorded as `ValueType::TypeVar` rather than being resolved to a concrete type. The name `T` does not need to be anything specific — it is just a local label inside the trait body.

During `ParseTraitDefinition`, the parameter name is stored in `TraitInfo::TypeParamName`. While the trait body is being parsed, the name is active in a global set (`ActiveTypeParams`). `ParseTypeToken` checks this set before treating an identifier as an unknown type — if the name is an active type parameter, it returns `ValueType::TypeVar` and records the parameter name as the struct name.

The active set is cleared when the trait body closes, so `T` has no meaning outside the trait definition.

## Implementing a Generic Trait

At the `impl` site, the concrete type argument fills in the placeholder:

```python
impl Addable[int] for Calc:
  def add(x: int, y: int) -> int:
    return x + y
```

The concrete type (`int` here) is stored alongside the trait name in an `ImplTraitRef`:

```cpp
struct ImplTraitRef {
  string TraitName;
  bool HasTypeArg = false;
  ValueType TypeArg = ValueType::Error;
  string TypeArgStructName;
};
```

## Conformance with Type Substitution

When `VerifyTraitConformance` runs, it has both the trait's abstract signatures (with `TypeVar` placeholders) and the concrete type from the `ImplTraitRef`. A `ResolveReq` lambda substitutes the placeholder:

```cpp
auto ResolveReq = [&](ValueType T, const string &S) -> std::pair<ValueType, string> {
  if (T == ValueType::TypeVar && S == TI.TypeParamName)
    return {ImplRef.TypeArg, ImplRef.TypeArgStructName};
  return {T, S};
};
```

For each method in the trait, every parameter type and the return type pass through `ResolveReq` before being compared against the class's actual method signature. If the trait says `T` and the impl says `int`, and `T` resolves to `int`, they match. If they do not match, the error is the same as for a concrete trait mismatch.

## Class Header with Type Argument

The `class Foo(Addable[int]):` form also works. The type argument is parsed and stored in the `ImplTraitRef` immediately at the class header:

```python
class IntCalc(Addable[int]):
  public def add(x: int, y: int) -> int:
    return x + y
```

Conformance is checked at the end of the class body, exactly as in chapter 28, but now with the type argument available for substitution.

## Error Cases

**Missing type argument on a generic trait:**
```
class Bad(Addable):   # Error: Trait 'Addable' requires a type argument
```

**Spurious type argument on a non-generic trait:**
```
impl Adder[int] for Calc:  # Error: Trait 'Adder' does not take type arguments
```

**Wrong concrete type in the method:**
```
impl Addable[int] for Bad:
  def add(x: int, y: float64) -> int:  # Error: does not match trait signature
    return x
```

## What This Is Not

Type parameters exist only on trait signatures. There are no generic functions, no generic structs, and no generic classes in this chapter. `T` cannot appear in a field declaration, a variable type, or a function return type outside a trait body. The feature is deliberately narrow: it solves the specific problem of writing a single trait that applies to multiple element types, without adding a general generics system.

## Things Worth Knowing

**The type parameter name is just a label.** `trait Addable[T]` and `trait Addable[Element]` are equivalent. The name is a convention, not a keyword.

**A class can implement the same generic trait with different type arguments.** `class Calc(Addable[int], Addable[float64]):` declares both instantiations. Each is verified separately against the corresponding concrete method.

**The `TypeVar` value type does not appear in the IR.** Conformance resolves all `TypeVar` occurrences to concrete types at compile time. The generated methods use `i64`, `double`, or whatever concrete LLVM type corresponds to the argument. Nothing generic survives into the IR.

## Build and Run

```bash
cd code/chapter-30
cmake -S . -B build && cmake --build build
```

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

---
description: "Add type parameters to traits: trait Addable[T] declares a contract over an abstract type, and classes instantiate it with a concrete type at the impl site."
---
# 30. pyxc: Generic Traits

## Where We Are

[Chapter 29](chapter-29.md) added `impl` blocks. A trait is still limited to concrete types — `trait Adder` specifies `int` parameters explicitly. After this chapter, a trait can name an abstract type parameter and leave the concrete type to be supplied by each implementor:

```pyxc
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

`Addable[int]` and `Addable[float64]` are separate contracts. A class can implement both with separate `impl` blocks.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-30
```

## Grammar

`traitdef` gains an optional type parameter. `classdef` and `impldef` use `traitref` wherever they previously used a bare identifier.

```ebnf
traitdef  = "trait" identifier [ "[" identifier "]" ] ":" eols traitblock ;  -- changed
classdef  = "class" identifier [ "(" traitref { "," traitref } ")" ] ":" eols structblock ;  -- changed
traitref  = identifier [ "[" type "]" ] ;  -- new
impldef   = "impl" traitref "for" identifier ":" eols implblock ;  -- changed
```

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

## `ValueType::TypeVar` and `ActiveTypeParams`

A new enum value represents an unresolved type parameter inside a trait body:

```cpp
enum class ValueType {
  // ...existing values...
  TypeVar,
};
```

The set of currently active type parameter names is tracked in a global:

```cpp
static std::set<string> ActiveTypeParams;
```

`ParseTraitDefinition` populates this set before parsing the trait body and clears it after:

```cpp
TI.TypeParamName = TypeParamName;
ActiveTypeParams.clear();
if (!TypeParamName.empty())
  ActiveTypeParams.insert(TypeParamName);
// ... parse body ...
ActiveTypeParams.clear();  // reset after body closes
```

`ParseTypeToken` checks `ActiveTypeParams` before treating an unknown identifier as an error. If the name is active, it returns `ValueType::TypeVar` and stores the parameter name as the struct name:

```cpp
if (ActiveTypeParams.count(TyName)) {
  getNextToken();
  if (StructName) *StructName = TyName;
  return ValueType::TypeVar;
}
```

This means `T` in `def add(x: T, y: T) -> T` resolves to `(ValueType::TypeVar, "T")` rather than failing as an unknown type. Outside a trait body, `T` has no meaning and would fall through to the normal identifier handling.

## Parsing the Type Parameter in `ParseTraitDefinition`

`ParseTraitDefinition` checks for an optional `[Param]` after the trait name:

```cpp
string TypeParamName;
if (CurTok == '[') {
  getNextToken(); // eat '['
  if (CurTok != tok_identifier) {
    LogError("Expected type parameter name in trait definition");
    return false;
  }
  TypeParamName = IdentifierStr;
  getNextToken(); // eat type parameter name
  if (CurTok != ']') {
    LogError("Expected ']' after trait type parameter");
    return false;
  }
  getNextToken(); // eat ']'
}
TI.TypeParamName = TypeParamName;
ActiveTypeParams.clear();
if (!TypeParamName.empty())
  ActiveTypeParams.insert(TypeParamName);
```

`TypeParamName` is stored on `TraitInfo`. An empty `TypeParamName` means the trait is non-generic.

## `ImplTraitRef` — Carrying the Type Argument

In chapter 29, `ImplementedTraits` was a `vector<string>`. This chapter replaces the element type with `ImplTraitRef`, which carries both the trait name and the concrete type argument supplied at the impl or class header:

```cpp
struct ImplTraitRef {
  string TraitName;
  bool HasTypeArg = false;
  ValueType TypeArg = ValueType::Error;
  string TypeArgStructName;
};
```

Both `ParseAggregateDefinition` (class header) and `ParseImplDefinition` (impl header) parse the optional `[type]` and fill this struct:

```cpp
StructTypeInfo::ImplTraitRef Ref;
Ref.TraitName = TraitName;
if (!TraitDef.TypeParamName.empty()) {
  // trait requires a type argument
  if (CurTok != '[') {
    LogError(("Trait '" + TraitName + "' requires a type argument").c_str());
    return false;
  }
  getNextToken(); // eat '['
  ValueType TypeArg = ParseTypeToken(&TypeArgStruct);
  // validate — must be a concrete type (not TypeVar, not Error, not None)
  getNextToken(); // eat ']'
  Ref.HasTypeArg = true;
  Ref.TypeArg = TypeArg;
  Ref.TypeArgStructName = TypeArgStruct;
} else if (CurTok == '[') {
  LogError(("Trait '" + TraitName + "' does not take type arguments").c_str());
  return false;
}
```

The duplicate impl check is updated to compare full `ImplTraitRef` values using a `SameImpl` lambda, so `impl Addable[int] for Calc` and `impl Addable[float64] for Calc` are treated as distinct and both allowed.

## `VerifyTraitConformance` with Type Substitution

`VerifyTraitConformance` now takes an `ImplTraitRef` instead of a bare `string`, and substitutes the concrete type for every `TypeVar` occurrence in the trait signature before comparing:

```cpp
static bool VerifyTraitConformance(const string &ClassName,
                                   const StructTypeInfo::ImplTraitRef &ImplRef) {
  const string &TraitName = ImplRef.TraitName;
  const auto &TI = Traits.at(TraitName);

  // Verify type-arg consistency
  if (!TI.TypeParamName.empty() && !ImplRef.HasTypeArg) {
    LogError(("Trait '" + TraitName + "' requires a type argument").c_str());
    return false;
  }

  // Lambda: resolve TypeVar → concrete type, leave everything else unchanged
  auto ResolveReq = [&](ValueType T, const string &S)
      -> std::pair<ValueType, string> {
    if (T == ValueType::TypeVar && S == TI.TypeParamName)
      return {ImplRef.TypeArg, ImplRef.TypeArgStructName};
    return {T, S};
  };

  for (const auto &Req : TI.Methods) {
    // check method exists, is public ...
    auto ReqRet = ResolveReq(Req.ReturnType, Req.ReturnStructName);
    if (P->getReturnType() != ReqRet.first ||
        P->getReturnStructName() != ReqRet.second) {
      LogError("does not match trait signature");
      return false;
    }
    for (size_t I = 0; I < Req.Args.size(); ++I) {
      auto ReqArg = ResolveReq(Req.Args[I].Type, Req.Args[I].StructName);
      if (P->getArgType(I + 1) != ReqArg.first ||
          P->getArgStructName(I + 1) != ReqArg.second) {
        LogError("does not match trait signature");
        return false;
      }
    }
  }
  return true;
}
```

For a non-generic trait, `ResolveReq` always returns its arguments unchanged — conformance works identically to chapter 29.

## Error Cases

**Missing type argument on a generic trait:**
```pyxc
class Bad(Addable):   # Error: Trait 'Addable' requires a type argument
```

**Spurious type argument on a non-generic trait:**
```pyxc
impl Adder[int] for Calc:  # Error: Trait 'Adder' does not take type arguments
```

**Wrong concrete type in the method:**
```pyxc
impl Addable[int] for Bad:
  def add(x: int, y: float64) -> int:  # Error: does not match trait signature
    return x
```

## What This Is Not

Type parameters exist only on trait signatures. There are no generic functions, no generic structs, and no generic classes. `T` cannot appear in a field declaration, a variable type, or a function return type outside a trait body. The feature is deliberately narrow: it solves the specific problem of writing a single trait that applies to multiple element types without adding a general generics system.

## Things Worth Knowing

**The type parameter name is just a label.** `trait Addable[T]` and `trait Addable[Element]` are equivalent.

**A class can implement the same generic trait with different type arguments.** `class Calc(Addable[int], Addable[float64]):` is valid. Each instantiation is verified separately.

**`TypeVar` does not appear in the IR.** Conformance resolves all `TypeVar` occurrences to concrete types at compile time. The generated methods use `i64`, `double`, or whatever LLVM type corresponds to the argument.

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

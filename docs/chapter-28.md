---
description: "Add traits: named method-signature contracts that a class declares it satisfies. Conformance is verified at compile time with no runtime overhead."
---
# 28. pyxc: Traits

## Where We Are

[Chapter 27](chapter-27.md) added visibility. Classes can now hide implementation details. But there is no way to say "this class promises to have these methods" — no interface contract, no way to write code that works against any class satisfying a given shape.

After this chapter:

```pyxc
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

## New Token and Data Structures

```cpp
tok_trait = -43,
```

Registered in the keyword table:

```cpp
{"trait", tok_trait}
```

Trait data is stored in two new structs and one new global map:

```cpp
struct TraitMethodSig {
  string Name;
  vector<PrototypeAST::ArgInfo> Args;  // explicit params only — no self
  ValueType ReturnType = ValueType::None;
  string ReturnStructName;
};

struct TraitInfo {
  string Name;
  vector<TraitMethodSig> Methods;
};

static std::map<string, TraitInfo> Traits;
```

`TraitMethodSig` stores explicit parameters only — `self` is not included. When conformance is checked, the compiler accounts for `self` being at index 0 of the implementing method's prototype by comparing `Req.Args[I]` against `P->getArgType(I + 1)`.

`StructTypeInfo` gains a list of trait names the class declares:

```cpp
struct StructTypeInfo {
  // ...
  vector<string> ImplementedTraits;  // new
};
```

`Traits` is cleared on each compiler reset (alongside `FunctionProtos`, `StructTypes`, etc.) so REPL sessions don't accumulate stale trait definitions.

## `ParseTraitDefinition` — Parsing Trait Bodies

`ParseTraitDefinition` is structured like `ParseAggregateDefinition` but simpler — no fields, no methods, just signatures:

```cpp
static bool ParseTraitDefinition() {
  getNextToken(); // eat 'trait'
  string TraitName = IdentifierStr;
  // Reject clashes with existing traits, struct types, and type aliases
  if (Traits.count(TraitName) || StructTypes.count(TraitName) ||
      TypeAliases.count(TraitName)) {
    LogError(("Name '" + TraitName + "' is already defined").c_str());
    return false;
  }
  getNextToken(); // eat trait name
  // ... eat ':', eat EOL, expect INDENT ...

  TraitInfo TI;
  TI.Name = TraitName;
  while (CurTok != tok_dedent && ...) {
    // expect 'def'
    getNextToken(); // eat 'def'
    string MethodName = IdentifierStr;
    getNextToken(); // eat method name
    // parse '(' params ')' with type annotations (same as prototype parsing)
    vector<PrototypeAST::ArgInfo> Args;
    // ... parse each param ...

    // parse optional -> ReturnType
    ValueType RetType = ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);

    // A body (colon) here is an error
    if (CurTok == ':') {
      LogError("Trait methods cannot have a body");
      return false;
    }
    // Reject duplicate method names
    TI.Methods.push_back({MethodName, std::move(Args), RetType, RetStructName});
  }
  // eat DEDENT, inject tok_block_end
  PendingTokens.push_front(tok_block_end);
  getNextToken();
  Traits[TraitName] = std::move(TI);
  return true;
}
```

Key points:
- `self` is not parsed — it appears in no trait signature.
- Method bodies are explicitly rejected with an error: "Trait methods cannot have a body".
- Duplicate method names within one trait are rejected.
- The name clash check covers `Traits`, `StructTypes`, and `TypeAliases` — a trait name cannot shadow any of these.

`HandleTraitDef` calls `ParseTraitDefinition` and handles error recovery, then dispatches from both `MainLoop` and `FileModeLoop` on `tok_trait`.

## Declaring Trait Conformance in the Class Header

`ParseAggregateDefinition` is extended to parse an optional trait list between the class name and the `:`  colon. This only applies to classes (`IsClass == true`):

```cpp
vector<string> ImplementedTraits;
bool IsClass = (strcmp(KindName, "class") == 0);
if (IsClass && CurTok == '(') {
  std::set<string> SeenTraits;
  getNextToken(); // eat '('
  while (CurTok != ')') {
    string TraitName = IdentifierStr;
    if (!Traits.count(TraitName)) {
      LogError(("Unknown trait '" + TraitName + "'").c_str());
      return false;
    }
    if (SeenTraits.count(TraitName)) {
      LogError(("Duplicate trait '" + TraitName + "' in class implements list").c_str());
      return false;
    }
    SeenTraits.insert(TraitName);
    ImplementedTraits.push_back(TraitName);
    getNextToken(); // eat trait name
    if (CurTok == ')') break;
    getNextToken(); // eat ','
  }
  getNextToken(); // eat ')'
}
// ...
Info.IsClass = IsClass;
Info.ImplementedTraits = ImplementedTraits;
```

Each trait name must already be in `Traits` — forward declarations are not supported. Listing the same trait twice is caught by `SeenTraits`.

## `VerifyTraitConformance` — Checking the Class at Close

After parsing the entire class body (at the closing `tok_dedent`), the compiler walks each declared trait and checks conformance. All three of the following must hold for every method in every declared trait:

1. **The method exists.** `ClassName.MethodName` must be in `FunctionProtos`.
2. **The method is public.** Trait conformance requires the method to be accessible to callers.
3. **The signature matches exactly.** Return type, return struct name, parameter count, and each parameter type must agree.

```cpp
for (const auto &TraitName : Info.ImplementedTraits) {
  const auto &TI = Traits.at(TraitName);
  for (const auto &Req : TI.Methods) {
    // 1. Method must exist
    auto PI = FunctionProtos.find(StructName + "." + Req.Name);
    if (PI == FunctionProtos.end()) {
      LogError(("Class '" + StructName + "' does not implement trait '" +
                TraitName + "' method '" + Req.Name + "'").c_str());
      return false;
    }
    // 2. Method must be public
    auto MI = Info.MethodIsPublic.find(Req.Name);
    if (MI == Info.MethodIsPublic.end() || !MI->second) {
      LogError(("Trait method '" + Req.Name + "' on class '" + StructName +
                "' must be public").c_str());
      return false;
    }
    // 3. Signature must match (Req.Args.size() + 1 because self is at index 0)
    PrototypeAST *P = PI->second.get();
    if (P->getNumArgs() != Req.Args.size() + 1 ||
        P->getReturnType() != Req.ReturnType ||
        P->getReturnStructName() != Req.ReturnStructName) {
      LogError(("Method '" + Req.Name + "' on class '" + StructName +
                "' does not match trait signature").c_str());
      return false;
    }
    for (size_t I = 0; I < Req.Args.size(); ++I) {
      if (P->getArgType(I + 1) != Req.Args[I].Type ||
          P->getArgStructName(I + 1) != Req.Args[I].StructName) {
        LogError(...);
        return false;
      }
    }
  }
}
```

The `+ 1` offset in `P->getArgType(I + 1)` is because `self` occupies index 0 of the implementing method but does not appear in `TraitMethodSig::Args` at all.

## What Traits Are Not

There is no dynamic dispatch. There is no vtable. The trait check is purely structural: it verifies that the method exists with the right signature and is public. The generated IR is identical to what you would get without the trait — trait methods are just regular LLVM functions.

There is no way in this chapter to pass a `Measurable` to a function without knowing the concrete type. Traits are a documentation and enforcement mechanism, not a polymorphism mechanism. Dynamic dispatch comes in a later chapter.

## Things Worth Knowing

**Traits must be defined before the classes that implement them.** The trait name lookup happens at class parse time; if the trait does not exist yet, it is an error.

**A class can implement multiple traits.** List them comma-separated in the class header. Listing the same trait twice is an error.

**Trait methods cannot have bodies.** Writing `:` after a trait method signature is a parse error: "Trait methods cannot have a body".

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

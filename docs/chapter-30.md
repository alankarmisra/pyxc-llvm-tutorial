---
description: "Add impl blocks: implement a trait for an existing class after the class definition, separating the class layout from its trait conformance."
---
# 30. pyxc: impl Blocks

## Where We Are

[Chapter 29](chapter-29.md) added traits. A class declares the traits it implements in its header, and the compiler verifies conformance when the class body closes. That works well when you write both the trait and the class together, but what if you want to implement a standard trait on a class that was already written?

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

The methods defined in the `impl` block become regular methods on `Calc`, callable with `c.add(...)` just like any other method.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-30
```

## Grammar

This chapter adds one new production and extends `top`.

```ebnf
top       = typealias | traitdef | structdef | classdef | impldef | definition | ...  -- changed
impldef   = "impl" identifier "for" identifier ":" eols implblock ;  -- new
implblock = indent implmethod { eols implmethod } dedent ;           -- new
implmethod = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ":" ( simplestmt | eols block ) ;  -- new
```

`implmethod` has a full body, unlike `traitmethodsig`. Methods in an `impl` block are fully defined here.

### Full Grammar

`code/chapter-30/pyxc.ebnf`

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

## New Token and Keyword

```cpp
tok_impl = -44,
```

Registered in the keyword table:

```cpp
{"impl", tok_impl}
```

The `for` in `impl TraitName for ClassName` reuses the existing `tok_for` token — the same token produced by the `for` keyword in loop statements. There is no ambiguity because `impl` always precedes it and the parser knows it is reading an impl header, not a loop.

## `VerifyTraitConformance` Extracted as a Shared Function

In chapter 28, the conformance check was inlined inside `ParseAggregateDefinition`. This chapter extracts it into a standalone function so both `ParseAggregateDefinition` (end of class body) and `ParseImplDefinition` (end of impl body) can call it:

```cpp
static bool VerifyTraitConformance(const string &ClassName,
                                   const string &TraitName) {
  auto CI = StructTypes.find(ClassName);
  // class must exist and be a class
  const auto &TI = Traits.at(TraitName);
  const auto &ClassInfo = CI->second;

  for (const auto &Req : TI.Methods) {
    // 1. Method must exist
    auto PI = FunctionProtos.find(ClassName + "." + Req.Name);
    if (PI == FunctionProtos.end()) {
      LogError(("Class '" + ClassName + "' does not implement trait '" +
                TraitName + "' method '" + Req.Name + "'").c_str());
      return false;
    }
    // 2. Method must be public
    auto MI = ClassInfo.MethodIsPublic.find(Req.Name);
    if (MI == ClassInfo.MethodIsPublic.end() || !MI->second) {
      LogError(("Trait method '" + Req.Name + "' on class '" + ClassName +
                "' must be public").c_str());
      return false;
    }
    // 3. Signature must match (self is at index 0; Req.Args starts at index 1)
    PrototypeAST *P = PI->second.get();
    if (P->getNumArgs() != Req.Args.size() + 1 ||
        P->getReturnType() != Req.ReturnType ||
        P->getReturnStructName() != Req.ReturnStructName) {
      LogError(...);
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
  return true;
}
```

`ParseAggregateDefinition` now calls `VerifyTraitConformance(StructName, TraitName)` at the closing DEDENT. The logic is identical to chapter 28 — it has just moved into its own function.

## `ParseImplDefinition` — The impl Block Parser

`ParseImplDefinition` validates the header, parses and compiles the methods, then calls `VerifyTraitConformance`:

```cpp
static bool ParseImplDefinition() {
  getNextToken(); // eat 'impl'

  // 1. Validate trait name
  string TraitName = IdentifierStr;
  if (!Traits.count(TraitName)) {
    LogError(("Unknown trait '" + TraitName + "'").c_str());
    return false;
  }
  getNextToken(); // eat trait name

  // 2. Expect 'for' (reuses tok_for)
  if (CurTok != tok_for) {
    LogError("Expected 'for' in impl definition");
    return false;
  }
  getNextToken(); // eat 'for'

  // 3. Validate class name — must exist and be a class, not a struct
  string ClassName = IdentifierStr;
  auto CI = StructTypes.find(ClassName);
  if (CI == StructTypes.end()) {
    LogError(("Unknown class '" + ClassName + "'").c_str());
    return false;
  }
  if (!CI->second.IsClass) {
    LogError(("'" + ClassName +
              "' is a struct, not a class; traits can only be implemented "
              "on classes").c_str());
    return false;
  }
  getNextToken(); // eat class name

  // 4. Reject duplicate impl for the same trait/class pair
  if (std::find(CI->second.ImplementedTraits.begin(),
                CI->second.ImplementedTraits.end(), TraitName)
      != CI->second.ImplementedTraits.end()) {
    LogError(("Trait '" + TraitName + "' is already implemented for class '"
              + ClassName + "'").c_str());
    return false;
  }

  // ... eat ':', eat EOL, expect INDENT ...

  // 5. Parse and compile each method body
  while (CurTok != tok_dedent && ...) {
    auto FnAST = ParseMethodDefinitionInClass(ClassName, /*IsPublic=*/true);
    if (auto *FnIR = FnAST->codegen()) { /* optionally dump IR */ }
  }

  // eat DEDENT, inject tok_block_end

  // 6. Record conformance and verify
  CI->second.ImplementedTraits.push_back(TraitName);
  if (!VerifyTraitConformance(ClassName, TraitName))
    return false;
  return true;
}
```

All methods in an `impl` block are forced public (`IsPublic=true`). Satisfying a trait contract is a public commitment — private trait methods are caught by `VerifyTraitConformance`'s public check.

`HandleImplDef` calls `ParseImplDefinition` with the same error-recovery pattern used by `HandleStructDef` and `HandleClassDef`, and both `MainLoop` and `FileModeLoop` dispatch on `tok_impl`.

## Methods Defined in `impl` Are Regular Methods

There is no runtime distinction between a method defined in the class body and one defined in an `impl` block. Both are stored in `FunctionProtos` under the mangled name `ClassName.MethodName` and emitted as `@ClassName.MethodName` in the IR. A caller cannot tell where the method was defined.

## Things Worth Knowing

**The trait must be defined before the `impl`.** `impl Adder for Calc:` requires that `Adder` is already in scope.

**The class must be defined before the `impl`.** The class name is looked up in `StructTypes` at parse time.

**Implementing a trait on a struct is rejected.** The `IsClass` flag is checked — a struct gives: `'S' is a struct, not a class; traits can only be implemented on classes`.

**`impl` cannot be used twice for the same trait/class pair.** A second `impl Adder for Calc:` is rejected: "Trait 'Adder' is already implemented for class 'Calc'".

## What's Next

[Chapter 31](chapter-31.md) adds type parameters to traits — `trait Addable[T]:` — so the same contract can be expressed for different element types.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

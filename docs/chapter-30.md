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

### Grammar

`code/chapter-30/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
-top-level-item             = type-alias | trait-definition | struct-definition | class-definition | function-definition | decorated-function-definition | external | top-level-expression ;
+top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | decorated-function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
 trait-definition        = "trait" name ":" end-of-lines trait-block ;
 trait-block      = indent trait-method-signature { end-of-lines trait-method-signature } dedent ;
 trait-method-signature  = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 class-definition        = "class" name [ "(" name { "," name } ")" ] ":" end-of-lines struct-block ;
+implementation-definition         = "impl" name "for" name ":" end-of-lines implementation-block ;
+implementation-block       = indent implementation-method { end-of-lines implementation-method } dedent ;
+implementation-method      = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 struct-block     = indent class-member { end-of-lines class-member } dedent ;
 class-member     = [ visibility ] ( field-declaration | method-definition ) ;
 visibility      = "public" | "private" ;
 method-definition       = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")"
                   [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 field-declaration       = name ":" type ;
 function-definition      = "def" function-signature [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 (* If the return type is omitted, it defaults to None. *)
 decorated-function-definition    = binary-decorator end-of-lines "def" binary-operator-signature [ "->" type ] ":" ( simple-statement | end-of-lines block )
                 | unary-decorator  end-of-lines "def" unary-operator-signature  [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 binary-decorator = "@" "binary" "(" integer ")" ;
 unary-decorator  = "@" "unary" ;
 binary-operator-signature = custom-operator-character "(" typed-parameter "," typed-parameter ")" ;
 unary-operator-signature  = custom-operator-character "(" typed-parameter ")" ;
 external        = "extern" "def" function-signature [ "->" type ] ;
 top-level-expression    = expression ;
 function-signature       = name "(" [ typed-parameter { "," typed-parameter } ] ")" ;
 typed-parameter      = name ":" type ;
 if-statement          = "if" expression ":" suite
                 [ end-of-lines "else" ":" suite ] ;
 for-statement         = "for"
                   ( "var" name ":" type | name )
                   "=" expression "," expression "," expression ":" suite ;
 variable-statement         = "var" variable-binding { "," variable-binding } ;
 assignment-statement      = lvalue "=" expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | variable-statement | assignment-statement | expression ;
 compound-statement    = if-statement | for-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
 expression      = unary-expression binary-operator-right ;
 binary-operator-right        = { binary-operator unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = unary-operator unary-expression | primary ;
 unary-operator         = "-" | user-defined-unary-operator ;
 primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
 cast-expression        = cast-type "(" expression ")" ;
 sizeof-expression      = "sizeof" "(" type ")" ;
 address-expression        = "addr" "(" lvalue ")" ;
 name-expression  = name | call-expression | method-call-expression | constructor-call-expression ;
 call-expression        = name "(" [ expression { "," expression } ] ")" ;
 method-call-expression  = name "." name "(" [ expression { "," expression } ] ")" ;
 constructor-call-expression    = name "(" [ expression { "," expression } ] ")" ;
 field-access     = name "." name { "." name } ;
 index-expression       = name "[" expression "]" ;
 number-expression      = number ;
 array-literal    = "[" [ expression { "," expression } ] "]" ;
 string-literal   = "\"" { ? any char except " and newline ? | escape } "\"" ;
 escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
 parenthesized-expression       = "(" expression ")" ;
 binary-operator        = builtin-binary-operator | user-defined-binary-operator ;
 indent          = INDENT ;
 dedent          = DEDENT ;

 builtin-binary-operator = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
 user-defined-binary-operator = ? any operator-character defined as a custom binary operator ? ;
 user-defined-unary-operator  = ? any operator-character defined as a custom unary operator ? ;
 custom-operator-character    = ? any operator-character that is not "-" or a builtin-binary-operator,
                     and not already defined as a custom operator ? ;
 operator-character          = ? any single ASCII punctuation character ? ;
 name      = (letter | "_") { letter | digit | "_" } ;
 builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
                 | "float" | "float32" | "float64"
                 | "bool" | "None" ;
 alias-type       = name ;
 struct-type      = name ;
 pointer-type     = "ptr" "[" type "]" ;
 type            = base-type [ array-suffix ] ;
 base-type        = builtin-type | alias-type | struct-type | pointer-type ;
 array-suffix     = "[" integer "]" ;
 cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
                 | "float" | "float32" | "float64"
                 | "bool" | pointer-type ;
 integer         = digit { digit } ;
 number          = ( digit { digit } [ "." { digit } ]
                   | "." digit { digit } ) [ exponent ] ;
 exponent        = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
 boolean-literal    = "True" | "False" ;
 letter          = "A".."Z" | "a".."z" ;
 digit           = "0".."9" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
 INDENT          = ? synthetic token emitted by lexer ? ;
 DEDENT          = ? synthetic token emitted by lexer ? ;

 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
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
      LogErrorExpression(("Class '" + ClassName + "' does not implement trait '" +
                TraitName + "' method '" + Req.Name + "'").c_str());
      return false;
    }
    // 2. Method must be public
    auto MI = ClassInfo.MethodIsPublic.find(Req.Name);
    if (MI == ClassInfo.MethodIsPublic.end() || !MI->second) {
      LogErrorExpression(("Trait method '" + Req.Name + "' on class '" + ClassName +
                "' must be public").c_str());
      return false;
    }
    // 3. Signature must match (self is at index 0; Req.Args starts at index 1)
    PrototypeAST *P = PI->second.get();
    if (P->getNumArgs() != Req.Args.size() + 1 ||
        P->getReturnType() != Req.ReturnType ||
        P->getReturnStructName() != Req.ReturnStructName) {
      LogErrorExpression(...);
      return false;
    }
    for (size_t I = 0; I < Req.Args.size(); ++I) {
      if (P->getArgType(I + 1) != Req.Args[I].Type ||
          P->getArgStructName(I + 1) != Req.Args[I].StructName) {
        LogErrorExpression(...);
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
    LogErrorExpression(("Unknown trait '" + TraitName + "'").c_str());
    return false;
  }
  getNextToken(); // eat trait name

  // 2. Expect 'for' (reuses tok_for)
  if (CurTok != tok_for) {
    LogErrorExpression("Expected 'for' in impl definition");
    return false;
  }
  getNextToken(); // eat 'for'

  // 3. Validate class name — must exist and be a class, not a struct
  string ClassName = IdentifierStr;
  auto CI = StructTypes.find(ClassName);
  if (CI == StructTypes.end()) {
    LogErrorExpression(("Unknown class '" + ClassName + "'").c_str());
    return false;
  }
  if (!CI->second.IsClass) {
    LogErrorExpression(("'" + ClassName +
              "' is a struct, not a class; traits can only be implemented "
              "on classes").c_str());
    return false;
  }
  getNextToken(); // eat class name

  // 4. Reject duplicate impl for the same trait/class pair
  if (std::find(CI->second.ImplementedTraits.begin(),
                CI->second.ImplementedTraits.end(), TraitName)
      != CI->second.ImplementedTraits.end()) {
    LogErrorExpression(("Trait '" + TraitName + "' is already implemented for class '"
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

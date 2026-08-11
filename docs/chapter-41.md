---
description: "Add impl blocks: implement a trait for an existing class after the class definition, separating the class layout from its trait conformance."
---
# 41. pyxc: `impl` Blocks

## What I Am Building

[Chapter 40](chapter-40.md) added traits. A class declares the traits it implements in its own header, and the compiler verifies conformance when the class body closes. That works well when I write the trait and the class together, but what if I want to implement a trait on a class that was already written, without touching its definition?

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

```text
12.000000
```

The methods defined in the `impl` block become regular methods on `Calc`, callable with `c.add(...)` just like any other method.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-30
```

## Grammar

One new production, `implementation-definition`, plus its own block and method productions. `implementation-method` has a full body, unlike `trait-method-signature`; methods in an `impl` block are fully defined right there:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
-top-level-item             = type-alias | trait-definition | struct-definition | class-definition | function-definition | external | top-level-expression ;
+top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | external | top-level-expression ;
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
 expression      = comparison ;
 comparison      = sum { comparison-operator sum } ;
 comparison-operator = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
 sum             = term { ("+" | "-") term } ;
 term            = unary-expression { ("*" | "/") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = "-" unary-expression | primary ;
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
 indent          = INDENT ;
 dedent          = DEDENT ;
 
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

Registered in the keyword table like every other keyword. The `for` in `impl TraitName for ClassName` reuses the existing `tok_for` token, the same one `for` loop statements produce. There's no ambiguity: `impl` always precedes it, and the parser already knows it's reading an impl header at that point, not a loop.

## Pulling Trait Conformance Checking Out into Its Own Function

In [Chapter 40](chapter-40.md), the conformance check was inlined at the end of `ParseAggregateDefinition`. This chapter pulls it out into a standalone function, `VerifyTraitConformance`, so both `ParseAggregateDefinition` (class body close) and the new `ParseImplDefinition` (impl body close) can call the same logic instead of duplicating it:

```cpp
static bool VerifyTraitConformance(const string &ClassName,
                                   const string &TraitName) {
  auto CI = StructTypes.find(ClassName);
  if (CI == StructTypes.end() || !CI->second.IsClass) {
    LogErrorExpression(("Unknown class '" + ClassName + "'").c_str());
    return false;
  }
  if (!Traits.count(TraitName)) {
    LogErrorExpression(("Unknown trait '" + TraitName + "'").c_str());
    return false;
  }
  const auto &TI = Traits.at(TraitName);
  const auto &ClassInfo = CI->second;
  for (const auto &Req : TI.Methods) {
    auto PI = FunctionSignatures.find(ClassName + "." + Req.Name);
    if (PI == FunctionSignatures.end()) {
      LogErrorExpression(("Class '" + ClassName + "' does not implement trait '" +
                TraitName + "' method '" + Req.Name + "'")
                   .c_str());
      return false;
    }
    auto MI = ClassInfo.MethodIsPublic.find(Req.Name);
    if (MI == ClassInfo.MethodIsPublic.end() || !MI->second) {
      LogErrorExpression(("Trait method '" + Req.Name + "' on class '" + ClassName +
                "' must be public")
                   .c_str());
      return false;
    }
    FunctionSignatureNode *P = PI->second.get();
    if (P->getNumParameters() != Req.Arguments.size() + 1 ||
        P->getReturnType() != Req.ReturnType ||
        P->getReturnStructName() != Req.ReturnStructName) {
      LogErrorExpression(("Method '" + Req.Name + "' on class '" + ClassName +
                "' does not match trait signature")
                   .c_str());
      return false;
    }
    for (size_t I = 0; I < Req.Arguments.size(); ++I) {
      if (P->getParameterType(I + 1) != Req.Arguments[I].Type ||
          P->getParameterStructName(I + 1) != Req.Arguments[I].StructName) {
        LogErrorExpression(("Method '" + Req.Name + "' on class '" + ClassName +
                  "' does not match trait signature")
                     .c_str());
        return false;
      }
    }
  }
  return true;
}
```

The three checks per required method (exists, public, signature matches) are exactly what [Chapter 40](chapter-40.md) already did inline; only the two guards at the top — unknown class, unknown trait — are new, needed now that this function can be called from a context (`impl`) where neither name was already looked up by the caller. `ParseAggregateDefinition` calls `VerifyTraitConformance(StructName, TraitName)` at the class body's closing DEDENT, same as before, just through the extracted function now.

## The `impl` Block Parser

`ParseImplDefinition` validates the header — trait exists, `for` follows, class exists and is actually a class, not a struct — parses and compiles each method, then checks conformance:

```cpp
static bool ParseImplDefinition() {
  // CurrentToken is 'impl'
  getNextToken(); // eat 'impl'
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected trait name after 'impl'"), false;
  string TraitName = Name;
  if (!Traits.count(TraitName))
    return LogErrorExpression(("Unknown trait '" + TraitName + "'").c_str()), false;
  getNextToken(); // eat trait name
  if (CurrentToken != tok_for)
    return LogErrorExpression("Expected 'for' in impl definition"), false;
  getNextToken(); // eat 'for'
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected class name after 'for'"), false;
  string ClassName = Name;
  auto CI = StructTypes.find(ClassName);
  if (CI == StructTypes.end())
    return LogErrorExpression(("Unknown class '" + ClassName + "'").c_str()), false;
  if (!CI->second.IsClass)
    return LogErrorExpression(("'" + ClassName +
              "' is a struct, not a class; traits can only be implemented on "
              "classes").c_str()), false;
  getNextToken(); // eat class name
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' in impl definition"), false;
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented impl body"), false;
  getNextToken(); // eat INDENT

  if (std::find(CI->second.ImplementedTraits.begin(),
                CI->second.ImplementedTraits.end(),
                TraitName) != CI->second.ImplementedTraits.end())
    return LogErrorExpression(("Trait '" + TraitName + "' is already implemented for class '" +
              ClassName + "'").c_str()), false;

  while (CurrentToken != tok_dedent && CurrentToken != tok_block_end && CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken != tok_def)
      return LogErrorExpression("Expected method definition in impl body"), false;
    auto FnAST = ParseMethodDefinitionInClass(ClassName, /*IsPublic=*/true);
    if (!FnAST)
      return false;
    if (auto *FnIR = FnAST->codegen()) {
      if (ShouldDumpIR())
        FnIR->print(errs());
    }
    if (CurrentToken == tok_eol)
      consumeNewlines();
    else if (CurrentToken == tok_block_end)
      getNextToken();
  }
  if (CurrentToken != tok_dedent)
    return LogErrorExpression("Expected dedent after impl body"), false;
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end

  CI->second.ImplementedTraits.push_back(TraitName);
  if (!VerifyTraitConformance(ClassName, TraitName))
    return false;
  return true;
}
```

The duplicate-`impl` check runs after the header is fully parsed and the body's `INDENT` consumed, not before — so a second `impl Adder for Calc:` reports its error from inside the body (at the first `def`), not at the header line itself. I confirmed this rather than assume it: compiling two `impl Adder for Calc:` blocks back to back reports the error on the line of the second block's first method, not its `impl` line.

Every method parsed here goes through `ParseMethodDefinitionInClass` with `IsPublic` hardcoded to `true`. Satisfying a trait is a public commitment; there's no such thing as a private trait method, so `impl` doesn't offer a visibility modifier to write one. A method that's private in spirit would just fail `VerifyTraitConformance`'s public check anyway, so forcing `true` here just skips straight to the outcome that check would have produced.

`HandleImplDef` calls `ParseImplDefinition` with the same error-recovery pattern `HandleStructDef` and `HandleClassDef` already use, and `tok_impl` is wired into the dispatch switch in both `MainLoop` and `FileModeLoop`.

## Methods Defined in `impl` Are Regular Methods

There's no runtime distinction between a method defined in the class body and one defined in an `impl` block. Both land in `FunctionSignatures` under the same mangled name, `ClassName.MethodName`, and are emitted as `@ClassName.MethodName` in the IR. A caller has no way to tell where the method was defined, and doesn't need to.

## Known Limitations

**The trait must already exist.** `impl Adder for Calc:` requires `Adder` to already be registered in `Traits` at the point the `impl` block is parsed.

**The class must already exist, and must be a class.** The name is looked up in `StructTypes` at parse time; a struct gives `'S' is a struct, not a class; traits can only be implemented on classes`.

**`impl` cannot be used twice for the same trait/class pair.** A second `impl Adder for Calc:` is rejected: `Trait 'Adder' is already implemented for class 'Calc'`.

## What's Next

[Chapter 42](chapter-42.md) adds generic traits.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

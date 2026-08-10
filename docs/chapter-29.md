---
description: "Add traits: named method-signature contracts that a class declares it satisfies. Conformance is verified at compile time with no runtime overhead."
---
# 29. pyxc: Traits

## What I Am Building

[Chapter 28](chapter-28.md) added visibility. Classes can now hide implementation details. But there's no way to say "this class promises to have these methods": no interface contract, no way to write code that works against any class satisfying a given shape.

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

```text
12.000000
```

If `Rect` doesn't implement `area`, or implements it with the wrong signature, the compiler reports an error before any code is generated.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-29
```

## Grammar

Three new productions (`trait-definition`, `trait-block`, `trait-method-signature`), and `class-definition` gains an optional parenthesized trait list. A `trait-method-signature` looks like a method definition but has no body and no `self` parameter:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
-top-level-item             = type-alias | struct-definition | class-definition | function-definition | external | top-level-expression ;
+top-level-item             = type-alias | trait-definition | struct-definition | class-definition | function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
+trait-definition        = "trait" name ":" end-of-lines trait-block ;
+trait-block      = indent trait-method-signature { end-of-lines trait-method-signature } dedent ;
+trait-method-signature  = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
-class-definition        = "class" name ":" end-of-lines struct-block ;
+class-definition        = "class" name [ "(" name { "," name } ")" ] ":" end-of-lines struct-block ;
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

## New Token and Data Structures

```cpp
tok_trait = -43,
```

Registered in the keyword table like every other keyword. Trait data lives in two new structs and one new global map:

```cpp
struct TraitMethodSig {
  string Name;
  vector<FunctionSignatureNode::ParameterInfo> Arguments;  // explicit params only — no self
  ValueType ReturnType = ValueType::None;
  string ReturnStructName;
};

struct TraitInfo {
  string Name;
  vector<TraitMethodSig> Methods;
};

static std::map<string, TraitInfo> Traits;
```

`TraitMethodSig::Arguments` holds explicit parameters only; `self` isn't included at all. When conformance is checked later, the compiler accounts for `self` sitting at index 0 of the implementing method's real signature by comparing `Req.Arguments[I]` against `P->getParameterType(I + 1)`.

`StructTypeInfo` gains a list of trait names the class declares:

```cpp
vector<string> ImplementedTraits;
```

`Traits` is cleared on every per-file parser reset alongside `FunctionSignatures`, `StructTypes`, and the rest, so REPL sessions and separate file compiles don't accumulate stale trait definitions.

## Parsing Trait Bodies

`ParseTraitDefinition` is structured like `ParseAggregateDefinition` but simpler: no fields, no method bodies, just signatures. It also checks for name collisions across all three top-level naming tables at once, since a trait name and a struct or alias name would otherwise be free to collide:

```cpp
static bool ParseTraitDefinition() {
  // CurrentToken is 'trait'
  getNextToken(); // eat 'trait'
  if (CurrentToken != tok_name) {
    LogErrorExpression("Expected trait name");
    return false;
  }
  string TraitName = Name;
  if (Traits.count(TraitName) || StructTypes.count(TraitName) ||
      TypeAliases.count(TraitName)) {
    LogErrorExpression(("Name '" + TraitName + "' is already defined").c_str());
    return false;
  }
  getNextToken(); // eat trait name
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after trait name"), false;
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented trait body"), false;
  getNextToken(); // eat INDENT

  TraitInfo TI;
  TI.Name = TraitName;
  while (CurrentToken != tok_dedent && CurrentToken != tok_block_end && CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken != tok_def)
      return LogErrorExpression("Expected method signature in trait body"), false;
    getNextToken(); // eat 'def'
    string MethodName = Name;
    getNextToken(); // eat method name
    // ... parse '(' typed-parameter { ',' typed-parameter } ')' into Arguments ...
    string RetStructName;
    ValueType RetType =
        ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);
    if (RetType == ValueType::Error)
      return false;

    for (const auto &M : TI.Methods) {
      if (M.Name == MethodName)
        return LogErrorExpression(("Duplicate trait method '" + MethodName + "'").c_str()), false;
    }
    TI.Methods.push_back({MethodName, std::move(Arguments), RetType, RetStructName});
    if (CurrentToken == tok_colon)
      return LogErrorExpression("Trait methods cannot have a body"), false;
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }
  if (CurrentToken != tok_dedent)
    return LogErrorExpression("Expected dedent after trait body"), false;
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end
  Traits[TraitName] = std::move(TI);
  return true;
}
```

Key points:
- `self` is never parsed; it appears in no trait signature.
- A `:` where a next signature or the dedent was expected means someone wrote a body, and that's rejected immediately: "Trait methods cannot have a body".
- Duplicate method names within one trait are rejected before the duplicate is even added.
- The name-clash check up front covers `Traits`, `StructTypes`, and `TypeAliases` together, so a trait name can't shadow any of them.

`HandleTraitDef` calls `ParseTraitDefinition` and handles error recovery the same way `HandleStructDef` and `HandleClassDef` do, and `tok_trait` is wired into the dispatch switch in both `MainLoop` and `FileModeLoop`.

## Declaring Trait Conformance in the Class Header

`ParseAggregateDefinition` now parses an optional trait list between the class name and the `:`. This only runs for classes (`IsClass == true`) — a `struct` never sees this branch at all, since `IsClass` gates it before the token is even inspected:

```cpp
vector<string> ImplementedTraits;
bool IsClass = (strcmp(KindName, "class") == 0);
if (IsClass && CurrentToken == tok_lparen) {
  std::set<string> SeenTraits;
  getNextToken(); // eat '('
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (CurrentToken != tok_name)
        return LogErrorExpression("Expected trait name in class implements list"), false;
      string TraitName = Name;
      if (!Traits.count(TraitName))
        return LogErrorExpression(("Unknown trait '" + TraitName + "'").c_str()), false;
      if (SeenTraits.count(TraitName))
        return LogErrorExpression(
            ("Duplicate trait '" + TraitName + "' in class implements list").c_str()), false;
      SeenTraits.insert(TraitName);
      ImplementedTraits.push_back(TraitName);
      getNextToken(); // eat trait name
      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in class implements list"), false;
      getNextToken(); // eat ','
    }
  }
  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')' after class implements list"), false;
  getNextToken(); // eat ')'
}
// ...
Info.IsClass = IsClass;
Info.ImplementedTraits = ImplementedTraits;
```

Each trait name has to already be in `Traits`; forward declarations aren't supported, so `trait` blocks have to appear before any class that implements them. Listing the same trait twice is caught by `SeenTraits`. Because `struct` never enters this branch, writing `struct S(Foo):` doesn't fail with an unknown-trait error — it fails one step later with "Expected ':' after struct name", since the parser is still expecting the colon it always expected there.

## Checking Trait Conformance at Class Close

Right after the class body's closing `DEDENT`, if `Info.IsClass` is true, the compiler walks every declared trait and checks three things for every method that trait requires:

```cpp
if (Info.IsClass) {
  for (const auto &TraitName : Info.ImplementedTraits) {
    const auto &TI = Traits.at(TraitName);
    for (const auto &Req : TI.Methods) {
      // 1. The method must exist.
      auto PI = FunctionSignatures.find(StructName + "." + Req.Name);
      if (PI == FunctionSignatures.end())
        return LogErrorExpression(("Class '" + StructName + "' does not implement trait '" +
                  TraitName + "' method '" + Req.Name + "'").c_str()), false;

      // 2. The method must be public.
      auto MI = Info.MethodIsPublic.find(Req.Name);
      if (MI == Info.MethodIsPublic.end() || !MI->second)
        return LogErrorExpression(("Trait method '" + Req.Name + "' on class '" + StructName +
                  "' must be public").c_str()), false;

      // 3. The signature must match exactly (the +1 skips self).
      FunctionSignatureNode *P = PI->second.get();
      if (P->getNumParameters() != Req.Arguments.size() + 1 ||
          P->getReturnType() != Req.ReturnType ||
          P->getReturnStructName() != Req.ReturnStructName)
        return LogErrorExpression(("Method '" + Req.Name + "' on class '" + StructName +
                  "' does not match trait signature").c_str()), false;
      for (size_t I = 0; I < Req.Arguments.size(); ++I) {
        if (P->getParameterType(I + 1) != Req.Arguments[I].Type ||
            P->getParameterStructName(I + 1) != Req.Arguments[I].StructName)
          return LogErrorExpression(("Method '" + Req.Name + "' on class '" + StructName +
                    "' does not match trait signature").c_str()), false;
      }
    }
  }
}
```

`P->getNumParameters() != Req.Arguments.size() + 1` is the same `self`-at-index-0 offset showing up again: the implementing method's own signature always has one more parameter than the trait requires it to declare.

## What Traits Are Not

There's no dynamic dispatch and no vtable. The check is purely structural: it verifies a matching, public method exists, nothing more. The generated IR is identical to what a class without the trait declaration would produce — a trait method is just a regular LLVM function, mangled the same way every other method is.

There's also no way yet to pass a `Measurable` to a function without knowing the concrete class. Traits are a documentation-and-enforcement mechanism here, not a polymorphism mechanism.

## Known Limitations

**Traits must be defined before the classes that implement them.** The trait-name lookup happens while parsing the class header; if the trait doesn't exist yet, `Unknown trait '...'` is reported right there.

**A class can implement multiple traits.** List them comma-separated in the class header. Listing the same trait twice is rejected.

**Structs cannot implement traits.** The `(Trait)` syntax is gated on `IsClass`, so it's only reachable after `class`; writing it after `struct` fails on the colon that would otherwise follow the name.

## Try It

**Missing trait method**

```pyxc
trait Measurable:
  def area() -> int

class Rect(Measurable):
  w: int
```

```text
Error (Line 6, Column 0): Class 'Rect' does not implement trait 'Measurable' method 'area'
```

**Private implementation of a trait method**

```pyxc
trait Measurable:
  def area() -> int
class Rect(Measurable):
  w: int
  private def area() -> int:
    return self.w
```

```text
Error (Line 7, Column 0): Trait method 'area' on class 'Rect' must be public
```

## What's Next

[Chapter 30](chapter-30.md) adds `impl` blocks: a way to implement a trait for a class outside the class definition, after the fact.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

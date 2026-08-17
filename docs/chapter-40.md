---
description: "Add traits: named method-signature contracts that a class declares it satisfies. Conformance is verified at compile time with no runtime overhead."
---
# 40. pyxc: Traits

## What I Am Building

[Chapter 39](chapter-39.md) added visibility. Classes can now hide implementation details. But there's no way to say "this class promises to have these methods": no interface contract, no way to write code that works against any class satisfying a given shape.

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
cd pyxc-llvm-tutorial/code/chapter-40
```

## Grammar

Three new productions (`trait-definition`, `trait-block`, `trait-method-signature`), and `class-definition` gains an optional parenthesized trait list. A `trait-method-signature` looks like a method definition but has no body and no `self` parameter:

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | type-alias
+                                    | trait-definition
                                     | struct-definition
                                     | class-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
-class-definition                  = "class" name ":" end-of-lines
+trait-definition                  = "trait" name ":" end-of-lines
+                                    trait-block ;
+trait-block                       = indent trait-method-signature
+                                    { end-of-lines trait-method-signature }
+                                    dedent ;
+trait-method-signature            = "def" name "(" [ parameters ] ")"
+                                    [ "->" type ] ;
+class-definition                  = "class" name
+                                    [ "(" trait-reference
+                                      { "," trait-reference } ")" ]
+                                    ":" end-of-lines
                                     class-block ;
+trait-reference                   = name ;
 type-alias                        = "type" name "=" type ;
 struct-block                      = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 class-block                       = indent class-member
                                     { end-of-lines class-member } dedent ;
 class-member                      = [ visibility ]
                                     ( field-declaration | method-definition ) ;
 visibility                        = "public" | "private" ;
 field-declaration                 = name ":" type ;
 method-definition                 = "def" name "(" [ parameters ] ")"
                                     [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 function-definition               = "def" function-signature [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 external                          = "extern" "def" external-function-signature
                                     [ "->" type ] ;
 top-level-statement               = statement ;
 function-signature                = name "(" [ parameters ] ")" ;
 external-function-signature       = name "(" [ parameters [ "," "..." ] | "..." ] ")" ;
 parameters                        = typed-parameter { "," typed-parameter } ;
 typed-parameter                   = name ":" type ;
 if-statement                      = "if" expression ":" suite
                                     { [ end-of-lines ] "elif" expression ":" suite }
                                     [ [ end-of-lines ] "else" ":" suite ] ;
 for-statement                     = "for" ( "var" name ":" type | name )
                                     "=" expression ","
                                     expression "," expression ":" suite ;
 while-statement                   = "while" expression ":" suite ;
 do-while-statement                = "do" ":" suite [ end-of-lines ]
                                     "while" expression ;
 switch-statement                  = "switch" expression ":" end-of-lines
                                     indent switch-body dedent ;
 switch-body                       = switch-case
                                     { end-of-lines switch-case }
                                     [ end-of-lines default-case ] ;
 switch-case                       = "case" switch-integer
                                     { "," switch-integer } ":" suite ;
 default-case                      = "default" ":" suite ;
 variable-statement                = "var" variable-binding
                                     { "," variable-binding } ;
 simple-statement                  = return-statement
                                     | break-statement
                                     | continue-statement
                                     | variable-statement
                                     | expression ;
 compound-statement                = if-statement
                                     | for-statement
                                     | while-statement
                                     | do-while-statement
                                     | switch-statement ;
 statement                         = simple-statement | compound-statement ;
 suite                             = simple-statement
                                     | compound-statement
                                     | end-of-lines block ;
 return-statement                  = "return" [ expression ] ;
 break-statement                   = "break" ;
 continue-statement                = "continue" ;
 statement-separator               = end-of-lines | BLOCK_END ;
 block                             = indent statement
                                     { statement-separator statement } dedent ;
 expression                        = assignment ;
 assignment                        = logical-or [ assignment-operator assignment ] ;
 logical-or                        = logical-and { "||" logical-and } ;
 logical-and                       = bitwise-or { "&&" bitwise-or } ;
 bitwise-or                        = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor                       = bitwise-and { "^" bitwise-and } ;
 bitwise-and                       = equality { "&" equality } ;
 equality                          = relational { ("==" | "!=") relational } ;
 relational                        = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift                             = sum { ("<<" | ">>") sum } ;
 sum                               = term { ("+" | "-") term } ;
 term                              = unary-expression
                                     { ("*" | "/" | "%") unary-expression } ;
 lvalue                            = name
                                     { "." name | "[" expression "]" } ;
 variable-binding                  = name ":" type [ "=" expression ] ;
 unary-expression                  = ("-" | "!" | "~" | "++" | "--")
                                     unary-expression
                                     | postfix-expression ;
 postfix-expression                = primary [ "++" | "--" ] ;
 primary                           = cast-expression
                                     | sizeof-expression
                                     | address-expression
                                     | array-literal
                                     | string-literal
                                     | character-literal
                                     | name-expression
                                     | number-expression
                                     | boolean-literal
                                     | parenthesized-expression ;
 cast-expression                   = cast-type "(" expression ")" ;
 sizeof-expression                 = "sizeof" "(" type ")" ;
 address-expression                = "addr" "(" lvalue ")" ;
 array-literal                     = "[" [ expression
                                       { "," expression } ] "]" ;
 string-literal                    = '"' { string-character | escape } '"' ;
 escape                            = literal-escape ;
 string-character                  = ? any character except '"', "\\", "\r", and "\n" ? ;
 character-literal                 = "'" ( character | character-escape ) "'" ;
 character-escape                  = literal-escape ;
 literal-escape                    = "\\" ( "\\" | "'" | '"' | "?"
                                       | "a" | "b" | "f" | "n" | "r"
                                       | "t" | "v"
                                       | "x" hex-digit hex-digit
                                       | octal-digit [ octal-digit
                                         [ octal-digit ] ]
                                       | "u" hex-digit hex-digit hex-digit hex-digit
                                       | "U" hex-digit hex-digit hex-digit hex-digit
                                         hex-digit hex-digit hex-digit hex-digit ) ;
 character                         = ? any character except "'", "\\", "\r", and "\n" ? ;
 hex-digit                         = digit | "A".."F" | "a".."f" ;
 assignment-operator               = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 octal-digit                       = "0".."7" ;
 name-expression                   = lvalue
                                     | call-expression
                                     | method-call-expression
                                     | constructor-call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 method-call-expression            = lvalue "." name "(" [ arguments ] ")" ;
 constructor-call-expression       = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 indent                            = INDENT ;
 dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 type                              = base-type [ array-suffix ] ;
 base-type                         = builtin-type | alias-type | struct-type
                                     | pointer-type ;
 pointer-type                      = "ptr" "[" type "]" ;
 array-suffix                      = "[" integer "]" ;
 builtin-type                      = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" | "None" ;
 struct-type                       = name ;
 alias-type                        = name ;
 cast-type                         = builtin-cast-type | pointer-type ;
 builtin-cast-type                 = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" ;
 number                            = ( digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ) [ exponent ] ;
 switch-integer                    = [ "-" ] digit { digit } ;
 exponent                          = ( "e" | "E" ) [ "+" | "-" ]
                                     digit { digit } ;
 boolean-literal                   = "True" | "False" ;
 integer                           = digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 (*
     A `comment` begins with "#" and continues to the end of the line. The lexer
      ignores its text and returns an end-of-line token when one follows it.
 *)
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 (*
     `whitespace` may appear before or between tokens
      and is ignored by the lexer.
 *)
 whitespace                        = " " | "\t" | "\v" | "\f" ;
 INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
 DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
 BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
                                       immediately after it consumes DEDENT ? ;

```

## New Token and Data Structures

```cppdiff
*  tok_public = -65,
*  tok_private = -66,
+  tok_trait = -67,
*
*  // punctuation and operators
```

Registered in the keyword table like every other keyword. Trait data lives in two new structs and one new global map:

```cpp
struct TraitMethodSignature {
  string Name;
  vector<pair<string, ValueType>> Parameters;
  vector<string> ParameterTypeInfo;
  ValueType ReturnType = ValueType::None;
  string ReturnTypeInfo;
};

struct TraitTypeInfo {
  vector<TraitMethodSignature> Methods;
};

static map<string, TraitTypeInfo> TraitTypes;
```

`TraitMethodSignature::Parameters` holds explicit parameters only; `self` isn't included at all. When conformance is checked later, the compiler accounts for `self` sitting at index 0 of the implementing method's real signature by comparing `Requirement.Parameters[Index]` against `Implementation->getParameterType(Index + 1)`.

`StructTypeInfo` gains a list of trait names the class declares:

```cppdiff
*struct StructTypeInfo {
*  vector<StructFieldInfo> Fields;
*  map<string, size_t> FieldIndices;
*  map<string, bool> Methods;
+  vector<string> ImplementedTraits;
*  bool IsClass = false;
*};
```

`TraitTypes` is cleared on every per-file parser reset (`ResetParserStateForFile`) alongside `FunctionSignatures`, `StructTypes`, and the rest, so REPL sessions and separate file compiles don't accumulate stale trait definitions.

## Parsing Trait Bodies

`ParseTraitDefinition` is structured like `ParseAggregateDefinition` but simpler: no fields, no method bodies, just signatures. It also checks for name collisions across all three top-level naming tables at once, since a trait name and a struct or alias name would otherwise be free to collide:

```cpp
static bool ParseTraitDefinition() {
  getNextToken(); // eat 'trait'
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected trait name"), false;
  string TraitName = Name;
  if (TraitTypes.count(TraitName) || StructTypes.count(TraitName) ||
      TypeAliases.count(TraitName))
    return LogErrorExpression(
               ("Type '" + TraitName + "' is already defined").c_str()),
           false;
  getNextToken(); // eat trait name
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after trait name"), false;
  getNextToken(); // eat ':'
  if (CurrentToken != tok_eol)
    return LogErrorExpression("Expected newline after trait header"), false;
  consumeNewlines();
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented trait body"), false;
  getNextToken(); // eat INDENT

  TraitTypeInfo Trait;
  set<string> MethodNames;
  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken != tok_def)
      return LogErrorExpression("Expected method signature in trait body"),
             false;
    getNextToken(); // eat 'def'
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected trait method name"), false;
    TraitMethodSignature Method;
    Method.Name = Name;
    if (!MethodNames.insert(Method.Name).second)
      return LogErrorExpression("Duplicate trait method"), false;
    getNextToken(); // eat method name
    if (CurrentToken != tok_lparen)
      return LogErrorExpression("Expected '(' in trait method signature"),
             false;
    getNextToken(); // eat '('
    if (CurrentToken != tok_rparen) {
      while (true) {
        if (CurrentToken != tok_name)
          return LogErrorExpression(
                     "Expected parameter name in trait method signature"),
                 false;
        string ParameterName = Name;
        if (ParameterName == "self")
          return LogErrorExpression(
                     "Method parameters cannot be named 'self'"),
                 false;
        getNextToken(); // eat parameter name
        if (CurrentToken != tok_colon)
          return LogErrorExpression(
                     "Trait method parameters require a type annotation"),
                 false;
        getNextToken(); // eat ':'
        string TypeInfo;
        ValueType Type = ParseTypeToken(&TypeInfo);
        if (Type == ValueType::Error || Type == ValueType::None)
          return false;
        Method.Parameters.push_back({ParameterName, Type});
        Method.ParameterTypeInfo.push_back(TypeInfo);
        if (CurrentToken == tok_rparen)
          break;
        if (CurrentToken != tok_comma)
          return LogErrorExpression("Expected ')' or ',' in parameter list"),
                 false;
        getNextToken(); // eat ','
      }
    }
    getNextToken(); // eat ')'
    Method.ReturnType =
        ParseOptionalReturnType(&Method.ReturnTypeInfo, ValueType::None);
    if (Method.ReturnType == ValueType::Error)
      return false;
    if (CurrentToken == tok_colon)
      return LogErrorExpression("Trait methods cannot have a body"), false;
    Trait.Methods.push_back(std::move(Method));
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }

  if (Trait.Methods.empty())
    return LogErrorExpression("Trait requires at least one method"), false;
  if (CurrentToken != tok_dedent)
    return LogErrorExpression("Expected dedent after trait body"), false;
  TraitTypes[TraitName] = std::move(Trait);
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface block-end
  return true;
}
```

Key points:
- `self` is never parsed; it appears in no trait signature.
- A `:` where a next signature or the dedent was expected means someone wrote a body, and that's rejected immediately: "Trait methods cannot have a body".
- Duplicate method names within one trait are rejected via a `set<string>` before the duplicate is even added.
- An empty trait (no methods at all) is rejected outright.
- The name-clash check up front covers `TraitTypes`, `StructTypes`, and `TypeAliases` together, so a trait name can't shadow any of them.

`HandleTraitDefinition` calls `ParseTraitDefinition` and handles error recovery the same way the struct and class handlers do, and `tok_trait` is wired into the dispatch switch in both `MainLoop` and `FileModeLoop`.

## Declaring Trait Conformance in the Class Header

`ParseAggregateDefinition` now parses an optional trait list between the class name and the `:`. This only runs for classes (`IsClass == true`) — a `struct` never sees this branch at all, since `IsClass` gates it before the token is even inspected:

```cppdiff
*static bool ParseAggregateDefinition(const char *KindName) {
*  getNextToken(); // eat 'struct' or 'class'
*  if (CurrentToken != tok_name) {
*    ...
*  }
*  string AggregateName = Name;
*  if (TypeAliases.count(AggregateName)) {
*    ...
*  }
*  if (StructTypes.count(AggregateName)) {
*    ...
*  }
+  if (TraitTypes.count(AggregateName)) {
+    LogErrorExpression(("Type '" + AggregateName +
+                        "' is already defined as a trait")
+                           .c_str());
+    return false;
+  }
*  getNextToken(); // eat name
-  if (CurrentToken != tok_colon) {
+  bool IsClass = string(KindName) == "class";
+  vector<string> ImplementedTraits;
+  if (IsClass && CurrentToken == tok_lparen) {
+    set<string> SeenTraits;
+    getNextToken(); // eat '('
+    while (CurrentToken != tok_rparen) {
+      if (CurrentToken != tok_name) {
+        LogErrorExpression("Expected trait name in class implements list");
+        return false;
+      }
+      string TraitName = Name;
+      if (!TraitTypes.count(TraitName)) {
+        LogErrorExpression(("Unknown trait '" + TraitName + "'"));
+        return false;
+      }
+      if (!SeenTraits.insert(TraitName).second) {
+        LogErrorExpression(
+            ("Duplicate trait '" + TraitName +
+             "' in class implements list")
+                .c_str());
+        return false;
+      }
+      ImplementedTraits.push_back(TraitName);
+      getNextToken(); // eat trait name
+      if (CurrentToken == tok_rparen)
+        break;
+      if (CurrentToken != tok_comma) {
+        LogErrorExpression("Expected ')' or ',' in class implements list");
+        return false;
+      }
+      getNextToken(); // eat ','
+    }
+    getNextToken(); // eat ')'
+  }
+  if (CurrentToken != tok_colon) {
*    LogErrorExpression((string("Expected ':' after ") + KindName + " name"));
*    return false;
*  }
*  getNextToken(); // eat ':'
*  ...
*  getNextToken(); // eat INDENT
*
*  StructTypeInfo Info;
-  Info.IsClass = string(KindName) == "class";
+  Info.IsClass = IsClass;
+  Info.ImplementedTraits = std::move(ImplementedTraits);
*  // Methods need the fields parsed before them, so keep the in-progress class
*  // metadata visible while I walk the body.
*  StructTypes[AggregateName] = Info;
*  ...
*}
```

Each trait name has to already be in `TraitTypes`; forward declarations aren't supported, so `trait` blocks have to appear before any class that implements them. Listing the same trait twice is caught by `SeenTraits`. Because `struct` never enters this branch, writing `struct S(Foo):` doesn't fail with an unknown-trait error — it fails one step later with "Expected ':' after struct name", since the parser is still expecting the colon it always expected there.

## Checking Trait Conformance at Class Close

Right after the class body's closing `DEDENT`, if the class declared any traits, `ParseAggregateDefinition` hands each one to `VerifyTraitConformance`:

```cppdiff
*  if (CurrentToken != tok_dedent) {
*    LogErrorExpression((string("Expected dedent after ") + KindName + " body"));
*    return false;
*  }
*  StructTypes[AggregateName] = std::move(Info);
+  for (const string &TraitName :
+       StructTypes[AggregateName].ImplementedTraits) {
+    if (!VerifyTraitConformance(AggregateName, TraitName))
+      return false;
+  }
*  for (auto &Method : Methods) {
*    if (!Method->codegen())
*      return false;
*  }
```

`VerifyTraitConformance` checks three things for every method the trait requires:

```cpp
static bool VerifyTraitConformance(const string &ClassName,
                                   const string &TraitName) {
  const auto &Trait = TraitTypes.at(TraitName);
  const auto &Class = StructTypes.at(ClassName);
  for (const auto &Requirement : Trait.Methods) {
    string MethodName = ClassName + "." + Requirement.Name;
    FunctionSignatureNode *Implementation =
        GetFunctionSignature(MethodName);
    if (!Implementation) {
      LogErrorExpression(
          ("Class '" + ClassName + "' does not implement trait '" + TraitName +
           "' method '" + Requirement.Name + "'")
              .c_str());
      return false;
    }
    auto Visibility = Class.Methods.find(Requirement.Name);
    if (Visibility == Class.Methods.end() || !Visibility->second) {
      LogErrorExpression(
          ("Trait method '" + Requirement.Name + "' on class '" + ClassName +
           "' must be public")
              .c_str());
      return false;
    }

    bool Matches =
        Implementation->getNumParameters() ==
            Requirement.Parameters.size() + 1 &&
        Implementation->getReturnType() == Requirement.ReturnType &&
        Implementation->getReturnStructName() == Requirement.ReturnTypeInfo;
    for (size_t Index = 0; Matches && Index < Requirement.Parameters.size();
         ++Index) {
      Matches =
          Implementation->getParameterType(Index + 1) ==
              Requirement.Parameters[Index].second &&
          Implementation->getParameterStructName(Index + 1) ==
              Requirement.ParameterTypeInfo[Index];
    }
    if (!Matches) {
      LogErrorExpression(
          ("Method '" + Requirement.Name + "' on class '" + ClassName +
           "' does not match trait signature")
              .c_str());
      return false;
    }
  }
  return true;
}
```

`Implementation->getNumParameters() == Requirement.Parameters.size() + 1` is the same `self`-at-index-0 offset showing up again: the implementing method's own signature always has one more parameter than the trait requires it to declare.

## What Traits Are Not

There's no dynamic dispatch and no vtable. The check is purely structural: it verifies a matching, public method exists, nothing more. The generated IR is identical to what a class without the trait declaration would produce — a trait method is just a regular LLVM function, mangled the same way every other method is.

There's also no way yet to pass a `Measurable` to a function without knowing the concrete class. Traits are a documentation-and-enforcement mechanism here, not a polymorphism mechanism.

## Known Limitations

**Traits must be defined before the classes that implement them.** The trait-name lookup happens while parsing the class header; if the trait doesn't exist yet, `Unknown trait '...'` is reported right there.

**A class can implement multiple traits.** List them comma-separated in the class header. Listing the same trait twice is rejected.

**Structs cannot implement traits.** The `(Trait)` syntax is gated on `IsClass`, so it's only reachable after `class`; writing it after `struct` fails on the colon that would otherwise follow the name.

## Build and Run

```bash
cd code/chapter-40
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

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

[Chapter 41](chapter-41.md) adds `impl` blocks.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

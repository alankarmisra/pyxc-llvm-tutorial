---
description: "Add type parameters to traits: trait Addable[T] declares a contract over an abstract type, and classes instantiate it with a concrete type at the impl site."
---
# 42. pyxc: Generic Traits

## What I Am Building

[Chapter 41](chapter-41.md) added `impl` blocks. A trait was still limited to concrete types: `trait Adder` had to spell out `int` parameters explicitly. After this chapter, a trait can name an abstract type parameter and leave the concrete type to be supplied by each implementor:

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

```text
11.000000
```

`Addable[int]` and `Addable[float64]` are separate contracts. A class can satisfy either one, though — as I found out while testing this chapter — not both at once on the same class (see Known Limitations).

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-42
```

## Grammar

`trait-definition` gains an optional type parameter. `class-definition` and `implementation-definition` use a new `trait-reference` production wherever they previously used a bare trait name:

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | type-alias
                                     | trait-definition
                                     | implementation-definition
                                     | struct-definition
                                     | class-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
-trait-definition                  = "trait" name ":" end-of-lines
+trait-definition                  = "trait" name [ "[" name "]" ] ":" end-of-lines
                                     trait-block ;
 trait-block                       = indent trait-method-signature
                                     { end-of-lines trait-method-signature }
                                     dedent ;
 trait-method-signature            = "def" name "(" [ parameters ] ")"
                                     [ "->" type ] ;
 class-definition                  = "class" name
                                     [ "(" trait-reference
                                       { "," trait-reference } ")" ]
                                     ":" end-of-lines
                                     class-block ;
-trait-reference                   = name ;
+trait-reference                   = name [ "[" type "]" ] ;
 implementation-definition         = "impl" trait-reference "for" name ":"
                                     end-of-lines implementation-block ;
 implementation-block              = indent method-definition
                                     { end-of-lines method-definition } dedent ;
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

## Representing an Unresolved Type Parameter

A new `ValueType` enum value represents an unresolved type parameter inside a trait body:

```cppdiff
*enum class ValueType {
*  None,
*  ...
*  Struct,
*  Array,
*  Pointer,
+  TypeVariable,
*  Error
*};
```

Because only one trait body is ever being parsed at a time, there's no need for a set of active names — a single global string holds the type parameter name currently in scope:

```cpp
static string ActiveTraitTypeParameter;
```

`ParseTypeToken` checks `ActiveTraitTypeParameter` before falling back to alias and struct lookup. If the current name matches it, it returns `ValueType::TypeVariable` and stores the parameter name itself as the struct name:

```cppdiff
*static ValueType ParseTypeToken(string *StructName) {
*  if (StructName)
*    StructName->clear();
*  ValueType BaseType = ValueType::Error;
*  string BaseTypeInfo;
*  switch (CurrentToken) {
*  ...
*  case tok_name: {
+    if (!ActiveTraitTypeParameter.empty() && Name == ActiveTraitTypeParameter) {
+      BaseTypeInfo = Name;
+      getNextToken();
+      BaseType = ValueType::TypeVariable;
+      break;
+    }
*    auto Alias = TypeAliases.find(Name);
*    if (Alias != TypeAliases.end()) {
*      BaseType = Alias->second.first;
*      BaseTypeInfo = Alias->second.second;
*      getNextToken();
*      break;
*    }
*    auto Found = StructTypes.find(Name);
*    if (Found == StructTypes.end()) {
*      LogErrorExpression(("Unknown type '" + Name + "'").c_str());
*      return ValueType::Error;
*    }
*    BaseTypeInfo = Name;
*    getNextToken();
*    BaseType = ValueType::Struct;
*    break;
*  }
*  default:
*    LogErrorExpression("Expected a type");
*    return ValueType::Error;
*  }
*  ...
*}
```

This is why `T` in `def add(x: T, y: T) -> T` resolves to `(ValueType::TypeVariable, "T")` instead of failing as an unknown type: the check runs first, before the alias and struct maps are even consulted. Outside a trait body `ActiveTraitTypeParameter` is empty, so `T` falls straight through to the ordinary unknown-type error.

## Parsing the Trait's Type Parameter

`ParseTraitDefinition` checks for an optional `[Param]` right after the trait name, before the `:`:

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
  string TypeParameterName;
  if (CurrentToken == tok_lbracket) {
    getNextToken(); // eat '['
    if (CurrentToken != tok_name)
      return LogErrorExpression(
                 "Expected type parameter name in trait definition"),
             false;
    TypeParameterName = Name;
    getNextToken(); // eat type parameter name
    if (CurrentToken != tok_rbracket)
      return LogErrorExpression(
                 "Expected ']' after trait type parameter"),
             false;
    getNextToken(); // eat ']'
  }
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
  Trait.TypeParameterName = TypeParameterName;
  ActiveTraitTypeParameter = TypeParameterName;
```

`TypeParameterName` is stored on `TraitTypeInfo`, the struct held in the `TraitTypes` map. An empty `TypeParameterName` means the trait isn't generic, and `ActiveTraitTypeParameter` stays empty for the whole body — every `T`-like name in a non-generic trait is still just an ordinary, probably-unknown identifier. `ActiveTraitTypeParameter` is cleared again once the trait body's `DEDENT` is consumed, at the end of `ParseTraitDefinition`.

## Carrying the Type Argument

`StructTypeInfo::ImplementedTraits` gains a nested struct, `StructTypeInfo::TraitReference`, that carries both the trait name and the concrete type argument supplied at the class header or the `impl` header:

```cpp
struct StructTypeInfo {
  struct TraitReference {
    string Name;
    bool HasTypeArgument = false;
    ValueType TypeArgument = ValueType::Error;
    string TypeArgumentInfo;
  };
  vector<StructFieldInfo> Fields;
  map<string, size_t> FieldIndices;
  map<string, bool> Methods;
  vector<TraitReference> ImplementedTraits;
  bool IsClass = false;
};
```

Both `ParseAggregateDefinition` (class header) and `ParseImplementationDefinition` (impl header) parse the optional `[type]` the same way. Here's the `impl`-header version:

```cpp
const auto &Trait = TraitTypes.at(TraitName);
if (!Trait.TypeParameterName.empty()) {
  if (CurrentToken != tok_lbracket)
    return LogErrorExpression(
               ("Trait '" + TraitName + "' requires a type argument")
                   .c_str()),
           false;
  getNextToken(); // eat '['
  TraitReference.TypeArgument =
      ParseTypeToken(&TraitReference.TypeArgumentInfo);
  if (TraitReference.TypeArgument == ValueType::Error ||
      TraitReference.TypeArgument == ValueType::None ||
      TraitReference.TypeArgument == ValueType::TypeVariable)
    return LogErrorExpression("Invalid trait type argument"), false;
  if (CurrentToken != tok_rbracket)
    return LogErrorExpression("Expected ']' after trait type argument"),
           false;
  getNextToken(); // eat ']'
  TraitReference.HasTypeArgument = true;
} else if (CurrentToken == tok_lbracket) {
  return LogErrorExpression(
             ("Trait '" + TraitName + "' does not take type arguments")
                 .c_str()),
         false;
}
```

The duplicate-impl check carried over from [Chapter 41](chapter-41.md) is unchanged: it still compares only the trait `Name`, not the type argument.

```cpp
if (any_of(Class->second.ImplementedTraits.begin(),
           Class->second.ImplementedTraits.end(),
           [&](const StructTypeInfo::TraitReference &Implemented) {
             return Implemented.Name == TraitName;
           }))
  return LogErrorExpression(
             ("Trait '" + TraitName + "' is already implemented for class '" +
              ClassName + "'")
                 .c_str()),
         false;
```

So `impl Addable[int] for Calc:` followed later by `impl Addable[float64] for Calc:` doesn't get as far as comparing type arguments — the second `impl` is rejected as already-implemented on the trait name alone. The class-header trait list's own duplicate check, a `SeenTraits` set of names, has the same limitation; see Known Limitations.

## Conformance Checking with Type Substitution

`VerifyTraitConformance` now takes a `StructTypeInfo::TraitReference` instead of a bare trait-name string. It substitutes the concrete type argument for every `TypeVariable` occurrence before comparing signatures:

```cpp
static bool VerifyTraitConformance(
    const string &ClassName,
    const StructTypeInfo::TraitReference &TraitReference) {
  const string &TraitName = TraitReference.Name;
  const auto &Trait = TraitTypes.at(TraitName);
  const auto &Class = StructTypes.at(ClassName);
  auto ResolveType = [&](ValueType Type,
                         const string &TypeInfo) -> pair<ValueType, string> {
    if (Type == ValueType::TypeVariable &&
        TypeInfo == Trait.TypeParameterName)
      return {TraitReference.TypeArgument,
              TraitReference.TypeArgumentInfo};
    return {Type, TypeInfo};
  };
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

    auto RequiredReturn =
        ResolveType(Requirement.ReturnType, Requirement.ReturnTypeInfo);
    bool Matches =
        Implementation->getNumParameters() ==
            Requirement.Parameters.size() + 1 &&
        Implementation->getReturnType() == RequiredReturn.first &&
        Implementation->getReturnStructName() == RequiredReturn.second;
    for (size_t Index = 0; Matches && Index < Requirement.Parameters.size();
         ++Index) {
      auto RequiredParameter = ResolveType(
          Requirement.Parameters[Index].second,
          Requirement.ParameterTypeInfo[Index]);
      Matches =
          Implementation->getParameterType(Index + 1) ==
              RequiredParameter.first &&
          Implementation->getParameterStructName(Index + 1) ==
              RequiredParameter.second;
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

For a non-generic trait, `Trait.TypeParameterName` is empty, so `ResolveType` never matches and always returns its arguments unchanged: conformance works exactly as it did in [Chapter 41](chapter-41.md). Note that this function itself doesn't check whether a type argument was required or supplied — that check (`Trait '...' requires a type argument` / `does not take type arguments`) happens earlier, while parsing the class header or `impl` header, before `VerifyTraitConformance` is ever called.

## What This Is Not

Type parameters exist only on trait signatures. There are no generic functions, no generic structs, and no generic classes. `T` can't appear in a field declaration, a variable type, or a function return type outside a trait body — `ActiveTraitTypeParameter` is only ever populated while a trait body is being parsed.

There's also no dynamic dispatch here, same as [Chapter 40](chapter-40.md): a generic trait is still a compile-time-checked contract, not a mechanism for writing code that's polymorphic over "anything implementing `Addable[T]`."

## Known Limitations

**A class cannot implement the same generic trait twice, even with different type arguments.** I initially thought `class Calc(Addable[int], Addable[float64]):` might work, since a class can list several distinct traits. But the class-header duplicate check only compares trait *names* — it rejects the second `Addable[...]` before the type arguments ever enter into it:

```
Error (Line 3, Column 26): Duplicate trait 'Addable' in class implements list
```

Sidestepping the header by using two separate `impl` blocks instead doesn't work either, for the same reason: the `impl`-header duplicate check also compares only the trait name, so the second `impl Addable[float64] for Calc:` is rejected as already-implemented before it even gets to parsing a body:

```
Error (Line 8, Column 27): Trait 'Addable' is already implemented for class 'Calc'
```

I confirmed both failure modes directly rather than assume either worked.

**Type arguments must be concrete.** `ValueType::TypeVariable` itself is rejected as a type argument, so a generic trait can't be implemented in terms of another trait's still-unresolved type parameter.

**No forward references.** A trait must exist before any class or `impl` references it, same restriction [Chapter 40](chapter-40.md) already had.

## Try It

**Missing type argument on a generic trait**

```pyxc
trait Addable[T]:
  def add(x: T, y: T) -> T

class Bad(Addable):
  x: int
```

```text
Error (Line 4, Column 18): Trait 'Addable' requires a type argument
```

**Spurious type argument on a non-generic trait**

```pyxc
trait Adder:
  def add(x: int, y: int) -> int
class Calc:
  x: int
impl Adder[int] for Calc:
  def add(x: int, y: int) -> int:
    return x
```

```text
Error (Line 5, Column 11): Trait 'Adder' does not take type arguments
```

**Wrong concrete type in the method**

```pyxc
trait Addable[T]:
  def add(x: T, y: T) -> T
class Bad:
  x: int
impl Addable[int] for Bad:
  def add(x: int, y: float64) -> int:
    return x
```

```text
Error (Line 8, Column 0): Method 'add' on class 'Bad' does not match trait signature
```

## Build and Run

```bash
cd code/chapter-42
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## What's Next

[Chapter 43](chapter-43.md) adds `module` and `export`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

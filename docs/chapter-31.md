---
description: "Add type parameters to traits: trait Addable[T] declares a contract over an abstract type, and classes instantiate it with a concrete type at the impl site."
---
# 31. pyxc: Generic Traits

## Where We Are

[Chapter 30](chapter-30.md) added `impl` blocks. A trait is still limited to concrete types — `trait Adder` specifies `int` parameters explicitly. After this chapter, a trait can name an abstract type parameter and leave the concrete type to be supplied by each implementor:

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
cd pyxc-llvm-tutorial/code/chapter-31
```

## Grammar

`traitdef` gains an optional type parameter. `classdef` and `impldef` use `traitref` wherever they previously used a bare identifier.

```ebnf
traitdef  = "trait" identifier [ "[" identifier "]" ] ":" eols traitblock ;  -- changed
classdef  = "class" identifier [ "(" traitref { "," traitref } ")" ] ":" eols structblock ;  -- changed
traitref  = identifier [ "[" type "]" ] ;  -- new
impldef   = "impl" traitref "for" identifier ":" eols implblock ;  -- changed
```

### Grammar

`code/chapter-31/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | decorated-function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
-trait-definition        = "trait" name ":" end-of-lines trait-block ;
+trait-definition        = "trait" name [ "[" name "]" ] ":" end-of-lines trait-block ;
 trait-block      = indent trait-method-signature { end-of-lines trait-method-signature } dedent ;
 trait-method-signature  = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
-class-definition        = "class" name [ "(" name { "," name } ")" ] ":" end-of-lines struct-block ;
-implementation-definition         = "impl" name "for" name ":" end-of-lines implementation-block ;
+class-definition        = "class" name [ "(" trait-reference { "," trait-reference } ")" ] ":" end-of-lines struct-block ;
+trait-reference        = name [ "[" type "]" ] ;
+implementation-definition         = "impl" trait-reference "for" name ":" end-of-lines implementation-block ;
 implementation-block       = indent implementation-method { end-of-lines implementation-method } dedent ;
 implementation-method      = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
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
    LogErrorExpression("Expected type parameter name in trait definition");
    return false;
  }
  TypeParamName = IdentifierStr;
  getNextToken(); // eat type parameter name
  if (CurTok != ']') {
    LogErrorExpression("Expected ']' after trait type parameter");
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
    LogErrorExpression(("Trait '" + TraitName + "' requires a type argument").c_str());
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
  LogErrorExpression(("Trait '" + TraitName + "' does not take type arguments").c_str());
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
    LogErrorExpression(("Trait '" + TraitName + "' requires a type argument").c_str());
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
      LogErrorExpression("does not match trait signature");
      return false;
    }
    for (size_t I = 0; I < Req.Args.size(); ++I) {
      auto ReqArg = ResolveReq(Req.Args[I].Type, Req.Args[I].StructName);
      if (P->getArgType(I + 1) != ReqArg.first ||
          P->getArgStructName(I + 1) != ReqArg.second) {
        LogErrorExpression("does not match trait signature");
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
cd code/chapter-31
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

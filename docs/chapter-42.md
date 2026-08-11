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
cd pyxc-llvm-tutorial/code/chapter-31
```

## Grammar

`trait-definition` gains an optional type parameter. `class-definition` and `implementation-definition` use a new `trait-reference` production wherever they previously used a bare trait name:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | external | top-level-expression ;
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

## Representing an Unresolved Type Parameter

A new `ValueType` enum value represents an unresolved type parameter inside a trait body:

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

`ParseTypeToken` checks `ActiveTypeParams` before falling back to alias and struct lookup. If the name is active, it returns `ValueType::TypeVar` and stores the parameter name itself as the struct name:

```cpp
case tok_name: {
  string TyName = Name;
  if (ActiveTypeParams.count(TyName)) {
    getNextToken();
    if (StructName)
      *StructName = TyName;
    return ValueType::TypeVar;
  }
  // ...alias lookup, then struct lookup, as before...
}
```

This is why `T` in `def add(x: T, y: T) -> T` resolves to `(ValueType::TypeVar, "T")` instead of failing as an unknown type: the check runs first, before the alias and struct maps are even consulted. Outside a trait body `ActiveTypeParams` is empty, so `T` falls straight through to the ordinary unknown-type error.

## Parsing the Trait's Type Parameter

`ParseTraitDefinition` checks for an optional `[Param]` right after the trait name, before the `:`:

```cpp
getNextToken(); // eat trait name
string TypeParamName;
if (CurrentToken == tok_lbracket) {
  getNextToken(); // eat '['
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected type parameter name in trait definition"), false;
  TypeParamName = Name;
  getNextToken(); // eat type parameter name
  if (CurrentToken != tok_rbracket)
    return LogErrorExpression("Expected ']' after trait type parameter"), false;
  getNextToken(); // eat ']'
}
if (CurrentToken != tok_colon)
  return LogErrorExpression("Expected ':' after trait name"), false;
// ...eat ':', consume newlines, expect INDENT...

TraitInfo TI;
TI.Name = TraitName;
TI.TypeParamName = TypeParamName;
ActiveTypeParams.clear();
if (!TypeParamName.empty())
  ActiveTypeParams.insert(TypeParamName);
```

`TypeParamName` is stored on `TraitInfo`. An empty `TypeParamName` means the trait isn't generic, and `ActiveTypeParams` stays empty for the whole body — every `T`-like name in a non-generic trait is still just an ordinary, probably-unknown identifier.

## Carrying the Type Argument

In [Chapter 40](chapter-40.md), `StructTypeInfo::ImplementedTraits` was a `vector<string>`. This chapter replaces the element type with a nested struct, `StructTypeInfo::ImplTraitRef`, that carries both the trait name and the concrete type argument supplied at the class header or the `impl` header:

```cpp
struct StructTypeInfo {
  // ...
  struct ImplTraitRef {
    string TraitName;
    bool HasTypeArg = false;
    ValueType TypeArg = ValueType::Error;
    string TypeArgStructName;
  };
  vector<ImplTraitRef> ImplementedTraits;
};
```

Both `ParseAggregateDefinition` (class header) and `ParseImplDefinition` (impl header) parse the optional `[type]` and fill one of these:

```cpp
StructTypeInfo::ImplTraitRef Ref;
Ref.TraitName = TraitName;
const auto &TI = Traits.at(TraitName);
if (!TI.TypeParamName.empty()) {
  // trait requires a type argument
  if (CurrentToken != tok_lbracket)
    return LogErrorExpression(("Trait '" + TraitName + "' requires a type argument").c_str()), false;
  getNextToken(); // eat '['
  string TypeArgStruct;
  ValueType TypeArg = ParseTypeToken(&TypeArgStruct);
  if (TypeArg == ValueType::Error || TypeArg == ValueType::None ||
      TypeArg == ValueType::TypeVar)
    return LogErrorExpression("Invalid trait type argument"), false;
  if (CurrentToken != tok_rbracket)
    return LogErrorExpression("Expected ']' after trait type argument"), false;
  getNextToken(); // eat ']'
  Ref.HasTypeArg = true;
  Ref.TypeArg = TypeArg;
  Ref.TypeArgStructName = TypeArgStruct;
} else if (CurrentToken == tok_lbracket) {
  return LogErrorExpression(("Trait '" + TraitName + "' does not take type arguments").c_str()), false;
}
```

In the `impl`-header path, the duplicate-impl check from [Chapter 41](chapter-41.md) is upgraded from comparing trait names to comparing full `ImplTraitRef`s with a `SameImpl` lambda:

```cpp
auto SameImpl = [&](const StructTypeInfo::ImplTraitRef &R) {
  return R.TraitName == ImplRef.TraitName &&
         R.HasTypeArg == ImplRef.HasTypeArg && R.TypeArg == ImplRef.TypeArg &&
         R.TypeArgStructName == ImplRef.TypeArgStructName;
};
```

So two separate `impl` blocks, `impl Addable[int] for Calc:` and `impl Addable[float64] for Calc:`, are recognized as different implementations, not a duplicate of each other — at the `impl`-header level. The class-header trait list is a different story; see Known Limitations.

## Conformance Checking with Type Substitution

`VerifyTraitConformance` now takes an `ImplTraitRef` instead of a bare trait-name string, checks that the trait's own type-parameter-ness agrees with whether an argument was supplied, then substitutes the concrete type for every `TypeVar` occurrence before comparing signatures:

```cpp
static bool VerifyTraitConformance(const string &ClassName,
                                   const StructTypeInfo::ImplTraitRef &ImplRef) {
  const string &TraitName = ImplRef.TraitName;
  auto CI = StructTypes.find(ClassName);
  if (CI == StructTypes.end() || !CI->second.IsClass)
    return LogErrorExpression(("Unknown class '" + ClassName + "'").c_str()), false;
  if (!Traits.count(TraitName))
    return LogErrorExpression(("Unknown trait '" + TraitName + "'").c_str()), false;
  const auto &TI = Traits.at(TraitName);
  if (!TI.TypeParamName.empty() && !ImplRef.HasTypeArg)
    return LogErrorExpression(("Trait '" + TraitName + "' requires a type argument").c_str()), false;
  if (TI.TypeParamName.empty() && ImplRef.HasTypeArg)
    return LogErrorExpression(("Trait '" + TraitName + "' does not take type arguments").c_str()), false;

  const auto &ClassInfo = CI->second;
  auto ResolveReq = [&](ValueType T,
                        const string &S) -> std::pair<ValueType, string> {
    if (T == ValueType::TypeVar && S == TI.TypeParamName)
      return {ImplRef.TypeArg, ImplRef.TypeArgStructName};
    return {T, S};
  };
  for (const auto &Req : TI.Methods) {
    // ...method exists, is public, same as chapter 41...
    FunctionSignatureNode *P = /* ... */;
    auto ReqRet = ResolveReq(Req.ReturnType, Req.ReturnStructName);
    if (P->getNumParameters() != Req.Arguments.size() + 1 ||
        P->getReturnType() != ReqRet.first ||
        P->getReturnStructName() != ReqRet.second)
      return LogErrorExpression("does not match trait signature"), false;
    for (size_t I = 0; I < Req.Arguments.size(); ++I) {
      auto ReqArg = ResolveReq(Req.Arguments[I].Type, Req.Arguments[I].StructName);
      if (P->getParameterType(I + 1) != ReqArg.first ||
          P->getParameterStructName(I + 1) != ReqArg.second)
        return LogErrorExpression("does not match trait signature"), false;
    }
  }
  return true;
}
```

For a non-generic trait, every `Req` type is already concrete, so `ResolveReq` always returns its arguments unchanged: conformance works exactly as it did in [Chapter 41](chapter-41.md).

## What This Is Not

Type parameters exist only on trait signatures. There are no generic functions, no generic structs, and no generic classes. `T` can't appear in a field declaration, a variable type, or a function return type outside a trait body — `ActiveTypeParams` is only ever populated while a trait body is being parsed.

There's also no dynamic dispatch here, same as [Chapter 40](chapter-40.md): a generic trait is still a compile-time-checked contract, not a mechanism for writing code that's polymorphic over "anything implementing `Addable[T]`."

## Known Limitations

**A class cannot implement the same generic trait twice, even with different type arguments.** I initially assumed `class Calc(Addable[int], Addable[float64]):` would work, since `impl`'s own duplicate check (`SameImpl`) does account for the type argument. But I tried it and it doesn't: the class-header trait list's duplicate check, unchanged since [Chapter 40](chapter-40.md), only compares trait *names*, so it rejects `Addable[int], Addable[float64]` as a duplicate before type arguments ever enter into it. And even sidestepping the header by using two separate `impl` blocks instead, both implementations of `Addable[T]` need a method literally named `add` — there's no per-instantiation mangling, so the second `impl`'s `def add` collides with the first's under the ordinary "Method 'add' is already defined on 'Calc'" redefinition error. I confirmed both failure modes directly rather than assume either worked.

**Type arguments must be concrete.** `ValueType::TypeVar` itself is rejected as a type argument, so a generic trait can't be implemented in terms of another trait's still-unresolved type parameter.

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
cd code/chapter-31
cmake -S . -B build && cmake --build build
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

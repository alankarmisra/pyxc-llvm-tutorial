---
description: "Add public and private visibility modifiers to class fields and methods. Private members are only accessible from within the class's own method bodies."
---
# 28. pyxc: Visibility

## What I Am Building

[Chapter 27](chapter-27.md) added constructors. Classes can now be initialized, but every field and method is accessible from anywhere. After this chapter, a class can hide its internals:

```pyxc
extern def printd(x: float64)

class BoundedCounter:
  private count: int
  private limit: int

  def __init__(max: int):
    self.count = 0
    self.limit = max

  public def increment():
    if self.count < self.limit:
      self.count = self.count + 1

  public def get() -> int:
    return self.count


def main() -> int:
  var c: BoundedCounter = BoundedCounter(3)
  c.increment()
  c.increment()
  c.increment()
  c.increment()    # no effect — limit reached
  printd(float64(c.get()))
  return 0
```

```text
3.000000
```

Accessing `c.count` directly from outside the class is an error.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-28
```

## Grammar

`class-member` gains an optional visibility prefix. `visibility` is a new production:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | struct-definition | class-definition | function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 class-definition        = "class" name ":" end-of-lines struct-block ;
 struct-block     = indent class-member { end-of-lines class-member } dedent ;
-class-member     = field-declaration | method-definition ;
+class-member     = [ visibility ] ( field-declaration | method-definition ) ;
+visibility      = "public" | "private" ;
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

## New Tokens

```cpp
tok_public = -41,
tok_private = -42,
```

Both are registered in the keyword table and in the token-name map, so error messages print `'public'` and `'private'`.

## Storing Visibility Information

Visibility lands in two places on `StructTypeInfo`. Fields gain an `IsPublic` flag directly on `StructFieldInfo`:

```cpp
struct StructFieldInfo {
  string Name;
  ValueType Type = ValueType::Error;
  string StructName;
  bool IsPublic = true;
};
```

Methods are tracked separately, in a map from method name to visibility:

```cpp
struct StructTypeInfo {
  string Name;
  bool IsClass = false;
  vector<StructFieldInfo> Fields;
  std::map<string, size_t> FieldIndex;
  std::map<string, bool> MethodIsPublic;
};
```

Methods need their own map rather than a flag next to the field, since a method's signature lives in `FunctionSignatures`, not in `StructTypeInfo::Fields` — there's no single struct visibility could hang off of the way `IsPublic` hangs off `StructFieldInfo`.

## Parsing Visibility Modifiers

The body loop inside `ParseAggregateDefinition` now reads an optional visibility token before deciding whether the member is a field or a method:

```cpp
bool MemberIsPublic = true;
bool HasVisibilityModifier = false;
if (CurrentToken == tok_public || CurrentToken == tok_private) {
  HasVisibilityModifier = true;
  MemberIsPublic = (CurrentToken == tok_public);
  getNextToken(); // eat visibility modifier
}
if (HasVisibilityModifier && !Info.IsClass) {
  LogErrorExpression("Visibility modifiers are only allowed inside class bodies");
  return false;
}
```

With no modifier, `MemberIsPublic` stays `true`: the default is public. A modifier inside a `struct` body is rejected immediately, before the parser even looks at what follows it.

A field's visibility rides along with everything else already pushed into `Info.Fields`:

```cpp
Info.Fields.push_back({FieldName, FieldType, FieldStructName, MemberIsPublic});
```

A method's visibility is passed as an extra argument into `ParseMethodDefinitionInClass`, which now takes `bool IsPublic` and records it directly:

```cpp
StructTypes[ClassName].MethodIsPublic[MethodName] = IsPublic;
```

`Info.MethodIsPublic` is copied back out of `StructTypes[StructName]` after every field (`Info.MethodIsPublic = StructTypes[StructName].MethodIsPublic;`), for the same reason [Chapter 26](chapter-26.md) already re-registers `StructTypes[StructName] = Info` after every member: methods parsed earlier in the body need to stay visible while later members are parsed, and vice versa.

## Enforcing Private Access

Access is decided by one small function:

```cpp
static string CurrentClassScopeName;

static bool CanAccessClassMember(const string &OwnerClass, bool IsPublic) {
  return IsPublic || (!CurrentClassScopeName.empty() &&
                      CurrentClassScopeName == OwnerClass);
}
```

A member is reachable if it's `public`, or if the code currently being parsed belongs to the same class the member is on. "Currently being parsed" is `CurrentClassScopeName`, set and restored by an RAII guard:

```cpp
struct ClassScopeGuard {
  string Saved;
  ClassScopeGuard(const string &ClassName) : Saved(CurrentClassScopeName) {
    CurrentClassScopeName = ClassName;
  }
  ~ClassScopeGuard() { CurrentClassScopeName = Saved; }
};
```

`ParseMethodDefinitionInClass` instantiates a `ClassScopeGuard` before parsing the method's body. When the method is done, the destructor restores whatever `CurrentClassScopeName` was before — empty at the top level, since pyxc has no nested classes to restore into instead.

## Access Checks at Every Use Site

`CanAccessClassMember` is checked wherever the parser resolves a class member, which turns out to be four places, not one:

**Field access, both existing field-chain parsers.** The auto-deref-capable `ParseFieldAccessFromFirstMember` from [Chapter 26](chapter-26.md) checks it inside its `ConsumeField` lambda:

```cpp
const auto &FD = SI->second.Fields[FI->second];
if (!CanAccessClassMember(CurStruct, FD.IsPublic)) {
  LogErrorExpression(
      ("Field '" + Field + "' is private on '" + CurStruct + "'").c_str());
  return false;
}
```

The older `ParseFieldAccessExpression` from [Chapter 18](chapter-18.md) — still used for field chains where the whole `.field` sequence is parsed from scratch rather than continuing from an already-consumed first member — gets the identical check inline in its own loop. Both paths reject reading *and* writing a private field, since assignment and read both resolve the field chain through one of these two functions before anything else happens.

**Method call**, in `ParseMethodCallExpression`, right after resolving `ClassName.MethodName`:

```cpp
auto MI = CI->second.MethodIsPublic.find(MethodName);
if (MI != CI->second.MethodIsPublic.end() &&
    !CanAccessClassMember(ClassName, MI->second)) {
  return LogErrorExpression(
      ("Method '" + MethodName + "' is private on '" + ClassName + "'")
          .c_str());
}
```

**Constructor call**, in `ParseNameExpressionWithName`, guarded by whether `__init__` exists at all:

```cpp
if (InitSignature) {
  auto MI = SI->second.MethodIsPublic.find("__init__");
  if (MI != SI->second.MethodIsPublic.end() &&
      !CanAccessClassMember(ParsedName, MI->second)) {
    return LogErrorExpression(
        ("Method '__init__' is private on '" + ParsedName + "'").c_str());
  }
}
```

A private `__init__` makes `ClassName(args)` fail from outside the class, the same way a private method or field would.

## IR Is Unchanged

Visibility is enforced entirely while parsing. Nothing changes in the generated IR: `public` and `private` leave no trace in the output. A `private` field and a `public` field of the same type generate identical IR.

## Known Limitations

**There is no `protected`.** Access is either class-private or world-public; there's no subclass-visible middle tier, since pyxc has no inheritance.

**Visibility modifiers on structs are rejected outright.** `struct` members are always public. The parser errors the moment it sees `public` or `private` before a struct member, rather than silently ignoring the modifier.

## Try It

**Private field, accessed from outside**

```pyxc
class Foo:
  private x: int

def main() -> int:
  var f: Foo
  f.x = 3
  return 0
```

```text
Error (Line 6, Column 7): Field 'x' is private on 'Foo'
```

**Private constructor, called from outside**

```pyxc
class Foo:
  x: int
  private def __init__():
    self.x = 0

def main() -> int:
  var f: Foo = Foo()
  return 0
```

```text
Error (Line 7, Column 20): Method '__init__' is private on 'Foo'
```

**Visibility modifier on a struct**

```pyxc
struct Foo:
  private x: int
```

```text
Error (Line 2, Column 11): Visibility modifiers are only allowed inside class bodies
```

## What's Next

[Chapter 29](chapter-29.md) adds traits: named contracts a class can declare it satisfies. Conformance is checked at compile time.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

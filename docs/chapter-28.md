---
description: "Add public and private visibility modifiers to class fields and methods. Private members are only accessible from within the class's own method bodies."
---
# 28. pyxc: Visibility

## Where We Are

[Chapter 27](chapter-27.md) added constructors. Classes can now be initialised, but every field and method is accessible from anywhere. After this chapter, a class can hide its internals:

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

```
3.000000
```

Accessing `c.count` directly from outside the class would be an error.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-28
```

## Grammar

`classmember` gains an optional visibility prefix. `visibility` is a new production.

```ebnf
classmember = [ visibility ] ( fielddecl | methoddef ) ;  -- changed
visibility  = "public" | "private" ;                      -- new
```

### Grammar

`code/chapter-28/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | struct-definition | class-definition | function-definition | decorated-function-definition | external | top-level-expression ;
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

## New Tokens

```cpp
tok_public  = -41,
tok_private = -42,
```

Both are registered in the keyword table:

```cpp
{"public",  tok_public},
{"private", tok_private},
```

They are also added to the token name map so error messages print `'public'` and `'private'`.

## Storing Visibility in `StructTypeInfo`

Visibility is stored on two places in `StructTypeInfo`:

**Fields** gain an `IsPublic` flag:

```cpp
struct FieldInfo {
  string Name;
  ValueType Type;
  string StructName;
  bool IsPublic = true;    // new — default public
};
```

**Methods** are tracked in a map from method name to boolean:

```cpp
struct StructTypeInfo {
  // ...
  std::map<string, bool> MethodIsPublic;  // new
};
```

Methods use a map rather than a flag on the prototype because the prototype lives in `FunctionProtos` and visibility is class metadata, not function metadata.

## Parsing Visibility Modifiers

In `ParseAggregateDefinition`, the body loop now reads an optional visibility token before each member:

```cpp
bool MemberIsPublic = true;
bool HasVisibilityModifier = false;
if (CurTok == tok_public || CurTok == tok_private) {
  HasVisibilityModifier = true;
  MemberIsPublic = (CurTok == tok_public);
  getNextToken(); // eat visibility modifier
}
if (HasVisibilityModifier && !Info.IsClass) {
  LogErrorExpression("Visibility modifiers are only allowed inside class bodies");
  return false;
}
```

If no modifier is present, `MemberIsPublic` stays `true` — the default is public. If the modifier appears inside a `struct` body, it is rejected immediately.

After parsing a field, the visibility is stored in `FieldInfo`:

```cpp
Info.Fields.push_back({FieldName, FieldType, FieldStructName, MemberIsPublic});
```

After parsing a method, the method's visibility is registered in `MethodIsPublic` by `ParseMethodDefinitionInClass` (which now takes `bool IsPublic` as a parameter):

```cpp
StructTypes[ClassName].MethodIsPublic[MethodName] = IsPublic;
```

The `StructTypes[StructName]` entry is written back after each member — `Info.MethodIsPublic = StructTypes[StructName].MethodIsPublic` — so the running map is always current as parsing proceeds.

## `CanAccessClassMember` and `ClassScopeGuard`

Access is decided by a single function:

```cpp
static string CurrentClassScopeName;

static bool CanAccessClassMember(const string &OwnerClass, bool IsPublic) {
  return IsPublic || (!CurrentClassScopeName.empty() &&
                      CurrentClassScopeName == OwnerClass);
}
```

A member is accessible if it is `public`, **or** if the code currently being compiled belongs to the same class. "Currently being compiled" is tracked by `CurrentClassScopeName`.

`ClassScopeGuard` sets and restores `CurrentClassScopeName` around method codegen:

```cpp
struct ClassScopeGuard {
  string Saved;
  ClassScopeGuard(const string &ClassName) : Saved(CurrentClassScopeName) {
    CurrentClassScopeName = ClassName;
  }
  ~ClassScopeGuard() { CurrentClassScopeName = Saved; }
};
```

`ParseMethodDefinitionInClass` creates a `ClassScopeGuard` before entering the body. When the method is done, the destructor restores the previous class scope (which is `""` at the top level, or the enclosing class if methods are somehow nested — though pyxc does not currently support nested classes).

## Access Checks at Every Use Site

`CanAccessClassMember` is inserted at every point where the compiler touches a class member:

**Field access** — in the `ConsumeField` lambda inside `ParseFieldAccessFromFirstMember`:

```cpp
if (!CanAccessClassMember(CurStruct, FD.IsPublic))
  return LogErrorExpression(("Field '" + Field + "' is private on '" + CurStruct + "'").c_str());
```

This fires for both read (`obj.x`) and write (`obj.x = v`) paths, because both go through `ParseFieldAccessFromFirstMember`.

**Method call** — in `ParseMethodCallExpr`, after looking up `ClassName.MethodName`:

```cpp
auto MI = CI->second.MethodIsPublic.find(MethodName);
if (MI != CI->second.MethodIsPublic.end() &&
    !CanAccessClassMember(ClassName, MI->second)) {
  return LogErrorExpression(("Method '" + MethodName + "' is private on '" + ClassName + "'").c_str());
}
```

**Constructor call** — in `ParseIdentifierExpr`, if `__init__` exists:

```cpp
auto MI = SI->second.MethodIsPublic.find("__init__");
if (MI != SI->second.MethodIsPublic.end() &&
    !CanAccessClassMember(IdName, MI->second)) {
  return LogErrorExpression(("Method '__init__' is private on '" + IdName + "'").c_str());
}
```

## IR Is Unchanged

Visibility is enforced entirely at parse and semantic check time. Nothing changes in the generated IR — `public` and `private` leave no trace in the output. A `private int` and a `public int` generate identical `i64` fields.

## Things Worth Knowing

**Default is public.** A member without a modifier is public. Existing code from chapters 25 and 26, which has no modifiers, continues to work exactly as before.

**`private __init__` prevents external construction.** If `__init__` is private, `ClassName(args)` from outside the class body is rejected.

**There is no `protected`.** Access is either class-private or world-public. No inheritance hierarchy, no friend declarations.

**Visibility modifiers on structs are rejected.** `struct` members are always public. The parser errors immediately if it sees `public` or `private` in a struct body.

## What's Next

[Chapter 29](chapter-29.md) adds traits — named contracts that a class can declare it satisfies. Conformance is checked at compile time.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

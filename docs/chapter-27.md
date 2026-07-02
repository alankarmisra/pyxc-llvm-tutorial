---
description: "Add public and private visibility modifiers to class fields and methods. Private members are only accessible from within the class's own method bodies."
---
# 27. pyxc: Visibility

## Where We Are

[Chapter 26](chapter-26.md) added constructors. Classes can now be initialised, but every field and method is accessible from anywhere. After this chapter, a class can hide its internals:

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
cd pyxc-llvm-tutorial/code/chapter-27
```

## Grammar

`classmember` gains an optional visibility prefix. `visibility` is a new production.

```ebnf
classmember = [ visibility ] ( fielddecl | methoddef ) ;  -- changed
visibility  = "public" | "private" ;                      -- new
```

### Full Grammar

`code/chapter-27/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | structdef | classdef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
structdef       = "struct" identifier ":" eols structblock ;
classdef        = "class" identifier ":" eols structblock ;
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
  LogError("Visibility modifiers are only allowed inside class bodies");
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
  return LogError(("Field '" + Field + "' is private on '" + CurStruct + "'").c_str());
```

This fires for both read (`obj.x`) and write (`obj.x = v`) paths, because both go through `ParseFieldAccessFromFirstMember`.

**Method call** — in `ParseMethodCallExpr`, after looking up `ClassName.MethodName`:

```cpp
auto MI = CI->second.MethodIsPublic.find(MethodName);
if (MI != CI->second.MethodIsPublic.end() &&
    !CanAccessClassMember(ClassName, MI->second)) {
  return LogError(("Method '" + MethodName + "' is private on '" + ClassName + "'").c_str());
}
```

**Constructor call** — in `ParseIdentifierExpr`, if `__init__` exists:

```cpp
auto MI = SI->second.MethodIsPublic.find("__init__");
if (MI != SI->second.MethodIsPublic.end() &&
    !CanAccessClassMember(IdName, MI->second)) {
  return LogError(("Method '__init__' is private on '" + IdName + "'").c_str());
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

[Chapter 28](chapter-28.md) adds traits — named contracts that a class can declare it satisfies. Conformance is checked at compile time.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

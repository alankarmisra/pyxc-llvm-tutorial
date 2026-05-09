---
description: "Add public and private visibility modifiers to class fields and methods. Private members are only accessible from within the class's own method bodies."
---
# 27. pyxc: Visibility

## Where We Are

[Chapter 26](chapter-26.md) added constructors. Classes can now be initialised, but every field and method is accessible from anywhere. After this chapter, a class can hide its internals:

```python
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

## New Keywords: `public` and `private`

```cpp
tok_public  = -41,
tok_private = -42,
```

Both are registered in the keyword table. They are only meaningful inside a class body; using them in any other context is a parse error.

## Storing Visibility

`StructTypeInfo` gains a boolean `IsPublic` on each `FieldInfo`, and a map from method name to boolean for methods:

```cpp
struct FieldInfo {
  string Name;
  ValueType Type;
  string StructName;
  bool IsPublic = true;    // new
};

struct StructTypeInfo {
  // ...
  std::map<string, bool> MethodIsPublic;  // new
};
```

The parser reads the optional `public`/`private` token before each member. If none is present, the member defaults to public.

## The Access Rule

The question at every field read, field write, and method call is: is the caller allowed to see this member?

```cpp
static string CurrentClassScopeName;

static bool CanAccessClassMember(const string &OwnerClass, bool IsPublic) {
  return IsPublic || (!CurrentClassScopeName.empty() &&
                      CurrentClassScopeName == OwnerClass);
}
```

A member is accessible if it is `public`, or if the code currently being compiled belongs to the same class. "Currently being compiled" is tracked by `CurrentClassScopeName`.

## Tracking the Current Class with `ClassScopeGuard`

When the compiler starts generating code for a method body, it needs to remember which class that method belongs to, so that `CanAccessClassMember` can grant access to private members. A RAII guard handles this:

```cpp
struct ClassScopeGuard {
  string Saved;
  ClassScopeGuard(const string &ClassName) : Saved(CurrentClassScopeName) {
    CurrentClassScopeName = ClassName;
  }
  ~ClassScopeGuard() { CurrentClassScopeName = Saved; }
};
```

`ParseMethodDefinitionInClass` creates a `ClassScopeGuard` before calling into the method body parser and codegen. When the method is done, the guard's destructor restores the previous class scope. Scopes nest correctly because each guard saves and restores independently.

## Where Checks Fire

Every point where the compiler accesses a member goes through `CanAccessClassMember`:

- **Field read** (`fieldaccess` in an expression): checks the field's `IsPublic` flag.
- **Field write** (`assignstmt` with a field lvalue): same check.
- **Method call** (`methodcallexpr`): checks `MethodIsPublic[MethodName]`.
- **Constructor call** (`ctorcallexpr`): if the class defines `__init__`, checks whether `__init__` is public before calling it.

## Structs Reject Visibility Modifiers

`public` and `private` are only meaningful on classes. Putting one in a `struct` body is a parse error:

```python
struct Pair:
  public x: int   # Error: visibility modifiers not allowed in struct
  y: int
```

Structs have no encapsulation concept — all their fields are always accessible. The parser detects the modifier-inside-struct case and rejects it immediately.

## IR Is Unchanged

Visibility is enforced entirely by the compiler's parser and semantic checks. Nothing changes in the generated IR — `public` and `private` leave no trace in the output. A `private int` and a `public int` generate identical `i64` fields.

## Things Worth Knowing

**Default is public.** A member without a modifier is public. Existing code from chapters 25 and 26, which has no modifiers, continues to work exactly as before.

**`private __init__` prevents construction from outside the class.** If `__init__` is private, `ClassName(args)` from an external call site is rejected.

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

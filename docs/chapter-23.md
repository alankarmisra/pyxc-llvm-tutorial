---
description: "Add type aliases so any type, scalar, pointer, or struct, can be given a readable name that vanishes completely from the generated IR."
---
# 23. pyxc: Type Aliases

## What I Am Building

[Chapter 22](chapter-22.md) gave me string literals, but only as `ptr[int8]`. That's accurate, but it's not what I want to keep typing every time a function takes or returns text. I want a name for it.

After this chapter:

```pyxc
type string = ptr[int8]

extern def puts(s: string) -> int

def greet(name: string) -> int:
  return puts(name)

def main() -> int:
  greet("world")
  return 0
```

```text
world
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-23
```

## Grammar

`top-level-item` gains `type-alias`, and `type` gains `alias-type`. `alias-type` and `struct-type` are both written identically, as a plain `name`; `ParseTypeToken` is what actually tells them apart, by trying `TypeAliases` before `StructTypes` and rejecting the name if neither lookup succeeds. Everything else is unchanged from [Chapter 22](chapter-22.md):

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
-top-level-item             = struct-definition | function-definition | external | top-level-expression ;
+top-level-item             = type-alias | struct-definition | function-definition | external | top-level-expression ;
+type-alias       = "type" name "=" type ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 struct-block     = indent field-declaration { end-of-lines field-declaration } dedent ;
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
 primary         = cast-expression | sizeof-expression | address-expression | string-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
 cast-expression        = cast-type "(" expression ")" ;
 sizeof-expression      = "sizeof" "(" type ")" ;
 address-expression        = "addr" "(" lvalue ")" ;
 name-expression  = name | call-expression ;
 call-expression        = name "(" [ expression { "," expression } ] ")" ;
 field-access     = name "." name { "." name } ;
 index-expression       = name "[" expression "]" ;
 number-expression      = number ;
 string-literal   = "\"" { ? any char except " and newline ? | escape } "\"" ;
 escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
 parenthesized-expression       = "(" expression ")" ;
 indent          = INDENT ;
 dedent          = DEDENT ;
 
 name      = (letter | "_") { letter | digit | "_" } ;
 builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
                 | "float" | "float32" | "float64"
                 | "bool" | "None" ;
+alias-type       = name ;
 struct-type      = name ;
 pointer-type     = "ptr" "[" type "]" ;
-type            = builtin-type | struct-type | pointer-type ;
+type            = builtin-type | alias-type | struct-type | pointer-type ;
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

## New Keyword: `type`

```cpp
tok_type = -39,
```

```cpp
{"type", tok_type}
```

## The Type-Alias Table

```cpp
static std::map<string, std::pair<ValueType, string>> TypeAliases;
```

An alias maps a name to a fully-resolved type: the same `(ValueType, StructName)` pair I already use everywhere a type needs a pointee or struct name attached, going back to [Chapter 18](chapter-18.md)'s structs. `TypeAliases` gets cleared alongside every other per-file symbol table in `ResetParserStateForFile`, so aliases don't leak from one compiled file into the next:

```cpp
static void ResetParserStateForFile() {
  FunctionSignatures.clear();
  StructTypes.clear();
  TypeAliases.clear();
  GlobalVarTypes.clear();
  GlobalVarStructTypes.clear();
  GlobalVarDecls.clear();
  VarScopes.clear();
  VarStructScopes.clear();
```

## Extending Type Parsing for Aliases

Every type annotation in pyxc, parameter types, return types, `var` declarations, `sizeof` operands, cast targets, goes through `ParseTypeToken`. That means I only have to teach alias resolution to one function for it to work everywhere else automatically.

Before this chapter, an unrecognized name here was just an error. Now I check `TypeAliases` first:

```cpp
case tok_name: {
  string TyName = Name;
  auto AliasIt = TypeAliases.find(TyName);
  if (AliasIt != TypeAliases.end()) {
    getNextToken();
    if (StructName)
      *StructName = AliasIt->second.second;
    return AliasIt->second.first;
  }
  if (!StructTypes.count(TyName)) {
    LogErrorExpression(("Unknown type '" + TyName + "'").c_str());
    return ValueType::Error;
  }
  getNextToken();
  if (StructName)
    *StructName = TyName;
  return ValueType::Struct;
}
```

If the name is a known alias, I return whatever it resolves to and I'm done; nothing downstream can even tell an alias was involved. If it isn't an alias, the old struct-name check runs exactly as before.

## Parsing the Definition Itself

```cpp
static bool ParseTypeAliasDefinition() {
  // CurrentToken is 'type'
  getNextToken(); // eat 'type'
  if (CurrentToken != tok_name) {
    LogErrorExpression("Expected alias name after 'type'");
    return false;
  }
  string AliasName = Name;
  if (TypeAliases.count(AliasName)) {
    LogErrorExpression(("Type alias '" + AliasName + "' is already defined").c_str());
    return false;
  }
  if (StructTypes.count(AliasName)) {
    LogErrorExpression(
        ("Name '" + AliasName + "' is already defined as a struct").c_str());
    return false;
  }
  getNextToken(); // eat alias name
  if (CurrentToken != tok_equal) {
    LogErrorExpression("Expected '=' in type alias");
    return false;
  }
  getNextToken(); // eat '='
  string AliasStructName;
  ValueType AliasType = ParseTypeToken(&AliasStructName);
  if (AliasType == ValueType::Error)
    return false;
  TypeAliases[AliasName] = {AliasType, AliasStructName};
  return true;
}
```

The right-hand side is parsed with the exact same `ParseTypeToken` I just extended, which means aliasing an alias works for free: `type Score = MyInt` resolves `MyInt` through the same lookup, and whatever `MyInt` already resolves to is what gets stored under `Score`. There's no chain kept around to walk later, by the time this function returns, `Score` and `MyInt`'s underlying type are indistinguishable.

`HandleTypeAliasDef` wraps this the same way every other top-level form is wrapped, and `tok_type` is wired into both the REPL and file-mode dispatch switches.

## Conflict Rules

Aliases and structs share one namespace, so I check both directions.

Defining the same alias twice:

```pyxc
type Foo = int
type Foo = int64
```

```text
Error (Line 2, Column 6): Type alias 'Foo' is already defined
type Foo 
     ^~~~
```

An alias colliding with an existing struct:

```pyxc
struct Foo:
  x: int
type Foo = int
```

```text
Error (Line 3, Column 6): Name 'Foo' is already defined as a struct
type Foo 
     ^~~~
```

And the reverse, a struct colliding with an existing alias, checked inside `ParseStructDefinition` itself:

```cpp
if (TypeAliases.count(StructName)) {
  LogErrorExpression(("Name '" + StructName + "' is already defined as a type alias")
               .c_str());
  return false;
}
```

```pyxc
type Foo = int
struct Foo:
  x: int
```

```text
Error (Line 2, Column 8): Name 'Foo' is already defined as a type alias
struct Foo:
       ^~~~
```

There's no forward reference either way. An alias has to exist in `TypeAliases` at the moment its name is looked up, and that lookup only ever happens while parsing something that comes after the `type` line:

```pyxc
def use_it(x: Meters) -> Meters:
  return x
type Meters = int64
```

```text
Error (Line 1, Column 15): Unknown type 'Meters'
def use_it(x: Meters)
              ^~~~
```

## IR Transparency

An alias produces no IR of its own. It's resolved entirely while parsing, so by the time code generation runs, there's nothing left that knows the alias ever existed.

```pyxc
type Score = int64

def id(x: Score) -> Score:
  return x
```

```llvm
define i64 @id(i64 %x) {
```

`Score` doesn't appear anywhere; LLVM sees `i64`, exactly as if I'd written `int64` directly. Chaining aliases doesn't change this:

```pyxc
type MyInt = int
type Score = MyInt

def id(x: Score) -> Score:
  return x
```

```llvm
define i64 @id(i64 %x) {
```

Same IR. `Score` resolved through `MyInt` down to `int` at the moment `type Score = MyInt` was parsed, so there's no chain left to collapse later, there was never a chain to begin with once parsing moved on.

## Build and Run

```bash
cd code/chapter-23
cmake -S . -B build && cmake --build build
```

## Try It

### `string` as a parameter type

```pyxc
extern def puts(s: ptr[int8]) -> int

type string = ptr[int8]

def greet(name: string) -> int:
  return puts(name)

def main() -> int:
  greet("world")
  return 0
```

```text
world
```

### Aliasing a struct

```pyxc
extern def printd(x: float64)

struct Point:
  x: int
  y: int

type Vec2 = Point

def main() -> int:
  var v: Vec2
  v.x = 3
  v.y = 4
  printd(float64(v.x))
  return 0
```

```text
3.000000
```

`Vec2` behaves exactly like `Point` everywhere: as a `var` type, and for field access.

### Inspecting the IR

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'define' out.ll
```

For the `Score` example above:

```text
define i64 @id(i64 %x)
```

## Known Limitations

**No forward references.** An alias has to be defined before anything uses it. `type List = ptr[List]` fails because `List` doesn't exist yet in `TypeAliases` when the right-hand side is parsed.

**No recursive aliases**, for the same reason: a self-referential definition would need the forward reference this chapter doesn't support.

**Aliases are purely syntactic.** There's no nominal typing anywhere in this. `Score` and `int64` are one type as far as the compiler is concerned, so a function expecting `Score` accepts a plain `int64` without complaint.

**No parameterized aliases.** `type Pair[T] = ...` isn't supported; type parameters are out of scope for this chapter.

**No scoping.** Every alias is global to the module. There's no way to limit one to a single function or file.

## What's Next

[Chapter 24](chapter-24.md) adds fixed-size arrays: `T[N]` types, stack allocation, indexing, and array literals.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

---
description: "Add type aliases so any type — scalar, pointer, or struct — can be given a readable name that vanishes completely from the generated IR."
---
# 22. pyxc: Type Aliases

## Where We Are

[Chapter 21](chapter-21.md) added string literals. We can write `"hello"` and pass it to `puts`. But the parameter type is a C-style `ptr[int8]` which is a bit annoying to write all the time. After this chapter we can write:

```pyxc
type string = ptr[int8]

extern def puts(s: string) -> int

def greet(name: string) -> int:
  return puts(name)

def main() -> int:
  greet("world")
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-22
```

## Grammar

This chapter adds two new productions (`typealias`, `aliastype`) and extends two existing ones (`top` gains a `typealias` alternative; `type` gains an `aliastype` alternative).

`code/chapter-22/pyxc.ebnf`

```ebnf
program    = [ eols ] [ top { eols top } ] [ eols ] ;
top        = typealias | structdef | definition | decorateddef | external | toplevelexpr ; -- new
typealias  = "type" identifier "=" type ;                                                  -- new
...
type       = builtintype | aliastype | structtype | pointertype ;                          -- new (aliastype)
aliastype  = identifier ;                                                                  -- new
```

`aliastype` and `structtype` are both written as `identifier`. The parser tries `TypeAliases` first, then `StructTypes`, and rejects the identifier if neither lookup succeeds.

### Full Grammar

`code/chapter-22/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | structdef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
structdef       = "struct" identifier ":" eols structblock ;
structblock     = indent fielddecl { eols fielddecl } dedent ;
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
primary         = castexpr | sizeofexpr | addrexpr | stringliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
castexpr        = casttype "(" expression ")" ;
sizeofexpr      = "sizeof" "(" type ")" ;
addrexpr        = "addr" "(" lvalue ")" ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
fieldaccess     = identifier "." identifier { "." identifier } ;
indexexpr       = identifier "[" expression "]" ;
numberexpr      = number ;
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
type            = builtintype | aliastype | structtype | pointertype ;
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
## New Keyword: `type`

```cpp
tok_type = -39,
```

Registered in the keyword table:

```cpp
{"type", tok_type}
```

## The `TypeAliases` Map

```cpp
static std::map<string, std::pair<ValueType, string>> TypeAliases;
```

This maps an alias name to the fully-resolved type it stands for. The pair is `(ValueType, StructName)`, you will notice, is the same two-field representation used in [chapter 18](chapter-18.md). 

`TypeAliases.clear()` is called at the start of each new module (inside `ResetParserStateForFile`), alongside `StructTypes.clear()`. For now, aliases do not persist across compilation units. This will change once we get into multi-file and `import` territory.

## Extending `ParseTypeToken`

`ParseTypeToken` is the single function that all type annotations in the language go through — parameter types, return types, `var` declarations, `sizeof` operands, cast targets. That means adding alias resolution in one place makes aliases work everywhere.

Before this chapter, an unknown identifier in `ParseTypeToken` was an immediate error. Now there is a lookup before the error:

```cpp
case tok_identifier: {
  string TyName = IdentifierStr;
  auto AliasIt = TypeAliases.find(TyName);
  if (AliasIt != TypeAliases.end()) {
    getNextToken();
    if (StructName)
      *StructName = AliasIt->second.second;
    return AliasIt->second.first;
  }
  if (!StructTypes.count(TyName)) {
    LogError(("Unknown type '" + TyName + "'").c_str());
    return ValueType::Error;
  }
  getNextToken();
  if (StructName)
    *StructName = TyName;
  return ValueType::Struct;
}
```

If the identifier matches an alias, the resolved type and struct name are returned directly. No other part of the compiler needs to change — the alias is transparent from this point on.

## `ParseTypeAliasDefinition`

```cpp
static bool ParseTypeAliasDefinition() {
  getNextToken(); // eat 'type'
  // expect identifier
  string AliasName = IdentifierStr;
  if (TypeAliases.count(AliasName))
    return LogError("Type alias 'X' is already defined");
  if (StructTypes.count(AliasName))
    return LogError("Name 'X' is already defined as a struct");
  getNextToken(); // eat alias name
  // expect '='
  getNextToken(); // eat '='
  string AliasStructName;
  ValueType AliasType = ParseTypeToken(&AliasStructName);
  if (AliasType == ValueType::Error)
    return false;
  TypeAliases[AliasName] = {AliasType, AliasStructName};
  LastTopLevelEndedWithBlock = false;
  return true;
}
```

The parser eats `type`, validates the alias name against both `TypeAliases` and `StructTypes`, eats the `=`, then calls `ParseTypeToken` to resolve the right-hand side. Whatever `ParseTypeToken` returns — after fully resolving any chain of aliases — is stored directly. There is no stored pointer to the original name.

`HandleTypeAliasDef` wraps this in the standard top-level handler and is wired into both the file-mode and REPL-mode dispatch loops under `tok_type`.

Resolution happens at definition time, not at use time. When `type Score = MyInt` is processed, `ParseTypeToken("MyInt")` runs immediately and looks up `MyInt` in `TypeAliases`. If `MyInt` is already defined as `(Int, "")`, then `Score` is stored as `(Int, "")`. There is no indirection at use time — `Score` and `int64` are identical to the compiler from the moment the alias is defined.


## Conflict Rules

The name spaces for aliases and struct types are shared. Three conflicts are checked:

**Alias redefinition.** Defining the same alias name twice is an error:

```
type Foo = int
type Foo = int64   → Error: Type alias 'Foo' is already defined
```

**Alias name collides with a struct.** If a struct is already defined under that name, the alias is rejected:

```
struct Foo:
  x: int
type Foo = int     → Error: Name 'Foo' is already defined as a struct
```

**Struct name collides with an alias.** `ParseStructDefinition` checks `TypeAliases` before accepting the struct name:

```
type Foo = int
struct Foo:        → Error: Name 'Foo' is already defined as a type alias
  x: int
```

Forward references are not supported. Using an alias before defining it gives "Unknown type 'X'" — `ParseTypeToken` only searches entries that already exist in the map.

## IR Transparency

Aliases produce no IR. They are resolved entirely during parsing and leave no trace in the generated output.

```pyxc
type Score = int64

def id(x: Score) -> Score:
  return x
```

```llvm
define i64 @id(i64 %x) {
entry:
  %x.addr = alloca i64
  store i64 %x, ptr %x.addr
  %x1 = load i64, ptr %x.addr
  ret i64 %x1
}
```

`Score` does not appear. LLVM sees `i64` exactly as if `int64` had been written directly. The same holds for pointer aliases:

```pyxc
type string = ptr[int8]

def say(msg: string) -> int:
  return puts(msg)

def greeting() -> string:
  return "hello"
```

The IR for both functions is identical to what you would get with `ptr[int8]` in every annotation.

## Build and Run

```bash
cd code/chapter-22
cmake -S . -B build && cmake --build build
```

## Try It

### `string` as a type

```pyxc
extern def puts(s: ptr[int8]) -> int

type string = ptr[int8]

def greet(name: string) -> int:
  return puts(name)

def main() -> int:
  greet("world")
  return 0
```

```bash
world
```

`string` is accepted as a parameter type and return type. The IR uses `ptr` throughout.

### IR transparency

```pyxc
type Score = int64

def id(x: Score) -> Score:
  return x
```

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'define' out.ll
```

```
define i64 @id(i64 %x)
```

`Score` is gone. The function signature is plain `i64`.

### Alias chain

```pyxc
type MyInt = int
type Score = MyInt
```

`Score` resolves to `(Int, "")` at definition time. `type Score = MyInt` calls `ParseTypeToken("MyInt")`, which finds the alias and returns `(Int, "")` immediately. The chain is collapsed to a single lookup in the map.

### Alias for a struct type

```pyxc
struct Point:
  x: int
  y: int

type Vec2 = Point
```

After this, `Vec2` can be used as a parameter type, return type, or `var` type, and the compiler treats it exactly like `Point`.

### Forward reference error

```pyxc
def use_it(x: Meters) -> Meters:
  return x

type Meters = int64
```

```
Error: Unknown type 'Meters'
```

The alias must be defined before it is used. There are no forward references.

## Known Limitations

**No forward references.** The alias must appear before any use. `type List = ptr[List]` would fail because `List` is not yet in `TypeAliases` when the right-hand side is parsed.

**No recursive aliases.** A consequence of no forward references — self-referential alias definitions are not possible.

**Aliases are purely syntactic.** There is no nominal typing. `Score` and `int64` are the same type to the compiler; a function expecting `Score` will accept an `int64` without complaint.

**No parameterized aliases.** `type Pair[T] = ...` is not supported. Type parameters are outside the scope of this chapter.

**No re-export or scoping.** All aliases are global to the module. There is no way to limit visibility of an alias to a single function.

## What's Next

[Chapter 23](chapter-23.md) adds fixed-size arrays (`T[N]`), stack allocation, indexing, and array literals — completing the types and memory phase.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

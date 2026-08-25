---
description: "Add type aliases so any type, scalar, pointer, or struct, can be given a readable name that vanishes completely from the generated IR."
---
# 29. pyxc: Type Aliases

## What I Am Building

[Chapter 28](chapter-28.md) gave me the heap: `malloc`, `free`, `sizeof`, and pointer casts. Between structs, pointers, and now heap-allocated memory, my type annotations are getting long and repetitive — `ptr[Node]` shows up everywhere. I want a name for it.

After this chapter:

<!-- code-merge:start -->
```pyxc
extern def printd(x: float64)

struct Point:
  x: int
  y: int

type Vec2 = Point

def magnitude_sq(v: Vec2) -> int:
  return v.x * v.x + v.y * v.y

def main() -> int:
  var p: Vec2
  p.x = 3
  p.y = 4
  printd(float64(magnitude_sq(p)))
  return 0
```
```text
25.000000
```
<!-- code-merge:end -->

`Vec2` is a `Point` in every way that matters — same fields, same layout, same struct underneath — it just has a name I find more meaningful in this context. This is also what sets up [Chapter 30](chapter-30.md): once string literals exist, `type string = ptr[int8]` gives the byte-pointer C functions expect a name I don't have to keep spelling out.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-29
```

## Grammar

`top-level-item` gains `type-alias`, and `type` gains `alias-type`. `alias-type` and `struct-type` are both written identically, as a plain `name`; `ParseTypeToken` is what actually tells them apart, by trying `TypeAliases` before `StructTypes` and rejecting the name if neither lookup succeeds. Everything else is unchanged from [Chapter 30](chapter-30.md):

```grammardiff
*...
*end-of-lines                      = end-of-line { end-of-line } ;
*top-level-item                    = function-definition
+                                    | type-alias
*                                    | struct-definition
*                                    | external
*                                    | top-level-statement ;
*struct-definition                 = "struct" name ":" end-of-lines
*                                    struct-block ;
+type-alias                        = "type" name "=" type ;
*struct-block                      = indent field-declaration
*                                    { end-of-lines field-declaration } dedent ;
*...
*                                    { letter | digit | "_" } ;
*type                              = base-type [ array-suffix ] ;
-base-type                         = builtin-type | struct-type | pointer-type ;
+base-type                         = builtin-type | alias-type | struct-type
+                                    | pointer-type ;
*pointer-type                      = "ptr" "[" type "]" ;
*array-suffix                      = "[" integer "]" ;
*...
*                                    | "float64" | "bool" | "None" ;
*struct-type                       = name ;
+alias-type                        = name ;
*cast-type                         = builtin-cast-type | pointer-type ;
*builtin-cast-type                 = "int" | "int8" | "int16" | "int32"
*...
```

## New Keyword: `type`

```cppdiff
*enum Token {
*  ...
*  tok_ptr = -49,
*  tok_addr = -50,
*  tok_sizeof = -51,
+  tok_type = -52,
*
*  // punctuation and operators
*  ...
*};
```

```cppdiff
*static map<string, Token> Keywords = {
*    ...
*    {"ptr", tok_ptr},         {"addr", tok_addr},
*    {"sizeof", tok_sizeof},
+    {"type", tok_type},
*    {"float", tok_float},
*    ...
*};
```

## The Type-Alias Table

```cpp
static std::map<string, std::pair<ValueType, string>> TypeAliases;
```

An alias maps a name to a fully-resolved type: the same `(ValueType, StructName)` pair I already use everywhere a type needs a pointee or struct name attached, going back to [Chapter 24](chapter-24.md)'s structs. `TypeAliases` gets cleared alongside every other per-file symbol table in `ResetParserStateForFile`, so aliases don't leak from one compiled file into the next:

```cppdiff
*static void ResetParserStateForFile() {
*  FunctionSignatures.clear();
*  StructTypes.clear();
+  TypeAliases.clear();
*  GlobalVarTypes.clear();
*  GlobalVarStructNames.clear();
*  GlobalVarDecls.clear();
*  VarScopes.clear();
*  VarStructScopes.clear();
*  ...
*}
```

## Extending Type Parsing for Aliases

Every type annotation in pyxc, parameter types, return types, `var` declarations, `sizeof` operands, cast targets, goes through `ParseTypeToken`. That means I only have to teach alias resolution to one function for it to work everywhere else automatically.

Before this chapter, an unrecognized name here was just an error. Now I check `TypeAliases` first:

```cppdiff
*static ValueType ParseTypeToken(string *StructName) {
*  ...
*  switch (CurrentToken) {
*  ...
*  case tok_name: {
+    auto Alias = TypeAliases.find(Name);
+    if (Alias != TypeAliases.end()) {
+      BaseType = Alias->second.first;
+      BaseTypeInfo = Alias->second.second;
+      getNextToken();
+      break;
+    }
*    auto Found = StructTypes.find(Name);
*    if (Found == StructTypes.end()) {
-      LogErrorExpression(("Unknown struct type '" + Name + "'"));
+      LogErrorExpression(("Unknown type '" + Name + "'"));
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

If the name is a known alias, I set `BaseType`/`BaseTypeInfo` to whatever it resolves to and break out of the case; nothing downstream can even tell an alias was involved. If it isn't an alias, the old struct-name check runs exactly as before.

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
    LogErrorExpression(("Type alias '" + AliasName + "' is already defined"));
    return false;
  }
  if (StructTypes.count(AliasName)) {
    LogErrorExpression(
        ("Name '" + AliasName + "' is already defined as a struct").c_str());
    return false;
  }
  getNextToken(); // eat alias name
  if (CurrentToken != tok_assign) {
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

```cppdiff
*static bool ParseStructDefinition() {
*  getNextToken(); // eat 'struct'
*  if (CurrentToken != tok_name) {
*    LogErrorExpression("Expected name after 'struct'");
*    return false;
*  }
*  string StructName = Name;
+  if (TypeAliases.count(StructName)) {
+    LogErrorExpression(("Type '" + StructName +
+                        "' is already defined as a type alias")
+                           .c_str());
+    return false;
+  }
*  if (StructTypes.count(StructName)) {
*    LogErrorExpression("Struct already defined");
*    return false;
*  }
*  ...
*}
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
cd code/chapter-29
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## Try It

### `string` as a Parameter Type

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

### Aliasing a Struct

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

[Chapter 30](chapter-30.md) adds string literals and real C library calls.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

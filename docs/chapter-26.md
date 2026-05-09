---
description: "Add constructors: define __init__ to initialise a class instance, and call it with ClassName(args) syntax. Instances are always zero-initialised before __init__ runs."
---
# 26. pyxc: Constructors

## Where We Are

[Chapter 25](chapter-25.md) added methods. You can define behaviour on a class and call it through `obj.method(args)`. But creating a class instance requires writing field assignments by hand:

```python
var c: Calc
c.x = 3
c.y = 4
```

After this chapter, a class can define `__init__` to package that work up, and callers use `ClassName(args)` to create a ready-to-use instance in one expression:

```python
extern def printd(x: float64)

class Point:
  x: int
  y: int

  def __init__(px: int, py: int):
    self.x = px
    self.y = py

  def sum() -> int:
    return self.x + self.y


def main() -> int:
  var p: Point = Point(3, 4)
  printd(float64(p.sum()))
  return 0
```

```
7.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-26
```

## Grammar

`ctorcallexpr` is added to `identifierexpr`. It is syntactically identical to `callexpr` — both are an identifier followed by `(args)`. The parser disambiguates by checking whether the identifier names a known class.

```ebnf
identifierexpr = identifier | callexpr | methodcallexpr | ctorcallexpr ;  -- changed
ctorcallexpr   = identifier "(" [ expression { "," expression } ] ")" ;   -- new
```

### Full Grammar

`code/chapter-26/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | structdef | classdef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
structdef       = "struct" identifier ":" eols structblock ;
classdef        = "class" identifier ":" eols structblock ;
structblock     = indent classmember { eols classmember } dedent ;
classmember     = fielddecl | methoddef ;
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

## Defining `__init__`

`__init__` is a method named literally `__init__`. It is defined the same way as any other method — inside the class body, with `def __init__(params):`. The compiler enforces one constraint: `__init__` must return `None`. Returning a value from a constructor is an error.

```python
class Rect:
  w: int
  h: int

  def __init__(width: int, height: int):
    self.w = width
    self.h = height
```

`__init__` is optional. A class without it still works — instances are zero-initialised by default.

## Constructor Call Syntax

`ClassName(args)` at the call site looks identical to a regular function call. The expression parser checks whether the identifier names a known class. If it does, it builds a `ConstructorCallExprAST` instead of a `CallExprAST`.

```python
var r: Rect = Rect(10, 20)
```

This is a zero-argument constructor call for a class without `__init__`:

```python
var p: Point = Point()
```

Both produce a stack-allocated instance of the class.

## What the Constructor Call Does at Runtime

`ConstructorCallExprAST::codegen` does three things in order:

1. **Allocate in the function's entry block.** A temporary named `ctor.tmp` is allocated with `CreateEntryBlockAlloca`. Allocating in the entry block (not at the call site) is critical: LLVM's `mem2reg` pass can only promote allocas that are in the entry block. An alloca elsewhere would defeat optimisation and grow the stack in loops.

2. **Zero-initialise.** The entire struct is zeroed before `__init__` runs:
   ```cpp
   Builder->CreateStore(ZeroConstant(ValueType::Struct, ClassName), Tmp);
   ```
   This guarantees that any field not touched by `__init__` starts at a defined value, not garbage.

3. **Call `__init__` if it exists.** The alloca pointer (`ctor.tmp`) is passed as `self`. The user-supplied arguments follow. After the call returns, the value of `ctor.tmp` is loaded and becomes the result of the constructor expression.

If there is no `__init__`, steps 1 and 2 still happen — you get a zero-initialised instance.

## Default Zero Initialisation

A class without `__init__` still produces a fully zeroed instance on construction:

```python
class Config:
  debug: bool
  level: int

var cfg: Config = Config()
# cfg.debug is False, cfg.level is 0
```

This is a guarantee, not an accident. The zero store always runs before any `__init__` call, and runs even when there is no `__init__`.

## IR

```python
class Point:
  x: int
  y: int

  def __init__(px: int, py: int):
    self.x = px
    self.y = py
```

A call `Point(3, 4)` generates roughly:

```llvm
; In the entry block of the calling function:
%ctor.tmp = alloca %Point

; At the call site:
store %Point zeroinitializer, ptr %ctor.tmp
call void @Point.__init__(ptr %ctor.tmp, i64 3, i64 4)
%result = load %Point, ptr %ctor.tmp
```

The result is a value (not a pointer) — the loaded struct is copied into the destination variable's alloca.

## Things Worth Knowing

**`__init__` must return `None`.** Attempting to give it a return type annotation is a parse-time error.

**`__init__` is a regular method.** It can call other methods via `self`, access all fields, and use any other class feature. It is not special beyond its name and the "must return None" rule.

**No overloading.** Only one `__init__` per class. If you define it twice, the second definition is a redefinition error.

**`ClassName()` with no `__init__` is always valid.** It produces a zero-initialised instance. `ClassName(args)` with arguments but no `__init__` is an error — there is nobody to receive the arguments.

## What's Next

[Chapter 27](chapter-27.md) adds visibility — `public` and `private` modifiers on class fields and methods, enforced at every access site.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

---
description: "Add methods to classes: define functions inside the class body, call them with obj.method(args), and mutate receiver state through an implicit self pointer."
---
# 25. pyxc: Methods and `self`

## Where We Are

[Chapter 24](chapter-24.md) added the `class` keyword. Classes can have fields and you can read and write them, but all behaviour lives in global functions. After this chapter, behaviour lives with the data:

```pyxc
extern def printd(x: float64)

class Counter:
  value: int

  def increment():
    self.value = self.value + 1

  def get() -> int:
    return self.value


def main() -> int:
  var c: Counter
  c.increment()
  c.increment()
  c.increment()
  printd(float64(c.get()))
  return 0
```

```
3.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-25
```

## Grammar

`structblock` now contains `classmember` instead of just `fielddecl`. A class member is either a field or a method. `methodcallexpr` is added to `identifierexpr`.

```ebnf
structblock    = indent classmember { eols classmember } dedent ;  -- changed
classmember    = fielddecl | methoddef ;                           -- new
methoddef      = "def" identifier "(" [ typedparam { "," typedparam } ] ")"
                 [ "->" type ] ":" ( simplestmt | eols block ) ;  -- new
identifierexpr = identifier | callexpr | methodcallexpr ;          -- changed
methodcallexpr = identifier "." identifier "(" [ expression { "," expression } ] ")" ;  -- new
```

Note that `self` is not in the grammar at all — it is injected automatically by the compiler, not written by the programmer.

### Full Grammar

`code/chapter-25/pyxc.ebnf`

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
identifierexpr  = identifier | callexpr | methodcallexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
methodcallexpr  = identifier "." identifier "(" [ expression { "," expression } ] ")" ;
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

## Defining a Method

Inside a class body, `def methodname(params) -> returntype:` defines a method. The programmer does not write `self` — it is invisible in the source. The parser adds it automatically to every method signature.

`ParseMethodDefinitionInClass` handles this. It synthesises a `self` parameter at position 0 before any user-declared parameters:

```cpp
static unique_ptr<FunctionAST>
ParseMethodDefinitionInClass(const string &ClassName) {
  // Inject implicit self as ptr[ClassName]
  ArgList.push_back(
      {"self", ValueType::Pointer,
       EncodePointerType(ValueType::Struct, ClassName)});
  // ... parse user parameters, return type, body ...
}
```

`self` is typed as `ptr[ClassName]` — a pointer to the receiver. This is what allows methods to mutate the object's fields: field accesses through `self.x` load from or store to the receiver's memory, not a copy.

## Method Mangling

Methods are stored in `FunctionProtos` under a mangled name: `ClassName.MethodName`. A method `def add(x: int, y: int) -> int:` on class `Calc` is stored as `"Calc.add"` and emitted as `@Calc.add` in the IR.

This means method names can collide with global function names freely — `Calc.add` and `add` are entirely distinct entries. It also means two classes can both have a method named `add` without conflict.

## Calling a Method

```pyxc
c.increment()
result = c.get()
```

The call site `receiver.method(args)` is parsed when the expression parser sees an identifier followed by `.` followed by another identifier followed by `(`. `ParseMethodCallExpr` takes over:

1. Confirms the receiver is a known class type.
2. Looks up `ClassName.MethodName` in `FunctionProtos`.
3. Prepends the receiver's address as the first argument using `AddrExprAST`.

```cpp
// implicit self: pass receiver address
Args.push_back(make_unique<AddrExprAST>(
    Var->getName(), vector<string>{},
    EncodePointerType(ValueType::Struct, ClassName)));
```

The receiver must be an lvalue (a named local or a field path). Calling a method on a temporary value or a function return is not allowed, because taking the address of an rvalue is not valid.

## `self` Inside the Method Body

Inside a method body, `self` is in scope as a local variable of type `ptr[ClassName]`. Field accesses through `self.x` are field accesses on a pointer:

```pyxc
def increment():
  self.value = self.value + 1
```

The compiler sees `self.value` as a GEP into the pointer that `self` holds, followed by a load or store. This is the same codegen path used for `ptr[T]` field access from chapter 18.

## What the IR Looks Like

```pyxc
class Calc:
  value: int

  def add(x: int, y: int) -> int:
    return x + y
```

```llvm
%Calc = type { i64 }

define i64 @Calc.add(ptr %self, i64 %x, i64 %y) {
entry:
  ; self, x, y are alloca'd and stored as usual
  %addtmp = add i64 %x.val, %y.val
  ret i64 %addtmp
}
```

The `self` pointer is the first argument, even though the programmer did not write it. A call site `c.add(3, 4)` emits `call i64 @Calc.add(ptr %c.addr, i64 3, i64 4)`.

## Things Worth Knowing

**Methods are only allowed on classes, not structs.** Defining a `def` inside a `struct` body is an error.

**`self` cannot be named by the programmer.** Writing a parameter called `self` in a method definition is rejected: "Method parameters cannot be named 'self'". The compiler owns that name.

**Method calls require an lvalue receiver.** `Calc().add(1, 2)` is not yet valid — there is no temporary materialisation. Use a `var` declaration first.

## What's Next

[Chapter 26](chapter-26.md) adds constructors — `__init__` methods that initialise a new instance, called with `ClassName(args)` syntax.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

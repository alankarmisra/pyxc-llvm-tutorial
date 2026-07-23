---
description: "Add constructors: define __init__ to initialise a class instance, and call it with ClassName(args) syntax. Instances are always zero-initialised before __init__ runs."
---
# 27. pyxc: Constructors

## Where We Are

[Chapter 26](chapter-26.md) added methods. You can define behaviour on a class and call it through `obj.method(args)`. But creating a class instance requires writing field assignments by hand:

```pyxc
var c: Calc
c.x = 3
c.y = 4
```

After this chapter, a class can define `__init__` to package that work up, and callers use `ClassName(args)` to create a ready-to-use instance in one expression:

```pyxc
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
cd pyxc-llvm-tutorial/code/chapter-27
```

## Grammar

`ctorcallexpr` is added to `identifierexpr`. It is syntactically identical to `callexpr` — both are an identifier followed by `(args)`. The parser disambiguates by checking whether the identifier names a known class.

```ebnf
identifierexpr = identifier | callexpr | methodcallexpr | ctorcallexpr ;  -- changed
ctorcallexpr   = identifier "(" [ expression { "," expression } ] ")" ;   -- new
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

## New AST Node — `ConstructorCallExprAST`

A constructor call `Point(3, 4)` is not the same as a function call `foo(3, 4)` — it allocates memory, zeroes it, may call `__init__`, and returns a struct value. A dedicated AST node captures this:

```cpp
class ConstructorCallExprAST : public ExprAST {
  string ClassName;
  vector<unique_ptr<ExprAST>> Args;
public:
  ConstructorCallExprAST(const string &ClassName,
                         vector<unique_ptr<ExprAST>> Args)
      : ClassName(ClassName), Args(std::move(Args)) {
    setType(ValueType::Struct, ClassName);  // result type is the class itself
  }
  Value *codegen() override;
};
```

The result type is `ValueType::Struct` with `ClassName` as the struct name — the same type you get from `var p: Point`.

## Disambiguating Constructor Calls at Parse Time

In `ParseIdentifierExpr`, when the parser sees `identifier(`, it now checks whether the identifier is a known class before deciding what to build. The check runs before the existing function-call path:

```cpp
// Constructor call: ClassName(...)
auto SI = StructTypes.find(IdName);
if (SI != StructTypes.end() && SI->second.IsClass) {
  getNextToken(); // eat '('
  string InitName = IdName + ".__init__";
  PrototypeAST *InitProto = GetFunctionProto(InitName);

  vector<unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    size_t ArgIndex = 0;
    while (true) {
      // Set expected type from __init__ prototype (skipping self at index 0)
      ValueType Expected = ValueType::Error;
      string ExpectedStructName;
      if (InitProto && ArgIndex + 1 < InitProto->getNumArgs()) {
        Expected = InitProto->getArgType(ArgIndex + 1);
        ExpectedStructName = InitProto->getArgStructName(ArgIndex + 1);
      }
      ExpectedLiteralTypeGuard Guard(Expected, ExpectedStructName);
      auto Arg = ParseExpression();
      Args.push_back(std::move(Arg));
      if (CurTok == ')') break;
      getNextToken(); // eat ','
      ++ArgIndex;
    }
  }
  getNextToken(); // eat ')'

  // Validate arg count and types against __init__ (minus self)
  if (InitProto) {
    size_t ExpectedArgs = InitProto->getNumArgs() > 0
                            ? InitProto->getNumArgs() - 1 : 0;
    if (Args.size() != ExpectedArgs)
      return LogError("Incorrect # arguments passed");
    // ...type check each arg...
  } else if (!Args.empty()) {
    return LogError("Class has no constructor; expected zero arguments");
  }
  return make_unique<ConstructorCallExprAST>(IdName, std::move(Args));
}

// Function call (falls through here if not a class)
```

If the class has `__init__`, argument count and types are checked against the prototype (minus the implicit `self` at index 0). If there is no `__init__`, any non-empty argument list is an error.

## `__init__` Must Return None

`ParseMethodDefinitionInClass` validates that `__init__` does not declare a return type:

```cpp
if (MethodName == "__init__" && RetType != ValueType::None)
  return LogErrorF("Constructor '__init__' must return None");
```

This check runs after parsing the optional `-> type` return annotation and before parsing the body. `__init__` always returns `None` — it cannot return a value.

## `ConstructorCallExprAST::codegen` — Allocate, Zero, Call, Load

The codegen for a constructor call does three things in a fixed order:

```cpp
Value *ConstructorCallExprAST::codegen() {
  // 1. Allocate in the function's entry block
  Function *CurFn = Builder->GetInsertBlock()->getParent();
  AllocaInst *Tmp = CreateEntryBlockAlloca(CurFn, "ctor.tmp",
                                           ValueType::Struct, ClassName);

  // 2. Zero-initialise the entire struct
  Builder->CreateStore(ZeroConstant(ValueType::Struct, ClassName), Tmp);

  // 3. Call __init__ if it exists, passing Tmp as self
  string InitName = ClassName + ".__init__";
  if (PrototypeAST *InitProto = GetFunctionProto(InitName)) {
    Function *InitF = getFunction(InitName);
    vector<Value *> ArgsV;
    ArgsV.push_back(Tmp);  // implicit self
    for (unsigned I = 0; I < Args.size(); ++I) {
      Value *ArgVal = Args[I]->codegen();
      // apply implicit casts, handle array decay...
      ArgsV.push_back(ArgVal);
    }
    Builder->CreateCall(InitF, ArgsV);
  } else if (!Args.empty()) {
    return LogErrorV("Constructor argument mismatch");
  }

  // 4. Load the finished struct as a value
  return Builder->CreateLoad(ClassTy, Tmp, "ctor.obj");
}
```

**Why `CreateEntryBlockAlloca`?** LLVM's `mem2reg` pass — which turns stack slots into SSA values — only works on allocas in the function's entry block. If the constructor call is inside a loop, allocating there would push the alloca deeper and prevent promotion. Placing the alloca in the entry block keeps the loop's stack frame constant regardless of iteration count.

**Why zero first?** Zero-initialising before calling `__init__` guarantees that fields not touched by `__init__` hold a defined value, not garbage.

**The result is a value, not a pointer.** The `CreateLoad` at the end copies the struct out of `Tmp`. What `Point(3, 4)` returns is a `%Point` aggregate, not a `ptr`. When assigned to `var p: Point`, this value is stored into `p`'s own alloca.

## What Lands in the IR

```pyxc
var p: Point = Point(3, 4)
```

```llvm
; In the entry block of the calling function:
%ctor.tmp = alloca %Point

; At the call site:
store %Point zeroinitializer, ptr %ctor.tmp
call void @Point.__init__(ptr %ctor.tmp, i64 3, i64 4)
%ctor.obj = load %Point, ptr %ctor.tmp
store %Point %ctor.obj, ptr %p
```

## Things Worth Knowing

**`__init__` must return `None`.** Attempting to give it a return type annotation is a parse-time error.

**`__init__` is a regular method.** It can call other methods via `self`, access all fields, and use any other class feature. It is not special beyond its name and the "must return None" rule.

**No overloading.** Only one `__init__` per class. A second definition is a redefinition error.

**`ClassName()` with no `__init__` is always valid.** It produces a zero-initialised instance. `ClassName(args)` with arguments but no `__init__` is an error.

## What's Next

[Chapter 28](chapter-28.md) adds visibility — `public` and `private` modifiers on class fields and methods, enforced at every access site.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

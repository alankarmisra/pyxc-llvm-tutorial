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

### Grammar

`code/chapter-27/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | struct-definition | class-definition | function-definition | decorated-function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 class-definition        = "class" name ":" end-of-lines struct-block ;
 struct-block     = indent class-member { end-of-lines class-member } dedent ;
 class-member     = field-declaration | method-definition ;
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
-name-expression  = name | call-expression | method-call-expression ;
+name-expression  = name | call-expression | method-call-expression | constructor-call-expression ;
 call-expression        = name "(" [ expression { "," expression } ] ")" ;
 method-call-expression  = name "." name "(" [ expression { "," expression } ] ")" ;
+constructor-call-expression    = name "(" [ expression { "," expression } ] ")" ;
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
      return LogErrorExpression("Incorrect # arguments passed");
    // ...type check each arg...
  } else if (!Args.empty()) {
    return LogErrorExpression("Class has no constructor; expected zero arguments");
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
  return LogErrorFunction("Constructor '__init__' must return None");
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

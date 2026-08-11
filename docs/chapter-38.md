---
description: "Add constructors: define __init__ to initialise a class instance, and call it with ClassName(args) syntax. Instances are always zero-initialised before __init__ runs."
---
# 38. pyxc: Constructors

## What I Am Building

[Chapter 37](chapter-37.md) added methods. I can define behavior on a class and call it through `obj.method(args)`. But creating a class instance still means writing field assignments by hand:

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

```text
7.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-27
```

## Grammar

`constructor-call-expression` joins `name-expression`. It's syntactically identical to `call-expression`, an identifier followed by `(args)`; the parser tells them apart by checking whether the identifier names a known class:

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | struct-definition | class-definition | function-definition | external | top-level-expression ;
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
 indent          = INDENT ;
 dedent          = DEDENT ;
 
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

## A New AST Node for Constructor Calls

A constructor call `Point(3, 4)` isn't the same as a function call `foo(3, 4)`: it allocates a temporary, zeroes it, may call `__init__`, and hands back a struct value rather than the result of an ordinary function. A dedicated AST node captures this:

```cpp
class ConstructorCallExpressionNode : public ExpressionNode {
  string ClassName;
  vector<unique_ptr<ExpressionNode>> Arguments;

public:
  ConstructorCallExpressionNode(const string &ClassName,
                         vector<unique_ptr<ExpressionNode>> Arguments)
      : ClassName(ClassName), Arguments(std::move(Arguments)) {
    setType(ValueType::Struct, ClassName);
  }
  Value *codegen() override;
};
```

The result type is `ValueType::Struct` with `ClassName` as the struct name: the same type `var p: Point` already carries.

## Disambiguating Constructor Calls at Parse Time

`ParseNameExpressionWithName` is where every bare-name expression starting with `(` gets decided. When the parser sees `identifier(`, it checks whether the identifier is a known class before falling through to the existing function-call path:

```cpp
// Constructor call: ClassName(...)
auto SI = StructTypes.find(ParsedName);
if (SI != StructTypes.end() && SI->second.IsClass) {
  getNextToken(); // eat '('
  string InitName = ParsedName + ".__init__";
  FunctionSignatureNode *InitSignature = GetFunctionSignature(InitName);
  vector<unique_ptr<ExpressionNode>> Arguments;
  if (CurrentToken != tok_rparen) {
    size_t ArgIndex = 0;
    while (true) {
      ValueType Expected = ValueType::Error;
      string ExpectedStructName;
      if (InitSignature && ArgIndex + 1 < InitSignature->getNumParameters()) {
        Expected = InitSignature->getParameterType(ArgIndex + 1);
        ExpectedStructName = InitSignature->getParameterStructName(ArgIndex + 1);
      }
      ExpectedLiteralTypeGuard Guard(Expected, ExpectedStructName);
      auto Arg = ParseExpression();
      if (!Arg)
        return nullptr;
      Arguments.push_back(std::move(Arg));
      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken(); // eat ','
      ++ArgIndex;
    }
  }
  getNextToken(); // eat ')'

  if (InitSignature) {
    size_t ExpectedArgs =
        InitSignature->getNumParameters() > 0 ? InitSignature->getNumParameters() - 1 : 0;
    if (Arguments.size() != ExpectedArgs)
      return LogErrorExpression("Incorrect # arguments passed");
    for (size_t I = 0; I < Arguments.size(); ++I) {
      ValueType ArgType = Arguments[I]->getType();
      ValueType ParamType = InitSignature->getParameterType(I + 1);
      if (!IsAssignable(ParamType, ArgType))
        return LogErrorExpression(("argument " + std::to_string(I + 1) + " expects " +
                         TypeName(ParamType))
                            .c_str());
      // ...and the matching struct-name check for pointer/struct/array params...
    }
  } else if (!Arguments.empty()) {
    return LogErrorExpression(
        ("Class '" + ParsedName + "' has no constructor; expected zero arguments")
            .c_str());
  }
  return make_unique<ConstructorCallExpressionNode>(ParsedName, std::move(Arguments));
}

// Function call (falls through here if ParsedName isn't a class)
```

If the class has `__init__`, argument count and types are checked against its signature, skipping index 0 (`self`). If there's no `__init__`, any non-empty argument list is rejected by name, so `Foo(1)` on a constructor-less `Foo` reports `Class 'Foo' has no constructor; expected zero arguments` rather than a generic type error.

## `__init__` Must Return None

`ParseMethodDefinitionInClass` checks the method name against `"__init__"` right after parsing the optional `-> type` annotation, before parsing the body:

```cpp
string RetStructName;
ValueType RetType =
    ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);
if (RetType == ValueType::Error)
  return nullptr;
if (MethodName == "__init__" && RetType != ValueType::None)
  return LogErrorFunction("Constructor '__init__' must return None");
```

`__init__` always returns `None`; it cannot return a value. This is the only thing that makes `__init__` special at the parser level — it's still parsed and registered as an ordinary method otherwise, mangled to `ClassName.__init__` exactly like any other, which is also why defining it twice on the same class hits the ordinary "Method '...' is already defined" redefinition check, not a dedicated constructor error.

## Constructor Codegen: Allocate, Zero, Call, Load

`ConstructorCallExpressionNode::codegen` does the work in a fixed order — allocate a temporary in the entry block, zero it, call `__init__` against it if one exists, then load the finished value back out:

```cpp
Value *ConstructorCallExpressionNode::codegen() {
  auto SI = StructTypes.find(ClassName);
  if (SI == StructTypes.end() || !SI->second.IsClass)
    return LogErrorV("Unknown class in constructor call");

  llvm::Type *ClassTy = LLVMTypeFor(ValueType::Struct, ClassName);
  if (!ClassTy)
    return LogErrorV("Unknown class type");
  Function *CurFn = Builder->GetInsertBlock()
                        ? Builder->GetInsertBlock()->getParent()
                        : nullptr;
  if (!CurFn)
    return LogErrorV("Constructor call outside function context");
  AllocaInst *Tmp =
      CreateEntryBlockAlloca(CurFn, "ctor.tmp", ValueType::Struct, ClassName);
  Builder->CreateStore(ZeroConstant(ValueType::Struct, ClassName), Tmp);

  string InitName = ClassName + ".__init__";
  if (FunctionSignatureNode *InitSignature = GetFunctionSignature(InitName)) {
    Function *InitF = getFunction(InitName);
    if (!InitF)
      return LogErrorV("Unknown constructor function");
    if (InitF->arg_size() != Arguments.size() + 1)
      return LogErrorV("Incorrect # arguments passed");
    vector<Value *> ArgsV;
    ArgsV.push_back(Tmp);
    for (unsigned I = 0, E = Arguments.size(); I != E; ++I) {
      Value *ArgVal = Arguments[I]->codegen();
      if (!ArgVal)
        return nullptr;
      ValueType ArgType = Arguments[I]->getType();
      ValueType ParamType = InitSignature->getParameterType(I + 1);
      if (ParamType == ValueType::Pointer && ArgType == ValueType::Array) {
        if (!ArrayDecaysToPointerType(Arguments[I]->getStructName(),
                                      InitSignature->getParameterStructName(I + 1)))
          return LogErrorV("Argument type mismatch");
      } else {
        ArgVal = EmitImplicitCast(ArgVal, ArgType, ParamType);
        if (!ArgVal)
          return LogErrorV("Argument type mismatch");
      }
      ArgsV.push_back(ArgVal);
    }
    Builder->CreateCall(InitF, ArgsV);
  } else if (!Arguments.empty()) {
    return LogErrorV("Constructor argument mismatch");
  }

  return Builder->CreateLoad(ClassTy, Tmp, "ctor.obj");
}
```

**Why `CreateEntryBlockAlloca`?** LLVM's `mem2reg` pass, which turns stack slots into SSA values, only works on allocas that live in the function's entry block. If I allocated `Tmp` wherever the constructor call happened to appear textually, a constructor called inside a loop body would allocate deeper on every pass through the loop rather than reusing one fixed stack slot.

**Why zero first?** Zero-initializing before calling `__init__` guarantees fields `__init__` doesn't touch hold a defined value, not stack garbage.

**The result is a value, not a pointer.** The final `CreateLoad` copies the struct out of `Tmp`. `Point(3, 4)` produces a `%struct.Point` aggregate, not a `ptr[Point]`. Assigning it to `var p: Point` stores that aggregate into `p`'s own, separate alloca.

## What Lands in the IR

I compiled `var p: Point = Point(3, 4)` (with `Point.__init__` and `Point.sum` from the intro example) and read the real output:

```llvm
%struct.Point = type { i64, i64 }

define i64 @__pyxc.user_main() {
entry:
  %p = alloca %struct.Point, align 8
  %ctor.tmp = alloca %struct.Point, align 8
  store %struct.Point zeroinitializer, ptr %ctor.tmp, align 8
  call void @Point.__init__(ptr %ctor.tmp, i64 3, i64 4)
  %ctor.obj = load %struct.Point, ptr %ctor.tmp, align 8
  store %struct.Point %ctor.obj, ptr %p, align 8
  ...
}
```

`%struct.Point` uses the same `struct.`-prefixed naming every named aggregate gets since [Chapter 24](chapter-24.md), class or struct alike. `%ctor.tmp` and `%p` are two distinct allocas: the constructor builds its result into the first, then a plain `store` copies it into the second, exactly the same copy that would happen for `var p: Point = some_other_point_var`.

## Known Limitations

**`__init__` must return `None`.** Giving it a return type annotation is a parse-time error, checked before the body is even parsed.

**`__init__` is a regular method otherwise.** It can call other methods through `self`, read and write any field, and use anything else a method can. Nothing about it is special beyond its name and the "must return None" rule.

**No overloading.** Only one `__init__` per class; a second definition hits the ordinary method-redefinition error, not a dedicated one.

**`ClassName()` with no `__init__` is always valid.** It produces a zero-initialized instance. `ClassName(args)` with arguments but no `__init__` is rejected by name: `Class 'Foo' has no constructor; expected zero arguments`.

## What's Next

[Chapter 39](chapter-39.md) adds `public`/`private` visibility.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

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
cd pyxc-llvm-tutorial/code/chapter-38
```

## Grammar

`constructor-call-expression` joins `name-expression`. It's syntactically identical to `call-expression`, an identifier followed by `(args)`; the parser tells them apart by checking whether the identifier names a known class:

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | type-alias
                                     | struct-definition
                                     | class-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
 class-definition                  = "class" name ":" end-of-lines
                                     class-block ;
 type-alias                        = "type" name "=" type ;
 struct-block                      = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 class-block                       = indent class-member
                                     { end-of-lines class-member } dedent ;
 class-member                      = field-declaration | method-definition ;
 field-declaration                 = name ":" type ;
 method-definition                 = "def" name "(" [ parameters ] ")"
                                     [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 function-definition               = "def" function-signature [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 external                          = "extern" "def" external-function-signature
                                     [ "->" type ] ;
 top-level-statement               = statement ;
 function-signature                = name "(" [ parameters ] ")" ;
 external-function-signature       = name "(" [ parameters [ "," "..." ] | "..." ] ")" ;
 parameters                        = typed-parameter { "," typed-parameter } ;
 typed-parameter                   = name ":" type ;
 if-statement                      = "if" expression ":" suite
                                     { [ end-of-lines ] "elif" expression ":" suite }
                                     [ [ end-of-lines ] "else" ":" suite ] ;
 for-statement                     = "for" ( "var" name ":" type | name )
                                     "=" expression ","
                                     expression "," expression ":" suite ;
 while-statement                   = "while" expression ":" suite ;
 do-while-statement                = "do" ":" suite [ end-of-lines ]
                                     "while" expression ;
 switch-statement                  = "switch" expression ":" end-of-lines
                                     indent switch-body dedent ;
 switch-body                       = switch-case
                                     { end-of-lines switch-case }
                                     [ end-of-lines default-case ] ;
 switch-case                       = "case" switch-integer
                                     { "," switch-integer } ":" suite ;
 default-case                      = "default" ":" suite ;
 variable-statement                = "var" variable-binding
                                     { "," variable-binding } ;
 simple-statement                  = return-statement
                                     | break-statement
                                     | continue-statement
                                     | variable-statement
                                     | expression ;
 compound-statement                = if-statement
                                     | for-statement
                                     | while-statement
                                     | do-while-statement
                                     | switch-statement ;
 statement                         = simple-statement | compound-statement ;
 suite                             = simple-statement
                                     | compound-statement
                                     | end-of-lines block ;
 return-statement                  = "return" [ expression ] ;
 break-statement                   = "break" ;
 continue-statement                = "continue" ;
 statement-separator               = end-of-lines | BLOCK_END ;
 block                             = indent statement
                                     { statement-separator statement } dedent ;
 expression                        = assignment ;
 assignment                        = logical-or [ assignment-operator assignment ] ;
 logical-or                        = logical-and { "||" logical-and } ;
 logical-and                       = bitwise-or { "&&" bitwise-or } ;
 bitwise-or                        = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor                       = bitwise-and { "^" bitwise-and } ;
 bitwise-and                       = equality { "&" equality } ;
 equality                          = relational { ("==" | "!=") relational } ;
 relational                        = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift                             = sum { ("<<" | ">>") sum } ;
 sum                               = term { ("+" | "-") term } ;
 term                              = unary-expression
                                     { ("*" | "/" | "%") unary-expression } ;
 lvalue                            = name
                                     { "." name | "[" expression "]" } ;
 variable-binding                  = name ":" type [ "=" expression ] ;
 unary-expression                  = ("-" | "!" | "~" | "++" | "--")
                                     unary-expression
                                     | postfix-expression ;
 postfix-expression                = primary [ "++" | "--" ] ;
 primary                           = cast-expression
                                     | sizeof-expression
                                     | address-expression
                                     | array-literal
                                     | string-literal
                                     | character-literal
                                     | name-expression
                                     | number-expression
                                     | boolean-literal
                                     | parenthesized-expression ;
 cast-expression                   = cast-type "(" expression ")" ;
 sizeof-expression                 = "sizeof" "(" type ")" ;
 address-expression                = "addr" "(" lvalue ")" ;
 array-literal                     = "[" [ expression
                                       { "," expression } ] "]" ;
 string-literal                    = '"' { string-character | escape } '"' ;
 escape                            = literal-escape ;
 string-character                  = ? any character except '"', "\\", "\r", and "\n" ? ;
 character-literal                 = "'" ( character | character-escape ) "'" ;
 character-escape                  = literal-escape ;
 literal-escape                    = "\\" ( "\\" | "'" | '"' | "?"
                                       | "a" | "b" | "f" | "n" | "r"
                                       | "t" | "v"
                                       | "x" hex-digit hex-digit
                                       | octal-digit [ octal-digit
                                         [ octal-digit ] ]
                                       | "u" hex-digit hex-digit hex-digit hex-digit
                                       | "U" hex-digit hex-digit hex-digit hex-digit
                                         hex-digit hex-digit hex-digit hex-digit ) ;
 character                         = ? any character except "'", "\\", "\r", and "\n" ? ;
 hex-digit                         = digit | "A".."F" | "a".."f" ;
 assignment-operator               = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 octal-digit                       = "0".."7" ;
 name-expression                   = lvalue
                                     | call-expression
-                                    | method-call-expression ;
+                                    | method-call-expression
+                                    | constructor-call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 method-call-expression            = lvalue "." name "(" [ arguments ] ")" ;
+constructor-call-expression       = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 indent                            = INDENT ;
 dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 type                              = base-type [ array-suffix ] ;
 base-type                         = builtin-type | alias-type | struct-type
                                     | pointer-type ;
 pointer-type                      = "ptr" "[" type "]" ;
 array-suffix                      = "[" integer "]" ;
 builtin-type                      = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" | "None" ;
 struct-type                       = name ;
 alias-type                        = name ;
 cast-type                         = builtin-cast-type | pointer-type ;
 builtin-cast-type                 = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" ;
 number                            = ( digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ) [ exponent ] ;
 switch-integer                    = [ "-" ] digit { digit } ;
 exponent                          = ( "e" | "E" ) [ "+" | "-" ]
                                     digit { digit } ;
 boolean-literal                   = "True" | "False" ;
 integer                           = digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 (*
     A `comment` begins with "#" and continues to the end of the line. The lexer
      ignores its text and returns an end-of-line token when one follows it.
 *)
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 (*
     `whitespace` may appear before or between tokens
      and is ignored by the lexer.
 *)
 whitespace                        = " " | "\t" | "\v" | "\f" ;
 INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
 DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
 BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
                                       immediately after it consumes DEDENT ? ;

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

```cppdiff
*static unique_ptr<ExpressionNode> ParseNameExpressionWithName(const string &ParsedName) {
*  if (CurrentToken != tok_lparen) { // Simple variable ref.
*    ...
*    return Result;
*  }
*
+  // A class name in call position constructs a value of that class.
+  auto Class = StructTypes.find(ParsedName);
+  if (Class != StructTypes.end() && Class->second.IsClass) {
+    getNextToken(); // eat '('
+    string InitializerName = ParsedName + ".__init__";
+    FunctionSignatureNode *Initializer =
+        GetFunctionSignature(InitializerName);
+    vector<unique_ptr<ExpressionNode>> Arguments;
+    if (CurrentToken != tok_rparen) {
+      size_t ParameterIndex = 1; // parameter zero is implicit self
+      while (true) {
+        ValueType ExpectedType = ValueType::Error;
+        string ExpectedTypeInfo;
+        if (Initializer && ParameterIndex < Initializer->getNumParameters()) {
+          ExpectedType = Initializer->getParameterType(ParameterIndex);
+          ExpectedTypeInfo =
+              Initializer->getParameterStructName(ParameterIndex);
+        }
+        ExpectedLiteralTypeGuard Guard(ExpectedType, ExpectedTypeInfo);
+        auto Argument = ParseExpression();
+        if (!Argument)
+          return nullptr;
+        Arguments.push_back(std::move(Argument));
+        if (CurrentToken == tok_rparen)
+          break;
+        if (CurrentToken != tok_comma)
+          return LogErrorExpression("Expected ')' or ',' in argument list");
+        getNextToken(); // eat ','
+        ++ParameterIndex;
+      }
+    }
+    getNextToken(); // eat ')'
+
+    if (!Initializer) {
+      if (!Arguments.empty())
+        return LogErrorExpression(
+            ("Class '" + ParsedName +
+             "' has no constructor; expected zero arguments")
+                .c_str());
+    } else {
+      if (Arguments.size() + 1 != Initializer->getNumParameters())
+        return LogErrorExpression("Incorrect # arguments passed");
+      for (size_t Index = 0; Index < Arguments.size(); ++Index) {
+        ValueType ParameterType =
+            Initializer->getParameterType(Index + 1);
+        if (!IsAssignable(ParameterType, Arguments[Index]->getType()))
+          return LogErrorExpression("Argument type mismatch");
+        if ((ParameterType == ValueType::Struct ||
+             ParameterType == ValueType::Pointer) &&
+            Initializer->getParameterStructName(Index + 1) !=
+                Arguments[Index]->getStructName())
+          return LogErrorExpression("Argument type mismatch");
+      }
+    }
+    return make_unique<ConstructorCallExpressionNode>(
+        ParsedName, std::move(Arguments));
+  }
+
*  // Function call.
*  getNextToken(); // eat (
*  ...
*}
```

If the class has `__init__`, argument count and types are checked against its signature, skipping index 0 (`self`). If there's no `__init__`, any non-empty argument list is rejected by name, so `Foo(1)` on a constructor-less `Foo` reports `Class 'Foo' has no constructor; expected zero arguments` rather than a generic type error.

## `__init__` Must Return None

`ParseMethodDefinition` checks the method name against `"__init__"` right after parsing the optional `-> type` annotation, before registering the signature or parsing the body:

```cppdiff
*static unique_ptr<FunctionDefinitionNode>
*ParseMethodDefinition(const string &ClassName) {
*  ...
*  getNextToken(); // eat ')'
*
*  string ReturnTypeInfo;
*  ValueType ReturnType =
*      ParseOptionalReturnType(&ReturnTypeInfo, ValueType::None);
*  if (ReturnType == ValueType::Error)
*    return nullptr;
+  if (MethodName == "__init__" && ReturnType != ValueType::None)
+    return LogErrorFunction("Constructor '__init__' must return None");
*
*  string MangledName = ClassName + "." + MethodName;
*  ...
*}
```

`__init__` always returns `None`; it cannot return a value. This is the only thing that makes `__init__` special at the parser level — it's still parsed and registered as an ordinary method otherwise, mangled to `ClassName.__init__` exactly like any other, which is also why defining it twice on the same class hits the ordinary "Method '...' is already defined" redefinition check, not a dedicated constructor error.

## Constructor Codegen: Allocate, Zero, Call, Load

`ConstructorCallExpressionNode::codegen` does the work in a fixed order — allocate a temporary in the entry block, zero it, call `__init__` against it if one exists, then load the finished value back out:

```cpp
Value *ConstructorCallExpressionNode::codegen() {
  Function *CurrentFunction = TheBuilder->GetInsertBlock()
                                  ? TheBuilder->GetInsertBlock()->getParent()
                                  : nullptr;
  if (!CurrentFunction)
    return LogErrorV("Constructor call outside function context");

  AllocaInst *Storage = CreateEntryBlockAlloca(
      CurrentFunction, "constructor.value", ValueType::Struct, ClassName);
  TheBuilder->CreateStore(ZeroConstant(ValueType::Struct, ClassName), Storage);

  string InitializerName = ClassName + ".__init__";
  if (FunctionSignatureNode *Initializer =
          GetFunctionSignature(InitializerName)) {
    Function *InitializerFunction = getFunction(InitializerName);
    if (!InitializerFunction)
      return LogErrorV("Unknown constructor function");
    vector<Value *> ArgumentValues;
    ArgumentValues.push_back(Storage);
    for (size_t Index = 0; Index < Arguments.size(); ++Index) {
      Value *ArgumentValue = Arguments[Index]->codegen();
      if (!ArgumentValue)
        return nullptr;
      ArgumentValue = EmitImplicitCast(
          ArgumentValue, Arguments[Index]->getType(),
          Initializer->getParameterType(Index + 1));
      if (!ArgumentValue)
        return LogErrorV("Constructor argument mismatch");
      ArgumentValues.push_back(ArgumentValue);
    }
    TheBuilder->CreateCall(InitializerFunction, ArgumentValues);
  }

  return TheBuilder->CreateLoad(LLVMTypeFor(ValueType::Struct, ClassName), Storage,
                             "constructor.result");
}
```

The parser's disambiguation code has already checked argument count and types against `__init__`'s signature (or rejected them if the class has no `__init__`), so codegen doesn't repeat that check; it only needs `EmitImplicitCast` to align each argument's runtime value with the parameter type already known to be compatible. When the class has no `__init__` at all, the `if (FunctionSignatureNode *Initializer = ...)` simply doesn't run, and `Storage` is returned zeroed with no call.

**Why `CreateEntryBlockAlloca`?** LLVM's `mem2reg` pass, which turns stack slots into SSA values, only works on allocas that live in the function's entry block. If I allocated `Storage` wherever the constructor call happened to appear textually, a constructor called inside a loop body would allocate deeper on every pass through the loop rather than reusing one fixed stack slot.

**Why zero first?** Zero-initializing before calling `__init__` guarantees fields `__init__` doesn't touch hold a defined value, not stack garbage.

**The result is a value, not a pointer.** The final `CreateLoad` copies the struct out of `Storage`. `Point(3, 4)` produces a `%struct.Point` aggregate, not a `ptr[Point]`. Assigning it to `var p: Point` stores that aggregate into `p`'s own, separate alloca.

## What Lands in the IR

I compiled `var p: Point = Point(3, 4)` (with `Point.__init__` and `Point.sum` from the intro example) and read the real output:

```llvm
%struct.Point = type { i64, i64 }

define i64 @__pyxc.user_main() {
entry:
  %p = alloca %struct.Point, align 8
  %constructor.value = alloca %struct.Point, align 8
  store %struct.Point zeroinitializer, ptr %constructor.value, align 8
  call void @Point.__init__(ptr %constructor.value, i64 3, i64 4)
  %constructor.result = load %struct.Point, ptr %constructor.value, align 8
  store %struct.Point %constructor.result, ptr %p, align 8
  %calltmp = call i64 @Point.sum(ptr %p)
  ...
}
```

`%struct.Point` uses the same `struct.`-prefixed naming every named aggregate gets since [Chapter 24](chapter-24.md), class or struct alike. `%constructor.value` and `%p` are two distinct allocas: the constructor builds its result into the first, then a plain `store` copies it into the second, exactly the same copy that would happen for `var p: Point = some_other_point_var`.

## Known Limitations

**`__init__` must return `None`.** Giving it a return type annotation is a parse-time error, checked before the body is even parsed.

**`__init__` is a regular method otherwise.** It can call other methods through `self`, read and write any field, and use anything else a method can. Nothing about it is special beyond its name and the "must return None" rule.

**No overloading.** Only one `__init__` per class; a second definition hits the ordinary method-redefinition error, not a dedicated one.

**`ClassName()` with no `__init__` is always valid.** It produces a zero-initialized instance. `ClassName(args)` with arguments but no `__init__` is rejected by name: `Class 'Foo' has no constructor; expected zero arguments`.

## Build and Run

```bash
cd code/chapter-38
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## Try It

<!-- code-merge:start -->
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
<!-- code-merge:end -->

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

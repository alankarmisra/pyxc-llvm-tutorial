---
description: "Add methods to classes: define functions inside the class body, call them with obj.method(args), and mutate receiver state through an implicit self pointer."
---
# 26. pyxc: Methods and `self`

## Where We Are

[Chapter 25](chapter-25.md) added the `class` keyword. Classes can have fields and you can read and write them, but all behaviour lives in global functions. After this chapter, behaviour lives with the data:

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
cd pyxc-llvm-tutorial/code/chapter-26
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

### Grammar

`code/chapter-26/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | struct-definition | class-definition | function-definition | decorated-function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 class-definition        = "class" name ":" end-of-lines struct-block ;
-struct-block     = indent field-declaration { end-of-lines field-declaration } dedent ;
+struct-block     = indent class-member { end-of-lines class-member } dedent ;
+class-member     = field-declaration | method-definition ;
+method-definition       = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")"
+                  [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
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
-name-expression  = name | call-expression ;
+name-expression  = name | call-expression | method-call-expression ;
 call-expression        = name "(" [ expression { "," expression } ] ")" ;
+method-call-expression  = name "." name "(" [ expression { "," expression } ] ")" ;
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

## Early Struct Registration and the `IsClass` Gate

In chapter 24, `StructTypes[StructName]` was populated after the body was parsed. This chapter moves the registration to *before* the body so that method signatures can reference the enclosing class — for example, a method that returns `ptr[Counter]` needs `Counter` already in `StructTypes`:

```cpp
Info.IsClass = (strcmp(KindName, "class") == 0);
// Register early so method signatures can reference the enclosing class.
StructTypes[StructName] = Info;

// Now parse the body (fields and methods)
while (CurTok != tok_dedent) {
  if (CurTok == tok_def) {
    if (!Info.IsClass) {
      LogErrorExpression("Methods are only allowed inside classes");
      return false;
    }
    auto FnAST = ParseMethodDefinitionInClass(StructName);
    // ...codegen the method...
  } else {
    // parse field declaration as before
  }
  // Keep metadata up to date as fields are added
  StructTypes[StructName] = Info;
}
```

The `IsClass` flag is checked here at the point where a `def` is seen inside the body. A `def` inside a `struct` is an immediate error. `StructTypes[StructName] = Info` is written again after each field so the running field list is always visible to method parsers.

## `ParseMethodDefinitionInClass` — Parsing Method Definitions

`ParseMethodDefinitionInClass` handles a `def` inside a class body. Its key responsibility is injecting `self` as the implicit first parameter — the programmer never writes it:

```cpp
static unique_ptr<FunctionAST>
ParseMethodDefinitionInClass(const string &ClassName) {
  getNextToken(); // eat 'def'
  // ... parse method name ...

  vector<PrototypeAST::ArgInfo> ArgNames;
  // Inject implicit self: typed as ptr[ClassName] so methods can mutate receiver state
  ArgNames.push_back({"self", ValueType::Pointer,
                      EncodePointerType(ValueType::Struct, ClassName)});

  // Parse user-declared parameters (none of them may be named 'self')
  while (CurTok != ')') {
    string ArgName = IdentifierStr;
    if (ArgName == "self")
      return LogErrorFunction("Method parameters cannot be named 'self'");
    // ... parse type annotation ...
    ArgNames.push_back({ArgName, ArgType, ArgStructName});
  }

  // ... parse optional return type ...

  // Mangle name: "ClassName.MethodName"
  string MangledName = ClassName + "." + MethodName;
  if (FunctionProtos.count(MangledName))
    return LogErrorFunction(("Method '" + MethodName + "' is already defined on '" +
                      ClassName + "'").c_str());

  auto Proto = make_unique<PrototypeAST>(MangledName, std::move(ArgNames), ...);
  FunctionProtos[Proto->getName()] = Proto->clone();

  // Parse body with self in scope and return type context set
  ReturnTypeGuard RetGuard(RetType, RetStructName);
  FunctionScopeGuard Scope(Proto->getArgs());
  // ... parse ':' and body ...
}
```

The method prototype is registered in `FunctionProtos` under the mangled name immediately, so the body can make recursive calls if needed.

## Method Mangling

Methods are stored in `FunctionProtos` under `ClassName.MethodName`. A method `def add()` on class `Calc` is registered as `"Calc.add"` and emitted as `@Calc.add` in the IR.

This means:
- Method names are independent of global function names — `Calc.add` and `add` are distinct entries.
- Two classes can both have a method named `add` without conflict.
- There is no runtime vtable — dispatch is a direct call to the statically-known mangled name.

## `ParseMethodCallExpr` — Parsing Call Sites

When the expression parser sees `receiver.methodName(`, it calls `ParseMethodCallExpr`. This function:

1. Confirms the receiver is a known class type.
2. Looks up `ClassName.MethodName` in `FunctionProtos`.
3. Prepends the receiver's address as argument 0 — the implicit `self`.
4. Parses the explicit arguments starting at index 1.

```cpp
static unique_ptr<ExprAST>
ParseMethodCallExpr(unique_ptr<ExprAST> Receiver, const string &MethodName) {
  string ClassName = Receiver->getStructName();
  string CalleeName = ClassName + "." + MethodName;
  PrototypeAST *Proto = GetFunctionProto(CalleeName);
  // ...

  getNextToken(); // eat '('
  vector<unique_ptr<ExprAST>> Args;

  // Build implicit self: addr(receiver) as ptr[ClassName]
  if (auto *Var = dynamic_cast<VariableExprAST *>(Receiver.get())) {
    Args.push_back(make_unique<AddrExprAST>(
        Var->getName(), vector<string>{},
        EncodePointerType(ValueType::Struct, Var->getStructName())));
  } else if (auto *Field = dynamic_cast<FieldExprAST *>(Receiver.get())) {
    // FieldExprAST is always rooted at a named variable — use its base name
    Args.push_back(make_unique<AddrExprAST>(
        *Field->getLValueName(), Field->getFieldPath(), ...));
  } else {
    return LogErrorExpression("Method call base must be an lvalue");
  }

  // Parse explicit args (skipping index 0 = self)
  size_t ArgIndex = 1;
  while (CurTok != ')') {
    // set ExpectedLiteralTypeGuard from Proto->getArgType(ArgIndex)
    auto Arg = ParseExpression();
    Args.push_back(std::move(Arg));
    ++ArgIndex;
  }
  getNextToken(); // eat ')'
  // type-check all args, then build CallExprAST with mangled name
}
```

The receiver must be an lvalue — a named variable or a field path. Calling a method on a function return value is not valid because taking the address of a temporary is not supported.

## Dot Dispatch — Deciding Method Call vs Field Access

Before this chapter, seeing `identifier.` always meant a field access. Now `.` can mean either field access or method call. The parser resolves this by reading the member name, then peeking at the *next* token:

```cpp
// In ParseIdentifierExpr and ParseSimpleStmt, after seeing identifier '.'
getNextToken(); // eat '.'
string MemberName = IdentifierStr;
getNextToken(); // eat member name
if (CurTok == '(') {
  // It's a method call
  Base = ParseMethodCallExpr(std::move(Base), MemberName);
} else {
  // It's a field access — use ParseFieldAccessFromFirstMember
  auto Field = ParseFieldAccessFromFirstMember(
      Var->getName(), Var->getType(), Var->getStructName(), MemberName);
  Base = std::move(Field);
}
```

This one-token lookahead at `(` is enough to distinguish the two cases unambiguously.

## `ParseFieldAccessFromFirstMember` — Field Access with Pointer Auto-Deref

Chapter 25 used a function called `ParseFieldAccessExpr` for field chains. This chapter replaces it with `ParseFieldAccessFromFirstMember`, which adds one new capability: **transparent pointer dereference**.

If the current type at any point in the field chain is `ptr[SomeName]` rather than `SomeName` directly, the compiler automatically treats it as a deref — exactly the same way you'd write `self.x` where `self` is `ptr[Counter]`:

```cpp
static unique_ptr<FieldExprAST>
ParseFieldAccessFromFirstMember(string BaseName, ValueType BaseType,
                                string BaseStructName,
                                const string &FirstMember) {
  vector<string> Path;
  ValueType CurType = BaseType;
  string CurStruct = std::move(BaseStructName);

  auto ConsumeField = [&](const string &Field) -> bool {
    // Auto-deref if current type is a pointer to a struct
    if (CurType == ValueType::Pointer) {
      ValueType PointeeType; string PointeeStruct;
      if (!DecodePointerType(CurStruct, PointeeType, PointeeStruct) ||
          PointeeType != ValueType::Struct) {
        LogErrorExpression("Field access requires a struct value");
        return false;
      }
      CurType = ValueType::Struct;
      CurStruct = PointeeStruct;
    }
    // Now CurType must be Struct
    auto FI = StructTypes[CurStruct].FieldIndex.find(Field);
    // ... look up field, advance CurType/CurStruct, push to Path ...
    return true;
  };

  ConsumeField(FirstMember);
  while (CurTok == '.') {
    getNextToken(); // eat '.'
    string Field = IdentifierStr;
    getNextToken(); // eat field name
    ConsumeField(Field);
  }
  return make_unique<FieldExprAST>(std::move(BaseName), std::move(Path),
                                   CurType, CurStruct);
}
```

The `ConsumeField` lambda runs for each segment of a field chain. The auto-deref at the top of `ConsumeField` means `self.x` in a method body — where `self` is `ptr[Counter]` — resolves `x` correctly without requiring explicit `(*self).x` syntax.

## `LoadPointerValue` — Pointer Auto-Deref in Codegen

The parser's auto-deref needs a matching codegen path. `LoadPointerValue` is extended: when the base variable is `ptr[StructName]`, it loads the pointer value first (getting the actual address of the pointee), then proceeds to generate the GEP for the field:

```cpp
// New at the top of LoadPointerValue:
if (BaseType == ValueType::Pointer) {
  ValueType PointeeType; string PointeeStruct;
  if (!DecodePointerType(BaseStruct, PointeeType, PointeeStruct) ||
      PointeeType != ValueType::Struct)
    return nullptr;
  // Load the pointer itself (dereference the ptr variable)
  BasePtr = Builder->CreateLoad(LLVMTypeFor(BaseType, BaseStruct),
                                BasePtr, (BaseName + ".ptr").c_str());
  BaseType = ValueType::Struct;
  BaseStruct = PointeeStruct;
}
// Then proceed with struct field GEP as before
```

This is what makes `self.value = self.value + 1` work: `self` is a `ptr[Counter]` alloca, the load retrieves the pointer value, and then the GEP walks to the `value` field on the pointed-to struct.

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

**Methods are only allowed on classes, not structs.** Defining a `def` inside a `struct` body is an error: "Methods are only allowed inside classes".

**`self` cannot be named by the programmer.** Writing a parameter called `self` in a method definition is rejected: "Method parameters cannot be named 'self'". The compiler owns that name.

**Method calls require an lvalue receiver.** `Calc().add(1, 2)` is not yet valid — there is no temporary materialisation. Use a `var` declaration first.

**`self.field` through a pointer works without explicit deref.** Both the parser (`ParseFieldAccessFromFirstMember`) and codegen (`LoadPointerValue`) transparently deref `ptr[StructName]` when accessing fields, so `self.x` reads and writes correctly even though `self` is a pointer.

## What's Next

[Chapter 27](chapter-27.md) adds constructors — `__init__` methods that initialise a new instance, called with `ClassName(args)` syntax.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

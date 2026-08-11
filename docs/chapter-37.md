---
description: "Add methods to classes: define functions inside the class body, call them with obj.method(args), and mutate receiver state through an implicit self pointer."
---
# 37. pyxc: Methods and `self`

## What I Am Building

[Chapter 36](chapter-36.md) added the `class` keyword. Classes can have fields and I can read and write them, but all behavior still lives in global functions. After this chapter, behavior lives with the data:

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

```text
3.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-26
```

## Grammar

`struct-block` now contains `class-member` instead of just `field-declaration`. A class member is either a field or a method. `method-call-expression` joins `name-expression`. `self` is not in the grammar at all: it's injected by the compiler, never written by the programmer.

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | struct-definition | class-definition | function-definition | external | top-level-expression ;
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

## Registering the Aggregate Before Its Body Is Parsed

Since [Chapter 27](chapter-27.md), `ParseAggregateDefinition` filled in `StructTypes[StructName]` only once, after the whole body was parsed. That doesn't work anymore: a method signature can reference the enclosing class (a method returning `ptr[Counter]` on `Counter` itself, say), so `Counter` needs to already be in `StructTypes` while its own methods are being parsed. I register early instead, then keep the entry updated as each field is added:

```cpp
StructTypeInfo Info;
Info.Name = StructName;
Info.IsClass = (strcmp(KindName, "class") == 0);
// Register early so method signatures can reference the enclosing class.
StructTypes[StructName] = Info;
while (CurrentToken != tok_dedent && CurrentToken != tok_block_end && CurrentToken != tok_eof) {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    continue;
  }
  if (CurrentToken == tok_def) {
    if (!Info.IsClass) {
      LogErrorExpression("Methods are only allowed inside classes");
      return false;
    }
    auto FnAST = ParseMethodDefinitionInClass(StructName);
    if (!FnAST)
      return false;
    if (auto *FnIR = FnAST->codegen()) {
      if (ShouldDumpIR())
        FnIR->print(errs());
    }
    if (CurrentToken == tok_eol)
      consumeNewlines();
    else if (CurrentToken == tok_block_end)
      getNextToken();
    continue;
  }
  // ... parse a field-declaration as before ...
  Info.FieldIndex[FieldName] = Info.Fields.size();
  Info.Fields.push_back({FieldName, FieldType, FieldStructName});
  // Keep aggregate metadata visible while parsing subsequent methods.
  StructTypes[StructName] = Info;
  if (CurrentToken == tok_eol)
    consumeNewlines();
}
```

`Info.IsClass` is the one bit of state this chapter adds to `StructTypeInfo` itself, and it's what the `tok_def` branch checks: a `def` inside a `struct` body is rejected immediately, before I even try to parse it as a method. `StructTypes[StructName] = Info` runs again after every field for the same reason it ran once early: a method later in the same body needs to see the fields declared before it, not just the ones declared after the class itself was first registered.

A method's own body can end with a block-end marker from its own indented block (via `ParseFunctionBody` → `ParseBlock`), so after codegen-ing a method I check for `tok_block_end` and consume it — otherwise the outer `while` loop here would mistake the method's own block-end for the end of the class body.

## Parsing Method Definitions

`ParseMethodDefinitionInClass` handles a `def` inside a class body. Its central job is injecting `self` as the implicit first parameter, typed as a pointer to the enclosing class, since the programmer never writes it:

```cpp
static unique_ptr<FunctionDefinitionNode>
ParseMethodDefinitionInClass(const string &ClassName) {
  // CurrentToken is 'def'
  getNextToken(); // eat 'def'
  if (CurrentToken != tok_name)
    return LogErrorFunction("Expected method name in class definition");
  string MethodName = Name;
  SourceLocation SignatureLoc = CurLoc;
  getNextToken(); // eat method name
  if (CurrentToken != tok_lparen)
    return LogErrorFunction("Expected '(' in method function signature");
  getNextToken(); // eat '('

  vector<FunctionSignatureNode::ParameterInfo> ParameterNames;
  // Implicit self parameter is a pointer so methods can mutate receiver state.
  ParameterNames.push_back({"self", ValueType::Pointer,
                      EncodePointerType(ValueType::Struct, ClassName)});

  if (CurrentToken != tok_rparen) {
    while (true) {
      if (CurrentToken != tok_name)
        return LogErrorFunction("Expected parameter name in method function signature");
      string ArgName = Name;
      if (ArgName == "self")
        return LogErrorFunction("Method parameters cannot be named 'self'");
      getNextToken(); // eat name
      if (CurrentToken != tok_colon)
        return LogErrorFunction(
            "Method parameters require a type annotation (e.g., ': int')");
      getNextToken(); // eat ':'
      string ArgStructName;
      ValueType ArgType = ParseTypeToken(&ArgStructName);
      if (ArgType == ValueType::Error)
        return nullptr;
      if (ArgType == ValueType::None)
        return LogErrorFunction("Parameters cannot have None type");
      ParameterNames.push_back({ArgName, ArgType, ArgStructName});

      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorFunction("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  if (CurrentToken != tok_rparen)
    return LogErrorFunction("Expected ')' in method function signature");
  getNextToken(); // eat ')'

  string RetStructName;
  ValueType RetType =
      ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);
  if (RetType == ValueType::Error)
    return nullptr;

  string MangledName = ClassName + "." + MethodName;
  if (FunctionSignatures.count(MangledName))
    return LogErrorFunction(("Method '" + MethodName + "' is already defined on '" +
                      ClassName + "'")
                         .c_str());

  auto Signature = make_unique<FunctionSignatureNode>(MangledName, std::move(ParameterNames),
                                         SignatureLoc, RetType);
  Signature->setReturnStructName(RetStructName);
  FunctionSignatures[Signature->getName()] = Signature->clone();

  ReturnTypeGuard RetGuard(RetType, RetStructName);
  FunctionScopeGuard Scope(Signature->getParameters());

  if (CurrentToken != tok_colon)
    return LogErrorFunction("Expected ':' in method definition");
  getNextToken(); // eat ':'
  unique_ptr<ExpressionNode> Body = ParseFunctionBody();
  if (Body) {
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
  }
  FunctionSignatures.erase(MangledName);
  return nullptr;
}
```

`self`'s encoded pointee type is built the same way every other `ptr[T]` is: `EncodePointerType(ValueType::Struct, ClassName)`. The method's own signature is registered in `FunctionSignatures` under the mangled name before the body is parsed, so a method can call itself recursively (or call a sibling method defined later in the same class, once that method's own signature is registered).

## Method Mangling

Methods are stored in `FunctionSignatures` under `ClassName.MethodName`. A method `def add()` on class `Calc` is registered as `"Calc.add"` and emitted as `@Calc.add` in the IR.

This means:
- Method names are independent of global function names — `Calc.add` and a top-level `add` are distinct entries.
- Two classes can both have a method named `add` without conflict.
- There is no runtime vtable — dispatch is a direct call to the statically-known mangled name.

## Parsing Call Sites

When the expression parser sees `receiver.methodName(`, it calls `ParseMethodCallExpression`. This function:

1. Confirms the receiver is a struct-typed value.
2. Looks up `ClassName.MethodName` in `FunctionSignatures`.
3. Builds the implicit `self` argument: the receiver's address.
4. Parses the explicit arguments, type-checking each against the signature starting at parameter index 1 (index 0 is `self`).

```cpp
static unique_ptr<ExpressionNode> ParseMethodCallExpression(unique_ptr<ExpressionNode> Receiver,
                                               const string &MethodName) {
  if (!Receiver || Receiver->getType() != ValueType::Struct)
    return LogErrorExpression("Method call base must be a class/struct value");
  string ClassName = Receiver->getStructName();
  if (ClassName.empty())
    return LogErrorExpression("Method call base must be a class/struct value");
  string CalleeName = ClassName + "." + MethodName;
  FunctionSignatureNode *Signature = GetFunctionSignature(CalleeName);
  if (!Signature)
    return LogErrorExpression(
        ("Unknown method '" + MethodName + "' on '" + ClassName + "'").c_str());

  getNextToken(); // eat '('
  vector<unique_ptr<ExpressionNode>> Arguments;
  // implicit self: pass receiver address
  if (auto *Var = dynamic_cast<NameExpressionNode *>(Receiver.get())) {
    Arguments.push_back(make_unique<AddrExpressionNode>(
        Var->getName(), vector<string>{},
        EncodePointerType(ValueType::Struct, Var->getStructName())));
  } else if (auto *Field = dynamic_cast<FieldExpressionNode *>(Receiver.get())) {
    // FieldExpressionNode always models an lvalue rooted at a base variable name.
    const string *BaseName = Field->getLValueName();
    if (!BaseName)
      return LogErrorExpression("Method call base must be an lvalue");
    Arguments.push_back(make_unique<AddrExpressionNode>(
        *BaseName, Field->getFieldPath(),
        EncodePointerType(ValueType::Struct, Field->getStructName())));
  } else {
    return LogErrorExpression("Method call base must be an lvalue");
  }
  if (CurrentToken != tok_rparen) {
    size_t ArgIndex = 1; // skip implicit self
    while (true) {
      ValueType Expected = ValueType::Error;
      string ExpectedStructName;
      if (ArgIndex < Signature->getNumParameters()) {
        Expected = Signature->getParameterType(ArgIndex);
        ExpectedStructName = Signature->getParameterStructName(ArgIndex);
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

  if (Arguments.size() != Signature->getNumParameters())
    return LogErrorExpression("Incorrect # arguments passed");
  // ... type-check each argument against Signature, same rules as a plain call ...

  return make_unique<CallExpressionNode>(CalleeName, std::move(Arguments),
                                  Signature->getReturnType(),
                                  Signature->getReturnStructName());
}
```

The receiver must be an lvalue: a `NameExpressionNode` (a plain named variable) or a `FieldExpressionNode` (a field path rooted at one). Calling a method on a function's return value doesn't work yet, since there's no variable there to take the address of.

## Dot Dispatch: Method Call vs. Field Access

Before this chapter, seeing `name.` always meant a field access. Now `.` can mean either a field access or a method call, and the parser can't tell which until it looks past the member name. `ParseNameExpression` reads the member name first, then checks whether `(` follows:

```cpp
if (CurrentToken == tok_dot) {
  getNextToken(); // eat '.'
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected field or method name after '.'");
  string MemberName = Name;
  getNextToken(); // eat member name
  if (CurrentToken == tok_lparen) {
    Base = ParseMethodCallExpression(std::move(Base), MemberName);
    if (!Base)
      return nullptr;
  } else {
    auto *Var = dynamic_cast<NameExpressionNode *>(Base.get());
    if (!Var)
      return LogErrorExpression("Field access base must be a variable");
    auto Field = ParseFieldAccessFromFirstMember(
        Var->getName(), Var->getType(), Var->getStructName(), MemberName);
    if (!Field)
      return LogErrorExpression("Invalid field access");
    Base = std::move(Field);
  }
}
```

One token of lookahead at `(` is enough to tell the two apart unambiguously.

## A Second Field-Access Parser for the First Member

The field-chain parser from [Chapter 24](chapter-24.md), `ParseFieldAccessExpression`, still exists unchanged: it starts from `.` and walks the whole chain itself. But the dot-dispatch code above has already consumed the *first* member name by the time it knows it isn't a method call, so it can't hand that name back to `ParseFieldAccessExpression`, which expects to see the leading `.` itself. Rather than back up the parser, I add a second function, `ParseFieldAccessFromFirstMember`, that takes the already-consumed first member as a parameter and continues from there for any further `.field` segments:

```cpp
static unique_ptr<FieldExpressionNode>
ParseFieldAccessFromFirstMember(string BaseName, ValueType BaseType,
                                string BaseStructName,
                                const string &FirstMember) {
  vector<string> Path;
  ValueType CurType = BaseType;
  string CurStruct = std::move(BaseStructName);
  auto ConsumeField = [&](const string &Field) -> bool {
    if (CurType == ValueType::Pointer) {
      ValueType PointeeType = ValueType::Error;
      string PointeeStruct;
      if (!DecodePointerType(CurStruct, PointeeType, PointeeStruct) ||
          PointeeType != ValueType::Struct) {
        LogErrorExpression("Field access requires a struct value");
        return false;
      }
      CurType = ValueType::Struct;
      CurStruct = PointeeStruct;
    }
    if (CurType != ValueType::Struct || CurStruct.empty()) {
      LogErrorExpression("Field access requires a struct value");
      return false;
    }
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end()) {
      LogErrorExpression("Unknown struct type in field access");
      return false;
    }
    auto FI = SI->second.FieldIndex.find(Field);
    if (FI == SI->second.FieldIndex.end()) {
      LogErrorExpression(("Unknown field '" + Field + "' on struct '" + CurStruct + "'")
                   .c_str());
      return false;
    }
    const auto &FD = SI->second.Fields[FI->second];
    CurType = FD.Type;
    CurStruct = FD.StructName;
    Path.push_back(Field);
    return true;
  };

  if (!ConsumeField(FirstMember))
    return nullptr;
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name) {
      LogErrorExpression("Expected field name after '.'");
      return nullptr;
    }
    string Field = Name;
    getNextToken(); // eat field
    if (!ConsumeField(Field))
      return nullptr;
  }
  return make_unique<FieldExpressionNode>(std::move(BaseName), std::move(Path),
                                   CurType, CurStruct);
}
```

The auto-deref this chapter needs lives at the top of the `ConsumeField` lambda: if the current type going into a `.field` step is `ptr[SomeStruct]` rather than `SomeStruct` directly, I decode the pointee and continue as if it had been a plain struct all along. This is exactly the case `self` needs — `self` is `ptr[Counter]`, and `self.value` should resolve `value` on `Counter` without the programmer writing `(*self).value`.

## Pointer Auto-Deref in Codegen

The parser's auto-deref needs a matching codegen path, and it lives in `GetFieldAddress` (introduced in [Chapter 24](chapter-24.md)), not in a new function of its own. Before walking the field path with GEPs, `GetFieldAddress` now checks whether the base it resolved is itself a pointer, and if so, loads through it first to get the actual struct address:

```cpp
if (BaseType == ValueType::Pointer) {
  ValueType PointeeType = ValueType::Error;
  string PointeeStruct;
  if (!DecodePointerType(BaseStruct, PointeeType, PointeeStruct) ||
      PointeeType != ValueType::Struct)
    return nullptr;
  BasePtr = Builder->CreateLoad(LLVMTypeFor(BaseType, BaseStruct), BasePtr,
                                (BaseName + ".ptr").c_str());
  if (!BasePtr)
    return nullptr;
  BaseType = ValueType::Struct;
  BaseStruct = PointeeStruct;
}
if (BaseType != ValueType::Struct || BaseStruct.empty())
  return nullptr;
```

This is what makes `self.value = self.value + 1` work: `self` is a `ptr[Counter]` alloca, `GetFieldAddress` loads the pointer value out of it, and the rest of the function walks the struct-GEP chain exactly as it always has, just starting from the loaded address instead of the alloca itself.

I looked for this logic in `LoadPointerValue` first, since that name sounds like the obvious place for it — but `LoadPointerValue` is a different, older function that loads the value of a variable that's already known to be a plain pointer (used by pointer indexing, for instance). The auto-deref only needed to change `GetFieldAddress`, the function every field read and write already went through.

## What the IR Looks Like

I compiled this and read the IR directly rather than write down what I expected:

```pyxc
class Calc:
  value: int

  def add(x: int, y: int) -> int:
    return x + y

def main() -> int:
  var c: Calc
  return c.add(3, 4)
```

```llvm
%struct.Calc = type { i64 }

define i64 @Calc.add(ptr %self, i64 %x, i64 %y) {
entry:
  %y3 = alloca i64, align 8
  %x2 = alloca i64, align 8
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  store i64 %x, ptr %x2, align 8
  store i64 %y, ptr %y3, align 8
  %x4 = load i64, ptr %x2, align 8
  %y5 = load i64, ptr %y3, align 8
  %addtmp = add i64 %x4, %y5
  ret i64 %addtmp
}

define i64 @__pyxc.user_main() {
entry:
  %c = alloca %struct.Calc, align 8
  store %struct.Calc zeroinitializer, ptr %c, align 8
  %calltmp = call i64 @Calc.add(ptr %c, i64 3, i64 4)
  ret i64 %calltmp
}
```

`%struct.Calc = type { i64 }` uses the same `struct.`-prefixed naming [Chapter 24](chapter-24.md) established for every named aggregate, class or struct alike; it only shows up here because `main` actually declares a `Calc` variable, not because `Calc.add` alone needs it. `self` is the first argument at the call site (`ptr %c`, the address of the caller's `c`), even though the programmer never wrote it.

## Known Limitations

**Methods are only allowed on classes, not structs.** Defining a `def` inside a `struct` body is an error: "Methods are only allowed inside classes".

**`self` cannot be named by the programmer.** Writing a parameter called `self` in a method definition is rejected: "Method parameters cannot be named 'self'". The compiler owns that name.

**Method calls require an lvalue receiver.** `Calc().add(1, 2)` isn't valid; there's no temporary materialization. Use a `var` declaration first.

## Try It

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

```text
3.000000
```

## What's Next

[Chapter 38](chapter-38.md) adds constructors.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

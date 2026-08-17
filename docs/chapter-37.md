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
cd pyxc-llvm-tutorial/code/chapter-37
```

## Grammar

`struct-block` now contains `class-member` instead of just `field-declaration`. A class member is either a field or a method. `method-call-expression` joins `name-expression`. `self` is not in the grammar at all: it's injected by the compiler, never written by the programmer.

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
-class-block                       = indent field-declaration
-                                    { end-of-lines field-declaration } dedent ;
+class-block                       = indent class-member
+                                    { end-of-lines class-member } dedent ;
+class-member                      = field-declaration | method-definition ;
 field-declaration                 = name ":" type ;
+method-definition                 = "def" name "(" [ parameters ] ")"
+                                    [ "->" type ] ":"
+                                    ( simple-statement
+                                      | end-of-lines block ) ;
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
-name-expression                   = lvalue | call-expression ;
+name-expression                   = lvalue
+                                    | call-expression
+                                    | method-call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
+method-call-expression            = lvalue "." name "(" [ arguments ] ")" ;
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

## Registering the Aggregate before Its Body Is Parsed

Before this chapter, `ParseAggregateDefinition` filled in `StructTypes[AggregateName]` only once, after the whole body was parsed. That doesn't work anymore: a method signature can reference the enclosing class (a method returning `ptr[Counter]` on `Counter` itself, say), so `Counter` needs to already be in `StructTypes` while its own methods are being parsed. I register early instead, then keep the entry updated as each field or method is added:

```cppdiff
*static bool ParseAggregateDefinition(const char *KindName) {
*  ...
*  getNextToken(); // eat INDENT
*
*  StructTypeInfo Info;
*  Info.IsClass = string(KindName) == "class";
+  // Methods need the fields parsed before them, so keep the in-progress class
+  // metadata visible while I walk the body.
+  StructTypes[AggregateName] = Info;
+  vector<unique_ptr<FunctionDefinitionNode>> Methods;
*  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
+    if (CurrentToken == tok_eol) {
+      consumeNewlines();
+      continue;
+    }
+    if (CurrentToken == tok_block_end) {
+      getNextToken();
+      continue;
+    }
+    if (CurrentToken == tok_def) {
+      if (!Info.IsClass) {
+        LogErrorExpression("Methods are only allowed inside classes");
+        return false;
+      }
+      auto Method = ParseMethodDefinition(AggregateName);
+      if (!Method)
+        return false;
+      Methods.push_back(std::move(Method));
+      Info.Methods = StructTypes[AggregateName].Methods;
+      StructTypes[AggregateName] = Info;
+      if (CurrentToken == tok_eol)
+        consumeNewlines();
+      else if (CurrentToken == tok_block_end)
+        getNextToken();
+      continue;
+    }
*    if (CurrentToken != tok_name) {
*      LogErrorExpression((string("Expected field name in ") + KindName + " body"));
*      return false;
*    }
*    ...
*    Info.FieldIndices[FieldName] = Info.Fields.size();
*    Info.Fields.push_back({FieldName, FieldType, FieldStructName});
+    StructTypes[AggregateName] = Info;
*    if (CurrentToken == tok_eol)
*      consumeNewlines();
*  }
*
*  if (Info.Fields.empty()) {
*    ...
*  }
*  if (CurrentToken != tok_dedent) {
*    ...
*  }
*  StructTypes[AggregateName] = std::move(Info);
+  for (auto &Method : Methods) {
+    if (!Method->codegen())
+      return false;
+  }
*  PendingTokens.push_front(tok_block_end);
*  getNextToken(); // eat DEDENT, then surface block-end
*  return true;
*}
```

`Info.IsClass` is the one bit of state this chapter adds to `StructTypeInfo` itself, and it's what the `tok_def` branch checks: a `def` inside a `struct` body is rejected immediately, before I even try to parse it as a method. `StructTypes[AggregateName] = Info` runs again after every field and every method for the same reason it ran once early: a method later in the same body needs to see the fields (and sibling methods) declared before it, not just what the class looked like when it was first registered.

Methods aren't codegen'd as they're parsed. `ParseMethodDefinition` only registers each method's signature (in `FunctionSignatures`, keyed by the mangled name, and in `Info.Methods`); the `FunctionDefinitionNode` it returns is collected into the local `Methods` vector. Once the whole body is parsed and `StructTypes[AggregateName]` holds the final field layout, `ParseAggregateDefinition` walks `Methods` and calls `codegen()` on each one. That ordering matters: a method's body can reference any field or sibling method in the class, including ones declared textually after it, because by the time codegen runs every field and every method signature is already in `StructTypes` and `FunctionSignatures`.

## Parsing Method Definitions

`ParseMethodDefinition` handles a `def` inside a class body. Its central job is injecting `self` as the implicit first parameter, typed as a pointer to the enclosing class, since the programmer never writes it:

```cpp
static unique_ptr<FunctionDefinitionNode>
ParseMethodDefinition(const string &ClassName) {
  getNextToken(); // eat 'def'
  if (CurrentToken != tok_name)
    return LogErrorFunction("Expected method name in class definition");
  string MethodName = Name;
  SourceLocation SignatureLocation = CurrentTokenLocation;
  getNextToken(); // eat method name

  if (CurrentToken != tok_lparen)
    return LogErrorFunction("Expected '(' in method signature");
  getNextToken(); // eat '('

  vector<pair<string, ValueType>> Parameters;
  vector<string> ParameterTypeInfo;
  Parameters.push_back({"self", ValueType::Pointer});
  ParameterTypeInfo.push_back(
      EncodePointerType(ValueType::Struct, ClassName));

  if (CurrentToken != tok_rparen) {
    while (true) {
      if (CurrentToken != tok_name)
        return LogErrorFunction("Expected parameter name in method signature");
      string ParameterName = Name;
      if (ParameterName == "self")
        return LogErrorFunction("Method parameters cannot be named 'self'");
      getNextToken(); // eat parameter name
      if (CurrentToken != tok_colon)
        return LogErrorFunction("Method parameters require a type annotation");
      getNextToken(); // eat ':'
      string TypeInfo;
      ValueType Type = ParseTypeToken(&TypeInfo);
      if (Type == ValueType::Error)
        return nullptr;
      if (Type == ValueType::None)
        return LogErrorFunction("Parameters cannot have None type");
      Parameters.push_back({ParameterName, Type});
      ParameterTypeInfo.push_back(TypeInfo);
      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorFunction("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }
  getNextToken(); // eat ')'

  string ReturnTypeInfo;
  ValueType ReturnType =
      ParseOptionalReturnType(&ReturnTypeInfo, ValueType::None);
  if (ReturnType == ValueType::Error)
    return nullptr;

  string MangledName = ClassName + "." + MethodName;
  if (FunctionSignatures.count(MangledName))
    return LogErrorFunction(
        ("Method '" + MethodName + "' is already defined on '" + ClassName +
         "'")
            .c_str());

  auto Signature = make_unique<FunctionSignatureNode>(
      MangledName, std::move(Parameters), SignatureLocation, ReturnType,
      std::move(ParameterTypeInfo), ReturnTypeInfo);
  FunctionSignatures[MangledName] = Signature->clone();
  StructTypes[ClassName].Methods[MethodName] = true;

  ReturnTypeGuard ReturnGuard(ReturnType, ReturnTypeInfo);
  FunctionScopeGuard Scope(Signature->getParameters(),
                           Signature->getParameterStructNames());
  if (CurrentToken != tok_colon)
    return LogErrorFunction("Expected ':' in method definition");
  getNextToken(); // eat ':'
  auto Body = ParseFunctionBody();
  if (!Body) {
    FunctionSignatures.erase(MangledName);
    StructTypes[ClassName].Methods.erase(MethodName);
    return nullptr;
  }
  return make_unique<FunctionDefinitionNode>(std::move(Signature),
                                              std::move(Body));
}
```

`self`'s encoded pointee type is built the same way every other `ptr[T]` is: `EncodePointerType(ValueType::Struct, ClassName)`. The method's own signature is registered in `FunctionSignatures` under the mangled name before the body is parsed, so a method can call itself recursively (or call a sibling method defined later in the same class, once that method's own signature is registered). `StructTypes[ClassName].Methods[MethodName] = true` also records the method name on the class's `StructTypeInfo`, alongside its fields.

## Method Mangling

Methods are stored in `FunctionSignatures` under `ClassName.MethodName`. A method `def add()` on class `Calc` is registered as `"Calc.add"` and emitted as `@Calc.add` in the IR.

This means:
- Method names are independent of global function names — `Calc.add` and a top-level `add` are distinct entries.
- Two classes can both have a method named `add` without conflict.
- There is no runtime vtable — dispatch is a direct call to the statically-known mangled name.

## Parsing Call Sites

When the expression parser sees `receiver.methodName(`, it calls `ParseMethodCallExpression`, passing it the already-parsed `Receiver` expression and the member name. This function:

1. Confirms the receiver is an lvalue struct/class value with a known struct name, and that struct is registered as a class (`IsClass`).
2. Looks up `ClassName.MethodName` in `FunctionSignatures`.
3. Builds the implicit `self` argument by wrapping the receiver expression itself in an `AddrExpressionNode`.
4. Parses the explicit arguments, type-checking each against the signature starting at parameter index 1 (index 0 is `self`).

```cpp
static unique_ptr<ExpressionNode>
ParseMethodCallExpression(unique_ptr<ExpressionNode> Receiver,
                          const string &MethodName) {
  if (!Receiver || Receiver->getType() != ValueType::Struct ||
      Receiver->getStructName().empty())
    return LogErrorExpression("Method call base must be a class value");
  if (!Receiver->isLValue())
    return LogErrorExpression("Method call base must be assignable");

  string ClassName = Receiver->getStructName();
  auto Class = StructTypes.find(ClassName);
  if (Class == StructTypes.end() || !Class->second.IsClass)
    return LogErrorExpression("Method call base must be a class value");

  string CalleeName = ClassName + "." + MethodName;
  FunctionSignatureNode *Signature = GetFunctionSignature(CalleeName);
  if (!Signature)
    return LogErrorExpression(
        ("Unknown method '" + MethodName + "' on '" + ClassName + "'").c_str());

  vector<unique_ptr<ExpressionNode>> Arguments;
  Arguments.push_back(make_unique<AddrExpressionNode>(
      std::move(Receiver), EncodePointerType(ValueType::Struct, ClassName)));

  getNextToken(); // eat '('
  if (CurrentToken != tok_rparen) {
    size_t ParameterIndex = 1; // parameter zero is implicit self
    while (true) {
      ValueType ExpectedType = ValueType::Error;
      string ExpectedTypeInfo;
      if (ParameterIndex < Signature->getNumParameters()) {
        ExpectedType = Signature->getParameterType(ParameterIndex);
        ExpectedTypeInfo =
            Signature->getParameterStructName(ParameterIndex);
      }
      ExpectedLiteralTypeGuard Guard(ExpectedType, ExpectedTypeInfo);
      auto Argument = ParseExpression();
      if (!Argument)
        return nullptr;
      Arguments.push_back(std::move(Argument));
      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken(); // eat ','
      ++ParameterIndex;
    }
  }
  getNextToken(); // eat ')'

  if (Arguments.size() != Signature->getNumParameters())
    return LogErrorExpression(
        "Incorrect number of arguments in call to '" + CalleeName +
        "': expected " + to_string(Signature->getNumParameters() - 1) +
        ", got " + to_string(Arguments.size() - 1));
  for (size_t Index = 0; Index < Arguments.size(); ++Index) {
    ValueType ParameterType = Signature->getParameterType(Index);
    if (!IsAssignable(ParameterType, Arguments[Index]->getType()))
      return LogErrorExpression("Argument type mismatch");
    if ((ParameterType == ValueType::Struct ||
         ParameterType == ValueType::Pointer) &&
        Signature->getParameterStructName(Index) !=
            Arguments[Index]->getStructName())
      return LogErrorExpression("Argument type mismatch");
  }

  return make_unique<CallExpressionNode>(
      CalleeName, std::move(Arguments), Signature->getReturnType(),
      Signature->getReturnStructName());
}
```

The receiver must be an lvalue (`Receiver->isLValue()`), which is true for a plain named variable or for a field access chain, but not for the result of a function call. Wrapping `Receiver` directly in an `AddrExpressionNode` reuses whatever address-computing logic that expression already has (`codegenAddress`), rather than re-deriving the receiver's address from scratch inside the method-call parser.

## Dot Dispatch: Method Call vs. Field Access

Before this chapter, seeing `name.` always meant a field access. Now `.` can mean either a field access or a method call, and the parser can't tell which until it looks past the member name. This lives inside `ParseNameExpressionWithName`, in the loop that walks `.` and `[` suffixes after a variable reference. It reads the member name first, then checks whether `(` follows:

```cppdiff
*    while (CurrentToken == tok_dot || CurrentToken == tok_lbracket) {
*      if (CurrentToken == tok_dot) {
*        if (Result->getType() != ValueType::Struct ||
*            Result->getStructName().empty())
*          return LogErrorExpression("Field access requires a struct value");
*        string BaseStructName = Result->getStructName();
*        getNextToken(); // eat '.'
*        if (CurrentToken != tok_name)
-          return LogErrorExpression("Expected field name after '.'");
-        auto Struct = StructTypes.find(BaseStructName);
+          return LogErrorExpression("Expected field or method name after '.'");
+        string MemberName = Name;
+        getNextToken(); // eat member name
+        if (CurrentToken == tok_lparen) {
+          Result = ParseMethodCallExpression(std::move(Result), MemberName);
+          if (!Result)
+            return nullptr;
+          continue;
+        }
+        auto Struct = StructTypes.find(BaseStructName);
*        if (Struct == StructTypes.end())
*          return LogErrorExpression("Unknown struct type in field access");
-        auto Field = Struct->second.FieldIndices.find(Name);
-        if (Field == Struct->second.FieldIndices.end())
-          return LogErrorExpression(("Unknown field '" + Name + "'"));
+        auto Field = Struct->second.FieldIndices.find(MemberName);
+        if (Field == Struct->second.FieldIndices.end())
+          return LogErrorExpression(
+              ("Unknown field '" + MemberName + "'").c_str());
*        const auto &FieldInfo = Struct->second.Fields[Field->second];
*        Result = make_unique<MemberExpressionNode>(
*            std::move(Result), Field->second, FieldInfo.Type,
*            FieldInfo.StructName);
-        getNextToken(); // eat field name
*        continue;
*      }
*
*      // '[' indexing branch, unchanged from earlier chapters.
*      if (Result->getType() != ValueType::Pointer &&
*          Result->getType() != ValueType::Array)
*        return LogErrorExpression("Indexing requires a pointer or array value");
*      ...
*    }
```

One token of lookahead at `(` is enough to tell the two apart unambiguously. When it's a field, not a method call, the field access builds a `MemberExpressionNode` wrapping the previous `Result` and the field's index, rather than a flat name-plus-path the way the pre-chapter-37 field parser did; each `.field` step wraps the one before it, so a chain like `a.b.c` nests three levels deep.

## Auto-Deref for `self`

`self` needs one more thing that an ordinary struct variable doesn't: it's typed `ptr[ClassName]`, not `ClassName` directly, so `self.value` has to look through the pointer before it can find `value`. That case is handled earlier in `ParseNameExpressionWithName`, right after a plain variable reference is looked up and before the general dot/bracket loop above ever runs:

```cppdiff
*static unique_ptr<ExpressionNode> ParseNameExpressionWithName(const string &ParsedName) {
*  if (CurrentToken != tok_lparen) { // Simple variable ref.
*    ValueType Type = LookupVarType(ParsedName);
*    if (Type == ValueType::Error) {
*      ...
*    }
*    string StructName = LookupVarStructName(ParsedName);
*
+    // An implicit self parameter is a pointer to its class. Field access
+    // dereferences that pointer before walking the field path.
+    if (Type == ValueType::Pointer && CurrentToken == tok_dot) {
+      ValueType PointeeType = ValueType::Error;
+      string PointeeStructName;
+      if (DecodePointerType(StructName, PointeeType, PointeeStructName) &&
+          PointeeType == ValueType::Struct)
+        return ParseFieldExpressionWithBase(ParsedName, PointeeType,
+                                            PointeeStructName);
+    }
*
*    unique_ptr<ExpressionNode> Result =
*        make_unique<NameExpressionNode>(ParsedName, Type, StructName);
*    ...
*  }
*  ...
*}
```

`ParseFieldExpressionWithBase` is the field-chain parser this codebase has had since it first gained struct field access: it starts from `.` and walks the whole `.field.field...` chain itself, building a `FieldExpressionNode` keyed by a base name and a flat field path. Any pointer-typed variable followed by `.` goes through this path, not just `self`, but in practice `self` is the only pointer-to-struct variable a method body has, so this is what makes `self.value = self.value + 1` resolve `value` without the programmer writing `(*self).value`.

## Pointer Auto-Deref in Codegen

The parser's auto-deref needs a matching codegen path, and it lives in `GetFieldAddress`, the function `FieldExpressionNode::codegen` and `codegenAddress` both call to compute a field's address. Before walking the field path with GEPs, `GetFieldAddress` checks whether the base it resolved is itself a pointer, and if so, loads through it first to get the actual struct address:

```cppdiff
*static Value *GetFieldAddress(const string &BaseName,
*                              const vector<string> &FieldPath,
*                              ValueType *OutType = nullptr,
*                              string *OutStructName = nullptr) {
*  ...
*  if (!Pointer || CurrentStructName.empty())
*    return nullptr;
*
*  ValueType CurrentType = ValueType::Struct;
+  ValueType PointeeType = ValueType::Error;
+  string PointeeStructName;
+  if (DecodePointerType(CurrentStructName, PointeeType, PointeeStructName)) {
+    if (PointeeType != ValueType::Struct)
+      return nullptr;
+    Pointer = TheBuilder->CreateLoad(
+        LLVMTypeFor(ValueType::Pointer, CurrentStructName), Pointer, "self");
+    CurrentType = PointeeType;
+    CurrentStructName = PointeeStructName;
+  }
*  for (const auto &FieldName : FieldPath) {
*    auto Struct = StructTypes.find(CurrentStructName);
*    ...
*  }
*  ...
*}
```

This is what makes `self.value = self.value + 1` work: `self` is a `ptr[Counter]` alloca, `GetFieldAddress` loads the pointer value out of it (into an instruction literally named `%self` in the IR, regardless of what the source variable was called), and the rest of the function walks the struct-GEP chain exactly as it always has over `FieldPath`, just starting from the loaded address instead of the alloca itself.

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

## Build and Run

```bash
cd code/chapter-37
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

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

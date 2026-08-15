---
description: "Add public and private visibility modifiers to class fields and methods. Private members are only accessible from within the class's own method bodies."
---
# 39. pyxc: Visibility

## What I Am Building

[Chapter 38](chapter-38.md) added constructors. Classes can now be initialized, but every field and method is accessible from anywhere. After this chapter, a class can hide its internals:

```pyxc
extern def printd(x: float64)

class BoundedCounter:
  private count: int
  private limit: int

  def __init__(max: int):
    self.count = 0
    self.limit = max

  public def increment():
    if self.count < self.limit:
      self.count = self.count + 1

  public def get() -> int:
    return self.count


def main() -> int:
  var c: BoundedCounter = BoundedCounter(3)
  c.increment()
  c.increment()
  c.increment()
  c.increment()    # no effect — limit reached
  printd(float64(c.get()))
  return 0
```

```text
3.000000
```

Accessing `c.count` directly from outside the class is an error.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-39
```

## Grammar

`class-member` gains an optional visibility prefix. `visibility` is a new production:

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
-class-member                      = field-declaration | method-definition ;
+class-member                      = [ visibility ]
+                                    ( field-declaration | method-definition ) ;
+visibility                        = "public" | "private" ;
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
                                     | method-call-expression
                                     | constructor-call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 method-call-expression            = lvalue "." name "(" [ arguments ] ")" ;
 constructor-call-expression       = name "(" [ arguments ] ")" ;
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

## New Tokens

```cppdiff
*  tok_minus_minus = -63,
*  tok_class = -64,
+  tok_public = -65,
+  tok_private = -66,
*
*  // punctuation and operators
```

Both are registered in the keyword table and in the token-name map, so error messages print `'public'` and `'private'`.

## Storing Visibility Information

Visibility lands in two places on `StructTypeInfo`. Fields gain an `IsPublic` flag directly on `StructFieldInfo`:

```cpp
struct StructFieldInfo {
  string Name;
  ValueType Type;
  string StructName;
  bool IsPublic = true;
};
```

Methods are tracked separately, in a map from method name to visibility:

```cpp
struct StructTypeInfo {
  vector<StructFieldInfo> Fields;
  map<string, size_t> FieldIndices;
  map<string, bool> Methods;
  bool IsClass = false;
};
```

Methods need their own map rather than a flag next to the field, since a method's signature lives in `FunctionSignatures`, not in `StructTypeInfo::Fields` — there's no single struct visibility could hang off of the way `IsPublic` hangs off `StructFieldInfo`.

## Parsing Visibility Modifiers

The body loop inside `ParseAggregateDefinition` now reads an optional visibility token before deciding whether the member is a field or a method:

```cppdiff
*    if (CurrentToken == tok_block_end) {
*      getNextToken();
*      continue;
*    }
+    bool IsPublic = true;
+    if (CurrentToken == tok_public || CurrentToken == tok_private) {
+      if (!Info.IsClass) {
+        LogErrorExpression(
+            "Visibility modifiers are only allowed inside class bodies");
+        return false;
+      }
+      IsPublic = CurrentToken == tok_public;
+      getNextToken(); // eat visibility modifier
+    }
*    if (CurrentToken == tok_def) {
*      if (!Info.IsClass) {
*        LogErrorExpression("Methods are only allowed inside classes");
*        return false;
*      }
-      auto Method = ParseMethodDefinition(AggregateName);
+      auto Method = ParseMethodDefinition(AggregateName, IsPublic);
*      if (!Method)
*        return false;
```

With no modifier, `IsPublic` stays `true`: the default is public. A modifier inside a `struct` body is rejected immediately, before the parser even eats the token, let alone looks at what follows it.

A field's visibility rides along with everything else already pushed into `Info.Fields`:

```cppdiff
*    Info.FieldIndices[FieldName] = Info.Fields.size();
-    Info.Fields.push_back({FieldName, FieldType, FieldStructName});
+    Info.Fields.push_back(
+        {FieldName, FieldType, FieldStructName, IsPublic});
*    StructTypes[AggregateName] = Info;
```

A method's visibility is passed as an extra argument into `ParseMethodDefinition`, which now takes `bool IsPublic` and records it directly:

```cppdiff
-static unique_ptr<FunctionDefinitionNode>
-ParseMethodDefinition(const string &ClassName) {
+static unique_ptr<FunctionDefinitionNode>
+ParseMethodDefinition(const string &ClassName, bool IsPublic) {
*  ...
*  auto Signature = make_unique<FunctionSignatureNode>(
*      MangledName, std::move(Parameters), SignatureLocation, ReturnType,
*      std::move(ParameterTypeInfo), ReturnTypeInfo);
*  FunctionSignatures[MangledName] = Signature->clone();
-  StructTypes[ClassName].Methods[MethodName] = true;
+  StructTypes[ClassName].Methods[MethodName] = IsPublic;
*
*  ReturnTypeGuard ReturnGuard(ReturnType, ReturnTypeInfo);
*  ...
*}
```

`Info.Methods` is copied back out of `StructTypes[AggregateName]` after every method (`Info.Methods = StructTypes[AggregateName].Methods;`), for the same reason [Chapter 37](chapter-37.md) already re-registers `StructTypes[AggregateName] = Info` after every member: methods parsed earlier in the body need to stay visible while later members are parsed, and vice versa.

## Enforcing Private Access

Access is decided by one small function:

```cpp
static string CurrentClassScopeName;

static bool CanAccessClassMember(const string &OwnerClass, bool IsPublic) {
  return IsPublic || (!CurrentClassScopeName.empty() &&
                      CurrentClassScopeName == OwnerClass);
}
```

A member is reachable if it's `public`, or if the code currently being parsed belongs to the same class the member is on. "Currently being parsed" is `CurrentClassScopeName`, set and restored by an RAII guard:

```cpp
struct ClassScopeGuard {
  string SavedClassName;
  ClassScopeGuard(const string &ClassName)
      : SavedClassName(CurrentClassScopeName) {
    CurrentClassScopeName = ClassName;
  }
  ~ClassScopeGuard() { CurrentClassScopeName = SavedClassName; }
};
```

`ParseMethodDefinition` instantiates a `ClassScopeGuard` before parsing the method's body. When the method is done, the destructor restores whatever `CurrentClassScopeName` was before — empty at the top level, since pyxc has no nested classes to restore into instead.

## Access Checks at Every Use Site

`CanAccessClassMember` is checked wherever the parser resolves a class member, which turns out to be four places, not one:

**Field access, in both places a `.field` chain gets resolved.** `ParseFieldExpressionWithBase` walks a chain that starts from an already-typed base (used for the auto-deref case, where `self` is `ptr[SomeStruct]` and the pointee is decoded before the walk begins):

```cppdiff
*static unique_ptr<ExpressionNode>
*ParseFieldExpressionWithBase(const string &BaseName, ValueType BaseType,
*                             string BaseStructName) {
*  ...
*  while (CurrentToken == tok_dot) {
*    getNextToken(); // eat '.'
*    ...
*    const auto &Field = Struct->second.Fields[FieldIndex->second];
+    if (!CanAccessClassMember(ResultStructName, Field.IsPublic))
+      return LogErrorExpression(
+          ("Field '" + FieldName + "' is private on '" +
+           ResultStructName + "'")
+              .c_str());
*    ResultType = Field.Type;
*    ResultStructName = Field.StructName;
*    FieldPath.push_back(FieldName);
*    ...
*  }
*  ...
*}
```

`ParseNameExpressionWithName` has its own inline dot-dispatch loop for the ordinary `name.field` case, with the identical check inline:

```cppdiff
*    while (CurrentToken == tok_dot || CurrentToken == tok_lbracket) {
*      if (CurrentToken == tok_dot) {
*        ...
*        string MemberName = Name;
*        getNextToken(); // eat member name
*        if (CurrentToken == tok_lparen) {
*          Result = ParseMethodCallExpression(std::move(Result), MemberName);
*          ...
*        }
*        auto Struct = StructTypes.find(BaseStructName);
*        ...
*        const auto &FieldInfo = Struct->second.Fields[Field->second];
+        if (!CanAccessClassMember(BaseStructName, FieldInfo.IsPublic))
+          return LogErrorExpression(
+              ("Field '" + MemberName + "' is private on '" +
+               BaseStructName + "'")
+                  .c_str());
*        Result = make_unique<MemberExpressionNode>(
*            std::move(Result), Field->second, FieldInfo.Type,
*            FieldInfo.StructName);
*        continue;
*      }
*      ...
*    }
```

Both paths reject reading *and* writing a private field, since assignment and read both resolve the field chain through one of these two places before anything else happens.

**Method call**, in `ParseMethodCallExpression`, right after resolving `ClassName.MethodName`:

```cppdiff
*static unique_ptr<ExpressionNode>
*ParseMethodCallExpression(unique_ptr<ExpressionNode> Receiver,
*                          const string &MethodName) {
*  ...
*  string ClassName = Receiver->getStructName();
*  auto Class = StructTypes.find(ClassName);
*  if (Class == StructTypes.end() || !Class->second.IsClass)
*    return LogErrorExpression("Method call base must be a class value");
+  auto Visibility = Class->second.Methods.find(MethodName);
+  if (Visibility != Class->second.Methods.end() &&
+      !CanAccessClassMember(ClassName, Visibility->second))
+    return LogErrorExpression(
+        ("Method '" + MethodName + "' is private on '" + ClassName + "'")
+            .c_str());
*
*  string CalleeName = ClassName + "." + MethodName;
*  ...
*}
```

**Constructor call**, in `ParseNameExpressionWithName`, guarded by whether `__init__` exists at all:

```cppdiff
*  // A class name in call position constructs a value of that class.
*  auto Class = StructTypes.find(ParsedName);
*  if (Class != StructTypes.end() && Class->second.IsClass) {
*    getNextToken(); // eat '('
*    string InitializerName = ParsedName + ".__init__";
*    FunctionSignatureNode *Initializer =
*        GetFunctionSignature(InitializerName);
+    if (Initializer) {
+      auto Visibility = Class->second.Methods.find("__init__");
+      if (Visibility != Class->second.Methods.end() &&
+          !CanAccessClassMember(ParsedName, Visibility->second))
+        return LogErrorExpression(
+            ("Method '__init__' is private on '" + ParsedName + "'").c_str());
+    }
*    vector<unique_ptr<ExpressionNode>> Arguments;
*    ...
*  }
```

A private `__init__` makes `ClassName(args)` fail from outside the class, the same way a private method or field would.

## IR Is Unchanged

Visibility is enforced entirely while parsing. Nothing changes in the generated IR: `public` and `private` leave no trace in the output. A `private` field and a `public` field of the same type generate identical IR.

## Known Limitations

**There is no `protected`.** Access is either class-private or world-public; there's no subclass-visible middle tier, since pyxc has no inheritance.

**Visibility modifiers on structs are rejected outright.** `struct` members are always public. The parser errors the moment it sees `public` or `private` before a struct member, rather than silently ignoring the modifier.

## Build and Run

```bash
cd code/chapter-39
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

**Private field, accessed from outside**

```pyxc
class Foo:
  private x: int

def main() -> int:
  var f: Foo
  f.x = 3
  return 0
```

```text
Error (Line 6, Column 7): Field 'x' is private on 'Foo'
```

**Private constructor, called from outside**

```pyxc
class Foo:
  x: int
  private def __init__():
    self.x = 0

def main() -> int:
  var f: Foo = Foo()
  return 0
```

```text
Error (Line 7, Column 20): Method '__init__' is private on 'Foo'
```

**Visibility modifier on a struct**

```pyxc
struct Foo:
  private x: int
```

```text
Error (Line 2, Column 3): Visibility modifiers are only allowed inside class bodies
```

## What's Next

[Chapter 40](chapter-40.md) adds traits.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

---
section: "Data and Memory"
description: "Add struct types with field declarations, field read/write, and nested field access."
---
# 24. pyxc: Structs

## What I Am Building

I think I'll add *structs* to the language now. I have enough *scalar* types and I'm keen on getting some structural help from the language for my data so I can keep related information together. The following is what I'm hoping to have by the end of this chapter. 

```pyxc
# defining a structure with multiple elements
struct Point:
  x: int
  y: int

extern def printd(x: float64)

# passing a structure by value and accessing different elements of the structure
def distance_sq(p: Point) -> float64:
  return float64(p.x * p.x + p.y * p.y)

def main() -> int:
  var p: Point # define it
  p.x = 3 # mutate element
  p.y = 4 # mutate element

  # pass to a function and print the result
  printd(distance_sq(p))  # 25.000000
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-24
```

## Grammar

First I'll extend the grammar cause it helps me write the lexer and parser better. 

To define something like this in my grammar:

```pyxc
struct Point:
```

I could write:

```ebnf
struct-definition = "struct" name ":" ;
```

`name` can be any legal pyxc identifier, `Point`, `Car`, whatever. Next I deal with fields.

```pyxc
struct Point: # an end-of-line follows
  x: int  # an indent, then a field
```

which I add as...

```ebnf
struct-definition = "struct" name ":" end-of-lines indent field-declaration ;
```

I need multiple fields, and then a `dedent` to mark that I'm done with them, so I wrap `field-declaration` in `{ }` (zero or more) and add the remaining pieces to finish `struct-definition`:

```ebnf
struct-definition = "struct" name ":" end-of-lines struct-block ;
struct-block      = indent field-declaration { end-of-lines field-declaration } dedent ;
```

Since a field looks like
```pyxc
x: int
```
I define a field as:

```ebnf
field-declaration = name ":" type ;
```

Next I'll deal with accessing struct fields:

```pyxc
printd(p.x) # pyxc won't understand p.x just yet so I have to extend the grammar
```

```ebnf
field-access = name "." name { "." name } ;
```

I grouped `"." name` under `{ }` because I might have structs containing structs, so I could have something like `route.destination.x`.

Structs also define a new type, so I need to extend the `type` rule too. I split the old `type` production into `builtin-type` (everything it used to mean) and a new `struct-type`, then let `type` be either one:

```ebnf
builtin-type = "int" | "int8" | ... | "bool" | "None" ; (* everything type used to mean *)
struct-type  = name ; (* struct name; must be declared above the point of use *)
type         = builtin-type | struct-type ;
```

None of these new pieces are reachable yet, though: I've defined `struct-definition` and `field-access` as standalone productions, but nothing in the existing grammar points at them. `struct-definition` needs to join `top-level-item` alongside `function-definition` and the rest, and `field-access` needs to plug into both `lvalue` (so `p.x = 5` parses as an assignment target) and `primary` (so `p.x` parses as a value to read). Here's the real diff against [Chapter 23](chapter-23.md)'s grammar with all of that wired in:

```grammardiff
*...
*end-of-lines                      = end-of-line { end-of-line } ;
*top-level-item                    = function-definition
+                                    | struct-definition
*                                    | external
*                                    | top-level-statement ;
+struct-definition                 = "struct" name ":" end-of-lines
+                                    struct-block ;
+struct-block                      = indent field-declaration
+                                    { end-of-lines field-declaration } dedent ;
+field-declaration                 = name ":" type ;
*function-definition               = "def" function-signature [ "->" type ] ":"
*                                    ( simple-statement
*...
*sum                               = term { ("+" | "-") term } ;
*term                              = factor { ("*" | "/" | "%") factor } ;
-lvalue                            = name ;
+lvalue                            = name | field-access ;
*variable-binding                  = name ":" type [ "=" expression ] ;
*factor                            = ("-" | "!" | "~") factor | primary ;
*primary                           = cast-expression
*                                    | name-expression
+                                    | field-access
*                                    | number-expression
*                                    | boolean-literal
*                                    | parenthesized-expression ;
*cast-expression                   = cast-type "(" expression ")" ;
*name-expression                   = name | call-expression ;
*call-expression                   = name "(" [ arguments ] ")" ;
+field-access                      = name "." name { "." name } ;
*arguments                         = expression { "," expression } ;
*number-expression                 = number ;
*...
*name                              = (letter | "_")
*                                    { letter | digit | "_" } ;
-type                              = "int" | "int8" | "int16" | "int32"
-                                    | "int64" | "uint8" | "uint16"
-                                    | "uint32" | "uint64"
-                                    | "float" | "float32"
-                                    | "float64" | "bool" | "None" ;
+type                              = builtin-type | struct-type ;
+builtin-type                      = "int" | "int8" | "int16" | "int32"
+                                    | "int64" | "uint8" | "uint16"
+                                    | "uint32" | "uint64"
+                                    | "float" | "float32"
+                                    | "float64" | "bool" | "None" ;
+struct-type                       = name ;
*cast-type                         = "int" | "int8" | "int16" | "int32"
*                                    | "int64" | "uint8" | "uint16"
*...
```

I think that should do it. I'll try implementing this first and come back to it if I see gaps in the language. I can already see that I haven't extended the field accessor notation to expressions, so I can't do something like:

```pyxc
make_point().x
```

I think for now, this is ok. 

## The `struct` Keyword

I'll start extending the lexer/parser. First I need a token for the `struct` keyword.  

```cppdiff
*enum Token {
*  ...
*  tok_switch = -47,
*  tok_case = -48,
*  tok_default = -49,
+  tok_struct = -50,
*
*  // punctuation and operators
*  ...
*};
```

I will also need to add the `struct` string to my keywords map

```cppdiff
*static map<string, Token> Keywords = {
*    ...
*    {"switch", tok_switch},   {"case", tok_case},
-    {"default", tok_default},
+    {"default", tok_default}, {"struct", tok_struct},
*    {"float", tok_float},
*    ...
*};
```

Great, now I can read the struct definitions and emit the tokens.

## Where Do I Keep Track of Struct Definitions?

Now that the lexer hands me a `tok_struct`, I need somewhere to actually record what a struct looks like once I've parsed it. What do I need to know about a struct? Its name, and its list of fields: each with a name and a type.

I'm also going to need to catch two mistakes as I parse: defining the same struct twice, and declaring the same field twice inside one struct. Both of those are "have I seen this name before?" checks, so I want a lookup by name, not just a list I'd have to scan linearly. A `map<string, ...>` keyed on the name gets me that.

So: one map for the fields of a single struct, and one map for all the structs I know about:

```cpp
struct StructFieldInfo {
  string Name;
  ValueType Type;
  string StructName;
};
```

I added `StructName` to the field because a field's type might itself be a struct (a struct containing a struct), and `ValueType::Struct` alone doesn't tell me *which* struct: I'll run into this same problem again in a minute for variables generally.

```cpp
struct StructTypeInfo {
  vector<StructFieldInfo> Fields;
  map<string, size_t> FieldIndices;  // field name → index into Fields
};
```

I kept `Fields` as an ordered `vector` and *also* added `FieldIndices`, a map from field name to its position in that vector. I need the vector because field order matters: it's the order LLVM will lay the fields out in memory, and I'll need to walk them in order for codegen. But I also need fast lookup by name for two things: checking for a duplicate field while parsing, and later, resolving `p.x` to "field 0" when I generate code for it. A map alongside the vector gets me both: ordered storage, and O(log n) lookup by name.

And then the registry that ties struct names to this info, so I can look up any struct I've seen so far:

```cpp
static std::map<string, StructTypeInfo> StructTypes;
```

`StructTypes` is the global registry of all declared structs. It gets populated as I parse `struct` blocks, and I'll consult it constantly afterward: every field access and every struct type annotation needs to look the struct up here to validate it.

## Parsing a Struct Definition

With the data structures in place I can write the actual parsing function. Let me walk through the grammar rule again and turn it into code step by step:

```ebnf
struct-definition = "struct" name ":" end-of-lines struct-block ;
struct-block      = indent field-declaration { end-of-lines field-declaration } dedent ;
```

`CurrentToken` is `tok_struct` when this function is called, so first thing, eat it and expect a name:

```cpp
getNextToken(); // eat 'struct'
if (CurrentToken != tok_name) {
  LogErrorExpression("Expected name after 'struct'");
  return false;
}
string StructName = Name;
```

Before I go any further I should check whether I've already seen this struct: that's exactly the "have I seen this name before" check I built `StructTypes` for:

```cpp
if (StructTypes.count(StructName)) {
  LogErrorExpression("Struct already defined");
  return false;
}
```

Then the `':' NEWLINE INDENT` part of the grammar, which is just token bookkeeping I've done before for function bodies:

```cpp
getNextToken(); // eat name
if (CurrentToken != tok_colon) {
  LogErrorExpression("Expected ':' after struct name");
  return false;
}
getNextToken(); // eat ':'
if (CurrentToken != tok_eol) {
  LogErrorExpression("Expected newline after struct header");
  return false;
}
consumeNewlines();
if (CurrentToken != tok_indent) {
  LogErrorExpression("Expected an indented struct body");
  return false;
}
getNextToken(); // eat INDENT
```

Now the `field+` part. I loop, reading one field per iteration, until I hit the `DEDENT`. Each field is `identifier ':' type NEWLINE`, so inside the loop I read a name, a colon, and a type:

```cpp
StructTypeInfo Info;
while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
  if (CurrentToken != tok_name) {
    LogErrorExpression("Expected field name in struct body");
    return false;
  }
  string FieldName = Name;
  if (Info.FieldIndices.count(FieldName)) {
    LogErrorExpression("Duplicate struct field");
    return false;
  }
  getNextToken(); // eat field name
  if (CurrentToken != tok_colon) {
    LogErrorExpression("Expected ':' after field name");
    return false;
  }
  getNextToken(); // eat ':'
  string FieldStructName;
  ValueType FieldType = ParseTypeToken(&FieldStructName);
  if (FieldType == ValueType::Error)
    return false;
  if (FieldType == ValueType::None) {
    LogErrorExpression("Struct fields cannot have None type");
    return false;
  }
```

I'm reusing `ParseTypeToken` here rather than writing a separate type parser for struct fields: it already knows how to parse `int`, `float64`, and so on, and I'm about to teach it to also recognize other struct names as types. One parser, every place a type can appear. The duplicate-field check runs before I even try to parse the type — that's the other reason I built `FieldIndices` as a map, and it means I catch `x: int` twice before caring whether the second one's type is even valid.

```cpp
  Info.FieldIndices[FieldName] = Info.Fields.size();
  Info.Fields.push_back({FieldName, FieldType, FieldStructName});
  if (CurrentToken == tok_eol)
    consumeNewlines();
}
```

`Info.Fields.size()` before the push is exactly the index the new field is about to land at, so I record that in `FieldIndices` first, then push. Once the loop exits, one more check before I register the struct — it needs at least one field:

```cpp
if (Info.Fields.empty()) {
  LogErrorExpression("Struct requires at least one field");
  return false;
}
if (CurrentToken != tok_dedent) {
  LogErrorExpression("Expected dedent after struct body");
  return false;
}
StructTypes[StructName] = std::move(Info);
PendingTokens.push_front(tok_block_end);
getNextToken(); // eat DEDENT, then surface block-end
return true;
```

That `PendingTokens.push_front(tok_block_end)` trick isn't new to this chapter: I'm reusing the same synthetic-token mechanism I used for function bodies, so whatever calls `ParseStructDefinition` sees a clean `tok_block_end` marker after the DEDENT instead of having to special-case struct endings.

## The `struct` Handler

I need a top-level handler like I have for `def` and `extern`. It just calls the parser and recovers from errors the same way the others do:

```cpp
static void HandleStructDefinition() {
  bool Parsed = ParseStructDefinition();
  bool HasTrailing = CurrentToken != tok_eol && CurrentToken != tok_eof &&
                     CurrentToken != tok_block_end;
  if (!Parsed || HasTrailing) {
    if (Parsed)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    DiscardRestOfLine();
    return;
  }
  Log("Parsed a struct definition.\n");
}
```

And wire it into both loops that dispatch on the current token: the REPL's `MainLoop` and the file-mode `FileModeLoop`:

```cppdiff
*    switch (CurrentToken) {
*    case tok_eol:
*      ...
*      break;
+    case tok_struct:
+      HandleStructDefinition();
+      break;
*    case tok_def:
*      HandleFunctionDefinition();
*      break;
*    case tok_extern:
*      HandleExtern();
*      break;
*    default:
*      HandleTopLevelStatement();
*      break;
*    }
```

Let's handle field access now.

## `struct` as a Type

Before I can write `x: int` *or* `p: Point` in the same field/parameter/variable declaration, `ParseTypeToken` needs to accept a struct name where it currently only accepts the scalar keywords. An identifier that isn't a keyword and shows up where a type is expected: that's a struct name, if it's one I know about:

```cpp
case tok_name: {
  auto Found = StructTypes.find(Name);
  if (Found == StructTypes.end()) {
    LogErrorExpression(("Unknown struct type '" + Name + "'"));
    return ValueType::Error;
  }
  if (StructName)
    *StructName = Name;
  getNextToken();
  return ValueType::Struct;
}
```

This is why I keep needing that "struct name alongside the type" pattern: `ValueType::Struct` on its own doesn't say *which* struct, so `ParseTypeToken` takes an optional `string *StructName` output parameter, and every caller that cares about struct types passes one in. I checked `StructTypes` for the name rather than just accepting any identifier: this also means a struct has to be declared *before* anything uses it as a type. No forward references. I could lift that restriction later with a pre-pass that just collects names, but I don't need it yet.

## Two New AST Nodes

Now for the parts of the grammar I haven't touched yet: reading a field and writing to one. Each needs its own AST node, because they compile to different code (a load vs. a `getelementptr` + store), even though they share a lot of the same "walk the field path" logic.

Both of these new nodes need to carry a struct name alongside their `ValueType`, and `ExpressionNode` couldn't do that before this chapter: it only tracked a bare `ValueType`. I extend the base class with a `StructName` member and widen `setType` to take it as a second, defaulted argument, so every existing call site that only ever set a `ValueType` keeps compiling unchanged:

```cppdiff
 class ExpressionNode {
   ValueType Type = ValueType::Error;
+  string StructName;
 
 public:
   virtual ~ExpressionNode() = default;
   ValueType getType() const { return Type; }
+  const string &getStructName() const { return StructName; }
   // getLValueName - If this node is a plain assignable variable, return its
   // name; otherwise return nullptr.
   virtual const string *getLValueName() const { return nullptr; }
+  virtual const vector<string> *getLValueFieldPath() const { return nullptr; }
   // isReturnExpr - True iff this node is a return statement.
   virtual bool isReturnExpr() const { return false; }
*  ...
   virtual Value *codegen() = 0;
 
 protected:
-  void setType(ValueType NewType) { Type = NewType; }
+  void setType(ValueType NewType, const string &NewStructName = "") {
+    Type = NewType;
+    StructName = NewStructName;
+  }
 };
```

I also add `getLValueFieldPath()` here, defaulting to `nullptr` the same way `getLValueName()` already does. Only `FieldExpressionNode` will override it below; it's what lets `ParseLeadingNameSimpleStatement` tell a plain variable reference apart from a field chain when it decides how to parse the right side of `=`.

### Field Read

A field read: `p.x`, `o.inner.value`. What does this node actually need to remember? Not the whole chain as one string: I want the pieces separately so codegen can walk them one GEP at a time. So: the name of the variable at the root, and the list of field names after it.

```cpp
class FieldExpressionNode : public ExpressionNode {
  string BaseName;           // the variable at the root: "p" or "o"
  vector<string> FieldPath;  // the chain of field names: ["x"] or ["inner", "value"]

public:
  FieldExpressionNode(string BaseName, vector<string> FieldPath, ValueType Type,
                      const string &StructName = "")
      : BaseName(std::move(BaseName)), FieldPath(std::move(FieldPath)) {
    setType(Type, StructName);
  }
  const string *getLValueName() const override { return &BaseName; }
  const vector<string> &getFieldPath() const { return FieldPath; }
  const vector<string> *getLValueFieldPath() const override {
    return &FieldPath;
  }
  Value *codegen() override;
};
```

The type of the whole expression is whatever the *last* field in the path resolves to, so I set that in the constructor once the parser has walked the chain. `getLValueName()` returns `&BaseName`: that's what assignment codegen will use to find the root pointer to start GEP-ing from.

### Field Write

A field write: `p.x = 5`. This one just needs the field expression on the left (so it knows *where* to write) and an expression on the right (what to write):

```cpp
class FieldAssignmentStatementNode : public ExpressionNode {
  unique_ptr<FieldExpressionNode> Left;
  unique_ptr<ExpressionNode> Right;

public:
  FieldAssignmentStatementNode(unique_ptr<FieldExpressionNode> Left,
                               unique_ptr<ExpressionNode> Right,
                               ValueType Type,
                               const string &StructName = "")
      : Left(std::move(Left)), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};
```

Like the plain `AssignmentStatementNode` I already have, `shouldPrintValue()` returns `false`: an assignment shouldn't print anything at the REPL.

## Parsing Field Access

Now the actual parsing. `ParseFieldExpressionWithBase` gets called once the parser already has the base identifier's name, type, and struct name in hand: it walks the chain of `.field` steps, and at each step, looks the field up in `StructTypes` to figure out what type it produces, so it can validate the *next* step in the chain and so the final node knows its own type.

I check up front that the base is even a struct; there's nothing to access a field *of* otherwise:

```cpp
static unique_ptr<ExpressionNode>
ParseFieldExpressionWithBase(const string &BaseName, ValueType BaseType,
                             string BaseStructName) {
  if (BaseType != ValueType::Struct)
    return LogErrorExpression("Field access requires a struct value");

  vector<string> FieldPath;
  ValueType ResultType = BaseType;
  string ResultStructName = std::move(BaseStructName);
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected field name after '.'");
    string FieldName = Name;
    auto Struct = StructTypes.find(ResultStructName);
    if (Struct == StructTypes.end())
      return LogErrorExpression("Unknown struct type in field access");
    auto FieldIndex = Struct->second.FieldIndices.find(FieldName);
    if (FieldIndex == Struct->second.FieldIndices.end())
      return LogErrorExpression(
          ("Unknown field '" + FieldName + "'").c_str());
    const auto &Field = Struct->second.Fields[FieldIndex->second];
    ResultType = Field.Type;
    ResultStructName = Field.StructName;
    FieldPath.push_back(FieldName);
    getNextToken(); // eat field name
    if (CurrentToken == tok_dot && ResultType != ValueType::Struct)
      return LogErrorExpression("Field access requires a struct value");
  }

  return make_unique<FieldExpressionNode>(BaseName, std::move(FieldPath),
                                           ResultType, ResultStructName);
}
```

`FieldIndices` earns its keep here: I use it to find the field's entry in `Fields`, then advance `ResultType`/`ResultStructName` to that field's type before the next loop iteration. By the time the loop ends, they describe the *leaf* field: that's what the whole `p.x` or `o.inner.value` expression evaluates to. This is why `route.destination.x` from the grammar section just falls out for free: each `.` step is the same lookup, chained. The check after each field is what stops the chain from continuing past a non-struct field: `p.x.y` fails there once `x` resolves to a plain `int`.

I call this from `ParseNameExpressionWithName`, right after I've looked up the base identifier's type. If it's a struct variable and the next token is `.`, I hand off to `ParseFieldExpressionWithBase`:

```cppdiff
*static unique_ptr<ExpressionNode> ParseNameExpressionWithName(const string &ParsedName) {
*  if (CurrentToken != tok_lparen) { // Simple variable ref.
*    ValueType Type = LookupVarType(ParsedName);
*    if (Type == ValueType::Error) {
*      return LogErrorExpression("Unknown variable name");
*    }
+    string StructName = LookupVarStructName(ParsedName);
+    if (CurrentToken == tok_dot)
+      return ParseFieldExpressionWithBase(ParsedName, Type, StructName);
-    return make_unique<NameExpressionNode>(ParsedName, Type);
+    return make_unique<NameExpressionNode>(ParsedName, Type, StructName);
*  }
*
*  // Call.
*  ...
*}
```

Since this only ever fires from a bare name that's already resolved to a variable, `make_point().x` never reaches `ParseFieldExpressionWithBase` at all: there's no name to look up, so it's rejected earlier, before field access parsing is even in the picture. That's what enforces the "field access must start with a named variable" limitation I noted in the grammar section.

Field access on the *left* of `=` doesn't get special-cased at the point of the `.`: `p.x = 5` first parses as an ordinary expression, the same `p.x` any read would produce, and only after that's done does `ParseLeadingNameSimpleStatement` check whether a trailing `=` follows and whether what it just parsed has a field path:

```cppdiff
*static unique_ptr<ExpressionNode> ParseLeadingNameSimpleStatement() {
*  ...
*  // Optional assignment tail: (<expr>) = ...
*  if (CurrentToken != tok_assign)
*    return Expr;
*
+  if (const auto *FieldPath = Expr->getLValueFieldPath()) {
+    const string *BaseName = Expr->getLValueName();
+    auto Field = make_unique<FieldExpressionNode>(
+        *BaseName, *FieldPath, Expr->getType(), Expr->getStructName());
+    return ParseFieldAssignmentRight(std::move(Field));
+  }
+
*  const string *AssignedName = Expr->getLValueName();
*  if (!AssignedName)
*    return LogErrorExpression("Destination of '=' must be a variable");
*
*  return ParseAssignmentRight(*AssignedName);
*}
```

`getLValueFieldPath()` is what a plain variable reference always returns `nullptr` for; only a `FieldExpressionNode` overrides it to return its actual path. Once that's confirmed, a fresh `FieldExpressionNode` is rebuilt from the pieces and handed to `ParseFieldAssignmentRight`:

```cpp
static unique_ptr<ExpressionNode>
ParseFieldAssignmentRight(unique_ptr<FieldExpressionNode> Left) {
  ValueType FieldType = Left->getType();
  string FieldStructName = Left->getStructName();
  getNextToken(); // eat '='
  ExpectedLiteralTypeGuard Guard(FieldType);
  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  if (!IsAssignable(FieldType, Right->getType()))
    return LogErrorExpression("Type mismatch in field assignment");
  if (FieldType == ValueType::Struct &&
      FieldStructName != Right->getStructName())
    return LogErrorExpression("Struct type mismatch in field assignment");
  return make_unique<FieldAssignmentStatementNode>(
      std::move(Left), std::move(Right), FieldType, FieldStructName);
}
```

Same `IsAssignable` check I already use for plain variable assignment: a struct field is just an assignable location with a type, same rules apply. The extra struct-name check only fires when the field itself is struct-typed: an `int` field doesn't have a struct name to mismatch on.

That extra struct-name check isn't unique to field assignment: anywhere a struct-typed value flows from one place to another, `ValueType::Struct` alone doesn't catch a mismatch, since two unrelated structs both report the same `ValueType`. I add the same comparison at the other three places a struct value crosses a boundary.

Passing an argument to a function, in the call-parsing branch of `ParseNameExpressionWithName`:

```cpp
if (ParamType == ValueType::Struct &&
    Signature->getParameterStructName(i) != Arguments[i]->getStructName())
  return LogErrorExpression("Struct argument type mismatch");
```

Returning a value, in `ParseReturnStatement`:

```cpp
if (CurrentFunctionReturnType == ValueType::Struct &&
    CurrentFunctionReturnStructName != Expr->getStructName())
  return LogErrorExpression("Struct return type mismatch");
```

Plain variable assignment, in `ParseAssignmentRight`:

```cpp
if (VarType == ValueType::Struct &&
    LookupVarStructName(Name) != Right->getStructName())
  return LogErrorExpression("Struct type mismatch in assignment");
```

And variable declaration with an initializer, in `ParseVarStatement`:

```cpp
if (DeclType == ValueType::Struct &&
    DeclStructName != Init->getStructName())
  return LogErrorExpression("Struct type mismatch in variable initialization");
```

Same shape every time: the ordinary `IsAssignable` check already passes, because both sides report `ValueType::Struct`, so I add one more comparison on the struct name itself, gated so it only runs when the type actually is `Struct`.

## A Lurking Lexer Bug

I was trying to run one of my `.pyxc` test files and hit a bug that was already there but only surfaced now. The number lexer entered the float-parsing path whenever it saw a standalone `.`:

```cpp
// Before: wrong
if (isdigit(LexerLastChar) || LexerLastChar == '.') {
```

That meant `p.x` would lex as: identifier `p`, then see `.` and enter the number-parsing path, find `x` instead of a digit, and produce garbage. Fine when `.` meant nothing on its own. Fatal now that it separates a variable from its field.

The fix: only enter the float path when the character *after* `.` is actually a digit. I already had a `peek()` helper for exactly this kind of one-character lookahead, so I just use it:

```cpp
// After: correct
if (isdigit(LexerLastChar) ||
    (LexerLastChar == '.' && isdigit(peek()))) {
```

`.5` still works as a float literal. `p.x` no longer gets eaten.

## Tracking Struct Names in Scope

Field access parsing needs to know a variable's struct name, not just that it's `ValueType::Struct`: I keep running into this. Chapter 18 already tracks variable *types* with `VarScopes: vector<map<string, ValueType>>`, a stack of maps for nested scopes. I need the same shape of thing, but for struct names, so I add a parallel stack rather than changing what `VarScopes` stores:

```cpp
static vector<std::map<string, string>> VarStructScopes;
```

I kept it separate instead of, say, changing `VarScopes` to hold a `(ValueType, string)` pair, because most variables aren't structs and I don't want every scope lookup paying for a string that's usually empty. Every place that pushes or pops a scope for `VarScopes` now does the same for `VarStructScopes` right alongside it: `BeginFunctionScope`, `BeginBlockScope`, `BeginLoopScope`, and their `End*` counterparts. And `DeclareVar` records into both when the variable being declared has a struct name:

```cpp
static void DeclareVar(const string &Name, ValueType Type,
                       const string &StructName = "") {
  // Only declare into an active local scope; at top level VarScopes is empty.
  if (VarScopes.empty())
    return;
  VarScopes.back()[Name] = Type;
  if (!StructName.empty())
    VarStructScopes.back()[Name] = StructName;
}
```

And lookup mirrors `LookupVarType` exactly: walk the scope stack innermost-first, fall back to the globals map if nothing local matches:

```cpp
static string LookupVarStructName(const string &Name) {
  for (auto It = VarStructScopes.rbegin(); It != VarStructScopes.rend(); ++It) {
    auto Found = It->find(Name);
    if (Found != It->end())
      return Found->second;
  }
  auto Global = GlobalVarStructNames.find(Name);
  return Global == GlobalVarStructNames.end() ? "" : Global->second;
}
```

Function parameters need the same treatment for the same reason: a parameter's `ValueType::Struct` alone doesn't say which struct. `FunctionSignatureNode` already stored `Parameters` as `vector<pair<string, ValueType>>`; rather than restructure that, I add a parallel vector alongside it, the same "keep it separate" choice I made for `VarStructScopes`:

```cppdiff
*class FunctionSignatureNode {
*  string Name;
*  vector<pair<string, ValueType>> Parameters;
+  vector<string> ParameterStructNames;
*  ValueType ReturnType;
+  string ReturnStructName;
*  SourceLocation Loc;
*
*public:
*  ...
*};
```

`ParameterStructNames` is resized to match `Parameters` in the constructor, so `getParameterStructName(Index)` can always index it safely; an ordinary scalar parameter just carries an empty string at its slot. `ReturnStructName` gets the same treatment for the same reason: a function returning a struct needs to say which one. Same mechanics as everywhere else in this chapter; just more places to carry the extra string.

## From Struct Name to LLVM Type

Everything so far has been the parser's view of a struct: I know the fields, I know the types, I've validated field accesses. Now I actually need to generate code, which means I need a real `StructType*` LLVM object, not just my own `StructTypeInfo`.

```cpp
static std::map<std::string, StructType *> LLVMStructTypes;

static Type *GetOrCreateLLVMStructType(const string &StructName) {
  auto Existing = LLVMStructTypes.find(StructName);
  if (Existing != LLVMStructTypes.end())
    return Existing->second;

  auto Definition = StructTypes.find(StructName);
  if (Definition == StructTypes.end())
    return nullptr;

  auto *LLVMStruct = StructType::create(*TheContext, "struct." + StructName);
  LLVMStructTypes[StructName] = LLVMStruct;
  vector<Type *> FieldTypes;
  for (const auto &Field : Definition->second.Fields) {
    Type *FieldType = LLVMTypeFor(Field.Type, Field.StructName);
    if (!FieldType)
      return nullptr;
    FieldTypes.push_back(FieldType);
  }
  LLVMStruct->setBody(FieldTypes, false);
  return LLVMStruct;
}
```

I look struct definitions up with `StructTypes.find` rather than `StructTypes[StructName]`: the indexing operator would silently insert an empty `StructTypeInfo` for a name I don't recognize, and I'd rather bail out with `nullptr` than build a zero-field struct type for a typo.

I need the cache: `LLVMStructTypes`: because LLVM creates a brand-new `StructType` object every time I call `StructType::create` with the same name; it doesn't deduplicate for me. Without the cache, two separate `alloca`s for the same pyxc struct would end up backed by two different LLVM types that just happen to have the same layout but different identity: every load, store, and GEP mixing them would fail.

I also deliberately register the type in the cache *before* I fill in its body. That's not an accident: it's what lets a struct hold a pointer to itself without this function recursing forever. (A struct containing *itself by value*, rather than a pointer to itself, would need infinite memory, so that case can't come up in code that passed my earlier checks anyway.)

`setBody(FieldTys, false)`: the `false` is "not packed," meaning fields get natural alignment, same default as a C struct.

And I wire it into `LLVMTypeFor`, the function everything else in codegen already goes through to turn a `ValueType` into an LLVM `Type*`:

```cppdiff
*static Type *LLVMTypeFor(ValueType Type, const string &StructName) {
*  switch (Type) {
*  ...
*  case ValueType::Bool:
*    return Type::getInt1Ty(*TheContext);
+  case ValueType::Struct:
+    return GetOrCreateLLVMStructType(StructName);
*  case ValueType::None:
*    return Type::getVoidTy(*TheContext);
*  default:
*    return nullptr;
*  }
*}
```

## The IR Layout

Let me check what this actually produces. For:

```pyxc
struct Point:
  x: int
  y: int
```

I get, with the `"struct."` prefix I chose above:

```llvm
%struct.Point = type { i64, i64 }
```

`int` is pointer-width, `i64` on my 64-bit host: that's not new to this chapter, just carried over. A struct with a `float64` field:

```pyxc
struct Circle:
  radius: float64
```

```llvm
%struct.Circle = type { double }
```

Fields show up in declaration order, which matches what I said `Fields` needed to preserve back when I chose a `vector` over just a `map`. LLVM inserts whatever padding the target's data layout calls for: I don't see it in the IR, but it's there in the generated machine code.

## Codegen: Getting a Field's Address

Both reading and writing a field come down to the same first step: compute a pointer to the field, then either load from it or store to it. So I want one function that does the pointer arithmetic, shared by both. `GetFieldAddress` walks `FieldPath` one step at a time, the same way `ParseFieldExpressionWithBase` did at parse time: except now I need an actual base pointer, not just a type.

I look the base variable up in `NamedValues` first (a local), and fall back to a global if it's not local. Each side has its own struct-name lookup table, `NamedValueStructNames` for locals and `GlobalVarStructNames` for globals, the codegen-time counterpart of `VarStructScopes`:

```cpp
static Value *GetFieldAddress(const string &BaseName,
                              const vector<string> &FieldPath,
                              ValueType *OutType = nullptr,
                              string *OutStructName = nullptr) {
  Value *Pointer = nullptr;
  string CurrentStructName;

  auto Local = NamedValues.find(BaseName);
  if (Local != NamedValues.end() && Local->second) {
    Pointer = Local->second;
    auto Struct = NamedValueStructNames.find(BaseName);
    if (Struct != NamedValueStructNames.end())
      CurrentStructName = Struct->second;
  } else if (auto *Global = GetGlobalVariable(BaseName)) {
    Pointer = Global;
    auto Struct = GlobalVarStructNames.find(BaseName);
    if (Struct != GlobalVarStructNames.end())
      CurrentStructName = Struct->second;
  }

  if (!Pointer || CurrentStructName.empty())
    return nullptr;

  ValueType CurrentType = ValueType::Struct;
  for (const auto &FieldName : FieldPath) {
    auto Struct = StructTypes.find(CurrentStructName);
    if (Struct == StructTypes.end())
      return nullptr;
    auto Field = Struct->second.FieldIndices.find(FieldName);
    if (Field == Struct->second.FieldIndices.end())
      return nullptr;

    const auto &FieldInfo = Struct->second.Fields[Field->second];
    Pointer = TheBuilder->CreateStructGEP(
        LLVMTypeFor(CurrentType, CurrentStructName), Pointer, Field->second,
        "fieldptr");
    CurrentType = FieldInfo.Type;
    CurrentStructName = FieldInfo.StructName;
  }

  if (OutType)
    *OutType = CurrentType;
  if (OutStructName)
    *OutStructName = CurrentStructName;
  return Pointer;
}
```

`FieldIndices` again: same map, now doing its third job: turning a field name into the integer index `CreateStructGEP` actually wants. `CreateStructGEP` emits a `getelementptr inbounds` for struct field access; one GEP per step in the path. For `p.x` on a `Point`:

```llvm
%fieldptr = getelementptr inbounds %struct.Point, ptr %p, i32 0, i32 0
```

And for `o.inner.value`, where `inner` is itself an `Inner`, I get one GEP per level rather than a single multi-index GEP: I could combine them into one instruction with multiple indices, but chaining single-field GEPs is simpler to emit and LLVM optimizes it the same either way:

```llvm
%fieldptr  = getelementptr inbounds %struct.Outer, ptr %o, i32 0, i32 0
%fieldptr1 = getelementptr inbounds %struct.Inner, ptr %fieldptr, i32 0, i32 0
```

## Codegen: Reading and Writing Fields

With `GetFieldAddress` written, the two AST nodes' `codegen()` methods are almost trivial.

**Read**: get the pointer, load through it:

```cpp
Value *FieldExpressionNode::codegen() {
  ValueType FieldType = ValueType::Error;
  string FieldStructName;
  Value *Pointer = GetFieldAddress(*getLValueName(), FieldPath, &FieldType,
                                   &FieldStructName);
  if (!Pointer)
    return LogErrorValue("Unknown field access");
  return TheBuilder->CreateLoad(LLVMTypeFor(FieldType, FieldStructName), Pointer,
                             "fieldload");
}
```

For `p.x` where `x: int`:

```llvm
%fieldptr  = getelementptr inbounds %struct.Point, ptr %p, i32 0, i32 0
%fieldload = load i64, ptr %fieldptr
```

**Write**: get the pointer, codegen the right side, cast if the types don't line up exactly, then store:

```cpp
Value *FieldAssignmentStatementNode::codegen() {
  ValueType FieldType = ValueType::Error;
  string FieldStructName;
  Value *Pointer = GetFieldAddress(*Left->getLValueName(), Left->getFieldPath(),
                                   &FieldType, &FieldStructName);
  if (!Pointer)
    return LogErrorValue("Unknown field access");

  Value *AssignedValue = Right->codegen();
  if (!AssignedValue)
    return nullptr;
  AssignedValue = EmitImplicitCast(AssignedValue, Right->getType(), FieldType);
  if (!AssignedValue)
    return LogErrorValue("Type mismatch in assignment");
  TheBuilder->CreateStore(AssignedValue, Pointer);
  return AssignedValue;
}
```

For `p.x = 5` where `x: int`:

```llvm
%fieldptr = getelementptr inbounds %struct.Point, ptr %p, i32 0, i32 0
store i64 5, ptr %fieldptr
```

I didn't need to write any new casting logic here: `EmitImplicitCast` is the same helper every other assignment already goes through. Assigning a `float64` into an `int` field is still a type error; assigning an `int8` into an `int` field still widens silently. A struct field is just another typed storage location as far as casting is concerned.

## Struct Variables and Zero Initialization

I already have zero-initialization for scalar `var` declarations with no initializer. Structs should work the same way, just with a struct-shaped zero value instead of a scalar one. `var p: Point` with no initializer allocates stack space and zero-fills it:

```cpp
Value *InitVal = Init ? Init->codegen()
                      : ZeroConstant(VarType, VarStructName);
if (!InitVal)
  return nullptr;
if (Init) {
  InitVal = EmitImplicitCast(InitVal, Init->getType(), VarType);
  if (!InitVal)
    return LogErrorValue("Type mismatch in variable initialization");
}

AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName, VarType,
                                            VarStructName);
TheBuilder->CreateStore(InitVal, Alloca);
```

`ZeroConstant` for a struct doesn't need to build up a `{0, 0}` aggregate field by field: LLVM has a shortcut, `Constant::getNullValue`, that produces an all-zero constant of whatever type I hand it:

```llvm
%p = alloca %struct.Point
store %struct.Point zeroinitializer, ptr %p
```

There's no struct initializer syntax yet: I can't write `var p: Point = Point{x: 1, y: 2}`. That's more grammar and more parsing I don't need for this chapter. Struct variables always start zeroed, and fields get assigned individually after.

## Structs Are Passed by Value

I haven't written any special-casing for struct parameters: they go through the exact same by-value parameter passing every other type already uses. Worth checking that actually does what I expect, though:

```pyxc
struct Box:
  value: int

def clobber(b: Box) -> None:
  b.value = 0

def main() -> int:
  var b: Box
  b.value = 99
  clobber(b)
  # b.value is still 99 here
  return 0
```

The IR confirms it:

```llvm
define void @clobber(%struct.Box %b) {
entry:
  %b1 = alloca %struct.Box, align 8
  store %struct.Box %b, ptr %b1, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Box, ptr %b1, i32 0, i32 0
  store i64 0, ptr %fieldptr, align 8
  ret void
}
```

LLVM renames the entry-block alloca to `%b1` since the parameter itself already claimed the name `%b`; functionally it's the same "shadow copy" alloca every parameter gets. `clobber` gets its own copy of `b`, allocated fresh inside its own stack frame. Writing to `b.value` inside `clobber` only ever touches that copy. The caller's struct is untouched after the call: which is what I'd want by default, but it does mean that if I ever want a function to mutate the caller's struct, passing by value can't do that. I'll need a pointer for that, which is [Chapter 25](chapter-25.md).

## Global Struct Variables

I didn't have to write anything new here either: struct globals fall out of the existing global-variable machinery once `ZeroConstant` and `LLVMTypeFor` both handle `ValueType::Struct`:

```pyxc
struct Counter:
  value: int

var g: Counter
```

```llvm
@g = global %struct.Counter zeroinitializer
```

And field reads/writes on globals go through the same `GetFieldAddress` I already wrote: it checks `NamedValues` for a local first, then falls back to `GetGlobalVariable`, exactly like every other variable lookup in this compiler.

## Build and Run

```bash
cd code/chapter-24
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## Try It

### Basic Field Access

```pyxc
struct Point:
  x: int
  y: int

extern def printd(x: float64)

def main() -> int:
  var p: Point
  p.x = 3
  p.y = 4
  printd(float64(p.x + p.y))
  return 0
```

```bash
7.000000
```

### Passing a Struct to a Function

```pyxc
struct Point:
  x: int
  y: int

extern def printd(x: float64)

def sum_point(p: Point) -> int:
  return p.x + p.y

def main() -> int:
  var p: Point
  p.x = 5
  p.y = 7
  printd(float64(sum_point(p)))
  return 0
```

```bash
12.000000
```

### Nested Field Access

```pyxc
struct Inner:
  value: int

struct Outer:
  inner: Inner

extern def printd(x: float64)

def main() -> int:
  var o: Outer
  o.inner.value = 9
  printd(float64(o.inner.value))
  return 0
```

```bash
9.000000
```

### Inspect the IR

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'struct\|getelementptr\|alloca' out.ll
```

**Basic field access (`point.pyxc`):**

```llvm
; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Point = type { i64, i64 }

declare void @printd(double)

define i64 @__pyxc.user_main() {
entry:
  %p = alloca %struct.Point, align 8
  store %struct.Point zeroinitializer, ptr %p, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Point, ptr %p, i32 0, i32 0
  store i64 3, ptr %fieldptr, align 8
  %fieldptr1 = getelementptr inbounds nuw %struct.Point, ptr %p, i32 0, i32 1
  store i64 4, ptr %fieldptr1, align 8
  %fieldptr2 = getelementptr inbounds nuw %struct.Point, ptr %p, i32 0, i32 0
  %fieldload = load i64, ptr %fieldptr2, align 8
  %fieldptr3 = getelementptr inbounds nuw %struct.Point, ptr %p, i32 0, i32 1
  %fieldload4 = load i64, ptr %fieldptr3, align 8
  %addtmp = add i64 %fieldload, %fieldload4
  %sitofp = sitofp i64 %addtmp to double
  call void @printd(double %sitofp)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
```

**Nested field access (`inner_outer.pyxc`):**

```llvm
; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Outer = type { %struct.Inner }
%struct.Inner = type { i64 }

declare void @printd(double)

define i64 @__pyxc.user_main() {
entry:
  %o = alloca %struct.Outer, align 8
  store %struct.Outer zeroinitializer, ptr %o, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Outer, ptr %o, i32 0, i32 0
  %fieldptr1 = getelementptr inbounds nuw %struct.Inner, ptr %fieldptr, i32 0, i32 0
  store i64 9, ptr %fieldptr1, align 8
  %fieldptr2 = getelementptr inbounds nuw %struct.Outer, ptr %o, i32 0, i32 0
  %fieldptr3 = getelementptr inbounds nuw %struct.Inner, ptr %fieldptr2, i32 0, i32 0
  %fieldload = load i64, ptr %fieldptr3, align 8
  %sitofp = sitofp i64 %fieldload to double
  call void @printd(double %sitofp)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
```

## Known Limitations

**No struct initializer syntax.** `var p: Point = Point{x: 1, y: 2}` is not supported. Fields must be assigned individually after declaration.

**No struct-to-struct copy.** `var p2: Point = p1` is not supported. Whole-struct initialization from another variable isn't implemented yet.

**Field access must start with a named variable.** `make_point().x` is rejected: the base must be a variable in scope, not an expression.

**No pointer-to-struct.** Functions take structs by value. To share a struct across functions and have modifications be visible to the caller, you need a pointer: that's [Chapter 25](chapter-25.md).

## What's Next

[Chapter 25](chapter-25.md) adds pointer types.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

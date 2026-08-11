---
section: "Object-Oriented Features"
description: "Add the class keyword as a second way to declare an aggregate type, sharing every bit of struct's parsing and layout machinery."
---
# 36. pyxc: Classes

## What I Am Building

[Chapter 35](chapter-35.md) added compound assignment and `++`/`--`. The type system covers scalars, structs, pointers, aliases, and fixed-size sequences. The one aggregate keyword I have is `struct`. Before I can add methods, constructors, or visibility in later chapters, I need a second keyword to hang those concepts off of: `class`.

After this chapter:

```pyxc
class Point:
  x: int
  y: int

def main() -> int:
  var p: Point
  p.x = 3
  p.y = 4
  return 0
```

I ran this; it compiles and exits cleanly. Right now, `class` behaves exactly like `struct`, same field layout, same field access, same everything. The distinction is nowhere yet, not even as a hidden flag; that's deliberate. This chapter is only about parsing the keyword and sharing the existing struct machinery. Whatever a future chapter needs to tell classes and structs apart, once methods show up, isn't here yet.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-36
```

## Grammar

`top-level-item` gains `class-definition`, and `class-definition` itself is new. It shares `struct-block` with `struct-definition` entirely; there's no separate body grammar for a class. Everything else is unchanged from [Chapter 27](chapter-27.md):

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | type-alias
                                     | struct-definition
+                                    | class-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
+class-definition                  = "class" name ":" end-of-lines
+                                    class-block ;
 type-alias                        = "type" name "=" type ;
 struct-block                      = indent field-declaration
+                                    { end-of-lines field-declaration } dedent ;
+class-block                       = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 field-declaration                 = name ":" type ;
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
 name-expression                   = lvalue | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
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

## One New Token

```cpp
tok_class = -40,
```

```cpp
{"struct", tok_struct},   {"class", tok_class},     {"ptr", tok_ptr},
{"addr", tok_addr},       {"sizeof", tok_sizeof},   {"type", tok_type}
```

## One Parser, Two Keywords

Before this chapter, struct parsing lived in a function that only knew about `struct`. Rather than write a second, nearly identical function for `class`, I parameterize the existing one on which keyword actually introduced this definition, using that only to build readable error messages:

```cpp
static bool ParseAggregateDefinition(const char *KindName) {
  // CurrentToken is 'struct' or 'class'
  getNextToken(); // eat keyword
  if (CurrentToken != tok_name) {
    LogErrorExpression((string("Expected ") + KindName + " name").c_str());
    return false;
  }
  string StructName = Name;
  if (TypeAliases.count(StructName)) {
    LogErrorExpression(("Name '" + StructName + "' is already defined as a type alias")
                 .c_str());
    return false;
  }
  if (StructTypes.count(StructName)) {
    LogErrorExpression(("Aggregate '" + StructName + "' is already defined").c_str());
    return false;
  }
  getNextToken(); // eat aggregate name
  if (CurrentToken != tok_colon) {
    LogErrorExpression((string("Expected ':' after ") + KindName + " name").c_str());
    return false;
  }
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent) {
    LogErrorExpression((string("Expected an indented ") + KindName + " body").c_str());
    return false;
  }
  getNextToken(); // eat INDENT

  StructTypeInfo Info;
  Info.Name = StructName;
  while (CurrentToken != tok_dedent && CurrentToken != tok_block_end && CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken != tok_name) {
      LogErrorExpression(
          (string("Expected field name in ") + KindName + " body").c_str());
      return false;
    }
    string FieldName = Name;
    getNextToken();
    if (CurrentToken != tok_colon) {
      LogErrorExpression("Expected ':' after field name");
      return false;
    }
    getNextToken();
    string FieldStructName;
    ValueType FieldType = ParseTypeToken(&FieldStructName);
    if (FieldType == ValueType::Error || FieldType == ValueType::None) {
      LogErrorExpression((string("Invalid ") + KindName + " field type").c_str());
      return false;
    }
    if (Info.FieldIndex.count(FieldName)) {
      LogErrorExpression((string("Duplicate ") + KindName + " field '" + FieldName + "'")
                   .c_str());
      return false;
    }
    Info.FieldIndex[FieldName] = Info.Fields.size();
    Info.Fields.push_back({FieldName, FieldType, FieldStructName});
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }
  if (CurrentToken != tok_dedent) {
    LogErrorExpression((string("Expected dedent after ") + KindName + " body").c_str());
    return false;
  }
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end
  StructTypes[StructName] = std::move(Info);
  return true;
}
```

`KindName` shows up in every error message this function produces, "Expected class name," "Duplicate class field 'x'," and so on, but that's the entire extent of what distinguishes the two keywords here. By the time `Info` lands in `StructTypes`, there's nothing left in it that remembers whether the source said `struct` or `class`. `StructTypeInfo` itself has no flag for it:

```cpp
struct StructTypeInfo {
  string Name;
  vector<StructFieldInfo> Fields;
  std::map<string, size_t> FieldIndex;
};
```

`HandleStructDef` and the new `HandleClassDef` just call this with the matching string:

```cpp
static void HandleClassDef() {
  bool Ok = ParseAggregateDefinition("class");
  if (!Ok) {
    SynchronizeToLineBoundary();
    return;
  }
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (HasTrailing) {
    LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
}
```

And both `MainLoop` and `FileModeLoop` get a matching case:

```cpp
case tok_class:
  HandleClassDef();
  break;
```

## The IR Doesn't Know Either

I confirmed there's no hidden distinction at the LLVM level either. A `class` produces exactly the type a `struct` with the same fields would:

```pyxc
class Vec2:
  x: float64
  y: float64

def main() -> int:
  var v: Vec2
  v.x = 1.0
  return 0
```

```llvm
%struct.Vec2 = type { double, double }
```

Note the name: LLVM's named type is `%struct.Vec2`, not `%Vec2`. That `struct.` prefix is a naming convention I apply uniformly to every named aggregate type, regardless of whether the source used `struct` or `class`, so there's nothing there to distinguish them either.

## Conflict Rules

Struct names and class names share one namespace, the same `StructTypes` map, so defining one under a name the other already used is rejected regardless of order:

```pyxc
struct Foo:
  x: int

class Foo:
  y: int
```

```text
Error (Line 4, Column 7): Aggregate 'Foo' is already defined
```

The type-alias namespace is shared too. A class name colliding with an existing alias is rejected the same way a struct name colliding with an alias already was, both routed through the same `TypeAliases.count(StructName)` check near the top of `ParseAggregateDefinition`.

## Build and Run

```bash
cd code/chapter-36
cmake -S . -B build && cmake --build build
```

## Try It

<!-- code-merge:start -->
```pyxc
extern def printd(x: float64)

class Point:
  x: int
  y: int

def main() -> int:
  var p: Point
  p.x = 3
  p.y = 4
  printd(float64(p.x + p.y))
  return 0
```
```text
7.000000
```
<!-- code-merge:end -->

Same field layout, same field access, same everything `struct Point` would give me — `class` is a pure alias for `struct`'s machinery at this point.

## Known Limitations

**No distinction exists yet, anywhere.** Not a flag, not a naming difference, nothing. If a later chapter needs to treat classes differently from structs, giving them methods, for instance, it has to introduce that tracking itself; this chapter deliberately doesn't.

**Same body grammar as struct.** A class body is field declarations only, exactly like a struct. Nothing method-shaped parses yet.

## What's Next

[Chapter 37](chapter-37.md) adds methods and `self`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

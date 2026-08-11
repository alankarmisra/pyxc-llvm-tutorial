---
description: "Add variadic extern declarations so pyxc code can call C functions like printf and scanf that take a variable number of arguments."
---
# 33. pyxc: Variadic Extern Functions

## What I Am Building

[Chapter 32](chapter-32.md) added Unicode escapes and validated UTF-8 strings. pyxc can call C functions via `extern def`, but only functions with a fixed number of typed parameters. `printf`, `scanf`, `sprintf`, and most other C I/O functions take a variable number of arguments — the `...` in their C signatures. Trying to declare them currently produces:

```pyxc
type string = ptr[int8]
extern def printf(fmt: string, ...) -> int32
```
```
Error (Line 2, Column 32): Expected parameter name in function signature
extern def printf(fmt: string, ..
                               ^~~~
```

After this chapter, variadic `extern` declarations work:

```pyxc
type string = ptr[int8]
extern def printf(fmt: string, ...) -> int32

def main() -> int:
  printf("hello world\n")
  printf("answer: %ld\n", 42)
  return 0
```

```
hello world
answer: 42
```

`%ld`, not `%d` — pyxc's `int` is 64-bit, and `printf`'s `%d` expects a 32-bit `int`. `%d` only works once I've explicitly cast the argument down to `int32`.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-33
```

## Grammar

I add a separate signature production for `extern`, since only `extern` allows `...`:

`code/chapter-33/pyxc.ebnf`

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | type-alias
                                     | struct-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
 type-alias                        = "type" name "=" type ;
 struct-block                      = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 field-declaration                 = name ":" type ;
 function-definition               = "def" function-signature [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
-external                          = "extern" "def" function-signature [ "->" type ] ;
+external                          = "extern" "def" external-function-signature
+                                    [ "->" type ] ;
 top-level-statement               = statement ;
 function-signature                = name "(" [ parameters ] ")" ;
+external-function-signature       = name "(" [ parameters [ "," "..." ] | "..." ] ")" ;
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
 assignment-statement              = lvalue "=" expression ;
 simple-statement                  = return-statement
                                     | break-statement
                                     | continue-statement
                                     | variable-statement
                                     | assignment-statement
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
 expression                        = logical-or ;
 logical-or                        = logical-and { "||" logical-and } ;
 logical-and                       = bitwise-or { "&&" bitwise-or } ;
 bitwise-or                        = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor                       = bitwise-and { "^" bitwise-and } ;
 bitwise-and                       = equality { "&" equality } ;
 equality                          = relational { ("==" | "!=") relational } ;
 relational                        = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift                             = sum { ("<<" | ">>") sum } ;
 sum                               = term { ("+" | "-") term } ;
 term                              = factor { ("*" | "/" | "%") factor } ;
 lvalue                            = name
                                     { "." name | "[" expression "]" } ;
 variable-binding                  = name ":" type [ "=" expression ] ;
 factor                            = ("-" | "!" | "~") factor | primary ;
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

Regular function signatures (used by `def`) are untouched — `...` isn't valid there.

## A New Field for Variadic Functions

The structural change is a new `bool IsVariadic` field on `FunctionSignatureNode`:

```cpp
class FunctionSignatureNode {
  string Name;
  vector<pair<string, ValueType>> Parameters;
  vector<string> ParameterStructNames;
  ValueType ReturnType;
  string ReturnStructName;
  bool IsVariadic;
  SourceLocation Loc;

public:
  FunctionSignatureNode(const string &Name,
                        vector<pair<string, ValueType>> Parameters,
                        SourceLocation Loc,
                        ValueType ReturnType = ValueType::Float64,
                        vector<string> ParameterStructNames = {},
                        string ReturnStructName = "",
                        bool IsVariadic = false)
      : Name(Name), Parameters(std::move(Parameters)), ReturnType(ReturnType),
        ReturnStructName(std::move(ReturnStructName)), IsVariadic(IsVariadic),
        Loc(Loc) {
    this->ParameterStructNames = std::move(ParameterStructNames);
    this->ParameterStructNames.resize(this->Parameters.size());
  }
  ...
  bool isVariadic() const { return IsVariadic; }
};
```

`IsVariadic` defaults to `false`, so every existing call site that builds a `FunctionSignatureNode` keeps working unchanged. Only the `extern def` path ever sets it to `true`.

## Allowing Variadic Arguments in Parsing

`ParseFunctionSignature` gains an `AllowVariadic` parameter that defaults to `false`. Inside the parameter loop, I check for `...` before I check for a parameter name:

```cpp
static unique_ptr<FunctionSignatureNode>
ParseFunctionSignature(bool AllowVariadic = false) {
  ...
  bool IsVariadic = false;
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (AllowVariadic && CurrentToken == tok_dot) {
        getNextToken(); // eat the first '.'
        if (CurrentToken != tok_dot)
          return LogErrorSignature(
              "Expected '...' in variadic function signature");
        getNextToken(); // eat the second '.'
        if (CurrentToken != tok_dot)
          return LogErrorSignature(
              "Expected '...' in variadic function signature");
        getNextToken(); // eat the third '.'
        IsVariadic = true;
        if (CurrentToken != tok_rparen)
          return LogErrorSignature(
              "Variadic marker must be last in parameter list");
        break;
      }
      ...
    }
  }

  getNextToken(); // eat ')'
  return make_unique<FunctionSignatureNode>(
      FnName, std::move(ParameterNames), SignatureLoc, ValueType::Float64,
      std::move(ParameterStructNames), "", IsVariadic);
}
```

`...` isn't a single lexer token — the lexer just hands me three separate `tok_dot` tokens, and I consume them one at a time here. If any of the three is missing, `LogErrorSignature` fires immediately:

```pyxc
extern def bad(fmt: ptr[int8], ..) -> int32
```
```
Error (Line 1, Column 34): Expected '...' in variadic function signature
extern def bad(fmt: ptr[int8], ..) 
                                 ^~~~
```

`...` also has to be the last thing before `)` — the `break` right after setting `IsVariadic` guarantees the loop can't come back around for another parameter. And since the `...` check runs before I even look for a parameter name, `extern def f(...)` with no fixed parameters at all works the same way — it just hits that branch on the very first iteration.

## Only `extern def` Allows Variadic Arguments

`ParseFunctionDefinition` calls `ParseFunctionSignature()` with the implicit default `false`. Only `ParseExtern` passes `true`:

```cpp
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat extern.
  ...
  auto Signature = ParseFunctionSignature(true);
  ...
}
```

So `...` in a regular function definition fails, because `AllowVariadic` is `false` there and the first `.` isn't a valid parameter name:

```pyxc
def bad(x: int, ...) -> int:
  return x
```
```
Error (Line 1, Column 17): Expected parameter name in function signature
def bad(x: int, ..
                ^~~~
```

There's no way around this — `...` only exists in `extern def`. pyxc has no `va_list`, `va_start`, or `va_arg`, so I can't write a variadic pyxc function myself, only declare and call variadic C ones.

## Arity Check Updated at Call Sites

The call-site arity check used to be an exact match. For a variadic signature it becomes "at least the fixed count," both at parse time and in codegen:

```cpp
// ParseNameExpressionWithName:
if ((!Signature->isVariadic() &&
     Signature->getNumParameters() != Arguments.size()) ||
    (Signature->isVariadic() &&
     Arguments.size() < Signature->getNumParameters()))
  return LogErrorExpression("Incorrect # arguments passed");
```

The codegen-side check runs the same logic, but against the LLVM `Function` object rather than my own `FunctionSignatureNode`. LLVM's `Function` class already has an `isVarArg()` method built in, so I use that directly:

```cpp
// CallExpressionNode::codegen:
if ((!CalleeF->isVarArg() && CalleeF->arg_size() != Arguments.size()) ||
    (CalleeF->isVarArg() && Arguments.size() < CalleeF->arg_size()))
  return LogErrorV("Incorrect # arguments passed");
```

```pyxc
extern def printf(fmt: ptr[int8], ...) -> int32
printf()
```
```
Error (Line 2, Column 9): Incorrect # arguments passed
printf()
        ^~~~
```

Type-checking only walks the fixed parameters — `for (size_t i = 0; i < Signature->getNumParameters(); ++i)`. Anything past that is on me: I can pass whatever I want after the fixed parameters, and pyxc won't check it against anything, the same way C's own `printf` doesn't.

## Codegen Passes the Variadic Flag Through

`FunctionSignatureNode::codegen` passes `IsVariadic` to `FunctionType::get` instead of the hardcoded `false` it used before:

```cpp
FunctionType *FT = FunctionType::get(
    LLVMTypeFor(ReturnType, ReturnStructName), ParameterTypes,
    IsVariadic);
```

That's the whole change on the codegen side. LLVM already knows how to emit a variadic declaration and handle the variadic calling convention at each call site — I just have to tell it `IsVariadic` is true:

```
declare i32 @printf(ptr, ...)
```

## Build and Run

```bash
cd code/chapter-33
cmake -S . -B build && cmake --build build
./build/pyxc
```

## Try It

```pyxc
extern def printf(fmt: ptr[int8], ...) -> int32
extern def sqrt(x: float64) -> float64

def main() -> int:
  printf("sqrt(2) = %f\n", sqrt(2.0))
  printf("%ld args after the format string, this time\n", 1)
  return 0
```

```bash
pyxc --emit exe -o vararg vararg.pyxc
./vararg
```

```
sqrt(2) = 1.414214
1 args after the format string, this time
```

## What's Next

[Chapter 34](chapter-34.md) allows assignment to appear inside an expression.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

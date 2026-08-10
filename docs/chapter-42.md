---
description: "Add variadic extern declarations so pyxc code can call C functions like printf and scanf that take a variable number of arguments."
---
# 42. pyxc: Variadic Extern Functions

## What I Am Building

[Chapter 41](chapter-41.md) completed the K&R toolbox. pyxc can call C functions via `extern def`, but only functions with a fixed number of typed parameters. `printf`, `scanf`, `sprintf`, and most other C I/O functions take a variable number of arguments — the `...` in their C signatures. Trying to declare them currently produces:

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
cd pyxc-llvm-tutorial/code/chapter-42
```

## Grammar

I add a separate signature production for `extern`, since only `extern` allows `...`:

`code/chapter-42/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
 top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | external | top-level-expression ;
 type-alias       = "type" name "=" type ;
 trait-definition        = "trait" name [ "[" name "]" ] ":" end-of-lines trait-block ;
 trait-block      = indent trait-method-signature { end-of-lines trait-method-signature } dedent ;
 trait-method-signature  = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ;
 struct-definition       = "struct" name ":" end-of-lines struct-block ;
 class-definition        = "class" name [ "(" trait-reference { "," trait-reference } ")" ] ":" end-of-lines struct-block ;
 trait-reference        = name [ "[" type "]" ] ;
 implementation-definition         = "impl" trait-reference "for" name ":" end-of-lines implementation-block ;
 implementation-block       = indent implementation-method { end-of-lines implementation-method } dedent ;
 implementation-method      = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")" [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 struct-block     = indent class-member { end-of-lines class-member } dedent ;
 class-member     = [ visibility ] ( field-declaration | method-definition ) ;
 visibility      = "public" | "private" ;
 method-definition       = "def" name "(" [ typed-parameter { "," typed-parameter } ] ")"
                   [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 field-declaration       = name ":" type ;
 function-definition      = "def" function-signature [ "->" type ] ":" ( simple-statement | end-of-lines block ) ;
 (* If the return type is omitted, it defaults to None. *)
-external        = "extern" "def" function-signature [ "->" type ] ;
+external        = "extern" "def" external-function-signature [ "->" type ] ;
 top-level-expression    = expression ;
 function-signature       = name "(" [ typed-parameter { "," typed-parameter } ] ")" ;
+external-function-signature = name "(" [ typed-parameter { "," typed-parameter } [ "," "..." ] | "..." ] ")" ;
 typed-parameter      = name ":" type ;
 if-statement          = "if" expression ":" suite
                 { end-of-lines "elif" expression ":" suite }
                 [ end-of-lines "else" ":" suite ] ;
 while-statement       = "while" expression ":" suite ;
 do-while-statement     = "do" ":" suite end-of-lines "while" expression ;
 switch-statement      = "switch" expression ":" end-of-lines indent switch-body dedent ;
 switch-body      = switch-case { end-of-lines switch-case } [ end-of-lines default-case ] ;
 switch-case      = "case" switch-integer { "," switch-integer } ":" suite ;
 default-case     = "default" ":" suite ;
 for-statement         = "for"
                   ( "var" name ":" type | name )
                   "=" expression "," expression "," expression ":" suite ;
 variable-statement         = "var" variable-binding { "," variable-binding } ;
 assignment-statement      = lvalue assignment-operator expression ; (* assignment is a statement here *)
 simple-statement      = return-statement | break-statement | continue-statement | variable-statement | assignment-statement | expression ;
 compound-statement    = if-statement | for-statement | while-statement | do-while-statement | switch-statement ;
 statement       = simple-statement | compound-statement ;
 suite           = simple-statement | compound-statement | end-of-lines block ;
 return-statement      = "return" [ expression ] ;
 break-statement       = "break" ;
 continue-statement    = "continue" ;
 statement-separator = end-of-lines | BLOCK_END ;
 block = indent statement { statement-separator statement } dedent ;
 expression      = assignment ;
 assignment      = logical-or [ assignment-operator assignment ] ;
 logical-or      = logical-and { "||" logical-and } ;
 logical-and     = bitwise-or { "&&" bitwise-or } ;
 bitwise-or      = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor     = bitwise-and { "^" bitwise-and } ;
 bitwise-and     = equality { "&" equality } ;
 equality        = relational { ("==" | "!=") relational } ;
 relational      = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift           = sum { ("<<" | ">>") sum } ;
 sum             = term { ("+" | "-") term } ;
 term            = unary-expression { ("*" | "/" | "%") unary-expression } ;
 lvalue          = name | field-access | index-expression ;
 variable-binding      = name ":" type [ "=" expression ] ;
 unary-expression       = ("-" | "!" | "~" | "++" | "--") unary-expression | postfix-expression ;
 postfix-expression     = primary [ postfix-operator ] ;
 postfix-operator       = "++" | "--" ;
 primary         = cast-expression | sizeof-expression | address-expression | array-literal | string-literal | character-literal | name-expression | field-access | index-expression | number-expression | boolean-literal | parenthesized-expression ;
 cast-expression        = cast-type "(" expression ")" ;
 sizeof-expression      = "sizeof" "(" type ")" ;
 address-expression        = "addr" "(" lvalue ")" ;
 name-expression  = name | call-expression | method-call-expression | constructor-call-expression ;
 call-expression        = name "(" [ expression { "," expression } ] ")" ;
 method-call-expression  = name "." name "(" [ expression { "," expression } ] ")" ;
 constructor-call-expression    = name "(" [ expression { "," expression } ] ")" ;
 field-access     = name "." name { "." name } ;
 index-expression       = name "[" expression "]" ;
 number-expression      = number ;
 array-literal    = "[" [ expression { "," expression } ] "]" ;
 string-literal   = "\"" { ? valid Unicode scalar value except " and newline, encoded as UTF-8 ? | literal-escape } "\"" ;
 character-literal     = "'" ( ? valid Unicode scalar value except ' and newline, encoded as UTF-8 ? | literal-escape ) "'" ;
 literal-escape   = "\\" ( simple-escape | octal-escape | "x" hex-digit hex-digit
                    | "u" hex-digit hex-digit hex-digit hex-digit
                    | "U" hex-digit hex-digit hex-digit hex-digit
                          hex-digit hex-digit hex-digit hex-digit ) ;
 simple-escape    = "a" | "b" | "f" | "n" | "r" | "t" | "v"
                  | "\\" | "'" | "\"" | "?" ;
 octal-escape     = octal-digit [ octal-digit [ octal-digit ] ] ;
 parenthesized-expression       = "(" expression ")" ;
 indent          = INDENT ;
 dedent          = DEDENT ;
 
 assignment-operator        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 name      = (letter | "_") { letter | digit | "_" } ;
 builtin-type     = "int" | "int8" | "int16" | "int32" | "int64"
                 | "uint8" | "uint16" | "uint32" | "uint64"
                 | "float" | "float32" | "float64"
                 | "bool" | "None" ;
 alias-type       = name ;
 struct-type      = name ;
 pointer-type     = "ptr" "[" type "]" ;
 type            = base-type [ array-suffix ] ;
 base-type        = builtin-type | alias-type | struct-type | pointer-type ;
 array-suffix     = "[" integer "]" ;
 cast-type        = "int" | "int8" | "int16" | "int32" | "int64"
                 | "uint8" | "uint16" | "uint32" | "uint64"
                 | "float" | "float32" | "float64"
                 | "bool" | pointer-type ;
 integer         = digit { digit } ;
 switch-integer       = [ "-" ] integer ;
 number          = ( digit { digit } [ "." { digit } ]
                   | "." digit { digit } ) [ exponent ] ;
 exponent        = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
 boolean-literal    = "True" | "False" ;
 letter          = "A".."Z" | "a".."z" ;
 digit           = "0".."9" ;
 hex-digit       = digit | "A".."F" | "a".."f" ;
 octal-digit     = "0".."7" ;
 end-of-line             = "\r\n" | "\r" | "\n" ;
 comment = "#" { comment-character } ;
 comment-character = ? any character except "\r" and "\n" ? ;
 whitespace = " " | "\t" | "\v" | "\f" ;
 INDENT          = ? synthetic token emitted by lexer ? ;
 DEDENT          = ? synthetic token emitted by lexer ? ;
 
 BLOCK_END = ? synthetic token injected into the stream by ParseBlock immediately after it consumes DEDENT ? ;
```

Regular function signatures (used by `def`) are untouched — `...` isn't valid there.

## A New Field for Variadic Functions

The structural change is a new `bool IsVarArg` field on `FunctionSignatureNode`:

```cpp
class FunctionSignatureNode {
  ...
  bool IsVarArg;
public:
  FunctionSignatureNode(const string &Name, vector<ParameterInfo> Parameters,
                        SourceLocation Loc,
                        ValueType ReturnType = ValueType::Float64,
                        bool IsVarArg = false,
                        string ReturnStructName = "")
      : ..., IsVarArg(IsVarArg), ... {}

  bool isVarArg() const { return IsVarArg; }
};
```

`IsVarArg` defaults to `false`, so every existing call site that builds a `FunctionSignatureNode` keeps working unchanged. Only the `extern def` path ever sets it to `true`.

## Allowing Variadic Arguments in Parsing

`ParseFunctionSignature` gains an `AllowVarArgs` parameter that defaults to `false`. Inside the parameter loop, I check for `...` before I check for a parameter name:

```cpp
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature(bool AllowVarArgs = false) {
  ...
  bool IsVarArg = false;
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (AllowVarArgs && CurrentToken == tok_dot) {
        getNextToken();
        if (CurrentToken != tok_dot)
          return LogErrorSignature("Expected '...' in variadic function signature");
        getNextToken();
        if (CurrentToken != tok_dot)
          return LogErrorSignature("Expected '...' in variadic function signature");
        getNextToken();
        IsVarArg = true;
        if (CurrentToken != tok_rparen)
          return LogErrorSignature("Variadic marker must be last in parameter list");
        break;
      }
      ...
    }
  }
  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames), SignatureLoc,
                                   ValueType::Float64, IsVarArg);
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

`...` also has to be the last thing before `)` — the `break` right after setting `IsVarArg` guarantees the loop can't come back around for another parameter. And since the `...` check runs before I even look for a parameter name, `extern def f(...)` with no fixed parameters at all works the same way — it just hits that branch on the very first iteration.

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

So `...` in a regular function definition fails, because `AllowVarArgs` is `false` there and the first `.` isn't a valid parameter name:

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
if ((!Signature->isVarArg() && Signature->getNumParameters() != Arguments.size()) ||
    (Signature->isVarArg() && Arguments.size() < Signature->getNumParameters()))
  return LogErrorExpression("Incorrect # arguments passed");
```

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

Type-checking only walks the fixed parameters — `for (size_t i = 0; i < Arguments.size() && i < Signature->getNumParameters(); ++i)`. Anything past that is on me: I can pass whatever I want after the fixed parameters, and pyxc won't check it against anything, the same way C's own `printf` doesn't.

## Codegen Passes the Variadic Flag Through

`FunctionSignatureNode::codegen` passes `IsVarArg` to `FunctionType::get` instead of the hardcoded `false` it used before:

```cpp
FunctionType *FT = FunctionType::get(
    LLVMTypeFor(ReturnType, ReturnStructName), ParameterTypes, IsVarArg);
```

That's the whole change on the codegen side. LLVM already knows how to emit a variadic declaration and handle the variadic calling convention at each call site — I just have to tell it `IsVarArg` is true:

```
declare i32 @printf(ptr, ...)
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

Phase 5 is complete. pyxc now has the full K&R toolbox: signed and unsigned integer types, character literals, the complete set of operators, `if`/`elif`/`else`, `switch`, `while`, `do/while`, `break`, `continue`, assignment as expression, and direct C library interop via `extern` (including variadic functions). [Chapter 43](chapter-43.md) begins Phase 6: module declarations and imports, giving pyxc programs a way to split across multiple files.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

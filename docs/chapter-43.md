---
section: "Program Structure"
description: "Introduce module declarations and export: name your compilation unit, mark your public API, and split a pyxc project across multiple files."
---
# 43. pyxc: Module Declarations and Export

## What I Am Building

[Chapter 42](chapter-42.md) finished the object model with generic traits. pyxc can express structs, classes, methods, and now generic trait implementations. What I haven't addressed is scale: every non-trivial program lives in more than one file. pyxc can already compile multiple files, but there's no way to say which functions are public and which are internal. I add `module` and `export` to fix that:

```pyxc
module app.math

export def square(x: int) -> int:
  return x * x
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-43
```

## Grammar

I add two new top-level forms — `module` and `export` — and a `module-path` production for the dotted name `module` uses:

`code/chapter-43/pyxc.ebnf`

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
-top-level-item                    = function-definition
+top-level-item                    = module-declaration
+                                    | export-declaration
+                                    | function-definition
                                     | type-alias
                                     | trait-definition
                                     | implementation-definition
                                     | struct-definition
                                     | class-definition
                                     | external
                                     | top-level-statement ;
+module-declaration                = "module" module-path ;
+export-declaration                = "export" ( function-definition
+                                    | external
+                                    | struct-definition
+                                    | class-definition
+                                    | type-alias
+                                    | trait-definition
+                                    | implementation-definition ) ;
+module-path                       = name { "." name } ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
 trait-definition                  = "trait" name [ "[" name "]" ] ":" end-of-lines
                                     trait-block ;
 trait-block                       = indent trait-method-signature
                                     { end-of-lines trait-method-signature }
                                     dedent ;
 trait-method-signature            = "def" name "(" [ parameters ] ")"
                                     [ "->" type ] ;
 class-definition                  = "class" name
                                     [ "(" trait-reference
                                       { "," trait-reference } ")" ]
                                     ":" end-of-lines
                                     class-block ;
 trait-reference                   = name [ "[" type "]" ] ;
 implementation-definition         = "impl" trait-reference "for" name ":"
                                     end-of-lines implementation-block ;
 implementation-block              = indent method-definition
                                     { end-of-lines method-definition } dedent ;
 type-alias                        = "type" name "=" type ;
 struct-block                      = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 class-block                       = indent class-member
                                     { end-of-lines class-member } dedent ;
 class-member                      = [ visibility ]
                                     ( field-declaration | method-definition ) ;
 visibility                        = "public" | "private" ;
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

`module` has to be the first non-comment line in a file. A file can have at most one `module` declaration. Both `module` and `export` are file-mode only — the REPL doesn't have files to name or export from.

## New Tokens and Keywords

Two new tokens:

```cppdiff
*  tok_trait = -67,
*  tok_impl = -68,
+  tok_module = -69,
+  tok_export = -70,
```

Added to the keyword table and token name map:

```cppdiff
*    {"trait", tok_trait},
*    {"impl", tok_impl},
+    {"module", tok_module},   {"export", tok_export},
*    {"ptr", tok_ptr},         {"addr", tok_addr},
```

## File-Level State Globals

Three new globals track module metadata while I parse a file:

```cpp
static bool SeenNonModuleTopLevel = false;
static bool ModuleDeclaredInFile = false;
static string CurrentModuleName;
```

I reset all three at the start of each file compilation:

```cpp
SeenNonModuleTopLevel = false;
ModuleDeclaredInFile = false;
CurrentModuleName.clear();
```

## Parsing a Dotted Module Path

```cpp
static bool ParseModulePath(string &Path) {
  Path.clear();
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected module path"), false;
  Path = Name;
  getNextToken(); // eat first name
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected name after '.' in module path"),
             false;
    Path += "." + Name;
    getNextToken(); // eat name
  }
  return true;
}
```

This turns `app.math` into the string `"app.math"`.

## The `module` Declaration

```cpp
/// module-declaration = "module" module-path ;
static bool ParseModuleDeclaration() {
  getNextToken(); // eat 'module'
  if (ModuleDeclaredInFile)
    return LogErrorExpression(
               "Only one module declaration is allowed per file"),
           false;
  if (SeenNonModuleTopLevel)
    return LogErrorExpression(
               "module declaration must appear before other top-level forms"),
           false;
  if (!ParseModulePath(CurrentModuleName))
    return false;
  ModuleDeclaredInFile = true;
  return true;
}
```

I check two things before even trying to parse the path: only one `module` per file, and it has to precede everything else.

```pyxc
def a() -> int:
  return 0
module late.name
```
```
Error (Line 3, Column 8): module declaration must appear before other top-level forms
module late.
       ^~~~
```

```pyxc
module app.a
module app.b
```
```
Error (Line 2, Column 8): Only one module declaration is allowed per file
module app.
       ^~~~
```

## Tracking Whether Anything Non-Module Ran First

`FileModeLoop` sets the flag itself, right before it dispatches on the current token, for anything that isn't `module`:

```cppdiff
*static void FileModeLoop() {
*  while (true) {
*    ...
*    if (CurrentToken == tok_error) {
*      SynchronizeToLineBoundary();
*      continue;
*    }
*
+    if (CurrentToken != tok_module)
+      SeenNonModuleTopLevel = true;
*
*    switch (CurrentToken) {
*    case tok_module:
*      HandleModuleDeclaration();
*      break;
*    ...
*    }
*  }
*}
```

This one line is what lets `ParseModuleDeclaration` detect that `module` showed up too late — by the time a second top-level form starts parsing, the flag is already set.

## Handling `module` and `export` at the Top Level

`HandleModuleDeclaration` rejects REPL input, delegates to `ParseModuleDeclaration`, and then checks that nothing unexpected follows on the same line:

```cpp
static void HandleModuleDeclaration() {
  if (IsRepl) {
    LogErrorExpression("'module' is only supported in file mode");
    SynchronizeToLineBoundary();
    return;
  }
  bool Parsed = ParseModuleDeclaration();
  bool HasTrailing = CurrentToken != tok_eol && CurrentToken != tok_eof &&
                     CurrentToken != tok_block_end;
  if (!Parsed || HasTrailing) {
    if (Parsed)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
  }
}
```

`HandleExportDeclaration` eats `export` and dispatches to whichever existing handler matches the token that follows:

```cpp
static void HandleExportDeclaration() {
  if (IsRepl) {
    LogErrorExpression("'export' is only supported in file mode");
    SynchronizeToLineBoundary();
    return;
  }
  getNextToken(); // eat 'export'
  switch (CurrentToken) {
  case tok_def:
    HandleFunctionDefinition();
    return;
  case tok_extern:
    HandleExtern();
    return;
  case tok_struct:
    HandleAggregateDefinition("struct");
    return;
  case tok_class:
    HandleAggregateDefinition("class");
    return;
  case tok_type:
    HandleTypeAliasDefinition();
    return;
  case tok_trait:
    HandleTraitDefinition();
    return;
  case tok_impl:
    HandleImplementationDefinition();
    return;
  default:
    LogErrorExpression(
        "'export' must be followed by a top-level declaration");
    SynchronizeToLineBoundary();
    return;
  }
}
```

```pyxc
module app.bad
export 1 + 2
```
```
Error (Line 2, Column 8): 'export' must be followed by a top-level declaration
export 1 
       ^~~~
```

```
ready> module foo
Error (Line 1, Column 1): 'module' is only supported in file mode
module 
 ^~~~
```

In this chapter `export` only marks a declaration; it doesn't yet restrict which symbols cross file boundaries — that enforcement is Chapter 44's job, once imports actually resolve to files.

## Main Loop Dispatch

Both `MainLoop` and `FileModeLoop` gain two new cases:

```cppdiff
*static void MainLoop() {
*  while (CurrentToken != tok_eof) {
*    switch (CurrentToken) {
*    ...
*    case tok_eol:
*      // A bare newline: just print a fresh prompt and read the next token.
*      PrintReplPrompt();
*      getNextToken();
*      break;
+    case tok_module:
+      HandleModuleDeclaration();
+      break;
+    case tok_export:
+      HandleExportDeclaration();
+      break;
*    case tok_type:
*      HandleTypeAliasDefinition();
*      break;
*    ...
*    }
*  }
*}
```

## Build and Run

```bash
cd code/chapter-43
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

```pyxc
module app.math

extern def printd(x: float64)

export def square(x: int) -> int:
  return x * x

def main() -> int:
  printd(float64(square(6)))
  return 0
```

```
36.000000
```

## What's Next

[Chapter 44](chapter-44.md) adds `import`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

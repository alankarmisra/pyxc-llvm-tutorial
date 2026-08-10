---
description: "Introduce module declarations and export: name your compilation unit, mark your public API, and split a pyxc project across multiple files."
---
# 43. pyxc: Module Declarations and Export

## What I Am Building

[Chapter 42](chapter-42.md) completed Phase 5. pyxc can call any C library function and express everything in the first four chapters of *The C Programming Language*. What I haven't addressed is scale: every non-trivial program lives in more than one file. pyxc can already compile multiple files, but there's no way to say which functions are public and which are internal. I add `module` and `export` to fix that:

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

I add three new top-level forms — `module`, `import`, `export` — and a shared `module-path` production for the dotted names both `module` and `import` use:

`code/chapter-43/pyxc.ebnf`

```grammardiff
 program         = [ end-of-lines ] [ top-level-item { end-of-lines top-level-item } ] [ end-of-lines ] ;
 end-of-lines            = end-of-line { end-of-line } ;
-top-level-item             = type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | external | top-level-expression ;
+top-level-item             = module-declaration | import-declaration | export-declaration | type-alias | trait-definition | struct-definition | class-definition | implementation-definition | function-definition | external | top-level-expression ;
+module-declaration      = "module" module-path ;
+import-declaration      = "import" module-path ;
+export-declaration      = "export" ( function-definition | external | struct-definition | class-definition | type-alias | trait-definition | implementation-definition ) ;
+module-path      = name { "." name } ;
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
 external        = "extern" "def" external-function-signature [ "->" type ] ;
 top-level-expression    = expression ;
 function-signature       = name "(" [ typed-parameter { "," typed-parameter } ] ")" ;
 external-function-signature = name "(" [ typed-parameter { "," typed-parameter } [ "," "..." ] | "..." ] ")" ;
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

`module` has to be the first non-comment line in a file. A file can have at most one `module` declaration. Both `module` and `export` are file-mode only — the REPL doesn't have files to name or export from.

## New Tokens and Keywords

Three new tokens:

```cpp
tok_module = -69,
tok_import = -70,
tok_export = -71,
```

Added to the keyword table and token name map:

```cpp
{"module", tok_module}, {"import", tok_import}, {"export", tok_export}
```

## File-Level State Globals

Four new globals track module metadata while I parse a file:

```cpp
static bool SeenNonModuleTopLevel = false;  // true once any def/struct/etc. seen
static bool ModuleDeclaredInFile  = false;  // true once 'module' has been seen
static string CurrentModuleName;            // e.g. "app.math"
static vector<string> ImportedModules;      // import names seen in this file
```

I reset all four at the start of each file compilation:

```cpp
SeenNonModuleTopLevel = false;
ModuleDeclaredInFile  = false;
CurrentModuleName.clear();
ImportedModules.clear();
```

## Parsing a Dotted Module Path

`module` and `import` share one parser for the dotted path:

```cpp
static bool ParseDottedModuleName(string &OutName) {
  OutName.clear();
  if (CurrentToken != tok_name) {
    LogErrorExpression("Expected module path");
    return false;
  }
  OutName = Name;
  getNextToken(); // eat first name
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name) {
      LogErrorExpression("Expected name after '.' in module path");
      return false;
    }
    OutName += ".";
    OutName += Name;
    getNextToken(); // eat name
  }
  return true;
}
```

This turns `app.math` into the string `"app.math"`.

## The `module` Declaration

```cpp
static bool ParseModuleDefinition() {
  getNextToken(); // eat 'module'
  if (!ParseDottedModuleName(CurrentModuleName))
    return false;
  if (ModuleDeclaredInFile) {
    LogErrorExpression("Only one module declaration is allowed per file");
    return false;
  }
  if (SeenNonModuleTopLevel) {
    LogErrorExpression("module declaration must appear before other top-level forms");
    return false;
  }
  ModuleDeclaredInFile = true;
  return true;
}
```

I check two things: only one `module` per file, and it has to precede everything else.

```pyxc
def a() -> int:
  return 0
module late.name
```
```
Error (Line 3, Column 17): module declaration must appear before other top-level forms
module late.name
                ^~~~
```

```pyxc
module app.a
module app.b
```
```
Error (Line 2, Column 13): Only one module declaration is allowed per file
module app.b
            ^~~~
```

## Parsing the `import` Declaration

```cpp
static bool ParseImportDefinition() {
  getNextToken(); // eat 'import'
  string ImportName;
  if (!ParseDottedModuleName(ImportName))
    return false;
  ImportedModules.push_back(ImportName);
  return true;
}
```

This chapter only collects import names — it doesn't resolve them to files yet. That's Chapter 44's job.

## Tracking Whether Anything Non-Module Ran First

Every top-level handler sets the flag when it runs:

```cpp
static void HandleFunctionDefinition() { SeenNonModuleTopLevel = true; ... }
static void HandleExtern()             { SeenNonModuleTopLevel = true; ... }
static void HandleStructDef()          { SeenNonModuleTopLevel = true; ... }
static void HandleClassDef()           { SeenNonModuleTopLevel = true; ... }
static void HandleTypeAliasDef()       { SeenNonModuleTopLevel = true; ... }
static void HandleTraitDef()           { SeenNonModuleTopLevel = true; ... }
static void HandleImplDef()            { SeenNonModuleTopLevel = true; ... }
// ... and HandleTopLevelExpression
```

This is exactly what lets `ParseModuleDefinition` detect that `module` showed up too late — it's just checking whether any of these handlers already ran once this file.

## Handling `module`, `import`, and `export` at the Top Level

`HandleModuleDef` and `HandleImportDef` both reject REPL input, delegate to their parse function, and then check that nothing unexpected follows on the same line:

```cpp
static void HandleModuleDef() {
  if (IsRepl) {
    LogErrorExpression("'module' is only supported in file mode");
    SynchronizeToLineBoundary();
    return;
  }
  bool Ok = ParseModuleDefinition();
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

`HandleImportDef` is the same shape, just calling `ParseImportDefinition` instead. `HandleExportDef` eats `export` and dispatches to whichever existing handler matches the token that follows:

```cpp
static void HandleExportDef() {
  if (IsRepl) {
    LogErrorExpression("'export' is only supported in file mode");
    SynchronizeToLineBoundary();
    return;
  }
  getNextToken(); // eat 'export'
  switch (CurrentToken) {
  case tok_def:    HandleFunctionDefinition(); return;
  case tok_extern: HandleExtern();             return;
  case tok_struct: HandleStructDef();          return;
  case tok_class:  HandleClassDef();           return;
  case tok_type:   HandleTypeAliasDef();       return;
  case tok_trait:  HandleTraitDef();           return;
  case tok_impl:   HandleImplDef();            return;
  default:
    LogErrorExpression("'export' must be followed by a top-level declaration");
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

Both `MainLoop` and `FileModeLoop` gain three new cases:

```cpp
case tok_module: HandleModuleDef(); break;
case tok_import: HandleImportDef(); break;
case tok_export: HandleExportDef(); break;
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

[Chapter 44](chapter-44.md) implements the import resolver: the compiler finds the source file, scans its `export` declarations, and makes them available — no `extern def` needed for pyxc-to-pyxc calls.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

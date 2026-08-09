---
description: "Introduce module declarations and export: name your compilation unit, mark your public API, and split a pyxc project across multiple files."
---
# 42. pyxc: Module Declarations and Export

## Where We Are

[Chapter 41](chapter-41.md) completed Phase 5. pyxc can call any C library function and express everything in the first four chapters of *The C Programming Language*. What we haven't addressed is scale. Every non-trivial program lives in more than one file. pyxc can already compile multiple files — but there's no way to say which functions are public and which are internal. This chapter introduces `module` and `export` to fix that.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-42
```

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

Four new globals track module metadata while parsing a file:

```cpp
static bool SeenNonModuleTopLevel = false;  // true once any def/struct/etc. seen
static bool ModuleDeclaredInFile  = false;  // true once 'module' has been seen
static string CurrentModuleName;            // e.g. "app.math"
static vector<string> ImportedModules;      // import names seen in this file
```

These are reset at the start of each file compilation:

```cpp
SeenNonModuleTopLevel = false;
ModuleDeclaredInFile  = false;
CurrentModuleName.clear();
ImportedModules.clear();
```

## `ParseDottedModuleName` — Dotted Path Parser

Both `module` and `import` share a parser for the dotted module path:

```cpp
static bool ParseDottedModuleName(string &OutName) {
  OutName.clear();
  if (CurTok != tok_identifier) {
    LogErrorExpression("Expected module path");
    return false;
  }
  OutName = IdentifierStr;
  getNextToken(); // eat first identifier
  while (CurTok == '.') {
    getNextToken(); // eat '.'
    if (CurTok != tok_identifier) {
      LogErrorExpression("Expected identifier after '.' in module path");
      return false;
    }
    OutName += ".";
    OutName += IdentifierStr;
    getNextToken(); // eat identifier
  }
  return true;
}
```

This produces strings like `"app.math"` or `"geo.shapes"`.

## `ParseModuleDefinition` — The `module` Declaration

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

Two validations: only one `module` per file, and it must precede all other top-level forms.

## `ParseImportDefinition`

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

In this chapter, import names are collected but not yet resolved to files. That is Chapter 43's job.

## `SeenNonModuleTopLevel` Tracking

Every top-level handler sets the flag when it runs:

```cpp
static void HandleDefinition()    { SeenNonModuleTopLevel = true; ... }
static void HandleExtern()        { SeenNonModuleTopLevel = true; ... }
static void HandleStructDef()     { SeenNonModuleTopLevel = true; ... }
static void HandleClassDef()      { SeenNonModuleTopLevel = true; ... }
static void HandleTypeAliasDef()  { SeenNonModuleTopLevel = true; ... }
static void HandleTraitDef()      { SeenNonModuleTopLevel = true; ... }
static void HandleImplDef()       { SeenNonModuleTopLevel = true; ... }
// ... and HandleTopLevelExpression
```

This is how `ParseModuleDefinition` can detect that `module` appeared too late.

## `HandleModuleDef`, `HandleImportDef`, `HandleExportDef`

`HandleModuleDef` and `HandleImportDef` reject REPL input and then delegate to their parse functions:

```cpp
static void HandleModuleDef() {
  if (IsRepl) {
    LogErrorExpression("'module' is only supported in file mode");
    SynchronizeToLineBoundary();
    return;
  }
  if (!ParseModuleDefinition())
    SynchronizeToLineBoundary();
}

static void HandleImportDef() {
  if (IsRepl) {
    LogErrorExpression("'import' is only supported in file mode");
    SynchronizeToLineBoundary();
    return;
  }
  if (!ParseImportDefinition())
    SynchronizeToLineBoundary();
}
```

`HandleExportDef` eats `export` and then dispatches to the appropriate existing handler based on the following token:

```cpp
static void HandleExportDef() {
  if (IsRepl) {
    LogErrorExpression("'export' is only supported in file mode");
    SynchronizeToLineBoundary();
    return;
  }
  getNextToken(); // eat 'export'
  switch (CurTok) {
  case tok_def:    HandleDefinition(); return;
  case tok_extern: HandleExtern();     return;
  case tok_struct: HandleStructDef();  return;
  case tok_class:  HandleClassDef();   return;
  case tok_type:   HandleTypeAliasDef(); return;
  case tok_trait:  HandleTraitDef();   return;
  case tok_impl:   HandleImplDef();    return;
  default:
    LogErrorExpression("'export' must be followed by a top-level declaration");
    SynchronizeToLineBoundary();
    return;
  }
}
```

In this chapter `export` is a visibility marker but does not yet restrict which symbols cross file boundaries — enforcement comes in Chapter 43.

## Main Loop Dispatch

Both `MainLoop` and `FileModeLoop` gain three new cases:

```cpp
case tok_module: HandleModuleDef(); break;
case tok_import: HandleImportDef(); break;
case tok_export: HandleExportDef(); break;
```

## Grammar

```ebnf
moduledecl  = "module" modulepath ;                                -- new
importdecl  = "import" modulepath ;                                -- new
exportdecl  = "export" ( definition | external | structdef
                        | classdef | typealias | traitdef
                        | impldef ) ;                              -- new
modulepath  = identifier { "." identifier } ;                      -- new
```

`module` must be the first non-comment line. A file can have at most one `module` declaration. Both `module` and `export` are file-mode only.

## Error Cases

**`module` after a definition:**
```pyxc
def a() -> int:
  return 0
module late.name   # Error: module declaration must appear before other top-level forms
```

**Duplicate `module`:**
```pyxc
module app.a
module app.b   # Error: Only one module declaration is allowed per file
```

**`export` on a non-declaration:**
```pyxc
module app.bad
export 1 + 2   # Error: 'export' must be followed by a top-level declaration
```

**`module` or `export` in the REPL:**
```
>>> module foo
Error: 'module' is only supported in file mode
```

## What's Next

[Chapter 43](chapter-43.md) implements the import resolver: the compiler finds the source file, scans its `export` declarations, and makes them available — no `extern def` needed for pyxc-to-pyxc calls.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

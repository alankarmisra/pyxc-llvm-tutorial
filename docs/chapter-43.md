---
description: "Add import declarations: pyxc finds the source file, scans its exported signatures, and makes them available — no extern def needed for pyxc-to-pyxc calls."
---
# 43. pyxc: Imports

## Where We Are

[Chapter 42](chapter-42.md) introduced `module` and `export`. A module now has a name and a public API, but callers still have to write `extern def` by hand. After this chapter:

```pyxc
# app/math.pyxc
module app.math

export def add(x: int, y: int) -> int:
  return x + y
```

```pyxc
# main.pyxc
module app.main
import app.math

extern def printd(x: float64) -> float64

def main() -> int:
  printd(float64(add(2, 3)))   # 5.000000
  return 0
```

```bash
pyxc --emit exe -o out main.pyxc
```

No `extern def add`. The compiler finds `app/math.pyxc`, reads its `export` declarations, and injects the prototype. In `--emit exe` mode, it compiles `app/math.pyxc` automatically too.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-43
```

## `SignatureScanMode` — Parsing Without Codegen

A new global flag suppresses codegen during the import scan:

```cpp
static bool SignatureScanMode = false;
static std::set<string> SignatureVisitedFiles; // deduplication
```

When `SignatureScanMode` is true, `ParseAggregateDefinition` skips method body codegen and calls a leaner helper instead:

```cpp
if (SignatureScanMode) {
  if (!ParseMethodSignatureOnlyInClass(StructName, MemberIsPublic))
    return false;
} else {
  auto FnAST = ParseMethodDefinitionInClass(StructName, MemberIsPublic);
  // ... codegen ...
}
```

## `SkipSignatureBody` — Discarding Function Bodies

While scanning signatures, function bodies need to be consumed and thrown away:

```cpp
static void SkipSignatureBody() {
  if (CurTok == tok_eol) {
    consumeNewlines();
    if (CurTok == tok_indent) {
      int Depth = 1;
      getNextToken(); // eat first indent
      while (CurTok != tok_eof && Depth > 0) {
        if (CurTok == tok_indent)  ++Depth;
        else if (CurTok == tok_dedent) --Depth;
        getNextToken();
      }
      return;
    }
    return;
  }
  // single-line body
  while (CurTok != tok_eof && CurTok != tok_eol)
    getNextToken();
  if (CurTok == tok_eol)
    getNextToken();
}
```

This counts INDENT/DEDENT pairs to skip multi-line bodies correctly.

## `ParseDefinitionSignatureOnly`

Parses a `def` signature, registers the prototype in `FunctionProtos`, then discards the body:

```cpp
static bool ParseDefinitionSignatureOnly() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return false;
  string RetStructName;
  ValueType RetType =
      ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);
  if (RetType == ValueType::Error)
    return false;
  Proto->setReturnType(RetType);
  Proto->setReturnStructName(RetStructName);
  FunctionProtos[Proto->getName()] = Proto->clone();
  if (CurTok != ':')
    return LogErrorExpression("Expected ':' in definition"), false;
  getNextToken(); // eat ':'
  SkipSignatureBody();
  return true;
}
```

## `ParseMethodSignatureOnlyInClass`

Registers a method prototype — including the implicit `self` injection — without generating any IR:

```cpp
static bool ParseMethodSignatureOnlyInClass(const string &ClassName,
                                            bool IsPublic) {
  getNextToken(); // eat 'def'
  // ... parse method name and parameter list (same as ParseMethodDefinitionInClass) ...
  // inject implicit self:
  ArgNames.push_back({"self", ValueType::Pointer,
                      EncodePointerType(ValueType::Struct, ClassName)});
  // ... parse remaining params and return type ...
  string MangledName = ClassName + "." + MethodName;
  auto Proto = make_unique<PrototypeAST>(MangledName, std::move(ArgNames), ProtoLoc, RetType);
  FunctionProtos[Proto->getName()] = Proto->clone();
  // update visibility map:
  StructTypes[ClassName].MethodIsPublic[MethodName] = IsPublic;
  getNextToken(); // eat ':'
  SkipSignatureBody();
  return true;
}
```

## `ParseExportSignatureOnly`

Dispatches on the token after `export` to run the right signature-only parser:

```cpp
static bool ParseExportSignatureOnly() {
  getNextToken(); // eat 'export'
  if (CurTok == tok_def)    return ParseDefinitionSignatureOnly();
  if (CurTok == tok_extern) {
    auto Proto = ParseExtern();
    if (!Proto) return false;
    FunctionProtos[Proto->getName()] = std::move(Proto);
    return true;
  }
  if (CurTok == tok_struct) return ParseAggregateDefinition("struct");
  if (CurTok == tok_class)  return ParseAggregateDefinition("class");
  if (CurTok == tok_trait)  return ParseTraitDefinition();
  if (CurTok == tok_type)   return ParseTypeAliasDefinition();
  return LogErrorExpression("Invalid export target"), false;
}
```

Struct, class, trait, and type alias parsers already register into `StructTypes`, `Traits`, and `TypeAliases`. They work unchanged in `SignatureScanMode` because their "bodies" are field/trait-method declarations, not IR-generating code.

## `ResolveImportToPath`

Converts `app.math` to an absolute file path by replacing dots with slashes and probing relative to the importer's location:

```cpp
static bool ResolveImportToPath(const string &ImporterPath,
                                const string &Import, string &OutPath) {
  string Rel = Import;
  std::replace(Rel.begin(), Rel.end(), '.', '/');
  SmallString<256> Candidate(ImporterPath);
  path::remove_filename(Candidate);
  path::append(Candidate, Rel + ".pyxc");
  if (fs::exists(Candidate)) {
    OutPath = std::string(Candidate.str());
    return true;
  }
  // Test-friendly fallback: Inputs/ subdirectory
  SmallString<256> InputsCandidate(ImporterPath);
  path::remove_filename(InputsCandidate);
  path::append(InputsCandidate, "Inputs");
  path::append(InputsCandidate, Rel + ".pyxc");
  if (fs::exists(InputsCandidate)) {
    OutPath = std::string(InputsCandidate.str());
    return true;
  }
  return false;
}
```

If neither probe succeeds, the import is unresolved and the compiler errors.

## `CanonicalizePath`

Converts a path to its canonical form for deduplication:

```cpp
static string CanonicalizePath(const string &Path) {
  SmallString<256> Canon(Path);
  if (!llvm::sys::fs::real_path(Path, Canon))
    return std::string(Canon.str());
  // fallback: make absolute and remove dots
  SmallString<256> Abs(Path);
  llvm::sys::fs::make_absolute(Abs);
  llvm::sys::path::remove_dots(Abs, true);
  return std::string(Abs.str());
}
```

## `CollectSignaturesFromFile`

The core of the import system. Opens the file, switches to `SignatureScanMode`, scans for `export` declarations, and saves/restores all global parser state:

```cpp
static bool CollectSignaturesFromFile(const string &Path) {
  const string CanonPath = CanonicalizePath(Path);
  if (!SignatureVisitedFiles.insert(CanonPath).second)
    return true; // already visited

  FILE *SavedInput = Input;
  bool SavedIsRepl = IsRepl;
  string SavedSourcePath = CurrentSourcePath;
  int SavedCurTok = CurTok;
  bool SavedHadError = HadError;
  bool OK = true;

  if (!OpenInputFile(Path))
    return false;
  ResetLexerState();
  IsRepl = false;
  SignatureScanMode = true;
  HadError = false;
  getNextToken();

  while (CurTok != tok_eof) {
    if (CurTok == tok_eol || CurTok == tok_indent || CurTok == tok_dedent) {
      getNextToken(); continue;
    }
    if (CurTok == tok_import) {
      // Recurse eagerly into nested imports
      getNextToken(); // eat 'import'
      string ImportName;
      if (!ParseDottedModuleName(ImportName)) { OK = false; break; }
      string ImportPath;
      if (!ResolveImportToPath(CanonPath, ImportName, ImportPath)) {
        LogErrorExpression(("Could not resolve import '" + ImportName + "'...").c_str());
        OK = false; break;
      }
      if (!CollectSignaturesFromFile(ImportPath)) { OK = false; break; }
      continue;
    }
    if (CurTok == tok_export) {
      if (!ParseExportSignatureOnly()) { OK = false; break; }
      continue;
    }
    SkipSignatureBody(); // skip non-exported forms
  }

  if (HadError) OK = false;

  CloseInputFile();
  Input = SavedInput;
  IsRepl = SavedIsRepl;
  CurrentSourcePath = SavedSourcePath;
  CurTok = SavedCurTok;
  SignatureScanMode = false;
  HadError = SavedHadError;
  ResetLexerState();
  return OK;
}
```

State save/restore lets the scanner work recursively — scanning B while in the middle of scanning A — without corrupting the outer parse.

## `ExtractTopLevelImports` — Text-Based Import Discovery

Before the lexer runs on the main file, a fast line-based scanner extracts its `import` names using `std::ifstream`. This avoids invoking the full lexer on the entry file twice:

```cpp
static vector<string> ExtractTopLevelImports(const string &Path) {
  vector<string> Result;
  std::ifstream In(Path);
  string Line;
  while (std::getline(In, Line)) {
    // skip blank lines and comments
    auto first = Line.find_first_not_of(" \t");
    if (first == string::npos || Line[first] == '#') continue;
    string Trim = Line.substr(first);
    if (Trim.rfind("module ", 0) == 0) continue;
    if (Trim.rfind("import ", 0) == 0) {
      string Name = Trim.substr(7);
      // strip inline comments
      auto hash = Name.find('#');
      if (hash != string::npos) Name = Name.substr(0, hash);
      while (!Name.empty() && isspace((unsigned char)Name.back())) Name.pop_back();
      if (!Name.empty()) Result.push_back(Name);
      continue;
    }
    if (Trim.rfind("export ", 0) == 0) continue;
    break; // first non-import top-level form — stop
  }
  return Result;
}
```

It stops at the first line that is neither `module`, `import`, nor `export` — so function bodies are never read.

## `PreloadImportedSignatures`

Called before the main parse loop of any file. Clears the visited set, then runs `CollectSignaturesFromFile` for each import:

```cpp
static bool PreloadImportedSignatures(const string &Path) {
  SignatureVisitedFiles.clear();
  for (const auto &ImportName : ExtractTopLevelImports(Path)) {
    string ImportPath;
    if (!ResolveImportToPath(Path, ImportName, ImportPath)) {
      LogErrorExpression(...);
      return false;
    }
    if (!CollectSignaturesFromFile(ImportPath))
      return false;
  }
  return true;
}
```

## `CollectImportClosure` — Auto-Expanding `--emit exe`

For `--emit exe`, every transitively imported `.pyxc` file must be compiled and linked. `CollectImportClosure` does a DFS of the import graph and returns the full file list:

```cpp
static bool CollectImportClosure(const string &Path, std::set<string> &Visited,
                                 vector<string> &OutFiles) {
  const string CanonPath = CanonicalizePath(Path);
  if (!Visited.insert(CanonPath).second)
    return true; // already in closure
  OutFiles.push_back(CanonPath);
  for (const auto &ImportName : ExtractTopLevelImports(CanonPath)) {
    string ImportPath;
    if (!ResolveImportToPath(CanonPath, ImportName, ImportPath)) {
      LogErrorExpression(...);
      return false;
    }
    if (!CollectImportClosure(ImportPath, Visited, OutFiles))
      return false;
  }
  return true;
}
```

The `--emit exe` driver replaces its explicit input list with the `ExpandedInputs` produced by this function:

```cpp
// Before: compile each file listed on the command line
// After: expand the import closure, then compile each file in the closure
vector<string> ExpandedInputs;
std::set<string> SeenPyxcInputs;
for each InputPath:
  if IsPyxcInput(InputPath):
    CollectImportClosure(InputPath, SeenPyxcInputs, ExpandedInputs);
  else:
    ExpandedInputs.push_back(InputPath);
// then compile everything in ExpandedInputs
```

## Grammar

```ebnf
importdecl = "import" modulepath ;   -- new
modulepath = identifier { "." identifier } ;
```

`import` is file-mode only. Use it after `module` and before the first function definition.

## Error Cases

**Import not found:**
```pyxc
import does.not.exist   # Error: Could not resolve import 'does.not.exist' from '...'
```

**Calling a non-exported function:**
```pyxc
import app.math
validate(5)   # Error: Unknown function referenced (not exported)
```

**Wrong argument type from imported function:**
```pyxc
import app.math
add(1.0, 2)   # Error: argument 1 expects int
```

## Things Worth Knowing

**`--emit llvm-ir` does not auto-include dependencies.** The closure expansion is specific to `--emit exe`. IR output is one-file-in, one-file-out.

**`import` in the REPL is not supported.** The import system is file-mode only.

**Circular imports work.** If A imports B and B imports A, the `SignatureVisitedFiles` set stops the recursion. Chapter 44 makes this more robust with a two-phase algorithm that handles the case where B's exports are needed by A before B finishes scanning.

## What's Next

[Chapter 44](chapter-44.md) explains how the compiler handles cyclic imports without infinite recursion.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

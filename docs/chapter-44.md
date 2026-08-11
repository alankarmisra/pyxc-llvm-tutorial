---
description: "Add import declarations: pyxc finds the source file, scans its exported signatures, and makes them available — no extern def needed for pyxc-to-pyxc calls."
---
# 44. pyxc: Imports

## What I Am Building

[Chapter 43](chapter-43.md) introduced `module` and `export`. A module now has a name and a public API, but callers still have to write `extern def` by hand. After this chapter:

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
  printd(float64(add(2, 3)))
  return 0
```

```bash
pyxc --emit exe -o out main.pyxc
```
```
5.000000
```

No `extern def add`. I find `app/math.pyxc`, read its `export` declarations, and inject the prototype. In `--emit exe` mode, I compile `app/math.pyxc` automatically too.

There's no grammar change this chapter — `import` syntax already exists from Chapter 43. This chapter is entirely about what happens once I see one.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-44
```

## Parsing Without Codegen

I add a global flag that suppresses codegen during an import scan:

```cpp
static bool SignatureScanMode = false;
static std::set<string> SignatureVisitedFiles; // deduplication
```

When `SignatureScanMode` is true, `ParseAggregateDefinition` skips method-body codegen and calls a leaner helper instead:

```cpp
if (SignatureScanMode) {
  if (!ParseMethodSignatureOnlyInClass(StructName, MemberIsPublic))
    return false;
} else {
  auto FnAST = ParseMethodDefinitionInClass(StructName, MemberIsPublic);
  // ... codegen ...
}
```

## Discarding Function Bodies

While scanning signatures, I still have to consume each function body — I just throw it away instead of parsing it into an AST:

```cpp
static void SkipSignatureBody() {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken == tok_indent) {
      int Depth = 1;
      getNextToken(); // eat first indent
      while (CurrentToken != tok_eof && Depth > 0) {
        if (CurrentToken == tok_indent)
          ++Depth;
        else if (CurrentToken == tok_dedent)
          --Depth;
        getNextToken();
      }
      return;
    }
    return;
  }
  while (CurrentToken != tok_eof && CurrentToken != tok_eol)
    getNextToken();
  if (CurrentToken == tok_eol)
    getNextToken();
}
```

I count `INDENT`/`DEDENT` pairs rather than just scanning for the next `DEDENT`, so a nested `if` inside the body doesn't make me stop early.

## Parsing a Function Signature Only

I parse a `def` signature, register it in `FunctionSignatures`, then discard the body:

```cpp
static bool ParseDefinitionSignatureOnly() {
  getNextToken(); // eat def
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return false;
  string RetStructName;
  ValueType RetType =
      ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);
  if (RetType == ValueType::Error)
    return false;
  Signature->setReturnType(RetType);
  Signature->setReturnStructName(RetStructName);
  FunctionSignatures[Signature->getName()] = Signature->clone();
  if (CurrentToken != tok_colon) {
    LogErrorExpression("Expected ':' in definition");
    return false;
  }
  getNextToken(); // eat ':'
  SkipSignatureBody();
  return true;
}
```

## Parsing a Method Signature Only

I register a method prototype — implicit `self` included — without generating any IR:

```cpp
static bool ParseMethodSignatureOnlyInClass(const string &ClassName,
                                            bool IsPublic) {
  getNextToken(); // eat 'def'
  // ... parse method name and parameter list (same shape as ParseMethodDefinitionInClass) ...
  ParameterNames.push_back({"self", ValueType::Pointer,
                      EncodePointerType(ValueType::Struct, ClassName)});
  // ... parse remaining parameters and return type ...
  string MangledName = ClassName + "." + MethodName;
  auto Signature = make_unique<FunctionSignatureNode>(MangledName, std::move(ParameterNames),
                                         SignatureLoc, RetType);
  FunctionSignatures[Signature->getName()] = Signature->clone();
  getNextToken(); // eat ':'
  SkipSignatureBody();

  auto It = StructTypes.find(ClassName);
  if (It != StructTypes.end())
    It->second.MethodIsPublic[MethodName] = IsPublic;
  return true;
}
```

## Parsing an Exported Signature

I dispatch on the token after `export` to run the right signature-only parser:

```cpp
static bool ParseExportSignatureOnly() {
  getNextToken(); // eat export
  if (CurrentToken == tok_def)
    return ParseDefinitionSignatureOnly();
  if (CurrentToken == tok_extern) {
    auto Signature = ParseExtern();
    if (!Signature)
      return false;
    FunctionSignatures[Signature->getName()] = std::move(Signature);
    return true;
  }
  if (CurrentToken == tok_struct)
    return ParseAggregateDefinition("struct");
  if (CurrentToken == tok_class)
    return ParseAggregateDefinition("class");
  if (CurrentToken == tok_trait)
    return ParseTraitDefinition();
  if (CurrentToken == tok_type)
    return ParseTypeAliasDefinition();
  LogErrorExpression("Invalid export target");
  return false;
}
```

Struct, class, trait, and type-alias parsers already register into `StructTypes`, `Traits`, and `TypeAliases` on their own — I don't need to change them for `SignatureScanMode`, since their "bodies" are field and trait-method declarations, not IR-generating code, so there's nothing to skip.

## Resolving an Import to a File Path

I turn `app.math` into an absolute file path by replacing dots with slashes and probing relative to the importer's own location:

```cpp
static bool ResolveImportToPath(const string &ImporterPath,
                                const string &Import, string &OutPath) {
  SmallString<256> Candidate(ImporterPath);
  path::remove_filename(Candidate);
  string Rel = Import;
  std::replace(Rel.begin(), Rel.end(), '.', '/');
  path::append(Candidate, Rel + ".pyxc");
  if (fs::exists(Candidate)) {
    OutPath = std::string(Candidate.str());
    return true;
  }

  // Test-friendly fallback: allow colocated "Inputs/" modules.
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

If neither probe succeeds, the import is unresolved:

```pyxc
import does.not.exist
```
```
Error (Line 1, Column 0): Could not resolve import 'does.not.exist' from '...'
```

## Canonicalizing Paths for Deduplication

`app.math` and `./app/math.pyxc` and `../foo/app/math.pyxc` can all name the same file. I canonicalize before deduplicating, so the same file scanned two different ways still counts as one visit:

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

## Collecting Signatures From a File

This is the core of the import system. I open the file, switch into `SignatureScanMode`, scan for `export` declarations, and save and restore all the global parser state I touch along the way:

```cpp
static bool CollectSignaturesFromFile(const string &Path) {
  const string CanonPath = CanonicalizePath(Path);
  if (!SignatureVisitedFiles.insert(CanonPath).second)
    return true; // already visited

  FILE *SavedInput = Input;
  bool SavedIsRepl = IsRepl;
  string SavedSourcePath = CurrentSourcePath;
  int SavedCurTok = CurrentToken;
  bool SavedHadError = HadError;
  bool OK = true;

  if (!OpenInputFile(Path))
    return false;
  ResetLexerState();
  ResetParserStateForFile();
  IsRepl = false;
  SignatureScanMode = true;
  HadError = false;
  getNextToken();

  while (CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol || CurrentToken == tok_indent || CurrentToken == tok_dedent) {
      getNextToken(); continue;
    }
    if (CurrentToken == tok_import) {
      // Resolve nested imports eagerly so their symbols are available.
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
    if (CurrentToken == tok_export) {
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
  CurrentToken = SavedCurTok;
  SignatureScanMode = false;
  HadError = SavedHadError;
  ResetLexerState();
  return OK;
}
```

I recurse straight into `import` lines as I hit them, so by the time I finish scanning a file, every file it (transitively) imports has already been scanned too. Saving and restoring the global parser state is what lets that recursion be safe — scanning B in the middle of scanning A can't corrupt A's own parse position once A resumes.

Calling something that never got registered this way — because it isn't `export`ed — fails the same way calling an undeclared name always has:

```pyxc
import app.math2

def main() -> int:
  return validate(5)
```
```
Error (Line 4, Column 21): Unknown function referenced
  return validate(5)
                    ^~~~
```

And an imported function is still type-checked against its real signature, same as any other call:

```pyxc
import app.math

def main() -> int:
  return add(1.0, 2)
```
```
Error (Line 4, Column 21): argument 1 expects int
  return add(1.0, 2)
                    ^~~~
```

## Text-Based Import Discovery

Before the lexer even runs on the main file, I use a fast line-based scan to pull out its `import` names with `std::ifstream`. This means I don't have to run the full lexer over the entry file twice:

```cpp
static vector<string> ExtractTopLevelImports(const string &Path) {
  vector<string> Result;
  std::ifstream In(Path);
  if (!In)
    return Result;
  string Line;
  while (std::getline(In, Line)) {
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

I stop at the first line that's neither `module`, `import`, nor `export`, so a function body never gets read here.

## Preloading Imported Signatures

I call this before the main parse loop of any file. It clears the visited set, then runs `CollectSignaturesFromFile` for each import:

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

`import` only works in file mode — the REPL has no file to resolve paths against:

```
ready> import app.math
Error (Line 1, Column 1): 'import' is only supported in file mode
import 
 ^~~~
```

## Auto-Expanding `--emit exe`

For `--emit exe`, every transitively imported `.pyxc` file has to actually get compiled and linked, not just have its signatures scanned. `CollectImportClosure` does a DFS of the import graph and collects the full file list:

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

I use it to replace the driver's explicit input list with the expanded closure:

```cpp
vector<string> ExpandedInputs;
std::set<string> SeenPyxcInputs;
for (const auto &InputPath : InputFiles) {
  if (IsPyxcInput(InputPath)) {
    if (!CollectImportClosure(InputPath, SeenPyxcInputs, ExpandedInputs)) {
      CleanupTemps();
      return false;
    }
  } else {
    ExpandedInputs.push_back(InputPath);
  }
}
// ... compile everything in ExpandedInputs
```

`--emit llvm-ir` doesn't get any of this — closure expansion is specific to `--emit exe`. IR output stays one-file-in, one-file-out.

## Try It

```bash
mkdir -p app
cat > app/math.pyxc <<'PYXC'
module app.math

export def add(x: int, y: int) -> int:
  return x + y
PYXC
cat > main.pyxc <<'PYXC'
module app.main
import app.math

extern def printd(x: float64) -> float64

def main() -> int:
  printd(float64(add(2, 3)))
  return 0
PYXC
pyxc --emit exe -o out main.pyxc
./out
```
```
5.000000
```

## What's Next

[Chapter 45](chapter-45.md) handles cyclic imports correctly.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

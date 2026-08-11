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

I add a global flag that suppresses codegen during an import scan, plus a map that tracks which files have been scanned:

```cpp
static bool SignatureScanMode = false;
enum class SignatureScanState { InProgress, Done };
static map<string, SignatureScanState> SignatureFileStates;
```

A file with no entry in `SignatureFileStates` hasn't been scanned yet. `InProgress` means I'm partway through scanning it (used for cycle detection, [Chapter 45](chapter-45.md)); `Done` means its exports are fully registered.

When `SignatureScanMode` is true, `ParseAggregateDefinition` skips method-body codegen and calls a leaner helper instead:

```cpp
if (SignatureScanMode) {
  if (!ParseMethodSignatureOnly(AggregateName, IsPublic))
    return false;
  Info.Methods = StructTypes[AggregateName].Methods;
  StructTypes[AggregateName] = Info;
  if (CurrentToken == tok_eol)
    consumeNewlines();
  else if (CurrentToken == tok_block_end)
    getNextToken();
  continue;
}
auto Method = ParseMethodDefinition(AggregateName, IsPublic);
```

## Discarding Function Bodies

While scanning signatures, I still have to consume each function body — I just throw it away instead of parsing it into an AST:

```cpp
static void SkipExportedDefinitionBody() {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken == tok_indent) {
      int Depth = 1;
      getNextToken();
      while (CurrentToken != tok_eof && Depth > 0) {
        if (CurrentToken == tok_indent)
          ++Depth;
        else if (CurrentToken == tok_dedent)
          --Depth;
        getNextToken();
      }
    }
    return;
  }
  while (CurrentToken != tok_eof && CurrentToken != tok_eol)
    getNextToken();
}
```

I count `INDENT`/`DEDENT` pairs rather than just scanning for the next `DEDENT`, so a nested `if` inside the body doesn't make me stop early.

## Parsing a Function Signature Only

I parse a `def` signature, register it in `FunctionSignatures`, then discard the body:

```cpp
static bool ParseExportedFunctionSignature() {
  getNextToken(); // eat 'def'
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return false;
  string ReturnTypeInfo;
  ValueType ReturnType =
      ParseOptionalReturnType(&ReturnTypeInfo, ValueType::None);
  if (ReturnType == ValueType::Error)
    return false;
  Signature->setReturnType(ReturnType);
  Signature->setReturnStructName(ReturnTypeInfo);
  FunctionSignatures[Signature->getName()] = Signature->clone();
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' in function definition"), false;
  getNextToken(); // eat ':'
  SkipExportedDefinitionBody();
  return true;
}
```

## Parsing a Method Signature Only

I register a method prototype — implicit `self` included — without generating any IR:

```cpp
static bool ParseMethodSignatureOnly(const string &ClassName, bool IsPublic) {
  getNextToken(); // eat 'def'
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected method name in class definition"), false;
  string MethodName = Name;
  SourceLocation SignatureLocation = CurLoc;
  getNextToken(); // eat method name
  if (CurrentToken != tok_lparen)
    return LogErrorExpression("Expected '(' in method function signature"), false;
  getNextToken(); // eat '('

  vector<pair<string, ValueType>> Parameters = {{"self", ValueType::Pointer}};
  vector<string> ParameterTypeInfo = {
      EncodePointerType(ValueType::Struct, ClassName)};
  // ... parse remaining parameters (same shape as ParseMethodDefinition) ...
  getNextToken(); // eat ')'

  string ReturnTypeInfo;
  ValueType ReturnType =
      ParseOptionalReturnType(&ReturnTypeInfo, ValueType::None);
  if (ReturnType == ValueType::Error)
    return false;
  string MangledName = ClassName + "." + MethodName;
  auto Signature = make_unique<FunctionSignatureNode>(
      MangledName, std::move(Parameters), SignatureLocation, ReturnType,
      std::move(ParameterTypeInfo), ReturnTypeInfo);
  FunctionSignatures[MangledName] = std::move(Signature);
  StructTypes[ClassName].Methods[MethodName] = IsPublic;
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' in function definition"), false;
  getNextToken();
  SkipExportedDefinitionBody();
  return true;
}
```

## Parsing an Exported Signature

I dispatch on the token after `export` to run the right signature-only parser:

```cpp
static bool ParseExportedDeclarationSignature() {
  getNextToken(); // eat 'export'
  if (CurrentToken == tok_def)
    return ParseExportedFunctionSignature();
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
  return LogErrorExpression("Invalid export target"), false;
}
```

Struct, class, trait, and type-alias parsers already register into `StructTypes`, `Traits`, and `TypeAliases` on their own — I don't need to change them for `SignatureScanMode`, since their "bodies" are field and trait-method declarations, not IR-generating code, so there's nothing to skip.

## Resolving an Import to a File Path

I turn `app.math` into a relative file path by replacing dots with slashes, then probe for it starting in the importer's own directory and walking up toward the filesystem root — at each level I also check an `Inputs/` subdirectory, which lets multi-file tests keep their helper modules out of the top-level test directory:

```cpp
static bool ResolveImportToPath(const string &ImporterPath,
                                const string &ImportName,
                                string &ResolvedPath) {
  string RelativePath = ImportName;
  replace(RelativePath.begin(), RelativePath.end(), '.', '/');
  RelativePath += ".pyxc";

  SmallString<256> Directory(ImporterPath);
  sys::path::remove_filename(Directory);
  while (!Directory.empty()) {
    SmallString<256> Candidate(Directory);
    sys::path::append(Candidate, RelativePath);
    if (sys::fs::exists(Candidate)) {
      ResolvedPath = Candidate.str().str();
      return true;
    }

    SmallString<256> InputsCandidate(Directory);
    sys::path::append(InputsCandidate, "Inputs", RelativePath);
    if (sys::fs::exists(InputsCandidate)) {
      ResolvedPath = InputsCandidate.str().str();
      return true;
    }

    SmallString<256> Parent(Directory);
    sys::path::remove_filename(Parent);
    if (Parent == Directory || Parent.empty())
      break;
    Directory = Parent;
  }
  return false;
}
```

If neither probe succeeds at any level, the import is unresolved:

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
  SmallString<256> Canonical(Path);
  if (!sys::fs::real_path(Path, Canonical))
    return Canonical.str().str();
  SmallString<256> Absolute(Path);
  sys::fs::make_absolute(Absolute);
  sys::path::remove_dots(Absolute, true);
  return Absolute.str().str();
}
```

## Collecting Signatures From a File

This is the core of the import system. I open the file, switch into `SignatureScanMode`, scan for `export` declarations, and save and restore all the global parser state I touch along the way. I check `SignatureFileStates` first: a `Done` file is reused as-is, and — this chapter — a file that's still `InProgress` (meaning I'm already in the middle of scanning it further up the call stack) is a cycle, which I reject outright:

```cpp
static bool CollectSignaturesFromFile(const string &Path) {
  string CanonicalPath = CanonicalizePath(Path);
  auto ExistingState = SignatureFileStates.find(CanonicalPath);
  if (ExistingState != SignatureFileStates.end()) {
    if (ExistingState->second == SignatureScanState::Done)
      return true;
    return LogErrorExpression("Cyclic imports are not supported"), false;
  }
  SignatureFileStates[CanonicalPath] = SignatureScanState::InProgress;

  FILE *SavedInput = Input;
  bool SavedIsRepl = IsRepl;
  bool SavedSignatureScanMode = SignatureScanMode;
  string SavedSourcePath = CurrentSourcePath;
  int SavedCurrentToken = CurrentToken;
  bool SavedHadError = HadError;
  bool Parsed = true;

  if (!OpenInputFile(Path)) {
    SignatureFileStates.erase(CanonicalPath);
    Input = SavedInput;
    return false;
  }
  ResetLexerState();
  IsRepl = false;
  SignatureScanMode = true;
  HadError = false;
  getNextToken();

  while (CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol || CurrentToken == tok_indent ||
        CurrentToken == tok_dedent || CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }
    if (CurrentToken == tok_import) {
      getNextToken(); // eat 'import'
      string ImportName;
      if (!ParseModulePath(ImportName)) {
        Parsed = false;
        break;
      }
      string ImportPath;
      if (!ResolveImportToPath(CanonicalPath, ImportName, ImportPath)) {
        LogErrorExpression(
            ("Could not resolve import '" + ImportName + "'").c_str());
        Parsed = false;
        break;
      }
      if (!CollectSignaturesFromFile(ImportPath)) {
        Parsed = false;
        break;
      }
      continue;
    }
    if (CurrentToken == tok_export) {
      if (!ParseExportedDeclarationSignature()) {
        Parsed = false;
        break;
      }
      continue;
    }
    SkipExportedDefinitionBody();
  }

  if (HadError)
    Parsed = false;
  CloseInputFile();
  Input = SavedInput;
  IsRepl = SavedIsRepl;
  CurrentSourcePath = SavedSourcePath;
  CurrentToken = SavedCurrentToken;
  SignatureScanMode = SavedSignatureScanMode;
  HadError = SavedHadError;
  ResetLexerState();

  if (!Parsed) {
    SignatureFileStates.erase(CanonicalPath);
    return false;
  }
  SignatureFileStates[CanonicalPath] = SignatureScanState::Done;
  return true;
}
```

I recurse straight into `import` lines as I hit them, so by the time I finish scanning a file, every file it (transitively) imports has already been scanned too. Saving and restoring the global parser state is what lets that recursion be safe — scanning B in the middle of scanning A can't corrupt A's own parse position once A resumes. This chapter's eager recursion is exactly what makes a cycle fatal: if A imports B and B imports A, A is still `InProgress` when B tries to scan it, so the compile fails outright. [Chapter 45](chapter-45.md) fixes that.

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
  vector<string> Imports;
  ifstream Source(Path);
  string Line;
  while (getline(Source, Line)) {
    size_t First = Line.find_first_not_of(" \t");
    if (First == string::npos || Line[First] == '#')
      continue;
    string Trimmed = Line.substr(First);
    if (Trimmed.rfind("module ", 0) == 0 ||
        Trimmed.rfind("export ", 0) == 0)
      continue;
    if (Trimmed.rfind("import ", 0) != 0)
      break;
    string ImportName = Trimmed.substr(7);
    size_t Comment = ImportName.find('#');
    if (Comment != string::npos)
      ImportName.erase(Comment);
    while (!ImportName.empty() &&
           isspace(static_cast<unsigned char>(ImportName.back())))
      ImportName.pop_back();
    if (!ImportName.empty())
      Imports.push_back(std::move(ImportName));
  }
  return Imports;
}
```

I stop at the first line that isn't blank, a comment, or one of `module`/`import`/`export`, so a function body never gets read here.

## Preloading Imported Signatures

I call this before the main parse loop of any file. It clears `SignatureFileStates`, then runs `CollectSignaturesFromFile` for each import:

```cpp
static bool PreloadImportedSignatures(const string &Path) {
  SignatureFileStates.clear();
  for (const string &ImportName : ExtractTopLevelImports(Path)) {
    string ImportPath;
    if (!ResolveImportToPath(Path, ImportName, ImportPath))
      return LogErrorExpression(
                 ("Could not resolve import '" + ImportName + "'").c_str()),
             false;
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
static bool CollectImportClosure(const string &Path, set<string> &Visited,
                                 vector<string> &Files) {
  string CanonicalPath = CanonicalizePath(Path);
  if (!Visited.insert(CanonicalPath).second)
    return true;
  Files.push_back(CanonicalPath);
  for (const string &ImportName : ExtractTopLevelImports(CanonicalPath)) {
    string ImportPath;
    if (!ResolveImportToPath(CanonicalPath, ImportName, ImportPath))
      return LogErrorExpression(
                 ("Could not resolve import '" + ImportName + "'").c_str()),
             false;
    if (!CollectImportClosure(ImportPath, Visited, Files))
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

## Build and Run

```bash
cd code/chapter-44
cmake -S . -B build && cmake --build build
./build/pyxc
```

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

---
description: "Add --emit exe so pyxc compiles and links a standalone executable in one command, using LLD as a library with no external tools."
---
# 17. pyxc: One-Step Executables

## What I Am Building

[Chapter 16](chapter-16.md) added `--emit obj`, `--emit asm`, and `--emit llvm-ir`. Producing a runnable binary from a pyxc program still needed an external tool:

```bash
pyxc --emit obj -o program.o program.pyxc
clang program.o runtime.c -o program   # ← still needed clang
./program
```

I want to remove that second step this chapter. After it:

```bash
pyxc --emit exe -o program program.pyxc
./program
```

No `clang`, no `runtime.c`, no separate link invocation. One command.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-17
```

## Grammar

No grammar changes. The language is unchanged — this is purely a compiler-driver extension.

## The Design

The key insight I'm leaning on: LLVM ships LLD — a full production linker — as a C++ library. Instead of shelling out to `clang` or `ld`, I can call `lld::macho::link` (or `lld::elf::link` on Linux) directly in-process. The pipeline becomes:

```
.pyxc → compile → temp .o
.o inputs → pass through
synthesize runtime .o (printd, putchard)
─────────────────────────────────────────
LLD links all .o files into executable
```

The runtime functions `printd` and `putchard` are generated as LLVM IR and emitted to a temporary `.o` — no `runtime.c` or external compiler needed.

## What Changes

I'm adding six new pieces on top of chapter 16:

1. **`cl::list<string> InputFiles`** — the positional argument changes from a single string to a list, enabling multiple inputs.
2. **`EmitRuntimeObject`** — synthesizes `printd` and `putchard` as LLVM IR, emits them to a temporary `.o`.
3. **`CompileFileToObject`** — per-file compilation: open → lex → parse → codegen → `.o`.
4. **`PrepareFileModeModule`** — refactored out of `EmitFileMode`: the shared logic that builds `__pyxc.global_init`, registers it in `llvm.global_ctors`, and wraps `main()`.
5. **`LinkExecutable`** + **`FindMacOSSDKRoot`** — LLD-as-library dispatch with platform-aware system library detection.
6. **`EmitExecutable`** — the new orchestrator that wires all of the above together.

One more change ties these together but isn't new behavior on its own: `EmitModuleToFile` (from chapter 16) now takes a `Module*`, an `EmitKind`, and an output path as parameters instead of reading them off file-scope globals. It has to — this chapter calls it three separate times against three different modules (the runtime object, each compiled `.pyxc` file, and the final wrapped file-mode module), and a single global-module version can't do that.

## Accepting Multiple Input Files

Through chapter 16, the positional argument was a single optional `cl::opt`:

```cpp
static cl::opt<string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
                                      cl::init(""), cl::cat(PyxcCategory));
```

I need it to be a list instead, so the driver can accept any number of `.pyxc` and `.o` files:

```cppdiff
-static cl::opt<string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
-                                      cl::init(""), cl::cat(PyxcCategory));
+static cl::list<string> InputFiles(cl::Positional, cl::desc("[inputs]"),
+                                        cl::ZeroOrMore, cl::cat(PyxcCategory));
```

I derive `IsRepl` from whether the list is empty now:

```cpp
int ProcessCommandLine(int argc, const char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::ParseCommandLineOptions(argc, argv, "pyxc\n");

  if (OptLevel > 3) {
    fprintf(stderr, "Error: -O level must be 0, 1, 2, or 3\n");
    return -1;
  }

  IsRepl = InputFiles.empty();
  // ...
}
```

The `--emit exe` path also enforces the multi-input rule:

```cpp
} else if (EmitKindOption == "exe") {
  EmitMode = EmitKind::Executable;
  if (OutputFile.empty() && InputFiles.size() > 1) {
    fprintf(stderr, "Error: multiple inputs require -o\n");
    return -1;
  }
  if (!OutputFile.empty())
    EmitOutputPath = OutputFile.getValue();
}
```

`--emit llvm-ir`, `--emit asm`, and `--emit obj` still require exactly one input and are unchanged.

## Output Naming

When `-o` is omitted and there's exactly one input, I want the output to be the input with its extension stripped — and `.exe` appended on Windows:

```cpp
static string DefaultExecutablePath(StringRef InputPath) {
  SmallString<256> OutputPath(InputPath);
  sys::path::replace_extension(OutputPath, "");
  string Result = OutputPath.str().str();
#ifdef _WIN32
  Result += ".exe";
#endif
  return Result;
}
```

`sys::path::replace_extension` handles both `.pyxc` and `.o` inputs uniformly: `foo.pyxc → foo`, `mylib.o → mylib`.

## Synthesizing the Runtime

In `--emit obj` mode (chapter 16), I linked test binaries against `runtime.c` to get `printd` and `putchard`. For `--emit exe` mode, I want pyxc to synthesize those functions itself — no C file, no external compiler:

```cpp
static bool EmitRuntimeObject(const string &ObjectPath) {
  LLVMContext Context;
  auto RuntimeModule = make_unique<Module>("pyxc.runtime", Context);
  auto *DoubleType = Type::getDoubleTy(Context);
  auto *Int32Type = Type::getInt32Ty(Context);
  auto *PointerType = llvm::PointerType::get(Context, 0);

  FunctionType *PrintfType =
      FunctionType::get(Int32Type, {PointerType}, true);
  Function *Printf = Function::Create(
      PrintfType, Function::ExternalLinkage, "printf", RuntimeModule.get());
  FunctionType *PutcharType =
      FunctionType::get(Int32Type, {Int32Type}, false);
  Function *Putchar = Function::Create(
      PutcharType, Function::ExternalLinkage, "putchar", RuntimeModule.get());

  FunctionType *PrintdType =
      FunctionType::get(DoubleType, {DoubleType}, false);
  Function *Printd = Function::Create(
      PrintdType, Function::ExternalLinkage, "printd", RuntimeModule.get());
  {
    BasicBlock *Entry = BasicBlock::Create(Context, "entry", Printd);
    IRBuilder<> RuntimeBuilder(Entry);
    auto *Format = RuntimeBuilder.CreateGlobalString("%f\n", "format");
    Value *Zero = ConstantInt::get(Int32Type, 0);
    Value *FormatPointer = RuntimeBuilder.CreateInBoundsGEP(
        Format->getValueType(), Format, {Zero, Zero}, "format.pointer");
    RuntimeBuilder.CreateCall(Printf, {FormatPointer, Printd->getArg(0)});
    RuntimeBuilder.CreateRet(ConstantFP::get(Context, APFloat(0.0)));
  }

  FunctionType *PutchardType =
      FunctionType::get(DoubleType, {DoubleType}, false);
  Function *Putchard = Function::Create(PutchardType,
      Function::ExternalLinkage, "putchard", RuntimeModule.get());
  {
    BasicBlock *Entry = BasicBlock::Create(Context, "entry", Putchard);
    IRBuilder<> RuntimeBuilder(Entry);
    Value *Character = RuntimeBuilder.CreateFPToUI(
        Putchard->getArg(0), Int32Type, "character");
    RuntimeBuilder.CreateCall(Putchar, {Character});
    RuntimeBuilder.CreateRet(ConstantFP::get(Context, APFloat(0.0)));
  }

  return EmitModuleToFile(RuntimeModule.get(), EmitKind::Object, ObjectPath);
}
```

The key points:

- I use a fresh, independent `LLVMContext` and `Module` — separate from the user's program module. This isolates the runtime from user IR.
- `printf` and `putchar` are declared as `extern` (they come from libc at link time).
- `printd` and `putchard` are *defined* with `ExternalLinkage` so the linker can resolve the `extern def printd(x)` declarations in user code.
- `EmitRuntimeObject` ends by calling `EmitModuleToFile` to write a real `.o` to a temp path. That `.o` gets added to the link list alongside user objects.

## Per-File Compilation

I want each `.pyxc` input to go through its own full parse-codegen-emit cycle:

```cpp
static bool CompileFileToObject(const string &Path, const string &ObjectPath,
                                bool *HasMain) {
  if (!OpenInputFile(Path))
    return false;

  ResetLexerState();
  ResetParserStateForFile();
  InitializeModuleAndManagers(false);
  IsRepl = false;
  getNextToken();
  FileModeLoop();
  CloseInputFile();

  if (HasMain)
    *HasMain = FunctionSignatures.find("main") != FunctionSignatures.end();
  if (!PrepareFileModeModule())
    return false;
  return EmitModuleToFile(TheModule.get(), EmitKind::Object, ObjectPath);
}
```

`ResetLexerState` and `ResetParserStateForFile` clear the persistent lexer and parser state between files, so each `.pyxc` compiles independently. This is what makes multi-file compilation safe — a global declared in `a.pyxc` doesn't silently bleed into `b.pyxc`.

Getting there needs one lexer change first. Through chapter 16, `getToken()`'s current input character (`LastChar`) was a variable local to the function, declared `static` so it survived between calls:

```cpp
static int getToken() {
  static int LastChar = ' ';
  // ...
}
```

A function-local `static` has no way to be reset from outside the function. To compile a second `.pyxc` file cleanly, the lexer needs to forget where it left off in the first one. So I lift `LastChar` out to file scope and rename it `LexerLastChar`, leaving every use inside `getToken()` otherwise unchanged:

```cpp
static int LexerLastChar = ' ';
// ...
static int getToken() {
  // ... every use of LastChar in this function becomes LexerLastChar ...
}
```

With `LexerLastChar` reachable from outside `getToken()`, `ResetLexerState` can put the whole lexer back to its startup condition before I open the next file:

```cpp
static void ResetLexerState() {
  IndentStack = {0};
  PendingTokens.clear();
  AtLineStart = true;
  LexerLocation = {1, 0};
  CurrentTokenLocation = {1, 0};
  LexerLastChar = ' ';
  PyxcSourceManager.reset();
}
```

`ResetParserStateForFile` does the equivalent for the parser and codegen-adjacent globals — the tables that would otherwise carry declarations from one file's compilation into the next:

```cpp
static void ResetParserStateForFile() {
  FunctionSignatures.clear();
  GlobalVarNames.clear();
  VarScopes.clear();
  FileTopLevelStatements.clear();
  LastTopLevelShouldPrint = true;
  ParsingTopLevel = false;
  InGlobalInit = false;
  ModuleHasGlobals = false;
}
```

`InitializeModuleAndManagers` also picks up a parameter this chapter, `FreshContext`, defaulting to `true` so every existing call site keeps behaving exactly as before:

```cpp
static void InitializeModuleAndManagers(bool FreshContext = true) {
  // Fresh context and module for this compilation unit.
  if (FreshContext || !TheContext)
    TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  // ...
}
```

`CompileFileToObject` calls it with `false`. Each `.pyxc` file still gets a brand-new `Module`, but they all share the one `LLVMContext` created at startup, rather than allocating a fresh `LLVMContext` per file. `LLVMContext` owns type and constant uniquing tables; there's no correctness reason a second file needs its own, and reusing it avoids that allocation on every file in a multi-file `--emit exe` build.

## Shared Codegen Finishing

In chapter 16, `EmitFileMode` contained all the logic for building `__pyxc.global_init`, validating `main`, and wrapping it. I want both the `--emit obj` path and the new per-file `--emit exe` path to share that, so I refactor it out into `PrepareFileModeModule`:

```cpp
static bool PrepareFileModeModule() {
  if (!FileTopLevelStatements.empty()) {
    auto Block =
        make_unique<BlockStatementNode>(std::move(FileTopLevelStatements));
    auto Signature = make_unique<FunctionSignatureNode>(
        "__pyxc.global_init", vector<string>());
    auto FunctionDefinition = make_unique<FunctionDefinitionNode>(
        std::move(Signature), std::move(Block));

    bool SavedInGlobalInit = InGlobalInit;
    InGlobalInit = true;
    if (auto *FunctionIR = FunctionDefinition->codegen()) {
      InGlobalInit = SavedInGlobalInit;
      if (ShouldDumpIR())
        FunctionIR->print(errs());
      AddGlobalConstructor(FunctionIR);
    } else {
      InGlobalInit = SavedInGlobalInit;
      return false;
    }
  }

  auto Main = FunctionSignatures.find("main");
  if (Main != FunctionSignatures.end() &&
      Main->second->getNumParameters() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    return false;
  }

  // A Pyxc main returns double. I preserve it under an internal name and
  // expose the conventional i32 main expected by native linkers.
  if (auto *UserMain = TheModule->getFunction("main")) {
    if (UserMain->getReturnType()->isDoubleTy()) {
      UserMain->setName("__pyxc.user_main");
      FunctionType *WrapperType =
          FunctionType::get(Type::getInt32Ty(*TheContext), false);
      Function *Wrapper = Function::Create(
          WrapperType, Function::ExternalLinkage, "main", TheModule.get());
      BasicBlock *Entry = BasicBlock::Create(*TheContext, "entry", Wrapper);
      IRBuilder<> WrapperBuilder(Entry);
      WrapperBuilder.CreateCall(UserMain);
      WrapperBuilder.CreateRet(
          ConstantInt::get(Type::getInt32Ty(*TheContext), 0));
    }
  }

  return true;
}
```

`EmitFileMode` now calls `PrepareFileModeModule()` followed by `EmitModuleToFile`. `CompileFileToObject` does the same. The logic lives in exactly one place. I save and restore `InGlobalInit` the same way I started doing back in chapter 15, rather than hard-resetting it to `false` — same reasoning as then: restoring whatever it actually was is a safer habit than assuming.

## Linking with LLD

`LinkExecutable` dispatches to the right LLD driver based on the host triple:

```cpp
static bool LinkExecutable(const vector<string> &Inputs,
                           const string &OutputPath) {
  Triple TargetTriple(sys::getDefaultTargetTriple());
  vector<string> ArgumentStorage;
  auto AddArgument = [&](const string &Argument) {
    ArgumentStorage.push_back(Argument);
  };

  if (TargetTriple.isOSDarwin()) {
    AddArgument("ld64.lld");
    AddArgument("-arch");
    AddArgument(TargetTriple.getArchName().str());
    AddArgument("-o");
    AddArgument(OutputPath);
    string SDKRoot = FindMacOSSDKRoot();
    if (!SDKRoot.empty()) {
      AddArgument("-syslibroot");
      AddArgument(SDKRoot);
      AddArgument("-L" + SDKRoot + "/usr/lib");
      AddArgument("-L" + SDKRoot + "/usr/lib/system");
      string SDKVersion = FindMacOSSDKVersion();
      AddArgument("-platform_version");
      AddArgument("macos");
      AddArgument(SDKVersion);
      AddArgument(SDKVersion);
    }
    for (const auto &InputPath : Inputs)
      AddArgument(InputPath);
    AddArgument("-lSystem");

    vector<const char *> Arguments;
    for (auto &Argument : ArgumentStorage)
      Arguments.push_back(Argument.c_str());
    return lld::macho::link(Arguments, outs(), errs(), false, false);
  }

  if (TargetTriple.isOSLinux()) {
    AddArgument("ld.lld");
    AddArgument("-o");
    AddArgument(OutputPath);
    for (const auto &InputPath : Inputs)
      AddArgument(InputPath);
    AddArgument("-lc");
    AddArgument("-lm");
    vector<const char *> Arguments;
    for (auto &Argument : ArgumentStorage)
      Arguments.push_back(Argument.c_str());
    return lld::elf::link(Arguments, outs(), errs(), false, false);
  }

  if (TargetTriple.isOSWindows()) {
    AddArgument("lld-link");
    AddArgument("/OUT:" + OutputPath);
    for (const auto &InputPath : Inputs)
      AddArgument(InputPath);
    vector<const char *> Arguments;
    for (auto &Argument : ArgumentStorage)
      Arguments.push_back(Argument.c_str());
    return lld::coff::link(Arguments, outs(), errs(), false, false);
  }

  fprintf(stderr, "Error: unsupported target for --emit exe\n");
  return false;
}
```

I don't pass `crt1.o`/`crti.o`/`crtn.o` the way a traditional Unix linker invocation would. Those are ELF startup objects; on macOS, `dyld` and `libSystem` handle process startup on their own, and a Mach-O link has no use for them.

The LLD API is the same on every platform: an array of `const char*` arguments (identical to what you'd pass on the command line), plus output/error streams and two flags — `exitEarly` (stop on first error) and `disableOutput` (dry-run). The return value is `true` on success.

This is a key architectural choice: **LLD is called as a library, not as a subprocess**. There's no `fork`/`exec`, no temporary shell script, no PATH lookup. If the library is linked into the `pyxc` binary, it's available.

## SDK Detection

LLD's Mach-O linker needs a sysroot to find system headers and `libSystem`, plus a version string for the `-platform_version` flag. Both of these are things Xcode's own `xcrun` tool already knows how to answer correctly, so I lean on it rather than re-deriving the logic myself:

```cpp
static string RunXcrun(const char *Arguments) {
  string Command = string("xcrun ") + Arguments + " 2>/dev/null";
  FILE *Pipe = popen(Command.c_str(), "r");
  if (!Pipe)
    return "";
  char Buffer[512];
  string Result;
  while (fgets(Buffer, sizeof(Buffer), Pipe))
    Result += Buffer;
  pclose(Pipe);
  while (!Result.empty() &&
         (Result.back() == '\n' || Result.back() == '\r' ||
          Result.back() == ' '))
    Result.pop_back();
  return Result;
}
```

`FindMacOSSDKRoot` tries an explicit override first, then asks `xcrun`:

```cpp
static string FindMacOSSDKRoot() {
  if (const char *SDKRoot = getenv("SDKROOT"))
    return string(SDKRoot);
  string Result = RunXcrun("--sdk macosx --show-sdk-path");
  if (!Result.empty() && sys::fs::exists(Result))
    return Result;
  return "";
}
```

```cpp
static string FindMacOSSDKVersion() {
  string Result = RunXcrun("--sdk macosx --show-sdk-version");
  if (!Result.empty())
    return Result;
  Triple TargetTriple(sys::getDefaultTargetTriple());
  VersionTuple Version = TargetTriple.getOSVersion();
  if (Version.getMajor()) {
    ostringstream Stream;
    Stream << Version.getMajor() << "." << Version.getMinor().value_or(0);
    return Stream.str();
  }
  return "11.0";
}
```

If `xcrun` doesn't resolve a version, I fall back to the OS version encoded in the host triple. Either way, the goal is the same: match whatever SDK version LLVM itself considers active, so `ld64.lld`'s `-platform_version` check doesn't warn about a mismatch.

The `-platform_version macos <min> <sdk>` flag is required by the Mach-O linker to set the LC_BUILD_VERSION load command. Without it, the linker produces a warning or errors depending on the LLD version.

## Telling Inputs Apart

`EmitExecutable` walks `InputFiles` and needs to know, per entry, whether it's pyxc source to compile or an already-built object to pass straight through to the linker. I decide by extension, checked case-insensitively so `Foo.PYXC` and `foo.pyxc` are treated the same:

```cpp
static bool EndsWithInsensitive(StringRef Path, StringRef Suffix) {
  return Path.size() >= Suffix.size() &&
         Path.take_back(Suffix.size()).equals_insensitive(Suffix);
}

static bool IsPyxcInput(StringRef Path) {
  return EndsWithInsensitive(Path, ".pyxc");
}

static bool IsObjectInput(StringRef Path) {
  return EndsWithInsensitive(Path, ".o") ||
         EndsWithInsensitive(Path, ".obj");
}
```

Anything that matches neither is rejected with an "unsupported input" error rather than silently ignored or guessed at.

## The Orchestrator

```cpp
static bool EmitExecutable() {
  vector<string> ObjectFiles;
  vector<string> TemporaryFiles;
  bool SawMain = false;
  bool SawObjectInput = false;

  auto RemoveTemporaryFiles = [&]() {
    for (const auto &Path : TemporaryFiles)
      sys::fs::remove(Path);
  };

  for (const auto &InputPath : InputFiles) {
    if (IsPyxcInput(InputPath)) {
      int FileDescriptor = -1;
      SmallString<128> TemporaryPath;
      if (auto ErrorCode = sys::fs::createTemporaryFile(
              "pyxc", "o", FileDescriptor, TemporaryPath)) {
        fprintf(stderr, "Error: could not create temporary file: %s\n",
                ErrorCode.message().c_str());
        RemoveTemporaryFiles();
        return false;
      }
      if (FileDescriptor != -1)
        close(FileDescriptor);

      string ObjectPath = TemporaryPath.str().str();
      TemporaryFiles.push_back(ObjectPath);
      bool FileHasMain = false;
      if (!CompileFileToObject(InputPath, ObjectPath, &FileHasMain)) {
        RemoveTemporaryFiles();
        return false;
      }
      SawMain = SawMain || FileHasMain;
      ObjectFiles.push_back(ObjectPath);
      continue;
    }

    if (IsObjectInput(InputPath)) {
      ObjectFiles.push_back(InputPath);
      SawObjectInput = true;
      continue;
    }

    fprintf(stderr, "Error: unsupported input '%s'\n", InputPath.c_str());
    RemoveTemporaryFiles();
    return false;
  }

  if (!SawMain && !SawObjectInput) {
    fprintf(stderr, "Error: main() not found\n");
    RemoveTemporaryFiles();
    return false;
  }

  int RuntimeDescriptor = -1;
  SmallString<128> RuntimePath;
  if (auto ErrorCode = sys::fs::createTemporaryFile(
          "pyxc_runtime", "o", RuntimeDescriptor, RuntimePath)) {
    fprintf(stderr, "Error: could not create runtime object: %s\n",
            ErrorCode.message().c_str());
    RemoveTemporaryFiles();
    return false;
  }
  if (RuntimeDescriptor != -1)
    close(RuntimeDescriptor);

  string RuntimeObjectPath = RuntimePath.str().str();
  TemporaryFiles.push_back(RuntimeObjectPath);
  if (!EmitRuntimeObject(RuntimeObjectPath)) {
    RemoveTemporaryFiles();
    return false;
  }
  ObjectFiles.push_back(RuntimeObjectPath);

  if (EmitOutputPath.empty())
    EmitOutputPath = DefaultExecutablePath(InputFiles.front());
  bool Linked = LinkExecutable(ObjectFiles, EmitOutputPath);
  RemoveTemporaryFiles();
  return Linked;
}
```

A few things I want to call out here:

- **`sys::fs::createTemporaryFile` takes a file descriptor out-param in this overload.** It actually creates and opens the file (so the name is guaranteed unique and reserved), and I have to `close()` that descriptor myself once I'm done needing it open — I only wanted the path, not a live handle.
- **`RemoveTemporaryFiles` is a local lambda, not a separate function.** Every error path in this function needs to remove whatever temp files exist so far before returning `false`. It only ever gets used inside `EmitExecutable`, and the lambda already captures exactly the state it needs by reference.
- **The `main()` check.** Nothing upstream of this function checks whether an entry point exists anywhere, so a missing `main` would otherwise silently make it all the way to the linker and fail there with a much less clear error. `SawObjectInput` exists so that "a `.o` might define `main`, I can't inspect it without disassembling it" doesn't turn into a false rejection — if any input is a pre-built object, I let the linker be the judge.
- **`EmitOutputPath.empty()` uses `InputFiles.front()` directly.** By the time `EmitExecutable` runs, `ProcessCommandLine` has already guaranteed `InputFiles` is non-empty (it's how `IsRepl` gets set), so there's no need to re-check emptiness here.

## Wiring It Into main

`main` itself changes shape to route to the right one of these paths. Through chapter 16 it primed the REPL (`PrintReplPrompt(); getNextToken();`) unconditionally before checking `IsRepl`, since file mode needed `getNextToken()` too and there was only ever one file to open. Now that opening a file is a repeatable, resettable operation, I move the REPL priming inside the `IsRepl` branch and give the non-REPL branch its own two-way split: `--emit exe` goes to `EmitExecutable`, which owns opening and closing each of its own input files internally, while every other case (JIT execution or `--emit llvm-ir`/`asm`/`obj`) still opens exactly one file the way chapter 16 did, just now through the shared `OpenInputFile`/`CloseInputFile` helpers and with `ResetLexerState`/`ResetParserStateForFile` called explicitly instead of relying on process startup defaults:

```cpp
if (IsRepl) {
  PrintReplPrompt();
  getNextToken();
  MainLoop();
} else {
  if (EmitMode == EmitKind::Executable) {
    if (!EmitExecutable())
      return 1;
  } else {
    if (!OpenInputFile(InputFiles.front()))
      return 1;
    ResetLexerState();
    ResetParserStateForFile();
    getNextToken();
    FileModeLoop();
    if (IsEmitMode())
      EmitFileMode();
    else
      RunFileMode();
    CloseInputFile();
  }
}
```

`OpenInputFile` and `CloseInputFile` are thin wrappers around `fopen`/`fclose` on the global `Input` handle — the same logic `ProcessCommandLine` used to inline for its single `InputFile` in chapter 16, now factored out so `CompileFileToObject` can reuse it per file inside `EmitExecutable`.

## Splitting Out the Target Machine

`EmitModuleToFile` in chapter 16 built its `TargetMachine` inline, once, because it only ever ran against `TheModule`. This chapter calls it three times against three different modules, so I pull the target-machine construction into its own function:

```cpp
static unique_ptr<TargetMachine> CreateTargetMachine() {
  Triple TargetTriple(sys::getDefaultTargetTriple());
  string Error;
  const Target *NativeTarget = TargetRegistry::lookupTarget(TargetTriple, Error);
  if (!NativeTarget) {
    fprintf(stderr, "Error: %s\n", Error.c_str());
    return nullptr;
  }

  TargetOptions Options;
  auto RelocationModel = std::optional<Reloc::Model>();
  return unique_ptr<TargetMachine>(NativeTarget->createTargetMachine(
      TargetTriple, "generic", "", Options, RelocationModel));
}
```

`EmitModuleToFile` calls it and, on success, reads the triple back off the resulting `Machine` rather than the `Triple` object built earlier: `ModuleIR->setTargetTriple(Machine->getTargetTriple())`. That's a small change from chapter 16, where the module's triple was set directly from `sys::getDefaultTargetTriple()` before the `TargetMachine` existed; going through `Machine->getTargetTriple()` keeps the module's recorded triple in exact agreement with whatever the backend actually resolved and normalized it to.

## New Headers and Build Changes

The new headers this chapter:

```cpp
#include "lld/Common/Driver.h"          // lld::macho::link, lld::elf::link, lld::coff::link
#include "llvm/Support/Path.h"          // sys::path::replace_extension
#include "llvm/Support/VersionTuple.h"  // VersionTuple for OS version extraction
#include <unistd.h>                     // close(), for the file descriptors
                                         // sys::fs::createTemporaryFile hands back
```

The three LLD driver macros need to appear at file scope to register the drivers:

```cpp
LLD_HAS_DRIVER(elf)
LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(macho)
```

`CMakeLists.txt` finds LLD as its own CMake package alongside LLVM, and links its libraries explicitly — `llvm_map_components_to_libnames` only resolves LLVM's own components, not LLD's:

```cmake
find_package(LLD REQUIRED CONFIG HINTS "${LLVM_DIR}/../lld" NO_DEFAULT_PATH)
include_directories(SYSTEM ${LLD_INCLUDE_DIRS})
...
target_link_libraries(pyxc PRIVATE ${LLVM_LIBS} lldCommon lldELF lldMachO lldCOFF)
```

## Known Limitations

**Target is always the host.** The SDK is detected for the machine running `pyxc`. Cross-compilation is not supported.

**`main()` always exits 0.** The `int main()` wrapper returns `0` unconditionally. There is no way to set a non-zero exit code from a pyxc program yet.

**Runtime is always linked in.** Even if a program never calls `printd` or `putchard`, the runtime object is included in every `--emit exe` link. A future chapter could strip unreferenced symbols with LTO.

**SDK detection depends on `xcrun` being on `PATH`.** `FindMacOSSDKRoot` shells out to `xcrun` and returns an empty string if that fails, in which case `LinkExecutable` skips the `-syslibroot` and `-platform_version` arguments entirely. If you have a non-standard Xcode installation and `xcrun` isn't resolving correctly, set the `SDKROOT` environment variable to the SDK path before running `pyxc` — that override is checked before `xcrun` is ever invoked.

**No debug information.** The emitted executables contain no DWARF. Debuggers cannot map instructions back to pyxc source lines.

**Multi-file globals are independent.** Each `.pyxc` file gets its own `__pyxc.global_init`. If two files declare globals with the same name, the linker reports a duplicate-symbol error. There's no cross-file global sharing.

## Try It

**The minimal case**

```bash
cat hello.pyxc
```
```pyxc
extern def printd(x)
def main():
    printd(42)
```
```bash
pyxc --emit exe -o hello hello.pyxc
./hello
```
```
42.000000
```

**Default output name**

```bash
pyxc --emit exe hello.pyxc   # produces ./hello
./hello
```

**Global init runs before main**

```pyxc
extern def printd(x)
var total = 0
for var i = 1, i < 6, 1:
    total = total + i
def main():
    printd(total)   # 15.000000
```

**Linking two files**

```bash
# lib.pyxc
def add(a, b): return a + b
```
```bash
# main.pyxc
extern def printd(x)
extern def add(a, b)
def main():
    printd(add(3, 4))
```
```bash
pyxc --emit exe -o prog main.pyxc lib.pyxc
./prog
```
```
7.000000
```

**Linking a pre-built object**

```bash
pyxc --emit obj -o lib.o lib.pyxc
pyxc --emit exe -o prog main.pyxc lib.o
./prog
```

**Inspect the IR before linking**

```bash
pyxc --dump-ir --emit exe -o prog main.pyxc
```

## Build and Run

```bash
cd code/chapter-17
cmake -S . -B build && cmake --build build
./build/pyxc --emit exe -o hello hello.pyxc
./hello
```

## What's Next

[Chapter 18](chapter-18.md) adds a static type system.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

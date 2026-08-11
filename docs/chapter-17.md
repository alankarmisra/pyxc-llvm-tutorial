---
description: "Add --emit exe so Pyxc compiles and links a standalone executable in one command, using LLD as a library with no external tools."
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
cd pyxc-llvm-tutorial/code/chapter-15
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

In chapter 15, the positional argument was a single optional `cl::opt`:

```cpp
// Chapter 16
static cl::opt<std::string> InputFile(cl::Positional, ...);
```

I need it to be a list instead, so the driver can accept any number of `.pyxc` and `.o` files:

```cpp
// Chapter 17
static cl::list<std::string>
    InputFiles(cl::Positional, cl::desc("[inputs]"), cl::ZeroOrMore,
               cl::cat(PyxcCategory));
```

I derive `IsRepl` from whether the list is empty now:

```cpp
IsRepl = InputFiles.empty();
```

The `--emit exe` path also enforces the multi-input rule:

```cpp
} else if (EmitKindOpt == "exe") {
  EmitMode = EmitKind::EXE;
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
static string DefaultExeOutputPath(StringRef InputPath) {
  SmallString<256> Out(InputPath);
  sys::path::replace_extension(Out, "");
  string OutStr = Out.str().str();
#ifdef _WIN32
  OutStr += ".exe";
#endif
  return OutStr;
}
```

`sys::path::replace_extension` handles both `.pyxc` and `.o` inputs uniformly: `foo.pyxc → foo`, `mylib.o → mylib`.

## Synthesizing the Runtime

In `--emit obj` mode (chapter 15), I linked test binaries against `runtime.c` to get `printd` and `putchard`. For `--emit exe` mode, I want pyxc to synthesize those functions itself — no C file, no external compiler:

```cpp
static bool EmitRuntimeObject(const string &ObjPath) {
  LLVMContext Ctx;
  auto M = std::make_unique<Module>("pyxc.runtime", Ctx);

  auto *DoubleTy  = Type::getDoubleTy(Ctx);
  auto *Int32Ty   = Type::getInt32Ty(Ctx);
  auto *CharPtrTy = PointerType::get(Ctx, 0);

  // Declare printf and putchar (provided by libc).
  FunctionType *PrintfTy = FunctionType::get(Int32Ty, {CharPtrTy}, /*vararg=*/true);
  Function *Printf = Function::Create(PrintfTy, Function::ExternalLinkage,
                                      "printf", M.get());

  FunctionType *PutcharTy = FunctionType::get(Int32Ty, {Int32Ty}, false);
  Function *Putchar = Function::Create(PutcharTy, Function::ExternalLinkage,
                                       "putchar", M.get());

  // Define printd(double) → double.
  FunctionType *PrintdTy = FunctionType::get(DoubleTy, {DoubleTy}, false);
  Function *Printd = Function::Create(PrintdTy, Function::ExternalLinkage,
                                      "printd", M.get());
  {
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Printd);
    IRBuilder<> B(BB);
    auto *FmtGV = B.CreateGlobalString("%f\n", "fmt");
    Value *Zero = ConstantInt::get(Int32Ty, 0);
    Value *Fmt  = B.CreateInBoundsGEP(FmtGV->getValueType(), FmtGV,
                                       {Zero, Zero}, "fmt_ptr");
    B.CreateCall(Printf, {Fmt, Printd->getArg(0)});
    B.CreateRet(ConstantFP::get(Ctx, APFloat(0.0)));
  }

  // Define putchard(double) → double.
  FunctionType *PutchardTy = FunctionType::get(DoubleTy, {DoubleTy}, false);
  Function *Putchard = Function::Create(PutchardTy, Function::ExternalLinkage,
                                        "putchard", M.get());
  {
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Putchard);
    IRBuilder<> B(BB);
    Value *Ch = B.CreateFPToUI(Putchard->getArg(0), Int32Ty, "ch");
    B.CreateCall(Putchar, {Ch});
    B.CreateRet(ConstantFP::get(Ctx, APFloat(0.0)));
  }

  return EmitModuleToFile(M.get(), EmitKind::OBJ, ObjPath);
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
static bool CompileFileToObject(const string &Path, const string &ObjPath,
                                bool *HasMain) {
  if (!OpenInputFile(Path))
    return false;

  ResetLexerState();
  ResetParserStateForFile();
  InitializeModuleAndManagers(false);

  IsRepl = false;
  PrintReplPrompt();
  getNextToken();

  FileModeLoop();
  CloseInputFile();

  if (HasMain)
    *HasMain = FunctionSignatures.find("main") != FunctionSignatures.end();

  if (!PrepareFileModeModule())
    return false;

  return EmitModuleToFile(TheModule.get(), EmitKind::OBJ, ObjPath);
}
```

`ResetLexerState` and `ResetParserStateForFile` clear the persistent lexer and parser state between files, so each `.pyxc` compiles independently. This is what makes multi-file compilation safe — a global declared in `a.pyxc` doesn't silently bleed into `b.pyxc`.

## Shared Codegen Finishing

In chapter 15, `EmitFileMode` contained all the logic for building `__pyxc.global_init`, validating `main`, and wrapping it. I want both the `--emit obj` path and the new per-file `--emit exe` path to share that, so I refactor it out into `PrepareFileModeModule`:

```cpp
static bool PrepareFileModeModule() {
  // 1. Compile __pyxc.global_init from collected top-level statements.
  if (!FileTopLevelStmts.empty()) {
    auto Block = make_unique<BlockExpressionNode>(std::move(FileTopLevelStmts));
    auto Signature = make_unique<FunctionSignatureNode>("__pyxc.global_init",
                                                        vector<string>());
    auto FnAST = make_unique<FunctionDefinitionNode>(std::move(Signature),
                                                     std::move(Block));
    bool SavedInGlobalInit = InGlobalInit;
    InGlobalInit = true;
    if (auto *FnIR = FnAST->codegen()) {
      InGlobalInit = SavedInGlobalInit;
      if (ShouldDumpIR())
        FnIR->print(errs());
      AddGlobalCtor(FnIR);
    } else {
      InGlobalInit = SavedInGlobalInit;
      return false;
    }
  }

  // 2. Validate main() arity.
  auto MainIt = FunctionSignatures.find("main");
  if (MainIt != FunctionSignatures.end() && MainIt->second->getNumParameters() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    return false;
  }

  // 3. Wrap main() to return int.
  if (auto *UserMain = TheModule->getFunction("main")) {
    if (UserMain->getReturnType()->isDoubleTy()) {
      UserMain->setName("__pyxc.user_main");
      FunctionType *FT =
          FunctionType::get(Type::getInt32Ty(*TheContext), false);
      Function *Wrapper = Function::Create(FT, Function::ExternalLinkage,
                                           "main", TheModule.get());
      BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", Wrapper);
      IRBuilder<> TmpB(BB);
      TmpB.CreateCall(UserMain);
      TmpB.CreateRet(ConstantInt::get(Type::getInt32Ty(*TheContext), 0));
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
  Triple TT(sys::getDefaultTargetTriple());
  vector<string> ArgStorage;
  auto PushArg = [&](const string &Arg) { ArgStorage.push_back(Arg); };

  if (TT.isOSDarwin()) {
    PushArg("ld64.lld");
    PushArg("-arch");
    PushArg(TT.getArchName().str());
    PushArg("-o");
    PushArg(OutputPath);

    string SDKRoot = FindMacOSSDKRoot();
    if (!SDKRoot.empty()) {
      PushArg("-syslibroot");
      PushArg(SDKRoot);
      PushArg("-L" + SDKRoot + "/usr/lib");
      PushArg("-L" + SDKRoot + "/usr/lib/system");
      string OSVer = FindMacOSSDKVersion();
      PushArg("-platform_version");
      PushArg("macos");
      PushArg(OSVer);
      PushArg(OSVer);
    }
    // macOS startup is handled by dyld + libSystem; crt1/crti/crtn are
    // GNU ELF files that do not belong in a MachO link and cause warnings
    // on arm64 (the SDK copy is x86_64-only legacy).
    for (const auto &Input : Inputs)
      PushArg(Input);
    PushArg("-lSystem");

    vector<const char *> Arguments;
    Arguments.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Arguments.push_back(Arg.c_str());
    return lld::macho::link(Arguments, llvm::outs(), llvm::errs(), false, false);
  }

  if (TT.isOSLinux()) {
    PushArg("ld.lld");
    PushArg("-o");
    PushArg(OutputPath);
    for (const auto &Input : Inputs)
      PushArg(Input);
    PushArg("-lc");
    PushArg("-lm");
    vector<const char *> Arguments;
    Arguments.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Arguments.push_back(Arg.c_str());
    return lld::elf::link(Arguments, llvm::outs(), llvm::errs(), false, false);
  }

  if (TT.isOSWindows()) {
    PushArg("lld-link");
    PushArg("/OUT:" + OutputPath);
    for (const auto &Input : Inputs)
      PushArg(Input);
    vector<const char *> Arguments;
    Arguments.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Arguments.push_back(Arg.c_str());
    return lld::coff::link(Arguments, llvm::outs(), llvm::errs(), false, false);
  }

  fprintf(stderr, "Error: unsupported target for --emit exe\n");
  return false;
}
```

That comment above the `-lSystem` line is there because I actually tried pushing `crt1.o`/`crti.o` first, the way a traditional Unix linker invocation does it. It seemed like the obviously-correct thing to include. It was wrong — those are GNU/ELF startup objects, this is a Mach-O link, and including them produced link warnings on arm64 (the SDK's copies are x86_64-only legacy files anyway). macOS doesn't need them: `dyld` and `libSystem` handle process startup on their own. I'm leaving the comment in because "the obvious thing" being wrong here is exactly the kind of trap I'd fall into again without it.

The LLD API is the same on every platform: an array of `const char*` arguments (identical to what you'd pass on the command line), plus output/error streams and two flags — `exitEarly` (stop on first error) and `disableOutput` (dry-run). The return value is `true` on success.

This is a key architectural choice: **LLD is called as a library, not as a subprocess**. There's no `fork`/`exec`, no temporary shell script, no PATH lookup. If the library is linked into the `pyxc` binary, it's available.

## SDK Detection

LLD's Mach-O linker needs a sysroot to find system headers and `libSystem`, plus a version string for the `-platform_version` flag. Both of these are things Xcode's own `xcrun` tool already knows how to answer correctly, so I lean on it rather than re-deriving the logic myself:

```cpp
static string RunXcrun(const char *Args) {
  string Cmd = string("xcrun ") + Args + " 2>/dev/null";
  FILE *Pipe = popen(Cmd.c_str(), "r");
  if (!Pipe)
    return "";
  char Buf[512];
  string Result;
  while (fgets(Buf, sizeof(Buf), Pipe))
    Result += Buf;
  pclose(Pipe);
  while (!Result.empty() && (Result.back() == '\n' || Result.back() == '\r' ||
                             Result.back() == ' '))
    Result.pop_back();
  return Result;
}
```

`FindMacOSSDKRoot` tries an explicit override first, then `xcrun`, then falls back to probing well-known install paths if `xcrun` itself isn't available for some reason:

```cpp
static string FindMacOSSDKRoot() {
  if (const char *EnvSDK = getenv("SDKROOT"))
    return string(EnvSDK);

  // Ask xcrun — it resolves the active SDK for the current Xcode/CLT selection
  // and returns the right path regardless of where Xcode is installed.
  string XcrunPath = RunXcrun("--sdk macosx --show-sdk-path");
  if (!XcrunPath.empty() && sys::fs::exists(XcrunPath))
    return XcrunPath;

  // Fallback: probe well-known paths.
  const char *XcodeSDK = "/Applications/Xcode.app/Contents/Developer/Platforms/"
                         "MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  if (sys::fs::exists(XcodeSDK))
    return string(XcodeSDK);

  const char *CLTSDK = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
  if (sys::fs::exists(CLTSDK))
    return string(CLTSDK);

  return "";
}
```

I initially wrote this without `xcrun` at all — just the two hardcoded fallback paths, plus the `SDKROOT` override. It worked, but it meant `pyxc` could report a different SDK version than the one Xcode itself considers active, which showed up as a version-mismatch warning from `ld64.lld`. Asking `xcrun` directly is what the rest of Apple's own toolchain does, so I match it instead of maintaining my own guess:

```cpp
/// FindMacOSSDKVersion - Return the macOS SDK version string (e.g. "26.0").
/// This matches the version LLVM encodes into object files at compile time,
/// avoiding a version mismatch warning from ld64.lld.
static string FindMacOSSDKVersion() {
  string Ver = RunXcrun("--sdk macosx --show-sdk-version");
  if (!Ver.empty())
    return Ver;
  // Fallback: extract from the triple (may be Darwin kernel version on older
  // LLVM builds, so prefer xcrun when available).
  Triple TT(sys::getDefaultTargetTriple());
  VersionTuple V = TT.getOSVersion();
  if (V.getMajor()) {
    std::ostringstream OS;
    OS << V.getMajor() << "." << V.getMinor().value_or(0);
    return OS.str();
  }
  return "11.0";
}
```

The `-platform_version macos <min> <sdk>` flag is required by the Mach-O linker to set the LC_BUILD_VERSION load command. Without it, the linker produces a warning or errors depending on the LLD version.

## The Orchestrator

```cpp
static bool EmitExecutable() {
  vector<string> ObjectFiles;
  vector<string> TempFiles;
  bool SawMain = false;
  bool SawObjectInput = false;

  auto CleanupTemps = [&]() {
    for (const auto &Path : TempFiles)
      sys::fs::remove(Path);
  };

  for (const auto &InputPath : InputFiles) {
    if (IsPyxcInput(InputPath)) {
      int FD = -1;
      SmallString<128> TmpPath;
      if (auto EC = sys::fs::createTemporaryFile("pyxc", "o", FD, TmpPath)) {
        fprintf(stderr, "Error: could not create temporary file: %s\n",
                EC.message().c_str());
        CleanupTemps();
        return false;
      }
      if (FD != -1)
        close(FD);

      string ObjPath = TmpPath.str().str();
      TempFiles.push_back(ObjPath);

      bool FileHasMain = false;
      if (!CompileFileToObject(InputPath, ObjPath, &FileHasMain)) {
        CleanupTemps();
        return false;
      }
      SawMain = SawMain || FileHasMain;
      ObjectFiles.push_back(ObjPath);
      continue;
    }

    if (IsObjectInput(InputPath)) {
      ObjectFiles.push_back(InputPath);
      SawObjectInput = true;
      continue;
    }

    fprintf(stderr, "Error: unsupported input '%s'\n", InputPath.c_str());
    CleanupTemps();
    return false;
  }

  // If nothing I compiled defines main(), and nothing else was passed in
  // that might — a pre-built .o could define it — there's no entry point
  // for the linker to build an executable around.
  if (!SawMain && !SawObjectInput) {
    fprintf(stderr, "Error: main() not found\n");
    CleanupTemps();
    return false;
  }

  int RuntimeFD = -1;
  SmallString<128> RuntimeObj;
  if (auto EC = sys::fs::createTemporaryFile("pyxc_runtime", "o", RuntimeFD,
                                             RuntimeObj)) {
    fprintf(stderr, "Error: could not create runtime object: %s\n",
            EC.message().c_str());
    CleanupTemps();
    return false;
  }
  if (RuntimeFD != -1)
    close(RuntimeFD);

  string RuntimePath = RuntimeObj.str().str();
  TempFiles.push_back(RuntimePath);
  if (!EmitRuntimeObject(RuntimePath)) {
    CleanupTemps();
    return false;
  }
  ObjectFiles.push_back(RuntimePath);

  if (EmitOutputPath.empty()) {
    if (InputFiles.empty()) {
      fprintf(stderr, "Error: --emit exe requires a file input\n");
      CleanupTemps();
      return false;
    }
    EmitOutputPath = DefaultExeOutputPath(InputFiles.front());
  }

  if (!LinkExecutable(ObjectFiles, EmitOutputPath)) {
    CleanupTemps();
    return false;
  }

  CleanupTemps();
  return true;
}
```

A few things I want to call out here:

- **`sys::fs::createTemporaryFile` takes a file descriptor out-param in this overload.** It actually creates and opens the file (so the name is guaranteed unique and reserved), and I have to `close()` that descriptor myself once I'm done needing it open — I only wanted the path, not a live handle.
- **`CleanupTemps` is a local lambda, not a separate function.** Every error path in this function needs to remove whatever temp files exist so far before returning `false`. I considered pulling it out into its own named function, but it only ever gets used inside `EmitExecutable`, and the lambda already captures exactly the state it needs by reference — a separate function would just mean passing `TempFiles` in explicitly every time for no real benefit.
- **The `main()` check.** I hadn't thought about this until I tried linking two plain `.o` files together with no `.pyxc` input at all — nothing in that path was checking whether an entry point existed anywhere, so a missing `main` would silently make it all the way to the linker and fail there with a much less clear error. `SawObjectInput` exists so that "a `.o` might define `main`, I can't inspect it without disassembling it" doesn't turn into a false rejection.

## New Headers and Build Changes

The new headers this chapter:

```cpp
#include "lld/Common/Driver.h"       // lld::macho::link, lld::elf::link, lld::coff::link
#include "llvm/Support/VersionTuple.h"  // VersionTuple for OS version extraction
```

The three LLD driver macros need to appear at file scope to register the drivers:

```cpp
LLD_HAS_DRIVER(elf)
LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(macho)
```

`CMakeLists.txt` links the LLD libraries explicitly — `llvm-config --libs all` doesn't include them:

```cmake
set(LLD_FLAGS "-llldCommon -llldELF -llldMachO -llldCOFF")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LLD_FLAGS}")
```

## Known Limitations

**Target is always the host.** The SDK is detected for the machine running `pyxc`. Cross-compilation is not supported.

**`main()` always exits 0.** The `int main()` wrapper returns `0` unconditionally. There is no way to set a non-zero exit code from a pyxc program yet.

**Runtime is always linked in.** Even if a program never calls `printd` or `putchard`, the runtime object is included in every `--emit exe` link. A future chapter could strip unreferenced symbols with LTO.

**SDK detection depends on `xcrun` being on `PATH`.** `FindMacOSSDKRoot` and `FindMacOSSDKVersion` shell out to `xcrun` first and only fall back to probing fixed filesystem paths if that fails. If you have a non-standard Xcode installation and `xcrun` isn't resolving correctly, set the `SDKROOT` environment variable to the SDK path before running `pyxc` — that override is checked before `xcrun` is ever invoked.

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
cd code/chapter-15
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

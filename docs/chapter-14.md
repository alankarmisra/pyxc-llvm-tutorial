---
description: "Add --emit exe so Pyxc compiles and links a standalone executable in one command, using LLD as a library with no external tools."
---
# 14. Pyxc: One-Step Executables

## Where We Are

[Chapter 13](chapter-13.md) added `--emit obj`, `--emit asm`, and `--emit llvm-ir`. Producing a runnable binary from a Pyxc program still required an external tool:

```bash
pyxc --emit obj -o program.o program.pyxc
clang program.o runtime.c -o program   # ← still needed clang
./program
```

This chapter removes that second step. After it:

```bash
pyxc --emit exe -o program program.pyxc
./program
```

No `clang`, no `runtime.c`, no separate link invocation. One command.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-14
```

## Grammar

No grammar changes. The language is unchanged — this is purely a compiler-driver extension.

## The Design

The key insight is that LLVM ships LLD — a full production linker — as a C++ library. Instead of shelling out to `clang` or `ld`, Pyxc calls `lld::macho::link` (or `lld::elf::link` on Linux) directly in-process. The pipeline becomes:

```
.pyxc → compile → temp .o
.o inputs → pass through
synthesize runtime .o (printd, putchard)
─────────────────────────────────────────
LLD links all .o files into executable
```

The runtime functions `printd` and `putchard` are generated as LLVM IR and emitted to a temporary `.o` — no `runtime.c` or external compiler needed.

## What Changes

Five new pieces on top of chapter 13:

1. **`cl::list<string> InputFiles`** — the positional argument changes from a single string to a list, enabling multiple inputs.
2. **`EmitRuntimeObject`** — synthesizes `printd` and `putchard` as LLVM IR, emits them to a temporary `.o`.
3. **`CompileFileToObject`** — per-file compilation: open → lex → parse → codegen → `.o`.
4. **`PrepareFileModeModule`** — refactored out of `EmitFileMode`: the shared logic that builds `__pyxc.global_init`, registers it in `llvm.global_ctors`, and wraps `main()`.
5. **`LinkExecutable`** + **`FindMacOSSDKRoot`** — LLD-as-library dispatch with platform-aware system library detection.
6. **`EmitExecutable`** — the new orchestrator that wires all of the above together.

## Multiple Inputs: `cl::list`

In chapter 13, the positional argument was a single optional `cl::opt`:

```cpp
// Chapter 13
static cl::opt<std::string> InputFile(cl::Positional, ...);
```

Chapter 14 changes it to a list so the driver can accept any number of `.pyxc` and `.o` files:

```cpp
// Chapter 14
static cl::list<std::string>
    InputFiles(cl::Positional, cl::desc("[inputs]"), cl::ZeroOrMore,
               cl::cat(PyxcCategory));
```

`IsRepl` is now derived from whether the list is empty:

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

## Output Naming: `DefaultExeOutputPath`

When `-o` is omitted and there is exactly one input, the output is the input with its extension stripped — and `.exe` appended on Windows:

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

## Synthesizing the Runtime: `EmitRuntimeObject`

In `--emit obj` mode (chapter 13), tests linked against `runtime.c` to get `printd` and `putchard`. In `--emit exe` mode, Pyxc synthesizes those functions itself — no C file, no external compiler:

```cpp
static bool EmitRuntimeObject(const string &ObjPath) {
  LLVMContext Ctx;
  auto M = std::make_unique<Module>("pyxc.runtime", Ctx);

  auto *DoubleTy = Type::getDoubleTy(Ctx);
  auto *Int32Ty  = Type::getInt32Ty(Ctx);
  auto *PtrTy    = PointerType::get(Ctx, 0);

  // Declare printf and putchar (provided by libc).
  FunctionType *PrintfTy = FunctionType::get(Int32Ty, {PtrTy}, /*vararg=*/true);
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

- A fresh, independent `LLVMContext` and `Module` are used — separate from the user's program module. This isolates the runtime from user IR.
- `printf` and `putchar` are declared as `extern` (they come from libc at link time).
- `printd` and `putchard` are *defined* with `ExternalLinkage` so the linker can resolve the `extern def printd(x)` declarations in user code.
- `EmitRuntimeObject` ends by calling `EmitModuleToFile` to write a real `.o` to a temp path. That `.o` is added to the link list alongside user objects.

## Per-File Compilation: `CompileFileToObject`

Each `.pyxc` input goes through its own full parse-codegen-emit cycle:

```cpp
static bool CompileFileToObject(const string &Path, const string &ObjPath,
                                bool *HasMain) {
  if (!OpenInputFile(Path))
    return false;

  ResetLexerState();
  ResetParserStateForFile();
  InitializeModuleAndManagers(false);  // false = emit mode, no JIT

  IsRepl = false;
  getNextToken();
  FileModeLoop();
  CloseInputFile();

  if (HasMain)
    *HasMain = FunctionProtos.find("main") != FunctionProtos.end();

  if (!PrepareFileModeModule())
    return false;

  return EmitModuleToFile(TheModule.get(), EmitKind::OBJ, ObjPath);
}
```

`ResetLexerState` and `ResetParserStateForFile` clear the persistent lexer and parser state between files, so each `.pyxc` is compiled independently. This is what makes multi-file compilation safe — a global declared in `a.pyxc` does not silently bleed into `b.pyxc`.

## `PrepareFileModeModule`: Shared Codegen Finishing

In chapter 13, `EmitFileMode` contained all the logic for building `__pyxc.global_init`, validating `main`, and wrapping it. Chapter 14 refactors that into `PrepareFileModeModule` so both the `--emit obj` path and the new per-file `--emit exe` path share it:

```cpp
static bool PrepareFileModeModule() {
  // 1. Compile __pyxc.global_init from collected top-level statements.
  if (!FileTopLevelStmts.empty()) {
    auto Block = make_unique<BlockExprAST>(std::move(FileTopLevelStmts));
    auto Proto = make_unique<PrototypeAST>("__pyxc.global_init",
                                           vector<string>());
    auto FnAST = make_unique<FunctionAST>(std::move(Proto),
                                          std::move(Block));
    InGlobalInit = true;
    if (auto *FnIR = FnAST->codegen()) {
      InGlobalInit = false;
      if (ShouldDumpIR())
        FnIR->print(errs());
      AddGlobalCtor(FnIR);
    } else {
      InGlobalInit = false;
      return false;
    }
  }

  // 2. Validate main() arity.
  auto MainIt = FunctionProtos.find("main");
  if (MainIt != FunctionProtos.end() && MainIt->second->getNumArgs() != 0) {
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

`EmitFileMode` now calls `PrepareFileModeModule()` followed by `EmitModuleToFile`. `CompileFileToObject` does the same. The logic lives in exactly one place.

## Linking with LLD: `LinkExecutable`

`LinkExecutable` dispatches to the right LLD driver based on the host triple:

```cpp
static bool LinkExecutable(const vector<string> &Inputs,
                           const string &OutputPath) {
  Triple TT(sys::getDefaultTargetTriple());
  vector<string> ArgStorage;
  auto Push = [&](const string &A) { ArgStorage.push_back(A); };

  if (TT.isOSDarwin()) {
    Push("ld64.lld");
    Push("-arch"); Push(TT.getArchName().str());
    Push("-o");    Push(OutputPath);

    string SDK = FindMacOSSDKRoot();
    if (!SDK.empty()) {
      Push("-syslibroot"); Push(SDK);
      Push("-L" + SDK + "/usr/lib");
      string OSVer = DefaultMacOSVersion(TT);
      Push("-platform_version"); Push("macos"); Push(OSVer); Push(OSVer);
      for (auto &Crt : {SDK+"/usr/lib/crt1.o", SDK+"/usr/lib/crti.o"})
        if (sys::fs::exists(Crt)) Push(Crt);
    }
    for (auto &I : Inputs) Push(I);
    Push("-lSystem");

    vector<const char*> Args;
    for (auto &A : ArgStorage) Args.push_back(A.c_str());
    return lld::macho::link(Args, llvm::outs(), llvm::errs(), false, false);
  }

  if (TT.isOSLinux()) {
    Push("ld.lld");
    Push("-o"); Push(OutputPath);
    for (auto &I : Inputs) Push(I);
    Push("-lc"); Push("-lm");

    vector<const char*> Args;
    for (auto &A : ArgStorage) Args.push_back(A.c_str());
    return lld::elf::link(Args, llvm::outs(), llvm::errs(), false, false);
  }

  if (TT.isOSWindows()) {
    Push("lld-link");
    Push("/OUT:" + OutputPath);
    for (auto &I : Inputs) Push(I);

    vector<const char*> Args;
    for (auto &A : ArgStorage) Args.push_back(A.c_str());
    return lld::coff::link(Args, llvm::outs(), llvm::errs(), false, false);
  }

  fprintf(stderr, "Error: unsupported target for --emit exe\n");
  return false;
}
```

The LLD API is the same on every platform: an array of `const char*` arguments (identical to what you'd pass on the command line), plus output/error streams and two flags — `exitEarly` (stop on first error) and `disableOutput` (dry-run). The return value is `true` on success.

This is a key architectural choice: **LLD is called as a library, not as a subprocess**. There is no `fork`/`exec`, no temporary shell script, no PATH lookup. If the library is linked into the `pyxc` binary, it is available.

## SDK Detection: `FindMacOSSDKRoot` and `DefaultMacOSVersion`

LLD's Mach-O linker needs a sysroot to find system headers and `libSystem`. `FindMacOSSDKRoot` checks three locations in order:

```cpp
static string FindMacOSSDKRoot() {
  // 1. Respect an explicit user override.
  if (const char *EnvSDK = getenv("SDKROOT"))
    return string(EnvSDK);

  // 2. Xcode.app installation.
  const char *XcodeSDK =
      "/Applications/Xcode.app/Contents/Developer/Platforms/"
      "MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  if (sys::fs::exists(XcodeSDK))
    return string(XcodeSDK);

  // 3. Command Line Tools installation (no full Xcode).
  const char *CLTSDK =
      "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
  if (sys::fs::exists(CLTSDK))
    return string(CLTSDK);

  return "";  // caller will try without -syslibroot
}
```

`DefaultMacOSVersion` extracts the OS version from the host triple (e.g., `arm64-apple-macosx14.2.0` → `"14.2"`) and falls back to `"11.0"` if the triple carries no version:

```cpp
static string DefaultMacOSVersion(const Triple &TT) {
  VersionTuple Ver = TT.getOSVersion();
  if (Ver.getMajor()) {
    std::ostringstream OS;
    OS << Ver.getMajor() << "." << Ver.getMinor().value_or(0);
    if (Ver.getSubminor().value_or(0) != 0)
      OS << "." << Ver.getSubminor().value();
    return OS.str();
  }
  return "11.0";
}
```

The `-platform_version macos <min> <sdk>` flag is required by the Mach-O linker to set the LC_BUILD_VERSION load command. Without it, the linker produces a warning or errors depending on the LLD version.

## `EmitExecutable`: The Orchestrator

```cpp
static bool EmitExecutable() {
  vector<string> ObjectFiles;
  vector<string> TempFiles;   // cleaned up on exit regardless of success/failure
  bool SawMain = false;

  for (const auto &InputPath : InputFiles) {
    if (IsPyxcInput(InputPath)) {
      // Compile .pyxc → temp .o
      SmallString<128> TmpPath;
      sys::fs::createTemporaryFile("pyxc", "o", TmpPath);
      TempFiles.push_back(TmpPath.str().str());

      bool FileHasMain = false;
      if (!CompileFileToObject(InputPath, TmpPath.str().str(), &FileHasMain))
        return CleanupAndFail(TempFiles);
      SawMain |= FileHasMain;
      ObjectFiles.push_back(TmpPath.str().str());
    } else if (IsObjectInput(InputPath)) {
      // Pass .o straight to the linker.
      ObjectFiles.push_back(InputPath);
    } else {
      fprintf(stderr, "Error: unsupported input '%s'\n", InputPath.c_str());
      return CleanupAndFail(TempFiles);
    }
  }

  // Synthesize and add the runtime object.
  SmallString<128> RuntimePath;
  sys::fs::createTemporaryFile("pyxc_runtime", "o", RuntimePath);
  TempFiles.push_back(RuntimePath.str().str());
  if (!EmitRuntimeObject(RuntimePath.str().str()))
    return CleanupAndFail(TempFiles);
  ObjectFiles.push_back(RuntimePath.str().str());

  // Resolve the output path if -o was omitted.
  if (EmitOutputPath.empty())
    EmitOutputPath = DefaultExeOutputPath(InputFiles.front());

  bool OK = LinkExecutable(ObjectFiles, EmitOutputPath);
  for (auto &P : TempFiles) sys::fs::remove(P);
  return OK;
}
```

The cleanup pattern — accumulating temp files in `TempFiles` and removing them whether or not linking succeeded — ensures no orphaned `.o` files are left in `/tmp`.

## New Headers and Build Changes

The new headers in chapter 14:

```cpp
#include "lld/Common/Driver.h"       // lld::macho::link, lld::elf::link, lld::coff::link
#include "llvm/Support/VersionTuple.h"  // VersionTuple for OS version extraction
```

The three LLD driver macros must appear at file scope to register the drivers:

```cpp
LLD_HAS_DRIVER(elf)
LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(macho)
```

`CMakeLists.txt` links the LLD libraries explicitly — `llvm-config --libs all` does not include them:

```cmake
set(LLD_FLAGS "-llldCommon -llldELF -llldMachO -llldCOFF")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LLD_FLAGS}")
```

## Known Limitations

**Target is always the host.** The SDK is detected for the machine running `pyxc`. Cross-compilation is not supported.

**`main()` always exits 0.** The `int main()` wrapper returns `0` unconditionally. There is no way to set a non-zero exit code from Pyxc yet.

**Runtime is always linked in.** Even if a program never calls `printd` or `putchard`, the runtime object is included in every `--emit exe` link. A future chapter could strip unreferenced symbols with LTO.

**SDK detection does not use `xcrun`.** The SDK path is found by probing well-known filesystem locations. If you have a non-standard Xcode installation, set the `SDKROOT` environment variable to the SDK path before running `pyxc`.

**No debug information.** The emitted executables contain no DWARF. Debuggers cannot map instructions back to Pyxc source lines.

**Multi-file globals are independent.** Each `.pyxc` file gets its own `__pyxc.global_init`. If two files declare globals with the same name, the linker will report a duplicate-symbol error. There is no cross-file global sharing.

## Try It

**The minimal case**

```bash
cat hello.pyxc
```
```python
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

```python
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
cd code/chapter-14
cmake -S . -B build && cmake --build build
./build/pyxc --emit exe -o hello hello.pyxc
./hello
```

## What's Next

At this point Pyxc can compile multi-file programs to standalone native binaries without requiring any external tools. Future chapters will extend the language itself: a type system beyond `double`, string literals, and structured data.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

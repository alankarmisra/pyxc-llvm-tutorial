---
description: "Add object-file emission so pyxc can compile programs to standalone native binaries without the JIT."
---
# 16. pyxc: Emitting Native Code

## What I Am Building

[Chapter 15](chapter-15.md) gave pyxc global variables and a proper file-mode entry point. By the end of that chapter, I could write a complete pyxc program — global state, helper functions, a `main` — and run it through the JIT:

```bash
./build/pyxc program.pyxc
```

But every run recompiled the program from source. There was no way to produce a `.o` file, link it with other objects, or ship a standalone binary. I want to fix that this chapter.

After this chapter:

```bash
pyxc --emit obj -o program.o program.pyxc
clang program.o test/runtime.c -o program
./program
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-16
```

## What Changes

I'm adding four tightly-coupled pieces on top of chapter 15's codebase:

1. **Command-line flags** — `--emit llvm-ir|asm|obj`, `-o <file>`, and `--dump-ir`.
2. **`EmitModuleToFile`** — writes the compiled module to a file as LLVM IR, native assembly, or a native object file.
3. **`EmitFileMode`** — orchestrates compilation for emit mode: builds `__pyxc.global_init`, wraps `main()`, then calls `EmitModuleToFile`.
4. **`AddGlobalConstructor`** — registers `__pyxc.global_init` in the `llvm.global_ctors` array so the linker wires it to run before `main()` in the emitted binary.

No parser or codegen changes are needed — the chapter 15 IR is already correct. Everything new this chapter is about routing that IR to a file instead of a JIT.

## Grammar

No grammar changes in this chapter. The language itself is unchanged — this is purely a compiler-driver extension.

## The Design

The key insight I'm leaning on: the compilation pipeline doesn't need to change at all — source → tokens → AST → LLVM IR → optimised IR stays exactly as it was. What changes is the *sink*. In JIT mode the sink is the JIT's in-process linker. In emit mode the sink is a file on disk. Because the IR is the same either way, the entire parser and codegen carry over with no modification.

## Command-Line Interface

I declare three new options with LLVM's command-line library:

```cpp
static cl::opt<bool> DumpIR("dump-ir",
                            cl::desc("Print generated LLVM IR to stderr"),
                            cl::init(false), cl::cat(PyxcCategory));
static cl::opt<bool> VerboseIR("v", cl::desc("Alias for --dump-ir"),
                               cl::init(false), cl::cat(PyxcCategory));

static cl::opt<std::string>
    EmitKindOption("emit", cl::desc("Emit output: llvm-ir | asm | obj"),
                   cl::init(""), cl::cat(PyxcCategory));
static cl::opt<std::string> OutputFile("o", cl::desc("Output filename"),
                                       cl::value_desc("filename"), cl::init(""),
                                       cl::cat(PyxcCategory));
```

`ProcessCommandLine` validates and resolves them before I do any parsing:

```cpp
if (!EmitKindOption.empty()) {
  if (IsRepl) {
    fprintf(stderr, "Error: --emit requires a file input\n");
    return -1;
  }

  if (EmitKindOption == "llvm-ir") {
    EmitMode = EmitKind::LLVMIR;
    EmitOutputPath = OutputFile.empty() ? "out.ll" : OutputFile.getValue();
  } else if (EmitKindOption == "asm") {
    EmitMode = EmitKind::Assembly;
    EmitOutputPath = OutputFile.empty() ? "out.s" : OutputFile.getValue();
  } else if (EmitKindOption == "obj") {
    EmitMode = EmitKind::Object;
    EmitOutputPath = OutputFile.empty() ? "out.o" : OutputFile.getValue();
  } else {
    fprintf(stderr, "Error: invalid --emit value '%s'\n",
            EmitKindOption.c_str());
    return -1;
  }
} else if (!OutputFile.empty()) {
  fprintf(stderr, "Error: -o requires --emit\n");
  return -1;
}
```

Key rules I'm enforcing here:

- `--emit` without a source file is an error. The JIT REPL has no concept of an output file.
- An unknown emit kind (`--emit wat`) is an error — the valid set is `llvm-ir`, `asm`, `obj`.
- `-o` without `--emit` is also an error — there's nothing to route to the file.
- If `-o` is omitted, the output path defaults to `out.ll`, `out.s`, or `out.o` in the current working directory.

I declare the `EmitKind` enum and a global string for the resolved path alongside the other global state:

```cpp
enum class EmitKind { None, LLVMIR, Assembly, Object };
static EmitKind EmitMode = EmitKind::None;
static string EmitOutputPath;

static bool IsEmitMode() { return EmitMode != EmitKind::None; }
```

After `FileModeLoop` finishes parsing the source file, I dispatch on `IsEmitMode()` in `main`:

```cpp
FileModeLoop();
if (IsEmitMode())
  EmitFileMode();
else
  RunFileMode();
```

`IsEmitMode()` also gates the per-function JIT path inside `HandleFunctionDefinition`. In JIT mode, each compiled function is immediately transferred to the JIT and the module is replaced:

```cpp
// HandleFunctionDefinition — after codegen:
if (!IsEmitMode()) {
  ExitOnErr(TheJIT->addModule(
      ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
  InitializeModuleAndManagers();
}
```

In emit mode this block is skipped entirely. All functions accumulate in the same `TheModule` until `EmitFileMode` writes it out. If I left the guard out, every `def` would hand the module to the JIT and reinitialise, leaving `EmitFileMode` with an empty module.

## The Emit Pipeline

`EmitModuleToFile` is the leaf that does the actual file writing. It opens the output path with `raw_fd_ostream` and then branches on the emit kind:

```cpp
static bool EmitModuleToFile() {
  std::error_code ErrorCode;
  raw_fd_ostream Destination(EmitOutputPath, ErrorCode, sys::fs::OF_None);
  if (ErrorCode) {
    fprintf(stderr, "Error: could not open output file '%s'\n",
            EmitOutputPath.c_str());
    return false;
  }

  if (EmitMode == EmitKind::LLVMIR) {
    TheModule->print(Destination, nullptr);
    return true;
  }

  Triple TargetTriple(sys::getDefaultTargetTriple());
  TheModule->setTargetTriple(TargetTriple);

  string Error;
  const Target *NativeTarget = TargetRegistry::lookupTarget(TargetTriple, Error);
  if (!NativeTarget) {
    fprintf(stderr, "Error: %s\n", Error.c_str());
    return false;
  }

  TargetOptions Options;
  auto RelocationModel = std::optional<Reloc::Model>();
  auto Machine = std::unique_ptr<TargetMachine>(NativeTarget->createTargetMachine(
      TargetTriple, "generic", "", Options, RelocationModel));
  TheModule->setDataLayout(Machine->createDataLayout());

  legacy::PassManager PassManager;
  CodeGenFileType FileType = EmitMode == EmitKind::Assembly
                                 ? CodeGenFileType::AssemblyFile
                                 : CodeGenFileType::ObjectFile;
  if (Machine->addPassesToEmitFile(PassManager, Destination, nullptr,
                                   FileType)) {
    fprintf(stderr, "Error: target does not support file emission\n");
    return false;
  }

  PassManager.run(*TheModule);
  return true;
}
```

I name the LLVM `Target*` variable `NativeTarget`, since `Target` is already taken by the `llvm::Target` type it points to — naming it `Target` too would shadow the type inside its own declaration.

**LLVM IR path.** `Module::print` writes the module's textual IR directly to the stream. No target information is needed — IR is portable.

**ASM / OBJ path.** These need the full backend pipeline:

- `sys::getDefaultTargetTriple()` returns the host's triple (e.g., `arm64-apple-macosx14.0.0`).
- `TargetRegistry::lookupTarget` finds the backend registered for that triple. It fails if the target wasn't initialized at startup — that's why the three `InitializeNativeTarget*` calls in `main` matter.
- `createTargetMachine` produces a `TargetMachine` that encapsulates the backend's code generator for the specific CPU and relocation model.
- I update the module's data layout to match the target, so type sizes and alignments are correct.
- I use `legacy::PassManager` here (not the new `PassManager`) because `addPassesToEmitFile` is part of the legacy pipeline API — it's the standard LLVM idiom for code generation to a file.
- `addPassesToEmitFile` adds all the backend passes needed to lower IR to machine code and format it as assembly text or an ELF/Mach-O object file.
- `PassManager.run(*TheModule)` runs the pipeline, writing the output into `Destination`.

The new headers this path needs:

```cpp
#include "llvm/IR/LegacyPassManager.h"     // legacy::PassManager
#include "llvm/MC/TargetRegistry.h"        // TargetRegistry
#include "llvm/Support/CodeGen.h"          // CodeGenFileType
#include "llvm/Support/FileSystem.h"       // raw_fd_ostream, OF_None
#include "llvm/Support/raw_ostream.h"      // raw_fd_ostream's base class
#include "llvm/Target/TargetOptions.h"     // TargetOptions
#include "llvm/TargetParser/Host.h"        // getDefaultTargetTriple
#include "llvm/TargetParser/Triple.h"      // Triple
```

`llvm/Target/TargetMachine.h` is already included from [Chapter 8](chapter-08.md)'s JIT setup, so it isn't new here even though this path depends on it too.

## The Orchestrator

`EmitFileMode` is the emit-mode counterpart to `RunFileMode`. It does the same setup — build `__pyxc.global_init`, validate `main`, wrap `main` — but instead of JIT-executing the result, it calls `EmitModuleToFile`.

```cpp
/// I build the queued global initializer and emit the complete module.
static void EmitFileMode() {
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
      return;
    }
  }

  auto Main = FunctionSignatures.find("main");
  if (Main != FunctionSignatures.end() &&
      Main->second->getNumParameters() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    return;
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

  EmitModuleToFile();
}
```

Three things are meaningfully different from `RunFileMode`:

1. **`AddGlobalConstructor` instead of JIT-calling `__pyxc.global_init`.** In JIT mode, `RunFileMode` looks up the symbol and calls it directly. In emit mode there is no JIT — the binary hasn't been linked yet. Instead, I register `__pyxc.global_init` in `llvm.global_ctors` so the linker wires it to run before `main()` automatically.

2. **`main()` return-type wrapping.** pyxc's `main()` returns `double` (everything in pyxc is a double). But the C runtime expects `int main()`. `EmitFileMode` detects this mismatch, renames the user's function to `__pyxc.user_main`, and synthesises a new `int main()` that calls it and returns `0`.

3. **`EmitModuleToFile()` as the final step** instead of looking up and calling symbols.

## Wiring Globals into the Binary

When a pyxc program declares global variables, `__pyxc.global_init` must run before `main()` — otherwise globals hold `0.0` when `main` starts. In JIT mode `RunFileMode` calls `__pyxc.global_init` explicitly before calling `main`. In a native binary, the C runtime manages startup: it calls everything in `llvm.global_ctors` before `main()`. `AddGlobalConstructor` puts `__pyxc.global_init` into that list.

```cpp
/// I register the global-initialization function to run before main.
static void AddGlobalConstructor(Function *FunctionIR, int Priority = 65535) {
  auto *Int32Type = Type::getInt32Ty(*TheContext);
  auto *PointerType = llvm::PointerType::get(*TheContext, 0);
  auto *EntryType = StructType::get(Int32Type, FunctionIR->getType(), PointerType);

  Constant *Entry = ConstantStruct::get(
      EntryType, ConstantInt::get(Int32Type, Priority), FunctionIR,
      ConstantPointerNull::get(PointerType));
  ArrayType *ConstructorsType = ArrayType::get(EntryType, 1);
  auto *Initializer = ConstantArray::get(ConstructorsType, {Entry});
  new GlobalVariable(*TheModule, ConstructorsType, false,
                     GlobalValue::AppendingLinkage, Initializer,
                     "llvm.global_ctors");
}
```

There's only one call site today (`EmitFileMode`, for `__pyxc.global_init`), and no guard against calling this twice — a second call would create a second `llvm.global_ctors` global in the same module, and LLVM would silently rename it rather than merge or error, quietly breaking the linker's contract with this special symbol. That's fine as long as `EmitFileMode` only ever calls it once per module, which is the only way it's used right now.

`llvm.global_ctors` is a special LLVM global with `AppendingLinkage`. The linker concatenates all contributions from different objects into one array. Each element is a `{ i32 priority, ptr fn, ptr data }` struct; the lower the priority number, the earlier the function runs. I use `65535` (lowest priority), which is conventional for user-level constructors.

The `data` field (third struct member) is a guard pointer: if non-null, the runtime skips the entry under certain conditions. I set it to null, meaning "always run."

## `main()` Return-Type Wrapping

pyxc's type system has only `double`. Every function — including `main` — returns `double`. But the C ABI that the linker and OS loader expect declares `main` as `int main()`.

I bridge this automatically inside `EmitFileMode`. When it finds a user-defined `main` function with a `double` return type, it:

1. Renames the original to `__pyxc.user_main`.
2. Creates a new `int main()` that calls `__pyxc.user_main` (discarding its return value) and returns the integer `0`.

```
; Before wrapping:
define double @main() { ... }

; After wrapping:
define double @__pyxc.user_main() { ... }

define i32 @main() {
entry:
  call double @__pyxc.user_main()
  ret i32 0
}
```

This is transparent to the pyxc programmer. You write `def main(): ...` exactly as in file mode.

## `--dump-ir` and `-v`

I renamed the flag that prints generated IR to stderr from `-v` to `--dump-ir`, to make its purpose more explicit. I kept the old `-v` around as a backward-compatible alias:

```cpp
static cl::opt<bool>
    DumpIR("dump-ir", cl::desc("Print generated LLVM IR to stderr"),
           cl::init(false), cl::cat(PyxcCategory));

static cl::opt<bool>
    VerboseIR("v", cl::desc("Alias for --dump-ir"), cl::init(false),
              cl::cat(PyxcCategory));

static bool ShouldDumpIR() { return DumpIR || VerboseIR; }
```

I call `ShouldDumpIR()` wherever IR is printed — after each function in JIT mode, and after codegen in emit mode. Both flags trigger the same behaviour.

## Target Initialization

The two `InitializeNative*` calls in `main` were already present for the JIT. They stay sufficient for emit mode too, because pyxc always targets the host machine:

```cpp
int main(int argc, const char **argv) {
  int commandLineResult = ProcessCommandLine(argc, argv);
  if (commandLineResult != 0) {
    return commandLineResult;
  }

  // Initialise LLVM's backend for the host machine.
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  // ...
}
```

`InitializeNativeTargetAsmPrinter` registers the backend that serializes machine instructions to assembly text or object file bytes — the part that `addPassesToEmitFile` depends on. Without it, `TargetRegistry::lookupTarget` would succeed but `addPassesToEmitFile` would fail.

## Known Limitations

**Emit mode does not run the program.** `--emit` compiles to a file and exits. If you want to both emit and run, compile, link, and execute the binary separately.

**Single-file compilation only.** pyxc does not have a multi-file model. Each invocation compiles one source file to one output file. Linking multiple pyxc objects together is possible but requires manual `extern def` declarations at the moment.

**No debug information.** The emitted object files contain no DWARF or other debug info. Debuggers cannot map machine instructions back to pyxc source lines.

**Target is always the host.** There is no cross-compilation support. The output file targets the same CPU and OS as the machine running `pyxc`.

**`main()` always returns 0.** The synthesised `int main()` wrapper ignores the double value returned by the user's `main()` and always returns `0`. There is no way to return a non-zero exit code from a pyxc program yet.

## Try It

**Emit LLVM IR and inspect it**

```bash
cat sq.pyxc
```
```pyxc
extern def printd(x)
def sq(x): return x * x
def main():
    printd(sq(3))
```
```bash
pyxc --emit llvm-ir -o sq.ll sq.pyxc
cat sq.ll
```
```llvm
; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "..."   ; host-specific; e.g. arm64 macOS

declare double @printd(double)

define double @sq(double %x) {
entry:
  %multmp = fmul double %x, %x
  ret double %multmp
}

define double @__pyxc.user_main() {
entry:
  %calltmp = call double @sq(double 3.000000e+00)
  %calltmp1 = call double @printd(double %calltmp)
  ret double 0.000000e+00
}

define i32 @main() {
entry:
  %0 = call double @__pyxc.user_main()
  ret i32 0
}
```

No `__pyxc.global_init` here — `sq.pyxc` has no top-level `var`, so `AddGlobalConstructor` never runs. What you do see is the `main()` wrapping from earlier: `main` renamed to `__pyxc.user_main`, and a fresh `i32 @main()` calling it and returning `0`.

**Emit assembly**

```bash
pyxc --emit asm -o sq.s sq.pyxc
grep -A2 "sq:" sq.s
```

On macOS the label is actually `_sq:` — the platform's C ABI prepends an underscore — but `grep "sq:"` still matches it as a substring, so this works on both macOS and Linux as written.

**Compile to a native binary**

```bash
# test/runtime.c provides printd/putchard for standalone binaries.
pyxc --emit obj -o sq.o sq.pyxc
file sq.o
clang sq.o test/runtime.c -o sq
./sq
```
```
sq.o: Mach-O 64-bit object arm64
9.000000
```

**Inspect IR while emitting**

```bash
pyxc --dump-ir --emit llvm-ir -o sq.ll sq.pyxc
```

The `--dump-ir` flag prints the IR to stderr as each function is compiled — before the file is written, so you see both the intermediate IR and the final output file.

**Default output paths**

```bash
pyxc --emit llvm-ir sq.pyxc   # writes out.ll
pyxc --emit asm    sq.pyxc   # writes out.s
pyxc --emit obj    sq.pyxc   # writes out.o
```

## Build and Run

```bash
cd code/chapter-16
cmake -S . -B build && cmake --build build
./build/pyxc --emit obj -o program.o program.pyxc
clang program.o test/runtime.c -o program
./program
```

## What's Next

[Chapter 17](chapter-17.md) adds `--emit exe` to compile and link a standalone executable in one step.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

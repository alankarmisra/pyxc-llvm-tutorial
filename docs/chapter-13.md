---
description: "Add object-file emission so Pyxc can compile programs to standalone native binaries without the JIT."
---
# 13. Pyxc: Emitting Native Code

## Where We Are

[Chapter 12](chapter-12.md) gave Pyxc global variables and a proper file-mode entry point. By the end of that chapter, you could write a complete Pyxc program — global state, helper functions, a `main` — and run it through the JIT:

```bash
./build/pyxc program.pyxc
```

But every run recompiled the program from source. There was no way to produce a `.o` file, link it with other objects, or ship a standalone binary. This chapter adds that.

After this chapter:

```bash
pyxc --emit obj -o program.o program.pyxc
clang program.o runtime.c -o program
./program
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-13
```

## What Changes

Chapter 13 adds four tightly-coupled pieces on top of chapter 12's codebase:

1. **Command-line flags** — `--emit llvm-ir|asm|obj`, `-o <file>`, and `--dump-ir`.
2. **`EmitModuleToFile`** — writes the compiled module to a file as LLVM IR, native assembly, or a native object file.
3. **`EmitFileMode`** — orchestrates compilation for emit mode: builds `__pyxc.global_init`, wraps `main()`, then calls `EmitModuleToFile`.
4. **`AddGlobalCtor`** — registers `__pyxc.global_init` in the `llvm.global_ctors` array so the linker wires it to run before `main()` in the emitted binary.

No parser or codegen changes are needed — the chapter 12 IR is already correct. Everything new in chapter 13 is about routing that IR to a file instead of a JIT.

## Grammar

No grammar changes in this chapter. The language itself is unchanged — this chapter is purely a compiler-driver extension.

## The Design

The key insight is that the compilation pipeline is unchanged: source → tokens → AST → LLVM IR → optimised IR. What changes is the *sink*. In JIT mode the sink is the JIT's in-process linker. In emit mode the sink is a file on disk. Because the IR is the same either way, the entire parser and codegen carry over with no modification.

## Command-Line Interface

Three new options are declared with LLVM's command-line library:

```cpp
static cl::opt<std::string>
    EmitKindOpt("emit",
                cl::desc("Emit output: llvm-ir | asm | obj"),
                cl::init(""), cl::cat(PyxcCategory));

static cl::opt<std::string> OutputFile("o", cl::desc("Output filename"),
                                       cl::value_desc("filename"),
                                       cl::init(""), cl::cat(PyxcCategory));

static cl::opt<bool>
    DumpIR("dump-ir", cl::desc("Print generated LLVM IR to stderr"),
           cl::init(false), cl::cat(PyxcCategory));
// Backward-compat alias.
static cl::opt<bool>
    VerboseIR("v", cl::desc("Alias for --dump-ir"), cl::init(false),
              cl::cat(PyxcCategory));
```

`ProcessCommandLine` validates and resolves them before any parsing happens:

```cpp
if (!EmitKindOpt.empty()) {
  if (IsRepl) {
    fprintf(stderr, "Error: --emit requires a file input\n");
    return -1;
  }

  if (EmitKindOpt == "llvm-ir") {
    EmitMode = EmitKind::LLVMIR;
    EmitOutputPath = OutputFile.empty() ? "out.ll" : OutputFile.getValue();
  } else if (EmitKindOpt == "asm") {
    EmitMode = EmitKind::ASM;
    EmitOutputPath = OutputFile.empty() ? "out.s" : OutputFile.getValue();
  } else if (EmitKindOpt == "obj") {
    EmitMode = EmitKind::OBJ;
    EmitOutputPath = OutputFile.empty() ? "out.o" : OutputFile.getValue();
  } else {
    fprintf(stderr, "Error: invalid --emit value '%s'\n",
            EmitKindOpt.c_str());
    return -1;
  }
} else if (!OutputFile.empty()) {
  fprintf(stderr, "Error: -o requires --emit\n");
  return -1;
}
```

Key rules enforced here:

- `--emit` without a source file is an error. The JIT REPL has no concept of an output file.
- An unknown emit kind (`--emit wat`) is an error — the valid set is `llvm-ir`, `asm`, `obj`.
- `-o` without `--emit` is also an error — there's nothing to route to the file.
- If `-o` is omitted, the output path defaults to `out.ll`, `out.s`, or `out.o` in the current working directory.

The `EmitKind` enum and a global string for the resolved path are declared alongside the other global state:

```cpp
enum class EmitKind { None, LLVMIR, ASM, OBJ };
static EmitKind EmitMode = EmitKind::None;
static string EmitOutputPath;

static bool IsEmitMode() { return EmitMode != EmitKind::None; }
```

After `FileModeLoop` finishes parsing the source file, `main` dispatches on `IsEmitMode()`:

```cpp
FileModeLoop();
if (IsEmitMode())
  EmitFileMode();
else
  RunFileMode();
```

`IsEmitMode()` also gates the per-function JIT path inside `HandleDefinition` and the decorator handler. In JIT mode, each compiled function is immediately transferred to the JIT and the module is replaced:

```cpp
// HandleDefinition — after codegen:
if (!IsEmitMode()) {
  ExitOnErr(TheJIT->addModule(
      ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
  InitializeModuleAndManagers();
}
```

In emit mode this block is skipped entirely. All functions accumulate in the same `TheModule` until `EmitFileMode` writes it out. If the guard were absent, every `def` would hand the module to the JIT and reinitialise, leaving `EmitFileMode` with an empty module.

## The Emit Pipeline: `EmitModuleToFile`

`EmitModuleToFile` is the leaf that does the actual file writing. It opens the output path with `raw_fd_ostream` and then branches on the emit kind:

```cpp
static bool EmitModuleToFile() {
  std::error_code EC;
  raw_fd_ostream Dest(EmitOutputPath, EC, sys::fs::OF_None);
  if (EC) {
    fprintf(stderr, "Error: could not open output file '%s'\n",
            EmitOutputPath.c_str());
    return false;
  }

  if (EmitMode == EmitKind::LLVMIR) {
    TheModule->print(Dest, nullptr);
    return true;
  }

  // ASM and OBJ paths require a TargetMachine.
  string TargetTriple = sys::getDefaultTargetTriple();
  Triple TT(TargetTriple);
  TheModule->setTargetTriple(TT);

  string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TargetTriple, Error);
  if (!TheTarget) {
    fprintf(stderr, "Error: %s\n", Error.c_str());
    return false;
  }

  TargetOptions Options;
  auto RM = std::optional<Reloc::Model>();
  std::unique_ptr<TargetMachine> TM(
      TheTarget->createTargetMachine(TT, "generic", "", Options, RM));
  TheModule->setDataLayout(TM->createDataLayout());

  legacy::PassManager PM;
  CodeGenFileType FileType = (EmitMode == EmitKind::ASM)
                                 ? CodeGenFileType::AssemblyFile
                                 : CodeGenFileType::ObjectFile;

  if (TM->addPassesToEmitFile(PM, Dest, nullptr, FileType)) {
    fprintf(stderr, "Error: target does not support file emission\n");
    return false;
  }

  PM.run(*TheModule);
  return true;
}
```

**LLVM IR path.** `Module::print` writes the module's textual IR directly to the stream. No target information is needed — IR is portable.

**ASM / OBJ path.** These require the full backend pipeline:

- `sys::getDefaultTargetTriple()` returns the host's triple (e.g., `arm64-apple-macosx14.0.0`).
- `TargetRegistry::lookupTarget` finds the backend registered for that triple. It will fail if the target was not initialized at startup — that's why the three `InitializeNativeTarget*` calls in `main` matter.
- `createTargetMachine` produces a `TargetMachine` that encapsulates the backend's code generator for the specific CPU and relocation model.
- The module's data layout is updated to match the target, so type sizes and alignments are correct.
- `legacy::PassManager` is used here (not the new `PassManager`) because `addPassesToEmitFile` is part of the legacy pipeline API — it is the standard LLVM idiom for code generation to a file.
- `addPassesToEmitFile` adds all the backend passes needed to lower IR to machine code and format it as assembly text or an ELF/Mach-O object file.
- `PM.run(*TheModule)` runs the pipeline, writing the output into `Dest`.

The new headers required for this path:

```cpp
#include "llvm/Support/FileSystem.h"       // raw_fd_ostream, OF_None
#include "llvm/Support/CodeGen.h"          // CodeGenFileType
#include "llvm/Target/TargetMachine.h"     // TargetMachine, TargetOptions
#include "llvm/Target/TargetOptions.h"     // TargetOptions
#include "llvm/MC/TargetRegistry.h"        // TargetRegistry
#include "llvm/TargetParser/Host.h"        // getDefaultTargetTriple
#include "llvm/TargetParser/Triple.h"      // Triple
#include "llvm/IR/LegacyPassManager.h"     // legacy::PassManager
```

## `EmitFileMode`: The Orchestrator

`EmitFileMode` is the emit-mode counterpart to `RunFileMode`. It does the same setup — build `__pyxc.global_init`, validate `main`, wrap `main` — but instead of JIT-executing the result, it calls `EmitModuleToFile`.

```cpp
static void EmitFileMode() {
  // 1. Compile __pyxc.global_init from the collected top-level statements.
  if (!FileTopLevelStmts.empty()) {
    auto Block = make_unique<BlockExprAST>(std::move(FileTopLevelStmts));
    auto Proto =
        make_unique<PrototypeAST>("__pyxc.global_init", vector<string>());
    auto FnAST = make_unique<FunctionAST>(std::move(Proto), std::move(Block));

    InGlobalInit = true;
    if (auto *FnIR = FnAST->codegen()) {
      InGlobalInit = false;
      if (ShouldDumpIR())
        FnIR->print(errs());
      AddGlobalCtor(FnIR);   // <-- differs from RunFileMode
    } else {
      InGlobalInit = false;
      return;
    }
  }

  // 2. Validate main() arity.
  auto MainIt = FunctionProtos.find("main");
  if (MainIt != FunctionProtos.end() && MainIt->second->getNumArgs() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    return;
  }

  // 3. Wrap main() to return int.
  if (auto *UserMain = TheModule->getFunction("main")) {
    if (UserMain->getReturnType()->isDoubleTy()) {
      UserMain->setName("__pyxc.user_main");
      FunctionType *FT =
          FunctionType::get(Type::getInt32Ty(*TheContext), false);
      Function *Wrapper =
          Function::Create(FT, Function::ExternalLinkage, "main",
                           TheModule.get());
      BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", Wrapper);
      IRBuilder<> TmpB(BB);
      TmpB.CreateCall(UserMain);
      TmpB.CreateRet(ConstantInt::get(Type::getInt32Ty(*TheContext), 0));
    }
  }

  // 4. Write the output file.
  EmitModuleToFile();
}
```

Three things are meaningfully different from `RunFileMode`:

1. **`AddGlobalCtor` instead of JIT-calling `__pyxc.global_init`.** In JIT mode, `RunFileMode` looks up the symbol and calls it directly. In emit mode there is no JIT — the binary hasn't been linked yet. Instead, `__pyxc.global_init` is registered in `llvm.global_ctors` so the linker will wire it to run before `main()` automatically.

2. **`main()` return-type wrapping.** Pyxc's `main()` returns `double` (everything in Pyxc is a double). But the C runtime expects `int main()`. `EmitFileMode` detects this mismatch, renames the user's function to `__pyxc.user_main`, and synthesises a new `int main()` that calls it and returns `0`.

3. **`EmitModuleToFile()` as the final step** instead of looking up and calling symbols.

## `AddGlobalCtor`: Wiring Globals into the Binary

When a Pyxc program declares global variables, `__pyxc.global_init` must run before `main()` — otherwise globals hold `0.0` when `main` starts. In JIT mode `RunFileMode` calls `__pyxc.global_init` explicitly before calling `main`. In a native binary, the C runtime manages startup: it calls everything in `llvm.global_ctors` before `main()`. `AddGlobalCtor` puts `__pyxc.global_init` into that list.

```cpp
static void AddGlobalCtor(Function *Fn, int Priority = 65535) {
  auto *Int32Ty = Type::getInt32Ty(*TheContext);
  auto *VoidPtrTy = PointerType::get(*TheContext, 0);
  auto *StructTy = StructType::get(Int32Ty, Fn->getType(), VoidPtrTy);

  Constant *CtorEntry = ConstantStruct::get(
      StructTy,
      ConstantInt::get(Int32Ty, Priority),
      Fn,
      ConstantPointerNull::get(cast<PointerType>(VoidPtrTy)));

  ArrayType *AT = ArrayType::get(StructTy, 1);
  auto *Init = ConstantArray::get(AT, {CtorEntry});
  new GlobalVariable(*TheModule, AT, false,
                     GlobalValue::AppendingLinkage,
                     Init, "llvm.global_ctors");
}
```

`llvm.global_ctors` is a special LLVM global with `AppendingLinkage`. The linker concatenates all contributions from different objects into one array. Each element is a `{ i32 priority, ptr fn, ptr data }` struct; the lower the priority number, the earlier the function runs. Pyxc uses `65535` (lowest priority), which is conventional for user-level constructors.

The `data` field (third struct member) is a guard pointer: if non-null, the runtime will skip the entry when running under certain conditions. Pyxc sets it to null, meaning "always run."

## `main()` Return-Type Wrapping

Pyxc's type system has only `double`. Every function — including `main` — returns `double`. But the C ABI that the linker and OS loader expect declares `main` as `int main()`.

`EmitFileMode` bridges this automatically. When it finds a user-defined `main` function with a `double` return type, it:

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

This is transparent to the Pyxc programmer. You write `def main(): ...` exactly as in file mode.

## `--dump-ir` and `-v`

The flag that prints generated IR to stderr has been renamed from `-v` to `--dump-ir` to make its purpose more explicit. The old `-v` is kept as a backward-compatible alias:

```cpp
static cl::opt<bool>
    DumpIR("dump-ir", cl::desc("Print generated LLVM IR to stderr"),
           cl::init(false), cl::cat(PyxcCategory));

static cl::opt<bool>
    VerboseIR("v", cl::desc("Alias for --dump-ir"), cl::init(false),
              cl::cat(PyxcCategory));

static bool ShouldDumpIR() { return DumpIR || VerboseIR; }
```

`ShouldDumpIR()` is called wherever IR is printed — after each function in JIT mode, and after codegen in emit mode. Both flags trigger the same behaviour.

## Target Initialization

The three `InitializeNative*` calls in `main` were already present for the JIT. They remain sufficient for emit mode too, because Pyxc always targets the host machine:

```cpp
InitializeNativeTarget();
InitializeNativeTargetAsmPrinter();
InitializeNativeTargetAsmParser();
```

`InitializeNativeTargetAsmPrinter` registers the backend that serializes machine instructions to assembly text or object file bytes — the part that `addPassesToEmitFile` depends on. Without it, `TargetRegistry::lookupTarget` would succeed but `addPassesToEmitFile` would fail.

## Known Limitations

**Emit mode does not run the program.** `--emit` compiles to a file and exits. If you want to both emit and run, compile, link, and execute the binary separately.

**Single-file compilation only.** Pyxc does not have a multi-file model. Each invocation compiles one source file to one output file. Linking multiple Pyxc objects together is possible but requires manual `extern def` declarations at the moment.

**No debug information.** The emitted object files contain no DWARF or other debug info. Debuggers cannot map machine instructions back to Pyxc source lines.

**Target is always the host.** There is no cross-compilation support. The output file targets the same CPU and OS as the machine running `pyxc`.

**`main()` always returns 0.** The synthesised `int main()` wrapper ignores the double value returned by the user's `main()` and always returns `0`. There is no way to return a non-zero exit code from a Pyxc program yet.

## Try It

**Emit LLVM IR and inspect it**

```bash
cat sq.pyxc
```
```python
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
define double @sq(double %x) {
entry:
  %multmp = fmul double %x, %x
  ret double %multmp
}
; ... __pyxc.global_init, main wrapper, ...
```

**Emit assembly**

```bash
pyxc --emit asm -o sq.s sq.pyxc
grep -A2 "sq:" sq.s
```

**Compile to a native binary**

```bash
# runtime.c provides printd/putchard for standalone binaries.
pyxc --emit obj -o sq.o sq.pyxc
file sq.o                        # Mach-O 64-bit object (arm64), not reachable
clang sq.o runtime.c -o sq
./sq
```
```
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
cd code/chapter-13
cmake -S . -B build && cmake --build build
./build/pyxc --emit obj -o program.o program.pyxc
clang program.o runtime.c -o program
./program
```

## What's Next

At this point Pyxc can parse, JIT-execute, and ahead-of-time compile programs with functions, control flow, and global variables. Future chapters will build on this foundation: a type system, aggregate data, and eventually a self-hosting compiler.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

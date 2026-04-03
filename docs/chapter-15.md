---
description: "Add DWARF debug info via DIBuilder and replace the fixed optimisation pass list with LLVM's standard O0–O3 pipelines."
---
# 15. Pyxc: Debug Info and the Optimisation Pipeline

## Where We Are

[Chapter 14](chapter-14.md) added `--emit exe` — Pyxc can now compile a program to a native binary in one step. What it cannot do is tell a debugger anything useful. Compile with `-g`, set a breakpoint in lldb, and the debugger sees only machine addresses. There are no source file names, no line numbers, no variable names.

This chapter adds two things:

1. **`-g`** — emits DWARF debug information: compile units, subprograms, source locations, local variables, parameters, and globals.
2. **`-O0`/`-O1`/`-O2`/`-O3`** — replaces the fixed optimisation pass list with LLVM's standard per-level pipelines, which are much richer and include inlining, LTO preparation, and interprocedural analyses at higher levels.

The two features are linked: `-g` without an explicit `-O` forces `-O0`, because optimised IR is significantly harder for a debugger to navigate.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-15
```

## Grammar

No grammar changes. Both additions are purely compiler-infrastructure concerns.

## The Design

Debug information in LLVM is metadata — side-channel nodes attached to the IR that do not affect code generation but are preserved into the final object file as DWARF sections. `DIBuilder` is the API for constructing this metadata. Each function, parameter, local variable, and global gets a corresponding descriptor node, and every emitted instruction carries a `!DILocation` annotation mapping it to a source line.

The optimisation pipeline change replaces a hand-selected list of four passes with `PassBuilder`, which knows the canonical pass ordering for each optimisation level — including interactions between passes that are easy to get wrong manually.

## New CLI Flags

```cpp
static cl::opt<bool>
    DebugInfo("g", cl::desc("Emit DWARF debug info"), cl::init(false),
              cl::cat(PyxcCategory));
```

`-g` is a boolean. It has no effect in REPL mode (there is no object file to embed DWARF in) but silently accepted so scripts can pass `-g` unconditionally.

The opt-level flag was already present from chapter 13. What changes in `ProcessCommandLine` is a single guard:

```cpp
if (DebugInfo && OptLevel.getNumOccurrences() == 0)
  OptLevel = 0;
```

`getNumOccurrences()` returns 0 if the flag was never supplied on the command line. So `-g` alone silently coerces to `-O0`; `-g -O2` leaves opt level at 2. This lets the user opt into debug-with-optimisation explicitly while protecting the common case.

## `IRBuilder<NoFolder>`: Preserving the Literal IR

The first change that touches every code path is the `Builder` declaration:

```cpp
// Before (chapter 14):
static std::unique_ptr<IRBuilder<>> Builder;

// After (chapter 15):
static std::unique_ptr<IRBuilder<NoFolder>> Builder;
```

`IRBuilder<>` uses `ConstantFolder` by default: it constant-folds arithmetic during IR construction. An expression like `1 + 2` emits `3.0` directly, skipping the `fadd` instruction entirely. This is fast and harmless for execution, but it makes debug info meaningless — there is no instruction to attach a `!DILocation` to, so the debugger sees nothing for that line.

`IRBuilder<NoFolder>` disables construction-time folding. Every arithmetic expression emits the corresponding instruction, and the optimiser (not the builder) decides what to fold and when. At `-O0` nothing is folded; at `-O2` the same folding happens as before, but it occurs in the optimisation pass rather than silently at construction time.

This is why the default optimisation level matters: without `-O`, `IRBuilder<NoFolder>` produces more instructions than `IRBuilder<>` did, but the instructions are all present in the IR and can carry debug locations.

## The Optimisation Pipeline

### Before: A Fixed Pass List

Chapter 14 populated `TheFPM` with four passes chosen to match the Kaleidoscope tutorial:

```cpp
TheFPM->addPass(InstCombinePass());
TheFPM->addPass(ReassociatePass());
TheFPM->addPass(GVNPass());
TheFPM->addPass(SimplifyCFGPass());
```

This was fine for a toy, but it has no inliner, no mem2reg for struct types, no interprocedural analysis, and no LTO preparation. The four passes are also applied in a fixed order that may not be optimal.

### After: `PassBuilder` Pipelines

Chapter 15 replaces this with LLVM's canonical pipelines. Two new managers are added:

```cpp
static std::unique_ptr<ModulePassManager>      TheMPM;   // module-level passes
static std::unique_ptr<CGSCCAnalysisManager>   TheCGAM;  // call-graph SCC analysis
static std::unique_ptr<ModuleAnalysisManager>  TheMAM;   // module-level analysis
```

`InitializeModuleAndManagers` now cross-registers all five managers and then builds the pipeline:

```cpp
PassBuilder PB;
PB.registerModuleAnalyses(*TheMAM);
PB.registerCGSCCAnalyses(*TheCGAM);
PB.registerFunctionAnalyses(*TheFAM);
PB.registerLoopAnalyses(*TheLAM);
PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);

if (OptLevel != 0) {
  auto FPM = PB.buildFunctionSimplificationPipeline(GetOptLevel(),
                                                    ThinOrFullLTOPhase::None);
  TheFPM = std::make_unique<FunctionPassManager>(std::move(FPM));
  auto MPM = PB.buildPerModuleDefaultPipeline(GetOptLevel());
  TheMPM = std::make_unique<ModulePassManager>(std::move(MPM));
}
```

`buildFunctionSimplificationPipeline` produces the per-function passes for the chosen level (analogous to the old four-pass list, but level-appropriate and correctly ordered). `buildPerModuleDefaultPipeline` adds interprocedural passes — inliner, IPSCCP, argument promotion — that operate across the whole module.

`GetOptLevel` translates the integer flag to LLVM's `OptimizationLevel` enum:

```cpp
static OptimizationLevel GetOptLevel() {
  switch (OptLevel) {
  case 0: return OptimizationLevel::O0;
  case 1: return OptimizationLevel::O1;
  case 2: return OptimizationLevel::O2;
  default: return OptimizationLevel::O3;
  }
}
```

At `-O0`, both managers are left with empty pipelines — no passes run. The literal IR from `IRBuilder<NoFolder>` reaches the backend unchanged, preserving every `alloca`, `store`, and `load` for the debugger.

Module-level optimisations are applied after all codegen in emit mode via `RunModuleOptimizations`:

```cpp
static void RunModuleOptimizations(Module *M) {
  if (!TheMPM || OptLevel == 0)
    return;
  TheMPM->run(*M, *TheMAM);
}
```

## Debug Info: Global State

Five new global variables hold the debug-info state:

```cpp
static std::unique_ptr<DIBuilder> DIB;       // the builder
static DICompileUnit *TheCU  = nullptr;      // the compilation unit
static DIFile        *TheDIFile = nullptr;   // the source file descriptor
static DIType        *DblDIType  = nullptr;  // double basic type
static DIType        *VoidDIType = nullptr;  // void type (for void returns)
static DIScope       *CurDIScope = nullptr;  // current lexical scope (function)
static unsigned      CurFunctionLine = 1;    // source line of current function
```

All pointers are null when `-g` is absent. Every helper checks `if (!DIB) return;` at the top, so the non-debug path is unchanged.

## `InitializeDebugInfo`: Per-Module Setup

Called at the end of `InitializeModuleAndManagers`, once per module:

```cpp
static void InitializeDebugInfo() {
  if (!DebugInfo) {
    DIB.reset(); TheCU = nullptr; TheDIFile = nullptr;
    DblDIType = nullptr; VoidDIType = nullptr;
    return;
  }

  DIB = std::make_unique<DIBuilder>(*TheModule);

  StringRef FileName = sys::path::filename(CurrentSourcePath);
  StringRef Dir     = sys::path::parent_path(CurrentSourcePath);
  if (Dir.empty()) Dir = ".";

  TheDIFile = DIB->createFile(FileName, Dir);
  bool IsOptimized = OptLevel != 0;
  TheCU = DIB->createCompileUnit(dwarf::DW_LANG_C, TheDIFile,
                                 "pyxc", IsOptimized, "", 0);
  DblDIType  = DIB->createBasicType("double", 64, dwarf::DW_ATE_float);
  VoidDIType = DIB->createUnspecifiedType("void");

  TheModule->addModuleFlag(Module::Warning, "Dwarf Version",
                           dwarf::DWARF_VERSION);
  TheModule->addModuleFlag(Module::Warning, "Debug Info Version",
                           DEBUG_METADATA_VERSION);
}
```

Four things happen here:

- **`DIFile`** records the source filename and directory. Every source-location reference in the DWARF will point to this file descriptor.
- **`DICompileUnit`** is the root of the DWARF tree. `DW_LANG_C` is used because Pyxc's semantics are closest to C among the enumerated languages; it affects how debuggers interpret scoping and calling conventions.
- **`DblDIType`** is a single `DIBasicType` shared by all variables and parameters. Pyxc has one type; one descriptor is sufficient.
- **Module flags** announce which DWARF and debug-metadata versions the module uses. These are mandatory for any consumer (linker, dwarfdump, lldb) to interpret the metadata correctly.

## `FinalizeDebugInfo`: Completing the Metadata

LLVM's `DIBuilder` is lazy: it accumulates work and writes the final metadata when `finalize()` is called. If `finalize()` is never called, the module has partial, inconsistent debug metadata.

`FinalizeDebugInfo` is called inside `EmitModuleToFile`, just before opening the output file:

```cpp
static void FinalizeDebugInfo() {
  if (DIB) DIB->finalize();
}
```

Calling it at the last possible moment ensures all functions and variables have been described before the metadata is sealed.

## Per-Instruction Location: `SetCurrentDebugLocation`

```cpp
static void SetCurrentDebugLocation(unsigned Line) {
  if (!DIB || !CurDIScope) return;
  Builder->SetCurrentDebugLocation(
      DILocation::get(*TheContext, Line, 1, CurDIScope));
}
```

Every instruction emitted after this call carries the `!DILocation` metadata for `Line`. The second argument (`1`) is the column; Pyxc does not track columns so it's always 1. `CurDIScope` is the containing `DISubprogram` — without a scope, `DILocation` is not valid.

`SetCurrentDebugLocation` is called at function entry (after the `DISubprogram` is created) and could in principle be called before each statement, though the current implementation sets it once per function.

## Functions: `DISubprogram`

`FunctionAST::codegen` creates a `DISubprogram` for every user-visible function (names beginning with `__pyxc.` are considered internal and skipped):

```cpp
DISubprogram *SP = nullptr;
if (DIB && TheDIFile) {
  bool IsInternal = P.getName().rfind("__pyxc.", 0) == 0;
  if (!IsInternal) {
    unsigned Line = P.getLocation().Line ? P.getLocation().Line : 1;

    SmallVector<Metadata *, 8> EltTys;
    EltTys.push_back(DblDIType);                     // return type
    for (size_t i = 0; i < P.getArgs().size(); ++i)
      EltTys.push_back(DblDIType);                   // parameter types
    auto *SubTy = DIB->createSubroutineType(
                       DIB->getOrCreateTypeArray(EltTys));

    SP = DIB->createFunction(TheDIFile, P.getName(), StringRef(),
                             TheDIFile, Line, SubTy, Line,
                             DINode::FlagZero,
                             DISubprogram::SPFlagDefinition);
    TheFunction->setSubprogram(SP);
    CurDIScope = SP;
    CurFunctionLine = Line;
  }
}
```

`createSubroutineType` takes a flat list: return type first, then parameter types. Since everything in Pyxc is `double`, all entries are `DblDIType`. `setSubprogram` attaches the descriptor to the LLVM `Function*` — without this, even if the `!DISubprogram` node exists, the LLVM verifier and DWARF emitter will not associate it with the function's machine code.

After the body is compiled, `CurDIScope` is cleared:

```cpp
CurDIScope = nullptr;
```

This ensures that instructions emitted outside a function (e.g., module-level init code) do not accidentally inherit the previous function's scope.

## Parameters and Local Variables: `EmitDebugDeclare`

```cpp
static void EmitDebugDeclare(AllocaInst *Alloca, StringRef Name,
                             unsigned Line, bool IsParam,
                             unsigned ArgNo = 0) {
  if (!DIB || !CurDIScope || !Alloca) return;

  DILocalVariable *Var = nullptr;
  if (IsParam) {
    Var = DIB->createParameterVariable(
              CurDIScope, Name, ArgNo, TheDIFile, Line, DblDIType, true);
  } else {
    Var = DIB->createAutoVariable(
              CurDIScope, Name, TheDIFile, Line, DblDIType, true);
  }

  auto *Loc = DILocation::get(*TheContext, Line, 1, CurDIScope);
  DIB->insertDeclare(Alloca, Var, DIB->createExpression(),
                     Loc, Builder->GetInsertBlock());
}
```

`createParameterVariable` and `createAutoVariable` both produce `DILocalVariable` nodes; the difference is the DWARF tag (`DW_TAG_formal_parameter` vs `DW_TAG_variable`). `ArgNo` (1-based) identifies parameter position for the `DW_TAG_formal_parameter` case.

`insertDeclare` emits the `@llvm.dbg.declare` intrinsic call. This is the LLVM mechanism that binds a live memory location (the `alloca`) to a debug variable descriptor. A debugger uses this to find where the variable lives in memory. The intrinsic is stripped during optimisation if the variable is promoted to a register — in that case `@llvm.dbg.value` takes over automatically.

`EmitDebugDeclare` is called in two places:

- `FunctionAST::codegen` — once per argument, with `IsParam = true`
- `VarStmtAST::codegen` — once per declared variable, with `IsParam = false`

## Global Variables: `EmitDebugGlobal`

```cpp
static void EmitDebugGlobal(GlobalVariable *GV, StringRef Name,
                            unsigned Line) {
  if (!DIB || !TheCU || !GV) return;

  auto *GVE = DIB->createGlobalVariableExpression(
                  TheCU, Name, Name, TheDIFile, Line, DblDIType, true);
  GV->addDebugInfo(GVE);
}
```

Global variables use a different path than locals. There is no `alloca` to declare; instead a `DIGlobalVariableExpression` is attached directly to the `GlobalVariable` IR node via `addDebugInfo`. The DWARF emitter converts this attachment into a `DW_TAG_variable` at the module level in the `.debug_info` section.

`EmitDebugGlobal` is called from `VarStmtAST::codegen` whenever a new `GlobalVariable` is defined (not declared).

## macOS: `MaybeEmitDsymBundle`

On macOS the standard linker (ld64) and LLD both follow Apple's debug-map model: DWARF is not copied into the final Mach-O executable. Instead the linker writes stab entries (`N_OSO`) that point back to the original object files. A debugger uses these entries to find and load DWARF from the objects directly.

`dsymutil` resolves this indirection: it follows the debug map, reads DWARF from the object files, and writes a self-contained `.dSYM` bundle beside the executable. Without this step, the executable is debuggable only if the original `.o` files are still on disk at their original paths.

```cpp
static void MaybeEmitDsymBundle(const string &ExePath) {
  if (!DebugInfo) return;

  Triple TT(sys::getDefaultTargetTriple());
  if (!TT.isOSDarwin()) return;

  auto Dsymutil = sys::findProgramByName("dsymutil");
  if (!Dsymutil) {
    fprintf(stderr,
            "Warning: dsymutil not found; debug info will remain in .o files\n");
    return;
  }

  std::vector<StringRef> Args = {*Dsymutil, ExePath};
  if (sys::ExecuteAndWait(*Dsymutil, Args))
    fprintf(stderr, "Warning: dsymutil failed; debug info may be missing\n");
}
```

`MaybeEmitDsymBundle` runs automatically after `LinkExecutable` whenever `-g` and `--emit exe` are both active on macOS. The result is an `out.dSYM` bundle beside the executable that lldb can load immediately.

On Linux (ELF), DWARF is linked directly into the executable and no post-processing is needed.

## What the IR Looks Like

Without `-g` — no metadata:

```llvm
define double @sq(double %x) {
entry:
  %multmp = fmul double %x, %x
  ret double %multmp
}
```

With `-g -O0` — metadata attached, alloca preserved:

```llvm
define double @sq(double %x) !dbg !7 {
entry:
  %x.addr = alloca double
  store double %x, ptr %x.addr
  call void @llvm.dbg.declare(metadata ptr %x.addr, metadata !12, ...)
  %x1 = load double, ptr %x.addr
  %multmp = fmul double %x1, %x1, !dbg !14
  ret double %multmp, !dbg !14
}

!7  = distinct !DISubprogram(name: "sq", ...)
!12 = !DILocalVariable(name: "x", arg: 1, ...)
!14 = !DILocation(line: 3, column: 1, scope: !7)
```

With `-g -O2` — debug metadata is present but `alloca` is eliminated by mem2reg:

```llvm
define double @sq(double %x) !dbg !7 {
entry:
  call void @llvm.dbg.value(metadata double %x, metadata !12, ...)
  %multmp = fmul double %x, %x, !dbg !14
  ret double %multmp, !dbg !14
}
```

The `alloca` → `dbg.declare` pair was replaced by a `dbg.value` attached to the SSA argument directly. The debug info is still correct — the debugger knows `x` lives in the register holding `%x`. The location information can degrade if the variable is kept in multiple registers across the function, which is the classic debug-at-O2 trade-off.

## Known Limitations

**Column tracking is absent.** All `!DILocation` nodes use column `1`. Breakpoints set within a line land at the beginning of the line. Adding column tracking requires plumbing column numbers through the lexer and all AST node types.

**`-g` in REPL/JIT mode is a no-op.** The JIT does not emit `.o` files, so there is no object to embed DWARF in. `-g` is accepted and ignored in that context.

**`dsymutil` must be installed on macOS.** Pyxc runs it automatically after `--emit exe -g`, but if it is absent (non-Xcode installs), debug info remains in the temporary `.o` files and is unreachable after they are cleaned up.

**Single source file per compilation.** `DICompileUnit` is created once per `InitializeModuleAndManagers` call, referencing `CurrentSourcePath`. In single-file mode this is always correct. Multi-file compilation (chapter 14's `--emit exe a.pyxc b.pyxc`) creates one module per input file and each gets its own CU — but the current implementation rebuilds the entire context between files, so each CU references only its own source.

## Try It

**Inspect debug metadata in IR**

```bash
pyxc -g --emit llvm-ir -o out.ll program.pyxc
grep -A3 'DISubprogram\|DILocalVariable\|DILocation' out.ll | head -30
```

**Step through a program in lldb**

```bash
pyxc -g --emit exe -o program program.pyxc
# on macOS: program.dSYM is created automatically
lldb program
(lldb) b main
(lldb) r
(lldb) n          # step over one source line
(lldb) p count    # inspect a global variable by name
```

**Compare O0 and O2 IR for the same function**

```bash
pyxc -g -O0 --emit llvm-ir -o at_o0.ll program.pyxc
pyxc -g -O2 --emit llvm-ir -o at_o2.ll program.pyxc
diff at_o0.ll at_o2.ll
# O0: alloca + store + load chains, dbg.declare
# O2: SSA registers, dbg.value, constants folded
```

**Verify DWARF sections in an object file**

```bash
pyxc -g --emit obj -o program.o program.pyxc
llvm-dwarfdump program.o | head -40
```

## Build and Run

```bash
cd code/chapter-15
cmake -S . -B build && cmake --build build
./build/pyxc -g --emit exe -o program program.pyxc
lldb program
```

## What's Next

Pyxc now produces debuggable, optimised native code. The language itself still has only one type (`double`) and no aggregate data. Future chapters will add a type system, and then the debug info infrastructure built here — with its typed descriptors and source-location tracking — will have real work to do.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

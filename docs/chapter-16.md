---
description: "Add DWARF debug info via DIBuilder and replace the fixed optimization pass list with LLVM's standard O0-O3 pipelines."
---
# 16. pyxc: Debug Info and the Optimization Pipeline

## What I Am Building

[Chapter 15](chapter-15.md) gave me `--emit exe`: I can compile a program to a native binary in one step. What I can't do yet is tell a debugger anything useful about it. I compile with `-g`, set a breakpoint in lldb, and the debugger sees only machine addresses. No source file name, no line numbers, no variable names.

This chapter adds two things:

1. **`-g`**: emits DWARF debug information: a compile unit, subprograms, source locations, local variables, parameters, and globals.
2. **`-O0`/`-O1`/`-O2`/`-O3`**: replaces the fixed four-pass list I've been running with LLVM's standard per-level pipelines, which are far richer, inlining, interprocedural analyses, and the rest, at the higher levels.

The two are linked: `-g` without an explicit `-O` forces `-O0`, because optimized IR is much harder for a debugger to make sense of.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-16
```

## Grammar

No grammar changes. Both additions are purely compiler-infrastructure concerns; nothing about the syntax of a pyxc program changes.

## New CLI Flags

```cpp
static cl::opt<bool> DebugInfo("g", cl::desc("Emit DWARF debug info"),
                               cl::init(false), cl::cat(PyxcCategory));
```

`-g` is a boolean. It has no effect in REPL mode, there's no object file to embed DWARF in there, but I accept it silently anyway so a script can pass `-g` unconditionally without special-casing the REPL.

The opt-level flag itself already existed. What's new in `main`'s command-line handling is one guard, right after parsing:

```cpp
if (DebugInfo && OptLevel.getNumOccurrences() == 0)
  OptLevel = 0;
```

`getNumOccurrences()` is 0 only if the flag was never supplied at all. So `-g` alone silently coerces to `-O0`, while `-g -O2` leaves the opt level at 2, letting me opt into debug-with-optimization explicitly while the common case, just `-g`, stays safe by default.

## Keeping the IR I Actually Wrote

The first change that touches every code path is the `Builder` declaration itself:

```cpp
static std::unique_ptr<IRBuilder<NoFolder>> Builder;
```

`IRBuilder<>` (what I'd been using) constant-folds arithmetic by default: `1.0 + 2.0` emits the literal `3.0` directly, no `fadd` instruction at all. That's harmless for execution, but it's fatal for debug info, there's no instruction left to attach a source location to. `IRBuilder<NoFolder>` disables that construction-time folding. Every arithmetic expression now emits its instruction, and it's the optimizer, not the builder, that decides what to fold and when. At `-O0` nothing gets folded; at `-O2` the same folding still happens, just later, as an optimization pass instead of a silent side effect of building the IR.

This is also why the new default opt level matters: without `IRBuilder<NoFolder>`, `-O0` output would already be partially folded and unrepresentative of what I actually wrote.

## Replacing the Fixed Pass List

Up through [Chapter 15](chapter-15.md), I ran a hand-picked, fixed list of four passes on every function:

```cpp
if (OptLevel != 0) {
  TheFPM->addPass(PromotePass());     // mem2reg: stack slots -> SSA regs
  TheFPM->addPass(InstCombinePass()); // peephole rewrites
  TheFPM->addPass(ReassociatePass()); // canonicalise commutative ops
  TheFPM->addPass(GVNPass());         // eliminate common sub-expressions
}
```

That was fine while pyxc had one type and no functions calling each other in interesting ways, but it has no inliner, no interprocedural analysis, and I chose the four passes and their order by hand rather than from any real pipeline. I replace it with `PassBuilder`, which knows LLVM's canonical pass ordering for each level, including interactions between passes I'd have gotten wrong by hand. Two new managers show up alongside the ones I already had:

```cpp
static std::unique_ptr<ModulePassManager> TheMPM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
```

`InitializeModuleAndManagers` cross-registers all of them, then builds the actual pipelines:

```cpp
// Cross-register so passes can access any analysis tier they need.
PassBuilder PB;
PB.registerModuleAnalyses(*TheMAM);
PB.registerCGSCCAnalyses(*TheCGAM);
PB.registerFunctionAnalyses(*TheFAM);
PB.registerLoopAnalyses(*TheLAM);
PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);

// Optimisation pipelines. With -O0 the pass managers are left empty so the
// emitted IR stays close to the direct lowering performed by the code
// generator.
if (OptLevel != 0) {
  auto FPM = PB.buildFunctionSimplificationPipeline(GetOptLevel(),
                                                    ThinOrFullLTOPhase::None);
  TheFPM = std::make_unique<FunctionPassManager>(std::move(FPM));
  auto MPM = PB.buildPerModuleDefaultPipeline(GetOptLevel());
  TheMPM = std::make_unique<ModulePassManager>(std::move(MPM));
}

InitializeDebugInfo();
```

`buildFunctionSimplificationPipeline` gives me the level-appropriate, correctly-ordered replacement for my old four-pass list. `buildPerModuleDefaultPipeline` adds passes that only make sense across the whole module, the inliner, in particular. I translate my own integer flag into LLVM's enum with a small helper:

```cpp
static OptimizationLevel GetOptLevel() {
  switch (OptLevel) {
  case 0:
    return OptimizationLevel::O0;
  case 1:
    return OptimizationLevel::O1;
  case 2:
    return OptimizationLevel::O2;
  default:
    return OptimizationLevel::O3;
  }
}
```

At `-O0` both managers stay empty, no passes run at all, and the literal IR `IRBuilder<NoFolder>` produced reaches the backend unchanged: every `alloca`, `store`, and `load` survives for the debugger to look at. Module-level optimization runs once, after codegen finishes, from a small helper I call in emit mode:

```cpp
static void RunModuleOptimizations(Module *M) {
  if (!TheMPM || OptLevel == 0)
    return;
  TheMPM->run(*M, *TheMAM);
}
```

## Debug Info: Where the State Lives

Debug information in LLVM is metadata: side-channel nodes attached to the IR that don't affect codegen themselves but get preserved into the final object file as DWARF. `DIBuilder` is the API for constructing it. I add one new state variable for the source path, plus the handful `DIBuilder` itself needs:

```cpp
static std::string CurrentSourcePath = "<stdin>";
static std::unique_ptr<DIBuilder> DIB;
static DICompileUnit *TheCU = nullptr;
static DIFile *TheDIFile = nullptr;
static DIType *DblDIType = nullptr;
static DIType *VoidDIType = nullptr;
static DIScope *CurDIScope = nullptr;
static unsigned CurFunctionLine = 1;
```

Every one of these is null, or `1` for the line, when `-g` is absent, and every helper that touches them checks for that up front, so the no-debug path is completely unaffected by any of this existing.

## Setting Up Once Per Module

```cpp
static void InitializeDebugInfo() {
  if (!DebugInfo) {
    DIB.reset();
    TheCU = nullptr;
    TheDIFile = nullptr;
    DblDIType = nullptr;
    VoidDIType = nullptr;
    return;
  }

  DIB = std::make_unique<DIBuilder>(*TheModule);

  StringRef FullPath(CurrentSourcePath);
  StringRef FileName = sys::path::filename(FullPath);
  StringRef Dir = sys::path::parent_path(FullPath);
  if (Dir.empty())
    Dir = ".";

  TheDIFile = DIB->createFile(FileName, Dir);
  bool IsOptimized = OptLevel != 0;
  TheCU = DIB->createCompileUnit(dwarf::DW_LANG_C, TheDIFile, "pyxc",
                                 IsOptimized, "", 0);
  DblDIType = DIB->createBasicType("double", 64, dwarf::DW_ATE_float);
  VoidDIType = DIB->createUnspecifiedType("void");

  TheModule->addModuleFlag(Module::Warning, "Dwarf Version",
                           dwarf::DWARF_VERSION);
  TheModule->addModuleFlag(Module::Warning, "Debug Info Version",
                           DEBUG_METADATA_VERSION);
}
```

I call it at the end of `InitializeModuleAndManagers`, so it runs exactly once per module. `DW_LANG_C` is the closest fit among the languages DWARF enumerates; pyxc doesn't have its own DWARF language code, and C's scoping and calling-convention assumptions are close enough. `DblDIType` is one shared basic-type descriptor, since pyxc still has exactly one type at this point in the tutorial: `double`. The two module flags are mandatory; without them a consumer like lldb or `llvm-dwarfdump` won't know how to interpret anything else I attach.

`DIBuilder` accumulates work lazily and doesn't actually write it until `finalize()` runs:

```cpp
static void FinalizeDebugInfo() {
  if (DIB)
    DIB->finalize();
}
```

I call this from `EmitModuleToFile`, right before I open the output file, the latest point I can call it and still be sure every function and variable has already been described.

## One Debug Location Per Function

```cpp
static void SetCurrentDebugLocation(unsigned Line) {
  if (!DIB || !CurDIScope)
    return;
  Builder->SetCurrentDebugLocation(
      DILocation::get(*TheContext, Line, 1, CurDIScope));
}
```

I call this once, right after creating each function's entry block, and every instruction I emit after that point picks up this location automatically until I change it again. I don't call it again per statement, which means every instruction in a function's body, no matter what line the actual statement is on, gets attributed to the function's own definition line. I verified this directly: compiling a two-line function with `-g` and reading the IR shows every instruction, the multiply and the return included, tagged with line 1, the `def` line, not line 2 where the `return` actually is. Line-per-statement tracking is a real gap this chapter leaves open, not just the column tracking I call out below. The second argument, `1`, is the column; I don't track columns at all yet, so it's always `1`.

## Attaching Debug Info to Each Function

`FunctionDefinitionNode::codegen` creates one for every function whose name isn't one of my own internal `__pyxc.`-prefixed helpers:

```cpp
DISubprogram *SP = nullptr;
if (DIB && TheDIFile) {
  bool IsInternal = P.getName().rfind("__pyxc.", 0) == 0;
  if (!IsInternal) {
    unsigned Line = P.getLocation().Line ? P.getLocation().Line : 1;
    SmallVector<Metadata *, 8> EltTys;
    EltTys.push_back(DblDIType);
    for (size_t i = 0; i < P.getParameters().size(); ++i)
      EltTys.push_back(DblDIType);
    auto *SubTy =
        DIB->createSubroutineType(DIB->getOrCreateTypeArray(EltTys));
    SP = DIB->createFunction(TheDIFile, P.getName(), StringRef(), TheDIFile,
                             Line, SubTy, Line, DINode::FlagZero,
                             DISubprogram::SPFlagDefinition);
    TheFunction->setSubprogram(SP);
    CurDIScope = SP;
    CurFunctionLine = Line;
  }
}
```

`createSubroutineType` wants a flat list, return type first, then each parameter's type in order. Since pyxc only has `double`, every entry is the same `DblDIType`. `setSubprogram` is the step that actually attaches this descriptor to the LLVM `Function*`, without it, even a correctly-built `DISubprogram` node wouldn't get connected to the function's machine code by the DWARF emitter. `CurDIScope` gets cleared back to `nullptr` after the body finishes, both on success and on the error path, so anything emitted outside a function, module-level init code, for instance, doesn't accidentally inherit whatever scope the previous function left behind.

## Parameters and Locals

```cpp
static void EmitDebugDeclare(AllocaInst *Alloca, StringRef Name, unsigned Line,
                             bool IsParam, unsigned ArgNo = 0) {
  if (!DIB || !CurDIScope || !Alloca)
    return;

  DIType *Ty = DblDIType
                   ? DblDIType
                   : DIB->createBasicType("double", 64, dwarf::DW_ATE_float);
  auto *Loc = DILocation::get(*TheContext, Line, 1, CurDIScope);
  DILocalVariable *Var = nullptr;
  if (IsParam) {
    Var = DIB->createParameterVariable(CurDIScope, Name, ArgNo, TheDIFile, Line,
                                       Ty, true);
  } else {
    Var = DIB->createAutoVariable(CurDIScope, Name, TheDIFile, Line, Ty, true);
  }

  DIB->insertDeclare(Alloca, Var, DIB->createExpression(), Loc,
                     Builder->GetInsertBlock());
}
```

I fall back to building a fresh `double` type descriptor if `DblDIType` somehow isn't set yet rather than assume it always is; cheap insurance against a call-ordering mistake I'd rather not debug later. `createParameterVariable` and `createAutoVariable` produce the same kind of node, `DILocalVariable`, differing only in the DWARF tag underneath (`DW_TAG_formal_parameter` versus `DW_TAG_variable`); `ArgNo`, 1-based, is what tells the parameter case its position. `insertDeclare` is what actually emits the debug-info record binding this `alloca` to that descriptor, so a debugger knows where in memory to find the variable's current value. I call this from three places: once per argument in `FunctionDefinitionNode::codegen`, once per declared variable in `VarStatementNode::codegen`, and once in `ForExpressionNode::codegen` when the loop introduces its own variable (`for var i = ...`, as opposed to reusing an existing one). All three land on the same `CurFunctionLine`, since that's the only line I'm tracking, so a `for`-loop variable's debug entry shows the function's `def` line rather than the line the `for` actually appears on.

## Globals

```cpp
static void EmitDebugGlobal(GlobalVariable *GV, StringRef Name, unsigned Line) {
  if (!DIB || !TheCU || !GV)
    return;
  DIType *Ty = DblDIType
                   ? DblDIType
                   : DIB->createBasicType("double", 64, dwarf::DW_ATE_float);
  auto *GVE = DIB->createGlobalVariableExpression(TheCU, Name, Name, TheDIFile,
                                                  Line, Ty, true);
  GV->addDebugInfo(GVE);
}
```

A global has no `alloca` to declare against, so this takes a different path: I build a `DIGlobalVariableExpression` and attach it directly to the `GlobalVariable` IR node with `addDebugInfo`. `VarStatementNode::codegen` calls this whenever it creates a brand-new module-level global, not on every assignment, just the one time the global itself comes into existence.

## macOS Needs One More Step

On macOS, the system linker doesn't copy DWARF into the final executable the way ELF linkers do. It writes debug-map stab entries that point back at the original `.o` files instead, so a debugger has to go find those object files to read any debug info at all. `dsymutil` resolves that indirection into a self-contained `.dSYM` bundle:

```cpp
static void MaybeEmitDsymBundle(const string &ExePath) {
  if (!DebugInfo)
    return;

  Triple TT(sys::getDefaultTargetTriple());
  if (!TT.isOSDarwin())
    return;

  auto Dsymutil = sys::findProgramByName("dsymutil");
  if (!Dsymutil) {
    fprintf(
        stderr,
        "Warning: dsymutil not found; debug info will remain in .o files\n");
    return;
  }

  std::vector<StringRef> Arguments;
  Arguments.push_back(*Dsymutil);
  Arguments.push_back(ExePath);
  if (sys::ExecuteAndWait(*Dsymutil, Arguments)) {
    fprintf(stderr, "Warning: dsymutil failed; debug info may be missing\n");
  }
}
```

I run this right after linking, whenever `-g` and `--emit exe` are both active. On Linux, DWARF lands directly in the executable and none of this is necessary.

## What the IR Actually Looks Like

I compiled `def sq(x):\n  return x * x` under `-g -O0` and read the real output rather than write down what I expected:

```llvm
define double @sq(double %x) !dbg !4 {
entry:
  %x1 = alloca double, align 8
  store double %x, ptr %x1, align 8, !dbg !10
    #dbg_declare(ptr %x1, !9, !DIExpression(), !10)
  %x2 = load double, ptr %x1, align 8, !dbg !10
  %x3 = load double, ptr %x1, align 8, !dbg !10
  %multmp = fmul double %x2, %x3, !dbg !10
  ret double %multmp, !dbg !10
}
```

`#dbg_declare` is LLVM's current record syntax for what used to be a `call void @llvm.dbg.declare(...)` intrinsic call; if you're reading IR from an older LLVM version you may still see the call form, they mean the same thing. Note every instruction shares `!dbg !10`, the line-1 location from `SetCurrentDebugLocation`, exactly the limitation described above.

At `-O2`, the `alloca` is gone and `#dbg_declare` becomes `#dbg_value`, tracking the SSA value directly instead of a memory location:

```llvm
define double @sq(double %x) local_unnamed_addr #0 !dbg !4 {
entry:
    #dbg_value(double %x, !9, !DIExpression(), !10)
  %multmp = fmul double %x, %x, !dbg !11
  ret double %multmp, !dbg !11
}
```

The debug info is still correct, the debugger knows `x` lives in whatever register or argument slot holds `%x`, but it can degrade if the value moves across multiple registers over the function's lifetime. That's the standard debug-at-`-O2` trade-off, and nothing pyxc-specific about it.

## Build and Run

```bash
cd code/chapter-16
cmake -S . -B build && cmake --build build
./build/pyxc -g --emit exe -o program program.pyxc
lldb program
```

## Try It

### Inspecting the metadata directly

```bash
pyxc -g --emit llvm-ir -o out.ll program.pyxc
grep -A3 'DISubprogram\|DILocalVariable\|DILocation' out.ll
```

### Verifying DWARF actually landed in the object file

```bash
pyxc -g --emit obj -o program.o program.pyxc
llvm-dwarfdump program.o
```

I ran this myself; a real compile unit, `DW_TAG_subprogram` for `sq`, decl line and all, comes back:

```text
0x0000000b: DW_TAG_compile_unit
              DW_AT_producer	("pyxc")
              DW_AT_language	(DW_LANG_C)
              DW_AT_name	("sq.pyxc")
              ...
0x0000002a:   DW_TAG_subprogram
                DW_AT_name	("sq")
                DW_AT_decl_file	("/tmp/sq.pyxc")
                DW_AT_decl_line	(1)
                ...
```

### Comparing `-O0` and `-O2` IR for the same function

```bash
pyxc -g -O0 --emit llvm-ir -o at_o0.ll program.pyxc
pyxc -g -O2 --emit llvm-ir -o at_o2.ll program.pyxc
diff at_o0.ll at_o2.ll
```

## Known Limitations

**Every instruction in a function shares one line.** `SetCurrentDebugLocation` is called once, at function entry, and never again. A multi-line function body still gets exactly one line number attached to all of it, the function's own `def` line, not wherever each statement actually is. I verified this directly rather than assume it from the code.

**No column tracking at all.** Every `!DILocation` uses column `1`. A breakpoint set within a line lands at its start.

**`-g` in REPL/JIT mode does nothing.** The JIT never produces an object file, so there's nowhere for DWARF to live. The flag is accepted and silently ignored there.

**`dsymutil` has to be installed on macOS.** I run it automatically, but if it's missing (non-Xcode installs sometimes lack it), debug info stays behind in temporary `.o` files that get cleaned up, and is effectively lost.

## What's Next

pyxc can now produce debuggable, optimized native code. The language itself still has exactly one type, `double`, and no aggregate data at all. [Chapter 17](chapter-17.md) starts changing that, and once real types exist, the typed descriptors and source-location tracking built in this chapter finally have real work to do.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

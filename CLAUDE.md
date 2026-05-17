# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Repo Is

A step-by-step compiler tutorial. Each chapter in `code/chapter-NN/` is a **complete, standalone copy** of the pyxc compiler at that stage — not a patch or diff. `pyxc.cpp` in chapter 43 is ~9,600 lines and contains the entire compiler. When making changes for chapter N, work inside `code/chapter-N/` only; never modify earlier chapters.

The matching tutorial docs live in `docs/chapter-NN.md`. `docs/chapter-00.md` is the master chapter guide. `ROADMAP.md` tracks completion status.

## Build and Test

**Build a chapter:**
```bash
cd code/chapter-43          # or whichever chapter
cmake -S . -B build
cmake --build build
```

**Run all tests for a chapter:**
```bash
cd code/chapter-43
lit test/ -j4
```

**Run a single test:**
```bash
cd code/chapter-43
lit test/module_import_cycle_exec.pyxc -v
```

**Run the compiler manually:**
```bash
# REPL
./build/pyxc

# File → LLVM IR
./build/pyxc --emit llvm-ir -o out.ll foo.pyxc

# File → object
./build/pyxc --emit obj -o foo.o foo.pyxc

# File → native executable (auto-includes import closure)
./build/pyxc --emit exe -o foo foo.pyxc

# File → run via JIT
./build/pyxc foo.pyxc
```

**Test file format** — tests use LLVM's `lit` + `FileCheck`:
```pyxc
# RUN: %pyxc --emit llvm-ir -o %t.ll %s
# RUN: FileCheck %s < %t.ll
# CHECK: define i64 @add
```
`%pyxc` and `%runtime_c` are substitution variables defined in `test/lit.cfg.py`. `Inputs/` subdirectory holds helper modules used by multi-file tests; it is excluded from the test runner.

## Compiler Architecture (`pyxc.cpp`)

The entire compiler is one file with these sections in order:

| Lines (approx) | Section |
|---|---|
| 1–111 | Command-line options (`cl::opt`) and `EmitKind` enum |
| 112–909 | **Lexer** — `gettok()`, `getNextToken()`, indent/dedent synthetic tokens, `SourceManager` |
| 910–969 | **Diagnostics** — `LogError`, `LogErrorV`, caret printing |
| 970–1,605 | **AST node classes** — one class per expression/statement form |
| 1,606–5,648 | **Parser** — recursive descent; one `Parse*` function per grammar rule |
| 5,649–7,896 | **Code generation** — `codegen()` methods on AST nodes; LLVM IR construction globals |
| 7,897–8,492 | **Top-level driver and JIT loop** — `MainLoop`, `HandleDefinition`, `HandleExtern`, etc. |
| 8,493–9,561 | **Module system** — `ResolveImportToPath`, `CollectSignaturesFromFile`, `PreloadImportedSignatures`, `CompileFileToObject`, `EmitExecutable` |
| 9,562–9,634 | **`main()`** |

## Key Global State

**Symbol tables** (all at file scope, reset between compilations):
- `FunctionProtos` — `map<string, unique_ptr<PrototypeAST>>`: all known function prototypes; `getFunction()` consults this to re-emit `declare` stubs in new modules.
- `StructTypes` — `map<string, StructTypeInfo>`: field layouts, method visibility, trait conformances.
- `Traits` — `map<string, TraitInfo>`: trait method signatures.
- `TypeAliases` — `map<string, pair<ValueType, string>>`: alias → resolved type.
- `VarScopes` — `vector<map<string, ValueType>>`: stack of local variable scopes; `BeginFunctionScope` / `EndFunctionScope` push/pop.
- `GlobalVarTypes` / `GlobalVarStructTypes` — module-level `var` declarations.
- `NamedValues` — `map<string, AllocaInst*>`: current function's named allocas.
- `LLVMStructTypes` — `map<string, StructType*>`: cached LLVM struct types; `GetOrCreateLLVMStructType` is the entry point.

**LLVM IR globals** (recreated per module via `InitializeModuleAndManagers`):
- `TheContext`, `TheModule`, `Builder` — standard LLVM IR construction triple.
- `TheJIT` — ORC LLJIT instance (created once at startup).
- `DIB`, `TheCU` — debug info builder and compile unit (only with `-g`).

**Driver state:**
- `IsRepl` — true when reading from stdin interactively; controls prompt printing, auto-print of expression results, and whether `module`/`import`/`export` are accepted.
- `EmitMode` — `EmitKind::{None, LLVMIR, ASM, OBJ, EXE}`.
- `HadError` — set by any `LogError` call; checked before emitting output.

## Pointer Type Encoding

pyxc has no C++ template system, so complex types are serialised to strings. `ptr[Point]` becomes the encoded string `"ptr:struct:Point"`. Use `EncodePointerType` / `DecodePointerType` — never construct or parse these strings by hand.

`ValueType` is an enum (`Int`, `Int8`, `Int32`, `Float64`, `Bool`, `Pointer`, `Struct`, `None`, …). When `ValueType == Pointer`, the struct name field holds the encoded pointee. When `ValueType == Struct`, it holds the struct name directly.

## Compilation Paths

**REPL path:** `main()` → `MainLoop()` → per-token dispatch → `HandleTopLevelExpression()` / `HandleDefinition()` etc. Each expression is JIT-compiled into its own module and called immediately. `InitializeModuleAndManagers()` creates a fresh module after each JIT transfer.

**File (non-EXE) path:** `main()` → `PreloadImportedSignatures()` → `FileModeLoop()` → `EmitFileMode()` or `RunFileMode()`. All top-level forms are collected into `FileTopLevelStmts`, wrapped in a synthetic `__pyxc_main`, then compiled in one shot.

**`--emit exe` path:** `EmitExecutable()` walks the import closure of all input files (`CollectImportClosure`), calls `CompileFileToObject` for each, then links with LLD.

## Module / Import System

`PreloadImportedSignatures(path)` is called before parsing any file. It uses a line-based text scan (`ExtractTopLevelImports`) to find `import` lines, then calls `CollectSignaturesFromFile` on each.

`CollectSignaturesFromFile` does a two-phase scan:
1. Opens the file, sets `SignatureScanMode = true`, collects only `export` declarations into the symbol tables, defers `import` names.
2. After phase 1, recurses into deferred imports.

Cycle detection uses `SignatureFileStates: map<string, SignatureScanState>` with `InProgress` / `Done` states. An `InProgress` hit returns immediately — the file's own exports (phase 1) are already registered.

`ResolveImportToPath` converts `app.math` → `app/math.pyxc`, probing up the directory tree from the importer's location. Results are cached in `ResolvedImportPathCache`.

`SignatureScanMode = true` gates method codegen in `ParseAggregateDefinition` — methods call `ParseMethodSignatureOnlyInClass` instead of `ParseMethodDefinitionInClass`, registering prototypes without emitting IR.

## Class and Trait Internals

`StructTypeInfo` holds field names+types, method visibility (`MethodIsPublic`), and implemented traits (`ImplTraitRefs`). Both `struct` and `class` use `StructTypeInfo`; the `IsClass` flag enables methods, constructors, and visibility checks.

`self` is **implicit** — it does not appear in pyxc source parameter lists. The parser injects it as the first `ArgInfo` (type `ptr[StructName]`) in every method prototype. Codegen passes the alloca address as the first argument on every method call.

`CurrentClassScopeName` is set by `ClassScopeGuard` during method body codegen. `CanAccessClassMember(ownerClass, isPublic)` checks it for private-access enforcement.

Trait conformance is checked in `VerifyTraitConformance` at `impl` parse time — no runtime vtable. Dispatch is a direct call to the mangled name `ClassName.MethodName`.

## Indentation / DEDENT Tokens

The lexer emits synthetic `tok_indent` / `tok_dedent` tokens using `IndentStack`. This is the only way the parser knows about block boundaries — there are no braces. `consumeNewlines()` is a parser helper that skips `tok_eol` tokens between statements inside a block.

## Known Bugs

- **Stale `CurLoc` in codegen diagnostics:** Most AST nodes use the global `CurLoc` at error time, which has already advanced past the node. `CallExprAST` is fixed (captures location at parse time). Full fix requires a `SourceLoc` field on `ExprAST` and propagation through every node constructor.
- **`extern def` ABI is trusted:** Mismatched declared vs actual C symbol signatures are not detected and cause silent ABI bugs at runtime.

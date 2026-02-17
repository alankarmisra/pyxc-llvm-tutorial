# Pyxc: Chapter 9: Debug Information

## Introduction

In this chapter, we add source-level debug information to our compiler. When you compile with `-g`, the object files will contain DWARF debug info that debuggers like `lldb` and `gdb` can use to show your source code, set breakpoints, and step through execution line by line.

## Why Debug Info Matters

Without debug info, when you look at a crash or try to step through your code, all you see is assembly:

```
(lldb) disassemble
->  0x100003f80: fadd   d0, d0, d1
    0x100003f84: ret
```

With debug info, the debugger shows you the actual source:

```
(lldb) list
1    def factorial(n):
2      return n * factorial(n - 1.0)
3
4    def main():
5      return factorial(5.0)
```

## Seeing Debug Info in Action

Let's start by demonstrating what debug info looks like before we implement it. Create `factorial.pyxc`:

```python
def factorial(n):
  return n * factorial(n - 1.0)

def main():
  return factorial(5.0)
```

Build it with debug info:

```bash
$ ./build/pyxc build factorial.pyxc --emit=obj -g -O0
Wrote factorial.o
```

Now inspect the debug information with `dwarfdump`:

```bash
$ dwarfdump --debug-info factorial.o
```

You'll see output like:

```
.debug_info contents:
0x0000000b: DW_TAG_compile_unit
              DW_AT_producer	("Pyxc Compiler")
              DW_AT_language	(DW_LANG_C)
              DW_AT_name	("factorial.pyxc")
              DW_AT_comp_dir	(".")

0x0000002a:   DW_TAG_subprogram
                DW_AT_name	("factorial")
                DW_AT_decl_file	("./factorial.pyxc")
                DW_AT_decl_line	(1)
                DW_AT_type	(0x0000005c "double")

0x00000043:   DW_TAG_subprogram
                DW_AT_name	("main")
                DW_AT_decl_file	("./factorial.pyxc")
                DW_AT_decl_line	(4)
                DW_AT_type	(0x0000005c "double")
```

The debug info includes:
- **Compile unit**: The source file name and directory
- **Subprograms**: Each function with its name and line number
- **Type information**: The `double` type
- **Line mappings**: Which machine code addresses correspond to which source lines

Check line number information:

```bash
$ dwarfdump --debug-line factorial.o
```

You'll see a line table mapping addresses to source locations:

```
Address            Line   Column File   ISA Discriminator OpIndex Flags
------------------ ------ ------ ------ --- ------------- ------- -------------
0x0000000000000000      1      0      1   0             0       0  is_stmt
0x0000000000000010      2     28      1   0             0       0  is_stmt prologue_end
0x0000000000000028      4      0      1   0             0       0  is_stmt
0x000000000000002c      5     20      1   0             0       0  is_stmt prologue_end
```

This table tells the debugger: "Address 0x0 is line 1, address 0x28 is line 4, address 0x2c is line 5, column 20."

When you eventually link this into an executable and run it in a debugger, you'll be able to:
- Set breakpoints by function name: `break factorial`
- Set breakpoints by line: `break factorial.pyxc:2`
- Step through code and see source lines
- View the call stack with function names

## Implementation Overview

To emit debug info, we need to:

1. **Track source locations** in our AST (line and column numbers)
2. **Create a DIBuilder** to generate DWARF metadata
3. **Create a compile unit** describing the source file
4. **Create debug info for each function** (DISubprogram)
5. **Emit location info** for each expression during code generation
6. **Finalize** the debug info before emitting the object file

LLVM's debug info system uses "metadata" - special IR nodes that don't affect program execution but provide information to debuggers.

## Adding the DIBuilder Include

First, add the LLVM debug info header:

```cpp
#include "llvm/IR/DIBuilder.h"
```

This provides the `DIBuilder` class, which is LLVM's API for creating debug information.

## Tracking Source Locations in the AST

Our lexer already tracks `CurLoc` (current location) as a `SourceLocation`. We need to capture this in our AST nodes.

Update `ExprAST` to store and expose location:

```cpp
class ExprAST {
  SourceLocation Loc;

public:
  ExprAST(SourceLocation Loc = CurLoc) : Loc(Loc) {}  // <-- Capture location
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
  int getLine() const { return Loc.Line; }   // <-- Add accessor
  int getCol() const { return Loc.Col; }     // <-- Add accessor
};
```

The default parameter `Loc = CurLoc` automatically captures the current source location when an AST node is created.

Update `PrototypeAST` similarly:

```cpp
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;
  SourceLocation Loc;  // <-- Add location field

public:
  PrototypeAST(SourceLocation Loc, const std::string &Name,
               std::vector<std::string> Args)
      : Name(Name), Args(std::move(Args)), Loc(Loc) {}

  Function *codegen();
  const std::string &getName() const { return Name; }
  int getLine() const { return Loc.Line; }  // <-- Add accessor
};
```

Now update `ParsePrototype()` to capture the location:

```cpp
static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogError<ProtoPtr>("Expected function name in prototype");

  SourceLocation FnLoc = CurLoc;  // <-- Capture function location
  std::string FnName = IdentifierStr;
  getNextToken();

  // ... parse arguments ...

  return std::make_unique<PrototypeAST>(FnLoc, FnName, std::move(ArgNames));
}
```

And update `ParseTopLevelExpr()`:

```cpp
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  SourceLocation FnLoc = CurLoc;  // <-- Capture location
  if (auto E = ParseExpression()) {
    auto Proto = std::make_unique<PrototypeAST>(FnLoc, "__anon_expr",
                                                std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}
```

## Debug Info Infrastructure

Add global variables and the `DebugInfo` struct after the other code generation globals:

```cpp
static bool InteractiveMode = true;
static bool BuildObjectMode = false;
static unsigned CurrentOptLevel = 0;

// Debug info support
struct DebugInfo {
  DICompileUnit *TheCU;
  DIType *DblTy;
  std::vector<DIScope *> LexicalBlocks;

  void emitLocation(ExprAST *AST);
  DIType *getDoubleTy();
} *KSDbgInfo = nullptr;

static std::unique_ptr<DIBuilder> DBuilder;
```

**Key components:**

- `DICompileUnit *TheCU`: The compile unit representing our source file
- `DIType *DblTy`: Cached type info for `double` (all our values are doubles)
- `std::vector<DIScope *> LexicalBlocks`: Stack of scopes (for nested functions, though we don't have those yet)
- `DBuilder`: The LLVM DIBuilder that creates debug metadata
- `KSDbgInfo`: Global pointer to our debug info state (null when `-g` is not used)

Implement the helper methods:

```cpp
DIType *DebugInfo::getDoubleTy() {
  if (DblTy)
    return DblTy;
  DblTy = DBuilder->createBasicType("double", 64, dwarf::DW_ATE_float);
  return DblTy;
}

void DebugInfo::emitLocation(ExprAST *AST) {
  if (!AST)
    return Builder->SetCurrentDebugLocation(DebugLoc());
  DIScope *Scope;
  if (LexicalBlocks.empty())
    Scope = TheCU;
  else
    Scope = LexicalBlocks.back();
  Builder->SetCurrentDebugLocation(
      DILocation::get(Scope->getContext(), AST->getLine(), AST->getCol(), Scope));
}
```

**`getDoubleTy()`**: Creates a DWARF basic type for `double` (64-bit floating point, `DW_ATE_float` encoding). This is cached so we only create it once.

**`emitLocation()`**: Sets the current debug location in the IR builder. When `AST` is null, it clears the location (used for function prologues). Otherwise, it creates a `DILocation` with the line and column from the AST node.

Add a helper to create function types:

```cpp
static DISubroutineType *CreateFunctionType(unsigned NumArgs) {
  SmallVector<Metadata *, 8> EltTys;
  DIType *DblTy = KSDbgInfo->getDoubleTy();

  // Add the result type.
  EltTys.push_back(DblTy);

  for (unsigned i = 0, e = NumArgs; i != e; ++i)
    EltTys.push_back(DblTy);

  return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(EltTys));
}
```

This creates a function signature in DWARF: the first element is the return type, followed by parameter types. For `def add(x, y)`, this creates the signature `double(double, double)`.

## Emitting Location Info in Expressions

Update each expression's `codegen()` to emit its source location:

```cpp
Value *NumberExprAST::codegen() {
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(this);
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  Value *V = NamedValues[Name];
  if (!V)
    return LogError<Value *>("Unknown variable name");
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(this);
  return V;
}

Value *BinaryExprAST::codegen() {
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(this);

  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  // ... rest of binary op codegen ...
}

Value *CallExprAST::codegen() {
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(this);

  Function *CalleeF = getFunction(Callee);
  // ... rest of call codegen ...
}
```

The pattern is simple: check if `KSDbgInfo` exists (i.e., `-g` was used), and if so, emit the location before generating code.

## Creating Debug Info for Functions

This is the most complex part. Update `FunctionAST::codegen()`:

```cpp
Function *FunctionAST::codegen() {
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());

  if (!TheFunction)
    return nullptr;

  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Create a subprogram DIE for this function if debug info is enabled
  DISubprogram *SP = nullptr;
  if (KSDbgInfo) {
    DIFile *Unit = DBuilder->createFile(KSDbgInfo->TheCU->getFilename(),
                                        KSDbgInfo->TheCU->getDirectory());
    DIScope *FContext = Unit;
    unsigned LineNo = P.getLine();
    unsigned ScopeLine = LineNo;
    SP = DBuilder->createFunction(
        FContext, P.getName(), StringRef(), Unit, LineNo,
        CreateFunctionType(TheFunction->arg_size()), ScopeLine,
        DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
    TheFunction->setSubprogram(SP);

    // Push the current scope
    KSDbgInfo->LexicalBlocks.push_back(SP);

    // Unset the location for the prologue emission
    KSDbgInfo->emitLocation(nullptr);
  }

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  if (Value *RetVal = Body->codegen()) {
    Builder->CreateRet(RetVal);

    // Pop off the lexical block for the function
    if (KSDbgInfo)
      KSDbgInfo->LexicalBlocks.pop_back();

    verifyFunction(*TheFunction);

    if (!BuildObjectMode) {
      TheFPM->run(*TheFunction, *TheFAM);
    }

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();

  if (KSDbgInfo)
    KSDbgInfo->LexicalBlocks.pop_back();

  return nullptr;
}
```

**Key steps:**

1. **Create a DIFile**: References the source file and directory from the compile unit
2. **Create a DISubprogram**: This is the debug info for the function itself
   - `FContext`: The lexical scope (the file)
   - `P.getName()`: Function name (e.g., "factorial")
   - `StringRef()`: Linkage name (empty for us)
   - `Unit`: The file it's defined in
   - `LineNo`: Line number where the function is defined
   - `CreateFunctionType(...)`: The function signature (double(double, double), etc.)
   - `ScopeLine`: Line where the function scope starts (same as LineNo for us)
   - `DINode::FlagPrototyped`: Indicates the function has a prototype
   - `DISubprogram::SPFlagDefinition`: This is a definition, not just a declaration
3. **Attach to the function**: `TheFunction->setSubprogram(SP)` links the LLVM function to its debug metadata
4. **Push the scope**: Add `SP` to the lexical blocks stack so expressions inside the function know their scope
5. **Emit null location**: This ensures the function prologue (setup instructions) don't have a source location. Debuggers skip past these when stepping.
6. **Pop the scope**: When the function is complete (or on error), remove it from the stack

## Initializing Debug Info in Build Mode

In the `BuildCommand` handler (in `main()`), initialize debug info right after `InitializeModuleAndManagers()`:

```cpp
InitializeModuleAndManagers();

// Initialize debug info if requested
if (BuildDebug) {
  DBuilder = std::make_unique<DIBuilder>(*TheModule);

  KSDbgInfo = new DebugInfo();
  KSDbgInfo->TheCU = DBuilder->createCompileUnit(
      dwarf::DW_LANG_C, DBuilder->createFile(BuildInputFile, "."),
      "Pyxc Compiler", CurrentOptLevel > 0, "", 0);
}

getNextToken();
MainLoop();

if (HadError)
  return 1;

// Finalize debug info
if (BuildDebug) {
  DBuilder->finalize();
}
```

**`createCompileUnit()` parameters:**

- `dwarf::DW_LANG_C`: Source language (we say "C" since DWARF doesn't have a "Pyxc" constant)
- `DBuilder->createFile(BuildInputFile, ".")`: The source file (e.g., "factorial.pyxc") and directory (".")
- `"Pyxc Compiler"`: Producer string (shows up in debug info)
- `CurrentOptLevel > 0`: Whether optimizations are enabled
- `""`: Flags (none)
- `0`: Runtime version (not applicable)

**`DBuilder->finalize()`**: This must be called after all debug info is created but before emitting the module. It finalizes the metadata and makes sure everything is consistent.

## Cleanup

After emitting the module or object file, clean up the debug info:

```cpp
if (BuildEmit == BuildEmitLLVM) {
  TheModule->print(outs(), nullptr);
  // Clean up debug info
  if (BuildDebug) {
    delete KSDbgInfo;
    KSDbgInfo = nullptr;
    DBuilder.reset();
  }
  return 0;
}

const std::string OutputPath = DeriveObjectOutputPath(BuildInputFile);
bool success = EmitObjectFile(OutputPath);

// Clean up debug info
if (BuildDebug) {
  delete KSDbgInfo;
  KSDbgInfo = nullptr;
  DBuilder.reset();
}

return success ? 0 : 1;
```

## Testing

Create a test file `test/cli_build_debug_info.pyxc`:

```python
# RUN: cp %s %t.pyxc
# RUN: %pyxc build %t.pyxc --emit=obj -g -O0 > %t.out 2>&1
# RUN: grep -q "Wrote " %t.out
# RUN: test -f %t.o
# RUN: dwarfdump --debug-info %t.o | grep -q "DW_TAG_subprogram"
# RUN: dwarfdump --debug-info %t.o | grep -q "DW_AT_name.*add"
# RUN: dwarfdump --debug-line %t.o | grep -q "\.pyxc"

def add(x, y):
  return x + y

def main():
  return add(3.0, 4.0)
```

This verifies:
1. The object file is created
2. Debug info contains subprogram entries
3. Function names appear in debug info
4. Line tables reference the .pyxc source file

Run the tests:

```bash
$ lit test/
Testing Time: 0.48s
  Total Discovered Tests: 48
  Passed: 48 (100.00%)
```

## Compile and Test

```bash
cd code/chapter09
./build.sh
```

Try it out:

```bash
$ ./build/pyxc build factorial.pyxc --emit=obj -g -O0
Wrote factorial.o

$ dwarfdump --debug-info factorial.o | grep -A5 DW_TAG_subprogram
  DW_TAG_subprogram
    DW_AT_low_pc	(0x0000000000000000)
    DW_AT_high_pc	(0x0000000000000028)
    DW_AT_name	("factorial")
    DW_AT_decl_file	("./factorial.pyxc")
    DW_AT_decl_line	(1)
```

Perfect! The object file now has complete debug information.

## What's Next?

In later chapters, when we implement:
- **Variables**: We'll add `DILocalVariable` for debugging local variables
- **Executables**: We'll be able to run programs under `lldb` and step through Pyxc source code
- **Optimizations**: We'll need to handle how aggressive optimizations can make debug info inaccurate

For now, we have the foundation: source locations, function metadata, and line number mapping. This is enough for basic source-level debugging once we can create executables.

## Summary

We added comprehensive debug info support:

1. **Source locations**: Captured line and column numbers in all AST nodes
2. **DIBuilder infrastructure**: Created `DebugInfo` struct and helpers
3. **Compile unit**: Describes the source file being compiled
4. **Function metadata**: Each function gets a `DISubprogram` with its name, line number, and type
5. **Location tracking**: Every expression emits its source location during codegen
6. **Finalization**: Debug metadata is finalized before object file emission

All controlled by the `-g` flag. When you compile with `-g`, you get rich DWARF debug information. Without it, object files are smaller and contain no debug metadata.

The pattern we established (check `if (KSDbgInfo)` before emitting debug info) keeps debug code cleanly separated and has zero runtime overhead when not debugging.

---
description: "Evolve pyxc from REPL-centric flow to a practical compiler frontend with CLI modes, diagnostics, debug options, and object/executable output paths."
---
# 17. Pyxc: From REPL-Only to a Real Compiler Pipeline

This chapter evolves `pyxc` from a REPL-centric JIT into a full compiler front-end with command-line modes, source locations, debug info, errors in color(!) and object/executable output. 

## Source Code
To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter14](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter14).

## Grammar (EBNF)

Chapter 14 still uses the pre-indentation grammar shape (newline-delimited statements).
The major chapter work is compiler plumbing (CLI, diagnostics, debug info, object/exe flow), not new syntax.

```ebnf
program      = { top_item } , eof ;
top_item     = newline | function_def | extern_decl | statement ;

function_def = [ decorator ] , "def" , prototype , ":" , expression ;
extern_decl  = "extern" , "def" , prototype ;
prototype    = identifier , "(" , [ identifier , { "," , identifier } ] , ")" ;

statement    = if_stmt | for_stmt | return_stmt | expr_stmt ;
if_stmt      = "if" , expression , ":" , expression ,
               "else" , ":" , expression ;
for_stmt     = "for" , identifier , "in" , "range" , "(" ,
               expression , "," , expression , [ "," , expression ] ,
               ")" , ":" , expression ;
return_stmt  = "return" , expression ;
expr_stmt    = expression ;
```

## CLI options and output naming

**Where:** global scope (new helpers and option definitions)

LLVM already ships a command-line parsing helper library in `llvm/Support/CommandLine.h`, so we do not need to write custom argv parsing code.

Reference: [LLVM CommandLine 2.0 Library Manual](https://llvm.org/docs/CommandLine.html)

Before writing options, start with the target UX: this is the kind of `--help` layout we are trying to achieve.

```text
OVERVIEW: Pyxc - Compiler and Interpreter
USAGE: pyxc [options] <input file>

OPTIONS:

Generic Options:
  --help      - Display available options
  --version   - Display the version of this program

Frontend Options:
  -i          - Interpret the input file immediately
  -c          - Compile to object file

Debug Options:
  -g          - Emit debug information
```

Now map that output to the LLVM helpers we use.

- To create titled help blocks like `Frontend Options` and `Debug Options`, use `cl::OptionCategory` and attach each option with `cl::cat(...)`.
Categories keep larger CLIs readable.
Yes, you can define multiple categories, and each one shows up as its own section in `--help`.

- To define a typed option, use `cl::opt<T>`:
The `T` decides what parser LLVM uses.
For simple options, `T` can be `std::string`, `int`, `unsigned`, `bool`, etc.
Example: `cl::opt<std::string>` reads a string value from the command line.

- To constrain an option to a fixed set of named choices, use an enum + `cl::values(...)`:
Define an enum first:

```cpp
enum ExecutionMode { Interpret, Executable, Object };
```

Then use it in `cl::opt<ExecutionMode>` with explicit allowed mappings:

```cpp
static cl::opt<ExecutionMode> Mode(
    cl::desc("Execution mode:"),
    cl::values(clEnumValN(Interpret, "i",
                          "Interpret the input file immediately (default)"),
               clEnumValN(Object, "c", "Compile to object file")),
    cl::init(Executable), cl::cat(PyxcCategory));
```

What each piece in `Mode(...)` does:
- `cl::desc("Execution mode:")` is help text. It appears as the heading for this option group in `--help`.
- `cl::values(...)` lists allowed enum choices for this option.
- `clEnumValN(Interpret, "i", "...")` maps the switch text `-i` to enum value `Interpret`.
- `clEnumValN(Object, "c", "...")` maps `-c` to enum value `Object`.
- The long strings like `"Interpret the input file immediately (default)"` and `"Compile to object file"` are shown in help as human-readable descriptions.
- `cl::init(Executable)` sets the default when no mode switch is passed.
- `cl::cat(PyxcCategory)` puts this option under the `Pyxc Options` help category.

- To read a bare input argument (no switch), use `cl::Positional`:
`cl::Positional` means this value is read by position, not by a flag like `-o` or `--input`.
So `InputFilename` is filled from the bare argument, like `pyxc fib.pyxc`.

- Position of positional args relative to switches:
For normal LLVM command-line parsing, options can usually appear before or after positional arguments, so `pyxc -c fib.pyxc` and `pyxc fib.pyxc -c` are both accepted.
The important rule for users in this chapter is: provide at most one bare filename argument for `<input file>`.
Why "at most one": this option is declared as `cl::opt<std::string>` with `cl::Optional`, so it accepts zero or one positional value.
A second bare argument is not accepted by this definition and will be reported as an unexpected extra argument.

- Accessing parsed values:
Once `cl::ParseCommandLineOptions(...)` runs, these globals hold the parsed values directly.
Example: `Mode` contains the selected `ExecutionMode`, and `OutputFilename` contains the `-o` value if one was provided.

```cpp
//===----------------------------------------------------------------------===//
// Command line arguments
//===----------------------------------------------------------------------===//
static cl::OptionCategory PyxcCategory("Pyxc Options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::Optional, cl::cat(PyxcCategory));

enum ExecutionMode { Interpret, Executable, Object };

static cl::opt<ExecutionMode> Mode(
    cl::desc("Execution mode:"),
    cl::values(clEnumValN(Interpret, "i",
                          "Interpret the input file immediately (default)"),
               clEnumValN(Object, "c", "Compile to object file")),
    cl::init(Executable), cl::cat(PyxcCategory));

static cl::opt<std::string> OutputFilename(
    "o",
    cl::desc("Specify output filename (optional, defaults to input basename)"),
    cl::value_desc("filename"), cl::Optional);

// Accepts -O0, -O1, -O2, -O3
static cl::opt<std::string> OptLevel(
    "O", cl::desc("Optimization level (0-3)"), cl::value_desc("level"),
    cl::init("2"), cl::Prefix, cl::cat(PyxcCategory));
```

After parsing, you can use `Mode` directly in control flow:

```cpp
if (Mode == Interpret) {
  // Run with JIT now
} else if (Mode == Object) {
  // Emit object file
} else {
  // Build executable
}
```

For output naming, we add a helper that behaves like typical compilers:
- if `-o` is provided, use it exactly
- otherwise, strip the input extension and append the requested output extension

Important behavior in executable mode:
- `-o` names the final executable
- the intermediate object file uses a temporary name (for example `myexe.tmp.o`)
- this avoids object/executable filename collisions

```cpp
// Examples:
//   input="fib.pyxc", ext=".o"  -> "fib.o"
//   input="src/fib.pyxc", ext="" -> "src/fib"
//   with -o custom.bin          -> "custom.bin" (overrides both input/ext)
std::string getOutputFilename(const std::string &input,
                              const std::string &ext) {
  if (!OutputFilename.empty())
    return OutputFilename;

  size_t lastDot = input.find_last_of('.');
  size_t lastSlash = input.find_last_of("/\\");

  std::string base;
  if (lastDot != std::string::npos &&
      (lastSlash == std::string::npos || lastDot > lastSlash)) {
    base = input.substr(0, lastDot);
  } else {
    base = input;
  }

  return base + ext;
}
```


## Colorized diagnostics (ANSI escape codes)

**Where:** global scope color helpers + `LogError()`

ANSI escape codes are small control sequences printed to terminals. They are not visible text; they tell the terminal how to render text (for example, red, bold, reset style).

In our code:
- `\x1b` is the ESC byte that starts an ANSI control sequence
- `"[31m"` means red text
- `"[1m"` means bold text
- `"[0m"` resets formatting

We only enable them when `stderr` is connected to an interactive console window (`isatty(...)`), so redirected output (for example, to a file) stays clean.

```cpp
//===----------------------------------------------------------------------===//
// Color
//===----------------------------------------------------------------------===//
bool UseColor = isatty(fileno(stderr));
const char *Red = UseColor ? "\x1b[31m" : "";
const char *Bold = UseColor ? "\x1b[1m" : "";
const char *Reset = UseColor ? "\x1b[0m" : "";
```

Then we wrap error text with those prefixes/suffixes:

```cpp
std::unique_ptr<ExprAST> LogError(const char *Str) {
  InForExpression = false;
  fprintf(stderr, "%sError (Line: %d, Column: %d): %s\n%s", Red, CurLoc.Line,
          CurLoc.Col, Str, Reset);
  return nullptr;
}
```

## Source locations in the lexer

We first define a small structure that stores source coordinates: row (`Line`) and column (`Col`).

Then we keep two location variables, not one:
- `LexLoc` is the moving cursor of the lexer as characters are consumed.
- `CurLoc` is the location we attach to the current token.

They must be separate because the lexer keeps reading ahead while building tokens. `LexLoc` keeps moving, but `CurLoc` needs to stay fixed at the token start we want to report in errors/debug info.

```cpp
struct SourceLocation {
  int Line;
  int Col;
};

static SourceLocation LexLoc = {1, 0};
// CurLoc is set when a token is lexed (CurLoc = LexLoc in gettok()).
// We do not initialize it to {1, 0} because there is no "current token" yet.
static SourceLocation CurLoc;
```

Next, we add `advance()` as the single character-reading helper for lexing.
It does two jobs together:
- reads the next byte
- updates `LexLoc` consistently

Notice it uses `getc(InputFile)` (not `getchar()`):
- `getchar()` always reads from standard input
- `getc(InputFile)` reads from whichever `FILE*` we configured

So this supports both file input and stdin with the same lexer. If we want stdin behavior, we just set `InputFile = stdin`.
After adding `advance()`, we replace direct character reads inside `gettok()` with `advance()` so location tracking always stays in sync.

```cpp
static int advance() {
  int LastChar = getc(InputFile);

  if (LastChar == '\n' || LastChar == '\r') {
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else
    LexLoc.Col++;
  return LastChar;
}
```

Now `gettok()` uses `advance()`, and snapshots `CurLoc = LexLoc` at token start:

```cpp
static int gettok() {
  static int LastChar = ' ';

  while (isspace(LastChar) && LastChar != '\n' && LastChar != '\r')
    LastChar = advance();

  CurLoc = LexLoc;

  if (LastChar == '\n' || LastChar == '\r') {
    LastChar = ' ';
    return tok_eol;
  }

  if (LastChar == '@') {
    LastChar = advance();
    return tok_decorator;
  }
  // ...
}
```

## AST nodes carry source locations (+ optional AST dumps)

Each AST node captures the location at creation time. That location is what we actually use later for diagnostics/debug info.
The `dump()` methods shown here are optional debugging helpers for manual inspection; we do not call them in this chapter's flow.

```cpp
class ExprAST {
  SourceLocation Loc;

public:
  ExprAST(SourceLocation Loc = CurLoc) : Loc(Loc) {}
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
  int getLine() const { return Loc.Line; }
  int getCol() const { return Loc.Col; }
  virtual raw_ostream &dump(raw_ostream &out, int ind) {
    return out << ':' << getLine() << ':' << getCol() << '\n';
  }
};
```

```cpp
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(SourceLocation Loc, char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : ExprAST(Loc), Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "binary" << Op, ind);
    LHS->dump(indent(out, ind) << "LHS:", ind + 1);
    RHS->dump(indent(out, ind) << "RHS:", ind + 1);
    return out;
  }
  Value *codegen() override;
};
```


## Location-aware error messages

**Where:** `LogError()`

We include line/column and add color (when available).

```cpp
std::unique_ptr<ExprAST> LogError(const char *Str) {
  InForExpression = false;
  fprintf(stderr, "%sError (Line: %d, Column: %d): %s\n%s", Red, CurLoc.Line,
          CurLoc.Col, Str, Reset);
  return nullptr;
}
```

## Capturing locations during parsing

Every time we are about to build an AST node, we first snapshot `CurLoc` into a local `SourceLocation` variable, then pass that into the AST constructor.  
Below are all location-capture points in this chapter:

```cpp
// number literal
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  SourceLocation NumLoc = CurLoc;
  auto Result = std::make_unique<NumberExprAST>(NumLoc, NumVal);
  getNextToken();
  return Result;
}

// identifier / call
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  SourceLocation LitLoc = CurLoc;

  getNextToken(); // eat identifier.

  if (CurTok != '(')
    return std::make_unique<VariableExprAST>(LitLoc, IdName);

  // Call.
  getNextToken(); // eat (
  // ...
  return std::make_unique<CallExprAST>(LitLoc, IdName, std::move(Args));
}
```

```cpp
// if statement
static std::unique_ptr<ExprAST> ParseIfExpr() {
  SourceLocation IfLoc = CurLoc;
  getNextToken(); // eat 'if'

  auto Cond = ParseExpression();
  // ...
  return std::make_unique<IfStmtAST>(IfLoc, std::move(Cond), std::move(Then),
                                     std::move(Else));
}
```

```cpp
// for statement
static std::unique_ptr<ExprAST> ParseForExpr() {
  SourceLocation ForLoc = CurLoc;
  InForExpression = true;
  getNextToken(); // eat for
  // ...
  return std::make_unique<ForStmtAST>(ForLoc, IdName, std::move(Start),
                                      std::move(End), std::move(Step),
                                      std::move(Body));
}

// var expression
static std::unique_ptr<ExprAST> ParseVarExpr() {
  SourceLocation VarLoc = CurLoc;
  getNextToken(); // eat `var`
  // ...
  return std::make_unique<VarExprAST>(VarLoc, std::move(VarNames),
                                      std::move(Body));
}
```

```cpp
// unary operator
static std::unique_ptr<ExprAST> ParseUnary() {
  int Opc = CurTok;
  SourceLocation OpLoc = CurLoc;
  getNextToken();
  if (auto Operand = ParseUnary())
    return std::make_unique<UnaryExprAST>(OpLoc, Opc, std::move(Operand));
  return nullptr;
}

// binary operator
int BinOp = CurTok;
SourceLocation BinLoc = CurLoc;
getNextToken(); // eat binop
// ...
LHS = std::make_unique<BinaryExprAST>(BinLoc, BinOp, std::move(LHS),
                                      std::move(RHS));
```

```cpp
// prototype
static std::unique_ptr<PrototypeAST> ParsePrototype(...) {
  std::string FnName;
  SourceLocation FnLoc = CurLoc;
  // ...
  return std::make_unique<PrototypeAST>(FnLoc, FnName, std::move(ArgNames),
                                        IsOperator, Precedence);
}

// top-level anonymous expression wrapper
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  SourceLocation FnLoc = CurLoc;
  // ...
  auto Proto = std::make_unique<PrototypeAST>(FnLoc, "__anon_expr",
                                              std::vector<std::string>());
  return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
}
```

**Why:** Now every AST node is tied to where it was parsed, which fuels both error messages and debug data.


## Debug info infrastructure (DIBuilder)

We add a small `DebugInfo` struct with a `DICompileUnit`, cached double type, and lexical scope stack. We then wire `emitLocation()` into codegen.

```cpp
struct DebugInfo {
  DICompileUnit *TheCU;
  DIType *DblTy;
  std::vector<DIScope *> LexicalBlocks;

  void emitLocation(ExprAST *AST);
  DIType *getDoubleTy();
};

static std::unique_ptr<DebugInfo> KSDbgInfo;
static std::unique_ptr<DIBuilder> DBuilder;

inline void emitLocation(ExprAST *AST) {
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(AST);
}
```

```cpp
void DebugInfo::emitLocation(ExprAST *AST) {
  if (!AST)
    return Builder->SetCurrentDebugLocation(DebugLoc());
  DIScope *Scope = LexicalBlocks.empty() ? TheCU : LexicalBlocks.back();
  Builder->SetCurrentDebugLocation(DILocation::get(
      Scope->getContext(), AST->getLine(), AST->getCol(), Scope));
}
```

**Why:** LLVM debug info must be emitted as metadata. This is the foundation for line-accurate debugging in tools like `lldb` or `gdb`.


## Emitting locations in codegen

**Where:** `NumberExprAST::codegen()`, `VariableExprAST::codegen()`, `BinaryExprAST::codegen()` and others

We simply call `emitLocation(this)` at the top of each codegen function.

```cpp
Value *NumberExprAST::codegen() {
  emitLocation(this);
  return ConstantFP::get(*TheContext, APFloat(Val));
}
```

```cpp
Value *BinaryExprAST::codegen() {
  emitLocation(this);
  if (Op == '=') {
    // ...
  }
  // ...
}
```

**Why:** This attaches the correct source location to each emitted instruction in the IR.


## main returns int (real executable behavior)

Up to this point, `main` was treated like every other function and returned `double`, because `double` was the only language type we had.

That is fine for JIT-only expression evaluation, but wrong for a native executable entry point. The platform expects `main` to return an integer status code. If we emit a mismatched `main` signature, executable behavior is unreliable and can fail at runtime.

So even though the language is still mostly `double`-typed, we patch codegen here:
- declare `main` as `i32` in LLVM IR
- convert the computed `double` result to `i32` right before `ret`

`i32` means a 32-bit signed integer type in LLVM IR (`i` = integer, `32` = 32 bits). This is the standard return type used by C-style `main` on most targets.

This gives us a practical milestone: we can compile and run full executables correctly now, while keeping the broader type system work for later chapters.

```cpp
Function *PrototypeAST::codegen() {
  // Special case: main function returns int, everything else returns double
  Type *RetType;
  if (Name == "main") {
    RetType = Type::getInt32Ty(*TheContext);
  } else {
    RetType = Type::getDoubleTy(*TheContext);
  }

  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT = FunctionType::get(RetType, Doubles, false);
  // ...
}
```

```cpp
if (Value *RetVal = Body->codegen()) {
  if (P.getName() == "main") {
    RetVal = Builder->CreateFPToSI(RetVal, Type::getInt32Ty(*TheContext),
                                   "mainret");
  }
  Builder->CreateRet(RetVal);
  // ...
}
```

While we hard-code `main` to return `i32` in the prototype, the expression body still computes a `double`.  
[CreateFPToSI(...)](https://llvm.org/doxygen/classllvm_1_1IRBuilderBase.html) is the cast that converts that floating-point value to a signed integer, so the function signature and the actual returned value type both match (`i32`).

## Debug subprogram and parameter metadata

We create a `DISubprogram` for the function and attach parameter info with `createParameterVariable()`.

```cpp
if (KSDbgInfo) {
  Unit = DBuilder->createFile(KSDbgInfo->TheCU->getFilename(),
                              KSDbgInfo->TheCU->getDirectory());
  DIScope *FContext = Unit;
  unsigned LineNo = P.getLine();
  unsigned ScopeLine = LineNo;
  SP = DBuilder->createFunction(
      FContext, P.getName(), StringRef(), Unit, LineNo,
      CreateFunctionType(TheFunction->arg_size()), ScopeLine,
      DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
  TheFunction->setSubprogram(SP);

  KSDbgInfo->LexicalBlocks.push_back(SP);
  emitLocation(nullptr);
}
```

```cpp
for (auto &Arg : TheFunction->args()) {
  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());
  // ...
  if (KSDbgInfo) {
    DILocalVariable *D = DBuilder->createParameterVariable(
        SP, Arg.getName(), ++ArgIdx, Unit, LineNo, KSDbgInfo->getDoubleTy(),
        true);

    DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
                            DILocation::get(SP->getContext(), LineNo, 0, SP),
                            Builder->GetInsertBlock());
  }
  // ...
}
```

## Splitting initialization (context vs passes)

**Where:** `InitializeContext()`, `InitializeOptimizationPasses()`, `InitializeModuleAndManagers()`

This makes the compiler reusable for JIT, file interpretation, and object output.

```cpp
static void InitializeContext() {
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcModule", *TheContext);
  Builder = std::make_unique<IRBuilder<>>(*TheContext);
}

static void InitializeOptimizationPasses() {
  TheFPM = std::make_unique<FunctionPassManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  // ...
  OptimizationLevel Level = OptimizationLevel::O2;
  (void)TryGetOptimizationLevel(Level);
  if (Level == OptimizationLevel::O1) {
    TheFPM->addPass(PromotePass());
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(SimplifyCFGPass());
  } else if (Level == OptimizationLevel::O2 || Level == OptimizationLevel::O3) {
    TheFPM->addPass(PromotePass());
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    TheFPM->addPass(GVNPass());
    TheFPM->addPass(SimplifyCFGPass());
  }
  // ...
}

static void InitializeModuleAndManagers() {
  InitializeContext();
  TheModule->setDataLayout(TheJIT->getDataLayout());
  InitializeOptimizationPasses();
}

static void OptimizeModuleForCodeGen(Module &M, TargetMachine *TM) {
  OptimizationLevel Level;
  if (!TryGetOptimizationLevel(Level) || Level == OptimizationLevel::O0)
    return;

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PassBuilder PB(TM);
  // register analyses...
  ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(Level);
  MPM.run(M, MAM);
}
```

**Why:** We now create LLVM state in different modes, and we support optimization levels. JIT keeps function-level passes; AOT object/executable uses the full module pipeline for the requested `-O` level.


## File parsing support (non-REPL)

**Where:** `ParseSourceFile()`

This helper consumes all tokens from a file and emits IR for each top-level construct.

```cpp
static void ParseSourceFile() {
  while (CurTok != tok_eof) {
    switch (CurTok) {
    case tok_def:
    case tok_decorator:
      if (auto FnAST = ParseDefinition()) {
        if (auto *FnIR = FnAST->codegen()) {
          if (Verbose) {
            fprintf(stderr, "Read function definition:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
          }
        }
      } else {
        getNextToken();
      }
      break;
    // ... extern, eol, top-level expr ...
    }
  }
}
```

**Why:** This is the core for compiling files instead of just running a REPL loop.


## Interpret file via JIT

**Where:** `InterpretFile()`

We open a file, parse it, and JIT execute top-level expressions.

```cpp
void InterpretFile(const std::string &filename) {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  TheJIT = ExitOnErr(PyxcJIT::Create());
  InitializeModuleAndManagers();

  InputFile = fopen(filename.c_str(), "r");
  if (!InputFile) {
    errs() << "Error: Could not open file " << filename << "\n";
    InputFile = stdin;
    return;
  }

  getNextToken();
  // ... parse like REPL, execute top-level expressions ...

  fclose(InputFile);
  InputFile = stdin;
}
```

**Why:** This enables `pyxc -i file.pyxc` to run a whole file using the JIT.


## Emit object files

**Where:** `CompileToObjectFile()`

We use the native target, set the target triple, and emit `.o` via the legacy pass manager.

```cpp
void CompileToObjectFile(const std::string &filename,
                         const std::string &explicitOutput = "") {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  InitializeContext();
  InitializeOptimizationPasses();
  ScopedFunctionOptimization DisableFunctionPasses(false);

  InputFile = fopen(filename.c_str(), "r");
  // ... parse ...

  auto TargetTriple = sys::getDefaultTargetTriple();
  TheModule->setTargetTriple(Triple(TargetTriple));

  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);
  // ... create TargetMachine ...

  std::string outputFilename =
      explicitOutput.empty() ? getOutputFilename(filename, ".o") : explicitOutput;
  raw_fd_ostream dest(outputFilename, EC, sys::fs::OF_None);

  legacy::PassManager pass;
  auto FileType = CodeGenFileType::ObjectFile;

  if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    errs() << "TargetMachine can't emit a file of this type\n";
    return;
  }

  OptimizeModuleForCodeGen(*TheModule, TargetMachine);
  pass.run(*TheModule);
  dest.flush();
}
```

**Why:** This adds a true compiler mode: `pyxc -c file.pyxc`, with configurable `-O` level and no ambiguity about where the object is written.


## Build executables by linking runtime (if any)

**Where:** `main()` (Executable case)

We compile `pyxc` source to an object file, then link with our in-process linker helper (`PyxcLinker`), passing `runtime.o` as the runtime object.

### Linker helper

For executable output, we add a small linker wrapper at `../include/PyxcLinker.h`. It uses `lld` from inside `pyxc`, so we do not need to invoke an external linker command at runtime.

Full file reference: [PyxcLinker.h](https://github.com/alankarmisra/pyxc-llvm-tutorial/blob/main/code/include/PyxcLinker.h)

The code below matches the current executable build flow in `main()`.

```cpp
case Executable: {
  std::string exeFile = getOutputFilename(InputFilename, "");
  if (Verbose)
    std::cout << "Compiling " << InputFilename
              << " to executable: " << exeFile << "\n";

  // Step 1: Compile the script to object file
  std::string scriptObj = exeFile + ".tmp.o";
  CompileToObjectFile(InputFilename, scriptObj);

  // Step 2: Link object files
  if (Verbose)
    std::cout << "Linking...\n";

  std::string runtimeObj = "runtime.o";

  // Step 3: Link using in-process lld via PyxcLinker.
  if (!PyxcLinker::Link(scriptObj, runtimeObj, exeFile)) {
    errs() << "Linking failed\n";
    return 1;
  }

  if (Verbose) {
    std::cout << "Successfully created executable: " << exeFile << "\n";
    std::cout << "Cleaning up intermediate files...\n";
    remove(scriptObj.c_str());
    // remove(runtimeObj.c_str());
  } else {
    std::cout << exeFile << "\n";
    remove(scriptObj.c_str());
    // remove(runtimeObj.c_str());
  }

  break;
}
```

In object mode, we pass the explicit object output directly:

```cpp
case Object: {
  std::string output = getOutputFilename(InputFilename, ".o");
  CompileToObjectFile(InputFilename, output);
  outs() << output << "\n";
  break;
}
```

### Runtime support (why/when)

In JIT/REPL mode, `pyxc` runs inside a host process that already contains built-in functions like `printd` and `putchard`, so the JIT can resolve them by symbol name. In executable mode, there is no host process, so those symbols must come from somewhere else.

That’s what `runtime.c` is for: a tiny C runtime that defines the built-ins. In the current code path, executable linking passes `runtime.o` to `PyxcLinker`, so you should build `runtime.c` to `runtime.o` beforehand if your program uses runtime-provided symbols.

```c
// runtime.c
#include <stdio.h>

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}
```

**Why:** This is the final step: the language now produces standalone binaries.


## REPL loop tweaks (prompts and verbosity)

**Where:** `MainLoop()` and `Handle*()` helpers

We only print IR when `-v` is set, and we move the prompt so it doesn’t interfere with blank lines.

```cpp
if (Verbose) {
  fprintf(stderr, "Read function definition:\n");
  FnIR->print(errs());
  fprintf(stderr, "\n");
}
```

```cpp
case tok_eol:
  getNextToken();
  break;

default:
  fprintf(stderr, "ready> ");
  switch (CurTok) {
  case tok_decorator:
  case tok_def:
    HandleDefinition();
    break;
  // ...
  }
  break;
```

**Why:** This makes the REPL smoother, and keeps output clean unless verbose mode is requested.


## Wrap-up

By the end of this chapter, `pyxc` is no longer just a toy REPL. It has:

- Accurate line/column tracking
- Debug metadata generation
- File-based compilation
- Object file output
- Executable linking
- Cleaner CLI and diagnostics

This is the pivot point where your language becomes a *real compiler toolchain*.

Here are some samples you can try:

Let's first try the interpreter.

```python
# fib.pyxc
extern def printd(x)

def fib(x):
    if(x < 3): 
        return 1
    else:
        return fib(x-1) + fib(x-2)

def main():
    return printd(fib(10))

main()
```

```bash
$ ./pyxc fib.pyxc -i
55.000000
```

Next we try the executable. For this, we have to remove the call to main in our script.

```python
# fib.pyxc
extern def printd(x)

def fib(x):
    if(x < 3): 
        return 1
    else:
        return fib(x-1) + fib(x-2)

def main():
    return printd(fib(10))

# main() <-- REMOVE THIS
```

We are ready to compile the file into an executable.

```bash
$./pyxc fib.pyxc
fib

$./fib
55.000000
```

Now let's try generating an object file with our fib function and call it from C++ (What?!! YES!). For this we have to remove the main function from our script entirely and add it to the C++ file.

```python
# fib.pyxc
# extern def printd(x) # <-- REMOVE THIS

def fib(x):
    if(x < 3): 
        return 1
    else:
        return fib(x-1) + fib(x-2)

## Compiling

This chapter ships with a ready-to-use build script in [`code/chapter14`](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter14).

Run:

```bash
cd code/chapter14 && ./build.sh
```

This produces `build/pyxc`. If `runtime.c` exists, the build also produces `runtime.o`.

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter14 && ./build.sh
```

Run one sample program:

```bash
code/chapter14/build/pyxc -i code/chapter14/fib.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter14/test
lit -sv .
```

Poke around the tests and tweak a few cases to see what breaks first.

When you're done, clean artifacts:

```bash
cd code/chapter14 && ./build.sh
```


## Need Help?

Stuck on something? Have questions about this chapter? Found an error?

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report bugs, errors, or problems
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- The chapter you're working on
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message or unexpected behavior
- What you've already tried

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!

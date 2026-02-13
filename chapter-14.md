# 14. Blocks, elif, Optional else, and Benchmarking

If Chapter 13 gave us indentation tokens, Chapter 14 is where we finally cash that check.

Until now, we could *lex* indentation. In this chapter, we use that structure to parse real statement blocks, support `elif`, make `else` optional, and tighten codegen so branch-heavy code doesn’t generate weird IR.

> Note from the future:
> This chapter still carries one Kaleidoscope legacy semantic: boolean/comparison-style results in parts of codegen are represented via floating values (`0.0` / `1.0`) instead of integer truth values.
> We *should* have cleaned that up around here, but we didn’t.
> We finally fixed it in Chapter 23 when we revisited signed/unsigned and truth-value correctness.
> We could pretend this was a grand long-term plan, but realistically we were lazy programmers optimizing for forward progress.

## What We’re Building

By the end of this chapter, `pyxc` supports:

- statement suites (`suite`) as real block bodies
- `if / elif / else` chains
- optional `else`
- safer control-flow codegen for early returns
- interpreter vs executable handling for `main`
- a benchmark harness against Python

Not bad for one chapter.

## Build Setup (same idea as Chapter 13)

If you built Chapter 13, this should look familiar. Chapter 14 keeps the same Makefile shape and toolchain assumptions.

From `code/chapter14/Makefile`:

```make
TARGET := pyxc
SRC := pyxc.cpp
RUNTIME_SRC := runtime.c
RUNTIME_OBJ := runtime.o

all: $(TARGET) $(RUNTIME_TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< $(LLVM_FLAGS) $(LDFLAGS) $(LLD_FLAGS) -o $@

$(RUNTIME_OBJ): $(RUNTIME_SRC)
	$(CC) $(CFLAGS) -c $< -o $@
```

Build and smoke test:

```bash
cd code/chapter14
make
./pyxc -t test/if_elif_optional.pyxc
./pyxc -i test/showcase_tools.pyxc
```

## Grammar Target (EBNF)

This is the exact syntax shape we’re aiming for. Writing it as EBNF keeps parser decisions crisp and prevents “I think this should parse” drift.

```ebnf
program         = { top_level , newline } ;
top_level       = definition | extern | expression ;

definition      = { decorator } , "def" , prototype , ":" , suite ;
extern          = "extern" , "def" , prototype ;

suite           = inline_suite | block_suite ;
inline_suite    = statement ;
block_suite     = newline , indent , statement_list , dedent ;
statement_list  = statement , { newline , statement } , [ newline ] ;

statement       = if_stmt | for_stmt | return_stmt | expr_stmt ;
if_stmt         = "if" , expression , ":" , suite ,
                  { "elif" , expression , ":" , suite } ,
                  [ "else" , ":" , suite ] ;
for_stmt        = "for" , identifier , "in" , "range" , "(" ,
                  expression , "," , expression , [ "," , expression ] , ")" ,
                  ":" , suite ;
return_stmt     = "return" , expression ;
expr_stmt       = expression ;
```

## Lexer Update: Teach It elif

Before parser work, the lexer must recognize `elif` as a keyword.

```cpp
enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  tok_def = -4,
  tok_extern = -5,
  tok_identifier = -6,
  tok_number = -7,

  tok_if = -8,
  tok_elif = -9,
  tok_else = -10,
  tok_return = -11,

  tok_for = -12,
  tok_in = -13,
  tok_range = -14,
  tok_decorator = -15,
  tok_var = -16,

  tok_indent = -17,
  tok_dedent = -18,
};

static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},   {"elif", tok_elif},     {"else", tok_else},
    {"for", tok_for}, {"in", tok_in},         {"range", tok_range},
    {"var", tok_var}};
```

Two subtle wins here:

- We get `elif` support directly in the token stream.
  Meaning: the lexer emits `tok_elif` (a dedicated token), instead of treating `elif` like a regular identifier.
- Token IDs are grouped cleanly by category, which helps while debugging parser traces.
  Meaning: when you print raw `CurTok` numbers while stepping through parser code, nearby values tend to belong to related syntax groups (`if/elif/else/return`, loop tokens, etc.). That makes parser state dumps easier to read.

## From Indentation Tokens to Real Blocks

Chapter 13 gave us `indent`/`dedent`. Chapter 14 turns those into a suite AST.

`suite` is essentially a block of statements.  
We use the name `suite` because Python grammar uses that term for:

- a single inline statement after `:`
- or a newline + indented statement list

### ParseBlockSuite()

```cpp
static std::unique_ptr<BlockSuiteAST> ParseBlockSuite() {
  auto BlockLoc = CurLoc;
  if (CurTok != tok_eol)
    return LogError<std::unique_ptr<BlockSuiteAST>>("Expected newline");

  if (getNextToken() != tok_indent)
    return LogError<std::unique_ptr<BlockSuiteAST>>("Expected indent");
  getNextToken(); // eat indent

  auto Stmts = ParseStatementList();
  if (Stmts.empty())
    return nullptr;

  getNextToken(); // eat dedent
  return std::make_unique<BlockSuiteAST>(BlockLoc, std::move(Stmts));
}
```

### ParseSuite()

```cpp
static std::unique_ptr<BlockSuiteAST> ParseSuite() {
  if (CurTok == tok_eol)
    return ParseBlockSuite();
  return ParseInlineSuite();
}
```

This is one of those deceptively small changes that unlocks half the chapter.

## if / elif / else, with Optional else

Here’s the heart of the parser work.

```cpp
static std::unique_ptr<StmtAST> ParseIfStmt() {
  SourceLocation IfLoc = CurLoc;
  if (CurTok != tok_if && CurTok != tok_elif)
    return LogError<StmtPtr>("expected `if`/`elif`");
  getNextToken();

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError<std::unique_ptr<StmtAST>>("expected `:`");
  getNextToken();

  auto Then = ParseSuite();
  if (!Then)
    return nullptr;

  std::unique_ptr<BlockSuiteAST> Else;
  if (CurTok == tok_elif) {
    auto ElseIfStmt = ParseIfStmt();
    if (!ElseIfStmt)
      return nullptr;
    std::vector<StmtPtr> ElseStmts;
    ElseStmts.push_back(std::move(ElseIfStmt));
    Else = std::make_unique<BlockSuiteAST>(IfLoc, std::move(ElseStmts));
  } else if (CurTok == tok_else) {
    getNextToken();
    if (CurTok != ':')
      return LogError<std::unique_ptr<StmtAST>>("expected `:`");
    getNextToken();
    Else = ParseSuite();
    if (!Else)
      return nullptr;
  }

  return std::make_unique<IfStmtAST>(IfLoc, std::move(Cond), std::move(Then),
                                     std::move(Else));
}
```

Also important: guardrails for stray branches.

```cpp
case tok_elif:
  return LogError<StmtPtr>("Unexpected `elif` without matching `if`");
case tok_else:
  return LogError<StmtPtr>("Unexpected `else` without matching `if`");
```

Why this shape works well:

- `elif` is represented as “`else` containing another `if`”.
- optional `else` means `Else` can be `nullptr`.
- this keeps the AST simple and codegen predictable.

## Codegen Fixes That Matter More Than They Look

If parser support expands and codegen doesn’t evolve, the compiler *seems* fine until it really isn’t.

### IfStmtAST::codegen() handles terminated branches

```cpp
bool ThenTerminated = Builder->GetInsertBlock()->getTerminator() != nullptr;
if (!ThenTerminated)
  Builder->CreateBr(MergeBB);

Value *ElseV = nullptr;
bool ElseTerminated = false;
if (Else) {
  ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;
  ElseTerminated = Builder->GetInsertBlock()->getTerminator() != nullptr;
} else {
  ElseV = ConstantFP::get(*TheContext, APFloat(0.0));
}
if (!ElseTerminated)
  Builder->CreateBr(MergeBB);

if (ThenTerminated && ElseTerminated) {
  BasicBlock *DeadCont =
      BasicBlock::Create(*TheContext, "ifcont.dead", TheFunction);
  Builder->SetInsertPoint(DeadCont);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

Here, “terminated branch” means a branch that already ends with something final like `ret`.

Bad shape (what we want to avoid):

```llvm
then:
  ret double 1.0
  br label %merge   ; invalid: jump after return
```

Good shape:

```llvm
then:
  ret double 1.0

else:
  br label %merge
```

Why:

- prevents generating jump instructions after a `return`
- avoids malformed IR in branch-heavy functions

### Return + function finalization

```cpp
Value *ReturnStmtAST::codegen() {
  Value *RetVal = Expr->codegen();
  ...
  if (ExpectedTy->isIntegerTy(32) && RetVal->getType()->isDoubleTy())
    RetVal = Builder->CreateFPToSI(RetVal, ExpectedTy, "ret_i32");
  Builder->CreateRet(RetVal);
  return RetVal;
}
```

```cpp
if (Value *RetVal = Body->codegen()) {
  if (!Builder->GetInsertBlock()->getTerminator()) {
    ...
    Builder->CreateRet(RetVal);
  }
  verifyFunction(*TheFunction);
  ...
}
```

`getTerminator()` is an LLVM API on a basic block.  
We use it to ask: “is this block already finished?”

- If yes, do not emit another `ret`/`br`.
- If no, emit the final return.

That is how we avoid duplicate final `ret` instructions, and it also keeps return typing logic in one place.

(And yes, this is one of those C++ compiler spots where one extra `CreateRet` can ruin your afternoon in under 30 seconds.)

## Interpreter vs Executable main

Chapter 14 introduces mode-aware `main` handling:

```cpp
static bool UseCMainSignature = false;
```

- `InterpretFile(...)` and `REPL()` keep it `false`.
- `CompileToObjectFile(...)` sets it `true`.

Why:

- JIT/interpreter paths want language-level function signatures.
- executable/object mode still needs native entrypoint behavior.

## Benchmarking (Python vs pyxc)

We added a benchmark suite under:

- `code/chapter14/bench/run_suite.sh`
- `code/chapter14/bench/cases/*.py`
- `code/chapter14/bench/cases/*.pyxc`

Run it:

```bash
cd code/chapter14
bench/run_suite.sh 3
```

Current averages (seconds, 2 decimals):

- `fib(41)`: Python `11.66`, `pyxc -i` `0.46`, `pyxc exe` `0.44`
- `loopsum(10000,10000)`: Python `3.39`, `pyxc -i` `0.15`, `pyxc exe` `0.10`
- `primecount(1900) x 10`: Python `1.22`, `pyxc -i` `0.17`, `pyxc exe` `0.15`

Overall case-average:

- Python: `5.42s`
- `pyxc -i`: `0.26s`
- `pyxc executable`: `0.23s`

Repo:

- [https://github.com/alankarmisra/pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial)

If something fails locally, open an issue with:

- OS/toolchain details
- exact command
- full stderr output

## Full Source Code Listing


```cpp
#include "../include/PyxcJIT.h"
#include "../include/PyxcLinker.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::orc;

//===----------------------------------------------------------------------===//
// Color
//===----------------------------------------------------------------------===//
bool UseColor = isatty(fileno(stderr));
const char *Red = UseColor ? "\x1b[31m" : "";
const char *Bold = UseColor ? "\x1b[1m" : "";
const char *Reset = UseColor ? "\x1b[0m" : "";

//===----------------------------------------------------------------------===//
// Command line arguments
//===----------------------------------------------------------------------===//
// Create a category for your options
static cl::OptionCategory PyxcCategory("Pyxc Options");

// Positional input file (optional)
static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::Optional, cl::cat(PyxcCategory));

// Execution mode enum
enum ExecutionMode { Interpret, Executable, Object, Tokens };

static cl::opt<ExecutionMode> Mode(
    cl::desc("Execution mode:"),
    cl::values(clEnumValN(Interpret, "i",
                          "Interpret the input file immediately (default)"),
               clEnumValN(Object, "c", "Compile to object file"),
               clEnumValN(Tokens, "t", "Print tokens")),
    cl::init(Executable), cl::cat(PyxcCategory));

static cl::opt<std::string> OutputFilename(
    "o",
    cl::desc("Specify output filename (optional, defaults to input basename)"),
    cl::value_desc("filename"), cl::Optional);

static cl::opt<bool> Verbose("v", cl::desc("Enable verbose output"));

static cl::opt<bool> EmitDebug("g", cl::desc("Emit debug information"),
                               cl::init(false), cl::cat(PyxcCategory));

std::string getOutputFilename(const std::string &input,
                              const std::string &ext) {
  if (!OutputFilename.empty())
    return OutputFilename;

  // Strip extension from input and add new extension
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

//===----------------------------------------------------------------------===//
// I/O
//===----------------------------------------------------------------------===//
static FILE *InputFile = stdin;
static bool UseCMainSignature = false;

//===----------------------------------------------------------------------===//
// Error reporting convenience types
//===----------------------------------------------------------------------===//
namespace {
class ExprAST;
class StmtAST;
class PrototypeAST;
class FunctionAST;
} // namespace

using ExprPtr = std::unique_ptr<ExprAST>;
using StmtPtr = std::unique_ptr<StmtAST>;
using ProtoPtr = std::unique_ptr<PrototypeAST>;
using FuncPtr = std::unique_ptr<FunctionAST>;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  // commands
  tok_def = -4,
  tok_extern = -5,

  // primary
  tok_identifier = -6,
  tok_number = -7,

  // control
  tok_if = -8,
  tok_elif = -9,
  tok_else = -10,
  tok_return = -11,

  // loop
  tok_for = -12,
  tok_in = -13,
  tok_range = -14,

  // decorator
  tok_decorator = -15,

  // var definition
  tok_var = -16,

  // indentation
  tok_indent = -17,
  tok_dedent = -18,
};

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},   {"elif", tok_elif},     {"else", tok_else},
    {"for", tok_for}, {"in", tok_in},         {"range", tok_range},
    {"var", tok_var}};

enum OperatorType { Undefined, Unary, Binary };

static constexpr int DEFAULT_BINARY_PRECEDENCE = 30;

static std::map<std::string, OperatorType> Decorators = {
    {"unary", OperatorType::Unary}, {"binary", OperatorType::Binary}};

struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

// Indentation related variables
static int ModuleIndentType = -1;
static bool AtStartOfLine = true;
static std::vector<int> Indents = {0};
static std::deque<int> PendingTokens;
static int LastIndentWidth = 0;

static int advance() {
  int LastChar = getc(InputFile);

  if (LastChar == '\n' || LastChar == '\r') {
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else
    LexLoc.Col++;
  return LastChar;
}

namespace {
class ExprAST;
}

/// LogError* - These are little helper functions for error handling.
static int CurTok;
template <typename T = void> T LogError(const char *Str) {
  // print CurTok with the error instead of two separate lines.
  fprintf(stderr, "%sError (Line: %d, Column: %d): %s\nCurTok = %d\n%s", Red,
          CurLoc.Line, CurLoc.Col, Str, CurTok, Reset);

  if constexpr (std::is_pointer_v<T>)
    return nullptr;
  else
    return T{};
}

/// countIndent - count the indent in terms of spaces
// LastChar is the current unconsumed character at the start of the line.
// LexLoc.Col already reflects that character’s column (0-based, after
// reading it), so for tabs we advance to the next tab stop using
// (LexLoc.Col % 8).
static int countLeadingWhitespace(int &LastChar) {
  //   fprintf(stderr, "countLeadingWhitespace(%d, %d)", LexLoc.Line,
  //   LexLoc.Col);

  int indentCount = 0;
  bool didSetIndent = false;

  while (true) {
    while (LastChar == ' ' || LastChar == '\t') {
      if (ModuleIndentType == -1) {
        didSetIndent = true;
        ModuleIndentType = LastChar;
      } else {
        if (LastChar != ModuleIndentType) {
          LogError<ExprPtr>("You cannot mix tabs and spaces.");
          return -1;
        }
      }
      indentCount += LastChar == '\t' ? 8 - (LexLoc.Col % 8) : 1;
      LastChar = advance();
    }

    if (LastChar == '\r' || LastChar == '\n') { // encountered a blank line
      //   PendingTokens.push_back(tok_eol);
      if (didSetIndent) {
        didSetIndent = false;
        indentCount = 0;
        ModuleIndentType = -1;
      }

      LastChar = advance(); // eat the newline
      continue;
    }

    break;
  }
  //   fprintf(stderr, " = %d | AtStartOfLine = %s\n", indentCount,
  //           AtStartOfLine ? "true" : "false");
  return indentCount;
}

static bool IsIndent(int leadingWhitespace) {
  assert(!Indents.empty());
  assert(leadingWhitespace >= 0);
  //   fprintf(stderr, "IsIndent(%d) = (%d)\n", leadingWhitespace,
  //           leadingWhitespace > Indents.back());
  return leadingWhitespace > Indents.back();
}

static int HandleIndent(int leadingWhitespace) {
  assert(!Indents.empty());
  assert(leadingWhitespace >= 0);

  LastIndentWidth = leadingWhitespace;
  Indents.push_back(leadingWhitespace);
  return tok_indent;
}

static int DrainIndents() {
  int dedents = 0;
  while (Indents.size() > 1) {
    Indents.pop_back();
    dedents++;
  }

  if (dedents > 0) {
    while (dedents-- > 1) {
      PendingTokens.push_back(tok_dedent);
    }
    return tok_dedent;
  }

  return tok_eof;
}

static int HandleDedent(int leadingWhitespace) {
  assert(!Indents.empty());
  assert(leadingWhitespace >= 0);
  assert(leadingWhitespace < Indents.back());

  int dedents = 0;

  while (leadingWhitespace < Indents.back()) {
    Indents.pop_back();
    dedents++;
  }

  if (leadingWhitespace != Indents.back()) {
    LogError("Expected indentation.");
    Indents = {0};
    PendingTokens.clear();
    return tok_error;
  }

  if (!dedents) // this should never happen
  {
    LogError("Internal error.");
    return tok_error;
  }

  //   fprintf(stderr, "Pushing %d dedents for whitespace %d on %d, %d\n",
  //   dedents,
  //           leadingWhitespace, LexLoc.Line, LexLoc.Col);
  while (dedents-- > 1) {
    PendingTokens.push_back(tok_dedent);
  }
  return tok_dedent;
}

static bool IsDedent(int leadingWhitespace) {
  assert(!Indents.empty());
  //   fprintf(stderr, "Return %s for IsDedent(%d), Indents.back = %d\n",
  //           (leadingWhitespace < Indents.back()) ? "true" : "false",
  //           leadingWhitespace, Indents.back());
  return leadingWhitespace < Indents.back();
}

/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = '\0';

  if (LastChar == '\0')
    LastChar = advance();

  if (!PendingTokens.empty()) {
    int tok = PendingTokens.front();
    PendingTokens.pop_front();
    return tok;
  }

  if (AtStartOfLine) {
    int leadingWhitespace = countLeadingWhitespace(LastChar);
    if (leadingWhitespace < 0)
      return tok_error;

    AtStartOfLine = false;
    if (IsIndent(leadingWhitespace)) {
      return HandleIndent(leadingWhitespace);
    }
    if (IsDedent(leadingWhitespace)) {
      //   fprintf(stderr, "Pushing dedent on row:%d, col:%d\n", LexLoc.Line,
      //           LexLoc.Col);
      return HandleDedent(leadingWhitespace);
    }
  }

  // Skip whitespace EXCEPT newlines (this will take care of spaces
  // mid-expressions)
  while (isspace(LastChar) && LastChar != '\n' && LastChar != '\r')
    LastChar = advance();

  CurLoc = LexLoc;

  // Return end-of-line token
  if (LastChar == '\n' || LastChar == '\r') {
    // Reset LastChar to a space instead of reading the next character.
    // If we called advance() here, it would block waiting for input,
    // requiring the user to press Enter twice in the REPL.
    // Setting LastChar = ' ' avoids this blocking read.
    LastChar = '\0';
    AtStartOfLine = true; // Modify state only when you're emitting the token.
    return tok_eol;
  }

  if (LastChar == '@') {
    LastChar = advance(); // consume '@'
    return tok_decorator;
  }

  if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())))
      IdentifierStr += LastChar;

    // Is this a known keyword? If yes, return that.
    // Else it is an ordinary identifier.
    const auto it = Keywords.find(IdentifierStr);
    return (it != Keywords.end()) ? it->second : tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return tok_eol;
  }

  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF) {
    return DrainIndents();
  }

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

static const char *TokenName(int Tok) {
  switch (Tok) {
  case tok_eof:
    return "<eof>";
  case tok_eol:
    return "<eol>";
  case tok_indent:
    return "<indent>";
  case tok_dedent:
    return "<dedent>";
  case tok_error:
    return "<error>";
  case tok_def:
    return "<def>";
  case tok_extern:
    return "<extern>";
  case tok_identifier:
    return "<identifier>";
  case tok_number:
    return "<number>";
  case tok_if:
    return "<if>";
  case tok_elif:
    return "<elif>";
  case tok_else:
    return "<else>";
  case tok_return:
    return "<return>";
  case tok_for:
    return "<for>";
  case tok_in:
    return "<in>";
  case tok_range:
    return "<range>";
  case tok_decorator:
    return "<decorator>";
  case tok_var:
    return "<var>";
  default:
    return nullptr;
  }
}

static void PrintTokens(const std::string &filename) {
  // Open input file
  InputFile = fopen(filename.c_str(), "r");
  if (!InputFile) {
    errs() << "Error: Could not open file " << filename << "\n";
    InputFile = stdin;
    return;
  }

  int Tok = gettok();
  bool FirstOnLine = true;

  while (Tok != tok_eof) {
    if (Tok == tok_eol) {
      fprintf(stderr, "<eol>\n");
      FirstOnLine = true;
      Tok = gettok();
      continue;
    }

    if (!FirstOnLine)
      fprintf(stderr, " ");
    FirstOnLine = false;

    if (Tok == tok_indent) {
      fprintf(stderr, "<indent=%d>", LastIndentWidth);
    } else {
      const char *Name = TokenName(Tok);
      if (Name)
        fprintf(stderr, "%s", Name);
      else if (isascii(Tok))
        fprintf(stderr, "<%c>", Tok);
      else
        fprintf(stderr, "<tok=%d>", Tok);
    }

    Tok = gettok();
  }

  if (!FirstOnLine)
    fprintf(stderr, " ");
  fprintf(stderr, "<eof>\n");
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//

namespace {

raw_ostream &indent(raw_ostream &O, int size) {
  return O << std::string(size, ' ');
}

/// ExprAST - Base class for all expression nodes.
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

/// StmtAST - Base class for all statement nodes.
class StmtAST {
  SourceLocation Loc;

public:
  StmtAST(SourceLocation Loc = CurLoc) : Loc(Loc) {}
  virtual ~StmtAST() = default;
  virtual Value *codegen() = 0;
  virtual bool isTerminator() { return false; }
  int getLine() const { return Loc.Line; }
  int getCol() const { return Loc.Col; }
  virtual raw_ostream &dump(raw_ostream &out, int ind) {
    return out << ':' << getLine() << ':' << getCol() << '\n';
  }
};

class ExprStmtAST : public StmtAST {
  std::unique_ptr<ExprAST> Expr;

public:
  ExprStmtAST(SourceLocation Loc, std::unique_ptr<ExprAST> Expr)
      : StmtAST(Loc), Expr(std::move(Expr)) {}

  Value *codegen() override {
    // Evaluate expression for side effects / value, then discard result.
    return Expr ? Expr->codegen() : nullptr;
  }

  raw_ostream &dump(raw_ostream &out, int ind) override {
    out << std::string(ind, ' ') << "exprstmt";
    StmtAST::dump(out, ind);
    if (Expr)
      Expr->dump(out, ind + 2);
    return out;
  }
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  raw_ostream &dump(raw_ostream &out, int ind) override {
    return ExprAST::dump(out << Val, ind);
  }
  Value *codegen() override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(SourceLocation Loc, const std::string &Name)
      : ExprAST(Loc), Name(Name) {}

  raw_ostream &dump(raw_ostream &out, int ind) override {
    return ExprAST::dump(out << Name, ind);
  }
  const std::string &getName() const { return Name; }
  Value *codegen() override;
};

/// UnaryExprAST - Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
  char Opcode;
  std::unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
      : Opcode(Opcode), Operand(std::move(Operand)) {}
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "unary" << Opcode, ind);
    Operand->dump(out, ind + 1);
    return out;
  }
  Value *codegen() override;
};

/// BinaryExprAST - Expression class for a binary operator.
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

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  CallExprAST(SourceLocation Loc, const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : ExprAST(Loc), Callee(Callee), Args(std::move(Args)) {}
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "call " << Callee, ind);
    for (const auto &Arg : Args)
      Arg->dump(indent(out, ind + 1), ind + 1);
    return out;
  }
  Value *codegen() override;
};

/// ReturnStmtAST - Return statements
class ReturnStmtAST : public StmtAST {
  std::unique_ptr<ExprAST> Expr;

public:
  ReturnStmtAST(SourceLocation Loc, std::unique_ptr<ExprAST> Expr)
      : StmtAST(Loc), Expr(std::move(Expr)) {}

  Value *codegen() override;

  bool isTerminator() override { return true; }

  raw_ostream &dump(raw_ostream &out, int ind) override {
    out << std::string(ind, ' ') << "return";
    StmtAST::dump(out, ind);
    if (Expr)
      Expr->dump(out, ind + 2);
    return out;
  }
};

/// SuiteAST - a suite ie a single or multi statement block
class BlockSuiteAST : public StmtAST {
  std::vector<std::unique_ptr<StmtAST>> Stmts;

public:
  BlockSuiteAST(SourceLocation Loc, std::vector<std::unique_ptr<StmtAST>> Stmts)
      : StmtAST(Loc), Stmts(std::move(Stmts)) {}

  Value *codegen() override;

  raw_ostream &dump(raw_ostream &out, int ind) override {
    out << std::string(ind, ' ') << "block";
    StmtAST::dump(out, ind);
    for (auto &S : Stmts)
      S->dump(out, ind + 2);
    return out;
  }
};

/// IfStmtAST - Expression class for if/else.
class IfStmtAST : public StmtAST {
  std::unique_ptr<ExprAST> Cond;
  std::unique_ptr<BlockSuiteAST> Then, Else;

public:
  IfStmtAST(SourceLocation Loc, std::unique_ptr<ExprAST> Cond,
            std::unique_ptr<BlockSuiteAST> Then,
            std::unique_ptr<BlockSuiteAST> Else)
      : StmtAST(Loc), Cond(std::move(Cond)), Then(std::move(Then)),
        Else(std::move(Else)) {}

  raw_ostream &dump(raw_ostream &out, int ind) override {
    StmtAST::dump(out << "if", ind);
    Cond->dump(indent(out, ind) << "Cond:", ind + 1);
    Then->dump(indent(out, ind) << "Then:", ind + 1);
    if (Else)
      Else->dump(indent(out, ind) << "Else:", ind + 1);
    return out;
  }
  Value *codegen() override;
};

/// ForExprAST - Expression class for for/in.
class ForStmtAST : public StmtAST {
  std::string VarName;
  std::unique_ptr<ExprAST> Start, End, Step;
  std::unique_ptr<BlockSuiteAST> Body;

public:
  ForStmtAST(std::string VarName, std::unique_ptr<ExprAST> Start,
             std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
             std::unique_ptr<BlockSuiteAST> Body)
      : VarName(std::move(VarName)), Start(std::move(Start)),
        End(std::move(End)), Step(std::move(Step)), Body(std::move(Body)) {}

  raw_ostream &dump(raw_ostream &out, int ind) override {
    StmtAST::dump(out << "for", ind);
    Start->dump(indent(out, ind) << "Cond:", ind + 1);
    End->dump(indent(out, ind) << "End:", ind + 1);
    Step->dump(indent(out, ind) << "Step:", ind + 1);
    Body->dump(indent(out, ind) << "Body:", ind + 1);
    return out;
  }

  Value *codegen() override;
};

/// VarExprAST - Expression class for var/in
class VarExprAST : public ExprAST {
  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
  std::unique_ptr<ExprAST> Body;

public:
  VarExprAST(
      std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
      std::unique_ptr<ExprAST> Body)
      : VarNames(std::move(VarNames)), Body(std::move(Body)) {}
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "var", ind);
    for (const auto &NamedVar : VarNames)
      NamedVar.second->dump(indent(out, ind) << NamedVar.first << ':', ind + 1);
    Body->dump(indent(out, ind) << "Body:", ind + 1);
    return out;
  }

  Value *codegen() override;
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its argument names as well as if it is an operator.
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;
  bool IsOperator;
  unsigned Precedence; // Precedence if a binary op.
  int Line;

public:
  PrototypeAST(SourceLocation Loc, const std::string &Name,
               std::vector<std::string> Args, bool IsOperator = false,
               unsigned Prec = 0)
      : Name(Name), Args(std::move(Args)), IsOperator(IsOperator),
        Precedence(Prec), Line(Loc.Line) {}
  Function *codegen();
  const std::string &getName() const { return Name; }

  bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  char getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    return Name[Name.size() - 1];
  }

  unsigned getBinaryPrecedence() const { return Precedence; }
  int getLine() const { return Line; }
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<BlockSuiteAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<BlockSuiteAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
  raw_ostream &dump(raw_ostream &out, int ind) {
    indent(out, ind) << "FunctionAST\n";
    ++ind;
    indent(out, ind) << "Body:";
    return Body ? Body->dump(out, ind) : out << "null\n";
  }
  Function *codegen();
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
// static int CurTok;
static int getNextToken() { return CurTok = gettok(); }
// Tracks all previously defined function prototypes
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence = {
    {'=', 2}, {'<', 10}, {'+', 20}, {'-', 20}, {'*', 40}};

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<BlockSuiteAST> ParseSuite();
static std::unique_ptr<BlockSuiteAST> ParseBlockSuite();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != ')')
    return LogError<ExprPtr>("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  SourceLocation LitLoc = CurLoc;

  getNextToken(); // eat identifier.

  if (CurTok != '(') // Simple variable ref.
    return std::make_unique<VariableExprAST>(LitLoc, IdName);

  // Call.
  getNextToken(); // eat (
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError<ExprPtr>("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return std::make_unique<CallExprAST>(LitLoc, IdName, std::move(Args));
}

/*
 * In later chapters, we will need to check the indentation
 * whenever we eat new lines.
 */
static bool EatNewLines() {
  bool consumedNewLine = CurTok == tok_eol;
  while (CurTok == tok_eol)
    getNextToken();
  return consumedNewLine;
}

// if_stmt        = "if" , expression , ":" , suite ,
//                  { "elif" , expression , ":" , suite } ,
//                  [ "else" , ":" , suite ] ;
static std::unique_ptr<StmtAST> ParseIfStmt() {
  SourceLocation IfLoc = CurLoc;
  if (CurTok != tok_if && CurTok != tok_elif)
    return LogError<StmtPtr>("expected `if`/`elif`");
  getNextToken(); // eat 'if' or 'elif'

  // condition
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError<std::unique_ptr<StmtAST>>("expected `:`");
  getNextToken(); // eat ':'

  auto Then = ParseSuite();
  if (!Then)
    return nullptr;

  std::unique_ptr<BlockSuiteAST> Else;
  if (CurTok == tok_elif) {
    auto ElseIfStmt = ParseIfStmt();
    if (!ElseIfStmt)
      return nullptr;
    std::vector<StmtPtr> ElseStmts;
    ElseStmts.push_back(std::move(ElseIfStmt));
    Else = std::make_unique<BlockSuiteAST>(IfLoc, std::move(ElseStmts));
  } else if (CurTok == tok_else) {
    getNextToken(); // eat 'else'

    if (CurTok != ':')
      return LogError<std::unique_ptr<StmtAST>>("expected `:`");
    getNextToken(); // eat ':'

    Else = ParseSuite();
    if (!Else)
      return nullptr;
  }

  // TODO: Check indent levels and check for consistent newlines usage.

  return std::make_unique<IfStmtAST>(IfLoc, std::move(Cond), std::move(Then),
                                     std::move(Else));
}

// for_stmt       = "for" , identifier , "in" , "range" , "(" ,
//                  expression , "," , expression ,
//                  [ "," , expression ] ,
//                  ")" , ":" , suite ;
static std::unique_ptr<StmtAST> ParseForStmt() {
  getNextToken(); // eat for

  if (CurTok != tok_identifier)
    return LogError<StmtPtr>("Expected identifier after for");

  std::string IdName = IdentifierStr;
  getNextToken(); // eat identifier

  if (CurTok != tok_in)
    return LogError<StmtPtr>("Expected `in` after identifier in for");
  getNextToken(); // eat 'in'

  if (CurTok != tok_range)
    return LogError<StmtPtr>("Expected `range` after identifier in for");
  getNextToken(); // eat range

  if (CurTok != '(')
    return LogError<StmtPtr>("Expected `(` after `range` in for");
  getNextToken(); // eat '('

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;

  if (CurTok != ',')
    return LogError<StmtPtr>("expected `,` after range start");
  getNextToken(); // eat ','

  auto End = ParseExpression();
  if (!End)
    return nullptr;
  std::unique_ptr<ExprAST> Step;
  if (CurTok == ',') {
    getNextToken(); // eat ,
    Step = ParseExpression();
    if (!Step)
      return nullptr;
  }

  if (CurTok != ')')
    return LogError<StmtPtr>("expected `)` after range operator");
  getNextToken(); // eat `)`

  if (CurTok != ':')
    return LogError<StmtPtr>("expected `:` after range operator");
  getNextToken(); // eat `:`

  auto Body = ParseSuite();
  if (!Body)
    return nullptr;

  return std::make_unique<ForStmtAST>(IdName, std::move(Start), std::move(End),
                                      std::move(Step), std::move(Body));
}

/// varexpr ::= 'var' identifier ('=' expression)?
//                    (',' identifier ('=' expression)?)* 'in' expression
static std::unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat `var`
  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

  // At least one variable name is required.
  if (CurTok != tok_identifier)
    return LogError<ExprPtr>("expected identifier after var");

  while (true) {
    std::string Name = IdentifierStr;
    getNextToken(); // eat identifier.

    // Read the optional initializer.
    std::unique_ptr<ExprAST> Init;
    if (CurTok == '=') {
      getNextToken(); // eat the '='.

      Init = ParseExpression();
      if (!Init)
        return nullptr;
    }

    VarNames.push_back(std::make_pair(Name, std::move(Init)));

    // End of var list, exit loop.
    if (CurTok != ',')
      break;
    getNextToken(); // eat the ','.

    if (CurTok != tok_identifier)
      return LogError<ExprPtr>("expected identifier list after var");
  }

  // At this point, we have to have 'in'.
  if (CurTok != tok_in)
    return LogError<ExprPtr>("expected 'in' keyword after 'var'");
  getNextToken(); // eat 'in'.

  EatNewLines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

// expr_stmt      = expression ;
static std::unique_ptr<ExprStmtAST> ParseExprStmt() {
  auto ExprLoc = CurLoc;
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  return std::make_unique<ExprStmtAST>(ExprLoc, std::move(Expr));
}

static std::unique_ptr<ReturnStmtAST> ParseReturnStmt() {
  auto ReturnLoc = CurLoc;
  getNextToken(); // eat `return`
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  return std::make_unique<ReturnStmtAST>(ReturnLoc, std::move(Expr));
}

// statement      = if_stmt
//                | for_stmt
//                | return_stmt
//                | expr_stmt ;
static std::unique_ptr<StmtAST> ParseStmt() {
  // This should parse statements
  switch (CurTok) {
  case tok_if:
    return ParseIfStmt();
  case tok_elif:
    return LogError<StmtPtr>("Unexpected `elif` without matching `if`");
  case tok_else:
    return LogError<StmtPtr>("Unexpected `else` without matching `if`");
  case tok_for:
    return ParseForStmt();
  case tok_return:
    return ParseReturnStmt();
  default:
    return ParseExprStmt();
  }
}

/// statement_list = statement , { newline , statement } , [ newline ] ;
static std::vector<std::unique_ptr<StmtAST>> ParseStatementList() {
  std::vector<std::unique_ptr<StmtAST>> Stmts;
  while (CurTok != tok_dedent && CurTok != tok_eof) {
    EatNewLines();
    if (CurTok == tok_dedent || CurTok == tok_eof)
      break;
    auto Stmt = ParseStmt();
    if (!Stmt)
      return {}; // Error: return empty to signal failure
    Stmts.push_back(std::move(Stmt));
  }
  return Stmts;
}

// inline_suite = statement;
static std::unique_ptr<BlockSuiteAST> ParseInlineSuite() {
  auto BlockLoc = CurLoc;
  auto Stmt = ParseStmt();
  if (!Stmt)
    return nullptr;

  std::vector<StmtPtr> Stmts;
  Stmts.push_back(std::move(Stmt));
  return std::make_unique<BlockSuiteAST>(BlockLoc, std::move(Stmts));
}

/// block_suite    = newline , indent , statement_list , dedent ;
static std::unique_ptr<BlockSuiteAST> ParseBlockSuite() {
  auto BlockLoc = CurLoc;
  // check if newline or error
  if (CurTok != tok_eol) {
    return LogError<std::unique_ptr<BlockSuiteAST>>("Expected newline");
  }

  // check if indent or error
  if (getNextToken() != tok_indent) {
    return LogError<std::unique_ptr<BlockSuiteAST>>("Expected indent");
  }
  getNextToken(); // eat indent

  auto Stmts = ParseStatementList();
  if (Stmts.empty())
    return nullptr;
  getNextToken(); // eat dedent
  return std::make_unique<BlockSuiteAST>(BlockLoc, std::move(Stmts));
}

// suite          = inline_suite
//                | block_suite ;
static std::unique_ptr<BlockSuiteAST> ParseSuite() {
  if (CurTok == tok_eol) {
    return ParseBlockSuite();
  } else {
    return ParseInlineSuite();
  }
}

// primary        = number
//                | identifier | call_expr
//                | paren_expr
//                | var_expr ;
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError<ExprPtr>("Unknown token when expecting an expression");
  case tok_identifier:
    return ParseIdentifierExpr(); // Also looks for call expressions
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  case tok_var:
    return ParseVarExpr();
  }
}

/// unary
///   ::= primary
///   ::= '!' unary
static std::unique_ptr<ExprAST> ParseUnary() {
  // If the current token is not an operator, it must be a primary expr.
  if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
    return ParsePrimary();

  // If this is a unary operator, read it.
  int Opc = CurTok;
  getNextToken();
  if (auto Operand = ParseUnary())
    return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}

/// binoprhs
///   ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
  // If this is a binop, find its precedence.
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < ExprPrec)
      return LHS;

    // Okay, we know this is a binop.
    int BinOp = CurTok;
    SourceLocation BinLoc = CurLoc;
    getNextToken(); // eat binop

    // Parse the primary expression after the binary operator.
    auto RHS = ParseUnary();
    if (!RHS)
      return nullptr;

    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    // Merge LHS/RHS.
    LHS = std::make_unique<BinaryExprAST>(BinLoc, BinOp, std::move(LHS),
                                          std::move(RHS));
  }
}

/// expression
///   ::= unary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParseUnary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///   ::= id '(' (id (',' id)*)? ')'
static std::unique_ptr<PrototypeAST>
ParsePrototype(OperatorType operatorType = Undefined, int precedence = 0) {
  std::string FnName;
  SourceLocation FnLoc = CurLoc;

  if (operatorType != Undefined) {
    // Expect a single-character operator
    if (CurTok == tok_identifier) {
      return LogError<ProtoPtr>("Expected single character operator");
    }

    if (!isascii(CurTok)) {
      return LogError<ProtoPtr>("Expected single character operator");
    }

    FnName = (operatorType == Unary ? "unary" : "binary");
    FnName += (char)CurTok;

    getNextToken();
  } else {
    if (CurTok != tok_identifier) {
      return LogError<ProtoPtr>("Expected function name in prototype");
    }

    FnName = IdentifierStr;

    getNextToken();
  }

  if (CurTok != '(') {
    return LogError<ProtoPtr>("Expected '(' in prototype");
  }

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
    getNextToken(); // Eat idenfitier

    if (CurTok == ')')
      break;

    if (CurTok != ',')
      return LogError<ProtoPtr>("Expected ')' or ',' in parameter list");
  }

  if (CurTok != ')')
    return LogError<ProtoPtr>("Expected ')' in prototype");

  // success.
  getNextToken(); // eat ')'.
  return std::make_unique<PrototypeAST>(FnLoc, FnName, std::move(ArgNames),
                                        operatorType != OperatorType::Undefined,
                                        precedence);
}

/// definition ::= (@unary | @binary | @binary() | @binary(precedence=\d+))*
///                 'def' prototype:
///                     expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
  OperatorType OpType = Undefined;
  int Precedence = DEFAULT_BINARY_PRECEDENCE;

  if (CurTok == tok_decorator) {
    getNextToken(); // eat '@'

    if (CurTok != tok_identifier)
      return LogError<FuncPtr>("expected decorator name after '@'");

    auto it = Decorators.find(IdentifierStr);
    OpType = it == Decorators.end() ? OperatorType::Undefined : it->second;
    getNextToken(); // eat decorator name

    if (OpType == Undefined)
      return LogError<FuncPtr>(
          ("unknown decorator '" + IdentifierStr + "'").c_str());

    if (OpType == Binary) {
      if (CurTok == '(') {
        getNextToken(); // eat '('
        if (CurTok != ')') {
          // Parse "precedence=N"
          // If we want to introduce more attributes, we would add "precedence"
          // to a map and associate it with a binary operator.
          if (CurTok != tok_identifier || IdentifierStr != "precedence") {
            return LogError<FuncPtr>(
                "expected 'precedence' parameter in decorator");
          }

          getNextToken(); // eat 'precedence'

          if (CurTok != '=')
            return LogError<FuncPtr>("expected '=' after 'precedence'");

          getNextToken(); // eat '='

          if (CurTok != tok_number)
            return LogError<FuncPtr>("expected number for precedence value");

          Precedence = NumVal;
          getNextToken(); // eat number
        }
        if (CurTok != ')')
          return LogError<FuncPtr>("expected ')' after precedence value");
        getNextToken(); // eat ')'
      }
    }
  }

  EatNewLines();

  if (CurTok != tok_def)
    return LogError<FuncPtr>("expected 'def'");

  getNextToken(); // eat def.
  auto Proto = ParsePrototype(OpType, Precedence);
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogError<FuncPtr>("Expected ':' in function definition");

  getNextToken(); // eat ':'

  auto E = ParseSuite();
  if (!E)
    return nullptr;

  EatNewLines();
  // TODO: Check indent levels and check for consistent newlines usage.
  while (CurTok == tok_dedent) {
    getNextToken();
  }

  return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  SourceLocation FnLoc = CurLoc;
  if (auto E = ParseExpression()) {
    // Wrap expression in ExprStmtAST -> BlockSuiteAST
    auto ExprStmt = std::make_unique<ExprStmtAST>(FnLoc, std::move(E));
    std::vector<StmtPtr> Stmts;
    Stmts.push_back(std::move(ExprStmt));
    auto Body = std::make_unique<BlockSuiteAST>(FnLoc, std::move(Stmts));

    // Make an anonymous proto.
    auto Proto = std::make_unique<PrototypeAST>(FnLoc, "__anon_expr",
                                                std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(Body));
  }
  return nullptr;
}

/// external ::= 'extern' 'def' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurTok != tok_def)
    return LogError<ProtoPtr>("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParsePrototype();
}

//===----------------------------------------------------------------------===//
// Code Generation Globals
//===----------------------------------------------------------------------===//

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, AllocaInst *> NamedValues;
static std::unique_ptr<PyxcJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static ExitOnError ExitOnErr;

//===----------------------------------------------------------------------===//
// Debug Info Support
//===----------------------------------------------------------------------===//

namespace {
class ExprAST;
class PrototypeAST;
} // namespace

struct DebugInfo {
  DICompileUnit *TheCU;
  DIType *DblTy;
  std::vector<DIScope *> LexicalBlocks;

  void emitLocation(ExprAST *AST);
  void emitLocation(StmtAST *AST);
  DIType *getDoubleTy();
};

static std::unique_ptr<DebugInfo> KSDbgInfo;
static std::unique_ptr<DIBuilder> DBuilder;

// Helper to safely emit location
inline void emitLocation(ExprAST *AST) {
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(AST);
}

inline void emitLocation(StmtAST *AST) {
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(AST);
}

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
  Builder->SetCurrentDebugLocation(DILocation::get(
      Scope->getContext(), AST->getLine(), AST->getCol(), Scope));
}

void DebugInfo::emitLocation(StmtAST *AST) {
  if (!AST)
    return Builder->SetCurrentDebugLocation(DebugLoc());
  DIScope *Scope;
  if (LexicalBlocks.empty())
    Scope = TheCU;
  else
    Scope = LexicalBlocks.back();
  Builder->SetCurrentDebugLocation(DILocation::get(
      Scope->getContext(), AST->getLine(), AST->getCol(), Scope));
}

static DISubroutineType *CreateFunctionType(unsigned NumArgs) {
  if (!EmitDebug)
    return nullptr;

  SmallVector<Metadata *, 8> EltTys;
  DIType *DblTy = KSDbgInfo->getDoubleTy();

  // Add the result type.
  EltTys.push_back(DblTy);

  for (unsigned i = 0, e = NumArgs; i != e; ++i)
    EltTys.push_back(DblTy);

  return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(EltTys));
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

Function *getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  return nullptr;
}

/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          StringRef VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
}

Value *NumberExprAST::codegen() {
  emitLocation(this);
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  AllocaInst *A = NamedValues[Name];
  if (!A)
    return LogError<Value *>(("Unknown variable name " + Name).c_str());
  emitLocation(this);
  // Load the value.
  return Builder->CreateLoad(A->getAllocatedType(), A, Name.c_str());
}

Value *UnaryExprAST::codegen() {
  Value *OperandV = Operand->codegen();
  if (!OperandV)
    return nullptr;

  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F) {
    return LogError<Value *>("Unknown unary operator");
  }
  emitLocation(this);
  return Builder->CreateCall(F, OperandV, "unop");
}

Value *BinaryExprAST::codegen() {
  emitLocation(this);
  // Special case '=' because we don't want to emit the LHS as an expression.
  if (Op == '=') {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    VariableExprAST *LHSE = static_cast<VariableExprAST *>(LHS.get());
    if (!LHSE)
      return LogError<Value *>("destination of '=' must be a variable");
    // Codegen the RHS.
    Value *Val = RHS->codegen();
    if (!Val)
      return nullptr;

    // Look up the name.
    Value *Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return LogError<Value *>("Unknown variable name");

    Builder->CreateStore(Val, Variable);
    return Val;
  }

  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  Function *F = getFunction(std::string("binary") + Op);
  assert(F && "binary operator not found!");

  Value *Ops[2] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}

Value *CallExprAST::codegen() {
  emitLocation(this);

  // Look up the name in the global module table.
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogError<Value *>("Unknown function referenced");

  // If argument mismatch error.
  if (CalleeF->arg_size() != Args.size())
    return LogError<Value *>("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfStmtAST::codegen() {
  emitLocation(this);

  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for the then and else cases. Insert the 'then' block at the
  // end of the function.
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value.
  Builder->SetInsertPoint(ThenBB);

  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;
  bool ThenTerminated = Builder->GetInsertBlock()->getTerminator() != nullptr;
  if (!ThenTerminated)
    Builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the
  // result selection.
  ThenBB = Builder->GetInsertBlock();

  // Emit else block.
  TheFunction->insert(TheFunction->end(), ElseBB);
  Builder->SetInsertPoint(ElseBB);

  Value *ElseV = nullptr;
  bool ElseTerminated = false;
  if (Else) {
    ElseV = Else->codegen();
    if (!ElseV)
      return nullptr;
    ElseTerminated = Builder->GetInsertBlock()->getTerminator() != nullptr;
  } else {
    ElseV = ConstantFP::get(*TheContext, APFloat(0.0));
  }
  if (!ElseTerminated)
    Builder->CreateBr(MergeBB);
  // Codegen of 'Else' can change the current block, update ElseBB for the
  // result selection.
  ElseBB = Builder->GetInsertBlock();

  // If both sides already terminated (e.g. both returned), there is no
  // reachable continuation.
  if (ThenTerminated && ElseTerminated) {
    BasicBlock *DeadCont =
        BasicBlock::Create(*TheContext, "ifcont.dead", TheFunction);
    Builder->SetInsertPoint(DeadCont);
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  // Emit merge block for any non-terminated path.
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  if (!ThenTerminated && !ElseTerminated) {
    PHINode *PN =
        Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    return PN;
  }

  // Exactly one side flows through to merge.
  return ThenTerminated ? ElseV : ThenV;
}

Value *ForStmtAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create an alloca for the variable in the entry block.
  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

  // Emit the start code first, without 'variable' in scope.
  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;

  // Store the value into the alloca.
  Builder->CreateStore(StartVal, Alloca);

  // If the loop variable shadows an existing variable, we have to restore it,
  // so save it now. Set VarName to refer to our recently created alloca.
  AllocaInst *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Alloca;

  // Make new basic blocks for loop condition, loop body and end-loop code.
  BasicBlock *LoopConditionBB =
      BasicBlock::Create(*TheContext, "loopcond", TheFunction);
  BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop");
  BasicBlock *EndLoopBB = BasicBlock::Create(*TheContext, "endloop");

  // Insert an explicit fall through from current block to LoopConditionBB.
  Builder->CreateBr(LoopConditionBB);

  // Start insertion in LoopConditionBB.
  Builder->SetInsertPoint(LoopConditionBB);

  // Compute the end condition.
  Value *EndCond = End->codegen();
  if (!EndCond)
    return nullptr;

  // Load new loop variable
  Value *CurVar =
      Builder->CreateLoad(Alloca->getAllocatedType(), Alloca, VarName);

  // Check if Variable < End
  EndCond = Builder->CreateFCmpULT(CurVar, EndCond, "loopcond");

  // Insert the conditional branch that either continues the loop, or exits the
  // loop.
  Builder->CreateCondBr(EndCond, LoopBB, EndLoopBB);

  // Attach the basic block that will soon hold the loop body to the end of the
  // parent function.
  TheFunction->insert(TheFunction->end(), LoopBB);

  // Emit the loop body within the LoopBB. This, like any other expr, can change
  // the current BB. Note that we ignore the value computed by the body, but
  // don't allow an error.
  Builder->SetInsertPoint(LoopBB);
  if (!Body->codegen()) {
    return nullptr;
  }

  // Emit the step value.
  Value *StepVal = nullptr;
  if (Step) {
    StepVal = Step->codegen();
    if (!StepVal)
      return nullptr;
  } else {
    // If not specified, use 1.0.
    StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
  }

  Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
  Builder->CreateStore(NextVar, Alloca);

  // Create the unconditional branch that returns to LoopConditionBB to
  // determine if we should continue looping.
  Builder->CreateBr(LoopConditionBB);

  // Append EndLoopBB after the loop body. We go to this basic block if the
  // loop condition says we should not loop anymore.
  TheFunction->insert(TheFunction->end(), EndLoopBB);

  // Any new code will be inserted after the loop.
  Builder->SetInsertPoint(EndLoopBB);

  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for expr always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *VarExprAST::codegen() {
  std::vector<AllocaInst *> OldBindings;

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Register all variables and emit their initializer.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
    const std::string &VarName = VarNames[i].first;
    ExprAST *Init = VarNames[i].second.get();

    // Emit the initializer before adding the variable to scope, this prevents
    // the initializer from referencing the variable itself, and permits stuff
    // like this:
    //  var a = 1 in
    //    var a = a in ...   # refers to outer 'a'.
    Value *InitVal;
    if (Init) {
      InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
    } else { // If not specified, use 0.0.
      InitVal = ConstantFP::get(*TheContext, APFloat(0.0));
    }

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);

    // Remember the old variable binding so that we can restore the binding when
    // we unrecurse.
    OldBindings.push_back(NamedValues[VarName]);

    // Remember this binding.
    NamedValues[VarName] = Alloca;
  }

  emitLocation(this);

  // Codegen the body, now that all vars are in scope.
  Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;

  // Pop all our variables from scope.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
    NamedValues[VarNames[i].first] = OldBindings[i];

  // Return the body computation.
  return BodyVal;
}

Value *ReturnStmtAST::codegen() {
  Value *RetVal = Expr->codegen();
  if (!RetVal)
    return nullptr;

  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  Type *ExpectedTy = TheFunction->getReturnType();
  if (ExpectedTy->isIntegerTy(32) && RetVal->getType()->isDoubleTy()) {
    RetVal = Builder->CreateFPToSI(RetVal, ExpectedTy, "ret_i32");
  } else if (ExpectedTy->isDoubleTy() && RetVal->getType()->isIntegerTy(32)) {
    RetVal = Builder->CreateSIToFP(RetVal, ExpectedTy, "ret_double");
  }

  Builder->CreateRet(RetVal);
  return RetVal;
}

Value *BlockSuiteAST::codegen() {
  Value *Last = nullptr;
  for (size_t i = 0; i < Stmts.size(); ++i) {
    Last = Stmts[i]->codegen();
    if (!Last)
      return nullptr;
    // Stop generating after a terminator (e.g. return).
    if (Stmts[i]->isTerminator()) {
      if (i + 1 < Stmts.size()) {
        fprintf(stderr,
                "Warning (Line %d): unreachable code after return statement\n",
                Stmts[i + 1]->getLine());
      }
      break;
    }
  }
  return Last;
}

Function *PrototypeAST::codegen() {
  // For native executable/object builds, emit C-style `int main`.
  // In interpreter mode, keep all functions (including `main`) as `double`.
  Type *RetType;
  if (UseCMainSignature && Name == "main") {
    RetType = Type::getInt32Ty(*TheContext);
  } else {
    RetType = Type::getDoubleTy(*TheContext);
  }

  // Make the function type
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT = FunctionType::get(RetType, Doubles, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

Function *FunctionAST::codegen() {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep
  // a reference to it for use below.
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // If this is an operator, install it.
  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  DIFile *Unit = nullptr;
  DISubprogram *SP = nullptr;

  if (KSDbgInfo) {
    // Create a subprogram DIE for this function.
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

    // Push the current scope.
    KSDbgInfo->LexicalBlocks.push_back(SP);

    // Unset the location for the prologue emission (leading instructions with
    // no location in a function are considered part of the prologue and the
    // debugger will run past them when breaking on a function)
    emitLocation((StmtAST *)nullptr);
  }

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  unsigned ArgIdx = 0;

  for (auto &Arg : TheFunction->args()) {
    // Create an alloca for this variable.
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

    // Create a debug descriptor for the variable.
    if (KSDbgInfo) {
      unsigned LineNo = P.getLine();
      DILocalVariable *D = DBuilder->createParameterVariable(
          SP, Arg.getName(), ++ArgIdx, Unit, LineNo, KSDbgInfo->getDoubleTy(),
          true);

      DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
                              DILocation::get(SP->getContext(), LineNo, 0, SP),
                              Builder->GetInsertBlock());
    }

    // Store the initial value into the alloca.
    Builder->CreateStore(&Arg, Alloca);

    // Add arguments to variable symbol table.
    NamedValues[std::string(Arg.getName())] = Alloca;
  }

  if (Value *RetVal = Body->codegen()) {
    // Finish off the function if the current block is still open.
    if (!Builder->GetInsertBlock()->getTerminator()) {
      // Special handling for native `main`: convert double to i32.
      if (UseCMainSignature && P.getName() == "main") {
        RetVal = Builder->CreateFPToSI(RetVal, Type::getInt32Ty(*TheContext),
                                       "mainret");
      }
      Builder->CreateRet(RetVal);
    }

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    // Run the optimizer on the function.
    TheFPM->run(*TheFunction, *TheFAM);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();

  if (P.isBinaryOp())
    BinopPrecedence.erase(P.getOperatorName());

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Initialization helpers
//===----------------------------------------------------------------------===//

static void InitializeContext() {
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcModule", *TheContext);
  Builder = std::make_unique<IRBuilder<>>(*TheContext);
}

static void InitializeOptimizationPasses() {
  TheFPM = std::make_unique<FunctionPassManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  TheCGAM = std::make_unique<CGSCCAnalysisManager>();
  TheMAM = std::make_unique<ModuleAnalysisManager>();
  ThePIC = std::make_unique<PassInstrumentationCallbacks>();
  TheSI = std::make_unique<StandardInstrumentations>(*TheContext,
                                                     /*DebugLogging*/ true);
  TheSI->registerCallbacks(*ThePIC, TheMAM.get());

  // Add transform passes
  TheFPM->addPass(PromotePass());
  TheFPM->addPass(InstCombinePass());
  TheFPM->addPass(ReassociatePass());
  TheFPM->addPass(GVNPass());
  TheFPM->addPass(SimplifyCFGPass());

  // Register analysis passes
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

static void InitializeModuleAndManagers() {
  InitializeContext();
  TheModule->setDataLayout(TheJIT->getDataLayout());
  InitializeOptimizationPasses();
}

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      if (Verbose) {
        fprintf(stderr, "Read function definition:\n");
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }
      ExitOnErr(TheJIT->addModule(
          ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
      InitializeModuleAndManagers();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      if (Verbose) {
        fprintf(stderr, "Read extern:\n");
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
      // Create a ResourceTracker to track JIT'd memory allocated to our
      // anonymous expression -- that way we can free it after executing.
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();

      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      if (Verbose) {
        fprintf(stderr, "Read top-level expression:\n");
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }

      // Search the JIT for the __anon_expr symbol.
      auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

      // Get the symbol's address and cast it to the right type (takes no
      // arguments, returns a double) so we can call it as a native
      // function.
      double (*FP)() = ExprSymbol.toPtr<double (*)()>();
      fprintf(stderr, "Evaluated to %f\nready> ", FP());

      // Delete the anonymous expression module from the JIT.
      ExitOnErr(RT->remove());

      // Remove the anonymous expression.
      //   FnIR->eraseFromParent();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

/// top ::= definition | external | expression | eol
static void MainLoop() {
  while (true) {
    switch (CurTok) {
    case tok_error:
      return;
    case tok_eof:
      return;
    case tok_eol:
      getNextToken();
      break;
    case tok_dedent:
      getNextToken();
      break;
    default:
      fprintf(stderr, "ready> ");
      switch (CurTok) {
      case tok_decorator:
      case tok_def:
        HandleDefinition();
        break;
      case tok_extern:
        HandleExtern();
        break;
      default:
        HandleTopLevelExpression();
        break;
      }
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}

//===----------------------------------------------------------------------===//
// Parsing helpers
//===----------------------------------------------------------------------===//

static void ParseSourceFile() {
  // Parse all definitions from the file
  while (CurTok != tok_eof && CurTok != tok_error) {
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
        getNextToken(); // Skip for error recovery
      }
      break;
    case tok_extern:
      if (auto ProtoAST = ParseExtern()) {
        if (auto *FnIR = ProtoAST->codegen()) {
          if (Verbose) {
            fprintf(stderr, "Read extern:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
          }
          FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
        }
      } else {
        getNextToken(); // Skip for error recovery
      }
      break;
    case tok_eol:
      getNextToken(); // Skip newlines
      break;
    default:
      // Top-level expressions
      if (auto FnAST = ParseTopLevelExpr()) {
        if (auto *FnIR = FnAST->codegen()) {
          if (Verbose) {
            fprintf(stderr, "Read top-level expression:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
          }
        }
      } else {
        getNextToken(); // Skip for error recovery
      }
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// Interpreter (JIT execution of file)
//===----------------------------------------------------------------------===//

void InterpretFile(const std::string &filename) {
  UseCMainSignature = false;
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Create JIT
  TheJIT = ExitOnErr(PyxcJIT::Create());
  InitializeModuleAndManagers();

  // Open input file
  InputFile = fopen(filename.c_str(), "r");
  if (!InputFile) {
    errs() << "Error: Could not open file " << filename << "\n";
    InputFile = stdin;
    return;
  }

  // Parse the source file
  getNextToken();

  while (CurTok != tok_eof && CurTok != tok_error) {
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
          // Add to JIT
          ExitOnErr(TheJIT->addModule(
              ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
          InitializeModuleAndManagers();
        }
      } else {
        getNextToken();
      }
      break;
    case tok_extern:
      if (auto ProtoAST = ParseExtern()) {
        if (auto *FnIR = ProtoAST->codegen()) {
          if (Verbose) {
            fprintf(stderr, "Read extern:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
          }
          FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
        }
      } else {
        getNextToken();
      }
      break;
    case tok_eol:
      getNextToken();
      break;
    default:
      // Top-level expressions - execute them
      if (auto FnAST = ParseTopLevelExpr()) {
        if (auto *FnIR = FnAST->codegen()) {
          auto RT = TheJIT->getMainJITDylib().createResourceTracker();

          auto TSM =
              ThreadSafeModule(std::move(TheModule), std::move(TheContext));
          ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
          InitializeModuleAndManagers();

          if (Verbose) {
            fprintf(stderr, "Read top-level expression:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
          }

          // Execute the expression
          auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));
          double (*FP)() = ExprSymbol.toPtr<double (*)()>();
          double result = FP();

          if (Verbose)
            fprintf(stderr, "Result: %f\n", result);

          // Clean up the anonymous expression
          ExitOnErr(RT->remove());
        }
      } else {
        getNextToken();
      }
      break;
    }
  }

  // Close file and restore stdin
  fclose(InputFile);
  InputFile = stdin;
}

//===----------------------------------------------------------------------===//
// Object file compilation
//===----------------------------------------------------------------------===//

void CompileToObjectFile(const std::string &filename) {
  UseCMainSignature = true;
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Initialize LLVM context and optimization passes
  InitializeContext();
  InitializeOptimizationPasses();

  // Open input file
  InputFile = fopen(filename.c_str(), "r");
  if (!InputFile) {
    errs() << "Error: Could not open file " << filename << "\n";
    InputFile = stdin;
    return;
  }

  // Parse the source file
  getNextToken();
  ParseSourceFile();

  // Close file and restore stdin
  fclose(InputFile);
  InputFile = stdin;

  // Setup target triple
  auto TargetTriple = sys::getDefaultTargetTriple();
  TheModule->setTargetTriple(Triple(TargetTriple));

  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);

  if (!Target) {
    errs() << "Error: " << Error << "\n";
    return;
  }

  auto CPU = "generic";
  auto Features = "";

  TargetOptions opt;
  llvm::Triple TheTriple(TargetTriple);

  auto TargetMachine =
      Target->createTargetMachine(TheTriple, CPU, Features, opt, Reloc::PIC_);

  TheModule->setDataLayout(TargetMachine->createDataLayout());

  // Determine output filename
  std::string outputFilename = getOutputFilename(filename, ".o");

  std::error_code EC;
  raw_fd_ostream dest(outputFilename, EC, sys::fs::OF_None);

  if (EC) {
    errs() << "Could not open file: " << EC.message() << "\n";
    return;
  }

  legacy::PassManager pass;
  auto FileType = CodeGenFileType::ObjectFile;

  if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    errs() << "TargetMachine can't emit a file of this type\n";
    return;
  }

  // Run the pass to emit object code
  pass.run(*TheModule);
  dest.flush();
}

//===----------------------------------------------------------------------===//
// REPL
//===----------------------------------------------------------------------===//

void REPL() {
  UseCMainSignature = false;
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  fprintf(stderr, "ready> ");
  getNextToken();

  TheJIT = ExitOnErr(PyxcJIT::Create());
  InitializeModuleAndManagers();

  MainLoop();

  if (Verbose)
    TheModule->print(errs(), nullptr);
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::ParseCommandLineOptions(argc, argv, "Pyxc - Compiler and Interpreter\n");

  if (InputFilename.empty()) {
    // REPL mode - only -v is allowed
    if (Mode != Interpret) {
      errs() << "Error: -x and -c flags require an input file\n";
      return 1;
    }

    if (Mode == Tokens) {
      errs() << "Error: -t flag requires an input file\n";
      return 1;
    }

    if (!OutputFilename.empty()) {
      errs() << "Error: REPL mode cannot work with an output file\n";
      return 1;
    }

    // Start REPL
    REPL();
  } else {
    if (EmitDebug && Mode != Executable && Mode != Object) {
      errs() << "Error: -g is only allowed with executable builds (-x) or "
                "object builds (-o)\n";
      return 1;
    }

    // File mode - all options are valid
    if (Verbose)
      std::cout << "Processing file: " << InputFilename << "\n";

    switch (Mode) {
    case Interpret:
      if (Verbose)
        std::cout << "Interpreting " << InputFilename << "...\n";
      InterpretFile(InputFilename);
      break;

    case Executable: {
      std::string exeFile = getOutputFilename(InputFilename, "");
      if (Verbose)
        std::cout << "Compiling " << InputFilename
                  << " to executable: " << exeFile << "\n";

      // Step 1: Compile the script to object file
      std::string scriptObj = getOutputFilename(InputFilename, ".o");
      CompileToObjectFile(InputFilename);

      // Step 3: Link object files
      if (Verbose)
        std::cout << "Linking...\n";

      std::string runtimeObj = "runtime.o";

      // Step 3: Link using PyxcLinker (NO MORE system() calls!)
      if (!PyxcLinker::Link(scriptObj, runtimeObj, exeFile)) {
        errs() << "Linking failed\n";
        return 1;
      }

      if (Verbose) {
        std::cout << "Successfully created executable: " << exeFile << "\n";
        // Optionally clean up intermediate files
        std::cout << "Cleaning up intermediate files...\n";
        remove(scriptObj.c_str());
      } else {
        std::cout << exeFile << "\n";
        remove(scriptObj.c_str());
      }

      break;
    }

    case Object: {
      std::string output = getOutputFilename(InputFilename, ".o");
      if (Verbose)
        std::cout << "Compiling " << InputFilename
                  << " to object file: " << output << "\n";
      CompileToObjectFile(InputFilename);
      std::string scriptObj = getOutputFilename(InputFilename, ".o");
      if (Verbose)
        outs() << "Wrote " << scriptObj << "\n";
      else
        outs() << scriptObj << "\n";
      break;
    }
    case Tokens: {
      if (Verbose)
        std::cout << "Tokenizing " << InputFilename << "...\n";
      PrintTokens(InputFilename);
      break;
    }
    }
  }

  return 0;
}
```

## Compiling
```bash
make
```

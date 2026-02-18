#include "../include/PyxcJIT.h"
#include "../include/PyxcLinker.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/OptimizationLevel.h"
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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
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
// Command line
//===----------------------------------------------------------------------===//
static cl::SubCommand ReplCommand("repl", "Start the interactive REPL");
static cl::SubCommand RunCommand("run", "Run a .pyxc script");
static cl::SubCommand BuildCommand("build", "Build a .pyxc script");
static cl::OptionCategory PyxcCategory("Pyxc options");
enum EmitMode { EmitDefault, EmitTokens, EmitLLVMIR, EmitObj, EmitLink };

static cl::opt<EmitMode> ReplEmit(
    "emit", cl::sub(ReplCommand), cl::desc("Output kind for repl"),
    cl::values(clEnumValN(EmitTokens, "tokens", "Print lexer tokens"),
               clEnumValN(EmitLLVMIR, "llvm-ir", "Print LLVM IR for parsed input")),
    cl::init(EmitDefault), cl::cat(PyxcCategory));

static cl::list<std::string> RunInputFiles(cl::Positional, cl::sub(RunCommand),
                                           cl::desc("<script.pyxc>"),
                                           cl::ZeroOrMore,
                                           cl::cat(PyxcCategory));
static cl::opt<EmitMode> RunEmit(
    "emit", cl::sub(RunCommand), cl::desc("Output kind for run"),
    cl::values(clEnumValN(EmitLLVMIR, "llvm-ir", "Emit LLVM IR for the script")),
    cl::init(EmitDefault), cl::cat(PyxcCategory));

static cl::list<std::string> BuildInputFiles(cl::Positional,
                                             cl::sub(BuildCommand),
                                             cl::desc("<script.pyxc>"),
                                             cl::ZeroOrMore,
                                             cl::cat(PyxcCategory));
static cl::opt<EmitMode>
    BuildEmit("emit", cl::sub(BuildCommand), cl::desc("Output kind for build"),
              cl::values(clEnumValN(EmitLLVMIR, "llvm-ir", "Emit LLVM IR"),
                         clEnumValN(EmitObj, "obj", "Emit object file"),
                         clEnumValN(EmitLink, "link", "Link and emit executable")),
              cl::init(EmitLink), cl::cat(PyxcCategory));
static cl::opt<bool> BuildDebug("g", cl::sub(BuildCommand),
                                cl::desc("Emit debug info"), cl::init(false),
                                cl::cat(PyxcCategory));
static cl::opt<unsigned>
    BuildOptLevel("O", cl::sub(BuildCommand),
                  cl::desc("Optimization level (use -O0..-O3)"), cl::Prefix,
                  cl::init(0), cl::cat(PyxcCategory));

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
  tok_return = -8,

  // multi-char comparison operators
  tok_eq = -9, // ==
  tok_ne = -10, // !=
  tok_le = -11, // <=
  tok_ge = -12  // >=
};

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number
static std::string NumLiteralStr; // Filled in if tok_number
static bool HadError = false;

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static std::map<int, std::string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  std::map<int, std::string> Names = {
      {tok_eof, "end of input"},
      {tok_eol, "newline"},
      {tok_error, "error"},
      {tok_def, "'def'"},
      {tok_extern, "'extern'"},
      {tok_identifier, "identifier"},
      {tok_number, "number"},
      {tok_return, "'return'"},
      {tok_eq, "'=='"},
      {tok_ne, "'!='"},
      {tok_le, "'<='"},
      {tok_ge, "'>='"},
  };

  // Single character tokens.
  for (int C = 0; C <= 255; ++C) {
    if (isprint(static_cast<unsigned char>(C)))
      Names[C] = "'" + std::string(1, static_cast<char>(C)) + "'";
    else if (C == '\n')
      Names[C] = "'\\n'";
    else if (C == '\t')
      Names[C] = "'\\t'";
    else if (C == '\r')
      Names[C] = "'\\r'";
    else if (C == '\0')
      Names[C] = "'\\0'";
    else {
      std::ostringstream OS;
      OS << "0x" << std::uppercase << std::hex << std::setw(2)
         << std::setfill('0') << C;
      Names[C] = OS.str();
    }
  }

  return Names;
}();

struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

class SourceManager {
  std::vector<std::string> CompletedLines;
  std::string CurrentLine;

public:
  void reset() {
    CompletedLines.clear();
    CurrentLine.clear();
  }

  void onChar(int C) {
    if (C == '\n') {
      CompletedLines.push_back(CurrentLine);
      CurrentLine.clear();
      return;
    }
    if (C != EOF)
      CurrentLine.push_back(static_cast<char>(C));
  }

  const std::string *getLine(int OneBasedLine) const {
    if (OneBasedLine <= 0)
      return nullptr;
    size_t Index = static_cast<size_t>(OneBasedLine - 1);
    if (Index < CompletedLines.size())
      return &CompletedLines[Index];
    if (Index == CompletedLines.size())
      return &CurrentLine;
    return nullptr;
  }
};

static SourceManager DiagSourceMgr;

static std::string FormatTokenForMessage(int Tok) {
  if (Tok == tok_identifier)
    return "identifier '" + IdentifierStr + "'";
  if (Tok == tok_number)
    return "number '" + NumLiteralStr + "'";

  const auto TokIt = TokenNames.find(Tok);
  if (TokIt != TokenNames.end())
    return TokIt->second;
  return "unknown token";
}

static const std::string &GetTokenName(int Tok) {
  const auto TokIt = TokenNames.find(Tok);
  if (TokIt != TokenNames.end())
    return TokIt->second;

  static const std::string Unknown = "tok_unknown";
  return Unknown;
}

static void PrintErrorSourceContext(SourceLocation Loc) {
  const std::string *LineText = DiagSourceMgr.getLine(Loc.Line);
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());

  int Spaces = Loc.Col - 1;
  if (Spaces < 0)
    Spaces = 0;
  for (int I = 0; I < Spaces; ++I)
    fputc(' ', stderr);
  fprintf(stderr, "%s^%s", Bold, Reset);
  fputc('~', stderr);
  fputc('~', stderr);
  fputc('~', stderr);
  fputc('\n', stderr);
}

static SourceLocation GetDiagnosticAnchorLoc(SourceLocation Loc, int Tok) {
  if (Tok != tok_eol)
    return Loc;

  // Keep CurLoc token-semantic for tok_eol (next-line boundary), but render
  // newline diagnostics at the end of the previous source line.
  int PrevLine = Loc.Line - 1;
  if (PrevLine <= 0)
    return Loc;

  const std::string *PrevLineText = DiagSourceMgr.getLine(PrevLine);
  if (!PrevLineText)
    return Loc;

  return SourceLocation{PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}

static int advance() {
  int LastChar = getchar();
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    DiagSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }
  if (LastChar == '\n') {
    DiagSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    DiagSourceMgr.onChar(LastChar);
    LexLoc.Col++;
  }
  return LastChar;
}

/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';
  // Skip whitespace EXCEPT newlines
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // Return end-of-line token.
  if (LastChar == '\n') {
    CurLoc = LexLoc;
    LastChar = ' ';
    return tok_eol;
  }

  CurLoc = LexLoc;

  if (isalpha(LastChar) ||
      LastChar == '_') { // identifier: [a-zA-Z_][a-zA-Z0-9_]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
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

    char *End;
    NumVal = strtod(NumStr.c_str(), &End);
    if (*End != '\0') {
      HadError = true;
      fprintf(stderr,
              "%sError%s (Line %d, Column %d): invalid number literal '%s'\n",
              Red, Reset, CurLoc.Line, CurLoc.Col, NumStr.c_str());
      return tok_error;
    }
    NumLiteralStr = NumStr;
    return tok_number;
  }

  // Multi-character comparison operators.
  if (LastChar == '=') {
    int NextChar = advance();
    if (NextChar == '=') {
      LastChar = advance();
      return tok_eq;
    }
    LastChar = NextChar;
    return '=';
  }

  if (LastChar == '!') {
    int NextChar = advance();
    if (NextChar == '=') {
      LastChar = advance();
      return tok_ne;
    }
    LastChar = NextChar;
    return '!';
  }

  if (LastChar == '<') {
    int NextChar = advance();
    if (NextChar == '=') {
      LastChar = advance();
      return tok_le;
    }
    LastChar = NextChar;
    return '<';
  }

  if (LastChar == '>') {
    int NextChar = advance();
    if (NextChar == '=') {
      LastChar = advance();
      return tok_ge;
    }
    LastChar = NextChar;
    return '>';
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF) {
      CurLoc = LexLoc;
      LastChar = ' ';
      return tok_eol;
    }
  }

  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//

namespace {

/// ExprAST - Base class for all expression nodes.
class ExprAST {
  SourceLocation Loc;

public:
  ExprAST(SourceLocation Loc = CurLoc) : Loc(Loc) {}
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
  int getLine() const { return Loc.Line; }
  int getCol() const { return Loc.Col; }
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  Value *codegen() override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(const std::string &Name) : Name(Name) {}
  Value *codegen() override;
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  int Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(int Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  Value *codegen() override;
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
  Value *codegen() override;
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;
  SourceLocation Loc;

public:
  PrototypeAST(SourceLocation Loc, const std::string &Name,
               std::vector<std::string> Args)
      : Name(Name), Args(std::move(Args)), Loc(Loc) {}

  Function *codegen();
  const std::string &getName() const { return Name; }
  int getLine() const { return Loc.Line; }
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
  Function *codegen();
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<int, int> BinopPrecedence = {
    {'<', 10}, {'>', 10}, {tok_le, 10}, {tok_ge, 10}, {tok_eq, 10},
    {tok_ne, 10}, {'+', 20}, {'-', 20}, {'*', 40},   {'/', 40},
    {'%', 40}};

/// Explanation-friendly precedence anchors used by parser control flow.
static constexpr int NO_OP_PREC = -1;
static constexpr int MIN_BINOP_PREC = 1;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  auto It = BinopPrecedence.find(CurTok);
  if (It == BinopPrecedence.end())
    return NO_OP_PREC;
  int TokPrec = It->second;
  if (TokPrec < MIN_BINOP_PREC)
    return NO_OP_PREC;
  return TokPrec;
}

using ExprPtr = std::unique_ptr<ExprAST>;
using ProtoPtr = std::unique_ptr<PrototypeAST>;
using FuncPtr = std::unique_ptr<FunctionAST>;

/// LogError - Unified error reporting template.
template <typename T = void> T LogError(const char *Str) {
  HadError = true;
  SourceLocation DiagLoc = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "%sError%s (Line: %d, Column: %d): %s\n", Red, Reset,
          DiagLoc.Line, DiagLoc.Col, Str);
  PrintErrorSourceContext(DiagLoc);
  if constexpr (std::is_void_v<T>)
    return;
  else if constexpr (std::is_pointer_v<T>)
    return nullptr;
  else
    return T{};
}

static std::unique_ptr<ExprAST> ParseExpression();

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

  getNextToken(); // eat identifier.

  if (CurTok != '(') // Simple variable ref.
    return std::make_unique<VariableExprAST>(IdName);

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

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  default: {
    std::string Msg = "Unexpected " + FormatTokenForMessage(CurTok) +
                      " when expecting an expression";
    return LogError<ExprPtr>(Msg.c_str());
  }
  }
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
    getNextToken(); // eat binop

    // Parse the primary expression after the binary operator.
    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      const int HigherPrecThanCurrent = TokPrec + 1;
      RHS = ParseBinOpRHS(HigherPrecThanCurrent, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    // Merge LHS/RHS.
    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

/// expression
///   ::= primary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(MIN_BINOP_PREC, std::move(LHS));
}

/// prototype
///   ::= id '(' (id (',' id)*)? ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogError<ProtoPtr>("Expected function name in prototype");

  SourceLocation FnLoc = CurLoc;
  std::string FnName = IdentifierStr;
  getNextToken();

  if (CurTok != '(')
    return LogError<ProtoPtr>("Expected '(' in prototype");

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
    getNextToken(); // eat identifier

    if (CurTok == ')')
      break;

    if (CurTok != ',')
      return LogError<ProtoPtr>("Expected ')' or ',' in parameter list");
  }

  if (CurTok != ')')
    return LogError<ProtoPtr>("Expected ')' in prototype");

  // success.
  getNextToken(); // eat ')'.

  return std::make_unique<PrototypeAST>(FnLoc, FnName, std::move(ArgNames));
}

/// definition ::= 'def' prototype ':' expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat def.

  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogError<FuncPtr>("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // This takes care of a situation where we decide to split the
  // function and expression
  // ready> def foo(x):
  // ready>  return x + 1
  while (CurTok == tok_eol)
    getNextToken();

  if (CurTok != tok_return)
    return LogError<FuncPtr>("Expected 'return' before return expression");
  getNextToken(); // eat return

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  SourceLocation FnLoc = CurLoc;
  if (auto E = ParseExpression()) {
    // Make an anonymous proto.
    auto Proto = std::make_unique<PrototypeAST>(FnLoc, "__anon_expr",
                                                std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
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
// Code Generation
//===----------------------------------------------------------------------===//

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> NamedValues;
static bool ShouldEmitIR = false;
static std::unique_ptr<PyxcJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static ExitOnError ExitOnErr;
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

static DISubroutineType *CreateFunctionType(unsigned NumArgs) {
  SmallVector<Metadata *, 8> EltTys;
  DIType *DblTy = KSDbgInfo->getDoubleTy();

  EltTys.push_back(DblTy);

  for (unsigned i = 0, e = NumArgs; i != e; ++i)
    EltTys.push_back(DblTy);

  return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(EltTys));
}

static Function *getFunction(const std::string &Name) {
  if (auto *F = TheModule->getFunction(Name))
    return F;

  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  return nullptr;
}

Value *NumberExprAST::codegen() {
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(this);
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
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

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '/':
    return Builder->CreateFDiv(L, R, "divtmp");
  case '%':
    return Builder->CreateFRem(L, R, "remtmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case '>':
    L = Builder->CreateFCmpUGT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_le:
    L = Builder->CreateFCmpULE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_ge:
    L = Builder->CreateFCmpUGE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_eq:
    L = Builder->CreateFCmpUEQ(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_ne:
    L = Builder->CreateFCmpUNE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogError<Value *>("invalid binary operator");
  }
}

Value *CallExprAST::codegen() {
  if (KSDbgInfo)
    KSDbgInfo->emitLocation(this);

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

Function *PrototypeAST::codegen() {
  // Special case: main function returns int, everything else returns double
  Type *RetType;
  if (Name == "main") {
    RetType = Type::getInt32Ty(*TheContext);
  } else {
    RetType = Type::getDoubleTy(*TheContext);
  }

  // Make the function type:  double(double,double) etc. or int32() for main
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
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());

  if (!TheFunction)
    return nullptr;

  // Create a new basic block to start insertion into.
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
    // Special handling for main: convert double to i32
    if (P.getName() == "main") {
      RetVal = Builder->CreateFPToSI(RetVal, Type::getInt32Ty(*TheContext), "mainret");
    }

    // Finish off the function.
    Builder->CreateRet(RetVal);

    // Pop off the lexical block for the function
    if (KSDbgInfo)
      KSDbgInfo->LexicalBlocks.pop_back();

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    // Run the optimizer on the function (only in REPL mode, not when building objects)
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

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static void InitializeModuleAndManagers() {
  // Open a new context and module.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  if (TheJIT)
    TheModule->setDataLayout(TheJIT->getDataLayout());

  // Create a new builder for the module.
  Builder = std::make_unique<IRBuilder<>>(*TheContext);

  // Create new pass and analysis managers.
  TheFPM = std::make_unique<FunctionPassManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  TheCGAM = std::make_unique<CGSCCAnalysisManager>();
  TheMAM = std::make_unique<ModuleAnalysisManager>();
  ThePIC = std::make_unique<PassInstrumentationCallbacks>();
  TheSI = std::make_unique<StandardInstrumentations>(*TheContext,
                                                     /*DebugLogging*/ false);
  TheSI->registerCallbacks(*ThePIC, TheMAM.get());

  switch (CurrentOptLevel) {
  case 0:
    break;
  case 1:
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    break;
  case 2:
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    TheFPM->addPass(GVNPass());
    break;
  default: // O3 and above
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    TheFPM->addPass(GVNPass());
    TheFPM->addPass(SimplifyCFGPass());
    break;
  }

  // Register analysis passes.
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

//===----------------------------------------------------------------------===//
// Top-Level parsing
//===----------------------------------------------------------------------===//

static void SynchronizeToLineBoundary() {
  while (CurTok != tok_eol && CurTok != tok_eof)
    getNextToken();
}

static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      std::string Msg = "Unexpected " + FormatTokenForMessage(CurTok);
      LogError<void>(Msg.c_str());
      SynchronizeToLineBoundary();
      return;
    }
    if (auto *FnIR = FnAST->codegen()) {
      if (ShouldEmitIR) {
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }
      if (!BuildObjectMode) {
        ExitOnErr(TheJIT->addModule(
            ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
        InitializeModuleAndManagers();
      }
    }
  } else {
    // Error recovery: skip the rest of the current line so leftover tokens
    // from a malformed construct don't get parsed as a new top-level form.
    SynchronizeToLineBoundary();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      std::string Msg = "Unexpected " + FormatTokenForMessage(CurTok);
      LogError<void>(Msg.c_str());
      SynchronizeToLineBoundary();
      return;
    }
    if (auto *FnIR = ProtoAST->codegen()) {
      if (ShouldEmitIR) {
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    SynchronizeToLineBoundary();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      std::string Msg = "Unexpected " + FormatTokenForMessage(CurTok);
      LogError<void>(Msg.c_str());
      SynchronizeToLineBoundary();
      return;
    }
    if (auto *FnIR = FnAST->codegen()) {
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      if (ShouldEmitIR) {
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }

      auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));
      double (*FP)() = ExprSymbol.toPtr<double (*)()>();
      fprintf(stderr, "%f\n", FP());

      ExitOnErr(RT->remove());
    }
  } else {
    SynchronizeToLineBoundary();
  }
}

/// top ::= definition | external | expression | eol
static void MainLoop() {
  while (true) {
    if (CurTok == tok_eof)
      return;

    if (CurTok == tok_eol) {
      if (InteractiveMode)
        fprintf(stderr, "ready> ");
      getNextToken();
      continue;
    }

    switch (CurTok) {
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      if (BuildObjectMode) {
        LogError<void>("Top-level expressions are not supported in build mode yet");
        SynchronizeToLineBoundary();
      } else {
        HandleTopLevelExpression();
      }
      break;
    }
  }
}

static void EmitTokenStream() {
  fprintf(stderr, "ready> ");
  while (true) {
    const int Tok = gettok();
    if (Tok == tok_eof)
      return;

    fprintf(stderr, "%s", GetTokenName(Tok).c_str());
    if (Tok == tok_eol)
      fprintf(stderr, "\nready> ");
    else
      fprintf(stderr, " ");
  }
}

static std::string DeriveObjectOutputPath(const std::string &InputFile) {
  const size_t DotPos = InputFile.find_last_of('.');
  if (DotPos == std::string::npos)
    return InputFile + ".o";
  return InputFile.substr(0, DotPos) + ".o";
}

// Returns empty string if the input file has no extension, to avoid
// silently overwriting the input file with the executable output.
static std::string DeriveExecutableOutputPath(const std::string &InputFile) {
  const size_t DotPos = InputFile.find_last_of('.');
  if (DotPos == std::string::npos)
    return "";
  return InputFile.substr(0, DotPos);
}

static bool EmitObjectFile(const std::string &OutputPath) {
  // Note: Optimization has already been run in main() before calling this function
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  const std::string TargetTriple = sys::getDefaultTargetTriple();

  std::string Error;
  const Target *Target = TargetRegistry::lookupTarget(TargetTriple, Error);
  if (!Target) {
    errs() << Error << "\n";
    return false;
  }

  TargetOptions Opts;
  std::unique_ptr<TargetMachine> TheTargetMachine(Target->createTargetMachine(
      Triple(TargetTriple), "generic", "", Opts, Reloc::PIC_));
  if (!TheTargetMachine) {
    errs() << "Could not create target machine.\n";
    return false;
  }

  // Set the module's target triple and data layout
  TheModule->setTargetTriple(Triple(TargetTriple));
  TheModule->setDataLayout(TheTargetMachine->createDataLayout());

  std::error_code EC;
  raw_fd_ostream Dest(OutputPath, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "Could not open file '" << OutputPath << "': " << EC.message()
           << "\n";
    return false;
  }

  // Use legacy PassManager for code generation (required by TargetMachine)
  legacy::PassManager CodeGenPass;
  if (TheTargetMachine->addPassesToEmitFile(CodeGenPass, Dest, nullptr,
                                            CodeGenFileType::ObjectFile)) {
    errs() << "TheTargetMachine can't emit an object file.\n";
    return false;
  }

  CodeGenPass.run(*TheModule);
  Dest.flush();
  outs() << "Wrote " << OutputPath << "\n";
  return true;
}

static bool LinkExecutable(const std::string &ObjectPath, const std::string &RuntimeObj, const std::string &ExePath) {
  // Use LLD (LLVM's linker) to link the object file into an executable.
  // PyxcLinker handles platform-specific linking (ELF, Mach-O, PE/COFF).

  bool success = PyxcLinker::Link(ObjectPath, RuntimeObj, ExePath);
  if (!success) {
    errs() << "Error: failed to link executable.\n";
    return false;
  }

  outs() << "Linked executable: " << ExePath << "\n";
  return true;
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main(int argc, const char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::HideUnrelatedOptions(PyxcCategory, ReplCommand);
  cl::HideUnrelatedOptions(PyxcCategory, RunCommand);
  cl::HideUnrelatedOptions(PyxcCategory, BuildCommand);
  cl::ParseCommandLineOptions(argc, argv, "pyxc chapter07\n");

  if (BuildOptLevel > 3) {
    fprintf(stderr, "Error: invalid optimization level -O%u (expected 0..3)\n",
            static_cast<unsigned>(BuildOptLevel));
    return 1;
  }

  if (RunCommand) {
    if (RunInputFiles.empty()) {
      fprintf(stderr, "Error: run requires a file name.\n");
      return 1;
    }
    if (RunInputFiles.size() > 1) {
      fprintf(stderr, "Error: run accepts only one file name.\n");
      return 1;
    }
    const std::string &RunInputFile = RunInputFiles.front();
    (void)RunInputFile;
    (void)RunEmit;
    fprintf(stderr, "run: i havent learnt how to do that yet.\n");
    return 1;
  }

  if (BuildCommand) {
    fprintf(stderr, "build: introduced in chapter16.\n");
    return 1;

    if (BuildInputFiles.empty()) {
      fprintf(stderr, "Error: build requires a file name.\n");
      return 1;
    }
    if (BuildInputFiles.size() > 1) {
      fprintf(stderr, "Error: build accepts only one file name.\n");
      return 1;
    }
    const std::string &BuildInputFile = BuildInputFiles.front();
    (void)BuildDebug;

    if (!freopen(BuildInputFile.c_str(), "r", stdin)) {
      fprintf(stderr, "Error: could not open file '%s'.\n",
              BuildInputFile.c_str());
      return 1;
    }

    DiagSourceMgr.reset();
    LexLoc = {1, 0};
    CurLoc = {1, 0};
    FunctionProtos.clear();
    HadError = false;
    InteractiveMode = false;
    BuildObjectMode = true;
    CurrentOptLevel = BuildOptLevel;
    ShouldEmitIR = false;

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

    // Initialize all target info (needed for both optimization and code generation)
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();

    // Run optimization passes on the module if requested
    if (CurrentOptLevel > 0) {

      const std::string TargetTriple = sys::getDefaultTargetTriple();
      TheModule->setTargetTriple(Triple(TargetTriple));

      std::string Error;
      const Target *Target = TargetRegistry::lookupTarget(TargetTriple, Error);
      if (!Target) {
        errs() << Error << "\n";
        return 1;
      }

      TargetOptions Opts;
      std::unique_ptr<TargetMachine> TM(Target->createTargetMachine(
          Triple(TargetTriple), "generic", "", Opts, Reloc::PIC_));
      if (!TM) {
        errs() << "Could not create target machine.\n";
        return 1;
      }

      TheModule->setDataLayout(TM->createDataLayout());

      // Create the analysis managers
      LoopAnalysisManager LAM;
      FunctionAnalysisManager FAM;
      CGSCCAnalysisManager CGAM;
      ModuleAnalysisManager MAM;

      // Create the PassBuilder and register analyses
      PassBuilder PB(TM.get());
      PB.registerModuleAnalyses(MAM);
      PB.registerCGSCCAnalyses(CGAM);
      PB.registerFunctionAnalyses(FAM);
      PB.registerLoopAnalyses(LAM);
      PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

      // Build the appropriate optimization pipeline
      ModulePassManager MPM;
      if (CurrentOptLevel == 1) {
        MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O1);
      } else if (CurrentOptLevel == 2) {
        MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);
      } else {
        MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O3);
      }

      // Run the optimization pipeline
      MPM.run(*TheModule, MAM);
    }

    if (BuildEmit == EmitLLVMIR) {
      TheModule->print(outs(), nullptr);
      // Clean up debug info
      if (BuildDebug) {
        delete KSDbgInfo;
        KSDbgInfo = nullptr;
        DBuilder.reset();
      }
      return 0;
    }

    // For executable output, we need to emit an object file first, then link
    if (BuildEmit == EmitLink) {
      // Validate output path before doing any work
      const std::string ExePath = DeriveExecutableOutputPath(BuildInputFile);
      if (ExePath.empty()) {
        errs() << "Error: cannot derive executable name from '" << BuildInputFile
               << "': input file has no extension. Rename it with a .pyxc extension.\n";
        return 1;
      }

      const std::string ObjectPath = DeriveObjectOutputPath(BuildInputFile);
      bool success = EmitObjectFile(ObjectPath);
      if (!success) {
        if (BuildDebug) {
          delete KSDbgInfo;
          KSDbgInfo = nullptr;
          DBuilder.reset();
        }
        return 1;
      }

      // Look for runtime.o in the current directory
      std::string RuntimeObj = "runtime.o";
      std::error_code EC;
      if (!sys::fs::exists(RuntimeObj)) {
        // If not in current directory, check build directory
        RuntimeObj = "build/runtime.o";
        if (!sys::fs::exists(RuntimeObj)) {
          RuntimeObj = ""; // No runtime found, link without it
        }
      }
      success = LinkExecutable(ObjectPath, RuntimeObj, ExePath);

      // Clean up debug info
      if (BuildDebug) {
        delete KSDbgInfo;
        KSDbgInfo = nullptr;
        DBuilder.reset();
      }

      return success ? 0 : 1;
    }

    // Object file output
    const std::string OutputPath = DeriveObjectOutputPath(BuildInputFile);
    bool success = EmitObjectFile(OutputPath);

    // Clean up debug info
    if (BuildDebug) {
      delete KSDbgInfo;
      KSDbgInfo = nullptr;
      DBuilder.reset();
    }

    return success ? 0 : 1;
  }

  DiagSourceMgr.reset();

  if (ReplCommand && ReplEmit == EmitTokens) {
    EmitTokenStream();
    return 0;
  }

  HadError = false;
  InteractiveMode = true;
  BuildObjectMode = false;
  CurrentOptLevel = 0;
  if (InteractiveMode)
    fprintf(stderr, "ready> ");
  getNextToken();

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();
  TheJIT = ExitOnErr(PyxcJIT::Create());

  // Make the module, which holds all the code and optimization managers.
  InitializeModuleAndManagers();
  ShouldEmitIR = (ReplEmit == EmitLLVMIR);

  // Run the main "interpreter loop" now.
  MainLoop();

  return 0;
}

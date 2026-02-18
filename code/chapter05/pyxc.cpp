#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace llvm;

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
  tok_return = -8
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
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
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
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
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

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args)
      : Name(Name), Args(std::move(Args)) {}

  Function *codegen();
  const std::string &getName() const { return Name; }
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

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence = {{'<', 10}, {'>', 10}, {'+', 20},
                                              {'-', 20}, {'*', 40}, {'/', 40},
                                              {'%', 40}};

/// Explanation-friendly precedence anchors used by parser control flow.
static constexpr int NO_OP_PREC = -1;
static constexpr int MIN_BINOP_PREC = 1;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return NO_OP_PREC;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec < MIN_BINOP_PREC)
    return NO_OP_PREC;
  return TokPrec;
}

using ExprPtr = std::unique_ptr<ExprAST>;
using ProtoPtr = std::unique_ptr<PrototypeAST>;
using FuncPtr = std::unique_ptr<FunctionAST>;

/// LogError - Unified error reporting template.
template <typename T = void> T LogError(const char *Str) {
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

  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
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
  if (auto E = ParseExpression()) {
    // Make an anonymous proto.
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
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

Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  Value *V = NamedValues[Name];
  if (!V)
    return LogError<Value *>("Unknown variable name");
  return V;
}

Value *BinaryExprAST::codegen() {
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
  default:
    return LogError<Value *>("invalid binary operator");
  }
}

Value *CallExprAST::codegen() {
  // Look up the name in the global module table.
  Function *CalleeF = TheModule->getFunction(Callee);
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
  // Make the function type:  double(double,double) etc.
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

Function *FunctionAST::codegen() {
  // First, check for an existing function from a previous 'extern' declaration.
  Function *TheFunction = TheModule->getFunction(Proto->getName());

  if (!TheFunction)
    TheFunction = Proto->codegen();

  if (!TheFunction)
    return nullptr;

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  if (Value *RetVal = Body->codegen()) {
    // Finish off the function.
    Builder->CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static void InitializeModule() {
  // Open a new context and module.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("pyxc jit", *TheContext);

  // Create a new builder for the module.
  Builder = std::make_unique<IRBuilder<>>(*TheContext);
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
      } else {
        fprintf(stderr, "Parsed a function definition\n");
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
      } else {
        fprintf(stderr, "Parsed an extern\n");
      }
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
      if (ShouldEmitIR) {
        FnIR->print(errs());
        fprintf(stderr, "\n");
      } else {
        fprintf(stderr, "Parsed a top-level expr\n");
      }

      // Remove the anonymous expression.
      FnIR->eraseFromParent();
    }
  } else {
    SynchronizeToLineBoundary();
  }
}

/// top ::= definition | external | expression | eol
static void MainLoop() {
  while (true) {
    // Don't print a prompt when we already know we're at EOF.
    if (CurTok == tok_eof)
      return;

    if (CurTok == tok_eol) {
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
      HandleTopLevelExpression();
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

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main(int argc, const char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::HideUnrelatedOptions(PyxcCategory, ReplCommand);
  cl::HideUnrelatedOptions(PyxcCategory, RunCommand);
  cl::HideUnrelatedOptions(PyxcCategory, BuildCommand);
  cl::ParseCommandLineOptions(argc, argv, "pyxc chapter05\n");

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
    if (BuildInputFiles.empty()) {
      fprintf(stderr, "Error: build requires a file name.\n");
      return 1;
    }
    if (BuildInputFiles.size() > 1) {
      fprintf(stderr, "Error: build accepts only one file name.\n");
      return 1;
    }
    const std::string &BuildInputFile = BuildInputFiles.front();
    (void)BuildInputFile;
    (void)BuildEmit;
    (void)BuildDebug;
    (void)BuildOptLevel;
    fprintf(stderr, "build: i havent learnt how to do that yet.\n");
    return 1;
  }

  DiagSourceMgr.reset();

  if (ReplCommand && ReplEmit == EmitTokens) {
    EmitTokenStream();
    return 0;
  }

  // Prime the first token.
  fprintf(stderr, "ready> ");
  getNextToken();

  // Make the module, which holds all the code.
  InitializeModule();
  ShouldEmitIR = (ReplEmit == EmitLLVMIR);

  // Run the main "interpreter loop" now.
  MainLoop();

  if (ShouldEmitIR)
    TheModule->print(errs(), nullptr);

  return 0;
}

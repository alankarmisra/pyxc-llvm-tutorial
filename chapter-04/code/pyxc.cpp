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

using namespace std;

//===----------------------------------------------------------------------===//
// Color
//===----------------------------------------------------------------------===//
static bool UseColor = isatty(fileno(stderr));
static const char *Red = UseColor ? "\x1b[31m" : "";
static const char *Bold = UseColor ? "\x1b[1m" : "";
static const char *Reset = UseColor ? "\x1b[0m" : "";

//===----------------------------------------------------------------------===//
// Command line
//===----------------------------------------------------------------------===//
static llvm::cl::SubCommand ReplCommand("repl", "Start the interactive REPL");
static llvm::cl::SubCommand RunCommand("run", "Run a .pyxc script");
static llvm::cl::SubCommand BuildCommand("build", "Build a .pyxc script");
static llvm::cl::OptionCategory PyxcCategory("Pyxc options");

enum EmitMode { EmitDefault, EmitTokens, EmitLLVMIR, EmitObj, EmitExe };

static llvm::cl::opt<EmitMode>
    ReplEmit("emit", llvm::cl::sub(ReplCommand),
             llvm::cl::desc("Output kind for repl"),
             llvm::cl::values(
                 clEnumValN(EmitTokens, "tokens", "Print lexer tokens"),
                 clEnumValN(EmitLLVMIR, "llvm-ir", "Emit LLVM IR from REPL input")),
             llvm::cl::init(EmitDefault), llvm::cl::cat(PyxcCategory));

static llvm::cl::list<string> RunInputFiles(llvm::cl::Positional,
                                            llvm::cl::sub(RunCommand),
                                            llvm::cl::desc("<script.pyxc>"),
                                            llvm::cl::ZeroOrMore,
                                            llvm::cl::cat(PyxcCategory));
static llvm::cl::opt<EmitMode>
    RunEmit("emit", llvm::cl::sub(RunCommand),
            llvm::cl::desc("Output kind for run"),
            llvm::cl::values(
                clEnumValN(EmitLLVMIR, "llvm-ir", "Emit LLVM IR for the script")),
            llvm::cl::init(EmitDefault), llvm::cl::cat(PyxcCategory));

static llvm::cl::list<string> BuildInputFiles(llvm::cl::Positional,
                                              llvm::cl::sub(BuildCommand),
                                              llvm::cl::desc("<script.pyxc>"),
                                              llvm::cl::ZeroOrMore,
                                              llvm::cl::cat(PyxcCategory));
static llvm::cl::opt<EmitMode> BuildEmit(
    "emit", llvm::cl::sub(BuildCommand),
    llvm::cl::desc("Output kind for build"),
    llvm::cl::values(clEnumValN(EmitLLVMIR, "llvm-ir", "Emit LLVM IR"),
                     clEnumValN(EmitObj, "obj", "Emit object file"),
                     clEnumValN(EmitExe, "exe", "Emit executable")),
    llvm::cl::init(EmitExe), llvm::cl::cat(PyxcCategory));
static llvm::cl::opt<bool> BuildDebug("g", llvm::cl::sub(BuildCommand),
                                      llvm::cl::desc("Emit debug info"),
                                      llvm::cl::init(false),
                                      llvm::cl::cat(PyxcCategory));
static llvm::cl::opt<unsigned>
    BuildOptLevel("O", llvm::cl::sub(BuildCommand),
                  llvm::cl::desc("Optimization level (use -O0..-O3)"),
                  llvm::cl::Prefix, llvm::cl::init(0),
                  llvm::cl::cat(PyxcCategory));

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  tok_def = -4,
  tok_extern = -5,

  tok_identifier = -6,
  tok_number = -7,

  tok_return = -8
};

static string IdentifierStr;
static double NumVal;
static string NumLiteralStr;

static map<string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};

static map<int, string> TokenNames = [] {
  map<int, string> Names = {
      {tok_eof, "end of input"},
      {tok_eol, "newline"},
      {tok_error, "error"},
      {tok_def, "'def'"},
      {tok_extern, "'extern'"},
      {tok_identifier, "identifier"},
      {tok_number, "number"},
      {tok_return, "'return'"},
  };

  for (int C = 0; C <= 255; ++C) {
    if (isprint(static_cast<unsigned char>(C)))
      Names[C] = "'" + string(1, static_cast<char>(C)) + "'";
    else if (C == '\n')
      Names[C] = "'\\n'";
    else if (C == '\t')
      Names[C] = "'\\t'";
    else if (C == '\r')
      Names[C] = "'\\r'";
    else if (C == '\0')
      Names[C] = "'\\0'";
    else {
      ostringstream OS;
      OS << "0x" << uppercase << hex << setw(2) << setfill('0') << C;
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
  vector<string> CompletedLines;
  string CurrentLine;

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

  const string *getLine(int OneBasedLine) const {
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

static int gettok() {
  static int LastChar = ' ';

  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  if (LastChar == '\n') {
    CurLoc = {LexLoc.Line - 1, LexLoc.Col};
    LastChar = ' ';
    return tok_eol;
  }

  CurLoc = LexLoc;

  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
      IdentifierStr += LastChar;

    auto It = Keywords.find(IdentifierStr);
    return (It == Keywords.end()) ? tok_identifier : It->second;
  }

  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    NumLiteralStr = NumStr;
    char *End = nullptr;
    NumVal = strtod(NumStr.c_str(), &End);
    if (!End || *End != '\0') {
      fprintf(stderr,
              "%sError%s (Line %d, Column %d): invalid number literal '%s'\n",
              Red, Reset, CurLoc.Line, CurLoc.Col, NumStr.c_str());
      return tok_error;
    }
    return tok_number;
  }

  if (LastChar == '#') {
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF) {
      LastChar = ' ';
      return tok_eol;
    }
  }

  if (LastChar == EOF)
    return tok_eof;

  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

//===----------------------------------------------------------------------===//
// Diagnostics helpers
//===----------------------------------------------------------------------===//

static int CurTok;

static SourceLocation GetDiagnosticAnchorLoc(SourceLocation Loc, int Tok) {
  if (Tok != tok_eol)
    return Loc;

  int PrevLine = Loc.Line - 1;
  if (PrevLine <= 0)
    return Loc;

  const string *PrevLineText = DiagSourceMgr.getLine(PrevLine);
  if (!PrevLineText)
    return Loc;

  return {PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}

static string FormatTokenForMessage(int Tok) {
  if (Tok == tok_identifier)
    return "identifier '" + IdentifierStr + "'";
  if (Tok == tok_number)
    return "number '" + NumLiteralStr + "'";

  auto It = TokenNames.find(Tok);
  if (It != TokenNames.end())
    return It->second;
  return "unknown token";
}

static void PrintErrorSourceContext(SourceLocation Loc) {
  const string *LineText = DiagSourceMgr.getLine(Loc.Line);
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int Spaces = Loc.Col - 1;
  if (Spaces < 0)
    Spaces = 0;
  for (int I = 0; I < Spaces; ++I)
    fputc(' ', stderr);
  fprintf(stderr, "%s^%s~~~\n", Bold, Reset);
}

template <typename T> static T LogError(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "%sError%s (Line %d, Column %d): %s\n", Red, Reset,
          Anchor.Line, Anchor.Col, Str);
  PrintErrorSourceContext(Anchor);
  return nullptr;
}

static void LogErrorV(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "%sError%s (Line %d, Column %d): %s\n", Red, Reset,
          Anchor.Line, Anchor.Col, Str);
  PrintErrorSourceContext(Anchor);
}

template <typename T>
static T LogErrorAt(SourceLocation Loc, int TokForAnchor, const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(Loc, TokForAnchor);
  fprintf(stderr, "%sError%s (Line %d, Column %d): %s\n", Red, Reset,
          Anchor.Line, Anchor.Col, Str);
  PrintErrorSourceContext(Anchor);
  return nullptr;
}

static int getNextToken() { return CurTok = gettok(); }

//===----------------------------------------------------------------------===//
// AST
//===----------------------------------------------------------------------===//

namespace {

class ExprAST {
public:
  virtual ~ExprAST() = default;
};

class NumberExprAST : public ExprAST {
  double Val;

public:
  explicit NumberExprAST(double Val) : Val(Val) {}
  double getValue() const { return Val; }
};

class VariableExprAST : public ExprAST {
  string Name;

public:
  explicit VariableExprAST(string Name) : Name(std::move(Name)) {}
  const string &getName() const { return Name; }
};

class BinaryExprAST : public ExprAST {
  char Op;
  unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;

public:
  CallExprAST(string Callee, vector<unique_ptr<ExprAST>> Args)
      : Callee(std::move(Callee)), Args(std::move(Args)) {}
};

class PrototypeAST {
  string Name;
  vector<string> Args;

public:
  PrototypeAST(string Name, vector<string> Args)
      : Name(std::move(Name)), Args(std::move(Args)) {}

  const string &getName() const { return Name; }
};

class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;

public:
  FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

} // namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

static map<char, int> BinopPrecedence;

static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  int TokPrec = BinopPrecedence[static_cast<char>(CurTok)];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

static unique_ptr<ExprAST> ParseExpression();

static unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return Result;
}

static unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat '('
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != ')')
    return LogError<unique_ptr<ExprAST>>("expected ')'");

  getNextToken(); // eat ')'
  return V;
}

static unique_ptr<ExprAST> ParseIdentifierExpr() {
  string IdName = IdentifierStr;
  getNextToken(); // eat identifier

  if (CurTok != '(')
    return make_unique<VariableExprAST>(IdName);

  getNextToken(); // eat '('
  vector<unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (CurTok == ')')
        return LogError<unique_ptr<ExprAST>>(
            "Unexpected ')' when expecting an expression");

      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError<unique_ptr<ExprAST>>("Expected ')' or ',' in argument list");

      getNextToken();
    }
  }

  getNextToken(); // eat ')'
  return make_unique<CallExprAST>(IdName, std::move(Args));
}

static unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError<unique_ptr<ExprAST>>(
        ("Unexpected " + FormatTokenForMessage(CurTok) +
         " when expecting an expression")
            .c_str());
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(': 
    return ParseParenExpr();
  }
}

static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                         unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();
    if (TokPrec < ExprPrec)
      return LHS;

    int BinOp = CurTok;
    getNextToken();

    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

static unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

static unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogError<unique_ptr<PrototypeAST>>("Expected function name in prototype");

  string FnName = IdentifierStr;
  SourceLocation OpenLoc = CurLoc;
  getNextToken();

  if (CurTok != '(')
    return LogError<unique_ptr<PrototypeAST>>("Expected '(' in prototype");

  getNextToken();
  vector<string> ArgNames;

  if (CurTok != ')') {
    while (true) {
      if (CurTok != tok_identifier)
        return LogErrorAt<unique_ptr<PrototypeAST>>(OpenLoc, CurTok,
                                                    "Expected ')' or ',' in parameter list");
      ArgNames.push_back(IdentifierStr);
      getNextToken();

      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogErrorAt<unique_ptr<PrototypeAST>>(OpenLoc, CurTok,
                                                    "Expected ')' or ',' in parameter list");
      getNextToken();
    }
  }

  getNextToken(); // eat ')'
  return make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat def

  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogError<unique_ptr<FunctionAST>>("Expected ':' in function definition");
  getNextToken();
  while (CurTok == tok_eol)
    getNextToken();

  if (CurTok != tok_return)
    return LogError<unique_ptr<FunctionAST>>(
        "Expected 'return' before return expression");
  getNextToken();

  auto E = ParseExpression();
  if (!E)
    return nullptr;

  return make_unique<FunctionAST>(std::move(Proto), std::move(E));
}

static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern

  if (CurTok != tok_def)
    return LogError<unique_ptr<PrototypeAST>>("Expected `def` after extern");

  getNextToken(); // eat def
  return ParsePrototype();
}

static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = make_unique<PrototypeAST>("__anon_expr", vector<string>());
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-level parsing driver
//===----------------------------------------------------------------------===//

static void SynchronizeToLineBoundary() {
  while (CurTok != tok_eol && CurTok != tok_eof)
    getNextToken();
}

static void HandleDefinition() {
  if (ParseDefinition()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      LogErrorV(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
      SynchronizeToLineBoundary();
      return;
    }
    fprintf(stderr, "Parsed a function definition\n");
  } else {
    SynchronizeToLineBoundary();
  }
}

static void HandleExtern() {
  if (ParseExtern()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      LogErrorV(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
      SynchronizeToLineBoundary();
      return;
    }
    fprintf(stderr, "Parsed an extern\n");
  } else {
    SynchronizeToLineBoundary();
  }
}

static void HandleTopLevelExpression() {
  if (ParseTopLevelExpr()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      LogErrorV(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
      SynchronizeToLineBoundary();
      return;
    }
    fprintf(stderr, "Parsed a top-level expr\n");
  } else {
    SynchronizeToLineBoundary();
  }
}

static void MainLoop() {
  while (true) {
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

static const string &GetTokenName(int Tok) {
  const auto TokIt = TokenNames.find(Tok);
  if (TokIt != TokenNames.end())
    return TokIt->second;

  static const string Unknown = "tok_unknown";
  return Unknown;
}

static void EmitTokenStream() {
  DiagSourceMgr.reset();
  fprintf(stderr, "ready> ");

  while (true) {
    int Tok = gettok();
    if (Tok == tok_eof)
      return;

    fprintf(stderr, "%s", GetTokenName(Tok).c_str());
    if (Tok == tok_eol)
      fprintf(stderr, "\nready> ");
    else
      fprintf(stderr, " ");
  }
}

int main(int argc, const char **argv) {
  llvm::cl::HideUnrelatedOptions(PyxcCategory);
  llvm::cl::HideUnrelatedOptions(PyxcCategory, ReplCommand);
  llvm::cl::HideUnrelatedOptions(PyxcCategory, RunCommand);
  llvm::cl::HideUnrelatedOptions(PyxcCategory, BuildCommand);
  llvm::cl::ParseCommandLineOptions(argc, argv, "pyxc chapter04\n");

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
    const string &RunInputFile = RunInputFiles.front();
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
    const string &BuildInputFile = BuildInputFiles.front();
    (void)BuildInputFile;
    (void)BuildEmit;
    (void)BuildDebug;
    (void)BuildOptLevel;
    fprintf(stderr, "build: i havent learnt how to do that yet.\n");
    return 1;
  }

  if (ReplCommand && ReplEmit == EmitTokens) {
    EmitTokenStream();
    return 0;
  }

  if (ReplCommand && ReplEmit == EmitLLVMIR) {
    fprintf(stderr, "repl --emit=llvm-ir: i havent learnt how to do that yet.\n");
  }

  BinopPrecedence['<'] = 10;
  BinopPrecedence['>'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;
  BinopPrecedence['/'] = 40;
  BinopPrecedence['%'] = 40;

  DiagSourceMgr.reset();

  fprintf(stderr, "ready> ");
  getNextToken();
  MainLoop();
  return 0;
}

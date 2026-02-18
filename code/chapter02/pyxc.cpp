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

static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number
static string NumLiteralStr; // Filled in if tok_number

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static map<string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
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

  // Single character tokens.
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

static string FormatTokenForMessage(int Tok) {
  if (Tok == tok_identifier)
    return "identifier '" + IdentifierStr + "'";
  if (Tok == tok_number)
    return "number '" + NumLiteralStr + "'";

  const auto TokIt = TokenNames.find(Tok);
  if (TokIt != TokenNames.end())
    return TokIt->second;
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
  fprintf(stderr, "%s^%s", Bold, Reset);
  fputc('~', stderr);
  fputc('~', stderr);
  fputc('~', stderr);
  fputc('\n', stderr);
}

static SourceLocation GetNewlineTokenLoc(SourceLocation Loc) {
  int PrevLine = Loc.Line - 1;
  if (PrevLine <= 0)
    return Loc;

  const string *PrevLineText = DiagSourceMgr.getLine(PrevLine);
  if (!PrevLineText)
    return Loc;

  return SourceLocation{PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}

static SourceLocation GetDiagnosticAnchorLoc(SourceLocation Loc, int Tok) {
  if (Tok != tok_eol)
    return Loc;

  // Newline tokens should already be anchored to the previous line-end. Keep
  // a fallback for any remaining boundary-style locations.
  if (Loc.Col > 0)
    return Loc;

  return GetNewlineTokenLoc(Loc);
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
    CurLoc = GetNewlineTokenLoc(LexLoc);
    LastChar = ' ';
    return tok_eol;
  }

  CurLoc = LexLoc;

  // identifier terminal rule: identifier = /[A-Za-z_][A-Za-z0-9_]*/
  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
      IdentifierStr += LastChar;

    // Is this a known keyword? If yes, return that.
    // Else it is an ordinary identifier.
    const auto it = Keywords.find(IdentifierStr);
    return (it != Keywords.end()) ? it->second : tok_identifier;
  }

  // number terminal rule: number = /[0-9]+(\.[0-9]+)?/
  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    NumLiteralStr = NumStr;
    char *End;
    NumVal = strtod(NumStr.c_str(), &End);
    if (*End != '\0') {
      fprintf(stderr,
              "%sError%s (Line %d, Column %d): invalid number literal '%s'\n",
              Red, Reset, CurLoc.Line, CurLoc.Col, NumStr.c_str());
      return tok_error;
    }
    return tok_number;
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF) {
      CurLoc = GetNewlineTokenLoc(LexLoc);
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
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  string Name;

public:
  VariableExprAST(const string &Name) : Name(Name) {}
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const string &Callee, vector<unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
class PrototypeAST {
  string Name;
  vector<string> Args;

public:
  PrototypeAST(const string &Name, vector<string> Args)
      : Name(Name), Args(std::move(Args)) {}

  const string &getName() const { return Name; }
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;

public:
  FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

} // end anonymous namespace

using ExprPtr = unique_ptr<ExprAST>;
using ProtoPtr = unique_ptr<PrototypeAST>;
using FuncPtr = unique_ptr<FunctionAST>;

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
static map<char, int> BinopPrecedence = {{'<', 10}, {'+', 20}, {'-', 20},
                                         {'*', 40}, {'/', 40}, {'%', 40}};

/// Explanation-friendly precedence anchors used by parser control flow.
static constexpr int NO_OP_PREC = -1;
static constexpr int MIN_BINOP_PREC = 1; // one higher than NO_OP_PREC

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

template <typename T = void> T LogError(const char *Str) {
  SourceLocation DiagLoc = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "%sError%s (Line: %d, Column: %d): %s\n", Red, Reset,
          DiagLoc.Line, DiagLoc.Col, Str);
  PrintErrorSourceContext(DiagLoc);
  if constexpr (is_void_v<T>)
    return;
  else if constexpr (is_pointer_v<T>)
    return nullptr;
  else
    return T{};
}

static unique_ptr<ExprAST> ParseExpression();

/// numberexpr = number
static unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}

/// parenexpr = '(' expression ')'
static unique_ptr<ExprAST> ParseParenExpr() {
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
///   = identifier
///   = identifier '(' [ expression { ',' expression } ] ')'
static unique_ptr<ExprAST> ParseIdentifierExpr() {
  string IdName = IdentifierStr;

  getNextToken(); // eat identifier.

  if (CurTok != '(') // Simple variable ref.
    return make_unique<VariableExprAST>(IdName);

  // Call.
  getNextToken(); // eat (
  vector<unique_ptr<ExprAST>> Args;
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

  return make_unique<CallExprAST>(IdName, std::move(Args));
}

/// primary
///   = identifierexpr
///   = numberexpr
///   = parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  case tok_error:
    // Lexer has already emitted a specific diagnostic for this token.
    return nullptr;
  default: {
    string Msg = "Unexpected " + FormatTokenForMessage(CurTok) +
                 " when expecting an expression";
    return LogError<ExprPtr>(Msg.c_str());
  }
  }
}

/// Parse helper (for expression): consumes { binaryop primary }
static unique_ptr<ExprAST> ParseBinOpRHS(int MinimumPrec,
                                         unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < MinimumPrec)
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
    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

/// expression
///   = primary { binaryop primary }
///
static unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  // Only parse further if the next token is a BinOp
  return ParseBinOpRHS(MIN_BINOP_PREC, std::move(LHS));
}

/// prototype
///   = identifier '(' [ identifier { ',' identifier } ] ')'
static unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogError<ProtoPtr>("Expected function name in prototype");

  string FnName = IdentifierStr;
  getNextToken();

  if (CurTok != '(')
    return LogError<ProtoPtr>("Expected '(' in prototype");

  vector<string> ArgNames;
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

  return make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition = 'def' prototype ':' expression
static unique_ptr<FunctionAST> ParseDefinition() {
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
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

/// toplevelexpr = expression
static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    // Make an anonymous proto.
    auto Proto = make_unique<PrototypeAST>("__anon_expr", vector<string>());
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

/// external = 'extern' 'def' prototype
static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurTok != tok_def)
    return LogError<ProtoPtr>("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParsePrototype();
}

//===----------------------------------------------------------------------===//
// Top-Level parsing
//===----------------------------------------------------------------------===//

static void SynchronizeToLineBoundary() {
  while (CurTok != tok_eol && CurTok != tok_eof)
    getNextToken();
}

static void HandleDefinition() {
  if (ParseDefinition()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      string Msg = "Unexpected " + FormatTokenForMessage(CurTok);
      LogError<void>(Msg.c_str());
      SynchronizeToLineBoundary();
      return;
    }
    fprintf(stderr, "Parsed a function definition\n");
  } else {
    // Error recovery: skip the rest of the current line so leftover tokens
    // from a malformed construct don't get parsed as a new top-level form.
    SynchronizeToLineBoundary();
  }
}

static void HandleExtern() {
  if (ParseExtern()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      string Msg = "Unexpected " + FormatTokenForMessage(CurTok);
      LogError<void>(Msg.c_str());
      SynchronizeToLineBoundary();
      return;
    }
    fprintf(stderr, "Parsed an extern\n");
  } else {
    SynchronizeToLineBoundary();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (ParseTopLevelExpr()) {
    if (CurTok != tok_eol && CurTok != tok_eof) {
      string Msg = "Unexpected " + FormatTokenForMessage(CurTok);
      LogError<void>(Msg.c_str());
      SynchronizeToLineBoundary();
      return;
    }
    fprintf(stderr, "Parsed a top-level expr\n");
  } else {
    SynchronizeToLineBoundary();
  }
}

/// mainloop = definition | external | expression | eol
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

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
  DiagSourceMgr.reset();

  // Prime the first token.
  fprintf(stderr, "ready> ");
  getNextToken();

  // Run the main "interpreter loop" now.
  MainLoop();

  return 0;
}

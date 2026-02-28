#include "../include/PyxcJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
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
#include <unistd.h>
#include <utility>
#include <vector>

using namespace std;
using namespace llvm;
using namespace llvm::orc;

//===----------------------------------------------------------------------===//
// Command line
//===----------------------------------------------------------------------===//
static cl::OptionCategory PyxcCategory("Pyxc options");

// Optional positional input: 0 args => REPL, 1 arg => file mode.
static cl::list<std::string> InputFiles(cl::Positional,
                                        cl::desc("[script.pyxc]"),
                                        cl::ZeroOrMore, cl::cat(PyxcCategory));

// Verbose IR dump in both REPL and file mode.
static cl::opt<bool> VerboseIR("v",
                               cl::desc("Print generated LLVM IR to stderr"),
                               cl::init(false), cl::cat(PyxcCategory));

static FILE *Input = stdin;
static bool IsRepl = true;

//===----------------------------------------===//
// Lexer
//===----------------------------------------===//

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

  // comparison operators
  tok_eq = -8,   // ==
  tok_neq = -9,  // !=
  tok_leq = -10, // <=
  tok_geq = -11, // >=

  // control
  tok_if = -12,
  tok_else = -13,
  tok_return = -14
};

static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number
static string NumLiteralStr; // Filled in if tok_number, used in error messages

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static map<string, Token> Keywords = {{"def", tok_def},
                                      {"extern", tok_extern},
                                      {"return", tok_return},
                                      {"if", tok_if},
                                      {"else", tok_else}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  static map<int, string> Names = {{tok_eof, "end of input"},
                                   {tok_eol, "newline"},
                                   {tok_error, "error"},
                                   {tok_def, "'def'"},
                                   {tok_extern, "'extern'"},
                                   {tok_identifier, "identifier"},
                                   {tok_number, "number"},
                                   {tok_return, "'return'"},
                                   {tok_eq, "'=='"},
                                   {tok_neq, "'!='"},
                                   {tok_leq, "'<='"},
                                   {tok_geq, "'>='"},
                                   {tok_if, "if"},
                                   {tok_else, "else"}};

  // Single character tokens.
  for (int ch = 0; ch <= 255; ++ch) {
    if (isprint(static_cast<unsigned char>(ch)))
      Names[ch] = "'" + string(1, static_cast<char>(ch)) + "'";
    else if (ch == '\n')
      Names[ch] = "'\\n'";
    else if (ch == '\t')
      Names[ch] = "'\\t'";
    else if (ch == '\r')
      Names[ch] = "'\\r'";
    else if (ch == '\0')
      Names[ch] = "'\\0'";
    else {
      ostringstream OS;
      OS << "0x" << uppercase << hex << setw(2) << setfill('0') << ch;
      Names[ch] = OS.str();
    }
  }

  return Names;
}();

// SourceLocation - A {Line, Col} pair. Line and Col are 1-based.
//
// Two globals track position as characters are consumed:
//   LexLoc  - where the character-read head (advance()) currently is.
//             Updated on every advance() call. After a '\n', Line increments
//             and Col resets to 0 so the next character will be Col 1.
//   CurLoc  - snapshotted at the start of each token in gettok(), before
//             consuming any of the token's characters. This is the position
//             the parser and diagnostics infrastructure see.
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

// SourceManager - Buffers every source line as it is read so that error
// messages can reprint the offending line with a caret underneath it.
//
// advance() calls onChar() for every character it consumes. When a '\n'
// arrives, the just-completed line is moved into CompletedLines and
// CurrentLine starts fresh. getLine(N) returns a pointer to the Nth line
// (1-based): completed lines are stable in the vector; the line currently
// being assembled is in CurrentLine.
//
// Because the REPL accumulates all input in one session, line numbers
// increase monotonically across inputs and getLine() can retrieve any
// previously seen line — useful for multi-line function bodies and for
// pointing the caret at a line that was parsed several inputs ago.
class SourceManager {
  vector<string> CompletedLines;
  string CurrentLine;

public:
  void reset() {
    CompletedLines.clear();
    CurrentLine.clear();
  }

  // Called by advance() for every character consumed from the input.
  void onChar(int C) {
    if (C == '\n') {
      CompletedLines.push_back(CurrentLine);
      CurrentLine.clear();
      return;
    }
    if (C != EOF)
      CurrentLine.push_back(static_cast<char>(C));
  }

  // Returns a pointer to the text of line OneBasedLine, or nullptr if out of
  // range. The pointer is stable for completed lines; CurrentLine may move if
  // more characters arrive, so callers should not hold it across advance().
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

static SourceManager PyxcSourceMgr;
static void PrintErrorSourceContext(SourceLocation Loc);

/// advance - Read one character from stdin, update LexLoc and SourceManager.
///
/// This is the single point through which all character consumption flows.
/// Every token branch in gettok() calls advance() rather than fgetc()
/// directly, so LexLoc and the source buffer are always in sync.
///
/// Windows line endings (\r\n) are coalesced to a single \n so the rest of
/// the lexer never needs to handle \r.
static int advance() {
  int LastChar = fgetc(Input);
  if (LastChar == '\r') {
    int NextChar = fgetc(Input);
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, Input);
    PyxcSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }

  if (LastChar == '\n') {
    PyxcSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    PyxcSourceMgr.onChar(LastChar);
    LexLoc.Col++;
  }

  return LastChar;
}

static int peek() {
  int C = fgetc(Input);
  ungetc(C, Input);
  return C;
}

/// gettok - Return the next token from standard input.
///
/// LastChar holds the last character read by advance() but not yet consumed
/// by a token. It is initialised to ' ' so the first call skips straight to
/// the whitespace loop without reading a character, and the loop's first
/// advance() call picks up the real first character.
///
/// CurLoc is snapshotted from LexLoc after the whitespace-skip loop and
/// before any token branch. For most tokens this points at the first
/// character of the token. For tok_eol the '\n' was already consumed by
/// advance() on a previous call, so LexLoc is already on the next line;
/// GetDiagnosticAnchorLoc compensates by subtracting one when building error
/// locations for tok_eol.
///
/// The comment path ('#' branch) re-snapshots CurLoc just before returning
/// tok_eol because it consumes many characters (the whole comment) after the
/// initial snapshot, leaving LexLoc well past the '#' position.
static int gettok() {
  static int LastChar = ' ';

  // Skip horizontal whitespace. Stop at '\n' — that becomes tok_eol.
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // Snapshot position for the upcoming token. See note above about tok_eol.
  CurLoc = LexLoc;

  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }

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
              "Error (Line %d, Column %d): invalid number literal '%s'\n",
              CurLoc.Line, CurLoc.Col, NumStr.c_str());
      PrintErrorSourceContext(CurLoc);
      return tok_error;
    }
    return tok_number;
  }

  if (LastChar == '#') {
    // Consume the rest of the line (comment). Stop at '\n' or EOF.
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF) {
      // Re-snapshot CurLoc now that the '\n' has been consumed and LexLoc
      // has advanced to the next line. Without this, CurLoc would point at
      // the '#' column, and GetDiagnosticAnchorLoc would look up the wrong
      // line when the next token triggers an error.
      CurLoc = LexLoc;
      LastChar = ' ';
      return tok_eol;
    }
  }

  if (LastChar == '=') {
    int Tok = (peek() == '=') ? (advance(), tok_eq) : '=';
    LastChar = advance();
    return Tok;
  }

  if (LastChar == '!') {
    int Tok = (peek() == '=') ? (advance(), tok_neq) : '!';
    LastChar = advance();
    return Tok;
  }

  if (LastChar == '<') {
    int Tok = (peek() == '=') ? (advance(), tok_leq) : '<';
    LastChar = advance();
    return Tok;
  }

  if (LastChar == '>') {
    int Tok = (peek() == '=') ? (advance(), tok_geq) : '>';
    LastChar = advance();
    return Tok;
  }

  if (LastChar == EOF)
    return tok_eof;

  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

//===----------------------------------------===//
// Diagnostics helpers
//===----------------------------------------===//

/// GetDiagnosticAnchorLoc - Resolve the source location to attach to an error.
///
/// For most tokens, CurLoc already points at the right place and is returned
/// unchanged. The special case is tok_eol: CurLoc for a newline token is
/// snapshotted after advance() has consumed the '\n' and incremented
/// LexLoc.Line, so CurLoc.Line is already the *next* line. Subtracting one
/// gives the line that just ended, and we report a column one past its last
/// character — pointing just after the final token on the line, which is
/// where the missing token (e.g. ':') should have appeared.
static SourceLocation GetDiagnosticAnchorLoc(SourceLocation Loc, int Tok) {
  if (Tok != tok_eol)
    return Loc;

  int PrevLine = Loc.Line - 1;
  if (PrevLine <= 0)
    return Loc;

  const string *PrevLineText = PyxcSourceMgr.getLine(PrevLine);
  if (!PrevLineText)
    return Loc;

  return {PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}

/// FormatTokenForMessage - Return a human-readable description of Tok for use
/// in error messages. Identifier and number tokens include their actual text
/// (e.g. "identifier 'foo'", "number '3.14'") since the name alone is not
/// enough to diagnose the problem. Everything else uses the static TokenNames
/// entry.
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

/// PrintErrorSourceContext - Reprint the source line at Loc and place a
/// '^~~~' caret under column Loc.Col. Col is 1-based, so we print Col-1
/// spaces before the caret.
static void PrintErrorSourceContext(SourceLocation Loc) {
  const string *LineText = PyxcSourceMgr.getLine(Loc.Line);
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = Loc.Col - 1;
  if (spaces < 0)
    spaces = 0;
  for (int i = 0; i < spaces; ++i)
    fputc(' ', stderr);
  fprintf(stderr, "^~~~\n");
}

//===----------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------===//
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
  string Name;

public:
  VariableExprAST(const string &Name) : Name(Name) {}
  Value *codegen() override;
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  Value *codegen() override;
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const string &Callee, vector<unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
  Value *codegen() override;
};

/// IfExprAST - Expression class for if/else.
class IfExprAST : public ExprAST {
  unique_ptr<ExprAST> Cond, Then, Else;

public:
  IfExprAST(unique_ptr<ExprAST> Cond, unique_ptr<ExprAST> Then,
            unique_ptr<ExprAST> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
  Value *codegen() override;
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
  Function *codegen();
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;

public:
  FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
  Function *codegen();
};

} // end anonymous namespace

//===----------------------------------------===//
// Parser
//===----------------------------------------===//

/// CurTok is the current token the parser is looking at.
/// getNextToken reads the next token from the lexer and stores it in CurTok.
/// Every parse function assumes CurTok is already loaded before it is called,
/// and leaves CurTok pointing at the first token it did not consume.
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

/// BinopPrecedence - Maps each binary operator character to its precedence.
/// Higher numbers bind more tightly: '*' (40) > '+'/'-' (20) > '<' (10).
/// Operators not in this map return -1 from GetTokPrecedence(), which tells
/// ParseBinOpRHS to stop consuming operators and return what it has so far.
static map<int, int> BinopPrecedence = {
    {tok_eq, 10},  // ==
    {tok_neq, 10}, // !=
    {tok_leq, 10}, // <=
    {tok_geq, 10}, // >=
    {'<', 10},     // <
    {'>', 10},     // >
    {'+', 20},     // +
    {'-', 20},     // -
    {'*', 40},     // *
};

/// GetTokPrecedence - Returns the precedence of CurTok if it is a known binary
/// operator, or -1 if it is not. Non-ASCII tokens (our named token enums) are
/// rejected immediately since they can never be binary operators here.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

void PrintConsoleReady() {
  if (IsRepl)
    fprintf(stderr, "ready> ");
}

void Log(const string &message) {
  if (IsRepl)
    fprintf(stderr, "%s", message.c_str());
}

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogError("message");
unique_ptr<ExprAST> LogError(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Col,
          Str);
  PrintErrorSourceContext(Anchor);
  PrintConsoleReady();
  return nullptr;
}

unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

unique_ptr<FunctionAST> LogErrorF(const char *Str) {
  LogError(Str);
  return nullptr;
}

static unique_ptr<ExprAST> ParseExpression();

/// numberexpr
///   = number ;
static unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}

/// parenexpr
///   = "(" expression ")" ;
static unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != ')')
    return LogError("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// identifierexpr
///   = identifier
///   | identifier "("[expression{"," expression}]")" ;
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
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return make_unique<CallExprAST>(IdName, std::move(Args));
}

/// ifexpr
///   = "if" expression ":" expression "else" ":" expression ;
static unique_ptr<ExprAST> ParseIfExpr() {
  getNextToken(); // eat 'if'

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError("Expected ':' after if condition");
  getNextToken(); // eat ':'

  // Allow body on next line
  while (CurTok == tok_eol)
    getNextToken();

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  // Allow 'else' on next line
  while (CurTok == tok_eol)
    getNextToken();

  if (CurTok != tok_else)
    return LogError("Expected 'else' in if expression");
  getNextToken(); // eat 'else'

  if (CurTok != ':')
    return LogError("Expected ':' after else");
  getNextToken(); // eat ':'

  // Allow body on next line
  while (CurTok == tok_eol)
    getNextToken();

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return make_unique<IfExprAST>(std::move(Cond), std::move(Then),
                                std::move(Else));
}

/// primary
///   = identifierexpr
///   | numberexpr
///   | parenexpr ;
static unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError("unknown token when expecting an expression");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  case tok_if:
    return ParseIfExpr();
  }
}

/// binoprhs
///   = { binaryop primary } ;
static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                         unique_ptr<ExprAST> LHS) {
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
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    // Merge LHS/RHS.
    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

/// expression
///   = primary binoprhs ;
static unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///   = identifier "(" [identifier {"," identifier}] ")" ;
static unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");

  string FnName = IdentifierStr;
  getNextToken(); // eat function name

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");

  // Parse argument names. The loop calls getNextToken() at the top to advance
  // past '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body we call getNextToken() again to move past the identifier
  // we just stored, then check whether ')' or ',' follows.
  vector<string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);

    if (getNextToken() == ')') // eat identifier, check what follows
      break;

    if (CurTok != ',')
      return LogErrorP("Expected ')' or ',' in parameter list");
    // loop continues: getNextToken() at the top eats the ','
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype");

  getNextToken(); // eat ')'

  return make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition
///   = "def" prototype ":" ["newline"] "return" expression ;
static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // Skip any newlines between ':' and 'return'. This allows the body to be
  // written on the next line:
  //   def foo(x):
  //     return x + 1
  while (CurTok == tok_eol)
    getNextToken();

  if (CurTok != tok_return)
    return LogErrorF("Expected 'return' in function body");
  getNextToken(); // eat 'return'

  if (auto E = ParseExpression())
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

/// toplevelexpr
///   = expression
/// A top-level expression (e.g. "1 + 2") is wrapped in an anonymous function
/// so it fits the same FunctionAST shape as everything else.
/// HandleTopLevelExpression compiles it into the JIT, calls it to get the
/// numeric result, then removes it from the JIT via a ResourceTracker.
static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = make_unique<PrototypeAST>("__anon_expr", vector<string>());
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

/// external
///   = "extern" "def" prototype
static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurTok != tok_def)
    return LogErrorP("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParsePrototype();
}

//===----------------------------------------===//
// Code Generation
//===----------------------------------------===//

// TheContext/TheModule/Builder/NamedValues - Core IR construction globals.
// Recreated fresh for each new module (see InitializeModuleAndManagers).
//
// TheContext - Owns all LLVM data structures: types, constants, and the
// interning tables that ensure two uses of 'double' resolve to the same
// object.
//
// TheModule - The unit of compilation handed to the JIT. Because the JIT
// takes ownership of the module when a function is compiled, we create a
// new module for every top-level input. Functions defined in earlier modules
// remain callable via the JIT's symbol table.
//
// Builder - A cursor into the IR being built. Point it at a BasicBlock with
// SetInsertPoint(), then call Create* methods to append instructions.
//
// NamedValues - Symbol table for the current function's parameters. Cleared
// and repopulated at the start of each function body. Mutable local
// variables come in a later chapter.
//
// TheJIT - The ORC JIT instance. Created once in main() and lives for the
// whole session. Compiled modules are added to it; symbols from C libraries
// (e.g. sin, cos) are resolved through the process's dynamic symbol table.
//
// TheFPM / TheLAM / TheFAM / TheCGAM / TheMAM - The new-PM pass and
// analysis managers. TheFPM holds the optimisation pipeline; the analysis
// managers cache analysis results and are cross-registered so passes that
// need loop or CGSCC analyses can find them.
//
// ThePIC / TheSI - Pass instrumentation plumbing required by the new PM.
// StandardInstrumentations registers the built-in timing and printing hooks.
//
// FunctionProtos - Persistent prototype registry. Because each function
// lands in its own module, a later module that calls 'foo' cannot find 'foo'
// in TheModule->getFunction(). FunctionProtos stores the PrototypeAST for
// every declared or defined function so getFunction() can re-emit a
// declaration into the current module on demand.
//
// ExitOnErr - Convenience wrapper that terminates the process on a
// recoverable LLVM error. Used for JIT operations that should never fail
// in a correct implementation.
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> NamedValues;
static std::unique_ptr<PyxcJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
static ExitOnError ExitOnErr;

// LogErrorV - Codegen-level error helper. Delegates to LogError for printing,
// then returns nullptr so codegen callers can write: return LogErrorV("msg");
Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

/// getFunction - Resolve a function name to an LLVM Function* in the current
/// module, re-emitting a declaration from FunctionProtos if necessary.
///
/// Because each top-level input gets its own Module, a function defined in an
/// earlier module is no longer in TheModule->getFunction(). When that happens
/// we look up its PrototypeAST in FunctionProtos and call codegen() on it,
/// which emits a fresh 'declare' with ExternalLinkage in the current module.
/// The JIT resolves that extern to the already-compiled body at link time.
Function *getFunction(std::string Name) {
  // Fast path: declaration or definition already in the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // Slow path: re-emit a declaration from the saved prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  return nullptr;
}

/// NumberExprAST::codegen - A numeric literal becomes a floating-point
/// constant value.
///
/// ConstantFP::get wraps an APFloat (LLVM's arbitrary-precision float) into a
/// constant node that can be used directly as an operand. No instruction is
/// emitted — constants are folded into whatever instruction uses them.
/// IRBuilder also recognises when both operands of a binary op are constants
/// and short-circuits to a single constant rather than emitting an instruction
/// at all (constant folding).
Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}

/// VariableExprAST::codegen - A variable reference looks up the name in
/// NamedValues and returns the Value* for the corresponding function argument.
///
/// For now NamedValues only contains the current function's parameters; any
/// other name is an error. Mutable local variables (alloca/store/load) come
/// in a later chapter.
Value *VariableExprAST::codegen() {
  Value *V = NamedValues[Name];
  if (!V)
    return LogErrorV("Unknown variable name");
  return V;
}

/// BinaryExprAST::codegen - Recursively codegen both operands, then emit the
/// operator-specific instruction.
///
/// The string arguments to each Create* call ("addtmp", "multmp", etc.) are
/// hint names for the SSA value. LLVM uses them when printing IR, appending a
/// numeric suffix when the same hint would otherwise repeat. They have no
/// effect on correctness.
///
/// '<' requires two steps: CreateFCmpULT produces a 1-bit integer (i1) —
/// LLVM's boolean type. Since Pyxc treats everything as double, CreateUIToFP
/// widens it: false -> 0.0, true -> 1.0.
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
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Widen the i1 boolean to double: false -> 0.0, true -> 1.0.
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case '>':
    L = Builder->CreateFCmpUGT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_eq:
    L = Builder->CreateFCmpUEQ(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_neq:
    L = Builder->CreateFCmpUNE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_leq:
    L = Builder->CreateFCmpULE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_geq:
    L = Builder->CreateFCmpUGE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}

/// CallExprAST::codegen - Look up the callee by name in TheModule, verify the
/// argument count, codegen each argument, then emit a call instruction.
///
/// getFunction searches the module for a declaration or definition with the
/// given name. This covers both previous 'extern' declarations and previously
/// defined functions. The argument count check catches mismatches that a typed
/// language would catch statically.
Value *CallExprAST::codegen() {
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfExprAST::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to bool by comparing != 0.0
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for then, else, and merge.
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then block.
  Builder->SetInsertPoint(ThenBB);
  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;
  Builder->CreateBr(MergeBB);

  // Codegen can change the current block — capture where then ended.
  ThenBB = Builder->GetInsertBlock();

  // Emit else block.
  TheFunction->insert(TheFunction->end(), ElseBB);
  Builder->SetInsertPoint(ElseBB);
  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;
  Builder->CreateBr(MergeBB);
  ElseBB = Builder->GetInsertBlock();

  // Emit merge block with phi node.
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);

  return PN;
}

/// PrototypeAST::codegen - Create a function declaration in TheModule: name,
/// return type (always double), and parameter types (all double).
///
/// ExternalLinkage makes the function visible outside this module. That is
/// what allows 'extern def sin(x)' to link against the C library's sin at
/// runtime, and what lets 'def foo(...)' be called from later expressions in
/// the same session.
///
/// Arg.setName() is optional — it only affects the printed IR, making output
/// read as 'double %a, double %b' rather than 'double %0, double %1'.
Function *PrototypeAST::codegen() {
  // All parameters and the return value are double.
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles,
                                       false /* not variadic */);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Name arguments so the printed IR is readable.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

/// FunctionAST::codegen - Generate IR for a complete function definition.
///
/// Four steps:
///
/// 1. Register the prototype. The PrototypeAST is moved into FunctionProtos
///    so that future modules can re-emit a declaration for this function via
///    getFunction(). A reference is kept for the getFunction() call below.
///    getFunction() either finds an existing declaration in the current module
///    (e.g. from a prior 'extern def') or calls Proto->codegen() to create one.
///
/// 2. Create the entry BasicBlock and point the Builder at it. A basic block
///    is a straight-line sequence of instructions with one entry and one exit.
///    Every function starts with exactly one entry block.
///
/// 3. Populate NamedValues. Clear the table (the previous function's arguments
///    are irrelevant) and insert each argument. VariableExprAST nodes in the
///    body look names up here.
///
/// 4. Codegen the body expression. On success, emit 'ret', run verifyFunction
///    (LLVM's internal consistency checker), then run TheFPM to apply the
///    optimisation pipeline. On failure, eraseFromParent() removes the
///    partially-built function so no broken declaration is left in the module.
Function *FunctionAST::codegen() {
  // Step 1: register the prototype and resolve the Function*.
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);

  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // Step 2: create the entry block and point the builder at it.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Step 3: populate NamedValues with this function's arguments.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  // Step 4: codegen the body, optimise, verify, or erase on failure.
  if (Value *RetVal = Body->codegen()) {
    Builder->CreateRet(RetVal);
    verifyFunction(*TheFunction);

    // Run the optimisation pipeline: InstCombine, Reassociate, GVN,
    // SimplifyCFG.
    TheFPM->run(*TheFunction, *TheFAM);
    return TheFunction;
  }

  // Body codegen failed — remove the incomplete function so it cannot be
  // called and does not pollute the module handed to the JIT.
  TheFunction->eraseFromParent();
  return nullptr;
}

//===----------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------===//

/// InitializeModuleAndManagers - Create a fresh module, IR builder, and
/// optimisation pipeline.
///
/// Called once at startup and again after every top-level input that hands
/// its module to the JIT. Because the JIT takes ownership of TheModule via
/// ThreadSafeModule, we cannot keep emitting into the old module — a new one
/// must be created for every subsequent definition or expression.
///
/// The optimisation pipeline is also recreated each time because
/// FunctionPassManager is tied to a specific LLVMContext (via
/// StandardInstrumentations).
///
/// Pipeline:
///   InstCombinePass  - Peephole rewrites: a+0->a, x*2->x<<1, etc.
///   ReassociatePass  - Reorder commutative ops to expose more folding:
///                      (x+2)+3 -> x+(2+3) -> x+5.
///   GVNPass          - Global Value Numbering: eliminate redundant loads and
///                      common sub-expressions across basic blocks.
///   SimplifyCFGPass  - Remove unreachable blocks, merge redundant branches.
///
/// The analysis managers are cross-registered so that a function pass that
/// needs loop information can reach TheLAM, and so on.
static void InitializeModuleAndManagers() {
  // Fresh context and module for this compilation unit.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  // Inform the module of the JIT's target data layout so codegen emits
  // correctly-sized types for the host machine.
  TheModule->setDataLayout(TheJIT->getDataLayout());

  Builder = std::make_unique<IRBuilder<>>(*TheContext);

  // Pass and analysis managers.
  TheFPM = std::make_unique<FunctionPassManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  TheCGAM = std::make_unique<CGSCCAnalysisManager>();
  TheMAM = std::make_unique<ModuleAnalysisManager>();
  ThePIC = std::make_unique<PassInstrumentationCallbacks>();
  TheSI = std::make_unique<StandardInstrumentations>(*TheContext,
                                                     /*DebugLogging*/ false);
  TheSI->registerCallbacks(*ThePIC, TheMAM.get());

  // Optimisation pipeline (applied per function after codegen).
  TheFPM->addPass(InstCombinePass()); // peephole rewrites
  TheFPM->addPass(ReassociatePass()); // canonicalise commutative ops
  TheFPM->addPass(GVNPass());         // eliminate common sub-expressions
  TheFPM->addPass(SimplifyCFGPass()); // remove dead blocks and branches

  // Cross-register so passes can access any analysis tier they need.
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

/// SynchronizeToLineBoundary - Panic-mode error recovery.
///
/// Advance past all remaining tokens on the current line so that MainLoop
/// sees tok_eol or tok_eof next. Called after any parse or codegen failure
/// and after any unexpected trailing token, ensuring the REPL always returns
/// to a clean state before printing the next prompt.
static void SynchronizeToLineBoundary() {
  while (CurTok != tok_eol && CurTok != tok_eof)
    getNextToken();
}

/// HandleDefinition - Parse, optimise, and JIT-compile a 'def' definition.
///
/// On success: codegen + optimise the function (TheFPM runs inside
/// FunctionAST::codegen), print the optimised IR, then hand the entire
/// module to the JIT via addModule. The JIT takes ownership of TheModule and
/// TheContext, so InitializeModuleAndManagers() is called immediately after
/// to create a fresh module for the next input. The compiled function remains
/// accessible in the JIT's symbol table for the rest of the session.
/// On parse failure or unexpected trailing tokens: discard the line.
static void HandleDefinition() {
  auto FnAST = ParseDefinition();
  if (!FnAST || (CurTok != tok_eol && CurTok != tok_eof)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FnIR = FnAST->codegen()) {
    Log("Parsed a function definition.\n");
    if (VerboseIR)
      FnIR->print(errs());
    // Transfer the module to the JIT. TheModule is now invalid; reinitialise.
    ExitOnErr(TheJIT->addModule(
        ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
    InitializeModuleAndManagers();
  }
}

/// HandleExtern - Parse and register an 'extern def' declaration.
///
/// On success: codegen the prototype (emits a 'declare' in the current module),
/// print it, then save the PrototypeAST into FunctionProtos. Saving into
/// FunctionProtos is the critical step — when this module is handed to the JIT
/// and a new one is created, getFunction() uses FunctionProtos to re-emit the
/// 'declare' in whichever module needs to call the extern.
/// On parse failure or unexpected trailing tokens: discard the line.
static void HandleExtern() {
  auto ProtoAST = ParseExtern();

  if (!ProtoAST)
    return;

  if (CurTok != tok_eol && CurTok != tok_eof) {
    if (CurTok)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (auto *FnIR = ProtoAST->codegen()) {
    Log("Parsed an extern.\n");
    if (VerboseIR)
      FnIR->print(errs());
    // Save the prototype so getFunction() can re-emit it in future modules.
    FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
  }
}

/// HandleTopLevelExpression - Compile, execute, and discard a bare expression.
///
/// The expression is wrapped in '__anon_expr' (a zero-argument function that
/// returns double) so it goes through the same codegen path as everything else.
///
/// Execution steps:
///   1. Codegen + optimise the anonymous function.
///   2. Print the optimised IR so the reader can inspect it.
///   3. Create a ResourceTracker scoped to this expression. The RT lets us
///      release the JIT-compiled code and its associated memory immediately
///      after execution, without disturbing other compiled functions.
///   4. Hand the module to the JIT (TheModule is now owned by the JIT).
///      Reinitialise for the next input.
///   5. Look up '__anon_expr' in the JIT, cast its address to a function
///      pointer, call it, and print the result.
///   6. Call RT->remove() to free the compiled code. The module was already
///      transferred to the JIT in step 4, so eraseFromParent() is not needed.
static void HandleTopLevelExpression() {
  auto FnAST = ParseTopLevelExpr();
  if (!FnAST || (CurTok != tok_eol && CurTok != tok_eof)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FnIR = FnAST->codegen()) {
    Log("Parsed a top-level expression.\n");
    if (VerboseIR)
      FnIR->print(errs());

    // ResourceTracker scopes the JIT memory for this expression so we can
    // free it precisely after the call, without affecting other symbols.
    auto RT = TheJIT->getMainJITDylib().createResourceTracker();

    // Transfer ownership of the module to the JIT; reinitialise for next input.
    auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
    ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
    InitializeModuleAndManagers();

    // Locate the compiled function in the JIT's symbol table.
    auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

    // Cast the symbol address to a callable function pointer and invoke it.
    double (*FP)() = ExprSymbol.toPtr<double (*)()>();
    double result = FP();
    if (IsRepl)
      fprintf(stderr, "Evaluated to %f\n", result);

    // Release the compiled code and JIT memory for this expression.
    ExitOnErr(RT->remove());
  }
}

//===----------------------------------------===//
// Runtime library — callable via 'extern def'
//===----------------------------------------===//

// These functions are compiled into the pyxc binary itself and exported with
// C linkage so the JIT can resolve 'extern def putchard(x)' and
// 'extern def printd(x)' against them at runtime.
//
// DLLEXPORT is required on Windows where symbols are not exported by default.
// On macOS/Linux it is a no-op — all extern "C" symbols are visible.

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - Write a single ASCII character to stderr. The double argument
/// is truncated to char. Returns 0.0 so it can be used as an expression.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - Print a double to stderr as "%f\n". Returns 0.0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}

/// MainLoop - Dispatch loop for the REPL.
///
/// grammar: top = { definition | external | expression | newline }
///
/// CurTok is primed before MainLoop() is called (see main()). After each
/// successful parse the handler prints a confirmation; after a failed parse it
/// skips one token. Either way we come back here and look at the new CurTok.
static void MainLoop() {
  while (true) {
    if (CurTok == tok_eof)
      return;

    // A bare newline: just print a fresh prompt and read the next token.
    if (CurTok == tok_eol) {
      PrintConsoleReady();
      getNextToken();
      continue;
    }

    if (CurTok == tok_error) {
      SynchronizeToLineBoundary();
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

int ProcessCommandLine(int argc, const char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::ParseCommandLineOptions(argc, argv, "pyxc\n");

  if (InputFiles.size() > 1) {
    fprintf(stderr, "Error: expected at most one input file.\n");
    return -1;
  }

  if (InputFiles.size() == 1) {
    Input = fopen(InputFiles[0].c_str(), "r");
    if (!Input) {
      perror(InputFiles[0].c_str());
      return -1;
    }
    IsRepl = false;
  } else {
    IsRepl = true;
  }

  return 0;
}

//===----------------------------------------===//
// Main driver code.
//===----------------------------------------===//

int main(int argc, const char **argv) {

  int commandLineResult = ProcessCommandLine(argc, argv);
  if (commandLineResult != 0) {
    return commandLineResult;
  }

  // Initialise LLVM's backend for the host machine. These three calls
  // together register the native target's instruction set, assembler, and
  // disassembler so the JIT can compile and link for the current CPU.
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Register binary operators and their precedence (higher = tighter binding).
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  // Prime the REPL: print the first prompt and load the first token.
  // Every parse function expects CurTok to be loaded before it is called.
  PrintConsoleReady();
  getNextToken();

  // Create the JIT first — InitializeModuleAndManagers() needs TheJIT in
  // order to set the data layout on the new module.
  TheJIT = ExitOnErr(PyxcJIT::Create());
  InitializeModuleAndManagers();

  MainLoop();

  if (Input && Input != stdin) {
    fclose(Input);
    Input = stdin;
  }

  return 0;
}
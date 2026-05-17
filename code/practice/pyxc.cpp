#include "../include/PyxcJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include <cstddef>
#include <cstdio>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;
using namespace llvm;
using namespace llvm::orc;

static cl::OptionCategory PyxcCategory("Pyxc options");

static cl::opt<string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
                                 cl::init(""), cl::cat(PyxcCategory));

static cl::opt<bool> VerboseIR("v",
                               cl::desc("Print generated LLVM IR to stderr"),
                               cl::init(false), cl::cat(PyxcCategory));

static cl::opt<unsigned> OptLevel("O", cl::desc("Optimization level"),
                                  cl::value_desc("0|1|2|3"), cl::Prefix,
                                  cl::init(2), cl::cat(PyxcCategory));

static FILE *Input = stdin;
static bool IsRepl = true;

enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  // commmands
  tok_def = -4,
  tok_extern = -5,

  // primary
  tok_identifier = -6,
  tok_number = -7,

  // comparison
  tok_eq = -8,
  tok_neq = -9,
  tok_leq = -10,
  tok_geq = -11,

  // control
  tok_if = -12,
  tok_else = -13,
  tok_return = -14,

  // loops
  tok_for = -15,

  // user defined functions
  tok_binary = -16,
  tok_unary = -17,

  // mutable variables
  tok_var = -18
};

static string IdentifierStr;
static double NumVal;
static string
    NumLiteralStr; // filled in if tok_number, used for printing errors

static unordered_map<string, int> Keywords = {
    {"def", tok_def},     {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},       {"else", tok_else},     {"for", tok_for},
    {"unary", tok_unary}, {"binary", tok_binary}, {"var", tok_var}};

const string ANON_EXPR = "__anon_expr";

// debug only
static map<int, string> TokenNames = [] {
  static map<int, string> Names = {
      {tok_eof, "end of input"}, {tok_eol, "newline"},
      {tok_error, "error"},      {tok_def, "'def'"},
      {tok_extern, "'extern'"},  {tok_identifier, "identifier"},
      {tok_number, "number"},    {tok_return, "'return'"},
      {tok_eq, "'=='"},          {tok_neq, "'!='"},
      {tok_leq, "'<='"},         {tok_geq, "'>='"},
      {tok_if, "'if'"},          {tok_else, "'else'"},
      {tok_for, "'for'"},        {tok_unary, "'unary'"},
      {tok_binary, "'binary'"},  {tok_var, "'var'"}};

  for (int ch = 0; ch < 255; ++ch) {
    if (isprint(static_cast<unsigned char>(ch)))
      Names[ch] = "'" + string(1, static_cast<int>(ch)) + "'";
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

  void onChar(int c) {
    if (c == '\n') {
      CompletedLines.push_back(CurrentLine);
      CurrentLine.clear();
      return;
    }

    if (c != EOF)
      CurrentLine.push_back(static_cast<char>(c));
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

static SourceManager PyxcSourceMgr;
static void PrintErrorSourceContext(SourceLocation Loc);

static int advance() {
  int LastChar = fgetc(Input);
  if (LastChar == '\r') {
    int NextChar = fgetc(Input);
    if (NextChar != '\n' && NextChar != EOF) {
      ungetc(NextChar, Input);
    }
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
  int c = fgetc(Input);
  if (c != EOF)
    ungetc(c, Input);
  return c;
}

int gettok() {
  static int LastChar = ' ';
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  CurLoc = LexLoc;

  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }

  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_') {
      IdentifierStr += LastChar;
    }

    // Is it a keyword?
    auto It = Keywords.find(IdentifierStr);
    if (It != Keywords.end())
      return It->second;

    return tok_identifier;
  }

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
              "Error (Line %d, Column %d): invalid number literal '%s'\n",
              CurLoc.Line, CurLoc.Col, NumStr.c_str());
      PrintErrorSourceContext(CurLoc);
      return tok_error;
    }
    return tok_number;
  }

  if (LastChar == '#') {
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar != EOF) {
      CurLoc = LexLoc; // re-snapshot where the comment ends
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

static SourceLocation GetDiagnosticAnchorLoc(SourceLocation Loc, int Tok) {
  if (Tok != tok_eol) {
    return Loc;
  }

  int PrevLine = Loc.Line - 1;
  if (PrevLine <= 0)
    return Loc;

  const string *PrevLineText = PyxcSourceMgr.getLine(PrevLine);
  if (!PrevLineText) {
    return Loc;
  }

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
  const string *LineText = PyxcSourceMgr.getLine(Loc.Line);
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = Loc.Col - 1;
  if (spaces < 0)
    spaces = 0;
  fprintf(stderr, "%*s", spaces, "");
  fprintf(stderr, "^~~~\n");
}

namespace {

class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual const string *getLValueName() const {
    return nullptr;
  } // variables will return an actual name
  virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  Value *codegen() override;
};

class VariableExprAST : public ExprAST {
  string Name;

public:
  VariableExprAST(const string &Name) : Name(Name) {}
  const string &getName() const { return Name; }
  const string *getLValueName() const override { return &Name; }
  Value *codegen() override;
};

class AssignmentExprAST : public ExprAST {
  string Name;
  unique_ptr<ExprAST> Expr;

public:
  AssignmentExprAST(const string &Name, unique_ptr<ExprAST> Expr)
      : Name(Name), Expr(std::move(Expr)) {}
  Value *codegen() override;
};

class BinaryExprAST : public ExprAST {
  int Op;
  unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(int Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  Value *codegen() override;
};

class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const string &Callee, vector<unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
  Value *codegen() override;
};

class ForExprAST : public ExprAST {
  string VarName;
  bool IsVarDecl;
  unique_ptr<ExprAST> Start, Cond, Step, Body;

public:
  ForExprAST(const string &VarName, bool IsVarDecl, unique_ptr<ExprAST> Start,
             unique_ptr<ExprAST> Cond, unique_ptr<ExprAST> Step,
             unique_ptr<ExprAST> Body)
      : VarName(VarName), IsVarDecl(IsVarDecl), Start(std::move(Start)),
        Cond(std::move(Cond)), Step(std::move(Step)), Body(std::move(Body)) {}
  Value *codegen() override;
};

class UnaryExprAST : public ExprAST {
  char OpCode;
  unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(char OpCode, unique_ptr<ExprAST> Operand)
      : OpCode(OpCode), Operand(std::move(Operand)) {}
  Value *codegen() override;
};

class IfExprAST : public ExprAST {
  unique_ptr<ExprAST> Cond, Then, Else;

public:
  IfExprAST(unique_ptr<ExprAST> Cond, unique_ptr<ExprAST> Then,
            unique_ptr<ExprAST> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
  Value *codegen() override;
};

///   var a = <init>, b = <init> : <body>
class VarExprAST : public ExprAST {
  vector<pair<string, unique_ptr<ExprAST>>> VarNames;
  unique_ptr<ExprAST> Body;

public:
  VarExprAST(vector<pair<string, unique_ptr<ExprAST>>> VarNames,
             unique_ptr<ExprAST> Body)
      : VarNames(VarNames), Body(std::move(Body)) {}
  Value *codegen();
};

class PrototypeAST {
  string Name;
  vector<string> Args;
  bool IsOperator;
  bool Precedence;

public:
  PrototypeAST(const string &Name, vector<string> Args, bool IsOperator = false,
               unsigned Prec = 0)
      : Name(Name), Args(std::move(Args)), IsOperator(IsOperator),
        Precedence(Prec) {}

  const string &getName() const { return Name; }
  size_t getNumArgs() const { return Args.size(); }

  bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  char getOperatorName() const {
    assert((isUnaryOp() || isBinaryOp()) && "Not an operator prototype");
    return Name.back();
  }

  unsigned getBinaryPrecedence() { return Precedence; }

  Function *codegen();
};

class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;

public:
  FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
  Function *codegen();
};

} // namespace

// Parser

static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

static void consumeNewLines() {
  while (CurTok == tok_eol)
    getNextToken();
}

static map<char, int> BinopPrecedence = {
    {'<', 10},    {'>', 10},     {'+', 20},     {'-', 20},    {'*', 40},
    {tok_eq, 10}, {tok_neq, 10}, {tok_leq, 10}, {tok_geq, 10}};

static set<int> KnownUnaryOperators = {'-'};
static map<string, unique_ptr<PrototypeAST>> FunctionProtos;

// Return tok precedence if it is a known operator, '-' otherwise
static int GetTokPrecedence() {
  auto it = BinopPrecedence.find(CurTok);
  return (it == BinopPrecedence.end() || it->second <= 0) ? -1 : it->second;
}

void PrintReplPrompt() {
  if (IsRepl)
    fprintf(stderr, "ready> ");
}

void Log(const string &message) {
  if (IsRepl)
    fprintf(stderr, "%s", message.c_str());
}

unique_ptr<ExprAST> LogError(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Col,
          Str);
  PrintErrorSourceContext(Anchor);
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

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

static unique_ptr<ExprAST> ParseExpression();
static unique_ptr<ExprAST> ParsePrimary();
static unique_ptr<ExprAST> ParseVarExpr();

static unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return Result;
};

static unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat '('
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != ')')
    return LogError("expected ')'");

  getNextToken(); // eat ')'

  return V;
}

/// identifierexpr
///   = identifier
///   | identifier "("[expression{"," expression}]")" ;
static unique_ptr<ExprAST> ParseIdentifierExprWithName(const string &IdName) {
  if (CurTok != '(') // regular identifier
  {
    return make_unique<VariableExprAST>(IdName);
  }

  getNextToken(); // eat '('
  vector<unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression()) {
        Args.push_back(std::move(Arg));
      } else {
        return nullptr;
      }

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list.");

      getNextToken();
    }
  }

  getNextToken(); // eat ')'
  return make_unique<CallExprAST>(IdName, std::move(Args));
}

static unique_ptr<ExprAST> ParseIdentifierExpr() {
  string IdName = IdentifierStr;
  getNextToken();
  return ParseIdentifierExprWithName(IdName);
}

static unique_ptr<ExprAST> ParseForExpr() {
  getNextToken(); // eat 'for'

  bool IsVarDecl = false;
  if (CurTok == tok_var) {
    IsVarDecl = true;
    getNextToken();
  }

  if (CurTok != tok_identifier)
    return LogError("Expected identifier after 'for'");
  string VarName = IdentifierStr;
  getNextToken(); // eat identifier

  if (CurTok != '=')
    return LogError("Expected '=' after for variable");
  getNextToken(); // eat '='

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;

  if (CurTok != ',')
    return LogError("Expected ',' after for start value");
  getNextToken(); // eat ','

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ',')
    return LogError("Expected ',' after for condition");
  getNextToken(); // eat ','

  auto Step = ParseExpression();
  if (!Step)
    return nullptr;

  if (CurTok != ':')
    return LogError("Expected ':' after for step");
  getNextToken(); // eat ':'

  consumeNewLines(); // allow the `for` body to be on a different line
  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return make_unique<ForExprAST>(VarName, IsVarDecl, std::move(Start),
                                 std::move(Cond), std::move(Step),
                                 std::move(Body));
}

/// varexpr
///   = "var" varbinding { "," varbinding } ":" [ eols ] expression ;
///
/// varbinding
///   = identifier [ "=" expression ] ;
static unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat 'var'
  vector<pair<string, unique_ptr<ExprAST>>> VarNames;

  while (true) {
    if (CurTok != tok_identifier)
      return LogError("Expected identifier after 'var'");

    string Name = IdentifierStr;
    getNextToken(); // eat the identifier

    unique_ptr<ExprAST> Init;
    if (CurTok == '=') {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init)
        return nullptr;
    } else {
      Init = make_unique<NumberExprAST>(0.0);
    }

    VarNames.push_back({Name, std::move(Init)});

    if (CurTok != ',')
      break;
    getNextToken();
  }

  if (CurTok != ':')
    return LogError("Expected ':' after var bindings");
  getNextToken(); // eat ':'

  consumeNewLines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
};

static unique_ptr<ExprAST> ParseIfExpr() {
  getNextToken(); // eat 'if'

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError("Expected ':' after if condition");
  getNextToken(); // eat the ':'

  consumeNewLines(); // allow the if body to be on the next line

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  consumeNewLines(); // else can be on the next line

  // else is required
  if (CurTok != tok_else)
    return LogError("Expected 'else' in if expression");
  getNextToken(); // eat 'else'

  if (CurTok != ':')
    return LogError("Expected ':' after else");
  getNextToken(); // eat ':'

  consumeNewLines();

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return make_unique<IfExprAST>(std::move(Cond), std::move(Then),
                                std::move(Else));
}

static unique_ptr<ExprAST>
ParseUnary(); // forward declaration for ParseUnaryMinus

static unique_ptr<ExprAST> ParseUnaryMinus() {
  getNextToken(); // eat '-'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  return make_unique<UnaryExprAST>('-', std::move(Operand));
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
  case tok_for:
    return ParseForExpr();
  }
}

static unique_ptr<ExprAST> ParseUnary() {
  if (!isascii(CurTok) || CurTok == '(' || isalpha(CurTok) || isdigit(CurTok))
    return ParsePrimary();

  if (CurTok == '-')
    return ParseUnaryMinus();

  int Op = CurTok;
  getNextToken();
  if (auto Operand = ParseUnary()) {
    return make_unique<UnaryExprAST>(Op, std::move(Operand));
  }
  return nullptr;
}

/// binoprhs
///   = { binaryop primary } ;
static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                         unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If the current token precedence is at least as much as our threshold, we
    // process otherwise we bail.
    if (TokPrec < ExprPrec)
      return LHS;

    // ok this is a binop (otherwise it would have been -1 or < ExprPrec)
    int BinOp = CurTok;
    getNextToken(); // eat binop

    auto RHS = ParseUnary();
    if (!RHS)
      return nullptr;

    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) { // NextPrec is an operator and binds tighter
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS)); // Recurse
      if (!RHS)
        return nullptr;
    }

    // now merge and loop to process other expressions
    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

/// expression
///   = varexpr | identifier "=" expression | unaryexpr binoprhs ;
static unique_ptr<ExprAST> ParseExpression() {
  auto primary = ParseUnary();
  if (!primary)
    return nullptr;
  return ParseBinOpRHS(0, std::move(primary));
}

/// prototype
///   = identifier "(" [identifier {"," identifier}] ")" ;
static unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");

  string FnName = IdentifierStr;
  getNextToken(); // eat identifier;

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");
  //   getNextToken(); // eat '('

  vector<string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);

    if (getNextToken() == ')')
      break;

    if (CurTok != ',')
      return LogErrorP("Expected ')' or ',' in parameter list");
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype");
  getNextToken(); // ')'

  return make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition.");

  getNextToken(); // eat ':'

  consumeNewLines(); // skip new lines allowing you to write the body on the
                     // next line

  if (CurTok != tok_return)
    return LogErrorF("Expected 'return' in function body");
  getNextToken(); // eat return

  if (auto E = ParseExpression())
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));

  return nullptr;
}

static unsigned ParseBinaryDecorator() {
  getNextToken(); // parse 'binary'
  if (CurTok != '(') {
    LogError("Expected '(' after '@binary'");
    return 0;
  }
  getNextToken(); // eat '('

  if (CurTok != tok_number) {
    LogError("Expected precedence number in '@binary(...)'");
    return 0;
  }

  if (NumLiteralStr.find('.') != string::npos) {
    LogError("Precedence must be an integer, not a decimal literal");
    return 0;
  }

  if (NumVal < 1) {
    LogError("Precedence must be a positive integer");
    return 0;
  }

  unsigned Prec = static_cast<int>(NumVal);
  getNextToken(); // eat number

  if (CurTok != ')') {
    LogError("Expected ')' after precedence in @binary(...)");
    return 0;
  }
  getNextToken(); // eat ')'

  return Prec;
}

static void ParseUnaryDecorator() {
  getNextToken(); // eat 'unary'
}

static bool IsCustomOpChar(int Tok) {
  return isascii(Tok) && ispunct(static_cast<unsigned char>(Tok)) && Tok != '@';
}

static bool IsKnownUnaryOperatorToken(int Tok) {
  return KnownUnaryOperators.find(Tok) != KnownUnaryOperators.end();
}

static bool IsKnownBinaryOperatorToken(int Tok) {
  return BinopPrecedence.find(Tok) != BinopPrecedence.end();
}

/// binaryopprototype
///   = customopchar "(" identifier "," identifier ")"
static unique_ptr<PrototypeAST> ParseBinaryOpPrototype(unsigned Precedence) {
  if (!IsCustomOpChar(CurTok)) {
    return LogErrorP(
        "Expected operator character in binary operator prototype");
  }

  char OpChar = (char)CurTok;
  string FnName = string("binary") + OpChar;

  // known binary operator
  if (IsKnownBinaryOperatorToken(CurTok))
    return LogErrorP(
        (string("Binary operator '") + OpChar + "' is already defined.")
            .c_str());

  // known unary operator with the same name
  if (IsKnownUnaryOperatorToken(CurTok))
    return LogErrorP((string("Binary operator '") + OpChar +
                      "' conflicts with an existing unary operator")
                         .c_str());

  // a function called binary<Op> exists too
  if (FunctionProtos.count(FnName))
    return LogErrorP((string("Function name 'binary") + OpChar +
                      "' conflicts with operator-reserved naming")
                         .c_str());

  getNextToken(); // eat operator character

  if (CurTok != '(')
    return LogErrorP("Expected '(' in binary operator prototype");

  vector<string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
    if (getNextToken() == ')')
      break;
    if (CurTok != ',')
      return LogErrorP("Expected ')' or ',' in parameter list");
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in binary operator prototype");
  getNextToken(); // eat ')'

  if (ArgNames.size() != 2)
    return LogErrorP("Binary operator must have exactly two arguments");

  return make_unique<PrototypeAST>(FnName, std::move(ArgNames),
                                   /* IsOperator */ true, Precedence);
}

static unique_ptr<PrototypeAST> ParseUnaryOpPrototype() {
  if (!IsCustomOpChar(CurTok)) {
    return LogErrorP("Expected operator character in unary operator prototype");
  }

  char OpChar = (char)CurTok;
  string FnName = string("unary") + OpChar;

  // known unary operator with the same name
  if (IsKnownUnaryOperatorToken(CurTok))
    return LogErrorP(
        (string("Unary operator '") + OpChar + "' is already defined").c_str());

  // known binary operator
  if (IsKnownBinaryOperatorToken(CurTok))
    return LogErrorP((string("Unary operator '") + OpChar +
                      "' conflicts with an existing binary operator")
                         .c_str());

  // a function called binary<Op> exists too
  if (FunctionProtos.count(FnName))
    return LogErrorP((string("Function name 'unary") + OpChar +
                      "' conflicts with operator-reserved naming")
                         .c_str());

  getNextToken(); // eat operator character

  if (CurTok != '(')
    return LogErrorP("Expected '(' in unary operator prototype");

  vector<string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
    if (getNextToken() == ')')
      break;
    if (CurTok != ',')
      return LogErrorP("Expected ')' or ',' in parameter list");
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in binary operator prototype");
  getNextToken(); // eat ')'

  if (ArgNames.size() != 1)
    return LogErrorP("Unary operator must have exactly one argument");

  return make_unique<PrototypeAST>(FnName, std::move(ArgNames),
                                   /* IsOperator */ true, 0);
}

static unique_ptr<FunctionAST> ParseDecoratedDef() {
  if (CurTok != tok_unary && CurTok != tok_binary)
    return LogErrorF("Expected 'binary' or 'unary' after '@'");

  bool IsBinary = (CurTok == tok_binary);
  unique_ptr<PrototypeAST> Proto;

  if (IsBinary) {
    unsigned Prec = ParseBinaryDecorator();
    if (!Prec)
      return nullptr;
    // decorator should end with a newline
    if (CurTok != tok_eol)
      return LogErrorF("Expected newline after '@binary(...)' decorator");
    consumeNewLines();
    if (CurTok != tok_def)
      return LogErrorF("Expected 'def' after decorator");
    getNextToken();
    Proto = ParseBinaryOpPrototype(Prec);
  } else { // Unary op
    ParseUnaryDecorator();
    if (CurTok != tok_eol)
      return LogErrorF("Expected newline after '@unary' decorator");
    consumeNewLines();
    if (CurTok != tok_def)
      return LogErrorF("Expected 'def' after decorator");
    getNextToken();
    Proto = ParseUnaryOpPrototype();
  }

  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in operator definition.");
  getNextToken();

  consumeNewLines();

  if (CurTok != tok_return)
    return LogErrorF("Expected 'return' in operator body");
  getNextToken();

  if (auto E = ParseExpression())
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = make_unique<PrototypeAST>(ANON_EXPR, vector<string>());
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

/// external
///   = "extern" "def" prototype
static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern
  if (CurTok != tok_def)
    return LogErrorP("Expected `def` after extern");
  getNextToken(); // eat def
  return ParsePrototype();
}

static unique_ptr<LLVMContext> TheContext;
static unique_ptr<Module> TheModule;
static unique_ptr<IRBuilder<>> TheBuilder;
static map<string, Value *> NamedValues;
static unique_ptr<PyxcJIT> TheJIT;
static unique_ptr<FunctionPassManager> TheFPM;
static unique_ptr<LoopAnalysisManager> TheLAM;
static unique_ptr<FunctionAnalysisManager> TheFAM;
static unique_ptr<CGSCCAnalysisManager> TheCGAM;
static unique_ptr<ModuleAnalysisManager> TheMAM;
static ExitOnError ExitOnErr;

// Code gen

Function *getFunction(const std::string &Name) {
  // The function was defined in this module
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // The function was defined in a different module. We can emit an external
  // declaration and allow the JIT linker to find it.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  return nullptr;
}

// nothing emitted
Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  auto It = NamedValues.find(Name);

  if (It != NamedValues.end() && It->second) {
    return It->second;
  }

  return LogErrorV("Unknown variable name");
}

Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  if (!L)
    return nullptr;

  Value *R = RHS->codegen();
  if (!R)
    return nullptr;

  switch (Op) {
  case '+':
    return TheBuilder->CreateFAdd(L, R, "addtmp");
  case '-':
    return TheBuilder->CreateFSub(L, R, "subtmp");
  case '*':
    return TheBuilder->CreateFMul(L, R, "multmp");
  case '<':
    L = TheBuilder->CreateFCmpOLT(L, R, "cmptmp");
    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                    "booltmp");
  case '>':
    L = TheBuilder->CreateFCmpOGT(L, R, "cmptmp");
    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                    "booltmp");
  case tok_eq:
    L = TheBuilder->CreateFCmpOEQ(L, R, "cmptmp");
    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                    "booltmp");
  case tok_neq:
    L = TheBuilder->CreateFCmpUNE(L, R, "cmptmp");
    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                    "booltmp");
  case tok_leq:
    L = TheBuilder->CreateFCmpOLE(L, R, "cmptmp");
    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                    "booltmp");
  case tok_geq:
    L = TheBuilder->CreateFCmpOGE(L, R, "cmptmp");
    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                    "booltmp");
  default:
    break;
  }

  // No default operator, let's look for user defined ops
  Function *F = getFunction(string("binary") + static_cast<char>(Op));
  if (!F)
    return LogErrorV("invalid binary operator");

  Value *Ops[] = {L, R};
  return TheBuilder->CreateCall(F, Ops, "binop");
}

Value *UnaryExprAST::codegen() {
  Value *Op = Operand->codegen();
  if (!Op)
    return nullptr;

  // Built-in unary minus
  if (OpCode == '-')
    return TheBuilder->CreateFNeg(Op, "negtmp");

  Function *F = getFunction(string("unary") + OpCode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return TheBuilder->CreateCall(F, Op, "unop");
}

Value *CallExprAST::codegen() {
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (size_t i = 0; i < Args.size(); ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return TheBuilder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfExprAST::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;
  // Convert condition to bool by comparing != 0.0
  CondV = TheBuilder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *F = TheBuilder->GetInsertBlock()->getParent();

  // Create blocks for then, else, merge
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", F);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", F);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", F);

  TheBuilder->CreateCondBr(CondV, ThenBB, ElseBB);
  TheBuilder->SetInsertPoint(ThenBB);
  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;
  TheBuilder->CreateBr(MergeBB);

  // Nested blocks might move the insert block position,
  // we need to merge to phi from there
  ThenBB = TheBuilder->GetInsertBlock();

  TheBuilder->SetInsertPoint(ElseBB);
  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;
  TheBuilder->CreateBr(MergeBB);
  ElseBB = TheBuilder->GetInsertBlock();

  TheBuilder->SetInsertPoint(MergeBB);
  PHINode *PN =
      TheBuilder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}

Value *ForExprAST::codegen() {
  Function *F = TheBuilder->GetInsertBlock()->getParent();

  // emit start value in the preheader (current block before the loop)
  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;

  BasicBlock *PreheaderBB = TheBuilder->GetInsertBlock();

  BasicBlock *CondBB = BasicBlock::Create(*TheContext, "loop_cond", F);
  BasicBlock *BodyBB = BasicBlock::Create(*TheContext, "loop_body", F);
  BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "after_loop", F);

  // Unconditional jump from the preheader into the condition check
  TheBuilder->CreateBr(CondBB);
  TheBuilder->SetInsertPoint(CondBB);
  PHINode *Variable =
      TheBuilder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
  Variable->addIncoming(StartVal, PreheaderBB);

  // Shadow old val
  Value *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Variable;

  // Evaluate the condition, treat 0.0 as false, anything else is true
  Value *CondVal = Cond->codegen();
  if (!CondVal)
    return nullptr;
  CondVal = TheBuilder->CreateFCmpONE(
      CondVal, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
  TheBuilder->CreateCondBr(CondVal, BodyBB, AfterBB);

  // loop body
  TheBuilder->SetInsertPoint(BodyBB);
  if (!Body->codegen())
    return nullptr;

  // step
  Value *StepVal = Step->codegen();
  if (!StepVal)
    return nullptr;

  Value *NextVar = TheBuilder->CreateFAdd(Variable, StepVal, "nextvar");

  BasicBlock *BodyEndBB = TheBuilder->GetInsertBlock();
  Variable->addIncoming(NextVar, BodyEndBB);
  TheBuilder->CreateBr(CondBB);

  TheBuilder->SetInsertPoint(AfterBB);
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Function *PrototypeAST::codegen() {
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);
  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  size_t Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  // install the binary operator
  if (isBinaryOp())
    BinopPrecedence[getOperatorName()] = Precedence;

  // install the unary operator
  if (isUnaryOp())
    KnownUnaryOperators.insert(getOperatorName());

  return F;
}

Function *FunctionAST::codegen() {
  auto &P = *Proto; // We will give up control on Proto next to put it in
                    // FunctionProtos so we keep a reference
  FunctionProtos[Proto->getName()] = std::move(Proto);

  // Now use the reference instead. Proto belongs to someone else.
  Function *F = getFunction(P.getName());

  // The function is either only declared, or defined
  // But we guard against redefinition.
  if (F && !F->empty()) {
    LogError("Function cannot be redefined");
    return nullptr;
  }

  if (!F)
    return nullptr;

  // Create an entry block and point the builder to it
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", F);
  TheBuilder->SetInsertPoint(BB);

  NamedValues.clear(); // Variables are currently only function scoped

  // Add all the parameters to the named values
  for (auto &Arg : F->args())
    NamedValues[string(Arg.getName())] = &Arg;

  // Codegen the body. The expressions will use NamedValues for the variables
  if (Value *R = Body->codegen()) {
    TheBuilder->CreateRet(R);
    verifyFunction(*F);
    // run the optimization pipeline
    TheFPM->run(*F, *TheFAM);
    return F;
  }

  F->eraseFromParent();
  return nullptr;
};

static void InitializeModuleAndManagers() {
  TheContext = make_unique<LLVMContext>();
  TheModule = make_unique<Module>("PyxcJIT", *TheContext);
  TheModule->setDataLayout(TheJIT->getDataLayout());
  TheBuilder = make_unique<IRBuilder<>>(*TheContext);

  TheFPM = make_unique<FunctionPassManager>();
  TheLAM = make_unique<LoopAnalysisManager>();
  TheFAM = make_unique<FunctionAnalysisManager>();
  TheCGAM = make_unique<CGSCCAnalysisManager>();
  TheMAM = make_unique<ModuleAnalysisManager>();

  if (OptLevel != 0) {
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    TheFPM->addPass(GVNPass());
  }

  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerCGSCCAnalyses(*TheCGAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.registerLoopAnalyses(*TheLAM);

  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

// Parsing
static void SynchronizeToLineBoundary() {
  while (CurTok != tok_eol && CurTok != tok_eof)
    getNextToken();
}

static void HandleDecorator() {
  auto FnAST = ParseDecoratedDef();

  if (!FnAST || (CurTok != tok_eol && CurTok != tok_eof)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());

    SynchronizeToLineBoundary();
    return;
  }

  if (auto FnIR = FnAST->codegen()) {
    Log("Parsed a user-defined operator.\n");
    if (VerboseIR)
      FnIR->print(errs());
    // hand over the module for immediate parsing/running
    ExitOnErr(TheJIT->addModule(
        ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
    // clean up shop and start with a fresh module
    InitializeModuleAndManagers();
  }
}

// top level parsing
static void HandleDefinition() {
  auto FnAST = ParseDefinition();

  if (!FnAST || (CurTok != tok_eol && CurTok != tok_eof)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (auto FnIR = FnAST->codegen()) {
    Log("Parsed a function definition.\n");
    if (VerboseIR)
      FnIR->print(errs());
    // hand over the module for immediate parsing/running
    ExitOnErr(TheJIT->addModule(
        ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
    // clean up shop and start with a fresh module
    InitializeModuleAndManagers();
  }
}

static void HandleExtern() {
  auto ProtoAST = ParseExtern();

  if (!ProtoAST || (CurTok != tok_eol && CurTok != tok_eof)) {
    if (ProtoAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  // Reject conflicting redeclarations: in Pyxc, function identity is just
  // name + arity, since all parameter and return types are double.
  auto Existing = FunctionProtos.find(ProtoAST->getName());
  if (Existing != FunctionProtos.end() &&
      (Existing->second->getNumArgs() != ProtoAST->getNumArgs())) {
    LogError((string("Conflicting extern declaration signatures for '") +
              ProtoAST->getName() + "'")
                 .c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (auto FnIR = ProtoAST->codegen()) {
    Log("Parsed an extern.\n");
    if (VerboseIR)
      FnIR->print(errs());
    // Save the prototype so getFunction() can re-emit it in future modules.
    FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
  }
}

static void HandleTopLevelExpression() {
  auto FnAST = ParseTopLevelExpr();

  if (!FnAST || (CurTok != tok_eol && CurTok != tok_eof)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (auto FnIR = FnAST->codegen()) {
    Log("Parsed a top-level expression.\n");
    if (VerboseIR)
      FnIR->print(errs());
    auto RT = TheJIT->getMainJITDylib().createResourceTracker();
    auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
    ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
    InitializeModuleAndManagers();

    // Find the anonymous function
    auto ExprSymbol = ExitOnErr(TheJIT->lookup(ANON_EXPR));

    // Cast the function into a callable function that returns a double
    double (*FP)() = ExprSymbol.toPtr<double (*)()>();
    // Call it
    double result = FP();
    if (IsRepl)
      fprintf(stderr, "Evaluated to %f\n", result);
    ExitOnErr(RT->remove());
  }
}

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}

static void MainLoop() {
  while (true) {
    if (CurTok == tok_eof)
      return;

    if (CurTok == tok_eol) {
      PrintReplPrompt();
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
    case '@':
      getNextToken(); // eat the @
      HandleDecorator();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

int ProcessCommandLine(int argc, char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::ParseCommandLineOptions(argc, argv, "pyxc\n");

  if (OptLevel > 3) {
    fprintf(stderr, "Error -O level must be 0, 1, 2, or 3\n");
    return -1;
  }

  if (!InputFile.empty()) {
    Input = fopen(InputFile.c_str(), "r");
    if (!Input) {
      perror(InputFile.c_str());
      return -1;
    }
    IsRepl = false;
  } else {
    IsRepl = true;
  }

  return 0;
}

class InputFileGuard {
  FILE *&input;

public:
  explicit InputFileGuard(FILE *&input_ref) : input(input_ref) {}
  ~InputFileGuard() {
    if (input && input != stdin) {
      fclose(input);
      input = stdin;
    }
  }

  InputFileGuard(const InputFileGuard &) = delete;
  InputFileGuard &operator=(const InputFileGuard &) = delete;
};

int main(int argc, char **argv) {
  int commandLineResult = ProcessCommandLine(argc, argv);
  if (commandLineResult != 0)
    return commandLineResult;
  InputFileGuard InputGuard(Input);

  InitializeNativeTarget();
  InitializeNativeTargetAsmParser();
  InitializeNativeTargetAsmPrinter();

  PrintReplPrompt();
  getNextToken(); // prime the lexer

  TheJIT = ExitOnErr(PyxcJIT::Create());
  InitializeModuleAndManagers();
  MainLoop();

  return 0;
}
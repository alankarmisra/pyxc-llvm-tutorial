#include "../include/PyxcJIT.h"
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

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_indent = -15,
  tok_dedent = -16,
  tok_error = -17,

  // commands
  tok_def = -3,
  tok_extern = -4,

  // primary
  tok_identifier = -5,
  tok_number = -6,

  // control
  tok_if = -7,
  tok_else = -8,
  tok_return = -9,

  // loop
  tok_for = -10,
  tok_in = -11,
  tok_range = -12,

  // decorator
  tok_decorator = -13,

  // var definition
  tok_var = -14,
};

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number
static int InForExpression;       // Track global parsing context

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},   {"else", tok_else},     {"for", tok_for},
    {"in", tok_in},   {"range", tok_range},   {"var", tok_var}};

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
std::unique_ptr<ExprAST> LogError(const char *Str);

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
          LogError("You cannot mix tabs and spaces.");
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

/// BlockAST - a block
class BlockAST : public StmtAST {
  std::vector<std::unique_ptr<StmtAST>> Stmts;

public:
  BlockAST(SourceLocation Loc, std::vector<std::unique_ptr<StmtAST>> Stmts)
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
  std::unique_ptr<BlockAST> Then, Else;

public:
  IfStmtAST(SourceLocation Loc, std::unique_ptr<ExprAST> Cond,
            std::unique_ptr<BlockAST> Then, std::unique_ptr<BlockAST> Else)
      : StmtAST(Loc), Cond(std::move(Cond)), Then(std::move(Then)),
        Else(std::move(Else)) {}

  raw_ostream &dump(raw_ostream &out, int ind) override {
    StmtAST::dump(out << "if", ind);
    Cond->dump(indent(out, ind) << "Cond:", ind + 1);
    Then->dump(indent(out, ind) << "Then:", ind + 1);
    Else->dump(indent(out, ind) << "Else:", ind + 1);
    return out;
  }
  Value *codegen() override;
};

/// ForExprAST - Expression class for for/in.
class ForStmtAST : public StmtAST {
  std::string VarName;
  std::unique_ptr<ExprAST> Start, End, Step;
  std::unique_ptr<BlockAST> Body;

public:
  ForStmtAST(std::string VarName, std::unique_ptr<ExprAST> Start,
             std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
             std::unique_ptr<BlockAST> Body)
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
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
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
static int CurTok;
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

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
  InForExpression = 0;
  fprintf(stderr, "%sError (Line: %d, Column: %d): %s\n%s", Red, CurLoc.Line,
          CurLoc.Col, Str, Reset);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

std::unique_ptr<FunctionAST> LogErrorF(const char *Str) {
  LogError(Str);
  return nullptr;
}

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
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
    return LogError("expected ')'");
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
        return LogError("Expected ')' or ',' in argument list");
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

// ifexpr ::= 'if' expression ':' expression 'else' ':' expression
static std::unique_ptr<ExprAST> ParseIfExpr() {
  SourceLocation IfLoc = CurLoc;
  getNextToken(); // eat 'if'

  // condition
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError("expected `:`");
  getNextToken(); // eat ':'

  bool ConditionUsesNewLines = EatNewLines();
  int thenIndentLevel = 0, elseIndentLevel = 0;

  // Parse `then` clause
  if (ConditionUsesNewLines) {
    if (CurTok != tok_indent) {
      return LogError("Expected indent");
    }
    thenIndentLevel = Indents.back();
    getNextToken(); // eat indent
  }

  // Handle nested `if` and `for` expressions:
  // For `if` expressions, `return` statements are emitted inside the
  // true and false branches, so we only require an explicit `return`
  // when we are not parsing an `if`.
  // For `for` expressions, control flow and the resulting value are
  // handled entirely within the loop body, so an explicit `return`
  // is not required at this level.
  if (!InForExpression && CurTok != tok_if && CurTok != tok_for) {
    if (CurTok != tok_return)
      return LogError("Expected 'return'");

    getNextToken(); // eat return
  }

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  bool ThenUsesNewLines = EatNewLines();
  if (!ThenUsesNewLines) {
    return LogError("Expected newline before else condition");
  }

  if (ThenUsesNewLines && CurTok != tok_dedent) {
    return LogError("Expected dedent after else");
  }
  // We could get multiple dedents from nested if's and for's
  while (getNextToken() == tok_dedent)
    ;

  if (CurTok != tok_else)
    return LogError("Expected `else`");
  getNextToken(); // eat else

  if (CurTok != ':')
    return LogError("expected `:`");

  getNextToken(); // eat ':'

  bool ElseUsesNewLines = EatNewLines();
  if (ThenUsesNewLines != ElseUsesNewLines) {
    return LogError("Both `then` and `else` clause should be consistent in "
                    "their usage of newlines.");
  }

  if (ElseUsesNewLines) {
    if (CurTok != tok_indent) {
      return LogError("Expected indent.");
    }
    elseIndentLevel = Indents.back();
    getNextToken(); // eat indent
  }

  if (thenIndentLevel != elseIndentLevel) {
    return LogError("Indent mismatch between if and else");
  }

  if (!InForExpression && CurTok != tok_if && CurTok != tok_for) {
    if (CurTok != tok_return)
      return LogError("Expected 'return'");

    getNextToken(); // eat return
  }

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  //   EatNewLines();

  //   if (ThenUsesNewLines && CurTok != tok_dedent) {
  //     return LogError("Expected dedent");
  //   }
  //   while (getNextToken() == tok_dedent)
  //     ;
  //   // getNextToken(); // eat dedent

  return std::make_unique<IfStmtAST>(IfLoc, std::move(Cond), std::move(Then),
                                     std::move(Else));
}

// `for` identifier `in` `range` `(`expression `,` expression
//   (`,` expression)? # optional
// `)`: expression
static std::unique_ptr<ExprAST> ParseForExpr() {
  InForExpression++;
  getNextToken(); // eat for

  if (CurTok != tok_identifier)
    return LogError("Expected identifier after for");

  std::string IdName = IdentifierStr;
  getNextToken(); // eat identifier

  if (CurTok != tok_in)
    return LogError("Expected `in` after identifier in for");
  getNextToken(); // eat 'in'

  if (CurTok != tok_range)
    return LogError("Expected `range` after identifier in for");
  getNextToken(); // eat range

  if (CurTok != '(')
    return LogError("Expected `(` after `range` in for");
  getNextToken(); // eat '('

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;

  if (CurTok != ',')
    return LogError("expected `,` after range start");
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
    return LogError("expected `)` after range operator");
  getNextToken(); // eat `)`

  if (CurTok != ':')
    return LogError("expected `:` after range operator");
  getNextToken(); // eat `:`

  bool UsesNewLines = EatNewLines();

  if (UsesNewLines && CurTok != tok_indent)
    return LogError("expected indent.");

  getNextToken(); // eat indent

  // `for` expressions don't have the return statement
  // they return 0 by default.
  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  InForExpression--;

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
    return LogError("expected identifier after var");

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
      return LogError("expected identifier list after var");
  }

  // At this point, we have to have 'in'.
  if (CurTok != tok_in)
    return LogError("expected 'in' keyword after 'var'");
  getNextToken(); // eat 'in'.

  EatNewLines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
///   ::= ifexpr
///   ::= forexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    fprintf(stderr, "CurTok = %d\n", CurTok);
    return LogError("Unknown token when expecting an expression");
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
      return LogErrorP("Expected single character operator");
    }

    if (!isascii(CurTok)) {
      return LogErrorP("Expected single character operator");
    }

    FnName = (operatorType == Unary ? "unary" : "binary");
    FnName += (char)CurTok;

    getNextToken();
  } else {
    if (CurTok != tok_identifier) {
      return LogErrorP("Expected function name in prototype");
    }

    FnName = IdentifierStr;

    getNextToken();
  }

  if (CurTok != '(') {
    return LogErrorP("Expected '(' in prototype");
  }

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
    getNextToken(); // Eat idenfitier

    if (CurTok == ')')
      break;

    if (CurTok != ',')
      return LogErrorP("Expected ')' or ',' in parameter list");
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype");

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
      return LogErrorF("expected decorator name after '@'");

    auto it = Decorators.find(IdentifierStr);
    OpType = it == Decorators.end() ? OperatorType::Undefined : it->second;
    getNextToken(); // eat decorator name

    if (OpType == Undefined)
      return LogErrorF(("unknown decorator '" + IdentifierStr + "'").c_str());

    if (OpType == Binary) {
      if (CurTok == '(') {
        getNextToken(); // eat '('
        if (CurTok != ')') {
          // Parse "precedence=N"
          // If we want to introduce more attributes, we would add "precedence"
          // to a map and associate it with a binary operator.
          if (CurTok != tok_identifier || IdentifierStr != "precedence") {
            return LogErrorF("expected 'precedence' parameter in decorator");
          }

          getNextToken(); // eat 'precedence'

          if (CurTok != '=')
            return LogErrorF("expected '=' after 'precedence'");

          getNextToken(); // eat '='

          if (CurTok != tok_number)
            return LogErrorF("expected number for precedence value");

          Precedence = NumVal;
          getNextToken(); // eat number
        }
        if (CurTok != ')')
          return LogErrorF("expected ')' after precedence value");
        getNextToken(); // eat ')'
      }
    }
  }

  EatNewLines();

  if (CurTok != tok_def)
    return LogErrorF("expected 'def'");

  getNextToken(); // eat def.
  auto Proto = ParsePrototype(OpType, Precedence);
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");

  getNextToken(); // eat ':'

  EatNewLines();

  if (CurTok != tok_indent)
    return LogErrorF("Expected indentation.");
  getNextToken(); // eat tok_indent

  if (!InForExpression && CurTok != tok_if && CurTok != tok_for &&
      CurTok != tok_var) {
    if (CurTok != tok_return) {
      //   fprintf(stderr, "CurTok = %d\n", CurTok);
      return LogErrorF("Expected 'return' before expression");
    }

    getNextToken(); // eat return
  }

  auto E = ParseExpression();
  if (!E)
    return nullptr;

  EatNewLines();
  while (CurTok == tok_dedent)
    getNextToken();
  return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  SourceLocation FnLoc = CurLoc;
  if (auto E = ParseExpression()) {
    // Make an anonymous proto.
    // TODO: What happens to do this in a binary?
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
    return LogErrorP("Expected `def` after extern.");
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
  DIType *getDoubleTy();
};

static std::unique_ptr<DebugInfo> KSDbgInfo;
static std::unique_ptr<DIBuilder> DBuilder;

// Helper to safely emit location
inline void emitLocation(ExprAST *AST) {
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
    return LogErrorV(("Unknown variable name " + Name).c_str());
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
    return LogErrorV("Unknown unary operator");
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
      return LogErrorV("destination of '=' must be a variable");
    // Codegen the RHS.
    Value *Val = RHS->codegen();
    if (!Val)
      return nullptr;

    // Look up the name.
    Value *Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return LogErrorV("Unknown variable name");

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
    return LogErrorV("Unknown function referenced");

  // If argument mismatch error.
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

Value *IfStmtAST::codegen() {
  emitLocation(this);

  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at
  // the end of the function.
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value.
  Builder->SetInsertPoint(ThenBB);

  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the
  // PHI.
  ThenBB = Builder->GetInsertBlock();

  // Emit else block.
  TheFunction->insert(TheFunction->end(), ElseBB);
  Builder->SetInsertPoint(ElseBB);

  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Else' can change the current block, update ElseBB for the
  // PHI.
  ElseBB = Builder->GetInsertBlock();

  // Emit merge block.
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");

  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
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

  Builder->CreateRet(RetVal);
  return RetVal;
}

Function *PrototypeAST::codegen() {
  // Special case: main function returns int, everything else returns double
  Type *RetType;
  if (Name == "main") {
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

  DIFile *Unit;
  DISubprogram *SP;

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
    emitLocation(nullptr);
  }

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  unsigned ArgIdx = 0;

  for (auto &Arg : TheFunction->args()) {
    // Create an alloca for this variable.
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());
    DIScope *FContext = Unit;
    unsigned LineNo = P.getLine();
    unsigned ScopeLine = LineNo;

    // Create a debug descriptor for the variable.
    if (KSDbgInfo) {
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
    // Special handling for main function: convert double to int
    if (P.getName() == "main") {
      // Convert double to i32
      RetVal = Builder->CreateFPToSI(RetVal, Type::getInt32Ty(*TheContext),
                                     "mainret");
    }

    // Finish off the function.
    Builder->CreateRet(RetVal);

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

      // Step 2: Optionally compile runtime.c to runtime.o if it exists
      std::string runtimeC = "runtime.c"; // Or use absolute path if needed
      std::string runtimeObj = "runtime.o";
      bool hasRuntime = sys::fs::exists(runtimeC);

      if (hasRuntime) {
        if (Verbose)
          std::cout << "Compiling runtime library...\n";

        std::string compileRuntime =
            "clang -c " + runtimeC + " -o " + runtimeObj;
        int compileResult = system(compileRuntime.c_str());

        if (compileResult != 0) {
          errs() << "Error: Failed to compile runtime library\n";
          errs() << "Make sure runtime.c exists in the current directory\n";
          return 1;
        }
      } else if (Verbose) {
        std::cout << "No runtime.c found; linking without runtime support\n";
      }

      // Step 3: Link object files
      if (Verbose)
        std::cout << "Linking...\n";

      std::string linkCmd = "clang " + scriptObj;
      if (hasRuntime)
        linkCmd += " " + runtimeObj;
      linkCmd += " -o " + exeFile;
      int linkResult = system(linkCmd.c_str());

      if (linkResult != 0) {
        errs() << "Error: Linking failed\n";
        return 1;
      }

      if (Verbose) {
        std::cout << "Successfully created executable: " << exeFile << "\n";
        // Optionally clean up intermediate files
        std::cout << "Cleaning up intermediate files...\n";
        remove(scriptObj.c_str());
        if (hasRuntime)
          remove(runtimeObj.c_str());
      } else {
        std::cout << exeFile << "\n";
        remove(scriptObj.c_str());
        if (hasRuntime)
          remove(runtimeObj.c_str());
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

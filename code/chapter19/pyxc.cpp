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
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
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

// Accepts -O0, -O1, -O2, -O3
static cl::opt<std::string> OptLevel(
    "O", cl::desc("Optimization level (0-3)"), cl::value_desc("level"),
    cl::init("2"), cl::Prefix, cl::cat(PyxcCategory));

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
  tok_type = -25,

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

  // var definition
  tok_var = -15,
  tok_print = -27,
  tok_while = -28,
  tok_do = -29,
  tok_break = -30,
  tok_continue = -31,
  tok_struct = -32,

  // indentation
  tok_indent = -16,
  tok_dedent = -17,

  // logical keywords
  tok_not = -18,
  tok_and = -19,
  tok_or = -20,

  // multi-character comparison operators
  tok_eq = -21, // ==
  tok_ne = -22, // !=
  tok_le = -23, // <=
  tok_ge = -24, // >=
  tok_arrow = -26, // ->
};

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number
static bool NumIsIntegerLiteral;  // Filled in if tok_number
static int64_t NumIntVal;         // Filled in if tok_number

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},   {"elif", tok_elif},     {"else", tok_else},
    {"for", tok_for}, {"in", tok_in},         {"range", tok_range},
    {"var", tok_var}, {"type", tok_type},     {"not", tok_not},
    {"and", tok_and}, {"print", tok_print},   {"while", tok_while},
    {"do", tok_do},   {"break", tok_break},   {"continue", tok_continue},
    {"or", tok_or},   {"struct", tok_struct}};

struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

enum class TypeExprKind { Builtin, AliasRef, Pointer };

struct TypeExpr {
  TypeExprKind Kind;
  std::string Name;
  std::shared_ptr<TypeExpr> Elem;

  static std::shared_ptr<TypeExpr> Builtin(const std::string &Name) {
    return std::make_shared<TypeExpr>(TypeExpr{TypeExprKind::Builtin, Name, nullptr});
  }

  static std::shared_ptr<TypeExpr> Alias(const std::string &Name) {
    return std::make_shared<TypeExpr>(TypeExpr{TypeExprKind::AliasRef, Name, nullptr});
  }

  static std::shared_ptr<TypeExpr> Pointer(std::shared_ptr<TypeExpr> Elem) {
    return std::make_shared<TypeExpr>(TypeExpr{TypeExprKind::Pointer, "", std::move(Elem)});
  }
};

using TypeExprPtr = std::shared_ptr<TypeExpr>;

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
static bool HadError = false;
template <typename T = void> T LogError(const char *Str) {
  HadError = true;
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

  if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
      IdentifierStr += LastChar;

    // Is this a known keyword? If yes, return that.
    // Else it is an ordinary identifier.
    const auto it = Keywords.find(IdentifierStr);
    return (it != Keywords.end()) ? it->second : tok_identifier;
  }

  bool DotStartsNumber = false;
  if (LastChar == '.') {
    int NextCh = getc(InputFile);
    if (NextCh != EOF)
      ungetc(NextCh, InputFile);
    DotStartsNumber = isdigit(NextCh);
  }

  if (isdigit(LastChar) || DotStartsNumber) { // Number: [0-9.]+
    std::string NumStr;
    bool SawDot = false;
    do {
      if (LastChar == '.')
        SawDot = true;
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    NumIsIntegerLiteral = !SawDot;
    NumIntVal = NumIsIntegerLiteral ? strtoll(NumStr.c_str(), nullptr, 10) : 0;
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

  // Multi-character comparison operators.
  if (LastChar == '=') {
    LastChar = advance();
    if (LastChar == '=') {
      LastChar = advance();
      return tok_eq;
    }
    return '=';
  }

  if (LastChar == '!') {
    LastChar = advance();
    if (LastChar == '=') {
      LastChar = advance();
      return tok_ne;
    }
    return '!';
  }

  if (LastChar == '<') {
    LastChar = advance();
    if (LastChar == '=') {
      LastChar = advance();
      return tok_le;
    }
    return '<';
  }

  if (LastChar == '>') {
    LastChar = advance();
    if (LastChar == '=') {
      LastChar = advance();
      return tok_ge;
    }
    return '>';
  }

  if (LastChar == '-') {
    LastChar = advance();
    if (LastChar == '>') {
      LastChar = advance();
      return tok_arrow;
    }
    return '-';
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
  case tok_type:
    return "<type>";
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
  case tok_var:
    return "<var>";
  case tok_print:
    return "<print>";
  case tok_while:
    return "<while>";
  case tok_do:
    return "<do>";
  case tok_break:
    return "<break>";
  case tok_continue:
    return "<continue>";
  case tok_struct:
    return "<struct>";
  case tok_not:
    return "<not>";
  case tok_and:
    return "<and>";
  case tok_or:
    return "<or>";
  case tok_arrow:
    return "<arrow>";
  case tok_eq:
    return "<eq>";
  case tok_ne:
    return "<ne>";
  case tok_le:
    return "<le>";
  case tok_ge:
    return "<ge>";
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
  virtual Value *codegenAddress() { return nullptr; }
  virtual const std::string *getVariableName() const { return nullptr; }
  virtual Type *getValueTypeHint() const { return nullptr; }
  virtual Type *getPointeeTypeHint() const { return nullptr; }
  virtual std::string getBuiltinLeafTypeHint() const { return ""; }
  virtual std::string getPointeeBuiltinLeafTypeHint() const { return ""; }
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

class TypedAssignStmtAST : public StmtAST {
  std::string Name;
  TypeExprPtr DeclType;
  std::unique_ptr<ExprAST> InitExpr;

public:
  TypedAssignStmtAST(SourceLocation Loc, std::string Name, TypeExprPtr DeclType,
                     std::unique_ptr<ExprAST> InitExpr)
      : StmtAST(Loc), Name(std::move(Name)), DeclType(std::move(DeclType)),
        InitExpr(std::move(InitExpr)) {}

  Value *codegen() override;
};

class AssignStmtAST : public StmtAST {
  std::unique_ptr<ExprAST> LHS;
  std::unique_ptr<ExprAST> RHS;

public:
  AssignStmtAST(SourceLocation Loc, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : StmtAST(Loc), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

  Value *codegen() override;
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;
  bool IsIntegerLiteral;
  int64_t IntVal;

public:
  NumberExprAST(double Val, bool IsIntegerLiteral, int64_t IntVal)
      : Val(Val), IsIntegerLiteral(IsIntegerLiteral), IntVal(IntVal) {}
  raw_ostream &dump(raw_ostream &out, int ind) override {
    if (IsIntegerLiteral)
      return ExprAST::dump(out << IntVal, ind);
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
  const std::string *getVariableName() const override { return &Name; }
  Value *codegen() override;
  Value *codegenAddress() override;
  Type *getValueTypeHint() const override;
  Type *getPointeeTypeHint() const override;
  std::string getBuiltinLeafTypeHint() const override;
  std::string getPointeeBuiltinLeafTypeHint() const override;
};

class AddrExprAST : public ExprAST {
  std::unique_ptr<ExprAST> Operand;

public:
  AddrExprAST(SourceLocation Loc, std::unique_ptr<ExprAST> Operand)
      : ExprAST(Loc), Operand(std::move(Operand)) {}

  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "addr", ind);
    Operand->dump(indent(out, ind) << "Expr:", ind + 1);
    return out;
  }
  Value *codegen() override;
  Type *getValueTypeHint() const override;
  Type *getPointeeTypeHint() const override;
  std::string getPointeeBuiltinLeafTypeHint() const override;
};

class IndexExprAST : public ExprAST {
  std::unique_ptr<ExprAST> Base;
  std::unique_ptr<ExprAST> Index;

public:
  IndexExprAST(SourceLocation Loc, std::unique_ptr<ExprAST> Base,
               std::unique_ptr<ExprAST> Index)
      : ExprAST(Loc), Base(std::move(Base)), Index(std::move(Index)) {}

  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "index", ind);
    Base->dump(indent(out, ind) << "Base:", ind + 1);
    Index->dump(indent(out, ind) << "Index:", ind + 1);
    return out;
  }
  Value *codegen() override;
  Value *codegenAddress() override;
  Type *getValueTypeHint() const override;
  std::string getBuiltinLeafTypeHint() const override;
};

class MemberExprAST : public ExprAST {
  std::unique_ptr<ExprAST> Base;
  std::string FieldName;

public:
  MemberExprAST(SourceLocation Loc, std::unique_ptr<ExprAST> Base,
                std::string FieldName)
      : ExprAST(Loc), Base(std::move(Base)), FieldName(std::move(FieldName)) {}

  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "member ." << FieldName, ind);
    Base->dump(indent(out, ind) << "Base:", ind + 1);
    return out;
  }
  Value *codegen() override;
  Value *codegenAddress() override;
  Type *getValueTypeHint() const override;
  std::string getBuiltinLeafTypeHint() const override;
};

/// UnaryExprAST - Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
  int Opcode;
  std::unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(SourceLocation Loc, int Opcode, std::unique_ptr<ExprAST> Operand)
      : ExprAST(Loc), Opcode(Opcode), Operand(std::move(Operand)) {}
  raw_ostream &dump(raw_ostream &out, int ind) override {
    if (const char *Name = TokenName(Opcode))
      ExprAST::dump(out << "unary" << Name, ind);
    else
      ExprAST::dump(out << "unary" << static_cast<char>(Opcode), ind);
    Operand->dump(out, ind + 1);
    return out;
  }
  Value *codegen() override;
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  int Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(SourceLocation Loc, int Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : ExprAST(Loc), Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  raw_ostream &dump(raw_ostream &out, int ind) override {
    if (const char *Name = TokenName(Op))
      ExprAST::dump(out << "binary" << Name, ind);
    else
      ExprAST::dump(out << "binary" << static_cast<char>(Op), ind);
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

class PrintStmtAST : public StmtAST {
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  PrintStmtAST(SourceLocation Loc, std::vector<std::unique_ptr<ExprAST>> Args)
      : StmtAST(Loc), Args(std::move(Args)) {}

  Value *codegen() override;
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
  ForStmtAST(SourceLocation Loc, std::string VarName, std::unique_ptr<ExprAST> Start,
             std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
             std::unique_ptr<BlockSuiteAST> Body)
      : StmtAST(Loc), VarName(std::move(VarName)), Start(std::move(Start)),
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

class WhileStmtAST : public StmtAST {
  std::unique_ptr<ExprAST> Cond;
  std::unique_ptr<BlockSuiteAST> Body;

public:
  WhileStmtAST(SourceLocation Loc, std::unique_ptr<ExprAST> Cond,
               std::unique_ptr<BlockSuiteAST> Body)
      : StmtAST(Loc), Cond(std::move(Cond)), Body(std::move(Body)) {}

  Value *codegen() override;
};

class DoWhileStmtAST : public StmtAST {
  std::unique_ptr<BlockSuiteAST> Body;
  std::unique_ptr<ExprAST> Cond;

public:
  DoWhileStmtAST(SourceLocation Loc, std::unique_ptr<BlockSuiteAST> Body,
                 std::unique_ptr<ExprAST> Cond)
      : StmtAST(Loc), Body(std::move(Body)), Cond(std::move(Cond)) {}

  Value *codegen() override;
};

class BreakStmtAST : public StmtAST {
public:
  explicit BreakStmtAST(SourceLocation Loc) : StmtAST(Loc) {}
  Value *codegen() override;
  bool isTerminator() override { return true; }
};

class ContinueStmtAST : public StmtAST {
public:
  explicit ContinueStmtAST(SourceLocation Loc) : StmtAST(Loc) {}
  Value *codegen() override;
  bool isTerminator() override { return true; }
};

/// VarExprAST - Expression class for var/in
class VarExprAST : public ExprAST {
  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
  std::unique_ptr<ExprAST> Body;

public:
  VarExprAST(
      SourceLocation Loc,
      std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
      std::unique_ptr<ExprAST> Body)
      : ExprAST(Loc), VarNames(std::move(VarNames)), Body(std::move(Body)) {}
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
  std::vector<TypeExprPtr> ArgTypes;
  TypeExprPtr RetType;
  int Line;

public:
  PrototypeAST(SourceLocation Loc, const std::string &Name,
               std::vector<std::string> Args,
               std::vector<TypeExprPtr> ArgTypes, TypeExprPtr RetType)
      : Name(Name), Args(std::move(Args)), ArgTypes(std::move(ArgTypes)),
        RetType(std::move(RetType)), Line(Loc.Line) {}
  Function *codegen();
  const std::string &getName() const { return Name; }
  const std::vector<std::string> &getArgs() const { return Args; }
  const std::vector<TypeExprPtr> &getArgTypes() const { return ArgTypes; }
  const TypeExprPtr &getRetType() const { return RetType; }
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
static std::map<std::string, TypeExprPtr> TypeAliases;
struct StructFieldDecl {
  std::string Name;
  TypeExprPtr Ty;
};
struct StructDeclInfo {
  std::string Name;
  std::vector<StructFieldDecl> Fields;
  std::map<std::string, unsigned> FieldIndex;
  StructType *LLTy = nullptr;
};
static std::map<std::string, StructDeclInfo> StructDecls;
static std::map<const StructType *, std::string> StructTypeNames;

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<int, int> BinopPrecedence = {
    {tok_or, 5}, {tok_and, 6}, {tok_eq, 10}, {tok_ne, 10},
    {'|', 7},    {'^', 8},     {'&', 9},     {'<', 12},
    {'>', 12},   {tok_le, 12}, {tok_ge, 12},
    {'+', 20},   {'-', 20},    {'*', 40},    {'/', 40}, {'%', 40}};

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  // Make sure it's a declared binop.
  auto It = BinopPrecedence.find(CurTok);
  if (It == BinopPrecedence.end())
    return -1;
  int TokPrec = It->second;
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS);
static std::unique_ptr<BlockSuiteAST> ParseSuite();
static std::unique_ptr<BlockSuiteAST> ParseBlockSuite();
static std::unique_ptr<StmtAST> ParsePrintStmt();
static TypeExprPtr ParseTypeExpr();
static bool ParseStructDecl();

static void SkipToNextLine() {
  while (CurTok != tok_eol && CurTok != tok_eof && CurTok != tok_error)
    getNextToken();
  if (CurTok == tok_eol)
    getNextToken();
}

static bool IsBuiltinTypeName(const std::string &Name) {
  static const std::set<std::string> Builtins = {
      "void", "i8",  "i16", "i32", "i64", "u8", "u16",
      "u32",  "u64", "f32", "f64"};
  return Builtins.find(Name) != Builtins.end();
}

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result =
      std::make_unique<NumberExprAST>(NumVal, NumIsIntegerLiteral, NumIntVal);
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

static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  SourceLocation IdLoc = CurLoc;
  getNextToken(); // eat identifier

  std::unique_ptr<ExprAST> Expr;
  if (IdName == "addr" && CurTok == '(') {
    getNextToken(); // eat '('
    auto Operand = ParseExpression();
    if (!Operand)
      return nullptr;
    if (CurTok != ')')
      return LogError<ExprPtr>("Expected ')' after addr operand");
    getNextToken(); // eat ')'
    Expr = std::make_unique<AddrExprAST>(IdLoc, std::move(Operand));
  } else {
    Expr = std::make_unique<VariableExprAST>(IdLoc, IdName);
  }

  while (true) {
    if (CurTok == '(') {
      const std::string *CalleeName = Expr->getVariableName();
      if (!CalleeName)
        return LogError<ExprPtr>("Only named functions can be called");

      getNextToken(); // eat '('
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
      getNextToken(); // eat ')'
      Expr = std::make_unique<CallExprAST>(IdLoc, *CalleeName, std::move(Args));
      continue;
    }

    if (CurTok == '[') {
      SourceLocation IndexLoc = CurLoc;
      getNextToken(); // eat '['
      auto Index = ParseExpression();
      if (!Index)
        return nullptr;
      if (CurTok != ']')
        return LogError<ExprPtr>("Expected ']'");
      getNextToken(); // eat ']'
      Expr =
          std::make_unique<IndexExprAST>(IndexLoc, std::move(Expr), std::move(Index));
      continue;
    }

    if (CurTok == '.') {
      SourceLocation MemberLoc = CurLoc;
      getNextToken(); // eat '.'
      if (CurTok != tok_identifier)
        return LogError<ExprPtr>("Expected field name after '.'");
      std::string FieldName = IdentifierStr;
      getNextToken(); // eat field name
      Expr = std::make_unique<MemberExprAST>(MemberLoc, std::move(Expr),
                                             std::move(FieldName));
      continue;
    }

    return Expr;
  }
}

static TypeExprPtr ParseTypeExpr() {
  if (CurTok != tok_identifier)
    return LogError<TypeExprPtr>("Expected type name");

  std::string TyName = IdentifierStr;
  getNextToken(); // eat type token

  if (TyName == "ptr") {
    if (CurTok != '[')
      return LogError<TypeExprPtr>("Expected '[' after ptr");
    getNextToken(); // eat '['
    auto Elem = ParseTypeExpr();
    if (!Elem)
      return nullptr;
    if (CurTok != ']')
      return LogError<TypeExprPtr>("Expected ']' after ptr element type");
    getNextToken(); // eat ']'
    return TypeExpr::Pointer(std::move(Elem));
  }

  if (IsBuiltinTypeName(TyName))
    return TypeExpr::Builtin(TyName);
  return TypeExpr::Alias(TyName);
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

static bool ParseTypeAliasDecl() {
  if (CurTok != tok_type)
    return false;
  getNextToken(); // eat 'type'
  if (CurTok != tok_identifier) {
    LogError("Expected alias name after 'type'");
    return false;
  }

  std::string AliasName = IdentifierStr;
  getNextToken(); // eat alias name
  if (CurTok != '=') {
    LogError("Expected '=' in type alias declaration");
    return false;
  }
  getNextToken(); // eat '='
  auto AliasedTy = ParseTypeExpr();
  if (!AliasedTy)
    return false;
  TypeAliases[AliasName] = std::move(AliasedTy);
  return true;
}

static bool ParseStructDecl() {
  if (CurTok != tok_struct)
    return false;

  getNextToken(); // eat 'struct'
  if (CurTok != tok_identifier) {
    LogError("Expected struct name after 'struct'");
    return false;
  }

  std::string StructName = IdentifierStr;
  SourceLocation StructLoc = CurLoc;
  getNextToken(); // eat name

  if (StructDecls.count(StructName)) {
    LogError(("Struct '" + StructName + "' is already defined").c_str());
    return false;
  }

  if (CurTok != ':') {
    LogError("Expected ':' after struct name");
    return false;
  }
  getNextToken(); // eat ':'

  if (CurTok != tok_eol) {
    LogError("Expected newline after struct declaration header");
    return false;
  }

  if (getNextToken() != tok_indent) {
    LogError("Expected indent for struct field list");
    return false;
  }
  getNextToken(); // eat indent

  StructDeclInfo Decl;
  Decl.Name = StructName;

  while (CurTok != tok_dedent && CurTok != tok_eof) {
    EatNewLines();
    if (CurTok == tok_dedent || CurTok == tok_eof)
      break;

    if (CurTok != tok_identifier) {
      LogError("Expected field name in struct declaration");
      return false;
    }
    std::string FieldName = IdentifierStr;
    getNextToken(); // eat field name

    if (Decl.FieldIndex.count(FieldName)) {
      LogError(("Duplicate field '" + FieldName + "' in struct '" + StructName +
                "'")
                   .c_str());
      return false;
    }

    if (CurTok != ':') {
      LogError("Expected ':' after struct field name");
      return false;
    }
    getNextToken(); // eat ':'

    auto FieldTy = ParseTypeExpr();
    if (!FieldTy)
      return false;

    unsigned Idx = static_cast<unsigned>(Decl.Fields.size());
    Decl.FieldIndex[FieldName] = Idx;
    Decl.Fields.push_back({FieldName, std::move(FieldTy)});

    if (CurTok == tok_eol)
      getNextToken();
  }

  if (Decl.Fields.empty()) {
    CurLoc = StructLoc;
    LogError(("Struct '" + StructName + "' must declare at least one field")
                 .c_str());
    return false;
  }

  if (CurTok != tok_dedent) {
    LogError("Expected dedent after struct field list");
    return false;
  }
  getNextToken(); // eat dedent

  StructDecls[StructName] = std::move(Decl);
  return true;
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
  SourceLocation ForLoc = CurLoc;
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

  return std::make_unique<ForStmtAST>(ForLoc, IdName, std::move(Start), std::move(End),
                                      std::move(Step), std::move(Body));
}

// while_stmt     = "while" , expression , ":" , suite ;
static std::unique_ptr<StmtAST> ParseWhileStmt() {
  SourceLocation WhileLoc = CurLoc;
  getNextToken(); // eat while

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError<StmtPtr>("expected `:` after while condition");
  getNextToken(); // eat ':'

  auto Body = ParseSuite();
  if (!Body)
    return nullptr;

  return std::make_unique<WhileStmtAST>(WhileLoc, std::move(Cond),
                                        std::move(Body));
}

// do_while_stmt  = "do" , ":" , suite , "while" , expression ;
static std::unique_ptr<StmtAST> ParseDoWhileStmt() {
  SourceLocation DoLoc = CurLoc;
  getNextToken(); // eat do

  if (CurTok != ':')
    return LogError<StmtPtr>("expected `:` after do");
  getNextToken(); // eat ':'

  auto Body = ParseSuite();
  if (!Body)
    return nullptr;

  if (CurTok != tok_while)
    return LogError<StmtPtr>("expected `while` after do suite");
  getNextToken(); // eat while

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  return std::make_unique<DoWhileStmtAST>(DoLoc, std::move(Body),
                                          std::move(Cond));
}

static std::unique_ptr<StmtAST> ParseBreakStmt() {
  auto Loc = CurLoc;
  getNextToken(); // eat break
  return std::make_unique<BreakStmtAST>(Loc);
}

static std::unique_ptr<StmtAST> ParseContinueStmt() {
  auto Loc = CurLoc;
  getNextToken(); // eat continue
  return std::make_unique<ContinueStmtAST>(Loc);
}

/// varexpr ::= 'var' identifier ('=' expression)?
//                    (',' identifier ('=' expression)?)* 'in' expression
static std::unique_ptr<ExprAST> ParseVarExpr() {
  SourceLocation VarLoc = CurLoc;
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

  return std::make_unique<VarExprAST>(VarLoc, std::move(VarNames), std::move(Body));
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
  std::unique_ptr<ExprAST> Expr;
  if (CurTok != tok_eol && CurTok != tok_dedent && CurTok != tok_eof) {
    Expr = ParseExpression();
    if (!Expr)
      return nullptr;
  }
  return std::make_unique<ReturnStmtAST>(ReturnLoc, std::move(Expr));
}

static std::unique_ptr<StmtAST> ParsePrintStmt() {
  auto PrintLoc = CurLoc;
  getNextToken(); // eat `print`
  if (CurTok != '(')
    return LogError<StmtPtr>("Expected '(' after print");
  getNextToken(); // eat '('

  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      auto Arg = ParseExpression();
      if (!Arg)
        return nullptr;
      Args.push_back(std::move(Arg));

      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogError<StmtPtr>("Expected ')' or ',' in print argument list");
      getNextToken(); // eat ','
      if (CurTok == ')')
        return LogError<StmtPtr>("Trailing comma is not allowed in print");
    }
  }

  getNextToken(); // eat ')'
  return std::make_unique<PrintStmtAST>(PrintLoc, std::move(Args));
}

static std::unique_ptr<StmtAST> ParseIdentifierLeadingStmt() {
  auto StmtLoc = CurLoc;
  auto LHS = ParseIdentifierExpr();
  if (!LHS)
    return nullptr;

  if (CurTok == ':') {
    const std::string *Name = LHS->getVariableName();
    if (!Name)
      return LogError<StmtPtr>("Typed declaration requires an identifier");
    getNextToken(); // eat ':'
    auto DeclType = ParseTypeExpr();
    if (!DeclType)
      return nullptr;
    std::unique_ptr<ExprAST> InitExpr;
    if (CurTok == '=') {
      getNextToken(); // eat '='
      InitExpr = ParseExpression();
      if (!InitExpr)
        return nullptr;
    }
    return std::make_unique<TypedAssignStmtAST>(StmtLoc, *Name,
                                                std::move(DeclType),
                                                std::move(InitExpr));
  }

  if (CurTok == '=') {
    getNextToken(); // eat '='
    auto RHS = ParseExpression();
    if (!RHS)
      return nullptr;
    return std::make_unique<AssignStmtAST>(StmtLoc, std::move(LHS),
                                           std::move(RHS));
  }

  auto Expr = ParseBinOpRHS(0, std::move(LHS));
  if (!Expr)
    return nullptr;
  return std::make_unique<ExprStmtAST>(StmtLoc, std::move(Expr));
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
  case tok_while:
    return ParseWhileStmt();
  case tok_do:
    return ParseDoWhileStmt();
  case tok_break:
    return ParseBreakStmt();
  case tok_continue:
    return ParseContinueStmt();
  case tok_return:
    return ParseReturnStmt();
  case tok_print:
    return ParsePrintStmt();
  case tok_type:
    return LogError<StmtPtr>("Type aliases are only allowed at top-level");
  case tok_struct:
    return LogError<StmtPtr>("Struct declarations are only allowed at top-level");
  case tok_identifier:
    return ParseIdentifierLeadingStmt();
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
  // Builtin unary operators only.
  if (CurTok != '+' && CurTok != '-' && CurTok != '!' && CurTok != '~' &&
      CurTok != tok_not)
    return ParsePrimary();

  // If this is a unary operator, read it.
  int Opc = CurTok;
  SourceLocation OpLoc = CurLoc;
  getNextToken();
  if (auto Operand = ParseUnary())
    return std::make_unique<UnaryExprAST>(OpLoc, Opc, std::move(Operand));
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
///   ::= id '(' (id ':' type_expr (',' id ':' type_expr)*)? ')' '->' type_expr
static std::unique_ptr<PrototypeAST> ParsePrototype() {
  std::string FnName;
  SourceLocation FnLoc = CurLoc;

  if (CurTok != tok_identifier) {
    return LogError<ProtoPtr>("Expected function name in prototype");
  }

  FnName = IdentifierStr;
  getNextToken();

  if (CurTok != '(') {
    return LogError<ProtoPtr>("Expected '(' in prototype");
  }

  std::vector<std::string> ArgNames;
  std::vector<TypeExprPtr> ArgTypes;
  getNextToken(); // eat '('
  if (CurTok != ')') {
    while (true) {
      if (CurTok != tok_identifier)
        return LogError<ProtoPtr>("Expected parameter name");
      ArgNames.push_back(IdentifierStr);
      getNextToken(); // eat parameter name
      if (CurTok != ':')
        return LogError<ProtoPtr>("Expected ':' after parameter name");
      getNextToken(); // eat ':'
      auto ParamTy = ParseTypeExpr();
      if (!ParamTy)
        return nullptr;
      ArgTypes.push_back(std::move(ParamTy));

      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogError<ProtoPtr>("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  if (CurTok != ')')
    return LogError<ProtoPtr>("Expected ')' in prototype");
  getNextToken(); // eat ')'.
  if (CurTok != tok_arrow)
    return LogError<ProtoPtr>("Expected '->' in prototype");
  getNextToken(); // eat '->'
  auto RetType = ParseTypeExpr();
  if (!RetType)
    return nullptr;

  return std::make_unique<PrototypeAST>(FnLoc, FnName, std::move(ArgNames),
                                        std::move(ArgTypes),
                                        std::move(RetType));
}

/// definition ::= 'def' prototype ':' suite
static std::unique_ptr<FunctionAST> ParseDefinition() {
  EatNewLines();

  if (CurTok != tok_def)
    return LogError<FuncPtr>("expected 'def'");

  getNextToken(); // eat def.
  auto Proto = ParsePrototype();
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
    auto Proto = std::make_unique<PrototypeAST>(
        FnLoc, "__anon_expr", std::vector<std::string>(),
        std::vector<TypeExprPtr>(), TypeExpr::Builtin("f64"));
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
struct VarBinding {
  AllocaInst *Alloca = nullptr;
  Type *Ty = nullptr;
  Type *PointeeTy = nullptr;
  std::string BuiltinLeafTy;
  std::string PointeeBuiltinLeafTy;
};
static std::map<std::string, VarBinding> NamedValues;
struct LoopContext {
  BasicBlock *BreakTarget = nullptr;
  BasicBlock *ContinueTarget = nullptr;
};
static std::vector<LoopContext> LoopContextStack;

class LoopContextGuard {
  bool Active = false;

public:
  LoopContextGuard(BasicBlock *BreakTarget, BasicBlock *ContinueTarget)
      : Active(true) {
    LoopContextStack.push_back({BreakTarget, ContinueTarget});
  }

  ~LoopContextGuard() {
    if (Active)
      LoopContextStack.pop_back();
  }
};

static std::unique_ptr<PyxcJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static bool EnableFunctionOptimizations = true;
static ExitOnError ExitOnErr;

static bool TryGetOptimizationLevel(OptimizationLevel &Level) {
  if (OptLevel == "0") {
    Level = OptimizationLevel::O0;
    return true;
  }
  if (OptLevel == "1") {
    Level = OptimizationLevel::O1;
    return true;
  }
  if (OptLevel == "2") {
    Level = OptimizationLevel::O2;
    return true;
  }
  if (OptLevel == "3") {
    Level = OptimizationLevel::O3;
    return true;
  }
  return false;
}

class ScopedFunctionOptimization {
  bool Prev;

public:
  explicit ScopedFunctionOptimization(bool Enabled)
      : Prev(EnableFunctionOptimizations) {
    EnableFunctionOptimizations = Enabled;
  }
  ~ScopedFunctionOptimization() { EnableFunctionOptimizations = Prev; }
};

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

static Type *BuiltinTypeToLLVM(const std::string &Name) {
  if (Name == "void")
    return Type::getVoidTy(*TheContext);
  if (Name == "i8" || Name == "u8")
    return Type::getInt8Ty(*TheContext);
  if (Name == "i16" || Name == "u16")
    return Type::getInt16Ty(*TheContext);
  if (Name == "i32" || Name == "u32")
    return Type::getInt32Ty(*TheContext);
  if (Name == "i64" || Name == "u64")
    return Type::getInt64Ty(*TheContext);
  if (Name == "f32")
    return Type::getFloatTy(*TheContext);
  if (Name == "f64")
    return Type::getDoubleTy(*TheContext);
  return nullptr;
}

static Type *ResolveTypeExpr(const TypeExprPtr &Ty,
                             std::set<std::string> &Visited);

static void EnsureDefaultTypeAliases() {
  auto ensure = [](const std::string &Alias, TypeExprPtr Ty) {
    if (TypeAliases.find(Alias) == TypeAliases.end())
      TypeAliases[Alias] = std::move(Ty);
  };

  ensure("int", TypeExpr::Builtin("i32"));
  ensure("char", TypeExpr::Builtin("i8"));
  ensure("float", TypeExpr::Builtin("f32"));
  ensure("double", TypeExpr::Builtin("f64"));

  const DataLayout &DL = TheModule->getDataLayout();
  unsigned PtrBits = DL.getPointerSizeInBits(0);
  ensure("long", TypeExpr::Builtin(PtrBits == 32 ? "i32" : "i64"));
  ensure("size_t", TypeExpr::Builtin(PtrBits == 32 ? "u32" : "u64"));
}

static StructType *ResolveStructTypeByName(const std::string &Name,
                                           std::set<std::string> &Visited);

static const StructDeclInfo *GetStructInfoForType(Type *Ty) {
  auto *ST = dyn_cast_or_null<StructType>(Ty);
  if (!ST)
    return nullptr;
  auto ItName = StructTypeNames.find(ST);
  if (ItName == StructTypeNames.end())
    return nullptr;
  auto ItDecl = StructDecls.find(ItName->second);
  if (ItDecl == StructDecls.end())
    return nullptr;
  return &ItDecl->second;
}

static const StructFieldDecl *
GetStructFieldByName(const StructDeclInfo &Decl, const std::string &FieldName,
                     unsigned *FieldIdx = nullptr) {
  auto It = Decl.FieldIndex.find(FieldName);
  if (It == Decl.FieldIndex.end())
    return nullptr;
  if (FieldIdx)
    *FieldIdx = It->second;
  return &Decl.Fields[It->second];
}

static StructType *ResolveStructTypeByName(const std::string &Name,
                                           std::set<std::string> &Visited) {
  auto It = StructDecls.find(Name);
  if (It == StructDecls.end())
    return nullptr;
  StructDeclInfo &Decl = It->second;
  if (Decl.LLTy)
    return Decl.LLTy;

  if (Visited.count(Name))
    return nullptr;
  Visited.insert(Name);

  StructType *ST = StructType::create(*TheContext, "struct." + Name);
  Decl.LLTy = ST;
  StructTypeNames[ST] = Name;

  std::vector<Type *> FieldTys;
  FieldTys.reserve(Decl.Fields.size());
  for (const auto &Field : Decl.Fields) {
    Type *FTy = nullptr;
    if (Field.Ty->Kind == TypeExprKind::AliasRef &&
        StructDecls.count(Field.Ty->Name) != 0) {
      StructType *Nested = ResolveStructTypeByName(Field.Ty->Name, Visited);
      if (!Nested)
        return LogError<StructType *>(
            ("Unknown struct field type: " + Field.Ty->Name).c_str());
      FTy = Nested;
    } else {
      FTy = ResolveTypeExpr(Field.Ty, Visited);
    }
    if (!FTy)
      return LogError<StructType *>(
          ("Failed to resolve field type in struct '" + Name + "'").c_str());
    FieldTys.push_back(FTy);
  }

  ST->setBody(FieldTys, false);
  Visited.erase(Name);
  return ST;
}

static Type *ResolveTypeExpr(const TypeExprPtr &Ty,
                             std::set<std::string> &Visited) {
  if (!Ty)
    return nullptr;

  if (Ty->Kind == TypeExprKind::Builtin)
    return BuiltinTypeToLLVM(Ty->Name);

  if (Ty->Kind == TypeExprKind::Pointer) {
    Type *ElemTy = ResolveTypeExpr(Ty->Elem, Visited);
    if (!ElemTy)
      return nullptr;
    (void)ElemTy;
    return PointerType::getUnqual(*TheContext);
  }

  auto It = TypeAliases.find(Ty->Name);
  if (It != TypeAliases.end()) {
    if (Visited.count(Ty->Name))
      return LogError<Type *>(
          ("Alias cycle detected at type: " + Ty->Name).c_str());
    Visited.insert(Ty->Name);
    return ResolveTypeExpr(It->second, Visited);
  }

  if (StructDecls.count(Ty->Name)) {
    StructType *ST = ResolveStructTypeByName(Ty->Name, Visited);
    if (!ST)
      return LogError<Type *>(("Unknown struct type: " + Ty->Name).c_str());
    return ST;
  }

  return LogError<Type *>(("Unknown type alias: " + Ty->Name).c_str());
}

static Type *ResolveTypeExpr(const TypeExprPtr &Ty) {
  std::set<std::string> Visited;
  return ResolveTypeExpr(Ty, Visited);
}

static Type *ResolvePointeeTypeExpr(const TypeExprPtr &Ty,
                                    std::set<std::string> &Visited) {
  if (!Ty)
    return nullptr;
  if (Ty->Kind == TypeExprKind::Pointer)
    return ResolveTypeExpr(Ty->Elem);
  if (Ty->Kind == TypeExprKind::AliasRef) {
    auto It = TypeAliases.find(Ty->Name);
    if (It == TypeAliases.end() || Visited.count(Ty->Name))
      return nullptr;
    Visited.insert(Ty->Name);
    return ResolvePointeeTypeExpr(It->second, Visited);
  }
  return nullptr;
}

static Type *ResolvePointeeTypeExpr(const TypeExprPtr &Ty) {
  std::set<std::string> Visited;
  return ResolvePointeeTypeExpr(Ty, Visited);
}

static std::string ResolveBuiltinLeafName(const TypeExprPtr &Ty,
                                          std::set<std::string> &Visited) {
  if (!Ty)
    return "";
  if (Ty->Kind == TypeExprKind::Builtin)
    return Ty->Name;
  if (Ty->Kind == TypeExprKind::Pointer)
    return "ptr";
  auto It = TypeAliases.find(Ty->Name);
  if (It == TypeAliases.end() || Visited.count(Ty->Name))
    return "";
  Visited.insert(Ty->Name);
  return ResolveBuiltinLeafName(It->second, Visited);
}

static std::string ResolveBuiltinLeafName(const TypeExprPtr &Ty) {
  std::set<std::string> Visited;
  return ResolveBuiltinLeafName(Ty, Visited);
}

static std::string ResolvePointeeBuiltinLeafName(const TypeExprPtr &Ty,
                                                 std::set<std::string> &Visited) {
  if (!Ty)
    return "";
  if (Ty->Kind == TypeExprKind::Pointer)
    return ResolveBuiltinLeafName(Ty->Elem);
  if (Ty->Kind == TypeExprKind::AliasRef) {
    auto It = TypeAliases.find(Ty->Name);
    if (It == TypeAliases.end() || Visited.count(Ty->Name))
      return "";
    Visited.insert(Ty->Name);
    return ResolvePointeeBuiltinLeafName(It->second, Visited);
  }
  return "";
}

static std::string ResolvePointeeBuiltinLeafName(const TypeExprPtr &Ty) {
  std::set<std::string> Visited;
  return ResolvePointeeBuiltinLeafName(Ty, Visited);
}

static Attribute::AttrKind GetExtAttrForTypeExpr(const TypeExprPtr &Ty) {
  Type *LLTy = ResolveTypeExpr(Ty);
  if (!LLTy || !LLTy->isIntegerTy())
    return Attribute::None;
  if (LLTy->getIntegerBitWidth() >= 32)
    return Attribute::None;

  std::string Leaf = ResolveBuiltinLeafName(Ty);
  if (Leaf == "u8" || Leaf == "u16")
    return Attribute::ZExt;
  return Attribute::SExt;
}

static bool IsIntegerLike(Type *Ty) { return Ty && Ty->isIntegerTy(); }

static Value *CastValueTo(Value *V, Type *DstTy) {
  if (!V || !DstTy)
    return nullptr;
  Type *SrcTy = V->getType();
  if (SrcTy == DstTy)
    return V;

  if (SrcTy->isFloatingPointTy() && DstTy->isFloatingPointTy())
    return Builder->CreateFPCast(V, DstTy, "castfp");
  if (SrcTy->isFloatingPointTy() && DstTy->isIntegerTy())
    return Builder->CreateFPToSI(V, DstTy, "castfptosi");
  if (SrcTy->isIntegerTy() && DstTy->isFloatingPointTy())
    return Builder->CreateSIToFP(V, DstTy, "castsitofp");
  if (SrcTy->isIntegerTy() && DstTy->isIntegerTy())
    return Builder->CreateIntCast(V, DstTy, true, "castint");
  if (SrcTy->isPointerTy() && DstTy->isPointerTy())
    return Builder->CreatePointerCast(V, DstTy, "castptr");
  if (SrcTy->isPointerTy() && DstTy->isIntegerTy())
    return Builder->CreatePtrToInt(V, DstTy, "castptrtoint");
  if (SrcTy->isIntegerTy() && DstTy->isPointerTy())
    return Builder->CreateIntToPtr(V, DstTy, "castinttoptr");

  return LogError<Value *>("Unsupported type conversion");
}

static Value *ToBoolI1(Value *V, const Twine &Name) {
  if (!V)
    return nullptr;
  Type *Ty = V->getType();
  if (Ty->isIntegerTy(1))
    return V;
  if (Ty->isFloatingPointTy())
    return Builder->CreateFCmpONE(
        V, ConstantFP::get(Ty, 0.0), Name);
  if (Ty->isIntegerTy())
    return Builder->CreateICmpNE(V, ConstantInt::get(Ty, 0), Name);
  if (Ty->isPointerTy())
    return Builder->CreateICmpNE(V, ConstantPointerNull::get(
                                        cast<PointerType>(Ty)),
                                 Name);
  return LogError<Value *>("Cannot convert value to boolean");
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
                                          StringRef VarName, Type *VarTy) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(VarTy, nullptr, VarName);
}

Value *NumberExprAST::codegen() {
  emitLocation(this);
  if (IsIntegerLiteral)
    return ConstantInt::get(Type::getInt64Ty(*TheContext), IntVal, true);
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end() || !It->second.Alloca)
    return LogError<Value *>(("Unknown variable name " + Name).c_str());
  AllocaInst *A = It->second.Alloca;
  emitLocation(this);
  // Load the value.
  return Builder->CreateLoad(A->getAllocatedType(), A, Name.c_str());
}

Value *VariableExprAST::codegenAddress() {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end() || !It->second.Alloca)
    return LogError<Value *>(("Unknown variable name " + Name).c_str());
  emitLocation(this);
  return It->second.Alloca;
}

Type *VariableExprAST::getValueTypeHint() const {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end())
    return nullptr;
  return It->second.Ty;
}

Type *VariableExprAST::getPointeeTypeHint() const {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end())
    return nullptr;
  return It->second.PointeeTy;
}

std::string VariableExprAST::getBuiltinLeafTypeHint() const {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end())
    return "";
  return It->second.BuiltinLeafTy;
}

std::string VariableExprAST::getPointeeBuiltinLeafTypeHint() const {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end())
    return "";
  return It->second.PointeeBuiltinLeafTy;
}

Value *AddrExprAST::codegen() {
  emitLocation(this);
  Value *AddrV = Operand->codegenAddress();
  if (!AddrV)
    return LogError<Value *>("addr() requires an addressable expression");
  return AddrV;
}

Type *AddrExprAST::getValueTypeHint() const {
  return PointerType::getUnqual(*TheContext);
}

Type *AddrExprAST::getPointeeTypeHint() const {
  return Operand->getValueTypeHint();
}

std::string AddrExprAST::getPointeeBuiltinLeafTypeHint() const {
  return Operand->getBuiltinLeafTypeHint();
}

Value *IndexExprAST::codegenAddress() {
  emitLocation(this);
  Value *BaseV = Base->codegen();
  if (!BaseV)
    return nullptr;
  Type *BaseTy = BaseV->getType();
  if (!BaseTy->isPointerTy())
    return LogError<Value *>("Indexing requires a pointer base");
  Type *ElemTy = Base->getPointeeTypeHint();
  if (!ElemTy)
    return LogError<Value *>("Cannot determine pointee type for indexing");
  if (ElemTy->isVoidTy())
    return LogError<Value *>("Cannot index through ptr[void]");

  Value *IdxV = Index->codegen();
  if (!IdxV)
    return nullptr;
  if (!IsIntegerLike(IdxV->getType()))
    return LogError<Value *>("Pointer index must be an integer type");

  IdxV = CastValueTo(IdxV, Type::getInt64Ty(*TheContext));
  if (!IdxV)
    return nullptr;
  return Builder->CreateGEP(ElemTy, BaseV, IdxV, "idx.addr");
}

Value *IndexExprAST::codegen() {
  Value *AddrV = codegenAddress();
  if (!AddrV)
    return nullptr;
  Type *ElemTy = getValueTypeHint();
  if (!ElemTy)
    return LogError<Value *>("Cannot determine index result type");
  return Builder->CreateLoad(ElemTy, AddrV, "idx.load");
}

Type *IndexExprAST::getValueTypeHint() const { return Base->getPointeeTypeHint(); }

std::string IndexExprAST::getBuiltinLeafTypeHint() const {
  return Base->getPointeeBuiltinLeafTypeHint();
}

Value *MemberExprAST::codegenAddress() {
  emitLocation(this);
  Value *BaseAddr = Base->codegenAddress();
  if (!BaseAddr)
    return LogError<Value *>("Member access requires an addressable base");
  Type *BaseTy = Base->getValueTypeHint();
  const StructDeclInfo *Decl = GetStructInfoForType(BaseTy);
  if (!Decl)
    return LogError<Value *>("Member access requires a struct-typed base");

  unsigned FieldIdx = 0;
  const StructFieldDecl *Field =
      GetStructFieldByName(*Decl, FieldName, &FieldIdx);
  if (!Field)
    return LogError<Value *>(
        ("Unknown field '" + FieldName + "' on struct '" + Decl->Name + "'")
            .c_str());
  auto *ST = dyn_cast<StructType>(BaseTy);
  return Builder->CreateStructGEP(ST, BaseAddr, FieldIdx, "field.addr");
}

Value *MemberExprAST::codegen() {
  Value *AddrV = codegenAddress();
  if (!AddrV)
    return nullptr;
  Type *FieldTy = getValueTypeHint();
  if (!FieldTy)
    return LogError<Value *>("Cannot determine member field type");
  return Builder->CreateLoad(FieldTy, AddrV, "field.load");
}

Type *MemberExprAST::getValueTypeHint() const {
  Type *BaseTy = Base->getValueTypeHint();
  const StructDeclInfo *Decl = GetStructInfoForType(BaseTy);
  if (!Decl)
    return nullptr;
  const StructFieldDecl *Field = GetStructFieldByName(*Decl, FieldName);
  if (!Field)
    return nullptr;
  return ResolveTypeExpr(Field->Ty);
}

std::string MemberExprAST::getBuiltinLeafTypeHint() const {
  Type *BaseTy = Base->getValueTypeHint();
  const StructDeclInfo *Decl = GetStructInfoForType(BaseTy);
  if (!Decl)
    return "";
  const StructFieldDecl *Field = GetStructFieldByName(*Decl, FieldName);
  if (!Field)
    return "";
  return ResolveBuiltinLeafName(Field->Ty);
}

Value *TypedAssignStmtAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  Type *DeclTy = ResolveTypeExpr(DeclType);
  if (!DeclTy)
    return nullptr;
  if (DeclTy->isVoidTy())
    return LogError<Value *>("Variables cannot have type void");

  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Name, DeclTy);
  Value *InitVal = nullptr;
  if (InitExpr) {
    if (DeclTy->isStructTy())
      return LogError<Value *>(
          "Struct variables do not support direct initializer expressions");
    InitVal = InitExpr->codegen();
    if (!InitVal)
      return nullptr;
    InitVal = CastValueTo(InitVal, DeclTy);
    if (!InitVal)
      return nullptr;
  } else {
    InitVal = Constant::getNullValue(DeclTy);
  }
  Builder->CreateStore(InitVal, Alloca);
  NamedValues[Name] = {Alloca, DeclTy, ResolvePointeeTypeExpr(DeclType),
                       ResolveBuiltinLeafName(DeclType),
                       ResolvePointeeBuiltinLeafName(DeclType)};
  return InitVal;
}

Value *AssignStmtAST::codegen() {
  Value *AddrV = LHS->codegenAddress();
  if (!AddrV)
    return LogError<Value *>("Assignment destination must be an lvalue");
  Type *ElemTy = LHS->getValueTypeHint();
  if (!ElemTy)
    return LogError<Value *>("Cannot determine assignment destination type");
  Value *RHSV = RHS->codegen();
  if (!RHSV)
    return nullptr;
  RHSV = CastValueTo(RHSV, ElemTy);
  if (!RHSV)
    return nullptr;
  Builder->CreateStore(RHSV, AddrV);
  return RHSV;
}

Value *UnaryExprAST::codegen() {
  Value *OperandV = Operand->codegen();
  if (!OperandV)
    return nullptr;
  emitLocation(this);
  switch (Opcode) {
  case '+':
    return OperandV;
  case '-':
    if (OperandV->getType()->isFloatingPointTy())
      return Builder->CreateFNeg(OperandV, "negtmp");
    if (OperandV->getType()->isIntegerTy())
      return Builder->CreateNeg(OperandV, "negtmp");
    return LogError<Value *>("Unary '-' requires numeric operand");
  case '!':
  case tok_not: {
    Value *AsBool = ToBoolI1(OperandV, "nottmp.bool");
    if (!AsBool)
      return nullptr;
    Value *NegBool = Builder->CreateNot(AsBool, "nottmp.inv");
    return Builder->CreateUIToFP(NegBool, Type::getDoubleTy(*TheContext),
                                 "nottmp");
  }
  case '~':
    if (!OperandV->getType()->isIntegerTy())
      return LogError<Value *>("Unary '~' requires integer operand");
    return Builder->CreateNot(OperandV, "bnottmp");
  default:
    return LogError<Value *>("Unknown unary operator");
  }
}

Value *BinaryExprAST::codegen() {
  emitLocation(this);
  Value *L = LHS->codegen();
  if (!L)
    return nullptr;

  if (Op == tok_and || Op == tok_or) {
    Function *TheFunction = Builder->GetInsertBlock()->getParent();
    Value *LBool = ToBoolI1(L, "logic.lbool");
    if (!LBool)
      return nullptr;

    BasicBlock *LHSBB = Builder->GetInsertBlock();
    BasicBlock *RHSBB = BasicBlock::Create(*TheContext, "logic.rhs", TheFunction);
    BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "logic.cont");

    if (Op == tok_and) {
      // false and X -> false (skip RHS), true and X -> evaluate RHS.
      Builder->CreateCondBr(LBool, RHSBB, MergeBB);
    } else {
      // true or X -> true (skip RHS), false or X -> evaluate RHS.
      Builder->CreateCondBr(LBool, MergeBB, RHSBB);
    }

    Builder->SetInsertPoint(RHSBB);
    Value *R = RHS->codegen();
    if (!R)
      return nullptr;
    Value *RBool = ToBoolI1(R, "logic.rbool");
    if (!RBool)
      return nullptr;
    Builder->CreateBr(MergeBB);
    RHSBB = Builder->GetInsertBlock();

    TheFunction->insert(TheFunction->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);
    PHINode *LogicPhi = Builder->CreatePHI(Type::getInt1Ty(*TheContext), 2,
                                           "logic.bool");
    if (Op == tok_and) {
      LogicPhi->addIncoming(ConstantInt::getFalse(*TheContext), LHSBB);
    } else {
      LogicPhi->addIncoming(ConstantInt::getTrue(*TheContext), LHSBB);
    }
    LogicPhi->addIncoming(RBool, RHSBB);
    return Builder->CreateUIToFP(LogicPhi, Type::getDoubleTy(*TheContext),
                                 "logictmp");
  }

  Value *R = RHS->codegen();
  if (!R)
    return nullptr;

  bool RequiresIntOnly = (Op == '%' || Op == '&' || Op == '^' || Op == '|');
  bool UseFP =
      L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy();
  if (RequiresIntOnly) {
    if (!(L->getType()->isIntegerTy() && R->getType()->isIntegerTy())) {
      if (Op == '%')
        return LogError<Value *>(
            "Modulo operator '%' requires integer operands");
      return LogError<Value *>("Bitwise operators require integer operands");
    }
    unsigned W = std::max(L->getType()->getIntegerBitWidth(),
                          R->getType()->getIntegerBitWidth());
    Type *IntTy = IntegerType::get(*TheContext, W);
    L = CastValueTo(L, IntTy);
    R = CastValueTo(R, IntTy);
  } else if (UseFP) {
    Type *FPType = Type::getDoubleTy(*TheContext);
    L = CastValueTo(L, FPType);
    R = CastValueTo(R, FPType);
  } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
    unsigned W = std::max(L->getType()->getIntegerBitWidth(),
                          R->getType()->getIntegerBitWidth());
    Type *IntTy = IntegerType::get(*TheContext, W);
    L = CastValueTo(L, IntTy);
    R = CastValueTo(R, IntTy);
  } else {
    return LogError<Value *>("Unsupported operand types");
  }
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return UseFP ? Builder->CreateFAdd(L, R, "addtmp")
                 : Builder->CreateAdd(L, R, "addtmp");
  case '-':
    return UseFP ? Builder->CreateFSub(L, R, "subtmp")
                 : Builder->CreateSub(L, R, "subtmp");
  case '*':
    return UseFP ? Builder->CreateFMul(L, R, "multmp")
                 : Builder->CreateMul(L, R, "multmp");
  case '/':
    return UseFP ? Builder->CreateFDiv(L, R, "divtmp")
                 : Builder->CreateSDiv(L, R, "divtmp");
  case '%':
    return Builder->CreateSRem(L, R, "modtmp");
  case '&':
    return Builder->CreateAnd(L, R, "andtmp");
  case '^':
    return Builder->CreateXor(L, R, "xortmp");
  case '|':
    return Builder->CreateOr(L, R, "ortmp");
  case '<':
    L = UseFP ? Builder->CreateFCmpULT(L, R, "cmptmp")
              : Builder->CreateICmpSLT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case '>':
    L = UseFP ? Builder->CreateFCmpUGT(L, R, "cmptmp")
              : Builder->CreateICmpSGT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_le:
    L = UseFP ? Builder->CreateFCmpULE(L, R, "cmptmp")
              : Builder->CreateICmpSLE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_ge:
    L = UseFP ? Builder->CreateFCmpUGE(L, R, "cmptmp")
              : Builder->CreateICmpSGE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_eq:
    L = UseFP ? Builder->CreateFCmpUEQ(L, R, "cmptmp")
              : Builder->CreateICmpEQ(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_ne:
    L = UseFP ? Builder->CreateFCmpUNE(L, R, "cmptmp")
              : Builder->CreateICmpNE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogError<Value *>("Unsupported binary operator");
  }
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
  unsigned I = 0;
  for (auto &Formal : CalleeF->args()) {
    Value *ArgV = Args[I++]->codegen();
    if (!ArgV)
      return nullptr;
    ArgV = CastValueTo(ArgV, Formal.getType());
    if (!ArgV)
      return nullptr;
    ArgsV.push_back(ArgV);
  }

  if (CalleeF->getReturnType()->isVoidTy()) {
    Builder->CreateCall(CalleeF, ArgsV);
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }
  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

static Function *GetOrCreatePrintHelper(const std::string &Name, Type *Ty,
                                        bool IsUnsignedInt) {
  if (Function *F = TheModule->getFunction(Name))
    return F;

  FunctionType *FT = FunctionType::get(Ty, {Ty}, false);
  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  if (Ty->isIntegerTy() && Ty->getIntegerBitWidth() < 32) {
    F->addRetAttr(IsUnsignedInt ? Attribute::ZExt : Attribute::SExt);
    F->addParamAttr(0, IsUnsignedInt ? Attribute::ZExt : Attribute::SExt);
  }
  return F;
}

static Function *GetPrintCharHelper() {
  return GetOrCreatePrintHelper("printchard", Type::getDoubleTy(*TheContext),
                                false);
}

static Function *GetPrintHelperForArg(Type *ArgTy, const std::string &LeafHint) {
  if (!ArgTy)
    return nullptr;

  if (ArgTy->isFloatTy())
    return GetOrCreatePrintHelper("printfloat32", ArgTy, false);
  if (ArgTy->isDoubleTy())
    return GetOrCreatePrintHelper("printfloat64", ArgTy, false);
  if (!ArgTy->isIntegerTy())
    return nullptr;

  bool IsUnsigned = !LeafHint.empty() && LeafHint[0] == 'u';
  unsigned W = ArgTy->getIntegerBitWidth();
  switch (W) {
  case 8:
    return GetOrCreatePrintHelper(IsUnsigned ? "printu8" : "printi8", ArgTy,
                                  IsUnsigned);
  case 16:
    return GetOrCreatePrintHelper(IsUnsigned ? "printu16" : "printi16", ArgTy,
                                  IsUnsigned);
  case 32:
    return GetOrCreatePrintHelper(IsUnsigned ? "printu32" : "printi32", ArgTy,
                                  IsUnsigned);
  case 64:
    return GetOrCreatePrintHelper(IsUnsigned ? "printu64" : "printi64", ArgTy,
                                  IsUnsigned);
  default:
    return nullptr;
  }
}

Value *PrintStmtAST::codegen() {
  emitLocation(this);

  Function *PrintCharF = GetPrintCharHelper();
  if (!PrintCharF)
    return LogError<Value *>("Could not resolve print character helper");

  for (size_t I = 0; I < Args.size(); ++I) {
    Value *ArgV = Args[I]->codegen();
    if (!ArgV)
      return nullptr;

    Type *ArgTy = ArgV->getType();
    if (ArgTy->isPointerTy())
      return LogError<Value *>("Unsupported print argument type: pointer");

    Function *PrintF = GetPrintHelperForArg(ArgTy, Args[I]->getBuiltinLeafTypeHint());
    if (!PrintF)
      return LogError<Value *>("Unsupported print argument type");

    Value *CastArg = CastValueTo(ArgV, PrintF->getFunctionType()->getParamType(0));
    if (!CastArg)
      return nullptr;
    Builder->CreateCall(PrintF, {CastArg});

    if (I + 1 < Args.size()) {
      Value *Space = ConstantFP::get(*TheContext, APFloat(32.0));
      Builder->CreateCall(PrintCharF, {Space});
    }
  }

  Value *NewLine = ConstantFP::get(*TheContext, APFloat(10.0));
  Builder->CreateCall(PrintCharF, {NewLine});
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *IfStmtAST::codegen() {
  emitLocation(this);

  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to zero.
  CondV = ToBoolI1(CondV, "ifcond");
  if (!CondV)
    return nullptr;

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

Value *BreakStmtAST::codegen() {
  if (LoopContextStack.empty())
    return LogError<Value *>("`break` used outside of a loop");
  emitLocation(this);
  Builder->CreateBr(LoopContextStack.back().BreakTarget);
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *ContinueStmtAST::codegen() {
  if (LoopContextStack.empty())
    return LogError<Value *>("`continue` used outside of a loop");
  emitLocation(this);
  Builder->CreateBr(LoopContextStack.back().ContinueTarget);
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *WhileStmtAST::codegen() {
  emitLocation(this);
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  BasicBlock *CondBB = BasicBlock::Create(*TheContext, "while.cond", TheFunction);
  BasicBlock *BodyBB = BasicBlock::Create(*TheContext, "while.body");
  BasicBlock *ExitBB = BasicBlock::Create(*TheContext, "while.exit");

  Builder->CreateBr(CondBB);
  Builder->SetInsertPoint(CondBB);
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;
  CondV = ToBoolI1(CondV, "whilecond");
  if (!CondV)
    return nullptr;
  Builder->CreateCondBr(CondV, BodyBB, ExitBB);

  TheFunction->insert(TheFunction->end(), BodyBB);
  Builder->SetInsertPoint(BodyBB);
  LoopContextGuard Guard(ExitBB, CondBB);
  if (!Body->codegen())
    return nullptr;

  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(CondBB);

  TheFunction->insert(TheFunction->end(), ExitBB);
  Builder->SetInsertPoint(ExitBB);
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *DoWhileStmtAST::codegen() {
  emitLocation(this);
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  BasicBlock *BodyBB = BasicBlock::Create(*TheContext, "do.body", TheFunction);
  BasicBlock *CondBB = BasicBlock::Create(*TheContext, "do.cond");
  BasicBlock *ExitBB = BasicBlock::Create(*TheContext, "do.exit");

  Builder->CreateBr(BodyBB);

  Builder->SetInsertPoint(BodyBB);
  LoopContextGuard Guard(ExitBB, CondBB);
  if (!Body->codegen())
    return nullptr;
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(CondBB);

  TheFunction->insert(TheFunction->end(), CondBB);
  Builder->SetInsertPoint(CondBB);
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;
  CondV = ToBoolI1(CondV, "docond");
  if (!CondV)
    return nullptr;
  Builder->CreateCondBr(CondV, BodyBB, ExitBB);

  TheFunction->insert(TheFunction->end(), ExitBB);
  Builder->SetInsertPoint(ExitBB);
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *ForStmtAST::codegen() {
  emitLocation(this);
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Emit the start code first, without 'variable' in scope.
  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;
  Type *LoopTy = StartVal->getType();

  // Create an alloca for the variable in the entry block.
  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName, LoopTy);

  // Store the value into the alloca.
  StartVal = CastValueTo(StartVal, LoopTy);
  Builder->CreateStore(StartVal, Alloca);

  // If the loop variable shadows an existing variable, we have to restore it,
  // so save it now. Set VarName to refer to our recently created alloca.
  VarBinding OldVal = NamedValues[VarName];
  NamedValues[VarName] = {Alloca, LoopTy, nullptr};

  // Make new basic blocks for loop condition, loop body and end-loop code.
  BasicBlock *LoopConditionBB =
      BasicBlock::Create(*TheContext, "loopcond", TheFunction);
  BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop");
  BasicBlock *StepBB = BasicBlock::Create(*TheContext, "loopstep");
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
  if (CurVar->getType()->isFloatingPointTy() || EndCond->getType()->isFloatingPointTy()) {
    CurVar = CastValueTo(CurVar, Type::getDoubleTy(*TheContext));
    EndCond = CastValueTo(EndCond, Type::getDoubleTy(*TheContext));
    EndCond = Builder->CreateFCmpULT(CurVar, EndCond, "loopcond");
  } else {
    unsigned W = std::max(CurVar->getType()->getIntegerBitWidth(),
                          EndCond->getType()->getIntegerBitWidth());
    Type *IntTy = IntegerType::get(*TheContext, W);
    CurVar = CastValueTo(CurVar, IntTy);
    EndCond = CastValueTo(EndCond, IntTy);
    EndCond = Builder->CreateICmpSLT(CurVar, EndCond, "loopcond");
  }

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
  LoopContextGuard Guard(EndLoopBB, StepBB);
  if (!Body->codegen()) {
    return nullptr;
  }

  // Fallthrough from body to step if body didn't already terminate.
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(StepBB);

  // Emit the step value in a dedicated block so `continue` can branch here.
  TheFunction->insert(TheFunction->end(), StepBB);
  Builder->SetInsertPoint(StepBB);
  Value *StepVal = nullptr;
  if (Step) {
    StepVal = Step->codegen();
    if (!StepVal)
      return nullptr;
  } else {
    // If not specified, use 1.
    if (LoopTy->isFloatingPointTy())
      StepVal = ConstantFP::get(LoopTy, 1.0);
    else
      StepVal = ConstantInt::get(LoopTy, 1);
  }
  StepVal = CastValueTo(StepVal, LoopTy);
  Value *CurVarStep =
      Builder->CreateLoad(Alloca->getAllocatedType(), Alloca, VarName);
  CurVarStep = CastValueTo(CurVarStep, LoopTy);
  Value *NextVar = LoopTy->isFloatingPointTy()
                       ? Builder->CreateFAdd(CurVarStep, StepVal, "nextvar")
                       : Builder->CreateAdd(CurVarStep, StepVal, "nextvar");
  Builder->CreateStore(NextVar, Alloca);
  Builder->CreateBr(LoopConditionBB);

  // Append EndLoopBB after the loop body. We go to this basic block if the
  // loop condition says we should not loop anymore.
  TheFunction->insert(TheFunction->end(), EndLoopBB);

  // Any new code will be inserted after the loop.
  Builder->SetInsertPoint(EndLoopBB);

  // Restore the unshadowed variable.
  if (OldVal.Alloca)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for expr always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *VarExprAST::codegen() {
  std::vector<VarBinding> OldBindings;

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

    AllocaInst *Alloca =
        CreateEntryBlockAlloca(TheFunction, VarName, InitVal->getType());
    Builder->CreateStore(InitVal, Alloca);

    // Remember the old variable binding so that we can restore the binding when
    // we unrecurse.
    OldBindings.push_back(NamedValues[VarName]);

    // Remember this binding.
    NamedValues[VarName] = {Alloca, InitVal->getType(), nullptr};
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
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  Type *ExpectedTy = TheFunction->getReturnType();
  if (!Expr) {
    if (ExpectedTy->isVoidTy()) {
      Builder->CreateRetVoid();
      return ConstantFP::get(*TheContext, APFloat(0.0));
    }
    return LogError<Value *>("Missing return value for non-void function");
  }

  Value *RetVal = Expr->codegen();
  if (!RetVal)
    return nullptr;
  if (ExpectedTy->isVoidTy())
    return LogError<Value *>("Void function cannot return a value");
  RetVal = CastValueTo(RetVal, ExpectedTy);
  if (!RetVal)
    return nullptr;
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
                "Warning (Line %d): unreachable code after terminator statement\n",
                Stmts[i + 1]->getLine());
      }
      break;
    }
  }
  return Last;
}

Function *PrototypeAST::codegen() {
  Type *RetTy = ResolveTypeExpr(this->RetType);
  if (!RetTy)
    return nullptr;
  if (UseCMainSignature && Name == "main")
    RetTy = Type::getInt32Ty(*TheContext);

  std::vector<Type *> ParamTypes;
  for (const auto &ArgTy : ArgTypes) {
    Type *Ty = ResolveTypeExpr(ArgTy);
    if (!Ty || Ty->isVoidTy()) {
      LogError("Function parameter type cannot be void");
      return nullptr;
    }
    ParamTypes.push_back(Ty);
  }
  FunctionType *FT = FunctionType::get(RetTy, ParamTypes, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Preserve C ABI sign/zero-extension intent for narrow integers.
  Attribute::AttrKind RetExt = GetExtAttrForTypeExpr(this->RetType);
  if (RetExt != Attribute::None)
    F->addRetAttr(RetExt);
  for (unsigned I = 0; I < ArgTypes.size(); ++I) {
    Attribute::AttrKind A = GetExtAttrForTypeExpr(ArgTypes[I]);
    if (A != Attribute::None)
      F->addParamAttr(I, A);
  }

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
  LoopContextStack.clear();
  unsigned ArgIdx = 0;
  unsigned ArgTyIdx = 0;

  for (auto &Arg : TheFunction->args()) {
    // Create an alloca for this variable.
    AllocaInst *Alloca =
        CreateEntryBlockAlloca(TheFunction, Arg.getName(), Arg.getType());

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
    Type *PointeeTy = nullptr;
    if (ArgTyIdx < P.getArgTypes().size())
      PointeeTy = ResolvePointeeTypeExpr(P.getArgTypes()[ArgTyIdx]);
    NamedValues[std::string(Arg.getName())] = {Alloca, Arg.getType(), PointeeTy};
    ++ArgTyIdx;
  }

  if (Value *RetVal = Body->codegen()) {
    // Finish off the function if the current block is still open.
    if (!Builder->GetInsertBlock()->getTerminator()) {
      Type *FnRetTy = TheFunction->getReturnType();
      if (FnRetTy->isVoidTy()) {
        Builder->CreateRetVoid();
      } else {
        RetVal = CastValueTo(RetVal, FnRetTy);
        if (!RetVal)
          return nullptr;
        Builder->CreateRet(RetVal);
      }
    }

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    // Run the optimizer on the function (JIT/REPL path).
    if (EnableFunctionOptimizations && TheFPM && TheFAM)
      TheFPM->run(*TheFunction, *TheFAM);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();
  FunctionProtos.erase(P.getName());

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

  // Add function transform passes based on -O level (used by JIT paths).
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

  // Register analysis passes
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

static void InitializeModuleAndManagers() {
  InitializeContext();
  TheModule->setDataLayout(TheJIT->getDataLayout());
  EnsureDefaultTypeAliases();
  InitializeOptimizationPasses();
}

static void OptimizeModuleForCodeGen(Module &M, TargetMachine *TM) {
  OptimizationLevel Level;
  if (!TryGetOptimizationLevel(Level))
    return;

  if (Level == OptimizationLevel::O0)
    return;

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PassBuilder PB(TM);

  PB.registerModuleAnalyses(MAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(Level);
  MPM.run(M, MAM);
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

static void HandleTypeAlias() {
  if (ParseTypeAliasDecl()) {
    if (CurTok == tok_eol)
      getNextToken();
  } else {
    getNextToken();
  }
}

static void HandleStructDecl() {
  if (ParseStructDecl()) {
    if (CurTok == tok_eol)
      getNextToken();
  } else {
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
    case '@':
      LogError("Decorators/custom operators are disabled in Chapter 16");
      SkipToNextLine();
      break;
    default:
      fprintf(stderr, "ready> ");
      switch (CurTok) {
      case tok_def:
        HandleDefinition();
        break;
      case tok_extern:
        HandleExtern();
        break;
      case tok_type:
        HandleTypeAlias();
        break;
      case tok_struct:
        HandleStructDecl();
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

extern "C" DLLEXPORT int8_t putchari8(int8_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT int16_t putchari16(int16_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT int32_t putchari32(int32_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT int64_t putchari64(int64_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT uint8_t putcharu8(uint8_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT uint16_t putcharu16(uint16_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT uint32_t putcharu32(uint32_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT uint64_t putcharu64(uint64_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT float putcharf32(float X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT double putcharf64(double X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT int64_t putchari(int64_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT double putchard(double X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
extern "C" DLLEXPORT double printchard(double X) {
  fputc((unsigned char)X, stderr);
  return 0;
}

extern "C" DLLEXPORT int8_t printi8(int8_t X) {
  fprintf(stderr, "%d", (int)X);
  return 0;
}
extern "C" DLLEXPORT int16_t printi16(int16_t X) {
  fprintf(stderr, "%d", (int)X);
  return 0;
}
extern "C" DLLEXPORT int32_t printi32(int32_t X) {
  fprintf(stderr, "%d", X);
  return 0;
}
extern "C" DLLEXPORT int64_t printi64(int64_t X) {
  fprintf(stderr, "%lld", (long long)X);
  return 0;
}
extern "C" DLLEXPORT uint8_t printu8(uint8_t X) {
  fprintf(stderr, "%u", (unsigned)X);
  return 0;
}
extern "C" DLLEXPORT uint16_t printu16(uint16_t X) {
  fprintf(stderr, "%u", (unsigned)X);
  return 0;
}
extern "C" DLLEXPORT uint32_t printu32(uint32_t X) {
  fprintf(stderr, "%u", X);
  return 0;
}
extern "C" DLLEXPORT uint64_t printu64(uint64_t X) {
  fprintf(stderr, "%llu", (unsigned long long)X);
  return 0;
}
extern "C" DLLEXPORT float printfloat32(float X) {
  fprintf(stderr, "%f", (double)X);
  return 0;
}
extern "C" DLLEXPORT double printfloat64(double X) {
  fprintf(stderr, "%f", X);
  return 0;
}
extern "C" DLLEXPORT int64_t printi(int64_t X) {
  fprintf(stderr, "%lld", (long long)X);
  return 0;
}
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f", X);
  return 0;
}

//===----------------------------------------------------------------------===//
// Parsing helpers
//===----------------------------------------------------------------------===//

static void ParseSourceFile() {
  HadError = false;
  // Parse all definitions from the file
  while (CurTok != tok_eof && CurTok != tok_error && !HadError) {
    switch (CurTok) {
    case tok_def:
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
    case tok_type:
      if (ParseTypeAliasDecl()) {
        if (CurTok == tok_eol)
          getNextToken();
      } else {
        getNextToken(); // Skip for error recovery
      }
      break;
    case tok_struct:
      if (ParseStructDecl()) {
        if (CurTok == tok_eol)
          getNextToken();
      } else {
        getNextToken(); // Skip for error recovery
      }
      break;
    case tok_eol:
      getNextToken(); // Skip newlines
      break;
    case '@':
      LogError("Decorators/custom operators are disabled in Chapter 16");
      SkipToNextLine();
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
  HadError = false;
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

  while (CurTok != tok_eof && CurTok != tok_error && !HadError) {
    switch (CurTok) {
    case tok_def:
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
    case tok_type:
      if (ParseTypeAliasDecl()) {
        if (CurTok == tok_eol)
          getNextToken();
      } else {
        getNextToken();
      }
      break;
    case tok_struct:
      if (ParseStructDecl()) {
        if (CurTok == tok_eol)
          getNextToken();
      } else {
        getNextToken();
      }
      break;
    case tok_eol:
      getNextToken();
      break;
    case '@':
      LogError("Decorators/custom operators are disabled in Chapter 16");
      SkipToNextLine();
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

void CompileToObjectFile(const std::string &filename,
                         const std::string &explicitOutput = "") {
  UseCMainSignature = true;
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Initialize LLVM context and optimization passes
  InitializeContext();
  InitializeOptimizationPasses();
  // Object/executable mode uses a whole-module pipeline later.
  ScopedFunctionOptimization DisableFunctionPasses(false);

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
  OptimizeModuleForCodeGen(*TheModule, TargetMachine);

  // Determine output filename
  std::string outputFilename =
      explicitOutput.empty() ? getOutputFilename(filename, ".o") : explicitOutput;

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

  OptimizationLevel ParsedOptLevel;
  if (!TryGetOptimizationLevel(ParsedOptLevel)) {
    errs() << "Error: invalid optimization level '" << OptLevel
           << "'. Use -O0, -O1, -O2, or -O3\n";
    return 1;
  }

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
      std::string scriptObj = exeFile + ".tmp.o";
      CompileToObjectFile(InputFilename, scriptObj);

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
      CompileToObjectFile(InputFilename, output);
      std::string scriptObj = output;
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

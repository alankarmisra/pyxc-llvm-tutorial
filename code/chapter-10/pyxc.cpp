#include "../include/PyxcJIT.h"
#include "llvm/ADT/APFloat.h"
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
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace llvm;
using namespace llvm::orc;

//===----------------------------------------===//
// Command line
//===----------------------------------------===//
static cl::OptionCategory PyxcCategory("Pyxc options");

// Optional positional input: 0 args => REPL, 1 arg => file mode.
static cl::opt<std::string> InputFile(cl::Positional, cl::desc("[script.pyxc]"),
                                      cl::init(""), cl::cat(PyxcCategory));

// Verbose IR dump in both REPL and file mode.
static cl::opt<bool> VerboseIR("v",
                               cl::desc("Print generated LLVM IR to stderr"),
                               cl::init(false), cl::cat(PyxcCategory));

// Optimization level. For now Pyxc only distinguishes -O0 (no passes) from
// any non-zero level (run the current fixed function pass pipeline).
static cl::opt<unsigned> OptLevel("O", cl::desc("Optimization level"),
                                  cl::value_desc("0|1|2|3"), cl::Prefix,
                                  cl::init(2), cl::cat(PyxcCategory));

static FILE *Input = stdin;
static bool IsRepl = true;

//===----------------------------------------===//
// Lexer
//===----------------------------------------===//

// I return named tokens for known language elements. I preserve the [0-255]
// character value of any other single character for diagnostics.
enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  // commands
  tok_def = -4,
  tok_extern = -5,

  // primary
  tok_name = -6,
  tok_number = -7,

  // comparison operators
  // Only multi-character operators use explicit tokens;
  // I give single-character operators named tokens whose values match their
  // corresponding characters.
  tok_eq = -8,   // ==
  tok_neq = -9,  // !=
  tok_leq = -10, // <=
  tok_geq = -11, // >=

  // control
  tok_if = -12,
  tok_else = -13,

  // loops
  tok_for = -15,

  // punctuation and operators
  tok_lparen = '(',
  tok_rparen = ')',
  tok_comma = ',',
  tok_colon = ':',
  tok_plus = '+',
  tok_minus = '-',
  tok_star = '*',
  tok_slash = '/',
  tok_percent = '%',
  tok_less = '<',
  tok_greater = '>',
  tok_equal = '=',
};

static string Name; // Filled in if tok_name
static double NumberValue;        // Filled in if tok_number
static string NumberLiteral; // Filled in if tok_number, used in error messages

// Language keywords. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static map<string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"if", tok_if},
    {"else", tok_else}, {"for", tok_for}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  static map<int, string> Names = {
      {tok_eof, "end of input"}, {tok_eol, "newline"},
      {tok_error, "error"},      {tok_def, "'def'"},
      {tok_extern, "'extern'"},  {tok_name, "name"},
      {tok_number, "number"},
      {tok_eq, "'=='"},          {tok_neq, "'!='"},
      {tok_leq, "'<='"},         {tok_geq, "'>='"},
      {tok_if, "'if'"},          {tok_else, "'else'"},
      {tok_for, "'for'"}};

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

/// SourceLocation - A {Line, Col} pair. Line and Col are 1-based.
///
/// Two globals track position as characters are consumed:
///   LexLoc  - where the character-read head (advance()) currently is.
///             Updated on every advance() call. After a '\n', Line increments
///             and Col resets to 0 so the next character will be Col 1.
///   CurLoc  - snapshotted at the start of each token in getToken(), before
///             consuming any of the token's characters. This is the position
///             the parser and diagnostics see.
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

/// SourceManager - Buffers every source line as it is read so that error
/// messages can reprint the offending line with a caret underneath it.
///
/// advance() calls onChar() for every character it consumes. When a '\n'
/// arrives, the just-completed line is moved into CompletedLines and
/// CurrentLine starts fresh. getLine(N) returns a pointer to the Nth line
/// (1-based): completed lines are stored in the vector; the line currently
/// being assembled is in CurrentLine.
class SourceManager {
  vector<string> CompletedLines;
  string CurrentLine;

public:
  /// reset - Clear all buffered source lines.
  ///
  /// Used when starting a new input stream so diagnostics only reference the
  /// current script/session content.
  void reset() {
    CompletedLines.clear();
    CurrentLine.clear();
  }

  /// onChar - Feed one consumed character into the source buffer.
  ///
  /// Preconditions:
  /// - Must be called for every character consumed by advance().
  /// - '\n' terminates the current line; EOF is ignored.
  void onChar(int C) {
    if (C == '\n') {
      CompletedLines.push_back(CurrentLine);
      CurrentLine.clear();
      return;
    }
    if (C != EOF)
      CurrentLine.push_back(static_cast<char>(C));
  }

  /// getLine - Return a pointer to a buffered source line by 1-based index.
  ///
  /// Completed lines come from CompletedLines; the in-progress line is
  /// CurrentLine when OneBasedLine == CompletedLines.size() + 1.
  ///
  /// Preconditions:
  /// - OneBasedLine is 1-based. Non-positive indices return nullptr.
  ///
  /// Note:
  /// - Do not retain the returned pointer across advance()/onChar() calls;
  ///   buffers may reallocate.
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
static void LogInvalidNumberLiteralAtLoc(const string &Literal, SourceLocation Loc);

/// advance - I return the next character, normalizing `\r\n` (Windows)
/// and bare `\r` (Old Macs) into `\n`.
///
/// This is the single point through which all character consumption flows.
/// Every token branch in getToken() calls advance() rather than fgetc()
/// directly, so LexLoc and the source buffer are always in sync.
static int advance() {
  int LastChar = fgetc(Input);

  // case: '\r' or '\r\n'
  if (LastChar == '\r') {
    int NextChar = fgetc(Input);

    // A following '\n' is part of the same line ending; eat it.
    // Anything else belongs to the next token; put it back.
    // (EOF can't be put back at all, so it's excluded from that check.)
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

  // case '\n' or any other non-newline character
  return LastChar;
}

/// peek - Return the next character from the input stream without consuming it.
///
/// Used by the two-character operator branches in getToken() to decide whether
/// '=' should become '==' (tok_eq), '!' should become '!=' (tok_neq), etc.,
/// without advancing LexLoc or notifying SourceManager.
static int peek() {
  int c = fgetc(Input);
  if (c != EOF)
    ungetc(c, Input);
  return c;
}

/// getToken - Return the next token from standard input.
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
static int getToken() {
  static int LastChar = ' ';

  // Skip horizontal whitespace. Stop at '\n' — that becomes tok_eol.
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // Snapshot position for the upcoming token. See note above about tok_eol.
  CurLoc = LexLoc;

  if (LastChar == '\n') {
    // Do not call advance() here.
    // We should return the newline token immediately. If we read one more
    // character first, REPL mode may wait for extra input before processing
    // the line the user just submitted.
    LastChar = ' ';
    return tok_eol;
  }

  if (isalpha(LastChar) || LastChar == '_') {
    Name = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
      Name += LastChar;

    auto It = Keywords.find(Name);
    return (It == Keywords.end()) ? tok_name : It->second;
  }

  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    NumberLiteral = NumStr;
    char *End = nullptr;
    NumberValue = strtod(NumStr.c_str(), &End);
    if (End == NumStr.c_str() /* no conversion */
        || *End != '\0' /* trailing unparsed characters */) {
      LogInvalidNumberLiteralAtLoc(NumStr, CurLoc);
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
      // line (because it subtracts 1) when the next token triggers an error.
      CurLoc = LexLoc;
      LastChar = ' ';
      return tok_eol;
    }
  }

  // peek(), if the next one completes a recognized token, eat it, and return
  // token; otherwise, I return the named single-character token.
  if (LastChar == '=') {
    int Tok = (peek() == '=') ? (advance(), tok_eq) : tok_equal;
    LastChar = advance();
    return Tok;
  }

  if (LastChar == '!') {
    int Tok = (peek() == '=') ? (advance(), tok_neq) : '!';
    LastChar = advance();
    return Tok;
  }

  if (LastChar == '<') {
    int Tok = (peek() == '=') ? (advance(), tok_leq) : tok_less;
    LastChar = advance();
    return Tok;
  }

  if (LastChar == '>') {
    int Tok = (peek() == '=') ? (advance(), tok_geq) : tok_greater;
    LastChar = advance();
    return Tok;
  }

  if (LastChar == EOF)
    return tok_eof;

  // I read a single-character token.
  int ThisChar = LastChar;

  // Position the lexer at the next character so the next getToken() starts there.
  LastChar = advance();

  // I return a named token for known punctuation and operators.
  switch (ThisChar) {
  case '(':
    return tok_lparen;
  case ')':
    return tok_rparen;
  case ',':
    return tok_comma;
  case ':':
    return tok_colon;
  case '+':
    return tok_plus;
  case '-':
    return tok_minus;
  case '*':
    return tok_star;
  case '/':
    return tok_slash;
  case '%':
    return tok_percent;
  case '<':
    return tok_less;
  case '>':
    return tok_greater;
  case '=':
    return tok_equal;
  default:
    return ThisChar;
  }
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
  if (Tok != tok_eol || Loc.Line <= 1)
    return Loc;

  // Tok == tok_eol && Loc.Line > 1
  int PrevLine = Loc.Line - 1;
  const string *PrevLineText = PyxcSourceMgr.getLine(PrevLine);

  // guard
  if (!PrevLineText)
    return Loc;

  // return a pointer just past the end of the previous line.
  return {PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}

/// FormatTokenForMessage - Return a human-readable description of Tok for use
/// in error messages. Name and number tokens include their actual text
/// (e.g. "name 'foo'", "number '3.14'") since the name alone is not
/// enough to diagnose the problem. Everything else uses the static TokenNames
/// entry.
static string FormatTokenForMessage(int Tok) {
  if (Tok == tok_name)
    return "name '" + Name + "'";
  if (Tok == tok_number)
    return "number '" + NumberLiteral + "'";

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
  int spaces = max(0, Loc.Col - 1);
  fprintf(stderr, "%*s", spaces, " ");
  fprintf(stderr, "^~~~\n");
}

static void LogInvalidNumberLiteralAtLoc(const string &Literal, SourceLocation Loc) {
  fprintf(stderr, "Error (Line %d, Column %d): invalid number literal '%s'\n",
          Loc.Line, Loc.Col, Literal.c_str());
  PrintErrorSourceContext(Loc);
}

//===----------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------===//
namespace {

/// ExpressionNode - Base class for all expression nodes.
class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;
  virtual Value *codegen() = 0;
};

/// NumberExpressionNode - Expression class for numeric literals like "1.0".
class NumberExpressionNode : public ExpressionNode {
  double Value;

public:
  NumberExpressionNode(double Value) : Value(Value) {}
  llvm::Value *codegen() override;
};

/// NameExpressionNode - Expression class for referencing a variable, like "a".
class NameExpressionNode : public ExpressionNode {
  string Name;

public:
  NameExpressionNode(const string &Name) : Name(Name) {}
  Value *codegen() override;
};

/// BinaryExpressionNode - Expression class for a binary operator.
/// Operator is an int (not char) to accommodate both single-character ASCII operators
/// like '+' and named multi-character token enums like tok_eq (==).
class BinaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Left, Right;

public:
  BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left, unique_ptr<ExpressionNode> Right)
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {}
  Value *codegen() override;
};

/// UnaryExpressionNode - Expression class for applying a unary operator.
class UnaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Operand;

public:
  UnaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Operand)
      : Operator(Operator), Operand(std::move(Operand)) {}
  Value *codegen() override;
};

/// CallExpressionNode - Expression class for function calls.
class CallExpressionNode : public ExpressionNode {
  string Callee;
  vector<unique_ptr<ExpressionNode>> Arguments;

public:
  CallExpressionNode(const string &Callee, vector<unique_ptr<ExpressionNode>> Arguments)
      : Callee(Callee), Arguments(std::move(Arguments)) {}
  Value *codegen() override;
};

/// ForExpressionNode - Expression class for for loops.
///   for <var> = <start>, <cond>, <step>: <body>
/// The loop variable is in scope for <cond>, <step>, and <body> (through
/// NamedValues). The expression always produces 0.0 — the loop is used for side
/// effects.
class ForExpressionNode : public ExpressionNode {
  string VarName;
  unique_ptr<ExpressionNode> Start, Cond, Step, Body;

public:
  ForExpressionNode(const string &VarName, unique_ptr<ExpressionNode> Start,
             unique_ptr<ExpressionNode> Cond, unique_ptr<ExpressionNode> Step,
             unique_ptr<ExpressionNode> Body)
      : VarName(VarName), Start(std::move(Start)), Cond(std::move(Cond)),
        Step(std::move(Step)), Body(std::move(Body)) {}
  Value *codegen() override;
};

/// IfExpressionNode - Expression class for if/else.
class IfExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Cond, Then, Else;

public:
  IfExpressionNode(unique_ptr<ExpressionNode> Cond, unique_ptr<ExpressionNode> Then,
            unique_ptr<ExpressionNode> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
  Value *codegen() override;
};

/// FunctionSignatureNode - This class represents the "function signature" for a function,
/// which captures its name, and its parameter names (thus implicitly the number
/// of parameters the function takes).
class FunctionSignatureNode {
  string Name;
  vector<string> Parameters;

public:
  FunctionSignatureNode(const string &Name, vector<string> Parameters)
      : Name(Name), Parameters(std::move(Parameters)) {}

  const string &getName() const { return Name; }
  size_t getNumParameters() const { return Parameters.size(); }
  Function *codegen();
};

/// FunctionDefinitionNode - This class represents a function definition itself.
class FunctionDefinitionNode {
  unique_ptr<FunctionSignatureNode> Signature;
  unique_ptr<ExpressionNode> Body;

public:
  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature, unique_ptr<ExpressionNode> Body)
      : Signature(std::move(Signature)), Body(std::move(Body)) {}
  Function *codegen();
};

} // end anonymous namespace

//===----------------------------------------===//
// Parser
//===----------------------------------------===//

/// CurrentToken is the current token the parser is looking at.
/// getNextToken reads the next token from the lexer and stores it in CurrentToken.
/// Every parse function assumes CurrentToken is already loaded before it is called,
/// and leaves CurrentToken pointing at the first token it did not consume.
static int CurrentToken;
static int getNextToken() { return CurrentToken = getToken(); }

/// consumeNewlines - Consume all consecutive tok_eol tokens.
///
/// Called after eating a structural token (e.g. ':') to allow the body or
/// next clause to appear on the following line.
static void consumeNewlines() {
  while (CurrentToken == tok_eol)
    getNextToken();
}

/// PrintReplPrompt - Print the interactive prompt to stderr.
/// Only emits output in REPL mode; silent when running a script file.
void PrintReplPrompt() {
  if (IsRepl)
    fprintf(stderr, "ready> ");
}

/// Log - Write a diagnostic message to stderr in REPL mode only.
/// Used by the Handle* functions to confirm what was parsed ("Parsed a
/// function definition.", etc.). Silent when processing a script file so
/// that stdout/stderr output from the program itself is not cluttered.
void Log(const string &message) {
  if (IsRepl)
    fprintf(stderr, "%s", message.c_str());
}

/// LogErrorExpression* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogErrorExpression("message");
unique_ptr<ExpressionNode> LogErrorExpression(const char *Str) {
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurrentToken);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Col,
          Str);
  PrintErrorSourceContext(Anchor);
  return nullptr;
}

unique_ptr<FunctionSignatureNode> LogErrorSignature(const char *Str) {
  LogErrorExpression(Str);
  return nullptr;
}

unique_ptr<FunctionDefinitionNode> LogErrorFunction(const char *Str) {
  LogErrorExpression(Str);
  return nullptr;
}

static unique_ptr<ExpressionNode> ParseExpression();
static unique_ptr<ExpressionNode> ParsePrimary();

/// number-expression
///   = number ;
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  auto Result = make_unique<NumberExpressionNode>(NumberValue);
  getNextToken(); // consume the number
  return Result;
}

/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// name-expression
///   = name
///   | call-expression ;
/// call-expression
///   = name "(" [ arguments ] ")" ;
/// arguments
///   = expression { "," expression } ;
static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;

  getNextToken(); // eat name.

  if (CurrentToken != tok_lparen) // Simple variable ref.
    return make_unique<NameExpressionNode>(ParsedName);

  // Call.
  getNextToken(); // eat (
  vector<unique_ptr<ExpressionNode>> Arguments;
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (auto Arg = ParseExpression())
        Arguments.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurrentToken == tok_rparen)
        break;

      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments));
}

/// for-expression
///   = "for" name "=" expression "," expression "," expression
///     ":" [ end-of-lines ] expression ;
///
/// The loop variable is introduced by the "for" and is in scope for the
/// condition, step, and body. It shadows any outer variable of the same name.
static unique_ptr<ExpressionNode> ParseForExpression() {
  getNextToken(); // eat 'for'

  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected name after 'for'");
  string VarName = Name;
  getNextToken(); // eat name

  if (CurrentToken != tok_equal)
    return LogErrorExpression("Expected '=' after for variable");
  getNextToken(); // eat '='

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;

  if (CurrentToken != tok_comma)
    return LogErrorExpression("Expected ',' after for start value");
  getNextToken(); // eat ','

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurrentToken != tok_comma)
    return LogErrorExpression("Expected ',' after for condition");
  getNextToken(); // eat ','

  auto Step = ParseExpression();
  if (!Step)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after for step");
  getNextToken(); // eat ':'

  // Allow body on next line.
  consumeNewlines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return make_unique<ForExpressionNode>(VarName, std::move(Start), std::move(Cond),
                                 std::move(Step), std::move(Body));
}

/// if-expression
///   = "if" expression ":" [ end-of-lines ] expression
///     [ end-of-lines ] "else" ":" [ end-of-lines ] expression ;
static unique_ptr<ExpressionNode> ParseIfExpression() {
  getNextToken(); // eat 'if'

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after if condition");
  getNextToken(); // eat ':'

  // Allow body on next line
  consumeNewlines();

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  // Allow 'else' on next line
  consumeNewlines();

  if (CurrentToken != tok_else)
    return LogErrorExpression("Expected 'else' in if expression");
  getNextToken(); // eat 'else'

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after else");
  getNextToken(); // eat ':'

  // Allow body on next line
  consumeNewlines();

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return make_unique<IfExpressionNode>(std::move(Cond), std::move(Then),
                                std::move(Else));
}

/// primary
///   = name-expression
///   | number-expression
///   | parenthesized-expression
///   | if-expression
///   | for-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  default:
    return LogErrorExpression("unknown token when expecting an expression");
  case tok_number:
    return ParseNumberExpression();
  case tok_name:
    return ParseNameExpression();
  case tok_lparen:
    return ParseParenthesizedExpression();
  case tok_if:
    return ParseIfExpression();
  case tok_for:
    return ParseForExpression();
  }
}

static unique_ptr<ExpressionNode> ParseFactor();

/// I parse the unary-minus branch of factor.
static unique_ptr<ExpressionNode> ParseUnaryMinus() {
  getNextToken(); // I eat '-'.
  auto Operand = ParseFactor();
  if (!Operand)
    return nullptr;
  return make_unique<UnaryExpressionNode>(tok_minus, std::move(Operand));
}

/// factor
///   = "-" factor
///   | primary ;
static unique_ptr<ExpressionNode> ParseFactor() {
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();
  return ParsePrimary();
}

/// term
///   = factor { ("*" | "/" | "%") factor } ;
static unique_ptr<ExpressionNode> ParseTerm() {
  auto Left = ParseFactor();
  if (!Left)
    return nullptr;
  while (CurrentToken == tok_star || CurrentToken == tok_slash ||
         CurrentToken == tok_percent) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseFactor();
    if (!Right)
      return nullptr;
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }
  return Left;
}

/// sum
///   = term { ("+" | "-") term } ;
static unique_ptr<ExpressionNode> ParseSum() {
  auto Left = ParseTerm();
  if (!Left)
    return nullptr;
  while (CurrentToken == tok_plus || CurrentToken == tok_minus) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseTerm();
    if (!Right)
      return nullptr;
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }
  return Left;
}

/// comparison
///   = sum { comparison-operator sum } ;
/// comparison-operator
///   = "==" | "!=" | "<=" | ">=" | "<" | ">" ;
static unique_ptr<ExpressionNode> ParseComparison() {
  auto Left = ParseSum();
  if (!Left)
    return nullptr;
  while (CurrentToken == tok_eq || CurrentToken == tok_neq ||
         CurrentToken == tok_leq || CurrentToken == tok_geq ||
         CurrentToken == tok_less || CurrentToken == tok_greater) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseSum();
    if (!Right)
      return nullptr;
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }
  return Left;
}

/// expression
///   = comparison ;
static unique_ptr<ExpressionNode> ParseExpression() {
  return ParseComparison();
}

/// function-signature
///   = name "(" [ parameters ] ")" ;
/// parameters
///   = parameter { "," parameter } ;
/// parameter
///   = name ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");

  string FnName = Name;
  if ((FnName.size() == 7 && FnName.rfind("binary", 0) == 0 &&
       isascii(static_cast<unsigned char>(FnName[6])) &&
       ispunct(static_cast<unsigned char>(FnName[6]))) ||
      (FnName.size() == 6 && FnName.rfind("unary", 0) == 0 &&
       isascii(static_cast<unsigned char>(FnName[5])) &&
       ispunct(static_cast<unsigned char>(FnName[5])))) {
    fprintf(stderr,
            "Warning: Function name '%s' may conflict with "
            "operator-reserved naming\n",
            FnName.c_str());
  }
  getNextToken(); // eat function name

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  // Parse parameter names. The loop calls getNextToken() at the top to advance
  // past '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body we call getNextToken() again to move past the name
  // we just stored, then check whether ')' or ',' follows.
  vector<string> ParameterNames;
  while (getNextToken() == tok_name) {
    ParameterNames.push_back(Name);

    if (getNextToken() == tok_rparen) // eat name, check what follows
      break;

    if (CurrentToken != tok_comma)
      return LogErrorSignature("Expected ')' or ',' in parameter list");
    // loop continues: getNextToken() at the top eats the ','
  }

  if (CurrentToken != tok_rparen)
    return LogErrorSignature("Expected ')' in function signature");

  getNextToken(); // eat ')'

  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames));
}

/// function-definition
///   = "def" function signature ":" [ end-of-lines ] expression ;
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // eat 'def'
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorFunction("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // Allow the body expression to start on the next line:
  //   def foo(x):
  //     x + 1
  consumeNewlines();

  if (auto E = ParseExpression())
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(E));
  return nullptr;
}

/// top-level-expression
///   = expression
/// A top-level expression (e.g. "1 + 2") is wrapped in an anonymous function
/// so it fits the same FunctionDefinitionNode shape as everything else.
/// HandleTopLevelExpression compiles it into the JIT, calls it to get the
/// numeric result, then removes it from the JIT via a ResourceTracker.
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  if (auto E = ParseExpression()) {
    auto Signature = make_unique<FunctionSignatureNode>("__anon_expr", vector<string>());
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(E));
  }
  return nullptr;
}

/// external
///   = "extern" "def" function signature
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurrentToken != tok_def)
    return LogErrorSignature("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParseFunctionSignature();
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
//
// FunctionSignatures - Persistent function signature registry. Because each function
// lands in its own module, a later module that calls 'foo' cannot find 'foo'
// in TheModule->getFunction(). FunctionSignatures stores the FunctionSignatureNode for
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
static std::map<std::string, std::unique_ptr<FunctionSignatureNode>> FunctionSignatures;
static ExitOnError ExitOnErr;

/// LogErrorV - Codegen-level error helper. Delegates to LogErrorExpression for printing,
/// then returns nullptr so codegen callers can write: return LogErrorV("msg");
Value *LogErrorV(const char *Str) {
  LogErrorExpression(Str);
  return nullptr;
}

/// getFunction - Resolve a function name to an LLVM Function* in the current
/// module, re-emitting a declaration from FunctionSignatures if necessary.
///
/// Because each top-level input gets its own Module, a function defined in an
/// earlier module is no longer in TheModule->getFunction(). When that happens
/// we look up its FunctionSignatureNode in FunctionSignatures and call codegen() on it,
/// which emits a fresh 'declare' with ExternalLinkage in the current module.
/// The JIT resolves that extern to the already-compiled body at link time.
Function *getFunction(const std::string &Name) {
  // Fast path: declaration or definition already in the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // Slow path: re-emit a declaration from the saved function signature.
  auto FI = FunctionSignatures.find(Name);
  if (FI != FunctionSignatures.end())
    return FI->second->codegen();

  return nullptr;
}

/// NumberExpressionNode::codegen - A numeric literal becomes a floating-point
/// constant value.
///
/// ConstantFP::get wraps an APFloat (LLVM's arbitrary-precision float) into a
/// constant node that can be used directly as an operand. No instruction is
/// emitted — constants are folded into whatever instruction uses them.
/// IRBuilder also recognises when both operands of a binary op are constants
/// and short-circuits to a single constant rather than emitting an instruction
/// at all (constant folding).
Value *NumberExpressionNode::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Value));
}

/// NameExpressionNode::codegen - A variable reference looks up the name in
/// NamedValues and returns the Value* for the corresponding function argument.
///
/// For now NamedValues only contains the current function's parameters; any
/// other name is an error. Mutable local variables (alloca/store/load) come
/// in a later chapter.
Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It == NamedValues.end() || !It->second)
    return LogErrorV("Unknown variable name");
  return It->second;
}

/// UnaryExpressionNode::codegen - Generate the operand, then negate it.
Value *UnaryExpressionNode::codegen() {
  Value *OperandValue = Operand->codegen();
  if (!OperandValue)
    return nullptr;

  if (Operator == tok_minus)
    return Builder->CreateFNeg(OperandValue, "negtmp");
  return LogErrorV("invalid unary operator");
}

/// BinaryExpressionNode::codegen - Recursively codegen both operands, then emit the
/// operator-specific instruction.
///
/// The string arguments to each Create* call ("addtmp", "multmp", etc.) are
/// hint names for the SSA value. LLVM uses them when printing IR, appending a
/// numeric suffix when the same hint would otherwise repeat. They have no
/// effect on correctness.
///
/// Comparison operators ('<', '>', tok_eq, tok_neq, tok_leq, tok_geq) each
/// require two steps: CreateFCmp* produces a 1-bit integer (i1) — LLVM's
/// boolean type. Since Pyxc treats everything as double, CreateUIToFP widens
/// it: false -> 0.0, true -> 1.0. This double boolean is then used as the
/// condition value in if/for expressions, where fcmp one != 0.0 converts it
/// back to i1.
/// We use ordered floating-point comparisons for ==, <, <=, >, and >=, so
/// comparisons involving NaN evaluate false. For != we use unordered
/// comparison, so x != NaN evaluates true.
Value *BinaryExpressionNode::codegen() {
  Value *L = Left->codegen();
  if (!L)
    return nullptr;

  Value *R = Right->codegen();
  if (!R)
    return nullptr;

  switch (Operator) {
  case tok_plus:
    return Builder->CreateFAdd(L, R, "addtmp");
  case tok_minus:
    return Builder->CreateFSub(L, R, "subtmp");
  case tok_star:
    return Builder->CreateFMul(L, R, "multmp");
  case tok_slash:
    return Builder->CreateFDiv(L, R, "divtmp");
  case tok_percent:
    return Builder->CreateFRem(L, R, "remtmp");
  case tok_less:
    L = Builder->CreateFCmpOLT(L, R, "cmptmp");
    // Widen the i1 boolean to double: false -> 0.0, true -> 1.0.
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_greater:
    L = Builder->CreateFCmpOGT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_eq:
    L = Builder->CreateFCmpOEQ(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_neq:
    // I use an unordered comparison so that 1 != NaN returns true.
    L = Builder->CreateFCmpUNE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_leq:
    L = Builder->CreateFCmpOLE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_geq:
    L = Builder->CreateFCmpOGE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}

/// CallExpressionNode::codegen - Look up the callee by name in TheModule, verify the
/// argument count, codegen each argument, then emit a call instruction.
///
/// getFunction searches the module for a declaration or function-definition with the
/// given name. This covers both previous 'extern' declarations and previously
/// defined functions. The argument count check catches mismatches that a typed
/// language would catch statically.
Value *CallExpressionNode::codegen() {
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Arguments.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Arguments.size(); i != e; ++i) {
    ArgsV.push_back(Arguments[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/// IfExpressionNode::codegen - Emit LLVM IR for an if/else expression.
///
/// Emitted control flow:
///
///   <current>:
///     ; Cond codegen emits a double value %condv
///     %ifcond = fcmp one double %condv, 0.0 ; convert the double to i1
///     br i1 %ifcond, label %then, label %else
///
///   then:
///     ; Then codegen emits %thenv
///     br label %ifcont
///
///   else:
///     ; Else codegen emits %elsev
///     br label %ifcont
///
///   ifcont:
///     %iftmp = phi double [ %thenv, %then_end ], [ %elsev, %else_end ]
///
/// `%iftmp` is the value of the whole if-expression.
///
/// We recapture `ThenBB` and `ElseBB` after branch codegen because nested
/// control flow can move the Builder insertion point to a different block.
/// PHI incoming edges must use the actual terminating blocks of each arm.
Value *IfExpressionNode::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to bool by comparing != 0.0
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for then, else, and merge.
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);

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
  Builder->SetInsertPoint(ElseBB);
  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;
  Builder->CreateBr(MergeBB);
  ElseBB = Builder->GetInsertBlock();

  // Emit merge block with phi node.
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);

  return PN;
}

/// ForExpressionNode::codegen - Emit LLVM IR for a for-expression.
///
/// CFG shape:
///
///   <preheader>:
///     %start = <Start codegen>
///     br label %loop_cond
///
///   loop_cond:
///     %i = phi double [ %start, %preheader ], [ %nextvar, %body_end ]
///     %condv = <Cond codegen>
///     %loopcond = fcmp one double %condv, 0.0
///     br i1 %loopcond, label %loop_body, label %after_loop
///
///   loop_body:
///     ; <Body codegen> (value ignored)
///     %step = <Step codegen>
///     %nextvar = fadd double %i, %step
///     br label %loop_cond
///
///   after_loop:
///     ; restore shadowed outer binding of loop variable (if any)
///     ; for-expression result:
///     ret-like value = 0.0
///
/// The loop variable name is rebound to `%i` while generating Cond/Step/Body,
/// then restored after the loop.
Value *ForExpressionNode::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Emit start value in the preheader (current block before the loop).
  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;

  BasicBlock *PreheaderBB = Builder->GetInsertBlock();

  // Create all three blocks up front so we can reference them in branches.
  BasicBlock *CondBB =
      BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
  BasicBlock *BodyBB =
      BasicBlock::Create(*TheContext, "loop_body", TheFunction);
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "after_loop", TheFunction);

  // Unconditional jump from preheader into the condition check.
  Builder->CreateBr(CondBB);

  // ---- loop_cond ----
  Builder->SetInsertPoint(CondBB);

  // PHI picks start_val on the first iteration, next_i on subsequent ones.
  // The back-edge incoming value is added below once we know BodyEndBB.
  PHINode *Variable =
      Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
  Variable->addIncoming(StartVal, PreheaderBB);

  // Shadow any outer variable of the same name so the body sees the loop var.
  Value *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Variable;

  // Evaluate the condition; treat 0.0 as false, anything else as true.
  Value *CondVal = Cond->codegen();
  if (!CondVal)
    return nullptr;
  CondVal = Builder->CreateFCmpONE(
      CondVal, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
  Builder->CreateCondBr(CondVal, BodyBB, AfterBB);

  // ---- loop_body ----
  Builder->SetInsertPoint(BodyBB);

  // Body is evaluated for side effects; its value is discarded.
  if (!Body->codegen())
    return nullptr;

  // Step: advance the loop variable.
  Value *StepVal = Step->codegen();
  if (!StepVal)
    return nullptr;
  Value *NextVar = Builder->CreateFAdd(Variable, StepVal, "nextvar");

  // Body codegen may have changed the insert block (e.g. nested ifs added
  // blocks). Capture where the body actually ended for the PHI back-edge.
  BasicBlock *BodyEndBB = Builder->GetInsertBlock();
  Variable->addIncoming(NextVar, BodyEndBB);
  Builder->CreateBr(CondBB);

  // ---- after_loop ----
  Builder->SetInsertPoint(AfterBB);

  // Restore the shadowed variable (if any) now that the loop is done.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // The for expression always produces 0.0.
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// FunctionSignatureNode::codegen - Create a function declaration in TheModule: name,
/// return type (always double), and parameter types (all double).
///
/// ExternalLinkage makes the function visible outside this module. That is
/// what allows 'extern def sin(x)' to link against the C library's sin at
/// runtime, and what lets 'def foo(...)' be called from later expressions in
/// the same session.
///
/// Arg.setName() is optional — it only affects the printed IR, making output
/// read as 'double %a, double %b' rather than 'double %0, double %1'.
Function *FunctionSignatureNode::codegen() {
  // All parameters and the return value are double.
  std::vector<Type *> Doubles(Parameters.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles,
                                       false /* not variadic */);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Name arguments so the printed IR is readable.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Parameters[Idx++]);

  return F;
}

/// FunctionDefinitionNode::codegen - Generate IR for a complete function definition.
///
/// Four steps:
///
/// 1. Register the function signature. The FunctionSignatureNode is moved into FunctionSignatures
///    so that future modules can re-emit a declaration for this function via
///    getFunction(). A reference is kept for the getFunction() call below.
///    getFunction() either finds an existing declaration in the current module
///    (e.g. from a prior 'extern def') or calls Signature->codegen() to create one.
///
/// 2. Create the entry BasicBlock and point the Builder at it. A basic block
///    is a straight-line sequence of instructions with one entry and one exit.
///    Every function starts with exactly one entry block.
///
/// 3. Populate NamedValues. Clear the table (the previous function's arguments
///    are irrelevant) and insert each argument. NameExpressionNode nodes in the
///    body look names up here.
///
/// 4. Codegen the body expression. On success, emit 'ret', run verifyFunction
///    (LLVM's internal consistency checker), then run TheFPM to apply the
///    optimisation pipeline. On failure, eraseFromParent() removes the
///    partially-built function so no broken declaration is left in the module.
Function *FunctionDefinitionNode::codegen() {
  // Step 1: register the function signature and resolve the Function*.
  auto &P = *Signature;
  FunctionSignatures[Signature->getName()] = std::move(Signature);

  // Step 1: reuse an existing `extern` declaration if one exists.
  Function *TheFunction = getFunction(P.getName());

  // Bail if the function is already fully defined — redefinition is an error.
  if (TheFunction && !TheFunction->empty()) {
    LogErrorExpression("Function cannot be redefined.");
    return nullptr;
  }

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
/// must be created for every subsequent function-definition or expression.
///
/// The optimisation pipeline is also recreated each time because
/// FunctionPassManager is tied to a specific LLVMContext.
///
/// Pipeline:
///   InstCombinePass  - Peephole rewrites: a+0->a, x*2->x<<1, etc.
///   ReassociatePass  - Reorder commutative ops to expose more folding:
///                      (x+2)+3 -> x+(2+3) -> x+5.
///   GVNPass          - Global Value Numbering: eliminate redundant loads and
///                      common sub-expressions across basic blocks.
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

  // Optimisation pipeline (applied per function after codegen). With -O0 the
  // pass manager is left empty so the emitted IR stays close to the direct
  // lowering performed by the code generator.
  if (OptLevel != 0) {
    TheFPM->addPass(InstCombinePass()); // peephole rewrites
    TheFPM->addPass(ReassociatePass()); // canonicalise commutative ops
    TheFPM->addPass(GVNPass());         // eliminate common sub-expressions
  }

  // Cross-register so passes can access any analysis tier they need.
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerCGSCCAnalyses(*TheCGAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.registerLoopAnalyses(*TheLAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

/// SynchronizeToLineBoundary - Panic-mode error recovery.
///
/// Advance past all remaining tokens on the current line so that MainLoop
/// sees tok_eol or tok_eof next. Called after any parse or codegen failure
/// and after any unexpected trailing token, ensuring the REPL always returns
/// to a clean state before printing the next prompt.
static void SynchronizeToLineBoundary() {
  while (CurrentToken != tok_eol && CurrentToken != tok_eof)
    getNextToken();
}

/// HandleFunctionDefinition - Parse, optimise, and JIT-compile a 'def' function-definition.
///
/// On success: codegen + optimise the function (TheFPM runs inside
/// FunctionDefinitionNode::codegen), print the optimised IR, then hand the entire
/// module to the JIT via addModule. The JIT takes ownership of TheModule and
/// TheContext, so InitializeModuleAndManagers() is called immediately after
/// to create a fresh module for the next input. The compiled function remains
/// accessible in the JIT's symbol table for the rest of the session.
/// On parse failure or unexpected trailing tokens: discard the line.
static void HandleFunctionDefinition() {
  auto FunctionDefinition = ParseFunctionDefinition();
  if (!FunctionDefinition || (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (FunctionDefinition)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FunctionIR = FunctionDefinition->codegen()) {
    Log("Parsed a function definition.\n");
    if (VerboseIR)
      FunctionIR->print(errs());
    // Transfer the module to the JIT. TheModule is now invalid; reinitialise.
    ExitOnErr(TheJIT->addModule(
        ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
    InitializeModuleAndManagers();
  }
}

/// HandleExtern - Parse and register an 'extern def' declaration.
///
/// On success: codegen the function signature (emits a 'declare' in the current module),
/// print it, then save the FunctionSignatureNode into FunctionSignatures. Saving into
/// FunctionSignatures is the critical step — when this module is handed to the JIT
/// and a new one is created, getFunction() uses FunctionSignatures to re-emit the
/// 'declare' in whichever module needs to call the extern.
/// On parse failure or unexpected trailing tokens: discard the line.
static void HandleExtern() {
  auto Signature = ParseExtern();

  if (!Signature || (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (Signature)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  // Reject conflicting redeclarations: in Pyxc, function identity is just
  // name + arity, since all parameter and return types are double.
  auto Existing = FunctionSignatures.find(Signature->getName());
  if (Existing != FunctionSignatures.end() &&
      Existing->second->getNumParameters() != Signature->getNumParameters()) {
    LogErrorExpression((string("Conflicting extern declaration for '") +
              Signature->getName() + "'")
                 .c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (auto *FunctionIR = Signature->codegen()) {
    Log("Parsed an extern.\n");
    if (VerboseIR)
      FunctionIR->print(errs());
    // Save the function signature so getFunction() can re-emit it in future modules.
    FunctionSignatures[Signature->getName()] = std::move(Signature);
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
  auto FunctionDefinition = ParseTopLevelExpression();
  if (!FunctionDefinition || (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (FunctionDefinition)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FunctionIR = FunctionDefinition->codegen()) {
    Log("Parsed a top-level expression.\n");
    if (VerboseIR)
      FunctionIR->print(errs());

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
/// top-level-item
///   = function-definition | external | top-level-expression ;
///
/// Dispatches on the leading token of each top-level form:
///   tok_def    → HandleFunctionDefinition   (function-definition)
///   tok_extern → HandleExtern       (external)
///   tok_eol    → skip blank line
///   anything else → HandleTopLevelExpression (top-level-expression)
///
/// CurrentToken is primed before MainLoop() is called (see main()). After each
/// successful parse the handler prints a confirmation; after a failed parse
/// the handler calls SynchronizeToLineBoundary() to discard all remaining
/// tokens on the current line. Either way we return here to look at the
/// next CurrentToken.
static void MainLoop() {
  while (true) {
    if (CurrentToken == tok_eof)
      return;

    // A bare newline: just print a fresh prompt and read the next token.
    if (CurrentToken == tok_eol) {
      PrintReplPrompt();
      getNextToken();
      continue;
    }

    if (CurrentToken == tok_error) {
      SynchronizeToLineBoundary();
      continue;
    }

    switch (CurrentToken) {
    case tok_def:
      HandleFunctionDefinition();
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

/// ProcessCommandLine - Parse argv and configure the global Input/IsRepl state.
///
/// Returns 0 on success, -1 on error (e.g. the file could not be opened). When
/// no file is given, Input stays as stdin and IsRepl is set to true.
int ProcessCommandLine(int argc, const char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::ParseCommandLineOptions(argc, argv, "pyxc\n");

  if (OptLevel > 3) {
    fprintf(stderr, "Error: -O level must be 0, 1, 2, or 3\n");
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

//===----------------------------------------===//
// Main driver code.
//===----------------------------------------===//

/// main - Entry point for the Pyxc compiler/REPL.
///
/// Initialises the LLVM native backend, creates the ORC JIT and an initial
/// module, then hands control to MainLoop(). On exit, any open script file is
/// closed.
int main(int argc, const char **argv) {

  int commandLineResult = ProcessCommandLine(argc, argv);
  if (commandLineResult != 0) {
    return commandLineResult;
  }

  // I initialise LLVM's target and assembly printer for the host machine.
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  // Prime the REPL: print the first prompt and load the first token.
  // Every parse function expects CurrentToken to be loaded before it is called.
  PrintReplPrompt();
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

#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
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

  // primary
  tok_name = -5,
  tok_number = -6,

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
};

static string Name;          // Filled in if tok_name
static double NumberValue;   // Filled in if tok_number
static string NumberLiteral; // Filled in if tok_number, used in error messages

// Keywords like `def`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static map<string, Token> Keywords = {{"def", tok_def}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // I list tokens that are not single characters.
  static map<int, string> Names = {
      {tok_eof, "end of input"}, {tok_eol, "newline"}, {tok_error, "error"},
      {tok_def, "'def'"},        {tok_name, "name"},   {tok_number, "number"},
  };

  // I add a readable name for every single-character value.
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
//   CurLoc  - snapshotted at the start of each token in getToken(), before
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
    // I may need the current line before I have consumed its newline.
    if (Index == CompletedLines.size())
      return &CurrentLine;
    return nullptr;
  }
};

static SourceManager PyxcSourceMgr;
static void PrintErrorSourceContext(SourceLocation Loc);
static void LogInvalidNumberLiteralAtLoc(const string &Literal,
                                         SourceLocation Loc);

/// advance - I return the next character, normalizing `\r\n` (Windows)
/// and bare `\r` (Old Macs) into `\n`.
///
/// This is the single point through which all character consumption flows.
/// Every token branch in getToken() calls advance() rather than getchar()
/// directly, so LexLoc and the source buffer are always in sync.
static int advance() {
  int LastChar = getchar();

  // case: '\r' or '\r\n'
  if (LastChar == '\r') {
    int NextChar = getchar();

    // A following '\n' is part of the same line ending; eat it.
    // Anything else belongs to the next token; put it back.
    // (EOF can't be put back at all, so it's excluded from that check.
    // The next getchar() will still return EOF, so we don't lose it.)
    if (NextChar != '\n' && NextChar != EOF) {
      ungetc(NextChar, stdin);
    }
    PyxcSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }

  // '\n' resets Col and starts a new buffered line; anything else
  // just advances Col within the current line.
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
/// GetCaretAnchorLoc compensates by subtracting one when building error
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
    LastChar = ' ';
    return tok_eol;
  }

  if (isalpha(LastChar) || LastChar == '_') {
    string NameLiteral;
    NameLiteral = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
      NameLiteral += LastChar;

    auto It = Keywords.find(NameLiteral);
    if (It != Keywords.end())
      return It->second;
    Name = NameLiteral;
    return tok_name;
  }

  if (isdigit(LastChar) || LastChar == '.') {
    NumberLiteral.clear();
    do {
      NumberLiteral += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    char *End = nullptr;
    NumberValue = strtod(NumberLiteral.c_str(), &End);
    if (!End || *End != '\0') {
      LogInvalidNumberLiteralAtLoc(NumberLiteral, CurLoc);
      return tok_error;
    }
    return tok_number;
  }

  // I discard a comment.
  if (LastChar == '#') {
    // I consume characters through the end of the line.
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar == '\n') {
      // Re-snapshot CurLoc now that the '\n' has been consumed and LexLoc
      // has advanced to the next line. Without this, CurLoc would point at
      // the '#' column, and GetCaretAnchorLoc would look up the wrong
      // line when the next token triggers an error.
      CurLoc = LexLoc;
      LastChar = ' ';
      return tok_eol;
    }
  }

  if (LastChar == EOF)
    return tok_eof;

  int ThisChar = LastChar;
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
  default:
    return ThisChar;
  }
}

//===----------------------------------------===//
// Diagnostics helpers
//===----------------------------------------===//

/// GetCaretAnchorLoc - Resolve the source location to attach to an error.
///
/// For most tokens, CurLoc already points at the right place and is returned
/// unchanged. The special case is tok_eol: CurLoc for a newline token is
/// snapshotted after advance() has consumed the '\n' and incremented
/// LexLoc.Line, so CurLoc.Line is already the *next* line. Subtracting one
/// gives the line that just ended, and I report a column one past its last
/// character — pointing just after the final token on the line, which is
/// where the missing token (e.g. ':') should have appeared.
static SourceLocation GetCaretAnchorLoc(SourceLocation Loc, int Tok) {
  if (Tok != tok_eol)
    return Loc;

  int PrevLine = Loc.Line - 1;
  if (PrevLine <= 0)
    return Loc;

  const string *PrevLineText = PyxcSourceMgr.getLine(PrevLine);
  // PrevLineText is null only if PrevLine hasn't been buffered yet —
  // it shouldn't happen, since I only get here after consuming that
  // line's trailing newline, but I fall back to the original Loc
  // rather than trust an out-of-range read.
  if (!PrevLineText)
    return Loc;

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
/// '^~~~' caret under column Loc.Col. Col is 1-based, so I print Col-1
/// spaces before the caret.
static void PrintErrorSourceContext(SourceLocation Loc) {
  const string *LineText = PyxcSourceMgr.getLine(Loc.Line);
  // LineText is null only if Loc points past everything buffered so
  // far (e.g. an uninitialized Loc.Line == 0). Skip printing rather
  // than dereference it below.
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = Loc.Col - 1;
  // I guard against an invalid column before printing the spaces.
  if (spaces < 0)
    spaces = 0;
  for (int i = 0; i < spaces; ++i)
    fputc(' ', stderr);
  fprintf(stderr, "^~~~\n");
}

static void LogInvalidNumberLiteralAtLoc(const string &Literal,
                                         SourceLocation Loc) {
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
class BinaryExpressionNode : public ExpressionNode {
  char Operator;
  unique_ptr<ExpressionNode> Left, Right;

public:
  BinaryExpressionNode(char Operator, unique_ptr<ExpressionNode> Left,
                       unique_ptr<ExpressionNode> Right)
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {}
  Value *codegen() override;
};

/// UnaryExpressionNode - Expression class for applying a unary operator.
class UnaryExpressionNode : public ExpressionNode {
  char Operator;
  unique_ptr<ExpressionNode> Operand;

public:
  UnaryExpressionNode(char Operator, unique_ptr<ExpressionNode> Operand)
      : Operator(Operator), Operand(std::move(Operand)) {}
  Value *codegen() override;
};

/// CallExpressionNode - Expression class for function calls.
class CallExpressionNode : public ExpressionNode {
  string Callee;
  vector<unique_ptr<ExpressionNode>> Arguments;

public:
  CallExpressionNode(const string &Callee,
                     vector<unique_ptr<ExpressionNode>> Arguments)
      : Callee(Callee), Arguments(std::move(Arguments)) {}
  Value *codegen() override;
};

/// FunctionSignatureNode - This class represents the "function signature" for a
/// function, which captures its name, and its parameter names (thus implicitly
/// the number of parameters the function takes).
class FunctionSignatureNode {
  string Name;
  vector<string> Parameters;

public:
  FunctionSignatureNode(const string &Name, vector<string> Parameters)
      : Name(Name), Parameters(std::move(Parameters)) {}

  const string &getName() const { return Name; }
  Function *codegen();
};

/// FunctionDefinitionNode - This class represents a function definition itself.
class FunctionDefinitionNode {
  unique_ptr<FunctionSignatureNode> Signature;
  unique_ptr<ExpressionNode> Body;

public:
  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature,
                         unique_ptr<ExpressionNode> Body)
      : Signature(std::move(Signature)), Body(std::move(Body)) {}
  Function *codegen();
};

} // end anonymous namespace

//===----------------------------------------===//
// Parser
//===----------------------------------------===//

/// CurrentToken is the current token the parser is looking at.
/// getNextToken reads the next token from the lexer and stores it in
/// CurrentToken. Every parse function assumes CurrentToken is already loaded
/// before it is called, and leaves CurrentToken pointing at the first token it
/// did not consume.
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

/// LogErrorExpression* - Error reporting helpers. Each returns nullptr for its
/// respective type so parse functions can write: return
/// LogErrorExpression("message");
unique_ptr<ExpressionNode> LogErrorExpression(const char *ErrorMessage) {
  SourceLocation Anchor = GetCaretAnchorLoc(CurLoc, CurrentToken);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Col,
          ErrorMessage);
  PrintErrorSourceContext(Anchor);
  return nullptr;
}

unique_ptr<FunctionSignatureNode> LogErrorSignature(const char *ErrorMessage) {
  LogErrorExpression(ErrorMessage);
  return nullptr;
}

unique_ptr<FunctionDefinitionNode> LogErrorFunction(const char *ErrorMessage) {
  LogErrorExpression(ErrorMessage);
  return nullptr;
}

static unique_ptr<ExpressionNode> ParseExpression();

/// number-expression
///   = number ;
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  auto Result = make_unique<NumberExpressionNode>(NumberValue);
  getNextToken(); // I consume the number.
  return Result;
}

/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // I eat '('.
  auto Expression = ParseExpression();
  if (!Expression)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')'");
  getNextToken(); // I eat ')'.
  return Expression;
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

  getNextToken(); // I eat the name.

  if (CurrentToken != tok_lparen) // I return a name, not a call.
    return make_unique<NameExpressionNode>(ParsedName);

  // I parse a call.
  getNextToken(); // I eat '('.
  vector<unique_ptr<ExpressionNode>> Arguments;
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (auto Argument = ParseExpression())
        Arguments.push_back(std::move(Argument));
      else
        return nullptr;

      // ParseExpression() has already consumed the argument and left
      // CurrentToken at the token after it.
      if (CurrentToken == tok_rparen)
        break;

      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken(); // I eat ','.
    }
  }

  // I only reach here after parsing `a()` or `a(<arguments>)`, so I eat ')'.
  getNextToken();

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments));
}

/// primary
///   = name-expression
///   | number-expression
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  case tok_number:
    return ParseNumberExpression(); // I parse a number such as 3.14.
  case tok_name:
    return ParseNameExpression(); // I parse `a` or `add(...)`.
  case tok_lparen:
    return ParseParenthesizedExpression(); // I parse `( ... )`.
  default:
    return LogErrorExpression(
        ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
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
  // I start the term by parsing one factor.
  auto Left = ParseFactor();
  if (!Left)
    return nullptr;

  // I consume only the operators that belong to this tier.
  while (CurrentToken == tok_star || CurrentToken == tok_slash ||
         CurrentToken == tok_percent) {
    int Operator = CurrentToken;
    getNextToken(); // I eat '*', '/', or '%'.
    auto Right = ParseFactor();
    if (!Right)
      return nullptr;

    // I fold each new operation into the tree on my left.
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }
  return Left;
}

/// sum
///   = term { ("+" | "-") term } ;
static unique_ptr<ExpressionNode> ParseSum() {
  // I call ParseTerm() so I finish every tighter * or / operation first.
  auto Left = ParseTerm();
  if (!Left)
    return nullptr;
  while (CurrentToken == tok_plus || CurrentToken == tok_minus) {
    int Operator = CurrentToken;
    getNextToken(); // I eat '+' or '-'.
    auto Right = ParseTerm();
    if (!Right)
      return nullptr;
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }
  return Left;
}

/// comparison
///   = sum { "<" sum } ;
static unique_ptr<ExpressionNode> ParseComparison() {
  // I call ParseSum() so I finish both sums before I build the comparison.
  auto Left = ParseSum();
  if (!Left)
    return nullptr;
  while (CurrentToken == tok_less) {
    int Operator = CurrentToken;
    getNextToken(); // I eat '<'.
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
  // I start at the loosest tier so the expression can contain every tier.
  return ParseComparison();
}

/// function-signature
///   = name "(" [ parameters ] ")" ;
/// parameters
///   = parameter { "," parameter } ;
/// parameter
///   = name ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  // Callers consume the leading 'def', so the current token must be the
  // function name.
  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");

  string FunctionName = Name;
  getNextToken(); // I eat the function name.

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  // I parse parameter names. I call getNextToken() at the top to advance past
  // '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body I call getNextToken() again to move past the name
  // I just stored, then check whether ')' or ',' follows.
  vector<string> ParameterNames;
  while (getNextToken() == tok_name) {
    ParameterNames.push_back(Name);

    if (getNextToken() == tok_rparen) // I eat the name and check what follows.
      break;

    if (CurrentToken != tok_comma)
      return LogErrorSignature("Expected ')' or ',' in parameter list");
    // I continue the loop so getNextToken() at the top eats the ','.
  }

  if (CurrentToken != tok_rparen)
    return LogErrorSignature("Expected ')' in function signature");

  getNextToken(); // I eat ')'.

  return make_unique<FunctionSignatureNode>(FunctionName, std::move(ParameterNames));
}

/// function-definition
///   = "def" function-signature ":" [ end-of-lines ] expression ;
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // I eat 'def'.
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;

  if (CurrentToken != tok_colon)
    return LogErrorFunction("Expected ':' in function definition");
  getNextToken(); // I eat ':'.

  // I allow the body expression to start on the next line:
  //   def foo(x):
  //     x + 1
  consumeNewlines();

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return std::make_unique<FunctionDefinitionNode>(std::move(Signature),
                                                  std::move(Body));
}

/// top-level-expression
///   = expression ;
/// A top-level expression (e.g. "1 + 2") is wrapped in an anonymous function
/// so it fits the same FunctionDefinitionNode shape as everything else. When I
/// add JIT execution later, I'll look up "__anon_expr" and call it to get the
/// result.
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  // I invent a function signature with an internal name and no parameters
  auto Signature =
      make_unique<FunctionSignatureNode>("__anon_expr", vector<string>());

  return std::make_unique<FunctionDefinitionNode>(std::move(Signature),
                                                  std::move(Body));
}

//===----------------------------------------===//
// Code Generation
//===----------------------------------------===//

// TheContext - Owns all LLVM data structures: types, constants, and the
// interning tables that ensure two uses of 'double' resolve to the same
// object. One context per compilation unit (one per thread in threaded builds).
//
// TheModule - The unit of compilation. Every function definition lands here.
// At session end I print the whole module so I can inspect all accumulated IR
// in one place.
//
// TheBuilder - A cursor into the IR being built. Point it at a BasicBlock with
// SetInsertPoint(), then call methods like CreateFAdd or CreateFMul to append
// instructions. Each method returns the Value* representing the result.
//
// NamedValues - Symbol table mapping parameter names to their Value*.
// Populated fresh at the start of each function body and cleared between
// functions — it only ever holds the current function's arguments. Mutable
// local variables come in a later chapter.
static unique_ptr<LLVMContext> TheContext;
static unique_ptr<Module> TheModule;
static unique_ptr<IRBuilder<>> TheBuilder;
static map<std::string, Value *> NamedValues;

// LogErrorV - Codegen-level error helper. Delegates to LogErrorExpression for
// printing, then returns nullptr so codegen callers can write: return
// LogErrorV("msg");
Value *LogErrorV(const char *ErrorMessage) {
  LogErrorExpression(ErrorMessage);
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
    return TheBuilder->CreateFNeg(OperandValue, "negtmp");
  return LogErrorV("invalid unary operator");
}

/// BinaryExpressionNode::codegen - Recursively codegen both operands, then emit
/// the operator-specific instruction.
///
/// The string arguments to each Create* call ("addtmp", "multmp", etc.) are
/// hint names for the SSA value. LLVM uses them when printing IR, appending a
/// numeric suffix when the same hint would otherwise repeat. They have no
/// effect on correctness.
///
/// '<' requires two steps: CreateFCmpULT produces a 1-bit integer (i1) —
/// LLVM's boolean type. Since Pyxc treats everything as double, CreateUIToFP
/// widens it: false -> 0.0, true -> 1.0.
Value *BinaryExpressionNode::codegen() {
  Value *L = Left->codegen();
  if (!L)
    return nullptr;

  Value *R = Right->codegen();
  if (!R)
    return nullptr;

  switch (Operator) {
  case tok_plus:
    return TheBuilder->CreateFAdd(L, R, "addtmp");
  case tok_minus:
    return TheBuilder->CreateFSub(L, R, "subtmp");
  case tok_star:
    return TheBuilder->CreateFMul(L, R, "multmp");
  case tok_slash:
    return TheBuilder->CreateFDiv(L, R, "divtmp");
  case tok_percent:
    return TheBuilder->CreateFRem(L, R, "remtmp");
  case tok_less:
    L = TheBuilder->CreateFCmpULT(L, R, "cmptmp");
    // Widen the i1 boolean to double: false -> 0.0, true -> 1.0.
    return TheBuilder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                    "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}

/// CallExpressionNode::codegen - Look up the callee by name in TheModule,
/// verify the argument count, codegen each argument, then emit a call
/// instruction.
///
/// getFunction searches the module for a declaration or function-definition
/// with the given name — any function already defined earlier in this session.
/// The argument count check catches mismatches that a typed language would
/// catch statically.
Value *CallExpressionNode::codegen() {
  Function *CalleeF = TheModule->getFunction(Callee);
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

  return TheBuilder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/// FunctionSignatureNode::codegen - Create a function declaration in TheModule:
/// name, return type (always double), and parameter types (all double).
///
/// ExternalLinkage makes the function visible outside this module. That is
/// what lets 'def foo(...)' be called from later expressions in the same
/// session. I'll lean on this same linkage again in Chapter 8, when 'extern'
/// declarations use it to resolve against real C library functions at
/// runtime.
///
/// Arg.setName() is optional — it only affects the printed IR, making output
/// read as 'double %a, double %b' rather than 'double %0, double %1'.
Function *FunctionSignatureNode::codegen() {
  // I use double for every parameter and for the return value.
  std::vector<Type *> Doubles(Parameters.size(),
                              Type::getDoubleTy(*TheContext));
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles,
                                       false /* not variadic */);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // I name the arguments so the printed IR is easier to read.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Parameters[Idx++]);

  return F;
}

/// FunctionDefinitionNode::codegen - Generate IR for a complete function
/// definition.
///
/// Four steps:
///
/// 1. Get or create the function declaration. If this name was already
///    declared earlier in this session, getFunction finds it — used below to
///    catch redefinition. Otherwise Signature->codegen() creates a fresh
///    declaration. Either way TheFunction is a valid Function* with no body
///    yet.
///
/// 2. Create the entry BasicBlock and point the TheBuilder at it. A basic block
///    is a straight-line sequence of instructions with one entry and one exit.
///    Every function starts with exactly one entry block.
///
/// 3. Populate NamedValues. Clear the table (the previous function's arguments
///    are irrelevant) and insert each argument. NameExpressionNode nodes in the
///    body look names up here.
///
/// 4. Codegen the body expression. On success, emit 'ret' and run
///    verifyFunction — LLVM's internal consistency checker that catches codegen
///    bugs such as using a value defined in a different function or leaving a
///    block without a terminator. On failure, eraseFromParent() removes the
///    partially-built function so no broken declaration is left in the module.
Function *FunctionDefinitionNode::codegen() {
  // Step 1: I get an existing declaration or create a new one.
  Function *TheFunction = TheModule->getFunction(Signature->getName());

  if (TheFunction && !TheFunction->empty()) {
    LogErrorExpression("Function cannot be redefined.");
    return nullptr;
  }

  if (!TheFunction)
    TheFunction = Signature->codegen();

  if (!TheFunction)
    return nullptr;

  // Step 2: I create the entry block and insert new instructions there.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  TheBuilder->SetInsertPoint(BB);

  // Step 3: I make the parameters available to the body.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  // Step 4: I generate the body, return its value, and verify the function.
  if (Value *RetVal = Body->codegen()) {
    TheBuilder->CreateRet(RetVal);
    verifyFunction(*TheFunction);
    return TheFunction;
  }

  // Reaching here means Body->codegen() failed. I remove the incomplete
  // function so no broken declaration is left in the module.
  TheFunction->eraseFromParent();
  return nullptr;
}

//===----------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------===//

/// InitializeModule - Create the three LLVM globals that all codegen depends
/// on. Called once before MainLoop(). In Chapter 8, which adds JIT
/// execution this will be called fresh for each top-level expression, because
/// the JIT takes ownership of the module after compiling it.
static void InitializeModuleAndManagers() {
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  TheBuilder = std::make_unique<IRBuilder<>>(*TheContext);
}

/// SynchronizeToLineBoundary - Panic-mode error recovery.
///
/// Advance past all remaining tokens on the current line so that MainLoop
/// sees tok_eol or tok_eof next. Called after any parse or codegen failure
/// and after any unexpected trailing token, ensuring the REPL always returns
/// to a clean state before printing the next prompt.
static void SynchronizeToLineBoundary() {
  // I leave the boundary token for MainLoop() to handle.
  while (CurrentToken != tok_eol && CurrentToken != tok_eof)
    getNextToken();
}

/// HandleFunctionDefinition - Parse and codegen a 'def' function definition.
///
/// On success: codegen the function, print the confirmation message and the
/// resulting IR. The function remains in TheModule for the rest of the session
/// so later calls to it can be resolved.
/// On parse failure or unexpected trailing tokens: discard the rest of the
/// line and return.
static void HandleFunctionDefinition() {
  auto FunctionDefinition = ParseFunctionDefinition();
  if (!FunctionDefinition ||
      (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (FunctionDefinition)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FunctionIR = FunctionDefinition->codegen()) {
    fprintf(stderr, "Parsed a function definition.\n");
    FunctionIR->print(errs());
    fprintf(stderr, "\n");
  }
}

/// HandleTopLevelExpression - Parse and codegen a bare expression.
///
/// The expression is wrapped in an anonymous function '__anon_expr' so it
/// fits the same FunctionDefinitionNode shape as everything else. After
/// printing the IR I call eraseFromParent() to remove it from the module —
/// anonymous expressions are for display only and should not appear in the
/// final dump. In Chapter 8 the JIT will execute the function before erasing
/// it, printing the numeric result.
static void HandleTopLevelExpression() {
  auto FunctionDefinition = ParseTopLevelExpression();
  if (!FunctionDefinition ||
      (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (FunctionDefinition)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FunctionIR = FunctionDefinition->codegen()) {
    fprintf(stderr, "Parsed a top-level expression.\n");
    FunctionIR->print(errs());
    fprintf(stderr, "\n");

    // Erase after printing — anonymous expressions don't belong in the final
    // module dump.
    FunctionIR->eraseFromParent();
  }
}

/// MainLoop - Dispatch loop for the REPL.
///
/// program
///   = [ end-of-lines ]
///     [ top-level-item { end-of-lines top-level-item } ]
///     [ end-of-lines ] ;
///
/// CurrentToken is primed before MainLoop() is called (see main()). After each
/// successful parse the handler prints a confirmation; after a failed parse it
/// skips one token. Either way I come back here and look at the new
/// CurrentToken.
static void MainLoop() {
  while (CurrentToken != tok_eof) {
    switch (CurrentToken) {
    case tok_error:
      SynchronizeToLineBoundary();
      break;
    case tok_eol:
      // For a bare newline, I print a fresh prompt and read the next token.
      fprintf(stderr, "ready> ");
      getNextToken();
      break;
    case tok_def:
      HandleFunctionDefinition();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

//===----------------------------------------===//
// Main driver code.
//===----------------------------------------===//

int main() {
  // I print the first prompt and load the first token before entering the loop.
  // I load CurrentToken before I call any parse function.
  fprintf(stderr, "ready> ");
  getNextToken();

  // Make the module, which holds all the code.
  InitializeModuleAndManagers();

  MainLoop();

  // Print out all of the generated code.
  TheModule->print(errs(), nullptr);

  return 0;
}

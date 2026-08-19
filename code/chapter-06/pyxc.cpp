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

static constexpr char AnonymousExpressionFunctionName[] = "__anon_expr";

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

// SourceLocation - A {Line, Column} pair. Line and Column are 1-based.
//
// Two globals track position as characters are consumed:
//   LexerLocation  - where the character-read head (advance()) currently is.
//             Updated on every advance() call. After a '\n', Line increments
//             and Column resets to 0 so the next character will be Column 1.
//   CurrentTokenLocation  - snapshotted at the start of each token in getToken(), before
//             consuming any of the token's characters. This is the position
//             the parser and diagnostics infrastructure see.
struct SourceLocation {
  int Line;
  int Column;
};
static SourceLocation CurrentTokenLocation;
static SourceLocation LexerLocation = {1, 0};

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

static SourceManager PyxcSourceManager;
static void PrintErrorSourceContext(SourceLocation Location);
static void LogInvalidNumberLiteralAtLocation(const string &Literal,
                                         SourceLocation Location);

/// advance - I return the next character, normalizing `\r\n` (Windows)
/// and bare `\r` (Old Macs) into `\n`.
///
/// This is the single point through which all character consumption flows.
/// Every token branch in getToken() calls advance() rather than getchar()
/// directly, so LexerLocation and the source buffer are always in sync.
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
    PyxcSourceManager.onChar('\n');
    LexerLocation.Line++;
    LexerLocation.Column = 0;
    return '\n';
  }

  // '\n' resets Column and starts a new buffered line; anything else
  // just advances Column within the current line.
  if (LastChar == '\n') {
    PyxcSourceManager.onChar('\n');
    LexerLocation.Line++;
    LexerLocation.Column = 0;
  } else {
    PyxcSourceManager.onChar(LastChar);
    LexerLocation.Column++;
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
/// CurrentTokenLocation is snapshotted from LexerLocation after the whitespace-skip loop and
/// before any token branch. For most tokens this points at the first
/// character of the token. For tok_eol the '\n' was already consumed by
/// advance() on a previous call, so LexerLocation is already on the next line;
/// GetCaretAnchorLocation compensates by subtracting one when building error
/// locations for tok_eol.
///
/// The comment path ('#' branch) re-snapshots CurrentTokenLocation just before returning
/// tok_eol because it consumes many characters (the whole comment) after the
/// initial snapshot, leaving LexerLocation well past the '#' position.
static int getToken() {
  static int LastChar = ' ';

  // Skip horizontal whitespace. Stop at '\n' — that becomes tok_eol.
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // Snapshot position for the upcoming token. See note above about tok_eol.
  CurrentTokenLocation = LexerLocation;

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
      LogInvalidNumberLiteralAtLocation(NumberLiteral, CurrentTokenLocation);
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
      // Re-snapshot CurrentTokenLocation now that the '\n' has been consumed and LexerLocation
      // has advanced to the next line. Without this, CurrentTokenLocation would point at
      // the '#' column, and GetCaretAnchorLocation would look up the wrong
      // line when the next token triggers an error.
      CurrentTokenLocation = LexerLocation;
      LastChar = ' ';
      return tok_eol;
    }
  }

  if (LastChar == EOF)
    return tok_eof;

  int ThisChar = LastChar;
  LastChar = advance();
  // Single-character tokens (operators and punctuation) are all defined
  // as their own char value (e.g. tok_lparen = '('), so the raw character
  // returned here already IS the right token.
  return ThisChar;
}

//===----------------------------------------===//
// Diagnostics helpers
//===----------------------------------------===//

/// GetCaretAnchorLocation - Resolve the source location to attach to an error.
///
/// For most tokens, CurrentTokenLocation already points at the right place and is returned
/// unchanged. The special case is tok_eol: CurrentTokenLocation for a newline token is
/// snapshotted after advance() has consumed the '\n' and incremented
/// LexerLocation.Line, so CurrentTokenLocation.Line is already the *next* line. Subtracting one
/// gives the line that just ended, and I report a column one past its last
/// character — pointing just after the final token on the line, which is
/// where the missing token (e.g. ':') should have appeared.
static SourceLocation GetCaretAnchorLocation(SourceLocation Location, int Token) {
  if (Token != tok_eol || Location.Line <= 1)
    return Location;

  // Token == tok_eol && Location.Line > 1. I need to return a location just
  // past the end of the previous line.
  int PrevLine = Location.Line - 1;
  const string *PrevLineText = PyxcSourceManager.getLine(PrevLine);
  // PrevLineText is null only if PrevLine hasn't been buffered yet —
  // it shouldn't happen, since I only get here after consuming that
  // line's trailing newline, but I fall back to the original Location
  // rather than trust an out-of-range read.
  if (!PrevLineText)
    return Location;

  return {PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}

/// FormatTokenForMessage - Return a human-readable description of Tok for use
/// in error messages. Name and number tokens include their actual text
/// (e.g. "name 'foo'", "number '3.14'") since the name alone is not
/// enough to diagnose the problem. Everything else uses the static TokenNames
/// entry.
static string FormatTokenForMessage(int Token) {
  if (Token == tok_name)
    return "name '" + Name + "'";
  if (Token == tok_number)
    return "number '" + NumberLiteral + "'";

  auto It = TokenNames.find(Token);
  if (It != TokenNames.end())
    return It->second;
  return "unknown token";
}

/// PrintErrorSourceContext - Reprint the source line at Loc and place a
/// '^~~~' caret under column Location.Column. Column is 1-based, so I print Column-1
/// spaces before the caret.
static void PrintErrorSourceContext(SourceLocation Location) {
  const string *LineText = PyxcSourceManager.getLine(Location.Line);
  // LineText is null only if Location points past everything buffered so
  // far (e.g. an uninitialized Location.Line == 0). Skip printing rather
  // than dereference it below.
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = Location.Column - 1;
  // I guard against an invalid column before printing the spaces.
  if (spaces < 0)
    spaces = 0;
  for (int i = 0; i < spaces; ++i)
    fputc(' ', stderr);
  fprintf(stderr, "^~~~\n");
}

static void LogInvalidNumberLiteralAtLocation(const string &Literal,
                                              SourceLocation Location) {
  fprintf(stderr, "Error (Line %d, Column %d): invalid number literal '%s'\n",
          Location.Line, Location.Column, Literal.c_str());
  PrintErrorSourceContext(Location);
}

//===----------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------===//

/// ExpressionNode - Base class for all expression nodes.
class ExpressionNode {
public:
  virtual ~ExpressionNode() = default;
};

/// NumberExpressionNode - Expression class for numeric literals like "1.0".
class NumberExpressionNode : public ExpressionNode {
  double Value;

public:
  NumberExpressionNode(double Value) : Value(Value) {}
};

/// NameExpressionNode - Expression class for referencing a variable, like "a".
class NameExpressionNode : public ExpressionNode {
  string Name;

public:
  NameExpressionNode(const string &Name) : Name(Name) {}
};

/// BinaryExpressionNode - Expression class for a binary operator.
class BinaryExpressionNode : public ExpressionNode {
  char Operator;
  unique_ptr<ExpressionNode> Left, Right;

public:
  BinaryExpressionNode(char Operator, unique_ptr<ExpressionNode> Left,
                       unique_ptr<ExpressionNode> Right)
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {}
};

/// UnaryExpressionNode - Expression class for applying a unary operator.
class UnaryExpressionNode : public ExpressionNode {
  char Operator;
  unique_ptr<ExpressionNode> Operand;

public:
  UnaryExpressionNode(char Operator, unique_ptr<ExpressionNode> Operand)
      : Operator(Operator), Operand(std::move(Operand)) {}
};

/// CallExpressionNode - Expression class for function calls.
class CallExpressionNode : public ExpressionNode {
  string Callee;
  vector<unique_ptr<ExpressionNode>> Arguments;

public:
  CallExpressionNode(const string &Callee,
                     vector<unique_ptr<ExpressionNode>> Arguments)
      : Callee(Callee), Arguments(std::move(Arguments)) {}
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
};

/// FunctionDefinitionNode - This class represents a function definition itself.
class FunctionDefinitionNode {
  unique_ptr<FunctionSignatureNode> Signature;
  unique_ptr<ExpressionNode> Body;

public:
  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature,
                         unique_ptr<ExpressionNode> Body)
      : Signature(std::move(Signature)), Body(std::move(Body)) {}
};

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

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogErrorExpression("message");
unique_ptr<ExpressionNode> LogErrorExpression(const string &ErrorMessage) {
  SourceLocation Anchor = GetCaretAnchorLocation(CurrentTokenLocation, CurrentToken);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Column,
          ErrorMessage.c_str());
  PrintErrorSourceContext(Anchor);
  return nullptr;
}

unique_ptr<FunctionSignatureNode> LogErrorSignature(const string &ErrorMessage) {
  LogErrorExpression(ErrorMessage);
  return nullptr;
}

unique_ptr<FunctionDefinitionNode> LogErrorFunction(const string &ErrorMessage) {
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
  auto Signature = make_unique<FunctionSignatureNode>(
      AnonymousExpressionFunctionName, vector<string>());

  return std::make_unique<FunctionDefinitionNode>(std::move(Signature),
                                                  std::move(Body));
}

//===----------------------------------------===//
// Top-Level parsing
//===----------------------------------------===//

/// DiscardRestOfLine - Panic-mode error recovery. Advance past all
/// remaining tokens on the current line so that the next thing MainLoop sees
/// is tok_eol or tok_eof. Called after any parse failure and after any
/// unexpected trailing token, ensuring the REPL always returns to a known
/// state before printing the next prompt.
///
/// HandleFunctionDefinition/HandleTopLevelExpression - Called by MainLoop
/// when it sees the appropriate leading token. On success, print a
/// confirmation. On failure or unexpected trailing tokens, call
/// DiscardRestOfLine() to discard the rest of the input line.

static void DiscardRestOfLine() {
  // I stop at tok_eol or tok_eof without consuming it, so MainLoop()
  // can handle it.
  while (CurrentToken != tok_eol && CurrentToken != tok_eof)
    getNextToken();
}

static void HandleFunctionDefinition() {
  if (!ParseFunctionDefinition()) {
    DiscardRestOfLine();
    return;
  }

  if (CurrentToken != tok_eol && CurrentToken != tok_eof) {
    LogErrorExpression("Unexpected " + FormatTokenForMessage(CurrentToken));
    DiscardRestOfLine();
    return;
  }

  fprintf(stderr, "Parsed a function definition.\n");
}

static void HandleTopLevelExpression() {
  if (!ParseTopLevelExpression()) {
    DiscardRestOfLine();
    return;
  }

  if (CurrentToken != tok_eol && CurrentToken != tok_eof) {
    LogErrorExpression("Unexpected " + FormatTokenForMessage(CurrentToken));
    DiscardRestOfLine();
    return;
  }

  fprintf(stderr, "Parsed a top-level expression.\n");
}

/// MainLoop - Dispatch loop for the REPL.
///
/// program
///   = [ end-of-lines ]
///     [ top-level-item { end-of-lines top-level-item } ]
///     [ end-of-lines ] ;
///
/// CurrentToken is primed before MainLoop() is called (see main()). After each
/// successful parse the handler prints a confirmation; after a failed parse I
/// synchronize to a line boundary. Either way I come back here and look at the
/// new CurrentToken.
static void MainLoop() {
  while (CurrentToken != tok_eof) {
    switch (CurrentToken) {
    case tok_eol:
      // For a bare newline, I print a fresh prompt and read the next token.
      fprintf(stderr, "ready> ");
      getNextToken();
      break;
    case tok_error:
      DiscardRestOfLine();
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

  MainLoop();

  return 0;
}

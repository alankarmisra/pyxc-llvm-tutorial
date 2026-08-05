#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std;

//===----------------------------------------===//
// Lexer
//===----------------------------------------===//

// The lexer returns one of these named tokens. Characters that do not belong
// to the language are reported as tok_error.
enum Token {
  // input boundaries
  tok_eof = 1,
  tok_eol,

  // lexer errors
  tok_error,

  // function definitions
  tok_def,

  // tokens that need additional information attached
  tok_name,
  tok_number,

  // punctuation and operators
  tok_lparen,
  tok_rparen,
  tok_comma,
  tok_colon,
  tok_plus,
  tok_minus,
  tok_star,
  tok_less,
};

static string Name; // Filled in with the name just read
static double NumberValue;  // Filled in with the number read
static map<string, Token> Keywords = {
    {"def", tok_def},
};

// TokenNames maps each named token to a readable string for debug output and
// error reporting.
static map<int, string> TokenNames = {
    {tok_eof, "end of input"},
    {tok_eol, "newline"},
    {tok_error, "error"},
    {tok_def, "'def'"},
    {tok_name, "name"},
    {tok_number, "number"},
    {tok_lparen, "'('"},
    {tok_rparen, "')'"},
    {tok_comma, "','"},
    {tok_colon, "':'"},
    {tok_plus, "'+'"},
    {tok_minus, "'-'"},
    {tok_star, "'*'"},
    {tok_less, "'<'"},
};

/// advance - returns the next character, coalescing `\r\n` (Windows) into `\n`
/// and converting bare `\r` (Old Macs) into `\n`.
int advance() {
  int LastChar = getchar();
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF) {
      ungetc(NextChar, stdin);
    }
    return '\n';
  }
  return LastChar;
}

/// getToken - Return the next token from standard input.
int getToken() {
  static int LastChar = ' ';

  // Skip whitespace EXCEPT newlines
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // Name
  if (isalpha(LastChar) || LastChar == '_') {
    Name = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_')
      Name += LastChar;

    // LastChar now holds the first character that is not part of this
    // name/keyword.

    // Keyword check.
    auto KeywordIt = Keywords.find(Name);
    if (KeywordIt != Keywords.end())
      return KeywordIt->second;
    return tok_name;
  }

  // Number
  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');
    // LastChar now holds the first character that is not part of this number.

    // TODO: This incorrectly lexes 1.23.45.67 as 1.23
    NumberValue = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  // Comment
  if (LastChar == '#') {
    // Comment until the end of the line
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar != EOF) {
      LastChar = ' ';
      return tok_eol;
    }
  }

  // Newline
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }

  // End of file
  if (LastChar == EOF)
    return tok_eof;

  // Single-character punctuation and operators
  int ThisChar = LastChar;
  LastChar = advance();
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
  case '<':
    return tok_less;
  default:
    return tok_error;
  }
}

//===----------------------------------------===//
// Syntax Tree Nodes
//===----------------------------------------===//

namespace {

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
  int Operator;
  unique_ptr<ExpressionNode> Left, Right;

public:
  BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left,
                       unique_ptr<ExpressionNode> Right)
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {}
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

/// OperatorPrecedence - Maps each binary operator token to its precedence.
/// Higher numbers bind more tightly: '*' (40) > '+'/'-' (20) > '<' (10).
/// Operators not in this map return -1 from GetTokenPrecedence(), which tells
/// ParseBinaryOperatorRight to stop consuming operators and return what it has
/// so far.
static const map<int, int> OperatorPrecedence = {
    {tok_less, 10},
    {tok_plus, 20},
    {tok_minus, 20},
    {tok_star, 40},
};

/// GetTokenPrecedence - Returns the precedence of CurrentToken if it is a known
/// binary operator, or -1 if it is not.
static int GetTokenPrecedence() {
  auto It = OperatorPrecedence.find(CurrentToken);
  if (It == OperatorPrecedence.end() || It->second <= 0)
    return -1;
  return It->second;
}

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// node type, allowing parse functions to return an error directly.
/// TokenNames provides a readable token description. Chapter 4 will add source
/// location (line/column) to these diagnostics.
unique_ptr<ExpressionNode> LogErrorExpression(const char *Str) {
  fprintf(stderr, "Error: %s (token: %s)\nready> ", Str,
          TokenNames.at(CurrentToken).c_str());
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

/// number-expression
///   = number ;
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  auto Result = make_unique<NumberExpressionNode>(NumberValue);
  getNextToken(); // consume the number
  return std::move(Result);
}

/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat '('
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("expected ')'");
  getNextToken(); // eat ')'
  return V;
}

/// name-expression
///   = name
///   | name "(" [ expression { "," expression } ] ")" ;
static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;

  getNextToken(); // eat name.

  if (CurrentToken != tok_lparen) // Simple name, not a call.
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

/// primary
///   = name-expression
///   | number-expression
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  case tok_name:
    return ParseNameExpression(); // handles names like `a` or function calls like `add(...)`
  case tok_number:
    return ParseNumberExpression(); // handles singular numbers like 3.14
  case tok_lparen:
    return ParseParenthesizedExpression(); // handles parenthesized expressions like `(` ... `)`
  default:
    return LogErrorExpression("unknown token when expecting an expression");
  }
}

/// binary-operator-right
///   = { binary-operator primary } ;
static unique_ptr<ExpressionNode>
ParseBinaryOperatorRight(int MinimumPrecedence,
                         unique_ptr<ExpressionNode> Left) {
  // If this is a binary operator, find its precedence.
  while (true) {
    int TokenPrecedence = GetTokenPrecedence();

    // If this binary operator binds at least as tightly as the current
    // expression, consume it; otherwise, I'm done.
    if (TokenPrecedence < MinimumPrecedence)
      return Left;

    // Okay, this is a binary operator.
    int Operator = CurrentToken;
    getNextToken(); // eat binary operator

    // Parse the primary expression after the binary operator.
    auto Right = ParsePrimary();
    if (!Right)
      return nullptr;

    // If Operator binds less tightly with Right than the operator after Right,
    // let the pending operator take Right as its Left.
    int NextTokenPrecedence = GetTokenPrecedence();
    if (TokenPrecedence < NextTokenPrecedence) {
      Right = ParseBinaryOperatorRight(TokenPrecedence + 1, std::move(Right));
      if (!Right)
        return nullptr;
    }

    // Merge Left/Right.
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left),
                                             std::move(Right));
  }
}

/// expression
///   = primary binary-operator-right ;
static unique_ptr<ExpressionNode> ParseExpression() {
  auto Left = ParsePrimary();
  if (!Left)
    return nullptr;

  return ParseBinaryOperatorRight(0, std::move(Left));
}

/// function-signature
///   = name "(" [ name { "," name } ] ")" ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");

  string FnName = Name;
  getNextToken(); // eat function name

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  // Parse parameter names. The loop calls getNextToken() at the top to advance
  // past '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body I call getNextToken() again to move past the name
  // I just stored, then check whether ')' or ',' follows.

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
///   = "def" function-signature ":" [ end-of-lines ] expression ;
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
///   = expression ;
/// A top-level expression (e.g. "1 + 2") is wrapped in an anonymous function
/// so it fits the same FunctionDefinitionNode shape as everything else. When I add JIT
/// execution later, I'll look up "__anon_expr" and call it to get the result.
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  if (auto E = ParseExpression()) {
    auto Signature = make_unique<FunctionSignatureNode>("__anon_expr", vector<string>());
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(E));
  }
  return nullptr;
}

//===----------------------------------------===//
// Top-Level parsing
//===----------------------------------------===//

/// HandleFunctionDefinition/TopLevelExpression - Called by MainLoop when it sees
/// the appropriate leading token. On success, print a confirmation. On failure,
/// skip one token and continue: crude error recovery that keeps the REPL alive
/// after a bad input without getting stuck on the same bad token forever.

static void HandleFunctionDefinition() {
  if (ParseFunctionDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // skip bad token
}

static void HandleTopLevelExpression() {
  if (ParseTopLevelExpression())
    fprintf(stderr, "Parsed a top-level expression.\n");
  else
    getNextToken(); // skip bad token
}

/// MainLoop - Dispatch loop for the REPL.
///
/// Dispatches each top-level-item (function-definition | top-level-expression)
/// to its handler. A bare newline isn't part of that grammar rule; it's REPL
/// bookkeeping: print a fresh prompt and keep going.
///
/// CurrentToken is primed before MainLoop() is called (see main()). After each
/// successful parse the handler prints a confirmation; after a failed parse it
/// skips one token. Either way execution returns here and looks at the new
/// CurrentToken.
static void MainLoop() {
  while (true) {
    if (CurrentToken == tok_eof)
      return;

    // A bare newline: just print a fresh prompt and read the next token.
    if (CurrentToken == tok_eol) {
      fprintf(stderr, "ready> ");
      getNextToken();
      continue;
    }

    switch (CurrentToken) {
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
  // Print the first prompt and load the first token before entering the loop.
  // Every parse function expects CurrentToken to already be loaded when it is called.
  fprintf(stderr, "ready> ");
  getNextToken();

  MainLoop();

  return 0;
}

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std;

static constexpr char AnonymousExpressionFunctionName[] = "__anon_expr";

//===----------------------------------------===//
// Lexer
//===----------------------------------------===//

// I return one of these named tokens from the lexer. I report characters that
// do not belong to the language as tok_error.
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
  tok_slash,
  tok_less,
};

static string Name;        // I store the name I just read.
static double NumberValue; // I store the number I just read.
static map<string, Token> Keywords = {
    {"def", tok_def},
};

// I map each named token to a readable string for debug output and error
// reporting.
static map<int, string> TokenNames = {
    {tok_eof, "end of input"}, {tok_eol, "newline"}, {tok_error, "error"},
    {tok_def, "'def'"},        {tok_name, "name"},   {tok_number, "number"},
    {tok_lparen, "'('"},       {tok_rparen, "')'"},  {tok_comma, "','"},
    {tok_colon, "':'"},        {tok_plus, "'+'"},    {tok_minus, "'-'"},
    {tok_star, "'*'"},         {tok_slash, "'/'"},   {tok_less, "'<'"},
};

/// advance - I return the next character, normalizing `\r\n` (Windows)
/// and bare `\r` (Old Macs) into `\n`.
int advance() {
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
    return '\n';
  }

  // case '\n' or any other non-newline character
  return LastChar;
}

/// getToken - I return the next token from standard input.
int getToken() {
  static int LastChar = ' ';

  // I skip whitespace except newlines.
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // I read a name or keyword.
  if (isalpha(LastChar) || LastChar == '_') {
    string NameLiteral;
    NameLiteral = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_')
      NameLiteral += LastChar;

    // I leave the first character that is not part of this name or keyword in
    // LastChar.

    // I check whether the name is a keyword.
    auto KeywordIt = Keywords.find(NameLiteral);
    if (KeywordIt != Keywords.end())
      return KeywordIt->second;
    // Only a real name updates Name; a keyword never touches it.
    Name = NameLiteral;
    return tok_name;
  }

  // I read a number.
  if (isdigit(LastChar) || LastChar == '.') {
    string NumberLiteral;
    do {
      NumberLiteral += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');
    // I leave the first character that is not part of this number in LastChar.

    // TODO: I consume all of 1.23.45.67 but parse it as 1.23.
    NumberValue = strtod(NumberLiteral.c_str(), 0);
    return tok_number;
  }

  // I discard a comment.
  if (LastChar == '#') {
    // I consume characters through the end of the line.
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar == '\n') {
      LastChar = ' ';
      return tok_eol;
    }
  }

  // I recognize a newline.
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }

  // I recognize the end of the file.
  if (LastChar == EOF)
    return tok_eof;

  // I read single-character punctuation and operators.
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
  case '/':
    return tok_slash;
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

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// node type, allowing parse functions to return an error directly.
/// TokenNames provides a readable token description. Chapter 5 will add source
/// location (line/column) to these diagnostics.
unique_ptr<ExpressionNode> LogErrorExpression(const string &ErrorMessage) {
  fprintf(stderr, "Error: %s (token: %s)\nready> ", ErrorMessage.c_str(),
          TokenNames.at(CurrentToken).c_str());
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
        ("Unexpected " + TokenNames.at(CurrentToken)).c_str());
  }
}

/// term
///   = primary { ("*" | "/") primary } ;
static unique_ptr<ExpressionNode> ParseTerm() {
  // I start the term by parsing one primary.
  auto Left = ParsePrimary();
  if (!Left)
    return nullptr;

  // I consume only the operators that belong to this tier.
  while (CurrentToken == tok_star || CurrentToken == tok_slash) {
    int Operator = CurrentToken;
    getNextToken(); // I eat '*' or '/'.
    auto Right = ParsePrimary();
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
    Left = make_unique<BinaryExpressionNode>(tok_plus, std::move(Left),
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

/// HandleFunctionDefinition/TopLevelExpression - Called by MainLoop when it
/// sees the appropriate leading token. On success, print a confirmation. On
/// failure, skip one token and continue: crude error recovery that keeps the
/// REPL alive after a bad input without getting stuck on the same bad token
/// forever.

static void HandleFunctionDefinition() {
  if (ParseFunctionDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // I skip the bad token.
}

static void HandleTopLevelExpression() {
  if (ParseTopLevelExpression())
    fprintf(stderr, "Parsed a top-level expression.\n");
  else
    getNextToken(); // I skip the bad token.
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
  while (CurrentToken != tok_eof) {
    switch (CurrentToken) {
    case tok_eol:
      // For a bare newline, I print a fresh prompt and read the next token.
      fprintf(stderr, "ready> ");
      getNextToken();
      break;
    case tok_error:
      LogErrorExpression("invalid character");
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

  MainLoop();

  return 0;
}

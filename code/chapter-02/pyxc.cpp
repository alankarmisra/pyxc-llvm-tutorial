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

  // control flow
  tok_return,

  // punctuation and operators
  tok_lparen,
  tok_rparen,
  tok_comma,
  tok_colon,
  tok_plus,
};

static string NameStr; // Filled in with the name just read
static double NumVal;  // Filled in with the number read
static map<string, Token> Keywords = {
    {"def", tok_def},
    {"return", tok_return},
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
    {tok_return, "'return'"},
    {tok_lparen, "'('"},
    {tok_rparen, "')'"},
    {tok_comma, "','"},
    {tok_colon, "':'"},
    {tok_plus, "'+'"},
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

/// gettok - Return the next token from standard input.
int gettok() {
  static int LastChar = ' ';

  // Skip whitespace EXCEPT newlines
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // Name
  if (isalpha(LastChar) || LastChar == '_') {
    NameStr = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_')
      NameStr += LastChar;

    // LastChar now holds the first character that is not part of this
    // name/keyword.

    // Keyword check.
    auto KeywordIt = Keywords.find(NameStr);
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
    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  // Comment
  if (LastChar == '#') {
    // Comment until the end of the line
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar != EOF) {
      LastChar = advance();
      return tok_eol;
    }
  }

  // Newline
  if (LastChar == '\n') {
    LastChar = advance();
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
  double Val;

public:
  NumberExpressionNode(double Val) : Val(Val) {}
};

/// NameExpressionNode - Expression class for referencing a variable, like "a".
class NameExpressionNode : public ExpressionNode {
  string Name;

public:
  NameExpressionNode(const string &Name) : Name(Name) {}
};

/// BinaryExpressionNode - Expression class for a binary operator.
class BinaryExpressionNode : public ExpressionNode {
  int Op;
  unique_ptr<ExpressionNode> LHS, RHS;

public:
  BinaryExpressionNode(int Op, unique_ptr<ExpressionNode> LHS, unique_ptr<ExpressionNode> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

/// CallExpressionNode - Expression class for function calls.
class CallExpressionNode : public ExpressionNode {
  string Callee;
  vector<unique_ptr<ExpressionNode>> Args;

public:
  CallExpressionNode(const string &Callee, vector<unique_ptr<ExpressionNode>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};

/// FunctionSignatureNode - This class represents the "function signature" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
class FunctionSignatureNode {
  string Name;
  vector<string> Args;

public:
  FunctionSignatureNode(const string &Name, vector<string> Args)
      : Name(Name), Args(std::move(Args)) {}

  const string &getName() const { return Name; }
};

/// FunctionDefinitionNode - This class represents a function definition itself.
class FunctionDefinitionNode {
  unique_ptr<FunctionSignatureNode> Signature;
  unique_ptr<ExpressionNode> Body;

public:
  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature, unique_ptr<ExpressionNode> Body)
      : Signature(std::move(Signature)), Body(std::move(Body)) {}
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

/// consumeNewlines - Consume all consecutive tok_eol tokens.
///
/// Called after eating a structural token (e.g. ':') to allow the body or
/// next clause to appear on the following line.
static void consumeNewlines() {
  while (CurTok == tok_eol)
    getNextToken();
}

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// node type, allowing parse functions to return an error directly.
/// TokenNames provides a readable token description. Chapter 3 will add source
/// location (line/column) to these diagnostics.
unique_ptr<ExpressionNode> LogErrorExpression(const char *Str) {
  fprintf(stderr, "Error: %s (token: %s)\nready> ", Str,
          TokenNames.at(CurTok).c_str());
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
  auto Result = make_unique<NumberExpressionNode>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}

/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != tok_rparen)
    return LogErrorExpression("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// name-expression
///   = name
///   | name "("[expression{"," expression}]")" ;
static unique_ptr<ExpressionNode> ParseNameExpression() {
  string Name = NameStr;

  getNextToken(); // eat name.

  if (CurTok != tok_lparen) // Simple variable ref.
    return make_unique<NameExpressionNode>(Name);

  // Call.
  getNextToken(); // eat (
  vector<unique_ptr<ExpressionNode>> Args;
  if (CurTok != tok_rparen) {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == tok_rparen)
        break;

      if (CurTok != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return make_unique<CallExpressionNode>(Name, std::move(Args));
}

/// primary
///   = name-expression
///   | number-expression
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogErrorExpression("unknown token when expecting an expression");
  case tok_name:
    return ParseNameExpression();
  case tok_number:
    return ParseNumberExpression();
  case tok_lparen:
    return ParseParenthesizedExpression();
  }
}

/// expression
///   = primary { "+" primary } ;
static unique_ptr<ExpressionNode> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  while (CurTok == tok_plus) {
    getNextToken(); // eat '+'
    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;
    LHS = make_unique<BinaryExpressionNode>(tok_plus, std::move(LHS),
                                     std::move(RHS));
  }

  return LHS;
}

/// function-signature
///   = name "(" [name {"," name}] ")" ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  if (CurTok != tok_name)
    return LogErrorSignature("Expected function name in function signature");

  string FnName = NameStr;
  getNextToken(); // eat function name

  if (CurTok != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  // Parse argument names. The loop calls getNextToken() at the top to advance
  // past '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body we call getNextToken() again to move past the name
  // we just stored, then check whether ')' or ',' follows.

  vector<string> ArgNames;
  while (getNextToken() == tok_name) {
    ArgNames.push_back(NameStr);

    if (getNextToken() == tok_rparen) // eat name, check what follows
      break;

    if (CurTok != tok_comma)
      return LogErrorSignature("Expected ')' or ',' in parameter list");
    // loop continues: getNextToken() at the top eats the ','
  }

  if (CurTok != tok_rparen)
    return LogErrorSignature("Expected ')' in function signature");

  getNextToken(); // eat ')'

  return make_unique<FunctionSignatureNode>(FnName, std::move(ArgNames));
}

/// function-definition
///   = "def" function-signature ":" [ end-of-lines ] "return" expression ;
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // eat 'def'
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;

  if (CurTok != tok_colon)
    return LogErrorFunction("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // Skip any newlines between ':' and 'return'. This allows the body to be
  // written on the next line:
  //   def foo(x):
  //     return x + 1
  consumeNewlines();

  if (CurTok != tok_return)
    return LogErrorFunction("Expected 'return' in function body");
  getNextToken(); // eat 'return'

  if (auto E = ParseExpression())
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(E));
  return nullptr;
}

/// top-level-expression
///   = expression
/// A top-level expression (e.g. "1 + 2") is wrapped in an anonymous function
/// so it fits the same FunctionDefinitionNode shape as everything else. When we add JIT
/// execution later, we'll look up "__anon_expr" and call it to get the result.
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
/// skip one token and continue — crude error recovery that keeps the REPL alive
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
/// grammar: top = { definition | expression | newline }
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
      fprintf(stderr, "ready> ");
      getNextToken();
      continue;
    }

    switch (CurTok) {
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
  // Every parse function expects CurTok to already be loaded when it is called.
  fprintf(stderr, "ready> ");
  getNextToken();

  MainLoop();

  return 0;
}

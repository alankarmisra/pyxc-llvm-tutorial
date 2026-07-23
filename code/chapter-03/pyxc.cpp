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
  tok_minus,
  tok_star,
  tok_less,
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
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------===//

namespace {

/// ExprAST - Base class for all expression nodes.
class ExprAST {
public:
  virtual ~ExprAST() = default;
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
};

/// NameExprAST - Expression class for referencing a variable, like "a".
class NameExprAST : public ExprAST {
  string Name;

public:
  NameExprAST(const string &Name) : Name(Name) {}
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  int Op;
  unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(int Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const string &Callee, vector<unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};

/// FunctionSignatureAST - This class represents the "function signature" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
class FunctionSignatureAST {
  string Name;
  vector<string> Args;

public:
  FunctionSignatureAST(const string &Name, vector<string> Args)
      : Name(Name), Args(std::move(Args)) {}

  const string &getName() const { return Name; }
};

/// FunctionDefAST - This class represents a function definition itself.
class FunctionDefAST {
  unique_ptr<FunctionSignatureAST> Signature;
  unique_ptr<ExprAST> Body;

public:
  FunctionDefAST(unique_ptr<FunctionSignatureAST> Signature, unique_ptr<ExprAST> Body)
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

/// BinopPrecedence - Maps each binary operator token to its precedence.
/// Higher numbers bind more tightly: '*' (40) > '+'/'-' (20) > '<' (10).
/// Operators not in this map return -1 from GetTokPrecedence(), which tells
/// ParseBinOpRHS to stop consuming operators and return what it has so far.
static const map<int, int> BinopPrecedence = {
    {tok_less, 10},
    {tok_plus, 20},
    {tok_minus, 20},
    {tok_star, 40},
};

/// GetTokPrecedence - Returns the precedence of CurTok if it is a known binary
/// operator, or -1 if it is not.
static int GetTokPrecedence() {
  auto It = BinopPrecedence.find(CurTok);
  if (It == BinopPrecedence.end() || It->second <= 0)
    return -1;
  return It->second;
}

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogError("message");
/// TokenNames provides a readable token description. Chapter 3 will add source
/// location (line/column) to these diagnostics.
unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s (token: %s)\nready> ", Str,
          TokenNames.at(CurTok).c_str());
  return nullptr;
}
unique_ptr<FunctionSignatureAST> LogErrorSignature(const char *Str) {
  LogError(Str);
  return nullptr;
}
unique_ptr<FunctionDefAST> LogErrorF(const char *Str) {
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

  if (CurTok != tok_rparen)
    return LogError("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// nameexpr
///   = name
///   | name "("[expression{"," expression}]")" ;
static unique_ptr<ExprAST> ParseNameExpr() {
  string Name = NameStr;

  getNextToken(); // eat name.

  if (CurTok != tok_lparen) // Simple variable ref.
    return make_unique<NameExprAST>(Name);

  // Call.
  getNextToken(); // eat (
  vector<unique_ptr<ExprAST>> Args;
  if (CurTok != tok_rparen) {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == tok_rparen)
        break;

      if (CurTok != tok_comma)
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return make_unique<CallExprAST>(Name, std::move(Args));
}

/// primary
///   = nameexpr
///   | numberexpr
///   | parenexpr ;
static unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError("unknown token when expecting an expression");
  case tok_name:
    return ParseNameExpr();
  case tok_number:
    return ParseNumberExpr();
  case tok_lparen:
    return ParseParenExpr();
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

/// functionsignature
///   = name "(" [name {"," name}] ")" ;
static unique_ptr<FunctionSignatureAST> ParseFunctionSignature() {
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

  return make_unique<FunctionSignatureAST>(FnName, std::move(ArgNames));
}

/// definition
///   = "def" function signature ":" ["newline"] "return" expression ;
static unique_ptr<FunctionDefAST> ParseFunctionDef() {
  getNextToken(); // eat 'def'
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;

  if (CurTok != tok_colon)
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // Skip any newlines between ':' and 'return'. This allows the body to be
  // written on the next line:
  //   def foo(x):
  //     return x + 1
  consumeNewlines();

  if (CurTok != tok_return)
    return LogErrorF("Expected 'return' in function body");
  getNextToken(); // eat 'return'

  if (auto E = ParseExpression())
    return make_unique<FunctionDefAST>(std::move(Signature), std::move(E));
  return nullptr;
}

/// toplevelexpr
///   = expression
/// A top-level expression (e.g. "1 + 2") is wrapped in an anonymous function
/// so it fits the same FunctionDefAST shape as everything else. When we add JIT
/// execution later, we'll look up "__anon_expr" and call it to get the result.
static unique_ptr<FunctionDefAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Signature = make_unique<FunctionSignatureAST>("__anon_expr", vector<string>());
    return make_unique<FunctionDefAST>(std::move(Signature), std::move(E));
  }
  return nullptr;
}

//===----------------------------------------===//
// Top-Level parsing
//===----------------------------------------===//

/// HandleFunctionDef/TopLevelExpression - Called by MainLoop when it sees
/// the appropriate leading token. On success, print a confirmation. On failure,
/// skip one token and continue — crude error recovery that keeps the REPL alive
/// after a bad input without getting stuck on the same bad token forever.

static void HandleFunctionDef() {
  if (ParseFunctionDef())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // skip bad token
}

static void HandleTopLevelExpression() {
  if (ParseTopLevelExpr())
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
      HandleFunctionDef();
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

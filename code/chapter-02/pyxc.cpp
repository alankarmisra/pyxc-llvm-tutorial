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

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,
  tok_eol = -2,

  // commands
  tok_def = -3,
  tok_extern = -4,

  // primary
  tok_identifier = -5,
  tok_number = -6,

  // control
  tok_return
};

static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number

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

  // Check for newline.
  if (LastChar == '\n') {
    // Don't try and read the next character. This will stall the REPL.
    // Just reset LastChar to a space which will force a new character
    // advance in the next call.
    LastChar = ' ';
    return tok_eol;
  }
  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_') {
      IdentifierStr += LastChar;
    }

    // TODO: Push this into a map
    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    if (IdentifierStr == "return")
      return tok_return;

    return tok_identifier;
  }

  // TODO: This incorrectly lexes 1.23.45.67 as 1.23
  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  if (LastChar == '#') {
    // Comment until the end of the line
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar != EOF) {
      // Don't attempt to read another character at the end of the line,
      // otherwise the REPL will stall waiting for another character.
      LastChar = ' ';
      return tok_eol;
    }
  }

  // Otherwise, just return the character as its ascii value
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//

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

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  string Name;

public:
  VariableExprAST(const string &Name) : Name(Name) {}
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
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

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
class PrototypeAST {
  string Name;
  vector<string> Args;

public:
  PrototypeAST(const string &Name, vector<string> Args)
      : Name(Name), Args(std::move(Args)) {}

  const string &getName() const { return Name; }
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;

public:
  FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok is the current token the parser is looking at.
/// getNextToken reads the next token from the lexer and stores it in CurTok.
/// Every parse function assumes CurTok is already loaded before it is called,
/// and leaves CurTok pointing at the first token it did not consume.
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

/// BinopPrecedence - Maps each binary operator character to its precedence.
/// Higher numbers bind more tightly: '*' (40) > '+'/'-' (20) > '<' (10).
/// Operators not in this map return -1 from GetTokPrecedence(), which tells
/// ParseBinOpRHS to stop consuming operators and return what it has so far.
static map<char, int> BinopPrecedence;

/// GetTokPrecedence - Returns the precedence of CurTok if it is a known binary
/// operator, or -1 if it is not. Non-ASCII tokens (our named token enums) are
/// rejected immediately since they can never be binary operators here.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogError("message");
/// We print the raw CurTok number for now. Chapter 3 will replace this with
/// readable token names and source location (line/column).
unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "ready> Error: %s (token: %d)\nready> ", Str, CurTok);
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

  if (CurTok != ')')
    return LogError("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// identifierexpr
///   = identifier
///   | identifier "("[expression{"," expression}]")" ;
static unique_ptr<ExprAST> ParseIdentifierExpr() {
  string IdName = IdentifierStr;

  getNextToken(); // eat identifier.

  if (CurTok != '(') // Simple variable ref.
    return make_unique<VariableExprAST>(IdName);

  // Call.
  getNextToken(); // eat (
  vector<unique_ptr<ExprAST>> Args;
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

  return make_unique<CallExprAST>(IdName, std::move(Args));
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

/// prototype
///   = identifier "(" [identifier {"," identifier}] ")" ;
static unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");

  string FnName = IdentifierStr;
  getNextToken(); // eat function name

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");

  // Parse argument names. The loop calls getNextToken() at the top to advance
  // past '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body we call getNextToken() again to move past the identifier
  // we just stored, then check whether ')' or ',' follows.
  vector<string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);

    if (getNextToken() == ')') // eat identifier, check what follows
      break;

    if (CurTok != ',')
      return LogErrorP("Expected ')' or ',' in parameter list");
    // loop continues: getNextToken() at the top eats the ','
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype");

  getNextToken(); // eat ')'

  return make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition
///   = "def" prototype ":" ["newline"] "return" expression ;
static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // Skip any newlines between ':' and 'return'. This allows the body to be
  // written on the next line:
  //   def foo(x):
  //     return x + 1
  while (CurTok == tok_eol)
    getNextToken();

  if (CurTok != tok_return)
    return LogErrorF("Expected 'return' in function body");
  getNextToken(); // eat 'return'

  if (auto E = ParseExpression())
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

/// toplevelexpr
///   = expression
/// A top-level expression (e.g. "1 + 2") is wrapped in an anonymous function
/// so it fits the same FunctionAST shape as everything else. When we add JIT
/// execution later, we'll look up "__anon_expr" and call it to get the result.
static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = make_unique<PrototypeAST>("__anon_expr", vector<string>());
    return make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

/// external
///   = "extern" "def" prototype
static unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurTok != tok_def)
    return LogErrorP("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParsePrototype();
}

//===----------------------------------------------------------------------===//
// Top-Level parsing
//===----------------------------------------------------------------------===//

/// HandleDefinition/Extern/TopLevelExpression - Called by MainLoop when it sees
/// the appropriate leading token. On success, print a confirmation. On failure,
/// skip one token and continue — crude error recovery that keeps the REPL alive
/// after a bad input without getting stuck on the same bad token forever.

static void HandleDefinition() {
  if (ParseDefinition())
    fprintf(stderr, "Parsed a function definition.\n");
  else
    getNextToken(); // skip bad token
}

static void HandleExtern() {
  if (ParseExtern())
    fprintf(stderr, "Parsed an extern.\n");
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
/// grammar: top = { definition | external | expression | newline }
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
      HandleDefinition();
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

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
  // Register binary operators and their precedence (higher = tighter binding).
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  // Print the first prompt and load the first token before entering the loop.
  // Every parse function expects CurTok to already be loaded when it is called.
  fprintf(stderr, "ready> ");
  getNextToken();

  MainLoop();

  return 0;
}
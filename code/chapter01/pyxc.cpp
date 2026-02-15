#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] for single-character tokens, otherwise one
// of these for known multi-character/token-class values.
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
  tok_return = -7
};

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static std::map<int, std::string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  std::map<int, std::string> Names = {
      {tok_eof, "tok_eof"},
      {tok_eol, "tok_eol"},
      {tok_def, "tok_def"},
      {tok_extern, "tok_extern"},
      {tok_identifier, "tok_identifier"},
      {tok_number, "tok_number"},
      {tok_return, "tok_return"},
  };

  // Single character tokens.
  for (int C = 0; C <= 255; ++C) {
    if (std::isprint(static_cast<unsigned char>(C)))
      Names[C] = "tok_char, '" + std::string(1, static_cast<char>(C)) + "'";
    else
      Names[C] = "tok_char, " + std::to_string(C);
  }

  return Names;
}();

struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

static int advance() {
  int LastChar = std::getchar();
  if (LastChar == '\r') {
    int NextChar = std::getchar();
    if (NextChar != '\n' && NextChar != EOF)
      std::ungetc(NextChar, stdin);
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }
  if (LastChar == '\n') {
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    LexLoc.Col++;
  }
  return LastChar;
}

/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';

  // Skip whitespace EXCEPT newlines
  while (std::isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // Return end-of-line token.
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }

  CurLoc = LexLoc;

  if (std::isalpha(LastChar) ||
      LastChar == '_') { // identifier: [a-zA-Z_][a-zA-Z0-9_]*
    IdentifierStr = LastChar;
    while (std::isalnum((LastChar = advance())) || LastChar == '_')
      IdentifierStr += LastChar;

    // Is this a known keyword? If yes, return that.
    // Else it is an ordinary identifier.
    const auto It = Keywords.find(IdentifierStr);
    return (It != Keywords.end()) ? It->second : tok_identifier;
  }

  if (std::isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (std::isdigit(LastChar) || LastChar == '.');

    NumVal = std::strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF)
      return tok_eol;
  }

  // Check for end of file. Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

static const std::string &GetTokenName(int Tok) {
  const auto It = TokenNames.find(Tok);
  if (It != TokenNames.end())
    return It->second;

  static const std::string Unknown = "tok_unknown";
  return Unknown;
}

/// A rudimentary REPL. Type things in, see them convert to tokens.
void MainLoop() {
  fprintf(stderr, "ready> ");
  while (true) {
    const int Tok = gettok();
    if (Tok == tok_eof)
      break;

    std::printf("<%s>", GetTokenName(Tok).c_str());

    if (Tok == tok_eol)
      std::printf("\nready> ");
  }
}

int main() {
  MainLoop();
  return 0;
}

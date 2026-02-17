#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

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

static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static map<string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  map<int, string> Names = {
      {tok_eof, "end of input"},
      {tok_eol, "newline"},
      {tok_def, "'def'"},
      {tok_extern, "'extern'"},
      {tok_identifier, "identifier"},
      {tok_number, "number"},
      {tok_return, "'return'"},
  };

  // Single character tokens.
  for (int C = 0; C <= 255; ++C) {
    if (isprint(static_cast<unsigned char>(C)))
      Names[C] = "'" + string(1, static_cast<char>(C)) + "'";
    else if (C == '\n')
      Names[C] = "'\\n'";
    else if (C == '\t')
      Names[C] = "'\\t'";
    else if (C == '\r')
      Names[C] = "'\\r'";
    else if (C == '\0')
      Names[C] = "'\\0'";
    else {
      ostringstream OS;
      OS << "0x" << uppercase << hex << setw(2) << setfill('0') << C;
      Names[C] = OS.str();
    }
  }

  return Names;
}();

struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

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
    if (Index == CompletedLines.size())
      return &CurrentLine;
    return nullptr;
  }
};

static SourceManager SourceMgr;

static int advance() {
  int LastChar = getchar();
  if (LastChar == '\r') {
    int NextChar = getchar();
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, stdin);
    SourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }
  if (LastChar == '\n') {
    SourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    SourceMgr.onChar(LastChar);
    LexLoc.Col++;
  }
  return LastChar;
}

/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';

  // Skip whitespace EXCEPT newlines
  while (isspace(LastChar) && LastChar != '\n')
    LastChar = advance();

  // Return end-of-line token.
  if (LastChar == '\n') {
    LastChar = ' ';
    return tok_eol;
  }

  CurLoc = LexLoc;

  if (isalpha(LastChar) ||
      LastChar == '_') { // identifier: [a-zA-Z_][a-zA-Z0-9_]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())) || LastChar == '_')
      IdentifierStr += LastChar;

    // Is this a known keyword? If yes, return that.
    // Else it is an ordinary identifier.
    const auto It = Keywords.find(IdentifierStr);
    return (It != Keywords.end()) ? It->second : tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n');

    if (LastChar != EOF) {
      LastChar = ' ';
      return tok_eol;
    }
  }

  // Check for end of file. Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  // This includes operator tokens like <, +, -, *, /, %.
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

static const string &GetTokenName(int Tok) {
  const auto It = TokenNames.find(Tok);
  if (It != TokenNames.end())
    return It->second;

  static const string Unknown = "tok_unknown";
  return Unknown;
}

/// A rudimentary REPL. Type things in, see them convert to tokens.
void MainLoop() {
  fprintf(stderr, "ready> ");
  while (true) {
    const int Tok = gettok();
    if (Tok == tok_eof)
      break;

    printf("%s", GetTokenName(Tok).c_str());

    if (Tok == tok_eol)
      printf("\nready> ");
    else
      printf(" ");
  }
}

int main() {
  SourceMgr.reset();
  MainLoop();
  return 0;
}

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

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
// Driver
//===----------------------------------------===//

int main() {
  int tok;
  while ((tok = getToken()) != tok_eof) {
    if (tok == tok_name)
      fprintf(stdout, "%s: %s\n", TokenNames.at(tok).c_str(),
              Name.c_str());
    else if (tok == tok_number)
      fprintf(stdout, "%s: %g\n", TokenNames.at(tok).c_str(), NumberValue);
    else
      fprintf(stdout, "%s\n", TokenNames.at(tok).c_str());
  }
  return 0;
}

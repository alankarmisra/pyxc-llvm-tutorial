#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace std;

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
    {tok_colon, "':'"},        {tok_plus, "'+'"},
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
    Name = LastChar;
    while (isalnum(LastChar = advance()) || LastChar == '_')
      Name += LastChar;
    // I leave the first character that is not part of this name or keyword in
    // LastChar.

    // I check whether the name is a keyword.
    auto KeywordIt = Keywords.find(Name);
    if (KeywordIt != Keywords.end())
      return KeywordIt->second;
    return tok_name;
  }

  // I read a number.
  if (isdigit(LastChar) || LastChar == '.') {
    string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');
    // I leave the first character that is not part of this number in LastChar.

    // TODO: I consume all of 1.23.45.67 but parse it as 1.23.
    NumberValue = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  // I discard a comment.
  if (LastChar == '#') {
    // I consume characters through the end of the line.
    do {
      LastChar = advance();
    } while (LastChar != '\n' && LastChar != EOF);

    if (LastChar != EOF) {
      LastChar = advance();
      return tok_eol;
    }
  }

  // I recognize a newline.
  if (LastChar == '\n') {
    LastChar = advance();
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
  default:
    return tok_error;
  }
}

//===----------------------------------------===//
// Driver
//===----------------------------------------===//

int main() {
  int Token;
  while ((Token = getToken()) != tok_eof) {
    if (Token == tok_name)
      fprintf(stdout, "%s: %s\n", TokenNames.at(Token).c_str(), Name.c_str());
    else if (Token == tok_number)
      fprintf(stdout, "%s: %g\n", TokenNames.at(Token).c_str(), NumberValue);
    else
      fprintf(stdout, "%s\n", TokenNames.at(Token).c_str());
  }
  return 0;
}

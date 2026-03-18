#include <cctype>
#include <cstdio>
#include <map>
#include <string>

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
  tok_return = -7
};

static string IdentifierStr; // Filled in if tok_identifier
static double NumVal;        // Filled in if tok_number

// TokenNames maps each named token to a readable string for debug output. In
// Chapter 3, this map is expanded to cover single-character tokens too, with
// friendlier names for error messages.
static map<int, string> TokenNames = {
    {tok_eof, "tok_eof"},
    {tok_eol, "tok_eol"},
    {tok_def, "tok_def"},
    {tok_extern, "tok_extern"},
    {tok_identifier, "tok_identifier"},
    {tok_number, "tok_number"},
    {tok_return, "tok_return"},
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

  // Check for newline.
  if (LastChar == '\n') {
    // Don't try and read the next character. This will stall the REPL.
    // Just reset LastChar to a space which will force a new character
    // advance in the next call.
    LastChar = ' ';
    return tok_eol;
  }

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

  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

//===----------------------------------------===//
// Driver
//===----------------------------------------===//

int main() {
  int tok;
  while ((tok = gettok()) != tok_eof) {
    if (tok == tok_identifier)
      fprintf(stdout, "tok_identifier: %s\n", IdentifierStr.c_str());
    else if (tok == tok_number)
      fprintf(stdout, "tok_number: %g\n", NumVal);
    else if (tok < 0)
      fprintf(stdout, "%s\n", TokenNames[tok].c_str());
    else
      fprintf(stdout, "'%c'\n", (char)tok);
  }
  return 0;
}

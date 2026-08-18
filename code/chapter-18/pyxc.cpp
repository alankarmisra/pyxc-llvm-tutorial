#include "../include/PyxcJIT.h"
#include "lld/Common/Driver.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace std;
using namespace llvm;
using namespace llvm::orc;

// Forward-declare lld::elf::link, lld::coff::link, lld::macho::link.
LLD_HAS_DRIVER(elf)
LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(macho)

//===----------------------------------------===//
// Command line
//===----------------------------------------===//
static cl::OptionCategory PyxcCategory("Pyxc options");

// Optional positional inputs: 0 args => REPL, 1+ args => file mode.
static cl::list<std::string> InputFiles(cl::Positional, cl::desc("[inputs]"),
                                        cl::ZeroOrMore, cl::cat(PyxcCategory));

// Dump IR to stderr in JIT modes.
static cl::opt<bool> DumpIR("dump-ir",
                            cl::desc("Print generated LLVM IR to stderr"),
                            cl::init(false), cl::cat(PyxcCategory));
// Alias for --dump-ir (kept for backwards compatibility).
static cl::opt<bool> VerboseIR("v", cl::desc("Alias for --dump-ir"),
                               cl::init(false), cl::cat(PyxcCategory));

// Emit output file in file mode.
static cl::opt<std::string>
    EmitKindOpt("emit", cl::desc("Emit output: llvm-ir | asm | obj | exe"),
                cl::init(""), cl::cat(PyxcCategory));
static cl::opt<std::string> OutputFile("o", cl::desc("Output filename"),
                                       cl::value_desc("filename"), cl::init(""),
                                       cl::cat(PyxcCategory));

// Optimization level.
static cl::opt<unsigned> OptLevel("O", cl::desc("Optimization level"),
                                  cl::value_desc("0|1|2|3"), cl::Prefix,
                                  cl::init(2), cl::cat(PyxcCategory));

static FILE *Input = stdin;
static bool IsRepl = true;

enum class EmitKind { None, LLVMIR, Assembly, Object, Executable };
static EmitKind EmitMode = EmitKind::None;
static string EmitOutputPath;

static bool ShouldDumpIR() { return DumpIR || VerboseIR; }
static bool IsEmitMode() { return EmitMode != EmitKind::None; }

//===----------------------------------------===//
// Lexer
//===----------------------------------------===//

// I return named tokens for known language elements. I preserve the [0-255]
// character value of any other single character for diagnostics.
enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  // commands
  tok_def = -4,
  tok_extern = -5,

  // primary
  tok_name = -6,
  tok_number = -7,

  // comparison operators
  tok_eq = -8,     // ==
  tok_neq = -9,    // !=
  tok_leq = -10,   // <=
  tok_geq = -11,   // >=
  tok_arrow = -12, // ->

  // control
  tok_if = -13,
  tok_else = -14,
  tok_return = -15,

  // loops
  tok_for = -16,


  // mutable variables
  tok_var = -19,

  // types
  tok_int = -20,

  // indentation
  tok_indent = -21,
  tok_dedent = -22,
  tok_block_end = -100, // synthetic: injected by ParseBlock after eating DEDENT

  // new type keywords
  tok_int8 = -23,
  tok_int16 = -24,
  tok_int32 = -25,
  tok_int64 = -26,
  tok_float = -27,
  tok_float32 = -28,
  tok_float64 = -29,
  tok_bool = -30,
  tok_none = -31,
  tok_true = -32,
  tok_false = -33,
  tok_elif = -34,
  tok_while = -35,
  tok_do = -36,
  tok_break = -37,
  tok_continue = -38,

  // punctuation and operators
  tok_lparen = '(',
  tok_rparen = ')',
  tok_comma = ',',
  tok_colon = ':',
  tok_plus = '+',
  tok_minus = '-',
  tok_star = '*',
  tok_slash = '/',
  tok_percent = '%',
  tok_less = '<',
  tok_greater = '>',
  tok_equal = '=',
};

enum class ValueType {
  None,
  Int, /* depends on system default for int */
  Int8,
  Int16,
  Int32,
  Int64,
  Float,
  Float32,
  Float64,
  Bool,
  Error
};

static string Name;    // Filled in if tok_name
static string NumberLiteral;    // Raw number literal text (no sign)
static bool NumberIsFloat = false; // True if the literal contains '.' or e/E.
static int LexerLastChar =
    ' '; // Last character read by the lexer (used for lookahead and whitespace
// handling).
static vector<int> IndentStack = {
    0}; // Stack of indentation levels (0 is the base indent).
static deque<int> PendingTokens; // Queue of synthetic tokens
                                 // (INDENT/DEDENT/EOL) produced by the lexer.
static bool AtLineStart =
    true; // True when the lexer is positioned at the start of a new line.

// Keywords like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static map<string, Token> Keywords = {
    {"def", tok_def},         {"extern", tok_extern},   {"return", tok_return},
    {"if", tok_if},           {"elif", tok_elif},       {"else", tok_else},
    {"for", tok_for},         {"while", tok_while},     {"do", tok_do},
    {"break", tok_break},     {"continue", tok_continue},
    {"var", tok_var},
    {"int", tok_int},         {"int8", tok_int8},       {"int16", tok_int16},
    {"int32", tok_int32},     {"int64", tok_int64},     {"float", tok_float},
    {"float32", tok_float32}, {"float64", tok_float64}, {"bool", tok_bool},
    {"None", tok_none},       {"True", tok_true},       {"False", tok_false}};
static constexpr int IndentTabWidth = 8;

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  static map<int, string> Names = {
      {tok_eof, "end of input"},  {tok_eol, "newline"},
      {tok_error, "error"},       {tok_def, "'def'"},
      {tok_extern, "'extern'"},   {tok_name, "name"},
      {tok_number, "number"},     {tok_return, "'return'"},
      {tok_eq, "'=='"},           {tok_neq, "'!='"},
      {tok_leq, "'<='"},          {tok_geq, "'>='"},
      {tok_arrow, "'->'"},        {tok_if, "'if'"},
      {tok_else, "'else'"},       {tok_for, "'for'"},
      {tok_var, "'var'"},         {tok_int, "'int'"},
      {tok_int8, "'int8'"},       {tok_int16, "'int16'"},
      {tok_int32, "'int32'"},     {tok_int64, "'int64'"},
      {tok_float, "'float'"},     {tok_float32, "'float32'"},
      {tok_float64, "'float64'"}, {tok_bool, "'bool'"},
      {tok_none, "'None'"},       {tok_true, "'True'"},
      {tok_false, "'False'"},     {tok_indent, "indent"},
      {tok_elif, "'elif'"},       {tok_while, "'while'"},
      {tok_do, "'do'"},           {tok_break, "'break'"},
      {tok_continue, "'continue'"},
      {tok_dedent, "dedent"},
                                   {tok_block_end, "block-end"}};

  // Single character tokens.
  for (int ch = 0; ch <= 255; ++ch) {
    if (isprint(static_cast<unsigned char>(ch)))
      Names[ch] = "'" + string(1, static_cast<char>(ch)) + "'";
    else if (ch == '\n')
      Names[ch] = "'\\n'";
    else if (ch == '\t')
      Names[ch] = "'\\t'";
    else if (ch == '\r')
      Names[ch] = "'\\r'";
    else if (ch == '\0')
      Names[ch] = "'\\0'";
    else {
      ostringstream OS;
      OS << "0x" << uppercase << hex << setw(2) << setfill('0') << ch;
      Names[ch] = OS.str();
    }
  }

  return Names;
}();

/// SourceLocation - A {Line, Column} pair. Line and Column are 1-based.
///
/// Two globals track position as characters are consumed:
///   LexerLocation  - where the character-read head (advance()) currently is.
///             Updated on every advance() call. After a '\n', Line increments
///             and Column resets to 0 so the next character will be Column 1.
///   CurrentTokenLocation  - snapshotted at the start of each token in getToken(), before
///             consuming any of the token's characters. This is the position
///             the parser and diagnostics see.
struct SourceLocation {
  int Line;
  int Column;
};
static SourceLocation CurrentTokenLocation;
static SourceLocation LexerLocation = {1, 0};
static void LogErrorAtLoc(const string &ErrorMessage, SourceLocation Loc);
static void LogInvalidNumberLiteralAtLocation(const string &Literal, SourceLocation Location);

/// SourceManager - Buffers every source line as it is read so that error
/// messages can reprint the offending line with a caret underneath it.
///
/// advance() calls onChar() for every character it consumes. When a '\n'
/// arrives, the just-completed line is moved into CompletedLines and
/// CurrentLine starts fresh. getLine(N) returns a pointer to the Nth line
/// (1-based): completed lines are stored in the vector; the line currently
/// being assembled is in CurrentLine.
class SourceManager {
  vector<string> CompletedLines;
  string CurrentLine;

public:
  /// reset - Clear all buffered source lines.
  ///
  /// Used when starting a new input stream so diagnostics only reference the
  /// current script/session content.
  void reset() {
    CompletedLines.clear();
    CurrentLine.clear();
  }

  /// onChar - Feed one consumed character into the source buffer.
  ///
  /// Preconditions:
  /// - Must be called for every character consumed by advance().
  /// - '\n' terminates the current line; EOF is ignored.
  void onChar(int C) {
    if (C == '\n') {
      CompletedLines.push_back(CurrentLine);
      CurrentLine.clear();
      return;
    }
    if (C != EOF)
      CurrentLine.push_back(static_cast<char>(C));
  }

  /// getLine - Return a pointer to a buffered source line by 1-based index.
  ///
  /// Completed lines come from CompletedLines; the in-progress line is
  /// CurrentLine when OneBasedLine == CompletedLines.size() + 1.
  ///
  /// Preconditions:
  /// - OneBasedLine is 1-based. Non-positive indices return nullptr.
  ///
  /// Note:
  /// - Do not retain the returned pointer across advance()/onChar() calls;
  ///   buffers may reallocate.
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

static SourceManager PyxcSourceManager;
static void PrintErrorSourceContext(SourceLocation Location);

/// advance - I return the next character, normalizing `\r\n` (Windows)
/// and bare `\r` (Old Macs) into `\n`.
///
/// This is the single point through which all character consumption flows.
/// Every token branch in getToken() calls advance() rather than fgetc()
/// directly, so LexerLocation and the source buffer are always in sync.
static int advance() {
  int LastChar = fgetc(Input);

  // case: '\r' or '\r\n'
  if (LastChar == '\r') {
    int NextChar = fgetc(Input);

    // A following '\n' is part of the same line ending; eat it.
    // Anything else belongs to the next token; put it back.
    // (EOF can't be put back at all, so it's excluded from that check.
    // The next getchar() will still return EOF, so we don't lose it.)
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, Input);
    PyxcSourceManager.onChar('\n');
    LexerLocation.Line++;
    LexerLocation.Column = 0;
    return '\n';
  }

  // '\n' resets Column and starts a new buffered line; anything else
  // just advances Column within the current line.
  if (LastChar == '\n') {
    PyxcSourceManager.onChar('\n');
    LexerLocation.Line++;
    LexerLocation.Column = 0;
  } else {
    PyxcSourceManager.onChar(LastChar);
    LexerLocation.Column++;
  }

  // case '\n' or any other non-newline character
  return LastChar;
}

/// peek - Return the next character from the input stream without consuming it.
///
/// Used by the two-character operator branches in getToken() to decide whether
/// '=' should become '==' (tok_eq), '!' should become '!=' (tok_neq), etc.,
/// without advancing LexerLocation or notifying SourceManager.
static int peek() {
  int c = fgetc(Input);
  if (c != EOF)
    ungetc(c, Input);
  return c;
}

/// getToken - Return the next token from standard input.
///
/// LastChar holds the last character read by advance() but not yet consumed
/// by a token. It is initialised to ' ' so the first call skips straight to
/// the whitespace loop without reading a character, and the loop's first
/// advance() call picks up the real first character.
///
/// CurrentTokenLocation is snapshotted from LexerLocation after the whitespace-skip loop and
/// before any token branch. For most tokens this points at the first
/// character of the token. For tok_eol the '\n' was already consumed by
/// advance() on a previous call, so LexerLocation is already on the next line;
/// GetCaretAnchorLocation compensates by subtracting one when building error
/// locations for tok_eol.
///
/// The comment path ('#' branch) re-snapshots CurrentTokenLocation just before returning
/// tok_eol because it consumes many characters (the whole comment) after the
/// initial snapshot, leaving LexerLocation well past the '#' position.
///
/// AtLineStart is true (1) at startup, (2) right after consuming '\n', and
/// (3) right after emitting tok_eol. It stays true while we are still resolving
/// indentation for the new line (including full-line comments, which produce
/// no tokens). It flips false as soon as indentation is settled and the line
/// is known to contain a real token, even before that token is emitted.
static int getToken() {
  // Drain tokens queued by a multi-level dedent on the previous line.
  if (!PendingTokens.empty()) {
    int Tok = PendingTokens.front();
    PendingTokens.pop_front();
    return Tok;
  }
  // ── Line-start: count indentation, emit INDENT / DEDENT ──────────────
  if (AtLineStart) {
    // Prime sentinel space once so indentation scans real input.
    if (LexerLastChar == ' ')
      LexerLastChar = advance();
    int CurrentIndentRead = 0;
    while (LexerLastChar == ' ' || LexerLastChar == '\t') {
      CurrentIndentRead += (LexerLastChar == ' ') ? 1 : (IndentTabWidth - CurrentIndentRead % IndentTabWidth);
      LexerLastChar = advance();
    }

    // Blank line: ignore in file mode; close the block immediately in REPL.
    if (LexerLastChar == '\n') {
      if (IsRepl && IndentStack.size() > 1) {
        IndentStack.pop_back();
        return tok_dedent;
      }
      CurrentTokenLocation = LexerLocation;
      LexerLastChar = ' '; // AtLineStart is still true so the lexer reads past
                           // the sentinel in the next call
      return tok_eol;
    }

    // Comment-only line: consume and return a newline.
    if (LexerLastChar == '#') {
      do {
        LexerLastChar = advance();
      } while (LexerLastChar != '\n' && LexerLastChar != EOF);
      if (LexerLastChar == '\n') {
        CurrentTokenLocation = LexerLocation;
        LexerLastChar = ' '; // AtLineStart is still true so the lexer reads
                             // past the sentinel in the next call
        return tok_eol;
      }
      // else fall through to EOF handling below
    }

    // EOF (with or without trailing newline): flush open blocks one at a time.
    if (LexerLastChar == EOF) {
      if (IndentStack.size() > 1) {
        IndentStack.pop_back();
        return tok_dedent;
      }
      return tok_eof;
    }

    // Real content: compare column to the indent stack.
    CurrentTokenLocation = LexerLocation;
    int CurrentIndentOnStack = IndentStack.back();
    if (CurrentIndentRead > CurrentIndentOnStack) {
      IndentStack.push_back(CurrentIndentRead);
      AtLineStart = false;
      return tok_indent;
    }
    if (CurrentIndentRead < CurrentIndentOnStack) {
      while (IndentStack.size() > 1 /* protect the 0-indent */ &&
             CurrentIndentRead < IndentStack.back()) {
        IndentStack.pop_back();
        PendingTokens.push_back(tok_dedent);
      }
      if (CurrentIndentRead != IndentStack.back()) {
        LogErrorAtLoc("inconsistent indentation", CurrentTokenLocation);
        PrintErrorSourceContext(CurrentTokenLocation);
        return tok_error;
      }
      // We have at least one pending dedent token. Instead of looping, we just
      // pop it and send it back from here.
      AtLineStart = false;
      int Tok = PendingTokens.front();
      PendingTokens.pop_front();
      return Tok;
    }
    // Same indentation level — no indent/dedent token needed.
    AtLineStart = false;
  }
  // ── End of line-start processing ─────────────────────────────────────

  // Not at line start anymore. Skip horizontal whitespace between tokens.
  // Stop at '\n' — it becomes tok_eol.
  while (isspace(LexerLastChar) && LexerLastChar != '\n')
    LexerLastChar = advance();

  // Snapshot position for the upcoming token. See note above about tok_eol.
  CurrentTokenLocation = LexerLocation;

  if (LexerLastChar == '\n') {
    // Do not call advance() here.
    // We should return the newline token immediately. If we read one more
    // character first, REPL mode may wait for extra input before processing
    // the line the user just submitted.
    LexerLastChar = ' ';
    AtLineStart = true;
    return tok_eol;
  }

  if (isalpha(LexerLastChar) || LexerLastChar == '_') {
    string NameLiteral;
    NameLiteral = LexerLastChar;
    while (isalnum((LexerLastChar = advance())) || LexerLastChar == '_')
      NameLiteral += LexerLastChar;

    auto It = Keywords.find(NameLiteral);
    if (It != Keywords.end())
      return It->second;
    Name = NameLiteral;
    return tok_name;
  }

  if (isdigit(LexerLastChar) || LexerLastChar == '.') {
    NumberLiteral.clear();
    bool SawDot = false;
    bool SawExp = false;

    auto ConsumeDigits = [&]() {
      while (isdigit(LexerLastChar)) {
        NumberLiteral += LexerLastChar;
        LexerLastChar = advance();
      }
    };

    if (LexerLastChar == '.') {
      SawDot = true;
      NumberLiteral += LexerLastChar;
      LexerLastChar = advance();
      ConsumeDigits();
    } else {
      ConsumeDigits();
      if (LexerLastChar == '.') {
        SawDot = true;
        NumberLiteral += LexerLastChar;
        LexerLastChar = advance();
        ConsumeDigits();
      }
    }

    if (LexerLastChar == 'e' || LexerLastChar == 'E') {
      SawExp = true;
      NumberLiteral += LexerLastChar;
      LexerLastChar = advance();
      if (LexerLastChar == '+' || LexerLastChar == '-') {
        NumberLiteral += LexerLastChar;
        LexerLastChar = advance();
      }
      if (!isdigit(LexerLastChar)) {
      LogInvalidNumberLiteralAtLocation(NumberLiteral, CurrentTokenLocation);
      return tok_error;
      }
      ConsumeDigits();
    }

    if (NumberLiteral == ".") {
      LogInvalidNumberLiteralAtLocation(NumberLiteral, CurrentTokenLocation);
      return tok_error;
    }

    NumberIsFloat = SawDot || SawExp;
    return tok_number;
  }

  // I discard a comment.
  if (LexerLastChar == '#') {
    // I consume characters through the end of the line.
    do {
      LexerLastChar = advance();
    } while (LexerLastChar != '\n' && LexerLastChar != EOF);

    if (LexerLastChar == '\n') {
      // Re-snapshot CurrentTokenLocation now that the '\n' has been consumed and LexerLocation
      // has advanced to the next line. Without this, CurrentTokenLocation would point at
      // the '#' column, and GetCaretAnchorLocation would look up the wrong
      // line (because it subtracts 1) when the next token triggers an error.
      CurrentTokenLocation = LexerLocation;
      LexerLastChar = ' ';
      AtLineStart = true;
      return tok_eol;
    }
  }

  // peek(), if the next one completes a recognized token, eat it, and return
  // token; otherwise, I return the named single-character token.
  if (LexerLastChar == '-') {
    int Tok = (peek() == '>') ? (advance(), tok_arrow) : tok_minus;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '=') {
    int Tok = (peek() == '=') ? (advance(), tok_eq) : tok_equal;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '!') {
    int Tok = (peek() == '=') ? (advance(), tok_neq) : '!';
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '<') {
    int Tok = (peek() == '=') ? (advance(), tok_leq) : tok_less;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '>') {
    int Tok = (peek() == '=') ? (advance(), tok_geq) : tok_greater;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == EOF) {
    // If the final line has no trailing newline, synthesize one so the parser
    // still sees a normal statement terminator before EOF.
    if (!AtLineStart) {
      LexerLastChar = ' ';
      AtLineStart = true;
      return tok_eol;
    }
    return tok_eof;
  }

  // I read a single-character token.
  int ThisChar = LexerLastChar;

  // Position the lexer at the next character so the next getToken() starts there.
  LexerLastChar = advance();

  // I return a named token for known punctuation and operators.
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
  case '%':
    return tok_percent;
  case '<':
    return tok_less;
  case '>':
    return tok_greater;
  case '=':
    return tok_equal;
  default:
    return ThisChar;
  }
}

/// ResetLexerState - Restore lexer globals to their initial state.
///
/// Used when starting a new input file so indentation, source locations,
/// and buffered source lines do not carry over between files.
static void ResetLexerState() {
  IndentStack = {0};
  PendingTokens.clear();
  AtLineStart = true;
  LexerLocation = {1, 0};
  CurrentTokenLocation = {1, 0};
  LexerLastChar = ' ';
  PyxcSourceManager.reset();
}

//===----------------------------------------===//
// Diagnostics helpers
//===----------------------------------------===//

/// GetCaretAnchorLocation - Resolve the source location to attach to an error.
///
/// For most tokens, CurrentTokenLocation already points at the right place and is returned
/// unchanged. The special case is tok_eol: CurrentTokenLocation for a newline token is
/// snapshotted after advance() has consumed the '\n' and incremented
/// LexerLocation.Line, so CurrentTokenLocation.Line is already the *next* line. Subtracting one
/// gives the line that just ended, and we report a column one past its last
/// character — pointing just after the final token on the line, which is
/// where the missing token (e.g. ':') should have appeared.
static SourceLocation GetCaretAnchorLocation(SourceLocation Location, int Token) {
  if (Token != tok_eol || Location.Line <= 1)
    return Location;

  // Token == tok_eol && Location.Line > 1. I need to return a location just
  // past the end of the previous line.
  int PrevLine = Location.Line - 1;
  const string *PrevLineText = PyxcSourceManager.getLine(PrevLine);

  // guard
  // PrevLineText is null only if PrevLine hasn't been buffered yet —
  // it shouldn't happen, since I only get here after consuming that
  // line's trailing newline, but I fall back to the original Location
  // rather than trust an out-of-range read.
  if (!PrevLineText)
    return Location;

  // return a pointer just past the end of the previous line.
  return {PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}

/// FormatTokenForMessage - Return a human-readable description of Tok for use
/// in error messages. Name and number tokens include their actual text
/// (e.g. "name 'foo'", "number '3.14'") since the name alone is not
/// enough to diagnose the problem. Everything else uses the static TokenNames
/// entry.
static string FormatTokenForMessage(int Token) {
  if (Token == tok_name)
    return "name '" + Name + "'";
  if (Token == tok_number)
    return "number '" + NumberLiteral + "'";

  auto It = TokenNames.find(Token);
  if (It != TokenNames.end())
    return It->second;
  return "unknown token";
}

/// PrintErrorSourceContext - Reprint the source line at Loc and place a
/// '^~~~' caret under column Location.Column. Column is 1-based, so we print Column-1
/// spaces before the caret.
static void PrintErrorSourceContext(SourceLocation Location) {
  const string *LineText = PyxcSourceManager.getLine(Location.Line);
  // LineText is null only if Location points past everything buffered so
  // far (e.g. an uninitialized Location.Line == 0). Skip printing rather
  // than dereference it below.
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = max(0, Location.Column - 1);
  fprintf(stderr, "%*s", spaces, " ");
  fprintf(stderr, "^~~~\n");
}


static void LogErrorAtLoc(const string &ErrorMessage, SourceLocation Loc) {
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Loc.Line, Loc.Column, ErrorMessage.c_str());
  PrintErrorSourceContext(Loc);
}

static void LogInvalidNumberLiteralAtLocation(const string &Literal,
                                              SourceLocation Location) {
  LogErrorAtLoc(("invalid number literal '" + Literal + "'"), Location);
}

//===----------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------===//
namespace {

/// ExpressionNode - Base class for all expression nodes.
class ExpressionNode {
  ValueType Type = ValueType::Error;

public:
  virtual ~ExpressionNode() = default;
  ValueType getType() const { return Type; }
  // getLValueName - If this node is a plain assignable variable, return its
  // name; otherwise return nullptr.
  virtual const string *getLValueName() const { return nullptr; }
  // isReturnExpr - True iff this node is a return statement.
  virtual bool isReturnExpr() const { return false; }
  // shouldPrintValue - Whether the REPL should print the value of this node
  // when it appears as a top-level form.
  virtual bool shouldPrintValue() const { return true; }
  virtual Value *codegen() = 0;

protected:
  void setType(ValueType NewType) { Type = NewType; }
};

/// NumberExpressionNode - Expression class for numeric literals.
class NumberExpressionNode : public ExpressionNode {
  bool IsIntLiteral;
  APInt IntegerValue;
  APFloat FloatValue;

public:
  NumberExpressionNode(APInt Value, ValueType Type)
      : IsIntLiteral(true), IntegerValue(std::move(Value)), FloatValue(0.0) {
    setType(Type);
  }
  NumberExpressionNode(APFloat Value, ValueType Type)
      : IsIntLiteral(false), IntegerValue(1, 0), FloatValue(std::move(Value)) {
    setType(Type);
  }
  llvm::Value *codegen() override;
};

/// BoolExpressionNode - Expression class for boolean literals: True/False.
class BoolExpressionNode : public ExpressionNode {
  bool Value;

public:
  BoolExpressionNode(bool Value) : Value(Value) { setType(ValueType::Bool); }
  llvm::Value *codegen() override;
};

/// NameExpressionNode - Expression class for referencing a variable, like "a".
class NameExpressionNode : public ExpressionNode {
  string Name;

public:
  NameExpressionNode(const string &Name, ValueType Type) : Name(Name) {
    setType(Type);
  }
  // convenience function
  const string &getName() const { return Name; }
  const string *getLValueName() const override { return &Name; }
  Value *codegen() override;
};

/// AssignmentStatementNode - Statement class for assignment to an existing variable.
/// The expression stores Right into the named variable and produces the assigned
/// value.
class AssignmentStatementNode : public ExpressionNode {
  string Name;
  unique_ptr<ExpressionNode> Expr;

public:
  AssignmentStatementNode(const string &Name, unique_ptr<ExpressionNode> Expr,
                    ValueType Type)
      : Name(Name), Expr(std::move(Expr)) {
    setType(Type);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// ReturnStatementNode - Statement-like expression for return.
/// Emits a function return and produces the returned value.
class ReturnStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Expr;

public:
  ReturnStatementNode(unique_ptr<ExpressionNode> Expr = nullptr) : Expr(std::move(Expr)) {
    setType(ValueType::None);
  }
  bool isReturnExpr() const override { return true; }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// BlockStatementNode - A sequence of statements evaluated in order.
/// The block's value is the value of the last statement executed.
class BlockStatementNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Stmts;

public:
  BlockStatementNode(vector<unique_ptr<ExpressionNode>> Stmts) : Stmts(std::move(Stmts)) {
    setType(ValueType::None);
  }
  Value *codegen() override;
};

/// BinaryExpressionNode - Expression class for a binary operator.
/// Operator is an int (not char) to accommodate both single-character ASCII operators
/// like '+' and named multi-character token enums like tok_eq (==).
class BinaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Left, Right;

public:
  BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left, unique_ptr<ExpressionNode> Right,
                ValueType Type)
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {
    setType(Type);
  }
  Value *codegen() override;
};

/// CallExpressionNode - Expression class for function calls.
class CallExpressionNode : public ExpressionNode {
  string Callee;
  vector<unique_ptr<ExpressionNode>> Arguments;

public:
  CallExpressionNode(const string &Callee, vector<unique_ptr<ExpressionNode>> Arguments,
              ValueType Type)
      : Callee(Callee), Arguments(std::move(Arguments)) {
    setType(Type);
  }
  bool shouldPrintValue() const override {
    return getType() != ValueType::None;
  }
  Value *codegen() override;
};

/// ForStatementNode - Statement class for for loops.
///   for <var> = <start>, <cond>, <step>: <body>
/// The loop variable is in scope for <cond>, <step>, and <body> (through
/// NamedValues). The expression always produces 0.0 — the loop is used for side
/// effects.
class ForStatementNode : public ExpressionNode {
  string VarName;
  bool IsVarDecl;
  ValueType VarType;
  unique_ptr<ExpressionNode> Start, Cond, Step, Body;

public:
  ForStatementNode(const string &VarName, bool IsVarDecl, ValueType VarType,
             unique_ptr<ExpressionNode> Start, unique_ptr<ExpressionNode> Cond,
             unique_ptr<ExpressionNode> Step, unique_ptr<ExpressionNode> Body)
      : VarName(VarName), IsVarDecl(IsVarDecl), VarType(VarType),
        Start(std::move(Start)), Cond(std::move(Cond)), Step(std::move(Step)),
        Body(std::move(Body)) {
    setType(ValueType::None);
  }
  ValueType getVarType() const { return VarType; }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// WhileStatementNode - Statement class for while and do/while loops.
class WhileStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Cond, Body;
  bool IsDoWhile;

public:
  WhileStatementNode(unique_ptr<ExpressionNode> Cond,
                     unique_ptr<ExpressionNode> Body, bool IsDoWhile)
      : Cond(std::move(Cond)), Body(std::move(Body)), IsDoWhile(IsDoWhile) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class BreakStatementNode : public ExpressionNode {
public:
  BreakStatementNode() { setType(ValueType::None); }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class ContinueStatementNode : public ExpressionNode {
public:
  ContinueStatementNode() { setType(ValueType::None); }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// UnaryExpressionNode - Expression class for a unary operator application.
/// The operator is identified by its ASCII character (e.g. '-' or '!').
/// Built-in unary minus is represented here with opcode '-' and lowered
/// directly to LLVM `fneg`. All other unary operators are resolved as regular
/// functions named "unary<op>" (e.g. "unary!") and called with the operand.
class UnaryExpressionNode : public ExpressionNode {
  char Opcode;
  unique_ptr<ExpressionNode> Operand;

public:
  UnaryExpressionNode(char Opcode, unique_ptr<ExpressionNode> Operand, ValueType Type)
      : Opcode(Opcode), Operand(std::move(Operand)) {
    setType(Type);
  }
  Value *codegen() override;
};

/// CastExpressionNode - Expression class for explicit casts: int(expr), float64(expr).
class CastExpressionNode : public ExpressionNode {
  ValueType TargetType;
  unique_ptr<ExpressionNode> Expr;

public:
  CastExpressionNode(ValueType TargetType, unique_ptr<ExpressionNode> Expr)
      : TargetType(TargetType), Expr(std::move(Expr)) {
    setType(TargetType);
  }
  Value *codegen() override;
};

/// IfStatementNode - Statement form of if/else.
/// Produces 0.0 and does not return a value.
class IfStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Cond, Then, Else;

public:
  IfStatementNode(unique_ptr<ExpressionNode> Cond, unique_ptr<ExpressionNode> Then,
            unique_ptr<ExpressionNode> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

// VarBinding - One declared variable and its optional initializer.
struct VarBinding {
  string Name;
  ValueType Type;
  unique_ptr<ExpressionNode> Init;
};

/// VarStatementNode - Statement form of mutable local variable bindings.
///   var a = <init>, b = <init>
/// Each binding allocates stack storage in the current function's entry block
/// and stores its initializer. Bindings persist for the rest of the function.
class VarStatementNode : public ExpressionNode {
  vector<VarBinding> VarNames;

public:
  VarStatementNode(vector<VarBinding> VarNames) : VarNames(std::move(VarNames)) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// FunctionSignatureNode - This class represents the "function signature" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
///
class FunctionSignatureNode {
  string Name;
  vector<pair<string, ValueType>> Parameters;
  ValueType ReturnType;
  SourceLocation Loc;

public:
  FunctionSignatureNode(const string &Name,
                        vector<pair<string, ValueType>> Parameters,
                        SourceLocation Loc,
                        ValueType ReturnType = ValueType::Float64)
      : Name(Name), Parameters(std::move(Parameters)), ReturnType(ReturnType),
        Loc(Loc) {}

  const string &getName() const { return Name; }
  const vector<pair<string, ValueType>> &getParameters() const { return Parameters; }
  size_t getNumParameters() const { return Parameters.size(); }
  SourceLocation getLocation() const { return Loc; }
  ValueType getReturnType() const { return ReturnType; }
  void setReturnType(ValueType Type) { ReturnType = Type; }

  ValueType getParameterType(size_t Index) const {
    if (Index >= Parameters.size())
      return ValueType::Error;
    return Parameters[Index].second;
  }


  std::unique_ptr<FunctionSignatureNode> clone() const {
    return std::make_unique<FunctionSignatureNode>(Name, Parameters, Loc,
                                                    ReturnType);
  }

  Function *codegen();
};

/// FunctionDefinitionNode - This class represents a function function-definition itself.
class FunctionDefinitionNode {
  unique_ptr<FunctionSignatureNode> Signature;
  unique_ptr<ExpressionNode> Body;

public:
  FunctionDefinitionNode(unique_ptr<FunctionSignatureNode> Signature, unique_ptr<ExpressionNode> Body)
      : Signature(std::move(Signature)), Body(std::move(Body)) {}
  const string &getName() const { return Signature->getName(); }
  ValueType getReturnType() const { return Signature->getReturnType(); }
  Function *codegen();
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

// FunctionSignatures - Persistent function signature registry used by codegen
// to re-emit declarations into fresh modules.
static std::map<std::string, std::unique_ptr<FunctionSignatureNode>> FunctionSignatures;

// Parse-time variable tracking for assignments and types.
// Scopes are stacked: function scope plus nested block scopes.
// for-loop variables are scoped to the loop body only.
static vector<std::map<string, ValueType>> VarScopes;
// Global variables declared at top level (persist across modules).
static std::map<string, ValueType> GlobalVarTypes;
// Track which globals were declared in this translation unit (for redeclare
// checks).
static std::set<string> GlobalVarDecls;
// True while parsing a top-level statement (var binds globals, not locals).
static bool ParsingTopLevel = false;
static int ParseLoopDepth = 0;
// Set when we hit a parse/codegen error; used to abort further processing.
static bool HadError = false;
// Current function's declared return type during parsing/codegen.
static ValueType CurrentFunctionReturnType = ValueType::None;

struct TopLevelParseGuard {
  TopLevelParseGuard() { ParsingTopLevel = true; }
  ~TopLevelParseGuard() { ParsingTopLevel = false; }
};

static void BeginFunctionScope(const vector<pair<string, ValueType>> &Parameters) {
  VarScopes.clear();
  VarScopes.emplace_back();
  for (const auto &Parameter : Parameters)
    VarScopes.front()[Parameter.first] = Parameter.second;
}

static void EndFunctionScope() { VarScopes.clear(); }

static void DeclareVar(const string &Name, ValueType Type) {
  // Only declare into an active local scope; at top level VarScopes is empty.
  if (VarScopes.empty())
    return;
  VarScopes.back()[Name] = Type;
}

static void BeginBlockScope() { VarScopes.emplace_back(); }

// Pop a block scope if one is active.
// Size > 1 means a nested block inside a function; never pop the function scope
// here. Size == 1 is only popped for top-level blocks (function scope is popped
// in EndFunctionScope).
static void EndBlockScope() {
  if (VarScopes.size() > 1)
    VarScopes.pop_back();
  else if (ParsingTopLevel && VarScopes.size() == 1)
    VarScopes.pop_back();
}

// Check only the innermost scope (used for redeclaration checks).

// Ensure a function scope exists, then add a new scope for the loop variable.
static void BeginLoopScope(const string &Name, ValueType Type) {
  VarScopes.emplace_back();
  VarScopes.back()[Name] = Type;
}

// Size == 1 is only popped for top-level blocks (function scope is popped in
// EndFunctionScope).
static void EndLoopScope() {
  if (VarScopes.size() > 1)
    VarScopes.pop_back();
  if (ParsingTopLevel && VarScopes.size() == 1)
    VarScopes.pop_back();
}

struct FunctionScopeGuard {
  FunctionScopeGuard(const vector<pair<string, ValueType>> &Parameters) {
    BeginFunctionScope(Parameters);
  }
  ~FunctionScopeGuard() { EndFunctionScope(); }
};

struct BlockScopeGuard {
  BlockScopeGuard() { BeginBlockScope(); }
  ~BlockScopeGuard() { EndBlockScope(); }
};

struct LoopScopeGuard {
  LoopScopeGuard(const string &Name, ValueType Type) {
    BeginLoopScope(Name, Type);
  }
  ~LoopScopeGuard() { EndLoopScope(); }
};

struct ParseLoopGuard {
  ParseLoopGuard() { ++ParseLoopDepth; }
  ~ParseLoopGuard() { --ParseLoopDepth; }
};



struct ReturnTypeGuard {
  ValueType Saved;
  ReturnTypeGuard(ValueType Type) : Saved(CurrentFunctionReturnType) {
    CurrentFunctionReturnType = Type;
  }
  ~ReturnTypeGuard() { CurrentFunctionReturnType = Saved; }
};

// Check only the innermost scope (used for redeclaration checks).
static bool IsDeclaredInCurrentScope(const string &Name) {
  if (VarScopes.empty())
    return false;
  return VarScopes.back().count(Name) > 0;
}

// IsDeclaredVar - Check all local scopes from innermost to outermost, then
// fall back to globals. Used to validate assignments and references.
static bool IsDeclaredVar(const string &Name) {
  for (auto It = VarScopes.rbegin(); It != VarScopes.rend(); ++It) {
    if (It->count(Name))
      return true;
  }
  return GlobalVarTypes.count(Name) > 0;
}

// LookupVarType - Return the type from the nearest enclosing local scope,
// or from globals if not found; otherwise ValueType::Error.
static ValueType LookupVarType(const string &Name) {
  for (auto It = VarScopes.rbegin(); It != VarScopes.rend(); ++It) {
    auto Found = It->find(Name);
    if (Found != It->end())
      return Found->second;
  }
  auto GI = GlobalVarTypes.find(Name);
  if (GI != GlobalVarTypes.end())
    return GI->second;
  return ValueType::Error;
}

/// PrintReplPrompt - Print the interactive prompt to stderr.
/// Only emits output in REPL mode; silent when running a script file.
void PrintReplPrompt() {
  if (IsRepl) {
    fflush(stdout);
    fprintf(stderr, "ready> ");
  }
}

/// Log - Write a diagnostic message to stderr in REPL mode only.
/// Used by the Handle* functions to confirm what was parsed ("Parsed a
/// function function-definition.", etc.). Silent when processing a script file so
/// that stdout/stderr output from the program itself is not cluttered.
void Log(const string &message) {
  if (IsRepl && ShouldDumpIR())
    fprintf(stderr, "%s", message.c_str());
}

/// LogErrorExpression* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogErrorExpression("message");
unique_ptr<ExpressionNode> LogErrorExpression(const string &ErrorMessage) {
  HadError = true;
  SourceLocation Anchor = GetCaretAnchorLocation(CurrentTokenLocation, CurrentToken);
  LogErrorAtLoc(ErrorMessage, Anchor);
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
static unique_ptr<ExpressionNode> ParsePrimary();
static unique_ptr<ExpressionNode> ParseVarStatement();
static unique_ptr<ExpressionNode> ParseStatement();
static unique_ptr<ExpressionNode> ParseSimpleStatement();
static unique_ptr<ExpressionNode> ParseBlock();
static unique_ptr<ExpressionNode> ParseFunctionBody();


// Counter to give each anonymous top-level expression a unique name.
static unsigned TopLevelExprCounter = 0;
// Whether the last top-level form should be printed in the REPL.
static bool LastTopLevelShouldPrint = true;

static unique_ptr<ExpressionNode> ParseSuite();
static ValueType ParseTypeToken();
static const char *TypeName(ValueType Type);
static bool IsNumericType(ValueType Type);
static bool IsIntType(ValueType Type);
static bool IsFloatType(ValueType Type);
static bool IsAssignable(ValueType Dest, ValueType Src);
static Type *LLVMTypeFor(ValueType Type);
static FunctionSignatureNode *GetFunctionSignature(const string &Name);
// Optional expected type for numeric literals (used for float/float32).
static ValueType ExpectedLiteralType = ValueType::Error;

struct ExpectedLiteralTypeGuard {
  ValueType Saved;
  ExpectedLiteralTypeGuard(ValueType Type) : Saved(ExpectedLiteralType) {
    ExpectedLiteralType = Type;
  }
  ~ExpectedLiteralTypeGuard() { ExpectedLiteralType = Saved; }
};

static unique_ptr<ExpressionNode> MakeZeroLiteral(ValueType Type) {
  if (IsIntType(Type)) {
    unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
    return make_unique<NumberExpressionNode>(APInt(Bits, 0), Type);
  }
  if (IsFloatType(Type)) {
    const fltSemantics &Semantics = (Type == ValueType::Float32)
                                        ? APFloat::IEEEsingle()
                                        : APFloat::IEEEdouble();
    return make_unique<NumberExpressionNode>(APFloat(Semantics, "0"), Type);
  }
  if (Type == ValueType::Bool)
    return make_unique<BoolExpressionNode>(false);
  return LogErrorExpression("Cannot default-initialize this type");
}

/// number-expression
///   = number ;
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  ValueType Type = NumberIsFloat ? ValueType::Float64 : ValueType::Int;
  if (NumberIsFloat) {
    if (IsFloatType(ExpectedLiteralType))
      Type = ExpectedLiteralType;
    const fltSemantics &Semantics = (Type == ValueType::Float32)
                                        ? APFloat::IEEEsingle()
                                        : APFloat::IEEEdouble();
    APFloat Val(Semantics);
    auto StatusOrErr =
        Val.convertFromString(NumberLiteral, APFloat::rmNearestTiesToEven);

    // convertFromString returns Expected<opStatus>. If it's in error, the
    // literal is malformed (e.g., bad syntax).
    if (!StatusOrErr)
      return LogErrorExpression("Invalid floating-point literal");

    // opStatus may still report conversion issues like invalid op or overflow.
    APFloat::opStatus Status = *StatusOrErr;
    if (Status & APFloat::opInvalidOp)
      return LogErrorExpression("Invalid floating-point literal");
    if (Status & APFloat::opOverflow)
      return LogErrorExpression("Floating-point literal out of range for type");

    auto Result = make_unique<NumberExpressionNode>(Val, Type);
    getNextToken(); // consume the number
    return Result;
  } else {
    // If the surrounding context expects an int type, honor it.
    if (IsIntType(ExpectedLiteralType))
      Type = ExpectedLiteralType;

    // Parse with enough bits to hold the literal’s full magnitude, then
    // range-check against the target *signed* width. This avoids cases like
    // 128: it needs 8 bits unsigned, but doesn’t fit in signed int8 (max 127).
    unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
    unsigned NeededBits = APInt::getBitsNeeded(NumberLiteral, 10);
    unsigned ParseBits = std::max(Bits, NeededBits);
    APInt Val(ParseBits, NumberLiteral, 10);

    // Reject if the literal doesn't fit in the target signed width.
    APInt Max = APInt::getSignedMaxValue(Bits);
    if (Val.ugt(Max))
      return LogErrorExpression("Integer literal out of range for type");

    // Truncate down to the target width once it's known to fit.
    // This will actually never happen. It's a paranoia move.
    if (ParseBits != Bits)
      Val = Val.trunc(Bits);

    auto Result = make_unique<NumberExpressionNode>(Val, Type);
    getNextToken(); // consume the number
    return Result;
  }
}

/// type
///   = "int" | "int8" | "int16" | "int32" | "int64"
///   | "float" | "float32" | "float64"
///   | "bool" | "None" ;
///
/// cast-type
///   = "int" | "int8" | "int16" | "int32" | "int64"
///   | "float" | "float32" | "float64"
///   | "bool" ;
static ValueType ParseTypeToken() {
  switch (CurrentToken) {
  case tok_int:
    getNextToken();
    return ValueType::Int;
  case tok_int8:
    getNextToken();
    return ValueType::Int8;
  case tok_int16:
    getNextToken();
    return ValueType::Int16;
  case tok_int32:
    getNextToken();
    return ValueType::Int32;
  case tok_int64:
    getNextToken();
    return ValueType::Int64;
  case tok_float:
    getNextToken();
    return ValueType::Float;
  case tok_float32:
    getNextToken();
    return ValueType::Float32;
  case tok_float64:
    getNextToken();
    return ValueType::Float64;
  case tok_bool:
    getNextToken();
    return ValueType::Bool;
  case tok_none:
    getNextToken();
    return ValueType::None;
  default:
    LogErrorExpression("Expected a type");
    return ValueType::Error;
  }
}

/// cast-expression
///   = cast-type "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseCastExpression() {
  ValueType Type = ParseTypeToken();
  if (Type == ValueType::Error)
    return nullptr;
  if (Type == ValueType::None)
    return LogErrorExpression("Cannot cast to None");
  if (CurrentToken != tok_lparen)
    return LogErrorExpression("Expected '(' after cast type");
  getNextToken(); // eat '('
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')' after cast expression");
  getNextToken(); // eat ')'
  return make_unique<CastExpressionNode>(Type, std::move(Expr));
}

/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat (.
  auto Expression = ParseExpression();
  if (!Expression)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')'");
  getNextToken(); // eat ).
  return Expression;
}

/// name-expression
///   = name
///   | call-expression ;
///
/// call-expression
///   = name "(" [ arguments ] ")" ;
static unique_ptr<ExpressionNode> ParseNameExpressionWithName(const string &ParsedName) {
  if (CurrentToken != tok_lparen) { // Simple variable ref.
    ValueType Type = LookupVarType(ParsedName);
    if (Type == ValueType::Error) {
      return LogErrorExpression("Unknown variable name");
    }
    return make_unique<NameExpressionNode>(ParsedName, Type);
  }

  // Call.
  getNextToken(); // eat (

  // Signature may be null for forward references. We still parse the call to keep
  // the token stream aligned; the “unknown function” error is raised later
  // during semantic/codegen.
  FunctionSignatureNode *Signature = GetFunctionSignature(ParsedName);
  vector<unique_ptr<ExpressionNode>> Arguments;
  if (CurrentToken != tok_rparen) {
    size_t ArgIndex = 0;
    while (true) {
      ValueType Expected = ValueType::Error;
      if (Signature && ArgIndex < Signature->getNumParameters())
        Expected = Signature->getParameterType(ArgIndex);
      {
        ExpectedLiteralTypeGuard Guard(Expected);
        if (auto Argument = ParseExpression())
          Arguments.push_back(std::move(Argument));
        else
          return nullptr;
      }

      // ParseExpression() has already consumed the argument and left
      // CurrentToken at the token after it.
      if (CurrentToken == tok_rparen)
        break;

      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken(); // I eat ','.
      ++ArgIndex;
    }
  }

  // I only reach here after parsing `a()` or `a(<arguments>)`, so I eat ')'.
  getNextToken();

  if (!Signature)
    return LogErrorExpression("Unknown function: '" + ParsedName + "'");
  if (Signature->getNumParameters() != Arguments.size())
    return LogErrorExpression(
        "Incorrect number of arguments in call to '" + ParsedName +
        "': expected " + to_string(Signature->getNumParameters()) +
        ", got " + to_string(Arguments.size()));

  for (size_t i = 0; i < Arguments.size(); ++i) {
    ValueType ArgType = Arguments[i]->getType();
    ValueType ParamType = Signature->getParameterType(i);
    if (!IsAssignable(ParamType, ArgType)) {
      return LogErrorExpression(("argument " + std::to_string(i + 1) + " expects " +
                       TypeName(ParamType))
                          .c_str());
    }
  }

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments),
                                  Signature->getReturnType());
}

static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;

  getNextToken(); // eat name.

  return ParseNameExpressionWithName(ParsedName);
}

// ParseForParts - Parse the "= start, cond, step : suite" tail of a for-loop.
// Also validates the parts against VarType (start/step assignable, cond bool).
// Returns true on success and fills Start/Cond/Step/Body.
static bool ParseForParts(ValueType VarType, unique_ptr<ExpressionNode> &Start,
                          unique_ptr<ExpressionNode> &Cond, unique_ptr<ExpressionNode> &Step,
                          unique_ptr<ExpressionNode> &Body) {
  if (CurrentToken != tok_equal)
    return LogErrorExpression("Expected '=' after for variable"), false;
  getNextToken(); // eat '='

  Start = ParseExpression();
  if (!Start)
    return false;
  if (!IsAssignable(VarType, Start->getType()))
    return LogErrorExpression("For loop start must match loop variable type"), false;
  if (!IsNumericType(VarType))
    return LogErrorExpression("For loop variable must be numeric"), false;

  if (CurrentToken != tok_comma)
    return LogErrorExpression("Expected ',' after for start value"), false;
  getNextToken(); // eat ','

  Cond = ParseExpression();
  if (!Cond)
    return false;
  if (Cond->getType() != ValueType::Bool)
    return LogErrorExpression("For loop condition must be bool"), false;

  if (CurrentToken != tok_comma)
    return LogErrorExpression("Expected ',' after for condition"), false;
  getNextToken(); // eat ','

  Step = ParseExpression();
  if (!Step)
    return false;
  if (!IsAssignable(VarType, Step->getType()))
    return LogErrorExpression("For loop step must match loop variable type"), false;

  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after for step"), false;
  getNextToken(); // eat ':'

  // Parse the suite after ':' (inline statement or indented block).
  ParseLoopGuard Loop;
  Body = ParseSuite();
  if (!Body)
    return false;

  return true;
}

/// for-statement
///   = "for"
///            ("var" name ":" type | name)
///            "=" expression "," expression "," expression ":" suite;
///
/// "for var" introduces a new loop variable scoped to the loop statement.
/// A plain "for i = ..." reuses an existing variable (error if undeclared).
static unique_ptr<ExpressionNode> ParseForStatement() {
  getNextToken(); // eat 'for'

  bool IsVarDecl = false;
  if (CurrentToken == tok_var) {
    IsVarDecl = true;
    getNextToken(); // optional 'var'
  }

  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected name after 'for'");
  string VarName = Name;
  getNextToken(); // eat name

  ValueType VarType = ValueType::Error;
  if (IsVarDecl) {
    if (CurrentToken != tok_colon)
      return LogErrorExpression(
          "For loop variable requires a type annotation (e.g., ': int')");
    getNextToken(); // eat ':'
    VarType = ParseTypeToken();
    if (VarType == ValueType::Error)
      return nullptr;
    if (VarType == ValueType::None)
      return LogErrorExpression("For loop variable cannot have None type");
    if (IsDeclaredInCurrentScope(VarName))
      return LogErrorExpression(
          ("Variable '" + VarName + "' already declared in this scope")
              .c_str());
  } else {
    if (CurrentToken == tok_colon)
      return LogErrorExpression("For loop variable requires 'var' to declare a type");
    VarType = LookupVarType(VarName);
    if (VarType == ValueType::Error)
      return LogErrorExpression("Assignment to undeclared variable");
  }

  unique_ptr<ExpressionNode> Start, Cond, Step, Body;

  if (IsVarDecl) {
    LoopScopeGuard LoopScope(VarName, VarType);
    if (!ParseForParts(VarType, Start, Cond, Step, Body))
      return nullptr;
  } else {
    if (!ParseForParts(VarType, Start, Cond, Step, Body))
      return nullptr;
  }
  return make_unique<ForStatementNode>(VarName, IsVarDecl, VarType, std::move(Start),
                                 std::move(Cond), std::move(Step),
                                 std::move(Body));
}

/// while-statement
///   = "while" expression ":" suite ;
static unique_ptr<ExpressionNode> ParseWhileStatement() {
  getNextToken(); // eat 'while'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogErrorExpression("While condition must be bool");
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after while condition");
  getNextToken(); // eat ':'

  ParseLoopGuard Loop;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  return make_unique<WhileStatementNode>(std::move(Cond), std::move(Body),
                                         false);
}

/// do-while-statement
///   = "do" ":" suite [ end-of-lines ] "while" expression ;
static unique_ptr<ExpressionNode> ParseDoWhileStatement() {
  getNextToken(); // eat 'do'
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after 'do'");
  getNextToken(); // eat ':'

  ParseLoopGuard Loop;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;

  if (CurrentToken == tok_block_end)
    getNextToken();
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_while)
    return LogErrorExpression("Expected 'while' after do body");
  getNextToken(); // eat 'while'

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogErrorExpression("Do/while condition must be bool");
  return make_unique<WhileStatementNode>(std::move(Cond), std::move(Body), true);
}

/// variable-statement
///   = "var" variable-binding { "," variable-binding } ;
///
/// variable-binding
///   = name ":" type [ "=" expression ] ;
static unique_ptr<ExpressionNode> ParseVarStatement() {
  getNextToken(); // eat 'var'

  vector<VarBinding> VarNames;
  bool IsGlobalDecl = ParsingTopLevel;

  while (true) {
    // name ":" type
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected name after 'var'");

    string ParsedName = Name;
    getNextToken(); // eat name

    if (CurrentToken != tok_colon)
      return LogErrorExpression(
          "Variable declaration requires a type annotation (e.g., ': int32')");
    getNextToken(); // eat ':'
    ValueType DeclType = ParseTypeToken();
    if (DeclType == ValueType::Error)
      return nullptr;
    if (DeclType == ValueType::None)
      return LogErrorExpression("Variables cannot have None type");

    if (IsGlobalDecl) {
      if (GlobalVarDecls.count(ParsedName))
        return LogErrorExpression(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    } else {
      if (IsDeclaredInCurrentScope(ParsedName))
        return LogErrorExpression(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    }

    unique_ptr<ExpressionNode> Init;
    // [ "=" expression ]
    if (CurrentToken == tok_equal) {
      getNextToken(); // eat '='
      ExpectedLiteralTypeGuard Guard(DeclType);
      Init = ParseExpression();
      if (!Init)
        return nullptr;
      if (!IsAssignable(DeclType, Init->getType()))
        return LogErrorExpression("Type mismatch in variable initialization");
    } else {
      Init = MakeZeroLiteral(DeclType);
      if (!Init)
        return nullptr;
    }

    VarNames.push_back({ParsedName, DeclType, std::move(Init)});
    if (IsGlobalDecl)
      GlobalVarTypes[ParsedName] = DeclType, GlobalVarDecls.insert(ParsedName);
    else
      DeclareVar(ParsedName, DeclType);

    if (CurrentToken != tok_comma)
      break;
    getNextToken(); // eat ','
  }

  return make_unique<VarStatementNode>(std::move(VarNames));
}

/// if-statement
///   = "if" expression ":" suite
///     { [ end-of-lines ] "elif" expression ":" suite }
///     [ [ end-of-lines ] "else" ":" suite ] ;
static unique_ptr<ExpressionNode> ParseIfStatement() {
  getNextToken(); // eat 'if'
  vector<pair<unique_ptr<ExpressionNode>, unique_ptr<ExpressionNode>>> Branches;
  bool LastBranchWasBlock = false;
  bool LastBranchHadTrailingEndOfLine = false;

  while (true) {
    auto Condition = ParseExpression();
    if (!Condition)
      return nullptr;
    if (Condition->getType() != ValueType::Bool)
      return LogErrorExpression("If condition must be bool");
    if (CurrentToken != tok_colon)
      return LogErrorExpression("Expected ':' after if/elif condition");
    getNextToken(); // eat ':'

    auto Body = ParseSuite();
    if (!Body)
      return nullptr;
    LastBranchWasBlock = CurrentToken == tok_block_end;
    if (LastBranchWasBlock)
      getNextToken();
    LastBranchHadTrailingEndOfLine = CurrentToken == tok_eol;
    Branches.push_back({std::move(Condition), std::move(Body)});
    consumeNewlines();

    if (CurrentToken != tok_elif)
      break;
    getNextToken(); // eat 'elif'
  }

  unique_ptr<ExpressionNode> Else;
  if (CurrentToken == tok_else) {
    getNextToken(); // eat 'else'
    if (CurrentToken != tok_colon)
      return LogErrorExpression("Expected ':' after else");
    getNextToken(); // eat ':'
    Else = ParseSuite();
    if (!Else)
      return nullptr;
  } else if (LastBranchWasBlock) {
    PendingTokens.push_front(CurrentToken);
    CurrentToken = tok_block_end;
  } else if (LastBranchHadTrailingEndOfLine) {
    PendingTokens.push_front(CurrentToken);
    CurrentToken = tok_eol;
  }

  unique_ptr<ExpressionNode> Tree = std::move(Else);
  for (auto It = Branches.rbegin(); It != Branches.rend(); ++It) {
    Tree = make_unique<IfStatementNode>(std::move(It->first),
                                        std::move(It->second), std::move(Tree));
  }
  return Tree;
}

static unique_ptr<ExpressionNode>
ParseFactor(); // forward declaration for ParseUnaryMinus

static bool IsIntType(ValueType Type);
static bool IsFloatType(ValueType Type);
static bool IsNumericType(ValueType Type);

static bool CanWidenInt(ValueType From, ValueType To) {
  if (From == To)
    return true;
  if (IsIntType(From) && IsIntType(To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    return FromBits <= ToBits;
  }
  return false;
}

static bool IsComparisonOp(int Operator) {
  return Operator == tok_less || Operator == tok_greater || Operator == tok_eq || Operator == tok_neq ||
         Operator == tok_leq || Operator == tok_geq;
}

static bool IsArithmeticOp(int Operator) {
  return Operator == tok_plus || Operator == tok_minus ||
         Operator == tok_star || Operator == tok_slash ||
         Operator == tok_percent;
}

// GetBinaryResultType decision table (Operator, L, R -> result)
//
// Arithmetic ops (+ - * etc.):
// - non-numeric (e.g., bool + int)             -> Error
// - float32 + float32                          -> float32
// - float64 + float64                          -> float64
// - float32 + float64 (either order)           -> float64
// - intN + intM (widen)                        -> wider int (e.g.,
// int16+int32->int32)
// - int + float (any float)                    -> float (handled by
// assignability)
// - otherwise                                  -> Error
//
// Comparison ops (== != < <= > >=):
// - bool ==/!= bool                            -> bool
// - bool < bool (or other non-numeric)         -> Error
// - numeric vs numeric                         -> bool (ints widen, float32/64
// allowed)
// - otherwise                                  -> Error
//
static ValueType GetBinaryResultType(int Operator, ValueType L, ValueType R) {
  if (IsArithmeticOp(Operator)) {
    if (!IsNumericType(L) || !IsNumericType(R))
      return ValueType::Error;
    // float + float (float32/float64): widen to float64 if mixed.
    if (IsFloatType(L) && IsFloatType(R)) {
      if (L == R)
        return L;
      if ((L == ValueType::Float && R == ValueType::Float64) ||
          (L == ValueType::Float64 && R == ValueType::Float))
        return ValueType::Float64;
      return ValueType::Error;
    }
    // int + int: widen to the larger integer type (e.g., int16 + int32 ->
    // int32).
    if (IsAssignable(L, R))
      return L;
    if (IsAssignable(R, L))
      return R;
    return ValueType::Error;
  }
  if (IsComparisonOp(Operator)) {
    // bool ==/!= bool: allowed; other comparisons on bool are rejected.
    if (L == ValueType::Bool && R == ValueType::Bool) {
      if (Operator == tok_eq || Operator == tok_neq)
        return ValueType::Bool;
      return ValueType::Error;
    }
    if (!IsNumericType(L) || !IsNumericType(R))
      return ValueType::Error;
    // numeric comparisons: allow mixed float32/float64 and mixed ints.
    if (IsFloatType(L) && IsFloatType(R)) {
      if (L == R)
        return ValueType::Bool;
      if ((L == ValueType::Float && R == ValueType::Float64) ||
          (L == ValueType::Float64 && R == ValueType::Float))
        return ValueType::Bool;
      return ValueType::Error;
    }
    if (IsAssignable(L, R) || IsAssignable(R, L))
      return ValueType::Bool;
    return ValueType::Error;
  }
  return ValueType::Error;
}

/// factor
///   = "-" factor | primary ;
/// Parse built-in unary minus into a UnaryExpressionNode with opcode '-'.
/// The operand is a full factor so unary chains work naturally
/// (e.g. -!x, --x, -(x+1)).
static unique_ptr<ExpressionNode> ParseUnaryMinus() {
  getNextToken(); // eat '-'
  auto Operand = ParseFactor();
  if (!Operand)
    return nullptr;
  if (!IsNumericType(Operand->getType()))
    return LogErrorExpression("Unary '-' requires a numeric operand");
  return make_unique<UnaryExpressionNode>(tok_minus, std::move(Operand), Operand->getType());
}

/// primary
///   = cast-expression
///   | name-expression
///   | number-expression
///   | boolean-literal
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  case tok_number:
    return ParseNumberExpression();
  case tok_name:
    return ParseNameExpression();
  case tok_true:
    getNextToken();
    return make_unique<BoolExpressionNode>(true);
  case tok_false:
    getNextToken();
    return make_unique<BoolExpressionNode>(false);
  case tok_int:
  case tok_int8:
  case tok_int16:
  case tok_int32:
  case tok_int64:
  case tok_float:
  case tok_float32:
  case tok_float64:
  case tok_bool:
    return ParseCastExpression();
  case tok_lparen:
    return ParseParenthesizedExpression();
  default:
    return LogErrorExpression(
        ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
  }
}

/// factor
///   = "-" factor | primary ;
static unique_ptr<ExpressionNode> ParseFactor() {
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();
  return ParsePrimary();
}

static unique_ptr<ExpressionNode>
MergeBinaryExpression(int Operator, unique_ptr<ExpressionNode> Left,
                      unique_ptr<ExpressionNode> Right) {
  ValueType ResultType =
      GetBinaryResultType(Operator, Left->getType(), Right->getType());
  if (ResultType == ValueType::Error)
    return LogErrorExpression("Type mismatch in binary operator");
  return make_unique<BinaryExpressionNode>(
      Operator, std::move(Left), std::move(Right), ResultType);
}

static unique_ptr<ExpressionNode>
ParseTermRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_star || CurrentToken == tok_slash ||
         CurrentToken == tok_percent) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseFactor();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseTerm() {
  auto Left = ParseFactor();
  if (!Left)
    return nullptr;
  return ParseTermRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseSumRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_plus || CurrentToken == tok_minus) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseTerm();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseSum() {
  auto Left = ParseTerm();
  if (!Left)
    return nullptr;
  return ParseSumRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseComparisonRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_eq || CurrentToken == tok_neq ||
         CurrentToken == tok_leq || CurrentToken == tok_geq ||
         CurrentToken == tok_less || CurrentToken == tok_greater) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseSum();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseComparison() {
  auto Left = ParseSum();
  if (!Left)
    return nullptr;
  return ParseComparisonRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseBinaryExpressionRight(unique_ptr<ExpressionNode> Left) {
  Left = ParseTermRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseSumRight(std::move(Left));
  if (!Left)
    return nullptr;
  return ParseComparisonRight(std::move(Left));
}

/// expression
///   = comparison ;
static unique_ptr<ExpressionNode> ParseExpression() {
  return ParseComparison();
}

/// return-statement
///   = "return" [ expression ] ;
static unique_ptr<ExpressionNode> ParseReturnStatement() {
  getNextToken(); // eat 'return'
  if (CurrentToken == tok_eol || CurrentToken == tok_dedent || CurrentToken == tok_eof) {
    if (CurrentFunctionReturnType != ValueType::None)
      return LogErrorExpression("Return value required");
    return make_unique<ReturnStatementNode>(nullptr);
  }

  ExpectedLiteralTypeGuard Guard(CurrentFunctionReturnType);
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  if (CurrentFunctionReturnType == ValueType::None)
    return LogErrorExpression("cannot return a value from a None function");
  if (!IsAssignable(CurrentFunctionReturnType, Expr->getType())) {
    return LogErrorExpression(("cannot return " + string(TypeName(Expr->getType())) +
                     " from function returning " +
                     string(TypeName(CurrentFunctionReturnType)))
                        .c_str());
  }
  return make_unique<ReturnStatementNode>(std::move(Expr));
}

static unique_ptr<ExpressionNode> ParseBreakStatement() {
  if (ParseLoopDepth <= 0)
    return LogErrorExpression("'break' used outside of a loop");
  getNextToken();
  return make_unique<BreakStatementNode>();
}

static unique_ptr<ExpressionNode> ParseContinueStatement() {
  if (ParseLoopDepth <= 0)
    return LogErrorExpression("'continue' used outside of a loop");
  getNextToken();
  return make_unique<ContinueStatementNode>();
}

static unique_ptr<ExpressionNode> ParseAssignmentRight(const string &Name) {
  if (!IsDeclaredVar(Name))
    return LogErrorExpression("Assignment to undeclared variable");
  ValueType VarType = LookupVarType(Name);
  getNextToken(); // eat '='

  ExpectedLiteralTypeGuard Guard(VarType);
  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  if (!IsAssignable(VarType, Right->getType()))
    return LogErrorExpression("Type mismatch in assignment");
  return make_unique<AssignmentStatementNode>(Name, std::move(Right), VarType);
}


// Parse name-led forms in a simple-statement:
//   assignstmt   : name "=" expression
//   expression   : name ...
// and reject trailing '=' when the parsed Left is not assignable.
static unique_ptr<ExpressionNode> ParseLeadingNameSimpleStatement() {
  string ParsedName = Name;
  getNextToken(); // eat name

  // Fast path for assignstmt: x = ...
  if (CurrentToken == tok_equal)
    return ParseAssignmentRight(ParsedName);

  // Otherwise parse as expression starting from name.
  auto Expr = ParseNameExpressionWithName(std::move(ParsedName));
  if (!Expr)
    return nullptr;
  Expr = ParseBinaryExpressionRight(std::move(Expr));
  if (!Expr)
    return nullptr;

  // Optional assignment tail: (<expr>) = ...
  if (CurrentToken != tok_equal)
    return Expr;

  const string *AssignedName = Expr->getLValueName();
  if (!AssignedName)
    return LogErrorExpression("Destination of '=' must be a variable");

  return ParseAssignmentRight(*AssignedName);
}

// Parse non-name-leading expression forms for a simple-statement and reject a
// trailing '=' so assignment diagnostics stay local and specific.
static unique_ptr<ExpressionNode> ParseNonLeadingNameSimpleStatement() {
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;

  if (CurrentToken != tok_equal)
    return Expr;

  return LogErrorExpression("Destination of '=' must be a variable");
}


/// simple-statement
///   = return-statement | break-statement | continue-statement
///   | variable-statement | assignment-statement | expression ;
static unique_ptr<ExpressionNode> ParseSimpleStatement() {
  if (CurrentToken == tok_return)
    return ParseReturnStatement();
  if (CurrentToken == tok_break)
    return ParseBreakStatement();
  if (CurrentToken == tok_continue)
    return ParseContinueStatement();
  if (CurrentToken == tok_var)
    return ParseVarStatement();
  if (CurrentToken == tok_name)
    return ParseLeadingNameSimpleStatement();
  return ParseNonLeadingNameSimpleStatement();
}

/// statement
///   = simple-statement | compound-statement ;
static unique_ptr<ExpressionNode> ParseStatement() {
  if (CurrentToken == tok_if)
    return ParseIfStatement();
  if (CurrentToken == tok_for)
    return ParseForStatement();
  if (CurrentToken == tok_while)
    return ParseWhileStatement();
  if (CurrentToken == tok_do)
    return ParseDoWhileStatement();
  return ParseSimpleStatement();
}

/// suite
///   = simple-statement | compound-statement | end-of-lines block ;
static unique_ptr<ExpressionNode> ParseSuite() {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken != tok_indent)
      return LogErrorExpression("Expected an indented block");
    return ParseBlock();
  }

  if (CurrentToken == tok_indent)
    return ParseBlock();

  return ParseStatement();
}

/// block
///   = indent statement { statement-separator statement } dedent ;
static unique_ptr<ExpressionNode> ParseBlock() {
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented block");
  getNextToken(); // eat INDENT

  BlockScopeGuard Scope;

  if (CurrentToken == tok_dedent)
    return LogErrorExpression("Expected at least one statement in block");

  vector<unique_ptr<ExpressionNode>> Stmts;

  while (true) {
    if (CurrentToken == tok_dedent)
      break;

    auto Stmt = ParseStatement();
    if (!Stmt)
      return nullptr;
    Stmts.push_back(std::move(Stmt));

    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }

    if (CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }

    if (CurrentToken == tok_dedent)
      break;

    return LogErrorExpression("Expected newline or end of block");
  }

  if (CurrentToken != tok_dedent)
    return LogErrorExpression("Expected end of block");

  // Consume DEDENT, but leave a synthetic separator visible to the enclosing
  // parser so it can distinguish "a nested block just ended" from arbitrary
  // trailing tokens without threading boolean state through every parser call.
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end

  return make_unique<BlockStatementNode>(std::move(Stmts));
}

/// function-signature
///   = name "(" [ parameters ] ")" ;
///
/// typed-parameter
///   = name ":" type ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  SourceLocation SignatureLoc = CurrentTokenLocation;

  // Callers consume the leading 'def', so the current token must be the
  // function name.
  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");
  string FunctionName = Name;
  getNextToken(); // eat function name

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  vector<pair<string, ValueType>> ParameterNames;
  getNextToken(); // eat '('

  if (CurrentToken != tok_rparen) {
    while (true) {
      if (CurrentToken != tok_name)
        return LogErrorSignature("Expected parameter name in function signature");
      string ArgName = Name;
      getNextToken(); // eat name

      if (CurrentToken != tok_colon)
        return LogErrorSignature(
            "Parameter requires a type annotation (e.g., ': int32')");
      getNextToken(); // eat ':'
      ValueType ArgType = ParseTypeToken();
      if (ArgType == ValueType::Error)
        return nullptr;
      if (ArgType == ValueType::None)
        return LogErrorSignature("Parameters cannot have None type");
      ParameterNames.push_back({ArgName, ArgType});

      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorSignature("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  getNextToken(); // eat ')'
  return make_unique<FunctionSignatureNode>(FunctionName, std::move(ParameterNames), SignatureLoc);
}

// DefaultType controls what return type is assumed when no '->' is present.
// In chapter 16, missing return types default to None.
static ValueType
ParseOptionalReturnType(ValueType DefaultType = ValueType::None) {
  if (CurrentToken != tok_arrow)
    return DefaultType;
  getNextToken(); // eat '->'
  ValueType Type = ParseTypeToken();
  return Type;
}

/// I parse the inline simple-statement or indented block portion of a
/// function-definition.
static unique_ptr<ExpressionNode> ParseFunctionBody() {
  // Allow the function body to start on the next line:
  //   def foo(x):
  //     x + 1
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken != tok_indent)
      return LogErrorExpression("Expected an indented block");
    return ParseBlock();
  }

  return ParseSimpleStatement();
}

/// function-definition
///   = "def" function-signature [ "->" type ] ":"
///     ( simple-statement | end-of-lines block ) ;
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // eat 'def'
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;
  ValueType RetType = ParseOptionalReturnType(ValueType::None);
  if (RetType == ValueType::Error)
    return nullptr;
  Signature->setReturnType(RetType);
  FunctionSignatures[Signature->getName()] = Signature->clone();
  ReturnTypeGuard RetGuard(RetType);
  FunctionScopeGuard Scope(Signature->getParameters());

  if (CurrentToken != tok_colon)
    return LogErrorFunction("Expected ':' in function definition");
  getNextToken(); // eat ':'
  unique_ptr<ExpressionNode> Body = ParseFunctionBody();

  if (Body) {
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
  }
  FunctionSignatures.erase(Signature->getName());
  return nullptr;
}

/// top-level-statement
///   = statement ;
static unique_ptr<ExpressionNode> ParseTopLevelStatement() {
  TopLevelParseGuard Guard;
  ReturnTypeGuard RetGuard(ValueType::None);
  auto Stmt = ParseStatement();
  if (!Stmt)
    return nullptr;
  LastTopLevelShouldPrint = Stmt->shouldPrintValue();
  return Stmt;
}

/// A top-level statement (e.g. "1 + 2", "var x = 1", "if ...") is wrapped in
/// an anonymous function so it fits the same FunctionDefinitionNode shape as everything
/// else. HandleTopLevelStatement compiles it into the JIT, calls it to get
/// the numeric result, then removes it from the JIT via a ResourceTracker.
static unique_ptr<FunctionDefinitionNode> ParseTopLevelStatementFunction() {
  auto Stmt = ParseTopLevelStatement();
  if (!Stmt)
    return nullptr;

  ValueType RetType = Stmt->getType();
  if (!Stmt->isReturnExpr() && RetType != ValueType::None)
    Stmt = make_unique<ReturnStatementNode>(std::move(Stmt));

  string FunctionName = "__pyxc.toplevel." + to_string(TopLevelExprCounter++);
  auto Signature = make_unique<FunctionSignatureNode>(
      FunctionName, vector<pair<string, ValueType>>(), CurrentTokenLocation, RetType);
  return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Stmt));
}

/// external
///   = "extern" "def" function-signature [ "->" type ] ;
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurrentToken != tok_def)
    return LogErrorSignature("Expected `def` after extern.");
  getNextToken(); // eat def
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;
  ValueType RetType = ParseOptionalReturnType();
  if (RetType == ValueType::Error)
    return nullptr;
  Signature->setReturnType(RetType);
  return Signature;
}

//===----------------------------------------===//
// Code Generation
//===----------------------------------------===//

// Core IR construction globals. Recreated for each new module.
// (See InitializeModuleAndManagers.)
//
// TheContext - Owns LLVM types/constants and uniquing tables.
static std::unique_ptr<LLVMContext> TheContext;
// TheModule - Current compilation unit handed to the JIT/emit path.
static std::unique_ptr<Module> TheModule;
// TheBuilder - Cursor used to append instructions into the current block.
static std::unique_ptr<IRBuilder<NoFolder>> TheBuilder;
// NamedValues - Maps variable names to allocas in the current function.
static std::map<std::string, AllocaInst *> NamedValues;
// InGlobalInit - True while emitting the synthetic global init function.
static bool InGlobalInit = false;
// ModuleHasGlobals - Tracks whether this module defines any globals.
static bool ModuleHasGlobals = false;
struct LoopControlTargets {
  BasicBlock *BreakTarget = nullptr;
  BasicBlock *ContinueTarget = nullptr;
};
static vector<LoopControlTargets> LoopControlStack;
// JIT - ORC JIT instance for REPL execution.
static std::unique_ptr<PyxcJIT> JIT;
// FunctionPasses - Per-function optimization pipeline (JIT).
static std::unique_ptr<FunctionPassManager> FunctionPasses;
// LoopAnalyses - Loop analysis manager (new PM).
static std::unique_ptr<LoopAnalysisManager> LoopAnalyses;
// FunctionAnalyses - Function analysis manager (new PM).
static std::unique_ptr<FunctionAnalysisManager> FunctionAnalyses;
// CallGraphAnalyses - CGSCC analysis manager (new PM).
static std::unique_ptr<CGSCCAnalysisManager> CallGraphAnalyses;
// ModuleAnalyses - Module analysis manager (new PM).
static std::unique_ptr<ModuleAnalysisManager> ModuleAnalyses;
// ExitOnErr - Crash-on-error wrapper for LLVM Error results.
static ExitOnError ExitOnErr;

static const char *TypeName(ValueType Type) {
  switch (Type) {
  case ValueType::None:
    return "None";
  case ValueType::Int:
    return "int";
  case ValueType::Int8:
    return "int8";
  case ValueType::Int16:
    return "int16";
  case ValueType::Int32:
    return "int32";
  case ValueType::Int64:
    return "int64";
  case ValueType::Float:
    return "float";
  case ValueType::Float32:
    return "float32";
  case ValueType::Float64:
    return "float64";
  case ValueType::Bool:
    return "bool";
  default:
    return "<error>";
  }
}

static bool IsIntType(ValueType Type) {
  return Type == ValueType::Int8 || Type == ValueType::Int16 ||
         Type == ValueType::Int32 || Type == ValueType::Int ||
         Type == ValueType::Int64;
}

static bool IsFloatType(ValueType Type) {
  return Type == ValueType::Float || Type == ValueType::Float32 ||
         Type == ValueType::Float64;
}

static bool IsNumericType(ValueType Type) {
  return IsIntType(Type) || IsFloatType(Type);
}

static Type *LLVMTypeFor(ValueType Type) {
  switch (Type) {
  case ValueType::Int: {
    unsigned bits = TheModule->getDataLayout().getPointerSizeInBits();
    return llvm::IntegerType::get(*TheContext, bits);
  }
  case ValueType::Int8:
    return Type::getInt8Ty(*TheContext);
  case ValueType::Int16:
    return Type::getInt16Ty(*TheContext);
  case ValueType::Int32:
    return Type::getInt32Ty(*TheContext);
  case ValueType::Int64:
    return Type::getInt64Ty(*TheContext);
  case ValueType::Float:
    return Type::getDoubleTy(*TheContext);
  case ValueType::Float32:
    return Type::getFloatTy(*TheContext);
  case ValueType::Float64:
    return Type::getDoubleTy(*TheContext);
  case ValueType::Bool:
    return Type::getInt1Ty(*TheContext);
  case ValueType::None:
    return Type::getVoidTy(*TheContext);
  default:
    return nullptr;
  }
}

static bool IsAssignable(ValueType Dest, ValueType Src) {
  if (Dest == Src)
    return true;
  if ((Dest == ValueType::Float && Src == ValueType::Float64) ||
      (Dest == ValueType::Float64 && Src == ValueType::Float))
    return true;
  if (IsFloatType(Dest) && IsFloatType(Src)) {
    unsigned DestBits = LLVMTypeFor(Dest)->getScalarSizeInBits();
    unsigned SrcBits = LLVMTypeFor(Src)->getScalarSizeInBits();
    if (DestBits >= SrcBits)
      return true;
  }
  if (IsIntType(Dest) && IsIntType(Src) && CanWidenInt(Src, Dest))
    return true;
  if (IsFloatType(Dest) && IsIntType(Src))
    return true;
  return false;
}

/// LogErrorValue - Codegen-level error helper. Delegates to LogErrorExpression for printing,
/// then returns nullptr so codegen callers can write: return LogErrorValue("msg");
Value *LogErrorValue(const string &ErrorMessage) {
  LogErrorExpression(ErrorMessage);
  return nullptr;
}

/// CreateEntryBlockAlloca - Create a stack slot in the current function's
/// entry block for a mutable variable.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VarName,
                                          ValueType Type) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(LLVMTypeFor(Type), nullptr, VarName);
}

static Constant *ZeroConstant(ValueType Type) {
  switch (Type) {
  case ValueType::Int8:
    return ConstantInt::get(Type::getInt8Ty(*TheContext), 0);
  case ValueType::Int16:
    return ConstantInt::get(Type::getInt16Ty(*TheContext), 0);
  case ValueType::Int32:
    return ConstantInt::get(Type::getInt32Ty(*TheContext), 0);
  case ValueType::Int:
    return ConstantInt::get(LLVMTypeFor(Type), 0);
  case ValueType::Int64:
    return ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  case ValueType::Float:
    return ConstantFP::get(*TheContext, APFloat(0.0));
  case ValueType::Float32:
    return ConstantFP::get(Type::getFloatTy(*TheContext), 0.0);
  case ValueType::Float64:
    return ConstantFP::get(*TheContext, APFloat(0.0));
  case ValueType::Bool:
    return ConstantInt::get(Type::getInt1Ty(*TheContext), 0);
  default:
    return nullptr;
  }
}

static Value *EmitCast(Value *V, ValueType From, ValueType To) {
  if (!V)
    return nullptr;
  if (From == To)
    return V;
  // Integer ↔ float conversions.
  if (IsIntType(From) && IsFloatType(To))
    return TheBuilder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
  if (IsFloatType(From) && IsIntType(To))
    return TheBuilder->CreateFPToSI(V, LLVMTypeFor(To), "fptosi");
  // Integer resize (trunc or sign-extend).
  if (IsIntType(From) && IsIntType(To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    if (ToBits < FromBits)
      return TheBuilder->CreateTrunc(V, LLVMTypeFor(To), "trunc");
    return TheBuilder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  // Float resize.
  if (IsFloatType(From) && IsFloatType(To)) {
    if (From == ValueType::Float32 && To == ValueType::Float64)
      return TheBuilder->CreateFPExt(V, LLVMTypeFor(To), "fpext");
    return TheBuilder->CreateFPTrunc(V, LLVMTypeFor(To), "fptrunc");
  }
  // Cast to bool: any nonzero value is true.
  if (To == ValueType::Bool) {
    if (IsIntType(From) || From == ValueType::Bool)
      return TheBuilder->CreateICmpNE(V, ConstantInt::get(LLVMTypeFor(From), 0),
                                   "tobool");
    if (IsFloatType(From))
      return TheBuilder->CreateFCmpONE(V, ConstantFP::get(LLVMTypeFor(From), 0.0),
                                    "tobool");
  }
  return nullptr;
}

static Value *EmitImplicitCast(Value *V, ValueType From, ValueType To) {
  if (From == To)
    return V;
  if (IsFloatType(From) && IsFloatType(To)) {
    unsigned FromBits = LLVMTypeFor(From)->getScalarSizeInBits();
    unsigned ToBits = LLVMTypeFor(To)->getScalarSizeInBits();
    if (FromBits == ToBits)
      return V;
    if (FromBits < ToBits)
      return TheBuilder->CreateFPExt(V, LLVMTypeFor(To), "fpext");
    return nullptr;
  }
  if (IsIntType(From) && IsIntType(To) && CanWidenInt(From, To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    if (FromBits == ToBits)
      return V;
    return TheBuilder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  if (IsIntType(From) && IsFloatType(To))
    return TheBuilder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
  return nullptr;
}

static Value *ToBool(Value *V, ValueType Type) {
  if (!V)
    return nullptr;
  if (Type == ValueType::Bool)
    return V;
  return nullptr;
}

/// GetGlobalVariable - Return a module-local GlobalVariable* for Name.
///
/// If the global is defined in this module, returns it. If the global exists
/// in another module (tracked by GlobalVarTypes), emit a declaration in the
/// current module and return that. Returns nullptr if the name is unknown.
static GlobalVariable *GetGlobalVariable(const string &Name) {
  if (auto *GV = TheModule->getNamedGlobal(Name))
    return GV;

  if (!GlobalVarTypes.count(Name))
    return nullptr;
  auto *Type = LLVMTypeFor(GlobalVarTypes[Name]);
  return new GlobalVariable(*TheModule, Type, false,
                            GlobalValue::ExternalLinkage, nullptr, Name);
}

static FunctionSignatureNode *GetFunctionSignature(const string &Name) {
  auto It = FunctionSignatures.find(Name);
  if (It != FunctionSignatures.end())
    return It->second.get();
  return nullptr;
}

/// getFunction - Resolve a function name to an LLVM Function* in the current
/// module, re-emitting a declaration from FunctionSignatures if necessary.
///
/// Because each top-level input gets its own Module, a function defined in an
/// earlier module is no longer in TheModule->getFunction(). When that happens
/// we look up its FunctionSignatureNode in FunctionSignatures and call codegen() on it,
/// which emits a fresh 'declare' with ExternalLinkage in the current module.
/// The JIT resolves that extern to the already-compiled body at link time.
Function *getFunction(const std::string &Name) {
  // Fast path: declaration or definition already in the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // Slow path: re-emit a declaration from the saved function signature.
  auto FI = FunctionSignatures.find(Name);
  if (FI != FunctionSignatures.end())
    return FI->second->codegen();

  return nullptr;
}

/// NumberExpressionNode::codegen - A numeric literal becomes a constant value.
///
Value *NumberExpressionNode::codegen() {
  if (IsIntLiteral)
    return ConstantInt::get(*TheContext, IntegerValue);
  if (IsFloatType(getType()))
    return ConstantFP::get(*TheContext, FloatValue);
  return LogErrorValue("Unknown numeric literal type");
}

Value *BoolExpressionNode::codegen() {
  return ConstantInt::get(Type::getInt1Ty(*TheContext), Value ? 1 : 0);
}

/// NameExpressionNode::codegen - A variable reference loads the current value
/// from the variable's stack slot.
Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second)
    return TheBuilder->CreateLoad(LLVMTypeFor(getType()), It->second,
                               Name.c_str());

  if (auto *GV = GetGlobalVariable(Name))
    return TheBuilder->CreateLoad(LLVMTypeFor(getType()), GV, Name.c_str());

  return LogErrorValue("Unknown variable name: " + Name);
}

/// AssignmentStatementNode::codegen - Evaluate the Right, store it into the variable's
/// stack slot, and produce the assigned value.
Value *AssignmentStatementNode::codegen() {
  Value *Val = Expr->codegen();
  if (!Val)
    return nullptr;
  Val = EmitImplicitCast(Val, Expr->getType(), getType());
  if (!Val)
    return LogErrorValue("Type mismatch in assignment");

  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second) {
    TheBuilder->CreateStore(Val, It->second);
    return Val;
  }

  if (auto *GV = GetGlobalVariable(Name)) {
    TheBuilder->CreateStore(Val, GV);
    return Val;
  }

  return LogErrorValue("Unknown variable name");
}

/// ReturnStatementNode::codegen - Emit a return from the current function.
Value *ReturnStatementNode::codegen() {
  if (!Expr) {
    TheBuilder->CreateRetVoid();
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Value *RetVal = Expr->codegen();
  if (!RetVal)
    return nullptr;
  RetVal = EmitImplicitCast(RetVal, Expr->getType(), CurrentFunctionReturnType);
  if (!RetVal)
    return LogErrorValue("Type mismatch in return");
  TheBuilder->CreateRet(RetVal);
  return RetVal;
}

/// BlockStatementNode::codegen - Evaluate statements in order.
/// Saves and restores NamedValues to implement block scoping: variables
/// declared inside the block are not visible after it exits.
Value *BlockStatementNode::codegen() {
  auto SavedBindings = NamedValues;

  Value *Last = nullptr;
  for (auto &Stmt : Stmts) {
    if (TheBuilder->GetInsertBlock()->getTerminator())
      break;
    Last = Stmt->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      return nullptr;
    }
  }

  NamedValues = SavedBindings;

  if (!Last)
    return LogErrorValue("Empty block");

  // Blocks are statement sequences. If control reaches the end without an
  // explicit return, the block's implicit value is always 0.0.
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// BinaryExpressionNode::codegen - Recursively codegen both operands, then emit the
/// operator-specific instruction.
///
/// The string arguments to each Create* call ("addtmp", "multmp", etc.) are
/// hint names for the SSA value. LLVM uses them when printing IR, appending a
/// numeric suffix when the same hint would otherwise repeat. They have no
/// effect on correctness.
///
/// Comparison operators ('<', '>', tok_eq, tok_neq, tok_leq, tok_geq) each
/// require two steps: CreateFCmp* produces a 1-bit integer (i1) — LLVM's
/// boolean type. Since Pyxc treats everything as double, CreateUIToFP widens
/// it: false -> 0.0, true -> 1.0. This double boolean is then used as the
/// condition value in if/for expressions, where fcmp one != 0.0 converts it
/// back to i1.
/// We use ordered floating-point comparisons for ==, <, <=, >, and >=, so
/// comparisons involving NaN evaluate false. For != we use unordered
/// comparison, so x != NaN evaluates true.
Value *BinaryExpressionNode::codegen() {
  Value *L = Left->codegen();
  if (!L)
    return nullptr;

  Value *R = Right->codegen();
  if (!R)
    return nullptr;
  ValueType LType = Left->getType();
  ValueType RType = Right->getType();

  switch (Operator) {
  case tok_plus:
  case tok_minus:
  case tok_star:
  case tok_slash:
  case tok_percent: {
    L = EmitImplicitCast(L, LType, getType());
    R = EmitImplicitCast(R, RType, getType());
    if (!L || !R)
      return LogErrorValue("Type mismatch in arithmetic");
    if (IsFloatType(getType())) {
      if (Operator == tok_plus)
        return TheBuilder->CreateFAdd(L, R, "addtmp");
      if (Operator == tok_minus)
        return TheBuilder->CreateFSub(L, R, "subtmp");
      if (Operator == tok_slash)
        return TheBuilder->CreateFDiv(L, R, "divtmp");
      if (Operator == tok_percent)
        return TheBuilder->CreateFRem(L, R, "remtmp");
      return TheBuilder->CreateFMul(L, R, "multmp");
    }
    if (Operator == tok_plus)
      return TheBuilder->CreateAdd(L, R, "addtmp");
    if (Operator == tok_minus)
      return TheBuilder->CreateSub(L, R, "subtmp");
    if (Operator == tok_slash)
      return TheBuilder->CreateSDiv(L, R, "divtmp");
    if (Operator == tok_percent)
      return TheBuilder->CreateSRem(L, R, "remtmp");
    return TheBuilder->CreateMul(L, R, "multmp");
  }
  case tok_less:
  case tok_greater:
  case tok_eq:
  case tok_neq:
  case tok_leq:
  case tok_geq: {
    ValueType CompareType = ValueType::Error;
    if (LType == ValueType::Bool && RType == ValueType::Bool) {
      if (Operator != tok_eq && Operator != tok_neq)
        return LogErrorValue("Type mismatch in comparison");
      CompareType = ValueType::Bool;
    } else if (IsFloatType(LType) && IsFloatType(RType)) {
      if (LType == RType)
        CompareType = LType;
      else if ((LType == ValueType::Float && RType == ValueType::Float64) ||
               (LType == ValueType::Float64 && RType == ValueType::Float))
        CompareType = ValueType::Float64;
    } else if (IsFloatType(LType) && IsIntType(RType)) {
      if (IsAssignable(LType, RType))
        CompareType = LType;
    } else if (IsFloatType(RType) && IsIntType(LType)) {
      if (IsAssignable(RType, LType))
        CompareType = RType;
    } else if (IsIntType(LType) && IsIntType(RType)) {
      if (IsAssignable(LType, RType))
        CompareType = LType;
      else if (IsAssignable(RType, LType))
        CompareType = RType;
    }

    if (CompareType == ValueType::Error)
      return LogErrorValue("Type mismatch in comparison");

    if (CompareType == ValueType::Bool) {
      if (Operator == tok_eq)
        return TheBuilder->CreateICmpEQ(L, R, "cmptmp");
      return TheBuilder->CreateICmpNE(L, R, "cmptmp");
    }

    L = EmitImplicitCast(L, LType, CompareType);
    R = EmitImplicitCast(R, RType, CompareType);
    if (!L || !R)
      return LogErrorValue("Type mismatch in comparison");

    if (IsFloatType(CompareType)) {
      switch (Operator) {
      case tok_less:
        return TheBuilder->CreateFCmpOLT(L, R, "cmptmp");
      case tok_greater:
        return TheBuilder->CreateFCmpOGT(L, R, "cmptmp");
      case tok_eq:
        return TheBuilder->CreateFCmpOEQ(L, R, "cmptmp");
      case tok_neq:
        return TheBuilder->CreateFCmpUNE(L, R, "cmptmp");
      case tok_leq:
        return TheBuilder->CreateFCmpOLE(L, R, "cmptmp");
      case tok_geq:
        return TheBuilder->CreateFCmpOGE(L, R, "cmptmp");
      default:
        break;
      }
    } else {
      switch (Operator) {
      case tok_less:
        return TheBuilder->CreateICmpSLT(L, R, "cmptmp");
      case tok_greater:
        return TheBuilder->CreateICmpSGT(L, R, "cmptmp");
      case tok_eq:
        return TheBuilder->CreateICmpEQ(L, R, "cmptmp");
      case tok_neq:
        return TheBuilder->CreateICmpNE(L, R, "cmptmp");
      case tok_leq:
        return TheBuilder->CreateICmpSLE(L, R, "cmptmp");
      case tok_geq:
        return TheBuilder->CreateICmpSGE(L, R, "cmptmp");
      default:
        break;
      }
    }
    return LogErrorValue("Type mismatch in comparison");
  }
  default:
    break;
  }

  return LogErrorValue("Invalid binary operator: " + FormatTokenForMessage(Operator));
}

/// UnaryExpressionNode::codegen - Emit built-in unary minus directly.
Value *UnaryExpressionNode::codegen() {
  Value *Operator = Operand->codegen();
  if (!Operator)
    return nullptr;

  if (Opcode != tok_minus)
    return LogErrorValue("Invalid unary operator: " +
                         FormatTokenForMessage(Opcode));

  if (IsIntType(getType()))
    return TheBuilder->CreateNeg(Operator, "negtmp");
  if (IsFloatType(getType()))
    return TheBuilder->CreateFNeg(Operator, "negtmp");
  return LogErrorValue("Unary '-' not supported for this type");
}

/// CastExpressionNode::codegen - Emit explicit int/double casts.
Value *CastExpressionNode::codegen() {
  Value *V = Expr->codegen();
  if (!V)
    return nullptr;
  Value *Cast = EmitCast(V, Expr->getType(), TargetType);
  if (!Cast)
    return LogErrorValue("Invalid cast");
  return Cast;
}

/// CallExpressionNode::codegen - Look up the callee by name in TheModule, verify the
/// argument count, codegen each argument, then emit a call instruction.
///
/// getFunction searches the module for a declaration or function-definition with the
/// given name. This covers both previous 'extern' declarations and previously
/// defined functions. The argument count check catches mismatches that a typed
/// language would catch statically.
Value *CallExpressionNode::codegen() {
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorValue("Unknown function: '" + Callee + "'");

  if (CalleeF->arg_size() != Arguments.size())
    return LogErrorValue(
        "Incorrect number of arguments in call to '" + Callee +
        "': expected " + to_string(CalleeF->arg_size()) + ", got " +
        to_string(Arguments.size()));

  FunctionSignatureNode *Signature = GetFunctionSignature(Callee);
  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Arguments.size(); i != e; ++i) {
    Value *ArgVal = Arguments[i]->codegen();
    if (!ArgVal)
      return nullptr;
    if (Signature) {
      ArgVal =
          EmitImplicitCast(ArgVal, Arguments[i]->getType(), Signature->getParameterType(i));
      if (!ArgVal)
        return LogErrorValue("Argument type mismatch");
    }
    ArgsV.push_back(ArgVal);
  }

  if (getType() == ValueType::None)
    return TheBuilder->CreateCall(CalleeF, ArgsV);
  return TheBuilder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/// IfStatementNode::codegen - Emit LLVM IR for a statement-style if.
///
/// If there is no else branch, control falls through to the merge block.
/// The statement evaluates to 0.0.
Value *IfStatementNode::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  CondV = ToBool(CondV, Cond->getType());
  if (!CondV)
    return LogErrorValue("Invalid condition type");

  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);

  TheBuilder->CreateCondBr(CondV, ThenBB, ElseBB);

  TheBuilder->SetInsertPoint(ThenBB);
  if (!Then->codegen())
    return nullptr;
  bool ThenTerminated = TheBuilder->GetInsertBlock()->getTerminator();
  if (!ThenTerminated)
    TheBuilder->CreateBr(MergeBB);

  TheBuilder->SetInsertPoint(ElseBB);
  if (Else) {
    if (!Else->codegen())
      return nullptr;
  }
  bool ElseTerminated = TheBuilder->GetInsertBlock()->getTerminator();
  if (!ElseTerminated)
    TheBuilder->CreateBr(MergeBB);

  if (Else && ThenTerminated && ElseTerminated) {
    TheBuilder->SetInsertPoint(MergeBB);
    TheBuilder->CreateUnreachable();
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  TheBuilder->SetInsertPoint(MergeBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// ForStatementNode::codegen - Emit LLVM IR for a for statement using a mutable
/// stack slot for the loop variable.
Value *ForStatementNode::codegen() {
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  Value *VarPtr = nullptr;
  AllocaInst *Alloca = nullptr;
  AllocaInst *OldVal = nullptr;
  if (IsVarDecl) {
    auto OldIt = NamedValues.find(VarName);
    OldVal = (OldIt != NamedValues.end()) ? OldIt->second : nullptr;
    Alloca = CreateEntryBlockAlloca(TheFunction, VarName, VarType);
    VarPtr = Alloca;
    NamedValues[VarName] = Alloca;
  } else {
    auto It = NamedValues.find(VarName);
    if (It != NamedValues.end() && It->second)
      VarPtr = It->second;
    else if (auto *GV = GetGlobalVariable(VarName))
      VarPtr = GV;
    else
      return LogErrorValue("Unknown variable name");
  }

  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;
  StartVal = EmitImplicitCast(StartVal, Start->getType(), VarType);
  if (!StartVal)
    return LogErrorValue("Type mismatch in for loop start");

  TheBuilder->CreateStore(StartVal, VarPtr);

  BasicBlock *CondBB =
      BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
  BasicBlock *BodyBB =
      BasicBlock::Create(*TheContext, "loop_body", TheFunction);
  BasicBlock *StepBB =
      BasicBlock::Create(*TheContext, "loop_step", TheFunction);
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "after_loop", TheFunction);

  TheBuilder->CreateBr(CondBB);

  TheBuilder->SetInsertPoint(CondBB);


  Value *CondVal = Cond->codegen();
  if (!CondVal)
    return nullptr;
  CondVal = ToBool(CondVal, Cond->getType());
  if (!CondVal)
    return LogErrorValue("Invalid loop condition type");
  TheBuilder->CreateCondBr(CondVal, BodyBB, AfterBB);

  TheBuilder->SetInsertPoint(BodyBB);

  LoopControlStack.push_back({AfterBB, StepBB});
  if (!Body->codegen()) {
    LoopControlStack.pop_back();
    return nullptr;
  }
  LoopControlStack.pop_back();
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateBr(StepBB);

  TheBuilder->SetInsertPoint(StepBB);

  Value *CurVar = TheBuilder->CreateLoad(LLVMTypeFor(VarType), VarPtr, VarName);
  Value *StepVal = Step->codegen();
  if (!StepVal)
    return nullptr;
  StepVal = EmitImplicitCast(StepVal, Step->getType(), VarType);
  if (!StepVal)
    return LogErrorValue("Type mismatch in for loop step");
  Value *NextVar = nullptr;
  if (VarType == ValueType::Float64)
    NextVar = TheBuilder->CreateFAdd(CurVar, StepVal, "nextvar");
  else
    NextVar = TheBuilder->CreateAdd(CurVar, StepVal, "nextvar");
  TheBuilder->CreateStore(NextVar, VarPtr);
  TheBuilder->CreateBr(CondBB);

  TheBuilder->SetInsertPoint(AfterBB);

  if (IsVarDecl) {
    if (OldVal)
      NamedValues[VarName] = OldVal;
    else
      NamedValues.erase(VarName);
  }

  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *WhileStatementNode::codegen() {
  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();
  BasicBlock *ConditionBlock =
      BasicBlock::Create(*TheContext, "while.condition", TheFunction);
  BasicBlock *BodyBlock =
      BasicBlock::Create(*TheContext, "while.body", TheFunction);
  BasicBlock *AfterBlock =
      BasicBlock::Create(*TheContext, "while.after", TheFunction);

  TheBuilder->CreateBr(IsDoWhile ? BodyBlock : ConditionBlock);

  if (!IsDoWhile) {
    TheBuilder->SetInsertPoint(ConditionBlock);
    Value *ConditionValue = Cond->codegen();
    if (!ConditionValue)
      return nullptr;
    ConditionValue = ToBool(ConditionValue, Cond->getType());
    if (!ConditionValue)
      return LogErrorValue("Invalid loop condition type");
    TheBuilder->CreateCondBr(ConditionValue, BodyBlock, AfterBlock);
  }

  TheBuilder->SetInsertPoint(BodyBlock);
  LoopControlStack.push_back({AfterBlock, ConditionBlock});
  if (!Body->codegen()) {
    LoopControlStack.pop_back();
    return nullptr;
  }
  LoopControlStack.pop_back();
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateBr(ConditionBlock);

  TheBuilder->SetInsertPoint(ConditionBlock);
  if (IsDoWhile) {
    Value *ConditionValue = Cond->codegen();
    if (!ConditionValue)
      return nullptr;
    ConditionValue = ToBool(ConditionValue, Cond->getType());
    if (!ConditionValue)
      return LogErrorValue("Invalid loop condition type");
    TheBuilder->CreateCondBr(ConditionValue, BodyBlock, AfterBlock);
  }

  TheBuilder->SetInsertPoint(AfterBlock);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *BreakStatementNode::codegen() {
  if (LoopControlStack.empty())
    return LogErrorValue("'break' used outside of a loop");
  TheBuilder->CreateBr(LoopControlStack.back().BreakTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *ContinueStatementNode::codegen() {
  if (LoopControlStack.empty())
    return LogErrorValue("'continue' used outside of a loop");
  TheBuilder->CreateBr(LoopControlStack.back().ContinueTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// VarStatementNode::codegen - Allocate mutable local variables and initialize them.
Value *VarStatementNode::codegen() {
  if (InGlobalInit) {
    for (auto &Var : VarNames) {
      const string &VarName = Var.Name;
      ValueType VarType = Var.Type;
      ExpressionNode *Init = Var.Init.get();

      auto *GV = TheModule->getNamedGlobal(VarName);
      if (GV && !GV->isDeclaration())
        return LogErrorValue("Global variable already defined");
      if (GV && GV->getValueType() != LLVMTypeFor(VarType))
        return LogErrorValue("Global variable type mismatch");

      if (!GV) {
        auto *Type = LLVMTypeFor(VarType);
        GV = new GlobalVariable(*TheModule, Type, false,
                                GlobalValue::ExternalLinkage,
                                ZeroConstant(VarType), VarName);
      } else if (GV->isDeclaration()) {
        GV->setInitializer(ZeroConstant(VarType));
        GV->setLinkage(GlobalValue::ExternalLinkage);
      }

      ModuleHasGlobals = true;

      Value *InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
      InitVal = EmitImplicitCast(InitVal, Init->getType(), VarType);
      if (!InitVal)
        return LogErrorValue("Type mismatch in variable initialization");

      TheBuilder->CreateStore(InitVal, GV);
    }

    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.Name;
    ValueType VarType = Var.Type;
    ExpressionNode *Init = Var.Init.get();

    Value *InitVal = Init->codegen();
    if (!InitVal)
      return nullptr;
    InitVal = EmitImplicitCast(InitVal, Init->getType(), VarType);
    if (!InitVal)
      return LogErrorValue("Type mismatch in variable initialization");

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName, VarType);
    TheBuilder->CreateStore(InitVal, Alloca);
    NamedValues[VarName] = Alloca;
  }

  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// FunctionSignatureNode::codegen - Create a function declaration in TheModule: name,
/// return type, and parameter types.
///
/// ExternalLinkage makes the function visible outside this module. That is
/// what allows 'extern def sin(x)' to link against the C library's sin at
/// runtime, and what lets 'def foo(...)' be called from later expressions in
/// the same session.
///
/// Argument.setName() is optional — it only affects the printed IR, making output
/// read as 'double %a, double %b' rather than 'double %0, double %1'.
Function *FunctionSignatureNode::codegen() {
  std::vector<Type *> ParameterTypes;
  ParameterTypes.reserve(Parameters.size());
  for (const auto &Parameter : Parameters)
    ParameterTypes.push_back(LLVMTypeFor(Parameter.second));
  FunctionType *LLVMFunctionType = FunctionType::get(LLVMTypeFor(ReturnType), ParameterTypes,
                                       false /* not variadic */);

  Function *TheFunction =
      Function::Create(LLVMFunctionType, Function::ExternalLinkage, Name,
                       TheModule.get());

  // Name arguments so the printed IR is readable.
  unsigned ParameterIndex = 0;
  for (auto &Argument : TheFunction->args())
    Argument.setName(Parameters[ParameterIndex++].first);


  return TheFunction;
}

/// FunctionDefinitionNode::codegen - Generate IR for a complete function function-definition.
///
/// Four steps:
///
/// 1. Register the function signature. The FunctionSignatureNode is moved into FunctionSignatures
///    so that future modules can re-emit a declaration for this function via
///    getFunction(). A reference is kept for the getFunction() call below.
///    getFunction() either finds an existing declaration in the current module
///    (e.g. from a prior 'extern def') or calls Signature->codegen() to create one.
///
/// 2. Create the entry BasicBlock and point the TheBuilder at it. A basic block
///    is a straight-line sequence of instructions with one entry and one exit.
///    Every function starts with exactly one entry block.
///
/// 3. Populate NamedValues. Clear the table, create an entry-block alloca for
///    each argument, store the incoming argument value into it, and map the
///    variable name to that stack slot. This gives parameters and mutable local
///    variables the same load/store representation.
///
/// 4. Codegen the body expression. On success, emit 'ret', run verifyFunction
///    (LLVM's internal consistency checker), then run FunctionPasses to apply the
///    optimisation pipeline. On failure, eraseFromParent() removes the
///    partially-built function so no broken declaration is left in the module.
Function *FunctionDefinitionNode::codegen() {
  const string FunctionName = Signature->getName();

  // Step 1: register the function signature and resolve the Function*.
  auto &FunctionSignature = *Signature;
  FunctionSignatures[FunctionName] = std::move(Signature);

  // Step 1: reuse an existing `extern` declaration if one exists.
  Function *TheFunction = getFunction(FunctionName);

  // Bail if the function is already fully defined — redefinition is an error.
  if (TheFunction && !TheFunction->empty()) {
    LogErrorExpression(
        "Function '" + FunctionName + "' cannot be redefined");
    return nullptr;
  }

  if (!TheFunction)
    return nullptr;

  ValueType SavedRetType = CurrentFunctionReturnType;
  CurrentFunctionReturnType = FunctionSignature.getReturnType();

  // Step 2: create the entry block and point the builder at it.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  TheBuilder->SetInsertPoint(BB);

  // Step 3: I store each argument in an entry-block stack slot and map its
  // parameter name to that slot. When I generate the body, I resolve each
  // parameter reference through this table in NameExpressionNode::codegen().
  NamedValues.clear();
  LoopControlStack.clear();
  size_t ArgTypeIndex = 0;
  for (auto &Argument : TheFunction->args()) {
    ValueType ArgType = FunctionSignature.getParameterType(ArgTypeIndex++);
    AllocaInst *Alloca = CreateEntryBlockAlloca(
        TheFunction, std::string(Argument.getName()), ArgType);
    TheBuilder->CreateStore(&Argument, Alloca);
    NamedValues[std::string(Argument.getName())] = Alloca;
  }

  // Step 4: codegen the body, optimise, verify, or erase on failure.
  if (Value *BodyVal = Body->codegen()) {
    // If the body didn't already terminate the current block (e.g. via
    // return), only void/None functions may fall through. Non-None functions
    // must return explicitly.
    if (!TheBuilder->GetInsertBlock()->getTerminator()) {
      if (FunctionSignature.getReturnType() == ValueType::None) {
        TheBuilder->CreateRetVoid();
      } else {
        BasicBlock *CurBB = TheBuilder->GetInsertBlock();
        bool IsEntry = CurBB == &TheFunction->getEntryBlock();
        if (!IsEntry && pred_empty(CurBB)) {
          TheBuilder->CreateUnreachable();
        } else {
          LogErrorValue("Non-None function must return a value");
          TheFunction->eraseFromParent();
          CurrentFunctionReturnType = SavedRetType;
          return nullptr;
        }
      }
    }
    verifyFunction(*TheFunction);

    // Run the optimisation pipeline: InstCombine, Reassociate, GVN,
    // SimplifyCFG.
    FunctionPasses->run(*TheFunction, *FunctionAnalyses);
    CurrentFunctionReturnType = SavedRetType;
    return TheFunction;
  }

  // Body codegen failed — remove the incomplete function so it cannot be
  // called and does not pollute the module handed to the JIT.
  TheFunction->eraseFromParent();
  CurrentFunctionReturnType = SavedRetType;
  return nullptr;
}

//===----------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------===//

static vector<unique_ptr<ExpressionNode>> FileTopLevelStatements;

/// ResetParserStateForFile - Clear parser/compiler state between input files.
///
/// Multi-file compilation emits each source into its own module, so symbols,
/// globals, and top-level statements should not leak across files.
static void ResetParserStateForFile() {
  FunctionSignatures.clear();
  GlobalVarTypes.clear();
  GlobalVarDecls.clear();
  VarScopes.clear();
  FileTopLevelStatements.clear();
  LastTopLevelShouldPrint = true;
  InGlobalInit = false;
  ModuleHasGlobals = false;
  HadError = false;
}

/// InitializeModuleAndManagers - Create a fresh module, IR builder, and
/// optimisation pipeline.
///
/// Called once at startup and again after every top-level input that hands
/// its module to the JIT. Because the JIT takes ownership of TheModule via
/// ThreadSafeModule, we cannot keep emitting into the old module — a new one
/// must be created for every subsequent function-definition or expression.
///
/// The optimisation pipeline is also recreated each time because
/// FunctionPassManager is tied to a specific LLVMContext.
///
/// Pipeline:
///   PromotePass     - Mem2Reg: promote stack slots to SSA registers.
///   InstCombinePass  - Peephole rewrites: a+0->a, x*2->x<<1, etc.
///   ReassociatePass  - Reorder commutative ops to expose more folding:
///                      (x+2)+3 -> x+(2+3) -> x+5.
///   GVNPass          - Global Value Numbering: eliminate redundant loads and
///                      common sub-expressions across basic blocks.
///
/// The analysis managers are cross-registered so that a function pass that
/// needs loop information can reach LoopAnalyses, and so on.
static void InitializeModuleAndManagers(bool FreshContext = true) {
  // Fresh context and module for this compilation unit.
  if (FreshContext || !TheContext)
    TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  // Inform the module of the JIT's target data layout so codegen emits
  // correctly-sized types for the host machine.
  TheModule->setDataLayout(JIT->getDataLayout());

  TheBuilder = std::make_unique<IRBuilder<NoFolder>>(*TheContext);
  ModuleHasGlobals = false;

  // Pass and analysis managers.
  FunctionPasses = std::make_unique<FunctionPassManager>();
  LoopAnalyses = std::make_unique<LoopAnalysisManager>();
  FunctionAnalyses = std::make_unique<FunctionAnalysisManager>();
  CallGraphAnalyses = std::make_unique<CGSCCAnalysisManager>();
  ModuleAnalyses = std::make_unique<ModuleAnalysisManager>();

  // Cross-register so passes can access any analysis tier they need.
  PassBuilder PB;
  PB.registerModuleAnalyses(*ModuleAnalyses);
  PB.registerCGSCCAnalyses(*CallGraphAnalyses);
  PB.registerFunctionAnalyses(*FunctionAnalyses);
  PB.registerLoopAnalyses(*LoopAnalyses);
  PB.crossRegisterProxies(*LoopAnalyses, *FunctionAnalyses, *CallGraphAnalyses, *ModuleAnalyses);

  // With -O0 the pass manager stays empty. Any non-zero level uses the
  // optimization pipeline introduced earlier in the tutorial.
  if (OptLevel != 0) {
    FunctionPasses->addPass(PromotePass());
    FunctionPasses->addPass(InstCombinePass());
    FunctionPasses->addPass(ReassociatePass());
    FunctionPasses->addPass(GVNPass());
  }
}

/// DiscardRestOfLine - Panic-mode error recovery.
///
/// Advance past all remaining tokens on the current line, stopping before
/// tok_eol, tok_eof, or tok_dedent so MainLoop can handle it. Called after
/// any parse or codegen failure
/// and after any unexpected trailing token, ensuring the REPL always returns
/// to a clean state before printing the next prompt.
static void DiscardRestOfLine() {
  // I stop before consuming tok_eol, tok_eof, or tok_dedent so MainLoop()
  // can handle it.
  while (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_dedent)
    getNextToken();
}

/// HandleFunctionDefinition - Parse, optimise, and JIT-compile a 'def' function-definition.
///
/// On success: codegen + optimise the function (FunctionPasses runs inside
/// FunctionDefinitionNode::codegen), print the optimised IR, then hand the entire module
/// to the JIT via addModule. The JIT takes ownership of TheModule and
/// TheContext, so InitializeModuleAndManagers() is called immediately after to
/// create a fresh module for the next input. The compiled function remains
/// accessible in the JIT's symbol table for the rest of the session.
/// On parse failure or unexpected trailing tokens: discard the line.
static void HandleFunctionDefinition() {
  auto FnAST = ParseFunctionDefinition();
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (!FnAST || HasTrailing) {
    if (FnAST)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)));
    DiscardRestOfLine();
    return;
  }
  if (auto *FnIR = FnAST->codegen()) {
    Log("Parsed a function definition.\n");
    if (ShouldDumpIR())
      FnIR->print(errs());
    if (!IsEmitMode()) {
      // Transfer the module to the JIT. TheModule is now invalid; reinitialise.
      ExitOnErr(JIT->addModule(
          ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
      InitializeModuleAndManagers();
    }
  }
}

/// HandleExtern - Parse and register an 'extern def' declaration.
///
/// On success: codegen the function signature (emits a 'declare' in the current module),
/// print it, then save the FunctionSignatureNode into FunctionSignatures. Saving into
/// FunctionSignatures is the critical step — when this module is handed to the JIT
/// and a new one is created, getFunction() uses FunctionSignatures to re-emit the
/// 'declare' in whichever module needs to call the extern.
/// On parse failure or unexpected trailing tokens: discard the line.
static void HandleExtern() {
  auto ProtoAST = ParseExtern();

  if (!ProtoAST || (CurrentToken != tok_eol && CurrentToken != tok_eof)) {
    if (ProtoAST)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)));
    DiscardRestOfLine();
    return;
  }

  // Reject conflicting redeclarations: in pyxc, function identity is just
  // name + arity. We validate types separately in the parser.
  auto Existing = FunctionSignatures.find(ProtoAST->getName());
  if (Existing != FunctionSignatures.end() &&
      Existing->second->getNumParameters() != ProtoAST->getNumParameters()) {
    LogErrorExpression((string("Conflicting declaration for function '") +
              ProtoAST->getName() + "'")
                 .c_str());
    DiscardRestOfLine();
    return;
  }

  if (auto *FnIR = ProtoAST->codegen()) {
    Log("Parsed an extern.\n");
    if (ShouldDumpIR())
      FnIR->print(errs());
    // Save the function signature so getFunction() can re-emit it in future modules.
    FunctionSignatures[ProtoAST->getName()] = std::move(ProtoAST);
  }
}

/// HandleTopLevelStatement - Compile, execute, and discard a bare expression.
///
/// The expression is wrapped in '__anon_expr' (a zero-argument function that
/// returns float64) so it goes through the same codegen path as everything
/// else.
///
/// Execution steps:
///   1. Codegen + optimise the anonymous function.
///   2. Print the optimised IR so the reader can inspect it.
///   3. Create a ResourceTracker scoped to this expression. The RT lets us
///      release the JIT-compiled code and its associated memory immediately
///      after execution, without disturbing other compiled functions.
///   4. Hand the module to the JIT (TheModule is now owned by the JIT).
///      Reinitialise for the next input.
///   5. Look up '__anon_expr' in the JIT, cast its address to a function
///      pointer, call it, and print the result.
///   6. Call RT->remove() to free the compiled code. The module was already
///      transferred to the JIT in step 4, so eraseFromParent() is not needed.
static void HandleTopLevelStatement() {
  auto FnAST = ParseTopLevelStatementFunction();
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (!FnAST || HasTrailing) {
    if (FnAST)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)));
    DiscardRestOfLine();
    return;
  }
  string FunctionName = FnAST->getName();
  ValueType RetType = FnAST->getReturnType();
  bool SavedInGlobalInit = InGlobalInit;
  InGlobalInit = true;
  if (auto *FnIR = FnAST->codegen()) {
    InGlobalInit = SavedInGlobalInit;
    Log("Parsed a top-level expression.\n");
    if (ShouldDumpIR())
      FnIR->print(errs());

    bool KeepModule = ModuleHasGlobals;

    if (KeepModule) {
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(JIT->addModule(std::move(TSM)));
      InitializeModuleAndManagers();
    } else {
      // ResourceTracker scopes the JIT memory for this expression so we can
      // free it precisely after the call, without affecting other symbols.
      auto RT = JIT->getMainJITDylib().createResourceTracker();

      // Transfer ownership of the module to the JIT; reinitialise for next
      // input.
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(JIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      // Locate the compiled function in the JIT's symbol table.
      auto ExprSymbol = ExitOnErr(JIT->lookup(FunctionName));

      if (RetType == ValueType::None) {
        void (*FP)() = ExprSymbol.toPtr<void (*)()>();
        FP();
      } else {
        switch (RetType) {
        case ValueType::Float64: {
          double (*FP)() = ExprSymbol.toPtr<double (*)()>();
          double result = FP();
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stdout, "%f\n", result);
          break;
        }
        case ValueType::Float32: {
          float (*FP)() = ExprSymbol.toPtr<float (*)()>();
          double result = static_cast<double>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stdout, "%f\n", result);
          break;
        }
        case ValueType::Int: {
          intptr_t (*FP)() = ExprSymbol.toPtr<intptr_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stdout, "%lld\n", result);
          break;
        }
        case ValueType::Int8: {
          int8_t (*FP)() = ExprSymbol.toPtr<int8_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stdout, "%lld\n", result);
          break;
        }
        case ValueType::Int16: {
          int16_t (*FP)() = ExprSymbol.toPtr<int16_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stdout, "%lld\n", result);
          break;
        }
        case ValueType::Int32: {
          int32_t (*FP)() = ExprSymbol.toPtr<int32_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stdout, "%lld\n", result);
          break;
        }
        case ValueType::Int64: {
          int64_t (*FP)() = ExprSymbol.toPtr<int64_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stdout, "%lld\n", result);
          break;
        }
        case ValueType::Bool: {
          bool (*FP)() = ExprSymbol.toPtr<bool (*)()>();
          bool result = FP();
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stdout, "%s\n", result ? "True" : "False");
          break;
        }
        default:
          break;
        }
      }

      // Release the compiled code and JIT memory for this expression.
      ExitOnErr(RT->remove());
      return;
    }

    // Keep-module path: call the compiled function after adding the module.
    auto ExprSymbol = ExitOnErr(JIT->lookup(FunctionName));
    if (RetType == ValueType::None) {
      void (*FP)() = ExprSymbol.toPtr<void (*)()>();
      FP();
    } else {
      switch (RetType) {
      case ValueType::Float64: {
        double (*FP)() = ExprSymbol.toPtr<double (*)()>();
        double result = FP();
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stdout, "%f\n", result);
        break;
      }
      case ValueType::Float32: {
        float (*FP)() = ExprSymbol.toPtr<float (*)()>();
        double result = static_cast<double>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stdout, "%f\n", result);
        break;
      }
      case ValueType::Int: {
        intptr_t (*FP)() = ExprSymbol.toPtr<intptr_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stdout, "%lld\n", result);
        break;
      }
      case ValueType::Int8: {
        int8_t (*FP)() = ExprSymbol.toPtr<int8_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stdout, "%lld\n", result);
        break;
      }
      case ValueType::Int16: {
        int16_t (*FP)() = ExprSymbol.toPtr<int16_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stdout, "%lld\n", result);
        break;
      }
      case ValueType::Int32: {
        int32_t (*FP)() = ExprSymbol.toPtr<int32_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stdout, "%lld\n", result);
        break;
      }
      case ValueType::Int64: {
        int64_t (*FP)() = ExprSymbol.toPtr<int64_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stdout, "%lld\n", result);
        break;
      }
      case ValueType::Bool: {
        bool (*FP)() = ExprSymbol.toPtr<bool (*)()>();
        bool result = FP();
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stdout, "%s\n", result ? "True" : "False");
        break;
      }
      default:
        break;
      }
    }
  } else {
    InGlobalInit = SavedInGlobalInit;
  }
}

/// HandleTopLevelStatementFileMode - Parse and queue a top-level statement.
///
/// In file mode, top-level statements are collected and emitted into a single
/// __pyxc.global_init function after the entire file is parsed.
static void HandleTopLevelStatementFileMode() {
  auto Stmt = ParseTopLevelStatement();
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (!Stmt || HasTrailing) {
    if (Stmt)
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)));
    DiscardRestOfLine();
    return;
  }

  FileTopLevelStatements.push_back(std::move(Stmt));
}

//===----------------------------------------===//
// Runtime library — callable via 'extern def'
//===----------------------------------------===//

// These functions are compiled into the pyxc binary itself and exported with
// C linkage so the JIT can resolve 'extern def putchard(x)' and
// 'extern def printd(x)' against them at runtime.
//
// DLLEXPORT is required on Windows where symbols are not exported by default.
// On macOS/Linux it is a no-op — all extern "C" symbols are visible.

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - Write a single ASCII character to stdout. The double argument
/// is truncated to char. Returns 0.0 so it can be used as an expression.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stdout);
  return 0;
}

/// printd - Print a double to stdout as "%f\n". Returns 0.0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stdout, "%f\n", X);
  return 0;
}

/// MainLoop - Dispatch loop for the REPL.
///
/// top-level-item
///   = function-definition | external | top-level-statement ;
///
/// Dispatches on the leading token of each top-level form:
///   tok_def    → HandleFunctionDefinition   (function-definition)
///   tok_extern → HandleExtern       (external)
///   tok_eol    → skip blank line
///   anything else → HandleTopLevelStatement (top-level-statement)
///
/// CurrentToken is primed before MainLoop() is called (see main()). After each
/// successful parse the handler prints a confirmation; after a failed parse
/// the handler calls DiscardRestOfLine() to discard all remaining
/// tokens on the current line. Either way we return here to look at the
/// next CurrentToken.
static void MainLoop() {
  while (CurrentToken != tok_eof) {
    switch (CurrentToken) {
    case tok_indent:
      LogErrorExpression("Unexpected indentation");
      DiscardRestOfLine();
      break;
    // Stray dedent at top level (can occur in REPL mode): skip it.
    case tok_dedent:
    case tok_block_end:
      getNextToken();
      break;
    case tok_error:
      DiscardRestOfLine();
      break;
    case tok_eol:
      // A bare newline: just print a fresh prompt and read the next token.
      PrintReplPrompt();
      getNextToken();
      break;
    case tok_def:
      HandleFunctionDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelStatement();
      break;
    }
  }
}

/// FileModeLoop - Parse a script file into top-level statements + definitions.
///
/// In file mode we do not execute top-level statements immediately. They are
/// collected into FileTopLevelStatements and later emitted into __pyxc.global_init.
static void FileModeLoop() {
  while (true) {
    if (CurrentToken == tok_eof)
      return;

    if (CurrentToken == tok_eol) {
      getNextToken();
      continue;
    }

    if (CurrentToken == tok_indent) {
      LogErrorExpression("Unexpected indentation");
      DiscardRestOfLine();
      continue;
    }

    if (CurrentToken == tok_dedent || CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }

    if (CurrentToken == tok_error) {
      DiscardRestOfLine();
      continue;
    }

    switch (CurrentToken) {
    case tok_def:
      HandleFunctionDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelStatementFileMode();
      break;
    }
  }
}

/// RunFileMode - Emit and execute __pyxc.global_init, then call main() if any.
static void RunFileMode() {
  if (!FileTopLevelStatements.empty()) {
    auto Block = make_unique<BlockStatementNode>(std::move(FileTopLevelStatements));
    auto Signature = make_unique<FunctionSignatureNode>(
        "__pyxc.global_init", vector<pair<string, ValueType>>(),
        SourceLocation{1, 1}, ValueType::None);
    auto FnAST = make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Block));

    bool SavedInGlobalInit = InGlobalInit;
    InGlobalInit = true;
    if (auto *FnIR = FnAST->codegen()) {
      InGlobalInit = SavedInGlobalInit;
      if (ShouldDumpIR())
        FnIR->print(errs());

      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(JIT->addModule(std::move(TSM)));
      InitializeModuleAndManagers();

      auto InitSymbol = ExitOnErr(JIT->lookup("__pyxc.global_init"));
      void (*InitFn)() = InitSymbol.toPtr<void (*)()>();
      InitFn();
    } else {
      InGlobalInit = SavedInGlobalInit;
      return;
    }
  }

  auto MainIt = FunctionSignatures.find("main");
  if (MainIt == FunctionSignatures.end())
    return;

  if (MainIt->second->getNumParameters() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    HadError = true;
    return;
  }
  if (!IsIntType(MainIt->second->getReturnType()) &&
      MainIt->second->getReturnType() != ValueType::None) {
    fprintf(stderr, "Error: main() must return int or None\n");
    HadError = true;
    return;
  }

  auto MainSymbol = ExitOnErr(JIT->lookup("main"));
  if (IsIntType(MainIt->second->getReturnType())) {
    int (*MainFn)() = MainSymbol.toPtr<int (*)()>();
    int ExitCode = MainFn();
    exit(ExitCode);
  } else {
    void (*MainFn)() = MainSymbol.toPtr<void (*)()>();
    MainFn();
  }
}

/// AddGlobalCtor - Register a function to run before main() via
/// llvm.global_ctors.
static void AddGlobalCtor(Function *Fn, int Priority = 65535) {
  auto *Int32Type = Type::getInt32Ty(*TheContext);
  auto *VoidPtrType = PointerType::get(*TheContext, 0);
  auto *StructType = StructType::get(Int32Type, Fn->getType(), VoidPtrType);

  Constant *CtorEntry = ConstantStruct::get(
      StructType, ConstantInt::get(Int32Type, Priority), Fn,
      ConstantPointerNull::get(cast<PointerType>(VoidPtrType)));

  GlobalVariable *GV = TheModule->getGlobalVariable("llvm.global_ctors");
  if (GV)
    return;

  ArrayType *AT = ArrayType::get(StructType, 1);
  auto *Init = ConstantArray::get(AT, {CtorEntry});
  new GlobalVariable(*TheModule, AT, false, GlobalValue::AppendingLinkage, Init,
                     "llvm.global_ctors");
}

/// CreateTargetMachine - Build a TargetMachine for the host triple.
static std::unique_ptr<TargetMachine> CreateTargetMachine() {
  string TargetTriple = sys::getDefaultTargetTriple();
  Triple TT(TargetTriple);

  string Error;
  const Target *Target = TargetRegistry::lookupTarget(TT, Error);
  if (!Target) {
    fprintf(stderr, "Error: %s\n", Error.c_str());
    return nullptr;
  }

  TargetOptions Options;
  auto RM = std::optional<Reloc::Model>();
  return std::unique_ptr<TargetMachine>(
      Target->createTargetMachine(TT, "generic", "", Options, RM));
}

/// EmitModuleToFile - Write the module to the requested path in the given
/// format.
static bool EmitModuleToFile(Module *M, EmitKind Kind,
                             const string &OutputPath) {
  std::error_code EC;
  raw_fd_ostream Dest(OutputPath, EC, sys::fs::OF_None);
  if (EC) {
    fprintf(stderr, "Error: could not open output file '%s'\n",
            OutputPath.c_str());
    return false;
  }

  if (Kind == EmitKind::LLVMIR) {
    M->print(Dest, nullptr);
    return true;
  }

  auto TM = CreateTargetMachine();
  if (!TM)
    return false;

  M->setTargetTriple(TM->getTargetTriple());
  M->setDataLayout(TM->createDataLayout());

  legacy::PassManager PM;
  CodeGenFileType FileType = (Kind == EmitKind::Assembly)
                                 ? CodeGenFileType::AssemblyFile
                                 : CodeGenFileType::ObjectFile;

  if (TM->addPassesToEmitFile(PM, Dest, nullptr, FileType)) {
    fprintf(stderr, "Error: target does not support file emission\n");
    return false;
  }

  PM.run(*M);
  return true;
}

static bool PrepareFileModeModule();

static bool OpenInputFile(const string &Path) {
  Input = fopen(Path.c_str(), "r");
  if (!Input) {
    perror(Path.c_str());
    return false;
  }
  return true;
}

static void CloseInputFile() {
  if (Input && Input != stdin) {
    fclose(Input);
    Input = stdin;
  }
}

static bool EndsWithInsensitive(StringRef Path, StringRef Suffix) {
  if (Path.size() < Suffix.size())
    return false;
  return Path.take_back(Suffix.size()).equals_insensitive(Suffix);
}

static bool IsPyxcInput(StringRef Path) {
  return EndsWithInsensitive(Path, ".pyxc");
}

static bool IsObjectInput(StringRef Path) {
  return EndsWithInsensitive(Path, ".o") || EndsWithInsensitive(Path, ".obj");
}

static string DefaultExeOutputPath(StringRef InputPath) {
  SmallString<256> Out(InputPath);
  sys::path::replace_extension(Out, "");
  string OutStr = Out.str().str();
#ifdef _WIN32
  OutStr += ".exe";
#endif
  return OutStr;
}

static bool EmitRuntimeObject(const string &ObjPath) {
  LLVMContext Ctx;
  auto M = std::make_unique<Module>("pyxc.runtime", Ctx);

  auto *DoubleType = Type::getDoubleTy(Ctx);
  auto *Int32Type = Type::getInt32Ty(Ctx);
  auto *CharPtrType = PointerType::get(Ctx, 0);

  FunctionType *PrintfType = FunctionType::get(Int32Type, {CharPtrType}, true);
  Function *Printf = Function::Create(PrintfType, Function::ExternalLinkage,
                                      "printf", M.get());

  FunctionType *PutcharType = FunctionType::get(Int32Type, {Int32Type}, false);
  Function *Putchar = Function::Create(PutcharType, Function::ExternalLinkage,
                                       "putchar", M.get());

  FunctionType *PrintdType = FunctionType::get(DoubleType, {DoubleType}, false);
  Function *Printd = Function::Create(PrintdType, Function::ExternalLinkage,
                                      "printd", M.get());
  {
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Printd);
    IRBuilder<> B(BB);
    auto *FmtGV = B.CreateGlobalString("%f\n", "fmt");
    Value *Zero = ConstantInt::get(Int32Type, 0);
    Value *Fmt = B.CreateInBoundsGEP(FmtGV->getValueType(), FmtGV, {Zero, Zero},
                                     "fmt_ptr");
    Value *Arg = Printd->getArg(0);
    B.CreateCall(Printf, {Fmt, Arg});
    B.CreateRet(ConstantFP::get(Ctx, APFloat(0.0)));
  }

  FunctionType *PutchardType =
      FunctionType::get(DoubleType, {DoubleType}, false);
  Function *Putchard = Function::Create(PutchardType, Function::ExternalLinkage,
                                        "putchard", M.get());
  {
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Putchard);
    IRBuilder<> B(BB);
    Value *Arg = Putchard->getArg(0);
    Value *Ch = B.CreateFPToUI(Arg, Int32Type, "ch");
    B.CreateCall(Putchar, {Ch});
    B.CreateRet(ConstantFP::get(Ctx, APFloat(0.0)));
  }

  return EmitModuleToFile(M.get(), EmitKind::Object, ObjPath);
}

static bool CompileFileToObject(const string &Path, const string &ObjPath,
                                bool *HasMain) {
  if (!OpenInputFile(Path))
    return false;

  ResetLexerState();
  ResetParserStateForFile();
  InitializeModuleAndManagers(false);

  IsRepl = false;
  PrintReplPrompt();
  getNextToken();

  FileModeLoop();
  CloseInputFile();
  if (HadError)
    return false;

  if (HasMain)
    *HasMain = FunctionSignatures.find("main") != FunctionSignatures.end();

  if (!PrepareFileModeModule())
    return false;

  return EmitModuleToFile(TheModule.get(), EmitKind::Object, ObjPath);
}

/// RunXcrun - Shell out to xcrun and return trimmed stdout, or "" on failure.
static string RunXcrun(const char *Arguments) {
  string Cmd = string("xcrun ") + Arguments + " 2>/dev/null";
  FILE *Pipe = popen(Cmd.c_str(), "r");
  if (!Pipe)
    return "";
  char Buf[512];
  string Result;
  while (fgets(Buf, sizeof(Buf), Pipe))
    Result += Buf;
  pclose(Pipe);
  while (!Result.empty() && (Result.back() == '\n' || Result.back() == '\r' ||
                             Result.back() == ' '))
    Result.pop_back();
  return Result;
}

static string FindMacOSSDKRoot() {
  if (const char *EnvSDK = getenv("SDKROOT"))
    return string(EnvSDK);

  // Ask xcrun — it resolves the active SDK for the current Xcode/CLT selection
  // and returns the right path regardless of where Xcode is installed.
  string XcrunPath = RunXcrun("--sdk macosx --show-sdk-path");
  if (!XcrunPath.empty() && sys::fs::exists(XcrunPath))
    return XcrunPath;

  // Fallback: probe well-known paths.
  const char *XcodeSDK = "/Applications/Xcode.app/Contents/Developer/Platforms/"
                         "MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  if (sys::fs::exists(XcodeSDK))
    return string(XcodeSDK);

  const char *CLTSDK = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
  if (sys::fs::exists(CLTSDK))
    return string(CLTSDK);

  return "";
}

/// FindMacOSSDKVersion - Return the macOS SDK version string (e.g. "26.0").
/// This matches the version LLVM encodes into object files at compile time,
/// avoiding a version mismatch warning from ld64.lld.
static string FindMacOSSDKVersion() {
  string Ver = RunXcrun("--sdk macosx --show-sdk-version");
  if (!Ver.empty())
    return Ver;
  // Fallback: extract from the triple (may be Darwin kernel version on older
  // LLVM builds, so prefer xcrun when available).
  Triple TT(sys::getDefaultTargetTriple());
  VersionTuple V = TT.getOSVersion();
  if (V.getMajor()) {
    std::ostringstream OS;
    OS << V.getMajor() << "." << V.getMinor().value_or(0);
    return OS.str();
  }
  return "11.0";
}

static bool LinkExecutable(const vector<string> &Inputs,
                           const string &OutputPath) {
  Triple TT(sys::getDefaultTargetTriple());
  vector<string> ArgStorage;
  auto PushArg = [&](const string &Arg) { ArgStorage.push_back(Arg); };

  if (TT.isOSDarwin()) {
    PushArg("ld64.lld");
    PushArg("-arch");
    PushArg(TT.getArchName().str());
    PushArg("-o");
    PushArg(OutputPath);

    string SDKRoot = FindMacOSSDKRoot();
    if (!SDKRoot.empty()) {
      PushArg("-syslibroot");
      PushArg(SDKRoot);
      PushArg("-L" + SDKRoot + "/usr/lib");
      PushArg("-L" + SDKRoot + "/usr/lib/system");
      string OSVer = FindMacOSSDKVersion();
      PushArg("-platform_version");
      PushArg("macos");
      PushArg(OSVer);
      PushArg(OSVer);
    }
    // macOS startup is handled by dyld + libSystem; crt1/crti/crtn are
    // GNU ELF files that do not belong in a MachO link and cause warnings
    // on arm64 (the SDK copy is x86_64-only legacy).
    for (const auto &Input : Inputs)
      PushArg(Input);
    PushArg("-lSystem");

    vector<const char *> Arguments;
    Arguments.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Arguments.push_back(Arg.c_str());
    return lld::macho::link(Arguments, llvm::outs(), llvm::errs(), false, false);
  }

  if (TT.isOSLinux()) {
    PushArg("ld.lld");
    PushArg("-o");
    PushArg(OutputPath);
    for (const auto &Input : Inputs)
      PushArg(Input);
    PushArg("-lc");
    PushArg("-lm");
    vector<const char *> Arguments;
    Arguments.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Arguments.push_back(Arg.c_str());
    return lld::elf::link(Arguments, llvm::outs(), llvm::errs(), false, false);
  }

  if (TT.isOSWindows()) {
    PushArg("lld-link");
    PushArg("/OUT:" + OutputPath);
    for (const auto &Input : Inputs)
      PushArg(Input);
    vector<const char *> Arguments;
    Arguments.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Arguments.push_back(Arg.c_str());
    return lld::coff::link(Arguments, llvm::outs(), llvm::errs(), false, false);
  }

  fprintf(stderr, "Error: unsupported target for --emit exe\n");
  return false;
}

/// PrepareFileModeModule - Build __pyxc.global_init and main wrapper.
///
/// Returns false on error (e.g., invalid main signature).
static bool PrepareFileModeModule() {
  if (!FileTopLevelStatements.empty()) {
    auto Block = make_unique<BlockStatementNode>(std::move(FileTopLevelStatements));
    auto Signature = make_unique<FunctionSignatureNode>(
        "__pyxc.global_init", vector<pair<string, ValueType>>(),
        SourceLocation{1, 1}, ValueType::None);
    auto FnAST = make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Block));

    bool SavedInGlobalInit = InGlobalInit;
    InGlobalInit = true;
    if (auto *FnIR = FnAST->codegen()) {
      InGlobalInit = SavedInGlobalInit;
      if (ShouldDumpIR())
        FnIR->print(errs());
      AddGlobalCtor(FnIR);
    } else {
      InGlobalInit = SavedInGlobalInit;
      return false;
    }
  }

  auto MainIt = FunctionSignatures.find("main");
  if (MainIt != FunctionSignatures.end() && MainIt->second->getNumParameters() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    HadError = true;
    return false;
  }

  if (MainIt != FunctionSignatures.end()) {
    ValueType MainRet = MainIt->second->getReturnType();
    if (MainRet != ValueType::Int && MainRet != ValueType::None) {
      fprintf(stderr, "Error: main() must return int or None\n");
      HadError = true;
      return false;
    }
  }

  if (auto *UserMain = TheModule->getFunction("main")) {
    // Always wrap user's main() in an int32 main() so the OS entry point has
    // the correct C ABI. The user-defined main() must return int or None.
    UserMain->setName("__pyxc.user_main");
    FunctionType *FT = FunctionType::get(Type::getInt32Ty(*TheContext), false);
    Function *Wrapper = Function::Create(FT, Function::ExternalLinkage, "main",
                                         TheModule.get());
    BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", Wrapper);
    IRBuilder<> TmpB(BB);
    if (UserMain->getReturnType()->isIntegerTy()) {
      Value *Ret = TmpB.CreateCall(UserMain);
      if (!UserMain->getReturnType()->isIntegerTy(32))
        Ret = TmpB.CreateTrunc(Ret, Type::getInt32Ty(*TheContext));
      TmpB.CreateRet(Ret);
    } else {
      TmpB.CreateCall(UserMain);
      TmpB.CreateRet(ConstantInt::get(Type::getInt32Ty(*TheContext), 0));
    }
  }

  return true;
}

/// EmitFileMode - Build __pyxc.global_init and emit the requested output file.
static void EmitFileMode() {
  if (HadError)
    return;
  if (!PrepareFileModeModule())
    return;
  EmitModuleToFile(TheModule.get(), EmitMode, EmitOutputPath);
}

/// EmitExecutable - Compile inputs to objects and link them into an executable.
static bool EmitExecutable() {
  vector<string> ObjectFiles;
  vector<string> TempFiles;
  bool SawMain = false;
  bool SawObjectInput = false;

  auto CleanupTemps = [&]() {
    for (const auto &Path : TempFiles)
      sys::fs::remove(Path);
  };

  for (const auto &InputPath : InputFiles) {
    if (IsPyxcInput(InputPath)) {
      int FD = -1;
      SmallString<128> TmpPath;
      if (auto EC = sys::fs::createTemporaryFile("pyxc", "o", FD, TmpPath)) {
        fprintf(stderr, "Error: could not create temporary file: %s\n",
                EC.message().c_str());
        CleanupTemps();
        return false;
      }
      if (FD != -1)
        close(FD);

      string ObjPath = TmpPath.str().str();
      TempFiles.push_back(ObjPath);

      bool FileHasMain = false;
      if (!CompileFileToObject(InputPath, ObjPath, &FileHasMain)) {
        CleanupTemps();
        return false;
      }
      SawMain = SawMain || FileHasMain;
      ObjectFiles.push_back(ObjPath);
      continue;
    }

    if (IsObjectInput(InputPath)) {
      ObjectFiles.push_back(InputPath);
      SawObjectInput = true;
      continue;
    }

    fprintf(stderr, "Error: unsupported input '%s'\n", InputPath.c_str());
    CleanupTemps();
    return false;
  }

  if (!SawMain && !SawObjectInput) {
    fprintf(stderr, "Error: main() not found\n");
    CleanupTemps();
    return false;
  }

  int RuntimeFD = -1;
  SmallString<128> RuntimeObj;
  if (auto EC = sys::fs::createTemporaryFile("pyxc_runtime", "o", RuntimeFD,
                                             RuntimeObj)) {
    fprintf(stderr, "Error: could not create runtime object: %s\n",
            EC.message().c_str());
    CleanupTemps();
    return false;
  }
  if (RuntimeFD != -1)
    close(RuntimeFD);

  string RuntimePath = RuntimeObj.str().str();
  TempFiles.push_back(RuntimePath);
  if (!EmitRuntimeObject(RuntimePath)) {
    CleanupTemps();
    return false;
  }
  ObjectFiles.push_back(RuntimePath);

  if (EmitOutputPath.empty()) {
    if (InputFiles.empty()) {
      fprintf(stderr, "Error: --emit exe requires a file input\n");
      CleanupTemps();
      return false;
    }
    EmitOutputPath = DefaultExeOutputPath(InputFiles.front());
  }

  if (!LinkExecutable(ObjectFiles, EmitOutputPath)) {
    CleanupTemps();
    return false;
  }
  CleanupTemps();
  return true;
}

/// ProcessCommandLine - Parse argv and configure the global Input/IsRepl state.
///
/// Returns 0 on success, -1 on error (e.g. the file could not be opened). When
/// no file is given, Input stays as stdin and IsRepl is set to true.
int ProcessCommandLine(int argc, const char **argv) {
  cl::HideUnrelatedOptions(PyxcCategory);
  cl::ParseCommandLineOptions(argc, argv, "pyxc\n");

  if (OptLevel > 3) {
    fprintf(stderr, "Error: -O level must be 0, 1, 2, or 3\n");
    return -1;
  }

  IsRepl = InputFiles.empty();

  if (!EmitKindOpt.empty()) {
    if (IsRepl) {
      fprintf(stderr, "Error: --emit requires a file input\n");
      return -1;
    }

    if (EmitKindOpt == "llvm-ir") {
      EmitMode = EmitKind::LLVMIR;
      if (InputFiles.size() != 1) {
        fprintf(stderr, "Error: --emit requires a single input file\n");
        return -1;
      }
      EmitOutputPath = OutputFile.empty() ? "out.ll" : OutputFile.getValue();
    } else if (EmitKindOpt == "asm") {
      EmitMode = EmitKind::Assembly;
      if (InputFiles.size() != 1) {
        fprintf(stderr, "Error: --emit requires a single input file\n");
        return -1;
      }
      EmitOutputPath = OutputFile.empty() ? "out.s" : OutputFile.getValue();
    } else if (EmitKindOpt == "obj") {
      EmitMode = EmitKind::Object;
      if (InputFiles.size() != 1) {
        fprintf(stderr, "Error: --emit requires a single input file\n");
        return -1;
      }
      EmitOutputPath = OutputFile.empty() ? "out.o" : OutputFile.getValue();
    } else if (EmitKindOpt == "exe") {
      EmitMode = EmitKind::Executable;
      if (OutputFile.empty() && InputFiles.size() > 1) {
        fprintf(stderr, "Error: multiple inputs require -o\n");
        return -1;
      }
      if (!OutputFile.empty())
        EmitOutputPath = OutputFile.getValue();
    } else {
      fprintf(stderr, "Error: invalid --emit value '%s'\n",
              EmitKindOpt.c_str());
      return -1;
    }
  } else if (!OutputFile.empty()) {
    fprintf(stderr, "Error: -o requires --emit\n");
    return -1;
  } else if (!IsRepl && InputFiles.size() > 1) {
    fprintf(stderr, "Error: multiple inputs require --emit exe\n");
    return -1;
  }

  return 0;
}

//===----------------------------------------===//
// Main driver code.
//===----------------------------------------===//

/// main - Entry point for the Pyxc compiler/REPL.
///
/// Initialises the LLVM native backend, creates the ORC JIT and an initial
/// module, then hands control to MainLoop(). On exit, any open script file is
/// closed.
int main(int argc, const char **argv) {

  int commandLineResult = ProcessCommandLine(argc, argv);
  if (commandLineResult != 0) {
    return commandLineResult;
  }

  // Initialise LLVM's backend for the host machine. These three calls
  // register the native target's instruction set, assembler, and disassembler
  // so both the JIT and the file-emission paths can generate code.
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Create the JIT first — InitializeModuleAndManagers() needs JIT in
  // order to set the data layout on the new module.
  JIT = ExitOnErr(PyxcJIT::Create());
  InitializeModuleAndManagers();

  if (IsRepl) {
    PrintReplPrompt();
    getNextToken();
    MainLoop();
  } else {
    if (EmitMode == EmitKind::Executable) {
      if (!EmitExecutable())
        return 1;
    } else {
      if (InputFiles.empty())
        return 1;
      if (!OpenInputFile(InputFiles.front()))
        return 1;
      ResetLexerState();
      ResetParserStateForFile();
      PrintReplPrompt();
      getNextToken();

      FileModeLoop();
      if (HadError) {
        CloseInputFile();
        return 1;
      }
      if (IsEmitMode())
        EmitFileMode();
      else
        RunFileMode();

      CloseInputFile();
    }
  }

  if (IsRepl)
    return 0;
  return HadError ? 1 : 0;
}

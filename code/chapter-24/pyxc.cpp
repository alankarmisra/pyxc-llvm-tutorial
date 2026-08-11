#include "../include/PyxcJIT.h"
#include "lld/Common/Driver.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
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
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <map>
#include <memory>
#include <limits>
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

// Emit DWARF debug information in native output.
static cl::opt<bool> DebugInfo("g", cl::desc("Emit DWARF debug info"),
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
                                  cl::init(0), cl::cat(PyxcCategory));

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
  tok_uint8 = -39,
  tok_uint16 = -40,
  tok_uint32 = -41,
  tok_uint64 = -42,
  tok_and = -43, // &&
  tok_or = -44,  // ||
  tok_shift_left = -45,  // <<
  tok_shift_right = -46, // >>
  tok_switch = -47,
  tok_case = -48,
  tok_default = -49,
  tok_struct = -50,

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
  tok_exclamation = '!',
  tok_ampersand = '&',
  tok_pipe = '|',
  tok_caret = '^',
  tok_tilde = '~',
  tok_dot = '.',
};

enum class ValueType {
  None,
  Int, /* depends on system default for int */
  Int8,
  Int16,
  Int32,
  Int64,
  UInt8,
  UInt16,
  UInt32,
  UInt64,
  Float,
  Float32,
  Float64,
  Bool,
  Struct,
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
    {"int32", tok_int32},     {"int64", tok_int64},
    {"uint8", tok_uint8},     {"uint16", tok_uint16},
    {"uint32", tok_uint32},   {"uint64", tok_uint64},
    {"switch", tok_switch},   {"case", tok_case},
    {"default", tok_default}, {"struct", tok_struct},
    {"float", tok_float},
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
      {tok_uint8, "'uint8'"},     {tok_uint16, "'uint16'"},
      {tok_uint32, "'uint32'"},   {tok_uint64, "'uint64'"},
      {tok_and, "'&&'"},          {tok_or, "'||'"},
      {tok_shift_left, "'<<'"},   {tok_shift_right, "'>>'"},
      {tok_switch, "'switch'"},   {tok_case, "'case'"},
      {tok_default, "'default'"},
      {tok_struct, "'struct'"},
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

/// SourceLocation - A {Line, Col} pair. Line and Col are 1-based.
///
/// Two globals track position as characters are consumed:
///   LexLoc  - where the character-read head (advance()) currently is.
///             Updated on every advance() call. After a '\n', Line increments
///             and Col resets to 0 so the next character will be Col 1.
///   CurLoc  - snapshotted at the start of each token in getToken(), before
///             consuming any of the token's characters. This is the position
///             the parser and diagnostics see.
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};
static void LogErrorAtLoc(const char *Str, SourceLocation Loc);
static void LogInvalidNumberLiteralAtLoc(const string &Literal, SourceLocation Loc);

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

static SourceManager PyxcSourceMgr;
static void PrintErrorSourceContext(SourceLocation Loc);

/// advance - Read one character from Input, update LexLoc and SourceManager.
///
/// This is the single point through which all character consumption flows.
/// Every token branch in getToken() calls advance() rather than fgetc()
/// directly, so LexLoc and the source buffer are always in sync.
///
/// Windows line endings (\r\n) are coalesced to a single \n
/// as are bare (old) Mac \r's (without a trailing \n)
/// so the rest of the lexer never needs to handle \r.
static int advance() {
  int LastChar = fgetc(Input);
  if (LastChar == '\r') {
    int NextChar = fgetc(Input);
    if (NextChar != '\n' && NextChar != EOF)
      ungetc(NextChar, Input);
    PyxcSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
    return '\n';
  }

  if (LastChar == '\n') {
    PyxcSourceMgr.onChar('\n');
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else {
    PyxcSourceMgr.onChar(LastChar);
    LexLoc.Col++;
  }

  return LastChar;
}

/// peek - Return the next character from the input stream without consuming it.
///
/// Used by the two-character operator branches in getToken() to decide whether
/// '=' should become '==' (tok_eq), '!' should become '!=' (tok_neq), etc.,
/// without advancing LexLoc or notifying SourceManager.
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
/// CurLoc is snapshotted from LexLoc after the whitespace-skip loop and
/// before any token branch. For most tokens this points at the first
/// character of the token. For tok_eol the '\n' was already consumed by
/// advance() on a previous call, so LexLoc is already on the next line;
/// GetDiagnosticAnchorLoc compensates by subtracting one when building error
/// locations for tok_eol.
///
/// The comment path ('#' branch) re-snapshots CurLoc just before returning
/// tok_eol because it consumes many characters (the whole comment) after the
/// initial snapshot, leaving LexLoc well past the '#' position.
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
      CurLoc = LexLoc;
      LexerLastChar = ' '; // AtLineStart is still true so the lexer reads past
                           // the sentinel in the next call
      return tok_eol;
    }

    // Comment-only line: consume and return a newline.
    if (LexerLastChar == '#') {
      do
        LexerLastChar = advance();
      while (LexerLastChar != EOF && LexerLastChar != '\n');
      if (LexerLastChar != EOF) {
        CurLoc = LexLoc;
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
    CurLoc = LexLoc;
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
        LogErrorAtLoc("inconsistent indentation", CurLoc);
        PrintErrorSourceContext(CurLoc);
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
  CurLoc = LexLoc;

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
    Name = LexerLastChar;
    while (isalnum((LexerLastChar = advance())) || LexerLastChar == '_')
      Name += LexerLastChar;

    auto It = Keywords.find(Name);
    return (It == Keywords.end()) ? tok_name : It->second;
  }

  if (isdigit(LexerLastChar) ||
      (LexerLastChar == '.' && isdigit(peek()))) {
    string NumStr;
    bool SawDot = false;
    bool SawExp = false;

    auto ConsumeDigits = [&]() {
      while (isdigit(LexerLastChar)) {
        NumStr += LexerLastChar;
        LexerLastChar = advance();
      }
    };

    if (LexerLastChar == '.') {
      SawDot = true;
      NumStr += LexerLastChar;
      LexerLastChar = advance();
      ConsumeDigits();
    } else {
      ConsumeDigits();
      if (LexerLastChar == '.') {
        SawDot = true;
        NumStr += LexerLastChar;
        LexerLastChar = advance();
        ConsumeDigits();
      }
    }

    if (LexerLastChar == 'e' || LexerLastChar == 'E') {
      SawExp = true;
      NumStr += LexerLastChar;
      LexerLastChar = advance();
      if (LexerLastChar == '+' || LexerLastChar == '-') {
        NumStr += LexerLastChar;
        LexerLastChar = advance();
      }
      if (!isdigit(LexerLastChar)) {
      LogInvalidNumberLiteralAtLoc(NumStr, CurLoc);
      return tok_error;
      }
      ConsumeDigits();
    }

    if (NumStr == ".") {
      LogInvalidNumberLiteralAtLoc(NumStr, CurLoc);
      return tok_error;
    }

    NumberLiteral = NumStr;
    NumberIsFloat = SawDot || SawExp;
    return tok_number;
  }

  if (LexerLastChar == '#') {
    // Consume the rest of the line (comment). Stop at '\n' or EOF.
    do
      LexerLastChar = advance();
    while (LexerLastChar != EOF && LexerLastChar != '\n');

    if (LexerLastChar != EOF) {
      // Re-snapshot CurLoc now that the '\n' has been consumed and LexLoc
      // has advanced to the next line. Without this, CurLoc would point at
      // the '#' column, and GetDiagnosticAnchorLoc would look up the wrong
      // line (because it subtracts 1) when the next token triggers an error.
      CurLoc = LexLoc;
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

  if (LexerLastChar == '&') {
    int Tok = (peek() == '&') ? (advance(), tok_and) : tok_ampersand;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '|') {
    int Tok = (peek() == '|') ? (advance(), tok_or) : tok_pipe;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '=') {
    int Tok = (peek() == '=') ? (advance(), tok_eq) : tok_equal;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '!') {
    int Tok = (peek() == '=') ? (advance(), tok_neq) : tok_exclamation;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '<') {
    int Next = peek();
    int Tok = tok_less;
    if (Next == '=')
      Tok = (advance(), tok_leq);
    else if (Next == '<')
      Tok = (advance(), tok_shift_left);
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '>') {
    int Next = peek();
    int Tok = tok_greater;
    if (Next == '=')
      Tok = (advance(), tok_geq);
    else if (Next == '>')
      Tok = (advance(), tok_shift_right);
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
  case '&':
    return tok_ampersand;
  case '|':
    return tok_pipe;
  case '^':
    return tok_caret;
  case '~':
    return tok_tilde;
  case '.':
    return tok_dot;
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
  LexLoc = {1, 0};
  CurLoc = {1, 0};
  LexerLastChar = ' ';
  PyxcSourceMgr.reset();
}

//===----------------------------------------===//
// Diagnostics helpers
//===----------------------------------------===//

/// GetDiagnosticAnchorLoc - Resolve the source location to attach to an error.
///
/// For most tokens, CurLoc already points at the right place and is returned
/// unchanged. The special case is tok_eol: CurLoc for a newline token is
/// snapshotted after advance() has consumed the '\n' and incremented
/// LexLoc.Line, so CurLoc.Line is already the *next* line. Subtracting one
/// gives the line that just ended, and we report a column one past its last
/// character — pointing just after the final token on the line, which is
/// where the missing token (e.g. ':') should have appeared.
static SourceLocation GetDiagnosticAnchorLoc(SourceLocation Loc, int Tok) {
  if (Tok != tok_eol || Loc.Line <= 1)
    return Loc;

  // Tok == tok_eol && Loc.Line > 1
  int PrevLine = Loc.Line - 1;
  const string *PrevLineText = PyxcSourceMgr.getLine(PrevLine);

  // guard
  if (!PrevLineText)
    return Loc;

  // return a pointer just past the end of the previous line.
  return {PrevLine, static_cast<int>(PrevLineText->size()) + 1};
}

/// FormatTokenForMessage - Return a human-readable description of Tok for use
/// in error messages. Name and number tokens include their actual text
/// (e.g. "name 'foo'", "number '3.14'") since the name alone is not
/// enough to diagnose the problem. Everything else uses the static TokenNames
/// entry.
static string FormatTokenForMessage(int Tok) {
  if (Tok == tok_name)
    return "name '" + Name + "'";
  if (Tok == tok_number)
    return "number '" + NumberLiteral + "'";

  auto It = TokenNames.find(Tok);
  if (It != TokenNames.end())
    return It->second;
  return "unknown token";
}

/// PrintErrorSourceContext - Reprint the source line at Loc and place a
/// '^~~~' caret under column Loc.Col. Col is 1-based, so we print Col-1
/// spaces before the caret.
static void PrintErrorSourceContext(SourceLocation Loc) {
  const string *LineText = PyxcSourceMgr.getLine(Loc.Line);
  if (!LineText)
    return;

  fprintf(stderr, "%s\n", LineText->c_str());
  int spaces = max(0, Loc.Col - 1);
  fprintf(stderr, "%*s", spaces, " ");
  fprintf(stderr, "^~~~\n");
}


static void LogErrorAtLoc(const char *Str, SourceLocation Loc) {
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Loc.Line, Loc.Col, Str);
  PrintErrorSourceContext(Loc);
}

static void LogInvalidNumberLiteralAtLoc(const string &Literal, SourceLocation Loc) {
  LogErrorAtLoc(("invalid number literal '" + Literal + "'").c_str(), Loc);
}

//===----------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------===//
namespace {

/// ExpressionNode - Base class for all expression nodes.
class ExpressionNode {
  ValueType Type = ValueType::Error;
  string StructName;

public:
  virtual ~ExpressionNode() = default;
  ValueType getType() const { return Type; }
  const string &getStructName() const { return StructName; }
  // getLValueName - If this node is a plain assignable variable, return its
  // name; otherwise return nullptr.
  virtual const string *getLValueName() const { return nullptr; }
  virtual const vector<string> *getLValueFieldPath() const { return nullptr; }
  // isReturnExpr - True iff this node is a return statement.
  virtual bool isReturnExpr() const { return false; }
  // shouldPrintValue - Whether the REPL should print the value of this node
  // when it appears as a top-level form.
  virtual bool shouldPrintValue() const { return true; }
  virtual Value *codegen() = 0;

protected:
  void setType(ValueType NewType, const string &NewStructName = "") {
    Type = NewType;
    StructName = NewStructName;
  }
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
  NameExpressionNode(const string &Name, ValueType Type,
                     const string &StructName = "")
      : Name(Name) {
    setType(Type, StructName);
  }
  // convenience function
  const string &getName() const { return Name; }
  const string *getLValueName() const override { return &Name; }
  Value *codegen() override;
};

class FieldExpressionNode : public ExpressionNode {
  string BaseName;
  vector<string> FieldPath;

public:
  FieldExpressionNode(string BaseName, vector<string> FieldPath, ValueType Type,
                      const string &StructName = "")
      : BaseName(std::move(BaseName)), FieldPath(std::move(FieldPath)) {
    setType(Type, StructName);
  }
  const string *getLValueName() const override { return &BaseName; }
  const vector<string> &getFieldPath() const { return FieldPath; }
  const vector<string> *getLValueFieldPath() const override {
    return &FieldPath;
  }
  Value *codegen() override;
};

class FieldAssignmentStatementNode : public ExpressionNode {
  unique_ptr<FieldExpressionNode> Left;
  unique_ptr<ExpressionNode> Right;

public:
  FieldAssignmentStatementNode(unique_ptr<FieldExpressionNode> Left,
                               unique_ptr<ExpressionNode> Right,
                               ValueType Type,
                               const string &StructName = "")
      : Left(std::move(Left)), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
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
              ValueType Type, const string &StructName = "")
      : Callee(Callee), Arguments(std::move(Arguments)) {
    setType(Type, StructName);
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

class SwitchStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Condition;
  vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases;
  unique_ptr<ExpressionNode> DefaultCase;

public:
  SwitchStatementNode(
      unique_ptr<ExpressionNode> Condition,
      vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases,
      unique_ptr<ExpressionNode> DefaultCase)
      : Condition(std::move(Condition)), Cases(std::move(Cases)),
        DefaultCase(std::move(DefaultCase)) {
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
  string StructName;
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
  vector<string> ParameterStructNames;
  ValueType ReturnType;
  string ReturnStructName;
  SourceLocation Loc;

public:
  FunctionSignatureNode(const string &Name,
                        vector<pair<string, ValueType>> Parameters,
                        SourceLocation Loc,
                        ValueType ReturnType = ValueType::Float64,
                        vector<string> ParameterStructNames = {},
                        string ReturnStructName = "")
      : Name(Name), Parameters(std::move(Parameters)), ReturnType(ReturnType),
        ReturnStructName(std::move(ReturnStructName)), Loc(Loc) {
    this->ParameterStructNames = std::move(ParameterStructNames);
    this->ParameterStructNames.resize(this->Parameters.size());
  }

  const string &getName() const { return Name; }
  const vector<pair<string, ValueType>> &getParameters() const { return Parameters; }
  const vector<string> &getParameterStructNames() const {
    return ParameterStructNames;
  }
  size_t getNumParameters() const { return Parameters.size(); }
  SourceLocation getLocation() const { return Loc; }
  ValueType getReturnType() const { return ReturnType; }
  const string &getReturnStructName() const { return ReturnStructName; }
  void setReturnType(ValueType Type) { ReturnType = Type; }
  void setReturnStructName(const string &Name) { ReturnStructName = Name; }

  ValueType getParameterType(size_t Index) const {
    if (Index >= Parameters.size())
      return ValueType::Error;
    return Parameters[Index].second;
  }
  const string &getParameterStructName(size_t Index) const {
    static const string Empty;
    return Index < ParameterStructNames.size() ? ParameterStructNames[Index]
                                                : Empty;
  }


  std::unique_ptr<FunctionSignatureNode> clone() const {
    return std::make_unique<FunctionSignatureNode>(
        Name, Parameters, Loc, ReturnType, ParameterStructNames,
        ReturnStructName);
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

struct StructFieldInfo {
  string Name;
  ValueType Type;
  string StructName;
};

struct StructTypeInfo {
  vector<StructFieldInfo> Fields;
  map<string, size_t> FieldIndices;
};

static map<string, StructTypeInfo> StructTypes;

// Parse-time variable tracking for assignments and types.
// Scopes are stacked: function scope plus nested block scopes.
// for-loop variables are scoped to the loop body only.
static vector<std::map<string, ValueType>> VarScopes;
static vector<std::map<string, string>> VarStructScopes;
// Global variables declared at top level (persist across modules).
static std::map<string, ValueType> GlobalVarTypes;
static std::map<string, string> GlobalVarStructNames;
// Track which globals were declared in this translation unit (for redeclare
// checks).
static std::set<string> GlobalVarDecls;
// True while parsing a top-level statement (var binds globals, not locals).
static bool ParsingTopLevel = false;
static int ParseLoopDepth = 0;
static int ParseSwitchDepth = 0;
// Set when we hit a parse/codegen error; used to abort further processing.
static bool HadError = false;
// Current function's declared return type during parsing/codegen.
static ValueType CurrentFunctionReturnType = ValueType::None;
static string CurrentFunctionReturnStructName;

struct TopLevelParseGuard {
  TopLevelParseGuard() { ParsingTopLevel = true; }
  ~TopLevelParseGuard() { ParsingTopLevel = false; }
};

static void BeginFunctionScope(
    const vector<pair<string, ValueType>> &Parameters,
    const vector<string> &ParameterStructNames) {
  VarScopes.clear();
  VarStructScopes.clear();
  VarScopes.emplace_back();
  VarStructScopes.emplace_back();
  for (size_t Index = 0; Index < Parameters.size(); ++Index) {
    const auto &Parameter = Parameters[Index];
    VarScopes.front()[Parameter.first] = Parameter.second;
    if (Index < ParameterStructNames.size() &&
        !ParameterStructNames[Index].empty())
      VarStructScopes.front()[Parameter.first] = ParameterStructNames[Index];
  }
}

static void EndFunctionScope() {
  VarScopes.clear();
  VarStructScopes.clear();
}

static void DeclareVar(const string &Name, ValueType Type,
                       const string &StructName = "") {
  // Only declare into an active local scope; at top level VarScopes is empty.
  if (VarScopes.empty())
    return;
  VarScopes.back()[Name] = Type;
  if (!StructName.empty())
    VarStructScopes.back()[Name] = StructName;
}

static void BeginBlockScope() {
  VarScopes.emplace_back();
  VarStructScopes.emplace_back();
}

// Pop a block scope if one is active.
// Size > 1 means a nested block inside a function; never pop the function scope
// here. Size == 1 is only popped for top-level blocks (function scope is popped
// in EndFunctionScope).
static void EndBlockScope() {
  if (VarScopes.size() > 1)
    VarScopes.pop_back(), VarStructScopes.pop_back();
  else if (ParsingTopLevel && VarScopes.size() == 1) {
    VarScopes.pop_back();
    VarStructScopes.pop_back();
  }
}

// Check only the innermost scope (used for redeclaration checks).

// Ensure a function scope exists, then add a new scope for the loop variable.
static void BeginLoopScope(const string &Name, ValueType Type) {
  VarScopes.emplace_back();
  VarStructScopes.emplace_back();
  VarScopes.back()[Name] = Type;
}

// Size == 1 is only popped for top-level blocks (function scope is popped in
// EndFunctionScope).
static void EndLoopScope() {
  if (VarScopes.size() > 1)
    VarScopes.pop_back(), VarStructScopes.pop_back();
  if (ParsingTopLevel && VarScopes.size() == 1) {
    VarScopes.pop_back();
    VarStructScopes.pop_back();
  }
}

struct FunctionScopeGuard {
  FunctionScopeGuard(const vector<pair<string, ValueType>> &Parameters,
                     const vector<string> &ParameterStructNames) {
    BeginFunctionScope(Parameters, ParameterStructNames);
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

struct ParseSwitchGuard {
  ParseSwitchGuard() { ++ParseSwitchDepth; }
  ~ParseSwitchGuard() { --ParseSwitchDepth; }
};



struct ReturnTypeGuard {
  ValueType Saved;
  string SavedStructName;
  ReturnTypeGuard(ValueType Type, const string &StructName = "")
      : Saved(CurrentFunctionReturnType),
        SavedStructName(CurrentFunctionReturnStructName) {
    CurrentFunctionReturnType = Type;
    CurrentFunctionReturnStructName = StructName;
  }
  ~ReturnTypeGuard() {
    CurrentFunctionReturnType = Saved;
    CurrentFunctionReturnStructName = SavedStructName;
  }
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

static string LookupVarStructName(const string &Name) {
  for (auto It = VarStructScopes.rbegin(); It != VarStructScopes.rend(); ++It) {
    auto Found = It->find(Name);
    if (Found != It->end())
      return Found->second;
  }
  auto Global = GlobalVarStructNames.find(Name);
  return Global == GlobalVarStructNames.end() ? "" : Global->second;
}

/// PrintReplPrompt - Print the interactive prompt to stderr.
/// Only emits output in REPL mode; silent when running a script file.
void PrintReplPrompt() {
  if (IsRepl)
    fprintf(stderr, "ready> ");
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
unique_ptr<ExpressionNode> LogErrorExpression(const char *Str) {
  HadError = true;
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurrentToken);
  LogErrorAtLoc(Str, Anchor);
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
static ValueType ParseTypeToken(string *StructName = nullptr);
static const char *TypeName(ValueType Type);
static bool IsNumericType(ValueType Type);
static bool IsIntType(ValueType Type);
static bool IsUnsignedIntType(ValueType Type);
static bool IsFloatType(ValueType Type);
static bool IsAssignable(ValueType Dest, ValueType Src);
static Type *LLVMTypeFor(ValueType Type, const string &StructName = "");
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
    return std::move(Result);
  } else {
    // If the surrounding context expects an int type, honor it.
    if (IsIntType(ExpectedLiteralType))
      Type = ExpectedLiteralType;

    // Parse with enough bits to hold the literal's full magnitude, then
    // range-check against the target width.
    unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
    unsigned NeededBits = APInt::getBitsNeeded(NumberLiteral, 10);
    unsigned ParseBits = std::max(Bits, NeededBits);
    APInt Val(ParseBits, NumberLiteral, 10);

    APInt Max = IsUnsignedIntType(Type) ? APInt::getAllOnes(Bits)
                                        : APInt::getSignedMaxValue(Bits);
    if (Val.ugt(Max))
      return LogErrorExpression("Integer literal out of range for type");

    // Truncate down to the target width once it's known to fit.
    // This will actually never happen. It's a paranoia move.
    if (ParseBits != Bits)
      Val = Val.trunc(Bits);

    auto Result = make_unique<NumberExpressionNode>(Val, Type);
    getNextToken(); // consume the number
    return std::move(Result);
  }
}

/// type
///   = "int" | "int8" | "int16" | "int32" | "int64"
///   | "uint8" | "uint16" | "uint32" | "uint64"
///   | "float" | "float32" | "float64"
///   | "bool" | "None" ;
///
/// cast-type
///   = "int" | "int8" | "int16" | "int32" | "int64"
///   | "uint8" | "uint16" | "uint32" | "uint64"
///   | "float" | "float32" | "float64"
///   | "bool" ;
static ValueType ParseTypeToken(string *StructName) {
  if (StructName)
    StructName->clear();
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
  case tok_uint8:
    getNextToken();
    return ValueType::UInt8;
  case tok_uint16:
    getNextToken();
    return ValueType::UInt16;
  case tok_uint32:
    getNextToken();
    return ValueType::UInt32;
  case tok_uint64:
    getNextToken();
    return ValueType::UInt64;
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
  case tok_name: {
    auto Found = StructTypes.find(Name);
    if (Found == StructTypes.end()) {
      LogErrorExpression(("Unknown struct type '" + Name + "'").c_str());
      return ValueType::Error;
    }
    if (StructName)
      *StructName = Name;
    getNextToken();
    return ValueType::Struct;
  }
  default:
    LogErrorExpression("Expected a type");
    return ValueType::Error;
  }
}

/// struct-definition
///   = "struct" name ":" end-of-lines struct-block ;
static bool ParseStructDefinition() {
  getNextToken(); // eat 'struct'
  if (CurrentToken != tok_name) {
    LogErrorExpression("Expected name after 'struct'");
    return false;
  }
  string StructName = Name;
  if (StructTypes.count(StructName)) {
    LogErrorExpression("Struct already defined");
    return false;
  }
  getNextToken(); // eat name
  if (CurrentToken != tok_colon) {
    LogErrorExpression("Expected ':' after struct name");
    return false;
  }
  getNextToken(); // eat ':'
  if (CurrentToken != tok_eol) {
    LogErrorExpression("Expected newline after struct header");
    return false;
  }
  consumeNewlines();
  if (CurrentToken != tok_indent) {
    LogErrorExpression("Expected an indented struct body");
    return false;
  }
  getNextToken(); // eat INDENT

  StructTypeInfo Info;
  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken != tok_name) {
      LogErrorExpression("Expected field name in struct body");
      return false;
    }
    string FieldName = Name;
    if (Info.FieldIndices.count(FieldName)) {
      LogErrorExpression("Duplicate struct field");
      return false;
    }
    getNextToken(); // eat field name
    if (CurrentToken != tok_colon) {
      LogErrorExpression("Expected ':' after field name");
      return false;
    }
    getNextToken(); // eat ':'
    string FieldStructName;
    ValueType FieldType = ParseTypeToken(&FieldStructName);
    if (FieldType == ValueType::Error)
      return false;
    if (FieldType == ValueType::None) {
      LogErrorExpression("Struct fields cannot have None type");
      return false;
    }
    Info.FieldIndices[FieldName] = Info.Fields.size();
    Info.Fields.push_back({FieldName, FieldType, FieldStructName});
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }

  if (Info.Fields.empty()) {
    LogErrorExpression("Struct requires at least one field");
    return false;
  }
  if (CurrentToken != tok_dedent) {
    LogErrorExpression("Expected dedent after struct body");
    return false;
  }
  StructTypes[StructName] = std::move(Info);
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface block-end
  return true;
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
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// name-expression
///   = name
///   | call-expression ;
///
/// call-expression
///   = name "(" [ arguments ] ")" ;
static unique_ptr<ExpressionNode>
ParseFieldExpressionWithBase(const string &BaseName, ValueType BaseType,
                             string BaseStructName) {
  if (BaseType != ValueType::Struct)
    return LogErrorExpression("Field access requires a struct value");

  vector<string> FieldPath;
  ValueType ResultType = BaseType;
  string ResultStructName = std::move(BaseStructName);
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name)
      return LogErrorExpression("Expected field name after '.'");
    string FieldName = Name;
    auto Struct = StructTypes.find(ResultStructName);
    if (Struct == StructTypes.end())
      return LogErrorExpression("Unknown struct type in field access");
    auto FieldIndex = Struct->second.FieldIndices.find(FieldName);
    if (FieldIndex == Struct->second.FieldIndices.end())
      return LogErrorExpression(
          ("Unknown field '" + FieldName + "'").c_str());
    const auto &Field = Struct->second.Fields[FieldIndex->second];
    ResultType = Field.Type;
    ResultStructName = Field.StructName;
    FieldPath.push_back(FieldName);
    getNextToken(); // eat field name
    if (CurrentToken == tok_dot && ResultType != ValueType::Struct)
      return LogErrorExpression("Field access requires a struct value");
  }

  return make_unique<FieldExpressionNode>(BaseName, std::move(FieldPath),
                                           ResultType, ResultStructName);
}

static unique_ptr<ExpressionNode> ParseNameExpressionWithName(const string &ParsedName) {
  if (CurrentToken != tok_lparen) { // Simple variable ref.
    ValueType Type = LookupVarType(ParsedName);
    if (Type == ValueType::Error) {
      return LogErrorExpression("Unknown variable name");
    }
    string StructName = LookupVarStructName(ParsedName);
    if (CurrentToken == tok_dot)
      return ParseFieldExpressionWithBase(ParsedName, Type, StructName);
    return make_unique<NameExpressionNode>(ParsedName, Type, StructName);
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
        if (auto Arg = ParseExpression())
          Arguments.push_back(std::move(Arg));
        else
          return nullptr;
      }

      if (CurrentToken == tok_rparen)
        break;

      if (CurrentToken != tok_comma)
        return LogErrorExpression("Expected ')' or ',' in argument list");
      getNextToken();
      ++ArgIndex;
    }
  }

  // Eat the ')'.
  getNextToken();

  if (!Signature)
    return LogErrorExpression("Unknown function referenced");
  if (Signature->getNumParameters() != Arguments.size())
    return LogErrorExpression("Incorrect # arguments passed");

  for (size_t i = 0; i < Arguments.size(); ++i) {
    ValueType ArgType = Arguments[i]->getType();
    ValueType ParamType = Signature->getParameterType(i);
    if (!IsAssignable(ParamType, ArgType)) {
      return LogErrorExpression(("argument " + std::to_string(i + 1) + " expects " +
                       TypeName(ParamType))
                          .c_str());
    }
    if (ParamType == ValueType::Struct &&
        Signature->getParameterStructName(i) != Arguments[i]->getStructName())
      return LogErrorExpression("Struct argument type mismatch");
  }

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments),
                                  Signature->getReturnType(),
                                  Signature->getReturnStructName());
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

static bool ParseSwitchCaseValue(int64_t &Value) {
  bool Negative = false;
  if (CurrentToken == tok_minus) {
    Negative = true;
    getNextToken();
  }
  if (CurrentToken != tok_number || NumberIsFloat) {
    LogErrorExpression("Switch case value must be an integer literal");
    return false;
  }

  uint64_t Magnitude = 0;
  for (char Digit : NumberLiteral) {
    unsigned ValueOfDigit = static_cast<unsigned>(Digit - '0');
    if (Magnitude >
        (std::numeric_limits<uint64_t>::max() - ValueOfDigit) / 10) {
      LogErrorExpression("Switch case value out of range");
      return false;
    }
    Magnitude = Magnitude * 10 + ValueOfDigit;
  }
  getNextToken(); // eat integer

  uint64_t NegativeLimit =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;
  if (Negative) {
    if (Magnitude > NegativeLimit) {
      LogErrorExpression("Switch case value out of range");
      return false;
    }
    Value = Magnitude == NegativeLimit
                ? std::numeric_limits<int64_t>::min()
                : -static_cast<int64_t>(Magnitude);
  } else {
    if (Magnitude >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      LogErrorExpression("Switch case value out of range");
      return false;
    }
    Value = static_cast<int64_t>(Magnitude);
  }
  return true;
}

/// switch-statement
///   = "switch" expression ":" end-of-lines indent switch-body dedent ;
static unique_ptr<ExpressionNode> ParseSwitchStatement() {
  getNextToken(); // eat 'switch'
  auto Condition = ParseExpression();
  if (!Condition)
    return nullptr;
  if (!IsIntType(Condition->getType()))
    return LogErrorExpression("Switch condition must be an integer type");
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after switch expression");
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented switch body");
  getNextToken(); // eat INDENT

  ParseSwitchGuard Switch;
  vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases;
  set<int64_t> SeenValues;
  unique_ptr<ExpressionNode> DefaultCase;

  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_case) {
      if (DefaultCase)
        return LogErrorExpression("Case cannot follow default");
      getNextToken(); // eat 'case'
      vector<int64_t> Values;
      while (true) {
        int64_t Value = 0;
        if (!ParseSwitchCaseValue(Value))
          return nullptr;
        if (!SeenValues.insert(Value).second)
          return LogErrorExpression("Duplicate switch case value");
        Values.push_back(Value);
        if (CurrentToken != tok_comma)
          break;
        getNextToken(); // eat ','
      }
      if (CurrentToken != tok_colon)
        return LogErrorExpression("Expected ':' after case value");
      getNextToken(); // eat ':'
      auto Body = ParseSuite();
      if (!Body)
        return nullptr;
      Cases.emplace_back(std::move(Values), std::move(Body));
    } else if (CurrentToken == tok_default) {
      if (DefaultCase)
        return LogErrorExpression("Duplicate default case");
      getNextToken(); // eat 'default'
      if (CurrentToken != tok_colon)
        return LogErrorExpression("Expected ':' after default");
      getNextToken(); // eat ':'
      DefaultCase = ParseSuite();
      if (!DefaultCase)
        return nullptr;
    } else {
      return LogErrorExpression("Expected 'case' or 'default' in switch body");
    }

    if (CurrentToken == tok_block_end)
      getNextToken();
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }

  if (CurrentToken != tok_dedent)
    return LogErrorExpression("Expected dedent after switch body");
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface block-end
  return make_unique<SwitchStatementNode>(
      std::move(Condition), std::move(Cases), std::move(DefaultCase));
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
    string DeclStructName;
    ValueType DeclType = ParseTypeToken(&DeclStructName);
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
      if (DeclType == ValueType::Struct &&
          DeclStructName != Init->getStructName())
        return LogErrorExpression("Struct type mismatch in variable initialization");
    } else {
      if (DeclType != ValueType::Struct) {
        Init = MakeZeroLiteral(DeclType);
        if (!Init)
          return nullptr;
      }
    }

    VarNames.push_back(
        {ParsedName, DeclType, DeclStructName, std::move(Init)});
    if (IsGlobalDecl) {
      GlobalVarTypes[ParsedName] = DeclType;
      if (!DeclStructName.empty())
        GlobalVarStructNames[ParsedName] = DeclStructName;
      GlobalVarDecls.insert(ParsedName);
    } else {
      DeclareVar(ParsedName, DeclType, DeclStructName);
    }

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
static bool IsUnsignedIntType(ValueType Type);
static bool IsFloatType(ValueType Type);
static bool IsNumericType(ValueType Type);

static bool CanWidenInt(ValueType From, ValueType To) {
  if (From == To)
    return true;
  if (IsIntType(From) && IsIntType(To)) {
    if (IsUnsignedIntType(From) != IsUnsignedIntType(To))
      return false;
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

static bool IsLogicalOp(int Operator) {
  return Operator == tok_and || Operator == tok_or;
}

static bool IsBitwiseOp(int Operator) {
  return Operator == tok_ampersand || Operator == tok_pipe ||
         Operator == tok_caret;
}

static bool IsShiftOp(int Operator) {
  return Operator == tok_shift_left || Operator == tok_shift_right;
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
  if (IsLogicalOp(Operator)) {
    if (L == ValueType::Bool && R == ValueType::Bool)
      return ValueType::Bool;
    return ValueType::Error;
  }
  if (IsBitwiseOp(Operator)) {
    if (!IsIntType(L) || !IsIntType(R))
      return ValueType::Error;
    if (IsAssignable(L, R))
      return L;
    if (IsAssignable(R, L))
      return R;
    return ValueType::Error;
  }
  if (IsShiftOp(Operator)) {
    if (!IsIntType(L) || !IsIntType(R))
      return ValueType::Error;
    return L;
  }
  return ValueType::Error;
}

/// factor
///   = ("-" | "!" | "~") factor | primary ;
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
  default:
    return LogErrorExpression("unknown token when expecting an expression");
  case tok_name:
    return ParseNameExpression();
  case tok_number:
    return ParseNumberExpression();
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
  case tok_uint8:
  case tok_uint16:
  case tok_uint32:
  case tok_uint64:
  case tok_float:
  case tok_float32:
  case tok_float64:
  case tok_bool:
    return ParseCastExpression();
  case tok_lparen:
    return ParseParenthesizedExpression();
  }
}

/// factor
///   = ("-" | "!" | "~") factor | primary ;
static unique_ptr<ExpressionNode> ParseFactor() {
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();
  if (CurrentToken == tok_exclamation) {
    getNextToken(); // eat '!'
    auto Operand = ParseFactor();
    if (!Operand)
      return nullptr;
    if (Operand->getType() != ValueType::Bool)
      return LogErrorExpression("Unary '!' requires a bool operand");
    return make_unique<UnaryExpressionNode>(tok_exclamation,
                                             std::move(Operand),
                                             ValueType::Bool);
  }
  if (CurrentToken == tok_tilde) {
    getNextToken(); // eat '~'
    auto Operand = ParseFactor();
    if (!Operand)
      return nullptr;
    if (!IsIntType(Operand->getType()))
      return LogErrorExpression("Unary '~' requires an integer operand");
    ValueType OperandType = Operand->getType();
    return make_unique<UnaryExpressionNode>(tok_tilde, std::move(Operand),
                                             OperandType);
  }
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
ParseShiftRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_shift_left || CurrentToken == tok_shift_right) {
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

static unique_ptr<ExpressionNode> ParseShift() {
  auto Left = ParseSum();
  if (!Left)
    return nullptr;
  return ParseShiftRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseRelationalRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_leq || CurrentToken == tok_geq ||
         CurrentToken == tok_less || CurrentToken == tok_greater) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseShift();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseRelational() {
  auto Left = ParseShift();
  if (!Left)
    return nullptr;
  return ParseRelationalRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseEqualityRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_eq || CurrentToken == tok_neq) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseRelational();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseEquality() {
  auto Left = ParseRelational();
  if (!Left)
    return nullptr;
  return ParseEqualityRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseBitwiseAndRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_ampersand) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseEquality();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseBitwiseAnd() {
  auto Left = ParseEquality();
  if (!Left)
    return nullptr;
  return ParseBitwiseAndRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseBitwiseXorRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_caret) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseBitwiseAnd();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseBitwiseXor() {
  auto Left = ParseBitwiseAnd();
  if (!Left)
    return nullptr;
  return ParseBitwiseXorRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseBitwiseOrRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_pipe) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseBitwiseXor();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseBitwiseOr() {
  auto Left = ParseBitwiseXor();
  if (!Left)
    return nullptr;
  return ParseBitwiseOrRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseLogicalAndRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_and) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseBitwiseOr();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseLogicalAnd() {
  auto Left = ParseBitwiseOr();
  if (!Left)
    return nullptr;
  return ParseLogicalAndRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseLogicalOrRight(unique_ptr<ExpressionNode> Left) {
  while (CurrentToken == tok_or) {
    int Operator = CurrentToken;
    getNextToken();
    auto Right = ParseLogicalAnd();
    if (!Right)
      return nullptr;
    Left = MergeBinaryExpression(Operator, std::move(Left), std::move(Right));
    if (!Left)
      return nullptr;
  }
  return Left;
}

static unique_ptr<ExpressionNode> ParseLogicalOr() {
  auto Left = ParseLogicalAnd();
  if (!Left)
    return nullptr;
  return ParseLogicalOrRight(std::move(Left));
}

static unique_ptr<ExpressionNode>
ParseBinaryExpressionRight(unique_ptr<ExpressionNode> Left) {
  Left = ParseTermRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseSumRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseShiftRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseRelationalRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseEqualityRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseBitwiseAndRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseBitwiseXorRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseBitwiseOrRight(std::move(Left));
  if (!Left)
    return nullptr;
  Left = ParseLogicalAndRight(std::move(Left));
  if (!Left)
    return nullptr;
  return ParseLogicalOrRight(std::move(Left));
}

/// expression
///   = logical-or ;
static unique_ptr<ExpressionNode> ParseExpression() {
  return ParseLogicalOr();
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
  if (CurrentFunctionReturnType == ValueType::Struct &&
      CurrentFunctionReturnStructName != Expr->getStructName())
    return LogErrorExpression("Struct return type mismatch");
  return make_unique<ReturnStatementNode>(std::move(Expr));
}

static unique_ptr<ExpressionNode> ParseBreakStatement() {
  if (ParseLoopDepth <= 0 && ParseSwitchDepth <= 0)
    return LogErrorExpression("'break' used outside of a loop or switch");
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
  if (VarType == ValueType::Struct &&
      LookupVarStructName(Name) != Right->getStructName())
    return LogErrorExpression("Struct type mismatch in assignment");
  return make_unique<AssignmentStatementNode>(Name, std::move(Right), VarType);
}

static unique_ptr<ExpressionNode>
ParseFieldAssignmentRight(unique_ptr<FieldExpressionNode> Left) {
  ValueType FieldType = Left->getType();
  string FieldStructName = Left->getStructName();
  getNextToken(); // eat '='
  ExpectedLiteralTypeGuard Guard(FieldType);
  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  if (!IsAssignable(FieldType, Right->getType()))
    return LogErrorExpression("Type mismatch in field assignment");
  if (FieldType == ValueType::Struct &&
      FieldStructName != Right->getStructName())
    return LogErrorExpression("Struct type mismatch in field assignment");
  return make_unique<FieldAssignmentStatementNode>(
      std::move(Left), std::move(Right), FieldType, FieldStructName);
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

  if (const auto *FieldPath = Expr->getLValueFieldPath()) {
    const string *BaseName = Expr->getLValueName();
    auto Field = make_unique<FieldExpressionNode>(
        *BaseName, *FieldPath, Expr->getType(), Expr->getStructName());
    return ParseFieldAssignmentRight(std::move(Field));
  }

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
  if (CurrentToken == tok_switch)
    return ParseSwitchStatement();
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
  SourceLocation SignatureLoc = CurLoc;

  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");
  string FnName = Name;
  getNextToken(); // eat function name

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  vector<pair<string, ValueType>> ParameterNames;
  vector<string> ParameterStructNames;
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
      string ArgStructName;
      ValueType ArgType = ParseTypeToken(&ArgStructName);
      if (ArgType == ValueType::Error)
        return nullptr;
      if (ArgType == ValueType::None)
        return LogErrorSignature("Parameters cannot have None type");
      ParameterNames.push_back({ArgName, ArgType});
      ParameterStructNames.push_back(ArgStructName);

      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorSignature("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  getNextToken(); // eat ')'
  return make_unique<FunctionSignatureNode>(
      FnName, std::move(ParameterNames), SignatureLoc, ValueType::Float64,
      std::move(ParameterStructNames));
}

// DefaultType controls what return type is assumed when no '->' is present.
// In chapter 16, missing return types default to None.
static ValueType
ParseOptionalReturnType(string *StructName,
                        ValueType DefaultType = ValueType::None) {
  if (StructName)
    StructName->clear();
  if (CurrentToken != tok_arrow)
    return DefaultType;
  getNextToken(); // eat '->'
  ValueType Type = ParseTypeToken(StructName);
  return Type;
}

/// I parse the inline simple-statement or indented block portion of a
/// function-definition.
static unique_ptr<ExpressionNode> ParseFunctionBody() {
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
  string ReturnStructName;
  ValueType RetType =
      ParseOptionalReturnType(&ReturnStructName, ValueType::None);
  if (RetType == ValueType::Error)
    return nullptr;
  Signature->setReturnType(RetType);
  Signature->setReturnStructName(ReturnStructName);
  FunctionSignatures[Signature->getName()] = Signature->clone();
  ReturnTypeGuard RetGuard(RetType, ReturnStructName);
  FunctionScopeGuard Scope(Signature->getParameters(),
                           Signature->getParameterStructNames());

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

  string FnName = "__pyxc.toplevel." + to_string(TopLevelExprCounter++);
  auto Signature = make_unique<FunctionSignatureNode>(
      FnName, vector<pair<string, ValueType>>(), CurLoc, RetType);
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
  string ReturnStructName;
  ValueType RetType = ParseOptionalReturnType(&ReturnStructName);
  if (RetType == ValueType::Error)
    return nullptr;
  Signature->setReturnType(RetType);
  Signature->setReturnStructName(ReturnStructName);
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
// Builder - Cursor used to append instructions into the current block.
static std::unique_ptr<IRBuilder<NoFolder>> Builder;
// NamedValues - Maps variable names to allocas in the current function.
static std::map<std::string, AllocaInst *> NamedValues;
static std::map<std::string, string> NamedValueStructNames;
static std::map<std::string, StructType *> LLVMStructTypes;
// InGlobalInit - True while emitting the synthetic global init function.
static bool InGlobalInit = false;
// ModuleHasGlobals - Tracks whether this module defines any globals.
static bool ModuleHasGlobals = false;
// Source path and metadata state used while emitting debug information.
static std::string CurrentSourcePath = "<stdin>";
static std::unique_ptr<DIBuilder> DIB;
static DICompileUnit *TheCU = nullptr;
static DIFile *TheDIFile = nullptr;
static DIType *IntDIType = nullptr;
static DIType *Int8DIType = nullptr;
static DIType *Int16DIType = nullptr;
static DIType *Int32DIType = nullptr;
static DIType *Int64DIType = nullptr;
static DIType *UInt8DIType = nullptr;
static DIType *UInt16DIType = nullptr;
static DIType *UInt32DIType = nullptr;
static DIType *UInt64DIType = nullptr;
static DIType *Float32DIType = nullptr;
static DIType *Float64DIType = nullptr;
static DIType *BoolDIType = nullptr;
static DIType *VoidDIType = nullptr;
static DIScope *CurDIScope = nullptr;
static unsigned CurFunctionLine = 1;
struct LoopControlTargets {
  BasicBlock *BreakTarget = nullptr;
  BasicBlock *ContinueTarget = nullptr;
};
static vector<LoopControlTargets> LoopControlStack;
static vector<BasicBlock *> BreakTargetStack;
// TheJIT - ORC JIT instance for REPL execution.
static std::unique_ptr<PyxcJIT> TheJIT;
// TheFPM - Per-function optimization pipeline (JIT).
static std::unique_ptr<FunctionPassManager> TheFPM;
// TheMPM - Per-module optimization pipeline used by file emission.
static std::unique_ptr<ModulePassManager> TheMPM;
// TheLAM - Loop analysis manager (new PM).
static std::unique_ptr<LoopAnalysisManager> TheLAM;
// TheFAM - Function analysis manager (new PM).
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
// TheCGAM - CGSCC analysis manager (new PM).
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
// TheMAM - Module analysis manager (new PM).
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
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
  case ValueType::UInt8:
    return "uint8";
  case ValueType::UInt16:
    return "uint16";
  case ValueType::UInt32:
    return "uint32";
  case ValueType::UInt64:
    return "uint64";
  case ValueType::Float:
    return "float";
  case ValueType::Float32:
    return "float32";
  case ValueType::Float64:
    return "float64";
  case ValueType::Bool:
    return "bool";
  case ValueType::Struct:
    return "struct";
  default:
    return "<error>";
  }
}

static bool IsIntType(ValueType Type) {
  return Type == ValueType::Int8 || Type == ValueType::Int16 ||
         Type == ValueType::Int32 || Type == ValueType::Int ||
         Type == ValueType::Int64 || Type == ValueType::UInt8 ||
         Type == ValueType::UInt16 || Type == ValueType::UInt32 ||
         Type == ValueType::UInt64;
}

static bool IsUnsignedIntType(ValueType Type) {
  return Type == ValueType::UInt8 || Type == ValueType::UInt16 ||
         Type == ValueType::UInt32 || Type == ValueType::UInt64;
}

static bool IsFloatType(ValueType Type) {
  return Type == ValueType::Float || Type == ValueType::Float32 ||
         Type == ValueType::Float64;
}

static bool IsNumericType(ValueType Type) {
  return IsIntType(Type) || IsFloatType(Type);
}

static Type *GetOrCreateLLVMStructType(const string &StructName) {
  auto Existing = LLVMStructTypes.find(StructName);
  if (Existing != LLVMStructTypes.end())
    return Existing->second;

  auto Definition = StructTypes.find(StructName);
  if (Definition == StructTypes.end())
    return nullptr;

  auto *LLVMStruct = StructType::create(*TheContext, "struct." + StructName);
  LLVMStructTypes[StructName] = LLVMStruct;
  vector<Type *> FieldTypes;
  for (const auto &Field : Definition->second.Fields) {
    Type *FieldType = LLVMTypeFor(Field.Type, Field.StructName);
    if (!FieldType)
      return nullptr;
    FieldTypes.push_back(FieldType);
  }
  LLVMStruct->setBody(FieldTypes, false);
  return LLVMStruct;
}

static Type *LLVMTypeFor(ValueType Type, const string &StructName) {
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
  case ValueType::UInt8:
    return Type::getInt8Ty(*TheContext);
  case ValueType::UInt16:
    return Type::getInt16Ty(*TheContext);
  case ValueType::UInt32:
    return Type::getInt32Ty(*TheContext);
  case ValueType::UInt64:
    return Type::getInt64Ty(*TheContext);
  case ValueType::Float:
    return Type::getDoubleTy(*TheContext);
  case ValueType::Float32:
    return Type::getFloatTy(*TheContext);
  case ValueType::Float64:
    return Type::getDoubleTy(*TheContext);
  case ValueType::Bool:
    return Type::getInt1Ty(*TheContext);
  case ValueType::Struct:
    return GetOrCreateLLVMStructType(StructName);
  case ValueType::None:
    return Type::getVoidTy(*TheContext);
  default:
    return nullptr;
  }
}

static DIType *DITypeFor(ValueType Type) {
  switch (Type) {
  case ValueType::Int:
    return IntDIType;
  case ValueType::Int8:
    return Int8DIType;
  case ValueType::Int16:
    return Int16DIType;
  case ValueType::Int32:
    return Int32DIType;
  case ValueType::Int64:
    return Int64DIType;
  case ValueType::UInt8:
    return UInt8DIType;
  case ValueType::UInt16:
    return UInt16DIType;
  case ValueType::UInt32:
    return UInt32DIType;
  case ValueType::UInt64:
    return UInt64DIType;
  case ValueType::Float:
  case ValueType::Float64:
    return Float64DIType;
  case ValueType::Float32:
    return Float32DIType;
  case ValueType::Bool:
    return BoolDIType;
  case ValueType::None:
    return VoidDIType;
  case ValueType::Struct:
    return DIB ? DIB->createUnspecifiedType("struct") : nullptr;
  default:
    return nullptr;
  }
}

static OptimizationLevel GetOptLevel() {
  switch (OptLevel) {
  case 0:
    return OptimizationLevel::O0;
  case 1:
    return OptimizationLevel::O1;
  case 2:
    return OptimizationLevel::O2;
  default:
    return OptimizationLevel::O3;
  }
}

static void InitializeDebugInfo() {
  if (!DebugInfo || !IsEmitMode()) {
    DIB.reset();
    TheCU = nullptr;
    TheDIFile = nullptr;
    return;
  }

  DIB = std::make_unique<DIBuilder>(*TheModule);

  StringRef FullPath(CurrentSourcePath);
  StringRef FileName = sys::path::filename(FullPath);
  StringRef Directory = sys::path::parent_path(FullPath);
  if (Directory.empty())
    Directory = ".";

  TheDIFile = DIB->createFile(FileName, Directory);
  TheCU = DIB->createCompileUnit(dwarf::DW_LANG_C, TheDIFile, "pyxc",
                                 OptLevel != 0, "", 0);

  unsigned IntBits = TheModule->getDataLayout().getPointerSizeInBits();
  IntDIType = DIB->createBasicType("int", IntBits, dwarf::DW_ATE_signed);
  Int8DIType = DIB->createBasicType("int8", 8, dwarf::DW_ATE_signed);
  Int16DIType = DIB->createBasicType("int16", 16, dwarf::DW_ATE_signed);
  Int32DIType = DIB->createBasicType("int32", 32, dwarf::DW_ATE_signed);
  Int64DIType = DIB->createBasicType("int64", 64, dwarf::DW_ATE_signed);
  UInt8DIType = DIB->createBasicType("uint8", 8, dwarf::DW_ATE_unsigned);
  UInt16DIType = DIB->createBasicType("uint16", 16, dwarf::DW_ATE_unsigned);
  UInt32DIType = DIB->createBasicType("uint32", 32, dwarf::DW_ATE_unsigned);
  UInt64DIType = DIB->createBasicType("uint64", 64, dwarf::DW_ATE_unsigned);
  Float32DIType = DIB->createBasicType("float32", 32, dwarf::DW_ATE_float);
  Float64DIType = DIB->createBasicType("float64", 64, dwarf::DW_ATE_float);
  BoolDIType = DIB->createBasicType("bool", 1, dwarf::DW_ATE_boolean);
  VoidDIType = DIB->createUnspecifiedType("None");

  TheModule->addModuleFlag(Module::Warning, "Dwarf Version",
                           dwarf::DWARF_VERSION);
  TheModule->addModuleFlag(Module::Warning, "Debug Info Version",
                           DEBUG_METADATA_VERSION);
}

static void FinalizeDebugInfo() {
  if (DIB)
    DIB->finalize();
}

static void SetCurrentDebugLocation(unsigned Line) {
  if (!DIB || !CurDIScope)
    return;
  Builder->SetCurrentDebugLocation(
      DILocation::get(*TheContext, Line, 1, CurDIScope));
}

static void EmitDebugDeclare(AllocaInst *Alloca, StringRef Name, unsigned Line,
                             bool IsParameter, unsigned ArgumentNumber,
                             ValueType Type) {
  if (!DIB || !CurDIScope || !Alloca)
    return;

  DIType *DebugType = DITypeFor(Type);
  auto *Location = DILocation::get(*TheContext, Line, 1, CurDIScope);
  DILocalVariable *Variable = nullptr;
  if (IsParameter) {
    Variable = DIB->createParameterVariable(
        CurDIScope, Name, ArgumentNumber, TheDIFile, Line, DebugType, true);
  } else {
    Variable = DIB->createAutoVariable(CurDIScope, Name, TheDIFile, Line,
                                       DebugType, true);
  }

  DIB->insertDeclare(Alloca, Variable, DIB->createExpression(), Location,
                     Builder->GetInsertBlock());
}

static void EmitDebugGlobal(GlobalVariable *Global, StringRef Name,
                            unsigned Line, ValueType Type) {
  if (!DIB || !TheCU || !Global)
    return;
  auto *Expression = DIB->createGlobalVariableExpression(
      TheCU, Name, Name, TheDIFile, Line, DITypeFor(Type), true);
  Global->addDebugInfo(Expression);
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

/// LogErrorV - Codegen-level error helper. Delegates to LogErrorExpression for printing,
/// then returns nullptr so codegen callers can write: return LogErrorV("msg");
Value *LogErrorV(const char *Str) {
  LogErrorExpression(Str);
  return nullptr;
}

/// CreateEntryBlockAlloca - Create a stack slot in the current function's
/// entry block for a mutable variable.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VarName,
                                          ValueType Type,
                                          const string &StructName = "") {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(LLVMTypeFor(Type, StructName), nullptr, VarName);
}

static Constant *ZeroConstant(ValueType Type, const string &StructName = "") {
  switch (Type) {
  case ValueType::Int8:
  case ValueType::UInt8:
    return ConstantInt::get(Type::getInt8Ty(*TheContext), 0);
  case ValueType::Int16:
  case ValueType::UInt16:
    return ConstantInt::get(Type::getInt16Ty(*TheContext), 0);
  case ValueType::Int32:
  case ValueType::UInt32:
    return ConstantInt::get(Type::getInt32Ty(*TheContext), 0);
  case ValueType::Int:
    return ConstantInt::get(LLVMTypeFor(Type), 0);
  case ValueType::Int64:
  case ValueType::UInt64:
    return ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  case ValueType::Float:
    return ConstantFP::get(*TheContext, APFloat(0.0));
  case ValueType::Float32:
    return ConstantFP::get(Type::getFloatTy(*TheContext), 0.0);
  case ValueType::Float64:
    return ConstantFP::get(*TheContext, APFloat(0.0));
  case ValueType::Bool:
    return ConstantInt::get(Type::getInt1Ty(*TheContext), 0);
  case ValueType::Struct:
    return Constant::getNullValue(LLVMTypeFor(Type, StructName));
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
    return IsUnsignedIntType(From)
               ? Builder->CreateUIToFP(V, LLVMTypeFor(To), "uitofp")
               : Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
  if (IsFloatType(From) && IsIntType(To))
    return IsUnsignedIntType(To)
               ? Builder->CreateFPToUI(V, LLVMTypeFor(To), "fptoui")
               : Builder->CreateFPToSI(V, LLVMTypeFor(To), "fptosi");
  // Integer resize (trunc or sign-extend).
  if (IsIntType(From) && IsIntType(To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    if (FromBits == ToBits)
      return V;
    if (ToBits < FromBits)
      return Builder->CreateTrunc(V, LLVMTypeFor(To), "trunc");
    return IsUnsignedIntType(From)
               ? Builder->CreateZExt(V, LLVMTypeFor(To), "zext")
               : Builder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  // Float resize.
  if (IsFloatType(From) && IsFloatType(To)) {
    if (From == ValueType::Float32 && To == ValueType::Float64)
      return Builder->CreateFPExt(V, LLVMTypeFor(To), "fpext");
    return Builder->CreateFPTrunc(V, LLVMTypeFor(To), "fptrunc");
  }
  // Cast to bool: any nonzero value is true.
  if (To == ValueType::Bool) {
    if (IsIntType(From) || From == ValueType::Bool)
      return Builder->CreateICmpNE(V, ConstantInt::get(LLVMTypeFor(From), 0),
                                   "tobool");
    if (IsFloatType(From))
      return Builder->CreateFCmpONE(V, ConstantFP::get(LLVMTypeFor(From), 0.0),
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
      return Builder->CreateFPExt(V, LLVMTypeFor(To), "fpext");
    return nullptr;
  }
  if (IsIntType(From) && IsIntType(To) && CanWidenInt(From, To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    if (FromBits == ToBits)
      return V;
    return IsUnsignedIntType(From)
               ? Builder->CreateZExt(V, LLVMTypeFor(To), "zext")
               : Builder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  if (IsIntType(From) && IsFloatType(To))
    return IsUnsignedIntType(From)
               ? Builder->CreateUIToFP(V, LLVMTypeFor(To), "uitofp")
               : Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
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
  auto *Type = LLVMTypeFor(GlobalVarTypes[Name], GlobalVarStructNames[Name]);
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
  return LogErrorV("Unknown numeric literal type");
}

Value *BoolExpressionNode::codegen() {
  return ConstantInt::get(Type::getInt1Ty(*TheContext), Value ? 1 : 0);
}

/// NameExpressionNode::codegen - A variable reference loads the current value
/// from the variable's stack slot.
Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second)
    return Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()), It->second,
                               Name.c_str());

  if (auto *GV = GetGlobalVariable(Name))
    return Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()), GV,
                               Name.c_str());

  return LogErrorV("Unknown variable name");
}

static Value *GetFieldAddress(const string &BaseName,
                              const vector<string> &FieldPath,
                              ValueType *OutType = nullptr,
                              string *OutStructName = nullptr) {
  Value *Pointer = nullptr;
  string CurrentStructName;

  auto Local = NamedValues.find(BaseName);
  if (Local != NamedValues.end() && Local->second) {
    Pointer = Local->second;
    auto Struct = NamedValueStructNames.find(BaseName);
    if (Struct != NamedValueStructNames.end())
      CurrentStructName = Struct->second;
  } else if (auto *Global = GetGlobalVariable(BaseName)) {
    Pointer = Global;
    auto Struct = GlobalVarStructNames.find(BaseName);
    if (Struct != GlobalVarStructNames.end())
      CurrentStructName = Struct->second;
  }

  if (!Pointer || CurrentStructName.empty())
    return nullptr;

  ValueType CurrentType = ValueType::Struct;
  for (const auto &FieldName : FieldPath) {
    auto Struct = StructTypes.find(CurrentStructName);
    if (Struct == StructTypes.end())
      return nullptr;
    auto Field = Struct->second.FieldIndices.find(FieldName);
    if (Field == Struct->second.FieldIndices.end())
      return nullptr;

    const auto &FieldInfo = Struct->second.Fields[Field->second];
    Pointer = Builder->CreateStructGEP(
        LLVMTypeFor(CurrentType, CurrentStructName), Pointer, Field->second,
        "fieldptr");
    CurrentType = FieldInfo.Type;
    CurrentStructName = FieldInfo.StructName;
  }

  if (OutType)
    *OutType = CurrentType;
  if (OutStructName)
    *OutStructName = CurrentStructName;
  return Pointer;
}

Value *FieldExpressionNode::codegen() {
  ValueType FieldType = ValueType::Error;
  string FieldStructName;
  Value *Pointer = GetFieldAddress(*getLValueName(), FieldPath, &FieldType,
                                   &FieldStructName);
  if (!Pointer)
    return LogErrorV("Unknown field access");
  return Builder->CreateLoad(LLVMTypeFor(FieldType, FieldStructName), Pointer,
                             "fieldload");
}

/// AssignmentStatementNode::codegen - Evaluate the Right, store it into the variable's
/// stack slot, and produce the assigned value.
Value *AssignmentStatementNode::codegen() {
  Value *Val = Expr->codegen();
  if (!Val)
    return nullptr;
  Val = EmitImplicitCast(Val, Expr->getType(), getType());
  if (!Val)
    return LogErrorV("Type mismatch in assignment");

  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second) {
    Builder->CreateStore(Val, It->second);
    return Val;
  }

  if (auto *GV = GetGlobalVariable(Name)) {
    Builder->CreateStore(Val, GV);
    return Val;
  }

  return LogErrorV("Unknown variable name");
}

Value *FieldAssignmentStatementNode::codegen() {
  ValueType FieldType = ValueType::Error;
  string FieldStructName;
  Value *Pointer = GetFieldAddress(*Left->getLValueName(), Left->getFieldPath(),
                                   &FieldType, &FieldStructName);
  if (!Pointer)
    return LogErrorV("Unknown field access");

  Value *AssignedValue = Right->codegen();
  if (!AssignedValue)
    return nullptr;
  AssignedValue = EmitImplicitCast(AssignedValue, Right->getType(), FieldType);
  if (!AssignedValue)
    return LogErrorV("Type mismatch in assignment");
  Builder->CreateStore(AssignedValue, Pointer);
  return AssignedValue;
}

/// ReturnStatementNode::codegen - Emit a return from the current function.
Value *ReturnStatementNode::codegen() {
  if (!Expr) {
    Builder->CreateRetVoid();
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Value *RetVal = Expr->codegen();
  if (!RetVal)
    return nullptr;
  RetVal = EmitImplicitCast(RetVal, Expr->getType(), CurrentFunctionReturnType);
  if (!RetVal)
    return LogErrorV("Type mismatch in return");
  Builder->CreateRet(RetVal);
  return RetVal;
}

/// BlockStatementNode::codegen - Evaluate statements in order.
/// Saves and restores NamedValues to implement block scoping: variables
/// declared inside the block are not visible after it exits.
Value *BlockStatementNode::codegen() {
  auto SavedBindings = NamedValues;
  auto SavedStructNames = NamedValueStructNames;

  Value *Last = nullptr;
  for (auto &Stmt : Stmts) {
    if (Builder->GetInsertBlock()->getTerminator())
      break;
    Last = Stmt->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      NamedValueStructNames = SavedStructNames;
      return nullptr;
    }
  }

  NamedValues = SavedBindings;
  NamedValueStructNames = SavedStructNames;

  if (!Last)
    return LogErrorV("Empty block");

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
  if (Operator == tok_and || Operator == tok_or) {
    Value *LeftValue = Left->codegen();
    if (!LeftValue)
      return nullptr;

    Function *FunctionIR = Builder->GetInsertBlock()->getParent();
    BasicBlock *LeftBlock = Builder->GetInsertBlock();
    BasicBlock *RightBlock =
        BasicBlock::Create(*TheContext, "logic.rhs", FunctionIR);
    BasicBlock *MergeBlock = BasicBlock::Create(*TheContext, "logic.end");

    if (Operator == tok_and)
      Builder->CreateCondBr(LeftValue, RightBlock, MergeBlock);
    else
      Builder->CreateCondBr(LeftValue, MergeBlock, RightBlock);

    Builder->SetInsertPoint(RightBlock);
    Value *RightValue = Right->codegen();
    if (!RightValue)
      return nullptr;
    Builder->CreateBr(MergeBlock);
    RightBlock = Builder->GetInsertBlock();

    FunctionIR->insert(FunctionIR->end(), MergeBlock);
    Builder->SetInsertPoint(MergeBlock);
    PHINode *Result =
        Builder->CreatePHI(Type::getInt1Ty(*TheContext), 2, "logictmp");
    if (Operator == tok_and) {
      Result->addIncoming(ConstantInt::getFalse(*TheContext), LeftBlock);
      Result->addIncoming(RightValue, RightBlock);
    } else {
      Result->addIncoming(ConstantInt::getTrue(*TheContext), LeftBlock);
      Result->addIncoming(RightValue, RightBlock);
    }
    return Result;
  }

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
      return LogErrorV("Type mismatch in arithmetic");
    if (IsFloatType(getType())) {
      if (Operator == tok_plus)
        return Builder->CreateFAdd(L, R, "addtmp");
      if (Operator == tok_minus)
        return Builder->CreateFSub(L, R, "subtmp");
      if (Operator == tok_slash)
        return Builder->CreateFDiv(L, R, "divtmp");
      if (Operator == tok_percent)
        return Builder->CreateFRem(L, R, "remtmp");
      return Builder->CreateFMul(L, R, "multmp");
    }
    if (Operator == tok_plus)
      return Builder->CreateAdd(L, R, "addtmp");
    if (Operator == tok_minus)
      return Builder->CreateSub(L, R, "subtmp");
    if (Operator == tok_slash)
      return IsUnsignedIntType(getType())
                 ? Builder->CreateUDiv(L, R, "divtmp")
                 : Builder->CreateSDiv(L, R, "divtmp");
    if (Operator == tok_percent)
      return IsUnsignedIntType(getType())
                 ? Builder->CreateURem(L, R, "remtmp")
                 : Builder->CreateSRem(L, R, "remtmp");
    return Builder->CreateMul(L, R, "multmp");
  }
  case tok_ampersand:
  case tok_pipe:
  case tok_caret: {
    ValueType ResultType = getType();
    L = EmitImplicitCast(L, LType, ResultType);
    R = EmitImplicitCast(R, RType, ResultType);
    if (!L || !R)
      return LogErrorV("Type mismatch in binary operator");
    if (Operator == tok_ampersand)
      return Builder->CreateAnd(L, R, "bwand");
    if (Operator == tok_pipe)
      return Builder->CreateOr(L, R, "bwor");
    return Builder->CreateXor(L, R, "bwxor");
  }
  case tok_shift_left:
  case tok_shift_right: {
    R = EmitCast(R, RType, LType);
    if (!R)
      return LogErrorV("Type mismatch in shift operator");
    if (Operator == tok_shift_left)
      return Builder->CreateShl(L, R, "shltmp");
    return IsUnsignedIntType(LType)
               ? Builder->CreateLShr(L, R, "shrtmp")
               : Builder->CreateAShr(L, R, "shrtmp");
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
        return LogErrorV("Type mismatch in comparison");
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
      return LogErrorV("Type mismatch in comparison");

    if (CompareType == ValueType::Bool) {
      if (Operator == tok_eq)
        return Builder->CreateICmpEQ(L, R, "cmptmp");
      return Builder->CreateICmpNE(L, R, "cmptmp");
    }

    L = EmitImplicitCast(L, LType, CompareType);
    R = EmitImplicitCast(R, RType, CompareType);
    if (!L || !R)
      return LogErrorV("Type mismatch in comparison");

    if (IsFloatType(CompareType)) {
      switch (Operator) {
      case tok_less:
        return Builder->CreateFCmpOLT(L, R, "cmptmp");
      case tok_greater:
        return Builder->CreateFCmpOGT(L, R, "cmptmp");
      case tok_eq:
        return Builder->CreateFCmpOEQ(L, R, "cmptmp");
      case tok_neq:
        return Builder->CreateFCmpUNE(L, R, "cmptmp");
      case tok_leq:
        return Builder->CreateFCmpOLE(L, R, "cmptmp");
      case tok_geq:
        return Builder->CreateFCmpOGE(L, R, "cmptmp");
      default:
        break;
      }
    } else {
      switch (Operator) {
      case tok_less:
        return IsUnsignedIntType(CompareType)
                   ? Builder->CreateICmpULT(L, R, "cmptmp")
                   : Builder->CreateICmpSLT(L, R, "cmptmp");
      case tok_greater:
        return IsUnsignedIntType(CompareType)
                   ? Builder->CreateICmpUGT(L, R, "cmptmp")
                   : Builder->CreateICmpSGT(L, R, "cmptmp");
      case tok_eq:
        return Builder->CreateICmpEQ(L, R, "cmptmp");
      case tok_neq:
        return Builder->CreateICmpNE(L, R, "cmptmp");
      case tok_leq:
        return IsUnsignedIntType(CompareType)
                   ? Builder->CreateICmpULE(L, R, "cmptmp")
                   : Builder->CreateICmpSLE(L, R, "cmptmp");
      case tok_geq:
        return IsUnsignedIntType(CompareType)
                   ? Builder->CreateICmpUGE(L, R, "cmptmp")
                   : Builder->CreateICmpSGE(L, R, "cmptmp");
      default:
        break;
      }
    }
    return LogErrorV("Type mismatch in comparison");
  }
  default:
    break;
  }

  return LogErrorV("invalid binary operator");
}

/// UnaryExpressionNode::codegen - Emit built-in unary minus directly.
Value *UnaryExpressionNode::codegen() {
  Value *Operator = Operand->codegen();
  if (!Operator)
    return nullptr;

  // Built-in unary minus.
  if (Opcode == tok_minus) {
    if (IsIntType(getType()))
      return Builder->CreateNeg(Operator, "negtmp");
    if (IsFloatType(getType()))
      return Builder->CreateFNeg(Operator, "negtmp");
    return LogErrorV("Unary '-' not supported for this type");
  }

  if (Opcode == tok_exclamation)
    return Builder->CreateNot(Operator, "nottmp");

  if (Opcode == tok_tilde)
    return Builder->CreateNot(Operator, "bnottmp");

  return LogErrorV("Unknown unary operator");
}

/// CastExpressionNode::codegen - Emit explicit int/double casts.
Value *CastExpressionNode::codegen() {
  Value *V = Expr->codegen();
  if (!V)
    return nullptr;
  Value *Cast = EmitCast(V, Expr->getType(), TargetType);
  if (!Cast)
    return LogErrorV("Invalid cast");
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
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Arguments.size())
    return LogErrorV("Incorrect # arguments passed");

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
        return LogErrorV("Argument type mismatch");
    }
    ArgsV.push_back(ArgVal);
  }

  if (getType() == ValueType::None)
    return Builder->CreateCall(CalleeF, ArgsV);
  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
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
    return LogErrorV("Invalid condition type");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  Builder->SetInsertPoint(ThenBB);
  if (!Then->codegen())
    return nullptr;
  bool ThenTerminated = Builder->GetInsertBlock()->getTerminator();
  if (!ThenTerminated)
    Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(ElseBB);
  if (Else) {
    if (!Else->codegen())
      return nullptr;
  }
  bool ElseTerminated = Builder->GetInsertBlock()->getTerminator();
  if (!ElseTerminated)
    Builder->CreateBr(MergeBB);

  if (Else && ThenTerminated && ElseTerminated) {
    Builder->SetInsertPoint(MergeBB);
    Builder->CreateUnreachable();
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Builder->SetInsertPoint(MergeBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// ForStatementNode::codegen - Emit LLVM IR for a for statement using a mutable
/// stack slot for the loop variable.
Value *ForStatementNode::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  Value *VarPtr = nullptr;
  AllocaInst *Alloca = nullptr;
  AllocaInst *OldVal = nullptr;
  if (IsVarDecl) {
    auto OldIt = NamedValues.find(VarName);
    OldVal = (OldIt != NamedValues.end()) ? OldIt->second : nullptr;
    Alloca = CreateEntryBlockAlloca(TheFunction, VarName, VarType);
    EmitDebugDeclare(Alloca, VarName, CurFunctionLine, false, 0, VarType);
    VarPtr = Alloca;
    NamedValues[VarName] = Alloca;
  } else {
    auto It = NamedValues.find(VarName);
    if (It != NamedValues.end() && It->second)
      VarPtr = It->second;
    else if (auto *GV = GetGlobalVariable(VarName))
      VarPtr = GV;
    else
      return LogErrorV("Unknown variable name");
  }

  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;
  StartVal = EmitImplicitCast(StartVal, Start->getType(), VarType);
  if (!StartVal)
    return LogErrorV("Type mismatch in for loop start");

  Builder->CreateStore(StartVal, VarPtr);

  BasicBlock *CondBB =
      BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
  BasicBlock *BodyBB =
      BasicBlock::Create(*TheContext, "loop_body", TheFunction);
  BasicBlock *StepBB =
      BasicBlock::Create(*TheContext, "loop_step", TheFunction);
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "after_loop", TheFunction);

  Builder->CreateBr(CondBB);

  Builder->SetInsertPoint(CondBB);


  Value *CondVal = Cond->codegen();
  if (!CondVal)
    return nullptr;
  CondVal = ToBool(CondVal, Cond->getType());
  if (!CondVal)
    return LogErrorV("Invalid loop condition type");
  Builder->CreateCondBr(CondVal, BodyBB, AfterBB);

  Builder->SetInsertPoint(BodyBB);

  LoopControlStack.push_back({AfterBB, StepBB});
  BreakTargetStack.push_back(AfterBB);
  if (!Body->codegen()) {
    BreakTargetStack.pop_back();
    LoopControlStack.pop_back();
    return nullptr;
  }
  BreakTargetStack.pop_back();
  LoopControlStack.pop_back();
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(StepBB);

  Builder->SetInsertPoint(StepBB);

  Value *CurVar = Builder->CreateLoad(LLVMTypeFor(VarType), VarPtr, VarName);
  Value *StepVal = Step->codegen();
  if (!StepVal)
    return nullptr;
  StepVal = EmitImplicitCast(StepVal, Step->getType(), VarType);
  if (!StepVal)
    return LogErrorV("Type mismatch in for loop step");
  Value *NextVar = nullptr;
  if (VarType == ValueType::Float64)
    NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
  else
    NextVar = Builder->CreateAdd(CurVar, StepVal, "nextvar");
  Builder->CreateStore(NextVar, VarPtr);
  Builder->CreateBr(CondBB);

  Builder->SetInsertPoint(AfterBB);

  if (IsVarDecl) {
    if (OldVal)
      NamedValues[VarName] = OldVal;
    else
      NamedValues.erase(VarName);
  }

  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *WhileStatementNode::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  BasicBlock *ConditionBlock =
      BasicBlock::Create(*TheContext, "while.condition", TheFunction);
  BasicBlock *BodyBlock =
      BasicBlock::Create(*TheContext, "while.body", TheFunction);
  BasicBlock *AfterBlock =
      BasicBlock::Create(*TheContext, "while.after", TheFunction);

  Builder->CreateBr(IsDoWhile ? BodyBlock : ConditionBlock);

  if (!IsDoWhile) {
    Builder->SetInsertPoint(ConditionBlock);
    Value *ConditionValue = Cond->codegen();
    if (!ConditionValue)
      return nullptr;
    ConditionValue = ToBool(ConditionValue, Cond->getType());
    if (!ConditionValue)
      return LogErrorV("Invalid loop condition type");
    Builder->CreateCondBr(ConditionValue, BodyBlock, AfterBlock);
  }

  Builder->SetInsertPoint(BodyBlock);
  LoopControlStack.push_back({AfterBlock, ConditionBlock});
  BreakTargetStack.push_back(AfterBlock);
  if (!Body->codegen()) {
    BreakTargetStack.pop_back();
    LoopControlStack.pop_back();
    return nullptr;
  }
  BreakTargetStack.pop_back();
  LoopControlStack.pop_back();
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(ConditionBlock);

  Builder->SetInsertPoint(ConditionBlock);
  if (IsDoWhile) {
    Value *ConditionValue = Cond->codegen();
    if (!ConditionValue)
      return nullptr;
    ConditionValue = ToBool(ConditionValue, Cond->getType());
    if (!ConditionValue)
      return LogErrorV("Invalid loop condition type");
    Builder->CreateCondBr(ConditionValue, BodyBlock, AfterBlock);
  }

  Builder->SetInsertPoint(AfterBlock);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *SwitchStatementNode::codegen() {
  Value *ConditionValue = Condition->codegen();
  if (!ConditionValue)
    return nullptr;

  auto *ConditionType = dyn_cast<IntegerType>(LLVMTypeFor(Condition->getType()));
  if (!ConditionType)
    return LogErrorV("Switch condition must be an integer type");

  Function *FunctionIR = Builder->GetInsertBlock()->getParent();
  BasicBlock *AfterBlock =
      BasicBlock::Create(*TheContext, "switch.after", FunctionIR);
  BasicBlock *DefaultBlock =
      DefaultCase
          ? BasicBlock::Create(*TheContext, "switch.default", FunctionIR)
          : AfterBlock;

  unsigned CaseCount = 0;
  for (const auto &Case : Cases)
    CaseCount += Case.first.size();
  auto *SwitchIR =
      Builder->CreateSwitch(ConditionValue, DefaultBlock, CaseCount);

  vector<BasicBlock *> CaseBlocks;
  for (const auto &Case : Cases) {
    BasicBlock *CaseBlock =
        BasicBlock::Create(*TheContext, "switch.case", FunctionIR);
    CaseBlocks.push_back(CaseBlock);
    for (int64_t Value : Case.first) {
      auto *Constant = ConstantInt::get(ConditionType,
                                        static_cast<uint64_t>(Value), true);
      SwitchIR->addCase(Constant, CaseBlock);
    }
  }

  BreakTargetStack.push_back(AfterBlock);
  for (size_t Index = 0; Index < Cases.size(); ++Index) {
    Builder->SetInsertPoint(CaseBlocks[Index]);
    if (!Cases[Index].second->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!Builder->GetInsertBlock()->getTerminator())
      Builder->CreateBr(AfterBlock);
  }

  if (DefaultCase) {
    Builder->SetInsertPoint(DefaultBlock);
    if (!DefaultCase->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!Builder->GetInsertBlock()->getTerminator())
      Builder->CreateBr(AfterBlock);
  }
  BreakTargetStack.pop_back();

  Builder->SetInsertPoint(AfterBlock);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *BreakStatementNode::codegen() {
  if (BreakTargetStack.empty())
    return LogErrorV("'break' used outside of a loop or switch");
  Builder->CreateBr(BreakTargetStack.back());
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *ContinueStatementNode::codegen() {
  if (LoopControlStack.empty())
    return LogErrorV("'continue' used outside of a loop");
  Builder->CreateBr(LoopControlStack.back().ContinueTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// VarStatementNode::codegen - Allocate mutable local variables and initialize them.
Value *VarStatementNode::codegen() {
  if (InGlobalInit) {
    for (auto &Var : VarNames) {
      const string &VarName = Var.Name;
      ValueType VarType = Var.Type;
      const string &VarStructName = Var.StructName;
      ExpressionNode *Init = Var.Init.get();

      auto *GV = TheModule->getNamedGlobal(VarName);
      if (GV && !GV->isDeclaration())
        return LogErrorV("Global variable already defined");
      if (GV && GV->getValueType() != LLVMTypeFor(VarType, VarStructName))
        return LogErrorV("Global variable type mismatch");

      if (!GV) {
        auto *Type = LLVMTypeFor(VarType, VarStructName);
        GV = new GlobalVariable(*TheModule, Type, false,
                                GlobalValue::ExternalLinkage,
                                ZeroConstant(VarType, VarStructName), VarName);
        EmitDebugGlobal(GV, VarName, CurFunctionLine, VarType);
      } else if (GV->isDeclaration()) {
        GV->setInitializer(ZeroConstant(VarType, VarStructName));
        GV->setLinkage(GlobalValue::ExternalLinkage);
        EmitDebugGlobal(GV, VarName, CurFunctionLine, VarType);
      }

      ModuleHasGlobals = true;

      Value *InitVal = Init ? Init->codegen()
                            : ZeroConstant(VarType, VarStructName);
      if (!InitVal)
        return nullptr;
      if (Init) {
        InitVal = EmitImplicitCast(InitVal, Init->getType(), VarType);
        if (!InitVal)
          return LogErrorV("Type mismatch in variable initialization");
      }

      Builder->CreateStore(InitVal, GV);
    }

    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.Name;
    ValueType VarType = Var.Type;
    const string &VarStructName = Var.StructName;
    ExpressionNode *Init = Var.Init.get();

    Value *InitVal = Init ? Init->codegen()
                          : ZeroConstant(VarType, VarStructName);
    if (!InitVal)
      return nullptr;
    if (Init) {
      InitVal = EmitImplicitCast(InitVal, Init->getType(), VarType);
      if (!InitVal)
        return LogErrorV("Type mismatch in variable initialization");
    }

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName, VarType,
                                                VarStructName);
    Builder->CreateStore(InitVal, Alloca);
    NamedValues[VarName] = Alloca;
    if (!VarStructName.empty())
      NamedValueStructNames[VarName] = VarStructName;
    EmitDebugDeclare(Alloca, VarName, CurFunctionLine, false, 0, VarType);
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
/// Arg.setName() is optional — it only affects the printed IR, making output
/// read as 'double %a, double %b' rather than 'double %0, double %1'.
Function *FunctionSignatureNode::codegen() {
  std::vector<Type *> ParameterTypes;
  ParameterTypes.reserve(Parameters.size());
  for (size_t Index = 0; Index < Parameters.size(); ++Index)
    ParameterTypes.push_back(
        LLVMTypeFor(Parameters[Index].second, getParameterStructName(Index)));
  FunctionType *FT = FunctionType::get(
      LLVMTypeFor(ReturnType, ReturnStructName), ParameterTypes,
      false /* not variadic */);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Name arguments so the printed IR is readable.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Parameters[Idx++].first);


  return F;
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
/// 2. Create the entry BasicBlock and point the Builder at it. A basic block
///    is a straight-line sequence of instructions with one entry and one exit.
///    Every function starts with exactly one entry block.
///
/// 3. Populate NamedValues. Clear the table, create an entry-block alloca for
///    each argument, store the incoming argument value into it, and map the
///    variable name to that stack slot. This gives parameters and mutable local
///    variables the same load/store representation.
///
/// 4. Codegen the body expression. On success, emit 'ret', run verifyFunction
///    (LLVM's internal consistency checker), then run TheFPM to apply the
///    optimisation pipeline. On failure, eraseFromParent() removes the
///    partially-built function so no broken declaration is left in the module.
Function *FunctionDefinitionNode::codegen() {
  // Step 1: register the function signature and resolve the Function*.
  auto &P = *Signature;
  FunctionSignatures[Signature->getName()] = std::move(Signature);

  // Step 1: reuse an existing `extern` declaration if one exists.
  Function *TheFunction = getFunction(P.getName());

  // Bail if the function is already fully defined — redefinition is an error.
  if (TheFunction && !TheFunction->empty()) {
    LogErrorExpression("Function cannot be redefined.");
    return nullptr;
  }

  if (!TheFunction)
    return nullptr;

  ValueType SavedRetType = CurrentFunctionReturnType;
  string SavedRetStructName = CurrentFunctionReturnStructName;
  CurrentFunctionReturnType = P.getReturnType();
  CurrentFunctionReturnStructName = P.getReturnStructName();

  if (DIB && TheDIFile && P.getName().rfind("__pyxc.", 0) != 0) {
    unsigned Line = P.getLocation().Line ? P.getLocation().Line : 1;
    SmallVector<Metadata *, 8> Types;
    Types.push_back(DITypeFor(P.getReturnType()));
    for (size_t Index = 0; Index < P.getParameters().size(); ++Index)
      Types.push_back(DITypeFor(P.getParameterType(Index)));
    auto *SubroutineType =
        DIB->createSubroutineType(DIB->getOrCreateTypeArray(Types));
    auto *Subprogram = DIB->createFunction(
        TheDIFile, P.getName(), StringRef(), TheDIFile, Line, SubroutineType,
        Line, DINode::FlagZero, DISubprogram::SPFlagDefinition);
    TheFunction->setSubprogram(Subprogram);
    CurDIScope = Subprogram;
    CurFunctionLine = Line;
  }

  // Step 2: create the entry block and point the builder at it.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);
  SetCurrentDebugLocation(CurFunctionLine);

  // Step 3: populate NamedValues with entry-block allocas for each argument.
  NamedValues.clear();
  NamedValueStructNames.clear();
  LoopControlStack.clear();
  BreakTargetStack.clear();
  unsigned ArgumentNumber = 1;
  size_t ArgTypeIndex = 0;
  for (auto &Arg : TheFunction->args()) {
    ValueType ArgType = P.getParameterType(ArgTypeIndex);
    const string &ArgStructName = P.getParameterStructName(ArgTypeIndex++);
    AllocaInst *Alloca = CreateEntryBlockAlloca(
        TheFunction, std::string(Arg.getName()), ArgType, ArgStructName);
    Builder->CreateStore(&Arg, Alloca);
    NamedValues[std::string(Arg.getName())] = Alloca;
    if (!ArgStructName.empty())
      NamedValueStructNames[std::string(Arg.getName())] = ArgStructName;
    EmitDebugDeclare(Alloca, Arg.getName(), CurFunctionLine, true,
                     ArgumentNumber++, ArgType);
  }

  // Step 4: codegen the body, optimise, verify, or erase on failure.
  if (Value *BodyVal = Body->codegen()) {
    // If the body didn't already terminate the current block (e.g. via
    // return), only void/None functions may fall through. Non-None functions
    // must return explicitly.
    if (!Builder->GetInsertBlock()->getTerminator()) {
      if (P.getReturnType() == ValueType::None) {
        Builder->CreateRetVoid();
      } else {
        BasicBlock *CurBB = Builder->GetInsertBlock();
        bool IsEntry = CurBB == &TheFunction->getEntryBlock();
        if (!IsEntry && pred_empty(CurBB)) {
          Builder->CreateUnreachable();
        } else {
          LogErrorV("Non-None function must return a value");
          TheFunction->eraseFromParent();
          CurDIScope = nullptr;
          CurrentFunctionReturnType = SavedRetType;
          CurrentFunctionReturnStructName = SavedRetStructName;
          return nullptr;
        }
      }
    }
    verifyFunction(*TheFunction);

    // Run the optimisation pipeline: InstCombine, Reassociate, GVN,
    // SimplifyCFG.
    TheFPM->run(*TheFunction, *TheFAM);
    CurDIScope = nullptr;
    CurrentFunctionReturnType = SavedRetType;
    CurrentFunctionReturnStructName = SavedRetStructName;
    return TheFunction;
  }

  // Body codegen failed — remove the incomplete function so it cannot be
  // called and does not pollute the module handed to the JIT.
  TheFunction->eraseFromParent();
  CurDIScope = nullptr;
  CurrentFunctionReturnType = SavedRetType;
  CurrentFunctionReturnStructName = SavedRetStructName;
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
  StructTypes.clear();
  GlobalVarTypes.clear();
  GlobalVarStructNames.clear();
  GlobalVarDecls.clear();
  VarScopes.clear();
  VarStructScopes.clear();
  NamedValueStructNames.clear();
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
/// The analysis managers are cross-registered so that a function pass that
/// needs loop information can reach TheLAM, and so on. PassBuilder then creates
/// LLVM's standard function and module pipelines for the selected level.
static void InitializeModuleAndManagers(bool FreshContext = true) {
  // Fresh context and module for this compilation unit.
  if (FreshContext || !TheContext)
    TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  LLVMStructTypes.clear();
  // Inform the module of the JIT's target data layout so codegen emits
  // correctly-sized types for the host machine.
  TheModule->setDataLayout(TheJIT->getDataLayout());

  Builder = std::make_unique<IRBuilder<NoFolder>>(*TheContext);
  ModuleHasGlobals = false;
  CurDIScope = nullptr;
  CurFunctionLine = 1;

  // Pass and analysis managers.
  TheFPM = std::make_unique<FunctionPassManager>();
  TheMPM = std::make_unique<ModulePassManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  TheCGAM = std::make_unique<CGSCCAnalysisManager>();
  TheMAM = std::make_unique<ModuleAnalysisManager>();

  // Cross-register so passes can access any analysis tier they need.
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerCGSCCAnalyses(*TheCGAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.registerLoopAnalyses(*TheLAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);

  // I ask LLVM to build its standard pipelines for the selected level.
  if (OptLevel != 0) {
    auto FunctionPipeline = PB.buildFunctionSimplificationPipeline(
        GetOptLevel(), ThinOrFullLTOPhase::None);
    TheFPM = std::make_unique<FunctionPassManager>(
        std::move(FunctionPipeline));
    auto ModulePipeline = PB.buildPerModuleDefaultPipeline(GetOptLevel());
    TheMPM =
        std::make_unique<ModulePassManager>(std::move(ModulePipeline));
  }

  InitializeDebugInfo();
}

static void RunModuleOptimizations(Module *Module) {
  if (TheMPM && OptLevel != 0)
    TheMPM->run(*Module, *TheMAM);
}

/// SynchronizeToLineBoundary - Panic-mode error recovery.
///
/// Advance past all remaining tokens on the current line so that MainLoop
/// sees tok_eol or tok_eof next. Called after any parse or codegen failure
/// and after any unexpected trailing token, ensuring the REPL always returns
/// to a clean state before printing the next prompt.
static void SynchronizeToLineBoundary() {
  while (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_dedent)
    getNextToken();
}

static void HandleStructDefinition() {
  bool Parsed = ParseStructDefinition();
  bool HasTrailing = CurrentToken != tok_eol && CurrentToken != tok_eof &&
                     CurrentToken != tok_block_end;
  if (!Parsed || HasTrailing) {
    if (Parsed)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  Log("Parsed a struct definition.\n");
}

/// HandleFunctionDefinition - Parse, optimise, and JIT-compile a 'def' function-definition.
///
/// On success: codegen + optimise the function (TheFPM runs inside
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
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FnIR = FnAST->codegen()) {
    Log("Parsed a function definition.\n");
    if (ShouldDumpIR())
      FnIR->print(errs());
    if (!IsEmitMode()) {
      // Transfer the module to the JIT. TheModule is now invalid; reinitialise.
      ExitOnErr(TheJIT->addModule(
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
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  // Reject conflicting redeclarations: in Pyxc, function identity is just
  // name + arity. We validate types separately in the parser.
  auto Existing = FunctionSignatures.find(ProtoAST->getName());
  if (Existing != FunctionSignatures.end() &&
      Existing->second->getNumParameters() != ProtoAST->getNumParameters()) {
    LogErrorExpression((string("Conflicting extern declaration for '") +
              ProtoAST->getName() + "'")
                 .c_str());
    SynchronizeToLineBoundary();
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
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  string FnName = FnAST->getName();
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
      ExitOnErr(TheJIT->addModule(std::move(TSM)));
      InitializeModuleAndManagers();
    } else {
      // ResourceTracker scopes the JIT memory for this expression so we can
      // free it precisely after the call, without affecting other symbols.
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();

      // Transfer ownership of the module to the JIT; reinitialise for next
      // input.
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      // Locate the compiled function in the JIT's symbol table.
      auto ExprSymbol = ExitOnErr(TheJIT->lookup(FnName));

      if (RetType == ValueType::None) {
        void (*FP)() = ExprSymbol.toPtr<void (*)()>();
        FP();
      } else {
        switch (RetType) {
        case ValueType::Float64: {
          double (*FP)() = ExprSymbol.toPtr<double (*)()>();
          double result = FP();
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%f\n", result);
          break;
        }
        case ValueType::Float32: {
          float (*FP)() = ExprSymbol.toPtr<float (*)()>();
          double result = static_cast<double>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%f\n", result);
          break;
        }
        case ValueType::Int: {
          intptr_t (*FP)() = ExprSymbol.toPtr<intptr_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%lld\n", result);
          break;
        }
        case ValueType::Int8: {
          int8_t (*FP)() = ExprSymbol.toPtr<int8_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%lld\n", result);
          break;
        }
        case ValueType::Int16: {
          int16_t (*FP)() = ExprSymbol.toPtr<int16_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%lld\n", result);
          break;
        }
        case ValueType::Int32: {
          int32_t (*FP)() = ExprSymbol.toPtr<int32_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%lld\n", result);
          break;
        }
        case ValueType::Int64: {
          int64_t (*FP)() = ExprSymbol.toPtr<int64_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%lld\n", result);
          break;
        }
        case ValueType::UInt8: {
          uint8_t (*FP)() = ExprSymbol.toPtr<uint8_t (*)()>();
          unsigned long long result = static_cast<unsigned long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%llu\n", result);
          break;
        }
        case ValueType::UInt16: {
          uint16_t (*FP)() = ExprSymbol.toPtr<uint16_t (*)()>();
          unsigned long long result = static_cast<unsigned long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%llu\n", result);
          break;
        }
        case ValueType::UInt32: {
          uint32_t (*FP)() = ExprSymbol.toPtr<uint32_t (*)()>();
          unsigned long long result = static_cast<unsigned long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%llu\n", result);
          break;
        }
        case ValueType::UInt64: {
          uint64_t (*FP)() = ExprSymbol.toPtr<uint64_t (*)()>();
          unsigned long long result = static_cast<unsigned long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%llu\n", result);
          break;
        }
        case ValueType::Bool: {
          bool (*FP)() = ExprSymbol.toPtr<bool (*)()>();
          bool result = FP();
          if (IsRepl && LastTopLevelShouldPrint)
            fprintf(stderr, "%s\n", result ? "True" : "False");
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
    auto ExprSymbol = ExitOnErr(TheJIT->lookup(FnName));
    if (RetType == ValueType::None) {
      void (*FP)() = ExprSymbol.toPtr<void (*)()>();
      FP();
    } else {
      switch (RetType) {
      case ValueType::Float64: {
        double (*FP)() = ExprSymbol.toPtr<double (*)()>();
        double result = FP();
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%f\n", result);
        break;
      }
      case ValueType::Float32: {
        float (*FP)() = ExprSymbol.toPtr<float (*)()>();
        double result = static_cast<double>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%f\n", result);
        break;
      }
      case ValueType::Int: {
        intptr_t (*FP)() = ExprSymbol.toPtr<intptr_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%lld\n", result);
        break;
      }
      case ValueType::Int8: {
        int8_t (*FP)() = ExprSymbol.toPtr<int8_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%lld\n", result);
        break;
      }
      case ValueType::Int16: {
        int16_t (*FP)() = ExprSymbol.toPtr<int16_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%lld\n", result);
        break;
      }
      case ValueType::Int32: {
        int32_t (*FP)() = ExprSymbol.toPtr<int32_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%lld\n", result);
        break;
      }
      case ValueType::Int64: {
        int64_t (*FP)() = ExprSymbol.toPtr<int64_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%lld\n", result);
        break;
      }
      case ValueType::UInt8: {
        uint8_t (*FP)() = ExprSymbol.toPtr<uint8_t (*)()>();
        unsigned long long result = static_cast<unsigned long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%llu\n", result);
        break;
      }
      case ValueType::UInt16: {
        uint16_t (*FP)() = ExprSymbol.toPtr<uint16_t (*)()>();
        unsigned long long result = static_cast<unsigned long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%llu\n", result);
        break;
      }
      case ValueType::UInt32: {
        uint32_t (*FP)() = ExprSymbol.toPtr<uint32_t (*)()>();
        unsigned long long result = static_cast<unsigned long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%llu\n", result);
        break;
      }
      case ValueType::UInt64: {
        uint64_t (*FP)() = ExprSymbol.toPtr<uint64_t (*)()>();
        unsigned long long result = static_cast<unsigned long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%llu\n", result);
        break;
      }
      case ValueType::Bool: {
        bool (*FP)() = ExprSymbol.toPtr<bool (*)()>();
        bool result = FP();
        if (IsRepl && LastTopLevelShouldPrint)
          fprintf(stderr, "%s\n", result ? "True" : "False");
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
      LogErrorExpression(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
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

/// putchard - Write a single ASCII character to stderr. The double argument
/// is truncated to char. Returns 0.0 so it can be used as an expression.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - Print a double to stderr as "%f\n". Returns 0.0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
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
/// the handler calls SynchronizeToLineBoundary() to discard all remaining
/// tokens on the current line. Either way we return here to look at the
/// next CurrentToken.
static void MainLoop() {
  while (true) {
    if (CurrentToken == tok_eof)
      return;

    // A bare newline: just print a fresh prompt and read the next token.
    if (CurrentToken == tok_eol) {
      PrintReplPrompt();
      getNextToken();
      continue;
    }

    if (CurrentToken == tok_indent) {
      LogErrorExpression("Unexpected indentation");
      SynchronizeToLineBoundary();
      continue;
    }

    // Stray dedent at top level (can occur in REPL mode): skip it.
    if (CurrentToken == tok_dedent || CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }

    if (CurrentToken == tok_error) {
      SynchronizeToLineBoundary();
      continue;
    }

    switch (CurrentToken) {
    case tok_struct:
      HandleStructDefinition();
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
      SynchronizeToLineBoundary();
      continue;
    }

    if (CurrentToken == tok_dedent || CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }

    if (CurrentToken == tok_error) {
      SynchronizeToLineBoundary();
      continue;
    }

    switch (CurrentToken) {
    case tok_struct:
      HandleStructDefinition();
      break;
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
      ExitOnErr(TheJIT->addModule(std::move(TSM)));
      InitializeModuleAndManagers();

      auto InitSymbol = ExitOnErr(TheJIT->lookup("__pyxc.global_init"));
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

  auto MainSymbol = ExitOnErr(TheJIT->lookup("main"));
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
  if (M == TheModule.get())
    FinalizeDebugInfo();

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
  CurrentSourcePath = Path;
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

  RunModuleOptimizations(TheModule.get());
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

static void MaybeEmitDsymBundle(const string &ExecutablePath) {
  if (!DebugInfo)
    return;

  Triple TargetTriple(sys::getDefaultTargetTriple());
  if (!TargetTriple.isOSDarwin())
    return;

  auto Dsymutil = sys::findProgramByName("dsymutil");
  if (!Dsymutil) {
    fprintf(stderr,
            "Warning: dsymutil not found; debug info will remain in .o files\n");
    return;
  }

  std::vector<StringRef> Arguments{*Dsymutil, ExecutablePath};
  if (sys::ExecuteAndWait(*Dsymutil, Arguments))
    fprintf(stderr, "Warning: dsymutil failed; debug info may be missing\n");
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
  RunModuleOptimizations(TheModule.get());
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
  MaybeEmitDsymBundle(EmitOutputPath);
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

  if (DebugInfo && OptLevel.getNumOccurrences() == 0)
    OptLevel = 0;

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

  CurrentSourcePath = IsRepl ? "<stdin>" : InputFiles.front();

  // Create the JIT first — InitializeModuleAndManagers() needs TheJIT in
  // order to set the data layout on the new module.
  TheJIT = ExitOnErr(PyxcJIT::Create());
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

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
static cl::list<string> InputFiles(cl::Positional, cl::desc("[inputs]"),
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
static cl::opt<string>
    EmitKindOpt("emit", cl::desc("Emit output: llvm-ir | asm | obj | exe"),
                cl::init(""), cl::cat(PyxcCategory));
static cl::opt<string> OutputFile("o", cl::desc("Output filename"),
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
  tok_var = -17,

  // types
  tok_int = -18,

  // indentation
  tok_indent = -19,
  tok_dedent = -20,
  tok_block_end = -100, // synthetic: injected by ParseBlock after eating DEDENT

  // new type keywords
  tok_int8 = -21,
  tok_int16 = -22,
  tok_int32 = -23,
  tok_int64 = -24,
  tok_float = -25,
  tok_float32 = -26,
  tok_float64 = -27,
  tok_bool = -28,
  tok_none = -29,
  tok_true = -30,
  tok_false = -31,
  tok_elif = -32,
  tok_while = -33,
  tok_do = -34,
  tok_break = -35,
  tok_continue = -36,
  tok_uint8 = -37,
  tok_uint16 = -38,
  tok_uint32 = -39,
  tok_uint64 = -40,
  tok_and = -41, // &&
  tok_or = -42,  // ||
  tok_shift_left = -43,  // <<
  tok_shift_right = -44, // >>
  tok_switch = -45,
  tok_case = -46,
  tok_default = -47,
  tok_struct = -48,
  tok_ptr = -49,
  tok_addr = -50,
  tok_sizeof = -51,
  tok_type = -52,
  tok_string = -53,
  tok_character = -54,

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
  tok_assign = '=',
  tok_exclamation = '!',
  tok_ampersand = '&',
  tok_pipe = '|',
  tok_caret = '^',
  tok_tilde = '~',
  tok_dot = '.',
  tok_lbracket = '[',
  tok_rbracket = ']',
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
  Array,
  Pointer,
  Error
};

static string Name;    // Filled in if tok_name
static string NumberLiteral;    // Raw number literal text (no sign)
static string StringLiteralValue;
static uint32_t CharacterLiteralValue = 0;
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
    {"ptr", tok_ptr},         {"addr", tok_addr},
    {"sizeof", tok_sizeof},
    {"type", tok_type},
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
      {tok_ptr, "'ptr'"},         {tok_addr, "'addr'"},
      {tok_sizeof, "'sizeof'"},
      {tok_type, "'type'"},
      {tok_string, "string literal"},
      {tok_character, "character literal"},
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
static void LogErrorAtLocation(const string &ErrorMessage, SourceLocation Location);
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
enum class LiteralDecodeError {
  None,
  InvalidEscape,
  InvalidCodePoint,
  InvalidUtf8,
};

static int HexDigitValue(int Character) {
  if (Character >= '0' && Character <= '9')
    return Character - '0';
  if (Character >= 'a' && Character <= 'f')
    return Character - 'a' + 10;
  if (Character >= 'A' && Character <= 'F')
    return Character - 'A' + 10;
  return -1;
}

static bool IsUnicodeScalarValue(uint32_t Value) {
  return Value <= 0x10FFFF && !(Value >= 0xD800 && Value <= 0xDFFF);
}

static LiteralDecodeError DecodeLiteralCodePoint(uint32_t &Value) {
  if (LexerLastChar == '\\') {
    LexerLastChar = advance(); // eat '\\'
    switch (LexerLastChar) {
    case '\\': Value = '\\'; LexerLastChar = advance(); return LiteralDecodeError::None;
    case '\'': Value = '\''; LexerLastChar = advance(); return LiteralDecodeError::None;
    case '"': Value = '"'; LexerLastChar = advance(); return LiteralDecodeError::None;
    case '?': Value = '?'; LexerLastChar = advance(); return LiteralDecodeError::None;
    case 'a': Value = 7; LexerLastChar = advance(); return LiteralDecodeError::None;
    case 'b': Value = 8; LexerLastChar = advance(); return LiteralDecodeError::None;
    case 'f': Value = 12; LexerLastChar = advance(); return LiteralDecodeError::None;
    case 'n': Value = 10; LexerLastChar = advance(); return LiteralDecodeError::None;
    case 'r': Value = 13; LexerLastChar = advance(); return LiteralDecodeError::None;
    case 't': Value = 9; LexerLastChar = advance(); return LiteralDecodeError::None;
    case 'v': Value = 11; LexerLastChar = advance(); return LiteralDecodeError::None;
    case 'x': {
      int High = HexDigitValue(advance());
      int Low = HexDigitValue(advance());
      if (High < 0 || Low < 0)
        return LiteralDecodeError::InvalidEscape;
      Value = static_cast<uint32_t>((High << 4) | Low);
      LexerLastChar = advance();
      return LiteralDecodeError::None;
    }
    case 'u':
    case 'U': {
      int DigitCount = LexerLastChar == 'u' ? 4 : 8;
      Value = 0;
      for (int Index = 0; Index < DigitCount; ++Index) {
        int Digit = HexDigitValue(advance());
        if (Digit < 0)
          return LiteralDecodeError::InvalidEscape;
        Value = (Value << 4) | static_cast<uint32_t>(Digit);
      }
      LexerLastChar = advance();
      return IsUnicodeScalarValue(Value)
                 ? LiteralDecodeError::None
                 : LiteralDecodeError::InvalidCodePoint;
    }
    default:
      if (LexerLastChar < '0' || LexerLastChar > '7')
        return LiteralDecodeError::InvalidEscape;
      Value = 0;
      for (int Index = 0; Index < 3; ++Index) {
        Value = (Value << 3) |
                static_cast<uint32_t>(LexerLastChar - '0');
        int Next = peek();
        if (Index == 2 || Next < '0' || Next > '7') {
          LexerLastChar = advance();
          break;
        }
        LexerLastChar = advance();
      }
      return LiteralDecodeError::None;
    }
  }

  unsigned Lead = static_cast<unsigned char>(LexerLastChar);
  if (Lead < 0x80) {
    Value = Lead;
    LexerLastChar = advance();
    return LiteralDecodeError::None;
  }

  int Length = 0;
  uint32_t Minimum = 0;
  if (Lead >= 0xC2 && Lead <= 0xDF) {
    Length = 2; Value = Lead & 0x1F; Minimum = 0x80;
  } else if (Lead >= 0xE0 && Lead <= 0xEF) {
    Length = 3; Value = Lead & 0x0F; Minimum = 0x800;
  } else if (Lead >= 0xF0 && Lead <= 0xF4) {
    Length = 4; Value = Lead & 0x07; Minimum = 0x10000;
  } else {
    return LiteralDecodeError::InvalidUtf8;
  }

  for (int Index = 1; Index < Length; ++Index) {
    int Next = advance();
    if (Next == EOF || (Next & 0xC0) != 0x80) {
      LexerLastChar = Next;
      return LiteralDecodeError::InvalidUtf8;
    }
    Value = (Value << 6) | static_cast<uint32_t>(Next & 0x3F);
  }
  LexerLastChar = advance();
  if (Value < Minimum)
    return LiteralDecodeError::InvalidUtf8;
  return IsUnicodeScalarValue(Value) ? LiteralDecodeError::None
                                     : LiteralDecodeError::InvalidCodePoint;
}

static void AppendUtf8(string &Output, uint32_t Value) {
  if (Value <= 0x7F) {
    Output.push_back(static_cast<char>(Value));
  } else if (Value <= 0x7FF) {
    Output.push_back(static_cast<char>(0xC0 | (Value >> 6)));
    Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
  } else if (Value <= 0xFFFF) {
    Output.push_back(static_cast<char>(0xE0 | (Value >> 12)));
    Output.push_back(static_cast<char>(0x80 | ((Value >> 6) & 0x3F)));
    Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
  } else {
    Output.push_back(static_cast<char>(0xF0 | (Value >> 18)));
    Output.push_back(static_cast<char>(0x80 | ((Value >> 12) & 0x3F)));
    Output.push_back(static_cast<char>(0x80 | ((Value >> 6) & 0x3F)));
    Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
  }
}

static int ReportLiteralDecodeError(LiteralDecodeError Error,
                                    const char *LiteralKind) {
  if (Error == LiteralDecodeError::InvalidEscape)
    fprintf(stderr, "Error (Line %d, Column %d): invalid %s escape\n",
            CurrentTokenLocation.Line, CurrentTokenLocation.Column, LiteralKind);
  else if (Error == LiteralDecodeError::InvalidCodePoint)
    fprintf(stderr,
            "Error (Line %d, Column %d): invalid Unicode code point in %s literal\n",
            CurrentTokenLocation.Line, CurrentTokenLocation.Column, LiteralKind);
  else
    fprintf(stderr, "Error (Line %d, Column %d): invalid UTF-8 in %s literal\n",
            CurrentTokenLocation.Line, CurrentTokenLocation.Column, LiteralKind);
  PrintErrorSourceContext(CurrentTokenLocation);
  return tok_error;
}

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
    // I see an indent: I remember the new level and open a block.
    if (CurrentIndentRead > CurrentIndentOnStack) {
      IndentStack.push_back(CurrentIndentRead);
      AtLineStart = false;
      return tok_indent;
    }
    // I see a dedent: I close blocks until I reach the new level.
    if (CurrentIndentRead < CurrentIndentOnStack) {
      while (IndentStack.size() > 1 /* protect the 0-indent */ &&
             CurrentIndentRead < IndentStack.back()) {
        IndentStack.pop_back();
        PendingTokens.push_back(tok_dedent);
      }
      if (CurrentIndentRead != IndentStack.back()) {
        LogErrorAtLocation("inconsistent indentation", CurrentTokenLocation);
        PrintErrorSourceContext(CurrentTokenLocation);
        PendingTokens.clear();
        AtLineStart = false;
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

  if (isdigit(LexerLastChar) ||
      (LexerLastChar == '.' && isdigit(peek()))) {
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

  if (LexerLastChar == '"') {
    StringLiteralValue.clear();
    LexerLastChar = advance(); // eat opening quote
    while (LexerLastChar != '"' && LexerLastChar != EOF &&
           LexerLastChar != '\n') {
      uint32_t CodePoint = 0;
      LiteralDecodeError Error = DecodeLiteralCodePoint(CodePoint);
      if (Error != LiteralDecodeError::None)
        return ReportLiteralDecodeError(Error, "string");
      AppendUtf8(StringLiteralValue, CodePoint);
    }

    if (LexerLastChar != '"') {
      fprintf(stderr,
              "Error (Line %d, Column %d): unterminated string literal\n",
              CurrentTokenLocation.Line, CurrentTokenLocation.Column);
      PrintErrorSourceContext(CurrentTokenLocation);
      return tok_error;
    }
    LexerLastChar = advance(); // eat closing quote
    return tok_string;
  }

  if (LexerLastChar == '\'') {
    LexerLastChar = advance(); // eat opening quote
    if (LexerLastChar == '\'') {
      fprintf(stderr, "Error (Line %d, Column %d): empty character literal\n",
              CurrentTokenLocation.Line, CurrentTokenLocation.Column);
      PrintErrorSourceContext(CurrentTokenLocation);
      return tok_error;
    }
    if (LexerLastChar == EOF || LexerLastChar == '\n') {
      fprintf(stderr,
              "Error (Line %d, Column %d): unterminated character literal\n",
              CurrentTokenLocation.Line, CurrentTokenLocation.Column);
      PrintErrorSourceContext(CurrentTokenLocation);
      return tok_error;
    }

    LiteralDecodeError Error =
        DecodeLiteralCodePoint(CharacterLiteralValue);
    if (Error != LiteralDecodeError::None)
      return ReportLiteralDecodeError(Error, "character");

    if (LexerLastChar != '\'') {
      const char *Message =
          (LexerLastChar == EOF || LexerLastChar == '\n')
              ? "unterminated character literal"
              : "character literal must contain one character";
      fprintf(stderr, "Error (Line %d, Column %d): %s\n", CurrentTokenLocation.Line,
              CurrentTokenLocation.Column, Message);
      PrintErrorSourceContext(CurrentTokenLocation);
      return tok_error;
    }
    LexerLastChar = advance(); // eat closing quote
    return tok_character;
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
    int Tok = (peek() == '=') ? (advance(), tok_eq) : tok_assign;
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

  // Single-character tokens (operators and punctuation) are all defined
  // as their own char value (e.g. tok_lparen = '('), so the raw character
  // returned here already IS the right token.
  return ThisChar;
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


static void LogErrorAtLocation(const string &ErrorMessage, SourceLocation Location) {
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Location.Line, Location.Column, ErrorMessage.c_str());
  PrintErrorSourceContext(Location);
}

static void LogInvalidNumberLiteralAtLocation(const string &Literal,
                                              SourceLocation Location) {
  LogErrorAtLocation(("invalid number literal '" + Literal + "'"), Location);
}

//===----------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------===//
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
  virtual bool isLValue() const { return false; }
  virtual Value *codegenAddress() { return nullptr; }
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

class StringExpressionNode : public ExpressionNode {
  string Text;

public:
  StringExpressionNode(string Text, const string &PointerTypeInfo)
      : Text(std::move(Text)) {
    setType(ValueType::Pointer, PointerTypeInfo);
  }
  Value *codegen() override;
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
  const string *getLValueName() const override { return &Name; }
  bool isLValue() const override { return true; }
  Value *codegenAddress() override;
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
  bool isLValue() const override { return true; }
  Value *codegenAddress() override;
  Value *codegen() override;
};

class MemberExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Base;
  size_t FieldIndex;

public:
  MemberExpressionNode(unique_ptr<ExpressionNode> Base, size_t FieldIndex,
                       ValueType Type, const string &StructName = "")
      : Base(std::move(Base)), FieldIndex(FieldIndex) {
    setType(Type, StructName);
  }
  bool isLValue() const override { return true; }
  Value *codegenAddress() override;
  Value *codegen() override;
};

class IndexExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Base;
  unique_ptr<ExpressionNode> Index;

public:
  IndexExpressionNode(unique_ptr<ExpressionNode> Base,
                      unique_ptr<ExpressionNode> Index, ValueType Type,
                      const string &StructName = "")
      : Base(std::move(Base)), Index(std::move(Index)) {
    setType(Type, StructName);
  }
  bool isLValue() const override { return true; }
  Value *codegenAddress() override;
  Value *codegen() override;
};

class AddrExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Operand;

public:
  AddrExpressionNode(unique_ptr<ExpressionNode> Operand,
                     const string &PointerTypeInfo)
      : Operand(std::move(Operand)) {
    setType(ValueType::Pointer, PointerTypeInfo);
  }
  Value *codegen() override;
};

class ArrayLiteralExpressionNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Elements;

public:
  ArrayLiteralExpressionNode(vector<unique_ptr<ExpressionNode>> Elements,
                             const string &ArrayTypeInfo)
      : Elements(std::move(Elements)) {
    setType(ValueType::Array, ArrayTypeInfo);
  }
  Value *codegen() override;
};

/// AssignmentExpressionNode - Store a value through any assignable expression
/// and produce the value that was stored.
class AssignmentExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Left;
  unique_ptr<ExpressionNode> Right;

public:
  AssignmentExpressionNode(unique_ptr<ExpressionNode> Left,
                           unique_ptr<ExpressionNode> Right, ValueType Type,
                           const string &StructName = "")
      : Left(std::move(Left)), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// ReturnStatementNode - Statement-like expression for return.
/// Emits a function return and produces the returned value.
class ReturnStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Expression;

public:
  ReturnStatementNode(unique_ptr<ExpressionNode> Expression = nullptr) : Expression(std::move(Expression)) {
    setType(ValueType::None);
  }
  bool isReturnExpr() const override { return true; }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// BlockStatementNode - A sequence of statements evaluated in order.
/// The block's value is the value of the last statement executed.
class BlockStatementNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Statements;

public:
  BlockStatementNode(vector<unique_ptr<ExpressionNode>> Statements) : Statements(std::move(Statements)) {
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
                ValueType Type, const string &StructName = "")
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {
    setType(Type, StructName);
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
///   for <var> = <start>, <cond>, <update>: <body>
/// The loop variable is in scope for <cond>, <update>, and <body> (through
/// NamedValues). The expression always produces 0.0 — the loop is used for side
/// effects.
class ForStatementNode : public ExpressionNode {
  string VariableName;
  bool DeclaresVariable;
  ValueType VarType;
  unique_ptr<ExpressionNode> Start, Condition, Update, Body;

public:
  ForStatementNode(const string &VariableName, bool DeclaresVariable, ValueType VarType,
             unique_ptr<ExpressionNode> Start, unique_ptr<ExpressionNode> Condition,
             unique_ptr<ExpressionNode> Update, unique_ptr<ExpressionNode> Body)
      : VariableName(VariableName), DeclaresVariable(DeclaresVariable), VarType(VarType),
        Start(std::move(Start)), Condition(std::move(Condition)), Update(std::move(Update)),
        Body(std::move(Body)) {
    setType(ValueType::None);
  }
  ValueType getVarType() const { return VarType; }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// WhileStatementNode - Statement class for while and do/while loops.
class WhileStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Condition, Body;
  bool IsDoWhile;

public:
  WhileStatementNode(unique_ptr<ExpressionNode> Condition,
                     unique_ptr<ExpressionNode> Body, bool IsDoWhile)
      : Condition(std::move(Condition)), Body(std::move(Body)), IsDoWhile(IsDoWhile) {
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
  string TargetTypeInfo;
  unique_ptr<ExpressionNode> Expr;

public:
  CastExpressionNode(ValueType TargetType, unique_ptr<ExpressionNode> Expr,
                     const string &TargetTypeInfo = "")
      : TargetType(TargetType), TargetTypeInfo(TargetTypeInfo),
        Expr(std::move(Expr)) {
    setType(TargetType, TargetTypeInfo);
  }
  Value *codegen() override;
};

class SizeofExpressionNode : public ExpressionNode {
  ValueType TargetType;
  string TargetTypeInfo;

public:
  SizeofExpressionNode(ValueType TargetType,
                       const string &TargetTypeInfo = "")
      : TargetType(TargetType), TargetTypeInfo(TargetTypeInfo) {
    setType(ValueType::Int64);
  }
  Value *codegen() override;
};

/// IfStatementNode - Statement form of if/else.
/// Produces 0.0 and does not return a value.
class IfStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Condition, Then, Else;

public:
  IfStatementNode(unique_ptr<ExpressionNode> Condition, unique_ptr<ExpressionNode> Then,
            unique_ptr<ExpressionNode> Else)
      : Condition(std::move(Condition)), Then(std::move(Then)), Else(std::move(Else)) {
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

/// VariableStatementNode - Statement form of mutable local variable bindings.
///   var a = <init>, b = <init>
/// Each binding allocates stack storage in the current function's entry block
/// and stores its initializer. Bindings persist for the rest of the function.
class VariableStatementNode : public ExpressionNode {
  vector<VarBinding> VariableBindings;

public:
  VariableStatementNode(vector<VarBinding> VariableBindings) : VariableBindings(std::move(VariableBindings)) {
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
  bool IsVariadic;
  SourceLocation Loc;

public:
  FunctionSignatureNode(const string &Name,
                        vector<pair<string, ValueType>> Parameters,
                        SourceLocation Loc,
                        ValueType ReturnType = ValueType::Float64,
                        vector<string> ParameterStructNames = {},
                        string ReturnStructName = "",
                        bool IsVariadic = false)
      : Name(Name), Parameters(std::move(Parameters)), ReturnType(ReturnType),
        ReturnStructName(std::move(ReturnStructName)), IsVariadic(IsVariadic),
        Loc(Loc) {
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
  bool isVariadic() const { return IsVariadic; }

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
        ReturnStructName, IsVariadic);
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
static std::map<string, std::unique_ptr<FunctionSignatureNode>> FunctionSignatures;

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
static map<string, pair<ValueType, string>> TypeAliases;

// Parse-time variable tracking for assignments and types.
// Scopes are stacked: function scope plus nested block scopes.
// for-loop variables are scoped to the loop body only.
static vector<std::map<string, ValueType>> LocalVariableScopes;
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
  LocalVariableScopes.clear();
  VarStructScopes.clear();
  LocalVariableScopes.push_back({}); // I add the function's top-level local scope.
  VarStructScopes.push_back({});
  auto &FunctionScope = LocalVariableScopes.back();
  auto &FunctionStructScope = VarStructScopes.back();

  for (size_t Index = 0; Index < Parameters.size(); ++Index) {
    const auto &Parameter = Parameters[Index];
    FunctionScope[Parameter.first] = Parameter.second;
    if (Index < ParameterStructNames.size() &&
        !ParameterStructNames[Index].empty())
      FunctionStructScope[Parameter.first] = ParameterStructNames[Index];
  }
}

static void EndFunctionScope() {
  LocalVariableScopes.clear();
  VarStructScopes.clear();
}

static void BeginBlockScope() {
  LocalVariableScopes.push_back({});
  VarStructScopes.push_back({});
}

// Pop a block scope if one is active.
// Size > 1 means a nested block inside a function; never pop the function scope
// here. Size == 1 is only popped for top-level blocks (function scope is popped
// in EndFunctionScope).
static void EndBlockScope() {
  if (LocalVariableScopes.size() > 1)
    LocalVariableScopes.pop_back(), VarStructScopes.pop_back();
  else if (ParsingTopLevel && LocalVariableScopes.size() == 1) {
    LocalVariableScopes.pop_back();
    VarStructScopes.pop_back();
  }
}

// Check only the innermost scope (used for redeclaration checks).

// Ensure a function scope exists, then add a new scope for the loop variable.
static void BeginLoopScope(const string &Name, ValueType Type) {
  LocalVariableScopes.push_back({});
  VarStructScopes.push_back({});
  auto &LoopScope = LocalVariableScopes.back();
  LoopScope[Name] = Type;
}

// Size == 1 is only popped for top-level blocks (function scope is popped in
// EndFunctionScope).
static void EndLoopScope() {
  if (LocalVariableScopes.size() > 1)
    LocalVariableScopes.pop_back(), VarStructScopes.pop_back();
  if (ParsingTopLevel && LocalVariableScopes.size() == 1) {
    LocalVariableScopes.pop_back();
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

static void DeclareVariable(const string &Name, ValueType Type,
                       const string &StructName = "") {
  // Only declare into an active local scope; at top level LocalVariableScopes is empty.
  if (LocalVariableScopes.empty())
    return;

  auto &CurrentLocalScope = LocalVariableScopes.back();
  CurrentLocalScope[Name] = Type;

  auto &CurrentStructScope = VarStructScopes.back();
  if (!StructName.empty())
    CurrentStructScope[Name] = StructName;
}

// Check only the innermost scope (used for redeclaration checks).
static bool IsVariableDeclaredInCurrentScope(const string &Name) {
  if (LocalVariableScopes.empty())
    return false;

  const auto &CurrentLocalScope = LocalVariableScopes.back();
  return CurrentLocalScope.count(Name) > 0;
}

// IsVariableDeclared - Check all local scopes from innermost to outermost, then
// fall back to globals. Used to validate assignments and references.
static bool IsVariableDeclared(const string &Name) {
  for (auto ScopeIterator = LocalVariableScopes.rbegin();
       ScopeIterator != LocalVariableScopes.rend(); ++ScopeIterator) {
    if (ScopeIterator->count(Name))
      return true;
  }
  return GlobalVarTypes.count(Name) > 0;
}

// LookupVarType - Return the type from the nearest enclosing local scope,
// or from globals if not found; otherwise ValueType::Error.
static ValueType LookupVarType(const string &Name) {
  for (auto ScopeIterator = LocalVariableScopes.rbegin();
       ScopeIterator != LocalVariableScopes.rend(); ++ScopeIterator) {
    auto Found = ScopeIterator->find(Name);
    if (Found != ScopeIterator->end())
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

/// PrintEvaluationResult - Print a typed REPL result to stdout.
void PrintEvaluationResult(double Result) { fprintf(stdout, "%f\n", Result); }
void PrintEvaluationResult(long long Result) {
  fprintf(stdout, "%lld\n", Result);
}
void PrintEvaluationResult(unsigned long long Result) {
  fprintf(stdout, "%llu\n", Result);
}
void PrintEvaluationResult(bool Result) {
  fprintf(stdout, "%s\n", Result ? "True" : "False");
}

/// LogErrorExpression* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogErrorExpression("message");
unique_ptr<ExpressionNode> LogErrorExpression(const string &ErrorMessage) {
  HadError = true;
  SourceLocation Anchor = GetCaretAnchorLocation(CurrentTokenLocation, CurrentToken);
  LogErrorAtLocation(ErrorMessage, Anchor);
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
static unique_ptr<ExpressionNode>
ParseNameExpressionWithName(const string &ParsedName);
static unique_ptr<ExpressionNode> ParseAddrExpression();
static unique_ptr<ExpressionNode> ParseSizeofExpression();
static unique_ptr<ExpressionNode> ParseVariableStatement();
static unique_ptr<ExpressionNode> ParseStatement();
static unique_ptr<ExpressionNode> ParseSimpleStatement();
static unique_ptr<ExpressionNode> ParseBlock();


// Counter to give each anonymous top-level expression a unique name.
static unsigned TopLevelExprCounter = 0;
// Whether the last top-level form should be printed in the REPL.
static bool LastTopLevelShouldPrint = true;

static unique_ptr<ExpressionNode> ParseSuite();
static ValueType ParseTypeToken(string *StructName = nullptr);
static string EncodePointerType(ValueType PointeeType,
                                const string &PointeeStructName = "");
static bool DecodePointerType(const string &Encoded, ValueType &PointeeType,
                              string &PointeeStructName);
static string EncodeArrayType(ValueType ElementType,
                              const string &ElementStructName,
                              uint64_t ElementCount);
static bool DecodeArrayType(const string &Encoded, ValueType &ElementType,
                            string &ElementStructName,
                            uint64_t &ElementCount);
static bool ArrayDecaysToPointerType(const string &ArrayTypeInfo,
                                     const string &PointerTypeInfo);
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
static string ExpectedLiteralTypeInfo;

struct ExpectedLiteralTypeGuard {
  ValueType Saved;
  string SavedTypeInfo;
  ExpectedLiteralTypeGuard(ValueType Type, const string &TypeInfo = "")
      : Saved(ExpectedLiteralType), SavedTypeInfo(ExpectedLiteralTypeInfo) {
    ExpectedLiteralType = Type;
    ExpectedLiteralTypeInfo = TypeInfo;
  }
  ~ExpectedLiteralTypeGuard() {
    ExpectedLiteralType = Saved;
    ExpectedLiteralTypeInfo = SavedTypeInfo;
  }
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
    return Result;
  }
}

static unique_ptr<ExpressionNode> ParseCharacterExpression() {
  ValueType Type = IsIntType(ExpectedLiteralType)
                       ? ExpectedLiteralType
                       : ValueType::Int32;
  unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
  uint64_t Maximum = IsUnsignedIntType(Type)
                         ? APInt::getMaxValue(Bits).getZExtValue()
                         : APInt::getSignedMaxValue(Bits).getZExtValue();
  if (CharacterLiteralValue > Maximum)
    return LogErrorExpression("Character literal out of range for type");
  auto Result = make_unique<NumberExpressionNode>(
      APInt(Bits, CharacterLiteralValue), Type);
  getNextToken(); // eat character literal
  return Result;
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
  ValueType BaseType = ValueType::Error;
  string BaseTypeInfo;
  switch (CurrentToken) {
  case tok_int:
    getNextToken();
    BaseType = ValueType::Int;
    break;
  case tok_int8:
    getNextToken();
    BaseType = ValueType::Int8;
    break;
  case tok_int16:
    getNextToken();
    BaseType = ValueType::Int16;
    break;
  case tok_int32:
    getNextToken();
    BaseType = ValueType::Int32;
    break;
  case tok_int64:
    getNextToken();
    BaseType = ValueType::Int64;
    break;
  case tok_uint8:
    getNextToken();
    BaseType = ValueType::UInt8;
    break;
  case tok_uint16:
    getNextToken();
    BaseType = ValueType::UInt16;
    break;
  case tok_uint32:
    getNextToken();
    BaseType = ValueType::UInt32;
    break;
  case tok_uint64:
    getNextToken();
    BaseType = ValueType::UInt64;
    break;
  case tok_float:
    getNextToken();
    BaseType = ValueType::Float;
    break;
  case tok_float32:
    getNextToken();
    BaseType = ValueType::Float32;
    break;
  case tok_float64:
    getNextToken();
    BaseType = ValueType::Float64;
    break;
  case tok_bool:
    getNextToken();
    BaseType = ValueType::Bool;
    break;
  case tok_none:
    getNextToken();
    BaseType = ValueType::None;
    break;
  case tok_ptr: {
    getNextToken(); // eat 'ptr'
    if (CurrentToken != tok_lbracket) {
      LogErrorExpression("Expected '[' after ptr");
      return ValueType::Error;
    }
    getNextToken(); // eat '['
    string PointeeStructName;
    ValueType PointeeType = ParseTypeToken(&PointeeStructName);
    if (PointeeType == ValueType::Error)
      return ValueType::Error;
    if (PointeeType == ValueType::None) {
      LogErrorExpression("Pointers to None are not allowed");
      return ValueType::Error;
    }
    if (PointeeType == ValueType::Pointer) {
      LogErrorExpression("Nested pointer types are not supported");
      return ValueType::Error;
    }
    if (PointeeType == ValueType::Array) {
      LogErrorExpression("Pointers to array types are not supported");
      return ValueType::Error;
    }
    if (CurrentToken != tok_rbracket) {
      LogErrorExpression("Expected ']' after pointer type");
      return ValueType::Error;
    }
    getNextToken(); // eat ']'
    BaseType = ValueType::Pointer;
    BaseTypeInfo = EncodePointerType(PointeeType, PointeeStructName);
    break;
  }
  case tok_name: {
    auto Alias = TypeAliases.find(Name);
    if (Alias != TypeAliases.end()) {
      BaseType = Alias->second.first;
      BaseTypeInfo = Alias->second.second;
      getNextToken();
      break;
    }
    auto Found = StructTypes.find(Name);
    if (Found == StructTypes.end()) {
      LogErrorExpression(("Unknown type '" + Name + "'"));
      return ValueType::Error;
    }
    BaseTypeInfo = Name;
    getNextToken();
    BaseType = ValueType::Struct;
    break;
  }
  default:
    LogErrorExpression("Expected a type");
    return ValueType::Error;
  }

  if (CurrentToken == tok_lbracket) {
    if (BaseType == ValueType::None) {
      LogErrorExpression("Arrays of None are not allowed");
      return ValueType::Error;
    }
    getNextToken(); // eat '['
    if (CurrentToken != tok_number || NumberIsFloat) {
      LogErrorExpression("Array size must be an integer literal");
      return ValueType::Error;
    }
    uint64_t ElementCount = std::strtoull(NumberLiteral.c_str(), nullptr, 10);
    if (ElementCount == 0) {
      LogErrorExpression("Array size must be greater than zero");
      return ValueType::Error;
    }
    getNextToken(); // eat array size
    if (CurrentToken != tok_rbracket) {
      LogErrorExpression("Expected ']' after array size");
      return ValueType::Error;
    }
    getNextToken(); // eat ']'
    if (CurrentToken == tok_lbracket) {
      LogErrorExpression("Nested arrays are not supported");
      return ValueType::Error;
    }
    BaseTypeInfo = EncodeArrayType(BaseType, BaseTypeInfo, ElementCount);
    BaseType = ValueType::Array;
  }

  if (StructName)
    *StructName = BaseTypeInfo;
  return BaseType;
}

static bool ParseTypeAliasDefinition() {
  getNextToken(); // eat 'type'
  if (CurrentToken != tok_name) {
    LogErrorExpression("Expected name after 'type'");
    return false;
  }
  string AliasName = Name;
  if (TypeAliases.count(AliasName)) {
    LogErrorExpression(("Type alias '" + AliasName + "' is already defined")
                           .c_str());
    return false;
  }
  if (StructTypes.count(AliasName)) {
    LogErrorExpression(("Type '" + AliasName +
                        "' is already defined as an aggregate type")
                           .c_str());
    return false;
  }
  getNextToken(); // eat alias name
  if (CurrentToken != tok_assign) {
    LogErrorExpression("Expected '=' in type alias");
    return false;
  }
  getNextToken(); // eat '='
  string AliasedTypeInfo;
  ValueType AliasedType = ParseTypeToken(&AliasedTypeInfo);
  if (AliasedType == ValueType::Error)
    return false;
  TypeAliases[AliasName] = {AliasedType, AliasedTypeInfo};
  return true;
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
  if (TypeAliases.count(StructName)) {
    LogErrorExpression(("Type '" + StructName +
                        "' is already defined as a type alias")
                           .c_str());
    return false;
  }
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
  string TargetTypeInfo;
  ValueType Type = ParseTypeToken(&TargetTypeInfo);
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
  if (Type == ValueType::Pointer && Expr->getType() != ValueType::Pointer)
    return LogErrorExpression("Pointer casts require a pointer operand");
  return make_unique<CastExpressionNode>(Type, std::move(Expr),
                                          TargetTypeInfo);
}

static unique_ptr<ExpressionNode> ParseSizeofExpression() {
  getNextToken(); // eat 'sizeof'
  if (CurrentToken != tok_lparen)
    return LogErrorExpression("Expected '(' after sizeof");
  getNextToken(); // eat '('
  string TargetTypeInfo;
  ValueType TargetType = ParseTypeToken(&TargetTypeInfo);
  if (TargetType == ValueType::Error)
    return nullptr;
  if (TargetType == ValueType::None)
    return LogErrorExpression("Cannot take sizeof(None)");
  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')' after sizeof type");
  getNextToken(); // eat ')'
  return make_unique<SizeofExpressionNode>(TargetType, TargetTypeInfo);
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
      if (CurrentToken == tok_assign)
        return LogErrorExpression("Assignment to undeclared variable");
      return LogErrorExpression("Unknown variable name: '" + ParsedName + "'");
    }
    string StructName = LookupVarStructName(ParsedName);
    unique_ptr<ExpressionNode> Result =
        make_unique<NameExpressionNode>(ParsedName, Type, StructName);

    while (CurrentToken == tok_dot || CurrentToken == tok_lbracket) {
      if (CurrentToken == tok_dot) {
        if (Result->getType() != ValueType::Struct ||
            Result->getStructName().empty())
          return LogErrorExpression("Field access requires a struct value");
        string BaseStructName = Result->getStructName();
        getNextToken(); // eat '.'
        if (CurrentToken != tok_name)
          return LogErrorExpression("Expected field name after '.'");
        auto Struct = StructTypes.find(BaseStructName);
        if (Struct == StructTypes.end())
          return LogErrorExpression("Unknown struct type in field access");
        auto Field = Struct->second.FieldIndices.find(Name);
        if (Field == Struct->second.FieldIndices.end())
          return LogErrorExpression(("Unknown field '" + Name + "'"));
        const auto &FieldInfo = Struct->second.Fields[Field->second];
        Result = make_unique<MemberExpressionNode>(
            std::move(Result), Field->second, FieldInfo.Type,
            FieldInfo.StructName);
        getNextToken(); // eat field name
        continue;
      }

      if (Result->getType() != ValueType::Pointer &&
          Result->getType() != ValueType::Array)
        return LogErrorExpression("Indexing requires a pointer or array value");
      ValueType ElementType = ValueType::Error;
      string ElementStructName;
      uint64_t ElementCount = 0;
      bool Decoded = Result->getType() == ValueType::Pointer
                         ? DecodePointerType(Result->getStructName(),
                                             ElementType, ElementStructName)
                         : DecodeArrayType(Result->getStructName(), ElementType,
                                           ElementStructName, ElementCount);
      if (!Decoded)
        return LogErrorExpression("Invalid indexed type metadata");
      getNextToken(); // eat '['
      auto Index = ParseExpression();
      if (!Index)
        return nullptr;
      if (!IsIntType(Index->getType()))
        return LogErrorExpression("Index must be an integer");
      if (CurrentToken != tok_rbracket)
        return LogErrorExpression("Expected ']' after index expression");
      getNextToken(); // eat ']'
      Result = make_unique<IndexExpressionNode>(
          std::move(Result), std::move(Index), ElementType,
          ElementStructName);
    }
    return Result;
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
      string ExpectedTypeInfo;
      if (Signature && ArgIndex < Signature->getNumParameters()) {
        Expected = Signature->getParameterType(ArgIndex);
        ExpectedTypeInfo = Signature->getParameterStructName(ArgIndex);
      }
      {
        ExpectedLiteralTypeGuard Guard(Expected, ExpectedTypeInfo);
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
  if ((!Signature->isVariadic() &&
       Signature->getNumParameters() != Arguments.size()) ||
      (Signature->isVariadic() &&
       Arguments.size() < Signature->getNumParameters()))
    return LogErrorExpression(
        "Incorrect number of arguments in call to '" + ParsedName +
        "': expected " + to_string(Signature->getNumParameters()) +
        (Signature->isVariadic() ? " or more, got " : ", got ") +
        to_string(Arguments.size()));

  for (size_t i = 0; i < Signature->getNumParameters(); ++i) {
    ValueType ArgType = Arguments[i]->getType();
    ValueType ParamType = Signature->getParameterType(i);
    if (ParamType == ValueType::Pointer && ArgType == ValueType::Array) {
      if (!ArrayDecaysToPointerType(
              Arguments[i]->getStructName(),
              Signature->getParameterStructName(i)))
        return LogErrorExpression("Argument type mismatch");
      continue;
    }
    if (!IsAssignable(ParamType, ArgType)) {
      return LogErrorExpression(("argument " + std::to_string(i + 1) + " expects " +
                       TypeName(ParamType))
                          .c_str());
    }
    if ((ParamType == ValueType::Struct || ParamType == ValueType::Pointer) &&
        Signature->getParameterStructName(i) != Arguments[i]->getStructName())
      return LogErrorExpression("Argument type mismatch");
  }

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments),
                                  Signature->getReturnType(),
                                  Signature->getReturnStructName());
}

static unique_ptr<ExpressionNode> ParseAddrExpression() {
  getNextToken(); // eat 'addr'
  if (CurrentToken != tok_lparen)
    return LogErrorExpression("Expected '(' after addr");
  getNextToken(); // eat '('
  if (CurrentToken != tok_name)
    return LogErrorExpression("addr expects an lvalue");

  string ParsedName = Name;
  getNextToken(); // eat name
  auto Operand = ParseNameExpressionWithName(ParsedName);
  if (!Operand || !Operand->isLValue())
    return LogErrorExpression("addr expects an lvalue");
  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')' after addr operand");
  getNextToken(); // eat ')'

  string PointerTypeInfo =
      EncodePointerType(Operand->getType(), Operand->getStructName());
  return make_unique<AddrExpressionNode>(std::move(Operand), PointerTypeInfo);
}

static unique_ptr<ExpressionNode> ParseArrayLiteralExpression() {
  if (ExpectedLiteralType != ValueType::Array)
    return LogErrorExpression("Array literal requires an expected array type");

  ValueType ElementType = ValueType::Error;
  string ElementStructName;
  uint64_t ExpectedCount = 0;
  if (!DecodeArrayType(ExpectedLiteralTypeInfo, ElementType,
                       ElementStructName, ExpectedCount))
    return LogErrorExpression("Invalid expected array type");

  getNextToken(); // eat '['
  vector<unique_ptr<ExpressionNode>> Elements;
  if (CurrentToken != tok_rbracket) {
    while (true) {
      ExpectedLiteralTypeGuard Guard(ElementType, ElementStructName);
      auto Element = ParseExpression();
      if (!Element)
        return nullptr;
      if (!IsAssignable(ElementType, Element->getType()) ||
          ((ElementType == ValueType::Struct ||
            ElementType == ValueType::Pointer) &&
           ElementStructName != Element->getStructName()))
        return LogErrorExpression("Array literal element type mismatch");
      Elements.push_back(std::move(Element));
      if (CurrentToken != tok_comma)
        break;
      getNextToken(); // eat ','
    }
  }
  if (CurrentToken != tok_rbracket)
    return LogErrorExpression("Expected ']' after array literal");
  getNextToken(); // eat ']'
  if (Elements.size() != ExpectedCount)
    return LogErrorExpression("Array literal element count mismatch");
  return make_unique<ArrayLiteralExpressionNode>(
      std::move(Elements), ExpectedLiteralTypeInfo);
}

static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;

  getNextToken(); // eat name.

  return ParseNameExpressionWithName(ParsedName);
}

// ParseForParts - Parse the "= start, cond, step : suite" tail of a for-loop.
// Also validates the parts against VarType (start/step assignable, cond bool).
// Returns true on success and fills Start/Condition/Update/Body.
static bool ParseForParts(ValueType VarType, unique_ptr<ExpressionNode> &Start,
                          unique_ptr<ExpressionNode> &Condition, unique_ptr<ExpressionNode> &Update,
                          unique_ptr<ExpressionNode> &Body) {
  if (CurrentToken != tok_assign) {
    LogErrorExpression("Expected '=' after 'for' variable");
    return false;
  }
  getNextToken(); // eat '='

  Start = ParseExpression();
  if (!Start)
    return false;
  if (!IsAssignable(VarType, Start->getType())) {
    LogErrorExpression("For loop start must match loop variable type");
    return false;
  }
  if (!IsNumericType(VarType)) {
    LogErrorExpression("For loop variable must be numeric");
    return false;
  }

  if (CurrentToken != tok_comma) {
    LogErrorExpression("Expected ',' after 'for' start value");
    return false;
  }
  getNextToken(); // eat ','

  Condition = ParseExpression();
  if (!Condition)
    return false;
  if (Condition->getType() != ValueType::Bool) {
    LogErrorExpression("For loop condition must be bool");
    return false;
  }

  if (CurrentToken != tok_comma) {
    LogErrorExpression("Expected ',' after 'for' condition");
    return false;
  }
  getNextToken(); // eat ','

  Update = ParseExpression();
  if (!Update)
    return false;

  if (CurrentToken != tok_colon) {
    LogErrorExpression("Expected ':' after 'for' step");
    return false;
  }
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
/// The final expression performs the complete loop update; its value is discarded.
///
/// "for var" introduces a new loop variable scoped to the loop statement.
/// A plain "for i = ..." reuses an existing variable (error if undeclared).
static unique_ptr<ExpressionNode> ParseForStatement() {
  getNextToken(); // eat 'for'

  bool DeclaresVariable = false;
  if (CurrentToken == tok_var) {
    DeclaresVariable = true;
    getNextToken(); // optional 'var'
  }

  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected variable name after 'for'");
  string VariableName = Name;
  getNextToken(); // eat name

  ValueType VarType = ValueType::Error;
  if (DeclaresVariable) {
    if (CurrentToken != tok_colon)
      return LogErrorExpression(
          "For loop variable requires a type annotation (e.g., ': int')");
    getNextToken(); // eat ':'
    VarType = ParseTypeToken();
    if (VarType == ValueType::Error)
      return nullptr;
    if (VarType == ValueType::None)
      return LogErrorExpression("For loop variable cannot have None type");
    if (IsVariableDeclaredInCurrentScope(VariableName))
      return LogErrorExpression(
          ("Variable '" + VariableName + "' already declared in this scope")
              .c_str());
  } else {
    if (CurrentToken == tok_colon)
      return LogErrorExpression("For loop variable requires 'var' to declare a type");
    VarType = LookupVarType(VariableName);
    if (VarType == ValueType::Error)
      return LogErrorExpression("Assignment to undeclared variable");
  }

  unique_ptr<ExpressionNode> Start, Condition, Update, Body;

  if (DeclaresVariable) {
    LoopScopeGuard LoopScope(VariableName, VarType);
    if (!ParseForParts(VarType, Start, Condition, Update, Body))
      return nullptr;
  } else {
    if (!ParseForParts(VarType, Start, Condition, Update, Body))
      return nullptr;
  }
  return make_unique<ForStatementNode>(VariableName, DeclaresVariable, VarType, std::move(Start),
                                 std::move(Condition), std::move(Update),
                                 std::move(Body));
}

/// while-statement
///   = "while" expression ":" suite ;
static unique_ptr<ExpressionNode> ParseWhileStatement() {
  getNextToken(); // eat 'while'
  auto Condition = ParseExpression();
  if (!Condition)
    return nullptr;
  if (Condition->getType() != ValueType::Bool)
    return LogErrorExpression("While condition must be bool");
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after while condition");
  getNextToken(); // eat ':'

  ParseLoopGuard Loop;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  return make_unique<WhileStatementNode>(std::move(Condition), std::move(Body),
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

  auto Condition = ParseExpression();
  if (!Condition)
    return nullptr;
  if (Condition->getType() != ValueType::Bool)
    return LogErrorExpression("Do/while condition must be bool");
  return make_unique<WhileStatementNode>(std::move(Condition), std::move(Body), true);
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
static unique_ptr<ExpressionNode> ParseVariableStatement() {
  getNextToken(); // eat 'var'

  vector<VarBinding> VariableBindings;
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
      if (IsVariableDeclaredInCurrentScope(ParsedName))
        return LogErrorExpression(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    }

    unique_ptr<ExpressionNode> Init;
    // [ "=" expression ]
    if (CurrentToken == tok_assign) {
      getNextToken(); // eat '='
      ExpectedLiteralTypeGuard Guard(DeclType, DeclStructName);
      Init = ParseExpression();
      if (!Init)
        return nullptr;
      bool DecaysToPointer =
          DeclType == ValueType::Pointer &&
          Init->getType() == ValueType::Array &&
          ArrayDecaysToPointerType(Init->getStructName(), DeclStructName);
      if (!IsAssignable(DeclType, Init->getType()) && !DecaysToPointer)
        return LogErrorExpression("Type mismatch in variable initialization");
      if ((DeclType == ValueType::Struct || DeclType == ValueType::Pointer ||
           DeclType == ValueType::Array) &&
          !DecaysToPointer && DeclStructName != Init->getStructName())
        return LogErrorExpression("Type mismatch in variable initialization");
    } else {
      if (DeclType != ValueType::Struct && DeclType != ValueType::Pointer &&
          DeclType != ValueType::Array) {
        // No '=': use the declared type's zero value.
        Init = MakeZeroLiteral(DeclType);
        if (!Init)
          return nullptr;
      }
    }

    VariableBindings.push_back(
        {ParsedName, DeclType, DeclStructName, std::move(Init)});
    if (IsGlobalDecl) {
      GlobalVarTypes[ParsedName] = DeclType;
      if (!DeclStructName.empty())
        GlobalVarStructNames[ParsedName] = DeclStructName;
      GlobalVarDecls.insert(ParsedName);
    } else {
      DeclareVariable(ParsedName, DeclType, DeclStructName);
    }

    if (CurrentToken != tok_comma)
      break;
    getNextToken(); // eat ','
  }

  return make_unique<VariableStatementNode>(std::move(VariableBindings));
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
      return LogErrorExpression("Expected ':' after 'else'");
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
static ValueType GetBinaryResultType(int Operator, ValueType L,
                                     const string &LTypeInfo, ValueType R,
                                     const string &RTypeInfo) {
  if (IsArithmeticOp(Operator)) {
    if (Operator == tok_plus &&
        ((L == ValueType::Pointer && IsIntType(R)) ||
         (R == ValueType::Pointer && IsIntType(L))))
      return ValueType::Pointer;
    if (Operator == tok_minus && L == ValueType::Pointer && IsIntType(R))
      return ValueType::Pointer;
    if (Operator == tok_minus && L == ValueType::Pointer &&
        R == ValueType::Pointer && LTypeInfo == RTypeInfo)
      return ValueType::Int64;
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
    if (L == ValueType::Pointer && R == ValueType::Pointer)
      return LTypeInfo == RTypeInfo ? ValueType::Bool : ValueType::Error;
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
  case tok_name:
    return ParseNameExpression();
  case tok_number:
    return ParseNumberExpression();
  case tok_string: {
    string Text = StringLiteralValue;
    getNextToken();
    return make_unique<StringExpressionNode>(
        std::move(Text), EncodePointerType(ValueType::Int8));
  }
  case tok_character:
    return ParseCharacterExpression();
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
  case tok_ptr:
    return ParseCastExpression();
  case tok_sizeof:
    return ParseSizeofExpression();
  case tok_addr:
    return ParseAddrExpression();
  case tok_lbracket:
    return ParseArrayLiteralExpression();
  case tok_lparen:
    return ParseParenthesizedExpression();
  default:
    return LogErrorExpression(
        ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
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
      GetBinaryResultType(Operator, Left->getType(), Left->getStructName(),
                          Right->getType(), Right->getStructName());
  if (ResultType == ValueType::Error)
    return LogErrorExpression("Type mismatch in binary operator");
  string ResultTypeInfo;
  if (ResultType == ValueType::Pointer)
    ResultTypeInfo = Left->getType() == ValueType::Pointer
                         ? Left->getStructName()
                         : Right->getStructName();
  return make_unique<BinaryExpressionNode>(
      Operator, std::move(Left), std::move(Right), ResultType,
      ResultTypeInfo);
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

/// assignment
///   = logical-or [ "=" assignment ] ;
static unique_ptr<ExpressionNode> ParseAssignment() {
  auto Left = ParseLogicalOr();
  if (!Left)
    return nullptr;
  if (CurrentToken != tok_assign)
    return Left;
  if (!Left->isLValue())
    return LogErrorExpression("Assignment target must be assignable");

  ValueType LeftType = Left->getType();
  string LeftTypeInfo = Left->getStructName();
  getNextToken(); // eat '='

  ExpectedLiteralTypeGuard Guard(LeftType, LeftTypeInfo);
  auto Right = ParseAssignment();
  if (!Right)
    return nullptr;

  if (LeftType == ValueType::Array)
    return LogErrorExpression("Type mismatch in assignment");
  if (LeftType == ValueType::Pointer &&
      Right->getType() == ValueType::Array) {
    if (!ArrayDecaysToPointerType(Right->getStructName(), LeftTypeInfo))
      return LogErrorExpression("Type mismatch in assignment");
  } else {
    if (!IsAssignable(LeftType, Right->getType()))
      return LogErrorExpression("Type mismatch in assignment");
    if ((LeftType == ValueType::Struct || LeftType == ValueType::Pointer) &&
        LeftTypeInfo != Right->getStructName())
      return LogErrorExpression("Type mismatch in assignment");
  }

  return make_unique<AssignmentExpressionNode>(
      std::move(Left), std::move(Right), LeftType, LeftTypeInfo);
}

/// expression
///   = assignment ;
static unique_ptr<ExpressionNode> ParseExpression() {
  return ParseAssignment();
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

  ExpectedLiteralTypeGuard Guard(CurrentFunctionReturnType,
                                  CurrentFunctionReturnStructName);
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
  if ((CurrentFunctionReturnType == ValueType::Struct ||
       CurrentFunctionReturnType == ValueType::Pointer) &&
      CurrentFunctionReturnStructName != Expr->getStructName())
    return LogErrorExpression("Return type mismatch");
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

/// simple-statement
///   = return-statement | break-statement | continue-statement
///   | variable-statement | expression ;
static unique_ptr<ExpressionNode> ParseSimpleStatement() {
  if (CurrentToken == tok_return)
    return ParseReturnStatement();
  if (CurrentToken == tok_break)
    return ParseBreakStatement();
  if (CurrentToken == tok_continue)
    return ParseContinueStatement();
  if (CurrentToken == tok_var)
    return ParseVariableStatement();
  return ParseExpression();
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
///   = simple-statement | end-of-lines block ;
static unique_ptr<ExpressionNode> ParseSuite() {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken != tok_indent)
      return LogErrorExpression("Expected an indented block");
    return ParseBlock();
  }

  return ParseSimpleStatement();
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

  vector<unique_ptr<ExpressionNode>> Statements;

  while (true) {
    if (CurrentToken == tok_dedent)
      break;

    auto Stmt = ParseStatement();
    if (!Stmt)
      return nullptr;
    Statements.push_back(std::move(Stmt));

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

  return make_unique<BlockStatementNode>(std::move(Statements));
}

/// function-signature
///   = name "(" [ parameters ] ")" ;
/// external-function-signature
///   = name "(" [ parameters [ "," "..." ] | "..." ] ")" ;
///
/// typed-parameter
///   = name ":" type ;
static unique_ptr<FunctionSignatureNode>
ParseFunctionSignature(bool AllowVariadic = false) {
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
  vector<string> ParameterStructNames;
  bool IsVariadic = false;
  getNextToken(); // eat '('

  if (CurrentToken != tok_rparen) {
    while (true) {
      if (AllowVariadic && CurrentToken == tok_dot) {
        getNextToken(); // eat the first '.'
        if (CurrentToken != tok_dot)
          return LogErrorSignature(
              "Expected '...' in variadic function signature");
        getNextToken(); // eat the second '.'
        if (CurrentToken != tok_dot)
          return LogErrorSignature(
              "Expected '...' in variadic function signature");
        getNextToken(); // eat the third '.'
        IsVariadic = true;
        if (CurrentToken != tok_rparen)
          return LogErrorSignature(
              "Variadic marker must be last in parameter list");
        break;
      }
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
      FunctionName, std::move(ParameterNames), SignatureLoc, ValueType::Float64,
      std::move(ParameterStructNames), "", IsVariadic);
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

/// I parse the inline simple-statement or indented block portion of a/// function-definition
///   = "def" function-signature [ "->" type ] ":" suite ;
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
  unique_ptr<ExpressionNode> Body = ParseSuite();

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
///   = "extern" "def" external-function-signature [ "->" type ] ;
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurrentToken != tok_def)
    return LogErrorSignature("Expected 'def' after 'extern'");
  getNextToken(); // eat def
  auto Signature = ParseFunctionSignature(true);
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
// TheBuilder - Cursor used to append instructions into the current block.
static std::unique_ptr<IRBuilder<NoFolder>> TheBuilder;
// NamedValues - Maps variable names to allocas in the current function.
static std::map<string, AllocaInst *> NamedValues;
static std::map<string, string> NamedValueStructNames;
static std::map<string, StructType *> LLVMStructTypes;
static unsigned StringLiteralCounter = 0;
// InGlobalInit - True while emitting the synthetic global init function.
static bool InGlobalInit = false;
// ModuleHasGlobals - Tracks whether this module defines any globals.
static bool ModuleHasGlobals = false;
// Source path and metadata state used while emitting debug information.
static string CurrentSourcePath = "<stdin>";
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
// JIT - ORC JIT instance for REPL execution.
static std::unique_ptr<PyxcJIT> JIT;
// FunctionPasses - Per-function optimization pipeline (JIT).
static std::unique_ptr<FunctionPassManager> FunctionPasses;
// ModulePasses - Per-module optimization pipeline used by file emission.
static std::unique_ptr<ModulePassManager> ModulePasses;
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
  case ValueType::Array:
    return "array";
  case ValueType::Pointer:
    return "ptr";
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

static string EncodePointerType(ValueType PointeeType,
                                const string &PointeeStructName) {
  return std::to_string(static_cast<int>(PointeeType)) + ":" +
         PointeeStructName;
}

static bool DecodePointerType(const string &Encoded, ValueType &PointeeType,
                              string &PointeeStructName) {
  size_t Separator = Encoded.find(':');
  if (Separator == string::npos)
    return false;
  int RawType = std::atoi(Encoded.substr(0, Separator).c_str());
  if (RawType < static_cast<int>(ValueType::None) ||
      RawType >= static_cast<int>(ValueType::Error))
    return false;
  PointeeType = static_cast<ValueType>(RawType);
  PointeeStructName = Encoded.substr(Separator + 1);
  return PointeeType != ValueType::None &&
         PointeeType != ValueType::Pointer;
}

static string EncodeArrayType(ValueType ElementType,
                              const string &ElementStructName,
                              uint64_t ElementCount) {
  return std::to_string(static_cast<int>(ElementType)) + ":" +
         ElementStructName + ":" + std::to_string(ElementCount);
}

static bool DecodeArrayType(const string &Encoded, ValueType &ElementType,
                            string &ElementStructName,
                            uint64_t &ElementCount) {
  size_t FirstSeparator = Encoded.find(':');
  size_t LastSeparator = Encoded.rfind(':');
  if (FirstSeparator == string::npos || LastSeparator == FirstSeparator)
    return false;
  int RawType = std::atoi(Encoded.substr(0, FirstSeparator).c_str());
  if (RawType < static_cast<int>(ValueType::None) ||
      RawType >= static_cast<int>(ValueType::Error) ||
      RawType == static_cast<int>(ValueType::Array))
    return false;
  ElementType = static_cast<ValueType>(RawType);
  ElementStructName = Encoded.substr(
      FirstSeparator + 1, LastSeparator - FirstSeparator - 1);
  ElementCount =
      std::strtoull(Encoded.substr(LastSeparator + 1).c_str(), nullptr, 10);
  return ElementCount > 0;
}

static bool ArrayDecaysToPointerType(const string &ArrayTypeInfo,
                                     const string &PointerTypeInfo) {
  ValueType ArrayElementType = ValueType::Error;
  string ArrayElementStructName;
  uint64_t ElementCount = 0;
  ValueType PointerElementType = ValueType::Error;
  string PointerElementStructName;
  return DecodeArrayType(ArrayTypeInfo, ArrayElementType,
                         ArrayElementStructName, ElementCount) &&
         DecodePointerType(PointerTypeInfo, PointerElementType,
                           PointerElementStructName) &&
         ArrayElementType == PointerElementType &&
         ArrayElementStructName == PointerElementStructName;
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
  case ValueType::Array: {
    ValueType ElementType = ValueType::Error;
    string ElementStructName;
    uint64_t ElementCount = 0;
    if (!DecodeArrayType(StructName, ElementType, ElementStructName,
                         ElementCount))
      return nullptr;
    return ArrayType::get(LLVMTypeFor(ElementType, ElementStructName),
                          ElementCount);
  }
  case ValueType::Pointer:
    return PointerType::get(*TheContext, 0);
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
  case ValueType::Array:
    return DIB ? DIB->createUnspecifiedType("array") : nullptr;
  case ValueType::Pointer:
    return DIB ? DIB->createUnspecifiedType("ptr") : nullptr;
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
  TheBuilder->SetCurrentDebugLocation(
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
                     TheBuilder->GetInsertBlock());
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

/// LogErrorValue - Codegen-level error helper. Delegates to LogErrorExpression for printing,
/// then returns nullptr so codegen callers can write: return LogErrorValue("msg");
Value *LogErrorValue(const string &ErrorMessage) {
  LogErrorExpression(ErrorMessage);
  return nullptr;
}

/// CreateEntryBlockAlloca - Create a stack slot in the current function's
/// entry block for a mutable variable.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VariableName,
                                          ValueType Type,
                                          const string &StructName = "") {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(LLVMTypeFor(Type, StructName), nullptr, VariableName);
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
  case ValueType::Array:
    return Constant::getNullValue(LLVMTypeFor(Type, StructName));
  case ValueType::Pointer:
    return ConstantPointerNull::get(
        cast<PointerType>(LLVMTypeFor(Type, StructName)));
  default:
    return nullptr;
  }
}

static Value *EmitCast(Value *V, ValueType From, ValueType To) {
  if (!V)
    return nullptr;
  if (From == To)
    return V;
  if (From == ValueType::Pointer && To == ValueType::Pointer)
    return V;
  // Integer ↔ float conversions.
  if (IsIntType(From) && IsFloatType(To))
    return IsUnsignedIntType(From)
               ? TheBuilder->CreateUIToFP(V, LLVMTypeFor(To), "uitofp")
               : TheBuilder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
  if (IsFloatType(From) && IsIntType(To))
    return IsUnsignedIntType(To)
               ? TheBuilder->CreateFPToUI(V, LLVMTypeFor(To), "fptoui")
               : TheBuilder->CreateFPToSI(V, LLVMTypeFor(To), "fptosi");
  // Integer resize (trunc or sign-extend).
  if (IsIntType(From) && IsIntType(To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    if (FromBits == ToBits)
      return V;
    if (ToBits < FromBits)
      return TheBuilder->CreateTrunc(V, LLVMTypeFor(To), "trunc");
    return IsUnsignedIntType(From)
               ? TheBuilder->CreateZExt(V, LLVMTypeFor(To), "zext")
               : TheBuilder->CreateSExt(V, LLVMTypeFor(To), "sext");
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
  if (From == ValueType::Array && To == ValueType::Pointer)
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
    return IsUnsignedIntType(From)
               ? TheBuilder->CreateZExt(V, LLVMTypeFor(To), "zext")
               : TheBuilder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  if (IsIntType(From) && IsFloatType(To))
    return IsUnsignedIntType(From)
               ? TheBuilder->CreateUIToFP(V, LLVMTypeFor(To), "uitofp")
               : TheBuilder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
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
Function *getFunction(const string &Name) {
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

Value *StringExpressionNode::codegen() {
  auto *ByteType = Type::getInt8Ty(*TheContext);
  auto *StorageType = ArrayType::get(ByteType, Text.size() + 1);
  auto *Initializer = ConstantDataArray::getString(*TheContext, Text, true);
  string GlobalName = ".str." + to_string(StringLiteralCounter++);
  auto *Global = new GlobalVariable(
      *TheModule, StorageType, true, GlobalValue::PrivateLinkage, Initializer,
      GlobalName);
  Global->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  Global->setAlignment(Align(1));
  ModuleHasGlobals = true;

  Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  return TheBuilder->CreateInBoundsGEP(StorageType, Global, {Zero, Zero},
                                    "strptr");
}

/// NameExpressionNode::codegen - A variable reference loads the current value
/// from the variable's stack slot.
Value *NameExpressionNode::codegenAddress() {
  auto Local = NamedValues.find(Name);
  if (Local != NamedValues.end() && Local->second)
    return Local->second;
  return GetGlobalVariable(Name);
}

Value *NameExpressionNode::codegen() {
  if (getType() == ValueType::Array) {
    Value *ArrayAddress = codegenAddress();
    if (!ArrayAddress)
      return LogErrorValue("Unknown variable name: '" + Name + "'");
    Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
    return TheBuilder->CreateInBoundsGEP(
        LLVMTypeFor(getType(), getStructName()), ArrayAddress, {Zero, Zero},
        "arraydecay");
  }

  auto VariableBinding = NamedValues.find(Name);
  if (VariableBinding != NamedValues.end() && VariableBinding->second)
    return TheBuilder->CreateLoad(LLVMTypeFor(getType(), getStructName()), VariableBinding->second,
                               Name.c_str());

  if (auto *GV = GetGlobalVariable(Name))
    return TheBuilder->CreateLoad(LLVMTypeFor(getType(), getStructName()), GV,
                               Name.c_str());

  return LogErrorValue("Unknown variable name: '" + Name + "'");
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
    Pointer = TheBuilder->CreateStructGEP(
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
    return LogErrorValue("Unknown field access");
  return TheBuilder->CreateLoad(LLVMTypeFor(FieldType, FieldStructName), Pointer,
                             "fieldload");
}

Value *FieldExpressionNode::codegenAddress() {
  Value *Pointer = GetFieldAddress(*getLValueName(), FieldPath);
  if (!Pointer)
    return LogErrorValue("Unknown field access");
  return Pointer;
}

Value *MemberExpressionNode::codegenAddress() {
  Value *BaseAddress = Base->codegenAddress();
  if (!BaseAddress)
    return LogErrorValue("Field access requires an lvalue");
  return TheBuilder->CreateStructGEP(
      LLVMTypeFor(ValueType::Struct, Base->getStructName()), BaseAddress,
      FieldIndex, "fieldptr");
}

Value *MemberExpressionNode::codegen() {
  Value *Address = codegenAddress();
  if (!Address)
    return nullptr;
  return TheBuilder->CreateLoad(LLVMTypeFor(getType(), getStructName()), Address,
                             "fieldload");
}

Value *IndexExpressionNode::codegenAddress() {
  Value *IndexValue = Index->codegen();
  if (!IndexValue)
    return nullptr;
  IndexValue = TheBuilder->CreateIntCast(
      IndexValue, Type::getInt64Ty(*TheContext),
      !IsUnsignedIntType(Index->getType()), "index");

  if (Base->getType() == ValueType::Array) {
    Value *ArrayAddress = Base->codegenAddress();
    if (!ArrayAddress)
      return nullptr;
    Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
    return TheBuilder->CreateInBoundsGEP(
        LLVMTypeFor(Base->getType(), Base->getStructName()), ArrayAddress,
        {Zero, IndexValue}, "elemptr");
  }

  Value *BasePointer = Base->codegen();
  if (!BasePointer)
    return nullptr;
  return TheBuilder->CreateInBoundsGEP(
      LLVMTypeFor(getType(), getStructName()), BasePointer, IndexValue,
      "elemptr");
}

Value *IndexExpressionNode::codegen() {
  Value *Address = codegenAddress();
  if (!Address)
    return nullptr;
  return TheBuilder->CreateLoad(LLVMTypeFor(getType(), getStructName()), Address,
                             "elemload");
}

Value *AddrExpressionNode::codegen() {
  Value *Address = Operand->codegenAddress();
  if (!Address)
    return LogErrorValue("addr expects an lvalue");
  return Address;
}

Value *ArrayLiteralExpressionNode::codegen() {
  ValueType ElementType = ValueType::Error;
  string ElementStructName;
  uint64_t ElementCount = 0;
  if (!DecodeArrayType(getStructName(), ElementType, ElementStructName,
                       ElementCount))
    return LogErrorValue("Invalid array literal type");

  Value *Aggregate = UndefValue::get(LLVMTypeFor(getType(), getStructName()));
  for (size_t Index = 0; Index < Elements.size(); ++Index) {
    Value *Element = Elements[Index]->codegen();
    if (!Element)
      return nullptr;
    Element = EmitImplicitCast(Element, Elements[Index]->getType(), ElementType);
    if (!Element)
      return LogErrorValue("Array literal element type mismatch");
    Aggregate = TheBuilder->CreateInsertValue(Aggregate, Element,
                                           {static_cast<unsigned>(Index)},
                                           "arrayinit");
  }
  return Aggregate;
}

Value *AssignmentExpressionNode::codegen() {
  Value *Address = Left->codegenAddress();
  if (!Address)
    return LogErrorValue("Assignment target must be assignable");
  Value *AssignedValue = Right->codegen();
  if (!AssignedValue)
    return nullptr;
  AssignedValue = EmitImplicitCast(AssignedValue, Right->getType(), getType());
  if (!AssignedValue)
    return LogErrorValue("Type mismatch in assignment");
  TheBuilder->CreateStore(AssignedValue, Address);
  return AssignedValue;
}

/// ReturnStatementNode::codegen - Emit a return from the current function.
Value *ReturnStatementNode::codegen() {
  if (!Expression) {
    TheBuilder->CreateRetVoid();
    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Value *RetVal = Expression->codegen();
  if (!RetVal)
    return nullptr;
  RetVal = EmitImplicitCast(RetVal, Expression->getType(), CurrentFunctionReturnType);
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
  auto SavedStructNames = NamedValueStructNames;

  Value *Last = nullptr;
  for (auto &Statement : Statements) {
    if (TheBuilder->GetInsertBlock()->getTerminator())
      break;
    Last = Statement->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      NamedValueStructNames = SavedStructNames;
      return nullptr;
    }
  }

  NamedValues = SavedBindings;
  NamedValueStructNames = SavedStructNames;

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
  if (Operator == tok_and || Operator == tok_or) {
    Value *LeftValue = Left->codegen();
    if (!LeftValue)
      return nullptr;

    Function *FunctionIR = TheBuilder->GetInsertBlock()->getParent();
    BasicBlock *LeftBlock = TheBuilder->GetInsertBlock();
    BasicBlock *RightBlock =
        BasicBlock::Create(*TheContext, "logic.rhs", FunctionIR);
    BasicBlock *MergeBlock = BasicBlock::Create(*TheContext, "logic.end");

    if (Operator == tok_and)
      TheBuilder->CreateCondBr(LeftValue, RightBlock, MergeBlock);
    else
      TheBuilder->CreateCondBr(LeftValue, MergeBlock, RightBlock);

    TheBuilder->SetInsertPoint(RightBlock);
    Value *RightValue = Right->codegen();
    if (!RightValue)
      return nullptr;
    TheBuilder->CreateBr(MergeBlock);
    RightBlock = TheBuilder->GetInsertBlock();

    FunctionIR->insert(FunctionIR->end(), MergeBlock);
    TheBuilder->SetInsertPoint(MergeBlock);
    PHINode *Result =
        TheBuilder->CreatePHI(Type::getInt1Ty(*TheContext), 2, "logictmp");
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
    if ((Operator == tok_plus || Operator == tok_minus) &&
        getType() == ValueType::Pointer) {
      Value *Pointer = nullptr;
      Value *Index = nullptr;
      if (LType == ValueType::Pointer && IsIntType(RType)) {
        Pointer = L;
        Index = R;
      } else if (Operator == tok_plus && RType == ValueType::Pointer &&
                 IsIntType(LType)) {
        Pointer = R;
        Index = L;
      }
      if (!Pointer || !Index)
        return LogErrorValue("Type mismatch in pointer arithmetic");
      ValueType IndexType = LType == ValueType::Pointer ? RType : LType;
      Index = TheBuilder->CreateIntCast(Index, Type::getInt64Ty(*TheContext),
                                     !IsUnsignedIntType(IndexType), "ptrindex");
      if (Operator == tok_minus)
        Index = TheBuilder->CreateNeg(Index, "negindex");
      ValueType ElementType = ValueType::Error;
      string ElementStructName;
      if (!DecodePointerType(getStructName(), ElementType, ElementStructName))
        return LogErrorValue("Invalid pointer type metadata");
      return TheBuilder->CreateInBoundsGEP(
          LLVMTypeFor(ElementType, ElementStructName), Pointer, Index,
          "ptrarith");
    }

    if (Operator == tok_minus && getType() == ValueType::Int64 &&
        LType == ValueType::Pointer && RType == ValueType::Pointer) {
      ValueType ElementType = ValueType::Error;
      string ElementStructName;
      if (!DecodePointerType(Left->getStructName(), ElementType,
                             ElementStructName))
        return LogErrorValue("Invalid pointer type metadata");
      return TheBuilder->CreatePtrDiff(
          LLVMTypeFor(ElementType, ElementStructName), L, R, "ptrdiff");
    }

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
      return IsUnsignedIntType(getType())
                 ? TheBuilder->CreateUDiv(L, R, "divtmp")
                 : TheBuilder->CreateSDiv(L, R, "divtmp");
    if (Operator == tok_percent)
      return IsUnsignedIntType(getType())
                 ? TheBuilder->CreateURem(L, R, "remtmp")
                 : TheBuilder->CreateSRem(L, R, "remtmp");
    return TheBuilder->CreateMul(L, R, "multmp");
  }
  case tok_ampersand:
  case tok_pipe:
  case tok_caret: {
    ValueType ResultType = getType();
    L = EmitImplicitCast(L, LType, ResultType);
    R = EmitImplicitCast(R, RType, ResultType);
    if (!L || !R)
      return LogErrorValue("Type mismatch in binary operator");
    if (Operator == tok_ampersand)
      return TheBuilder->CreateAnd(L, R, "bwand");
    if (Operator == tok_pipe)
      return TheBuilder->CreateOr(L, R, "bwor");
    return TheBuilder->CreateXor(L, R, "bwxor");
  }
  case tok_shift_left:
  case tok_shift_right: {
    R = EmitCast(R, RType, LType);
    if (!R)
      return LogErrorValue("Type mismatch in shift operator");
    if (Operator == tok_shift_left)
      return TheBuilder->CreateShl(L, R, "shltmp");
    return IsUnsignedIntType(LType)
               ? TheBuilder->CreateLShr(L, R, "shrtmp")
               : TheBuilder->CreateAShr(L, R, "shrtmp");
  }
  case tok_less:
  case tok_greater:
  case tok_eq:
  case tok_neq:
  case tok_leq:
  case tok_geq: {
    if (LType == ValueType::Pointer && RType == ValueType::Pointer) {
      switch (Operator) {
      case tok_less:
        return TheBuilder->CreateICmpULT(L, R, "cmptmp");
      case tok_greater:
        return TheBuilder->CreateICmpUGT(L, R, "cmptmp");
      case tok_eq:
        return TheBuilder->CreateICmpEQ(L, R, "cmptmp");
      case tok_neq:
        return TheBuilder->CreateICmpNE(L, R, "cmptmp");
      case tok_leq:
        return TheBuilder->CreateICmpULE(L, R, "cmptmp");
      case tok_geq:
        return TheBuilder->CreateICmpUGE(L, R, "cmptmp");
      default:
        break;
      }
    }

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
        return IsUnsignedIntType(CompareType)
                   ? TheBuilder->CreateICmpULT(L, R, "cmptmp")
                   : TheBuilder->CreateICmpSLT(L, R, "cmptmp");
      case tok_greater:
        return IsUnsignedIntType(CompareType)
                   ? TheBuilder->CreateICmpUGT(L, R, "cmptmp")
                   : TheBuilder->CreateICmpSGT(L, R, "cmptmp");
      case tok_eq:
        return TheBuilder->CreateICmpEQ(L, R, "cmptmp");
      case tok_neq:
        return TheBuilder->CreateICmpNE(L, R, "cmptmp");
      case tok_leq:
        return IsUnsignedIntType(CompareType)
                   ? TheBuilder->CreateICmpULE(L, R, "cmptmp")
                   : TheBuilder->CreateICmpSLE(L, R, "cmptmp");
      case tok_geq:
        return IsUnsignedIntType(CompareType)
                   ? TheBuilder->CreateICmpUGE(L, R, "cmptmp")
                   : TheBuilder->CreateICmpSGE(L, R, "cmptmp");
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

  // Built-in unary minus.
  if (Opcode == tok_minus) {
    if (IsIntType(getType()))
      return TheBuilder->CreateNeg(Operator, "negtmp");
    if (IsFloatType(getType()))
      return TheBuilder->CreateFNeg(Operator, "negtmp");
    return LogErrorValue("Unary '-' not supported for this type");
  }

  if (Opcode == tok_exclamation)
    return TheBuilder->CreateNot(Operator, "nottmp");

  if (Opcode == tok_tilde)
    return TheBuilder->CreateNot(Operator, "bnottmp");

  return LogErrorValue("Invalid unary operator: " + FormatTokenForMessage(Opcode));
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

Value *SizeofExpressionNode::codegen() {
  llvm::Type *TargetLLVMType = LLVMTypeFor(TargetType, TargetTypeInfo);
  if (!TargetLLVMType)
    return LogErrorValue("Invalid sizeof target type");
  uint64_t Bytes = TheModule->getDataLayout()
                       .getTypeAllocSize(TargetLLVMType)
                       .getFixedValue();
  return ConstantInt::get(Type::getInt64Ty(*TheContext), Bytes);
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

  if ((!CalleeF->isVarArg() && CalleeF->arg_size() != Arguments.size()) ||
      (CalleeF->isVarArg() && Arguments.size() < CalleeF->arg_size()))
    return LogErrorValue(
        "Incorrect number of arguments in call to '" + Callee +
        "': expected " + to_string(CalleeF->arg_size()) +
        (CalleeF->isVarArg() ? " or more, got " : ", got ") +
        to_string(Arguments.size()));

  FunctionSignatureNode *Signature = GetFunctionSignature(Callee);
  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Arguments.size(); i != e; ++i) {
    Value *ArgVal = Arguments[i]->codegen();
    if (!ArgVal)
      return nullptr;
    if (Signature && i < Signature->getNumParameters()) {
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
  Value *CondV = Condition->codegen();
  if (!CondV)
    return nullptr;

  CondV = ToBool(CondV, Condition->getType());
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
  if (DeclaresVariable) {
    auto OldIt = NamedValues.find(VariableName);
    OldVal = (OldIt != NamedValues.end()) ? OldIt->second : nullptr;
    Alloca = CreateEntryBlockAlloca(TheFunction, VariableName, VarType);
    EmitDebugDeclare(Alloca, VariableName, CurFunctionLine, false, 0, VarType);
    VarPtr = Alloca;
    NamedValues[VariableName] = Alloca;
  } else {
    auto It = NamedValues.find(VariableName);
    if (It != NamedValues.end() && It->second)
      VarPtr = It->second;
    else if (auto *GV = GetGlobalVariable(VariableName))
      VarPtr = GV;
    else
      return LogErrorValue("Unknown variable name: '" + VariableName + "'");
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


  Value *CondVal = Condition->codegen();
  if (!CondVal)
    return nullptr;
  CondVal = ToBool(CondVal, Condition->getType());
  if (!CondVal)
    return LogErrorValue("Invalid loop condition type");
  TheBuilder->CreateCondBr(CondVal, BodyBB, AfterBB);

  TheBuilder->SetInsertPoint(BodyBB);

  LoopControlStack.push_back({AfterBB, StepBB});
  BreakTargetStack.push_back(AfterBB);
  if (!Body->codegen()) {
    BreakTargetStack.pop_back();
    LoopControlStack.pop_back();
    return nullptr;
  }
  BreakTargetStack.pop_back();
  LoopControlStack.pop_back();
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateBr(StepBB);

  TheBuilder->SetInsertPoint(StepBB);

  // Execute the complete update expression; its value is discarded.
  if (!Update->codegen())
    return nullptr;
  TheBuilder->CreateBr(CondBB);

  TheBuilder->SetInsertPoint(AfterBB);

  if (DeclaresVariable) {
    if (OldVal)
      NamedValues[VariableName] = OldVal;
    else
      NamedValues.erase(VariableName);
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
    Value *ConditionValue = Condition->codegen();
    if (!ConditionValue)
      return nullptr;
    ConditionValue = ToBool(ConditionValue, Condition->getType());
    if (!ConditionValue)
      return LogErrorValue("Invalid loop condition type");
    TheBuilder->CreateCondBr(ConditionValue, BodyBlock, AfterBlock);
  }

  TheBuilder->SetInsertPoint(BodyBlock);
  LoopControlStack.push_back({AfterBlock, ConditionBlock});
  BreakTargetStack.push_back(AfterBlock);
  if (!Body->codegen()) {
    BreakTargetStack.pop_back();
    LoopControlStack.pop_back();
    return nullptr;
  }
  BreakTargetStack.pop_back();
  LoopControlStack.pop_back();
  if (!TheBuilder->GetInsertBlock()->getTerminator())
    TheBuilder->CreateBr(ConditionBlock);

  TheBuilder->SetInsertPoint(ConditionBlock);
  if (IsDoWhile) {
    Value *ConditionValue = Condition->codegen();
    if (!ConditionValue)
      return nullptr;
    ConditionValue = ToBool(ConditionValue, Condition->getType());
    if (!ConditionValue)
      return LogErrorValue("Invalid loop condition type");
    TheBuilder->CreateCondBr(ConditionValue, BodyBlock, AfterBlock);
  }

  TheBuilder->SetInsertPoint(AfterBlock);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *SwitchStatementNode::codegen() {
  Value *ConditionValue = Condition->codegen();
  if (!ConditionValue)
    return nullptr;

  auto *ConditionType = dyn_cast<IntegerType>(LLVMTypeFor(Condition->getType()));
  if (!ConditionType)
    return LogErrorValue("Switch condition must be an integer type");

  Function *FunctionIR = TheBuilder->GetInsertBlock()->getParent();
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
      TheBuilder->CreateSwitch(ConditionValue, DefaultBlock, CaseCount);

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
    TheBuilder->SetInsertPoint(CaseBlocks[Index]);
    if (!Cases[Index].second->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!TheBuilder->GetInsertBlock()->getTerminator())
      TheBuilder->CreateBr(AfterBlock);
  }

  if (DefaultCase) {
    TheBuilder->SetInsertPoint(DefaultBlock);
    if (!DefaultCase->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!TheBuilder->GetInsertBlock()->getTerminator())
      TheBuilder->CreateBr(AfterBlock);
  }
  BreakTargetStack.pop_back();

  TheBuilder->SetInsertPoint(AfterBlock);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *BreakStatementNode::codegen() {
  if (BreakTargetStack.empty())
    return LogErrorValue("'break' used outside of a loop or switch");
  TheBuilder->CreateBr(BreakTargetStack.back());
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *ContinueStatementNode::codegen() {
  if (LoopControlStack.empty())
    return LogErrorValue("'continue' used outside of a loop");
  TheBuilder->CreateBr(LoopControlStack.back().ContinueTarget);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// VariableStatementNode::codegen - Allocate mutable local variables and initialize them.
Value *VariableStatementNode::codegen() {
  if (InGlobalInit) {
    for (auto &Var : VariableBindings) {
      const string &VariableName = Var.Name;
      ValueType VarType = Var.Type;
      const string &VarStructName = Var.StructName;
      ExpressionNode *Init = Var.Init.get();

      auto *GV = TheModule->getNamedGlobal(VariableName);
      if (GV && !GV->isDeclaration())
        return LogErrorValue("Global variable already defined");
      if (GV && GV->getValueType() != LLVMTypeFor(VarType, VarStructName))
        return LogErrorValue("Global variable type mismatch");

      if (!GV) {
        auto *Type = LLVMTypeFor(VarType, VarStructName);
        GV = new GlobalVariable(*TheModule, Type, false,
                                GlobalValue::ExternalLinkage,
                                ZeroConstant(VarType, VarStructName), VariableName);
        EmitDebugGlobal(GV, VariableName, CurFunctionLine, VarType);
      } else if (GV->isDeclaration()) {
        GV->setInitializer(ZeroConstant(VarType, VarStructName));
        GV->setLinkage(GlobalValue::ExternalLinkage);
        EmitDebugGlobal(GV, VariableName, CurFunctionLine, VarType);
      }

      ModuleHasGlobals = true;

      Value *InitVal = Init ? Init->codegen()
                            : ZeroConstant(VarType, VarStructName);
      if (!InitVal)
        return nullptr;
      if (Init) {
        InitVal = EmitImplicitCast(InitVal, Init->getType(), VarType);
        if (!InitVal)
          return LogErrorValue("Type mismatch in variable initialization");
      }

      TheBuilder->CreateStore(InitVal, GV);
    }

    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Function *TheFunction = TheBuilder->GetInsertBlock()->getParent();

  for (auto &Var : VariableBindings) {
    const string &VariableName = Var.Name;
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
        return LogErrorValue("Type mismatch in variable initialization");
    }

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VariableName, VarType,
                                                VarStructName);
    TheBuilder->CreateStore(InitVal, Alloca);
    NamedValues[VariableName] = Alloca;
    if (!VarStructName.empty())
      NamedValueStructNames[VariableName] = VarStructName;
    EmitDebugDeclare(Alloca, VariableName, CurFunctionLine, false, 0, VarType);
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
  for (size_t Index = 0; Index < Parameters.size(); ++Index)
    ParameterTypes.push_back(
        LLVMTypeFor(Parameters[Index].second, getParameterStructName(Index)));
  FunctionType *LLVMFunctionType = FunctionType::get(
      LLVMTypeFor(ReturnType, ReturnStructName), ParameterTypes,
      IsVariadic);

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
  string SavedRetStructName = CurrentFunctionReturnStructName;
  CurrentFunctionReturnType = FunctionSignature.getReturnType();
  CurrentFunctionReturnStructName = FunctionSignature.getReturnStructName();

  if (DIB && TheDIFile && FunctionSignature.getName().rfind("__pyxc.", 0) != 0) {
    unsigned Line = FunctionSignature.getLocation().Line ? FunctionSignature.getLocation().Line : 1;
    SmallVector<Metadata *, 8> Types;
    Types.push_back(DITypeFor(FunctionSignature.getReturnType()));
    for (size_t Index = 0; Index < FunctionSignature.getParameters().size(); ++Index)
      Types.push_back(DITypeFor(FunctionSignature.getParameterType(Index)));
    auto *SubroutineType =
        DIB->createSubroutineType(DIB->getOrCreateTypeArray(Types));
    auto *Subprogram = DIB->createFunction(
        TheDIFile, FunctionSignature.getName(), StringRef(), TheDIFile, Line, SubroutineType,
        Line, DINode::FlagZero, DISubprogram::SPFlagDefinition);
    TheFunction->setSubprogram(Subprogram);
    CurDIScope = Subprogram;
    CurFunctionLine = Line;
  }

  // Step 2: create the entry block and point the builder at it.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  TheBuilder->SetInsertPoint(BB);
  SetCurrentDebugLocation(CurFunctionLine);

  // Step 3: I store each argument in an entry-block stack slot and map its
  // parameter name to that slot. When I generate the body, I resolve each
  // parameter reference through this table in NameExpressionNode::codegen().
  NamedValues.clear();
  NamedValueStructNames.clear();
  LoopControlStack.clear();
  BreakTargetStack.clear();
  unsigned ArgumentNumber = 1;
  size_t ArgTypeIndex = 0;
  for (auto &Argument : TheFunction->args()) {
    ValueType ArgType = FunctionSignature.getParameterType(ArgTypeIndex);
    const string &ArgStructName = FunctionSignature.getParameterStructName(ArgTypeIndex++);
    AllocaInst *Alloca = CreateEntryBlockAlloca(
        TheFunction, string(Argument.getName()), ArgType, ArgStructName);
    TheBuilder->CreateStore(&Argument, Alloca);
    NamedValues[string(Argument.getName())] = Alloca;
    if (!ArgStructName.empty())
      NamedValueStructNames[string(Argument.getName())] = ArgStructName;
    EmitDebugDeclare(Alloca, Argument.getName(), CurFunctionLine, true,
                     ArgumentNumber++, ArgType);
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
    FunctionPasses->run(*TheFunction, *FunctionAnalyses);
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
  TypeAliases.clear();
  StringLiteralCounter = 0;
  GlobalVarTypes.clear();
  GlobalVarStructNames.clear();
  GlobalVarDecls.clear();
  LocalVariableScopes.clear();
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
/// needs loop information can reach LoopAnalyses, and so on. PassBuilder then creates
/// LLVM's standard function and module pipelines for the selected level.
static void InitializeModuleAndManagers(bool FreshContext = true) {
  // Fresh context and module for this compilation unit.
  if (FreshContext || !TheContext)
    TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  LLVMStructTypes.clear();
  // Inform the module of the JIT's target data layout so codegen emits
  // correctly-sized types for the host machine.
  TheModule->setDataLayout(JIT->getDataLayout());

  TheBuilder = std::make_unique<IRBuilder<NoFolder>>(*TheContext);
  ModuleHasGlobals = false;
  CurDIScope = nullptr;
  CurFunctionLine = 1;

  // Pass and analysis managers.
  FunctionPasses = std::make_unique<FunctionPassManager>();
  ModulePasses = std::make_unique<ModulePassManager>();
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

  // I ask LLVM to build its standard pipelines for the selected level.
  if (OptLevel != 0) {
    auto FunctionPipeline = PB.buildFunctionSimplificationPipeline(
        GetOptLevel(), ThinOrFullLTOPhase::None);
    FunctionPasses = std::make_unique<FunctionPassManager>(
        std::move(FunctionPipeline));
    auto ModulePipeline = PB.buildPerModuleDefaultPipeline(GetOptLevel());
    ModulePasses =
        std::make_unique<ModulePassManager>(std::move(ModulePipeline));
  }

  InitializeDebugInfo();
}

static void RunModuleOptimizations(Module *Module) {
  if (ModulePasses && OptLevel != 0)
    ModulePasses->run(*Module, *ModuleAnalyses);
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

static void HandleStructDefinition() {
  bool Parsed = ParseStructDefinition();
  bool HasTrailing = CurrentToken != tok_eol && CurrentToken != tok_eof &&
                     CurrentToken != tok_block_end;
  if (!Parsed || HasTrailing) {
    if (Parsed)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    DiscardRestOfLine();
    return;
  }
  Log("Parsed a struct definition.\n");
}

static void HandleTypeAliasDefinition() {
  bool Parsed = ParseTypeAliasDefinition();
  bool HasTrailing = CurrentToken != tok_eol && CurrentToken != tok_eof;
  if (!Parsed || HasTrailing) {
    if (Parsed)
      LogErrorExpression(
          ("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    DiscardRestOfLine();
    return;
  }
  Log("Parsed a type alias.\n");
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

  // Reject redeclarations with a different arity or variadic shape.
  // We validate parameter and return types separately in the parser.
  auto Existing = FunctionSignatures.find(ProtoAST->getName());
  if (Existing != FunctionSignatures.end() &&
      (Existing->second->getNumParameters() != ProtoAST->getNumParameters() ||
       Existing->second->isVariadic() != ProtoAST->isVariadic())) {
    string ConflictReason;
    const size_t PreviousParameterCount =
        Existing->second->getNumParameters();
    const size_t NewParameterCount = ProtoAST->getNumParameters();
    if (PreviousParameterCount != NewParameterCount) {
      ConflictReason =
          "previous declaration has " + to_string(PreviousParameterCount) +
          (PreviousParameterCount == 1 ? " parameter" : " parameters") +
          ", but this declaration has " + to_string(NewParameterCount) +
          (NewParameterCount == 1 ? " parameter" : " parameters");
    } else {
      ConflictReason = string("previous declaration is ") +
                       (Existing->second->isVariadic() ? "variadic"
                                                       : "not variadic") +
                       ", but this declaration is " +
                       (ProtoAST->isVariadic() ? "variadic" : "not variadic");
    }
    LogErrorExpression("Conflicting declaration for function '" +
                       ProtoAST->getName() + "': " + ConflictReason);
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
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::Float32: {
          float (*FP)() = ExprSymbol.toPtr<float (*)()>();
          double result = static_cast<double>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::Int: {
          intptr_t (*FP)() = ExprSymbol.toPtr<intptr_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::Int8: {
          int8_t (*FP)() = ExprSymbol.toPtr<int8_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::Int16: {
          int16_t (*FP)() = ExprSymbol.toPtr<int16_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::Int32: {
          int32_t (*FP)() = ExprSymbol.toPtr<int32_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::Int64: {
          int64_t (*FP)() = ExprSymbol.toPtr<int64_t (*)()>();
          long long result = static_cast<long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::UInt8: {
          uint8_t (*FP)() = ExprSymbol.toPtr<uint8_t (*)()>();
          unsigned long long result = static_cast<unsigned long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::UInt16: {
          uint16_t (*FP)() = ExprSymbol.toPtr<uint16_t (*)()>();
          unsigned long long result = static_cast<unsigned long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::UInt32: {
          uint32_t (*FP)() = ExprSymbol.toPtr<uint32_t (*)()>();
          unsigned long long result = static_cast<unsigned long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::UInt64: {
          uint64_t (*FP)() = ExprSymbol.toPtr<uint64_t (*)()>();
          unsigned long long result = static_cast<unsigned long long>(FP());
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
          break;
        }
        case ValueType::Bool: {
          bool (*FP)() = ExprSymbol.toPtr<bool (*)()>();
          bool result = FP();
          if (IsRepl && LastTopLevelShouldPrint)
            PrintEvaluationResult(result);
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
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::Float32: {
        float (*FP)() = ExprSymbol.toPtr<float (*)()>();
        double result = static_cast<double>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::Int: {
        intptr_t (*FP)() = ExprSymbol.toPtr<intptr_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::Int8: {
        int8_t (*FP)() = ExprSymbol.toPtr<int8_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::Int16: {
        int16_t (*FP)() = ExprSymbol.toPtr<int16_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::Int32: {
        int32_t (*FP)() = ExprSymbol.toPtr<int32_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::Int64: {
        int64_t (*FP)() = ExprSymbol.toPtr<int64_t (*)()>();
        long long result = static_cast<long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::UInt8: {
        uint8_t (*FP)() = ExprSymbol.toPtr<uint8_t (*)()>();
        unsigned long long result = static_cast<unsigned long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::UInt16: {
        uint16_t (*FP)() = ExprSymbol.toPtr<uint16_t (*)()>();
        unsigned long long result = static_cast<unsigned long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::UInt32: {
        uint32_t (*FP)() = ExprSymbol.toPtr<uint32_t (*)()>();
        unsigned long long result = static_cast<unsigned long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::UInt64: {
        uint64_t (*FP)() = ExprSymbol.toPtr<uint64_t (*)()>();
        unsigned long long result = static_cast<unsigned long long>(FP());
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
        break;
      }
      case ValueType::Bool: {
        bool (*FP)() = ExprSymbol.toPtr<bool (*)()>();
        bool result = FP();
        if (IsRepl && LastTopLevelShouldPrint)
          PrintEvaluationResult(result);
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
    case tok_type:
      HandleTypeAliasDefinition();
      break;
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
    case tok_type:
      HandleTypeAliasDefinition();
      break;
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

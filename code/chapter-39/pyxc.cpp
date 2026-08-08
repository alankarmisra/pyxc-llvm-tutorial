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
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iomanip>
#include <limits>
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

// Emit DWARF debug info.
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

enum class EmitKind { None, LLVMIR, ASM, OBJ, EXE };
static EmitKind EmitMode = EmitKind::None;
static string EmitOutputPath;

static bool ShouldDumpIR() { return DumpIR || VerboseIR; }
static bool IsEmitMode() { return EmitMode != EmitKind::None; }

//===----------------------------------------===//
// Lexer
//===----------------------------------------===//

// I return named tokens for known language elements. I preserve the [0-255]
// character value of any other single character for diagnostics and custom operators.
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
  tok_elif = -63,

  // loops
  tok_for = -16,
  tok_while = -52,
  tok_do = -53,
  tok_break = -54,
  tok_continue = -55,
  tok_switch = -60,
  tok_case = -61,
  tok_default = -62,

  // user-defined operators
  tok_binary = -17,
  tok_unary = -18,

  // mutable variables
  tok_var = -19,

  // types
  tok_int = -20,

  // indentation
  tok_indent = -21,
  tok_dedent = -22,

  // new type keywords
  tok_int8 = -23,
  tok_int16 = -24,
  tok_int32 = -25,
  tok_int64 = -26,
  tok_uint8 = -65,
  tok_uint16 = -66,
  tok_uint32 = -67,
  tok_uint64 = -68,
  tok_float = -27,
  tok_float32 = -28,
  tok_float64 = -29,
  tok_bool = -30,
  tok_none = -31,
  tok_true = -32,
  tok_false = -33,
  tok_struct = -34,
  tok_ptr = -35,
  tok_addr = -36,
  tok_sizeof = -37,
  tok_string = -38,
  tok_type = -39,
  tok_class = -40,
  tok_public = -41,
  tok_private = -42,
  tok_trait = -43,
  tok_impl = -44,
  tok_char = -64,
  tok_pluseq = -45,
  tok_minuseq = -46,
  tok_muleq = -47,
  tok_diveq = -48,
  tok_modeq = -49,
  tok_and = -50, // &&
  tok_or = -51,  // ||
  tok_plusplus = -56,
  tok_minusminus = -57,
  tok_shl = -58, // <<
  tok_shr = -59, // >>
  tok_block_end = -100, // synthetic: injected by ParseBlock after eating DEDENT

  // punctuation and operators
  tok_lparen = '(',
  tok_rparen = ')',
  tok_comma = ',',
  tok_colon = ':',
  tok_plus = '+',
  tok_minus = '-',
  tok_star = '*',
  tok_slash = '/',
  tok_less = '<',
  tok_greater = '>',
  tok_equal = '=',
  tok_at = '@',
  tok_dot = '.',
  tok_lbracket = '[',
  tok_rbracket = ']',
  tok_percent = '%',
  tok_exclamation = '!',
  tok_ampersand = '&',
  tok_pipe = '|',
  tok_caret = '^',
  tok_tilde = '~',
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
  TypeVar,
  Error
};

static string Name;          // Filled in if tok_name
static string NumberLiteral;          // Raw number literal text (no sign)
static string StringLiteralStr;       // Filled in if tok_string
static uint32_t CharLiteralValue = 0; // Filled in if tok_char
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
static map<string, Token> Keywords = {{"def", tok_def},
                                      {"extern", tok_extern},
                                      {"return", tok_return},
                                      {"if", tok_if},
                                      {"elif", tok_elif},
                                      {"else", tok_else},
                                      {"for", tok_for},
                                      {"while", tok_while},
                                      {"do", tok_do},
                                      {"break", tok_break},
                                      {"continue", tok_continue},
                                      {"switch", tok_switch},
                                      {"case", tok_case},
                                      {"default", tok_default},
                                      {"binary", tok_binary},
                                      {"unary", tok_unary},
                                      {"var", tok_var},
                                      {"int", tok_int},
                                      {"int8", tok_int8},
                                      {"int16", tok_int16},
                                      {"int32", tok_int32},
                                      {"int64", tok_int64},
                                      {"uint8", tok_uint8},
                                      {"uint16", tok_uint16},
                                      {"uint32", tok_uint32},
                                      {"uint64", tok_uint64},
                                      {"float", tok_float},
                                      {"float32", tok_float32},
                                      {"float64", tok_float64},
                                      {"bool", tok_bool},
                                      {"None", tok_none},
                                      {"True", tok_true},
                                      {"False", tok_false},
                                      {"struct", tok_struct},
                                      {"class", tok_class},
                                      {"public", tok_public},
                                      {"private", tok_private},
                                      {"ptr", tok_ptr},
                                      {"addr", tok_addr},
                                      {"sizeof", tok_sizeof},
                                      {"type", tok_type},
                                      {"trait", tok_trait},
                                      {"impl", tok_impl}};
static constexpr int IndentTabWidth = 8;

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  static map<int, string> Names = {{tok_eof, "end of input"},
                                   {tok_eol, "newline"},
                                   {tok_error, "error"},
                                   {tok_def, "'def'"},
                                   {tok_extern, "'extern'"},
                                   {tok_name, "name"},
                                   {tok_number, "number"},
                                   {tok_return, "'return'"},
                                   {tok_eq, "'=='"},
                                   {tok_neq, "'!='"},
                                   {tok_leq, "'<='"},
                                   {tok_geq, "'>='"},
                                   {tok_arrow, "'->'"},
                                   {tok_if, "'if'"},
                                   {tok_elif, "'elif'"},
                                   {tok_else, "'else'"},
                                   {tok_for, "'for'"},
                                   {tok_while, "'while'"},
                                   {tok_do, "'do'"},
                                   {tok_break, "'break'"},
                                   {tok_continue, "'continue'"},
                                   {tok_switch, "'switch'"},
                                   {tok_case, "'case'"},
                                   {tok_default, "'default'"},
                                   {tok_binary, "'binary'"},
                                   {tok_unary, "'unary'"},
                                   {tok_var, "'var'"},
                                   {tok_int, "'int'"},
                                   {tok_int8, "'int8'"},
                                   {tok_int16, "'int16'"},
                                   {tok_int32, "'int32'"},
                                   {tok_int64, "'int64'"},
                                   {tok_uint8, "'uint8'"},
                                   {tok_uint16, "'uint16'"},
                                   {tok_uint32, "'uint32'"},
                                   {tok_uint64, "'uint64'"},
                                   {tok_float, "'float'"},
                                   {tok_float32, "'float32'"},
                                   {tok_float64, "'float64'"},
                                   {tok_bool, "'bool'"},
                                   {tok_none, "'None'"},
                                   {tok_true, "'True'"},
                                   {tok_false, "'False'"},
                                   {tok_struct, "'struct'"},
                                   {tok_class, "'class'"},
                                   {tok_public, "'public'"},
                                   {tok_private, "'private'"},
                                   {tok_trait, "'trait'"},
                                   {tok_impl, "'impl'"},
                                   {tok_pluseq, "'+='"},
                                   {tok_minuseq, "'-='"},
                                   {tok_muleq, "'*='"},
                                   {tok_diveq, "'/='"},
                                   {tok_modeq, "'%='"},
                                   {tok_and, "'&&'"},
                                   {tok_or, "'||'"},
                                   {tok_plusplus, "'++'"},
                                   {tok_minusminus, "'--'"},
                                   {tok_shl, "'<<'"},
                                   {tok_shr, "'>>'"},
                                   {tok_ptr, "'ptr'"},
                                   {tok_addr, "'addr'"},
                                   {tok_sizeof, "'sizeof'"},
                                   {tok_string, "string literal"},
                                   {tok_char, "character literal"},
                                   {tok_type, "'type'"},
                                   {tok_indent, "indent"},
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

  if (isdigit(LexerLastChar) || (LexerLastChar == '.' && isdigit(peek()))) {
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

  if (LexerLastChar == '"') {
    StringLiteralStr.clear();
    LexerLastChar = advance(); // eat opening quote
    while (LexerLastChar != '"' && LexerLastChar != EOF &&
           LexerLastChar != '\n') {
      if (LexerLastChar == '\\') {
        LexerLastChar = advance();
        switch (LexerLastChar) {
        case '\\':
          StringLiteralStr.push_back('\\');
          break;
        case '"':
          StringLiteralStr.push_back('"');
          break;
        case 'n':
          StringLiteralStr.push_back('\n');
          break;
        case 't':
          StringLiteralStr.push_back('\t');
          break;
        case '0':
          StringLiteralStr.push_back('\0');
          break;
        default:
          fprintf(stderr, "Error (Line %d, Column %d): invalid string escape\n",
                  CurLoc.Line, CurLoc.Col);
          PrintErrorSourceContext(CurLoc);
          return tok_error;
        }
      } else {
        StringLiteralStr.push_back(static_cast<char>(LexerLastChar));
      }
      LexerLastChar = advance();
    }

    if (LexerLastChar != '"') {
      fprintf(stderr,
              "Error (Line %d, Column %d): unterminated string literal\n",
              CurLoc.Line, CurLoc.Col);
      PrintErrorSourceContext(CurLoc);
      return tok_error;
    }
    LexerLastChar = advance(); // eat closing quote
    return tok_string;
  }

  if (LexerLastChar == '\'') {
    LexerLastChar = advance(); // eat opening quote
    if (LexerLastChar == '\'' || LexerLastChar == '\n' ||
        LexerLastChar == EOF) {
      fprintf(stderr, "Error (Line %d, Column %d): empty character literal\n",
              CurLoc.Line, CurLoc.Col);
      PrintErrorSourceContext(CurLoc);
      return tok_error;
    }

    uint32_t Value = 0;
    if (LexerLastChar == '\\') {
      LexerLastChar = advance();
      switch (LexerLastChar) {
      case '\\':
        Value = '\\';
        break;
      case '\'':
        Value = '\'';
        break;
      case 'n':
        Value = '\n';
        break;
      case 't':
        Value = '\t';
        break;
      case '0':
        Value = '\0';
        break;
      default:
        fprintf(stderr,
                "Error (Line %d, Column %d): invalid character escape\n",
                CurLoc.Line, CurLoc.Col);
        PrintErrorSourceContext(CurLoc);
        return tok_error;
      }
    } else {
      Value = static_cast<unsigned char>(LexerLastChar);
    }

    LexerLastChar = advance();
    if (LexerLastChar != '\'') {
      fprintf(stderr,
              "Error (Line %d, Column %d): unterminated character literal\n",
              CurLoc.Line, CurLoc.Col);
      PrintErrorSourceContext(CurLoc);
      return tok_error;
    }
    LexerLastChar = advance(); // eat closing quote
    CharLiteralValue = Value;
    return tok_char;
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

  if (LexerLastChar == '+') {
    int Next = peek();
    int Tok = tok_plus;
    if (Next == '=')
      Tok = (advance(), tok_pluseq);
    else if (Next == '+')
      Tok = (advance(), tok_plusplus);
    LexerLastChar = advance();
    return Tok;
  }

  // peek(), if the next one completes a recognized token, eat it, and return
  // token; otherwise, I return the named single-character token.
  if (LexerLastChar == '-') {
    int Next = peek();
    int Tok = tok_minus;
    if (Next == '>')
      Tok = (advance(), tok_arrow);
    else if (Next == '=')
      Tok = (advance(), tok_minuseq);
    else if (Next == '-')
      Tok = (advance(), tok_minusminus);
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '*') {
    int Tok = (peek() == '=') ? (advance(), tok_muleq) : tok_star;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '/') {
    int Tok = (peek() == '=') ? (advance(), tok_diveq) : tok_slash;
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '%') {
    int Tok = (peek() == '=') ? (advance(), tok_modeq) : tok_percent;
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
      Tok = (advance(), tok_shl);
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '>') {
    int Next = peek();
    int Tok = tok_greater;
    if (Next == '=')
      Tok = (advance(), tok_geq);
    else if (Next == '>')
      Tok = (advance(), tok_shr);
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
  case '<':
    return tok_less;
  case '>':
    return tok_greater;
  case '=':
    return tok_equal;
  case '@':
    return tok_at;
  case '.':
    return tok_dot;
  case '[':
    return tok_lbracket;
  case ']':
    return tok_rbracket;
  case '%':
    return tok_percent;
  case '!':
    return tok_exclamation;
  case '&':
    return tok_ampersand;
  case '|':
    return tok_pipe;
  case '^':
    return tok_caret;
  case '~':
    return tok_tilde;
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
  explicit StringExpressionNode(string Text, const string &PtrTypeInfo)
      : Text(std::move(Text)) {
    setType(ValueType::Pointer, PtrTypeInfo);
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
  const vector<unique_ptr<ExpressionNode>> &getElements() const { return Elements; }
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
  // convenience function
  const string &getName() const { return Name; }
  const string *getLValueName() const override { return &Name; }
  Value *codegen() override;
};

/// FieldExpressionNode - Nested field access, e.g. p.x or p.inner.value
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
  Value *codegen() override;
};

/// AssignmentExpressionNode - Expression class for assignment to an existing variable.
/// The expression stores Right into the named variable and produces the assigned
/// value.
class AssignmentExpressionNode : public ExpressionNode {
  string Name;
  unique_ptr<ExpressionNode> Expr;

public:
  AssignmentExpressionNode(const string &Name, unique_ptr<ExpressionNode> Expr,
                    ValueType Type)
      : Name(Name), Expr(std::move(Expr)) {
    setType(Type);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class CompoundAssignmentExpressionNode : public ExpressionNode {
  string Name;
  int Operator;
  unique_ptr<ExpressionNode> Right;

public:
  CompoundAssignmentExpressionNode(const string &Name, int Operator, unique_ptr<ExpressionNode> Right,
                            ValueType Type, const string &StructName = "")
      : Name(Name), Operator(Operator), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// FieldAssignmentExpressionNode - Assignment to a field path, e.g. p.x = 1
class FieldAssignmentExpressionNode : public ExpressionNode {
  unique_ptr<FieldExpressionNode> Left;
  unique_ptr<ExpressionNode> Right;

public:
  FieldAssignmentExpressionNode(unique_ptr<FieldExpressionNode> Left, unique_ptr<ExpressionNode> Right,
                         ValueType Type)
      : Left(std::move(Left)), Right(std::move(Right)) {
    setType(Type);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class FieldCompoundAssignmentExpressionNode : public ExpressionNode {
  unique_ptr<FieldExpressionNode> Left;
  int Operator;
  unique_ptr<ExpressionNode> Right;

public:
  FieldCompoundAssignmentExpressionNode(unique_ptr<FieldExpressionNode> Left, int Operator,
                                 unique_ptr<ExpressionNode> Right, ValueType Type,
                                 const string &StructName = "")
      : Left(std::move(Left)), Operator(Operator), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// AddrExpressionNode - address-of for lvalues: addr(x), addr(p.x)
class AddrExpressionNode : public ExpressionNode {
  string BaseName;
  vector<string> FieldPath;

public:
  AddrExpressionNode(string BaseName, vector<string> FieldPath,
              const string &PointerTypeInfo = "")
      : BaseName(std::move(BaseName)), FieldPath(std::move(FieldPath)) {
    setType(ValueType::Pointer, PointerTypeInfo);
  }
  Value *codegen() override;
};

/// IndexExpressionNode - pointer indexing and load: p[i]
class IndexExpressionNode : public ExpressionNode {
  string BaseName;
  vector<string> FieldPath;
  unique_ptr<ExpressionNode> Index;

public:
  IndexExpressionNode(string BaseName, vector<string> FieldPath,
               unique_ptr<ExpressionNode> Index, ValueType ElemType,
               const string &ElemStructName = "")
      : BaseName(std::move(BaseName)), FieldPath(std::move(FieldPath)),
        Index(std::move(Index)) {
    setType(ElemType, ElemStructName);
  }
  ExpressionNode *getIndex() const { return Index.get(); }
  const string &getBaseName() const { return BaseName; }
  const vector<string> &getFieldPath() const { return FieldPath; }
  Value *codegen() override;
};

class IndexAssignmentExpressionNode : public ExpressionNode {
  unique_ptr<IndexExpressionNode> Left;
  unique_ptr<ExpressionNode> Right;

public:
  IndexAssignmentExpressionNode(unique_ptr<IndexExpressionNode> Left, unique_ptr<ExpressionNode> Right,
                         ValueType Type, const string &StructName = "")
      : Left(std::move(Left)), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class IndexCompoundAssignmentExpressionNode : public ExpressionNode {
  unique_ptr<IndexExpressionNode> Left;
  int Operator;
  unique_ptr<ExpressionNode> Right;

public:
  IndexCompoundAssignmentExpressionNode(unique_ptr<IndexExpressionNode> Left, int Operator,
                                 unique_ptr<ExpressionNode> Right, ValueType Type,
                                 const string &StructName = "")
      : Left(std::move(Left)), Operator(Operator), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class IndexedFieldExpressionNode : public ExpressionNode {
  unique_ptr<IndexExpressionNode> BaseIndex;
  vector<string> FieldPath;

public:
  IndexedFieldExpressionNode(unique_ptr<IndexExpressionNode> BaseIndex,
                      vector<string> FieldPath, ValueType Type,
                      const string &StructName = "")
      : BaseIndex(std::move(BaseIndex)), FieldPath(std::move(FieldPath)) {
    setType(Type, StructName);
  }
  IndexExpressionNode *getBaseIndex() const { return BaseIndex.get(); }
  const vector<string> &getFieldPath() const { return FieldPath; }
  bool shouldPrintValue() const override { return true; }
  Value *codegen() override;
};

class IndexedFieldAssignmentExpressionNode : public ExpressionNode {
  unique_ptr<IndexedFieldExpressionNode> Left;
  unique_ptr<ExpressionNode> Right;

public:
  IndexedFieldAssignmentExpressionNode(unique_ptr<IndexedFieldExpressionNode> Left,
                                unique_ptr<ExpressionNode> Right, ValueType Type,
                                const string &StructName = "")
      : Left(std::move(Left)), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class IndexedFieldCompoundAssignmentExpressionNode : public ExpressionNode {
  unique_ptr<IndexedFieldExpressionNode> Left;
  int Operator;
  unique_ptr<ExpressionNode> Right;

public:
  IndexedFieldCompoundAssignmentExpressionNode(unique_ptr<IndexedFieldExpressionNode> Left,
                                        int Operator, unique_ptr<ExpressionNode> Right,
                                        ValueType Type,
                                        const string &StructName = "")
      : Left(std::move(Left)), Operator(Operator), Right(std::move(Right)) {
    setType(Type, StructName);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// ReturnExpressionNode - Statement-like expression for return.
/// Emits a function return and produces the returned value.
class ReturnExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Expr;

public:
  ReturnExpressionNode(unique_ptr<ExpressionNode> Expr = nullptr) : Expr(std::move(Expr)) {
    setType(ValueType::None);
  }
  bool isReturnExpr() const override { return true; }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// BlockExpressionNode - A sequence of statements evaluated in order.
/// The block's value is the value of the last statement executed.
class BlockExpressionNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Stmts;

public:
  BlockExpressionNode(vector<unique_ptr<ExpressionNode>> Stmts) : Stmts(std::move(Stmts)) {
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

/// ConstructorCallExpressionNode - Expression class for class construction:
///   Point(1, 2)
/// Lowers to stack-temp allocation + optional Point.__init__ call + load.
class ConstructorCallExpressionNode : public ExpressionNode {
  string ClassName;
  vector<unique_ptr<ExpressionNode>> Arguments;

public:
  ConstructorCallExpressionNode(const string &ClassName,
                         vector<unique_ptr<ExpressionNode>> Arguments)
      : ClassName(ClassName), Arguments(std::move(Arguments)) {
    setType(ValueType::Struct, ClassName);
  }
  Value *codegen() override;
};

/// ForExpressionNode - Expression class for for loops.
///   for <var> = <start>, <cond>, <step>: <body>
/// The loop variable is in scope for <cond>, <step>, and <body> (through
/// NamedValues). The expression always produces 0.0 — the loop is used for side
/// effects.
class ForExpressionNode : public ExpressionNode {
  string VarName;
  bool IsVarDecl;
  ValueType VarType;
  unique_ptr<ExpressionNode> Start, Cond, Step, Body;

public:
  ForExpressionNode(const string &VarName, bool IsVarDecl, ValueType VarType,
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

class WhileExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Cond;
  unique_ptr<ExpressionNode> Body;
  bool IsDoWhile;

public:
  WhileExpressionNode(unique_ptr<ExpressionNode> Cond, unique_ptr<ExpressionNode> Body,
               bool IsDoWhile)
      : Cond(std::move(Cond)), Body(std::move(Body)), IsDoWhile(IsDoWhile) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class BreakExpressionNode : public ExpressionNode {
public:
  BreakExpressionNode() { setType(ValueType::None); }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class ContinueExpressionNode : public ExpressionNode {
public:
  ContinueExpressionNode() { setType(ValueType::None); }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

class SwitchExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Cond;
  // Each case may list more than one value (e.g. "case 'a', 'e':"); all
  // values in the list share the same body.
  vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases;
  unique_ptr<ExpressionNode> DefaultCase;

public:
  SwitchExpressionNode(unique_ptr<ExpressionNode> Cond,
                vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases,
                unique_ptr<ExpressionNode> DefaultCase)
      : Cond(std::move(Cond)), Cases(std::move(Cases)),
        DefaultCase(std::move(DefaultCase)) {
    setType(ValueType::None);
  }
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

class IncDecExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Operand;
  bool IsIncrement;
  bool IsPrefix;

public:
  IncDecExpressionNode(unique_ptr<ExpressionNode> Operand, bool IsIncrement, bool IsPrefix,
                ValueType Type, const string &StructName = "")
      : Operand(std::move(Operand)), IsIncrement(IsIncrement),
        IsPrefix(IsPrefix) {
    setType(Type, StructName);
  }
  Value *codegen() override;
};

class LogicalNotExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Operand;

public:
  explicit LogicalNotExpressionNode(unique_ptr<ExpressionNode> Operand)
      : Operand(std::move(Operand)) {
    setType(ValueType::Bool);
  }
  Value *codegen() override;
};

/// CastExpressionNode - Expression class for explicit casts: int(expr), float64(expr).
class CastExpressionNode : public ExpressionNode {
  ValueType TargetType;
  string TargetStructName;
  unique_ptr<ExpressionNode> Expr;

public:
  CastExpressionNode(ValueType TargetType, unique_ptr<ExpressionNode> Expr,
              const string &TargetStructName = "")
      : TargetType(TargetType), TargetStructName(TargetStructName),
        Expr(std::move(Expr)) {
    setType(TargetType, TargetStructName);
  }
  Value *codegen() override;
};

class SizeofExpressionNode : public ExpressionNode {
  ValueType TargetType;
  string TargetStructName;

public:
  SizeofExpressionNode(ValueType TargetType, const string &TargetStructName = "")
      : TargetType(TargetType), TargetStructName(TargetStructName) {
    setType(ValueType::Int64);
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
/// For user-defined operators, IsOperator is true and the function name encodes
/// the operator character: "binary+" for a binary '+' operator, "unary!" for a
/// unary '!' operator. Precedence is only meaningful for binary operators — it
/// is installed into OperatorPrecedence at codegen time, making the new operator
/// immediately available to the parser for subsequent expressions.
class FunctionSignatureNode {
public:
  struct ParameterInfo {
    string Name;
    ValueType Type;
    string StructName;
  };

private:
  string Name;
  vector<ParameterInfo> Parameters;
  ValueType ReturnType;
  string ReturnStructName;
  bool IsOperator;
  unsigned Precedence; // binary operators only
  SourceLocation Loc;

public:
  FunctionSignatureNode(const string &Name, vector<ParameterInfo> Parameters, SourceLocation Loc,
               ValueType ReturnType = ValueType::Float64,
               bool IsOperator = false, unsigned Prec = 0,
               string ReturnStructName = "")
      : Name(Name), Parameters(std::move(Parameters)), ReturnType(ReturnType),
        ReturnStructName(std::move(ReturnStructName)), IsOperator(IsOperator),
        Precedence(Prec), Loc(Loc) {}

  const string &getName() const { return Name; }
  const vector<ParameterInfo> &getParameters() const { return Parameters; }
  size_t getNumParameters() const { return Parameters.size(); }
  SourceLocation getLocation() const { return Loc; }
  ValueType getReturnType() const { return ReturnType; }
  const string &getReturnStructName() const { return ReturnStructName; }
  void setReturnType(ValueType Type) { ReturnType = Type; }
  void setReturnStructName(const string &Name) { ReturnStructName = Name; }

  ValueType getParameterType(size_t Index) const {
    if (Index >= Parameters.size())
      return ValueType::Error;
    return Parameters[Index].Type;
  }
  const string &getParameterStructName(size_t Index) const {
    static string Empty;
    if (Index >= Parameters.size())
      return Empty;
    return Parameters[Index].StructName;
  }

  bool isUnaryOp() const { return IsOperator && Parameters.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Parameters.size() == 2; }

  // The operator character is the last character of the encoded name.
  // e.g. "binary+" -> '+', "unary!" -> '!'
  char getOperatorName() const {
    assert((isUnaryOp() || isBinaryOp()) && "Not an operator function signature");
    return Name.back();
  }

  unsigned getBinaryPrecedence() const { return Precedence; }

  std::unique_ptr<FunctionSignatureNode> clone() const {
    return std::make_unique<FunctionSignatureNode>(
        Name, Parameters, Loc, ReturnType, IsOperator, Precedence, ReturnStructName);
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

/// OperatorPrecedence - Maps each binary operator token to its precedence.
/// Higher numbers bind more tightly: '*' (40) > '+'/'-' (20) > comparisons
/// (10). The key is an int rather than char so it can hold both
/// single-character ASCII operators ('+', '-', '*', '<', '>') and
/// multi-character named token enums (tok_eq, tok_neq, tok_leq, tok_geq). All
/// comparison operators share precedence 10 so they bind equally tightly and
/// are left-associative. Operators not in this map return -1 from
/// GetTokenPrecedence(), which tells ParseBinaryOperatorRight to stop consuming operators
/// and return what it has so far.
static const map<int, int> DefaultOperatorPrecedence = {
    {tok_or, 5},   // ||
    {tok_and, 7},  // &&
    {tok_pipe, 10},     // |
    {tok_caret, 11},     // ^
    {tok_ampersand, 12},     // &
    {tok_eq, 13},  // ==
    {tok_neq, 13}, // !=
    {tok_leq, 14}, // <=
    {tok_geq, 14}, // >=
    {tok_less, 14},     // <
    {tok_greater, 14},     // >
    {tok_shl, 15}, // <<
    {tok_shr, 15}, // >>
    {tok_plus, 20},     // +
    {tok_minus, 20},     // -
    {tok_slash, 40},     // /
    {tok_percent, 40},     // %
    {tok_star, 40},     // *
};
static map<int, int> OperatorPrecedence = DefaultOperatorPrecedence;

static void ResetOperatorPrecedence() { OperatorPrecedence = DefaultOperatorPrecedence; }

// KnownUnaryOperators - Tracks unary operator tokens that are already reserved
// or defined.
//
// Seed with '-' because unary minus is a built-in form handled by
// ParseUnaryMinus(), so users cannot define a custom unary '-'.
static const std::set<int> DefaultKnownUnaryOperators = {tok_minus};
static std::set<int> KnownUnaryOperators = DefaultKnownUnaryOperators;

static void ResetKnownUnaryOperators() {
  KnownUnaryOperators = DefaultKnownUnaryOperators;
}

// FunctionSignatures - Persistent function signature registry used by the parser to detect
// redefinition of operators. Also used by codegen to re-emit declarations into
// fresh modules. Declared here so parser functions can access it.
static std::map<std::string, std::unique_ptr<FunctionSignatureNode>> FunctionSignatures;

struct StructFieldInfo {
  string Name;
  ValueType Type = ValueType::Error;
  string StructName;
  bool IsPublic = true;
};

struct StructTypeInfo {
  string Name;
  bool IsClass = false;
  vector<StructFieldInfo> Fields;
  std::map<string, size_t> FieldIndex;
  std::map<string, bool> MethodIsPublic;
  struct ImplTraitRef {
    string TraitName;
    bool HasTypeArg = false;
    ValueType TypeArg = ValueType::Error;
    string TypeArgStructName;
  };
  vector<ImplTraitRef> ImplementedTraits;
};

struct TraitMethodSig {
  string Name;
  vector<FunctionSignatureNode::ParameterInfo> Arguments;
  ValueType ReturnType = ValueType::None;
  string ReturnStructName;
};

struct TraitInfo {
  string Name;
  string TypeParamName;
  vector<TraitMethodSig> Methods;
};

static std::map<string, StructTypeInfo> StructTypes;
static std::map<string, TraitInfo> Traits;
static std::map<string, std::pair<ValueType, string>> TypeAliases;

// Parse-time variable tracking for assignments and types.
// Scopes are stacked: function scope plus nested block scopes.
// for-loop variables are scoped to the loop body only.
static vector<std::map<string, ValueType>> VarScopes;
static vector<std::map<string, string>> VarStructScopes;
// Global variables declared at top level (persist across modules).
static std::map<string, ValueType> GlobalVarTypes;
static std::map<string, string> GlobalVarStructTypes;
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
static string CurrentClassScopeName;
static std::set<string> ActiveTypeParams;

static bool CanAccessClassMember(const string &OwnerClass, bool IsPublic) {
  return IsPublic || (!CurrentClassScopeName.empty() &&
                      CurrentClassScopeName == OwnerClass);
}

struct ClassScopeGuard {
  string Saved;
  ClassScopeGuard(const string &ClassName) : Saved(CurrentClassScopeName) {
    CurrentClassScopeName = ClassName;
  }
  ~ClassScopeGuard() { CurrentClassScopeName = Saved; }
};

struct TopLevelParseGuard {
  TopLevelParseGuard() { ParsingTopLevel = true; }
  ~TopLevelParseGuard() { ParsingTopLevel = false; }
};

struct ParseLoopGuard {
  ParseLoopGuard() { ++ParseLoopDepth; }
  ~ParseLoopGuard() { --ParseLoopDepth; }
};

struct ParseSwitchGuard {
  ParseSwitchGuard() { ++ParseSwitchDepth; }
  ~ParseSwitchGuard() { --ParseSwitchDepth; }
};

static void BeginFunctionScope(const vector<FunctionSignatureNode::ParameterInfo> &Parameters) {
  VarScopes.clear();
  VarStructScopes.clear();
  VarScopes.emplace_back();
  VarStructScopes.emplace_back();
  for (const auto &Parameter : Parameters) {
    VarScopes.front()[Parameter.Name] = Parameter.Type;
    if (Parameter.Type == ValueType::Struct || Parameter.Type == ValueType::Pointer ||
        Parameter.Type == ValueType::Array)
      VarStructScopes.front()[Parameter.Name] = Parameter.StructName;
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
  if (Type == ValueType::Struct || Type == ValueType::Pointer ||
      Type == ValueType::Array)
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
  if (VarScopes.size() > 1) {
    VarScopes.pop_back();
    VarStructScopes.pop_back();
  } else if (ParsingTopLevel && VarScopes.size() == 1) {
    VarScopes.pop_back();
    VarStructScopes.pop_back();
  }
}

// Check only the innermost scope (used for redeclaration checks).

// Ensure a function scope exists, then add a new scope for the loop variable.
static void BeginLoopScope(const string &Name, ValueType Type,
                           const string &StructName = "") {
  VarScopes.emplace_back();
  VarStructScopes.emplace_back();
  VarScopes.back()[Name] = Type;
  if (Type == ValueType::Struct || Type == ValueType::Pointer ||
      Type == ValueType::Array)
    VarStructScopes.back()[Name] = StructName;
}

// Size == 1 is only popped for top-level blocks (function scope is popped in
// EndFunctionScope).
static void EndLoopScope() {
  if (VarScopes.size() > 1) {
    VarScopes.pop_back();
    if (!VarStructScopes.empty())
      VarStructScopes.pop_back();
  }
  if (ParsingTopLevel && VarScopes.size() == 1) {
    VarScopes.pop_back();
    if (!VarStructScopes.empty())
      VarStructScopes.pop_back();
  }
}

struct FunctionScopeGuard {
  FunctionScopeGuard(const vector<FunctionSignatureNode::ParameterInfo> &Parameters) {
    BeginFunctionScope(Parameters);
  }
  ~FunctionScopeGuard() { EndFunctionScope(); }
};

struct BlockScopeGuard {
  BlockScopeGuard() { BeginBlockScope(); }
  ~BlockScopeGuard() { EndBlockScope(); }
};

struct LoopScopeGuard {
  LoopScopeGuard(const string &Name, ValueType Type,
                 const string &StructName = "") {
    BeginLoopScope(Name, Type, StructName);
  }
  ~LoopScopeGuard() { EndLoopScope(); }
};



struct ReturnTypeGuard {
  ValueType Saved;
  string SavedStruct;
  ReturnTypeGuard(ValueType Type, const string &StructName = "")
      : Saved(CurrentFunctionReturnType),
        SavedStruct(CurrentFunctionReturnStructName) {
    CurrentFunctionReturnType = Type;
    CurrentFunctionReturnStructName = StructName;
  }
  ~ReturnTypeGuard() {
    CurrentFunctionReturnType = Saved;
    CurrentFunctionReturnStructName = SavedStruct;
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
  auto GI = GlobalVarStructTypes.find(Name);
  if (GI != GlobalVarStructTypes.end())
    return GI->second;
  return "";
}

/// GetTokenPrecedence - Returns the precedence of CurrentToken if it is a known binary
/// operator, or -1 if it is not. Both single-character ASCII operators ('+',
/// '-', '*', '<', '>') and named multi-character token enums (tok_eq, tok_neq,
/// tok_leq, tok_geq) are looked up in OperatorPrecedence.
static int GetTokenPrecedence() {
  auto It = OperatorPrecedence.find(CurrentToken);
  if (It == OperatorPrecedence.end() || It->second <= 0)
    return -1;
  return It->second;
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

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogError("message");
unique_ptr<ExpressionNode> LogError(const char *Str) {
  HadError = true;
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurrentToken);
  LogErrorAtLoc(Str, Anchor);
  return nullptr;
}

unique_ptr<FunctionSignatureNode> LogErrorSignature(const char *Str) {
  LogError(Str);
  return nullptr;
}

unique_ptr<FunctionDefinitionNode> LogErrorF(const char *Str) {
  LogError(Str);
  return nullptr;
}

static unique_ptr<ExpressionNode> ParseExpression();
static unique_ptr<ExpressionNode> ParsePrimary();
static unique_ptr<ExpressionNode> ParseSizeofExpression();
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
static string EncodePointerType(ValueType PointeeType,
                                const string &PointeeStructName = "");
static bool DecodePointerType(const string &Encoded, ValueType &PointeeType,
                              string &PointeeStructName);
static string EncodeArrayType(ValueType ElemType, const string &ElemStructName,
                              uint64_t Count);
static bool DecodeArrayType(const string &Encoded, ValueType &ElemType,
                            string &ElemStructName, uint64_t &Count);
static bool ArrayDecaysToPointerType(const string &ArrayInfo,
                                     const string &PointerInfo);
static bool ParseUnsignedDecimal(const string &Text, uint64_t &Out);
static bool ParseAggregateDefinition(const char *KindName);
static unique_ptr<FunctionDefinitionNode>
ParseMethodDefinitionInClass(const string &ClassName, bool IsPublic);
static bool ParseTraitDefinition();
static bool VerifyTraitConformance(const string &ClassName,
                                   const StructTypeInfo::ImplTraitRef &ImplRef);
static bool ParseImplDefinition();
static bool ParseTypeAliasDefinition();
static const char *TypeName(ValueType Type);
static bool IsNumericType(ValueType Type);
static bool IsIntType(ValueType Type);
static bool IsUnsignedIntType(ValueType Type);
static bool IsSignedIntType(ValueType Type);
static bool IsFloatType(ValueType Type);
static bool IsAssignable(ValueType Dest, ValueType Src);
static Type *LLVMTypeFor(ValueType Type, const string &StructName = "");
static FunctionSignatureNode *GetFunctionSignature(const string &Name);
// Optional expected type for numeric literals (used for float/float32).
static ValueType ExpectedLiteralType = ValueType::Error;
static string ExpectedLiteralStructName;

struct ExpectedLiteralTypeGuard {
  ValueType Saved;
  string SavedStruct;
  ExpectedLiteralTypeGuard(ValueType Type, const string &StructName = "")
      : Saved(ExpectedLiteralType), SavedStruct(ExpectedLiteralStructName) {
    ExpectedLiteralType = Type;
    ExpectedLiteralStructName = StructName;
  }
  ~ExpectedLiteralTypeGuard() {
    ExpectedLiteralType = Saved;
    ExpectedLiteralStructName = SavedStruct;
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
  return LogError("Cannot default-initialize this type");
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
      return LogError("Invalid floating-point literal");

    // opStatus may still report conversion issues like invalid op or overflow.
    APFloat::opStatus Status = *StatusOrErr;
    if (Status & APFloat::opInvalidOp)
      return LogError("Invalid floating-point literal");
    if (Status & APFloat::opOverflow)
      return LogError("Floating-point literal out of range for type");

    auto Result = make_unique<NumberExpressionNode>(Val, Type);
    getNextToken(); // consume the number
    return std::move(Result);
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

    // Reject if the literal doesn't fit in the target width.
    APInt Max = IsUnsignedIntType(Type) ? APInt::getAllOnes(Bits)
                                        : APInt::getSignedMaxValue(Bits);
    if (Val.ugt(Max))
      return LogError("Integer literal out of range for type");

    // Truncate down to the target width once it's known to fit.
    // This will actually never happen. It's a paranoia move.
    if (ParseBits != Bits)
      Val = Val.trunc(Bits);

    auto Result = make_unique<NumberExpressionNode>(Val, Type);
    getNextToken(); // consume the number
    return std::move(Result);
  }
}

/// charexpr
///   = "'" <char> "'" ;
static unique_ptr<ExpressionNode> ParseCharExpression() {
  ValueType Type = ValueType::Int32;
  if (IsIntType(ExpectedLiteralType))
    Type = ExpectedLiteralType;
  unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
  APInt Max = IsUnsignedIntType(Type) ? APInt::getAllOnes(Bits)
                                      : APInt::getSignedMaxValue(Bits);
  APInt Val(std::max(1u, Bits), CharLiteralValue, false);
  if (Val.ugt(Max))
    return LogError("Character literal out of range for type");
  if (Val.getBitWidth() != Bits)
    Val = Val.trunc(Bits);
  auto Result = make_unique<NumberExpressionNode>(Val, Type);
  getNextToken(); // consume the character literal
  return Result;
}

static unique_ptr<ExpressionNode> ParseArrayLiteralExpression() {
  if (ExpectedLiteralType != ValueType::Array)
    return LogError("Array literal requires an expected array type");
  ValueType ElemType = ValueType::Error;
  string ElemStructName;
  uint64_t Count = 0;
  if (!DecodeArrayType(ExpectedLiteralStructName, ElemType, ElemStructName,
                       Count))
    return LogError("Invalid expected array type");

  getNextToken(); // eat '['
  vector<unique_ptr<ExpressionNode>> Elements;
  if (CurrentToken != tok_rbracket) {
    while (true) {
      ExpectedLiteralTypeGuard Guard(ElemType, ElemStructName);
      auto E = ParseExpression();
      if (!E)
        return nullptr;
      if (!IsAssignable(ElemType, E->getType()))
        return LogError("Array literal element type mismatch");
      if ((ElemType == ValueType::Pointer || ElemType == ValueType::Array ||
           ElemType == ValueType::Struct) &&
          ElemStructName != E->getStructName())
        return LogError("Array literal element type mismatch");
      Elements.push_back(std::move(E));
      if (CurrentToken == tok_rbracket)
        break;
      if (CurrentToken != tok_comma)
        return LogError("Expected ']' or ',' in array literal");
      getNextToken();
    }
  }
  getNextToken(); // eat ']'
  if (Elements.size() != Count)
    return LogError("Array literal element count mismatch");
  return make_unique<ArrayLiteralExpressionNode>(std::move(Elements),
                                          ExpectedLiteralStructName);
}

/// type
///   = "int" | "int8" | "int16" | "int32" | "int64"
///   | "float" | "float32" | "float64"
///   | "bool" | "None" ;
///
/// casttype
///   = "int" | "int8" | "int16" | "int32" | "int64"
///   | "float" | "float32" | "float64"
///   | "bool" ;
static ValueType ParseTypeToken(string *StructName) {
  if (StructName)
    StructName->clear();
  ValueType BaseType = ValueType::Error;
  string BaseStructName;
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
      LogError("Expected '[' after ptr");
      return ValueType::Error;
    }
    getNextToken(); // eat '['
    string PointeeStructName;
    ValueType PointeeType = ParseTypeToken(&PointeeStructName);
    if (PointeeType == ValueType::Error)
      return ValueType::Error;
    if (PointeeType == ValueType::None) {
      LogError("Pointers to None are not allowed");
      return ValueType::Error;
    }
    if (PointeeType == ValueType::Pointer) {
      LogError("Nested pointer types are not supported");
      return ValueType::Error;
    }
    if (PointeeType == ValueType::Array) {
      LogError("Pointers to array types are not supported");
      return ValueType::Error;
    }
    if (CurrentToken != tok_rbracket) {
      LogError("Expected ']' after pointer pointee type");
      return ValueType::Error;
    }
    getNextToken(); // eat ']'
    BaseType = ValueType::Pointer;
    BaseStructName = EncodePointerType(PointeeType, PointeeStructName);
    break;
  }
  case tok_name: {
    string TyName = Name;
    if (ActiveTypeParams.count(TyName)) {
      getNextToken();
      if (StructName)
        *StructName = TyName;
      return ValueType::TypeVar;
    }
    auto AliasIt = TypeAliases.find(TyName);
    if (AliasIt != TypeAliases.end()) {
      getNextToken();
      if (StructName)
        *StructName = AliasIt->second.second;
      return AliasIt->second.first;
    }
    if (!StructTypes.count(TyName)) {
      LogError(("Unknown type '" + TyName + "'").c_str());
      return ValueType::Error;
    }
    getNextToken();
    BaseType = ValueType::Struct;
    BaseStructName = TyName;
    break;
  }
  default:
    LogError("Expected a type");
    return ValueType::Error;
  }

  if (CurrentToken == tok_lbracket) {
    if (BaseType == ValueType::None)
      return LogError("Arrays of None are not allowed"), ValueType::Error;
    if (BaseType == ValueType::Array)
      return LogError("Nested array types are not supported"), ValueType::Error;
    getNextToken(); // eat '['
    if (CurrentToken != tok_number || NumberIsFloat)
      return LogError("Array size must be an integer literal"),
             ValueType::Error;
    uint64_t Count = 0;
    if (!ParseUnsignedDecimal(NumberLiteral, Count))
      return LogError("Invalid array size"), ValueType::Error;
    if (Count == 0)
      return LogError("Array size must be > 0"), ValueType::Error;
    getNextToken(); // eat number
    if (CurrentToken != tok_rbracket)
      return LogError("Expected ']' after array size"), ValueType::Error;
    getNextToken(); // eat ']'
    if (StructName)
      *StructName = EncodeArrayType(BaseType, BaseStructName, Count);
    if (CurrentToken == tok_lbracket)
      return LogError("Nested arrays are not supported"), ValueType::Error;
    return ValueType::Array;
  }

  if (StructName)
    *StructName = BaseStructName;
  return BaseType;
}

/// castexpr
///   = casttype "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseCastExpression() {
  string TargetStructName;
  ValueType Type = ParseTypeToken(&TargetStructName);
  if (Type == ValueType::Error)
    return nullptr;
  if (Type == ValueType::None)
    return LogError("Cannot cast to None");
  if (Type == ValueType::Struct)
    return LogError("Cannot cast to struct type");
  if (Type == ValueType::Array)
    return LogError("Cannot cast to array type");
  if (CurrentToken != tok_lparen)
    return LogError("Expected '(' after cast type");
  getNextToken(); // eat '('
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  if (CurrentToken != tok_rparen)
    return LogError("Expected ')' after cast expression");
  getNextToken(); // eat ')'
  if (Type == ValueType::Pointer && Expr->getType() != ValueType::Pointer)
    return LogError("Pointer casts require a pointer operand");
  return make_unique<CastExpressionNode>(Type, std::move(Expr), TargetStructName);
}

static unique_ptr<ExpressionNode> ParseSizeofExpression() {
  getNextToken(); // eat 'sizeof'
  if (CurrentToken != tok_lparen)
    return LogError("Expected '(' after sizeof");
  getNextToken(); // eat '('
  string TargetStructName;
  ValueType TargetType = ParseTypeToken(&TargetStructName);
  if (TargetType == ValueType::Error)
    return nullptr;
  if (TargetType == ValueType::None)
    return LogError("Cannot take sizeof(None)");
  if (CurrentToken != tok_rparen)
    return LogError("Expected ')' after sizeof type");
  getNextToken(); // eat ')'
  return make_unique<SizeofExpressionNode>(TargetType, TargetStructName);
}

static unique_ptr<ExpressionNode> ParseAddrExpression() {
  getNextToken(); // eat 'addr'
  if (CurrentToken != tok_lparen)
    return LogError("Expected '(' after addr");
  getNextToken(); // eat '('
  if (CurrentToken != tok_name)
    return LogError("addr expects an lvalue");
  string BaseName = Name;
  getNextToken(); // eat name
  ValueType CurType = LookupVarType(BaseName);
  if (CurType == ValueType::Error)
    return LogError("Unknown variable name");
  string CurStruct = LookupVarStructName(BaseName);
  vector<string> Path;
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name)
      return LogError("Expected field name after '.'");
    string Field = Name;
    getNextToken(); // eat field
    if (CurType != ValueType::Struct || CurStruct.empty())
      return LogError("Field access requires a struct value");
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end())
      return LogError("Unknown struct type in field access");
    auto FI = SI->second.FieldIndex.find(Field);
    if (FI == SI->second.FieldIndex.end())
      return LogError("Unknown field on struct");
    const auto &FD = SI->second.Fields[FI->second];
    if (!CanAccessClassMember(CurStruct, FD.IsPublic))
      return LogError(
          ("Field '" + Field + "' is private on '" + CurStruct + "'").c_str());
    CurType = FD.Type;
    CurStruct = FD.StructName;
    Path.push_back(Field);
  }
  if (CurrentToken != tok_rparen)
    return LogError("Expected ')' after addr operand");
  getNextToken(); // eat ')'
  return make_unique<AddrExpressionNode>(std::move(BaseName), std::move(Path),
                                  EncodePointerType(CurType, CurStruct));
}

/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogError("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// name-expression
///   = name
///   | call-expression ;
///
/// call-expression
///   = name "(" [ expression { "," expression } ] ")" ;
static unique_ptr<ExpressionNode> ParseNameExpressionWithName(const string &ParsedName) {
  if (CurrentToken != tok_lparen) { // Simple variable ref.
    ValueType Type = LookupVarType(ParsedName);
    if (Type == ValueType::Error) {
      return LogError("Unknown variable name");
    }
    return make_unique<NameExpressionNode>(ParsedName, Type,
                                        LookupVarStructName(ParsedName));
  }

  // Constructor call: ClassName(...)
  auto SI = StructTypes.find(ParsedName);
  if (SI != StructTypes.end() && SI->second.IsClass) {
    getNextToken(); // eat '('
    string InitName = ParsedName + ".__init__";
    FunctionSignatureNode *InitSignature = GetFunctionSignature(InitName);
    if (InitSignature) {
      auto MI = SI->second.MethodIsPublic.find("__init__");
      if (MI != SI->second.MethodIsPublic.end() &&
          !CanAccessClassMember(ParsedName, MI->second)) {
        return LogError(
            ("Method '__init__' is private on '" + ParsedName + "'").c_str());
      }
    }
    vector<unique_ptr<ExpressionNode>> Arguments;
    if (CurrentToken != tok_rparen) {
      size_t ArgIndex = 0;
      while (true) {
        ValueType Expected = ValueType::Error;
        string ExpectedStructName;
        if (InitSignature && ArgIndex + 1 < InitSignature->getNumParameters()) {
          Expected = InitSignature->getParameterType(ArgIndex + 1);
          ExpectedStructName = InitSignature->getParameterStructName(ArgIndex + 1);
        }
        ExpectedLiteralTypeGuard Guard(Expected, ExpectedStructName);
        auto Arg = ParseExpression();
        if (!Arg)
          return nullptr;
        Arguments.push_back(std::move(Arg));
        if (CurrentToken == tok_rparen)
          break;
        if (CurrentToken != tok_comma)
          return LogError("Expected ')' or ',' in argument list");
        getNextToken(); // eat ','
        ++ArgIndex;
      }
    }
    getNextToken(); // eat ')'

    if (InitSignature) {
      size_t ExpectedArgs =
          InitSignature->getNumParameters() > 0 ? InitSignature->getNumParameters() - 1 : 0;
      if (Arguments.size() != ExpectedArgs)
        return LogError("Incorrect # arguments passed");
      for (size_t I = 0; I < Arguments.size(); ++I) {
        ValueType ArgType = Arguments[I]->getType();
        ValueType ParamType = InitSignature->getParameterType(I + 1);
        if (!IsAssignable(ParamType, ArgType))
          return LogError(("argument " + std::to_string(I + 1) + " expects " +
                           TypeName(ParamType))
                              .c_str());
        if ((ParamType == ValueType::Pointer ||
             ParamType == ValueType::Struct || ParamType == ValueType::Array) &&
            InitSignature->getParameterStructName(I + 1) != Arguments[I]->getStructName())
          return LogError(("argument " + std::to_string(I + 1) + " expects " +
                           TypeName(ParamType))
                              .c_str());
      }
    } else if (!Arguments.empty()) {
      return LogError(
          ("Class '" + ParsedName + "' has no constructor; expected zero arguments")
              .c_str());
    }
    return make_unique<ConstructorCallExpressionNode>(ParsedName, std::move(Arguments));
  }

  // Function call.
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
      string ExpectedStructName;
      if (Signature && ArgIndex < Signature->getNumParameters())
        Expected = Signature->getParameterType(ArgIndex);
      if (Signature && ArgIndex < Signature->getNumParameters())
        ExpectedStructName = Signature->getParameterStructName(ArgIndex);
      {
        ExpectedLiteralTypeGuard Guard(Expected, ExpectedStructName);
        if (auto Arg = ParseExpression())
          Arguments.push_back(std::move(Arg));
        else
          return nullptr;
      }

      if (CurrentToken == tok_rparen)
        break;

      if (CurrentToken != tok_comma)
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
      ++ArgIndex;
    }
  }

  // Eat the ')'.
  getNextToken();

  if (!Signature)
    return LogError("Unknown function referenced");
  if (Signature->getNumParameters() != Arguments.size())
    return LogError("Incorrect # arguments passed");

  for (size_t i = 0; i < Arguments.size(); ++i) {
    ValueType ArgType = Arguments[i]->getType();
    ValueType ParamType = Signature->getParameterType(i);
    if (ParamType == ValueType::Pointer && ArgType == ValueType::Array) {
      if (!ArrayDecaysToPointerType(Arguments[i]->getStructName(),
                                    Signature->getParameterStructName(i))) {
        return LogError(("argument " + std::to_string(i + 1) + " expects " +
                         TypeName(ParamType))
                            .c_str());
      }
      continue;
    }
    if (!IsAssignable(ParamType, ArgType)) {
      return LogError(("argument " + std::to_string(i + 1) + " expects " +
                       TypeName(ParamType))
                          .c_str());
    }
    if (ParamType == ValueType::Pointer &&
        Signature->getParameterStructName(i) != Arguments[i]->getStructName()) {
      return LogError(("argument " + std::to_string(i + 1) + " expects " +
                       TypeName(ParamType))
                          .c_str());
    }
  }

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments),
                                  Signature->getReturnType(),
                                  Signature->getReturnStructName());
}

static unique_ptr<ExpressionNode> ParseMethodCallExpression(unique_ptr<ExpressionNode> Receiver,
                                               const string &MethodName) {
  if (!Receiver || Receiver->getType() != ValueType::Struct)
    return LogError("Method call base must be a class/struct value");
  string ClassName = Receiver->getStructName();
  if (ClassName.empty())
    return LogError("Method call base must be a class/struct value");
  auto CI = StructTypes.find(ClassName);
  if (CI == StructTypes.end())
    return LogError("Unknown class/struct type in method call");
  auto MI = CI->second.MethodIsPublic.find(MethodName);
  if (MI != CI->second.MethodIsPublic.end() &&
      !CanAccessClassMember(ClassName, MI->second)) {
    return LogError(
        ("Method '" + MethodName + "' is private on '" + ClassName + "'")
            .c_str());
  }
  string CalleeName = ClassName + "." + MethodName;
  FunctionSignatureNode *Signature = GetFunctionSignature(CalleeName);
  if (!Signature)
    return LogError(
        ("Unknown method '" + MethodName + "' on '" + ClassName + "'").c_str());

  getNextToken(); // eat '('
  vector<unique_ptr<ExpressionNode>> Arguments;
  // implicit self: pass receiver address
  if (auto *Var = dynamic_cast<NameExpressionNode *>(Receiver.get())) {
    Arguments.push_back(make_unique<AddrExpressionNode>(
        Var->getName(), vector<string>{},
        EncodePointerType(ValueType::Struct, Var->getStructName())));
  } else if (auto *Field = dynamic_cast<FieldExpressionNode *>(Receiver.get())) {
    // FieldExpressionNode always models an lvalue rooted at a base variable name.
    const string *BaseName = Field->getLValueName();
    if (!BaseName)
      return LogError("Method call base must be an lvalue");
    Arguments.push_back(make_unique<AddrExpressionNode>(
        *BaseName, Field->getFieldPath(),
        EncodePointerType(ValueType::Struct, Field->getStructName())));
  } else {
    return LogError("Method call base must be an lvalue");
  }
  if (CurrentToken != tok_rparen) {
    size_t ArgIndex = 1; // skip implicit self
    while (true) {
      ValueType Expected = ValueType::Error;
      string ExpectedStructName;
      if (ArgIndex < Signature->getNumParameters()) {
        Expected = Signature->getParameterType(ArgIndex);
        ExpectedStructName = Signature->getParameterStructName(ArgIndex);
      }
      ExpectedLiteralTypeGuard Guard(Expected, ExpectedStructName);
      auto Arg = ParseExpression();
      if (!Arg)
        return nullptr;
      Arguments.push_back(std::move(Arg));
      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogError("Expected ')' or ',' in argument list");
      getNextToken(); // eat ','
      ++ArgIndex;
    }
  }
  getNextToken(); // eat ')'

  if (Arguments.size() != Signature->getNumParameters())
    return LogError("Incorrect # arguments passed");
  for (size_t I = 0; I < Arguments.size(); ++I) {
    ValueType ArgType = Arguments[I]->getType();
    ValueType ParamType = Signature->getParameterType(I);
    if (ParamType == ValueType::Pointer && ArgType == ValueType::Array) {
      if (!ArrayDecaysToPointerType(Arguments[I]->getStructName(),
                                    Signature->getParameterStructName(I)))
        return LogError("Argument type mismatch");
      continue;
    }
    if (!IsAssignable(ParamType, ArgType))
      return LogError("Argument type mismatch");
    if ((ParamType == ValueType::Pointer || ParamType == ValueType::Struct ||
         ParamType == ValueType::Array) &&
        Signature->getParameterStructName(I) != Arguments[I]->getStructName())
      return LogError("Argument type mismatch");
  }

  return make_unique<CallExpressionNode>(CalleeName, std::move(Arguments),
                                  Signature->getReturnType(),
                                  Signature->getReturnStructName());
}

static unique_ptr<FieldExpressionNode> ParseFieldAccessExpression(string BaseName,
                                                     ValueType BaseType,
                                                     string BaseStructName) {
  vector<string> Path;
  ValueType CurType = BaseType;
  string CurStruct = std::move(BaseStructName);
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name) {
      LogError("Expected field name after '.'");
      return nullptr;
    }
    string Field = Name;
    getNextToken(); // eat field name
    if (CurType == ValueType::Pointer) {
      ValueType PointeeType = ValueType::Error;
      string PointeeStruct;
      if (!DecodePointerType(CurStruct, PointeeType, PointeeStruct) ||
          PointeeType != ValueType::Struct)
        return LogError("Field access requires a struct value"), nullptr;
      CurType = ValueType::Struct;
      CurStruct = PointeeStruct;
    }
    if (CurType != ValueType::Struct || CurStruct.empty()) {
      LogError("Field access requires a struct value");
      return nullptr;
    }
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end()) {
      LogError("Unknown struct type in field access");
      return nullptr;
    }
    auto FI = SI->second.FieldIndex.find(Field);
    if (FI == SI->second.FieldIndex.end()) {
      LogError(("Unknown field '" + Field + "' on struct '" + CurStruct + "'")
                   .c_str());
      return nullptr;
    }
    const auto &FD = SI->second.Fields[FI->second];
    if (!CanAccessClassMember(CurStruct, FD.IsPublic)) {
      LogError(
          ("Field '" + Field + "' is private on '" + CurStruct + "'").c_str());
      return nullptr;
    }
    CurType = FD.Type;
    CurStruct = FD.StructName;
    Path.push_back(Field);
  }
  return make_unique<FieldExpressionNode>(std::move(BaseName), std::move(Path),
                                   CurType, CurStruct);
}

static unique_ptr<FieldExpressionNode>
ParseFieldAccessFromFirstMember(string BaseName, ValueType BaseType,
                                string BaseStructName,
                                const string &FirstMember) {
  vector<string> Path;
  ValueType CurType = BaseType;
  string CurStruct = std::move(BaseStructName);
  auto ConsumeField = [&](const string &Field) -> bool {
    if (CurType == ValueType::Pointer) {
      ValueType PointeeType = ValueType::Error;
      string PointeeStruct;
      if (!DecodePointerType(CurStruct, PointeeType, PointeeStruct) ||
          PointeeType != ValueType::Struct) {
        LogError("Field access requires a struct value");
        return false;
      }
      CurType = ValueType::Struct;
      CurStruct = PointeeStruct;
    }
    if (CurType != ValueType::Struct || CurStruct.empty()) {
      LogError("Field access requires a struct value");
      return false;
    }
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end()) {
      LogError("Unknown struct type in field access");
      return false;
    }
    auto FI = SI->second.FieldIndex.find(Field);
    if (FI == SI->second.FieldIndex.end()) {
      LogError(("Unknown field '" + Field + "' on struct '" + CurStruct + "'")
                   .c_str());
      return false;
    }
    const auto &FD = SI->second.Fields[FI->second];
    if (!CanAccessClassMember(CurStruct, FD.IsPublic)) {
      LogError(
          ("Field '" + Field + "' is private on '" + CurStruct + "'").c_str());
      return false;
    }
    CurType = FD.Type;
    CurStruct = FD.StructName;
    Path.push_back(Field);
    return true;
  };

  if (!ConsumeField(FirstMember))
    return nullptr;
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name) {
      LogError("Expected field name after '.'");
      return nullptr;
    }
    string Field = Name;
    getNextToken(); // eat field
    if (!ConsumeField(Field))
      return nullptr;
  }
  return make_unique<FieldExpressionNode>(std::move(BaseName), std::move(Path),
                                   CurType, CurStruct);
}

static unique_ptr<ExpressionNode> ParseIndexExpression(string BaseName,
                                          vector<string> FieldPath,
                                          ValueType BaseType,
                                          const string &BaseStructName) {
  if (BaseType != ValueType::Pointer && BaseType != ValueType::Array)
    return LogError("Indexing requires a pointer or array value");
  getNextToken(); // eat '['
  auto Index = ParseExpression();
  if (!Index)
    return nullptr;
  if (!IsIntType(Index->getType()))
    return LogError("Index must be an integer");
  if (CurrentToken != tok_rbracket)
    return LogError("Expected ']' after index expression");
  getNextToken(); // eat ']'
  ValueType ElemType = ValueType::Error;
  string ElemStruct;
  if (BaseType == ValueType::Pointer) {
    if (!DecodePointerType(BaseStructName, ElemType, ElemStruct))
      return LogError("Invalid pointer type metadata");
  } else {
    uint64_t Count = 0;
    if (!DecodeArrayType(BaseStructName, ElemType, ElemStruct, Count))
      return LogError("Invalid array type metadata");
  }
  return make_unique<IndexExpressionNode>(std::move(BaseName), std::move(FieldPath),
                                   std::move(Index), ElemType, ElemStruct);
}

static unique_ptr<ExpressionNode>
ParseIndexedFieldAccessExpression(unique_ptr<IndexExpressionNode> BaseIndex) {
  ValueType CurType = BaseIndex->getType();
  string CurStruct = BaseIndex->getStructName();
  vector<string> Path;
  while (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name)
      return LogError("Expected field name after '.'");
    string Field = Name;
    getNextToken(); // eat field
    if (CurType != ValueType::Struct || CurStruct.empty())
      return LogError("Field access requires a struct value");
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end())
      return LogError("Unknown struct type in field access");
    auto FI = SI->second.FieldIndex.find(Field);
    if (FI == SI->second.FieldIndex.end())
      return LogError(
          ("Unknown field '" + Field + "' on struct '" + CurStruct + "'")
              .c_str());
    const auto &FD = SI->second.Fields[FI->second];
    CurType = FD.Type;
    CurStruct = FD.StructName;
    Path.push_back(Field);
  }
  return make_unique<IndexedFieldExpressionNode>(std::move(BaseIndex), std::move(Path),
                                          CurType, CurStruct);
}

static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;

  getNextToken(); // eat name.

  auto Base = ParseNameExpressionWithName(ParsedName);
  if (!Base)
    return nullptr;

  if (CurrentToken == tok_dot) {
    getNextToken(); // eat '.'
    if (CurrentToken != tok_name)
      return LogError("Expected field or method name after '.'");
    string MemberName = Name;
    getNextToken(); // eat member name
    if (CurrentToken == tok_lparen) {
      Base = ParseMethodCallExpression(std::move(Base), MemberName);
      if (!Base)
        return nullptr;
    } else {
      auto *Var = dynamic_cast<NameExpressionNode *>(Base.get());
      if (!Var)
        return LogError("Field access base must be a variable");
      auto Field = ParseFieldAccessFromFirstMember(
          Var->getName(), Var->getType(), Var->getStructName(), MemberName);
      if (!Field)
        return LogError("Invalid field access");
      Base = std::move(Field);
    }
  }
  if (CurrentToken == tok_lbracket) {
    if (auto *Var = dynamic_cast<NameExpressionNode *>(Base.get())) {
      Base = ParseIndexExpression(Var->getName(), {}, Var->getType(),
                            Var->getStructName());
    } else if (auto *Field = dynamic_cast<FieldExpressionNode *>(Base.get())) {
      Base = ParseIndexExpression(*Field->getLValueName(), Field->getFieldPath(),
                            Field->getType(), Field->getStructName());
    } else {
      return LogError("Indexing requires a pointer or array value");
    }
    if (!Base)
      return nullptr;
  }
  if (CurrentToken == tok_dot) {
    auto *Idx = dynamic_cast<IndexExpressionNode *>(Base.get());
    if (!Idx)
      return LogError("Field access base must be a struct value");
    auto Owned = std::unique_ptr<IndexExpressionNode>(Idx);
    Base.release();
    Base = ParseIndexedFieldAccessExpression(std::move(Owned));
    if (!Base)
      return nullptr;
  }
  return Base;
}

// ParseForParts - Parse the "= start, cond, step : suite" tail of a for-loop.
// Also validates the parts against VarType (start/step assignable, cond bool).
// Returns true on success and fills Start/Cond/Step/Body.
static bool ParseForParts(ValueType VarType, unique_ptr<ExpressionNode> &Start,
                          unique_ptr<ExpressionNode> &Cond, unique_ptr<ExpressionNode> &Step,
                          unique_ptr<ExpressionNode> &Body) {
  if (CurrentToken != tok_equal)
    return LogError("Expected '=' after for variable"), false;
  getNextToken(); // eat '='

  Start = ParseExpression();
  if (!Start)
    return false;
  if (!IsAssignable(VarType, Start->getType()))
    return LogError("For loop start must match loop variable type"), false;
  if (!IsNumericType(VarType))
    return LogError("For loop variable must be numeric"), false;

  if (CurrentToken != tok_comma)
    return LogError("Expected ',' after for start value"), false;
  getNextToken(); // eat ','

  Cond = ParseExpression();
  if (!Cond)
    return false;
  if (Cond->getType() != ValueType::Bool)
    return LogError("For loop condition must be bool"), false;

  if (CurrentToken != tok_comma)
    return LogError("Expected ',' after for condition"), false;
  getNextToken(); // eat ','

  Step = ParseExpression();
  if (!Step)
    return false;
  if (!IsAssignable(VarType, Step->getType()))
    return LogError("For loop step must match loop variable type"), false;

  if (CurrentToken != tok_colon)
    return LogError("Expected ':' after for step"), false;
  getNextToken(); // eat ':'

  // Parse the suite after ':' (inline statement or indented block).
  Body = ParseSuite();
  if (!Body)
    return false;

  return true;
}

/// forstmt  = "for"
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
    return LogError("Expected name after 'for'");
  string VarName = Name;
  getNextToken(); // eat name

  ValueType VarType = ValueType::Error;
  if (IsVarDecl) {
    if (CurrentToken != tok_colon)
      return LogError(
          "For loop variable requires a type annotation (e.g., ': int')");
    getNextToken(); // eat ':'
    VarType = ParseTypeToken();
    if (VarType == ValueType::Error)
      return nullptr;
    if (VarType == ValueType::None)
      return LogError("For loop variable cannot have None type");
    if (IsDeclaredInCurrentScope(VarName))
      return LogError(
          ("Variable '" + VarName + "' already declared in this scope")
              .c_str());
  } else {
    if (CurrentToken == tok_colon)
      return LogError("For loop variable requires 'var' to declare a type");
    VarType = LookupVarType(VarName);
    if (VarType == ValueType::Error)
      return LogError("Assignment to undeclared variable");
  }

  unique_ptr<ExpressionNode> Start, Cond, Step, Body;
  ParseLoopGuard LoopGuard;

  if (IsVarDecl) {
    LoopScopeGuard LoopScope(VarName, VarType);
    if (!ParseForParts(VarType, Start, Cond, Step, Body))
      return nullptr;
  } else {
    if (!ParseForParts(VarType, Start, Cond, Step, Body))
      return nullptr;
  }
  return make_unique<ForExpressionNode>(VarName, IsVarDecl, VarType, std::move(Start),
                                 std::move(Cond), std::move(Step),
                                 std::move(Body));
}

/// whilestmt = "while" expression ":" suite ;
static unique_ptr<ExpressionNode> ParseWhileStatement() {
  getNextToken(); // eat 'while'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogError("While loop condition must be bool");
  if (CurrentToken != tok_colon)
    return LogError("Expected ':' after while condition");
  getNextToken(); // eat ':'
  ParseLoopGuard LoopGuard;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  return make_unique<WhileExpressionNode>(std::move(Cond), std::move(Body),
                                   /*IsDoWhile=*/false);
}

/// dowhilestmt = "do" ":" suite end-of-lines "while" expression ;
static unique_ptr<ExpressionNode> ParseDoWhileStatement() {
  getNextToken(); // eat 'do'
  if (CurrentToken != tok_colon)
    return LogError("Expected ':' after 'do'");
  getNextToken(); // eat ':'
  ParseLoopGuard LoopGuard;
  auto Body = ParseSuite();
  if (!Body)
    return nullptr;
  if (CurrentToken == tok_block_end)
    getNextToken();
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_while)
    return LogError("Expected 'while' after do-body");
  getNextToken(); // eat 'while'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogError("Do-while condition must be bool");
  return make_unique<WhileExpressionNode>(std::move(Cond), std::move(Body),
                                   /*IsDoWhile=*/true);
}

static bool ParseSwitchCaseValue(int64_t &Out) {
  bool Neg = false;
  if (CurrentToken == tok_minus) {
    Neg = true;
    getNextToken();
  }
  uint64_t Raw = 0;
  if (CurrentToken == tok_char) {
    // Character literals ('a', '\n', ...) are valid case values too — they
    // are just integer constants under the hood (see CharLiteralValue).
    Raw = static_cast<uint64_t>(CharLiteralValue);
    getNextToken(); // eat character literal
  } else if (CurrentToken == tok_number && !NumberIsFloat) {
    if (!ParseUnsignedDecimal(NumberLiteral, Raw))
      return LogError("Invalid switch case value"), false;
    getNextToken(); // eat number
  } else {
    return LogError(
               "Switch case value must be an integer or character literal"),
           false;
  }
  if (Neg) {
    if (Raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ULL)
      return LogError("Switch case value out of range"), false;
    Out = static_cast<int64_t>(0) - static_cast<int64_t>(Raw);
  } else {
    if (Raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return LogError("Switch case value out of range"), false;
    Out = static_cast<int64_t>(Raw);
  }
  return true;
}

static unique_ptr<ExpressionNode> ParseSwitchStatement() {
  getNextToken(); // eat 'switch'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (!IsIntType(Cond->getType()))
    return LogError("Switch condition must be an integer type");
  if (CurrentToken != tok_colon)
    return LogError("Expected ':' after switch expression");
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent)
    return LogError("Expected an indented switch body");
  getNextToken(); // eat INDENT

  ParseSwitchGuard SwitchGuard;
  vector<pair<vector<int64_t>, unique_ptr<ExpressionNode>>> Cases;
  std::set<int64_t> SeenCaseValues;
  unique_ptr<ExpressionNode> DefaultCase;

  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_case) {
      getNextToken(); // eat 'case'
      // A case may list several comma-separated values that all share the
      // body that follows, e.g. "case 'a', 'e', 'i', 'o', 'u':".
      vector<int64_t> CaseVals;
      while (true) {
        int64_t CaseVal = 0;
        if (!ParseSwitchCaseValue(CaseVal))
          return nullptr;
        if (!SeenCaseValues.insert(CaseVal).second)
          return LogError("Duplicate switch case value");
        CaseVals.push_back(CaseVal);
        if (CurrentToken != tok_comma)
          break;
        getNextToken(); // eat ',' and parse the next case value
      }
      if (CurrentToken != tok_colon)
        return LogError("Expected ':' after case value");
      getNextToken(); // eat ':'
      auto Body = ParseSuite();
      if (!Body)
        return nullptr;
      Cases.emplace_back(std::move(CaseVals), std::move(Body));
    } else if (CurrentToken == tok_default) {
      if (DefaultCase)
        return LogError("Duplicate default case");
      getNextToken(); // eat 'default'
      if (CurrentToken != tok_colon)
        return LogError("Expected ':' after default");
      getNextToken(); // eat ':'
      DefaultCase = ParseSuite();
      if (!DefaultCase)
        return nullptr;
    } else {
      return LogError("Expected 'case' or 'default' in switch body");
    }
    if (CurrentToken == tok_block_end)
      getNextToken();
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }
  if (CurrentToken != tok_dedent)
    return LogError("Expected dedent after switch body");
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end
  return make_unique<SwitchExpressionNode>(std::move(Cond), std::move(Cases),
                                    std::move(DefaultCase));
}

/// varstmt
///   = "var" varbinding { "," varbinding } ;
///
/// varbinding
///   = name ":" type [ "=" expression ] ;
static unique_ptr<ExpressionNode> ParseVarStatement() {
  getNextToken(); // eat 'var'

  vector<VarBinding> VarNames;
  bool IsGlobalDecl = ParsingTopLevel;

  while (true) {
    // name ":" type
    if (CurrentToken != tok_name)
      return LogError("Expected name after 'var'");

    string ParsedName = Name;
    getNextToken(); // eat name

    if (CurrentToken != tok_colon)
      return LogError(
          "Variable declaration requires a type annotation (e.g., ': int32')");
    getNextToken(); // eat ':'
    string DeclStructName;
    ValueType DeclType = ParseTypeToken(&DeclStructName);
    if (DeclType == ValueType::Error)
      return nullptr;
    if (DeclType == ValueType::None)
      return LogError("Variables cannot have None type");

    if (IsGlobalDecl) {
      if (GlobalVarDecls.count(ParsedName))
        return LogError(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    } else {
      if (IsDeclaredInCurrentScope(ParsedName))
        return LogError(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    }

    unique_ptr<ExpressionNode> Init;
    // [ "=" expression ]
    if (CurrentToken == tok_equal) {
      getNextToken(); // eat '='
      ExpectedLiteralTypeGuard Guard(DeclType, DeclStructName);
      Init = ParseExpression();
      if (!Init)
        return nullptr;
      bool ExactArrayInit =
          (DeclType == ValueType::Array &&
           Init->getType() == ValueType::Array &&
           DeclStructName == Init->getStructName() &&
           dynamic_cast<ArrayLiteralExpressionNode *>(Init.get()) != nullptr);
      if (!ExactArrayInit && !IsAssignable(DeclType, Init->getType()))
        return LogError("Type mismatch in variable initialization");
      if ((DeclType == ValueType::Pointer || DeclType == ValueType::Array) &&
          DeclStructName != Init->getStructName() && !ExactArrayInit)
        return LogError("Type mismatch in variable initialization");
    } else {
      if (DeclType != ValueType::Struct && DeclType != ValueType::Array) {
        Init = MakeZeroLiteral(DeclType);
        if (!Init)
          return nullptr;
      }
    }

    VarNames.push_back({ParsedName, DeclType, DeclStructName, std::move(Init)});
    if (IsGlobalDecl) {
      GlobalVarTypes[ParsedName] = DeclType;
      GlobalVarDecls.insert(ParsedName);
      if (DeclType == ValueType::Struct || DeclType == ValueType::Pointer ||
          DeclType == ValueType::Array)
        GlobalVarStructTypes[ParsedName] = DeclStructName;
    } else {
      DeclareVar(ParsedName, DeclType, DeclStructName);
    }

    if (CurrentToken != tok_comma)
      break;
    getNextToken(); // eat ','
  }

  return make_unique<VarStatementNode>(std::move(VarNames));
}

/// ifstmt
///   = "if" expression ":" suite
///     { end-of-lines "elif" expression ":" suite }
///     [ end-of-lines "else" ":" suite ] ;
static unique_ptr<ExpressionNode> ParseIfStatement() {
  getNextToken(); // eat 'if'
  vector<pair<unique_ptr<ExpressionNode>, unique_ptr<ExpressionNode>>> Branches;
  bool LastBranchWasBlock = false;

  while (true) {
    auto Cond = ParseExpression();
    if (!Cond)
      return nullptr;
    if (Cond->getType() != ValueType::Bool)
      return LogError("If condition must be bool");

    if (CurrentToken != tok_colon)
      return LogError("Expected ':' after if/elif condition");
    getNextToken(); // eat ':'

    auto Body = ParseSuite();
    if (!Body)
      return nullptr;
    LastBranchWasBlock = (CurrentToken == tok_block_end);
    if (LastBranchWasBlock)
      getNextToken();
    Branches.push_back({std::move(Cond), std::move(Body)});

    consumeNewlines();
    if (CurrentToken != tok_elif)
      break;
    getNextToken(); // eat 'elif'
  }

  unique_ptr<ExpressionNode> Else;
  if (CurrentToken == tok_else) {
    getNextToken(); // eat 'else'
    if (CurrentToken != tok_colon)
      return LogError("Expected ':' after else");
    getNextToken(); // eat ':'
    Else = ParseSuite();
    if (!Else)
      return nullptr;
  } else if (LastBranchWasBlock) {
    // No else: restore the synthetic separator for the enclosing block/top level.
    PendingTokens.push_front(CurrentToken);
    CurrentToken = tok_block_end;
  }

  // Lower if/elif chain to nested IfStatementNode in else branch.
  unique_ptr<ExpressionNode> Tree = std::move(Else);
  for (auto It = Branches.rbegin(); It != Branches.rend(); ++It) {
    Tree = make_unique<IfStatementNode>(std::move(It->first), std::move(It->second),
                                  std::move(Tree));
  }
  return Tree;
}

static unique_ptr<ExpressionNode>
ParseUnary(); // forward declaration for ParseUnaryMinus

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

static bool IsLogicalOp(int Operator) { return Operator == tok_and || Operator == tok_or; }

static bool IsBitwiseOp(int Operator) { return Operator == tok_ampersand || Operator == tok_pipe || Operator == tok_caret; }
static bool IsShiftOp(int Operator) { return Operator == tok_shl || Operator == tok_shr; }

static bool IsArithmeticOp(int Operator) {
  return Operator == tok_plus || Operator == tok_minus || Operator == tok_star || Operator == tok_slash || Operator == tok_percent;
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
// User-defined binary ops:
// - float64 op float64                         -> float64
// - otherwise                                  -> Error
static ValueType GetBinaryResultType(int Operator, ValueType L, const string &LStruct,
                                     ValueType R, const string &RStruct,
                                     string *ResultStructName = nullptr) {
  if (ResultStructName)
    ResultStructName->clear();
  if (IsArithmeticOp(Operator)) {
    if ((Operator == tok_plus || Operator == tok_minus) &&
        ((L == ValueType::Pointer && IsIntType(R)) ||
         (R == ValueType::Pointer && IsIntType(L)))) {
      if (ResultStructName)
        *ResultStructName = (L == ValueType::Pointer) ? LStruct : RStruct;
      return ValueType::Pointer;
    }
    if (Operator == tok_minus && L == ValueType::Pointer && R == ValueType::Pointer &&
        LStruct == RStruct)
      return ValueType::Int64;
    if (Operator == tok_percent && (!IsIntType(L) || !IsIntType(R)))
      return ValueType::Error;
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
    if (L == ValueType::Pointer && R == ValueType::Pointer &&
        LStruct == RStruct)
      return ValueType::Bool;
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
  // User-defined operators are float64-only.
  if (L == ValueType::Float64 && R == ValueType::Float64)
    return ValueType::Float64;
  return ValueType::Error;
}

/// unaryminus
///   = "-" unaryexpr ;
/// Parse built-in unary minus into a UnaryExpressionNode with opcode '-'.
/// The operand is a full unaryexpr so unary chains work naturally
/// (e.g. -!x, --x, -(x+1)).
static unique_ptr<ExpressionNode> ParseUnaryMinus() {
  getNextToken(); // eat '-'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  if (!IsNumericType(Operand->getType()))
    return LogError("Unary '-' requires a numeric operand");
  return make_unique<UnaryExpressionNode>(tok_minus, std::move(Operand), Operand->getType());
}

static bool IsIncDecAssignableExpr(const ExpressionNode *E) {
  return dynamic_cast<const NameExpressionNode *>(E) ||
         dynamic_cast<const FieldExpressionNode *>(E) ||
         dynamic_cast<const IndexExpressionNode *>(E) ||
         dynamic_cast<const IndexedFieldExpressionNode *>(E);
}

static unique_ptr<ExpressionNode> ParsePostfixIncDec(unique_ptr<ExpressionNode> Base) {
  while (CurrentToken == tok_plusplus || CurrentToken == tok_minusminus) {
    bool IsIncrement = (CurrentToken == tok_plusplus);
    if (!IsIncDecAssignableExpr(Base.get()))
      return LogError("Increment/decrement target must be assignable");
    if (!IsNumericType(Base->getType()) &&
        Base->getType() != ValueType::Pointer)
      return LogError("Increment/decrement requires numeric or pointer type");
    ValueType T = Base->getType();
    string S = Base->getStructName();
    getNextToken(); // eat ++/--
    Base = make_unique<IncDecExpressionNode>(std::move(Base), IsIncrement,
                                      /*IsPrefix=*/false, T, S);
  }
  return Base;
}

/// primary
///   = castexpr
///   | name-expression
///   | number-expression
///   | bool_literal
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  default:
    return LogError("unknown token when expecting an expression");
  case tok_name:
    return ParseNameExpression();
  case tok_number:
    return ParseNumberExpression();
  case tok_char:
    return ParseCharExpression();
  case tok_string: {
    string S = StringLiteralStr;
    getNextToken();
    return make_unique<StringExpressionNode>(std::move(S),
                                      EncodePointerType(ValueType::Int8, ""));
  }
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
  case tok_lbracket:
    return ParseArrayLiteralExpression();
  case tok_addr:
    return ParseAddrExpression();
  case tok_lparen:
    return ParseParenthesizedExpression();
  }
}

/// unaryexpr
///   = unaryop unaryexpr
///   | primary ;
///
/// unaryop
///   = "-"
///   | userdefunaryop ;
///
/// Parsing strategy:
/// 1) If the token starts a primary, parse primary.
/// 2) If we see '-', parse built-in unary minus.
/// 3) Else treat the token as a user-defined unary operator and recurse for
///    its operand.
///
/// This is called from both ParseExpression (as the Left seed) and from
/// ParseBinaryOperatorRight (as the Right of a binary operator), so user-defined unary ops
/// work in both positions: !x + 1 and f(x) + !y.
static unique_ptr<ExpressionNode> ParseUnary() {
  if (CurrentToken == tok_plusplus || CurrentToken == tok_minusminus) {
    bool IsIncrement = (CurrentToken == tok_plusplus);
    getNextToken(); // eat ++/--
    auto Operand = ParseUnary();
    if (!Operand)
      return nullptr;
    if (!IsIncDecAssignableExpr(Operand.get()))
      return LogError("Increment/decrement target must be assignable");
    if (!IsNumericType(Operand->getType()) &&
        Operand->getType() != ValueType::Pointer)
      return LogError("Increment/decrement requires numeric or pointer type");
    return make_unique<IncDecExpressionNode>(std::move(Operand), IsIncrement,
                                      /*IsPrefix=*/true, Operand->getType(),
                                      Operand->getStructName());
  }

  // Primary starters will be handled with ParsePrimary.
  if (!isascii(CurrentToken) /* multi-character tokens */ || CurrentToken == tok_lparen ||
      CurrentToken == tok_lbracket || isalpha(CurrentToken) || isdigit(CurrentToken))
    return ParsePostfixIncDec(ParsePrimary());

  // Built-in unary minus.
  if (CurrentToken == tok_minus)
    return ParseUnaryMinus();

  if (CurrentToken == tok_tilde) {
    getNextToken(); // eat '~'
    auto Operand = ParseUnary();
    if (!Operand)
      return nullptr;
    if (!IsIntType(Operand->getType()))
      return LogError("Unary '~' requires an integer operand");
    return make_unique<UnaryExpressionNode>(tok_tilde, std::move(Operand),
                                     Operand->getType());
  }

  // Built-in logical not for bool operands. Non-bool '!' continues to resolve
  // through user-defined unary operators for backward compatibility.
  if (CurrentToken == tok_exclamation) {
    getNextToken(); // eat '!'
    auto Operand = ParseUnary();
    if (!Operand)
      return nullptr;
    if (Operand->getType() == ValueType::Bool)
      return make_unique<LogicalNotExpressionNode>(std::move(Operand));
    auto Signature = GetFunctionSignature("unary!");
    if (!Signature)
      return LogError("Unknown unary operator");
    if (Signature->getNumParameters() != 1)
      return LogError("Unary operator must have exactly one argument");
    ValueType ParamType = Signature->getParameterType(0);
    if (!IsAssignable(ParamType, Operand->getType())) {
      return LogError(
          ("unary operator expects " + string(TypeName(ParamType))).c_str());
    }
    return make_unique<UnaryExpressionNode>(tok_exclamation, std::move(Operand),
                                     Signature->getReturnType());
  }

  // It's an ASCII punctuation character — treat it as a user-defined unary op.
  int Opc = CurrentToken;
  getNextToken(); // eat the operator character
  if (auto Operand = ParseUnary()) {
    auto Signature = GetFunctionSignature(string("unary") + (char)Opc);
    if (!Signature)
      return LogError("Unknown unary operator");
    if (Signature->getNumParameters() != 1)
      return LogError("Unary operator must have exactly one argument");
    ValueType ParamType = Signature->getParameterType(0);
    if (!IsAssignable(ParamType, Operand->getType())) {
      return LogError(
          ("unary operator expects " + string(TypeName(ParamType))).c_str());
    }
    return make_unique<UnaryExpressionNode>(Opc, std::move(Operand),
                                     Signature->getReturnType());
  }
  return nullptr;
}

/// binary-operator-right
///   = { binary-operator unaryexpr } ;
static unique_ptr<ExpressionNode> ParseBinaryOperatorRight(int MinimumPrecedence,
                                         unique_ptr<ExpressionNode> Left) {
  // If this is a binary operator, find its precedence.
  while (true) {
    int TokenPrecedence = GetTokenPrecedence();

    // If this is a binary operator that binds at least as tightly as the current binary operator,
    // consume it, otherwise we are done.
    if (TokenPrecedence < MinimumPrecedence)
      return Left;

    // Okay, we know this is a binary operator and that binds at least as tightly as the
    // current binary operator.
    int Operator = CurrentToken;
    getNextToken(); // eat binary operator

    // Parse the unary expression after the binary operator.  Using ParseUnary
    // here (rather than ParsePrimary directly) means unary operators bind
    // tighter than any binary operator, matching normal convention.
    auto Right = ParseUnary();
    if (!Right)
      return nullptr;

    // If Operator binds less tightly with Right than the operator after Right, let
    // the pending operator take Right as its Left.
    int NextTokenPrecedence = GetTokenPrecedence();
    if (TokenPrecedence < NextTokenPrecedence) {
      Right = ParseBinaryOperatorRight(TokenPrecedence + 1, std::move(Right));
      if (!Right)
        return nullptr;
    }

    ValueType ResultType = ValueType::Error;
    if (IsComparisonOp(Operator) || IsArithmeticOp(Operator) || IsLogicalOp(Operator) ||
        IsBitwiseOp(Operator) || IsShiftOp(Operator)) {
      string ResultStructName;
      ResultType = GetBinaryResultType(Operator, Left->getType(),
                                       Left->getStructName(), Right->getType(),
                                       Right->getStructName(), &ResultStructName);
      if (ResultType == ValueType::Error)
        return LogError("Type mismatch in binary operator");
      Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left), std::move(Right),
                                       ResultType, ResultStructName);
      continue;
    } else {
      auto Signature = GetFunctionSignature(string("binary") + (char)Operator);
      if (!Signature)
        return LogError("Unknown binary operator");
      if (Signature->getNumParameters() != 2)
        return LogError("Binary operator must have exactly two arguments");
      ValueType LType = Signature->getParameterType(0);
      ValueType RType = Signature->getParameterType(1);
      if (!IsAssignable(LType, Left->getType()))
        return LogError(("binary operator expects " + string(TypeName(LType)) +
                         " for left operand")
                            .c_str());
      if (!IsAssignable(RType, Right->getType()))
        return LogError(("binary operator expects " + string(TypeName(RType)) +
                         " for right operand")
                            .c_str());
      ResultType = Signature->getReturnType();
    }

    // Merge Left/Right.
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left), std::move(Right),
                                     ResultType);
  }
}

/// expression
///   = unaryexpr binary-operator-right ;
static unique_ptr<ExpressionNode> ParseExpression() {
  auto Left = ParseUnary();
  if (!Left)
    return nullptr;

  return ParseBinaryOperatorRight(0, std::move(Left));
}

/// returnstmt
///   = "return" [ expression ] ;
static unique_ptr<ExpressionNode> ParseReturnStatement() {
  getNextToken(); // eat 'return'
  if (CurrentToken == tok_eol || CurrentToken == tok_dedent || CurrentToken == tok_eof) {
    if (CurrentFunctionReturnType != ValueType::None)
      return LogError("Return value required");
    return make_unique<ReturnExpressionNode>(nullptr);
  }

  ExpectedLiteralTypeGuard Guard(CurrentFunctionReturnType,
                                 CurrentFunctionReturnStructName);
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  if (CurrentFunctionReturnType == ValueType::None)
    return LogError("cannot return a value from a None function");
  if (!IsAssignable(CurrentFunctionReturnType, Expr->getType())) {
    return LogError(("cannot return " + string(TypeName(Expr->getType())) +
                     " from function returning " +
                     string(TypeName(CurrentFunctionReturnType)))
                        .c_str());
  }
  return make_unique<ReturnExpressionNode>(std::move(Expr));
}

static unique_ptr<ExpressionNode> ParseBreakStatement() {
  if (ParseLoopDepth <= 0 && ParseSwitchDepth <= 0)
    return LogError("'break' used outside of a loop or switch");
  getNextToken(); // eat 'break'
  return make_unique<BreakExpressionNode>();
}

static unique_ptr<ExpressionNode> ParseContinueStatement() {
  if (ParseLoopDepth <= 0)
    return LogError("'continue' used outside of a loop");
  getNextToken(); // eat 'continue'
  return make_unique<ContinueExpressionNode>();
}

static unique_ptr<ExpressionNode> ParseAssignmentRight(const string &Name) {
  if (!IsDeclaredVar(Name))
    return LogError("Assignment to undeclared variable");
  ValueType VarType = LookupVarType(Name);
  getNextToken(); // eat '='

  ExpectedLiteralTypeGuard Guard(VarType, LookupVarStructName(Name));
  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  if (!IsAssignable(VarType, Right->getType()))
    return LogError("Type mismatch in assignment");
  if (VarType == ValueType::Pointer &&
      LookupVarStructName(Name) != Right->getStructName())
    return LogError("Type mismatch in assignment");
  return make_unique<AssignmentExpressionNode>(Name, std::move(Right), VarType);
}

static bool IsCompoundAssignTok(int Tok) {
  return Tok == tok_pluseq || Tok == tok_minuseq || Tok == tok_muleq ||
         Tok == tok_diveq || Tok == tok_modeq;
}

static int CompoundAssignToBinaryOp(int Tok) {
  switch (Tok) {
  case tok_pluseq:
    return tok_plus;
  case tok_minuseq:
    return tok_minus;
  case tok_muleq:
    return tok_star;
  case tok_diveq:
    return tok_slash;
  case tok_modeq:
    return tok_percent;
  default:
    return 0;
  }
}

static unique_ptr<ExpressionNode> ParseCompoundAssignmentRight(const string &Name,
                                                      int AssignTok) {
  if (!IsDeclaredVar(Name))
    return LogError("Assignment to undeclared variable");
  ValueType DestType = LookupVarType(Name);
  string DestStruct = LookupVarStructName(Name);
  int Operator = CompoundAssignToBinaryOp(AssignTok);
  if (!Operator)
    return LogError("Unknown compound assignment operator");
  getNextToken(); // eat compound assignment token
  ExpectedLiteralTypeGuard Guard(DestType, DestStruct);
  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  string ResultStructName;
  ValueType ResultType =
      GetBinaryResultType(Operator, DestType, DestStruct, Right->getType(),
                          Right->getStructName(), &ResultStructName);
  if (ResultType == ValueType::Error)
    return LogError("Type mismatch in assignment");
  if (!IsAssignable(DestType, ResultType))
    return LogError("Type mismatch in assignment");
  if (DestType == ValueType::Pointer && DestStruct != ResultStructName)
    return LogError("Type mismatch in assignment");
  return make_unique<CompoundAssignmentExpressionNode>(Name, Operator, std::move(Right),
                                                DestType, DestStruct);
}

static unique_ptr<ExpressionNode>
ParseFieldAssignmentRight(unique_ptr<FieldExpressionNode> Left) {
  ValueType DestType = Left->getType();
  getNextToken(); // eat '='
  ExpectedLiteralTypeGuard Guard(DestType, Left->getStructName());
  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  if (!IsAssignable(DestType, Right->getType()))
    return LogError("Type mismatch in assignment");
  if (DestType == ValueType::Pointer &&
      Left->getStructName() != Right->getStructName())
    return LogError("Type mismatch in assignment");
  return make_unique<FieldAssignmentExpressionNode>(std::move(Left), std::move(Right),
                                             DestType);
}

static unique_ptr<ExpressionNode>
ParseFieldCompoundAssignmentRight(unique_ptr<FieldExpressionNode> Left, int AssignTok) {
  ValueType DestType = Left->getType();
  string DestStruct = Left->getStructName();
  int Operator = CompoundAssignToBinaryOp(AssignTok);
  if (!Operator)
    return LogError("Unknown compound assignment operator");
  getNextToken(); // eat compound assignment token
  ExpectedLiteralTypeGuard Guard(DestType, DestStruct);
  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  string ResultStructName;
  ValueType ResultType =
      GetBinaryResultType(Operator, DestType, DestStruct, Right->getType(),
                          Right->getStructName(), &ResultStructName);
  if (ResultType == ValueType::Error)
    return LogError("Type mismatch in assignment");
  if (!IsAssignable(DestType, ResultType))
    return LogError("Type mismatch in assignment");
  if (DestType == ValueType::Pointer && DestStruct != ResultStructName)
    return LogError("Type mismatch in assignment");
  return make_unique<FieldCompoundAssignmentExpressionNode>(
      std::move(Left), Operator, std::move(Right), DestType, DestStruct);
}

/// simplestmt
///   = returnstmt | varstmt | assignstmt | expression ;
// Parse name-led forms in simplestmt:
//   assignstmt   : name "=" expression
//   expression   : name ...
// and reject trailing assignment when the parsed Left is not assignable.
static unique_ptr<ExpressionNode> ParseLeadingNameSimpleStatement() {
  unique_ptr<ExpressionNode> Expr;
    string ParsedName = Name;
    getNextToken(); // eat name.

    if (CurrentToken == tok_equal) {
      return ParseAssignmentRight(ParsedName);
    }
    if (IsCompoundAssignTok(CurrentToken)) {
      return ParseCompoundAssignmentRight(ParsedName, CurrentToken);
    }

    Expr = ParseNameExpressionWithName(std::move(ParsedName));
    if (!Expr)
      return nullptr;
    if (CurrentToken == tok_dot) {
      getNextToken(); // eat '.'
      if (CurrentToken != tok_name)
        return LogError("Expected field or method name after '.'");
      string MemberName = Name;
      getNextToken(); // eat member name
      if (CurrentToken == tok_lparen) {
        Expr = ParseMethodCallExpression(std::move(Expr), MemberName);
        if (!Expr)
          return nullptr;
      } else {
        auto *Var = dynamic_cast<NameExpressionNode *>(Expr.get());
        if (!Var)
          return LogError("Field access base must be a variable");
        auto Field = ParseFieldAccessFromFirstMember(
            Var->getName(), Var->getType(), Var->getStructName(), MemberName);
        if (!Field)
          return LogError("Invalid field access");
        Expr = std::move(Field);
      }
    }
    if (CurrentToken == tok_lbracket) {
      if (auto *Var = dynamic_cast<NameExpressionNode *>(Expr.get())) {
        Expr = ParseIndexExpression(Var->getName(), {}, Var->getType(),
                              Var->getStructName());
      } else if (auto *Field = dynamic_cast<FieldExpressionNode *>(Expr.get())) {
        Expr = ParseIndexExpression(*Field->getLValueName(), Field->getFieldPath(),
                              Field->getType(), Field->getStructName());
      } else {
        return LogError("Indexing requires a pointer or array value");
      }
      if (!Expr)
        return nullptr;
    }
    if (CurrentToken == tok_dot) {
      if (auto *Idx = dynamic_cast<IndexExpressionNode *>(Expr.get())) {
        auto Owned = std::unique_ptr<IndexExpressionNode>(Idx);
        Expr.release();
        Expr = ParseIndexedFieldAccessExpression(std::move(Owned));
      } else {
        return LogError("Field access base must be a struct value");
      }
      if (!Expr)
        return nullptr;
    }
    Expr = ParsePostfixIncDec(std::move(Expr));
    if (!Expr)
      return nullptr;
    Expr = ParseBinaryOperatorRight(0, std::move(Expr));
    if (!Expr)
      return nullptr;

    if (CurrentToken != tok_equal && !IsCompoundAssignTok(CurrentToken))
      return Expr;

    if (auto *Field = dynamic_cast<FieldExpressionNode *>(Expr.get())) {
      auto Owned = std::unique_ptr<FieldExpressionNode>(Field);
      Expr.release();
      if (CurrentToken == tok_equal)
        return ParseFieldAssignmentRight(std::move(Owned));
      return ParseFieldCompoundAssignmentRight(std::move(Owned), CurrentToken);
    }
    if (auto *Idx = dynamic_cast<IndexExpressionNode *>(Expr.get())) {
      int AssignTok = CurrentToken;
      getNextToken(); // eat '='
      ExpectedLiteralTypeGuard Guard(Idx->getType(), Idx->getStructName());
      auto Right = ParseExpression();
      if (!Right)
        return nullptr;
      if (AssignTok == tok_equal) {
        if (!IsAssignable(Idx->getType(), Right->getType()))
          return LogError("Type mismatch in assignment");
      } else {
        string ResultStructName;
        ValueType ResultType = GetBinaryResultType(
            CompoundAssignToBinaryOp(AssignTok), Idx->getType(),
            Idx->getStructName(), Right->getType(), Right->getStructName(),
            &ResultStructName);
        if (ResultType == ValueType::Error ||
            !IsAssignable(Idx->getType(), ResultType) ||
            (Idx->getType() == ValueType::Pointer &&
             Idx->getStructName() != ResultStructName))
          return LogError("Type mismatch in assignment");
      }
      ValueType ElemType = Idx->getType();
      string ElemStructName = Idx->getStructName();
      auto Owned = std::unique_ptr<IndexExpressionNode>(Idx);
      Expr.release();
      if (AssignTok == tok_equal) {
        return std::make_unique<IndexAssignmentExpressionNode>(
            std::move(Owned), std::move(Right), ElemType, ElemStructName);
      }
      return std::make_unique<IndexCompoundAssignmentExpressionNode>(
          std::move(Owned), CompoundAssignToBinaryOp(AssignTok), std::move(Right),
          ElemType, ElemStructName);
    }
    if (auto *IdxField = dynamic_cast<IndexedFieldExpressionNode *>(Expr.get())) {
      int AssignTok = CurrentToken;
      getNextToken(); // eat assignment token
      ExpectedLiteralTypeGuard Guard(IdxField->getType(),
                                     IdxField->getStructName());
      auto Right = ParseExpression();
      if (!Right)
        return nullptr;
      if (AssignTok == tok_equal) {
        if (!IsAssignable(IdxField->getType(), Right->getType()))
          return LogError("Type mismatch in assignment");
      } else {
        string ResultStructName;
        ValueType ResultType = GetBinaryResultType(
            CompoundAssignToBinaryOp(AssignTok), IdxField->getType(),
            IdxField->getStructName(), Right->getType(), Right->getStructName(),
            &ResultStructName);
        if (ResultType == ValueType::Error ||
            !IsAssignable(IdxField->getType(), ResultType) ||
            (IdxField->getType() == ValueType::Pointer &&
             IdxField->getStructName() != ResultStructName))
          return LogError("Type mismatch in assignment");
      }
      ValueType ElemType = IdxField->getType();
      string ElemStructName = IdxField->getStructName();
      auto Owned = std::unique_ptr<IndexedFieldExpressionNode>(IdxField);
      Expr.release();
      if (AssignTok == tok_equal) {
        return std::make_unique<IndexedFieldAssignmentExpressionNode>(
            std::move(Owned), std::move(Right), ElemType, ElemStructName);
      }
      return std::make_unique<IndexedFieldCompoundAssignmentExpressionNode>(
          std::move(Owned), CompoundAssignToBinaryOp(AssignTok), std::move(Right),
          ElemType, ElemStructName);
    }
    if (CurrentToken != tok_equal)
      return LogError("Destination of compound assignment must be assignable");

    const string *AssignedName = Expr->getLValueName();
    if (!AssignedName)
      return LogError("Destination of '=' must be a variable");
    return ParseAssignmentRight(*AssignedName);
  }

// Parse non-name-leading expression forms for simplestmt and reject
// trailing assignment so diagnostics stay local and specific.
static unique_ptr<ExpressionNode> ParseNonLeadingNameSimpleStatement() {
  unique_ptr<ExpressionNode> Expr;
  Expr = ParseExpression();
  if (!Expr)
    return nullptr;

  if (CurrentToken != tok_equal && !IsCompoundAssignTok(CurrentToken))
    return Expr;

  if (IsCompoundAssignTok(CurrentToken))
    return LogError("Destination of compound assignment must be assignable");
  return LogError("Destination of '=' must be a variable");
}

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
///   = simplestmt | compoundstmt ;
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
///   = simplestmt | compoundstmt | end-of-lines block ;
static unique_ptr<ExpressionNode> ParseSuite() {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken != tok_indent)
      return LogError("Expected an indented block");
    return ParseBlock();
  }

  if (CurrentToken == tok_indent)
    return ParseBlock();

  return ParseStatement();
}

/// block
///   = INDENT statement { stmtsep statement } DEDENT ;
static unique_ptr<ExpressionNode> ParseBlock() {
  if (CurrentToken != tok_indent)
    return LogError("Expected an indented block");
  getNextToken(); // eat INDENT

  BlockScopeGuard Scope;

  if (CurrentToken == tok_dedent)
    return LogError("Expected at least one statement in block");

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

    return LogError("Expected newline or end of block");
  }

  if (CurrentToken != tok_dedent)
    return LogError("Expected end of block");

  // Consume DEDENT, but leave a synthetic separator visible to the enclosing
  // parser so it can distinguish "a nested block just ended" from arbitrary
  // trailing tokens without threading boolean state through every parser call.
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end

  return make_unique<BlockExpressionNode>(std::move(Stmts));
}

/// function-signature
///   = name "(" [ typedparam { "," typedparam } ] ")" ;
///
/// typedparam
///   = name ":" type ;
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
  SourceLocation SignatureLoc = CurLoc;

  if (CurrentToken != tok_name)
    return LogErrorSignature("Expected function name in function signature");
  string FnName = Name;
  if ((FnName.size() == 7 && FnName.rfind("binary", 0) == 0 &&
       isascii(static_cast<unsigned char>(FnName[6])) &&
       ispunct(static_cast<unsigned char>(FnName[6]))) ||
      (FnName.size() == 6 && FnName.rfind("unary", 0) == 0 &&
       isascii(static_cast<unsigned char>(FnName[5])) &&
       ispunct(static_cast<unsigned char>(FnName[5])))) {
    fprintf(stderr,
            "Warning: Function name '%s' may conflict with "
            "operator-reserved naming\n",
            FnName.c_str());
  }
  getNextToken(); // eat function name

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in function signature");

  vector<FunctionSignatureNode::ParameterInfo> ParameterNames;
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
      ParameterNames.push_back({ArgName, ArgType, ArgStructName});

      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorSignature("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  getNextToken(); // eat ')'
  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames), SignatureLoc);
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

static ValueType
ParseOptionalReturnTypeWithStruct(string &StructName,
                                  ValueType DefaultType = ValueType::None) {
  StructName.clear();
  if (CurrentToken != tok_arrow)
    return DefaultType;
  getNextToken();
  return ParseTypeToken(&StructName);
}

/// functionbody
///   = simplestmt | end-of-lines block ;
static unique_ptr<ExpressionNode> ParseFunctionBody() {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken != tok_indent)
      return LogError("Expected an indented block");
    return ParseBlock();
  }

  return ParseSimpleStatement();
}

/// function-definition
static unique_ptr<FunctionDefinitionNode> ParseFunctionDefinition() {
  getNextToken(); // eat 'def'
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;
  string RetStructName;
  ValueType RetType =
      ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);
  if (RetType == ValueType::Error)
    return nullptr;
  Signature->setReturnType(RetType);
  Signature->setReturnStructName(RetStructName);
  FunctionSignatures[Signature->getName()] = Signature->clone();
  ReturnTypeGuard RetGuard(RetType, RetStructName);
  FunctionScopeGuard Scope(Signature->getParameters());

  if (CurrentToken != tok_colon)
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'
  unique_ptr<ExpressionNode> Body = ParseFunctionBody();

  if (Body) {
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
  }
  FunctionSignatures.erase(Signature->getName());
  return nullptr;
}

static unique_ptr<FunctionDefinitionNode>
ParseMethodDefinitionInClass(const string &ClassName, bool IsPublic) {
  // CurrentToken is 'def'
  getNextToken(); // eat 'def'
  if (CurrentToken != tok_name)
    return LogErrorF("Expected method name in class definition");
  string MethodName = Name;
  SourceLocation SignatureLoc = CurLoc;
  getNextToken(); // eat method name
  if (CurrentToken != tok_lparen)
    return LogErrorF("Expected '(' in method function signature");
  getNextToken(); // eat '('

  vector<FunctionSignatureNode::ParameterInfo> ParameterNames;
  // Implicit self parameter is a pointer so methods can mutate receiver state.
  ParameterNames.push_back({"self", ValueType::Pointer,
                      EncodePointerType(ValueType::Struct, ClassName)});

  if (CurrentToken != tok_rparen) {
    while (true) {
      if (CurrentToken != tok_name)
        return LogErrorF("Expected parameter name in method function signature");
      string ArgName = Name;
      if (ArgName == "self")
        return LogErrorF("Method parameters cannot be named 'self'");
      getNextToken(); // eat name
      if (CurrentToken != tok_colon)
        return LogErrorF(
            "Method parameters require a type annotation (e.g., ': int')");
      getNextToken(); // eat ':'
      string ArgStructName;
      ValueType ArgType = ParseTypeToken(&ArgStructName);
      if (ArgType == ValueType::Error)
        return nullptr;
      if (ArgType == ValueType::None)
        return LogErrorF("Parameters cannot have None type");
      ParameterNames.push_back({ArgName, ArgType, ArgStructName});

      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorF("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  if (CurrentToken != tok_rparen)
    return LogErrorF("Expected ')' in method function signature");
  getNextToken(); // eat ')'

  string RetStructName;
  ValueType RetType =
      ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);
  if (RetType == ValueType::Error)
    return nullptr;
  if (MethodName == "__init__" && RetType != ValueType::None)
    return LogErrorF("Constructor '__init__' must return None");

  string MangledName = ClassName + "." + MethodName;
  if (FunctionSignatures.count(MangledName))
    return LogErrorF(("Method '" + MethodName + "' is already defined on '" +
                      ClassName + "'")
                         .c_str());

  auto Signature = make_unique<FunctionSignatureNode>(MangledName, std::move(ParameterNames),
                                         SignatureLoc, RetType);
  Signature->setReturnStructName(RetStructName);
  FunctionSignatures[Signature->getName()] = Signature->clone();
  StructTypes[ClassName].MethodIsPublic[MethodName] = IsPublic;

  ReturnTypeGuard RetGuard(RetType, RetStructName);
  FunctionScopeGuard Scope(Signature->getParameters());
  ClassScopeGuard ClassScope(ClassName);

  if (CurrentToken != tok_colon)
    return LogErrorF("Expected ':' in method definition");
  getNextToken(); // eat ':'
  unique_ptr<ExpressionNode> Body = ParseFunctionBody();
  if (Body) {
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
  }
  FunctionSignatures.erase(MangledName);
  return nullptr;
}

/// binarydecorator
///   = "@" "binary" "(" integer ")"
///
/// Called after '@' has been consumed. CurrentToken is on 'binary'.
/// Returns the parsed precedence (>= 1), or 0 on error.
/// 0 is a safe sentinel because valid precedences must be >= 1.
static unsigned ParseBinaryDecorator() {
  getNextToken(); // eat 'binary'

  if (CurrentToken != tok_lparen) {
    LogError("Expected '(' after '@binary'");
    return 0;
  }
  getNextToken(); // eat '('

  if (CurrentToken != tok_number) {
    LogError("Expected precedence number in '@binary(...)'");
    return 0;
  }
  // The lexer has no separate tok_integer — it emits tok_number for both
  // integer and decimal literals. Reject decimals/exponents by checking the
  // raw source.
  if (NumberLiteral.find('.') != string::npos ||
      NumberLiteral.find('e') != string::npos ||
      NumberLiteral.find('E') != string::npos) {
    LogError("Precedence must be an integer, not a decimal literal");
    return 0;
  }
  unsigned NeededBits = APInt::getBitsNeeded(NumberLiteral, 10);
  if (NeededBits > 32) {
    LogError("Precedence is too large");
    return 0;
  }
  APInt Val(NeededBits == 0 ? 1 : NeededBits, NumberLiteral, 10);
  if (Val.isZero()) {
    LogError("Precedence must be a positive integer");
    return 0;
  }
  unsigned Prec = static_cast<unsigned>(Val.getZExtValue());
  getNextToken(); // eat number

  if (CurrentToken != tok_rparen) {
    LogError("Expected ')' after precedence in '@binary(...)'");
    return 0;
  }
  getNextToken(); // eat ')'

  return Prec;
}

/// unarydecorator
///   = "@" "unary"
/// Called after '@' has been consumed. CurrentToken is on 'unary'.
/// Consumes the 'unary' token.
static void ParseUnaryDecorator() {
  getNextToken(); // eat 'unary'
}

// IsCustomOpChar - Return true if Tok can be used as a user-defined operator
// character in Pyxc operator prototypes.
//
// We restrict to ASCII punctuation so operator definitions stay single-char and
// predictable across platforms/locales. '@' is reserved for decorator syntax
// (@binary / @unary), so it is explicitly excluded.
static bool IsCustomOpChar(int Tok) {
  return isascii(Tok) && ispunct(static_cast<unsigned char>(Tok)) && Tok != tok_at;
}

// IsKnownBinaryOperatorToken - Return true if Tok is already present in the
// parser's binary-operator table.
//
// OperatorPrecedence contains built-in binary operators at startup and gains
// custom binary operators as their prototypes are codegen'd. This makes it a
// single source of truth for "is this binary operator already known?".
static bool IsKnownBinaryOperatorToken(int Tok) {
  return OperatorPrecedence.find(Tok) != OperatorPrecedence.end();
}

// IsKnownUnaryOperatorToken - Return true if Tok is already present in the
// parser's unary-operator registry.
//
// KnownUnaryOperators contains reserved built-in unary operators at startup
// and gains custom unary operators as their prototypes are codegen'd.
static bool IsKnownUnaryOperatorToken(int Tok) {
  return KnownUnaryOperators.find(Tok) != KnownUnaryOperators.end();
}

/// binaryopprototype
///   = customopchar "(" typedparam "," typedparam ")"
///
/// CurrentToken is on the operator character.
/// The function is stored internally as "binary<opchar>" (e.g. "binary%"),
/// which is how BinaryExpressionNode::codegen() looks it up at call sites.
static unique_ptr<FunctionSignatureNode> ParseBinaryOperatorSignature(unsigned Precedence) {
  SourceLocation SignatureLoc = CurLoc;
  if (!IsCustomOpChar(CurrentToken))
    return LogErrorSignature(
        "Expected operator character in binary operator signature");

  char OperatorCharacter = (char)CurrentToken;
  string FnName = string("binary") + OperatorCharacter;

  // Reject redefining any binary operator that is already known to the parser.
  // This covers both language built-ins and previously defined custom
  // operators, since both live in OperatorPrecedence.
  if (IsKnownBinaryOperatorToken(CurrentToken))
    return LogErrorSignature(
        (string("Binary operator '") + OperatorCharacter + "' is already defined")
            .c_str());

  // Reject cross-arity reuse: if a token is already known as a unary operator,
  // we do not allow defining it as binary.
  if (IsKnownUnaryOperatorToken(CurrentToken))
    return LogErrorSignature((string("Binary operator '") + OperatorCharacter +
                      "' conflicts with an existing unary operator")
                         .c_str());

  // Separate guard: reject any existing function/function signature named "binary<op>".
  // This catches symbol collisions even if the operator was not registered in
  // OperatorPrecedence (e.g. an earlier extern/def with the same encoded name).
  // Without this, a new definition could silently shadow the old symbol in the
  // JIT. For operators, we don't want this. For other functions, shadowing is
  // permissable.
  if (FunctionSignatures.count(FnName))
    return LogErrorSignature((string("Function name 'binary") + OperatorCharacter +
                      "' conflicts with operator-reserved naming")
                         .c_str());

  getNextToken(); // eat operator char

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in binary operator signature");

  vector<FunctionSignatureNode::ParameterInfo> ParameterNames;
  getNextToken(); // eat '('
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (CurrentToken != tok_name)
        return LogErrorSignature("Expected parameter name in operator function signature");
      string ArgName = Name;
      getNextToken(); // eat name
      if (CurrentToken != tok_colon)
        return LogErrorSignature("Operator parameters require a type annotation (e.g., "
                         "': float64')");
      getNextToken(); // eat ':'
      string ArgStructName;
      ValueType ArgType = ParseTypeToken(&ArgStructName);
      if (ArgType == ValueType::Error)
        return nullptr;
      if (ArgType == ValueType::None)
        return LogErrorSignature("Parameters cannot have None type");
      ParameterNames.push_back({ArgName, ArgType, ArgStructName});

      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorSignature("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  if (CurrentToken != tok_rparen)
    return LogErrorSignature("Expected ')' in binary operator signature");
  getNextToken(); // eat ')'

  if (ParameterNames.size() != 2)
    return LogErrorSignature("Binary operator must have exactly two arguments");

  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames), SignatureLoc,
                                   ValueType::None, /*IsOperator=*/true,
                                   Precedence);
}

/// unaryopprototype
///   = customopchar "(" typedparam ")"
///
/// CurrentToken is on the operator character.
/// The function is stored internally as "unary<opchar>" (e.g. "unary&"),
/// which is how ParseUnary() looks it up at call sites.
static unique_ptr<FunctionSignatureNode> ParseUnaryOperatorSignature() {
  SourceLocation SignatureLoc = CurLoc;
  if (!IsCustomOpChar(CurrentToken))
    return LogErrorSignature("Expected operator character in unary operator signature");

  char OperatorCharacter = (char)CurrentToken;
  string FnName = string("unary") + OperatorCharacter;

  // Reject redefining any unary operator that is already known to the parser.
  // This covers reserved unary operators and previously defined custom unary
  // operators tracked in KnownUnaryOperators.
  if (IsKnownUnaryOperatorToken(CurrentToken))
    return LogErrorSignature(
        (string("Unary operator '") + OperatorCharacter + "' is already defined").c_str());

  // Reject cross-arity reuse: if a token is already known as a binary operator,
  // we do not allow defining it as unary.
  if (IsKnownBinaryOperatorToken(CurrentToken))
    return LogErrorSignature((string("Unary operator '") + OperatorCharacter +
                      "' conflicts with an existing binary operator")
                         .c_str());

  // Prevent silent JIT shadowing (same reason as in ParseBinaryOperatorSignature).
  if (FunctionSignatures.count(FnName))
    return LogErrorSignature((string("Function name 'unary") + OperatorCharacter +
                      "' conflicts with operator-reserved naming")
                         .c_str());

  getNextToken(); // eat operator char

  if (CurrentToken != tok_lparen)
    return LogErrorSignature("Expected '(' in unary operator signature");

  vector<FunctionSignatureNode::ParameterInfo> ParameterNames;
  getNextToken(); // eat '('
  if (CurrentToken != tok_rparen) {
    while (true) {
      if (CurrentToken != tok_name)
        return LogErrorSignature("Expected parameter name in operator function signature");
      string ArgName = Name;
      getNextToken(); // eat name
      if (CurrentToken != tok_colon)
        return LogErrorSignature("Operator parameters require a type annotation (e.g., "
                         "': float64')");
      getNextToken(); // eat ':'
      string ArgStructName;
      ValueType ArgType = ParseTypeToken(&ArgStructName);
      if (ArgType == ValueType::Error)
        return nullptr;
      if (ArgType == ValueType::None)
        return LogErrorSignature("Parameters cannot have None type");
      ParameterNames.push_back({ArgName, ArgType, ArgStructName});

      if (CurrentToken == tok_rparen)
        break;
      if (CurrentToken != tok_comma)
        return LogErrorSignature("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  if (CurrentToken != tok_rparen)
    return LogErrorSignature("Expected ')' in unary operator signature");
  getNextToken(); // eat ')'

  if (ParameterNames.size() != 1)
    return LogErrorSignature("Unary operator must have exactly one argument");

  // Unary operators have no precedence — they bind tighter than any binary op
  // by virtue of being parsed before ParseBinaryOperatorRight is entered.
  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames), SignatureLoc,
                                   ValueType::None, /*IsOperator=*/true,
                                   /*Precedence=*/0);
}

/// decorateddef
///   = binarydecorator end-of-lines "def" binaryopprototype ":" ( simplestmt | end-of-lines
///   block ) | unarydecorator  end-of-lines "def" unaryopprototype  ":" ( simplestmt |
///   end-of-lines block )
///
/// Called after '@' has been consumed. CurrentToken is on 'binary' or 'unary'.
/// The two branches share the same body structure (':' / block).
static unique_ptr<FunctionDefinitionNode> ParseDecoratedFunctionDef() {
  if (CurrentToken != tok_binary && CurrentToken != tok_unary)
    return LogErrorF("Expected 'binary' or 'unary' after '@'");

  bool IsBinary = (CurrentToken == tok_binary);
  unique_ptr<FunctionSignatureNode> Signature;

  if (IsBinary) {
    unsigned Prec = ParseBinaryDecorator(); // consumes "binary(N)"
    if (!Prec)
      return nullptr;
    // The decorator must end at a newline before 'def'.
    if (CurrentToken != tok_eol)
      return LogErrorF("Expected newline after '@binary(...)' decorator");
    consumeNewlines();
    if (CurrentToken != tok_def)
      return LogErrorF("Expected 'def' after decorator");
    getNextToken(); // eat 'def'
    Signature = ParseBinaryOperatorSignature(Prec);
  } else {
    ParseUnaryDecorator(); // consumes "unary"
    if (CurrentToken != tok_eol)
      return LogErrorF("Expected newline after '@unary' decorator");
    consumeNewlines();
    if (CurrentToken != tok_def)
      return LogErrorF("Expected 'def' after decorator");
    getNextToken(); // eat 'def'
    Signature = ParseUnaryOperatorSignature();
  }

  if (!Signature)
    return nullptr;
  string RetStructName;
  ValueType RetType = ParseOptionalReturnTypeWithStruct(RetStructName);
  if (RetType == ValueType::Error)
    return nullptr;
  Signature->setReturnType(RetType);
  Signature->setReturnStructName(RetStructName);
  FunctionSignatures[Signature->getName()] = Signature->clone();
  ReturnTypeGuard RetGuard(RetType, RetStructName);
  FunctionScopeGuard Scope(Signature->getParameters());

  // Shared body: ":" ( simplestmt | end-of-lines block ) — identical to
  // ParseFunctionDefinition.
  if (CurrentToken != tok_colon)
    return LogErrorF("Expected ':' in operator definition");
  getNextToken(); // eat ':'
  unique_ptr<ExpressionNode> Body = ParseFunctionBody();

  if (Body) {
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
  }
  FunctionSignatures.erase(Signature->getName());
  return nullptr;
}

/// toplevelstmt
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

/// top-level-expression
///   = statement
/// A top-level statement (e.g. "1 + 2", "var x = 1", "if ...") is wrapped in
/// an anonymous function so it fits the same FunctionDefinitionNode shape as everything
/// else. HandleTopLevelExpression compiles it into the JIT, calls it to get
/// the numeric result, then removes it from the JIT via a ResourceTracker.
static unique_ptr<FunctionDefinitionNode> ParseTopLevelExpression() {
  auto Stmt = ParseTopLevelStatement();
  if (!Stmt)
    return nullptr;

  ValueType RetType = Stmt->getType();
  if (!Stmt->isReturnExpr() && RetType != ValueType::None)
    Stmt = make_unique<ReturnExpressionNode>(std::move(Stmt));

  string FnName = "__pyxc.toplevel." + to_string(TopLevelExprCounter++);
  auto Signature = make_unique<FunctionSignatureNode>(
      FnName, vector<FunctionSignatureNode::ParameterInfo>(), CurLoc, RetType);
  return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Stmt));
}

/// external
///   = "extern" "def" function signature [ "->" type ] ;
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurrentToken != tok_def)
    return LogErrorSignature("Expected `def` after extern.");
  getNextToken(); // eat def
  auto Signature = ParseFunctionSignature();
  if (!Signature)
    return nullptr;
  string RetStructName;
  ValueType RetType = ParseOptionalReturnTypeWithStruct(RetStructName);
  if (RetType == ValueType::Error)
    return nullptr;
  Signature->setReturnType(RetType);
  Signature->setReturnStructName(RetStructName);
  return Signature;
}

static bool ParseAggregateDefinition(const char *KindName) {
  // CurrentToken is 'struct' or 'class'
  getNextToken(); // eat keyword
  if (CurrentToken != tok_name) {
    LogError((string("Expected ") + KindName + " name").c_str());
    return false;
  }
  string StructName = Name;
  if (TypeAliases.count(StructName)) {
    LogError(("Name '" + StructName + "' is already defined as a type alias")
                 .c_str());
    return false;
  }
  if (StructTypes.count(StructName)) {
    LogError(("Aggregate '" + StructName + "' is already defined").c_str());
    return false;
  }
  getNextToken(); // eat aggregate name
  vector<StructTypeInfo::ImplTraitRef> ImplementedTraits;
  bool IsClass = (strcmp(KindName, "class") == 0);
  if (IsClass && CurrentToken == tok_lparen) {
    std::set<string> SeenTraits;
    getNextToken(); // eat '('
    if (CurrentToken != tok_rparen) {
      while (true) {
        if (CurrentToken != tok_name) {
          LogError("Expected trait name in class implements list");
          return false;
        }
        StructTypeInfo::ImplTraitRef Ref;
        Ref.TraitName = Name;
        string TraitName = Ref.TraitName;
        if (!Traits.count(TraitName)) {
          LogError(("Unknown trait '" + TraitName + "'").c_str());
          return false;
        }
        if (SeenTraits.count(TraitName)) {
          LogError(
              ("Duplicate trait '" + TraitName + "' in class implements list")
                  .c_str());
          return false;
        }
        SeenTraits.insert(TraitName);
        getNextToken(); // eat trait name
        const auto &TI = Traits.at(TraitName);
        if (!TI.TypeParamName.empty()) {
          if (CurrentToken != tok_lbracket) {
            LogError(
                ("Trait '" + TraitName + "' requires a type argument").c_str());
            return false;
          }
          getNextToken(); // eat '['
          string TypeArgStruct;
          ValueType TypeArg = ParseTypeToken(&TypeArgStruct);
          if (TypeArg == ValueType::Error || TypeArg == ValueType::None ||
              TypeArg == ValueType::TypeVar) {
            LogError("Invalid trait type argument");
            return false;
          }
          if (CurrentToken != tok_rbracket) {
            LogError("Expected ']' after trait type argument");
            return false;
          }
          getNextToken(); // eat ']'
          Ref.HasTypeArg = true;
          Ref.TypeArg = TypeArg;
          Ref.TypeArgStructName = TypeArgStruct;
        } else if (CurrentToken == tok_lbracket) {
          LogError(("Trait '" + TraitName + "' does not take type arguments")
                       .c_str());
          return false;
        }
        ImplementedTraits.push_back(Ref);
        if (CurrentToken == tok_rparen)
          break;
        if (CurrentToken != tok_comma) {
          LogError("Expected ')' or ',' in class implements list");
          return false;
        }
        getNextToken(); // eat ','
      }
    }
    if (CurrentToken != tok_rparen) {
      LogError("Expected ')' after class implements list");
      return false;
    }
    getNextToken(); // eat ')'
  }
  if (CurrentToken != tok_colon) {
    LogError((string("Expected ':' after ") + KindName + " name").c_str());
    return false;
  }
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent) {
    LogError((string("Expected an indented ") + KindName + " body").c_str());
    return false;
  }
  getNextToken(); // eat INDENT

  StructTypeInfo Info;
  Info.Name = StructName;
  Info.IsClass = IsClass;
  Info.ImplementedTraits = ImplementedTraits;
  // Register early so method signatures can reference the enclosing class.
  StructTypes[StructName] = Info;
  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }
    bool MemberIsPublic = true;
    bool HasVisibilityModifier = false;
    if (CurrentToken == tok_public || CurrentToken == tok_private) {
      HasVisibilityModifier = true;
      MemberIsPublic = (CurrentToken == tok_public);
      getNextToken(); // eat visibility modifier
    }
    if (HasVisibilityModifier && !Info.IsClass) {
      LogError("Visibility modifiers are only allowed inside class bodies");
      return false;
    }
    if (CurrentToken == tok_def) {
      if (!Info.IsClass) {
        LogError("Methods are only allowed inside classes");
        return false;
      }
      auto FnAST = ParseMethodDefinitionInClass(StructName, MemberIsPublic);
      if (!FnAST)
        return false;
      if (auto *FnIR = FnAST->codegen()) {
        if (ShouldDumpIR())
          FnIR->print(errs());
      }
      if (CurrentToken == tok_eol)
        consumeNewlines();
      else if (CurrentToken == tok_block_end)
        // The method body was itself an indented block; ParseFunctionBody
        // (via ParseBlock) left its own block-end marker in CurrentToken. Consume
        // it here so the loop condition below sees the real next token
        // (another class member, or the class's own DEDENT) instead of
        // mistaking the method's block-end for the class body's.
        getNextToken();
      continue;
    }
    if (CurrentToken != tok_name) {
      LogError(
          (string("Expected field name in ") + KindName + " body").c_str());
      return false;
    }
    string FieldName = Name;
    getNextToken();
    if (CurrentToken != tok_colon) {
      LogError("Expected ':' after field name");
      return false;
    }
    getNextToken();
    string FieldStructName;
    ValueType FieldType = ParseTypeToken(&FieldStructName);
    if (FieldType == ValueType::Error || FieldType == ValueType::None) {
      LogError((string("Invalid ") + KindName + " field type").c_str());
      return false;
    }
    if (Info.FieldIndex.count(FieldName)) {
      LogError((string("Duplicate ") + KindName + " field '" + FieldName + "'")
                   .c_str());
      return false;
    }
    Info.FieldIndex[FieldName] = Info.Fields.size();
    Info.Fields.push_back(
        {FieldName, FieldType, FieldStructName, MemberIsPublic});
    // Keep aggregate metadata visible while parsing subsequent methods.
    Info.MethodIsPublic = StructTypes[StructName].MethodIsPublic;
    StructTypes[StructName] = Info;
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }
  if (CurrentToken != tok_dedent) {
    LogError((string("Expected dedent after ") + KindName + " body").c_str());
    return false;
  }
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end
  Info.MethodIsPublic = StructTypes[StructName].MethodIsPublic;
  if (Info.IsClass) {
    for (const auto &ImplRef : Info.ImplementedTraits) {
      if (!VerifyTraitConformance(StructName, ImplRef))
        return false;
    }
  }
  StructTypes[StructName] = std::move(Info);
  return true;
}

static bool
VerifyTraitConformance(const string &ClassName,
                       const StructTypeInfo::ImplTraitRef &ImplRef) {
  const string &TraitName = ImplRef.TraitName;
  auto CI = StructTypes.find(ClassName);
  if (CI == StructTypes.end() || !CI->second.IsClass) {
    LogError(("Unknown class '" + ClassName + "'").c_str());
    return false;
  }
  if (!Traits.count(TraitName)) {
    LogError(("Unknown trait '" + TraitName + "'").c_str());
    return false;
  }
  const auto &TI = Traits.at(TraitName);
  if (!TI.TypeParamName.empty() && !ImplRef.HasTypeArg) {
    LogError(("Trait '" + TraitName + "' requires a type argument").c_str());
    return false;
  }
  if (TI.TypeParamName.empty() && ImplRef.HasTypeArg) {
    LogError(
        ("Trait '" + TraitName + "' does not take type arguments").c_str());
    return false;
  }
  const auto &ClassInfo = CI->second;
  auto ResolveReq = [&](ValueType T,
                        const string &S) -> std::pair<ValueType, string> {
    if (T == ValueType::TypeVar && S == TI.TypeParamName) {
      return {ImplRef.TypeArg, ImplRef.TypeArgStructName};
    }
    return {T, S};
  };
  for (const auto &Req : TI.Methods) {
    auto PI = FunctionSignatures.find(ClassName + "." + Req.Name);
    if (PI == FunctionSignatures.end()) {
      LogError(("Class '" + ClassName + "' does not implement trait '" +
                TraitName + "' method '" + Req.Name + "'")
                   .c_str());
      return false;
    }
    auto MI = ClassInfo.MethodIsPublic.find(Req.Name);
    if (MI == ClassInfo.MethodIsPublic.end() || !MI->second) {
      LogError(("Trait method '" + Req.Name + "' on class '" + ClassName +
                "' must be public")
                   .c_str());
      return false;
    }
    FunctionSignatureNode *P = PI->second.get();
    auto ReqRet = ResolveReq(Req.ReturnType, Req.ReturnStructName);
    if (P->getNumParameters() != Req.Arguments.size() + 1 ||
        P->getReturnType() != ReqRet.first ||
        P->getReturnStructName() != ReqRet.second) {
      LogError(("Method '" + Req.Name + "' on class '" + ClassName +
                "' does not match trait signature")
                   .c_str());
      return false;
    }
    for (size_t I = 0; I < Req.Arguments.size(); ++I) {
      auto ReqArg = ResolveReq(Req.Arguments[I].Type, Req.Arguments[I].StructName);
      if (P->getParameterType(I + 1) != ReqArg.first ||
          P->getParameterStructName(I + 1) != ReqArg.second) {
        LogError(("Method '" + Req.Name + "' on class '" + ClassName +
                  "' does not match trait signature")
                     .c_str());
        return false;
      }
    }
  }
  return true;
}

static bool ParseTypeAliasDefinition() {
  // CurrentToken is 'type'
  getNextToken(); // eat 'type'
  if (CurrentToken != tok_name) {
    LogError("Expected alias name after 'type'");
    return false;
  }
  string AliasName = Name;
  if (TypeAliases.count(AliasName)) {
    LogError(("Type alias '" + AliasName + "' is already defined").c_str());
    return false;
  }
  if (StructTypes.count(AliasName)) {
    LogError(
        ("Name '" + AliasName + "' is already defined as an aggregate type")
            .c_str());
    return false;
  }
  getNextToken(); // eat alias name
  if (CurrentToken != tok_equal) {
    LogError("Expected '=' in type alias");
    return false;
  }
  getNextToken(); // eat '='
  string AliasStructName;
  ValueType AliasType = ParseTypeToken(&AliasStructName);
  if (AliasType == ValueType::Error)
    return false;
  TypeAliases[AliasName] = {AliasType, AliasStructName};
  return true;
}

static bool ParseTraitDefinition() {
  // CurrentToken is 'trait'
  getNextToken(); // eat 'trait'
  if (CurrentToken != tok_name) {
    LogError("Expected trait name");
    return false;
  }
  string TraitName = Name;
  if (Traits.count(TraitName) || StructTypes.count(TraitName) ||
      TypeAliases.count(TraitName)) {
    LogError(("Name '" + TraitName + "' is already defined").c_str());
    return false;
  }
  getNextToken(); // eat trait name
  string TypeParamName;
  if (CurrentToken == tok_lbracket) {
    getNextToken(); // eat '['
    if (CurrentToken != tok_name) {
      LogError("Expected type parameter name in trait definition");
      return false;
    }
    TypeParamName = Name;
    getNextToken(); // eat type parameter name
    if (CurrentToken != tok_rbracket) {
      LogError("Expected ']' after trait type parameter");
      return false;
    }
    getNextToken(); // eat ']'
  }
  if (CurrentToken != tok_colon) {
    LogError("Expected ':' after trait name");
    return false;
  }
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent) {
    LogError("Expected an indented trait body");
    return false;
  }
  getNextToken(); // eat INDENT

  TraitInfo TI;
  TI.Name = TraitName;
  TI.TypeParamName = TypeParamName;
  ActiveTypeParams.clear();
  if (!TypeParamName.empty())
    ActiveTypeParams.insert(TypeParamName);
  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }
    if (CurrentToken != tok_def) {
      LogError("Expected method signature in trait body");
      return false;
    }
    getNextToken(); // eat 'def'
    if (CurrentToken != tok_name) {
      LogError("Expected method name in trait");
      return false;
    }
    string MethodName = Name;
    getNextToken(); // eat method name
    if (CurrentToken != tok_lparen) {
      LogError("Expected '(' in trait method signature");
      return false;
    }
    getNextToken(); // eat '('
    vector<FunctionSignatureNode::ParameterInfo> Arguments;
    if (CurrentToken != tok_rparen) {
      while (true) {
        if (CurrentToken != tok_name) {
          LogError("Expected parameter name in trait method");
          return false;
        }
        string ArgName = Name;
        getNextToken();
        if (CurrentToken != tok_colon) {
          LogError("Trait method parameters require a type annotation");
          return false;
        }
        getNextToken(); // eat ':'
        string ArgStructName;
        ValueType ArgType = ParseTypeToken(&ArgStructName);
        if (ArgType == ValueType::Error || ArgType == ValueType::None) {
          LogError("Invalid trait method parameter type");
          return false;
        }
        Arguments.push_back({ArgName, ArgType, ArgStructName});
        if (CurrentToken == tok_rparen)
          break;
        if (CurrentToken != tok_comma) {
          LogError("Expected ')' or ',' in parameter list");
          return false;
        }
        getNextToken(); // eat ','
      }
    }
    if (CurrentToken != tok_rparen) {
      LogError("Expected ')' in trait method signature");
      return false;
    }
    getNextToken(); // eat ')'

    string RetStructName;
    ValueType RetType =
        ParseOptionalReturnTypeWithStruct(RetStructName, ValueType::None);
    if (RetType == ValueType::Error)
      return false;
    bool Duplicate = false;
    for (const auto &M : TI.Methods) {
      if (M.Name == MethodName) {
        Duplicate = true;
        break;
      }
    }
    if (Duplicate) {
      LogError(("Duplicate trait method '" + MethodName + "'").c_str());
      return false;
    }
    TI.Methods.push_back({MethodName, std::move(Arguments), RetType, RetStructName});
    if (CurrentToken == tok_colon) {
      LogError("Trait methods cannot have a body");
      return false;
    }
    if (CurrentToken == tok_eol)
      consumeNewlines();
  }
  if (CurrentToken != tok_dedent) {
    LogError("Expected dedent after trait body");
    return false;
  }
  ActiveTypeParams.clear();
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end
  Traits[TraitName] = std::move(TI);
  return true;
}

static bool ParseImplDefinition() {
  // CurrentToken is 'impl'
  getNextToken(); // eat 'impl'
  if (CurrentToken != tok_name) {
    LogError("Expected trait name after 'impl'");
    return false;
  }
  string TraitName = Name;
  if (!Traits.count(TraitName)) {
    LogError(("Unknown trait '" + TraitName + "'").c_str());
    return false;
  }
  StructTypeInfo::ImplTraitRef ImplRef;
  ImplRef.TraitName = TraitName;
  getNextToken(); // eat trait name
  const auto &TraitDef = Traits.at(TraitName);
  if (!TraitDef.TypeParamName.empty()) {
    if (CurrentToken != tok_lbracket) {
      LogError(("Trait '" + TraitName + "' requires a type argument").c_str());
      return false;
    }
    getNextToken(); // eat '['
    string TypeArgStruct;
    ValueType TypeArg = ParseTypeToken(&TypeArgStruct);
    if (TypeArg == ValueType::Error || TypeArg == ValueType::None ||
        TypeArg == ValueType::TypeVar) {
      LogError("Invalid trait type argument");
      return false;
    }
    if (CurrentToken != tok_rbracket) {
      LogError("Expected ']' after trait type argument");
      return false;
    }
    getNextToken(); // eat ']'
    ImplRef.HasTypeArg = true;
    ImplRef.TypeArg = TypeArg;
    ImplRef.TypeArgStructName = TypeArgStruct;
  } else if (CurrentToken == tok_lbracket) {
    LogError(
        ("Trait '" + TraitName + "' does not take type arguments").c_str());
    return false;
  }
  if (CurrentToken != tok_for) {
    LogError("Expected 'for' in impl definition");
    return false;
  }
  getNextToken(); // eat 'for'
  if (CurrentToken != tok_name) {
    LogError("Expected class name after 'for'");
    return false;
  }
  string ClassName = Name;
  auto CI = StructTypes.find(ClassName);
  if (CI == StructTypes.end()) {
    LogError(("Unknown class '" + ClassName + "'").c_str());
    return false;
  }
  if (!CI->second.IsClass) {
    LogError(("'" + ClassName +
              "' is a struct, not a class; traits can only be implemented on "
              "classes")
                 .c_str());
    return false;
  }
  getNextToken(); // eat class name
  if (CurrentToken != tok_colon) {
    LogError("Expected ':' in impl definition");
    return false;
  }
  getNextToken(); // eat ':'
  if (CurrentToken == tok_eol)
    consumeNewlines();
  if (CurrentToken != tok_indent) {
    LogError("Expected an indented impl body");
    return false;
  }
  getNextToken(); // eat INDENT

  auto SameImpl = [&](const StructTypeInfo::ImplTraitRef &R) {
    return R.TraitName == ImplRef.TraitName &&
           R.HasTypeArg == ImplRef.HasTypeArg && R.TypeArg == ImplRef.TypeArg &&
           R.TypeArgStructName == ImplRef.TypeArgStructName;
  };
  bool AlreadyImplemented = false;
  for (const auto &R : CI->second.ImplementedTraits) {
    if (SameImpl(R)) {
      AlreadyImplemented = true;
      break;
    }
  }
  if (AlreadyImplemented) {
    LogError(("Trait '" + TraitName + "' is already implemented for class '" +
              ClassName + "'")
                 .c_str());
    return false;
  }

  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }
    if (CurrentToken != tok_def) {
      LogError("Expected method definition in impl body");
      return false;
    }
    auto FnAST = ParseMethodDefinitionInClass(ClassName, /*IsPublic=*/true);
    if (!FnAST)
      return false;
    if (auto *FnIR = FnAST->codegen()) {
      if (ShouldDumpIR())
        FnIR->print(errs());
    }
    if (CurrentToken == tok_eol)
      consumeNewlines();
    else if (CurrentToken == tok_block_end)
      // The method body was itself an indented block; ParseFunctionBody
      // (via ParseBlock) left its own block-end marker in CurrentToken. Consume it
      // here so the loop condition above sees the real next token (another
      // method, or the impl body's own DEDENT) instead of mistaking the
      // method's block-end for the impl body's.
      getNextToken();
  }
  if (CurrentToken != tok_dedent) {
    LogError("Expected dedent after impl body");
    return false;
  }
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface tok_block_end

  bool Present = false;
  for (const auto &R : CI->second.ImplementedTraits) {
    if (SameImpl(R)) {
      Present = true;
      break;
    }
  }
  if (!Present) {
    CI->second.ImplementedTraits.push_back(ImplRef);
  }
  if (!VerifyTraitConformance(ClassName, ImplRef))
    return false;
  return true;
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
static std::map<std::string, ValueType> NamedValueTypes;
static std::map<std::string, string> NamedValueStructNames;
static std::map<std::string, StructType *> LLVMStructTypes;
struct LoopControlTargets {
  BasicBlock *BreakTarget = nullptr;
  BasicBlock *ContinueTarget = nullptr;
};
static std::vector<LoopControlTargets> LoopControlStack;
static std::vector<BasicBlock *> BreakTargetStack;
static unsigned StringLiteralCounter = 0;
// InGlobalInit - True while emitting the synthetic global init function.
static bool InGlobalInit = false;
// ModuleHasGlobals - Tracks whether this module defines any globals.
static bool ModuleHasGlobals = false;
// CurrentSourcePath - Path used in debug info and diagnostics.
static std::string CurrentSourcePath = "<stdin>";
// DIB - DIBuilder used to emit DWARF metadata into the module.
static std::unique_ptr<DIBuilder> DIB;
// TheCU - Compile unit metadata node (one per module).
static DICompileUnit *TheCU = nullptr;
// TheDIFile - Current source file metadata node.
static DIFile *TheDIFile = nullptr;
// IntDIType - Debug info type for platform int.
static DIType *IntDIType = nullptr;
// Float64DIType - Debug info type for float64.
static DIType *Float64DIType = nullptr;
// VoidDIType - Debug info type for None/void.
static DIType *VoidDIType = nullptr;
// Int8DIType - Debug info type for int8.
static DIType *Int8DIType = nullptr;
// Int16DIType - Debug info type for int16.
static DIType *Int16DIType = nullptr;
// Int32DIType - Debug info type for int32.
static DIType *Int32DIType = nullptr;
// Int64DIType - Debug info type for int64.
static DIType *Int64DIType = nullptr;
// Float32DIType - Debug info type for float32.
static DIType *Float32DIType = nullptr;
// BoolDIType - Debug info type for bool.
static DIType *BoolDIType = nullptr;
// PtrDIType - Debug info type for pointers.
static DIType *PtrDIType = nullptr;
// CurDIScope - Current debug scope (function or block).
static DIScope *CurDIScope = nullptr;
// CurFunctionLine - Line number for current function definition.
static unsigned CurFunctionLine = 1;
// TheJIT - ORC JIT instance for REPL execution.
static std::unique_ptr<PyxcJIT> TheJIT;
// TheFPM - Per-function optimization pipeline (JIT).
static std::unique_ptr<FunctionPassManager> TheFPM;
// TheMPM - Per-module optimization pipeline (emit mode).
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
  case ValueType::Array:
    return "array";
  case ValueType::Pointer:
    return "ptr";
  case ValueType::TypeVar:
    return "typevar";
  default:
    return "<error>";
  }
}

static string EncodePointerType(ValueType PointeeType,
                                const string &PointeeStructName) {
  return std::to_string(static_cast<int>(PointeeType)) + ":" +
         PointeeStructName;
}

static bool DecodePointerType(const string &Encoded, ValueType &PointeeType,
                              string &PointeeStructName) {
  auto Pos = Encoded.find(':');
  if (Pos == string::npos)
    return false;
  int Raw = std::atoi(Encoded.substr(0, Pos).c_str());
  if (Raw < static_cast<int>(ValueType::None) ||
      Raw > static_cast<int>(ValueType::Pointer))
    return false;
  PointeeType = static_cast<ValueType>(Raw);
  PointeeStructName = Encoded.substr(Pos + 1);
  return true;
}

static string EncodeArrayType(ValueType ElemType, const string &ElemStructName,
                              uint64_t Count) {
  return std::to_string(static_cast<int>(ElemType)) + ":" + ElemStructName +
         ":" + std::to_string(Count);
}

static bool DecodeArrayType(const string &Encoded, ValueType &ElemType,
                            string &ElemStructName, uint64_t &Count) {
  auto Last = Encoded.rfind(':');
  if (Last == string::npos)
    return false;
  auto First = Encoded.find(':');
  if (First == string::npos || First == Last)
    return false;
  int Raw = std::atoi(Encoded.substr(0, First).c_str());
  if (Raw < static_cast<int>(ValueType::None) ||
      Raw > static_cast<int>(ValueType::Pointer))
    return false;
  // ParseTypeToken rejects nested arrays, so array-of-array metadata should
  // never appear in valid source.
  if (Raw == static_cast<int>(ValueType::Array))
    return false;
  ElemType = static_cast<ValueType>(Raw);
  ElemStructName = Encoded.substr(First + 1, Last - First - 1);
  if (!ParseUnsignedDecimal(Encoded.substr(Last + 1), Count))
    return false;
  return Count > 0;
}

static bool ArrayDecaysToPointerType(const string &ArrayInfo,
                                     const string &PointerInfo) {
  ValueType ElemType = ValueType::Error;
  string ElemStructName;
  uint64_t Count = 0;
  ValueType PointeeType = ValueType::Error;
  string PointeeStructName;
  if (!DecodeArrayType(ArrayInfo, ElemType, ElemStructName, Count))
    return false;
  if (!DecodePointerType(PointerInfo, PointeeType, PointeeStructName))
    return false;
  return ElemType == PointeeType && ElemStructName == PointeeStructName;
}

static bool ParseUnsignedDecimal(const string &Text, uint64_t &Out) {
  if (Text.empty())
    return false;
  uint64_t V = 0;
  for (char C : Text) {
    if (C < '0' || C > '9')
      return false;
    uint64_t D = static_cast<uint64_t>(C - '0');
    if (V > (std::numeric_limits<uint64_t>::max() - D) / 10)
      return false;
    V = V * 10 + D;
  }
  Out = V;
  return true;
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

static bool IsSignedIntType(ValueType Type) {
  return IsIntType(Type) && !IsUnsignedIntType(Type);
}

static bool IsFloatType(ValueType Type) {
  return Type == ValueType::Float || Type == ValueType::Float32 ||
         Type == ValueType::Float64;
}

static bool IsNumericType(ValueType Type) {
  return IsIntType(Type) || IsFloatType(Type);
}

static Type *GetOrCreateLLVMStructType(const string &StructName) {
  auto It = LLVMStructTypes.find(StructName);
  if (It != LLVMStructTypes.end())
    return It->second;
  auto DefIt = StructTypes.find(StructName);
  if (DefIt == StructTypes.end())
    return nullptr;
  // LLVM named aggregate types use a conventional "struct." prefix here for
  // both source-level 'struct' and 'class' in chapter 24. They are layout-
  // equivalent at this stage; chapter 25 can layer semantic distinctions.
  auto *ST = StructType::create(*TheContext, "struct." + StructName);
  LLVMStructTypes[StructName] = ST;
  std::vector<Type *> FieldTys;
  FieldTys.reserve(DefIt->second.Fields.size());
  for (const auto &Field : DefIt->second.Fields) {
    Type *FT = LLVMTypeFor(Field.Type, Field.StructName);
    if (!FT)
      return nullptr;
    FieldTys.push_back(FT);
  }
  ST->setBody(FieldTys, false);
  return ST;
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
    ValueType ElemType = ValueType::Error;
    string ElemStructName;
    uint64_t Count = 0;
    if (!DecodeArrayType(StructName, ElemType, ElemStructName, Count))
      return nullptr;
    llvm::Type *ElemLLVM = LLVMTypeFor(ElemType, ElemStructName);
    if (!ElemLLVM)
      return nullptr;
    return ArrayType::get(ElemLLVM, Count);
  }
  case ValueType::Pointer:
    return PointerType::getUnqual(*TheContext);
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
  case ValueType::Float:
    return Float64DIType;
  case ValueType::Float64:
    return Float64DIType;
  case ValueType::None:
    return VoidDIType;
  case ValueType::Int8:
    return Int8DIType;
  case ValueType::Int16:
    return Int16DIType;
  case ValueType::Int32:
    return Int32DIType;
  case ValueType::Int64:
    return Int64DIType;
  case ValueType::Float32:
    return Float32DIType;
  case ValueType::Bool:
    return BoolDIType;
  case ValueType::Pointer:
    return PtrDIType;
  case ValueType::Array:
    return nullptr;
  default:
    return nullptr;
  }
}

static bool IsAssignable(ValueType Dest, ValueType Src) {
  if (Dest == ValueType::Array || Src == ValueType::Array)
    return false;
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

/// LogErrorV - Codegen-level error helper. Delegates to LogError for printing,
/// then returns nullptr so codegen callers can write: return LogErrorV("msg");
Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

/// CreateEntryBlockAlloca - Create a stack slot in the current function's
/// entry block for a mutable variable.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VarName, ValueType Type,
                                          const string &StructName = "") {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(LLVMTypeFor(Type, StructName), nullptr, VarName);
}

static Constant *ZeroConstant(ValueType Type, const string &StructName = "") {
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
  case ValueType::Struct:
    return Constant::getNullValue(LLVMTypeFor(Type, StructName));
  case ValueType::Array:
    return ConstantAggregateZero::get(LLVMTypeFor(Type, StructName));
  case ValueType::Pointer:
    return ConstantPointerNull::get(
        cast<PointerType>(LLVMTypeFor(ValueType::Pointer)));
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
  if (From == ValueType::Pointer && To == ValueType::Pointer)
    return Builder->CreateBitCast(V, LLVMTypeFor(ValueType::Pointer),
                                  "ptrcast");
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
  if (!DebugInfo) {
    DIB.reset();
    TheCU = nullptr;
    TheDIFile = nullptr;
    IntDIType = nullptr;
    Float64DIType = nullptr;
    VoidDIType = nullptr;
    Int8DIType = nullptr;
    Int16DIType = nullptr;
    Int32DIType = nullptr;
    Int64DIType = nullptr;
    Float32DIType = nullptr;
    BoolDIType = nullptr;
    PtrDIType = nullptr;
    return;
  }

  DIB = std::make_unique<DIBuilder>(*TheModule);

  StringRef FullPath(CurrentSourcePath);
  StringRef FileName = sys::path::filename(FullPath);
  StringRef Dir = sys::path::parent_path(FullPath);
  if (Dir.empty())
    Dir = ".";

  TheDIFile = DIB->createFile(FileName, Dir);
  bool IsOptimized = OptLevel != 0;
  TheCU = DIB->createCompileUnit(dwarf::DW_LANG_C, TheDIFile, "pyxc",
                                 IsOptimized, "", 0);
  unsigned bits = TheModule->getDataLayout().getPointerSizeInBits();
  IntDIType = DIB->createBasicType("int", bits, dwarf::DW_ATE_signed);
  Float64DIType = DIB->createBasicType("float64", 64, dwarf::DW_ATE_float);
  VoidDIType = DIB->createUnspecifiedType("None");
  Int8DIType = DIB->createBasicType("int8", 8, dwarf::DW_ATE_signed);
  Int16DIType = DIB->createBasicType("int16", 16, dwarf::DW_ATE_signed);
  Int32DIType = DIB->createBasicType("int32", 32, dwarf::DW_ATE_signed);
  Int64DIType = DIB->createBasicType("int64", 64, dwarf::DW_ATE_signed);
  Float32DIType = DIB->createBasicType("float32", 32, dwarf::DW_ATE_float);
  BoolDIType = DIB->createBasicType("bool", 1, dwarf::DW_ATE_boolean);
  PtrDIType = DIB->createPointerType(Int8DIType, bits);

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
                             bool IsParam, unsigned ArgNo = 0,
                             ValueType Type = ValueType::Float64) {
  if (!DIB || !CurDIScope || !Alloca)
    return;

  DIType *DIType = DITypeFor(Type);
  if (!DIType)
    DIType = Float64DIType
                 ? Float64DIType
                 : DIB->createBasicType("float64", 64, dwarf::DW_ATE_float);
  auto *Loc = DILocation::get(*TheContext, Line, 1, CurDIScope);
  DILocalVariable *Var = nullptr;
  if (IsParam) {
    Var = DIB->createParameterVariable(CurDIScope, Name, ArgNo, TheDIFile, Line,
                                       DIType, true);
  } else {
    Var = DIB->createAutoVariable(CurDIScope, Name, TheDIFile, Line, DIType,
                                  true);
  }

  DIB->insertDeclare(Alloca, Var, DIB->createExpression(), Loc,
                     Builder->GetInsertBlock());
}

static void EmitDebugGlobal(GlobalVariable *GV, StringRef Name, unsigned Line,
                            ValueType Type) {
  if (!DIB || !TheCU || !GV)
    return;
  DIType *DIType = DITypeFor(Type);
  if (!DIType)
    DIType = Float64DIType
                 ? Float64DIType
                 : DIB->createBasicType("float64", 64, dwarf::DW_ATE_float);
  auto *GVE = DIB->createGlobalVariableExpression(TheCU, Name, Name, TheDIFile,
                                                  Line, DIType, true);
  GV->addDebugInfo(GVE);
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
  auto *Type = LLVMTypeFor(GlobalVarTypes[Name], GlobalVarStructTypes[Name]);
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

Value *StringExpressionNode::codegen() {
  auto *I8Ty = Type::getInt8Ty(*TheContext);
  auto *ArrTy = ArrayType::get(I8Ty, Text.size() + 1);
  auto *Init = ConstantDataArray::getString(*TheContext, Text, true);
  string Name = ".str." + to_string(StringLiteralCounter++);
  auto *GV = new GlobalVariable(*TheModule, ArrTy, true,
                                GlobalValue::PrivateLinkage, Init, Name);
  GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(Align(1));
  ModuleHasGlobals = true;

  Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  return Builder->CreateInBoundsGEP(ArrTy, GV, {Zero, Zero}, "strptr");
}

Value *ArrayLiteralExpressionNode::codegen() {
  ValueType ElemType = ValueType::Error;
  string ElemStructName;
  uint64_t Count = 0;
  if (!DecodeArrayType(getStructName(), ElemType, ElemStructName, Count))
    return LogErrorV("Invalid array literal type");
  if (Elements.size() != Count)
    return LogErrorV("Array literal element count mismatch");

  auto *ArrTy = dyn_cast<ArrayType>(LLVMTypeFor(getType(), getStructName()));
  if (!ArrTy)
    return LogErrorV("Invalid array LLVM type");

  Value *Agg = UndefValue::get(ArrTy);
  for (size_t I = 0; I < Elements.size(); ++I) {
    Value *Elem = Elements[I]->codegen();
    if (!Elem)
      return nullptr;
    Elem = EmitImplicitCast(Elem, Elements[I]->getType(), ElemType);
    if (!Elem)
      return nullptr;
    Agg = Builder->CreateInsertValue(Agg, Elem, {static_cast<unsigned>(I)},
                                     "arr.ins");
  }
  return Agg;
}

/// NameExpressionNode::codegen - A variable reference loads the current value
/// from the variable's stack slot.
Value *NameExpressionNode::codegen() {
  auto DecayArray = [&](Value *BasePtr) -> Value * {
    if (getType() != ValueType::Array)
      return BasePtr;
    auto *ArrTy = LLVMTypeFor(getType(), getStructName());
    Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
    return Builder->CreateInBoundsGEP(ArrTy, BasePtr, {Zero, Zero},
                                      "arraydecay");
  };

  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second)
    return (getType() == ValueType::Array)
               ? DecayArray(It->second)
               : Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()),
                                     It->second, Name.c_str());

  if (auto *GV = GetGlobalVariable(Name))
    return (getType() == ValueType::Array)
               ? DecayArray(GV)
               : Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()),
                                     GV, Name.c_str());

  return LogErrorV("Unknown variable name");
}

static Value *LoadPointerValue(const string &BaseName,
                               const vector<string> &FieldPath,
                               ValueType BaseType,
                               const string &BaseStructName);

static Value *GetFieldAddress(const string &BaseName,
                              const vector<string> &FieldPath,
                              ValueType *OutType = nullptr,
                              string *OutStructName = nullptr) {
  Value *BasePtr = nullptr;
  ValueType BaseType = ValueType::Error;
  string BaseStruct;
  auto It = NamedValues.find(BaseName);
  if (It != NamedValues.end() && It->second) {
    BasePtr = It->second;
    auto TI = NamedValueTypes.find(BaseName);
    if (TI != NamedValueTypes.end())
      BaseType = TI->second;
    auto SI = NamedValueStructNames.find(BaseName);
    if (SI != NamedValueStructNames.end())
      BaseStruct = SI->second;
  } else if (auto *GV = GetGlobalVariable(BaseName)) {
    BasePtr = GV;
    auto TI = GlobalVarTypes.find(BaseName);
    if (TI != GlobalVarTypes.end())
      BaseType = TI->second;
    auto SI = GlobalVarStructTypes.find(BaseName);
    if (SI != GlobalVarStructTypes.end())
      BaseStruct = SI->second;
  }
  if (!BasePtr)
    return nullptr;
  if (BaseType == ValueType::Pointer) {
    ValueType PointeeType = ValueType::Error;
    string PointeeStruct;
    if (!DecodePointerType(BaseStruct, PointeeType, PointeeStruct) ||
        PointeeType != ValueType::Struct)
      return nullptr;
    BasePtr = Builder->CreateLoad(LLVMTypeFor(BaseType, BaseStruct), BasePtr,
                                  (BaseName + ".ptr").c_str());
    if (!BasePtr)
      return nullptr;
    BaseType = ValueType::Struct;
    BaseStruct = PointeeStruct;
  }
  if (BaseType != ValueType::Struct || BaseStruct.empty())
    return nullptr;

  Value *Ptr = BasePtr;
  ValueType CurType = BaseType;
  string CurStruct = BaseStruct;
  for (const auto &FieldName : FieldPath) {
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end())
      return nullptr;
    auto FI = SI->second.FieldIndex.find(FieldName);
    if (FI == SI->second.FieldIndex.end())
      return nullptr;
    const auto &FD = SI->second.Fields[FI->second];
    llvm::Type *BaseLLVM = LLVMTypeFor(CurType, CurStruct);
    Ptr = Builder->CreateStructGEP(BaseLLVM, Ptr, FI->second, "fieldptr");
    CurType = FD.Type;
    CurStruct = FD.StructName;
  }
  if (OutType)
    *OutType = CurType;
  if (OutStructName)
    *OutStructName = CurStruct;
  return Ptr;
}

Value *FieldExpressionNode::codegen() {
  ValueType LeafType = ValueType::Error;
  string LeafStruct;
  Value *Ptr =
      GetFieldAddress(*getLValueName(), FieldPath, &LeafType, &LeafStruct);
  if (!Ptr)
    return LogErrorV("Unknown field access");
  return Builder->CreateLoad(LLVMTypeFor(LeafType, LeafStruct), Ptr,
                             "fieldload");
}

Value *AddrExpressionNode::codegen() {
  if (FieldPath.empty()) {
    auto It = NamedValues.find(BaseName);
    if (It != NamedValues.end() && It->second)
      return It->second;
    if (auto *GV = GetGlobalVariable(BaseName))
      return GV;
    return LogErrorV("Unknown variable name");
  }
  Value *Ptr = GetFieldAddress(BaseName, FieldPath);
  if (!Ptr)
    return LogErrorV("Unknown field access");
  return Ptr;
}

static Value *LoadPointerValue(const string &BaseName,
                               const vector<string> &FieldPath,
                               ValueType &PtrType, string &PtrStructName) {
  if (FieldPath.empty()) {
    auto It = NamedValues.find(BaseName);
    if (It != NamedValues.end() && It->second) {
      PtrType = NamedValueTypes[BaseName];
      PtrStructName = NamedValueStructNames[BaseName];
      if (PtrType != ValueType::Pointer)
        return nullptr;
      return Builder->CreateLoad(LLVMTypeFor(ValueType::Pointer), It->second,
                                 "ptrload");
    }
    if (auto *GV = GetGlobalVariable(BaseName)) {
      PtrType = GlobalVarTypes[BaseName];
      PtrStructName = GlobalVarStructTypes[BaseName];
      if (PtrType != ValueType::Pointer)
        return nullptr;
      return Builder->CreateLoad(LLVMTypeFor(ValueType::Pointer), GV,
                                 "ptrload");
    }
    return nullptr;
  }
  Value *PtrAddr =
      GetFieldAddress(BaseName, FieldPath, &PtrType, &PtrStructName);
  if (!PtrAddr || PtrType != ValueType::Pointer)
    return nullptr;
  return Builder->CreateLoad(LLVMTypeFor(ValueType::Pointer), PtrAddr,
                             "ptrload");
}

static Value *BuildIndexElementPtr(IndexExpressionNode *IdxExpr) {
  ValueType PtrType = ValueType::Error;
  string PtrStructName;
  Value *BasePtr = nullptr;
  if (IdxExpr->getFieldPath().empty()) {
    auto It = NamedValues.find(IdxExpr->getBaseName());
    if (It != NamedValues.end() && It->second) {
      PtrType = NamedValueTypes[IdxExpr->getBaseName()];
      PtrStructName = NamedValueStructNames[IdxExpr->getBaseName()];
      if (PtrType == ValueType::Pointer) {
        BasePtr = Builder->CreateLoad(LLVMTypeFor(ValueType::Pointer),
                                      It->second, "ptrload");
      } else if (PtrType == ValueType::Array) {
        Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
        auto *ArrTy = LLVMTypeFor(PtrType, PtrStructName);
        BasePtr = Builder->CreateInBoundsGEP(ArrTy, It->second, {Zero, Zero},
                                             "arraydecay");
      }
    } else if (auto *GV = GetGlobalVariable(IdxExpr->getBaseName())) {
      PtrType = GlobalVarTypes[IdxExpr->getBaseName()];
      PtrStructName = GlobalVarStructTypes[IdxExpr->getBaseName()];
      if (PtrType == ValueType::Pointer) {
        BasePtr =
            Builder->CreateLoad(LLVMTypeFor(ValueType::Pointer), GV, "ptrload");
      } else if (PtrType == ValueType::Array) {
        Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
        auto *ArrTy = LLVMTypeFor(PtrType, PtrStructName);
        BasePtr =
            Builder->CreateInBoundsGEP(ArrTy, GV, {Zero, Zero}, "arraydecay");
      }
    }
  } else {
    BasePtr = LoadPointerValue(IdxExpr->getBaseName(), IdxExpr->getFieldPath(),
                               PtrType, PtrStructName);
  }
  if (!BasePtr)
    return LogErrorV("Indexing requires a pointer or array value");
  Value *IdxVal = IdxExpr->getIndex()->codegen();
  if (!IdxVal)
    return nullptr;
  if (!IsIntType(IdxExpr->getIndex()->getType()))
    return LogErrorV("Pointer index must be an integer");
  if (IdxExpr->getIndex()->getType() != ValueType::Int64) {
    IdxVal = EmitImplicitCast(IdxVal, IdxExpr->getIndex()->getType(),
                              ValueType::Int64);
    if (!IdxVal)
      return LogErrorV("Index must be an integer");
  }
  return Builder->CreateInBoundsGEP(
      LLVMTypeFor(IdxExpr->getType(), IdxExpr->getStructName()), BasePtr,
      IdxVal, "elemptr");
}

static Value *BuildIndexedFieldPtr(IndexedFieldExpressionNode *Expr,
                                   ValueType *LeafType,
                                   string *LeafStructName) {
  Value *BaseElemPtr = BuildIndexElementPtr(Expr->getBaseIndex());
  if (!BaseElemPtr)
    return nullptr;
  Value *Ptr = BaseElemPtr;
  ValueType CurType = Expr->getBaseIndex()->getType();
  string CurStruct = Expr->getBaseIndex()->getStructName();
  for (const auto &FieldName : Expr->getFieldPath()) {
    if (CurType != ValueType::Struct || CurStruct.empty())
      return nullptr;
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end())
      return nullptr;
    auto FI = SI->second.FieldIndex.find(FieldName);
    if (FI == SI->second.FieldIndex.end())
      return nullptr;
    const auto &FD = SI->second.Fields[FI->second];
    llvm::Type *BaseLLVM = LLVMTypeFor(CurType, CurStruct);
    Ptr = Builder->CreateStructGEP(BaseLLVM, Ptr, FI->second, "fieldptr");
    CurType = FD.Type;
    CurStruct = FD.StructName;
  }
  if (LeafType)
    *LeafType = CurType;
  if (LeafStructName)
    *LeafStructName = CurStruct;
  return Ptr;
}

static Value *ResolveIncDecLValuePtr(ExpressionNode *Operand, ValueType *Ty,
                                     string *StructName) {
  if (auto *Var = dynamic_cast<NameExpressionNode *>(Operand)) {
    const string &Name = Var->getName();
    auto It = NamedValues.find(Name);
    if (It != NamedValues.end() && It->second) {
      if (Ty)
        *Ty = Var->getType();
      if (StructName)
        *StructName = Var->getStructName();
      return It->second;
    }
    if (auto *GV = GetGlobalVariable(Name)) {
      if (Ty)
        *Ty = Var->getType();
      if (StructName)
        *StructName = Var->getStructName();
      return GV;
    }
    return nullptr;
  }
  if (auto *Field = dynamic_cast<FieldExpressionNode *>(Operand))
    return GetFieldAddress(*Field->getLValueName(), Field->getFieldPath(), Ty,
                           StructName);
  if (auto *Idx = dynamic_cast<IndexExpressionNode *>(Operand)) {
    if (Ty)
      *Ty = Idx->getType();
    if (StructName)
      *StructName = Idx->getStructName();
    return BuildIndexElementPtr(Idx);
  }
  if (auto *IdxField = dynamic_cast<IndexedFieldExpressionNode *>(Operand))
    return BuildIndexedFieldPtr(IdxField, Ty, StructName);
  return nullptr;
}

static Value *EmitBuiltInArithmetic(int Operator, Value *L, ValueType LType,
                                    const string &LStruct, Value *R,
                                    ValueType RType, const string &RStruct,
                                    ValueType ResultType,
                                    const string &ResultStruct) {
  if ((Operator == tok_plus || Operator == tok_minus) &&
      ((LType == ValueType::Pointer && IsIntType(RType)) ||
       (RType == ValueType::Pointer && IsIntType(LType)))) {
    Value *Ptr = nullptr;
    Value *Idx = nullptr;
    if (LType == ValueType::Pointer && IsIntType(RType) && Operator == tok_plus) {
      Ptr = L;
      Idx = EmitImplicitCast(R, RType, ValueType::Int64);
    } else if (RType == ValueType::Pointer && IsIntType(LType) && Operator == tok_plus) {
      Ptr = R;
      Idx = EmitImplicitCast(L, LType, ValueType::Int64);
    } else if (LType == ValueType::Pointer && IsIntType(RType) && Operator == tok_minus) {
      Ptr = L;
      Idx = EmitImplicitCast(R, RType, ValueType::Int64);
      if (Idx)
        Idx = Builder->CreateNeg(Idx, "negidx");
    }
    if (!Ptr || !Idx)
      return LogErrorV("Type mismatch in assignment");
    ValueType ElemType = ValueType::Error;
    string ElemStruct;
    if (!DecodePointerType(ResultStruct, ElemType, ElemStruct))
      return LogErrorV("Invalid pointer type metadata");
    llvm::Type *ElemLLVM = LLVMTypeFor(ElemType, ElemStruct);
    if (!ElemLLVM)
      return LogErrorV("Invalid pointer element type");
    return Builder->CreateInBoundsGEP(ElemLLVM, Ptr, Idx, "ptrarith");
  }

  if (Operator == tok_minus && ResultType == ValueType::Int64 &&
      LType == ValueType::Pointer && RType == ValueType::Pointer &&
      LStruct == RStruct) {
    ValueType ElemType = ValueType::Error;
    string ElemStruct;
    if (!DecodePointerType(LStruct, ElemType, ElemStruct))
      return LogErrorV("Invalid pointer type metadata");
    llvm::Type *ElemLLVM = LLVMTypeFor(ElemType, ElemStruct);
    if (!ElemLLVM)
      return LogErrorV("Invalid pointer element type");
    return Builder->CreatePtrDiff(ElemLLVM, L, R, "ptrdiff");
  }

  L = EmitImplicitCast(L, LType, ResultType);
  R = EmitImplicitCast(R, RType, ResultType);
  if (!L || !R)
    return LogErrorV("Type mismatch in assignment");
  if (IsFloatType(ResultType)) {
    if (Operator == tok_plus)
      return Builder->CreateFAdd(L, R, "addtmp");
    if (Operator == tok_minus)
      return Builder->CreateFSub(L, R, "subtmp");
    if (Operator == tok_star)
      return Builder->CreateFMul(L, R, "multmp");
    return Builder->CreateFDiv(L, R, "divtmp");
  }
  if (Operator == tok_plus)
    return Builder->CreateAdd(L, R, "addtmp");
  if (Operator == tok_minus)
    return Builder->CreateSub(L, R, "subtmp");
  if (Operator == tok_star)
    return Builder->CreateMul(L, R, "multmp");
  if (Operator == tok_slash)
    return IsUnsignedIntType(ResultType) ? Builder->CreateUDiv(L, R, "divtmp")
                                         : Builder->CreateSDiv(L, R, "divtmp");
  return IsUnsignedIntType(ResultType) ? Builder->CreateURem(L, R, "modtmp")
                                       : Builder->CreateSRem(L, R, "modtmp");
}

Value *IndexExpressionNode::codegen() {
  Value *ElemPtr = BuildIndexElementPtr(this);
  if (!ElemPtr)
    return nullptr;
  return Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()), ElemPtr,
                             "elemload");
}

Value *IndexAssignmentExpressionNode::codegen() {
  Value *ElemPtr = BuildIndexElementPtr(Left.get());
  if (!ElemPtr)
    return nullptr;
  Value *Val = Right->codegen();
  if (!Val)
    return nullptr;
  Val = EmitImplicitCast(Val, Right->getType(), getType());
  if (!Val)
    return LogErrorV("Type mismatch in assignment");
  Builder->CreateStore(Val, ElemPtr);
  return Val;
}

Value *IndexCompoundAssignmentExpressionNode::codegen() {
  Value *ElemPtr = BuildIndexElementPtr(Left.get());
  if (!ElemPtr)
    return nullptr;
  Value *L = Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()),
                                 ElemPtr, "elemload");
  Value *R = Right->codegen();
  if (!R)
    return nullptr;
  Value *Combined = EmitBuiltInArithmetic(Operator, L, getType(), getStructName(), R,
                                          Right->getType(), Right->getStructName(),
                                          getType(), getStructName());
  if (!Combined)
    return nullptr;
  Builder->CreateStore(Combined, ElemPtr);
  return Combined;
}

Value *IndexedFieldExpressionNode::codegen() {
  Value *BaseElemPtr = BuildIndexElementPtr(BaseIndex.get());
  if (!BaseElemPtr)
    return nullptr;
  Value *Ptr = BaseElemPtr;
  ValueType CurType = BaseIndex->getType();
  string CurStruct = BaseIndex->getStructName();
  for (const auto &FieldName : FieldPath) {
    if (CurType != ValueType::Struct || CurStruct.empty())
      return LogErrorV("Field access requires a struct value");
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end())
      return LogErrorV("Unknown struct type in field access");
    auto FI = SI->second.FieldIndex.find(FieldName);
    if (FI == SI->second.FieldIndex.end())
      return LogErrorV("Unknown field access");
    const auto &FD = SI->second.Fields[FI->second];
    llvm::Type *BaseLLVM = LLVMTypeFor(CurType, CurStruct);
    Ptr = Builder->CreateStructGEP(BaseLLVM, Ptr, FI->second, "fieldptr");
    CurType = FD.Type;
    CurStruct = FD.StructName;
  }
  return Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()), Ptr,
                             "fieldload");
}

Value *IndexedFieldAssignmentExpressionNode::codegen() {
  Value *BaseElemPtr = BuildIndexElementPtr(Left->getBaseIndex());
  if (!BaseElemPtr)
    return nullptr;
  Value *Ptr = BaseElemPtr;
  ValueType CurType = Left->getBaseIndex()->getType();
  string CurStruct = Left->getBaseIndex()->getStructName();
  for (const auto &FieldName : Left->getFieldPath()) {
    if (CurType != ValueType::Struct || CurStruct.empty())
      return LogErrorV("Field access requires a struct value");
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end())
      return LogErrorV("Unknown struct type in field access");
    auto FI = SI->second.FieldIndex.find(FieldName);
    if (FI == SI->second.FieldIndex.end())
      return LogErrorV("Unknown field access");
    const auto &FD = SI->second.Fields[FI->second];
    llvm::Type *BaseLLVM = LLVMTypeFor(CurType, CurStruct);
    Ptr = Builder->CreateStructGEP(BaseLLVM, Ptr, FI->second, "fieldptr");
    CurType = FD.Type;
    CurStruct = FD.StructName;
  }
  Value *Val = Right->codegen();
  if (!Val)
    return nullptr;
  Val = EmitImplicitCast(Val, Right->getType(), getType());
  if (!Val)
    return LogErrorV("Type mismatch in assignment");
  Builder->CreateStore(Val, Ptr);
  return Val;
}

Value *IndexedFieldCompoundAssignmentExpressionNode::codegen() {
  Value *BaseElemPtr = BuildIndexElementPtr(Left->getBaseIndex());
  if (!BaseElemPtr)
    return nullptr;
  Value *Ptr = BaseElemPtr;
  ValueType CurType = Left->getBaseIndex()->getType();
  string CurStruct = Left->getBaseIndex()->getStructName();
  for (const auto &FieldName : Left->getFieldPath()) {
    if (CurType != ValueType::Struct || CurStruct.empty())
      return LogErrorV("Field access requires a struct value");
    auto SI = StructTypes.find(CurStruct);
    if (SI == StructTypes.end())
      return LogErrorV("Unknown struct type in field access");
    auto FI = SI->second.FieldIndex.find(FieldName);
    if (FI == SI->second.FieldIndex.end())
      return LogErrorV("Unknown field access");
    const auto &FD = SI->second.Fields[FI->second];
    llvm::Type *BaseLLVM = LLVMTypeFor(CurType, CurStruct);
    Ptr = Builder->CreateStructGEP(BaseLLVM, Ptr, FI->second, "fieldptr");
    CurType = FD.Type;
    CurStruct = FD.StructName;
  }
  Value *L = Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()), Ptr,
                                 "fieldload");
  Value *R = Right->codegen();
  if (!R)
    return nullptr;
  Value *Combined = EmitBuiltInArithmetic(Operator, L, getType(), getStructName(), R,
                                          Right->getType(), Right->getStructName(),
                                          getType(), getStructName());
  if (!Combined)
    return nullptr;
  Builder->CreateStore(Combined, Ptr);
  return Combined;
}

/// AssignmentExpressionNode::codegen - Evaluate the Right, store it into the variable's
/// stack slot, and produce the assigned value.
Value *AssignmentExpressionNode::codegen() {
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

Value *CompoundAssignmentExpressionNode::codegen() {
  Value *R = Right->codegen();
  if (!R)
    return nullptr;

  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second) {
    Value *L = Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()),
                                   It->second, Name);
    Value *Combined = EmitBuiltInArithmetic(
        Operator, L, getType(), getStructName(), R, Right->getType(),
        Right->getStructName(), getType(), getStructName());
    if (!Combined)
      return nullptr;
    Builder->CreateStore(Combined, It->second);
    return Combined;
  }
  if (auto *GV = GetGlobalVariable(Name)) {
    Value *L =
        Builder->CreateLoad(LLVMTypeFor(getType(), getStructName()), GV, Name);
    Value *Combined = EmitBuiltInArithmetic(
        Operator, L, getType(), getStructName(), R, Right->getType(),
        Right->getStructName(), getType(), getStructName());
    if (!Combined)
      return nullptr;
    Builder->CreateStore(Combined, GV);
    return Combined;
  }
  return LogErrorV("Unknown variable name");
}

Value *FieldAssignmentExpressionNode::codegen() {
  ValueType DestType = ValueType::Error;
  string DestStruct;
  Value *Ptr = GetFieldAddress(*Left->getLValueName(), Left->getFieldPath(),
                               &DestType, &DestStruct);
  if (!Ptr)
    return LogErrorV("Unknown field access");
  Value *Val = Right->codegen();
  if (!Val)
    return nullptr;
  Val = EmitImplicitCast(Val, Right->getType(), DestType);
  if (!Val)
    return LogErrorV("Type mismatch in assignment");
  Builder->CreateStore(Val, Ptr);
  return Val;
}

Value *FieldCompoundAssignmentExpressionNode::codegen() {
  ValueType DestType = ValueType::Error;
  string DestStruct;
  Value *Ptr = GetFieldAddress(*Left->getLValueName(), Left->getFieldPath(),
                               &DestType, &DestStruct);
  if (!Ptr)
    return LogErrorV("Unknown field access");
  Value *L =
      Builder->CreateLoad(LLVMTypeFor(DestType, DestStruct), Ptr, "fieldload");
  Value *R = Right->codegen();
  if (!R)
    return nullptr;
  Value *Combined =
      EmitBuiltInArithmetic(Operator, L, DestType, DestStruct, R, Right->getType(),
                            Right->getStructName(), getType(), getStructName());
  if (!Combined)
    return nullptr;
  Builder->CreateStore(Combined, Ptr);
  return Combined;
}

/// ReturnExpressionNode::codegen - Emit a return from the current function.
Value *ReturnExpressionNode::codegen() {
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

/// BlockExpressionNode::codegen - Evaluate statements in order.
/// Saves and restores NamedValues to implement block scoping: variables
/// declared inside the block are not visible after it exits.
Value *BlockExpressionNode::codegen() {
  auto SavedBindings = NamedValues;
  auto SavedTypes = NamedValueTypes;
  auto SavedStructs = NamedValueStructNames;

  Value *Last = nullptr;
  for (auto &Stmt : Stmts) {
    if (Builder->GetInsertBlock()->getTerminator())
      break;
    Last = Stmt->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      NamedValueTypes = SavedTypes;
      NamedValueStructNames = SavedStructs;
      return nullptr;
    }
  }

  NamedValues = SavedBindings;
  NamedValueTypes = SavedTypes;
  NamedValueStructNames = SavedStructs;

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
    Value *L = Left->codegen();
    if (!L)
      return nullptr;
    if (Left->getType() != ValueType::Bool || Right->getType() != ValueType::Bool)
      return LogErrorV("Type mismatch in binary operator");

    Function *F = Builder->GetInsertBlock()->getParent();
    BasicBlock *LHSBB = Builder->GetInsertBlock();
    BasicBlock *RHSBB = BasicBlock::Create(*TheContext, "logic.rhs", F);
    BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "logic.end");

    if (Operator == tok_and)
      Builder->CreateCondBr(L, RHSBB, MergeBB);
    else
      Builder->CreateCondBr(L, MergeBB, RHSBB);

    Builder->SetInsertPoint(RHSBB);
    Value *RHSVal = Right->codegen();
    if (!RHSVal)
      return nullptr;
    if (Right->getType() != ValueType::Bool)
      return LogErrorV("Type mismatch in binary operator");
    Builder->CreateBr(MergeBB);
    RHSBB = Builder->GetInsertBlock();

    F->insert(F->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);
    PHINode *PN =
        Builder->CreatePHI(Type::getInt1Ty(*TheContext), 2, "logictmp");
    if (Operator == tok_and) {
      PN->addIncoming(ConstantInt::getFalse(*TheContext), LHSBB);
      PN->addIncoming(RHSVal, RHSBB);
    } else {
      PN->addIncoming(ConstantInt::getTrue(*TheContext), LHSBB);
      PN->addIncoming(RHSVal, RHSBB);
    }
    return PN;
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
    return EmitBuiltInArithmetic(Operator, L, LType, Left->getStructName(), R, RType,
                                 Right->getStructName(), getType(),
                                 getStructName());
  }
  case tok_ampersand:
  case tok_pipe:
  case tok_caret: {
    ValueType Ty = getType();
    L = EmitImplicitCast(L, LType, Ty);
    R = EmitImplicitCast(R, RType, Ty);
    if (!L || !R)
      return LogErrorV("Type mismatch in binary operator");
    if (Operator == tok_ampersand)
      return Builder->CreateAnd(L, R, "bwand");
    if (Operator == tok_pipe)
      return Builder->CreateOr(L, R, "bwor");
    return Builder->CreateXor(L, R, "bwxor");
  }
  case tok_shl:
  case tok_shr: {
    ValueType Ty = getType();
    L = EmitImplicitCast(L, LType, Ty);
    R = EmitImplicitCast(R, RType, Ty);
    if (!L || !R)
      return LogErrorV("Type mismatch in binary operator");
    if (Operator == tok_shl)
      return Builder->CreateShl(L, R, "shltmp");
    return IsUnsignedIntType(Ty) ? Builder->CreateLShr(L, R, "shrtmp")
                                 : Builder->CreateAShr(L, R, "shrtmp");
  }
  case tok_less:
  case tok_greater:
  case tok_eq:
  case tok_neq:
  case tok_leq:
  case tok_geq: {
    if (LType == ValueType::Pointer && RType == ValueType::Pointer &&
        Left->getStructName() == Right->getStructName()) {
      switch (Operator) {
      case tok_eq:
        return Builder->CreateICmpEQ(L, R, "cmptmp");
      case tok_neq:
        return Builder->CreateICmpNE(L, R, "cmptmp");
      case tok_less:
        return Builder->CreateICmpULT(L, R, "cmptmp");
      case tok_greater:
        return Builder->CreateICmpUGT(L, R, "cmptmp");
      case tok_leq:
        return Builder->CreateICmpULE(L, R, "cmptmp");
      case tok_geq:
        return Builder->CreateICmpUGE(L, R, "cmptmp");
      default:
        break;
      }
    }
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

  // If we get here it's not a built-in operator — look for a user-defined one.
  // User-defined binary operators are stored as regular functions named
  // "binary" + opchar.  We call that function with L and R as arguments.
  Function *F = getFunction(std::string("binary") + (char)Operator);
  if (!F)
    return LogErrorV("invalid binary operator");

  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}

/// UnaryExpressionNode::codegen - Emit built-in unary minus directly, or call a
/// user-defined unary operator function ("unary" + opchar).
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
  if (Opcode == tok_tilde) {
    if (!IsIntType(getType()))
      return LogErrorV("Unary '~' not supported for this type");
    return Builder->CreateNot(Operator, "bnottmp");
  }

  // User-defined unary operator.
  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return Builder->CreateCall(F, Operator, "unop");
}

Value *IncDecExpressionNode::codegen() {
  ValueType TargetType = getType();
  string TargetStruct = getStructName();
  Value *Ptr =
      ResolveIncDecLValuePtr(Operand.get(), &TargetType, &TargetStruct);
  if (!Ptr)
    return LogErrorV("Increment/decrement target must be assignable");

  Value *OldVal = Builder->CreateLoad(LLVMTypeFor(TargetType, TargetStruct),
                                      Ptr, "incdec.old");
  Value *One = nullptr;
  if (TargetType == ValueType::Pointer) {
    One = ConstantInt::get(Type::getInt64Ty(*TheContext), 1);
  } else if (IsIntType(TargetType)) {
    One = ConstantInt::get(LLVMTypeFor(TargetType), 1, true);
  } else if (IsFloatType(TargetType)) {
    One = ConstantFP::get(LLVMTypeFor(TargetType), 1.0);
  } else {
    return LogErrorV("Increment/decrement requires numeric or pointer type");
  }

  int Operator = IsIncrement ? tok_plus : tok_minus;
  Value *NewVal = EmitBuiltInArithmetic(
      Operator, OldVal, TargetType, TargetStruct, One,
      TargetType == ValueType::Pointer ? ValueType::Int64 : TargetType, "",
      TargetType, TargetStruct);
  if (!NewVal)
    return nullptr;
  Builder->CreateStore(NewVal, Ptr);
  return IsPrefix ? NewVal : OldVal;
}

Value *LogicalNotExpressionNode::codegen() {
  Value *V = Operand->codegen();
  if (!V)
    return nullptr;
  if (Operand->getType() != ValueType::Bool)
    return LogErrorV("Type mismatch in unary operator");
  return Builder->CreateNot(V, "nottmp");
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

Value *SizeofExpressionNode::codegen() {
  llvm::Type *Ty = LLVMTypeFor(TargetType, TargetStructName);
  if (!Ty)
    return LogErrorV("Invalid sizeof target type");
  uint64_t Bytes =
      TheModule->getDataLayout().getTypeAllocSize(Ty).getFixedValue();
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
      ValueType ArgType = Arguments[i]->getType();
      ValueType ParamType = Signature->getParameterType(i);
      if (ParamType == ValueType::Pointer && ArgType == ValueType::Array) {
        if (!ArrayDecaysToPointerType(Arguments[i]->getStructName(),
                                      Signature->getParameterStructName(i)))
          return LogErrorV("Argument type mismatch");
      } else {
        ArgVal = EmitImplicitCast(ArgVal, ArgType, ParamType);
        if (!ArgVal)
          return LogErrorV("Argument type mismatch");
      }
    }
    ArgsV.push_back(ArgVal);
  }

  if (getType() == ValueType::None)
    return Builder->CreateCall(CalleeF, ArgsV);
  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *ConstructorCallExpressionNode::codegen() {
  auto SI = StructTypes.find(ClassName);
  if (SI == StructTypes.end() || !SI->second.IsClass)
    return LogErrorV("Unknown class in constructor call");

  llvm::Type *ClassTy = LLVMTypeFor(ValueType::Struct, ClassName);
  if (!ClassTy)
    return LogErrorV("Unknown class type");
  Function *CurFn = Builder->GetInsertBlock()
                        ? Builder->GetInsertBlock()->getParent()
                        : nullptr;
  if (!CurFn)
    return LogErrorV("Constructor call outside function context");
  AllocaInst *Tmp =
      CreateEntryBlockAlloca(CurFn, "ctor.tmp", ValueType::Struct, ClassName);
  Builder->CreateStore(ZeroConstant(ValueType::Struct, ClassName), Tmp);

  string InitName = ClassName + ".__init__";
  if (FunctionSignatureNode *InitSignature = GetFunctionSignature(InitName)) {
    Function *InitF = getFunction(InitName);
    if (!InitF)
      return LogErrorV("Unknown constructor function");
    if (InitF->arg_size() != Arguments.size() + 1)
      return LogErrorV("Incorrect # arguments passed");
    vector<Value *> ArgsV;
    ArgsV.push_back(Tmp);
    for (unsigned I = 0, E = Arguments.size(); I != E; ++I) {
      Value *ArgVal = Arguments[I]->codegen();
      if (!ArgVal)
        return nullptr;
      ValueType ArgType = Arguments[I]->getType();
      ValueType ParamType = InitSignature->getParameterType(I + 1);
      if (ParamType == ValueType::Pointer && ArgType == ValueType::Array) {
        if (!ArrayDecaysToPointerType(Arguments[I]->getStructName(),
                                      InitSignature->getParameterStructName(I + 1)))
          return LogErrorV("Argument type mismatch");
      } else {
        ArgVal = EmitImplicitCast(ArgVal, ArgType, ParamType);
        if (!ArgVal)
          return LogErrorV("Argument type mismatch");
      }
      ArgsV.push_back(ArgVal);
    }
    Builder->CreateCall(InitF, ArgsV);
  } else if (!Arguments.empty()) {
    return LogErrorV("Constructor argument mismatch");
  }

  return Builder->CreateLoad(ClassTy, Tmp, "ctor.obj");
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

/// ForExpressionNode::codegen - Emit LLVM IR for a for-expression using a mutable
/// stack slot for the loop variable.
Value *ForExpressionNode::codegen() {
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
    NamedValueTypes[VarName] = VarType;
    NamedValueStructNames.erase(VarName);
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
    NamedValueTypes.erase(VarName);
    NamedValueStructNames.erase(VarName);
  }

  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *WhileExpressionNode::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  BasicBlock *CondBB =
      BasicBlock::Create(*TheContext, "while_cond", TheFunction);
  BasicBlock *BodyBB =
      BasicBlock::Create(*TheContext, "while_body", TheFunction);
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "while_after", TheFunction);

  if (IsDoWhile) {
    Builder->CreateBr(BodyBB);
  } else {
    Builder->CreateBr(CondBB);
  }

  if (!IsDoWhile) {
    Builder->SetInsertPoint(CondBB);
    Value *CondVal = Cond->codegen();
    if (!CondVal)
      return nullptr;
    CondVal = ToBool(CondVal, Cond->getType());
    if (!CondVal)
      return LogErrorV("Invalid loop condition type");
    Builder->CreateCondBr(CondVal, BodyBB, AfterBB);
  }

  Builder->SetInsertPoint(BodyBB);
  LoopControlStack.push_back({AfterBB, CondBB});
  BreakTargetStack.push_back(AfterBB);
  if (!Body->codegen()) {
    BreakTargetStack.pop_back();
    LoopControlStack.pop_back();
    return nullptr;
  }
  BreakTargetStack.pop_back();
  LoopControlStack.pop_back();
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(CondBB);

  Builder->SetInsertPoint(CondBB);
  if (IsDoWhile || !CondBB->getTerminator()) {
    Value *CondVal = Cond->codegen();
    if (!CondVal)
      return nullptr;
    CondVal = ToBool(CondVal, Cond->getType());
    if (!CondVal)
      return LogErrorV("Invalid loop condition type");
    Builder->CreateCondBr(CondVal, BodyBB, AfterBB);
  }

  Builder->SetInsertPoint(AfterBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *SwitchExpressionNode::codegen() {
  Value *CondVal = Cond->codegen();
  if (!CondVal)
    return nullptr;

  ValueType CondType = Cond->getType();
  llvm::Type *CondLLVMType = LLVMTypeFor(CondType);
  if (!CondLLVMType || !CondLLVMType->isIntegerTy())
    return LogErrorV("Switch condition must be an integer type");
  CondVal = EmitImplicitCast(CondVal, CondType, CondType);
  if (!CondVal)
    return LogErrorV("Invalid switch condition type");

  Function *F = Builder->GetInsertBlock()->getParent();
  BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "switch.after", F);
  BasicBlock *DefaultBB =
      DefaultCase ? BasicBlock::Create(*TheContext, "switch.default", F)
                  : AfterBB;
  auto *SwitchI = Builder->CreateSwitch(CondVal, DefaultBB, Cases.size());

  vector<BasicBlock *> CaseBBs;
  CaseBBs.reserve(Cases.size());
  for (const auto &C : Cases) {
    BasicBlock *CaseBB = BasicBlock::Create(*TheContext, "switch.case", F);
    CaseBBs.push_back(CaseBB);
    // Every value in this case's list branches to the same block — LLVM's
    // switch instruction natively supports many values mapping to one
    // destination, so this is just one addCase() call per value.
    for (int64_t Val : C.first) {
      auto *CaseConst = ConstantInt::get(cast<IntegerType>(CondLLVMType),
                                         static_cast<uint64_t>(Val),
                                         /*isSigned=*/true);
      SwitchI->addCase(CaseConst, CaseBB);
    }
  }

  BreakTargetStack.push_back(AfterBB);
  for (size_t I = 0; I < Cases.size(); ++I) {
    Builder->SetInsertPoint(CaseBBs[I]);
    if (!Cases[I].second->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!Builder->GetInsertBlock()->getTerminator())
      Builder->CreateBr(AfterBB);
  }

  if (DefaultCase) {
    Builder->SetInsertPoint(DefaultBB);
    if (!DefaultCase->codegen()) {
      BreakTargetStack.pop_back();
      return nullptr;
    }
    if (!Builder->GetInsertBlock()->getTerminator())
      Builder->CreateBr(AfterBB);
  }
  BreakTargetStack.pop_back();

  Builder->SetInsertPoint(AfterBB);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *BreakExpressionNode::codegen() {
  if (BreakTargetStack.empty())
    return LogErrorV("'break' used outside of a loop or switch");
  Builder->CreateBr(BreakTargetStack.back());
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

Value *ContinueExpressionNode::codegen() {
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

      Value *InitVal = nullptr;
      if (Init) {
        InitVal = Init->codegen();
        if (!InitVal)
          return nullptr;
        InitVal = EmitImplicitCast(InitVal, Init->getType(), VarType);
        if (!InitVal)
          return LogErrorV("Type mismatch in variable initialization");
      } else {
        InitVal = ZeroConstant(VarType, VarStructName);
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

    Value *InitVal = nullptr;
    if (Init) {
      InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
      InitVal = EmitImplicitCast(InitVal, Init->getType(), VarType);
      if (!InitVal)
        return LogErrorV("Type mismatch in variable initialization");
    } else {
      InitVal = ZeroConstant(VarType, VarStructName);
    }

    AllocaInst *Alloca =
        CreateEntryBlockAlloca(TheFunction, VarName, VarType, VarStructName);
    Builder->CreateStore(InitVal, Alloca);
    NamedValues[VarName] = Alloca;
    NamedValueTypes[VarName] = VarType;
    if (!VarStructName.empty())
      NamedValueStructNames[VarName] = VarStructName;
    else
      NamedValueStructNames.erase(VarName);
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
  for (const auto &Parameter : Parameters)
    ParameterTypes.push_back(LLVMTypeFor(Parameter.Type, Parameter.StructName));
  FunctionType *FT =
      FunctionType::get(LLVMTypeFor(ReturnType, ReturnStructName), ParameterTypes,
                        false /* not variadic */);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Name arguments so the printed IR is readable.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Parameters[Idx++].Name);

  // For user-defined binary operators, register the precedence in the global
  // table so the parser knows how tightly the new operator binds.  This happens
  // at JIT time (inside codegen), meaning the operator is immediately usable in
  // subsequent REPL lines or file definitions — exactly what we want.
  if (isBinaryOp())
    OperatorPrecedence[getOperatorName()] = Precedence;

  // For user-defined unary operators, register the operator token in the
  // unary registry so later definitions can detect duplicates.
  if (isUnaryOp())
    KnownUnaryOperators.insert(getOperatorName());

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
    LogError("Function cannot be redefined.");
    return nullptr;
  }

  if (!TheFunction)
    return nullptr;

  ValueType SavedRetType = CurrentFunctionReturnType;
  CurrentFunctionReturnType = P.getReturnType();

  DISubprogram *SP = nullptr;
  if (DIB && TheDIFile) {
    bool IsInternal = P.getName().rfind("__pyxc.", 0) == 0;
    if (!IsInternal) {
      unsigned Line = P.getLocation().Line ? P.getLocation().Line : 1;
      SmallVector<Metadata *, 8> EltTys;
      EltTys.push_back(DITypeFor(P.getReturnType()));
      for (size_t i = 0; i < P.getParameters().size(); ++i)
        EltTys.push_back(DITypeFor(P.getParameterType(i)));
      auto *SubType =
          DIB->createSubroutineType(DIB->getOrCreateTypeArray(EltTys));
      SP = DIB->createFunction(TheDIFile, P.getName(), StringRef(), TheDIFile,
                               Line, SubType, Line, DINode::FlagZero,
                               DISubprogram::SPFlagDefinition);
      TheFunction->setSubprogram(SP);
      CurDIScope = SP;
      CurFunctionLine = Line;
    }
  }

  // Step 2: create the entry block and point the builder at it.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);
  SetCurrentDebugLocation(CurFunctionLine);

  // Step 3: populate NamedValues with entry-block allocas for each argument.
  NamedValues.clear();
  NamedValueTypes.clear();
  NamedValueStructNames.clear();
  LoopControlStack.clear();
  BreakTargetStack.clear();
  unsigned ArgIndex = 1;
  size_t ArgTypeIndex = 0;
  for (auto &Arg : TheFunction->args()) {
    ValueType ArgType = P.getParameterType(ArgTypeIndex);
    string ArgStructName = P.getParameterStructName(ArgTypeIndex);
    ++ArgTypeIndex;
    AllocaInst *Alloca = CreateEntryBlockAlloca(
        TheFunction, std::string(Arg.getName()), ArgType, ArgStructName);
    Builder->CreateStore(&Arg, Alloca);
    NamedValues[std::string(Arg.getName())] = Alloca;
    NamedValueTypes[std::string(Arg.getName())] = ArgType;
    if (!ArgStructName.empty())
      NamedValueStructNames[std::string(Arg.getName())] = ArgStructName;
    EmitDebugDeclare(Alloca, Arg.getName(), CurFunctionLine, true, ArgIndex++,
                     ArgType);
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
    return TheFunction;
  }

  // Body codegen failed — remove the incomplete function so it cannot be
  // called and does not pollute the module handed to the JIT.
  TheFunction->eraseFromParent();
  CurDIScope = nullptr;
  CurrentFunctionReturnType = SavedRetType;
  return nullptr;
}

//===----------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------===//

static vector<unique_ptr<ExpressionNode>> FileTopLevelStmts;

/// ResetParserStateForFile - Clear parser/compiler state between input files.
///
/// Multi-file compilation emits each source into its own module, so symbols,
/// globals, and top-level statements should not leak across files.
static void ResetParserStateForFile() {
  FunctionSignatures.clear();
  StructTypes.clear();
  Traits.clear();
  ActiveTypeParams.clear();
  TypeAliases.clear();
  GlobalVarTypes.clear();
  GlobalVarStructTypes.clear();
  GlobalVarDecls.clear();
  VarScopes.clear();
  VarStructScopes.clear();
  FileTopLevelStmts.clear();
  LastTopLevelShouldPrint = true;
  InGlobalInit = false;
  ModuleHasGlobals = false;
  HadError = false;
  ResetOperatorPrecedence();
  ResetKnownUnaryOperators();
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
/// needs loop information can reach TheLAM, and so on.
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
  StringLiteralCounter = 0;
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

  // Optimisation pipelines. With -O0 the pass managers are left empty so the
  // emitted IR stays close to the direct lowering performed by the code
  // generator.
  if (OptLevel != 0) {
    auto FPM = PB.buildFunctionSimplificationPipeline(GetOptLevel(),
                                                      ThinOrFullLTOPhase::None);
    TheFPM = std::make_unique<FunctionPassManager>(std::move(FPM));
    auto MPM = PB.buildPerModuleDefaultPipeline(GetOptLevel());
    TheMPM = std::make_unique<ModulePassManager>(std::move(MPM));
  }

  InitializeDebugInfo();
}

static void RunModuleOptimizations(Module *M) {
  if (!TheMPM || OptLevel == 0)
    return;
  TheMPM->run(*M, *TheMAM);
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

/// HandleDecorator - Parse a decorator line and the 'def' that follows it.
///
/// Decorator syntax (note: decorator and 'def' must be on separate lines):
///   @binary(precedence)
///   def opchar(lhs, rhs): ...
///
///   @unary
///   def opchar(x): ...
///
/// The '@' has already been consumed by MainLoop before calling here.
/// CurrentToken is on 'binary' or 'unary'. Delegates to ParseDecoratedFunctionDef.
static void HandleDecorator() {
  auto FnAST = ParseDecoratedFunctionDef();
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (!FnAST || HasTrailing) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  if (auto *FnIR = FnAST->codegen()) {
    Log("Parsed a user-defined operator.\n");
    if (ShouldDumpIR())
      FnIR->print(errs());
    if (!IsEmitMode()) {
      ExitOnErr(TheJIT->addModule(
          ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
      InitializeModuleAndManagers();
    }
  }
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
      LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
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
      LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  // Reject conflicting redeclarations: in Pyxc, function identity is just
  // name + arity. We validate types separately in the parser.
  auto Existing = FunctionSignatures.find(ProtoAST->getName());
  if (Existing != FunctionSignatures.end() &&
      Existing->second->getNumParameters() != ProtoAST->getNumParameters()) {
    LogError((string("Conflicting extern declaration for '") +
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

static void HandleStructDef() {
  bool Ok = ParseAggregateDefinition("struct");
  if (!Ok) {
    SynchronizeToLineBoundary();
    return;
  }
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (HasTrailing) {
    LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
}

static void HandleClassDef() {
  bool Ok = ParseAggregateDefinition("class");
  if (!Ok) {
    SynchronizeToLineBoundary();
    return;
  }
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (HasTrailing) {
    LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
}

static void HandleTypeAliasDef() {
  bool Ok = ParseTypeAliasDefinition();
  if (!Ok) {
    SynchronizeToLineBoundary();
    return;
  }
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (HasTrailing) {
    LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
}

static void HandleTraitDef() {
  bool Ok = ParseTraitDefinition();
  if (!Ok) {
    SynchronizeToLineBoundary();
    return;
  }
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (HasTrailing) {
    LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
}

static void HandleImplDef() {
  bool Ok = ParseImplDefinition();
  if (!Ok) {
    SynchronizeToLineBoundary();
    return;
  }
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (HasTrailing) {
    LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
}

/// HandleTopLevelExpression - Compile, execute, and discard a bare expression.
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
static void HandleTopLevelExpression() {
  auto FnAST = ParseTopLevelExpression();
  bool HasTrailing = (CurrentToken != tok_eol && CurrentToken != tok_eof && CurrentToken != tok_block_end);
  if (!FnAST || HasTrailing) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
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
      LogError(("Unexpected " + FormatTokenForMessage(CurrentToken)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  FileTopLevelStmts.push_back(std::move(Stmt));
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
/// top             = function-definition | external | toplevelstmt ;
///
/// Dispatches on the leading token of each top-level form:
///   tok_def    → HandleFunctionDefinition   (function-definition)
///   tok_extern → HandleExtern       (external)
///   '@'        → HandleDecorator    (decorateddef: @binary / @unary)
///   tok_eol    → skip blank line
///   anything else → HandleTopLevelExpression (toplevelstmt)
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
      LogError("Unexpected indentation");
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
      HandleStructDef();
      break;
    case tok_class:
      HandleClassDef();
      break;
    case tok_type:
      HandleTypeAliasDef();
      break;
    case tok_trait:
      HandleTraitDef();
      break;
    case tok_impl:
      HandleImplDef();
      break;
    case tok_def:
      HandleFunctionDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    case tok_at:
      // Decorator: '@binary(N)' or '@unary' — consume the '@' then dispatch.
      getNextToken(); // eat '@', now on 'binary' or 'unary'
      HandleDecorator();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

/// FileModeLoop - Parse a script file into top-level statements + definitions.
///
/// In file mode we do not execute top-level statements immediately. They are
/// collected into FileTopLevelStmts and later emitted into __pyxc.global_init.
static void FileModeLoop() {
  while (true) {
    if (CurrentToken == tok_eof)
      return;

    if (CurrentToken == tok_eol) {
      getNextToken();
      continue;
    }

    if (CurrentToken == tok_indent) {
      LogError("Unexpected indentation");
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
      HandleStructDef();
      break;
    case tok_class:
      HandleClassDef();
      break;
    case tok_type:
      HandleTypeAliasDef();
      break;
    case tok_trait:
      HandleTraitDef();
      break;
    case tok_impl:
      HandleImplDef();
      break;
    case tok_def:
      HandleFunctionDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    case tok_at:
      getNextToken(); // eat '@'
      HandleDecorator();
      break;
    default:
      HandleTopLevelStatementFileMode();
      break;
    }
  }
}

/// RunFileMode - Emit and execute __pyxc.global_init, then call main() if any.
static void RunFileMode() {
  if (!FileTopLevelStmts.empty()) {
    auto Block = make_unique<BlockExpressionNode>(std::move(FileTopLevelStmts));
    auto Signature = make_unique<FunctionSignatureNode>(
        "__pyxc.global_init", vector<FunctionSignatureNode::ParameterInfo>(),
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
  CodeGenFileType FileType = (Kind == EmitKind::ASM)
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

  return EmitModuleToFile(M.get(), EmitKind::OBJ, ObjPath);
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
  return EmitModuleToFile(TheModule.get(), EmitKind::OBJ, ObjPath);
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

static void MaybeEmitDsymBundle(const string &ExePath) {
  if (!DebugInfo)
    return;

  Triple TT(sys::getDefaultTargetTriple());
  if (!TT.isOSDarwin())
    return;

  auto Dsymutil = sys::findProgramByName("dsymutil");
  if (!Dsymutil) {
    fprintf(
        stderr,
        "Warning: dsymutil not found; debug info will remain in .o files\n");
    return;
  }

  std::vector<StringRef> Arguments;
  Arguments.push_back(*Dsymutil);
  Arguments.push_back(ExePath);
  if (sys::ExecuteAndWait(*Dsymutil, Arguments)) {
    fprintf(stderr, "Warning: dsymutil failed; debug info may be missing\n");
  }
}

static bool LinkExecutable(const vector<string> &Inputs,
                           const string &OutputPath) {
  // Use the TargetMachine's normalized triple so the linker's -platform_version
  // matches exactly what was baked into the object files during compilation.
  auto TM = CreateTargetMachine();
  Triple TT =
      TM ? TM->getTargetTriple() : Triple(sys::getDefaultTargetTriple());
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
  if (!FileTopLevelStmts.empty()) {
    auto Block = make_unique<BlockExpressionNode>(std::move(FileTopLevelStmts));
    auto Signature = make_unique<FunctionSignatureNode>(
        "__pyxc.global_init", vector<FunctionSignatureNode::ParameterInfo>(),
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
      EmitMode = EmitKind::ASM;
      if (InputFiles.size() != 1) {
        fprintf(stderr, "Error: --emit requires a single input file\n");
        return -1;
      }
      EmitOutputPath = OutputFile.empty() ? "out.s" : OutputFile.getValue();
    } else if (EmitKindOpt == "obj") {
      EmitMode = EmitKind::OBJ;
      if (InputFiles.size() != 1) {
        fprintf(stderr, "Error: --emit requires a single input file\n");
        return -1;
      }
      EmitOutputPath = OutputFile.empty() ? "out.o" : OutputFile.getValue();
    } else if (EmitKindOpt == "exe") {
      EmitMode = EmitKind::EXE;
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

  if (IsRepl || InputFiles.empty())
    CurrentSourcePath = "<stdin>";
  else
    CurrentSourcePath = InputFiles.front();

  // Create the JIT first — InitializeModuleAndManagers() needs TheJIT in
  // order to set the data layout on the new module.
  TheJIT = ExitOnErr(PyxcJIT::Create());
  InitializeModuleAndManagers();

  if (IsRepl) {
    PrintReplPrompt();
    getNextToken();
    MainLoop();
  } else {
    if (EmitMode == EmitKind::EXE) {
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

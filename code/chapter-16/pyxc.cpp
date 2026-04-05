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

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  // commands
  tok_def = -4,
  tok_extern = -5,

  // primary
  tok_identifier = -6,
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
  tok_float = -27,
  tok_float32 = -28,
  tok_float64 = -29,
  tok_bool = -30,
  tok_none = -31,
  tok_true = -32,
  tok_false = -33,
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

static string IdentifierStr;    // Filled in if tok_identifier
static string NumLiteralStr;    // Raw number literal text (no sign)
static bool NumIsFloat = false; // True if the literal contains '.' or e/E.
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
    {"if", tok_if},           {"else", tok_else},       {"for", tok_for},
    {"binary", tok_binary},   {"unary", tok_unary},     {"var", tok_var},
    {"int", tok_int},         {"int8", tok_int8},       {"int16", tok_int16},
    {"int32", tok_int32},     {"int64", tok_int64},     {"float", tok_float},
    {"float32", tok_float32}, {"float64", tok_float64}, {"bool", tok_bool},
    {"None", tok_none},       {"True", tok_true},       {"False", tok_false}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  static map<int, string> Names = {
      {tok_eof, "end of input"},  {tok_eol, "newline"},
      {tok_error, "error"},       {tok_def, "'def'"},
      {tok_extern, "'extern'"},   {tok_identifier, "identifier"},
      {tok_number, "number"},     {tok_return, "'return'"},
      {tok_eq, "'=='"},           {tok_neq, "'!='"},
      {tok_leq, "'<='"},          {tok_geq, "'>='"},
      {tok_arrow, "'->'"},        {tok_if, "'if'"},
      {tok_else, "'else'"},       {tok_for, "'for'"},
      {tok_binary, "'binary'"},   {tok_unary, "'unary'"},
      {tok_var, "'var'"},         {tok_int, "'int'"},
      {tok_int8, "'int8'"},       {tok_int16, "'int16'"},
      {tok_int32, "'int32'"},     {tok_int64, "'int64'"},
      {tok_float, "'float'"},     {tok_float32, "'float32'"},
      {tok_float64, "'float64'"}, {tok_bool, "'bool'"},
      {tok_none, "'None'"},       {tok_true, "'True'"},
      {tok_false, "'False'"},     {tok_indent, "indent"},
      {tok_dedent, "dedent"}};

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
///   CurLoc  - snapshotted at the start of each token in gettok(), before
///             consuming any of the token's characters. This is the position
///             the parser and diagnostics see.
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

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
/// Every token branch in gettok() calls advance() rather than fgetc()
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
/// Used by the two-character operator branches in gettok() to decide whether
/// '=' should become '==' (tok_eq), '!' should become '!=' (tok_neq), etc.,
/// without advancing LexLoc or notifying SourceManager.
static int peek() {
  int c = fgetc(Input);
  if (c != EOF)
    ungetc(c, Input);
  return c;
}

/// gettok - Return the next token from standard input.
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
static int gettok() {
  // Drain tokens queued by a multi-level dedent on the previous line.
  if (!PendingTokens.empty()) {
    int Tok = PendingTokens.front();
    PendingTokens.pop_front();
    return Tok;
  }

  // At line start with the sentinel space, advance to the first real char
  // so the indentation counting below sees actual input.
  if (AtLineStart && LexerLastChar == ' ')
    LexerLastChar = advance();

  // ── Line-start: count indentation, emit INDENT / DEDENT ──────────────
  if (AtLineStart) {
    int IndentCol = 0;
    while (LexerLastChar == ' ' || LexerLastChar == '\t') {
      IndentCol += (LexerLastChar == ' ') ? 1 : (8 - IndentCol % 8);
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
    int CurrentIndent = IndentStack.back();
    if (IndentCol > CurrentIndent) {
      IndentStack.push_back(IndentCol);
      AtLineStart = false;
      return tok_indent;
    }
    if (IndentCol < CurrentIndent) {
      while (IndentStack.size() > 1 /* protect the 0-indent */ &&
             IndentCol < IndentStack.back()) {
        IndentStack.pop_back();
        PendingTokens.push_back(tok_dedent);
      }
      if (IndentCol != IndentStack.back()) {
        fprintf(stderr,
                "Error (Line %d, Column %d): inconsistent indentation\n",
                CurLoc.Line, CurLoc.Col);
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
    IdentifierStr = LexerLastChar;
    while (isalnum((LexerLastChar = advance())) || LexerLastChar == '_')
      IdentifierStr += LexerLastChar;

    auto It = Keywords.find(IdentifierStr);
    return (It == Keywords.end()) ? tok_identifier : It->second;
  }

  if (isdigit(LexerLastChar) || LexerLastChar == '.') {
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
        fprintf(stderr,
                "Error (Line %d, Column %d): invalid number literal '%s'\n",
                CurLoc.Line, CurLoc.Col, NumStr.c_str());
        PrintErrorSourceContext(CurLoc);
        return tok_error;
      }
      ConsumeDigits();
    }

    if (NumStr == ".") {
      fprintf(stderr,
              "Error (Line %d, Column %d): invalid number literal '%s'\n",
              CurLoc.Line, CurLoc.Col, NumStr.c_str());
      PrintErrorSourceContext(CurLoc);
      return tok_error;
    }

    NumLiteralStr = NumStr;
    NumIsFloat = SawDot || SawExp;
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
  // token else return the single character token.
  if (LexerLastChar == '-') {
    int Tok = (peek() == '>') ? (advance(), tok_arrow) : '-';
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '=') {
    int Tok = (peek() == '=') ? (advance(), tok_eq) : '=';
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '!') {
    int Tok = (peek() == '=') ? (advance(), tok_neq) : '!';
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '<') {
    int Tok = (peek() == '=') ? (advance(), tok_leq) : '<';
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == '>') {
    int Tok = (peek() == '=') ? (advance(), tok_geq) : '>';
    LexerLastChar = advance();
    return Tok;
  }

  if (LexerLastChar == EOF)
    return tok_eof;

  // Single character token
  int ThisChar = LexerLastChar;

  // Position the lexer at the next character so the next gettok() starts there.
  LexerLastChar = advance();

  // Return ThisChar.
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
/// in error messages. Identifier and number tokens include their actual text
/// (e.g. "identifier 'foo'", "number '3.14'") since the name alone is not
/// enough to diagnose the problem. Everything else uses the static TokenNames
/// entry.
static string FormatTokenForMessage(int Tok) {
  if (Tok == tok_identifier)
    return "identifier '" + IdentifierStr + "'";
  if (Tok == tok_number)
    return "number '" + NumLiteralStr + "'";

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

//===----------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------===//
namespace {

/// ExprAST - Base class for all expression nodes.
class ExprAST {
  ValueType Type = ValueType::Error;

public:
  virtual ~ExprAST() = default;
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

/// NumberExprAST - Expression class for numeric literals.
class NumberExprAST : public ExprAST {
  bool IsIntLiteral;
  APInt IntVal;
  APFloat FloatVal;

public:
  NumberExprAST(APInt Val, ValueType Type)
      : IsIntLiteral(true), IntVal(std::move(Val)), FloatVal(0.0) {
    setType(Type);
  }
  NumberExprAST(APFloat Val, ValueType Type)
      : IsIntLiteral(false), IntVal(1, 0), FloatVal(std::move(Val)) {
    setType(Type);
  }
  Value *codegen() override;
};

/// BoolExprAST - Expression class for boolean literals: True/False.
class BoolExprAST : public ExprAST {
  bool Val;

public:
  BoolExprAST(bool Val) : Val(Val) { setType(ValueType::Bool); }
  Value *codegen() override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  string Name;

public:
  VariableExprAST(const string &Name, ValueType Type) : Name(Name) {
    setType(Type);
  }
  // convenience function
  const string &getName() const { return Name; }
  const string *getLValueName() const override { return &Name; }
  Value *codegen() override;
};

/// AssignmentExprAST - Expression class for assignment to an existing variable.
/// The expression stores RHS into the named variable and produces the assigned
/// value.
class AssignmentExprAST : public ExprAST {
  string Name;
  unique_ptr<ExprAST> Expr;

public:
  AssignmentExprAST(const string &Name, unique_ptr<ExprAST> Expr,
                    ValueType Type)
      : Name(Name), Expr(std::move(Expr)) {
    setType(Type);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// ReturnExprAST - Statement-like expression for return.
/// Emits a function return and produces the returned value.
class ReturnExprAST : public ExprAST {
  unique_ptr<ExprAST> Expr;

public:
  ReturnExprAST(unique_ptr<ExprAST> Expr = nullptr) : Expr(std::move(Expr)) {
    setType(ValueType::None);
  }
  bool isReturnExpr() const override { return true; }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// BlockExprAST - A sequence of statements evaluated in order.
/// The block's value is the value of the last statement executed.
class BlockExprAST : public ExprAST {
  vector<unique_ptr<ExprAST>> Stmts;

public:
  BlockExprAST(vector<unique_ptr<ExprAST>> Stmts) : Stmts(std::move(Stmts)) {
    setType(ValueType::None);
  }
  Value *codegen() override;
};

/// BinaryExprAST - Expression class for a binary operator.
/// Op is an int (not char) to accommodate both single-character ASCII operators
/// like '+' and named multi-character token enums like tok_eq (==).
class BinaryExprAST : public ExprAST {
  int Op;
  unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(int Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS,
                ValueType Type)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {
    setType(Type);
  }
  Value *codegen() override;
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  string Callee;
  vector<unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const string &Callee, vector<unique_ptr<ExprAST>> Args,
              ValueType Type)
      : Callee(Callee), Args(std::move(Args)) {
    setType(Type);
  }
  bool shouldPrintValue() const override {
    return getType() != ValueType::None;
  }
  Value *codegen() override;
};

/// ForExprAST - Expression class for for loops.
///   for <var> = <start>, <cond>, <step>: <body>
/// The loop variable is in scope for <cond>, <step>, and <body> (through
/// NamedValues). The expression always produces 0.0 — the loop is used for side
/// effects.
class ForExprAST : public ExprAST {
  string VarName;
  bool IsVarDecl;
  ValueType VarType;
  unique_ptr<ExprAST> Start, Cond, Step, Body;

public:
  ForExprAST(const string &VarName, bool IsVarDecl, ValueType VarType,
             unique_ptr<ExprAST> Start, unique_ptr<ExprAST> Cond,
             unique_ptr<ExprAST> Step, unique_ptr<ExprAST> Body)
      : VarName(VarName), IsVarDecl(IsVarDecl), VarType(VarType),
        Start(std::move(Start)), Cond(std::move(Cond)), Step(std::move(Step)),
        Body(std::move(Body)) {
    setType(ValueType::None);
  }
  ValueType getVarType() const { return VarType; }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// UnaryExprAST - Expression class for a unary operator application.
/// The operator is identified by its ASCII character (e.g. '-' or '!').
/// Built-in unary minus is represented here with opcode '-' and lowered
/// directly to LLVM `fneg`. All other unary operators are resolved as regular
/// functions named "unary<op>" (e.g. "unary!") and called with the operand.
class UnaryExprAST : public ExprAST {
  char Opcode;
  unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(char Opcode, unique_ptr<ExprAST> Operand, ValueType Type)
      : Opcode(Opcode), Operand(std::move(Operand)) {
    setType(Type);
  }
  Value *codegen() override;
};

/// CastExprAST - Expression class for explicit casts: int(expr), float64(expr).
class CastExprAST : public ExprAST {
  ValueType TargetTy;
  unique_ptr<ExprAST> Expr;

public:
  CastExprAST(ValueType TargetType, unique_ptr<ExprAST> Expr)
      : TargetTy(TargetType), Expr(std::move(Expr)) {
    setType(TargetType);
  }
  Value *codegen() override;
};

/// IfStmtAST - Statement form of if/else.
/// Produces 0.0 and does not return a value.
class IfStmtAST : public ExprAST {
  unique_ptr<ExprAST> Cond, Then, Else;

public:
  IfStmtAST(unique_ptr<ExprAST> Cond, unique_ptr<ExprAST> Then,
            unique_ptr<ExprAST> Else)
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
  unique_ptr<ExprAST> Init;
};

/// VarStmtAST - Statement form of mutable local variable bindings.
///   var a = <init>, b = <init>
/// Each binding allocates stack storage in the current function's entry block
/// and stores its initializer. Bindings persist for the rest of the function.
class VarStmtAST : public ExprAST {
  vector<VarBinding> VarNames;

public:
  VarStmtAST(vector<VarBinding> VarNames) : VarNames(std::move(VarNames)) {
    setType(ValueType::None);
  }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
///
/// For user-defined operators, IsOperator is true and the function name encodes
/// the operator character: "binary+" for a binary '+' operator, "unary!" for a
/// unary '!' operator. Precedence is only meaningful for binary operators — it
/// is installed into BinopPrecedence at codegen time, making the new operator
/// immediately available to the parser for subsequent expressions.
class PrototypeAST {
  string Name;
  vector<pair<string, ValueType>> Args;
  ValueType ReturnType;
  bool IsOperator;
  unsigned Precedence; // binary operators only
  SourceLocation Loc;

public:
  PrototypeAST(const string &Name, vector<pair<string, ValueType>> Args,
               SourceLocation Loc, ValueType ReturnType = ValueType::Float64,
               bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), Args(std::move(Args)), ReturnType(ReturnType),
        IsOperator(IsOperator), Precedence(Prec), Loc(Loc) {}

  const string &getName() const { return Name; }
  const vector<pair<string, ValueType>> &getArgs() const { return Args; }
  size_t getNumArgs() const { return Args.size(); }
  SourceLocation getLocation() const { return Loc; }
  ValueType getReturnType() const { return ReturnType; }
  void setReturnType(ValueType Type) { ReturnType = Type; }

  ValueType getArgType(size_t Index) const {
    if (Index >= Args.size())
      return ValueType::Error;
    return Args[Index].second;
  }

  bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  // The operator character is the last character of the encoded name.
  // e.g. "binary+" -> '+', "unary!" -> '!'
  char getOperatorName() const {
    assert((isUnaryOp() || isBinaryOp()) && "Not an operator prototype");
    return Name.back();
  }

  unsigned getBinaryPrecedence() const { return Precedence; }

  std::unique_ptr<PrototypeAST> clone() const {
    return std::make_unique<PrototypeAST>(Name, Args, Loc, ReturnType,
                                          IsOperator, Precedence);
  }

  Function *codegen();
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  unique_ptr<PrototypeAST> Proto;
  unique_ptr<ExprAST> Body;

public:
  FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
  const string &getName() const { return Proto->getName(); }
  ValueType getReturnType() const { return Proto->getReturnType(); }
  Function *codegen();
};

} // end anonymous namespace

//===----------------------------------------===//
// Parser
//===----------------------------------------===//

/// CurTok is the current token the parser is looking at.
/// getNextToken reads the next token from the lexer and stores it in CurTok.
/// Every parse function assumes CurTok is already loaded before it is called,
/// and leaves CurTok pointing at the first token it did not consume.
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

/// consumeNewlines - Consume all consecutive tok_eol tokens.
///
/// Called after eating a structural token (e.g. ':') to allow the body or
/// next clause to appear on the following line.
static void consumeNewlines() {
  while (CurTok == tok_eol)
    getNextToken();
}

/// BinopPrecedence - Maps each binary operator token to its precedence.
/// Higher numbers bind more tightly: '*' (40) > '+'/'-' (20) > comparisons
/// (10). The key is an int rather than char so it can hold both
/// single-character ASCII operators ('+', '-', '*', '<', '>') and
/// multi-character named token enums (tok_eq, tok_neq, tok_leq, tok_geq). All
/// comparison operators share precedence 10 so they bind equally tightly and
/// are left-associative. Operators not in this map return -1 from
/// GetTokPrecedence(), which tells ParseBinOpRHS to stop consuming operators
/// and return what it has so far.
static const map<int, int> DefaultBinopPrecedence = {
    {tok_eq, 10},  // ==
    {tok_neq, 10}, // !=
    {tok_leq, 10}, // <=
    {tok_geq, 10}, // >=
    {'<', 10},     // <
    {'>', 10},     // >
    {'+', 20},     // +
    {'-', 20},     // -
    {'*', 40},     // *
};
static map<int, int> BinopPrecedence = DefaultBinopPrecedence;

static void ResetBinopPrecedence() { BinopPrecedence = DefaultBinopPrecedence; }

// KnownUnaryOperators - Tracks unary operator tokens that are already reserved
// or defined.
//
// Seed with '-' because unary minus is a built-in form handled by
// ParseUnaryMinus(), so users cannot define a custom unary '-'.
static const std::set<int> DefaultKnownUnaryOperators = {'-'};
static std::set<int> KnownUnaryOperators = DefaultKnownUnaryOperators;

static void ResetKnownUnaryOperators() {
  KnownUnaryOperators = DefaultKnownUnaryOperators;
}

// FunctionProtos - Persistent prototype registry used by the parser to detect
// redefinition of operators. Also used by codegen to re-emit declarations into
// fresh modules. Declared here so parser functions can access it.
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

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
// Set when we hit a parse/codegen error; used to abort further processing.
static bool HadError = false;
// Current function's declared return type during parsing/codegen.
static ValueType CurrentFunctionReturnType = ValueType::None;

struct TopLevelParseGuard {
  TopLevelParseGuard() { ParsingTopLevel = true; }
  ~TopLevelParseGuard() { ParsingTopLevel = false; }
};

static void BeginFunctionScope(const vector<pair<string, ValueType>> &Args) {
  VarScopes.clear();
  VarScopes.emplace_back();
  for (const auto &Arg : Args)
    VarScopes.front()[Arg.first] = Arg.second;
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
static bool IsDeclaredInCurrentScope(const string &Name) {
  if (VarScopes.empty())
    return false;
  return VarScopes.back().count(Name) > 0;
}

// Ensure a function scope exists, then add a new scope for the loop variable.
static void EnterLoopScope(const string &Name, ValueType Type) {
  if (VarScopes.empty())
    VarScopes.emplace_back();
  VarScopes.emplace_back();
  VarScopes.back()[Name] = Type;
}

// Size == 1 is only popped for top-level blocks (function scope is popped in
// EndFunctionScope).
static void ExitLoopScope() {
  if (VarScopes.size() > 1)
    VarScopes.pop_back();
  if (ParsingTopLevel && VarScopes.size() == 1)
    VarScopes.pop_back();
}

struct FunctionScopeGuard {
  FunctionScopeGuard(const vector<pair<string, ValueType>> &Args) {
    BeginFunctionScope(Args);
  }
  ~FunctionScopeGuard() { EndFunctionScope(); }
};

struct LoopScopeGuard {
  LoopScopeGuard(const string &Name, ValueType Type) {
    EnterLoopScope(Name, Type);
  }
  ~LoopScopeGuard() { ExitLoopScope(); }
};

struct BlockScopeGuard {
  BlockScopeGuard() { BeginBlockScope(); }
  ~BlockScopeGuard() { EndBlockScope(); }
};

struct ReturnTypeGuard {
  ValueType Saved;
  ReturnTypeGuard(ValueType Type) : Saved(CurrentFunctionReturnType) {
    CurrentFunctionReturnType = Type;
  }
  ~ReturnTypeGuard() { CurrentFunctionReturnType = Saved; }
};

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

/// GetTokPrecedence - Returns the precedence of CurTok if it is a known binary
/// operator, or -1 if it is not. Both single-character ASCII operators ('+',
/// '-', '*', '<', '>') and named multi-character token enums (tok_eq, tok_neq,
/// tok_leq, tok_geq) are looked up in BinopPrecedence.
static int GetTokPrecedence() {
  auto It = BinopPrecedence.find(CurTok);
  if (It == BinopPrecedence.end() || It->second <= 0)
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
/// function definition.", etc.). Silent when processing a script file so
/// that stdout/stderr output from the program itself is not cluttered.
void Log(const string &message) {
  if (IsRepl && ShouldDumpIR())
    fprintf(stderr, "%s", message.c_str());
}

/// LogError* - Error reporting helpers. Each returns nullptr for its respective
/// type so parse functions can write: return LogError("message");
unique_ptr<ExprAST> LogError(const char *Str) {
  HadError = true;
  SourceLocation Anchor = GetDiagnosticAnchorLoc(CurLoc, CurTok);
  fprintf(stderr, "Error (Line %d, Column %d): %s\n", Anchor.Line, Anchor.Col,
          Str);
  PrintErrorSourceContext(Anchor);
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
static unique_ptr<ExprAST> ParsePrimary();
static unique_ptr<ExprAST> ParseVarStmt();
static unique_ptr<ExprAST> ParseStatement();
static unique_ptr<ExprAST> ParseSimpleStmt();
static unique_ptr<ExprAST> ParseBlock();
static unique_ptr<ExprAST> ParseFunctionBody(bool *BodyIsBlock);

// Inside a block: true if the last statement was a compound block (if/for),
// so the next statement can start without a tok_eol.
static bool LastStatementWasBlock = false;

// At top level: true if the last top‑level form ended with a block,
// so the next top‑level form can start without a tok_eol.
static bool LastTopLevelEndedWithBlock = false;

// Counter to give each anonymous top-level expression a unique name.
static unsigned TopLevelExprCounter = 0;
// Whether the last top-level form should be printed in the REPL.
static bool LastTopLevelShouldPrint = true;

static unique_ptr<ExprAST> ParseSuite(bool *EndedWithBlock);
static ValueType ParseTypeToken();
static const char *TypeName(ValueType Type);
static bool IsNumericType(ValueType Type);
static bool IsIntType(ValueType Type);
static bool IsFloatType(ValueType Type);
static bool IsAssignable(ValueType Dest, ValueType Src);
static Type *LLVMTypeFor(ValueType Type);
static PrototypeAST *GetFunctionProto(const string &Name);
// Optional expected type for numeric literals (used for float/float32).
static ValueType ExpectedLiteralType = ValueType::Error;

struct ExpectedLiteralTypeGuard {
  ValueType Saved;
  ExpectedLiteralTypeGuard(ValueType Type) : Saved(ExpectedLiteralType) {
    ExpectedLiteralType = Type;
  }
  ~ExpectedLiteralTypeGuard() { ExpectedLiteralType = Saved; }
};

static unique_ptr<ExprAST> MakeZeroLiteral(ValueType Type) {
  if (IsIntType(Type)) {
    unsigned Bits = LLVMTypeFor(Type)->getIntegerBitWidth();
    return make_unique<NumberExprAST>(APInt(Bits, 0), Type);
  }
  if (IsFloatType(Type)) {
    const fltSemantics &Semantics = (Type == ValueType::Float32)
                                        ? APFloat::IEEEsingle()
                                        : APFloat::IEEEdouble();
    return make_unique<NumberExprAST>(APFloat(Semantics, "0"), Type);
  }
  if (Type == ValueType::Bool)
    return make_unique<BoolExprAST>(false);
  return LogError("Cannot default-initialize this type");
}

/// numberexpr
///   = number ;
static unique_ptr<ExprAST> ParseNumberExpr() {
  ValueType Type = NumIsFloat ? ValueType::Float64 : ValueType::Int;
  if (NumIsFloat) {
    if (IsFloatType(ExpectedLiteralType))
      Type = ExpectedLiteralType;
    const fltSemantics &Semantics = (Type == ValueType::Float32)
                                        ? APFloat::IEEEsingle()
                                        : APFloat::IEEEdouble();
    APFloat Val(Semantics);
    auto StatusOrErr =
        Val.convertFromString(NumLiteralStr, APFloat::rmNearestTiesToEven);

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

    auto Result = make_unique<NumberExprAST>(Val, Type);
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
    unsigned NeededBits = APInt::getBitsNeeded(NumLiteralStr, 10);
    unsigned ParseBits = std::max(Bits, NeededBits);
    APInt Val(ParseBits, NumLiteralStr, 10);

    // Reject if the literal doesn't fit in the target signed width.
    APInt Max = APInt::getSignedMaxValue(Bits);
    if (Val.ugt(Max))
      return LogError("Integer literal out of range for type");

    // Truncate down to the target width once it's known to fit.
    // This will actually never happen. It's a paranoia move.
    if (ParseBits != Bits)
      Val = Val.trunc(Bits);

    auto Result = make_unique<NumberExprAST>(Val, Type);
    getNextToken(); // consume the number
    return std::move(Result);
  }
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
static ValueType ParseTypeToken() {
  switch (CurTok) {
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
    LogError("Expected a type");
    return ValueType::Error;
  }
}

/// castexpr
///   = casttype "(" expression ")" ;
static unique_ptr<ExprAST> ParseCastExpr() {
  ValueType Type = ParseTypeToken();
  if (Type == ValueType::Error)
    return nullptr;
  if (Type == ValueType::None)
    return LogError("Cannot cast to None");
  if (CurTok != '(')
    return LogError("Expected '(' after cast type");
  getNextToken(); // eat '('
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  if (CurTok != ')')
    return LogError("Expected ')' after cast expression");
  getNextToken(); // eat ')'
  return make_unique<CastExprAST>(Type, std::move(Expr));
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
///   | callexpr ;
///
/// callexpr
///   = identifier "(" [ expression { "," expression } ] ")" ;
static unique_ptr<ExprAST> ParseIdentifierExprWithName(string IdName) {
  if (CurTok != '(') { // Simple variable ref.
    ValueType Type = LookupVarType(IdName);
    if (Type == ValueType::Error) {
      return LogError("Unknown variable name");
    }
    return make_unique<VariableExprAST>(IdName, Type);
  }

  // Call.
  getNextToken(); // eat (

  // Proto may be null for forward references. We still parse the call to keep
  // the token stream aligned; the “unknown function” error is raised later
  // during semantic/codegen.
  PrototypeAST *Proto = GetFunctionProto(IdName);
  vector<unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    size_t ArgIndex = 0;
    while (true) {
      ValueType Expected = ValueType::Error;
      if (Proto && ArgIndex < Proto->getNumArgs())
        Expected = Proto->getArgType(ArgIndex);
      {
        ExpectedLiteralTypeGuard Guard(Expected);
        if (auto Arg = ParseExpression())
          Args.push_back(std::move(Arg));
        else
          return nullptr;
      }

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
      ++ArgIndex;
    }
  }

  // Eat the ')'.
  getNextToken();

  if (!Proto)
    return LogError("Unknown function referenced");
  if (Proto->getNumArgs() != Args.size())
    return LogError("Incorrect # arguments passed");

  for (size_t i = 0; i < Args.size(); ++i) {
    ValueType ArgType = Args[i]->getType();
    ValueType ParamType = Proto->getArgType(i);
    if (!IsAssignable(ParamType, ArgType)) {
      return LogError(("argument " + std::to_string(i + 1) + " expects " +
                       TypeName(ParamType))
                          .c_str());
    }
  }

  return make_unique<CallExprAST>(IdName, std::move(Args),
                                  Proto->getReturnType());
}

static unique_ptr<ExprAST> ParseIdentifierExpr() {
  string IdName = IdentifierStr;

  getNextToken(); // eat identifier.

  return ParseIdentifierExprWithName(std::move(IdName));
}

// ParseForParts - Parse the "= start, cond, step : suite" tail of a for-loop.
// Also validates the parts against VarType (start/step assignable, cond bool).
// Returns true on success and fills Start/Cond/Step/Body plus BodyIsBlock.
static bool ParseForParts(ValueType VarType, unique_ptr<ExprAST> &Start,
                          unique_ptr<ExprAST> &Cond, unique_ptr<ExprAST> &Step,
                          unique_ptr<ExprAST> &Body, bool &BodyIsBlock) {
  if (CurTok != '=')
    return LogError("Expected '=' after for variable"), false;
  getNextToken(); // eat '='

  Start = ParseExpression();
  if (!Start)
    return false;
  if (!IsAssignable(VarType, Start->getType()))
    return LogError("For loop start must match loop variable type"), false;
  if (!IsNumericType(VarType))
    return LogError("For loop variable must be numeric"), false;

  if (CurTok != ',')
    return LogError("Expected ',' after for start value"), false;
  getNextToken(); // eat ','

  Cond = ParseExpression();
  if (!Cond)
    return false;
  if (Cond->getType() != ValueType::Bool)
    return LogError("For loop condition must be bool"), false;

  if (CurTok != ',')
    return LogError("Expected ',' after for condition"), false;
  getNextToken(); // eat ','

  Step = ParseExpression();
  if (!Step)
    return false;
  if (!IsAssignable(VarType, Step->getType()))
    return LogError("For loop step must match loop variable type"), false;

  if (CurTok != ':')
    return LogError("Expected ':' after for step"), false;
  getNextToken(); // eat ':'

  // Parse the suite after ':' (inline statement or indented block).
  Body = ParseSuite(&BodyIsBlock);
  if (!Body)
    return false;

  return true;
}

/// forstmt  = "for"
///            ("var" identifier ":" type | identifier)
///            "=" expression "," expression "," expression ":" suite;
///
/// "for var" introduces a new loop variable scoped to the loop statement.
/// A plain "for i = ..." reuses an existing variable (error if undeclared).
static unique_ptr<ExprAST> ParseForStmt() {
  getNextToken(); // eat 'for'

  bool IsVarDecl = false;
  if (CurTok == tok_var) {
    IsVarDecl = true;
    getNextToken(); // optional 'var'
  }

  if (CurTok != tok_identifier)
    return LogError("Expected identifier after 'for'");
  string VarName = IdentifierStr;
  getNextToken(); // eat identifier

  ValueType VarType = ValueType::Error;
  if (IsVarDecl) {
    if (CurTok != ':')
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
    if (CurTok == ':')
      return LogError("For loop variable requires 'var' to declare a type");
    VarType = LookupVarType(VarName);
    if (VarType == ValueType::Error)
      return LogError("Assignment to undeclared variable");
  }

  unique_ptr<ExprAST> Start, Cond, Step, Body;
  bool BodyIsBlock = false;

  if (IsVarDecl) {
    LoopScopeGuard LoopScope(VarName, VarType);
    if (!ParseForParts(VarType, Start, Cond, Step, Body, BodyIsBlock))
      return nullptr;
  } else {
    if (!ParseForParts(VarType, Start, Cond, Step, Body, BodyIsBlock))
      return nullptr;
  }

  LastStatementWasBlock = BodyIsBlock;
  return make_unique<ForExprAST>(VarName, IsVarDecl, VarType, std::move(Start),
                                 std::move(Cond), std::move(Step),
                                 std::move(Body));
}

/// varstmt
///   = "var" varbinding { "," varbinding } ;
///
/// varbinding
///   = identifier ":" type [ "=" expression ] ;
static unique_ptr<ExprAST> ParseVarStmt() {
  getNextToken(); // eat 'var'

  vector<VarBinding> VarNames;
  bool IsGlobalDecl = ParsingTopLevel;

  while (true) {
    // identifier ":" type
    if (CurTok != tok_identifier)
      return LogError("Expected identifier after 'var'");

    string Name = IdentifierStr;
    getNextToken(); // eat identifier

    if (CurTok != ':')
      return LogError(
          "Variable declaration requires a type annotation (e.g., ': int32')");
    getNextToken(); // eat ':'
    ValueType DeclType = ParseTypeToken();
    if (DeclType == ValueType::Error)
      return nullptr;
    if (DeclType == ValueType::None)
      return LogError("Variables cannot have None type");

    if (IsGlobalDecl) {
      if (GlobalVarDecls.count(Name))
        return LogError(
            ("Variable '" + Name + "' already declared in this scope").c_str());
    } else {
      if (IsDeclaredInCurrentScope(Name))
        return LogError(
            ("Variable '" + Name + "' already declared in this scope").c_str());
    }

    unique_ptr<ExprAST> Init;
    // [ "=" expression ]
    if (CurTok == '=') {
      getNextToken(); // eat '='
      ExpectedLiteralTypeGuard Guard(DeclType);
      Init = ParseExpression();
      if (!Init)
        return nullptr;
      if (!IsAssignable(DeclType, Init->getType()))
        return LogError("Type mismatch in variable initialization");
    } else {
      Init = MakeZeroLiteral(DeclType);
      if (!Init)
        return nullptr;
    }

    VarNames.push_back({Name, DeclType, std::move(Init)});
    if (IsGlobalDecl)
      GlobalVarTypes[Name] = DeclType, GlobalVarDecls.insert(Name);
    else
      DeclareVar(Name, DeclType);

    if (CurTok != ',')
      break;
    getNextToken(); // eat ','
  }

  return make_unique<VarStmtAST>(std::move(VarNames));
}

/// ifstmt
///   = "if" expression ":" suite [ eols "else" ":" suite ] ;
static unique_ptr<ExprAST> ParseIfStmt() {
  getNextToken(); // eat 'if'

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;
  if (Cond->getType() != ValueType::Bool)
    return LogError("If condition must be bool");

  if (CurTok != ':')
    return LogError("Expected ':' after if condition");
  getNextToken(); // eat ':'

  bool ThenIsBlock = false;
  unique_ptr<ExprAST> Then = ParseSuite(&ThenIsBlock);
  if (!Then)
    return nullptr;

  // Allow 'else' on next line
  consumeNewlines();

  unique_ptr<ExprAST> Else;
  bool ElseIsBlock = false;
  if (CurTok == tok_else) {
    getNextToken(); // eat 'else'
    if (CurTok != ':')
      return LogError("Expected ':' after else");
    getNextToken(); // eat ':'
    Else = ParseSuite(&ElseIsBlock);
    if (!Else)
      return nullptr;
  }

  LastStatementWasBlock = ThenIsBlock || ElseIsBlock;
  return make_unique<IfStmtAST>(std::move(Cond), std::move(Then),
                                std::move(Else));
}

static unique_ptr<ExprAST>
ParseUnary(); // forward declaration for ParseUnaryMinus

static bool IsIntType(ValueType Type);
static bool IsFloatType(ValueType Type);
static bool IsNumericType(ValueType Type);

static bool IsFixedIntType(ValueType Type) {
  return Type == ValueType::Int8 || Type == ValueType::Int16 ||
         Type == ValueType::Int32 || Type == ValueType::Int64;
}

static int FixedIntRank(ValueType Type) {
  switch (Type) {
  case ValueType::Int8:
    return 1;
  case ValueType::Int16:
    return 2;
  case ValueType::Int32:
    return 3;
  case ValueType::Int64:
    return 4;
  default:
    return 0;
  }
}

static bool CanWidenInt(ValueType From, ValueType To) {
  if (From == To)
    return true;
  if (IsFixedIntType(From) && IsFixedIntType(To))
    return FixedIntRank(From) <= FixedIntRank(To);
  if (From == ValueType::Int && To == ValueType::Int64)
    return true;
  return false;
}

static bool IsComparisonOp(int Op) {
  return Op == '<' || Op == '>' || Op == tok_eq || Op == tok_neq ||
         Op == tok_leq || Op == tok_geq;
}

static bool IsArithmeticOp(int Op) {
  return Op == '+' || Op == '-' || Op == '*';
}

static ValueType GetBinaryResultType(int Op, ValueType L, ValueType R) {
  if (IsArithmeticOp(Op)) {
    if (!IsNumericType(L) || !IsNumericType(R))
      return ValueType::Error;
    if (IsFloatType(L) && IsFloatType(R)) {
      if (L == R)
        return L;
      if ((L == ValueType::Float && R == ValueType::Float64) ||
          (L == ValueType::Float64 && R == ValueType::Float))
        return ValueType::Float64;
      return ValueType::Error;
    }
    if (IsAssignable(L, R))
      return L;
    if (IsAssignable(R, L))
      return R;
    return ValueType::Error;
  }
  if (IsComparisonOp(Op)) {
    if (L == ValueType::Bool && R == ValueType::Bool) {
      if (Op == tok_eq || Op == tok_neq)
        return ValueType::Bool;
      return ValueType::Error;
    }
    if (!IsNumericType(L) || !IsNumericType(R))
      return ValueType::Error;
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
  // User-defined operators are float64-only.
  if (L == ValueType::Float64 && R == ValueType::Float64)
    return ValueType::Float64;
  return ValueType::Error;
}

/// unaryminus
///   = "-" unaryexpr ;
/// Parse built-in unary minus into a UnaryExprAST with opcode '-'.
/// The operand is a full unaryexpr so unary chains work naturally
/// (e.g. -!x, --x, -(x+1)).
static unique_ptr<ExprAST> ParseUnaryMinus() {
  getNextToken(); // eat '-'
  auto Operand = ParseUnary();
  if (!Operand)
    return nullptr;
  if (!IsNumericType(Operand->getType()))
    return LogError("Unary '-' requires a numeric operand");
  return make_unique<UnaryExprAST>('-', std::move(Operand), Operand->getType());
}

/// primary
///   = castexpr
///   | identifierexpr
///   | numberexpr
///   | bool-literal
///   | parenexpr ;
static unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError("unknown token when expecting an expression");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case tok_true:
    getNextToken();
    return make_unique<BoolExprAST>(true);
  case tok_false:
    getNextToken();
    return make_unique<BoolExprAST>(false);
  case tok_int:
  case tok_int8:
  case tok_int16:
  case tok_int32:
  case tok_int64:
  case tok_float:
  case tok_float32:
  case tok_float64:
  case tok_bool:
    return ParseCastExpr();
  case '(':
    return ParseParenExpr();
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
/// This is called from both ParseExpression (as the LHS seed) and from
/// ParseBinOpRHS (as the RHS of a binary operator), so user-defined unary ops
/// work in both positions: !x + 1 and f(x) + !y.
static unique_ptr<ExprAST> ParseUnary() {
  // Primary starters will be handled with ParsePrimary.
  if (!isascii(CurTok) /* multi-character tokens */ || CurTok == '(' ||
      isalpha(CurTok) || isdigit(CurTok))
    return ParsePrimary();

  // Built-in unary minus.
  if (CurTok == '-')
    return ParseUnaryMinus();

  // It's an ASCII punctuation character — treat it as a user-defined unary op.
  int Opc = CurTok;
  getNextToken(); // eat the operator character
  if (auto Operand = ParseUnary()) {
    auto Proto = GetFunctionProto(string("unary") + (char)Opc);
    if (!Proto)
      return LogError("Unknown unary operator");
    if (Proto->getNumArgs() != 1)
      return LogError("Unary operator must have exactly one argument");
    ValueType ParamTy = Proto->getArgType(0);
    if (!IsAssignable(ParamTy, Operand->getType())) {
      return LogError(
          ("unary operator expects " + string(TypeName(ParamTy))).c_str());
    }
    return make_unique<UnaryExprAST>(Opc, std::move(Operand),
                                     Proto->getReturnType());
  }
  return nullptr;
}

/// binoprhs
///   = { binaryop unaryexpr } ;
static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                         unique_ptr<ExprAST> LHS) {
  // If this is a binop, find its precedence.
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < ExprPrec)
      return LHS;

    // Okay, we know this is a binop and that binds at least as tightly as the
    // current binop.
    int BinOp = CurTok;
    getNextToken(); // eat binop

    // Parse the unary expression after the binary operator.  Using ParseUnary
    // here (rather than ParsePrimary directly) means unary operators bind
    // tighter than any binary operator, matching normal convention.
    auto RHS = ParseUnary();
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

    ValueType ResultTy = ValueType::Error;
    if (IsComparisonOp(BinOp) || IsArithmeticOp(BinOp)) {
      ResultTy = GetBinaryResultType(BinOp, LHS->getType(), RHS->getType());
      if (ResultTy == ValueType::Error)
        return LogError("Type mismatch in binary operator");
    } else {
      auto Proto = GetFunctionProto(string("binary") + (char)BinOp);
      if (!Proto)
        return LogError("Unknown binary operator");
      if (Proto->getNumArgs() != 2)
        return LogError("Binary operator must have exactly two arguments");
      ValueType LTy = Proto->getArgType(0);
      ValueType RTy = Proto->getArgType(1);
      if (!IsAssignable(LTy, LHS->getType()))
        return LogError(("binary operator expects " + string(TypeName(LTy)) +
                         " for left operand")
                            .c_str());
      if (!IsAssignable(RTy, RHS->getType()))
        return LogError(("binary operator expects " + string(TypeName(RTy)) +
                         " for right operand")
                            .c_str());
      ResultTy = Proto->getReturnType();
    }

    // Merge LHS/RHS.
    LHS = make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS),
                                     ResultTy);
  }
}

/// expression
///   = unaryexpr binoprhs ;
static unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParseUnary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

/// returnstmt
///   = "return" [ expression ] ;
static unique_ptr<ExprAST> ParseReturnStmt() {
  getNextToken(); // eat 'return'
  if (CurTok == tok_eol || CurTok == tok_dedent || CurTok == tok_eof) {
    if (CurrentFunctionReturnType != ValueType::None)
      return LogError("Return value required");
    return make_unique<ReturnExprAST>(nullptr);
  }

  ExpectedLiteralTypeGuard Guard(CurrentFunctionReturnType);
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
  return make_unique<ReturnExprAST>(std::move(Expr));
}

static unique_ptr<ExprAST> ParseAssignmentRHS(const string &Name) {
  if (!IsDeclaredVar(Name))
    return LogError("Assignment to undeclared variable");
  ValueType VarType = LookupVarType(Name);
  getNextToken(); // eat '='

  ExpectedLiteralTypeGuard Guard(VarType);
  auto RHS = ParseExpression();
  if (!RHS)
    return nullptr;
  if (!IsAssignable(VarType, RHS->getType()))
    return LogError("Type mismatch in assignment");
  return make_unique<AssignmentExprAST>(Name, std::move(RHS), VarType);
}

/// simplestmt
///   = returnstmt | varstmt | assignstmt | expression ;
static unique_ptr<ExprAST> ParseSimpleStmt() {
  if (CurTok == tok_return)
    return ParseReturnStmt();
  if (CurTok == tok_var)
    return ParseVarStmt();

  unique_ptr<ExprAST> Expr;
  if (CurTok == tok_identifier) {
    string Name = IdentifierStr;
    getNextToken(); // eat identifier.

    if (CurTok == '=') {
      return ParseAssignmentRHS(Name);
    }

    Expr = ParseIdentifierExprWithName(std::move(Name));
    if (!Expr)
      return nullptr;
    Expr = ParseBinOpRHS(0, std::move(Expr));
    if (!Expr)
      return nullptr;

    if (CurTok != '=')
      return Expr;

    const string *AssignedName = Expr->getLValueName();
    if (!AssignedName)
      return LogError("Destination of '=' must be a variable");

    return ParseAssignmentRHS(*AssignedName);
  }

  Expr = ParseExpression();
  if (!Expr)
    return nullptr;

  if (CurTok != '=')
    return Expr;

  return LogError("Destination of '=' must be a variable");
}

/// statement
///   = simplestmt | compoundstmt ;
static unique_ptr<ExprAST> ParseStatement() {
  LastStatementWasBlock = false;
  if (CurTok == tok_if)
    return ParseIfStmt();
  if (CurTok == tok_for)
    return ParseForStmt();
  return ParseSimpleStmt();
}

/// suite
///   = simplestmt | compoundstmt | eols block ;
static unique_ptr<ExprAST> ParseSuite(bool *EndedWithBlock) {
  if (CurTok == tok_eol) {
    consumeNewlines();
    if (CurTok != tok_indent)
      return LogError("Expected an indented block");
    *EndedWithBlock = true;
    return ParseBlock();
  }

  if (CurTok == tok_indent) {
    *EndedWithBlock = true;
    return ParseBlock();
  }

  auto Stmt = ParseStatement();
  if (!Stmt)
    return nullptr;
  *EndedWithBlock = LastStatementWasBlock;
  return Stmt;
}

/// block
///   = INDENT statement { eols statement } DEDENT ;
static unique_ptr<ExprAST> ParseBlock() {
  if (CurTok != tok_indent)
    return LogError("Expected an indented block");
  getNextToken(); // eat INDENT

  BlockScopeGuard Scope;

  consumeNewlines();

  vector<unique_ptr<ExprAST>> Stmts;
  if (CurTok == tok_dedent)
    return LogError("Expected at least one statement in block");

  while (true) {
    if (CurTok == tok_dedent)
      break;

    auto Stmt = ParseStatement();
    if (!Stmt)
      return nullptr;
    Stmts.push_back(std::move(Stmt));

    if (CurTok == tok_eol) {
      consumeNewlines();
      continue;
    }

    if (CurTok == tok_dedent)
      break;

    if (LastStatementWasBlock)
      continue;

    // if (CurTok != tok_eol && CurTok != dedent && !LastStatementWasBlock)
    // error()
    return LogError("Expected newline or end of block");
  }

  if (CurTok != tok_dedent)
    return LogError("Expected end of block");
  getNextToken(); // eat DEDENT

  return make_unique<BlockExprAST>(std::move(Stmts));
}

/// prototype
///   = identifier "(" [ typedparam { "," typedparam } ] ")" ;
///
/// typedparam
///   = identifier ":" type ;
static unique_ptr<PrototypeAST> ParsePrototype() {
  SourceLocation ProtoLoc = CurLoc;

  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");
  string FnName = IdentifierStr;
  getNextToken(); // eat function name

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");

  vector<pair<string, ValueType>> ArgNames;
  getNextToken(); // eat '('

  if (CurTok != ')') {
    while (true) {
      if (CurTok != tok_identifier)
        return LogErrorP("Expected parameter name in prototype");
      string ArgName = IdentifierStr;
      getNextToken(); // eat identifier

      if (CurTok != ':')
        return LogErrorP(
            "Parameter requires a type annotation (e.g., ': int32')");
      getNextToken(); // eat ':'
      ValueType ArgTy = ParseTypeToken();
      if (ArgTy == ValueType::Error)
        return nullptr;
      if (ArgTy == ValueType::None)
        return LogErrorP("Parameters cannot have None type");
      ArgNames.push_back({ArgName, ArgTy});

      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogErrorP("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  getNextToken(); // eat ')'
  return make_unique<PrototypeAST>(FnName, std::move(ArgNames), ProtoLoc);
}

// DefaultType controls what return type is assumed when no '->' is present.
// In chapter 16, missing return types default to None.
static ValueType
ParseOptionalReturnType(ValueType DefaultType = ValueType::None) {
  if (CurTok != tok_arrow)
    return DefaultType;
  getNextToken(); // eat '->'
  ValueType Type = ParseTypeToken();
  return Type;
}

/// functionbody
///   = simplestmt | eols block ;
static unique_ptr<ExprAST> ParseFunctionBody(bool *BodyIsBlock) {
  if (CurTok == tok_eol) {
    consumeNewlines();
    if (CurTok != tok_indent)
      return LogError("Expected an indented block");
    *BodyIsBlock = true;
    return ParseBlock();
  }

  *BodyIsBlock = false;
  return ParseSimpleStmt();
}

/// definition
///   = "def" prototype [ "->" type ] ":" ( simplestmt | eols block ) ;
static unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;
  ValueType RetTy = ParseOptionalReturnType(ValueType::None);
  if (RetTy == ValueType::Error)
    return nullptr;
  Proto->setReturnType(RetTy);
  FunctionProtos[Proto->getName()] = Proto->clone();
  ReturnTypeGuard RetGuard(RetTy);
  FunctionScopeGuard Scope(Proto->getArgs());

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'

  bool BodyIsBlock = false;
  unique_ptr<ExprAST> Body = ParseFunctionBody(&BodyIsBlock);

  if (Body) {
    LastTopLevelEndedWithBlock = BodyIsBlock;
    return make_unique<FunctionAST>(std::move(Proto), std::move(Body));
  }
  FunctionProtos.erase(Proto->getName());
  return nullptr;
}

/// binarydecorator
///   = "@" "binary" "(" integer ")"
///
/// Called after '@' has been consumed. CurTok is on 'binary'.
/// Returns the parsed precedence (>= 1), or 0 on error.
/// 0 is a safe sentinel because valid precedences must be >= 1.
static unsigned ParseBinaryDecorator() {
  getNextToken(); // eat 'binary'

  if (CurTok != '(') {
    LogError("Expected '(' after '@binary'");
    return 0;
  }
  getNextToken(); // eat '('

  if (CurTok != tok_number) {
    LogError("Expected precedence number in '@binary(...)'");
    return 0;
  }
  // The lexer has no separate tok_integer — it emits tok_number for both
  // integer and decimal literals. Reject decimals/exponents by checking the
  // raw source.
  if (NumLiteralStr.find('.') != string::npos ||
      NumLiteralStr.find('e') != string::npos ||
      NumLiteralStr.find('E') != string::npos) {
    LogError("Precedence must be an integer, not a decimal literal");
    return 0;
  }
  unsigned NeededBits = APInt::getBitsNeeded(NumLiteralStr, 10);
  if (NeededBits > 32) {
    LogError("Precedence is too large");
    return 0;
  }
  APInt Val(NeededBits == 0 ? 1 : NeededBits, NumLiteralStr, 10);
  if (Val.isZero()) {
    LogError("Precedence must be a positive integer");
    return 0;
  }
  unsigned Prec = static_cast<unsigned>(Val.getZExtValue());
  getNextToken(); // eat number

  if (CurTok != ')') {
    LogError("Expected ')' after precedence in '@binary(...)'");
    return 0;
  }
  getNextToken(); // eat ')'

  return Prec;
}

/// unarydecorator
///   = "@" "unary"
/// Called after '@' has been consumed. CurTok is on 'unary'.
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
  return isascii(Tok) && ispunct(static_cast<unsigned char>(Tok)) && Tok != '@';
}

// IsKnownBinaryOperatorToken - Return true if Tok is already present in the
// parser's binary-operator table.
//
// BinopPrecedence contains built-in binary operators at startup and gains
// custom binary operators as their prototypes are codegen'd. This makes it a
// single source of truth for "is this binary operator already known?".
static bool IsKnownBinaryOperatorToken(int Tok) {
  return BinopPrecedence.find(Tok) != BinopPrecedence.end();
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
/// CurTok is on the operator character.
/// The function is stored internally as "binary<opchar>" (e.g. "binary%"),
/// which is how BinaryExprAST::codegen() looks it up at call sites.
static unique_ptr<PrototypeAST> ParseBinaryOpPrototype(unsigned Precedence) {
  SourceLocation ProtoLoc = CurLoc;
  if (!IsCustomOpChar(CurTok))
    return LogErrorP(
        "Expected operator character in binary operator prototype");

  char OpChar = (char)CurTok;
  string FnName = string("binary") + OpChar;

  // Reject redefining any binary operator that is already known to the parser.
  // This covers both language built-ins and previously defined custom
  // operators, since both live in BinopPrecedence.
  if (IsKnownBinaryOperatorToken(CurTok))
    return LogErrorP(
        (string("Binary operator '") + OpChar + "' is already defined")
            .c_str());

  // Reject cross-arity reuse: if a token is already known as a unary operator,
  // we do not allow defining it as binary.
  if (IsKnownUnaryOperatorToken(CurTok))
    return LogErrorP((string("Binary operator '") + OpChar +
                      "' conflicts with an existing unary operator")
                         .c_str());

  // Separate guard: reject any existing function/prototype named "binary<op>".
  // This catches symbol collisions even if the operator was not registered in
  // BinopPrecedence (e.g. an earlier extern/def with the same encoded name).
  // Without this, a new definition could silently shadow the old symbol in the
  // JIT. For operators, we don't want this. For other functions, shadowing is
  // permissable.
  if (FunctionProtos.count(FnName))
    return LogErrorP(
        (string("Binary operator '") + OpChar + "' is already defined")
            .c_str());

  getNextToken(); // eat operator char

  if (CurTok != '(')
    return LogErrorP("Expected '(' in binary operator prototype");

  vector<pair<string, ValueType>> ArgNames;
  getNextToken(); // eat '('
  if (CurTok != ')') {
    while (true) {
      if (CurTok != tok_identifier)
        return LogErrorP("Expected parameter name in operator prototype");
      string ArgName = IdentifierStr;
      getNextToken(); // eat identifier
      if (CurTok != ':')
        return LogErrorP("Operator parameters require a type annotation (e.g., "
                         "': float64')");
      getNextToken(); // eat ':'
      ValueType ArgTy = ParseTypeToken();
      if (ArgTy == ValueType::Error)
        return nullptr;
      if (ArgTy == ValueType::None)
        return LogErrorP("Parameters cannot have None type");
      ArgNames.push_back({ArgName, ArgTy});

      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogErrorP("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in binary operator prototype");
  getNextToken(); // eat ')'

  if (ArgNames.size() != 2)
    return LogErrorP("Binary operator must have exactly two arguments");

  return make_unique<PrototypeAST>(FnName, std::move(ArgNames), ProtoLoc,
                                   ValueType::None, /*IsOperator=*/true,
                                   Precedence);
}

/// unaryopprototype
///   = customopchar "(" typedparam ")"
///
/// CurTok is on the operator character.
/// The function is stored internally as "unary<opchar>" (e.g. "unary&"),
/// which is how ParseUnary() looks it up at call sites.
static unique_ptr<PrototypeAST> ParseUnaryOpPrototype() {
  SourceLocation ProtoLoc = CurLoc;
  if (!IsCustomOpChar(CurTok))
    return LogErrorP("Expected operator character in unary operator prototype");

  char OpChar = (char)CurTok;
  string FnName = string("unary") + OpChar;

  // Reject redefining any unary operator that is already known to the parser.
  // This covers reserved unary operators and previously defined custom unary
  // operators tracked in KnownUnaryOperators.
  if (IsKnownUnaryOperatorToken(CurTok))
    return LogErrorP(
        (string("Unary operator '") + OpChar + "' is already defined").c_str());

  // Reject cross-arity reuse: if a token is already known as a binary operator,
  // we do not allow defining it as unary.
  if (IsKnownBinaryOperatorToken(CurTok))
    return LogErrorP((string("Unary operator '") + OpChar +
                      "' conflicts with an existing binary operator")
                         .c_str());

  // Prevent silent JIT shadowing (same reason as in ParseBinaryOpPrototype).
  if (FunctionProtos.count(FnName))
    return LogErrorP(
        (string("Unary operator '") + OpChar + "' is already defined").c_str());

  getNextToken(); // eat operator char

  if (CurTok != '(')
    return LogErrorP("Expected '(' in unary operator prototype");

  vector<pair<string, ValueType>> ArgNames;
  getNextToken(); // eat '('
  if (CurTok != ')') {
    while (true) {
      if (CurTok != tok_identifier)
        return LogErrorP("Expected parameter name in operator prototype");
      string ArgName = IdentifierStr;
      getNextToken(); // eat identifier
      if (CurTok != ':')
        return LogErrorP("Operator parameters require a type annotation (e.g., "
                         "': float64')");
      getNextToken(); // eat ':'
      ValueType ArgTy = ParseTypeToken();
      if (ArgTy == ValueType::Error)
        return nullptr;
      if (ArgTy == ValueType::None)
        return LogErrorP("Parameters cannot have None type");
      ArgNames.push_back({ArgName, ArgTy});

      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogErrorP("Expected ')' or ',' in parameter list");
      getNextToken(); // eat ','
    }
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in unary operator prototype");
  getNextToken(); // eat ')'

  if (ArgNames.size() != 1)
    return LogErrorP("Unary operator must have exactly one argument");

  // Unary operators have no precedence — they bind tighter than any binary op
  // by virtue of being parsed before ParseBinOpRHS is entered.
  return make_unique<PrototypeAST>(FnName, std::move(ArgNames), ProtoLoc,
                                   ValueType::None, /*IsOperator=*/true,
                                   /*Precedence=*/0);
}

/// decorateddef
///   = binarydecorator eols "def" binaryopprototype ":" ( simplestmt | eols
///   block ) | unarydecorator  eols "def" unaryopprototype  ":" ( simplestmt |
///   eols block )
///
/// Called after '@' has been consumed. CurTok is on 'binary' or 'unary'.
/// The two branches share the same body structure (':' / block).
static unique_ptr<FunctionAST> ParseDecoratedDef() {
  if (CurTok != tok_binary && CurTok != tok_unary)
    return LogErrorF("Expected 'binary' or 'unary' after '@'");

  bool IsBinary = (CurTok == tok_binary);
  unique_ptr<PrototypeAST> Proto;

  if (IsBinary) {
    unsigned Prec = ParseBinaryDecorator(); // consumes "binary(N)"
    if (!Prec)
      return nullptr;
    // The decorator must end at a newline before 'def'.
    if (CurTok != tok_eol)
      return LogErrorF("Expected newline after '@binary(...)' decorator");
    consumeNewlines();
    if (CurTok != tok_def)
      return LogErrorF("Expected 'def' after decorator");
    getNextToken(); // eat 'def'
    Proto = ParseBinaryOpPrototype(Prec);
  } else {
    ParseUnaryDecorator(); // consumes "unary"
    if (CurTok != tok_eol)
      return LogErrorF("Expected newline after '@unary' decorator");
    consumeNewlines();
    if (CurTok != tok_def)
      return LogErrorF("Expected 'def' after decorator");
    getNextToken(); // eat 'def'
    Proto = ParseUnaryOpPrototype();
  }

  if (!Proto)
    return nullptr;
  ValueType RetTy = ParseOptionalReturnType();
  if (RetTy == ValueType::Error)
    return nullptr;
  Proto->setReturnType(RetTy);
  FunctionProtos[Proto->getName()] = Proto->clone();
  ReturnTypeGuard RetGuard(RetTy);
  FunctionScopeGuard Scope(Proto->getArgs());

  // Shared body: ":" ( simplestmt | eols block ) — identical to
  // ParseDefinition.
  if (CurTok != ':')
    return LogErrorF("Expected ':' in operator definition");
  getNextToken(); // eat ':'

  bool BodyIsBlock = false;
  unique_ptr<ExprAST> Body = ParseFunctionBody(&BodyIsBlock);

  if (Body) {
    LastTopLevelEndedWithBlock = BodyIsBlock;
    return make_unique<FunctionAST>(std::move(Proto), std::move(Body));
  }
  FunctionProtos.erase(Proto->getName());
  return nullptr;
}

/// toplevelstmt
///   = statement ;
static unique_ptr<ExprAST> ParseTopLevelStatement() {
  LastTopLevelEndedWithBlock = false;
  TopLevelParseGuard Guard;
  ReturnTypeGuard RetGuard(ValueType::None);
  auto Stmt = ParseStatement();
  if (!Stmt)
    return nullptr;
  LastTopLevelShouldPrint = Stmt->shouldPrintValue();
  LastTopLevelEndedWithBlock = LastStatementWasBlock;
  return Stmt;
}

/// toplevelexpr
///   = statement
/// A top-level statement (e.g. "1 + 2", "var x = 1", "if ...") is wrapped in
/// an anonymous function so it fits the same FunctionAST shape as everything
/// else. HandleTopLevelExpression compiles it into the JIT, calls it to get
/// the numeric result, then removes it from the JIT via a ResourceTracker.
static unique_ptr<FunctionAST> ParseTopLevelExpr() {
  auto Stmt = ParseTopLevelStatement();
  if (!Stmt)
    return nullptr;

  ValueType RetTy = Stmt->getType();
  if (!Stmt->isReturnExpr() && RetTy != ValueType::None)
    Stmt = make_unique<ReturnExprAST>(std::move(Stmt));

  string FnName = "__pyxc.toplevel." + to_string(TopLevelExprCounter++);
  auto Proto = make_unique<PrototypeAST>(
      FnName, vector<pair<string, ValueType>>(), CurLoc, RetTy);
  return make_unique<FunctionAST>(std::move(Proto), std::move(Stmt));
}

/// external
///   = "extern" "def" prototype [ "->" type ] ;
static unique_ptr<PrototypeAST> ParseExtern() {
  LastTopLevelEndedWithBlock = false;
  getNextToken(); // eat extern.
  if (CurTok != tok_def)
    return LogErrorP("Expected `def` after extern.");
  getNextToken(); // eat def
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;
  ValueType RetTy = ParseOptionalReturnType();
  if (RetTy == ValueType::Error)
    return nullptr;
  Proto->setReturnType(RetTy);
  return Proto;
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

/// LogErrorV - Codegen-level error helper. Delegates to LogError for printing,
/// then returns nullptr so codegen callers can write: return LogErrorV("msg");
Value *LogErrorV(const char *Str) {
  LogError(Str);
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
    return Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
  if (IsFloatType(From) && IsIntType(To))
    return Builder->CreateFPToSI(V, LLVMTypeFor(To), "fptosi");
  // Integer resize (trunc or sign-extend).
  if (IsIntType(From) && IsIntType(To)) {
    unsigned FromBits = LLVMTypeFor(From)->getIntegerBitWidth();
    unsigned ToBits = LLVMTypeFor(To)->getIntegerBitWidth();
    if (ToBits < FromBits)
      return Builder->CreateTrunc(V, LLVMTypeFor(To), "trunc");
    return Builder->CreateSExt(V, LLVMTypeFor(To), "sext");
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
    return Builder->CreateSExt(V, LLVMTypeFor(To), "sext");
  }
  if (IsIntType(From) && To == ValueType::Float64)
    return Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
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

  DIType *DITy = DITypeFor(Type);
  if (!DITy)
    DITy = Float64DIType
               ? Float64DIType
               : DIB->createBasicType("float64", 64, dwarf::DW_ATE_float);
  auto *Loc = DILocation::get(*TheContext, Line, 1, CurDIScope);
  DILocalVariable *Var = nullptr;
  if (IsParam) {
    Var = DIB->createParameterVariable(CurDIScope, Name, ArgNo, TheDIFile, Line,
                                       DITy, true);
  } else {
    Var =
        DIB->createAutoVariable(CurDIScope, Name, TheDIFile, Line, DITy, true);
  }

  DIB->insertDeclare(Alloca, Var, DIB->createExpression(), Loc,
                     Builder->GetInsertBlock());
}

static void EmitDebugGlobal(GlobalVariable *GV, StringRef Name, unsigned Line,
                            ValueType Type) {
  if (!DIB || !TheCU || !GV)
    return;
  DIType *DITy = DITypeFor(Type);
  if (!DITy)
    DITy = Float64DIType
               ? Float64DIType
               : DIB->createBasicType("float64", 64, dwarf::DW_ATE_float);
  auto *GVE = DIB->createGlobalVariableExpression(TheCU, Name, Name, TheDIFile,
                                                  Line, DITy, true);
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
  auto *Type = LLVMTypeFor(GlobalVarTypes[Name]);
  return new GlobalVariable(*TheModule, Type, false,
                            GlobalValue::ExternalLinkage, nullptr, Name);
}

static PrototypeAST *GetFunctionProto(const string &Name) {
  auto It = FunctionProtos.find(Name);
  if (It != FunctionProtos.end())
    return It->second.get();
  return nullptr;
}

/// getFunction - Resolve a function name to an LLVM Function* in the current
/// module, re-emitting a declaration from FunctionProtos if necessary.
///
/// Because each top-level input gets its own Module, a function defined in an
/// earlier module is no longer in TheModule->getFunction(). When that happens
/// we look up its PrototypeAST in FunctionProtos and call codegen() on it,
/// which emits a fresh 'declare' with ExternalLinkage in the current module.
/// The JIT resolves that extern to the already-compiled body at link time.
Function *getFunction(std::string Name) {
  // Fast path: declaration or definition already in the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // Slow path: re-emit a declaration from the saved prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  return nullptr;
}

/// NumberExprAST::codegen - A numeric literal becomes a constant value.
///
Value *NumberExprAST::codegen() {
  if (IsIntLiteral)
    return ConstantInt::get(*TheContext, IntVal);
  if (IsFloatType(getType()))
    return ConstantFP::get(*TheContext, FloatVal);
  return LogErrorV("Unknown numeric literal type");
}

Value *BoolExprAST::codegen() {
  return ConstantInt::get(Type::getInt1Ty(*TheContext), Val ? 1 : 0);
}

/// VariableExprAST::codegen - A variable reference loads the current value
/// from the variable's stack slot.
Value *VariableExprAST::codegen() {
  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second)
    return Builder->CreateLoad(LLVMTypeFor(getType()), It->second,
                               Name.c_str());

  if (auto *GV = GetGlobalVariable(Name))
    return Builder->CreateLoad(LLVMTypeFor(getType()), GV, Name.c_str());

  return LogErrorV("Unknown variable name");
}

/// AssignmentExprAST::codegen - Evaluate the RHS, store it into the variable's
/// stack slot, and produce the assigned value.
Value *AssignmentExprAST::codegen() {
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

/// ReturnExprAST::codegen - Emit a return from the current function.
Value *ReturnExprAST::codegen() {
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

/// BlockExprAST::codegen - Evaluate statements in order.
/// Saves and restores NamedValues to implement block scoping: variables
/// declared inside the block are not visible after it exits.
Value *BlockExprAST::codegen() {
  auto SavedBindings = NamedValues;

  Value *Last = nullptr;
  for (auto &Stmt : Stmts) {
    if (Builder->GetInsertBlock()->getTerminator())
      break;
    Last = Stmt->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      return nullptr;
    }
  }

  NamedValues = SavedBindings;

  if (!Last)
    return LogErrorV("Empty block");

  // Blocks are statement sequences. If control reaches the end without an
  // explicit return, the block's implicit value is always 0.0.
  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// BinaryExprAST::codegen - Recursively codegen both operands, then emit the
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
Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;
  ValueType LTy = LHS->getType();
  ValueType RTy = RHS->getType();

  switch (Op) {
  case '+':
  case '-':
  case '*': {
    L = EmitImplicitCast(L, LTy, getType());
    R = EmitImplicitCast(R, RTy, getType());
    if (!L || !R)
      return LogErrorV("Type mismatch in arithmetic");
    if (IsFloatType(getType())) {
      if (Op == '+')
        return Builder->CreateFAdd(L, R, "addtmp");
      if (Op == '-')
        return Builder->CreateFSub(L, R, "subtmp");
      return Builder->CreateFMul(L, R, "multmp");
    }
    if (Op == '+')
      return Builder->CreateAdd(L, R, "addtmp");
    if (Op == '-')
      return Builder->CreateSub(L, R, "subtmp");
    return Builder->CreateMul(L, R, "multmp");
  }
  case '<':
  case '>':
  case tok_eq:
  case tok_neq:
  case tok_leq:
  case tok_geq: {
    ValueType CompareType = ValueType::Error;
    if (LTy == ValueType::Bool && RTy == ValueType::Bool) {
      if (Op != tok_eq && Op != tok_neq)
        return LogErrorV("Type mismatch in comparison");
      CompareType = ValueType::Bool;
    } else if (IsFloatType(LTy) && IsFloatType(RTy)) {
      if (LTy == RTy)
        CompareType = LTy;
      else if ((LTy == ValueType::Float && RTy == ValueType::Float64) ||
               (LTy == ValueType::Float64 && RTy == ValueType::Float))
        CompareType = ValueType::Float64;
    } else if (IsFloatType(LTy) && IsIntType(RTy)) {
      if (IsAssignable(LTy, RTy))
        CompareType = LTy;
    } else if (IsFloatType(RTy) && IsIntType(LTy)) {
      if (IsAssignable(RTy, LTy))
        CompareType = RTy;
    } else if (IsIntType(LTy) && IsIntType(RTy)) {
      if (IsAssignable(LTy, RTy))
        CompareType = LTy;
      else if (IsAssignable(RTy, LTy))
        CompareType = RTy;
    }

    if (CompareType == ValueType::Error)
      return LogErrorV("Type mismatch in comparison");

    if (CompareType == ValueType::Bool) {
      if (Op == tok_eq)
        return Builder->CreateICmpEQ(L, R, "cmptmp");
      return Builder->CreateICmpNE(L, R, "cmptmp");
    }

    L = EmitImplicitCast(L, LTy, CompareType);
    R = EmitImplicitCast(R, RTy, CompareType);
    if (!L || !R)
      return LogErrorV("Type mismatch in comparison");

    if (IsFloatType(CompareType)) {
      switch (Op) {
      case '<':
        return Builder->CreateFCmpOLT(L, R, "cmptmp");
      case '>':
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
      switch (Op) {
      case '<':
        return Builder->CreateICmpSLT(L, R, "cmptmp");
      case '>':
        return Builder->CreateICmpSGT(L, R, "cmptmp");
      case tok_eq:
        return Builder->CreateICmpEQ(L, R, "cmptmp");
      case tok_neq:
        return Builder->CreateICmpNE(L, R, "cmptmp");
      case tok_leq:
        return Builder->CreateICmpSLE(L, R, "cmptmp");
      case tok_geq:
        return Builder->CreateICmpSGE(L, R, "cmptmp");
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
  Function *F = getFunction(std::string("binary") + (char)Op);
  if (!F)
    return LogErrorV("invalid binary operator");

  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}

/// UnaryExprAST::codegen - Emit built-in unary minus directly, or call a
/// user-defined unary operator function ("unary" + opchar).
Value *UnaryExprAST::codegen() {
  Value *Op = Operand->codegen();
  if (!Op)
    return nullptr;

  // Built-in unary minus.
  if (Opcode == '-') {
    if (IsIntType(getType()))
      return Builder->CreateNeg(Op, "negtmp");
    if (IsFloatType(getType()))
      return Builder->CreateFNeg(Op, "negtmp");
    return LogErrorV("Unary '-' not supported for this type");
  }

  // User-defined unary operator.
  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return Builder->CreateCall(F, Op, "unop");
}

/// CastExprAST::codegen - Emit explicit int/double casts.
Value *CastExprAST::codegen() {
  Value *V = Expr->codegen();
  if (!V)
    return nullptr;
  Value *Cast = EmitCast(V, Expr->getType(), TargetTy);
  if (!Cast)
    return LogErrorV("Invalid cast");
  return Cast;
}

/// CallExprAST::codegen - Look up the callee by name in TheModule, verify the
/// argument count, codegen each argument, then emit a call instruction.
///
/// getFunction searches the module for a declaration or definition with the
/// given name. This covers both previous 'extern' declarations and previously
/// defined functions. The argument count check catches mismatches that a typed
/// language would catch statically.
Value *CallExprAST::codegen() {
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  PrototypeAST *Proto = GetFunctionProto(Callee);
  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    Value *ArgVal = Args[i]->codegen();
    if (!ArgVal)
      return nullptr;
    if (Proto) {
      ArgVal =
          EmitImplicitCast(ArgVal, Args[i]->getType(), Proto->getArgType(i));
      if (!ArgVal)
        return LogErrorV("Argument type mismatch");
    }
    ArgsV.push_back(ArgVal);
  }

  if (getType() == ValueType::None)
    return Builder->CreateCall(CalleeF, ArgsV);
  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/// IfStmtAST::codegen - Emit LLVM IR for a statement-style if.
///
/// If there is no else branch, control falls through to the merge block.
/// The statement evaluates to 0.0.
Value *IfStmtAST::codegen() {
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

/// ForExprAST::codegen - Emit LLVM IR for a for-expression using a mutable
/// stack slot for the loop variable.
Value *ForExprAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  Value *VarPtr = nullptr;
  AllocaInst *Alloca = nullptr;
  AllocaInst *OldVal = nullptr;
  if (IsVarDecl) {
    Alloca = CreateEntryBlockAlloca(TheFunction, VarName, VarType);
    EmitDebugDeclare(Alloca, VarName, CurFunctionLine, false, 0, VarType);
    VarPtr = Alloca;
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
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "after_loop", TheFunction);

  Builder->CreateBr(CondBB);

  Builder->SetInsertPoint(CondBB);

  if (IsVarDecl) {
    OldVal = NamedValues[VarName];
    NamedValues[VarName] = Alloca;
  }

  Value *CondVal = Cond->codegen();
  if (!CondVal)
    return nullptr;
  CondVal = ToBool(CondVal, Cond->getType());
  if (!CondVal)
    return LogErrorV("Invalid loop condition type");
  Builder->CreateCondBr(CondVal, BodyBB, AfterBB);

  Builder->SetInsertPoint(BodyBB);

  if (!Body->codegen())
    return nullptr;

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

/// VarStmtAST::codegen - Allocate mutable local variables and initialize them.
Value *VarStmtAST::codegen() {
  if (InGlobalInit) {
    for (auto &Var : VarNames) {
      const string &VarName = Var.Name;
      ValueType VarTy = Var.Type;
      ExprAST *Init = Var.Init.get();

      auto *GV = TheModule->getNamedGlobal(VarName);
      if (GV && !GV->isDeclaration())
        return LogErrorV("Global variable already defined");
      if (GV && GV->getValueType() != LLVMTypeFor(VarTy))
        return LogErrorV("Global variable type mismatch");

      if (!GV) {
        auto *Type = LLVMTypeFor(VarTy);
        GV = new GlobalVariable(*TheModule, Type, false,
                                GlobalValue::ExternalLinkage,
                                ZeroConstant(VarTy), VarName);
        EmitDebugGlobal(GV, VarName, CurFunctionLine, VarTy);
      } else if (GV->isDeclaration()) {
        GV->setInitializer(ZeroConstant(VarTy));
        GV->setLinkage(GlobalValue::ExternalLinkage);
        EmitDebugGlobal(GV, VarName, CurFunctionLine, VarTy);
      }

      ModuleHasGlobals = true;

      Value *InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
      InitVal = EmitImplicitCast(InitVal, Init->getType(), VarTy);
      if (!InitVal)
        return LogErrorV("Type mismatch in variable initialization");

      Builder->CreateStore(InitVal, GV);
    }

    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.Name;
    ValueType VarTy = Var.Type;
    ExprAST *Init = Var.Init.get();

    Value *InitVal = Init->codegen();
    if (!InitVal)
      return nullptr;
    InitVal = EmitImplicitCast(InitVal, Init->getType(), VarTy);
    if (!InitVal)
      return LogErrorV("Type mismatch in variable initialization");

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName, VarTy);
    Builder->CreateStore(InitVal, Alloca);
    NamedValues[VarName] = Alloca;
    EmitDebugDeclare(Alloca, VarName, CurFunctionLine, false, 0, VarTy);
  }

  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// PrototypeAST::codegen - Create a function declaration in TheModule: name,
/// return type, and parameter types.
///
/// ExternalLinkage makes the function visible outside this module. That is
/// what allows 'extern def sin(x)' to link against the C library's sin at
/// runtime, and what lets 'def foo(...)' be called from later expressions in
/// the same session.
///
/// Arg.setName() is optional — it only affects the printed IR, making output
/// read as 'double %a, double %b' rather than 'double %0, double %1'.
Function *PrototypeAST::codegen() {
  std::vector<Type *> ArgTys;
  ArgTys.reserve(Args.size());
  for (const auto &Arg : Args)
    ArgTys.push_back(LLVMTypeFor(Arg.second));
  FunctionType *FT = FunctionType::get(LLVMTypeFor(ReturnType), ArgTys,
                                       false /* not variadic */);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Name arguments so the printed IR is readable.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++].first);

  // For user-defined binary operators, register the precedence in the global
  // table so the parser knows how tightly the new operator binds.  This happens
  // at JIT time (inside codegen), meaning the operator is immediately usable in
  // subsequent REPL lines or file definitions — exactly what we want.
  if (isBinaryOp())
    BinopPrecedence[getOperatorName()] = Precedence;

  // For user-defined unary operators, register the operator token in the
  // unary registry so later definitions can detect duplicates.
  if (isUnaryOp())
    KnownUnaryOperators.insert(getOperatorName());

  return F;
}

/// FunctionAST::codegen - Generate IR for a complete function definition.
///
/// Four steps:
///
/// 1. Register the prototype. The PrototypeAST is moved into FunctionProtos
///    so that future modules can re-emit a declaration for this function via
///    getFunction(). A reference is kept for the getFunction() call below.
///    getFunction() either finds an existing declaration in the current module
///    (e.g. from a prior 'extern def') or calls Proto->codegen() to create one.
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
Function *FunctionAST::codegen() {
  // Step 1: register the prototype and resolve the Function*.
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);

  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  ValueType SavedRetTy = CurrentFunctionReturnType;
  CurrentFunctionReturnType = P.getReturnType();

  DISubprogram *SP = nullptr;
  if (DIB && TheDIFile) {
    bool IsInternal = P.getName().rfind("__pyxc.", 0) == 0;
    if (!IsInternal) {
      unsigned Line = P.getLocation().Line ? P.getLocation().Line : 1;
      SmallVector<Metadata *, 8> EltTys;
      EltTys.push_back(DITypeFor(P.getReturnType()));
      for (size_t i = 0; i < P.getArgs().size(); ++i)
        EltTys.push_back(DITypeFor(P.getArgType(i)));
      auto *SubTy =
          DIB->createSubroutineType(DIB->getOrCreateTypeArray(EltTys));
      SP = DIB->createFunction(TheDIFile, P.getName(), StringRef(), TheDIFile,
                               Line, SubTy, Line, DINode::FlagZero,
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
  unsigned ArgIndex = 1;
  size_t ArgTypeIndex = 0;
  for (auto &Arg : TheFunction->args()) {
    ValueType ArgTy = P.getArgType(ArgTypeIndex++);
    AllocaInst *Alloca =
        CreateEntryBlockAlloca(TheFunction, std::string(Arg.getName()), ArgTy);
    Builder->CreateStore(&Arg, Alloca);
    NamedValues[std::string(Arg.getName())] = Alloca;
    EmitDebugDeclare(Alloca, Arg.getName(), CurFunctionLine, true, ArgIndex++,
                     ArgTy);
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
          CurrentFunctionReturnType = SavedRetTy;
          return nullptr;
        }
      }
    }
    verifyFunction(*TheFunction);

    // Run the optimisation pipeline: InstCombine, Reassociate, GVN,
    // SimplifyCFG.
    TheFPM->run(*TheFunction, *TheFAM);
    CurDIScope = nullptr;
    CurrentFunctionReturnType = SavedRetTy;
    return TheFunction;
  }

  // Body codegen failed — remove the incomplete function so it cannot be
  // called and does not pollute the module handed to the JIT.
  TheFunction->eraseFromParent();
  CurDIScope = nullptr;
  CurrentFunctionReturnType = SavedRetTy;
  return nullptr;
}

//===----------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------===//

static vector<unique_ptr<ExprAST>> FileTopLevelStmts;

/// ResetParserStateForFile - Clear parser/compiler state between input files.
///
/// Multi-file compilation emits each source into its own module, so symbols,
/// globals, and top-level statements should not leak across files.
static void ResetParserStateForFile() {
  FunctionProtos.clear();
  GlobalVarTypes.clear();
  GlobalVarDecls.clear();
  VarScopes.clear();
  FileTopLevelStmts.clear();
  LastTopLevelEndedWithBlock = false;
  LastTopLevelShouldPrint = true;
  InGlobalInit = false;
  ModuleHasGlobals = false;
  HadError = false;
  ResetBinopPrecedence();
  ResetKnownUnaryOperators();
}

/// InitializeModuleAndManagers - Create a fresh module, IR builder, and
/// optimisation pipeline.
///
/// Called once at startup and again after every top-level input that hands
/// its module to the JIT. Because the JIT takes ownership of TheModule via
/// ThreadSafeModule, we cannot keep emitting into the old module — a new one
/// must be created for every subsequent definition or expression.
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
  while (CurTok != tok_eol && CurTok != tok_eof && CurTok != tok_dedent)
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
/// CurTok is on 'binary' or 'unary'. Delegates to ParseDecoratedDef.
static void HandleDecorator() {
  auto FnAST = ParseDecoratedDef();
  bool HasTrailing = (CurTok != tok_eol && CurTok != tok_eof);
  if (!FnAST || (HasTrailing && !LastTopLevelEndedWithBlock)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
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

/// HandleDefinition - Parse, optimise, and JIT-compile a 'def' definition.
///
/// On success: codegen + optimise the function (TheFPM runs inside
/// FunctionAST::codegen), print the optimised IR, then hand the entire module
/// to the JIT via addModule. The JIT takes ownership of TheModule and
/// TheContext, so InitializeModuleAndManagers() is called immediately after to
/// create a fresh module for the next input. The compiled function remains
/// accessible in the JIT's symbol table for the rest of the session.
/// On parse failure or unexpected trailing tokens: discard the line.
static void HandleDefinition() {
  auto FnAST = ParseDefinition();
  bool HasTrailing = (CurTok != tok_eol && CurTok != tok_eof);
  if (!FnAST || (HasTrailing && !LastTopLevelEndedWithBlock)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
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
/// On success: codegen the prototype (emits a 'declare' in the current module),
/// print it, then save the PrototypeAST into FunctionProtos. Saving into
/// FunctionProtos is the critical step — when this module is handed to the JIT
/// and a new one is created, getFunction() uses FunctionProtos to re-emit the
/// 'declare' in whichever module needs to call the extern.
/// On parse failure or unexpected trailing tokens: discard the line.
static void HandleExtern() {
  auto ProtoAST = ParseExtern();

  if (!ProtoAST)
    return;

  // Reject conflicting redeclarations: in Pyxc, function identity is just
  // name + arity. We validate types separately in the parser.
  auto Existing = FunctionProtos.find(ProtoAST->getName());
  if (Existing != FunctionProtos.end() &&
      Existing->second->getNumArgs() != ProtoAST->getNumArgs()) {
    LogError((string("Conflicting extern declaration for '") +
              ProtoAST->getName() + "'")
                 .c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (CurTok != tok_eol && CurTok != tok_eof) {
    if (CurTok)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }

  if (auto *FnIR = ProtoAST->codegen()) {
    Log("Parsed an extern.\n");
    if (ShouldDumpIR())
      FnIR->print(errs());
    // Save the prototype so getFunction() can re-emit it in future modules.
    FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
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
  auto FnAST = ParseTopLevelExpr();
  bool HasTrailing = (CurTok != tok_eol && CurTok != tok_eof);
  if (!FnAST || (HasTrailing && !LastTopLevelEndedWithBlock)) {
    if (FnAST)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
    SynchronizeToLineBoundary();
    return;
  }
  string FnName = FnAST->getName();
  ValueType RetTy = FnAST->getReturnType();
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

      if (RetTy == ValueType::None) {
        void (*FP)() = ExprSymbol.toPtr<void (*)()>();
        FP();
      } else {
        switch (RetTy) {
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
    if (RetTy == ValueType::None) {
      void (*FP)() = ExprSymbol.toPtr<void (*)()>();
      FP();
    } else {
      switch (RetTy) {
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
  bool HasTrailing = (CurTok != tok_eol && CurTok != tok_eof);
  if (!Stmt || (HasTrailing && !LastTopLevelEndedWithBlock)) {
    if (Stmt)
      LogError(("Unexpected " + FormatTokenForMessage(CurTok)).c_str());
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
/// top             = definition | external | toplevelstmt ;
///
/// Dispatches on the leading token of each top-level form:
///   tok_def    → HandleDefinition   (definition)
///   tok_extern → HandleExtern       (external)
///   '@'        → HandleDecorator    (decorateddef: @binary / @unary)
///   tok_eol    → skip blank line
///   anything else → HandleTopLevelExpression (toplevelstmt)
///
/// CurTok is primed before MainLoop() is called (see main()). After each
/// successful parse the handler prints a confirmation; after a failed parse
/// the handler calls SynchronizeToLineBoundary() to discard all remaining
/// tokens on the current line. Either way we return here to look at the
/// next CurTok.
static void MainLoop() {
  while (true) {
    if (CurTok == tok_eof)
      return;

    // A bare newline: just print a fresh prompt and read the next token.
    if (CurTok == tok_eol) {
      PrintReplPrompt();
      getNextToken();
      continue;
    }

    if (CurTok == tok_indent) {
      LogError("Unexpected indentation");
      SynchronizeToLineBoundary();
      continue;
    }

    // Stray dedent at top level (can occur in REPL mode): skip it.
    if (CurTok == tok_dedent) {
      getNextToken();
      continue;
    }

    if (CurTok == tok_error) {
      SynchronizeToLineBoundary();
      continue;
    }

    switch (CurTok) {
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    case '@':
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
    if (CurTok == tok_eof)
      return;

    if (CurTok == tok_eol) {
      getNextToken();
      continue;
    }

    if (CurTok == tok_indent) {
      LogError("Unexpected indentation");
      SynchronizeToLineBoundary();
      continue;
    }

    if (CurTok == tok_dedent) {
      getNextToken();
      continue;
    }

    if (CurTok == tok_error) {
      SynchronizeToLineBoundary();
      continue;
    }

    switch (CurTok) {
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    case '@':
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
    auto Block = make_unique<BlockExprAST>(std::move(FileTopLevelStmts));
    auto Proto = make_unique<PrototypeAST>(
        "__pyxc.global_init", vector<pair<string, ValueType>>(),
        SourceLocation{1, 1}, ValueType::None);
    auto FnAST = make_unique<FunctionAST>(std::move(Proto), std::move(Block));

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

  auto MainIt = FunctionProtos.find("main");
  if (MainIt == FunctionProtos.end())
    return;

  if (MainIt->second->getNumArgs() != 0) {
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
    MainFn();
  } else {
    void (*MainFn)() = MainSymbol.toPtr<void (*)()>();
    MainFn();
  }
}

/// AddGlobalCtor - Register a function to run before main() via
/// llvm.global_ctors.
static void AddGlobalCtor(Function *Fn, int Priority = 65535) {
  auto *Int32Ty = Type::getInt32Ty(*TheContext);
  auto *VoidPtrTy = PointerType::get(*TheContext, 0);
  auto *StructTy = StructType::get(Int32Ty, Fn->getType(), VoidPtrTy);

  Constant *CtorEntry = ConstantStruct::get(
      StructTy, ConstantInt::get(Int32Ty, Priority), Fn,
      ConstantPointerNull::get(cast<PointerType>(VoidPtrTy)));

  GlobalVariable *GV = TheModule->getGlobalVariable("llvm.global_ctors");
  if (GV)
    return;

  ArrayType *AT = ArrayType::get(StructTy, 1);
  auto *Init = ConstantArray::get(AT, {CtorEntry});
  new GlobalVariable(*TheModule, AT, false, GlobalValue::AppendingLinkage, Init,
                     "llvm.global_ctors");
}

/// CreateTargetMachine - Build a TargetMachine for the host triple.
static std::unique_ptr<TargetMachine> CreateTargetMachine() {
  string TargetTriple = sys::getDefaultTargetTriple();
  Triple TT(TargetTriple);

  string Error;
  const Target *Target = TargetRegistry::lookupTarget(TargetTriple, Error);
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

  auto *DoubleTy = Type::getDoubleTy(Ctx);
  auto *Int32Ty = Type::getInt32Ty(Ctx);
  auto *CharPtrTy = PointerType::get(Ctx, 0);

  FunctionType *PrintfTy = FunctionType::get(Int32Ty, {CharPtrTy}, true);
  Function *Printf =
      Function::Create(PrintfTy, Function::ExternalLinkage, "printf", M.get());

  FunctionType *PutcharTy = FunctionType::get(Int32Ty, {Int32Ty}, false);
  Function *Putchar = Function::Create(PutcharTy, Function::ExternalLinkage,
                                       "putchar", M.get());

  FunctionType *PrintdTy = FunctionType::get(DoubleTy, {DoubleTy}, false);
  Function *Printd =
      Function::Create(PrintdTy, Function::ExternalLinkage, "printd", M.get());
  {
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Printd);
    IRBuilder<> B(BB);
    auto *FmtGV = B.CreateGlobalString("%f\n", "fmt");
    Value *Zero = ConstantInt::get(Int32Ty, 0);
    Value *Fmt = B.CreateInBoundsGEP(FmtGV->getValueType(), FmtGV, {Zero, Zero},
                                     "fmt_ptr");
    Value *Arg = Printd->getArg(0);
    B.CreateCall(Printf, {Fmt, Arg});
    B.CreateRet(ConstantFP::get(Ctx, APFloat(0.0)));
  }

  FunctionType *PutchardTy = FunctionType::get(DoubleTy, {DoubleTy}, false);
  Function *Putchard = Function::Create(PutchardTy, Function::ExternalLinkage,
                                        "putchard", M.get());
  {
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Putchard);
    IRBuilder<> B(BB);
    Value *Arg = Putchard->getArg(0);
    Value *Ch = B.CreateFPToUI(Arg, Int32Ty, "ch");
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
    *HasMain = FunctionProtos.find("main") != FunctionProtos.end();

  if (!PrepareFileModeModule())
    return false;

  RunModuleOptimizations(TheModule.get());
  return EmitModuleToFile(TheModule.get(), EmitKind::OBJ, ObjPath);
}

static string FindMacOSSDKRoot() {
  if (const char *EnvSDK = getenv("SDKROOT"))
    return string(EnvSDK);

  const char *XcodeSDK = "/Applications/Xcode.app/Contents/Developer/Platforms/"
                         "MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  if (sys::fs::exists(XcodeSDK))
    return string(XcodeSDK);

  const char *CLTSDK = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
  if (sys::fs::exists(CLTSDK))
    return string(CLTSDK);

  return "";
}

static string DefaultMacOSVersion(const Triple &TT) {
  VersionTuple Ver = TT.getOSVersion();
  if (Ver.getMajor()) {
    std::ostringstream OS;
    OS << Ver.getMajor() << "." << Ver.getMinor().value_or(0);
    if (Ver.getSubminor().value_or(0) != 0)
      OS << "." << Ver.getSubminor().value();
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

  std::vector<StringRef> Args;
  Args.push_back(*Dsymutil);
  Args.push_back(ExePath);
  if (sys::ExecuteAndWait(*Dsymutil, Args)) {
    fprintf(stderr, "Warning: dsymutil failed; debug info may be missing\n");
  }
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
      string OSVer = DefaultMacOSVersion(TT);
      PushArg("-platform_version");
      PushArg("macos");
      PushArg(OSVer);
      PushArg(OSVer);

      string Crt1 = SDKRoot + "/usr/lib/crt1.o";
      string Crti = SDKRoot + "/usr/lib/crti.o";
      string Crtn = SDKRoot + "/usr/lib/crtn.o";
      if (sys::fs::exists(Crt1))
        PushArg(Crt1);
      if (sys::fs::exists(Crti))
        PushArg(Crti);
      for (const auto &Input : Inputs)
        PushArg(Input);
      if (sys::fs::exists(Crtn)) {
        PushArg("-lSystem");
        PushArg(Crtn);
      } else {
        PushArg("-lSystem");
      }
    } else {
      for (const auto &Input : Inputs)
        PushArg(Input);
      PushArg("-lSystem");
    }

    vector<const char *> Args;
    Args.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Args.push_back(Arg.c_str());
    return lld::macho::link(Args, llvm::outs(), llvm::errs(), false, false);
  }

  if (TT.isOSLinux()) {
    PushArg("ld.lld");
    PushArg("-o");
    PushArg(OutputPath);
    for (const auto &Input : Inputs)
      PushArg(Input);
    PushArg("-lc");
    PushArg("-lm");
    vector<const char *> Args;
    Args.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Args.push_back(Arg.c_str());
    return lld::elf::link(Args, llvm::outs(), llvm::errs(), false, false);
  }

  if (TT.isOSWindows()) {
    PushArg("lld-link");
    PushArg("/OUT:" + OutputPath);
    for (const auto &Input : Inputs)
      PushArg(Input);
    vector<const char *> Args;
    Args.reserve(ArgStorage.size());
    for (auto &Arg : ArgStorage)
      Args.push_back(Arg.c_str());
    return lld::coff::link(Args, llvm::outs(), llvm::errs(), false, false);
  }

  fprintf(stderr, "Error: unsupported target for --emit exe\n");
  return false;
}

/// PrepareFileModeModule - Build __pyxc.global_init and main wrapper.
///
/// Returns false on error (e.g., invalid main signature).
static bool PrepareFileModeModule() {
  if (!FileTopLevelStmts.empty()) {
    auto Block = make_unique<BlockExprAST>(std::move(FileTopLevelStmts));
    auto Proto = make_unique<PrototypeAST>(
        "__pyxc.global_init", vector<pair<string, ValueType>>(),
        SourceLocation{1, 1}, ValueType::None);
    auto FnAST = make_unique<FunctionAST>(std::move(Proto), std::move(Block));

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

  auto MainIt = FunctionProtos.find("main");
  if (MainIt != FunctionProtos.end() && MainIt->second->getNumArgs() != 0) {
    fprintf(stderr, "Error: main() must take no arguments\n");
    HadError = true;
    return false;
  }

  if (MainIt != FunctionProtos.end()) {
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
    if (UserMain->getReturnType()->isIntegerTy(32)) {
      Value *Ret = TmpB.CreateCall(UserMain);
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
  // together register the native target's instruction set, assembler, and
  // disassembler so the JIT can compile and link for the current CPU.
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

#include "../include/PyxcJIT.h"
#include "lld/Common/Driver.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/BasicBlock.h"
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
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include <cctype>
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
  tok_name = -6,
  tok_number = -7,

  // comparison operators
  tok_eq = -8,   // ==
  tok_neq = -9,  // !=
  tok_leq = -10, // <=
  tok_geq = -11, // >=

  // control
  tok_if = -12,
  tok_else = -13,
  tok_return = -14,

  // loops
  tok_for = -15,

  // user-defined operators
  tok_binary = -16,
  tok_unary = -17,

  // mutable variables
  tok_var = -18,

  // indentation
  tok_indent = -19,
  tok_dedent = -20,
  tok_block_end = -100, // synthetic: injected by ParseBlock after eating DEDENT
};

static string Name; // Filled in if tok_name
static double NumberValue;        // Filled in if tok_number
static string NumberLiteral; // Filled in if tok_number
static int LexerLastChar = ' ';
static vector<int> IndentStack = {0};
static deque<int> PendingTokens;
static bool AtLineStart = true;
static constexpr int IndentTabWidth = 8;

// Keywords like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static map<string, Token> Keywords = {
    {"def", tok_def},       {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},         {"else", tok_else},     {"for", tok_for},
    {"binary", tok_binary}, {"unary", tok_unary},   {"var", tok_var}};

// Debug-only token names. Kept separate from Keywords because this map is
// purely for printing token stream output.
static map<int, string> TokenNames = [] {
  // Unprintable character tokens, and multi-character tokens.
  static map<int, string> Names = {
      {tok_eof, "end of input"}, {tok_eol, "newline"},
      {tok_error, "error"},      {tok_def, "'def'"},
      {tok_extern, "'extern'"},  {tok_name, "name"},
      {tok_number, "number"},    {tok_return, "'return'"},
      {tok_eq, "'=='"},          {tok_neq, "'!='"},
      {tok_leq, "'<='"},         {tok_geq, "'>='"},
      {tok_if, "'if'"},          {tok_else, "'else'"},
      {tok_for, "'for'"},        {tok_binary, "'binary'"},
      {tok_unary, "'unary'"},    {tok_var, "'var'"},
      {tok_indent, "indent"},    {tok_dedent, "dedent"},
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
      LexerLastChar = ' ';
      return tok_eol;
    }

    // Comment-only line: consume and return a newline.
    if (LexerLastChar == '#') {
      do
        LexerLastChar = advance();
      while (LexerLastChar != EOF && LexerLastChar != '\n');
      if (LexerLastChar != EOF) {
        CurLoc = LexLoc;
        LexerLastChar = ' ';
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
      while (IndentStack.size() > 1 && CurrentIndentRead < IndentStack.back()) {
        IndentStack.pop_back();
        PendingTokens.push_back(tok_dedent);
      }
      if (CurrentIndentRead != IndentStack.back()) {
        LogErrorAtLoc("inconsistent indentation", CurLoc);
        PrintErrorSourceContext(CurLoc);
        return tok_error;
      }
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

  if (isdigit(LexerLastChar) || LexerLastChar == '.') {
    string NumStr;
    do {
      NumStr += LexerLastChar;
      LexerLastChar = advance();
    } while (isdigit(LexerLastChar) || LexerLastChar == '.');

    NumberLiteral = NumStr;
    char *End = nullptr;
    NumberValue = strtod(NumStr.c_str(), &End);
    if (End == NumStr.c_str() /* no conversion */
        || *End != '\0' /* trailing unparsed characters */) {
      LogInvalidNumberLiteralAtLoc(NumStr, CurLoc);
      return tok_error;
    }
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

  // Single character token
  int ThisChar = LexerLastChar;

  // Position the lexer at the next character so the next getToken() starts there.
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
public:
  virtual ~ExpressionNode() = default;
  // getLValueName - If this node is a plain assignable variable, return its
  // name; otherwise return nullptr.
  virtual const string *getLValueName() const { return nullptr; }
  // isReturnExpr - True iff this node is a return statement.
  virtual bool isReturnExpr() const { return false; }
  // shouldPrintValue - Whether the REPL should print the value of this node
  // when it appears as a top-level form.
  virtual bool shouldPrintValue() const { return true; }
  virtual Value *codegen() = 0;
};

/// NumberExpressionNode - Expression class for numeric literals like "1.0".
class NumberExpressionNode : public ExpressionNode {
  double Value;

public:
  NumberExpressionNode(double Value) : Value(Value) {}
  llvm::Value *codegen() override;
};

/// NameExpressionNode - Expression class for referencing a variable, like "a".
class NameExpressionNode : public ExpressionNode {
  string Name;

public:
  NameExpressionNode(const string &Name) : Name(Name) {}
  // convenience function
  const string &getName() const { return Name; }
  const string *getLValueName() const override { return &Name; }
  Value *codegen() override;
};

/// AssignmentExpressionNode - Expression class for assignment to an existing variable.
/// The expression stores Right into the named variable and produces the assigned
/// value.
class AssignmentExpressionNode : public ExpressionNode {
  string Name;
  unique_ptr<ExpressionNode> Expr;

public:
  AssignmentExpressionNode(const string &Name, unique_ptr<ExpressionNode> Expr)
      : Name(Name), Expr(std::move(Expr)) {}
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// ReturnExpressionNode - Statement-like expression for return.
/// Emits a function return and produces the returned value.
class ReturnExpressionNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Expr;

public:
  ReturnExpressionNode(unique_ptr<ExpressionNode> Expr) : Expr(std::move(Expr)) {}
  bool isReturnExpr() const override { return true; }
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// BlockExpressionNode - A sequence of statements evaluated in order.
/// The block's value is the value of the last statement executed.
class BlockExpressionNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Stmts;

public:
  BlockExpressionNode(vector<unique_ptr<ExpressionNode>> Stmts) : Stmts(std::move(Stmts)) {}
  Value *codegen() override;
};

/// BinaryExpressionNode - Expression class for a binary operator.
/// Operator is an int (not char) to accommodate both single-character ASCII operators
/// like '+' and named multi-character token enums like tok_eq (==).
class BinaryExpressionNode : public ExpressionNode {
  int Operator;
  unique_ptr<ExpressionNode> Left, Right;

public:
  BinaryExpressionNode(int Operator, unique_ptr<ExpressionNode> Left, unique_ptr<ExpressionNode> Right)
      : Operator(Operator), Left(std::move(Left)), Right(std::move(Right)) {}
  Value *codegen() override;
};

/// CallExpressionNode - Expression class for function calls.
class CallExpressionNode : public ExpressionNode {
  string Callee;
  vector<unique_ptr<ExpressionNode>> Arguments;

public:
  CallExpressionNode(const string &Callee, vector<unique_ptr<ExpressionNode>> Arguments)
      : Callee(Callee), Arguments(std::move(Arguments)) {}
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
  unique_ptr<ExpressionNode> Start, Cond, Step, Body;

public:
  ForExpressionNode(const string &VarName, bool IsVarDecl, unique_ptr<ExpressionNode> Start,
             unique_ptr<ExpressionNode> Cond, unique_ptr<ExpressionNode> Step,
             unique_ptr<ExpressionNode> Body)
      : VarName(VarName), IsVarDecl(IsVarDecl), Start(std::move(Start)),
        Cond(std::move(Cond)), Step(std::move(Step)), Body(std::move(Body)) {}
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
  UnaryExpressionNode(char Opcode, unique_ptr<ExpressionNode> Operand)
      : Opcode(Opcode), Operand(std::move(Operand)) {}
  Value *codegen() override;
};

/// IfStatementNode - Statement form of if/else.
/// Produces 0.0 and does not return a value.
class IfStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Cond, Then, Else;

public:
  IfStatementNode(unique_ptr<ExpressionNode> Cond, unique_ptr<ExpressionNode> Then,
            unique_ptr<ExpressionNode> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
  bool shouldPrintValue() const override { return false; }
  Value *codegen() override;
};

/// VarStatementNode - Statement form of mutable local variable bindings.
///   var a = <init>, b = <init>
/// Each binding allocates stack storage in the current function's entry block
/// and stores its initializer. Bindings persist for the rest of the function.
class VarStatementNode : public ExpressionNode {
  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;

public:
  VarStatementNode(vector<pair<string, unique_ptr<ExpressionNode>>> VarNames)
      : VarNames(std::move(VarNames)) {}
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
  string Name;
  vector<string> Parameters;
  bool IsOperator;
  unsigned Precedence; // binary operators only
  SourceLocation Loc;

public:
  FunctionSignatureNode(const string &Name, vector<string> Parameters, SourceLocation Loc,
               bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), Parameters(std::move(Parameters)), IsOperator(IsOperator),
        Precedence(Prec), Loc(Loc) {}

  const string &getName() const { return Name; }
  const vector<string> &getParameters() const { return Parameters; }
  size_t getNumParameters() const { return Parameters.size(); }
  SourceLocation getLocation() const { return Loc; }

  bool isUnaryOp() const { return IsOperator && Parameters.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Parameters.size() == 2; }

  // The operator character is the last character of the encoded name.
  // e.g. "binary+" -> '+', "unary!" -> '!'
  char getOperatorName() const {
    assert((isUnaryOp() || isBinaryOp()) && "Not an operator function signature");
    return Name.back();
  }

  unsigned getBinaryPrecedence() const { return Precedence; }

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
static map<int, int> OperatorPrecedence = DefaultOperatorPrecedence;

static void ResetOperatorPrecedence() { OperatorPrecedence = DefaultOperatorPrecedence; }

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

// FunctionSignatures - Persistent function signature registry used by the parser to detect
// redefinition of operators. Also used by codegen to re-emit declarations into
// fresh modules. Declared here so parser functions can access it.
static std::map<std::string, std::unique_ptr<FunctionSignatureNode>> FunctionSignatures;

// Parse-time variable tracking for assignments.
// Scopes are stacked: function scope plus nested block scopes.
// for-loop variables are scoped to the loop body only.
static vector<set<string>> VarScopes;
// Global variables declared at top level (persist across modules).
static set<string> GlobalVarNames;
// True while parsing a top-level statement (var binds globals, not locals).
static bool ParsingTopLevel = false;

struct TopLevelParseGuard {
  TopLevelParseGuard() { ParsingTopLevel = true; }
  ~TopLevelParseGuard() { ParsingTopLevel = false; }
};

static void BeginFunctionScope(const vector<string> &Parameters) {
  VarScopes.clear();
  VarScopes.emplace_back();
  for (const auto &Parameter : Parameters)
    VarScopes.front().insert(Parameter);
}

static void EndFunctionScope() { VarScopes.clear(); }

static void DeclareVar(const string &Name) {
  if (VarScopes.empty())
    return;
  VarScopes.back().insert(Name);
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
static void BeginLoopScope(const string &Name) {
  VarScopes.emplace_back();
  VarScopes.back().insert(Name);
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
  FunctionScopeGuard(const vector<string> &Parameters) { BeginFunctionScope(Parameters); }
  ~FunctionScopeGuard() { EndFunctionScope(); }
};

struct BlockScopeGuard {
  BlockScopeGuard() { BeginBlockScope(); }
  ~BlockScopeGuard() { EndBlockScope(); }
};

struct LoopScopeGuard {
  LoopScopeGuard(const string &Name) { BeginLoopScope(Name); }
  ~LoopScopeGuard() { EndLoopScope(); }
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
  return GlobalVarNames.count(Name) > 0;
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
static unique_ptr<ExpressionNode> ParseVarStatement();
static unique_ptr<ExpressionNode> ParseStatement();
static unique_ptr<ExpressionNode> ParseSimpleStatement();
static unique_ptr<ExpressionNode> ParseBlock();
static unique_ptr<ExpressionNode> ParseFunctionBody();


static unsigned TopLevelExprCounter = 0;
static bool LastTopLevelShouldPrint = true;
static unique_ptr<ExpressionNode> ParseSuite();

/// number-expression
///   = number ;
static unique_ptr<ExpressionNode> ParseNumberExpression() {
  auto Result = make_unique<NumberExpressionNode>(NumberValue);
  getNextToken(); // consume the number
  return std::move(Result);
}

/// parenthesized-expression
///   = "(" expression ")" ;
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != ')')
    return LogError("expected ')'");
  getNextToken(); // eat ).
  return V;
}

/// name-expression
///   = name
///   | name "("[expression{"," expression}]")" ;
static unique_ptr<ExpressionNode> ParseNameExpressionWithName(const string &ParsedName) {
  if (CurrentToken != '(') // Simple variable ref.
    return make_unique<NameExpressionNode>(ParsedName);

  // Call.
  getNextToken(); // eat (
  vector<unique_ptr<ExpressionNode>> Arguments;
  if (CurrentToken != ')') {
    while (true) {
      if (auto Arg = ParseExpression())
        Arguments.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurrentToken == ')')
        break;

      if (CurrentToken != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments));
}

static unique_ptr<ExpressionNode> ParseNameExpression() {
  string ParsedName = Name;

  getNextToken(); // eat name.

  return ParseNameExpressionWithName(ParsedName);
}

static bool ParseForParts(unique_ptr<ExpressionNode> &Start, unique_ptr<ExpressionNode> &Cond,
                          unique_ptr<ExpressionNode> &Step, unique_ptr<ExpressionNode> &Body) {
  if (CurrentToken != '=')
    return LogError("Expected '=' after for variable"), false;
  getNextToken(); // eat '='

  Start = ParseExpression();
  if (!Start)
    return false;

  if (CurrentToken != ',')
    return LogError("Expected ',' after for start value"), false;
  getNextToken(); // eat ','

  Cond = ParseExpression();
  if (!Cond)
    return false;

  if (CurrentToken != ',')
    return LogError("Expected ',' after for condition"), false;
  getNextToken(); // eat ','

  Step = ParseExpression();
  if (!Step)
    return false;

  if (CurrentToken != ':')
    return LogError("Expected ':' after for step"), false;
  getNextToken(); // eat ':'

  // Parse the suite after ':' (inline statement or indented block).
  Body = ParseSuite();
  if (!Body)
    return false;

  return true;
}

/// forstmt
///   = "for" [ "var" ] name "=" expression "," expression "," expression
///     ":" suite ;
///
/// The loop variable is introduced by the "for" and is in scope for the
/// condition, step, and body. It shadows any outer variable of the same name.
static unique_ptr<ExpressionNode> ParseForStatement() {
  getNextToken(); // eat 'for'

  bool IsVarDecl = false;
  if (CurrentToken == tok_var)
    IsVarDecl = true, getNextToken(); // optional 'var'

  if (CurrentToken != tok_name)
    return LogError("Expected name after 'for'");
  string VarName = Name;
  getNextToken(); // eat name

  if (IsVarDecl) {
    if (IsDeclaredInCurrentScope(VarName))
      return LogError(
          ("Variable '" + VarName + "' already declared in this scope")
              .c_str());
  } else if (!IsDeclaredVar(VarName)) {
    return LogError("Assignment to undeclared variable");
  }

  unique_ptr<ExpressionNode> Start, Cond, Step, Body;

  unique_ptr<LoopScopeGuard> LoopScope;
  if (IsVarDecl)
    LoopScope = make_unique<LoopScopeGuard>(VarName);

  if (!ParseForParts(Start, Cond, Step, Body))
    return nullptr;
  return make_unique<ForExpressionNode>(VarName, IsVarDecl, std::move(Start),
                                 std::move(Cond), std::move(Step),
                                 std::move(Body));
}

/// varstmt
///   = "var" varbinding { "," varbinding } ;
///
/// varbinding
///   = name [ "=" expression ] ;
static unique_ptr<ExpressionNode> ParseVarStatement() {
  getNextToken(); // eat 'var'

  vector<pair<string, unique_ptr<ExpressionNode>>> VarNames;
  bool IsGlobalDecl = ParsingTopLevel;

  while (true) {
    if (CurrentToken != tok_name)
      return LogError("Expected name after 'var'");

    string ParsedName = Name;
    getNextToken(); // eat name

    if (IsGlobalDecl) {
      if (GlobalVarNames.count(ParsedName))
        return LogError(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    } else {
      if (IsDeclaredInCurrentScope(ParsedName))
        return LogError(
            ("Variable '" + ParsedName + "' already declared in this scope").c_str());
    }

    unique_ptr<ExpressionNode> Init;
    if (CurrentToken == '=') {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init)
        return nullptr;
    } else {
      Init = make_unique<NumberExpressionNode>(0.0);
    }

    VarNames.push_back({ParsedName, std::move(Init)});
    if (IsGlobalDecl)
      GlobalVarNames.insert(ParsedName);
    else
      DeclareVar(ParsedName);

    if (CurrentToken != ',')
      break;
    getNextToken(); // eat ','
  }

  return make_unique<VarStatementNode>(std::move(VarNames));
}

/// ifstmt
///   = "if" expression ":" suite [ end-of-lines "else" ":" suite ] ;
static unique_ptr<ExpressionNode> ParseIfStatement() {
  getNextToken(); // eat 'if'
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurrentToken != ':')
    return LogError("Expected ':' after if condition");
  getNextToken(); // eat ':'

  unique_ptr<ExpressionNode> Then = ParseSuite();
  if (!Then)
    return nullptr;

  bool ThenWasBlock = (CurrentToken == tok_block_end);
  if (ThenWasBlock)
    getNextToken();

  // Allow 'else' on next line.
  consumeNewlines();

  unique_ptr<ExpressionNode> Else;
  if (CurrentToken == tok_else) {
    getNextToken(); // eat 'else'
    if (CurrentToken != ':')
      return LogError("Expected ':' after else");
    getNextToken(); // eat ':'
    Else = ParseSuite();
    if (!Else)
      return nullptr;
  } else if (ThenWasBlock) {
    // No else: restore the synthetic separator for the enclosing block/top level.
    PendingTokens.push_front(CurrentToken);
    CurrentToken = tok_block_end;
  }

  return make_unique<IfStatementNode>(std::move(Cond), std::move(Then),
                                std::move(Else));
}

static unique_ptr<ExpressionNode>
ParseUnary(); // forward declaration for ParseUnaryMinus

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
  return make_unique<UnaryExpressionNode>('-', std::move(Operand));
}

/// primary
///   = name-expression
///   | number-expression
///   | parenthesized-expression ;
static unique_ptr<ExpressionNode> ParsePrimary() {
  switch (CurrentToken) {
  default:
    return LogError("unknown token when expecting an expression");
  case tok_name:
    return ParseNameExpression();
  case tok_number:
    return ParseNumberExpression();
  case '(':
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
  // Primary starters will be handled with ParsePrimary.
  if (!isascii(CurrentToken) /* multi-character tokens */ || CurrentToken == '(' ||
      isalpha(CurrentToken) || isdigit(CurrentToken))
    return ParsePrimary();

  // Built-in unary minus.
  if (CurrentToken == '-')
    return ParseUnaryMinus();

  // It's an ASCII punctuation character — treat it as a user-defined unary op.
  int Opc = CurrentToken;
  getNextToken(); // eat the operator character
  if (auto Operand = ParseUnary())
    return make_unique<UnaryExpressionNode>(Opc, std::move(Operand));
  return nullptr;
}

/// binary-operator-right
///   = { binary-operator unaryexpr } ;
static unique_ptr<ExpressionNode> ParseBinaryOperatorRight(int ExpressionPrecedence,
                                         unique_ptr<ExpressionNode> Left) {
  // If this is a binary operator, find its precedence.
  while (true) {
    int TokenPrecedence = GetTokenPrecedence();

    // If this is a binary operator that binds at least as tightly as the current binary operator,
    // consume it, otherwise we are done.
    if (TokenPrecedence < ExpressionPrecedence)
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

    // Merge Left/Right.
    Left = make_unique<BinaryExpressionNode>(Operator, std::move(Left), std::move(Right));
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
///   = "return" expression ;
static unique_ptr<ExpressionNode> ParseReturnStatement() {
  getNextToken(); // eat 'return'
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  return make_unique<ReturnExpressionNode>(std::move(Expr));
}

static unique_ptr<ExpressionNode> ParseAssignmentRight(const string &Name) {
  if (!IsDeclaredVar(Name))
    return LogError("Assignment to undeclared variable");
  getNextToken(); // eat '='

  auto Right = ParseExpression();
  if (!Right)
    return nullptr;
  return make_unique<AssignmentExpressionNode>(Name, std::move(Right));
}


// Parse name-led forms in simplestmt:
//   assignstmt   : name "=" expression
//   expression   : name ...
// and reject trailing '=' when the parsed Left is not assignable.
static unique_ptr<ExpressionNode> ParseLeadingNameSimpleStatement() {
  string ParsedName = Name;
  getNextToken(); // eat name

  // Fast path for assignstmt: x = ...
  if (CurrentToken == '=')
    return ParseAssignmentRight(ParsedName);

  // Otherwise parse as expression starting from name.
  auto Expr = ParseNameExpressionWithName(std::move(ParsedName));
  if (!Expr)
    return nullptr;
  Expr = ParseBinaryOperatorRight(0, std::move(Expr));
  if (!Expr)
    return nullptr;

  // Optional assignment tail: (<expr>) = ...
  if (CurrentToken != '=')
    return Expr;

  const string *AssignedName = Expr->getLValueName();
  if (!AssignedName)
    return LogError("Destination of '=' must be a variable");

  return ParseAssignmentRight(*AssignedName);
}

// Parse non-name-leading expression forms for simplestmt and reject a
// trailing '=' so assignment diagnostics stay local and specific.
static unique_ptr<ExpressionNode> ParseNonLeadingNameSimpleStatement() {
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;

  if (CurrentToken != '=')
    return Expr;

  return LogError("Destination of '=' must be a variable");
}


/// simplestmt
///   = returnstmt | varstmt | assignstmt | expression ;
static unique_ptr<ExpressionNode> ParseSimpleStatement() {
  // simplestmt = returnstmt | varstmt | assignstmt | expression
  if (CurrentToken == tok_return)
    return ParseReturnStatement();
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
///   = name "(" [ name { "," name } ] ")" ;
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

  if (CurrentToken != '(')
    return LogErrorSignature("Expected '(' in function signature");

  // Parse argument names. The loop calls getNextToken() at the top to advance
  // past '(' on the first iteration, and past ',' on subsequent ones.
  // Inside the body we call getNextToken() again to move past the name
  // we just stored, then check whether ')' or ',' follows.

  vector<string> ParameterNames;
  while (getNextToken() == tok_name) {
    ParameterNames.push_back(Name);
    if (getNextToken() == ')') // eat name, check what follows
      break;
    if (CurrentToken != ',')
      return LogErrorSignature("Expected ')' or ',' in parameter list");
    // loop continues: getNextToken() at the top eats the ','
  }

  if (CurrentToken != ')')
    return LogErrorSignature("Expected ')' in function signature");
  getNextToken(); // eat ')'

  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames), SignatureLoc);
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
  FunctionScopeGuard Scope(Signature->getParameters());

  if (CurrentToken != ':')
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'
  unique_ptr<ExpressionNode> Body = ParseFunctionBody();

  if (Body) {
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
  }
  return nullptr;
}

/// binarydecorator
///   = "binary" "(" integer ")"
///
/// Called after '@' has been consumed. CurrentToken is on 'binary'.
/// Returns the parsed precedence (>= 1), or 0 on error.
/// 0 is a safe sentinel because valid precedences must be >= 1.
static unsigned ParseBinaryDecorator() {
  getNextToken(); // eat 'binary'

  if (CurrentToken != '(') {
    LogError("Expected '(' after '@binary'");
    return 0;
  }
  getNextToken(); // eat '('

  if (CurrentToken != tok_number) {
    LogError("Expected precedence number in '@binary(...)'");
    return 0;
  }
  // The lexer has no separate tok_integer — it emits tok_number for both
  // integer and decimal literals. Reject decimals by checking the raw source.
  if (NumberLiteral.find('.') != string::npos) {
    LogError("Precedence must be an integer, not a decimal literal");
    return 0;
  }
  if (NumberValue < 1) {
    LogError("Precedence must be a positive integer");
    return 0;
  }
  unsigned Prec = static_cast<unsigned>(NumberValue);
  getNextToken(); // eat number

  if (CurrentToken != ')') {
    LogError("Expected ')' after precedence in '@binary(...)'");
    return 0;
  }
  getNextToken(); // eat ')'

  return Prec;
}

/// unarydecorator
///   = "unary"
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
  return isascii(Tok) && ispunct(static_cast<unsigned char>(Tok)) && Tok != '@';
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
///   = customopchar "(" name "," name ")"
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

  if (CurrentToken != '(')
    return LogErrorSignature("Expected '(' in binary operator signature");

  vector<string> ParameterNames;
  while (getNextToken() == tok_name) {
    ParameterNames.push_back(Name);
    if (getNextToken() == ')')
      break;
    if (CurrentToken != ',')
      return LogErrorSignature("Expected ')' or ',' in parameter list");
  }

  if (CurrentToken != ')')
    return LogErrorSignature("Expected ')' in binary operator signature");
  getNextToken(); // eat ')'

  if (ParameterNames.size() != 2)
    return LogErrorSignature("Binary operator must have exactly two arguments");

  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames), SignatureLoc,
                                   /*IsOperator=*/true, Precedence);
}

/// unaryopprototype
///   = customopchar "(" name ")"
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

  if (CurrentToken != '(')
    return LogErrorSignature("Expected '(' in unary operator signature");

  vector<string> ParameterNames;
  while (getNextToken() == tok_name) {
    ParameterNames.push_back(Name);
    if (getNextToken() == ')')
      break;
    if (CurrentToken != ',')
      return LogErrorSignature("Expected ')' or ',' in parameter list");
  }

  if (CurrentToken != ')')
    return LogErrorSignature("Expected ')' in unary operator signature");
  getNextToken(); // eat ')'

  if (ParameterNames.size() != 1)
    return LogErrorSignature("Unary operator must have exactly one argument");

  // Unary operators have no precedence — they bind tighter than any binary op
  // by virtue of being parsed before ParseBinaryOperatorRight is entered.
  return make_unique<FunctionSignatureNode>(FnName, std::move(ParameterNames), SignatureLoc,
                                   /*IsOperator=*/true, /*Precedence=*/0);
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
  FunctionScopeGuard Scope(Signature->getParameters());

  // Shared body: ":" ( simplestmt | end-of-lines block ) — identical to
  // ParseFunctionDefinition.
  if (CurrentToken != ':')
    return LogErrorF("Expected ':' in operator definition");
  getNextToken(); // eat ':'
  unique_ptr<ExpressionNode> Body = ParseFunctionBody();

  if (Body) {
    return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Body));
  }
  return nullptr;
}

/// toplevelstmt
///   = statement ;
static unique_ptr<ExpressionNode> ParseTopLevelStatement() {
  TopLevelParseGuard Guard;
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

  if (!Stmt->isReturnExpr())
    Stmt = make_unique<ReturnExpressionNode>(std::move(Stmt));

  string FnName = "__pyxc.toplevel." + to_string(TopLevelExprCounter++);
  auto Signature = make_unique<FunctionSignatureNode>(FnName, vector<string>(), CurLoc);
  return make_unique<FunctionDefinitionNode>(std::move(Signature), std::move(Stmt));
}

/// external
///   = "extern" "def" function signature
static unique_ptr<FunctionSignatureNode> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurrentToken != tok_def)
    return LogErrorSignature("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParseFunctionSignature();
}

//===----------------------------------------===//
// Code Generation
//===----------------------------------------===//

// TheContext/TheModule/Builder/NamedValues - Core IR construction globals.
// Recreated fresh for each new module (see InitializeModuleAndManagers).
//
// TheContext - Owns all LLVM data structures: types, constants, and the
// interning tables that ensure two uses of 'double' resolve to the same
// object.
//
// TheModule - The unit of compilation handed to the JIT. Because the JIT
// takes ownership of the module when a function is compiled, we create a
// new module for every top-level input. Functions defined in earlier modules
// remain callable via the JIT's symbol table.
//
// Builder - A cursor into the IR being built. Point it at a BasicBlock with
// SetInsertPoint(), then call Create* methods to append instructions.
//
// NamedValues - Symbol table mapping variable names to stack slots (allocas)
// in the current function. Function parameters are first copied into entry
// block allocas so parameters, loop variables, and mutable locals all share
// the same load/store path.
//
// TheJIT - The ORC JIT instance. Created once in main() and lives for the
// whole session. Compiled modules are added to it; symbols from C libraries
// (e.g. sin, cos) are resolved through the process's dynamic symbol table.
//
// TheFPM / TheMPM / TheLAM / TheFAM / TheCGAM / TheMAM - The new-PM pass and
// analysis managers. TheFPM holds the function pipeline used by the JIT;
// TheMPM holds the module pipeline used for file-mode compilation. The
// analysis managers cache analysis results and are cross-registered so passes
// that need loop or CGSCC analyses can find them.
//
//
// ExitOnErr - Convenience wrapper that terminates the process on a
// recoverable LLVM error. Used for JIT operations that should never fail
// in a correct implementation.
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<NoFolder>> Builder;
static std::map<std::string, AllocaInst *> NamedValues;
static bool InGlobalInit = false;
static bool ModuleHasGlobals = false;
static std::string CurrentSourcePath = "<stdin>";
static std::unique_ptr<DIBuilder> DIB;
static DICompileUnit *TheCU = nullptr;
static DIFile *TheDIFile = nullptr;
static DIType *DblDIType = nullptr;
static DIType *VoidDIType = nullptr;
static DIScope *CurDIScope = nullptr;
static unsigned CurFunctionLine = 1;
static std::unique_ptr<PyxcJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<ModulePassManager> TheMPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static ExitOnError ExitOnErr;

/// LogErrorV - Codegen-level error helper. Delegates to LogError for printing,
/// then returns nullptr so codegen callers can write: return LogErrorV("msg");
Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

/// CreateEntryBlockAlloca - Create a stack slot in the current function's
/// entry block for a mutable variable.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const string &VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
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
    DblDIType = nullptr;
    VoidDIType = nullptr;
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
  DblDIType = DIB->createBasicType("double", 64, dwarf::DW_ATE_float);
  VoidDIType = DIB->createUnspecifiedType("void");

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
                             bool IsParam, unsigned ArgNo = 0) {
  if (!DIB || !CurDIScope || !Alloca)
    return;

  DIType *Ty = DblDIType
                   ? DblDIType
                   : DIB->createBasicType("double", 64, dwarf::DW_ATE_float);
  auto *Loc = DILocation::get(*TheContext, Line, 1, CurDIScope);
  DILocalVariable *Var = nullptr;
  if (IsParam) {
    Var = DIB->createParameterVariable(CurDIScope, Name, ArgNo, TheDIFile, Line,
                                       Ty, true);
  } else {
    Var = DIB->createAutoVariable(CurDIScope, Name, TheDIFile, Line, Ty, true);
  }

  DIB->insertDeclare(Alloca, Var, DIB->createExpression(), Loc,
                     Builder->GetInsertBlock());
}

static void EmitDebugGlobal(GlobalVariable *GV, StringRef Name, unsigned Line) {
  if (!DIB || !TheCU || !GV)
    return;
  DIType *Ty = DblDIType
                   ? DblDIType
                   : DIB->createBasicType("double", 64, dwarf::DW_ATE_float);
  auto *GVE = DIB->createGlobalVariableExpression(TheCU, Name, Name, TheDIFile,
                                                  Line, Ty, true);
  GV->addDebugInfo(GVE);
}

/// GetGlobalVariable - Return a module-local GlobalVariable* for Name.
///
/// If the global is defined in this module, returns it. If the global exists
/// in another module (tracked by GlobalVarNames), emit a declaration in the
/// current module and return that. Returns nullptr if the name is unknown.
static GlobalVariable *GetGlobalVariable(const string &Name) {
  if (auto *GV = TheModule->getNamedGlobal(Name))
    return GV;

  if (!GlobalVarNames.count(Name))
    return nullptr;

  auto *Ty = Type::getDoubleTy(*TheContext);
  return new GlobalVariable(*TheModule, Ty, false, GlobalValue::ExternalLinkage,
                            nullptr, Name);
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

/// NumberExpressionNode::codegen - A numeric literal becomes a floating-point
/// constant value.
///
/// ConstantFP::get wraps an APFloat (LLVM's arbitrary-precision float) into a
/// constant node that can be used directly as an operand. No instruction is
/// emitted — constants are folded into whatever instruction uses them. In
/// this chapter we disable IRBuilder's constant folder so that -O0 preserves
/// the original IR and constant folding only happens in optimisation passes.
Value *NumberExpressionNode::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Value));
}

/// NameExpressionNode::codegen - A variable reference loads the current value
/// from the variable's stack slot.
Value *NameExpressionNode::codegen() {
  auto It = NamedValues.find(Name);
  if (It != NamedValues.end() && It->second)
    return Builder->CreateLoad(Type::getDoubleTy(*TheContext), It->second,
                               Name.c_str());

  if (auto *GV = GetGlobalVariable(Name))
    return Builder->CreateLoad(Type::getDoubleTy(*TheContext), GV,
                               Name.c_str());

  return LogErrorV("Unknown variable name");
}

/// AssignmentExpressionNode::codegen - Evaluate the Right, store it into the variable's
/// stack slot, and produce the assigned value.
Value *AssignmentExpressionNode::codegen() {
  Value *Val = Expr->codegen();
  if (!Val)
    return nullptr;

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

/// ReturnExpressionNode::codegen - Emit a return from the current function.
Value *ReturnExpressionNode::codegen() {
  Value *RetVal = Expr->codegen();
  if (!RetVal)
    return nullptr;

  Builder->CreateRet(RetVal);
  return RetVal;
}

/// BlockExpressionNode::codegen - Evaluate statements in order.
/// Saves and restores NamedValues to implement block scoping: variables
/// declared inside the block are not visible after it exits.
Value *BlockExpressionNode::codegen() {
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

  switch (Operator) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpOLT(L, R, "cmptmp");
    // Widen the i1 boolean to double: false -> 0.0, true -> 1.0.
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case '>':
    L = Builder->CreateFCmpOGT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_eq:
    L = Builder->CreateFCmpOEQ(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_neq:
    L = Builder->CreateFCmpUNE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_leq:
    L = Builder->CreateFCmpOLE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case tok_geq:
    L = Builder->CreateFCmpOGE(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
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
  if (Opcode == '-')
    return Builder->CreateFNeg(Operator, "negtmp");

  // User-defined unary operator.
  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return Builder->CreateCall(F, Operator, "unop");
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

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Arguments.size(); i != e; ++i) {
    ArgsV.push_back(Arguments[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

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

  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else", TheFunction);
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont", TheFunction);

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  Builder->SetInsertPoint(ThenBB);
  if (!Then->codegen())
    return nullptr;
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(ElseBB);
  if (Else) {
    if (!Else->codegen())
      return nullptr;
  }
  if (!Builder->GetInsertBlock()->getTerminator())
    Builder->CreateBr(MergeBB);

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
    Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    EmitDebugDeclare(Alloca, VarName, CurFunctionLine, false);
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

  Builder->CreateStore(StartVal, VarPtr);

  BasicBlock *CondBB =
      BasicBlock::Create(*TheContext, "loop_cond", TheFunction);
  BasicBlock *BodyBB =
      BasicBlock::Create(*TheContext, "loop_body", TheFunction);
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "after_loop", TheFunction);

  Builder->CreateBr(CondBB);

  Builder->SetInsertPoint(CondBB);


  Value *CondVal = Cond->codegen();
  if (!CondVal)
    return nullptr;
  CondVal = Builder->CreateFCmpONE(
      CondVal, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
  Builder->CreateCondBr(CondVal, BodyBB, AfterBB);

  Builder->SetInsertPoint(BodyBB);

  if (!Body->codegen())
    return nullptr;

  Value *CurVar =
      Builder->CreateLoad(Type::getDoubleTy(*TheContext), VarPtr, VarName);
  Value *StepVal = Step->codegen();
  if (!StepVal)
    return nullptr;
  Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
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

/// VarStatementNode::codegen - Allocate mutable local variables and initialize them.
Value *VarStatementNode::codegen() {
  if (InGlobalInit) {
    for (auto &Var : VarNames) {
      const string &VarName = Var.first;
      ExpressionNode *Init = Var.second.get();

      auto *GV = TheModule->getNamedGlobal(VarName);
      if (GV && !GV->isDeclaration())
        return LogErrorV("Global variable already defined");

      if (!GV) {
        auto *Ty = Type::getDoubleTy(*TheContext);
        GV = new GlobalVariable(
            *TheModule, Ty, false, GlobalValue::ExternalLinkage,
            ConstantFP::get(*TheContext, APFloat(0.0)), VarName);
        EmitDebugGlobal(GV, VarName, CurFunctionLine);
      } else if (GV->isDeclaration()) {
        GV->setInitializer(ConstantFP::get(*TheContext, APFloat(0.0)));
        GV->setLinkage(GlobalValue::ExternalLinkage);
        EmitDebugGlobal(GV, VarName, CurFunctionLine);
      }

      ModuleHasGlobals = true;

      Value *InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;

      Builder->CreateStore(InitVal, GV);
    }

    return ConstantFP::get(*TheContext, APFloat(0.0));
  }

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const string &VarName = Var.first;
    ExpressionNode *Init = Var.second.get();

    Value *InitVal = Init->codegen();
    if (!InitVal)
      return nullptr;

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);
    NamedValues[VarName] = Alloca;
    EmitDebugDeclare(Alloca, VarName, CurFunctionLine, false);
  }

  return ConstantFP::get(*TheContext, APFloat(0.0));
}

/// FunctionSignatureNode::codegen - Create a function declaration in TheModule: name,
/// return type (always double), and parameter types (all double).
///
/// ExternalLinkage makes the function visible outside this module. That is
/// what allows 'extern def sin(x)' to link against the C library's sin at
/// runtime, and what lets 'def foo(...)' be called from later expressions in
/// the same session.
///
/// Arg.setName() is optional — it only affects the printed IR, making output
/// read as 'double %a, double %b' rather than 'double %0, double %1'.
Function *FunctionSignatureNode::codegen() {
  // All parameters and the return value are double.
  std::vector<Type *> Doubles(Parameters.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles,
                                       false /* not variadic */);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Name arguments so the printed IR is readable.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Parameters[Idx++]);

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

  DISubprogram *SP = nullptr;
  if (DIB && TheDIFile) {
    bool IsInternal = P.getName().rfind("__pyxc.", 0) == 0;
    if (!IsInternal) {
      unsigned Line = P.getLocation().Line ? P.getLocation().Line : 1;
      SmallVector<Metadata *, 8> EltTys;
      EltTys.push_back(DblDIType);
      for (size_t i = 0; i < P.getParameters().size(); ++i)
        EltTys.push_back(DblDIType);
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
  for (auto &Arg : TheFunction->args()) {
    AllocaInst *Alloca =
        CreateEntryBlockAlloca(TheFunction, std::string(Arg.getName()));
    Builder->CreateStore(&Arg, Alloca);
    NamedValues[std::string(Arg.getName())] = Alloca;
    EmitDebugDeclare(Alloca, Arg.getName(), CurFunctionLine, true, ArgIndex++);
  }

  // Step 4: codegen the body, optimise, verify, or erase on failure.
  if (Value *BodyVal = Body->codegen()) {
    // If the body didn't already terminate the current block (e.g. via
    // return), return 0.0. Implicit returns never use the last expression.
    if (!Builder->GetInsertBlock()->getTerminator())
      Builder->CreateRet(ConstantFP::get(*TheContext, APFloat(0.0)));
    verifyFunction(*TheFunction);

    // Run the optimisation pipeline: InstCombine, Reassociate, GVN,
    // SimplifyCFG.
    TheFPM->run(*TheFunction, *TheFAM);
    CurDIScope = nullptr;
    return TheFunction;
  }

  // Body codegen failed — remove the incomplete function so it cannot be
  // called and does not pollute the module handed to the JIT.
  TheFunction->eraseFromParent();
  CurDIScope = nullptr;
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
  GlobalVarNames.clear();
  VarScopes.clear();
  FileTopLevelStmts.clear();
  LastTopLevelShouldPrint = true;
  InGlobalInit = false;
  ModuleHasGlobals = false;
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
  // name + arity, since all parameter and return types are double.
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

/// HandleTopLevelExpression - Compile, execute, and discard a bare expression.
///
/// The expression is wrapped in '__anon_expr' (a zero-argument function that
/// returns double) so it goes through the same codegen path as everything else.
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

      // Cast the symbol address to a callable function pointer and invoke it.
      double (*FP)() = ExprSymbol.toPtr<double (*)()>();
      double result = FP();
      if (IsRepl && LastTopLevelShouldPrint)
        fprintf(stderr, "%f\n", result);

      // Release the compiled code and JIT memory for this expression.
      ExitOnErr(RT->remove());
      return;
    }

    // Keep-module path: call the compiled function after adding the module.
    auto ExprSymbol = ExitOnErr(TheJIT->lookup(FnName));
    double (*FP)() = ExprSymbol.toPtr<double (*)()>();
    double result = FP();
    if (IsRepl && LastTopLevelShouldPrint)
      fprintf(stderr, "%f\n", result);
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
    case tok_def:
      HandleFunctionDefinition();
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
    case tok_def:
      HandleFunctionDefinition();
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
    auto Block = make_unique<BlockExpressionNode>(std::move(FileTopLevelStmts));
    auto Signature = make_unique<FunctionSignatureNode>(
        "__pyxc.global_init", vector<string>(), SourceLocation{1, 1});
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
      double (*InitFn)() = InitSymbol.toPtr<double (*)()>();
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
    return;
  }

  auto MainSymbol = ExitOnErr(TheJIT->lookup("main"));
  double (*MainFn)() = MainSymbol.toPtr<double (*)()>();
  MainFn();
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
  if (!FileTopLevelStmts.empty()) {
    auto Block = make_unique<BlockExpressionNode>(std::move(FileTopLevelStmts));
    auto Signature = make_unique<FunctionSignatureNode>(
        "__pyxc.global_init", vector<string>(), SourceLocation{1, 1});
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
    return false;
  }

  if (auto *UserMain = TheModule->getFunction("main")) {
    if (UserMain->getReturnType()->isDoubleTy()) {
      UserMain->setName("__pyxc.user_main");
      FunctionType *FT =
          FunctionType::get(Type::getInt32Ty(*TheContext), false);
      Function *Wrapper = Function::Create(FT, Function::ExternalLinkage,
                                           "main", TheModule.get());
      BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", Wrapper);
      IRBuilder<> TmpB(BB);
      TmpB.CreateCall(UserMain);
      TmpB.CreateRet(ConstantInt::get(Type::getInt32Ty(*TheContext), 0));
    }
  }

  return true;
}

/// EmitFileMode - Build __pyxc.global_init and emit the requested output file.
static void EmitFileMode() {
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
      if (IsEmitMode())
        EmitFileMode();
      else
        RunFileMode();

      CloseInputFile();
    }
  }

  return 0;
}

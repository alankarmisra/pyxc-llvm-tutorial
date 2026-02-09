--- pyxc.cpp	2026-02-09 13:50:33
+++ ../chapter11/pyxc.cpp	2026-02-09 00:07:46
@@ -64,14 +64,13 @@
                                           cl::Optional, cl::cat(PyxcCategory));
 
 // Execution mode enum
-enum ExecutionMode { Interpret, Executable, Object, Tokens };
+enum ExecutionMode { Interpret, Executable, Object };
 
 static cl::opt<ExecutionMode> Mode(
     cl::desc("Execution mode:"),
     cl::values(clEnumValN(Interpret, "i",
                           "Interpret the input file immediately (default)"),
-               clEnumValN(Object, "c", "Compile to object file"),
-               clEnumValN(Tokens, "t", "Print tokens")),
+               clEnumValN(Object, "c", "Compile to object file")),
     cl::init(Executable), cl::cat(PyxcCategory));
 
 static cl::opt<std::string> OutputFilename(
@@ -118,9 +117,6 @@
 enum Token {
   tok_eof = -1,
   tok_eol = -2,
-  tok_indent = -15,
-  tok_dedent = -16,
-  tok_error = -17,
 
   // commands
   tok_def = -3,
@@ -149,7 +145,7 @@
 
 static std::string IdentifierStr; // Filled in if tok_identifier
 static double NumVal;             // Filled in if tok_number
-static int InForExpression;       // Track global parsing context
+static bool InForExpression;      // Track global parsing context
 
 // Keywords words like `def`, `extern` and `return`. The lexer will return the
 // associated Token. Additional language keywords can easily be added here.
@@ -172,15 +168,6 @@
 static SourceLocation CurLoc;
 static SourceLocation LexLoc = {1, 0};
 
-// Indentation related variables
-static int ModuleIndentType = -1;
-static bool AtStartOfLine = true;
-static std::vector<int> Indents = {0};
-static std::deque<int> PendingTokens;
-// CHATGPT: START
-static int LastIndentWidth = 0;
-// CHATGPT: END
-
 static int advance() {
   int LastChar = getc(InputFile);
 
@@ -192,166 +179,11 @@
   return LastChar;
 }
 
-namespace {
-class ExprAST;
-}
-std::unique_ptr<ExprAST> LogError(const char *Str);
-
-/// countIndent - count the indent in terms of spaces
-// LastChar is the current unconsumed character at the start of the line.
-// LexLoc.Col already reflects that character’s column (0-based, after
-// reading it), so for tabs we advance to the next tab stop using
-// (LexLoc.Col % 8).
-static int countLeadingWhitespace(int &LastChar) {
-  //   fprintf(stderr, "countLeadingWhitespace(%d, %d)", LexLoc.Line,
-  //   LexLoc.Col);
-
-  int indentCount = 0;
-  bool didSetIndent = false;
-
-  while (true) {
-    while (LastChar == ' ' || LastChar == '\t') {
-      if (ModuleIndentType == -1) {
-        didSetIndent = true;
-        ModuleIndentType = LastChar;
-      } else {
-        if (LastChar != ModuleIndentType) {
-          LogError("You cannot mix tabs and spaces.");
-          return -1;
-        }
-      }
-      indentCount += LastChar == '\t' ? 8 - (LexLoc.Col % 8) : 1;
-      LastChar = advance();
-    }
-
-    if (LastChar == '\r' || LastChar == '\n') { // encountered a blank line
-      //   PendingTokens.push_back(tok_eol);
-      if (didSetIndent) {
-        didSetIndent = false;
-        indentCount = 0;
-        ModuleIndentType = -1;
-      }
-
-      LastChar = advance(); // eat the newline
-      continue;
-    }
-
-    break;
-  }
-  //   fprintf(stderr, " = %d | AtStartOfLine = %s\n", indentCount,
-  //           AtStartOfLine ? "true" : "false");
-  return indentCount;
-}
-
-static bool IsIndent(int leadingWhitespace) {
-  assert(!Indents.empty());
-  assert(leadingWhitespace >= 0);
-  //   fprintf(stderr, "IsIndent(%d) = (%d)\n", leadingWhitespace,
-  //           leadingWhitespace > Indents.back());
-  return leadingWhitespace > Indents.back();
-}
-
-static int HandleIndent(int leadingWhitespace) {
-  assert(!Indents.empty());
-  assert(leadingWhitespace >= 0);
-
-  // CHATGPT: START
-  LastIndentWidth = leadingWhitespace;
-  // CHATGPT: END
-  Indents.push_back(leadingWhitespace);
-  return tok_indent;
-}
-
-static int DrainIndents() {
-  int dedents = 0;
-  while (Indents.size() > 1) {
-    Indents.pop_back();
-    dedents++;
-  }
-
-  if (dedents > 0) {
-    while (dedents-- > 1) {
-      PendingTokens.push_back(tok_dedent);
-    }
-    return tok_dedent;
-  }
-
-  return tok_eof;
-}
-
-static int HandleDedent(int leadingWhitespace) {
-  assert(!Indents.empty());
-  assert(leadingWhitespace >= 0);
-  assert(leadingWhitespace < Indents.back());
-
-  int dedents = 0;
-
-  while (leadingWhitespace < Indents.back()) {
-    Indents.pop_back();
-    dedents++;
-  }
-
-  if (leadingWhitespace != Indents.back()) {
-    LogError("Expected indentation.");
-    Indents = {0};
-    PendingTokens.clear();
-    return tok_error;
-  }
-
-  if (!dedents) // this should never happen
-  {
-    LogError("Internal error.");
-    return tok_error;
-  }
-
-  //   fprintf(stderr, "Pushing %d dedents for whitespace %d on %d, %d\n",
-  //   dedents,
-  //           leadingWhitespace, LexLoc.Line, LexLoc.Col);
-  while (dedents-- > 1) {
-    PendingTokens.push_back(tok_dedent);
-  }
-  return tok_dedent;
-}
-
-static bool IsDedent(int leadingWhitespace) {
-  assert(!Indents.empty());
-  //   fprintf(stderr, "Return %s for IsDedent(%d), Indents.back = %d\n",
-  //           (leadingWhitespace < Indents.back()) ? "true" : "false",
-  //           leadingWhitespace, Indents.back());
-  return leadingWhitespace < Indents.back();
-}
-
 /// gettok - Return the next token from standard input.
 static int gettok() {
-  static int LastChar = '\0';
+  static int LastChar = ' ';
 
-  if (LastChar == '\0')
-    LastChar = advance();
-
-  if (!PendingTokens.empty()) {
-    int tok = PendingTokens.front();
-    PendingTokens.pop_front();
-    return tok;
-  }
-
-  if (AtStartOfLine) {
-    int leadingWhitespace = countLeadingWhitespace(LastChar);
-    if (leadingWhitespace < 0)
-      return tok_error;
-
-    AtStartOfLine = false;
-    if (IsIndent(leadingWhitespace)) {
-      return HandleIndent(leadingWhitespace);
-    }
-    if (IsDedent(leadingWhitespace)) {
-      //   fprintf(stderr, "Pushing dedent on row:%d, col:%d\n", LexLoc.Line,
-      //           LexLoc.Col);
-      return HandleDedent(leadingWhitespace);
-    }
-  }
-
-  // Skip whitespace EXCEPT newlines (this will take care of spaces
-  // mid-expressions)
+  // Skip whitespace EXCEPT newlines
   while (isspace(LastChar) && LastChar != '\n' && LastChar != '\r')
     LastChar = advance();
 
@@ -363,8 +195,7 @@
     // If we called advance() here, it would block waiting for input,
     // requiring the user to press Enter twice in the REPL.
     // Setting LastChar = ' ' avoids this blocking read.
-    LastChar = '\0';
-    AtStartOfLine = true; // Modify state only when you're emitting the token.
+    LastChar = ' ';
     return tok_eol;
   }
 
@@ -406,102 +237,14 @@
   }
 
   // Check for end of file.  Don't eat the EOF.
-  if (LastChar == EOF) {
-    return DrainIndents();
-  }
+  if (LastChar == EOF)
+    return tok_eof;
 
   // Otherwise, just return the character as its ascii value.
   int ThisChar = LastChar;
   LastChar = advance();
   return ThisChar;
-}
-
-// CHATGPT: START
-static const char *TokenName(int Tok) {
-  switch (Tok) {
-  case tok_eof:
-    return "<eof>";
-  case tok_eol:
-    return "<eol>";
-  case tok_indent:
-    return "<indent>";
-  case tok_dedent:
-    return "<dedent>";
-  case tok_error:
-    return "<error>";
-  case tok_def:
-    return "<def>";
-  case tok_extern:
-    return "<extern>";
-  case tok_identifier:
-    return "<identifier>";
-  case tok_number:
-    return "<number>";
-  case tok_if:
-    return "<if>";
-  case tok_else:
-    return "<else>";
-  case tok_return:
-    return "<return>";
-  case tok_for:
-    return "<for>";
-  case tok_in:
-    return "<in>";
-  case tok_range:
-    return "<range>";
-  case tok_decorator:
-    return "<decorator>";
-  case tok_var:
-    return "<var>";
-  default:
-    return nullptr;
-  }
-}
-
-static void PrintTokens(const std::string &filename) {
-  // Open input file
-  InputFile = fopen(filename.c_str(), "r");
-  if (!InputFile) {
-    errs() << "Error: Could not open file " << filename << "\n";
-    InputFile = stdin;
-    return;
-  }
-
-  int Tok = gettok();
-  bool FirstOnLine = true;
-
-  while (Tok != tok_eof) {
-    if (Tok == tok_eol) {
-      fprintf(stderr, "<eol>\n");
-      FirstOnLine = true;
-      Tok = gettok();
-      continue;
-    }
-
-    if (!FirstOnLine)
-      fprintf(stderr, " ");
-    FirstOnLine = false;
-
-    if (Tok == tok_indent) {
-      fprintf(stderr, "<indent=%d>", LastIndentWidth);
-    } else {
-      const char *Name = TokenName(Tok);
-      if (Name)
-        fprintf(stderr, "%s", Name);
-      else if (isascii(Tok))
-        fprintf(stderr, "<%c>", Tok);
-      else
-        fprintf(stderr, "<tok=%d>", Tok);
-    }
-
-    Tok = gettok();
-  }
-
-  if (!FirstOnLine)
-    fprintf(stderr, " ");
-  fprintf(stderr, "<eof>\n");
 }
-// CHATGPT: END
 
 //===----------------------------------------------------------------------===//
 // Abstract Syntax Tree (aka Parse Tree)
@@ -753,7 +496,7 @@
 
 /// LogError* - These are little helper functions for error handling.
 std::unique_ptr<ExprAST> LogError(const char *Str) {
-  InForExpression = 0;
+  InForExpression = false;
   fprintf(stderr, "%sError (Line: %d, Column: %d): %s\n%s", Red, CurLoc.Line,
           CurLoc.Col, Str, Reset);
   return nullptr;
@@ -837,11 +580,9 @@
  * In later chapters, we will need to check the indentation
  * whenever we eat new lines.
  */
-static bool EatNewLines() {
-  bool consumedNewLine = CurTok == tok_eol;
+static void EatNewLines() {
   while (CurTok == tok_eol)
     getNextToken();
-  return consumedNewLine;
 }
 
 // ifexpr ::= 'if' expression ':' expression 'else' ':' expression
@@ -858,18 +599,8 @@
     return LogError("expected `:`");
   getNextToken(); // eat ':'
 
-  bool ConditionUsesNewLines = EatNewLines();
-  int thenIndentLevel = 0, elseIndentLevel = 0;
+  EatNewLines();
 
-  // Parse `then` clause
-  if (ConditionUsesNewLines) {
-    if (CurTok != tok_indent) {
-      return LogError("Expected indent");
-    }
-    thenIndentLevel = Indents.back();
-    getNextToken(); // eat indent
-  }
-
   // Handle nested `if` and `for` expressions:
   // For `if` expressions, `return` statements are emitted inside the
   // true and false branches, so we only require an explicit `return`
@@ -888,20 +619,11 @@
   if (!Then)
     return nullptr;
 
-  bool ThenUsesNewLines = EatNewLines();
-  if (!ThenUsesNewLines) {
-    return LogError("Expected newline before else condition");
-  }
+  EatNewLines();
 
-  if (ThenUsesNewLines && CurTok != tok_dedent) {
-    return LogError("Expected dedent after else");
-  }
-  // We could get multiple dedents from nested if's and for's
-  while (getNextToken() == tok_dedent)
-    ;
+  if (CurTok != tok_else)
+    return LogError("expected `else`");
 
-  if (CurTok != tok_else)
-    return LogError("Expected `else`");
   getNextToken(); // eat else
 
   if (CurTok != ':')
@@ -909,23 +631,7 @@
 
   getNextToken(); // eat ':'
 
-  bool ElseUsesNewLines = EatNewLines();
-  if (ThenUsesNewLines != ElseUsesNewLines) {
-    return LogError("Both `then` and `else` clause should be consistent in "
-                    "their usage of newlines.");
-  }
-
-  if (ElseUsesNewLines) {
-    if (CurTok != tok_indent) {
-      return LogError("Expected indent.");
-    }
-    elseIndentLevel = Indents.back();
-    getNextToken(); // eat indent
-  }
-
-  if (thenIndentLevel != elseIndentLevel) {
-    return LogError("Indent mismatch between if and else");
-  }
+  EatNewLines();
 
   if (!InForExpression && CurTok != tok_if && CurTok != tok_for) {
     if (CurTok != tok_return)
@@ -938,15 +644,6 @@
   if (!Else)
     return nullptr;
 
-  //   EatNewLines();
-
-  //   if (ThenUsesNewLines && CurTok != tok_dedent) {
-  //     return LogError("Expected dedent");
-  //   }
-  //   while (getNextToken() == tok_dedent)
-  //     ;
-  //   // getNextToken(); // eat dedent
-
   return std::make_unique<IfExprAST>(IfLoc, std::move(Cond), std::move(Then),
                                      std::move(Else));
 }
@@ -955,7 +652,7 @@
 //   (`,` expression)? # optional
 // `)`: expression
 static std::unique_ptr<ExprAST> ParseForExpr() {
-  InForExpression++;
+  InForExpression = true;
   getNextToken(); // eat for
 
   if (CurTok != tok_identifier)
@@ -1003,21 +700,15 @@
     return LogError("expected `:` after range operator");
   getNextToken(); // eat `:`
 
-  bool UsesNewLines = EatNewLines();
+  EatNewLines();
 
-  if (UsesNewLines && CurTok != tok_indent)
-    return LogError("expected indent.");
-
-  getNextToken(); // eat indent
-
   // `for` expressions don't have the return statement
   // they return 0 by default.
   auto Body = ParseExpression();
   if (!Body)
     return nullptr;
 
-  InForExpression--;
-
+  InForExpression = false;
   return std::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End),
                                       std::move(Step), std::move(Body));
 }
@@ -1080,7 +771,6 @@
 static std::unique_ptr<ExprAST> ParsePrimary() {
   switch (CurTok) {
   default:
-    fprintf(stderr, "CurTok = %d\n", CurTok);
     return LogError("Unknown token when expecting an expression");
   case tok_identifier:
     return ParseIdentifierExpr();
@@ -1287,28 +977,17 @@
 
   EatNewLines();
 
-  if (CurTok != tok_indent)
-    return LogErrorF("Expected indentation.");
-  getNextToken(); // eat tok_indent
-
   if (!InForExpression && CurTok != tok_if && CurTok != tok_for &&
       CurTok != tok_var) {
-    if (CurTok != tok_return) {
-      //   fprintf(stderr, "CurTok = %d\n", CurTok);
+    if (CurTok != tok_return)
       return LogErrorF("Expected 'return' before expression");
-    }
-
     getNextToken(); // eat return
   }
 
-  auto E = ParseExpression();
-  if (!E)
-    return nullptr;
+  if (auto E = ParseExpression())
+    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
 
-  EatNewLines();
-  while (CurTok == tok_dedent)
-    getNextToken();
-  return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
+  return nullptr;
 }
 
 /// toplevelexpr ::= expression
@@ -1992,16 +1671,13 @@
 static void MainLoop() {
   while (true) {
     switch (CurTok) {
-    case tok_error:
-      return;
     case tok_eof:
       return;
+
     case tok_eol: // Skip newlines
-      getNextToken();
-      break;
-    case tok_dedent:
       getNextToken();
       break;
+
     default:
       fprintf(stderr, "ready> ");
       switch (CurTok) {
@@ -2049,7 +1725,7 @@
 
 static void ParseSourceFile() {
   // Parse all definitions from the file
-  while (CurTok != tok_eof && CurTok != tok_error) {
+  while (CurTok != tok_eof) {
     switch (CurTok) {
     case tok_def:
     case tok_decorator:
@@ -2124,7 +1800,7 @@
   // Parse the source file
   getNextToken();
 
-  while (CurTok != tok_eof && CurTok != tok_error) {
+  while (CurTok != tok_eof) {
     switch (CurTok) {
     case tok_def:
     case tok_decorator:
@@ -2312,21 +1988,15 @@
       errs() << "Error: -x and -c flags require an input file\n";
       return 1;
     }
-
-    if (Mode == Tokens) {
-      errs() << "Error: -t flag requires an input file\n";
-      return 1;
-    }
-
     if (!OutputFilename.empty()) {
-      errs() << "Error: REPL mode cannot work with an output file\n";
+      errs() << "Error: -o flag requires an input file\n";
       return 1;
     }
 
     // Start REPL
     REPL();
   } else {
-    if (EmitDebug && Mode != Executable && Mode != Object) {
+    if (Mode != Executable && Mode != Object && EmitDebug) {
       errs() << "Error: -g is only allowed with executable builds (-x) or "
                 "object builds (-o)\n";
       return 1;
@@ -2420,13 +2090,7 @@
         outs() << scriptObj << "\n";
       break;
     }
-    case Tokens: {
-      if (Verbose)
-        std::cout << "Tokenizing " << InputFilename << "...\n";
-      PrintTokens(InputFilename);
-      break;
     }
-    }
   }
 
   return 0;

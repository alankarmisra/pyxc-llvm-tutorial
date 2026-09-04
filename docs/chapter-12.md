---
section: "Statements and Control Flow"
description: "Add indentation-based statement blocks, return, and statement forms of var, assignment, if, and for."
---

# 12. pyxc: Statement Blocks

Next: replace one-expression function bodies with real statement blocks.

Chapter 11 needs sequencing tricks:

```pyxc
def sum_to(n): var acc = 0: (for var i = 1, i <= n, i = i + 1: acc = acc + i) + acc
```

After this chapter, write the same function directly:

```pyxc
def sum_to(n):
    var acc = 0
    for var i = 1, i <= n, i = i + 1:
        acc = acc + i
    return acc
```

The new compiler boundary is:

```text
newline + indentation -> statement block -> ordered codegen
```

Work in:

```bash
cd code/chapter-12
```

## 1. Separate Statements from Expressions

Replace the Chapter 11 expression forms with:

```ebnf
function-definition = "def" function-signature ":" suite ;

statement          = simple-statement | compound-statement ;
simple-statement   = return-statement
                   | variable-statement
                   | assignment-statement
                   | expression ;
compound-statement = if-statement | for-statement ;

suite = simple-statement | end-of-lines block ;
block = INDENT statement
        { (end-of-lines | BLOCK_END) statement }
        DEDENT ;

return-statement     = "return" expression ;
variable-statement   = "var" variable-binding
                       { "," variable-binding } ;
assignment-statement = lvalue "=" expression ;

if-statement  = "if" expression ":" suite
                [ [ end-of-lines ] "else" ":" suite ] ;
for-statement = "for" [ "var" ] name "=" expression ","
                expression "," for-update ":" suite ;
for-update    = assignment-statement | expression ;

expression = comparison ;
lvalue     = name ;
```

The essential split is:

```text
value-producing: number, name, call, unary, binary, comparison
statement-only:  var, assignment, return, if, for, block
```

An inline suite remains valid:

```pyxc
if x: return 1
```

An indented suite can contain multiple statements:

```pyxc
if x:
    var y = 1
    return y
```

## 2. Add Statement and Layout Tokens

Add:

```cpp
tok_return = -14,
tok_indent = -17,
tok_dedent = -18,
tok_block_end = -19,
```

Add `{"return", tok_return}` to the keyword table and readable token names for all four.

`INDENT` and `DEDENT` come from the lexer. `BLOCK_END` is injected by the parser after it consumes a `DEDENT`; it tells an enclosing block that a nested compound statement ended without requiring another physical newline.

## 3. Track Indentation State

Add:

```cpp
static vector<int> IndentStack = {0};
static deque<int> PendingTokens;
static bool AtLineStart = true;
static constexpr int IndentTabWidth = 8;
```

The stack stores active indentation columns:

```text
[0]       -> top level
[0, 4]    -> one block
[0, 4, 8] -> nested block
```

The token queue handles a line that closes several blocks. One source position may need to emit several `DEDENT` tokens before its first real token.

At the start of `getToken()`, drain queued layout tokens first:

```cpp
if (!PendingTokens.empty()) {
  int Tok = PendingTokens.front();
  PendingTokens.pop_front();
  return Tok;
}
```

## 4. Count Indentation at Line Start

When `AtLineStart` is true, consume spaces and tabs before normal tokenization:

```cpp
int CurrentIndentRead = 0;

while (LastChar == ' ' || LastChar == '\t') {
  CurrentIndentRead +=
      LastChar == ' '
          ? 1
          : IndentTabWidth - CurrentIndentRead % IndentTabWidth;
  LastChar = advance();
}
```

Tabs advance to the next multiple of eight. Blank and comment-only lines do not change indentation in file mode.

In REPL mode, a blank line while inside a block closes one indentation level. This gives the user a way to finish a multi-line definition.

## 5. Emit `INDENT` and `DEDENT`

For an increase:

```cpp
int CurrentIndentOnStack = IndentStack.back();

if (CurrentIndentRead > CurrentIndentOnStack) {
  IndentStack.push_back(CurrentIndentRead);
  AtLineStart = false;
  return tok_indent;
}
```

For a decrease, pop and queue every closed level:

```cpp
if (CurrentIndentRead < CurrentIndentOnStack) {
  while (IndentStack.size() > 1 &&
         CurrentIndentRead < IndentStack.back()) {
    IndentStack.pop_back();
    PendingTokens.push_back(tok_dedent);
  }

  if (CurrentIndentRead != IndentStack.back()) {
    LogErrorAtLocation("inconsistent indentation",
                       CurrentTokenLocation);
    PendingTokens.clear();
    AtLineStart = false;
    return tok_error;
  }

  AtLineStart = false;
  int Tok = PendingTokens.front();
  PendingTokens.pop_front();
  return Tok;
}
```

Reject a dedent that matches no earlier level. At EOF, emit remaining `DEDENT` tokens one at a time before `tok_eof`. Whenever a newline is emitted, set `AtLineStart = true`.

## 6. Add Statement AST Nodes

Add a return node:

```cpp
class ReturnStatementNode : public ExpressionNode {
  unique_ptr<ExpressionNode> Expression;

public:
  ReturnStatementNode(unique_ptr<ExpressionNode> Expression)
      : Expression(std::move(Expression)) {}
  Value *codegen() override;
};
```

Add a block node:

```cpp
class BlockStatementNode : public ExpressionNode {
  vector<unique_ptr<ExpressionNode>> Statements;

public:
  BlockStatementNode(vector<unique_ptr<ExpressionNode>> Statements)
      : Statements(std::move(Statements)) {}
  Value *codegen() override;
};
```

Rename the Chapter 11 nodes:

```text
AssignmentExpressionNode -> AssignmentStatementNode
VariableExpressionNode   -> VariableStatementNode
IfExpressionNode         -> IfStatementNode
ForExpressionNode        -> ForStatementNode
```

`var` no longer owns a body. Its bindings remain active for the current block. `if` no longer needs both arms to produce a value, so `else` becomes optional.

## 7. Track Variables While Parsing

Add a stack of parse-time scopes:

```cpp
static vector<set<string>> LocalVariableScopes;
```

Add helpers to:

```text
begin/end a function scope
begin/end a nested block scope
begin/end a loop-variable scope
declare a name in the current scope
check the current scope for redeclaration
search all active scopes for assignment
```

Parameters enter the function scope. `var` bindings enter the current block. A `for var` name enters a temporary loop scope.

Use RAII guards so early parser returns still pop scopes:

```cpp
struct BlockScopeGuard {
  BlockScopeGuard() { BeginBlockScope(); }
  ~BlockScopeGuard() { EndBlockScope(); }
};
```

Use equivalent guards for function and loop scopes. This gives early errors for assignment to an undeclared name and redeclaration in the same scope.

## 8. Parse `return` and `var`

Add:

```cpp
static unique_ptr<ExpressionNode> ParseReturnStatement() {
  getNextToken(); // eat 'return'
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;
  return make_unique<ReturnStatementNode>(std::move(Expr));
}
```

Reuse Chapter 11's binding loop for `ParseVariableStatement()`, but remove the colon and body. For each binding:

1. Require a name.
2. Reject a duplicate in the current scope.
3. Parse an initializer or use `0.0`.
4. Append the binding.
5. Declare the name for later statements.

Return a `VariableStatementNode` containing only the bindings.

## 9. Parse Assignment at Statement Level

Make `ParseExpression()` return only `ParseComparison()` again.

Add:

```cpp
static unique_ptr<ExpressionNode>
ParseAssignmentOrExpressionStatement() {
  auto Expr = ParseExpression();
  if (!Expr)
    return nullptr;

  if (CurrentToken != tok_assign)
    return Expr;

  const string *AssignedName = Expr->getLValueName();
  if (!AssignedName)
    return LogErrorExpression(
        "Destination of '=' must be a variable");

  if (!IsVariableDeclared(*AssignedName))
    return LogErrorExpression(
        "Assignment to undeclared variable");

  getNextToken(); // eat '='
  auto Right = ParseExpression();
  if (!Right)
    return nullptr;

  return make_unique<AssignmentStatementNode>(
      *AssignedName, std::move(Right));
}
```

Use this for ordinary statements and the `for` update so `i = i + 1` remains valid in the header.

## 10. Parse Statements and Suites

Add the dispatch layers:

```cpp
static unique_ptr<ExpressionNode> ParseSimpleStatement() {
  if (CurrentToken == tok_return)
    return ParseReturnStatement();
  if (CurrentToken == tok_var)
    return ParseVariableStatement();
  return ParseAssignmentOrExpressionStatement();
}

static unique_ptr<ExpressionNode> ParseStatement() {
  if (CurrentToken == tok_if)
    return ParseIfStatement();
  if (CurrentToken == tok_for)
    return ParseForStatement();
  return ParseSimpleStatement();
}
```

Then parse what follows a colon:

```cpp
static unique_ptr<ExpressionNode> ParseSuite() {
  if (CurrentToken == tok_eol) {
    consumeNewlines();
    if (CurrentToken != tok_indent)
      return LogErrorExpression("Expected an indented block");
    return ParseBlock();
  }

  return ParseSimpleStatement();
}
```

## 11. Parse Blocks and Inject `BLOCK_END`

`ParseBlock()` should:

1. Consume `INDENT`.
2. Begin a block scope.
3. Require at least one statement.
4. Parse statements until `DEDENT`.
5. Accept newline or `BLOCK_END` between statements.
6. Consume `DEDENT` and expose a synthetic `BLOCK_END` to the caller.

The core loop is:

```cpp
while (CurrentToken != tok_dedent) {
  auto Statement = ParseStatement();
  if (!Statement)
    return nullptr;
  Statements.push_back(std::move(Statement));

  if (CurrentToken == tok_eol) {
    consumeNewlines();
    continue;
  }

  if (CurrentToken == tok_block_end) {
    getNextToken();
    continue;
  }

  if (CurrentToken != tok_dedent)
    return LogErrorExpression(
        "Expected newline or end of block");
}
```

Before returning:

```cpp
PendingTokens.push_front(tok_block_end);
getNextToken(); // consume DEDENT, expose BLOCK_END
```

The marker solves nested-block bookkeeping. A compound statement consumes the newline and dedent that ended its suite; the enclosing block still needs a separator before its next statement.

## 12. Convert `if` and `for` to Statements

For `if`, parse the condition and colon, then call `ParseSuite()` for the then arm. Make `else` optional. If the then suite ended with `BLOCK_END`, temporarily consume it while probing for `else`; restore it if no `else` follows.

For `for`, retain the Chapter 11 header fields:

```text
optional var, name, start, condition, update
```

Parse the update with `ParseAssignmentOrExpressionStatement()` and the body with `ParseSuite()`. If `var` is absent, require the loop variable to be already declared.

The loop itself no longer participates in `(for ...) + result`. The enclosing block sequences it naturally.

## 13. Generate `return`

Implement:

```cpp
Value *ReturnStatementNode::codegen() {
  Value *RetVal = Expression->codegen();
  if (!RetVal)
    return nullptr;

  TheBuilder->CreateRet(RetVal);
  return RetVal;
}
```

`ret` terminates the current basic block. No later instruction may be appended there.

## 14. Generate Blocks in Order

Implement:

```cpp
Value *BlockStatementNode::codegen() {
  auto SavedBindings = NamedValues;
  Value *Last = nullptr;

  for (auto &Statement : Statements) {
    if (TheBuilder->GetInsertBlock()->getTerminator())
      break;

    Last = Statement->codegen();
    if (!Last) {
      NamedValues = SavedBindings;
      return nullptr;
    }
  }

  NamedValues = SavedBindings;

  if (!Last)
    return LogErrorValue("Empty block");

  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

Stop after a terminator such as `return`. Restore `NamedValues` when leaving the block so inner declarations do not escape.

## 15. Generate Statement-Style Control Flow

For `VariableStatementNode`, allocate and initialize every binding, leave it installed until the enclosing block restores its table, and return `0.0`.

For `IfStatementNode`:

1. Convert the condition to `i1`.
2. Branch to then or else blocks.
3. Generate each present suite.
4. Branch to the merge only if that arm has no terminator.
5. Return `0.0` from the merge.

There is no PHI because an `if` statement does not produce a selected value.

For `ForStatementNode`, retain the mutable-slot CFG from Chapter 11. Generate the suite as the body, then the complete update, then branch back to the condition. Return `0.0` after the loop.

## 16. Add Implicit `0.0` Return

After generating a function body, add a return only if the current block has no terminator:

```cpp
if (!TheBuilder->GetInsertBlock()->getTerminator())
  TheBuilder->CreateRet(
      ConstantFP::get(*TheContext, APFloat(0.0)));
```

An explicit `return` wins. Reaching the end returns `0.0`. The last expression in a block is no longer an implicit return.

## 17. Keep Top-Level Mutable State Out for Now

Reject top-level `var` and assignment. Each top-level expression uses a temporary module; a local alloca cannot persist into the next REPL input.

```pyxc
ready> var count = 0
```

Expected:

```text
Error (Line 1, Column 1): Unexpected 'var'
```

Mutable storage works inside functions, where its lifetime is one call. Globals arrive later.

## 18. Build and Run

```bash
cmake -S . -B build \
  -DLLVM_DIR="$(llvm-config --cmakedir)"
cmake --build build
./build/pyxc
```

Enter the accumulator, ending the REPL block with a blank line:

```pyxc
ready> def sum_to(n):
    var acc = 0
    for var i = 1, i <= n, i = i + 1:
        acc = acc + i
    return acc

ready> sum_to(5)
```

Expected:

```text
Parsed a function definition.
Parsed a top-level expression.
Evaluated to 15.000000
```

Run the suite:

```bash
llvm-lit -v test/
```

Pay particular attention to indentation, multi-level dedent, nested blocks, inline suites, optional `else`, returns, block scoping, undeclared assignment, implicit return, and EOF with an open block.

What you built is the statement pipeline:

```text
indentation -> INDENT/DEDENT
suite       -> one statement or block
block       -> ordered statements
return      -> terminator
fallthrough -> implicit 0.0
```

Next: [Chapter 13](chapter-13.md) adds more loop control on top of statement blocks.

## Need Help?

Build issues? Questions?

- [Report a problem with GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- [Ask a question in GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your operating system and version
- The chapter number
- The exact command you ran
- The complete error message
- The output of `c++ --version` and `cmake --version`
- The output of `llvm-config --version` for Chapter 6 and later

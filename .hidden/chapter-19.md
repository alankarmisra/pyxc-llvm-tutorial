---
description: "Use indentation tokens to parse real statement blocks, add elif and optional else behavior, and tighten branching codegen for clearer control flow."
---
# 19. Pyxc: Blocks, elif, Optional else, and Benchmarking

If Chapter 15 gave us indentation tokens, Chapter 16 is where we finally cash that check.

Until now, we could *lex* indentation. In this chapter, we use that structure to parse real statement blocks, support `elif`, make `else` optional, and tighten codegen so branch-heavy code doesn’t generate weird IR.

> Note from the future:
> This chapter still carries one Kaleidoscope legacy semantic: boolean/comparison-style results in parts of codegen are represented via floating values (`0.0` / `1.0`) instead of integer truth values.
> We *should* have cleaned that up around here, but we didn’t.
> We finally fixed it in Chapter 25 when we revisited signed/unsigned and truth-value correctness.
> We could pretend this was a grand long-term plan, but realistically we were lazy programmers optimizing for forward progress.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter16](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter16).

## What We’re Building

By the end of this chapter, `pyxc` supports:

- statement suites (`suite`) as real block bodies
- `if / elif / else` chains
- optional `else`
- safer control-flow codegen for early returns
- interpreter vs executable handling for `main`
- a benchmark harness against Python

Not bad for one chapter.

## Build Setup (same idea as Chapter 15)

If you built Chapter 15, this should look familiar. Chapter 16 keeps the same Makefile shape and toolchain assumptions.

From `code/chapter16/Makefile`:

```make
TARGET := pyxc
SRC := pyxc.cpp
RUNTIME_SRC := runtime.c
RUNTIME_OBJ := runtime.o

all: $(TARGET) $(RUNTIME_TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< $(LLVM_FLAGS) $(LDFLAGS) $(LLD_FLAGS) -o $@

$(RUNTIME_OBJ): $(RUNTIME_SRC)
	$(CC) $(CFLAGS) -c $< -o $@
```

Build and smoke test:

```bash
cd code/chapter16 && ./build.sh
./pyxc -t test/if_elif_optional.pyxc
./pyxc -i test/showcase_tools.pyxc
```

## Grammar Target (EBNF)

This is the exact syntax shape we’re aiming for. Writing it as EBNF keeps parser decisions crisp and prevents “I think this should parse” drift.

```ebnf
program         = { top_level , newline } ;
top_level       = definition | extern | expression ;

definition      = { decorator } , "def" , prototype , ":" , suite ;
extern          = "extern" , "def" , prototype ;

suite           = inline_suite | block_suite ;
inline_suite    = statement ;
block_suite     = newline , indent , statement_list , dedent ;
statement_list  = statement , { newline , statement } , [ newline ] ;

statement       = if_stmt | for_stmt | return_stmt | expr_stmt ;
if_stmt         = "if" , expression , ":" , suite ,
                  { "elif" , expression , ":" , suite } ,
                  [ "else" , ":" , suite ] ;
for_stmt        = "for" , identifier , "in" , "range" , "(" ,
                  expression , "," , expression , [ "," , expression ] , ")" ,
                  ":" , suite ;
return_stmt     = "return" , expression ;
expr_stmt       = expression ;
```

## Lexer Update: Teach It elif

Before parser work, the lexer must recognize `elif` as a keyword.

```cpp
enum Token {
  tok_eof = -1,
  tok_eol = -2,
  tok_error = -3,

  tok_def = -4,
  tok_extern = -5,
  tok_identifier = -6,
  tok_number = -7,

  tok_if = -8,
  tok_elif = -9,
  tok_else = -10,
  tok_return = -11,

  tok_for = -12,
  tok_in = -13,
  tok_range = -14,
  tok_decorator = -15,
  tok_var = -16,

  tok_indent = -17,
  tok_dedent = -18,
};

static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},   {"elif", tok_elif},     {"else", tok_else},
    {"for", tok_for}, {"in", tok_in},         {"range", tok_range},
    {"var", tok_var}};
```

Two subtle wins here:

- We get `elif` support directly in the token stream.
  Meaning: the lexer emits `tok_elif` (a dedicated token), instead of treating `elif` like a regular identifier.
- Token IDs are grouped cleanly by category, which helps while debugging parser traces.
  Meaning: when you print raw `CurTok` numbers while stepping through parser code, nearby values tend to belong to related syntax groups (`if/elif/else/return`, loop tokens, etc.). That makes parser state dumps easier to read.

## From Indentation Tokens to Real Blocks

Chapter 15 gave us `indent`/`dedent`. Chapter 16 turns those into a suite AST.

`suite` is essentially a block of statements.  
We use the name `suite` because Python grammar uses that term for:

- a single inline statement after `:`
- or a newline + indented statement list

### ParseBlockSuite()

```cpp
static std::unique_ptr<BlockSuiteAST> ParseBlockSuite() {
  auto BlockLoc = CurLoc;
  if (CurTok != tok_eol)
    return LogError<std::unique_ptr<BlockSuiteAST>>("Expected newline");

  if (getNextToken() != tok_indent)
    return LogError<std::unique_ptr<BlockSuiteAST>>("Expected indent");
  getNextToken(); // eat indent

  auto Stmts = ParseStatementList();
  if (Stmts.empty())
    return nullptr;

  getNextToken(); // eat dedent
  return std::make_unique<BlockSuiteAST>(BlockLoc, std::move(Stmts));
}
```

### ParseSuite()

```cpp
static std::unique_ptr<BlockSuiteAST> ParseSuite() {
  if (CurTok == tok_eol)
    return ParseBlockSuite();
  return ParseInlineSuite();
}
```

This is one of those deceptively small changes that unlocks half the chapter.

## if / elif / else, with Optional else

Here’s the heart of the parser work.

```cpp
static std::unique_ptr<StmtAST> ParseIfStmt() {
  SourceLocation IfLoc = CurLoc;
  if (CurTok != tok_if && CurTok != tok_elif)
    return LogError<StmtPtr>("expected `if`/`elif`");
  getNextToken();

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError<std::unique_ptr<StmtAST>>("expected `:`");
  getNextToken();

  auto Then = ParseSuite();
  if (!Then)
    return nullptr;

  std::unique_ptr<BlockSuiteAST> Else;
  if (CurTok == tok_elif) {
    auto ElseIfStmt = ParseIfStmt();
    if (!ElseIfStmt)
      return nullptr;
    std::vector<StmtPtr> ElseStmts;
    ElseStmts.push_back(std::move(ElseIfStmt));
    Else = std::make_unique<BlockSuiteAST>(IfLoc, std::move(ElseStmts));
  } else if (CurTok == tok_else) {
    getNextToken();
    if (CurTok != ':')
      return LogError<std::unique_ptr<StmtAST>>("expected `:`");
    getNextToken();
    Else = ParseSuite();
    if (!Else)
      return nullptr;
  }

  return std::make_unique<IfStmtAST>(IfLoc, std::move(Cond), std::move(Then),
                                     std::move(Else));
}
```

Also important: guardrails for stray branches.

```cpp
case tok_elif:
  return LogError<StmtPtr>("Unexpected `elif` without matching `if`");
case tok_else:
  return LogError<StmtPtr>("Unexpected `else` without matching `if`");
```

Why this shape works well:

- `elif` is represented as “`else` containing another `if`”.
- optional `else` means `Else` can be `nullptr`.
- this keeps the AST simple and codegen predictable.

## Codegen Fixes That Matter More Than They Look

If parser support expands and codegen doesn’t evolve, the compiler *seems* fine until it really isn’t.

### IfStmtAST::codegen() handles terminated branches

```cpp
bool ThenTerminated = Builder->GetInsertBlock()->getTerminator() != nullptr;
if (!ThenTerminated)
  Builder->CreateBr(MergeBB);

Value *ElseV = nullptr;
bool ElseTerminated = false;
if (Else) {
  ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;
  ElseTerminated = Builder->GetInsertBlock()->getTerminator() != nullptr;
} else {
  ElseV = ConstantFP::get(*TheContext, APFloat(0.0));
}
if (!ElseTerminated)
  Builder->CreateBr(MergeBB);

if (ThenTerminated && ElseTerminated) {
  BasicBlock *DeadCont =
      BasicBlock::Create(*TheContext, "ifcont.dead", TheFunction);
  Builder->SetInsertPoint(DeadCont);
  return ConstantFP::get(*TheContext, APFloat(0.0));
}
```

Here, “terminated branch” means a branch that already ends with something final like `ret`.

Bad shape (what we want to avoid):

```llvm
then:
  ret double 1.0
  br label %merge   ; invalid: jump after return
```

Good shape:

```llvm
then:
  ret double 1.0

else:
  br label %merge
```

Why:

- prevents generating jump instructions after a `return`
- avoids malformed IR in branch-heavy functions

### Return + function finalization

```cpp
Value *ReturnStmtAST::codegen() {
  Value *RetVal = Expr->codegen();
  ...
  if (ExpectedTy->isIntegerTy(32) && RetVal->getType()->isDoubleTy())
    RetVal = Builder->CreateFPToSI(RetVal, ExpectedTy, "ret_i32");
  Builder->CreateRet(RetVal);
  return RetVal;
}
```

```cpp
if (Value *RetVal = Body->codegen()) {
  if (!Builder->GetInsertBlock()->getTerminator()) {
    ...
    Builder->CreateRet(RetVal);
  }
  verifyFunction(*TheFunction);
  ...
}
```

`getTerminator()` is an LLVM API on a basic block.  
We use it to ask: “is this block already finished?”

- If yes, do not emit another `ret`/`br`.
- If no, emit the final return.

That is how we avoid duplicate final `ret` instructions, and it also keeps return typing logic in one place.

(And yes, this is one of those C++ compiler spots where one extra `CreateRet` can ruin your afternoon in under 30 seconds.)

## Interpreter vs Executable main

Chapter 16 introduces mode-aware `main` handling:

```cpp
static bool UseCMainSignature = false;
```

- `InterpretFile(...)` and `REPL()` keep it `false`.
- `CompileToObjectFile(...)` sets it `true`.

Why:

- JIT/interpreter paths want language-level function signatures.
- executable/object mode still needs native entrypoint behavior.

## Benchmarking (Python vs pyxc)

We added a benchmark suite under:

- `code/chapter16/bench/run_suite.sh`
- `code/chapter16/bench/cases/*.py`
- `code/chapter16/bench/cases/*.pyxc`

Run it:

```bash
cd code/chapter16
bench/run_suite.sh 3
```

Current averages (seconds, 2 decimals):

- `fib(41)`: Python `11.66`, `pyxc -i` `0.46`, `pyxc exe` `0.44`
- `loopsum(10000,10000)`: Python `3.39`, `pyxc -i` `0.15`, `pyxc exe` `0.10`
- `primecount(1900) x 10`: Python `1.22`, `pyxc -i` `0.17`, `pyxc exe` `0.15`

Overall case-average:

- Python: `5.42s`
- `pyxc -i`: `0.26s`
- `pyxc executable`: `0.23s`

Repo:

- [https://github.com/alankarmisra/pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial)

If something fails locally, open an issue with:

- OS/toolchain details
- exact command
- full stderr output

## Compiling
```bash
cd code/chapter16 && ./build.sh
```

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
cd code/chapter16 && ./build.sh
```

Run one sample program:

```bash
code/chapter16/pyxc -i code/chapter16/test/blocks_bad_indent.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter16/test
lit -sv .
```

Explore the test folder a bit and add one tiny edge case of your own.

When you're done, clean artifacts:

```bash
cd code/chapter16 && ./build.sh
```


## Need Help?

Stuck on something? Have questions about this chapter? Found an error?

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report bugs, errors, or problems
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- The chapter you're working on
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message or unexpected behavior
- What you've already tried

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!

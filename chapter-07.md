# 7. Pyxc: Extending the Language: Control Flow

!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter07](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter07).

## Introduction
Welcome to Chapter 7 of the [Implementing a language with LLVM](chapter-00.md) tutorial. Parts 1-4 described the implementation of the simple Pyxc language and included support for generating LLVM IR, followed by optimizations and a JIT compiler. Unfortunately, as presented, Pyxc is mostly useless: it has no control flow other than call and return. This means that you can’t have conditional branches in the code, significantly limiting its power. In this episode of “build that compiler”, we’ll extend Pyxc to have an if/then/else expression plus a simple ‘for’ loop.

## If/Then/Else
Extending Pyxc to support if/then/else is quite straightforward. It basically requires adding support for this “new” concept to the lexer, parser, AST, and LLVM code emitter. This example is nice, because it shows how easy it is to “grow” a language over time, incrementally extending it as new ideas are discovered.

Before we get going on “how” we add this extension, let’s talk about “what” we want. The basic idea is that we want to be able to write this sort of thing:

```python
def fib(x):
    if x < 3:
        return 1
    else:
        return fib(x-1) + fib(x-2)        
```

!!!note At this stage, we do not enforce Python-style indentation rules. We will introduce proper indentation handling in a later chapter, once you are more familiar with the LLVM workflow. While this is not particularly difficult to implement, it would be an unnecessary distraction at this point, so we intentionally omit it for now.

In Pyxc, every construct is an expression: there are no statements. As such, the if/then/else expression needs to return a value like any other. Since we’re using a mostly functional form, we’ll have it evaluate its conditional, then return the ‘then’ or ‘else’ value based on how the condition was resolved. This is very similar to the C “?:” expression.

The semantics of the if/then/else expression is that it evaluates the condition to a boolean equality value: 0.0 is considered to be false and everything else is considered to be true. If the condition is true, the first subexpression is evaluated and returned, if the condition is false, the second subexpression is evaluated and returned. Since Pyxc allows side-effects, this behavior is important to nail down.

Now that we know what we “want”, let’s break this down into its constituent pieces.

## Lexer Extensions for if/else
The lexer extensions are straightforward. First we add new enum values for the relevant tokens:

Notice, we have no `then` keyword in Pyxc so we need only tokens for `if` and `else`.

```cpp
// control
tok_if = -7,
tok_else = -8,
```

Once we have that, we recognize the new keywords in the lexer. This is pretty simple stuff:

```cpp
static std::map<std::string, Token> Keywords = {{"def", tok_def},
                                                {"extern", tok_extern},
                                                {"return", tok_return},
                                                {"if", tok_if}, // <-- add this
                                                {"else", tok_else}}; // <-- add this

```

## AST Extensions for If/Then/Else
To represent the new expression we add a new AST node for it:

```cpp
/// IfExprAST - Expression class for if/then/else.
class IfExprAST : public ExprAST {
  std::unique_ptr<ExprAST> Cond, Then, Else;

public:
  IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
            std::unique_ptr<ExprAST> Else)
    : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

  Value *codegen() override;
};
```

The AST node just has pointers to the various subexpressions.

Now that we have the relevant tokens coming from the lexer and we have the AST node to build, our parsing logic is relatively straightforward. 

As we discussed earlier, placing an entire if/then/else expression on a single line quickly becomes hard to read. To improve clarity, we add support for splitting the expression across multiple lines. Each sub-expression, however, must still remain on a single line; otherwise, the parser will report an error.

Since this requires us to consume newline tokens more frequently, we introduce a small helper function.

```cpp
/*
 * In later chapters, we will need to check the indentation 
 * whenever we eat new lines. 
 */
static void EatNewLines() {
  while (CurTok == tok_eol)
    getNextToken();
}

```

Next we define a new parsing function:

```cpp
// ifexpr ::= 'if' expression ':' expression 'else' ':' expression
static std::unique_ptr<ExprAST> ParseIfExpr() {
  getNextToken(); // eat 'if'

  // condition
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != ':')
    return LogError("expected `:`");
  getNextToken(); // eat ':'

  EatNewLines();

// Handle nested `if`expressions: `return` statements are emitted inside the
// true and false branches, so we only require an explicit `return`
// when we are not parsing an `if`.
  if (CurTok != tok_if) {
    if (CurTok != tok_return)
      return LogError("Expected 'return'");

    getNextToken(); // eat return
  }
  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  EatNewLines();

  if (CurTok != tok_else)
    return LogError("expected `else`");

  getNextToken(); // eat else

  if (CurTok != ':')
    return LogError("expected `:`");

  getNextToken(); // eat ':'

  EatNewLines();

  if (CurTok != tok_if) {
    if (CurTok != tok_return)
      return LogError("Expected 'return'");

    getNextToken(); // eat return
  }

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then),
                                     std::move(Else));
}
```

Notice how we have to account for nested `if` statements. Our parser expects a `return` before expressions unless it's an `if` expression. We have to make a similar edit in `ParseDefinition`.

```cpp
static std::unique_ptr<FunctionAST> ParseDefinition() {
 ...
  while (CurTok == tok_eol)
    getNextToken();

  if (CurTok != tok_if) { // <-- ADD THIS
    if (CurTok != tok_return)
      return LogErrorF("Expected 'return' before return expression");
    getNextToken(); // eat return
  }

 ...
}
```

Next we hook it up as a primary expression:

```cpp
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError("unknown token when expecting an expression");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  case tok_if:
    return ParseIfExpr(); // <-- ADD THIS
  }
}
```

## LLVM IR for the if Expression
Now that we have it parsing and building the AST, the final piece is adding LLVM code generation support. This is the most interesting part of the if/then/else example, because this is where it starts to introduce new concepts. All of the code above has been thoroughly described in previous chapters.

Internally, we still refer to the true and false branches as the “then” and “else” blocks, even though 'then' is *not* a keyword in pyxc.

To motivate the code we want to produce, let’s take a look at a simple example. Consider:
```python
extern def foo()
extern def bar()

def baz(x):
    if x:
        foo()
    else:
        bar()
```

If you disable optimizations, the code you’ll (soon) get from Pyxc looks like this:

```cpp
declare double @foo()

declare double @bar()

define double @baz(double %x) {
entry:
  %ifcond = fcmp one double %x, 0.000000e+00
  br i1 %ifcond, label %then, label %else

then:       ; preds = %entry
  %calltmp = call double @foo()
  br label %ifcont

else:       ; preds = %entry
  %calltmp1 = call double @bar()
  br label %ifcont

ifcont:     ; preds = %else, %then
  %iftmp = phi double [ %calltmp, %then ], [ %calltmp1, %else ]
  ret double %iftmp
}
```

To visualize the control flow graph, you can use a nifty feature of the LLVM [opt](https://llvm.org/cmds/opt.html) tool. If you put this LLVM IR into “t.ll” and run `llvm-as < t.ll | opt -passes=view-cfg`, [a window will pop up](https://llvm.org/docs/ProgrammersManual.html#viewing-graphs-while-debugging-code) and you’ll see a graph similar to this:

<p align="center">
  <img src="phi.jpg" width="600">
</p>

Another way to get this is to call “F->viewCFG()” or “F->viewCFGOnly()” (where F is a “Function*”) either by inserting actual calls into the code and recompiling or by calling these in the debugger. LLVM has many nice features for visualizing various graphs.

Getting back to the generated code, it is fairly simple: the entry block evaluates the conditional expression (“x” in our case here) and compares the result to 0.0 with the “fcmp one” instruction (‘one’ is “Ordered and Not Equal”). Based on the result of this expression, the code jumps to either the “then” or “else” blocks, which contain the expressions for the true/false cases.

Once the true and false branches are finished executing, they both branch back to the ‘ifcont’ block to execute the code that happens after the if/then/else. In this case the only thing left to do is to return to the caller of the function. The question then becomes: how does the code know which expression to return?

The answer to this question involves an important SSA operation: the Phi operation. If you’re not familiar with SSA, the wikipedia article is a good introduction and there are various other introductions to it available on your favorite search engine. The short version is that “execution” of the Phi operation requires “remembering” which block control came from. The Phi operation takes on the value corresponding to the input control block. In this case, if control comes in from the “then” block, it gets the value of “calltmp”. If control comes from the “else” block, it gets the value of “calltmp1”.

At this point, you are probably starting to think “Oh no! This means my simple and elegant front-end will have to start generating SSA form in order to use LLVM!”. Fortunately, this is not the case, and we strongly advise not implementing an SSA construction algorithm in your front-end unless there is an amazingly good reason to do so. In practice, there are two sorts of values that float around in code written for your average imperative programming language that might need Phi nodes:

Code that involves user variables: x = 1; x = x + 1;

Values that are implicit in the structure of your AST, such as the Phi node in this case.

In Chapter 8 of this tutorial (“mutable variables”), we’ll talk about #1 in depth. For now, just believe me that you don’t need SSA construction to handle this case. For #2, you have the choice of using the techniques that we will describe for #1, or you can insert Phi nodes directly, if convenient. In this case, it is really easy to generate the Phi node, so we choose to do it directly.

Okay, enough of the motivation and overview, let’s generate code!

## Code Generation for If/Then/Else

In order to generate code for this, we implement the codegen method for IfExprAST:

```cpp
Value *IfExprAST::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");
```

This code is straightforward and similar to what we saw before. We emit the expression for the condition, then compare that value to zero to get a truth value as a 1-bit (bool) value.

```cpp
Function *TheFunction = Builder->GetInsertBlock()->getParent();

// Create blocks for the then and else cases.  Insert the 'then' block at the
// end of the function.
BasicBlock *ThenBB =
    BasicBlock::Create(*TheContext, "then", TheFunction);
BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

Builder->CreateCondBr(CondV, ThenBB, ElseBB);
```

This code creates the basic blocks that are related to the if/then/else statement, and correspond directly to the blocks in the example above. The first line gets the current Function object that is being built. It gets this by asking the builder for the current BasicBlock, and asking that block for its “parent” (the function it is currently embedded into).

Once it has that, it creates three blocks. Note that it passes “TheFunction” into the constructor for the “then” block. This causes the constructor to automatically insert the new block into the end of the specified function. The other two blocks are created, but aren’t yet inserted into the function.

Once the blocks are created, we can emit the conditional branch that chooses between them. Note that creating new blocks does not implicitly affect the IRBuilder, so it is still inserting into the block that the condition went into. Also note that it is creating a branch to the “then” block and the “else” block, even though the “else” block isn’t inserted into the function yet. This is all ok: it is the standard way that LLVM supports forward references.

```cpp
// Emit then value.
Builder->SetInsertPoint(ThenBB);

Value *ThenV = Then->codegen();
if (!ThenV)
  return nullptr;

Builder->CreateBr(MergeBB);
// Codegen of 'Then' can change the current block, update ThenBB for the PHI.
ThenBB = Builder->GetInsertBlock();
```

After the conditional branch is inserted, we move the builder to start inserting into the “then” block. Strictly speaking, this call moves the insertion point to be at the end of the specified block. However, since the “then” block is empty, it also starts out by inserting at the beginning of the block. :)

Once the insertion point is set, we recursively codegen the “then” expression from the AST. To finish off the “then” block, we create an unconditional branch to the merge block. One interesting (and very important) aspect of the LLVM IR is that it [requires all basic blocks to be “terminated”](https://llvm.org/docs/LangRef.html#functionstructure) with a [control flow instruction](https://llvm.org/docs/LangRef.html#terminators) such as return or branch. This means that all control flow, *including fall throughs* must be made explicit in the LLVM IR. If you violate this rule, the verifier will emit an error.

The final line here is quite subtle, but is very important. The basic issue is that when we create the Phi node in the merge block, we need to set up the block/value pairs that indicate how the Phi will work. Importantly, the Phi node expects to have an entry for each predecessor of the block in the CFG. Why then, are we getting the current block when we just set it to ThenBB 5 lines above? The problem is that the “Then” expression may actually itself change the block that the Builder is emitting into if, for example, it contains a nested “if/then/else” expression. Because calling codegen() recursively could arbitrarily change the notion of the current block, we are required to get an up-to-date value for code that will set up the Phi node.

```cpp
// Emit else block.
TheFunction->insert(TheFunction->end(), ElseBB);
Builder->SetInsertPoint(ElseBB);

Value *ElseV = Else->codegen();
if (!ElseV)
  return nullptr;

Builder->CreateBr(MergeBB);
// codegen of 'Else' can change the current block, update ElseBB for the PHI.
ElseBB = Builder->GetInsertBlock();
```

Code generation for the ‘else’ block is basically identical to codegen for the ‘then’ block. The only significant difference is the first line, which adds the ‘else’ block to the function. Recall previously that the ‘else’ block was created, but not added to the function. Now that the ‘then’ and ‘else’ blocks are emitted, we can finish up with the merge code:

```cpp
  // Emit merge block.
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN =
    Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");

  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}
```

The first two lines here are now familiar: the first adds the “merge” block to the Function object (it was previously floating, like the else block above). The second changes the insertion point so that newly created code will go into the “merge” block. Once that is done, we need to create the PHI node and set up the block/value pairs for the PHI.

Finally, the CodeGen function returns the phi node as the value computed by the if/then/else expression. In our example above, this returned value will feed into the code for the top-level function, which will create the return instruction.

Overall, we now have the ability to execute conditional code in Pyxc. With this extension, Pyxc is a fairly complete language that can calculate a wide variety of numeric functions. Next up we’ll add another useful expression that is familiar from non-functional languages…

## A Python-style for Expression

Now that we know how to add basic control flow constructs to the language, we have the tools to add more powerful things. Let’s add something more aggressive, a ‘for’ expression. To keep the syntax familiar and Pythonic, we initially pair 'for' with a 'range' operator. In later chapters, we will decouple the two and allow 'for' to iterate over any iterable.

```python
extern def putchard(x)

def printstar(n):
  for i in range(1, n+1, 1):
    # Notice we don't add `return`. 
    # `for` expressions return 0 by default.
    putchard(42)  # ascii 42 = '*'

# print 100 '*' characters
printstar(100)     
```

This expression introduces a new loop variable (i in this case) and iterates over the values produced by range. The range operator takes a starting value, an end value, and an optional step. Iteration begins at the starting value and continues up to, but not including, the end value, advancing by the step on each iteration. If the step is omitted, it defaults to 1.0.

On each iteration, the body expression is evaluated. Since the language does not yet support mutable variables or meaningful loop results, the for expression itself is defined to always return 0.0. This will become more useful once mutation is introduced in later chapters.

As before, let’s talk about the changes that we need to Pyxc to support this.

# Lexer Extensions for the ‘for’ Loop
The lexer extensions are the same sort of thing as for the 'if' expression.

```cpp
... in enum Token ...
  // loop
  tok_for = -10,
  tok_in = -11,
  tok_range = -12,

... modify the keywords variable ...
static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},   {"else", tok_else},     {"for", tok_for},
    {"in", tok_in},   {"range", tok_range}};
```

## AST Extensions for the 'for' Loop
The AST node is just as simple. It basically boils down to capturing the variable name and the constituent expressions in the node.

```cpp
/// ForExprAST - Expression class for for/in.
class ForExprAST : public ExprAST {
  std::string VarName;
  std::unique_ptr<ExprAST> Start, End, Step, Body;

public:
  ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
             std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
             std::unique_ptr<ExprAST> Body)
    : VarName(VarName), Start(std::move(Start)), End(std::move(End)),
      Step(std::move(Step)), Body(std::move(Body)) {}

  Value *codegen() override;
};
```

## Parser Extensions for the ‘for’ Loop
Since `for` expressions return a default value of 0, we shouldn't expect the `return` keyword within the subexpressions. To do this, we define a variable to know if we are in a `for` expression, and skip the `return` test. 

```cpp
...
static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number
static bool InForExpression;      // Track global parsing context <-- ADD THIS
...
```

If we encounter an error while parsing a `for` expression or one of its subexpressions, `InForExpression` needs to be reset to `false` because we will skip parsing the rest of the expression. The most convenient place to put the reset code is `LogError` which gets called directly or indirectly each time we encounter an error. 

```cpp
/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
  InForExpression = false; // <-- ADD THIS
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}
```

Next we parse the expression. The only interesting thing here is handling of the optional step value. The parser code handles it by checking to see if the second comma is present. If not, it sets the step value to null in the AST node. We also reset InForExpression to `false` at the end of ParseForExpr when parsing completes successfully.

```cpp
static std::unique_ptr<ExprAST> ParseForExpr() {
  InForExpression = true;
  getNextToken(); // eat for

  if (CurTok != tok_identifier)
    return LogError("Expected identifier after for");

  std::string IdName = IdentifierStr;
  getNextToken(); // eat identifier

  if (CurTok != tok_in)
    return LogError("Expected `in` after identifier in for");
  getNextToken(); // eat 'in'

  if (CurTok != tok_range) // Range is a function actually but for our purposes
                           // we use it as a keyword
    return LogError("Expected `range` after identifier in for");
  getNextToken(); // eat range

  if (CurTok != '(')
    return LogError("Expected `(` after `range` in for");
  getNextToken(); // eat '('

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;

  if (CurTok != ',')
    return LogError("expected `,` after range start");
  getNextToken(); // eat ','

  auto End = ParseExpression();
  if (!End)
    return nullptr;
  std::unique_ptr<ExprAST> Step;
  if (CurTok == ',') {
    getNextToken(); // eat ,
    Step = ParseExpression();
    if (!Step)
      return nullptr;
  }

  if (CurTok != ')')
    return LogError("expected `)` after range operator");
  getNextToken(); // eat `)`

  if (CurTok != ':')
    return LogError("expected `:` after range operator");
  getNextToken(); // eat `:`

  EatNewLines();

  // for expressions don't have the return statement
  // they return 0 by default.

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  InForExpression = false;

  return std::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End),
                                      std::move(Step), std::move(Body));
}
```

We also need to skip `return` statements before nested `for` statements like we did for `if`.

```cpp
static std::unique_ptr<ExprAST> ParseIfExpr() {
  ...
  // Handle nested `if` and `for` expressions:
  // For `if` expressions, `return` statements are emitted inside the
  // true and false branches, so we only require an explicit `return`
  // when we are not parsing an `if`.
  // For `for` expressions, control flow and the resulting value are
  // handled entirely within the loop body, so an explicit `return`
  // is not required at this level.
  if (!InForExpression && CurTok != tok_if && CurTok != tok_for) { // <-- EXTEND THIS
    if (CurTok != tok_return)
      return LogError("Expected 'return'");

    getNextToken(); // eat return
  }
  
  ...

  if (CurTok != tok_else)
    return LogError("expected `else`");

  getNextToken(); // eat else

  if (CurTok != ':')
    return LogError("expected `:`");

  getNextToken(); // eat ':'

  EatNewLines();
  
  if (!InForExpression && CurTok != tok_if && CurTok != tok_for) { // <-- EXTEND THIS
    if (CurTok != tok_return)
      return LogError("Expected 'return'");

    getNextToken(); // eat return
  }

  auto Else = ParseExpression();
  ...
}
```

Similarly, we need to update `ParseDefinition`:

```cpp
/// definition ::= 'def' prototype ':' expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat def.

  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");
  getNextToken(); // eat ':'

  // Consume newlines between a function header and its body.
  // This allows definitions to be split across multiple lines, e.g.:
  //
  // ready> def foo(x):
  // ready>   return x + 1
  EatNewLines();

  if (!InForExpression && CurTok != tok_if && CurTok != tok_for) { // <-- EXTEND THIS
    if (CurTok != tok_return)
      return LogErrorF("Expected 'return' before return expression");
    getNextToken(); // eat return
  }

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}
```

And again we hook it up as a primary expression:

```cpp
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError("unknown token when expecting an expression");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  case tok_if:
    return ParseIfExpr();
  case tok_for:
    return ParseForExpr();
  }
}
```

## LLVM IR for the ‘for’ Loop
Now we get to the good part: the LLVM IR we want to generate for this thing. With the simple example above, we get this LLVM IR (note that this dump is generated with optimizations disabled for clarity):

```cpp
define double @printstar(double %n) {
entry:
  br label %loopcond

loopcond:                                         ; preds = %loop, %entry
  %i = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop ]
  %addtmp = fadd double %n, 1.000000e+00
  %endcond = fcmp ult double %i, %addtmp
  br i1 %endcond, label %loop, label %endloop

loop:                                             ; preds = %loopcond
  %calltmp = call double @putchard(double 4.200000e+01)
  %nextvar = fadd double %i, 1.000000e+00
  br label %loopcond

endloop:                                          ; preds = %loopcond
  ret double 0.000000e+00
}
```

This loop contains all the same constructs we saw before: a phi node, several expressions, and some basic blocks. Let’s see how this fits together.

## Code Generation for the 'for' Loop
The first part of codegen is very simple: we just output the start expression for the loop value:

```cpp
Value *ForExprAST::codegen() {
  // Emit the start code first, without 'variable' in scope.
  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;
```

With this out of the way, the next step is to set up the LLVM basic block for the start of the loop body. In the case above, the whole loop body is one block, but remember that the body code itself could consist of multiple blocks (e.g. if it contains an if/then/else or a for/in expression).

```cpp
// Make new basic blocks for pre-loop, loop condition, loop body and
// end-loop code.
Function *TheFunction = Builder->GetInsertBlock()->getParent();
BasicBlock *PreLoopBB = Builder->GetInsertBlock();
BasicBlock *LoopConditionBB =
    BasicBlock::Create(*TheContext, "loopcond", TheFunction);
BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop");
BasicBlock *EndLoopBB = BasicBlock::Create(*TheContext, "endloop");

// Insert an explicit fall through from current block to LoopConditionBB.
Builder->CreateBr(LoopConditionBB);
```

This code is similar to what we saw for if/then/else. Because we will need it to create the Phi node, we remember the block that falls through into the loop condition check. Once we have that, we create the actual block that determines if control should enter the loop and attach it directly to the end of our parent function. The other two blocks (`LoopBB` and `EndLoopBB`) are created, but aren't inserted into the function, similarly to our previous work on if/then/else. These will be used to complete our loop IR later on.

We also create an unconditional branch into the loop condition block from the pre-loop code.

```cpp
// Start insertion in LoopConditionBB.
Builder->SetInsertPoint(LoopConditionBB);

// Start the PHI node with an entry for Start.
PHINode *Variable =
      Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
Variable->addIncoming(StartVal, PreLoopBB);
```
Now that the "preheader" for the loop is set up, we switch to emitting code for the loop condition check. To begin with, we move the insertion point and create the Phi node for the loop induction variable. Since we already know the incoming value for the starting value, we add it to the Phi node. Note that the Phi node will eventually get a second value for the backedge, but we can't set it up yet (because it doesn't exist!).

```cpp
// Within the loop, the variable is defined equal to the PHI node.  If it
// shadows an existing variable, we have to restore it, so save it now.
Value *OldVal = NamedValues[VarName];
NamedValues[VarName] = Variable;
// Compute the end condition.
Value *EndCond = End->codegen();
if (!EndCond)
    return nullptr;
```

Our for-loop introduces a new variable to the symbol table. This means that our symbol table can now contain either function arguments or loop variables. To handle this, before we codegen the remainder of the loop, we add the loop variable as the current value for its name. Note that it is possible there is a variable of the same name in the outer scope. It would be easy to make this an error (emit an error and return null if there is already an entry for VarName) but we choose to allow shadowing of variables. In order to handle this correctly, we remember the Value that we are potentially shadowing in OldVal (which will be null if there is no shadowed variable). This allows the loop body (which we will codegen soon) to use the loop variable: any references to it will naturally find it in the symbol table.

Once the loop variable is set into the symbol table, we codegen the condition that determines if we can enter into the loop (or continue looping, depending on if we are arriving from the "preheader" or the loop body).

```cpp
// Check if Variable < End
EndCond = Builder->CreateFCmpULT(Variable, EndCond, "endcond");

// Insert the conditional branch that either continues the loop, or exits
// the loop.
Builder->CreateCondBr(EndCond, LoopBB, EndLoopBB);
```

As with if/then/else, after emitting the condition, we compare that value to zero to get a truth value as a 1-bit (bool) value. Next we emit the conditional branch that chooses if we enter the loop body, or move on to the post-loop code.

```cpp
// Attach the basic block that will soon hold the loop body to the end of
// the parent function.
TheFunction->insert(TheFunction->end(), LoopBB);

// Emit the loop body within the LoopBB. This, like any other expr, can
// change the current BB. Note that we ignore the value computed by the
// body, but don't allow an error.
Builder->SetInsertPoint(LoopBB);
if (!Body->codegen()) {
return nullptr;
}
```
Next, we insert our basic block that will soon hold the loop body to the end of the specified function. We recursively codegen the body, remembering to update the IRBuilder's insert point to the basic block that is supposed to hold the loop body beforehand.

```cpp
// Emit the step value.
Value *StepVal = nullptr;
if (Step) {
  StepVal = Step->codegen();
  if (!StepVal)
    return nullptr;
} else {
  // If not specified, use 1.0.
  StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
}

Value *NextVar = Builder->CreateFAdd(Variable, StepVal, "nextvar");
```

Now that the body is emitted, we compute the next value of the iteration variable by adding the step value, or 1.0 if it isn't present. NextVar will be the value of the loop variable on the next iteration of the loop.

```cpp
// Add a new entry to the PHI node for the backedge.
LoopBB = Builder->GetInsertBlock();
Variable->addIncoming(NextVar, LoopBB);

// Create the unconditional branch that returns to LoopConditionBB to
// determine if we should continue looping.
Builder->CreateBr(LoopConditionBB);
```
Here, similarly to how we did it in our implementation of the if/then/else statement, we get an up-to-date value for LoopBB (because it's possible that the loop body has changed the basic block that the Builder is emitting into) and use it to add a backedge to our Phi node. This backedge denotes the value of the incremented loop variable. We also create an unconditional branch back to the basic block that performs the check if we should continue looping. This completes the loop body code.

```cpp
// Append EndLoopBB after the loop body. We go to this basic block if the
// loop condition says we should not loop anymore.
TheFunction->insert(TheFunction->end(), EndLoopBB);

// Any new code will be inserted after the loop.
Builder->SetInsertPoint(EndLoopBB);
```

```cpp
// Create the "after loop" block and insert it.
BasicBlock *LoopEndBB = Builder->GetInsertBlock();
BasicBlock *AfterBB =
    BasicBlock::Create(*TheContext, "afterloop", TheFunction);

// Insert the conditional branch into the end of LoopEndBB.
Builder->CreateCondBr(EndCond, LoopBB, AfterBB);

// Any new code will be inserted in AfterBB.
Builder->SetInsertPoint(AfterBB);
```

Finally, we append the post-loop basic block created earlier (denoted by EndLoopBB) to the parent function, and update the IRBuilder's insert point such that any new subsequent code is generated in that post-loop basic block.

```cpp
  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for expr always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}
```

The final bit of code handles some clean-ups: We remove the loop variable from the symbol table so that it isn't in scope after the for-loop, and return a 0.0 value.

With these changes in place, you can mix and match `for` loops and `if` like so:

```python
extern def putchard(x)

def printpattern(n):
    if n < 3:
        for i in range(1, n+1, 1):
            putchard(33) # '!'
    else:
        for i in range(1, n+1, 1):
            if i < 3:
                putchard(33) # '!'
            else:
                putchard(42) # '*'

printpattern(2)
printpattern(20)
```

and get 

```cpp
!!
Evaluated to 0.000000

!!******************
Evaluated to 0.000000
```

With this, we conclude the “adding control flow to Pyxc chapter of the tutorial. In this chapter we added two control flow constructs, and used them to motivate a couple of aspects of the LLVM IR that are important for front-end implementors to know. In the next chapter of our saga, we will get a bit crazier and add user-defined operators to our poor innocent language.

## Compiling

```bash
cd code/chapter07 && ./build.sh
```

!!!note You may have noticed that our approach to handling `return` keywords using the `InForExpression` flag is somewhat ad-hoc. In a production compiler, these patterns would be handled more elegantly through a formal grammar specification and proper context tracking in the parser. However, for this tutorial, we've intentionally chosen a simpler approach that lets you focus on the core LLVM concepts—control flow, PHI nodes, and basic block manipulation—without getting bogged down in parser theory. As your language grows more complex, you'll naturally develop better abstractions for managing parsing context. For now, this solution works well enough to let us explore the interesting parts of code generation.


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

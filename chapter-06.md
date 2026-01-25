# 6. Pyxc: Extending the Language: User-defined Operators

## Introduction
Welcome to Chapter 6 of the [Implementing a language with LLVM](chapter-00.md) tutorial. At this point in our tutorial, we now have a fully functional language that is fairly minimal, but also useful. There is still one big problem with it, however. Our language doesn’t have many useful operators (like division, logical negation, or even any comparisons besides less-than).

This chapter of the tutorial takes a wild digression into adding user-defined operators to the simple and beautiful Pyxc language. This digression now gives us a simple and ugly language in some ways, but also a powerful one at the same time. One of the great things about creating your own language is that you get to decide what is good or bad. In this tutorial we’ll assume that it is okay to use this as a way to show some interesting parsing techniques.

At the end of this tutorial, we’ll run through an example Pyxc application that renders the Mandelbrot set. This gives an example of what you can build with Pyxc and its feature set.

## User-defined Operators: the Idea
The “operator overloading” that we will add to Pyxc is more general than in languages like C++. In C++, you are only allowed to redefine existing operators: you can’t programmatically change the grammar, introduce new operators, change precedence levels, etc. In this chapter, we will add this capability to Pyxc, which will let the user round out the set of operators that are supported.

The point of going into user-defined operators in a tutorial like this is to show the power and flexibility of using a hand-written parser. Thus far, the parser we have been implementing uses recursive descent for most parts of the grammar and operator precedence parsing for the expressions. See [Chapter 2](chapter-02.md) for details. By using operator precedence parsing, it is very easy to allow the programmer to introduce new operators into the grammar: the grammar is dynamically extensible as the JIT runs.

The two specific features we’ll add are programmable unary operators (right now, Pyxc has no unary operators at all) as well as binary operators. We do this borrowing from the python decorator syntax. An example of this is:

```python
# Unary not.
@unary
def !(v):
    if v: return 0
    else: return 1

# Unary negate.
@unary
def -(v):
  return 0-v

# Define > with the same precedence as <.
@binary(precedence=10)
def >(LHS,RHS):
  return RHS < LHS

# Binary logical or (no short-circuit)
@binary(precedence=5)
def |(LHS, RHS):
    if LHS: 
        return 1
    else: 
        if RHS: 
            return 1
        else: 
            return 0


# Binary logical and (no short-circuit)
@binary(precedence=6)
def &(LHS, RHS):
    if !LHS:
        return 0
    else:
        return !!RHS

# Define = with slightly lower precedence than relationals
@binary(precedence=9)
def =(LHS, RHS):
    return !(LHS < RHS | LHS > RHS)
```

Many languages aspire to being able to implement their standard runtime library in the language itself. In Pyxc, we can implement significant parts of the language in the library!

We will break down implementation of these features into two parts: implementing support for user-defined binary operators and adding unary operators.

# User-defined Binary Operators
Adding support for user-defined binary operators is pretty simple with our current framework. We’ll first add support for decorators:

```cpp
enum Token {
  ...
  // decorator
  tok_decorator = -13,
};

static int gettok() {
...
  if (LastChar == '#') {
    ...
  }

  if (LastChar == '@') {
    LastChar = getchar(); // consume '@'
    return tok_decorator;
  }
```

Next we add support for the different operator types.
```cpp
enum OperatorType { Undefined, Unary, Binary };

static std::map<std::string, OperatorType> Decorators = {
    {"unary", OperatorType::Unary}, {"binary", OperatorType::Binary}};
```

We have to extend our parsing logic to parse our decorators. Since decorators just come before the function definition, let's add the parsing logic there. 

```cpp
..
static constexpr int DEFAULT_BINARY_PRECEDENCE = 30; // <-- ADD THIS 
...

/// definition ::= (@unary | @binary | @binary() | @binary(precedence=\d+))*
///                 'def' prototype:
///                     expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
  OperatorType OpType = Undefined;
  int Precedence = DEFAULT_BINARY_PRECEDENCE;

  if (CurTok == tok_decorator) {
    getNextToken(); // eat '@'

    if (CurTok != tok_identifier)
      return LogErrorF("expected decorator name after '@'");

    auto it = Decorators.find(IdentifierStr);
    OpType = it == Decorators.end() ? OperatorType::Undefined : it->second;
    getNextToken(); // eat decorator name

    if (OpType == Undefined)
      return LogErrorF(("unknown decorator '" + IdentifierStr + "'").c_str());

    if (OpType == Binary) {
      if (CurTok == '(') {
        getNextToken(); // eat '('
        if (CurTok != ')') {
          // Parse "precedence=N"
          // If we want to introduce more attributes, we would add "precedence"
          // to a map and associate it with a binary operator.
          if (CurTok != tok_identifier || IdentifierStr != "precedence") {
            return LogErrorF("expected 'precedence' parameter in decorator");
          }

          getNextToken(); // eat 'precedence'

          if (CurTok != '=')
            return LogErrorF("expected '=' after 'precedence'");

          getNextToken(); // eat '='

          if (CurTok != tok_number)
            return LogErrorF("expected number for precedence value");

          Precedence = NumVal;
          getNextToken(); // eat number
        }
        if (CurTok != ')')
          return LogErrorF("expected ')' after precedence value");
        getNextToken(); // eat ')'
      }
    }
  }

  EatNewLines();

  if (CurTok != tok_def)
    return LogErrorF("expected 'def'");
  ...
```

We also adjust the Main loop to handle the decorator
```cpp
/// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (true) {
    fprintf(stderr, "ready> ");
    switch (CurTok) {
    case tok_eof:
      return;
    case tok_def: 
    case tok_decorator: // <-- ADD THIS AND FALL THROUGH TO HandleDefinition()
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}
```

In our grammar so far, the “name” for the function definition is parsed as the “prototype” production and into the PrototypeAST AST node. To represent our new user-defined operators as prototypes, we have to extend the PrototypeAST AST node like this:

```cpp
/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its argument names as well as if it is an operator.
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;
  bool IsOperator;
  unsigned Precedence;  // Precedence if a binary op.

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args,
               bool IsOperator = false, unsigned Prec = 0)
  : Name(Name), Args(std::move(Args)), IsOperator(IsOperator),
    Precedence(Prec) {}

  Function *codegen();
  const std::string &getName() const { return Name; }

  bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  char getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    return Name[Name.size() - 1];
  }

  unsigned getBinaryPrecedence() const { return Precedence; }
};
```

Basically, in addition to knowing a name for the prototype, we now keep track of whether it was an operator, and if it was, what precedence level the operator is at. The precedence is only used for binary operators (as you’ll see below, it just doesn’t apply for unary operators). 

We need to pass OpType and Precedence read from the decorator in `ParseDefinition` to `ParsePrototype`, so it can create a `PrototypeAST` with the right parameters: 

```cpp
static std::unique_ptr<PrototypeAST>
ParsePrototype(OperatorType operatorType = Undefined, int precedence = 0) {
  std::string FnName;
  
  if (operatorType != Undefined) {
    // Parsing operator definition: expect a single character operator token
    if (CurTok == tok_identifier || !isascii(CurTok))
      return LogErrorP("Expected single character operator");

    FnName = (operatorType == Unary ? "unary" : "binary");
    FnName += (char)CurTok;
    getNextToken();
  } else {
    // Parsing regular function: expect identifier
    if (CurTok != tok_identifier)
      return LogErrorP("Expected function name in prototype");

    FnName = IdentifierStr;
    getNextToken();
  }

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");

  ...

  // success.
  getNextToken(); // eat ')'.

  // Pass on the parameters to PrototypeAST constructor
  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames),
                                        operatorType != OperatorType::Undefined,
                                        precedence);
}
```

This is all fairly straightforward parsing code, and we have already seen a lot of similar code in the past. One interesting part about the code above is the couple lines that set up FnName for binary operators. This builds names like “binary@” for a newly defined “@” operator. It then takes advantage of the fact that symbol names in the LLVM symbol table are allowed to have any character in them, including embedded nul characters.

The next interesting thing to add, is codegen support for these binary operators. Given our current structure, this is a simple addition of a default case for our existing binary operator node:

```cpp
Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                "booltmp");
  default:
    break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  Function *F = getFunction(std::string("binary") + Op);
  assert(F && "binary operator not found!");

  Value *Ops[2] = { L, R };
  return Builder->CreateCall(F, Ops, "binop");
}
```

As you can see above, the new code is actually really simple. It just does a lookup for the appropriate operator in the symbol table and generates a function call to it. Since user-defined operators are just built as normal functions (because the “prototype” boils down to a function with the right name) everything falls into place.

The final piece of code we are missing, is a bit of top-level magic:

```cpp
Function *FunctionAST::codegen() {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // If this is an operator, install it.
  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  ...
```
Basically, before codegening a function, if it is a user-defined operator, we register it in the precedence table. This allows the binary operator parsing logic we already have in place to handle it. Since we are working on a fully-general operator precedence parser, this is all we need to do to “extend the grammar”.

Now we have useful user-defined binary operators. This builds a lot on the previous framework we built for other operators. Adding unary operators is a bit more challenging, because we don’t have any framework for it yet - let’s see what it takes.

## User-defined Unary Operators
Since we don’t currently support unary operators in the Pyxc language, we’ll need to add everything to support them. Above, we added simple support for the ‘unary’ keyword to the lexer. In addition to that, we need an AST node:

```cpp
/// UnaryExprAST - Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
  char Opcode;
  std::unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
    : Opcode(Opcode), Operand(std::move(Operand)) {}

  Value *codegen() override;
};
```

This AST node is very simple and obvious by now. It directly mirrors the binary operator AST node, except that it only has one child. With this, we need to add the parsing logic. Parsing a unary operator is pretty simple: we’ll add a new function to do it:

```cpp
/// unary
///   ::= primary
///   ::= '!' unary
static std::unique_ptr<ExprAST> ParseUnary() {
  // If the current token is not an operator, it must be a primary expr.
  if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
    return ParsePrimary();

  // If this is a unary operator, read it.
  int Opc = CurTok;
  getNextToken();
  if (auto Operand = ParseUnary())
    return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}
```
The grammar we add is pretty straightforward here. If we see a unary operator when parsing a primary operator, we eat the operator as a prefix and parse the remaining piece as another unary operator. This allows us to handle multiple unary operators (e.g. “!!x”). Note that unary operators can’t have ambiguous parses like binary operators can, so there is no need for precedence information.

The problem with this function, is that we need to call ParseUnary from somewhere. To do this, we change previous callers of ParsePrimary to call ParseUnary instead:

```cpp
/// binoprhs
///   ::= ('+' unary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
  ...
    // Parse the unary expression after the binary operator.
    auto RHS = ParseUnary();
    if (!RHS)
      return nullptr;
  ...
}

/// expression
///   ::= unary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParseUnary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}
```

With these two simple changes, we are now able to parse unary operators and build the AST for them. 

As with binary operators, we name unary operators with a name that includes the operator character. This assists us at code generation time. Speaking of, the final piece we need to add is codegen support for unary operators. It looks like this:

```cpp
Value *UnaryExprAST::codegen() {
  Value *OperandV = Operand->codegen();
  if (!OperandV)
    return nullptr;

  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return Builder->CreateCall(F, OperandV, "unop");
}
```

## Kicking the Tires
It is somewhat hard to believe, but with a few simple extensions we’ve covered in the last chapters, we have grown a real-ish language. With this, we can do a lot of interesting things, including I/O, math, and a bunch of other things. For example, we can now add a nice sequencing operator (printd is defined to print out the specified value and a newline):

```
ready> extern def printd(x)
ready> Read extern:
declare double @printd(double)

ready> @binary(precedence=1) def ;(x,y): return 0
ready> Read function definition:
...

ready> printd(123) ; printd(456) ; printd(789)
...

123.000000
456.000000
789.000000

Evaluated to 0.000000
```

We can also define a bunch of other “primitive” operations, such as:

```python
# unary not
@unary
def !(v):
    if v: return 0
    else: return 1

# Unary negate.
@unary
def -(v):
  return 0-v

# Define > with the same precedence as <.
@binary(precedence=10)
def >(LHS,RHS):
  return RHS < LHS

# Binary logical or (no short-circuit)
@binary(precedence=5)
def |(LHS, RHS):
    if LHS: 
        return 1
    else: 
        if RHS: 
            return 1
        else: 
            return 0


# Binary logical and (no short-circuit)
@binary(precedence=6)
def &(LHS, RHS):
    if !LHS:
        return 0
    else:
        return !!RHS

# Define = with slightly lower precedence than relationals
@binary(precedence=9)
def =(LHS, RHS):
    return !(LHS < RHS | LHS > RHS)

@binary(precedence=1)
def ;(x, y):
    return y
```

Given the previous if/then/else support, we can also define interesting functions for I/O. For example, the following prints out a character whose “density” reflects the value passed in: the lower the value, the denser the character:

```python
extern def putchard(char)

def printdensity(d):
    if d > 8: 
        return putchard(32) # ' '
    else: 
        if d > 4:
            return putchard(46)  # '.'
        else: 
            if d > 2:
                return putchard(43) # '+'
            else:
                return putchard(42)  # '*'

printdensity(1); printdensity(2); printdensity(3); printdensity(4); printdensity(5); printdensity(9); putchard(10)

**++.
Evaluated to 0.000000
```

Based on these simple primitive operations, we can start to define more interesting things. For example, here’s a little function that determines the number of iterations it takes for a certain function in the complex plane to diverge:

```python
# Determine whether the specific location diverges.
# Solve for z = z^2 + c in the complex plane.
def mandelconverger(real, imag, iters, creal, cimag):
  if iters > 255 | (real*real + imag*imag > 4):
    return iters
  else:
    return mandelconverger(real*real - imag*imag + creal, 2*real*imag + cimag, iters+1, creal, cimag)

# Return the number of iterations required for the iteration to escape
def mandelconverge(real, imag):
  return mandelconverger(real, imag, 0, real, imag)
```

This “z = z2 + c” function is a beautiful little creature that is the basis for computation of the [Mandelbrot Set](http://en.wikipedia.org/wiki/Mandelbrot_set). Our mandelconverge function returns the number of iterations that it takes for a complex orbit to escape, saturating to 255. This is not a very useful function by itself, but if you plot its value over a two-dimensional plane, you can see the Mandelbrot set. Given that we are limited to using putchard here, our amazing graphical output is limited, but we can whip together something using the density plotter above:

```python
# Compute and plot the mandelbrot set with the specified 2 dimensional range
# info.
def mandelhelp(xmin, xmax, xstep, ymin, ymax, ystep):
  for y in range(ymin, ymax, ystep):
    (for x in range(xmin, xmax, xstep):
       printdensity(mandelconverge(x,y))) ; putchard(10)

# mandel - This is a convenient helper function for plotting the mandelbrot set
# from the specified position with the specified Magnification.
def mandel(realstart, imagstart, realmag, imagmag):
  # NOTE: The entire expression needs to be on the same line. Line breaks won't work here.
  return mandelhelp(realstart, realstart+realmag*78, realmag, imagstart, imagstart+imagmag*40, imagmag)
```

```python
ready> mandel(-2.3, -1.3, 0.05, 0.07)
*******************************************************************************
*******************************************************************************
****************************************++++++*********************************
************************************+++++...++++++*****************************
*********************************++++++++.. ...+++++***************************
*******************************++++++++++..   ..+++++**************************
******************************++++++++++.     ..++++++*************************
****************************+++++++++....      ..++++++************************
**************************++++++++.......      .....++++***********************
*************************++++++++.   .            ... .++**********************
***********************++++++++...                     ++**********************
*********************+++++++++....                    .+++*********************
******************+++..+++++....                      ..+++********************
**************++++++. ..........                        +++********************
***********++++++++..        ..                         .++********************
*********++++++++++...                                 .++++*******************
********++++++++++..                                   .++++*******************
*******++++++.....                                    ..++++*******************
*******+........                                     ...++++*******************
*******+... ....                                     ...++++*******************
*******+++++......                                    ..++++*******************
*******++++++++++...                                   .++++*******************
*********++++++++++...                                  ++++*******************
**********+++++++++..        ..                        ..++********************
*************++++++.. ..........                        +++********************
******************+++...+++.....                      ..+++********************
*********************+++++++++....                    ..++*********************
***********************++++++++...                     +++*********************
*************************+++++++..   .            ... .++**********************
**************************++++++++.......      ......+++***********************
****************************+++++++++....      ..++++++************************
*****************************++++++++++..     ..++++++*************************
*******************************++++++++++..  ...+++++**************************
*********************************++++++++.. ...+++++***************************
***********************************++++++....+++++*****************************
***************************************++++++++********************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
Evaluated to 0.000000

ready> mandel(-2, -1, 0.02, 0.04)
******************************************************************+++++++++++++
****************************************************************+++++++++++++++
*************************************************************++++++++++++++++++
***********************************************************++++++++++++++++++++
********************************************************+++++++++++++++++++++++
******************************************************++++++++++++++++++++++...
***************************************************+++++++++++++++++++++.......
*************************************************++++++++++++++++++++..........
***********************************************+++++++++++++++++++...       ...
********************************************++++++++++++++++++++......         
******************************************++++++++++++++++++++.......          
***************************************+++++++++++++++++++++..........         
************************************++++++++++++++++++++++...........          
********************************++++++++++++++++++++++++.........              
***************************++++++++...........+++++..............              
*********************++++++++++++....  .........................               
***************+++++++++++++++++....   .........   ............                
***********+++++++++++++++++++++.....                   ......                 
********+++++++++++++++++++++++.......                                         
******+++++++++++++++++++++++++........                                        
****+++++++++++++++++++++++++.......                                           
***+++++++++++++++++++++++.........                                            
**++++++++++++++++...........                                                  
*++++++++++++................                                                  
*++++....................                                                      
                                                                               
*++++....................                                                      
*++++++++++++................                                                  
**++++++++++++++++...........                                                  
***+++++++++++++++++++++++.........                                            
****+++++++++++++++++++++++++.......                                           
******+++++++++++++++++++++++++........                                        
********+++++++++++++++++++++++.......                                         
***********+++++++++++++++++++++.....                   ......                 
***************+++++++++++++++++....   .........   ............                
*********************++++++++++++....  .........................               
***************************++++++++...........+++++..............              
********************************++++++++++++++++++++++++.........              
************************************++++++++++++++++++++++...........          
***************************************+++++++++++++++++++++..........         
******************************************++++++++++++++++++++.......       
Evaluated to 0.000000

ready> mandel(-0.9, -1.4, 0.02, 0.03)
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
****************************+++++++++++++++++**********************************
***********************+++++++++++...++++++++++++******************************
********************+++++++++++++.. . .++++++++++++++**************************
*****************++++++++++++++++... ......++++++++++++************************
**************+++++++++++++++++++...   .......+++++++++++**********************
************++++++++++++++++++++....    .... ..++++++++++++********************
**********++++++++++++++++++++++......       ...++++++++++++*******************
********+++++++++++++++++++++++.......     .....++++++++++++++*****************
******++++++++++++++++++++++++.......      .....+++++++++++++++****************
****+++++++++++++++++++++++++.... .         .....+++++++++++++++***************
**+++++++++++++++++++++++++....                ...++++++++++++++++*************
*+++++++++++++++++++++++.......                ....++++++++++++++++************
+++++++++++++++++++++..........                .....++++++++++++++++***********
++++++++++++++++++.............                .......+++++++++++++++**********
+++++++++++++++................                ............++++++++++**********
+++++++++++++.................                  .................+++++*********
+++++++++++...       ....                            ..........  .+++++********
++++++++++.....                                       ........  ...+++++*******
++++++++......                                                   ..++++++******
+++++++........                                                   ..+++++******
+++++..........                                                   ..++++++*****
++++..........                                                  ....++++++*****
++..........                                                    ....+++++++****
..........                                                     ......+++++++***
..........                                                      .....+++++++***
..........                                                       .....++++++***
.........                                                            .+++++++**
........                                                             .+++++++**
 ......                                                             ...+++++++*
   .                                                              ....++++++++*
                                                                   ...++++++++*
                                                                    ..+++++++++
                                                                    ..+++++++++
Evaluated to 0.000000
ready> ^D
```
At this point, you may be starting to realize that Pyxc is a real and powerful language. It may not be self-similar :), but it can be used to plot things that are!

With this, we conclude the “adding user-defined operators” chapter of the tutorial. We have successfully augmented our language, adding the ability to extend the language in the library, and we have shown how this can be used to build a simple but interesting end-user application in Pyxc. At this point, Pyxc can build a variety of applications that are functional and can call functions with side-effects, but it can’t actually define and mutate a variable itself.

Strikingly, variable mutation is an important feature of some languages, and it is not at all obvious how to add support for mutable variables without having to add an “SSA construction” phase to your front-end. In the next chapter, we will describe how you can add variable mutation without building SSA in your front-end.

## Full Code Listing
Here is the complete code listing for our running example, enhanced with the support for user-defined operators.
```cpp
#include "../include/PyxcJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::orc;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,
  tok_eol = -2,

  // commands
  tok_def = -3,
  tok_extern = -4,

  // primary
  tok_identifier = -5,
  tok_number = -6,

  // control
  tok_if = -7,
  tok_else = -8,
  tok_return = -9,

  // loop
  tok_for = -10,
  tok_in = -11,
  tok_range = -12,

  // decorator
  tok_decorator = -13,
};

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number
static bool InForExpression;      // Track global parsing context

// Keywords words like `def`, `extern` and `return`. The lexer will return the
// associated Token. Additional language keywords can easily be added here.
static std::map<std::string, Token> Keywords = {
    {"def", tok_def}, {"extern", tok_extern}, {"return", tok_return},
    {"if", tok_if},   {"else", tok_else},     {"for", tok_for},
    {"in", tok_in},   {"range", tok_range}};

enum OperatorType { Undefined, Unary, Binary };

static constexpr int DEFAULT_BINARY_PRECEDENCE = 30;

static std::map<std::string, OperatorType> Decorators = {
    {"unary", OperatorType::Unary}, {"binary", OperatorType::Binary}};

/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';

  // Skip whitespace EXCEPT newlines
  while (isspace(LastChar) && LastChar != '\n' && LastChar != '\r')
    LastChar = getchar();

  // Return end-of-line token
  if (LastChar == '\n' || LastChar == '\r') {
    // Reset LastChar to a space instead of reading the next character.
    // If we called getchar() here, it would block waiting for input,
    // requiring the user to press Enter twice in the REPL.
    // Setting LastChar = ' ' avoids this blocking read.
    LastChar = ' ';
    return tok_eol;
  }

  if (LastChar == '@') {
    LastChar = getchar(); // consume '@'
    return tok_decorator;
  }

  if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;

    // Is this a known keyword? If yes, return that.
    // Else it is an ordinary identifier.
    const auto it = Keywords.find(IdentifierStr);
    return (it != Keywords.end()) ? it->second : tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return tok_eol;
  }

  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//

namespace {

/// ExprAST - Base class for all expression nodes.
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  Value *codegen() override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(const std::string &Name) : Name(Name) {}
  Value *codegen() override;
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  Value *codegen() override;
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
  Value *codegen() override;
};

/// IfExprAST - Expression class for if/else.
class IfExprAST : public ExprAST {
  std::unique_ptr<ExprAST> Cond, Then, Else;

public:
  IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
            std::unique_ptr<ExprAST> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

  Value *codegen() override;
};

/// ForExprAST - Expression class for for/in.
class ForExprAST : public ExprAST {
  std::string VarName;
  std::unique_ptr<ExprAST> Start, End, Step, Body;

public:
  ForExprAST(std::string VarName, std::unique_ptr<ExprAST> Start,
             std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
             std::unique_ptr<ExprAST> Body)
      : VarName(std::move(VarName)), Start(std::move(Start)),
        End(std::move(End)), Step(std::move(Step)), Body(std::move(Body)) {}

  Value *codegen();
};

/// UnaryExprAST - Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
  char Opcode;
  std::unique_ptr<ExprAST> Operand;

public:
  UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
      : Opcode(Opcode), Operand(std::move(Operand)) {}

  Value *codegen() override;
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its argument names as well as if it is an operator.
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;
  bool IsOperator;
  unsigned Precedence; // Precedence if a binary op.

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args,
               bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), Args(std::move(Args)), IsOperator(IsOperator),
        Precedence(Prec) {}

  Function *codegen();
  const std::string &getName() const { return Name; }

  bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

  char getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    return Name[Name.size() - 1];
  }

  unsigned getBinaryPrecedence() const { return Precedence; }
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
  Function *codegen();
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }
// Tracks all previously defined function prototypes
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence = {
    {'<', 10}, {'+', 20}, {'-', 20}, {'*', 40}};

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
  InForExpression = false;
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

std::unique_ptr<FunctionAST> LogErrorF(const char *Str) {
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
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
///   ::= identifier
///   ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;

  getNextToken(); // eat identifier.

  if (CurTok != '(') // Simple variable ref.
    return std::make_unique<VariableExprAST>(IdName);

  // Call.
  getNextToken(); // eat (
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/*
 * In later chapters, we will need to check the indentation
 * whenever we eat new lines.
 */
static void EatNewLines() {
  while (CurTok == tok_eol)
    getNextToken();
}

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

  // Handle nested `if` and `for` expressions:
  // For `if` expressions, `return` statements are emitted inside the
  // true and false branches, so we only require an explicit `return`
  // when we are not parsing an `if`.
  // For `for` expressions, control flow and the resulting value are
  // handled entirely within the loop body, so an explicit `return`
  // is not required at this level.
  if (!InForExpression && CurTok != tok_if && CurTok != tok_for) {
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

  if (!InForExpression && CurTok != tok_if && CurTok != tok_for) {
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

// `for` identifier `in` `range` `(`expression `,` expression
//   (`,` expression)? # optional
// `)`: expression
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

  if (CurTok != tok_range)
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

  // `for` expressions don't have the return statement
  // they return 0 by default.
  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  InForExpression = false;
  return std::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End),
                                      std::move(Step), std::move(Body));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
///   ::= ifexpr
///   ::= forexpr
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

/// unary
///   ::= primary
///   ::= '!' unary
static std::unique_ptr<ExprAST> ParseUnary() {
  // If the current token is not an operator, it must be a primary expr.
  if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
    return ParsePrimary();

  // If this is a unary operator, read it.
  int Opc = CurTok;
  getNextToken();
  if (auto Operand = ParseUnary())
    return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}

/// binoprhs
///   ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
  // If this is a binop, find its precedence.
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < ExprPrec)
      return LHS;

    // Okay, we know this is a binop.
    int BinOp = CurTok;
    getNextToken(); // eat binop

    // Parse the primary expression after the binary operator.
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

    // Merge LHS/RHS.
    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

/// expression
///   ::= unary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParseUnary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///   ::= id '(' (id (',' id)*)? ')'
static std::unique_ptr<PrototypeAST>
ParsePrototype(OperatorType operatorType = Undefined, int precedence = 0) {
  std::string FnName;

  if (operatorType != Undefined) {
    // Expect a single-character operator
    if (CurTok == tok_identifier) {
      return LogErrorP("Expected single character operator");
    }

    if (!isascii(CurTok)) {
      return LogErrorP("Expected single character operator");
    }

    FnName = (operatorType == Unary ? "unary" : "binary");
    FnName += (char)CurTok;

    getNextToken();
  } else {
    if (CurTok != tok_identifier) {
      return LogErrorP("Expected function name in prototype");
    }

    FnName = IdentifierStr;

    getNextToken();
  }

  if (CurTok != '(') {
    return LogErrorP("Expected '(' in prototype");
  }

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
    getNextToken(); // Eat idenfitier

    if (CurTok == ')')
      break;

    if (CurTok != ',')
      return LogErrorP("Expected ')' or ',' in parameter list");
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype");

  // success.
  getNextToken(); // eat ')'.
  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames),
                                        operatorType != OperatorType::Undefined,
                                        precedence);
}

/// definition ::= (@unary | @binary | @binary() | @binary(precedence=\d+))*
///                 'def' prototype:
///                     expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
  OperatorType OpType = Undefined;
  int Precedence = DEFAULT_BINARY_PRECEDENCE;

  if (CurTok == tok_decorator) {
    getNextToken(); // eat '@'

    if (CurTok != tok_identifier)
      return LogErrorF("expected decorator name after '@'");

    auto it = Decorators.find(IdentifierStr);
    OpType = it == Decorators.end() ? OperatorType::Undefined : it->second;
    getNextToken(); // eat decorator name

    if (OpType == Undefined)
      return LogErrorF(("unknown decorator '" + IdentifierStr + "'").c_str());

    if (OpType == Binary) {
      if (CurTok == '(') {
        getNextToken(); // eat '('
        if (CurTok != ')') {
          // Parse "precedence=N"
          // If we want to introduce more attributes, we would add "precedence"
          // to a map and associate it with a binary operator.
          if (CurTok != tok_identifier || IdentifierStr != "precedence") {
            return LogErrorF("expected 'precedence' parameter in decorator");
          }

          getNextToken(); // eat 'precedence'

          if (CurTok != '=')
            return LogErrorF("expected '=' after 'precedence'");

          getNextToken(); // eat '='

          if (CurTok != tok_number)
            return LogErrorF("expected number for precedence value");

          Precedence = NumVal;
          getNextToken(); // eat number
        }
        if (CurTok != ')')
          return LogErrorF("expected ')' after precedence value");
        getNextToken(); // eat ')'
      }
    }
  }

  EatNewLines();

  if (CurTok != tok_def)
    return LogErrorF("expected 'def'");

  getNextToken(); // eat def.
  auto Proto = ParsePrototype(OpType, Precedence);
  if (!Proto)
    return nullptr;

  if (CurTok != ':')
    return LogErrorF("Expected ':' in function definition");

  getNextToken(); // eat ':'

  EatNewLines();

  if (!InForExpression && CurTok != tok_if && CurTok != tok_for) {
    if (CurTok != tok_return)
      return LogErrorF("Expected 'return' before return expression");
    getNextToken(); // eat return
  }

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));

  return nullptr;
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    // Make an anonymous proto.
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

/// external ::= 'extern' 'def' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern.
  if (CurTok != tok_def)
    return LogErrorP("Expected `def` after extern.");
  getNextToken(); // eat def
  return ParsePrototype();
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> NamedValues;
static std::unique_ptr<PyxcJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static ExitOnError ExitOnErr;

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

Function *getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  return nullptr;
}

Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  Value *V = NamedValues[Name];
  if (!V)
    return LogErrorV(("Unknown variable name " + Name).c_str());
  return V;
}

Value *UnaryExprAST::codegen() {
  Value *OperandV = Operand->codegen();
  if (!OperandV)
    return nullptr;

  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F) {
    return LogErrorV("Unknown unary operator");
  }

  return Builder->CreateCall(F, OperandV, "unop");
}

Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  Function *F = getFunction(std::string("binary") + Op);
  assert(F && "binary operator not found!");

  Value *Ops[2] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}

Value *CallExprAST::codegen() {
  // Look up the name in the global module table.
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  // If argument mismatch error.
  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfExprAST::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at
  // the end of the function.
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value.
  Builder->SetInsertPoint(ThenBB);

  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the
  // PHI.
  ThenBB = Builder->GetInsertBlock();

  // Emit else block.
  TheFunction->insert(TheFunction->end(), ElseBB);
  Builder->SetInsertPoint(ElseBB);

  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Else' can change the current block, update ElseBB for the
  // PHI.
  ElseBB = Builder->GetInsertBlock();

  // Emit merge block.
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");

  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}

Value *ForExprAST::codegen() {
  // Emit the start code first, without 'variable' in scope.
  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;

  // Make the new basic block for the loop header, inserting after current
  // block.
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  BasicBlock *PreheaderBB = Builder->GetInsertBlock();
  BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop", TheFunction);

  // Insert an explicit fall through from the current block to the LoopBB.
  Builder->CreateBr(LoopBB);

  // Start insertion in LoopBB.
  Builder->SetInsertPoint(LoopBB);

  // Start the PHI node with an entry for Start.
  PHINode *Variable =
      Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
  Variable->addIncoming(StartVal, PreheaderBB);

  // Within the loop, the variable is defined equal to the PHI node.  If it
  // shadows an existing variable, we have to restore it, so save it now.
  Value *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Variable;

  // Emit the body of the loop.  This, like any other expr, can change the
  // current BB.  Note that we ignore the value computed by the body, but
  // don't allow an error.
  if (!Body->codegen())
    return nullptr;

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

  // Generate the end condition.
  Value *EndCond = End->codegen();
  if (!EndCond)
    return nullptr;

  // Compare Variable < End
  EndCond = Builder->CreateFCmpULT(Variable, EndCond, "loopcond");

  // Create the "after loop" block and insert it.
  BasicBlock *LoopEndBB = Builder->GetInsertBlock();
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "afterloop", TheFunction);

  // Insert the conditional branch into the end of LoopEndBB.
  Builder->CreateCondBr(EndCond, LoopBB, AfterBB);

  // Any new code will be inserted in AfterBB.
  Builder->SetInsertPoint(AfterBB);

  // Add a new entry to the PHI node for the backedge.
  Variable->addIncoming(NextVar, LoopEndBB);

  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for expr always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Function *PrototypeAST::codegen() {
  // Make the function type:  double(double,double) etc.
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

Function *FunctionAST::codegen() {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // If this is an operator, install it.
  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  if (Value *RetVal = Body->codegen()) {
    // Finish off the function.
    Builder->CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    // Run the optimizer on the function.
    TheFPM->run(*TheFunction, *TheFAM);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static void InitializeModuleAndManagers() {
  // Open a new context and module.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("PyxcJIT", *TheContext);
  TheModule->setDataLayout(TheJIT->getDataLayout());

  // Create a new builder for the module.
  Builder = std::make_unique<IRBuilder<>>(*TheContext);

  // Create new pass and analysis managers.
  TheFPM = std::make_unique<FunctionPassManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  TheCGAM = std::make_unique<CGSCCAnalysisManager>();
  TheMAM = std::make_unique<ModuleAnalysisManager>();
  ThePIC = std::make_unique<PassInstrumentationCallbacks>();
  TheSI = std::make_unique<StandardInstrumentations>(*TheContext,
                                                     /*DebugLogging*/ true);
  TheSI->registerCallbacks(*ThePIC, TheMAM.get());

  // Add transform passes.
  // Do simple "peephole" optimizations and bit-twiddling optzns.
  TheFPM->addPass(InstCombinePass());
  // Reassociate expressions.
  TheFPM->addPass(ReassociatePass());
  // Eliminate Common SubExpressions.
  TheFPM->addPass(GVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  TheFPM->addPass(SimplifyCFGPass());

  // Register analysis passes used in these transform passes.
  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read function definition:\n");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      ExitOnErr(TheJIT->addModule(
          ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
      InitializeModuleAndManagers();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      fprintf(stderr, "Read extern:\n");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
      // Create a ResourceTracker to track JIT'd memory allocated to our
      // anonymous expression -- that way we can free it after executing.
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();

      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      fprintf(stderr, "Read top-level expression:\n");
      FnIR->print(errs());
      fprintf(stderr, "\n");

      // Search the JIT for the __anon_expr symbol.
      auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

      // Get the symbol's address and cast it to the right type (takes no
      // arguments, returns a double) so we can call it as a native function.
      double (*FP)() = ExprSymbol.toPtr<double (*)()>();
      fprintf(stderr, "\nEvaluated to %f\n", FP());

      // Delete the anonymous expression module from the JIT.
      ExitOnErr(RT->remove());

      // Remove the anonymous expression.
      //   FnIR->eraseFromParent();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

/// top ::= definition | external | expression | eol
static void MainLoop() {
  while (true) {
    fprintf(stderr, "ready> ");
    switch (CurTok) {
    case tok_eof:
      return;
    case tok_decorator:
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    case tok_eol: // Skip newlines
      getNextToken();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Prime the first token.
  fprintf(stderr, "ready> ");
  getNextToken();

  TheJIT = ExitOnErr(PyxcJIT::Create());
  InitializeModuleAndManagers();

  // Run the main "interpreter loop" now.
  MainLoop();

  // Run the main "interpreter loop" now.
  TheModule->print(errs(), nullptr);

  return 0;
}
```

## Compiling
```bash
clang++ -g pyxc.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o pyxc
```

On some platforms, you will need to specify -rdynamic or -Wl,–export-dynamic when linking. This ensures that symbols defined in the main executable are exported to the dynamic linker and so are available for symbol resolution at run time. This is not needed if you compile your support code into a shared library, although doing that will cause problems on Windows.

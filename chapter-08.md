# 8. Pyxc: Extending the Language: User-defined Operators

!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter08](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter08).


## Introduction
Welcome to Chapter 8 of the [Implementing a language with LLVM](chapter-00.md) tutorial. At this point in our tutorial, we now have a fully functional language that is fairly minimal, but also useful. There is still one big problem with it, however. Our language doesn’t have many useful operators (like division, logical negation, or even any comparisons besides less-than).

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

```bash
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

## Compiling
```bash
cd code/chapter08 && ./build.sh
```

On some platforms, you will need to specify -rdynamic or -Wl,–export-dynamic when linking. This ensures that symbols defined in the main executable are exported to the dynamic linker and so are available for symbol resolution at run time. This is not needed if you compile your support code into a shared library, although doing that will cause problems on Windows.

## Known Bugs / Shortcomings

As of this chapter, multi-character operators (for example `==`, `!=`, `<=`, `>=`) are not tokenized as single operators. The lexer currently emits them as separate single-character tokens.

This can corrupt the parse tree for some expressions, and the eventual diagnostic may appear late (often near end-of-file) instead of at the exact source of the problem. A common symptom is an error like:

```text
Error (Line: N, Column: M): Unknown unary operator
```

So if you see an EOF-adjacent or late-location operator error, check for multi-character operators first.


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

---
description: "Emit debug information so generated code can be inspected with debuggers, including source locations and function-level metadata."
---
# 15. Pyxc: Adding Debug Information

## Introduction

Welcome to Chapter 12 of the [Implementing a language with LLVM](chapter-00.md) tutorial. In chapters 1 through 8, we’ve built a decent little programming language with functions and variables. What happens if something goes wrong though, how do you debug your program?

Source level debugging uses formatted data that helps a debugger translate from binary and the state of the machine back to the source that the programmer wrote. In LLVM we generally use a format called DWARF. DWARF is a compact encoding that represents types, source locations, and variable locations.

The short summary of this chapter is that we’ll go through the various things you have to add to a programming language to support debug info, and how you translate that into DWARF.

Caveat: We’ll need to compile our program down to a standalone. This means that we’ll have a source file with a simple program written in Pyxc rather than the interactive JIT. It does involve a limitation that we can only have one “top level” command at a time to reduce the number of changes necessary.

Here’s the sample program we’ll be compiling:

```cpp
def fib(x): 
    if x < 3: 
        return 1 
    else: 
        return fib(x-1)+fib(x-2)

fib(10) 
```

## Source Code
To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter12](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter12).

## Why is this a hard problem?
Debug information is a hard problem for a few different reasons - mostly centered around optimized code. First, optimization makes keeping source locations more difficult. In LLVM IR we keep the original source location for each IR level instruction on the instruction. Optimization passes should keep the source locations for newly created instructions, but merged instructions only get to keep a single location - this can cause jumping around when stepping through optimized programs. Secondly, optimization can move variables in ways that are either optimized out, shared in memory with other variables, or difficult to track. For the purposes of this tutorial we’re going to avoid optimization (as you’ll see with one of the next sets of patches).

double main()

To keep things simple, we follow the Kaleidoscope approach and wrap the program’s top-level expression in an implicit main function.

In pyxc, all functions currently return double, so main is also emitted as a double-returning function. This is not a valid C/ABI entry point, and if you try to run the resulting executable normally, it may crash or exit unpredictably.

For now, this is acceptable.

At this stage, our goal is debugging and code generation, not producing a fully well-behaved executable. We only need the program to run far enough for a debugger (lldb) to enter main, step through the generated code, and correlate LLVM IR with source locations.

In later chapters, we will fix this properly by:
- JIT-executing code directly instead of relying on the OS entry point
- Emitting object files and linking them with a small C/C++ wrapper that provides a correct int main()
- Adding a real type system so main can explicitly return an int

Until then, treating main as a double-returning function is a deliberate and temporary simplification that lets us focus on the compiler pipeline itself.

## Compile Unit
The top level container for a section of code in DWARF is a compile unit. This contains the type and function data for an individual translation unit (read: one file of source code). So the first thing we need to do is construct one for our fig.pyxc file.

## DWARF Emission Setup
Similar to the IRBuilder class we have a [DIBuilder](https://llvm.org/doxygen/classllvm_1_1DIBuilder.html) class that helps in constructing debug metadata for an LLVM IR file. It corresponds 1:1 similarly to IRBuilder and LLVM IR, but with nicer names. Using it does require that you be more familiar with DWARF terminology than you needed to be with IRBuilder and Instruction names, but if you read through the general documentation on the [Metadata Format](https://llvm.org/docs/SourceLevelDebugging.html) it should be a little more clear. We’ll be using this class to construct all of our IR level descriptions. Construction for it takes a module so we need to construct it shortly after we construct our module. We’ve left it as a global static variable to make it a bit easier to use.

Next we’re going to create a small container to cache some of our frequent data. The first will be our compile unit, but we’ll also write a bit of code for our one type since we won’t have to worry about multiple typed expressions:

```cpp
static std::unique_ptr<DIBuilder> DBuilder;

struct DebugInfo {
  DICompileUnit *TheCU;
  DIType *DblTy;

  DIType *getDoubleTy();
} KSDbgInfo;

DIType *DebugInfo::getDoubleTy() {
  if (DblTy)
    return DblTy;

  DblTy = DBuilder->createBasicType("double", 64, dwarf::DW_ATE_float);
  return DblTy;
}
```

And then later on in main when we’re constructing our module:

```cpp
DBuilder = std::make_unique<DIBuilder>(*TheModule);

KSDbgInfo.TheCU = DBuilder->createCompileUnit(
    dwarf::DW_LANG_C, DBuilder->createFile("fib.pyxc", "."),
    "Pyxc Compiler", false, "", 0);    
```

There are a couple of things to note here. First, while we’re producing a compile unit for a language called Kaleidoscope we used the language constant for C. This is because a debugger wouldn’t necessarily understand the calling conventions or default ABI for a language it doesn’t recognize and we follow the C ABI in our LLVM code generation so it’s the closest thing to accurate. This ensures we can actually call functions from the debugger and have them execute. Secondly, you’ll see the “fib.ks” in the call to createCompileUnit. This is a default hard coded value since we’re using shell redirection to put our source into the Kaleidoscope compiler. In a usual front end you’d have an input file name and it would go there.

One last thing as part of emitting debug information via DIBuilder is that we need to “finalize” the debug information. The reasons are part of the underlying API for DIBuilder, but make sure you do this near the end of main:

```cpp
DBuilder->finalize();
```

before you dump out the module.

## Functions
Now that we have our Compile Unit and our source locations, we can add function definitions to the debug info. So in FunctionAST::codegen() we add a few lines of code to describe a context for our subprogram, in this case the “File”, and the actual definition of the function itself.

So the context:
```cpp
DIFile *Unit = DBuilder->createFile(KSDbgInfo.TheCU->getFilename(),
                                    KSDbgInfo.TheCU->getDirectory());
```

giving us an DIFile and asking the Compile Unit we created above for the directory and filename where we are currently. Then, for now, we use some source locations of 0 (since our AST doesn’t currently have source location information) and construct our function definition:

```cpp
DIScope *FContext = Unit;
unsigned LineNo = 0;
unsigned ScopeLine = 0;
DISubprogram *SP = DBuilder->createFunction(
    FContext, P.getName(), StringRef(), Unit, LineNo,
    CreateFunctionType(TheFunction->arg_size()),
    ScopeLine,
    DINode::FlagPrototyped,
    DISubprogram::SPFlagDefinition);
TheFunction->setSubprogram(SP);
```

## Source Locations
The most important thing for debug information is accurate source location - this makes it possible to map your source code back. We have a problem though, Kaleidoscope really doesn’t have any source location information in the lexer or parser so we’ll need to add it.

```cpp
struct SourceLocation {
  int Line;
  int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

static int advance() {
  int LastChar = getchar();

  if (LastChar == '\n' || LastChar == '\r') {
    LexLoc.Line++;
    LexLoc.Col = 0;
  } else
    LexLoc.Col++;
  return LastChar;
}
```

In this set of code we’ve added some functionality on how to keep track of the line and column of the “source file”. As we lex every token we set our current “lexical location” to the assorted line and column for the beginning of the token. We do this by overriding all of the previous calls to getchar() with our new advance() that keeps track of the information and then we have added to all of our AST classes a source location:

```cpp
class ExprAST {
  SourceLocation Loc;

  public:
    ExprAST(SourceLocation Loc = CurLoc) : Loc(Loc) {}
    virtual ~ExprAST() {}
    virtual Value* codegen() = 0;
    int getLine() const { return Loc.Line; }
    int getCol() const { return Loc.Col; }
    virtual raw_ostream &dump(raw_ostream &out, int ind) {
      return out << ':' << getLine() << ':' << getCol() << '\n';
    }
```

that we pass down through when we create a new expression:

```cpp
LHS = std::make_unique<BinaryExprAST>(BinLoc, BinOp, std::move(LHS),
                                       std::move(RHS));
```

giving us locations for each of our expressions and variables.

To make sure that every instruction gets proper source location information, we have to tell Builder whenever we’re at a new source location. We use a small helper function for this:

```cpp
void DebugInfo::emitLocation(ExprAST *AST) {
  if (!AST)
    return Builder->SetCurrentDebugLocation(DebugLoc());
  DIScope *Scope;
  if (LexicalBlocks.empty())
    Scope = TheCU;
  else
    Scope = LexicalBlocks.back();
  Builder->SetCurrentDebugLocation(
      DILocation::get(Scope->getContext(), AST->getLine(), AST->getCol(), Scope));
}
```

This both tells the main IRBuilder where we are, but also what scope we’re in. The scope can either be on compile-unit level or be the nearest enclosing lexical block like the current function. To represent this we create a stack of scopes in DebugInfo:

```cpp
std::vector<DIScope *> LexicalBlocks;
```

and push the scope (function) to the top of the stack when we start generating the code for each function:

```cpp
KSDbgInfo.LexicalBlocks.push_back(SP);
```

Also, we may not forget to pop the scope back off of the scope stack at the end of the code generation for the function:

```cpp
// Pop off the lexical block for the function since we added it
// unconditionally.
KSDbgInfo.LexicalBlocks.pop_back();
```

Then we make sure to emit the location every time we start to generate code for a new AST object:

```cpp
KSDbgInfo.emitLocation(this);
```

## Variables
Now that we have functions, we need to be able to print out the variables we have in scope. Let’s get our function arguments set up so we can get decent backtraces and see how our functions are being called. It isn’t a lot of code, and we generally handle it when we’re creating the argument allocas in FunctionAST::codegen.

```cpp
// Record the function arguments in the NamedValues map.
NamedValues.clear();
unsigned ArgIdx = 0;
for (auto &Arg : TheFunction->args()) {
  // Create an alloca for this variable.
  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

  // Create a debug descriptor for the variable.
  DILocalVariable *D = DBuilder->createParameterVariable(
      SP, Arg.getName(), ++ArgIdx, Unit, LineNo, KSDbgInfo.getDoubleTy(),
      true);

  DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
                          DILocation::get(SP->getContext(), LineNo, 0, SP),
                          Builder->GetInsertBlock());

  // Store the initial value into the alloca.
  Builder->CreateStore(&Arg, Alloca);

  // Add arguments to variable symbol table.
  NamedValues[std::string(Arg.getName())] = Alloca;
}
```

Here we’re first creating the variable, giving it the scope (SP), the name, source location, type, and since it’s an argument, the argument index. Next, we create a #dbg_declare record to indicate at the IR level that we’ve got a variable in an alloca (and it gives a starting location for the variable), and setting a source location for the beginning of the scope on the declare.

One interesting thing to note at this point is that various debuggers have assumptions based on how code and debug information was generated for them in the past. In this case we need to do a little bit of a hack to avoid generating line information for the function prologue so that the debugger knows to skip over those instructions when setting a breakpoint. So in FunctionAST::CodeGen we add some more lines:

```cpp
// Unset the location for the prologue emission (leading instructions with no
// location in a function are considered part of the prologue and the debugger
// will run past them when breaking on a function)
KSDbgInfo.emitLocation(nullptr);
```

and then emit a new location when we actually start generating code for the body of the function:

```cpp
KSDbgInfo.emitLocation(Body.get());
```

With this we have enough debug information to set breakpoints in functions, print out argument variables, and call functions. Not too bad for just a few simple lines of code!


## Compiling
```bash
cd code/chapter12 && ./build.sh
```

```bash
# Compile fib using pyxc
./code/chapter12/build/pyxc < code/chapter12/fib.pyxc |& clang -g -x ir - -o fib
```

```bash
# Debug
$ lldb ./fib
(lldb) target create "./fib"
Current executable set to './fib' (arm64).
(lldb) b main
Breakpoint 1: where = fib`main at fib.pyxc:7:2
(lldb) run
Process launched: './fib'
Process stopped
* thread #1, stop reason = breakpoint
    frame #0: fib`main at fib.pyxc:7:2
   4           else:
   5               return fib(x-1)+fib(x-2)
   6
-> 7       fib(10)
(lldb) q
(lldb) Quitting LLDB will kill one or more processes. Do you really want to proceed: [Y/n] y
$
```

## Core Foundation Conclusion

This is the last chapter of the original Core Foundation run.

From here onward, we treat the frontend like a real compiler project: parser architecture, semantic stability, toolchain UX, interop, and regression tests become first-class concerns.

If you want a practical handoff point, Chapter 13 is now that bridge.


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

# 10. Conclusion and Other Useful LLVM Tidbits

## Core Foundation Conclusion

Welcome to the final chapter of the Core Foundation section of the [Implementing a language with LLVM](chapter-00.md) tutorial. In the course of these chapters, we have grown our little Pyxc language from being a useless toy, to being a semi-interesting (but probably still useless) toy. :)

It is interesting to see how far we've come, and how little code it has taken. We built the entire lexer, parser, AST, code generator, an interactive run-loop (with a JIT!), and emitted debug information in standalone executables - all in under 1000 lines of (non-comment/non-blank) code.

Our little language supports a couple of interesting features: it supports user defined binary and unary operators, it uses JIT compilation for immediate evaluation, and it supports a few control flow constructs with SSA construction.

Part of the idea of this tutorial was to show you how easy and fun it can be to define, build, and play with languages. Building a compiler need not be a scary or mystical process! Now that you've seen some of the basics, we encourage you to continue with the **Intermediate Topics** chapters, where we'll tackle the features we promised at the outset:

## What's Coming in Intermediate Topics

In the remaining chapters of this tutorial, we will build upon this foundation to add the features that make Pyxc a more complete, Python-inspired language:

**Python-style indentation** - We'll modify the lexer to track indentation levels and generate INDENT/DEDENT tokens, making Pyxc feel more like Python while maintaining the LLVM backend.

**Type system and annotations** - We'll add basic type annotations for functions and implement simple type inference for local variables, showing how to map high-level types to LLVM's type system.

**Structures and named tuples** - Adding structured data types will demonstrate how to use LLVM's struct types and handle memory layout for composite types.

**First-class functions and closures** - We'll make functions true first-class citizens with closures, showing how LLVM handles indirect calls and closure environments.

**Classes and methods** - Simple object-oriented features with single inheritance, methods, and dynamic dispatch will demonstrate vtables in LLVM.

**Arrays and collections** - Practical data structures with bounds checking will introduce LLVM's array types and pointer operations.

**Modules and separate compilation** - A simple module system will show how to compile and link multiple modules together using LLVM's module linking capabilities.

**Standard library integration** - We'll show how to call the C standard library and create a minimal runtime for essential built-in functions.

**Better error handling** - Improved diagnostics with source location tracking will show how to create a better developer experience.

Of course, you're also welcome to take the code and hack on it yourself! Try adding your own features or exploring some of these interesting possibilities:

**Global variables** - While global variables have questionable value in modern software engineering, they are often useful when putting together quick little hacks. Fortunately, our current setup makes it very easy to add global variables: just have value lookup check to see if an unresolved variable is in the global variable symbol table before rejecting it. To create a new global variable, make an instance of the LLVM GlobalVariable class.

**Memory management** - Currently we can only access the stack in Pyxc. It would be useful to be able to allocate heap memory, either with calls to the standard libc malloc/free interface or with a garbage collector. If you would like to use garbage collection, note that LLVM fully supports [Accurate Garbage Collection](https://llvm.org/docs/GarbageCollection.html) including algorithms that move objects and need to scan/update the stack.

**Exception handling support** - LLVM supports generation of [zero cost exceptions](https://llvm.org/docs/ExceptionHandling.html) which interoperate with code compiled in other languages. You could also generate code by implicitly making every function return an error value and checking it. You could also make explicit use of setjmp/longjmp. There are many different ways to go here.

**Generics, database access, complex numbers, geometric programming, …** - Really, there is no end of crazy features that you can add to the language.

**Unusual domains** - We've been talking about applying LLVM to a domain that many people are interested in: building a compiler for a specific language. However, there are many other domains that can use compiler technology that are not typically considered. For example, LLVM has been used to implement OpenGL graphics acceleration, translate C++ code to ActionScript, and many other cute and clever things. Maybe you will be the first to JIT compile a regular expression interpreter into native code with LLVM?

Have fun - try doing something crazy and unusual. Building a language like everyone else always has, is much less fun than trying something a little crazy or off the wall and seeing how it turns out. If you get stuck or want to talk about it, please post on the [LLVM forums](https://discourse.llvm.org/): it has lots of people who are interested in languages and are often willing to help out.

Before we move on to the Intermediate Topics, let's talk about some "tips and tricks" for generating LLVM IR. These are some of the more subtle things that may not be obvious, but are very useful if you want to take advantage of LLVM's capabilities.

## Properties of LLVM IR

We have a couple of common questions about code in the LLVM IR form - let's just get these out of the way right now, shall we?

### Target Independence

Pyxc is an example of a "portable language": any program written in Pyxc will work the same way on any target that it runs on. Many other languages have this property, e.g. lisp, java, haskell, javascript, python, etc (note that while these languages are portable, not all their libraries are).

One nice aspect of LLVM is that it is often capable of preserving target independence in the IR: you can take the LLVM IR for a Pyxc-compiled program and run it on any target that LLVM supports, even emitting C code and compiling that on targets that LLVM doesn't support natively. You can trivially tell that the Pyxc compiler generates target-independent code because it never queries for any target-specific information when generating code.

The fact that LLVM provides a compact, target-independent, representation for code gets a lot of people excited. Unfortunately, these people are usually thinking about C or a language from the C family when they are asking questions about language portability. I say "unfortunately", because there is really no way to make (fully general) C code portable, other than shipping the source code around (and of course, C source code is not actually portable in general either - ever port a really old application from 32- to 64-bits?).

The problem with C (again, in its full generality) is that it is heavily laden with target-specific assumptions. As one simple example, the preprocessor often destructively removes target-independence from the code when it processes the input text:

```cpp
#ifdef __i386__
  int X = 1;
#else
  int X = 42;
#endif
```

While it is possible to engineer more and more complex solutions to problems like this, it cannot be solved in full generality in a way that is better than shipping the actual source code.

That said, there are interesting subsets of C that can be made portable. If you are willing to fix primitive types to a fixed size (say int = 32-bits, and long = 64-bits), don't care about ABI compatibility with existing binaries, and are willing to give up some other minor features, you can have portable code. This can make sense for specialized domains such as an in-kernel language.

### Safety Guarantees

Many of the languages above are also "safe" languages: it is impossible for a program written in Java to corrupt its address space and crash the process (assuming the JVM has no bugs). Safety is an interesting property that requires a combination of language design, runtime support, and often operating system support.

It is certainly possible to implement a safe language in LLVM, but LLVM IR does not itself guarantee safety. The LLVM IR allows unsafe pointer casts, use after free bugs, buffer over-runs, and a variety of other problems. Safety needs to be implemented as a layer on top of LLVM and, conveniently, several groups have investigated this. Ask on the [LLVM forums](https://discourse.llvm.org/) if you are interested in more details.

### Language-Specific Optimizations

One thing about LLVM that turns off many people is that it does not solve all the world's problems in one system. One specific complaint is that people perceive LLVM as being incapable of performing high-level language-specific optimization: LLVM "loses too much information". Here are a few observations about this:

First, you're right that LLVM does lose information. For example, as of this writing, there is no way to distinguish in the LLVM IR whether an SSA-value came from a C "int" or a C "long" on an ILP32 machine (other than debug info). Both get compiled down to an 'i32' value and the information about what it came from is lost. The more general issue here, is that the LLVM type system uses "structural equivalence" instead of "name equivalence". Another place this surprises people is if you have two types in a high-level language that have the same structure (e.g. two different structs that have a single int field): these types will compile down into a single LLVM type and it will be impossible to tell what it came from.

Second, while LLVM does lose information, LLVM is not a fixed target: we continue to enhance and improve it in many different ways. In addition to adding new features (LLVM did not always support exceptions or debug info), we also extend the IR to capture important information for optimization (e.g. whether an argument is sign or zero extended, information about pointers aliasing, etc). Many of the enhancements are user-driven: people want LLVM to include some specific feature, so they go ahead and extend it.

Third, it is possible and easy to add language-specific optimizations, and you have a number of choices in how to do it. As one trivial example, it is easy to add language-specific optimization passes that "know" things about code compiled for a language. In the case of the C family, there is an optimization pass that "knows" about the standard C library functions. If you call "exit(0)" in main(), it knows that it is safe to optimize that into "return 0;" because C specifies what the 'exit' function does.

In addition to simple library knowledge, it is possible to embed a variety of other language-specific information into the LLVM IR. If you have a specific need and run into a wall, please bring the topic up on the llvm-dev list. At the very worst, you can always treat LLVM as if it were a "dumb code generator" and implement the high-level optimizations you desire in your front-end, on the language-specific AST.

## Tips and Tricks

There is a variety of useful tips and tricks that you come to know after working on/with LLVM that aren't obvious at first glance. Instead of letting everyone rediscover them, this section talks about some of these issues.

### Implementing Portable offsetof/sizeof

One interesting thing that comes up, if you are trying to keep the code generated by your compiler "target independent", is that you often need to know the size of some LLVM type or the offset of some field in an llvm structure. For example, you might need to pass the size of a type into a function that allocates memory.

Unfortunately, this can vary widely across targets: for example the width of a pointer is trivially target-specific. However, there is a [clever way to use the getelementptr instruction](http://nondot.org/sabre/LLVMNotes/SizeOf-OffsetOf-VariableSizedStructs.txt) that allows you to compute this in a portable way.

### Garbage Collected Stack Frames

Some languages want to explicitly manage their stack frames, often so that they are garbage collected or to allow easy implementation of closures. There are often better ways to implement these features than explicit stack frames, but [LLVM does support them](http://nondot.org/sabre/LLVMNotes/ExplicitlyManagedStackFrames.txt), if you want. It requires your front-end to convert the code into [Continuation Passing Style](http://en.wikipedia.org/wiki/Continuation-passing_style) and the use of tail calls (which LLVM also supports).

This concludes the Core Foundation section! Thank you for following along.

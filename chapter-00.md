---
title: "My First Language Frontend with LLVM Tutorial"
---

# Pyxc: My First Language Frontend with LLVM Tutorial

!!!note Requirements: This tutorial assumes you know C++, but no previous compiler experience is necessary.

## Credits
This tutorial is HEAVILY inspired by and builds upon the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html) by Chris Lattner and others. In fact, for the Core Foundation section, the tutorial reproduces the code from the original tutorial with minor adapatations to the python syntax. All credit goes to the original authors! In the later chapters, however, the tutorial's mission is to extend these ideas with:

- Python-inspired syntax (colons, familiar keywords, and in later chapters, indentation)
- Object-oriented features (classes, structs)
- More extensive type system
- Additional chapters covering real-world language features

Many thanks to the LLVM team for their outstanding educational materials and to the open-source community for making compiler development accessible.

## Introduction
Welcome to the *Pyxc: My First Language Frontend with LLVM* tutorial. Here we run through the implementation of a simple language, showing how fun and easy it can be. This tutorial will get you up and running fast and show a concrete example of something that uses LLVM to generate code.

This tutorial introduces the simple `Pyxc` language, building it iteratively over the course of several chapters, showing how it is built over time. This lets us cover a range of language design and LLVM-specific ideas, showing and explaining the code for it all along the way, and reduces the overwhelming amount of details up front. We strongly encourage that you work with this code - make a copy and hack it up and experiment.

**Warning:** In order to focus on teaching compiler techniques and LLVM specifically, the first part of the tutorial does not show best practices in software engineering principles. For example, the code uses global variables pervasively, doesn’t use [visitors](http://en.wikipedia.org/wiki/Visitor_pattern), etc… but instead keeps things simple and focuses on the topics at hand. However, we will adapt this to more formal techniques in later chapters, once you've had a chance to familiarize yourself with the LLVM pipeline in an easier setting.

This tutorial is structured into chapters covering individual topics, allowing you to skip ahead as you wish:

## Core Foundation

[Chapter #1: Pyxc language and Lexer](chapter-01.md) - This shows where we are going and the basic functionality that we want to build. A lexer is also the first part of building a parser for a language, and we use a simple C++ lexer which is easy to understand.

[Chapter #2: Implementing a Parser and AST](chapter-02.md) - With the lexer in place, we can talk about parsing techniques and basic AST construction. This tutorial describes recursive descent parsing and operator precedence parsing.

[Chapter #3: Code generation to LLVM IR](chapter-03.md) - with the AST ready, we show how easy it is to generate LLVM IR, and show a simple way to incorporate LLVM into your project.

[Chapter #4: Adding JIT and Optimizer Support](chapter-04.md) - One great thing about LLVM is its support for JIT compilation, so we’ll dive right into it and show you the 3 lines it takes to add JIT support. Later chapters show how to generate .o files.

[Chapter #5: Extending the Language: Control Flow](chapter-05.md) - With the basic language up and running, we show how to extend it with control flow operations (‘if’ statement and a ‘for’ loop). This gives us a chance to talk about SSA construction and control flow.

[Chapter #6: Extending the Language: User-defined Operators](chapter-06.md) - This chapter extends the language to let users define arbitrary unary and binary operators - with assignable precedence! This allows us to build a significant piece of the “language” as library routines.

[Chapter #7: Extending the Language: Mutable Variables](chapter-07.md) - This chapter talks about adding user-defined local variables along with an assignment operator. This shows how easy it is to construct SSA form in LLVM: LLVM does not require your front-end to construct SSA form in order to use it!

[Chapter #8: Compiling to Object Files](chapter-08.md) - This chapter explains how to take LLVM IR and compile it down to object files, like a static compiler does.

**Chapter #9: Debug Information** - A real language needs to support debuggers, so we add debug information that allows setting breakpoints in Pyxc functions, print out argument variables, and call functions!

**Chapter #10: Conclusion and other tidbits** - This chapter wraps up the *Core Foundation* series by discussing ways to extend the language and includes pointers to info on *special topics* like adding garbage collection support, exceptions, debugging, support for *spaghetti stacks*, etc.

## Intermediate topics

**Chapter #11: Python-style Indentation and Blocks** - Pyxc's syntax uses indentation to denote blocks, just like Python. This chapter shows how to modify the lexer to track indentation levels and automatically generate INDENT/DEDENT tokens, making the parser indentation-aware without explicit braces.

**Chapter #12: Basic Type System and Annotations** - Every practical language needs some form of type checking. We implement basic type annotations for functions, simple type inference for local variables, and show how to generate appropriate LLVM types from our AST nodes.

**Chapter #13: Structures and Named Tuples** - Adding structured data types to Pyxc. We implement simple structs (similar to C structs) with field access, and Python-style named tuples. This demonstrates how to use LLVM's struct types and handle memory layout for composite types.

**Chapter #14: First-class Functions and Closures** - Making functions truly first-class citizens. We implement function pointers, higher-order functions, and simple closures with explicit capture lists. This chapter shows how LLVM handles indirect calls and closure environments.

**Chapter #15: Simple Classes and Methods** - Adding object-oriented features without complexity. We implement single inheritance, methods with explicit self parameter, and basic constructors. This demonstrates vtables and dynamic dispatch in LLVM.

**Chapter #16: Arrays and Basic Collections** - Practical data structures for Pyxc. We implement fixed-size arrays, bounds checking, and show how to create simple list-like types. This introduces LLVM's array types and pointer operations.

**Chapter #17: Modules and Separate Compilation** - Breaking code into multiple files. We implement a simple module system with import statements and show how to compile and link multiple modules together using LLVM's module linking capabilities.

**Chapter #18: Standard Library and Built-in Functions** - A language isn't complete without basic utilities. We show how to implement essential built-in functions (print, len, file I/O) by calling the C standard library, and create a minimal runtime for Pyxc.

**Chapter #19: Better Error Handling and Diagnostics** - Improving the developer experience. We add source location tracking, better error messages with context, and simple warning systems. This shows how to integrate diagnostics throughout the compiler pipeline.

**Chapter #20: Concluding Intermediate Topics** - This chapter wraps up the Intermediate Topics series by discussing advanced extension possibilities and includes pointers to further topics like concurrency support, foreign function interfaces, custom optimizations, and language interoperability.

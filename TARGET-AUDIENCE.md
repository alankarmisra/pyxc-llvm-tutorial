# Target Audience

## Who This Tutorial Is For

This tutorial is written for working programmers who are curious about how compilers work but have never built one. You do not need a computer science degree. You do not need prior compiler experience. You do need to be comfortable reading and writing code.

### You will be fine if you:

- Are comfortable writing C++. Classes, pointers, references, and the standard library are assumed throughout.
- Understand what a function, a variable, and a return value are. That's the baseline.
- Have a rough idea of what a compiler does — it turns source code into something a machine can run — even if you have no idea how.
- Are comfortable with the command line: running a build, passing flags, reading terminal output.
- Can read a simple grammar rule like `expression = term { "+" term }` without panicking.

### You will struggle if you:

- Have never written code in any language. This tutorial assumes programming fundamentals throughout.
- Are not comfortable reading error messages and working out what went wrong. We explain concepts carefully, but debugging your own setup requires some independence.
- Need every C++ concept explained from scratch. We explain LLVM concepts in detail, but we assume you can look up what `unique_ptr` or `make_unique` does.

### You do not need to know:

- Anything about LLVM before starting. We introduce every LLVM concept when it appears.
- Assembly language or CPU architecture. We mention registers and instructions occasionally but never require deep knowledge of them.
- Formal language theory — grammars, automata, parsing theory. We use grammars as a notation tool, not a subject of study.
- How operating systems work. We mention stack frames and memory in plain terms when relevant.

## What Kind of Programmer Gets the Most Out of This

The ideal reader has been writing code for a few years in any language — Python, JavaScript, Go, Rust, Java, anything — and has occasionally wondered what happens between "I wrote this code" and "it runs." They are not intimidated by a long project. They like understanding things from first principles. They have tried to read the LLVM documentation and found it assumes too much.

If you have ever thought "I want to know how a compiler actually works, not just in theory" — this tutorial is for you.

## A Note on C++

Pyxc is written in C++ because LLVM is a C++ library. You should be comfortable writing C++ before starting — classes, pointers, references, and the standard library. We explain every LLVM concept in detail, but we do not explain C++ itself.

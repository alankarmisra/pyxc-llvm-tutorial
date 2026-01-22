# Pyxc (Pixie) - Building a Pythonic Compiled Language

## Overview
Pyxc (pronounced "Pixie") is a compiled language with Python-inspired syntax, built from scratch using LLVM. This tutorial guides you through implementing a real programming language—from lexer to code generation—while keeping the familiar feel of Python that developers already know.

Unlike interpreters, pyxc compiles to native code for performance, but maintains the clean, readable syntax that makes Python popular. This makes it an ideal learning project for understanding how compiled languages work under the hood.

## Why Pyxc?
- **Familiar syntax**: If you know Python, you already know most of pyxc
- **Real compilation**: Generates native machine code via LLVM
- **Educational**: Learn compiler design by building something practical
- **Extensible**: Foundation for adding advanced features like classes, type systems, and more

## What You'll Build

### Core Tutorial
1. **Lexer**: Tokenizing Python-like syntax (indentation-aware)
2. **Parser & AST**: Building an abstract syntax tree
3. **Code Generation**: LLVM IR generation and JIT compilation
4. **Optimizer**: Applying LLVM optimization passes
5. **Control Flow**: `if`/`then`/`else` expressions
6. **Loops**: `for` loops with ranges
7. **Mutable Variables**: `var` keyword for explicit mutability
8. **User-Defined Operators**: Custom unary and binary operators with decorators

### Extended Features (Planned)
- **Structs**: Value types and data structures
- **Classes**: Object-oriented programming with inheritance
- **Type System**: Static type checking and inference
- **Standard Library**: Built-in functions and utilities
- **Module System**: Code organization and imports

### Future Possibilities
- **MLIR Backend**: Once the language stabilizes, explore MLIR for advanced optimizations and multi-level representation

## Prerequisites
- **C++ knowledge**: Comfortable with modern C++ (C++14 or later)
- **LLVM installed**: Version 14.0 or higher recommended
- **Compiler basics**: Helpful but not required—we'll explain as we go

## Getting Started

### Building the Compiler
```bash
# Clone the repository
git clone https://github.com/alankarmisra/pyxc-llvm
cd pyxc-llvm

# Build a specific chapter
cd code/chapter1
clang++ -g pyxc.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o pyxc

# Run the REPL
./pyxc
```

### Example Code
```python
# Define a function
def fib(n):
    if n < 2:
        return n
    else:
        return fib(n-1) + fib(n-2)

# Call it
fib(10)
```

## Project Structure
```
pyxc-llvm/
├───── docs/           # Tutorial chapters in markdown
├───── code/           # Complete code for each chapter
│   ├── chapter1/
│   ├── chapter2/
│   └── ...
└───── README.md
```

## Credits
This tutorial is inspired by and builds upon the excellent [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html) by Chris Lattner and others. The original tutorial introduces compiler concepts through a simple functional language. Pyxc extends these ideas with:

- Python-inspired syntax (indentation, colons, familiar keywords)
- Explicit mutability with `var`/`let` keywords
- Object-oriented features (classes, structs)
- More extensive type system
- Additional chapters covering real-world language features

Many thanks to the LLVM team for their outstanding educational materials and to the open-source community for making compiler development accessible.

## License
MIT

## Contributing
Contributions, issues, and feature requests are welcome! Feel free to check the issues page.

## Roadmap
- [x] Core language features (chapters 1-8)
- [ ] Structs and value types
- [ ] Classes and OOP
- [ ] Type checking and inference
- [ ] Standard library
- [ ] Module system
- [ ] MLIR backend (long-term)

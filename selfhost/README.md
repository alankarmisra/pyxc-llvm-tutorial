# Pyxc Self-Hosting Project

This directory contains the self-hosting implementation of the Pyxc compiler - a version of Pyxc written in Pyxc itself.

## Overview

The goal is to bootstrap the Pyxc compiler by:
1. Creating a C bridge to LLVM (Phase 0) ✅
2. Implementing a lexer in Pyxc (Phase 1)
3. Implementing an AST in Pyxc (Phase 2)
4. Implementing a parser in Pyxc (Phase 3)
5. Implementing code generation in Pyxc (Phase 4)
6. Creating the main driver (Phase 5)
7. Bootstrapping! (Phase 6)

## Current Status

**Phase 0: Foundation** - ✅ COMPLETE

Files created:
- `llvm_bridge.h` - C API declarations for LLVM
- `llvm_bridge.cpp` - C++ implementation wrapping LLVM
- `string_utils.c` - String manipulation helpers
- `file_utils.c` - File I/O helpers
- `test_bridge.c` - Test program to verify bridge works
- `Makefile` - Build system

## Building

### Prerequisites

- LLVM 21+ installed and `llvm-config` in PATH
- Clang compiler
- CMake 3.16+

### Build and Test

```bash
# Configure (from selfhost directory)
cmake -B build

# Build
cmake --build build

# Run the bridge test
./build/test_bridge

# Or use CMake's test runner
cd build && ctest --output-on-failure
```

The test program creates a simple LLVM function and compiles it to an object file (`test_add.o`).

### Mac-Specific Notes

The CMakeLists.txt automatically detects Homebrew's zstd library on Mac. If you need to specify a custom LLVM location:

```bash
cmake -B build -DLLVM_PREFIX=$HOME/llvm-21-with-clang-lld-lldb
```

## Phase 0 Verification

Run `./build/test_bridge`. You should see:
```
=== LLVM Bridge Test ===
Creating LLVM context...
Creating LLVM module...
...
=== All tests passed! ===
```

And a file `test_add.o` should be created.

## Next Steps

Once Phase 0 is verified:
1. Start Phase 1 - implement `lexer.pyxc`
2. See `SELFHOSTING.md` for the complete roadmap

## File Organization

```
selfhost/
├── README.md              # This file
├── SELFHOSTING.md         # Complete self-hosting plan
├── Makefile               # Build system
├── llvm_bridge.h          # LLVM C API
├── llvm_bridge.cpp        # LLVM bridge implementation
├── string_utils.c         # String helpers
├── file_utils.c           # File I/O helpers
├── test_bridge.c          # Phase 0 test
└── (future phases...)
    ├── lexer.pyxc         # Phase 1
    ├── ast.pyxc           # Phase 2
    ├── parser.pyxc        # Phase 3
    ├── codegen.pyxc       # Phase 4
    └── pyxc.pyxc          # Phase 5 - self-hosted compiler!
```

## Architecture

```
┌─────────────────────────┐
│   pyxc.pyxc             │  ← Compiler in pyxc (future)
└───────────┬─────────────┘
            │ extern calls
            ↓
┌─────────────────────────┐
│   llvm_bridge.c         │  ← Phase 0 (current)
│   string_utils.c        │
│   file_utils.c          │
└───────────┬─────────────┘
            │ C++ API
            ↓
┌─────────────────────────┐
│   LLVM Libraries        │
└─────────────────────────┘
```

## Notes

- The bridge uses opaque pointers (`void*`) for all LLVM objects
- This allows Pyxc to work with LLVM without understanding C++ classes
- All bridge functions use `extern "C"` linkage for compatibility
- Memory management is manual - Pyxc code must free allocated objects

## Progress Tracking

See `SELFHOSTING.md` for the complete phase-by-phase plan and progress checklist.

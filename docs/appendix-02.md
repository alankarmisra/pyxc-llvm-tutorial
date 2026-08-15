---
section: "Appendices"
title: "Appendix 2: Coverage"
description: "Verifying test completeness, and its limitations, with LLVM's coverage tool"
---

# 2. Verifying Test Completeness With `llvm-cov`

## Why Passing Tests Aren't Enough

Every test in every chapter can pass while a whole branch of `pyxc.cpp` never executes once. A green `lit` run only tells me the tests I wrote behave as I expect; it says nothing about the tests I forgot to write. LLVM ships its own answer to that: Clang's source-based code coverage, built on the same toolchain I already use to compile pyxc itself. I use it here the same way I used it to close real gaps in several chapters throughout this tutorial.

## Building With Coverage Instrumentation

I build a second, separate copy of the chapter binary with two extra compiler flags, so my normal `build/` directory stays untouched. `cmake -S . -B build-cov` creates that second build directory on its own; I don't need to make it myself first.

`-fprofile-instr-generate` makes Clang insert a counter increment at every region of code as it compiles. `-fcoverage-mapping` makes it also embed a table in the binary tying each counter back to a real line and column range in `pyxc.cpp`. Neither flag changes what the program computes, only what it silently counts while doing it. This is a Clang-specific mechanism — GCC has a different, coarser scheme (`-fprofile-arcs -ftest-coverage`, the classic `gcov` format), and MSVC's `cl.exe` has no equivalent — so `CMAKE_CXX_COMPILER` has to point at an actual `clang`/`clang++`/`clang-cl`, not whatever C++ compiler happens to be default on the system.

That compiler also needs to be a Clang build that includes `compiler-rt`'s profiling runtime library. If I built LLVM from source in [Chapter 6](chapter-06.md) without also building `compiler-rt`, that from-source Clang won't have it, and linking fails looking for `libclang_rt.profile`. A Clang installed via Homebrew, `apt`, or the official Windows installer normally has it already.

### macOS / Linux

```bash
cmake -S . -B build-cov \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"
cmake --build build-cov
```

If this fails looking for `libclang_rt.profile`, `clang++` on your `PATH` resolved to a from-source build missing `compiler-rt`. Point `CMAKE_CXX_COMPILER` at a system-installed Clang instead — on macOS that's usually `/usr/bin/clang++` (Apple's system Clang, which ships this runtime), on Linux whatever `clang++` your package manager installed (e.g. `/usr/bin/clang++-18` for a Homebrew/apt Clang, if the from-source build is what your plain `clang++` resolves to).

### Windows (PowerShell)

```powershell
cmake -S . -B build-cov `
  -DCMAKE_CXX_COMPILER=clang++ `
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" `
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"
cmake --build build-cov
```

The official LLVM Windows installer from [Chapter 6](chapter-06.md) includes `compiler-rt`, so this should work directly against the `clang++` it put on your `PATH`.

## Running Tests And Capturing Profiles

`lit` runs the instrumented binary once per `# RUN:` line, and each process needs to write its own profile file or they overwrite each other. I point `%pyxc` at the instrumented binary and give each process a unique output file with a `%p` (process ID) pattern.

(`test/lit.cfg.py` normally hardcodes `%pyxc` to `build/pyxc`; I temporarily repoint it at `build-cov/pyxc` for this run, then revert it, so my regular build stays what everyone else's `git clone` produces.)

### macOS / Linux

```bash
mkdir -p pyxc-cov
LLVM_PROFILE_FILE="pyxc-cov/%p.profraw" lit test/ -j1
```

### Windows (PowerShell)

```powershell
mkdir pyxc-cov
$env:LLVM_PROFILE_FILE = "pyxc-cov\%p.profraw"
lit test/ -j1
```

## Merging And Reading The Report

Each test process left its own `.profraw` file in `pyxc-cov/`. I fold them into one summary and ask `llvm-cov` for a report — these two commands are identical on every platform:

```bash
llvm-profdata merge -sparse pyxc-cov/*.profraw -o pyxc-cov/merged.profdata
llvm-cov report build-cov/pyxc -instr-profile=pyxc-cov/merged.profdata pyxc.cpp
```

That prints one line per file: how many regions, lines, functions, and branches exist, and how many of each were never executed by any test.

## Finding The Exact Missing Lines

The report tells me *how much* is missing; `llvm-cov show` tells me *which lines*. The `llvm-cov show` invocation itself is identical everywhere; only the tool I use to filter its output for zero-count lines differs.

### macOS / Linux

```bash
llvm-cov show build-cov/pyxc -instr-profile=pyxc-cov/merged.profdata pyxc.cpp | awk '/^ *[0-9]+\| *0\|/'
```

### Windows (PowerShell)

```powershell
llvm-cov show build-cov/pyxc -instr-profile=pyxc-cov/merged.profdata pyxc.cpp | Select-String -Pattern '^\s*\d+\|\s*0\|'
```

Either way, that prints every line whose execution count is exactly `0`, with its real line number, so I can jump straight to it in `pyxc.cpp`.

## Worked Example: Chapter 3

Before I added a couple of tests to [Chapter 3](chapter-03.md), its report looked like this:

```text
Filename    Regions  Missed Regions   Cover   Functions  Missed Functions  Executed   Lines  Missed Lines   Cover   Branches  Missed Branches   Cover
pyxc.cpp        213               2  99.06%          29                 0  100.00%     277              2  99.28%       140                2  98.57%
```

Two lines, unreached by every one of chapter 3's 35 tests:

```text
384|      0|      return nullptr;
428|      0|      return nullptr;
```

Both turned out to be the same shape of gap in two different places: `ParseTerm` and `ParseComparison` each have a `while` loop that consumes an operator (`*`/`/` for one, `<` for the other) and then parses the right-hand operand. If that operand fails to parse — an operator with nothing after it, like `1 * +` — the function returns `nullptr` on the spot. Nothing in chapter 3's test suite ever fed it an operator with a missing right-hand side for *these two* tiers; a sibling test already covered the identical shape one tier up, in `ParseSum` (`1 + +`), which is what made the gap easy to miss by inspection alone.

I added two lines to `test/coverage_error_paths.pyxc`:

```pyxc
# RUN: printf '1 * +\n' | %pyxc 2>&1 | FileCheck %s --check-prefix=TERM_RHS
# RUN: printf '1 < +\n' | %pyxc 2>&1 | FileCheck %s --check-prefix=COMPARISON_RHS

# TERM_RHS: unknown token when expecting an expression
# COMPARISON_RHS: unknown token when expecting an expression
```

Re-running the whole pipeline:

```text
Filename    Regions  Missed Regions   Cover   Functions  Missed Functions  Executed   Lines  Missed Lines   Cover   Branches  Missed Branches   Cover
pyxc.cpp        213               0 100.00%          29                 0  100.00%     277              0 100.00%       140                0  100.00%
```

## Limitations

**Coverage tells me code ran, not that it ran correctly.** A line can execute and still compute the wrong thing; `llvm-cov` has no idea what the *right* answer was supposed to be, only that some instruction executed. In [Chapter 2](chapter-02.md) I flagged a version of exactly this problem: through Chapter 6, `pyxc` has no way to print which operator ended up in a parsed expression, so a bug that silently swapped `-` for `+` would leave every existing test green and every coverage number at 100%, because the *lines* involved in building either operator are identical, just fed a different token value. Coverage can only ever tell me a branch executed, never that the value it produced was the one I meant.

**Not every red line is a real gap.** Some lines never execute because nothing in the current grammar can reach them, not because a test is missing. I found several of these across multiple chapters: `SourceManager::reset()` is declared but never called by anything in the REPL driver; `BinaryExpressionNode::codegen()`'s `default:` case exists only because C++ requires an exhaustive `switch`, but the parser never constructs a `BinaryExpressionNode` with an operator that default case would catch. Writing a test to force these lines to run would mean calling C++ internals directly, or contriving an input that can't actually occur through the language's own grammar — not a real gap, just defensive code with no live path to it yet. The judgment call is always the same question: can real, parseable pyxc source ever reach this line? If yes, it's worth a test. If the only way to trigger it is to call the C++ function directly or violate an invariant the parser already enforces, it's fine to leave uncovered.

**100% is a floor, not a ceiling.** A test can execute every line in a function while still asserting nothing meaningful about what it returned. Coverage numbers say "this ran," never "this was checked" — that second, stronger claim is still on me, one test at a time.

## Try It

I pick any chapter and ask the same question: does its test suite actually exercise everything it claims to? I build it with coverage instrumentation, run its suite, and read the report — the same commands from the sections above, just against a chapter I haven't checked yet:

### macOS / Linux

```bash
cd code/chapter-05
cmake -S . -B build-cov \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"
cmake --build build-cov
mkdir -p pyxc-cov
LLVM_PROFILE_FILE="pyxc-cov/%p.profraw" lit test/ -j1
llvm-profdata merge -sparse pyxc-cov/*.profraw -o pyxc-cov/merged.profdata
llvm-cov report build-cov/pyxc -instr-profile=pyxc-cov/merged.profdata pyxc.cpp
```

### Windows (PowerShell)

```powershell
cd code/chapter-05
cmake -S . -B build-cov `
  -DCMAKE_CXX_COMPILER=clang++ `
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" `
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"
cmake --build build-cov
mkdir pyxc-cov
$env:LLVM_PROFILE_FILE = "pyxc-cov\%p.profraw"
lit test/ -j1
llvm-profdata merge -sparse pyxc-cov/*.profraw -o pyxc-cov/merged.profdata
llvm-cov report build-cov/pyxc -instr-profile=pyxc-cov/merged.profdata pyxc.cpp
```

If the report shows anything less than 100%, `llvm-cov show build-cov/pyxc -instr-profile=pyxc-cov/merged.profdata pyxc.cpp` piped into `awk '/^ *[0-9]+\| *0\|/'` (or `Select-String -Pattern '^\s*\d+\|\s*0\|'` on Windows) points me straight at the exact lines, ready for the same judgment call from the Limitations section above.

## What's Next

I use this workflow whenever I want a straight answer to "does this chapter's test suite actually cover what I think it does," not just "do the tests I already wrote still pass."

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`

I'll help you figure it out.

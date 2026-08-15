---
section: "Appendices"
title: "Appendix 1: Lit"
description: "lit or llvm-lit Quickstart"
---

# 1. `lit` or `llvm-lit` Quickstart

## Reading a Test File

I use LLVM's [lit](https://llvm.org/docs/CommandGuide/lit.html) test runner for all my tests. Every chapter's `test/` directory is full of small `.pyxc` files with `# RUN:` and `# CHECK:` comments at the top, and I never explain what those actually do inside a chapter itself, since it has nothing to do with the language I'm building. The `lit` documentation is a bit too terse for me too, so I'll walk through one test in simpler terms here instead, once, and point back to this appendix whenever it comes up.

`test/sample_chapter_1.pyxc` contains the same `add.pyxc` program from earlier in this chapter. I add `RUN` and `CHECK` instructions at the top:

```pyxc
# RUN: %pyxc < %s | FileCheck %s --match-full-lines

# CHECK: 'def'
# CHECK: name: print
# CHECK: number: 1
# CHECK: number: 2

# sample: the add.pyxc example from the doc, run end-to-end through the lexer.
# add.pyxc
def add(x, y): # define a function
    x + y # return the sum

print(add(1, 2)) # call the add function and print its value
```

These instructions begin with `#`, so pyxc ignores their text as comments. `lit` and `FileCheck`, however, see these as instructions.

## The RUN Instruction

I start with the `RUN` instruction:

```pyxc
# RUN: %pyxc < %s | FileCheck %s --match-full-lines
```

It tells `lit` which command to run for this test.

Before running the command, `lit` replaces two placeholders:

- `%pyxc` becomes the path to the pyxc executable, such as `build/pyxc`. I define that substitution in `test/lit.cfg.py`:

  ```python
  config.substitutions.append(
      ("%pyxc", os.path.join(chapter_dir, "build", "pyxc"))
  )
  ```

- `%s` becomes the path to the current test file, such as `test/sample_chapter_1.pyxc`.

After those replacements, the command is roughly:

```bash
build/pyxc < test/sample_chapter_1.pyxc |
    FileCheck test/sample_chapter_1.pyxc --match-full-lines
```

I use the test file in two different ways.

First, I use `< test/sample_chapter_1.pyxc` to send the entire file to pyxc as input. pyxc tokenizes the sample program.

Second, I use the pipe sends pyxc's output to `FileCheck`. The filename after `FileCheck` tells it where to find the `CHECK` instructions. In this case, those instructions are in the same test file:

```pyxc
# CHECK: 'def'
# CHECK: name: print
# CHECK: number: 1
# CHECK: number: 2
```

## Matching Full Lines With FileCheck

I use `--match-full-lines` with `FileCheck` because I want each `CHECK` pattern to match a whole output line. For example, `number: 1` should match this:

```text
number: 1
```

but not this:

```text
number: 1 <any-additional-text>
```

## The `CHECK` Instructions

I use `FileCheck` to find complete lines matching `'def'`, `name: print`, `number: 1`, `number: 2`, in that order. `# CHECK:` doesn't need the lines to be next to one another. They only need to appear in the specified order. So the following output will pass the above checks:

```text
'def'
name: print
number: 1
number: 2
```

as will this:

```text
'def'
name: print
newline
newline
number: 1
number: 2
```

## What I Check In This Test

Each check corresponds to part of the sample program:

- `'def'` is the line printed for the `def` keyword. `TokenNames` maps `tok_def` to the string `'def'`.
- `name: print` is the line printed for the `print` name.
- `number: 1` and `number: 2` are printed for the numeric literals in `add(1, 2)`.

If `FileCheck` finds all four lines in order, it exits with `0`, and the `RUN` instruction passes. If a line is missing or appears in the wrong order, `FileCheck` exits with a nonzero value, and `lit` reports the test as failed. If I run `lit` with `-v`, `FileCheck` also points me to the `CHECK` instruction it could not satisfy.

I run the sample test with:

```bash
llvm-lit -v test/sample_chapter_1.pyxc
```

I get:

```text
-- Testing: 1 tests, 1 workers --
PASS: pyxc-chapter01 :: sample_chapter_1.pyxc (1 of 1)

Testing Time: 0.12s

Total Discovered Tests: 1
  Passed: 1 (100.00%)
```

## Try It

I break the test on purpose, to see what a failure actually looks like. I change the last `CHECK` line in `test/sample_chapter_1.pyxc` so it expects text that never appears in the output:

```pyxc
# CHECK: 'def'
# CHECK: name: print
# CHECK: number: 1
# CHECK: number: 999
```

I run the same pipeline the `RUN:` line builds, by hand, so I can see `FileCheck`'s own diagnostic without `lit`'s extra wrapping around it:

```bash
build/pyxc < test/sample_chapter_1.pyxc | FileCheck test/sample_chapter_1.pyxc --match-full-lines -dump-input=never
```

```text
test/sample_chapter_1.pyxc:6:10: error: CHECK: expected string not found in input
# CHECK: number: 999
         ^
<stdin>:28:10: note: scanning from here
number: 1
         ^
<stdin>:30:1: note: possible intended match here
number: 2
^
```

`FileCheck` tells me exactly which `CHECK` line it couldn't satisfy (`number: 999`, line 6 of the test file), where in the lexer's output it was still scanning when it gave up, and its best guess at what I probably meant instead (`number: 2`, the actual next number in the output). I revert `999` back to `2` before moving on.

## What's Next

That's everything I need to read any test file in this tutorial. Back to [Chapter 1](chapter-01.md) to keep building the lexer.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:

- Your OS and version
- Full error message
- Output of `cmake --version`

I'll help you figure it out.

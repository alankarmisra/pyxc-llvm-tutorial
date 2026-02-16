# 4. Better Diagnostics, Newline-Aware Parsing, and a Real CLI Surface

## Introduction

Welcome to Chapter 4 of the [Pyxc: My First Language Frontend with LLVM](chapter-00.md) tutorial.

In [Chapter 2](chapter-02.md), we built a working recursive-descent parser and an AST. In doing so, we stripped our token printing `MainLoop()` with a promise to return to it later. 

Well, later is now. 

## Source Code

To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter04](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter04).


## CLI shape with LLVM CommandLine

Chapter 4 now uses LLVM's command-line utility (`llvm::cl`) and exposes a subcommand-oriented interface:

```bash
./pyxc repl [--emit-tokens] [--emit-llvm]
./pyxc run <file.pyxc> [--emit-llvm]
./pyxc build <file.pyxc> [--emit=llvm|obj|exe] [-g] [-O0|-O1|-O2|-O3]
```

This is implemented with `llvm::cl::SubCommand`, not ad-hoc argv parsing.

### Why use `llvm::cl` here?

- It is already part of LLVM tooling conventions.
- It gives structured subcommands and help output.
- It scales better as flags grow in later chapters.

## Chapter-appropriate behavior gates

Not every command is fully implemented yet, and that is intentional.

Current behavior in Chapter 4:

- `repl --emit-tokens`: implemented.
- `repl --emit-llvm`: accepted, prints a "not learnt yet" message.
- `run ...`: validates arguments, then prints "not learnt yet".
- `build ...`: validates arguments/options, then prints "not learnt yet".

So the CLI surface is real, but execution backends are still staged for later chapters.

## Token stream mode (chapter 1 style)

`repl --emit-tokens` prints tokens in the familiar Chapter 1 style:

- token names separated by spaces,
- `newline` ends the line and prints a new prompt,
- no extra debug scaffolding required.

Example:

```text
ready> 'def' identifier '(' identifier ')' ':' 'return' identifier '+' number newline
ready>
```

This keeps lexer-debug mode simple and readable.

## User-facing validation messages

One small but important UX detail: we use "file name" wording instead of low-level parser jargon.

For example:

- `Error: run requires a file name.`
- `Error: build requires a file name.`
- `Error: build accepts only one file name.`

That avoids exposing internal terminology like "positional argument" to learners.

## Optimization flag now, optimization work later

The `build` command accepts optimization switches in Chapter 4:

- `-O0`, `-O1`, `-O2`, `-O3`

Invalid values are rejected early.

Even though we are not yet performing the full build pipeline in this chapter, validating CLI intent now makes later transitions smoother.

## Why this chapter matters

Chapter 4 is not about adding new language constructs. It is about maturing the frontend behavior:

- diagnostics that help humans,
- parser recovery that keeps sessions alive,
- and command surfaces that look like a real compiler.

That foundation pays off heavily once we add IR emission, object generation, and linking in later chapters.

## What’s next

In the next chapters, we will connect these UX improvements to real compilation steps:

- emitting LLVM IR for code paths behind CLI switches,
- mapping build modes to actual outputs,
- and gradually removing the "not learnt yet" placeholders.

The key idea: get the interface and diagnostics right early, then plug in deeper compiler stages behind it.

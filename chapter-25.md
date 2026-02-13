# 25. Separate Compilation and Multi-File Linking

Chapter 25 upgrades pyxc from single-file flow to translation-unit flow.

You can now compile multiple `.pyxc` files in one invocation and either:

- produce one object per source file (`-c file1 file2`), or
- link all compiled objects into one executable (`pyxc file1 file2` or `pyxc -o app file1 file2`).


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter25](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter25).

## Grammar (EBNF)

Chapter 25 is primarily a driver/linker architecture chapter (multi-file compile/link).
Language grammar is effectively unchanged from Chapter 24.

Reference: `code/chapter25/pyxc.ebnf`

```ebnf
program      = { top_item } , eof ;
top_item     = newline | type_alias_decl | struct_decl
             | function_def | extern_decl | statement ;

statement    = if_stmt | for_stmt | while_stmt | do_while_stmt
             | break_stmt | continue_stmt | free_stmt | print_stmt
             | return_stmt | const_decl_stmt
             | typed_assign_stmt | assign_stmt | expr_stmt ;
```

## What we are building

We want this to work:

```bash
pyxc -o app math.pyxc main.pyxc
```

where `math.pyxc` defines a function and `main.pyxc` declares it `extern` and calls it.

That requires:

- CLI support for multiple positional inputs
- per-file compilation loop
- linker accepting multiple object files
- state reset between file compiles so file N+1 is clean

## CLI: from single input to list input

In `code/chapter25/pyxc.cpp`, positional input changed to a list:

```cpp
static cl::list<std::string> InputFilenames(cl::Positional,
                                            cl::desc("<input files>"),
                                            cl::ZeroOrMore,
                                            cl::cat(PyxcCategory));
```

Mode guards were tightened:

```cpp
if (Mode == Interpret && InputFilenames.size() != 1) ...
if (Mode == Tokens && InputFilenames.size() != 1) ...
if (Mode == Object && !OutputFilename.empty() && InputFilenames.size() != 1) ...
```

So:

- `-i` and `-t` remain intentionally single-file
- `-c` supports many inputs
- `-c -o out` only valid for one input

## Object naming for multi-file builds

Chapter 25 adds a helper to derive per-input object names:

```cpp
static std::string getIntermediateObjectFilename(const std::string &input) {
  ...
  return base + ".o";
}
```

Executable mode loop uses it:

```cpp
for (const auto &InFile : InputFilenames) {
  std::string Obj = getIntermediateObjectFilename(InFile);
  CompileToObjectFile(InFile, Obj);
  ScriptObjs.push_back(Obj);
}
```

Object mode loop also compiles each file independently.

## Linker API: one object to many objects

`code/include/PyxcLinker.h` now has multi-object entry:

```cpp
static bool Link(const std::vector<std::string> &objFiles,
                 const std::string &runtimeObj,
                 const std::string &outputExe)
```

and the old single-object signature delegates to it.

This is what makes final executable linking possible when multiple source files are compiled in one command.

## The critical stability fix: reset state per translation unit

The first implementation compiled file 1, then crashed on file 2 in some flows.

Root cause: global frontend and LLVM manager state was leaking across translation units.

Chapter 25 fixes this with explicit reset functions in `code/chapter25/pyxc.cpp`.

Frontend reset:

```cpp
static void ResetFrontendState() {
  ModuleIndentType = -1;
  AtStartOfLine = true;
  Indents = {0};
  PendingTokens.clear();
  ...
  LastChar = '\0';
}
```

Semantic-map reset:

```cpp
static void ResetCompilationState() {
  FunctionProtos.clear();
  TypeAliases.clear();
  StructDecls.clear();
  StructTypeNames.clear();
}
```

LLVM/manager reset:

```cpp
static void ResetLLVMStateForNextCompile() {
  TheSI.reset();
  ThePIC.reset();
  TheFPM.reset();
  TheLAM.reset();
  TheFAM.reset();
  TheCGAM.reset();
  TheMAM.reset();
  Builder.reset();
  TheModule.reset();
  TheContext.reset();
}
```

Debug reset:

```cpp
static void ResetDebugInfoStateForNextCompile() {
  DBuilder.reset();
  KSDbgInfo.reset();
}
```

Then `CompileToObjectFile(...)` starts with:

```cpp
ResetFrontendState();
ResetCompilationState();
NamedValues.clear();
LoopContextStack.clear();
ResetDebugInfoStateForNextCompile();
ResetLLVMStateForNextCompile();
```

This is the reason multi-file compile is now stable.

## Default aliases and data layout during per-file compile

Because type alias defaults use pointer-size assumptions, per-file compile ensures aliases are seeded after context/module creation.

Chapter 25 also makes `EnsureDefaultTypeAliases()` safe when module layout is not yet fully populated.

That avoids cross-file surprises in alias setup.

## Runtime object resolution hardening

Executable link step previously relied on `runtime.o` from current working directory.

Chapter 25 resolves runtime object relative to the pyxc binary path:

```cpp
std::string runtimeObj = "runtime.o";
if (const char *Slash = strrchr(argv[0], '/'))
  runtimeObj = std::string(argv[0], Slash - argv[0] + 1) + "runtime.o";
```

This makes lit test sandboxes and out-of-tree invocations reliable.

## New Chapter 25 tests

Added in `code/chapter25/test`:

Helper module:

```py
# c25_mod_add.pyxc
def add(a: i32, b: i32) -> i32:
    return a + b
```

Executable multi-file link:

```py
# multifile_executable_link.pyxc
extern def add(a: i32, b: i32) -> i32

def main() -> i32:
    print(add(4, 5))
    return 0

main()
```

Object multi-file mode:

```py
# multifile_object_mode.pyxc
def use() -> i32:
    return 0
```

Negative mode check:

```py
# multifile_error_tokens_requires_single.pyxc
# token mode with 2 files must fail
```

Expected message includes:

```text
token mode requires exactly one input file
```

## Main driver flow after Chapter 25

In executable mode:

- compile each input file to temporary object
- call `PyxcLinker::Link(ScriptObjs, runtimeObj, exeFile)`
- cleanup intermediate objects

In object mode:

- compile each input file to its own object
- print each produced object path

In token/interpret modes:

- reject multi-file invocation

## Outcome

After these changes:

- multi-file object and executable workflows are first-class
- translation-unit boundaries are cleanly isolated
- linker path supports real modular programs
- lit suite covers positive and negative multi-file behavior

Chapter 25 is the bridge from language features to practical project-scale compilation.

## Compiling

From repository root:

```bash
make -C code/chapter25 clean all
```

## Testing

From repository root:

```bash
lit -sv code/chapter25/test
```

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
make -C code/chapter25 clean all
```

Run one sample program:

```bash
code/chapter25/pyxc -i code/chapter25/test/c25_mod_add.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter25/test
lit -sv .
```

Pick a couple of tests, mutate the inputs, and watch how diagnostics respond.

When you're done, clean artifacts:

```bash
make -C code/chapter25 clean
```

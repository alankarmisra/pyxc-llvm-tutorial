---
description: "Add string literals: \"hello\" produces a ptr[int8] backed by a null-terminated global constant, enabling calls to puts, printf, and other C string functions."
---
# 21. pyxc: String Literals

## Where We Are

[Chapter 20](chapter-20.md) added array literals — `[1, 2, 3]` as a shorthand for initializing arrays. This chapter adds the same convenience for strings. After this chapter:

```python
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  puts("hello, pyxc")
  return 0
```

```
hello, pyxc
```

That's the whole pitch. A string literal in double quotes produces a `ptr[int8]` you can pass directly to any C function that takes `char *`.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-21
```

## Grammar

```ebnf
stringliteral ::= '"' { char | escape } '"'
escape        ::= '\' ( '\' | '"' | 'n' | 't' | '0' )

primary ::= ... | stringliteral | ...
```

Supported escape sequences: `\\` (backslash), `\"` (double quote), `\n` (newline), `\t` (tab), `\0` (null byte). Any other character after `\` is a lexer error. A string that reaches end-of-line without a closing `"` is also an error.

## The Type of a String

A string literal has type `ptr[int8]`. It is a pointer to the first byte of a null-terminated byte array. This is exactly what C calls `char *`, so it works directly with `puts`, `printf`, `fputs`, and any other C function that takes a string.

The pointer encoding is `EncodePointerType(Int8, "")` — same encoding as `ptr[int8]` declared anywhere else in Pyxc. String literals fit into the existing pointer type system without any special casing downstream.

## Lexing

`tok_string` is a new token kind. The lexer fills `StringLiteralStr` as it reads characters between the quotes, processing escape sequences on the way:

```cpp
if (LexerLastChar == '"') {
  StringLiteralStr.clear();
  LexerLastChar = advance(); // eat opening quote
  while (LexerLastChar != '"' && LexerLastChar != EOF && LexerLastChar != '\n') {
    if (LexerLastChar == '\\') {
      LexerLastChar = advance();
      if (LexerLastChar == 'n')       StringLiteralStr.push_back('\n');
      else if (LexerLastChar == 't')  StringLiteralStr.push_back('\t');
      else if (LexerLastChar == '0')  StringLiteralStr.push_back('\0');
      else if (LexerLastChar == '"' || LexerLastChar == '\\')
                                      StringLiteralStr.push_back(LexerLastChar);
      else {
        // invalid escape — emit error immediately and return tok_error
      }
    } else {
      StringLiteralStr.push_back(LexerLastChar);
    }
    LexerLastChar = advance();
  }
  if (LexerLastChar != '"') {
    // unterminated string — emit error and return tok_error
  }
  LexerLastChar = advance(); // eat closing quote
  return tok_string;
}
```

By the time `tok_string` is returned, `StringLiteralStr` already holds the decoded content — `"\n"` in source becomes a single newline byte in the string. The parser does not see raw escape sequences.

Errors (invalid escape, unterminated string) are reported with file coordinates and a source context snippet, then the lexer returns `tok_error`, which causes the parser to bail out.

## Parsing

`ParseStringExpr` is the simplest parser function in the codebase:

```cpp
static unique_ptr<ExprAST> ParseStringExpr() {
  string Val = StringLiteralStr;
  getNextToken(); // eat string literal
  return make_unique<StringExprAST>(
      std::move(Val), EncodePointerType(ValueType::Int8, ""));
}
```

It grabs the already-decoded string, advances the token, and wraps it in a `StringExprAST` whose type is `ptr[int8]`. That's it. No expected-type context needed — a string literal is always `ptr[int8]` regardless of where it appears.

```cpp
class StringExprAST : public ExprAST {
  string Text;
public:
  explicit StringExprAST(string Text, const string &PtrTypeInfo)
      : Text(std::move(Text)) {
    setType(ValueType::Pointer, PtrTypeInfo);
  }
  const string &getText() const { return Text; }
  Value *codegen() override;
};
```

## Codegen: A Global Constant

String literals are immutable data. They don't belong on the stack and they outlive any function call. The right home is a private constant global:

```cpp
static unsigned StringLiteralCounter = 0;

Value *StringExprAST::codegen() {
  std::string Name = "__str." + std::to_string(StringLiteralCounter++);
  auto *Arr = ConstantDataArray::getString(*TheContext, getText(), true);
  auto *GV = new GlobalVariable(*TheModule, Arr->getType(), true,
                                GlobalValue::PrivateLinkage, Arr, Name);
  GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(Align(1));
  Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  return Builder->CreateInBoundsGEP(Arr->getType(), GV, {Zero, Zero}, "strptr");
}
```

`ConstantDataArray::getString(ctx, text, true)` creates a `[N x i8]` constant with a null terminator appended (`true` = add null). For `"hello"`, that's `[6 x i8] c"hello\00"`.

`PrivateLinkage` means the symbol is internal to the module — no cross-module collisions even when multiple JIT modules each start `StringLiteralCounter` at zero.

`UnnamedAddr::Global` tells the linker (and the optimizer) that the address of this global is not meaningful — only its content is. This allows the optimizer to merge identical string constants rather than keeping duplicates.

The two-element GEP `{Zero, Zero}` is the standard LLVM idiom for getting a pointer to the first byte of a global array:

- The first index steps past the "enclosing object" (the global itself), always zero.
- The second index steps to byte offset zero within the `[N x i8]` array.

The result is an opaque `ptr`, the same type returned by all pointer-typed expressions in Pyxc.

For `"hello"`:

```llvm
@__str.0 = private unnamed_addr constant [6 x i8] c"hello\00", align 1

; in the function body:
%strptr = getelementptr inbounds [6 x i8], ptr @__str.0, i64 0, i64 0
```

`%strptr` is a `ptr` pointing at byte `'h'`.

## String Type Checking

Because strings are `ptr[int8]`, they participate in the same pointer type checking as everything else.

**Correct usage** — `ptr[int8]` expected:

```python
extern def puts(s: ptr[int8]) -> int
puts("hello")             # ok — string is ptr[int8]

var s: ptr[int8] = "world"  # ok
```

**Type mismatch** — `ptr[int]` expected:

```python
var p: ptr[int] = "oops"   # error — ptr[int8] != ptr[int]
```

The pointer struct name `"2:"` (for `ptr[int8]`) does not match `"1:"` (for `ptr[int]`), so the type check in `ParseVarStmt` rejects it.

**In an array of string pointers:**

```python
var msgs: ptr[int8][2] = ["one", "two"]
```

The array literal parser already received the element type `ptr[int8]` from the array's type encoding. It validates that each element's struct name matches — so `["one", "two"]` in a `ptr[int][2]` context is a type error.

## Returning a String

Functions can return string literals:

```python
def greeting() -> ptr[int8]:
  return "hello"
```

This requires `CurrentFunctionReturnStructName` to be set correctly when parsing the function body — a fix applied in this chapter to the `ReturnTypeGuard` call site in `FunctionAST::codegen`. With that in place, the return context carries the full pointer type information, and the type-check on the `return` expression succeeds.

## One String, One Global

There is no deduplication. Two occurrences of `"hello"` in the same function produce two separate globals:

```llvm
@__str.0 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@__str.1 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
```

At `-O0`, both globals survive. At `-O1` and above, the `unnamed_addr` attribute signals to the optimizer that addresses are interchangeable, and the two globals get merged into one. For most programs, this is not worth worrying about.

`StringLiteralCounter` resets to zero at the start of each new module so that JIT sessions that compile multiple modules don't accumulate unbounded counter values. Since the globals have `PrivateLinkage`, names never conflict across modules.

## Build and Run

```bash
cd code/chapter-21
cmake -S . -B build && cmake --build build
```

## Try It

### Basic string output

```python
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  puts("hello, pyxc")
  return 0
```

```
hello, pyxc
```

### Store in a variable, pass around

```python
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  var s: ptr[int8] = "stored"
  puts(s)
  return 0
```

```
stored
```

### Return from a function

```python
extern def puts(s: ptr[int8]) -> int

def label() -> ptr[int8]:
  return "from function"

def main() -> int:
  puts(label())
  return 0
```

```
from function
```

### Escape sequences

```python
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  puts("line1\nline2")
  return 0
```

```
line1
line2
```

### Array of strings

```python
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  var msgs: ptr[int8][3] = ["one", "two", "three"]
  puts(msgs[0])
  puts(msgs[1])
  puts(msgs[2])
  return 0
```

```
one
two
three
```

### Calling printf

`printf` is variadic — Pyxc does not support variadic functions natively, but you can declare a single-argument wrapper:

```python
extern def printf(fmt: ptr[int8]) -> int

def main() -> int:
  printf("value: 42\n")
  return 0
```

For formatted output with arguments, wrap `printf` in a C helper compiled separately and linked in.

### Inspect the IR

```bash
./build/pyxc --emit llvm-ir -o out.ll program.pyxc
grep '__str\|constant' out.ll
```

You'll see one `private unnamed_addr constant [N x i8]` per string literal.

## Known Limitations

**`ptr[int8]` only.** There is no `string` type — strings are pointers. You get no length, no bounds checking, no UTF-8 awareness. This is the C model.

**No variadic functions.** You cannot declare `printf(fmt: ptr[int8], ...)`. For formatted output you need a C wrapper.

**No string concatenation.** `"hello" + " world"` is a type error — pointer arithmetic is not supported, and there is no string concat operator.

**No deduplication at `-O0`.** Each string literal is its own global. The optimizer merges them at higher optimization levels.

**`\0` in the middle of a string.** The lexer processes `\0` and inserts a null byte. Functions like `puts` will stop at the first null, ignoring the rest of the string. This is standard C behavior and not a bug, but it can be surprising.

## What's Next

The next chapter adds `while` loops and uses everything built so far — structs, pointers, arrays, array literals, and strings — to write a Mandelbrot set renderer that outputs to the terminal.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

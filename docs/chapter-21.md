---
description: "Add string literals and C interop — so pyxc programs can call puts, printf, and other C standard library functions directly."
---
# 21. pyxc: String Literals and C Interop

## Where We Are

[Chapter 20](chapter-20.md) added array literals, giving us a clean syntax for constructing fixed-size arrays inline. But there is one conspicuously missing piece: text. Every useful program eventually needs to produce output that is more than a raw number, and the C standard library is full of functions — `puts`, `printf`, `strlen` — that are ready to help. What we are missing is a way to write string data in pyxc source code.

After this chapter:

```python
extern def puts(s: ptr[int8]) -> int

def greeting() -> ptr[int8]:
  return "hello, pyxc"

def main() -> int:
  puts(greeting())
  return 0
```

Output:

```
hello, pyxc
```

String literals are `ptr[int8]` — a pointer to the first byte of a null-terminated buffer. That is exactly what C's `char *` is. `puts`, `printf`, `strlen`, and every other C string function accept `ptr[int8]` directly, with no adapter needed.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-21
```

## Grammar

```ebnf
string-expr ::= '"' { char | escape-seq } '"'
escape-seq  ::= '\\' | '\"' | '\n' | '\t' | '\0'
```

A string literal is a sequence of characters and escape sequences enclosed in double quotes. It has type `ptr[int8]` regardless of context. The closing quote must appear on the same line as the opening quote — unterminated strings are a lexer error.

Supported escape sequences: `\\` (backslash), `\"` (double quote), `\n` (newline), `\t` (tab), `\0` (null byte). Any other `\x` is a lexer error.

## A New Token: `tok_string`

```cpp
tok_string = -38,
```

Unlike keywords, `tok_string` is not registered in the keyword map. The lexer produces it directly when it encounters a `"` character. The string's content — with escapes already resolved — is stored in a global:

```cpp
static string StringLiteralStr;
```

This mirrors the existing pattern for `IdentifierStr` and `NumVal`: the lexer fills a global, and the parser consumes it before calling `getNextToken` again.

## Lexer String Handling

In `getTok`, the `LexerLastChar == '"'` branch handles the full scan:

```cpp
if (LexerLastChar == '"') {
  StringLiteralStr.clear();
  LexerLastChar = advance(); // eat opening quote
  while (LexerLastChar != '"' && LexerLastChar != EOF && LexerLastChar != '\n') {
    if (LexerLastChar == '\\') {
      LexerLastChar = advance();
      switch (LexerLastChar) {
      case '\\': StringLiteralStr.push_back('\\'); break;
      case '"':  StringLiteralStr.push_back('"');  break;
      case 'n':  StringLiteralStr.push_back('\n'); break;
      case 't':  StringLiteralStr.push_back('\t'); break;
      case '0':  StringLiteralStr.push_back('\0'); break;
      default:
        fprintf(stderr, "Error: invalid string escape\n");
        return tok_error;
      }
    } else {
      StringLiteralStr.push_back(static_cast<char>(LexerLastChar));
    }
    LexerLastChar = advance();
  }
  if (LexerLastChar != '"') {
    fprintf(stderr, "Error: unterminated string literal\n");
    return tok_error;
  }
  LexerLastChar = advance(); // eat closing quote
  return tok_string;
}
```

The loop advances character by character. When it sees `\`, it immediately advances again to read the escape character. Each recognized escape is pushed as its actual byte value. Anything not in the switch returns `tok_error` immediately, which aborts compilation.

The while condition checks for both EOF and `\n` in addition to the closing `"`. This means hitting end-of-file or end-of-line before the closing quote is caught as an unterminated string rather than looping forever.

## `StringExprAST`

```cpp
class StringExprAST : public ExprAST {
  string Text;
public:
  explicit StringExprAST(string Text, const string &PtrTypeInfo)
      : Text(std::move(Text)) {
    setType(ValueType::Pointer, PtrTypeInfo);
  }
  Value *codegen() override;
};
```

The type is always `ptr[int8]`. `PtrTypeInfo` is `EncodePointerType(ValueType::Int8, "")` — the same encoding used for any other `ptr[int8]` in the system. From the type checker's perspective, a string literal is indistinguishable from any other `ptr[int8]` value.

`Text` holds the processed string content: the characters between the quotes, with all escape sequences already resolved to their byte values. No further processing happens at codegen time.

## Parsing `tok_string`

The `ParsePrimary` switch gains a `tok_string` case:

```cpp
case tok_string: {
  string S = StringLiteralStr;
  getNextToken();
  return make_unique<StringExprAST>(
      std::move(S), EncodePointerType(ValueType::Int8, ""));
}
```

The global `StringLiteralStr` is copied into a local before `getNextToken` is called — the same pattern used for `IdentifierStr` in the `tok_identifier` case. There is no context-sensitivity: string literals are always `ptr[int8]`, regardless of where they appear.

## `StringExprAST::codegen`

```cpp
Value *StringExprAST::codegen() {
  auto *I8Ty = Type::getInt8Ty(*TheContext);
  auto *ArrTy = ArrayType::get(I8Ty, Text.size() + 1);
  auto *Init = ConstantDataArray::getString(*TheContext, Text, true);
  string Name = ".str." + to_string(StringLiteralCounter++);
  auto *GV = new GlobalVariable(*TheModule, ArrTy, true,
                                GlobalValue::PrivateLinkage, Init, Name);
  GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(Align(1));
  ModuleHasGlobals = true;
  Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  return Builder->CreateInBoundsGEP(ArrTy, GV, {Zero, Zero}, "strptr");
}
```

Each string literal becomes a private global constant in the LLVM module. The details:

**Array type.** The string `"hello"` (5 bytes) becomes `[6 x i8]`. The `+ 1` accounts for the null terminator. The `true` argument to `ConstantDataArray::getString` appends the `\0` automatically.

**`PrivateLinkage`.** The global is not visible outside the translation unit. Two different `.c`/`.pyxc` files can each have a `.str.0` without name collision.

**`UnnamedAddr::Global`.** The address of the constant does not matter to the program — it is only ever used through the pointer, not compared for identity. This attribute lets LLVM merge identical string constants at link time when optimizing.

**`Align(1)`.** Byte-aligned, correct for a `char` array. No stricter alignment is required or useful.

**`StringLiteralCounter`.** A `static unsigned` declared at module scope, reset to 0 at the start of each new module compilation. It generates unique names: `.str.0`, `.str.1`, and so on. Two identical string literals in the same file produce two separate globals at `-O0`; LLVM may merge them at higher optimization levels.

**The GEP.** The global `GV` has type `[N x i8]` — it is a pointer to an array, not a pointer to a byte. `CreateInBoundsGEP` with indices `{i64 0, i64 0}` steps through the global (first index, advancing zero array elements) and then to byte 0 (second index, advancing zero bytes within the array). The result has type `ptr` pointing to the first byte. This is the standard C idiom for converting an array to a pointer.

**`ModuleHasGlobals = true`.** String literal globals require the module-level `__init_globals` function to be emitted even if the user has declared no global variables. Setting this flag ensures that function is generated.

### Generated IR

For `"hello"`:

```llvm
@.str.0 = private unnamed_addr constant [6 x i8] c"hello\00"

define i32 @main() {
entry:
  %strptr = getelementptr inbounds [6 x i8], ptr @.str.0, i64 0, i64 0
  call i32 @puts(ptr %strptr)
  ret i32 0
}
```

The global is a read-only constant — `true` in the `GlobalVariable` constructor sets the `isConstant` flag. LLVM is free to place it in the `.rodata` section (or equivalent on the target platform).

## The String Type in pyxc

Strings in pyxc are `ptr[int8]`. There is no separate `string` type — a string literal is simply a pointer to the first byte of a null-terminated buffer, exactly matching C's `char *`. Every C string function accepts `ptr[int8]` directly:

```python
extern def puts(s: ptr[int8]) -> int
extern def printf(fmt: ptr[int8]) -> int
extern def strlen(s: ptr[int8]) -> int
```

Returning a string from a function works because the return type check compares `ptr[int8]` against `ptr[int8]`:

```python
def greeting() -> ptr[int8]:
  return "hello"
```

Storing a string literal in a variable works the same way:

```python
var msg: ptr[int8] = "hello, pyxc"
puts(msg)
```

The pointer stored in `msg` points directly into the global constant. The storage for the string is static — it lives for the lifetime of the program.

## Build and Run

```bash
cd code/chapter-21
cmake -S . -B build && cmake --build build
```

## Try It

### Basic string literal

```python
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  puts("hello, pyxc")
  return 0
```

```bash
hello, pyxc
```

### Escape sequences

```python
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  puts("line one\nline two")
  return 0
```

```bash
line one
line two
```

The `\n` inside the string literal is resolved by the lexer to a real newline byte. `puts` adds a trailing newline of its own, so the output ends with a blank line.

### Return a string from a function

```python
extern def puts(s: ptr[int8]) -> int

def greeting() -> ptr[int8]:
  return "hello from a function"

def main() -> int:
  puts(greeting())
  return 0
```

```bash
hello from a function
```

### Store in a variable, then pass

```python
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  var msg: ptr[int8] = "stored string"
  puts(msg)
  return 0
```

```bash
stored string
```

### Inspect the IR for a string global

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep '\.str\.' out.ll
```

You will see lines like:

```llvm
@.str.0 = private unnamed_addr constant [14 x i8] c"stored string\00"
```

Each string literal in the source file produces one entry. The counter suffix increments for each additional literal.

## Known Limitations

**No length tracking.** Strings are raw `ptr[int8]` — there is no stored length. Operations like bounds checking or safe slicing require the caller to track the length separately or call `strlen`.

**No built-in string operations.** Concatenation, comparison, and copying are not in the language. Use the C standard library (`strcat`, `strcmp`, `strcpy`) via `extern` declarations, or allocate a buffer with `malloc` (chapter 20) and write to it manually.

**No deduplication at `-O0`.** Two identical string literals in the same file produce two separate globals. LLVM may merge them at higher optimization levels thanks to `UnnamedAddr::Global`, but not at `-O0`.

**No `string` type alias yet.** Writing `ptr[int8]` everywhere is verbose. Chapter 22 adds `type string = ptr[int8]`, which makes string-typed function signatures read naturally without any new runtime machinery.

**Mutable string buffers require malloc.** String literal globals are read-only constants. To build or modify a string at runtime you need a heap-allocated buffer, which requires `malloc` (chapter 20).

## What's Next

[Chapter 22](chapter-22.md) adds type aliases — `type string = ptr[int8]` — so you can write `string` wherever you currently write `ptr[int8]`. The underlying representation does not change at all; the alias is purely syntactic, resolved at parse time.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

---
description: "Add string literals and C interop: pyxc programs can call puts, printf, and other C standard library functions directly."
---
# 30. pyxc: String Literals and C Interop

## What I Am Building

[Chapter 29](chapter-29.md) gave me type aliases, so I can write `string` instead of `ptr[int8]`. But everything I've built so far only moves numbers and raw bytes around. I still have no way to write literal text in pyxc source at all. The C standard library is full of functions that want exactly that: `puts`, `printf`, `strlen`. What I'm missing is a way to write a string in pyxc and have it show up as the `ptr[int8]` those functions expect.

After this chapter:

```pyxc
extern def puts(s: ptr[int8]) -> int

def greeting() -> ptr[int8]:
  return "hello, pyxc"

def main() -> int:
  puts(greeting())
  return 0
```

```text
hello, pyxc
```

String literals are `ptr[int8]`: a pointer to the first byte of a null-terminated buffer. That's exactly what C's `char *` is, so `puts`, `printf`, `strlen`, and every other C string function accept a pyxc string literal directly, with no adapter needed.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-30
```

## Grammar

`primary` gains `string-literal` as an alternative. `string-literal` and `escape` are both new. Nothing else in the grammar changes; the `CallExpressionNode` fix later in this chapter is a codegen and type-checking change, not a grammar change, so it doesn't show up here:

```grammardiff
*...
*                                    | address-expression
*                                    | array-literal
+                                    | string-literal
*                                    | name-expression
*                                    | number-expression
*...
*array-literal                     = "[" [ expression
*                                      { "," expression } ] "]" ;
+string-literal                    = '"' { string-character | escape } '"' ;
+escape                            = "\\" ( "\\" | '"' | "n" | "t" | "0" ) ;
+string-character                  = ? any character except '"', "\\", "\r", and "\n" ? ;
*name-expression                   = lvalue | call-expression ;
*call-expression                   = name "(" [ arguments ] ")" ;
*...
```

## A New Token for String Literals

```cppdiff
 enum Token {
*  ...
*  tok_sizeof = -51,
*  tok_type = -52,
+  tok_string = -53,
*
*  // punctuation and operators
*  tok_lparen = '(',
*  ...
*};
```

Unlike `def` or `sizeof`, a string literal doesn't have a fixed spelling I can put in the keyword map. The lexer has to recognize it structurally, by seeing an opening `"`. I still need somewhere to put the text once I've read it:

```cpp
static string StringLiteralValue;
```

This is the same pattern I already use for `Name` and `NumberLiteral`: the lexer fills a global, and whatever consumes the token copies it out before asking for another one.

## Reading a String Literal

```cpp
if (LexerLastChar == '"') {
  StringLiteralValue.clear();
  LexerLastChar = advance(); // eat opening quote
  while (LexerLastChar != '"' && LexerLastChar != EOF &&
         LexerLastChar != '\n') {
    if (LexerLastChar == '\\') {
      LexerLastChar = advance();
      switch (LexerLastChar) {
      case '\\':
        StringLiteralValue.push_back('\\');
        break;
      case '"':
        StringLiteralValue.push_back('"');
        break;
      case 'n':
        StringLiteralValue.push_back('\n');
        break;
      case 't':
        StringLiteralValue.push_back('\t');
        break;
      case '0':
        StringLiteralValue.push_back('\0');
        break;
      default:
        fprintf(stderr,
                "Error (Line %d, Column %d): invalid string escape\n",
                CurrentTokenLocation.Line, CurrentTokenLocation.Column);
        PrintErrorSourceContext(CurrentTokenLocation);
        return tok_error;
      }
    } else {
      StringLiteralValue.push_back(static_cast<char>(LexerLastChar));
    }
    LexerLastChar = advance();
  }

  if (LexerLastChar != '"') {
    fprintf(stderr,
            "Error (Line %d, Column %d): unterminated string literal\n",
            CurrentTokenLocation.Line, CurrentTokenLocation.Column);
    PrintErrorSourceContext(CurrentTokenLocation);
    return tok_error;
  }
  LexerLastChar = advance(); // eat closing quote
  return tok_string;
}
```

I resolve escapes as I go, one character at a time, rather than storing the raw text and resolving escapes later. There's no reason to make a second pass over something I'm already reading character by character.

I deliberately stop the loop at `\n` as well as `"` and `EOF`. A string literal that runs off the end of a line without a closing quote is almost always a typo, a missing `"`, not an intentional multi-line string, so I catch it immediately rather than let it swallow the rest of the file looking for a `"` that was never going to come. Both failure paths use the same location-and-context error reporting every other lexer error in pyxc uses by this point.

## The String Literal AST Node

```cpp
class StringExpressionNode : public ExpressionNode {
  string Text;

public:
  StringExpressionNode(string Text, const string &PointerTypeInfo)
      : Text(std::move(Text)) {
    setType(ValueType::Pointer, PointerTypeInfo);
  }
  Value *codegen() override;
};
```

`Text` holds the string with escapes already resolved to real bytes; there's nothing left to process by the time codegen runs. The type is always `ValueType::Pointer`, and `PointerTypeInfo` is the encoded pointee-type string I use everywhere else a pointer's pointee needs to travel alongside it, produced the same way `ptr[int8]` produces it anywhere else in the type checker:

```cppdiff
 static unique_ptr<ExpressionNode> ParsePrimary() {
*  switch (CurrentToken) {
*  case tok_number:
*    return ParseNumberExpression();
*  case tok_name:
*    return ParseNameExpression();
+  case tok_string: {
+    string Text = StringLiteralValue;
+    getNextToken();
+    return make_unique<StringExpressionNode>(
+        std::move(Text), EncodePointerType(ValueType::Int8));
+  }
*  case tok_true:
*    getNextToken();
*    return make_unique<BoolExpressionNode>(true);
*  ...
*  }
*}
```

From the type checker's point of view, a string literal is just an ordinary `ptr[int8]` value. There's no separate string type hiding underneath, and no special case anywhere downstream needs to know it came from a literal rather than, say, a `malloc`'d buffer.

## Codegen: One Global per Literal

```cpp
Value *StringExpressionNode::codegen() {
  auto *ByteType = Type::getInt8Ty(*TheContext);
  auto *StorageType = ArrayType::get(ByteType, Text.size() + 1);
  auto *Initializer = ConstantDataArray::getString(*TheContext, Text, true);
  string GlobalName = ".str." + to_string(StringLiteralCounter++);
  auto *Global = new GlobalVariable(
      *TheModule, StorageType, true, GlobalValue::PrivateLinkage, Initializer,
      GlobalName);
  Global->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  Global->setAlignment(Align(1));
  ModuleHasGlobals = true;

  Value *Zero = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
  return TheBuilder->CreateInBoundsGEP(StorageType, Global, {Zero, Zero},
                                    "strptr");
}
```

Every string literal becomes its own private global constant, sized one byte longer than the text to hold the null terminator (`ConstantDataArray::getString`'s `true` argument appends it for me). I give each one a unique name off a counter (`.str.0`, `.str.1`, ...) since two literals in the same module can't share a global name.

A few choices here are deliberate, not defaults I happened to leave in place:

- **`PrivateLinkage`** keeps the global out of the module's external symbol table. Nothing outside this translation unit needs to see `.str.0` by name, and I don't want a `.str.0` in one file colliding with a `.str.0` in another.
- **`UnnamedAddr::Global`** tells LLVM the *address* of this constant doesn't matter to my program, only its contents do. I never compare two string literals by pointer identity, so I'm free to let LLVM merge identical literals at higher optimization levels.
- **`Align(1)`** is just honest about what a byte array needs. Nothing about a `char` buffer benefits from stricter alignment.

The global itself has type `ptr` to a `[N x i8]` array, not a pointer to a single byte, so I still need a `getelementptr` to step into it and get a `ptr[int8]`-shaped value out: index `0` into the global, then index `0` into the array, landing on the first byte. That's the same array-to-pointer idiom C uses under the hood every time a string literal decays to a `char *`.

I also set `ModuleHasGlobals = true` here. That flag controls whether pyxc emits the module-level initialization function it uses for global variables, and a string literal's backing storage is exactly that: a global, even though nothing in the source looks like a `var` declaration.

For `puts("hello")`:

```llvm
@.str.0 = private unnamed_addr constant [6 x i8] c"hello\00", align 1

define i64 @__pyxc.user_main() {
entry:
  %strptr = getelementptr inbounds [6 x i8], ptr @.str.0, i64 0, i64 0
  %calltmp = call i64 @puts(ptr %strptr)
  ret i64 0
}
```

`[6 x i8]` is "hello" plus its null terminator. LLVM is free to place a `constant` global like this in read-only memory.

## The Second Fix This Chapter Needed

Writing the "return a string from a function" example surfaced a real gap I'd documented but not yet fixed. In [Chapter 28](chapter-28.md)'s Known Limitations, I noted that calling an `extern` function returning any pointer type required wrapping the call in an explicit same-type cast, because the call result didn't carry its pointee-type metadata even when the declared return type matched exactly. Returning a string literal from a pyxc-defined function hits the identical problem: `greeting()` is declared `-> ptr[int8]`, but without a fix, the call expression's own type comes back with no pointee information, and assigning it to anything typed `ptr[int8]` fails the same metadata check.

The fix is to stop leaving that metadata behind at the call site:

```cpp
static unique_ptr<ExpressionNode> ParseNameExpressionWithName(const string &ParsedName) {
  // ...
  if (!Signature)
    return LogErrorExpression("Unknown function: '" + ParsedName + "'");
  if (Signature->getNumParameters() != Arguments.size())
    return LogErrorExpression(
        "Incorrect number of arguments in call to '" + ParsedName +
        "': expected " + to_string(Signature->getNumParameters()) +
        ", got " + to_string(Arguments.size()));

  for (size_t i = 0; i < Arguments.size(); ++i) {
    // ...
  }

  return make_unique<CallExpressionNode>(ParsedName, std::move(Arguments),
                                  Signature->getReturnType(),
                                  Signature->getReturnStructName());
}
```

`CallExpressionNode` already had a `Type` it set from the callee's signature; it just never asked the signature for the matching `StructName`. Once it does, a function call carries exactly the same pointee metadata a local expression of the same type would, and I no longer need the workaround cast from Chapter 28 for either case.

## Build and Run

```bash
cd code/chapter-30
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## Try It

### Basic String Literal

```pyxc
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  puts("hello, pyxc")
  return 0
```

```text
hello, pyxc
```

### Escape Sequences

```pyxc
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  puts("line one\nline two")
  return 0
```

```text
line one
line two
```

The `\n` is resolved by the lexer to a real newline byte before codegen ever sees it. `puts` adds its own trailing newline, which is why there's a blank line after "line two".

### Returning a String from a Function

```pyxc
extern def puts(s: ptr[int8]) -> int

def greeting() -> ptr[int8]:
  return "hello from a function"

def main() -> int:
  puts(greeting())
  return 0
```

```text
hello from a function
```

### Storing a String in a Variable

```pyxc
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  var msg: ptr[int8] = "stored string"
  puts(msg)
  return 0
```

```text
stored string
```

### Inspecting the IR

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep '\.str\.' out.ll
```

For the "stored string" example above:

```llvm
@.str.0 = private unnamed_addr constant [14 x i8] c"stored string\00", align 1
```

## Known Limitations

**No length tracking.** A string literal is just a `ptr[int8]`; there's no stored length anywhere. Anything that needs the length has to call `strlen` or track it separately.

**No built-in string operations.** Concatenation, comparison, copying: none of that is in the language. I reach for the C standard library (`strcat`, `strcmp`, `strcpy`) through `extern`, or allocate a buffer with `malloc` ([Chapter 28](chapter-28.md)) and write into it manually.

**No deduplication at `-O0`.** Two identical string literals in the same file get two separate globals; `UnnamedAddr::Global` lets LLVM merge them at higher optimization levels, but at `-O0` they stay separate.

**No `string` type alias yet.** Writing `ptr[int8]` everywhere works but reads oddly for something that's conceptually text. [Chapter 29](chapter-29.md) adds `type string = ptr[int8]`, purely as a name.

**String buffers are read-only.** A string literal's backing global is a constant. Building or mutating text at runtime still needs a heap buffer from `malloc`.

## What's Next

[Chapter 31](chapter-31.md) adds character literals.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

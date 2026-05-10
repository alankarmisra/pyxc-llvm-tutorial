---
description: "Add fixed-size stack arrays: declare int[4], initialise with [1,2,3,4], and index with arr[i]. Arrays decay to pointers when passed to functions."
---
# 23. pyxc: Arrays

## Where We Are

[Chapter 22](chapter-22.md) added type aliases. The type system covers scalars, structs, and pointers, but there is no way to allocate a fixed-size sequence of values on the stack. After this chapter:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var scores: int[4] = [10, 20, 30, 40]
  printd(float64(scores[2]))
  return 0
```

```
30.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-23
```

## Grammar

This chapter extends `type` with an optional `arraysuffix` and adds the `arrayliteral` production to `primary`.

```ebnf
type       = basetype [ arraysuffix ] ;   -- changed
basetype   = builtintype | aliastype | structtype | pointertype ;  -- new
arraysuffix = "[" integer "]" ;           -- new
arrayliteral = "[" [ expression { "," expression } ] "]" ;  -- new
primary    = castexpr | sizeofexpr | addrexpr | arrayliteral | ...  -- changed
```

### Full Grammar

`code/chapter-23/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | structdef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
structdef       = "struct" identifier ":" eols structblock ;
structblock     = indent fielddecl { eols fielddecl } dedent ;
fielddecl       = identifier ":" type ;
definition      = "def" prototype [ "->" type ] ":" ( simplestmt | eols block ) ;
decorateddef    = binarydecorator eols "def" binaryopprototype [ "->" type ] ":" ( simplestmt | eols block )
                | unarydecorator  eols "def" unaryopprototype  [ "->" type ] ":" ( simplestmt | eols block ) ;
binarydecorator = "@" "binary" "(" integer ")" ;
unarydecorator  = "@" "unary" ;
binaryopprototype = customopchar "(" typedparam "," typedparam ")" ;
unaryopprototype  = customopchar "(" typedparam ")" ;
external        = "extern" "def" prototype [ "->" type ] ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ typedparam { "," typedparam } ] ")" ;
typedparam      = identifier ":" type ;
ifstmt          = "if" expression ":" suite
                [ eols "else" ":" suite ] ;
forstmt         = "for"
                  ( "var" identifier ":" type | identifier )
                  "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = lvalue "=" expression ;
simplestmt      = returnstmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" [ expression ] ;
block           = indent statement { eols statement } dedent ;
expression      = unaryexpr binoprhs ;
binoprhs        = { binaryop unaryexpr } ;
lvalue          = identifier | fieldaccess | indexexpr ;
varbinding      = identifier ":" type [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | primary ;
unaryop         = "-" | userdefunaryop ;
primary         = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
castexpr        = casttype "(" expression ")" ;
sizeofexpr      = "sizeof" "(" type ")" ;
addrexpr        = "addr" "(" lvalue ")" ;
identifierexpr  = identifier | callexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
fieldaccess     = identifier "." identifier { "." identifier } ;
indexexpr       = identifier "[" expression "]" ;
numberexpr      = number ;
arrayliteral    = "[" [ expression { "," expression } ] "]" ;
stringliteral   = "\"" { ? any char except " and newline ? | escape } "\"" ;
escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;
dedent          = DEDENT ;

builtinbinaryop = "+" | "-" | "*" | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
builtintype     = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | "None" ;
aliastype       = identifier ;
structtype      = identifier ;
pointertype     = "ptr" "[" type "]" ;
type            = basetype [ arraysuffix ] ;
basetype        = builtintype | aliastype | structtype | pointertype ;
arraysuffix     = "[" integer "]" ;
casttype        = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | pointertype ;
integer         = digit { digit } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
bool_literal    = "True" | "False" ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
INDENT          = ? synthetic token emitted by lexer ? ;
DEDENT          = ? synthetic token emitted by lexer ? ;
```

## Array Types

An array type is a base type followed by a size in brackets: `int[4]`, `float64[8]`, `Point[3]`. The size must be a compile-time integer literal greater than zero — expressions are not allowed.

```pyxc
var buf: int[4]        # four 64-bit integers on the stack
var v:   float64[3]    # three doubles
```

In `ParseTypeToken`, after parsing the base type, the function checks whether the next token is `[`. If it is, it reads the integer size, validates it is nonzero, and returns `ValueType::Array` with the type information encoded in the struct name string.

## How Array Types Are Represented Internally

The same two-field representation used for pointers (`ValueType` + struct name string) is extended to arrays with a third field: the element count. The encoding is a colon-separated string:

```
"<ElemTypeInt>:<ElemStructName>:<Count>"
```

Examples:

| Type       | Encoding        |
|------------|-----------------|
| `int[4]`   | `"1::4"`        |
| `float64[3]` | `"8::3"`      |
| `Point[2]` | `"10:Point:2"`  |

`EncodeArrayType` produces this string. `DecodeArrayType` splits it back out. All code that works with arrays — alloca sizing, GEP emission, literal initialisation, decay checks — calls `DecodeArrayType` to recover the element type, struct name, and count.

## Array Literals

An array literal is a comma-separated list of expressions inside `[` `]`:

```pyxc
var scores: int[4] = [10, 20, 30, 40]
```

The literal has no type on its own — it takes its type from the declaration context. `ParseArrayLiteralExpr` reads the expected element type from `ExpectedLiteralType` (set by the `var` parser before calling into the expression parser). If the context does not provide an array type, the literal is an error.

The element count in the literal must exactly match the declared count. Too few or too many elements is rejected at parse time.

## Index Expressions

```pyxc
scores[2]       # read
scores[i] = 99  # write
```

`indexexpr` is already part of `lvalue` and `primary`. What changes in this chapter is that the codegen for `IndexExprAST` now handles `ValueType::Array` by emitting a two-index GEP:

```llvm
%scores = alloca [4 x i64]
; scores[2]
%ptr = getelementptr inbounds [4 x i64], ptr %scores, i64 0, i64 2
%val = load i64, ptr %ptr
```

The first GEP index (`i64 0`) steps past the alloca header to reach the array itself. The second index selects the element. This is the standard LLVM pattern for stack arrays.

The index expression must be an integer type. Floating-point indices are an error.

## Decay to Pointer

An array variable can be passed to a function that expects `ptr[T]` for the matching element type. The array decays to a pointer to its first element — the same behaviour as C.

```pyxc
extern def puts(s: ptr[int8]) -> int

def main() -> int:
  var msg: int8[6] = [72, 101, 108, 108, 111, 0]
  puts(addr(msg[0]))
  return 0
```

The decay check is in `ArrayDecaysToPointerType`: it decodes both the array encoding and the pointer encoding and confirms the element types match.

## What Lands in the IR

```pyxc
def sum4(a: int[4]) -> int:
  return a[0] + a[1] + a[2] + a[3]
```

```llvm
define i64 @sum4([4 x i64] %a) {
entry:
  %a.addr = alloca [4 x i64]
  store [4 x i64] %a, ptr %a.addr
  %p0 = getelementptr inbounds [4 x i64], ptr %a.addr, i64 0, i64 0
  %v0 = load i64, ptr %p0
  ; ... and so on for indices 1, 2, 3
  %sum = add i64 %v0, ...
  ret i64 %sum
}
```

## Build and Run

```bash
cd code/chapter-23
cmake -S . -B build && cmake --build build
```

## Things Worth Knowing

**Size must be a literal.** `var buf: int[n]` is rejected — variable sizes are not supported. The element count must be a constant integer known at parse time.

**No nested arrays.** `int[4][2]` is not valid syntax. An array of arrays is not supported. Use a struct with multiple array fields if you need a 2-D layout.

**No heap arrays.** Arrays in this chapter live on the stack only. Heap allocation is done through `malloc` and `ptr[T]` from [chapter 20](chapter-20.md).

**Struct fields cannot be arrays.** Array fields in struct definitions are not yet supported. Struct fields use the scalar or pointer types from earlier chapters.

**No pointer arithmetic on arrays.** Indexing works. Direct pointer arithmetic (`arr + 1`) on an array variable does not — use `addr(arr[i])` to get a pointer to an element and arithmetic from there.

## What's Next

[Chapter 24](chapter-24.md) adds the `class` keyword — a named aggregate type that will support methods, constructors, and visibility in the chapters that follow.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

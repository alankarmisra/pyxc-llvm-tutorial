---
description: "Add variadic extern declarations so pyxc code can call C functions like printf and scanf that take a variable number of arguments."
---
# 40. pyxc: Variadic Extern Functions

## Where We Are

[Chapter 39](chapter-39.md) completed Phase 5. pyxc can call C functions via `extern def`, but only functions with a fixed number of typed parameters. `printf`, `scanf`, `sprintf`, and most other C I/O functions take a variable number of arguments — the `...` in their C signatures. Trying to declare them produces an error:

```pyxc
extern def printf(fmt: ptr[int8], ...) -> int32
```

```
Error: Expected parameter name in prototype
```

After this chapter, variadic `extern` declarations work:

```pyxc
type string = ptr[int8]
extern def printf(fmt: string, ...) -> int32

def main() -> int:
  printf("hello world\n")
  printf("answer: %ld\n", 42)
  return 0
```

```
hello world
answer: 42
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-40
```

## Grammar

The `external` rule gains a new `externprototype` production that allows an optional `...` at the end of the parameter list. Regular `definition` and `decorateddef` are unchanged — variadic syntax is only valid in `extern def`.

```ebnf
external        = "extern" "def" externprototype [ "->" type ] ;  -- changed
externprototype = identifier "(" [ typedparam { "," typedparam } [ "," "..." ] | "..." ] ")" ; -- new
```

The two forms:
- `extern def f(a: T, b: U, ...)` — fixed typed parameters followed by `...`
- `extern def f(...)` — only variadic, no fixed parameters

### Full Grammar

`code/chapter-40/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | traitdef | structdef | classdef | impldef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
traitdef        = "trait" identifier [ "[" identifier "]" ] ":" eols traitblock ;
traitblock      = indent traitmethodsig { eols traitmethodsig } dedent ;
traitmethodsig  = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ;
structdef       = "struct" identifier ":" eols structblock ;
classdef        = "class" identifier [ "(" traitref { "," traitref } ")" ] ":" eols structblock ;
traitref        = identifier [ "[" type "]" ] ;
impldef         = "impl" traitref "for" identifier ":" eols implblock ;
implblock       = indent implmethod { eols implmethod } dedent ;
implmethod      = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ":" ( simplestmt | eols block ) ;
structblock     = indent classmember { eols classmember } dedent ;
classmember     = [ visibility ] ( fielddecl | methoddef ) ;
visibility      = "public" | "private" ;
methoddef       = "def" identifier "(" [ typedparam { "," typedparam } ] ")"
                  [ "->" type ] ":" ( simplestmt | eols block ) ;
fielddecl       = identifier ":" type ;
definition      = "def" prototype [ "->" type ] ":" ( simplestmt | eols block ) ;
decorateddef    = binarydecorator eols "def" binaryopprototype [ "->" type ] ":" ( simplestmt | eols block )
                | unarydecorator  eols "def" unaryopprototype  [ "->" type ] ":" ( simplestmt | eols block ) ;
binarydecorator = "@" "binary" "(" integer ")" ;
unarydecorator  = "@" "unary" ;
binaryopprototype = customopchar "(" typedparam "," typedparam ")" ;
unaryopprototype  = customopchar "(" typedparam ")" ;
external        = "extern" "def" externprototype [ "->" type ] ;
externprototype = identifier "(" [ typedparam { "," typedparam } [ "," "..." ] | "..." ] ")" ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ typedparam { "," typedparam } ] ")" ;
typedparam      = identifier ":" type ;
ifstmt          = "if" expression ":" suite
                { eols "elif" expression ":" suite }
                [ eols "else" ":" suite ] ;
whilestmt       = "while" expression ":" suite ;
dowhilestmt     = "do" ":" suite eols "while" expression ;
switchstmt      = "switch" expression ":" eols indent switchbody dedent ;
switchbody      = switchcase { eols switchcase } [ eols defaultcase ] ;
switchcase      = "case" switchint ":" suite ;
defaultcase     = "default" ":" suite ;
forstmt         = "for"
                  ( "var" identifier ":" type | identifier )
                  "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = lvalue assignop expression ;
simplestmt      = returnstmt | breakstmt | continuestmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt | whilestmt | dowhilestmt | switchstmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" [ expression ] ;
breakstmt       = "break" ;
continuestmt    = "continue" ;
block           = indent statement { eols statement } dedent ;
expression      = lvalue assignop expression | unaryexpr binoprhs ;
binoprhs        = { binaryop unaryexpr } ;
lvalue          = identifier | fieldaccess | indexexpr ;
varbinding      = identifier ":" type [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | postfixexpr ;
unaryop         = "-" | "!" | "~" | "++" | "--" | userdefunaryop ;
postfixexpr     = primary [ postfixop ] ;
postfixop       = "++" | "--" ;
primary         = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral | charliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
castexpr        = casttype "(" expression ")" ;
sizeofexpr      = "sizeof" "(" type ")" ;
addrexpr        = "addr" "(" lvalue ")" ;
identifierexpr  = identifier | callexpr | methodcallexpr | ctorcallexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
methodcallexpr  = identifier "." identifier "(" [ expression { "," expression } ] ")" ;
ctorcallexpr    = identifier "(" [ expression { "," expression } ] ")" ;
fieldaccess     = identifier "." identifier { "." identifier } ;
indexexpr       = identifier "[" expression "]" ;
numberexpr      = number ;
arrayliteral    = "[" [ expression { "," expression } ] "]" ;
stringliteral   = "\"" { ? any char except " and newline ? | escape } "\"" ;
charliteral     = "'" ( ? any char except ' and newline ? | charescape ) "'" ;
escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
charescape      = "\\" ( "\\" | "'" | "n" | "t" | "0" ) ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;
dedent          = DEDENT ;

assignop        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!="
                | "&&" | "||"
                | "&" | "|" | "^" | "<<" | ">>" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
builtintype     = "int" | "int8" | "int16" | "int32" | "int64"
                | "uint8" | "uint16" | "uint32" | "uint64"
                | "float" | "float32" | "float64"
                | "bool" | "None" ;
aliastype       = identifier ;
structtype      = identifier ;
pointertype     = "ptr" "[" type "]" ;
type            = basetype [ arraysuffix ] ;
basetype        = builtintype | aliastype | structtype | pointertype ;
arraysuffix     = "[" integer "]" ;
casttype        = "int" | "int8" | "int16" | "int32" | "int64"
                | "uint8" | "uint16" | "uint32" | "uint64"
                | "float" | "float32" | "float64"
                | "bool" | pointertype ;
integer         = digit { digit } ;
switchint       = [ "-" ] integer ;
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

## Parsing

`ParsePrototype` takes a new `AllowVarArgs` parameter, defaulting to `false`. Only the `extern def` path passes `true`. The `...` is not a token — the lexer does not recognise it as such — so the parser manually consumes three consecutive `.` characters. Anything fewer is rejected immediately.

`...` must be the last thing before the closing `)`. Parameters after `...` are a parse error.

Regular `def` functions do not accept `...`. Variadic syntax in a user-defined function body is a parse error. pyxc has no mechanism to *implement* a variadic function — only to *call* one defined in C.

## Codegen

`IsVarArg` is stored on `PrototypeAST` and passed directly to `FunctionType::get`:

```
declare i64 @printf(ptr, ...)
```

At the call site, type checking applies only to the fixed parameters. Arguments beyond the fixed count are passed through as-is — their types are whatever the caller provides. The arity check ensures at least the fixed parameters are present:

```
printf()          # Error: Incorrect # arguments passed  (needs at least fmt)
printf("%ld\n")   # OK — zero variadic args is valid
printf("%ld\n", 42, 99)  # OK — extra args pass through
```

## Error Cases

**Incomplete `...`:**
```pyxc
extern def bad(fmt: ptr[int8], ..) -> int32  # Error: Expected '...' in variadic prototype
```

**Too few fixed arguments:**
```pyxc
extern def printf(fmt: ptr[int8], ...) -> int32
printf()   # Error: Incorrect # arguments passed
```

**`...` in a user-defined function:**
```pyxc
def bad(x: int, ...) -> int:  # Error: Expected parameter name in prototype
  return x
```

## Things Worth Knowing

**`%ld` not `%d` for pyxc's `int`.** pyxc's `int` is 64-bit on 64-bit targets. C's `printf` format specifier `%d` expects a 32-bit `int`. Passing a 64-bit value to `%d` is undefined behaviour — it happens to work for small positive numbers on x86-64 because the upper bits are zero and integers are passed in registers, but it will silently produce wrong output for values above 2,147,483,647. Use `%ld` (long) or `%lld` (long long) for pyxc's `int`. Use `%d` only when you have explicitly cast to `int32`.

**Variadic arguments are not type-checked.** The fixed parameters are checked against the declared types at compile time. Anything past `...` is your responsibility — the compiler passes whatever value you provide, unchanged, to the callee.

**You cannot implement a variadic function in pyxc.** `...` is only valid in `extern def`. There is no `va_list`, `va_start`, or `va_arg` in pyxc. If you need a variadic-style interface in pyxc code, write an overloaded wrapper or accept a fixed-size array.

**Pure `...` with no fixed parameters is valid syntax but rarely useful.** `extern def f(...)` compiles and generates `declare ... @f(...)` in the IR. However, the underlying C `va_start` macro requires at least one named parameter to locate the start of the variadic arguments. Any real C function declared this way would be broken at the ABI level. The form exists because LLVM supports it; use it only if you know the specific function you are wrapping genuinely requires it.

## What's Next

Phase 5 is complete. [Chapter 41](chapter-41.md) begins Phase 6: module declarations and imports, giving pyxc programs a way to split across multiple files.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

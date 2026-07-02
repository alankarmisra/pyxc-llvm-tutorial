---
description: "Complete pyxc's arithmetic: add / and %, five compound assignment operators, and prefix/postfix ++/-- for all lvalue shapes."
---
# 31. pyxc: Arithmetic Completeness

## Where We Are

[Chapter 30](chapter-30.md) finished the object model. Before moving further, there is a gap worth closing: pyxc has `+`, `-`, and `*` but not `/` or `%`. Compound assignment (`+=`, `*=` etc.) does not exist. Neither do `++` and `--`. After this chapter, all of that works:

```pyxc
extern def printd(x: float64)

def main() -> int:
  var a: int = 17
  var b: int = 4
  var q: int = a / b
  var r: int = a % b

  var x: int = 10
  x += 5
  x -= 3
  x *= 2
  x /= 4
  x %= 10

  var i: int = 0
  i++
  ++i

  printd(float64(q + r + x + i))
  return 0
```

```
14.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-31
```

## Grammar

Three areas of the grammar change.

`assignop` replaces the bare `=` in `assignstmt`, now accepting any of the six assignment operators. `postfixexpr` is inserted between `unaryexpr` and `primary` to capture postfix `++`/`--`. `builtinbinaryop` gains `/` and `%`.

```ebnf
assignstmt      = lvalue assignop expression ;           -- changed
assignop        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;  -- new
unaryexpr       = unaryop unaryexpr | postfixexpr ;      -- changed
unaryop         = "-" | "++" | "--" | userdefunaryop ;   -- changed
postfixexpr     = primary [ postfixop ] ;                -- new
postfixop       = "++" | "--" ;                          -- new
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!=" ;  -- changed
```

### Full Grammar

`code/chapter-31/pyxc.ebnf`

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
assignstmt      = lvalue assignop expression ;
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
unaryexpr       = unaryop unaryexpr | postfixexpr ;
unaryop         = "-" | "++" | "--" | userdefunaryop ;
postfixexpr     = primary [ postfixop ] ;
postfixop       = "++" | "--" ;
primary         = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
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
escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;
dedent          = DEDENT ;

assignop        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!=" ;
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

## New Tokens and Lexer Peek-Ahead

Seven new tokens cover the compound assignment operators and the increment/decrement operators:

```cpp
tok_pluseq     = -45,   // +=
tok_minuseq    = -46,   // -=
tok_muleq      = -47,   // *=
tok_diveq      = -48,   // /=
tok_modeq      = -49,   // %=
tok_plusplus   = -56,   // ++
tok_minusminus = -57,   // --
```

Each is produced by a one-character peek in the lexer. The `+` path illustrates the pattern — on seeing `+`, it peeks at the next character to decide between `+=`, `++`, and bare `+`:

```cpp
if (LexerLastChar == '+') {
  int Next = peek();
  int Tok = '+';
  if (Next == '=')      Tok = (advance(), tok_pluseq);
  else if (Next == '+') Tok = (advance(), tok_plusplus);
  LexerLastChar = advance();
  return Tok;
}
```

The same pattern applies to `-` (which must also handle `->` for the arrow token), `*`, `/`, and `%`. The `/` path is new — previously `/` was an unknown character. Now it returns `'/'` bare or `tok_diveq` if followed by `=`.

## Division and Remainder

`/` and `%` are added to the precedence table at level 40 — the same level as `*`:

```cpp
{'/', 40},
{'%', 40},
```

The LLVM instructions emitted by `EmitBuiltInArithmetic` differ by type:

| Op | Integer | Float |
|----|---------|-------|
| `/` | `sdiv` | `fdiv` |
| `%` | `srem` | error |

`%` on float operands is a type error — `GetBinaryResultType` returns `ValueType::Error` for `%` when either operand is not an integer:

```cpp
if (Op == '%' && (!IsIntType(L) || !IsIntType(R)))
  return ValueType::Error;
```

The pointer arithmetic guard is also tightened: only `+` and `−` allow a pointer on one side. `/` and `%` with a pointer operand are now explicitly rejected:

```cpp
if ((Op == '+' || Op == '-') &&
    ((L == ValueType::Pointer && IsIntType(R)) || ...)) {
  // pointer arithmetic
}
```

## Compound Assignment AST Nodes

There are four AST node classes, one for each lvalue shape, all sharing the same structure: an lvalue, an operator token, and an RHS expression:

```cpp
class CompoundAssignmentExprAST : public ExprAST {       // plain variable
  string Name; int Op; unique_ptr<ExprAST> RHS; ...
};
class FieldCompoundAssignmentExprAST : public ExprAST {  // p.x += 1
  unique_ptr<FieldExprAST> LHS; int Op; unique_ptr<ExprAST> RHS; ...
};
class IndexCompoundAssignmentExprAST : public ExprAST {  // arr[i] *= 2
  unique_ptr<IndexExprAST> LHS; int Op; unique_ptr<ExprAST> RHS; ...
};
class IndexedFieldCompoundAssignmentExprAST : public ExprAST { // arr[i].x += 3
  unique_ptr<IndexedFieldExprAST> LHS; int Op; unique_ptr<ExprAST> RHS; ...
};
```

All four override `shouldPrintValue()` to return `false` — compound assignment is a statement, not a value expression, so the REPL does not auto-print its result.

Two helpers drive the parse dispatch. `IsCompoundAssignTok` checks whether the current token is one of the five compound assignment tokens. `CompoundAssignToBinaryOp` converts it to the corresponding arithmetic operator character so codegen can call `EmitBuiltInArithmetic`:

```cpp
static bool IsCompoundAssignTok(int Tok) {
  return Tok == tok_pluseq || Tok == tok_minuseq || Tok == tok_muleq ||
         Tok == tok_diveq  || Tok == tok_modeq;
}
static int CompoundAssignToBinaryOp(int Tok) {
  switch (Tok) {
  case tok_pluseq:  return '+';
  case tok_minuseq: return '-';
  case tok_muleq:   return '*';
  case tok_diveq:   return '/';
  case tok_modeq:   return '%';
  default:          return 0;
  }
}
```

`ParseCompoundAssignmentRHS` handles the plain-variable case. It looks up the destination type, converts the token to a binary op, calls `ParseExpression` for the RHS, type-checks the result, and returns a `CompoundAssignmentExprAST`. The field and index variants follow the same pattern in their respective parse helpers (`ParseFieldCompoundAssignmentRHS`, etc.).

Codegen for all four nodes is identical in structure: resolve the lvalue to a pointer, load the current value, call `EmitBuiltInArithmetic(Op, old, rhs)`, store the result back.

## `IncDecExprAST` — Prefix and Postfix `++`/`--`

A single AST node handles all four combinations of prefix/postfix × increment/decrement:

```cpp
class IncDecExprAST : public ExprAST {
  unique_ptr<ExprAST> Operand;
  bool IsIncrement;  // true for ++, false for --
  bool IsPrefix;     // true for prefix, false for postfix
public:
  IncDecExprAST(unique_ptr<ExprAST> Operand, bool IsIncrement, bool IsPrefix,
                ValueType Type, const string &StructName = "")
      : Operand(std::move(Operand)), IsIncrement(IsIncrement),
        IsPrefix(IsPrefix) {
    setType(Type, StructName);
  }
  Value *codegen() override;
};
```

The operand must pass `IsIncDecAssignableExpr` — it must be a variable, field, index, or indexed-field expression:

```cpp
static bool IsIncDecAssignableExpr(const ExprAST *E) {
  return dynamic_cast<const VariableExprAST *>(E) ||
         dynamic_cast<const FieldExprAST *>(E) ||
         dynamic_cast<const IndexExprAST *>(E) ||
         dynamic_cast<const IndexedFieldExprAST *>(E);
}
```

Codegen: load the old value → compute `old ± 1` via `EmitBuiltInArithmetic` → store the new value → return `IsPrefix ? new : old`. The postfix form returns the value that existed *before* the mutation, matching C semantics.

## Parsing `++`/`--`

**Postfix** is handled by `ParsePostfixIncDec`, which wraps the primary expression in an `IncDecExprAST` if followed by `++` or `--`. `ParseUnary` now calls this instead of `ParsePrimary` directly:

```cpp
static unique_ptr<ExprAST> ParsePostfixIncDec(unique_ptr<ExprAST> Base) {
  while (CurTok == tok_plusplus || CurTok == tok_minusminus) {
    bool IsIncrement = (CurTok == tok_plusplus);
    if (!IsIncDecAssignableExpr(Base.get()))
      return LogError("Increment/decrement target must be assignable");
    getNextToken(); // eat ++/--
    Base = make_unique<IncDecExprAST>(std::move(Base), IsIncrement,
                                     /*IsPrefix=*/false, ...);
  }
  return Base;
}
// In ParseUnary:
return ParsePostfixIncDec(ParsePrimary());
```

**Prefix** is handled at the top of `ParseUnary`, before the primary:

```cpp
if (CurTok == tok_plusplus || CurTok == tok_minusminus) {
  bool IsIncrement = (CurTok == tok_plusplus);
  getNextToken(); // eat ++/--
  auto Operand = ParseUnary();
  // validate assignable and numeric/pointer
  return make_unique<IncDecExprAST>(std::move(Operand), IsIncrement,
                                   /*IsPrefix=*/true, ...);
}
```

Recursive descent through `ParseUnary` means `++++x` is syntactically valid (prefix applied twice) but only meaningful if `x` is assignable at each level.

## Things Worth Knowing

**`EmitBuiltInArithmetic` is the single implementation path.** Both `BinaryExprAST` and every compound assignment and `IncDecExprAST` node call it. Adding a new arithmetic operator means touching one function.

**Postfix `++` returns the old value.** `var y: int = x++` captures the value before the increment — identical to C.

**`++`/`--` work on pointers.** `p++` advances by one element. Pointer arithmetic rules apply.

**`%` on floats is an error.** There is no floating-point remainder operator in pyxc.

## What's Next

[Chapter 32](chapter-32.md) adds `&&`, `||`, and `!` — logical operators with short-circuit evaluation.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

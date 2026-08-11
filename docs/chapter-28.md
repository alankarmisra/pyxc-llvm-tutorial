---
description: "Add sizeof and pointer casts so pyxc can call malloc and free: heap allocation with manual ownership."
---
# 28. pyxc: Heap Allocation

## What I Am Building

[Chapter 27](chapter-27.md) gave me fixed-size arrays, but they have two limits I can't get around: their size has to be known at compile time, and they die the moment the function that declared them returns. Neither works for data whose size I only know at runtime, or that needs to outlive the function that created it. For that I need the heap, and the heap means calling `malloc` and `free`.

After this chapter:

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

def main() -> int:
  var n: int64 = 5
  var raw: ptr[int8] = ptr[int8](malloc(n * sizeof(int64)))
  var p: ptr[int64] = ptr[int64](raw)
  p[0] = 5
  p[1] = 7
  p[2] = 9
  p[3] = 6
  p[4] = 8
  var q: ptr[int64] = p + 2
  printd(float64(q[0] + q[1] + q[2]))  # 23.000000
  free(raw)
  return 0
```

`malloc` and `free` are just the C standard library functions. I don't need any new machinery to call them; `extern` already lets me call any C function. What I'm actually missing is two smaller things: a way to tell `malloc` how many bytes I want, and a way to tell pyxc what type the bytes it hands back should be treated as.

- **`sizeof(T)`** gives me the byte size of type `T` as a compile-time constant, so I can compute the right argument to `malloc`.
- **`ptr[T](expr)`** reinterprets a pointer of one type as a pointer of another, which I need because `malloc` only ever hands me `ptr[int8]` (raw bytes), and I want to treat them as, say, `ptr[int64]`.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-28
```

## Grammar

Two productions change this chapter: `primary` gains a `sizeof-expression` alternative, and `cast-type` gains `pointer-type`, since a pointer cast target is now legal where it wasn't before. Everything else is exactly what [Chapter 27](chapter-27.md) already had:

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | struct-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
 struct-block                      = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 field-declaration                 = name ":" type ;
 function-definition               = "def" function-signature [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 external                          = "extern" "def" function-signature [ "->" type ] ;
 top-level-statement               = statement ;
 function-signature                = name "(" [ parameters ] ")" ;
 parameters                        = typed-parameter { "," typed-parameter } ;
 typed-parameter                   = name ":" type ;
 if-statement                      = "if" expression ":" suite
                                     { [ end-of-lines ] "elif" expression ":" suite }
                                     [ [ end-of-lines ] "else" ":" suite ] ;
 for-statement                     = "for" ( "var" name ":" type | name )
                                     "=" expression ","
                                     expression "," expression ":" suite ;
 while-statement                   = "while" expression ":" suite ;
 do-while-statement                = "do" ":" suite [ end-of-lines ]
                                     "while" expression ;
 switch-statement                  = "switch" expression ":" end-of-lines
                                     indent switch-body dedent ;
 switch-body                       = switch-case
                                     { end-of-lines switch-case }
                                     [ end-of-lines default-case ] ;
 switch-case                       = "case" switch-integer
                                     { "," switch-integer } ":" suite ;
 default-case                      = "default" ":" suite ;
 variable-statement                = "var" variable-binding
                                     { "," variable-binding } ;
 assignment-statement              = lvalue "=" expression ;
 simple-statement                  = return-statement
                                     | break-statement
                                     | continue-statement
                                     | variable-statement
                                     | assignment-statement
                                     | expression ;
 compound-statement                = if-statement
                                     | for-statement
                                     | while-statement
                                     | do-while-statement
                                     | switch-statement ;
 statement                         = simple-statement | compound-statement ;
 suite                             = simple-statement
                                     | compound-statement
                                     | end-of-lines block ;
 return-statement                  = "return" [ expression ] ;
 break-statement                   = "break" ;
 continue-statement                = "continue" ;
 statement-separator               = end-of-lines | BLOCK_END ;
 block                             = indent statement
                                     { statement-separator statement } dedent ;
 expression                        = logical-or ;
 logical-or                        = logical-and { "||" logical-and } ;
 logical-and                       = bitwise-or { "&&" bitwise-or } ;
 bitwise-or                        = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor                       = bitwise-and { "^" bitwise-and } ;
 bitwise-and                       = equality { "&" equality } ;
 equality                          = relational { ("==" | "!=") relational } ;
 relational                        = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift                             = sum { ("<<" | ">>") sum } ;
 sum                               = term { ("+" | "-") term } ;
 term                              = factor { ("*" | "/" | "%") factor } ;
 lvalue                            = name
                                     { "." name | "[" expression "]" } ;
 variable-binding                  = name ":" type [ "=" expression ] ;
 factor                            = ("-" | "!" | "~") factor | primary ;
 primary                           = cast-expression
+                                    | sizeof-expression
                                     | address-expression
                                     | array-literal
                                     | name-expression
                                     | number-expression
                                     | boolean-literal
                                     | parenthesized-expression ;
 cast-expression                   = cast-type "(" expression ")" ;
+sizeof-expression                 = "sizeof" "(" type ")" ;
 address-expression                = "addr" "(" lvalue ")" ;
 array-literal                     = "[" [ expression
                                       { "," expression } ] "]" ;
 name-expression                   = lvalue | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 indent                            = INDENT ;
 dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 type                              = base-type [ array-suffix ] ;
 base-type                         = builtin-type | struct-type | pointer-type ;
 pointer-type                      = "ptr" "[" type "]" ;
 array-suffix                      = "[" integer "]" ;
 builtin-type                      = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" | "None" ;
 struct-type                       = name ;
-cast-type                         = "int" | "int8" | "int16" | "int32"
+cast-type                         = builtin-cast-type | pointer-type ;
+builtin-cast-type                 = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" ;
 number                            = ( digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ) [ exponent ] ;
 switch-integer                    = [ "-" ] digit { digit } ;
 exponent                          = ( "e" | "E" ) [ "+" | "-" ]
                                     digit { digit } ;
 boolean-literal                   = "True" | "False" ;
 integer                           = digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 (*
     A `comment` begins with "#" and continues to the end of the line. The lexer
      ignores its text and returns an end-of-line token when one follows it.
 *)
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 (*
     `whitespace` may appear before or between tokens
      and is ignored by the lexer.
 *)
 whitespace                        = " " | "\t" | "\v" | "\f" ;
 INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
 DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
 BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
                                       immediately after it consumes DEDENT ? ;

```

## One New Keyword

`sizeof` needs a token like every other keyword I've added:

```cpp
tok_sizeof = -53,
```

```cpp
{"sizeof", tok_sizeof}
```

## `sizeof(T)`: a Size I Don't Have to Compute Myself

I could compute a struct's size by hand: add up its fields, account for padding, remember that pointers are 8 bytes on a 64-bit target. But I already have all of that information, since I just built the LLVM type for every struct I've declared. Asking LLVM for the size directly is both less error-prone and correct across whatever target I eventually compile for.

`sizeof(T)` always produces an `int64`, no matter what `T` is:

```cpp
class SizeofExpressionNode : public ExpressionNode {
  ValueType TargetType;
  string TargetTypeInfo;

public:
  SizeofExpressionNode(ValueType TargetType,
                       const string &TargetTypeInfo = "")
      : TargetType(TargetType), TargetTypeInfo(TargetTypeInfo) {
    setType(ValueType::Int64);
  }
  Value *codegen() override;
};
```

I call `setType(ValueType::Int64)` in the constructor rather than leaving it to be inferred later, since the result type never depends on `TargetType`: `sizeof(int8)` and `sizeof(Point)` are both `int64`.

Parsing it means reusing `ParseTypeToken`, the same function every other type annotation in pyxc goes through:

```cpp
static unique_ptr<ExpressionNode> ParseSizeofExpression() {
  getNextToken(); // eat 'sizeof'
  if (CurrentToken != tok_lparen)
    return LogErrorExpression("Expected '(' after sizeof");
  getNextToken(); // eat '('
  string TargetTypeInfo;
  ValueType TargetType = ParseTypeToken(&TargetTypeInfo);
  if (TargetType == ValueType::Error)
    return nullptr;
  if (TargetType == ValueType::None)
    return LogErrorExpression("Cannot take sizeof(None)");
  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')' after sizeof type");
  getNextToken(); // eat ')'
  return make_unique<SizeofExpressionNode>(TargetType, TargetTypeInfo);
}
```

`sizeof(None)` is the one type I reject outright: `None` isn't a value at all, so asking for its size is a question that shouldn't have been asked. `ParsePrimary` routes `tok_sizeof` here:

```cpp
case tok_sizeof:
  return ParseSizeofExpression();
```

Codegen doesn't emit an instruction, since there's nothing to compute at runtime:

```cpp
Value *SizeofExpressionNode::codegen() {
  llvm::Type *TargetLLVMType = LLVMTypeFor(TargetType, TargetTypeInfo);
  if (!TargetLLVMType)
    return LogErrorV("Invalid sizeof target type");
  uint64_t Bytes = TheModule->getDataLayout()
                       .getTypeAllocSize(TargetLLVMType)
                       .getFixedValue();
  return ConstantInt::get(Type::getInt64Ty(*TheContext), Bytes);
}
```

`getTypeAllocSize` is LLVM's own answer to "how many bytes does one of these take up in an array," padding included, for whatever target I'm compiling for. I hand it a type and get back a number I fold directly into a constant. For a function that just returns `sizeof(int64)`, the generated IR is:

```llvm
define i64 @size_i64() {
entry:
  ret i64 8
}
```

No call, no load: the size was already known once code generation started, so it's just a literal by the time IR exists.

Sizes I get on a 64-bit target:

| Type | `sizeof` |
|------|---------|
| `int8` | 1 |
| `int32` | 4 |
| `int64` | 8 |
| `ptr[int8]` | 8 |
| `Point` (two `int` fields) | 16 |

Every pointer is 8 bytes here regardless of what it points to. LLVM's opaque pointer model means there's only one pointer representation at the IR level; pyxc is the one tracking what it points to, not LLVM.

## `ptr[T](expr)`: Reinterpreting a Pointer

Before this chapter, `ptr[int64](raw)` wasn't rejected by any type check, it was rejected by the parser before it got that far: `ParsePrimary` had no `case tok_ptr` at all, so a leading `ptr` in expression position was simply "unknown token when expecting an expression." I confirmed this against [Chapter 27](chapter-27.md)'s binary directly rather than guess at it.

So the fix isn't lifting a guard, it's adding a case, falling through to the exact same call every other cast target already uses:

```cpp
case tok_bool:
case tok_ptr:
  return ParseCastExpression();
case tok_sizeof:
  return ParseSizeofExpression();
```

Once `tok_ptr` is reachable, though, I do need one new guard, since `ParseCastExpression` will now happily see `Type == ValueType::Pointer` and I don't want to allow casting just anything to a pointer:

```cpp
if (Type == ValueType::Pointer && Expr->getType() != ValueType::Pointer)
  return LogErrorExpression("Pointer casts require a pointer operand");
return make_unique<CastExpressionNode>(Type, std::move(Expr),
                                        TargetTypeInfo);
```

I don't allow casting an integer to a pointer. There's no address I could hand it that pyxc could vouch for, and letting that through would just be a way to smuggle in undefined behavior with a friendlier syntax.

`CastExpressionNode` needs a `TargetTypeInfo` now, for the same reason `SizeofExpressionNode` does: `ptr[Point]` and `ptr[int64]` are both `ValueType::Pointer`, so the pointee type has to travel separately:

```cpp
class CastExpressionNode : public ExpressionNode {
  ValueType TargetType;
  string TargetTypeInfo;
  unique_ptr<ExpressionNode> Expr;

public:
  CastExpressionNode(ValueType TargetType, unique_ptr<ExpressionNode> Expr,
                     const string &TargetTypeInfo = "")
      : TargetType(TargetType), TargetTypeInfo(TargetTypeInfo),
        Expr(std::move(Expr)) {
    setType(TargetType, TargetTypeInfo);
  }
  Value *codegen() override;
};
```

Without it, a cast to `ptr[int64]` would carry no pointee information at all, and anything downstream that indexes or reads through the result wouldn't know what it's pointing at.

Codegen for the pointer-to-pointer case is nothing at all, because at the LLVM level there's nothing to do:

```cpp
if (From == ValueType::Pointer && To == ValueType::Pointer)
  return V;
```

With opaque pointers, every pointer is the same IR type regardless of what it points to, so there's no instruction to emit between two of them: the value just passes through unchanged. I confirmed this by compiling a cast and reading the IR:

```llvm
%calltmp = call ptr @malloc(i64 8)
store ptr %calltmp, ptr %raw, align 8
%raw1 = load ptr, ptr %raw, align 8
store ptr %raw1, ptr %p, align 8
```

No `bitcast` anywhere. The cast's only real effect is at the pyxc level: the result is now typed `ptr[int64]` instead of `ptr[int8]`, so any GEP or load I generate from it afterward uses the right element type.

## Calling `malloc` and `free`

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
```

Nothing about these declarations is special; any C function with compatible types can be called the same way. `malloc` always hands back `ptr[int8]`, raw bytes with no notion of what they're eventually going to hold. Assigning that straight to a `var raw: ptr[int8]` works fine, since the declared pointee and the call's pointee already match. What doesn't work is going straight to the type I actually want:

```pyxc
var p: ptr[Point] = malloc(sizeof(Point))
```

```text
Error: Type mismatch in variable initialization
```

This is the same pointee-type check every other pointer assignment goes through: the declared variable's pointee (`Point`) doesn't match the initializer's pointee (`int8`), so it's rejected exactly like assigning a `ptr[int8]` local to a `ptr[Point]` variable would be. `sizeof(Point)` tells `malloc` how many bytes to hand back, but it doesn't change what type those bytes come back as. The fix is the cast I just added: `ptr[Point](expr)` reinterprets the `ptr[int8]` result as a `ptr[Point]`.

```pyxc
var raw: ptr[int8] = malloc(sizeof(Point))
var p: ptr[Point] = ptr[Point](raw)
```

The full pattern for heap-allocating a single struct:

```pyxc
struct Point:
  x: int
  y: int

def main() -> int:
  var raw: ptr[int8] = ptr[int8](malloc(sizeof(Point)))
  var p: ptr[Point] = ptr[Point](raw)
  p[0].x = 77
  printd(float64(p[0].x))
  free(raw)
  return 0
```

`malloc` hands back raw bytes as `ptr[int8]`. `ptr[Point](raw)` tells pyxc to treat those same bytes as a `Point`, so `p[0].x` generates the right field offset. `free` gets the original `ptr[int8]` back. Passing `p` directly would be a type error, since `p` is `ptr[Point]`, not `ptr[int8]`.

## Build and Run

```bash
cd code/chapter-28
cmake -S . -B build && cmake --build build
```

## Try It

### `sizeof` of scalar types and a struct

```pyxc
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def main() -> int:
  printd(float64(sizeof(int8)))
  printd(float64(sizeof(int32)))
  printd(float64(sizeof(int64)))
  printd(float64(sizeof(ptr[int8])))
  printd(float64(sizeof(Point)))
  return 0
```

```text
1.000000
4.000000
8.000000
8.000000
16.000000
```

### `malloc`, a pointer cast, and field access

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def main() -> int:
  var raw: ptr[int8] = ptr[int8](malloc(sizeof(Point)))
  var p: ptr[Point] = ptr[Point](raw)
  p[0].x = 77
  p[0].y = 33
  printd(float64(p[0].x))
  printd(float64(p[0].y))
  free(raw)
  return 0
```

```text
77.000000
33.000000
```

### `malloc` and pointer arithmetic: a heap array

```pyxc
extern def malloc(n: int64) -> ptr[int8]
extern def free(p: ptr[int8])
extern def printd(x: float64)

def main() -> int:
  var n: int64 = 5
  var raw: ptr[int8] = ptr[int8](malloc(n * sizeof(int64)))
  var p: ptr[int64] = ptr[int64](raw)
  p[0] = 5
  p[1] = 7
  p[2] = 9
  p[3] = 6
  p[4] = 8
  var q: ptr[int64] = p + 2
  printd(float64(q[0] + q[1] + q[2]))
  free(raw)
  return 0
```

```text
23.000000
```

### Inspecting the IR: `sizeof` really is just a constant

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'ret i64' out.ll
```

```llvm
ret i64 8
```

## Known Limitations

**No null check.** `malloc` can return null when the system is out of memory. I don't insert a null check; dereferencing a null pointer crashes silently.

**No bounds checking.** Accessing `p[n]` on a heap buffer of size `n` is an out-of-bounds write. I don't track buffer sizes anywhere.

**Manual ownership.** There's no destructor, no reference counting, no garbage collector. Forgetting to call `free` leaks memory; calling it twice or reading after `free` is undefined behavior: silently corrupted data, or a crash, with no diagnostic pointing at why.

**Pointer casts are pointer-only.** `ptr[T](expr)` requires `expr` to already be a pointer; I don't let an integer become a pointer through a cast. And the cast doesn't do anything at the LLVM level, since every pointer already has the same IR representation; its only job is telling pyxc what pointee type to track from here on.

## What's Next

[Chapter 29](chapter-29.md) adds type aliases.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

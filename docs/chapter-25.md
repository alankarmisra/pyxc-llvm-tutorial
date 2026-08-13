---
description: "Add pointer types, addr() for taking addresses, and p[i] indexing: so functions can modify the caller's data."
---
# 25. pyxc: Pointers

## What I Am Building

[Chapter 24](chapter-24.md) gave me structs, but with a catch: structs are passed by value. If I hand a struct to a function and the function modifies a field, my copy back at the call site is unchanged. That's fine for pure computations, and it's a deliberate design choice, but sometimes I actually want to modify the caller's data.

That's what pointers are for. After this chapter:

```pyxc
struct Point:
  x: int
  y: int

def translate(p: ptr[Point], dx: int, dy: int) -> None:
  p[0].x = p[0].x + dx
  p[0].y = p[0].y + dy

def main() -> int:
  var pt: Point
  pt.x = 3
  pt.y = 4
  translate(addr(pt), 10, 20)
  printd(float64(pt.x))  # 13.000000
  printd(float64(pt.y))  # 24.000000
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-25
```

## Grammar

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
-lvalue                            = name | field-access ;
+lvalue                            = name
+                                    { "." name | "[" expression "]" } ;
 variable-binding                  = name ":" type [ "=" expression ] ;
 factor                            = ("-" | "!" | "~") factor | primary ;
 primary                           = cast-expression
+                                    | address-expression
                                     | name-expression
-                                    | field-access
                                     | number-expression
                                     | boolean-literal
                                     | parenthesized-expression ;
 cast-expression                   = cast-type "(" expression ")" ;
-name-expression                   = name | call-expression ;
+address-expression                = "addr" "(" lvalue ")" ;
+name-expression                   = lvalue | call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
-field-access                      = name "." name { "." name } ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 indent                            = INDENT ;
 dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
-type                              = builtin-type | struct-type ;
+type                              = builtin-type | struct-type | pointer-type ;
+pointer-type                      = "ptr" "[" type "]" ;
 builtin-type                      = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" | "None" ;
 struct-type                       = name ;
 cast-type                         = "int" | "int8" | "int16" | "int32"
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
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 whitespace                        = " " | "\t" | "\v" | "\f" ;
 INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
 DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
 BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
                                       immediately after it consumes DEDENT ? ;
```

`ptr[T]` is a type annotation only: I can't construct one without `addr`. `addr` takes an lvalue (a named variable, optionally followed by field access) and returns a pointer to it. `p[i]` reads or writes the value at offset `i` from the pointer. `p[i].field` chains field access after indexing, for pointers to structs.

Nested pointer types (`ptr[ptr[int]]`) and pointers to `None` are rejected at parse time.

`lvalue`'s `{ "." name | "[" expression "]" }` repetition is what lets `.field` and `[index]` chain in any combination after the leading name — `p.field[i]` and `p[i].field` are both just different orderings of the same repeated suffix, not separate grammar rules.

## Two New Keywords

```cpp
tok_ptr = -51,
tok_addr = -52,
```

Registered in the keyword map:

```cpp
{"ptr", tok_ptr}, {"addr", tok_addr}
```

## Representing a Pointer's Type

`Pointer` is a new entry in the `ValueType` enum, after `Struct`. Unlike scalar types, a pointer value isn't self-describing: `ValueType::Pointer` alone doesn't say what the pointer points to. I need the pointee type carried alongside it.

For scalar types and structs, the pointee information is carried in the `StructName` string field that already exists on every `ExpressionNode` node. For pointers, that same field is reused to carry an encoded string describing the pointee:

```cpp
static string EncodePointerType(ValueType PointeeType,
                                const string &PointeeStructName) {
  return std::to_string(static_cast<int>(PointeeType)) + ":" + PointeeStructName;
}

static bool DecodePointerType(const string &Encoded, ValueType &PointeeType,
                              string &PointeeStructName) {
  auto Pos = Encoded.find(':');
  // split on ':', parse the int as ValueType, rest is struct name
  ...
}
```

Every type in the compiler is described by two fields: a `ValueType` enum and a `StructName` string. For most types `StructName` is empty. For structs it holds the struct name. For pointers it holds a serialized description of the pointee, because `ValueType::Pointer` alone does not say what the pointer points to:

| Type | `ValueType` | `StructName` |
|---|---|---|
| `int`, `float64`, … | `Int`, `Float64`, … | `""` |
| `Point` (struct) | `Struct` | `"Point"` |
| `ptr[int]` | `Pointer` | `"1:"` |
| `ptr[Point]` | `Pointer` | `"14:Point"` |

The format is `"<ValueType int>:<struct name>"`. `ptr[int]` encodes as `"1:"` (`ValueType::Int` = 1, no struct name). `ptr[Point]` encodes as `"14:Point"` (`ValueType::Struct` = 14, struct name `"Point"`). The caller that created the pointer type is responsible for encoding; any site that needs the pointee type decodes it.

This reuses the existing type-tracking infrastructure without adding a new field to the base class. It is a tradeoff: the encoding is not beautiful, but it works and the surface is small.

## The LLVM Pointer Type

In LLVM IR, all pointer types are the same opaque type:

```cpp
case ValueType::Pointer:
  return PointerType::get(*TheContext, 0);
```

`PointerType::get` with address space 0 produces the opaque `ptr` type: LLVM does not distinguish `ptr[int]` from `ptr[Point]` in the type system. The element type only appears in `getelementptr` and `load`/`store` instructions, not in the pointer type itself. This is LLVM's opaque pointer model, which has been the default since LLVM 15.

The zero value for a pointer is null:

```cpp
case ValueType::Pointer:
  return ConstantPointerNull::get(
      cast<PointerType>(LLVMTypeFor(Type, StructName)));
```

`var p: ptr[int]` with no initializer starts as a null pointer.

## Parsing `ptr[T]`

`ParseTypeToken` handles the `tok_ptr` case:

```cpp
case tok_ptr: {
  getNextToken(); // eat 'ptr'
  // expect '['
  getNextToken(); // eat '['
  string PointeeStructName;
  ValueType PointeeType = ParseTypeToken(&PointeeStructName);
  // reject None and nested ptr
  getNextToken(); // eat ']'
  if (StructName)
    *StructName = EncodePointerType(PointeeType, PointeeStructName);
  return ValueType::Pointer;
}
```

The parsed pointee type is immediately encoded and written into the `StructName` output parameter. From this point on, the pointer's pointee information travels with it as an opaque string through `VarScopes`, `FunctionSignatureNode::ParameterInfo`, `NameExpressionNode`, and every other place that stores a `ValueType` alongside a `StructName`.

## `addr`: Taking the Address of an Lvalue

`addr(x)` returns a pointer to `x`. `addr(p.x)` returns a pointer to the field `x` of struct `p`. `ParseAddrExpression` handles both by parsing the operand as a full name expression and requiring the result to be an lvalue:

```cpp
static unique_ptr<ExpressionNode> ParseAddrExpression() {
  getNextToken(); // eat 'addr'
  if (CurrentToken != tok_lparen)
    return LogErrorExpression("Expected '(' after addr");
  getNextToken(); // eat '('
  if (CurrentToken != tok_name)
    return LogErrorExpression("addr expects an lvalue");

  string ParsedName = Name;
  getNextToken(); // eat name
  auto Operand = ParseNameExpressionWithName(ParsedName);
  if (!Operand || !Operand->isLValue())
    return LogErrorExpression("addr expects an lvalue");
  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')' after addr operand");
  getNextToken(); // eat ')'

  string PointerTypeInfo =
      EncodePointerType(Operand->getType(), Operand->getStructName());
  return make_unique<AddrExpressionNode>(std::move(Operand), PointerTypeInfo);
}
```

Because `ParseNameExpressionWithName` (below) already walks `.field` and `[index]` chains, `addr` gets `addr(p.x)` and even `addr(p[i].x)` for free: it doesn't need its own field-walking logic. It only has to check `isLValue()` on the result.

`addr` only accepts a named variable, optionally followed by field access or indexing. Expressions like `addr(1 + 2)` are rejected immediately: the parser checks for `tok_name` right after the opening `(`, and a non-lvalue expression fails the `isLValue()` check.

### Codegen for `addr`

```cpp
Value *AddrExpressionNode::codegen() {
  Value *Address = Operand->codegenAddress();
  if (!Address)
    return LogErrorV("addr expects an lvalue");
  return Address;
}
```

`codegenAddress()` is a virtual method every lvalue node overrides. For a plain variable it's the `alloca` already sitting in `NamedValues`; for a field it's a `getelementptr` off the base's address; for an index it's a `getelementptr` off the loaded pointer value. `addr` just asks its operand for that address and returns it unchanged; it doesn't need to know which kind of lvalue it's holding.

```llvm
; var x: int = 42
; var p: ptr[int] = addr(x)
%p = alloca ptr, align 8
%x = alloca i64, align 8
store i64 42, ptr %x, align 8
store ptr %x, ptr %p, align 8   ; addr(x) is just %x: the alloca itself
```

```llvm
; var pt: Point
; var px: ptr[int] = addr(pt.x)
%px = alloca ptr, align 8
%pt = alloca %struct.Point, align 8
store %struct.Point zeroinitializer, ptr %pt, align 8
%fieldptr = getelementptr inbounds nuw %struct.Point, ptr %pt, i32 0, i32 0
store ptr %fieldptr, ptr %px, align 8
```

`%p`/`%px` show up before `%x`/`%pt` in the IR even though they're declared second in the source: `CreateEntryBlockAlloca` inserts every new `alloca` at the very front of the entry block (`TheFunction->getEntryBlock().begin()`), not after whatever's already there, so each variable's `alloca` ends up ahead of every one declared before it.

## `p[i]`: Pointer Indexing

`p[i]` computes the address at offset `i` from the pointer and loads from it. `p[i] = v` stores to it. Both go through the same `ParseNameExpressionWithName` loop that also handles `.field`: after parsing the base name into a `NameExpressionNode`, the parser loops while it sees `.` or `[`, wrapping the result in one more node each time:

```cpp
if (Result->getType() != ValueType::Pointer)
  return LogErrorExpression("Indexing requires a pointer value");
ValueType ElementType = ValueType::Error;
string ElementStructName;
if (!DecodePointerType(Result->getStructName(), ElementType,
                       ElementStructName))
  return LogErrorExpression("Invalid pointer type metadata");
getNextToken(); // eat '['
auto Index = ParseExpression();
if (!Index)
  return nullptr;
if (!IsIntType(Index->getType()))
  return LogErrorExpression("Pointer index must be an integer");
if (CurrentToken != tok_rbracket)
  return LogErrorExpression("Expected ']' after index expression");
getNextToken(); // eat ']'
Result = make_unique<IndexExpressionNode>(
    std::move(Result), std::move(Index), ElementType,
    ElementStructName);
```

`IndexExpressionNode` wraps whatever came before it as its `Base`, so `p[i]` and (later) `p.field[i]` fall out of the same loop without a separate parsing path. The element type is decoded from the pointer's encoded `StructName`.

### The Shared Address Computation

Every lvalue node in the compiler implements `codegenAddress()`, which computes the storage address without loading. `IndexExpressionNode`'s version evaluates the base (loading the pointer value), widens the index to `i64`, and GEPs:

```cpp
Value *IndexExpressionNode::codegenAddress() {
  Value *BasePointer = Base->codegen();
  if (!BasePointer)
    return nullptr;
  Value *IndexValue = Index->codegen();
  if (!IndexValue)
    return nullptr;
  IndexValue = TheBuilder->CreateIntCast(
      IndexValue, Type::getInt64Ty(*TheContext),
      !IsUnsignedIntType(Index->getType()), "index");
  return TheBuilder->CreateInBoundsGEP(
      LLVMTypeFor(getType(), getStructName()), BasePointer, IndexValue,
      "elemptr");
}
```

The index is always widened to `i64` before the GEP: LLVM requires a consistent index type.

### Read codegen

```cpp
Value *IndexExpressionNode::codegen() {
  Value *Address = codegenAddress();
  if (!Address)
    return nullptr;
  return TheBuilder->CreateLoad(LLVMTypeFor(getType(), getStructName()), Address,
                             "elemload");
}
```

For `p[0]` where `p: ptr[int]`:

```llvm
%p1 = load ptr, ptr %p            ; load the pointer value
%elemptr = getelementptr inbounds i64, ptr %p1, i64 0
%elemload = load i64, ptr %elemptr, align 8
```

### Write codegen

An assignment whose left side is an lvalue, `p[0] = 99`, goes through `LValueAssignmentStatementNode`, which asks the left side for its address the same way `addr` does:

```cpp
Value *LValueAssignmentStatementNode::codegen() {
  Value *Address = Left->codegenAddress();
  if (!Address)
    return LogErrorV("Destination of '=' must be an lvalue");
  Value *AssignedValue = Right->codegen();
  if (!AssignedValue)
    return nullptr;
  AssignedValue = EmitImplicitCast(AssignedValue, Right->getType(), getType());
  if (!AssignedValue)
    return LogErrorV("Type mismatch in assignment");
  TheBuilder->CreateStore(AssignedValue, Address);
  return AssignedValue;
}
```

For `p[0] = 99` where `p: ptr[int]`:

```llvm
%p1 = load ptr, ptr %p
%elemptr = getelementptr inbounds i64, ptr %p1, i64 0
%v2 = load i64, ptr %v, align 8
store i64 %v2, ptr %elemptr, align 8
```

The implicit cast rules from chapter 20 apply: assigning an integer to a `ptr[float64]` is a type error; assigning `int8` to `ptr[int]` widens.

## `p[i].field`: Field Access After Indexing

For pointers to structs, I can chain field access after the index: `p[0].x`. Because `.field` and `[index]` both go through the same loop in `ParseNameExpressionWithName`, this needs no separate AST node: `p[0].x` is a `MemberExpressionNode` whose `Base` happens to be an `IndexExpressionNode` instead of a `NameExpressionNode`. The same field-access branch of the loop handles it, since it only checks that the current result's type is `Struct`.

### Codegen

`MemberExpressionNode::codegenAddress()` asks its base for its address first, then GEPs to the field. When the base is an `IndexExpressionNode`, that recursive call runs the pointer-indexing GEP from the section above:

```cpp
Value *MemberExpressionNode::codegenAddress() {
  Value *BaseAddress = Base->codegenAddress();
  if (!BaseAddress)
    return LogErrorV("Field access requires an lvalue");
  return TheBuilder->CreateStructGEP(
      LLVMTypeFor(ValueType::Struct, Base->getStructName()), BaseAddress,
      FieldIndex, "fieldptr");
}

Value *MemberExpressionNode::codegen() {
  Value *Address = codegenAddress();
  if (!Address)
    return nullptr;
  return TheBuilder->CreateLoad(LLVMTypeFor(getType(), getStructName()), Address,
                             "fieldload");
}
```

For `p[0].x` where `p: ptr[Point]`:

```llvm
%p3 = load ptr, ptr %p1, align 8
%elemptr = getelementptr inbounds %struct.Point, ptr %p3, i64 0
%fieldptr = getelementptr inbounds nuw %struct.Point, ptr %elemptr, i32 0, i32 0
%fieldload = load i64, ptr %fieldptr
```

Two GEPs: one to reach element 0 of the array, one to reach field `x` of that element. No load between them: `codegenAddress()` chains straight from the index's address into the field's GEP.

## Mutation Through a Pointer Parameter

This is the payoff. A function that takes `ptr[T]` can modify the caller's data:

```pyxc
def set_value(p: ptr[int], v: int) -> None:
  p[0] = v

def main() -> int:
  var x: int = 5
  set_value(addr(x), 100)
  # x is now 100
  return 0
```

```llvm
define void @set_value(ptr %p, i64 %v) {
entry:
  %v2 = alloca i64, align 8
  %p1 = alloca ptr, align 8
  store ptr %p, ptr %p1, align 8
  store i64 %v, ptr %v2, align 8
  %p3 = load ptr, ptr %p1, align 8
  %elemptr = getelementptr inbounds i64, ptr %p3, i64 0
  %v3 = load i64, ptr %v2, align 8
  store i64 %v3, ptr %elemptr, align 8
  ret void
}
```

The pointer is passed by value (it's just an address), but the store through it writes to `x`'s alloca in the caller's stack frame. The caller sees the updated value.

Pointer arguments are type-checked: passing `ptr[float64]` where `ptr[int]` is expected is a type error.

## Parse Flow for Name Expressions

`ParseNameExpressionWithName` parses the base identifier into a `NameExpressionNode`, then loops while it sees `.` or `[`, rewrapping the result at each step:

1. Parse the base identifier into a `NameExpressionNode`.
2. While `.` or `[` follows: if `.`, require the current result to be a `Struct` and wrap it in a `MemberExpressionNode`; if `[`, require the current result to be a `Pointer` and wrap it in an `IndexExpressionNode`.

Because the loop rewraps whatever came before, this one function covers `x`, `p.field`, `p[i]`, `p.field[i]`, `p[i].field`, and `p[i].field.subfield` without separate parsing paths for each combination.

`ParseLeadingNameSimpleStatement` calls the same function to parse the left side of an assignment, then checks `isLValue()` on the result before accepting a trailing `=`.

## Build and Run

```bash
cd code/chapter-25
cmake -S . -B build && cmake --build build
```

## Try It

### Take an address, read through it

```pyxc
extern def printd(x: float64)

def main() -> int:
  var x: int = 42
  var p: ptr[int] = addr(x)
  printd(float64(p[0]))
  return 0
```

```bash
42.000000
```

### Write through a pointer, see it in the caller

```pyxc
extern def printd(x: float64)

def main() -> int:
  var x: int = 5
  var p: ptr[int] = addr(x)
  p[0] = 99
  printd(float64(x))
  return 0
```

```bash
99.000000
```

### Pass a struct by pointer

```pyxc
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def set_x(p: ptr[Point], v: int) -> None:
  p[0].x = v

def main() -> int:
  var pt: Point
  pt.x = 3
  set_x(addr(pt), 7)
  printd(float64(pt.x))
  return 0
```

```bash
7.000000
```

### Address of a struct field

```pyxc
extern def printd(x: float64)

struct Point:
  x: int
  y: int

def main() -> int:
  var p: Point
  p.x = 11
  var px: ptr[int] = addr(p.x)
  printd(float64(px[0]))
  return 0
```

```bash
11.000000
```

### Inspect the IR

```bash
pyxc --emit llvm-ir -o out.ll program.pyxc
grep 'getelementptr\|load\|store' out.ll
```

## Known Limitations

**No pointer arithmetic.** `p + 1` is not supported: use `p[1]` to access adjacent elements.

**No nested pointers.** `ptr[ptr[int]]` is rejected at parse time.

**No pointer comparisons.** `p == nullptr` is not supported.

**No pointer-to-pointer casting.** You cannot reinterpret a `ptr[int]` as a `ptr[float64]`.

**Null pointer is silent.** `var p: ptr[int]` with no initializer is a null pointer. Dereferencing it crashes at runtime with no helpful error. Bounds checking and null safety are not implemented.

**Pointee type is encoded in a string.** The `StructName` field on `ExpressionNode` nodes doubles as pointer type metadata, stored as `"<ValueType int>:<struct name>"` (e.g. `"1:"` for `ptr[int]`, `"14:Point"` for `ptr[Point]`). It works but is not the cleanest representation: a dedicated field would be cleaner. This is a consequence of the single-AST-hierarchy design established in chapter 12.

## What's Next

[Chapter 26](chapter-26.md) adds pointer arithmetic.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

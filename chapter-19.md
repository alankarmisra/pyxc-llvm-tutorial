# 19. Structs and Named Field Access

Chapter 18 gave us mature control flow and a stronger operator set.

But data modeling was still flat. We could pass scalars, pointers, and aliases, yet we had no way to express “a value with multiple named parts” in the language itself.

Chapter 19 introduces that missing piece: `struct`.

The chapter is intentionally focused. We do **not** add methods, constructors, classes, or inheritance. We add only what we need for C-style aggregate data:

- top-level struct declarations
- struct-typed variables
- field reads (`obj.x`)
- field writes (`obj.x = ...`)
- nested member chains (`outer.inner.x`)

That limited scope keeps the parser and type resolver understandable while still unlocking a major jump in language expressiveness.


!!!note
    To follow along you can download the code from GitHub [pyxc-llvm-tutorial](https://github.com/alankarmisra/pyxc-llvm-tutorial) or you can see the full source code here [code/chapter19](https://github.com/alankarmisra/pyxc-llvm-tutorial/tree/main/code/chapter19).

## Why this chapter matters

Once you have loops and conditionals, the next real bottleneck is data shape.

Without structs, code quickly turns into parallel variables (`x`, `y`, `z`) that are hard to pass around safely. With structs, you can represent coherent records:

```py
struct Point:
    x: i32
    y: i32
```

That one construct creates a foundation for all later data features, including arrays-of-structs (Chapter 20), heap-allocated structs (Chapter 21), and pointer-oriented interop work.

## Scope and constraints

Chapter 19 supports:

- `struct` declarations at top level only
- named fields with explicit types
- field selection with `.`
- field assignment and nested field access

Chapter 19 intentionally does *not* support:

- struct literals
- struct constructors
- methods/member functions
- inline struct declarations in local scope

Keeping struct declarations top-level avoids many symbol/ownership complexities in this step.

## What changed from Chapter 18

Primary diff:

- `code/chapter18/pyxc.cpp` -> `code/chapter19/pyxc.cpp`
- `code/chapter18/pyxc.ebnf` -> `code/chapter19/pyxc.ebnf`

Major implementation buckets:

1. lexical keyword support for `struct`
2. grammar/parser support for top-level struct declarations
3. postfix parser support for `.` member access
4. struct metadata tables in the compiler
5. LLVM struct type resolution and caching
6. member address/load codegen (`MemberExprAST`)
7. struct diagnostics + test coverage

We walk through these in order.

## 1. Grammar updates

In `code/chapter19/pyxc.ebnf`, Chapter 19 introduces `struct_decl` and member-expression forms.

```ebnf
top_item        = newline
                | type_alias_decl
                | struct_decl
                | function_def
                | extern_decl
                | statement ;

struct_decl     = "struct" , identifier , ":" , newline , indent ,
                  struct_field , { newline , struct_field } , [ newline ] ,
                  dedent ;

postfix_expr    = primary , { call_suffix | index_suffix | member_suffix } ;
member_suffix   = "." , identifier ;
member_expr     = postfix_expr , member_suffix ;

lvalue          = identifier
                | index_expr
                | member_expr ;
```

The important design choice is: member access is a **postfix chain**. That means `a.b.c[0].x` can be parsed naturally by repeating postfix rules.

## 2. Lexer updates

Chapter 19 adds `struct` as a keyword token:

```cpp
tok_struct = -32,
```

and keyword-table entry:

```cpp
{"struct", tok_struct}
```

### Dot/number disambiguation

The lexer already supported numeric literals like `.5`. But adding member access means `.` must also work as a punctuation token for `obj.field`.

So Chapter 19 tightens the check: `.` starts a number **only if the next char is a digit**.

```cpp
bool DotStartsNumber = false;
if (LastChar == '.') {
  int NextCh = getc(InputFile);
  if (NextCh != EOF)
    ungetc(NextCh, InputFile);
  DotStartsNumber = isdigit(NextCh);
}

if (isdigit(LastChar) || DotStartsNumber) {
  ...
}
```

That small lexer decision prevents member-access parsing bugs later.

## 3. AST additions: MemberExprAST

Chapter 19 introduces:

```cpp
class MemberExprAST : public ExprAST {
  std::unique_ptr<ExprAST> Base;
  std::string FieldName;
  ...
  Value *codegen() override;
  Value *codegenAddress() override;
  Type *getValueTypeHint() const override;
  std::string getBuiltinLeafTypeHint() const override;
};
```

Two methods are central:

- `codegenAddress()` for lvalue use (`p.x = 10`)
- `codegen()` for rvalue use (`print(p.x)`)

This mirrors the same lvalue/rvalue split already used for variables and indexing.

## 4. Parsing struct declarations

Chapter 19 adds a dedicated top-level parser path:

```cpp
static bool ParseStructDecl();
```

Core behavior:

- parse header: `struct <Name>:`
- require newline + indented field list
- parse each field: `<name>: <type_expr>`
- enforce constraints:
  - no duplicate struct names
  - no duplicate field names in one struct
  - at least one field

Struct declarations are accepted only in top-level loops and explicitly rejected in statement context:

```cpp
case tok_struct:
  return LogError<StmtPtr>("Struct declarations are only allowed at top-level");
```

This keeps symbol lifetime and lookup simple for this phase.

## 5. Parsing member access chains

`ParseIdentifierExpr()` already had a postfix loop for calls and indexing.
Chapter 19 extends that loop with member suffix handling:

```cpp
if (CurTok == '.') {
  SourceLocation MemberLoc = CurLoc;
  getNextToken(); // eat '.'
  if (CurTok != tok_identifier)
    return LogError<ExprPtr>("Expected field name after '.'");
  std::string FieldName = IdentifierStr;
  getNextToken(); // eat field name
  Expr = std::make_unique<MemberExprAST>(MemberLoc, std::move(Expr),
                                         std::move(FieldName));
  continue;
}
```

Because this is chained in the same postfix loop, nested forms like `o.i.x` require no special parser function.

## 6. Struct metadata model inside the compiler

Chapter 19 introduces internal metadata structures:

```cpp
struct StructFieldDecl {
  std::string Name;
  TypeExprPtr Ty;
};

struct StructDeclInfo {
  std::string Name;
  std::vector<StructFieldDecl> Fields;
  std::map<std::string, unsigned> FieldIndex;
  StructType *LLTy = nullptr;
};

static std::map<std::string, StructDeclInfo> StructDecls;
static std::map<const StructType *, std::string> StructTypeNames;
```

Why both maps?

- `StructDecls`: source-name -> declaration/type data
- `StructTypeNames`: LLVM struct pointer -> source-name

That reverse map is useful when resolving field info from LLVM type handles during codegen.

## 7. LLVM type resolution for structs

Chapter 19 extends type resolution so alias/base names can resolve to struct types.

A key helper is:

```cpp
static StructType *ResolveStructTypeByName(const std::string &Name,
                                           std::set<std::string> &Visited);
```

It:

1. finds source declaration
2. creates/gets LLVM `StructType`
3. resolves each field LLVM type in declaration order
4. sets the LLVM struct body
5. caches results to avoid rebuilding

This enables nested struct fields and struct aliases to work consistently.

## 8. Member access codegen

### Address path (codegenAddress)

`MemberExprAST::codegenAddress()` does:

1. obtain base address (`Base->codegenAddress()`)
2. verify base value type is struct
3. resolve field index by name
4. emit `CreateStructGEP`

```cpp
return Builder->CreateStructGEP(ST, BaseAddr, FieldIdx, "field.addr");
```

### Value path (codegen)

`MemberExprAST::codegen()` uses the computed address and loads:

```cpp
return Builder->CreateLoad(FieldTy, AddrV, "field.load");
```

This keeps field assignment integrated with existing `AssignStmtAST` behavior because assignment already expects lvalue expressions to provide addresses.

## 9. Diagnostics added in Chapter 19

Examples of new diagnostics:

- `Struct '<name>' is already defined`
- `Duplicate field '<field>' in struct '<name>'`
- `Unknown field '<field>' on struct '<name>'`
- `Member access requires a struct-typed base`

These errors are important for tutorial pace because they make failure modes obvious during incremental development.

## 10. Tests added in Chapter 19

Chapter 19 preserves Chapter 18 tests and adds struct-focused tests:

- `code/chapter19/test/struct_basic_fields.pyxc`
- `code/chapter19/test/struct_nested_fields.pyxc`
- `code/chapter19/test/struct_alias_type.pyxc`
- `code/chapter19/test/struct_error_unknown_field.pyxc`
- `code/chapter19/test/struct_error_nonstruct_member.pyxc`
- `code/chapter19/test/struct_error_duplicate_field.pyxc`
- `code/chapter19/test/struct_error_duplicate_struct.pyxc`

Representative positive test:

```py
struct Point:
    x: i32
    y: i32

def main() -> i32:
    p: Point
    p.x = 10
    p.y = 20
    print(p.x, p.y)
    return 0

main()
```

Representative negative test:

```py
struct Point:
    x: i32

def main() -> i32:
    p: Point
    print(p.z)
    return 0

main()
```

This validates that declared fields work and undeclared fields fail cleanly.

## 11. Build and test for this chapter

From repository root:

```bash
cd code/chapter19
make
lit -sv test
```

Chapter 19 was also checked against Chapter 18 behavior as a regression sanity step.

## 12. Design takeaways

What Chapter 19 establishes:

- structured data is now a first-class part of the language
- postfix expression machinery is now rich enough for chained data access
- type resolution now handles named aggregate types

This chapter is a foundational dependency for the next two:

- Chapter 20 uses structs + indexing together (`array[Point, N]`)
- Chapter 21 allocates structs on heap (`ptr[Point] = malloc[Point](...)`)

In short: Chapter 19 is where the language stops being scalar-centric.

## Compiling

From repository root:

```bash
make -C code/chapter19 clean all
```

## Testing

From repository root:

```bash
lit -sv code/chapter19/test
```

## Compile / Run / Test (Hands-on)

Build this chapter:

```bash
make -C code/chapter19 clean all
```

Run one sample program:

```bash
code/chapter19/pyxc -i code/chapter19/test/break_outside_loop.pyxc
```

Run the chapter tests (when a test suite exists):

```bash
cd code/chapter19/test
lit -sv .
```

Explore the test folder a bit and add one tiny edge case of your own.

When you're done, clean artifacts:

```bash
make -C code/chapter19 clean
```

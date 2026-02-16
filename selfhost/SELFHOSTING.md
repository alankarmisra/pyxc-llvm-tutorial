# Pyxc Self-Hosting Plan

**Goal:** Rewrite the pyxc compiler in pyxc itself, using C bridge functions to access LLVM functionality.

## Table of Contents
1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Pyxc Feature Assessment](#pyxc-feature-assessment)
4. [Required C Extensions](#required-c-extensions)
5. [Implementation Phases](#implementation-phases)
6. [Key Challenges & Solutions](#key-challenges--solutions)
7. [Bootstrap Process](#bootstrap-process)
8. [Progress Tracking](#progress-tracking)

---

## Overview

Pyxc (chapter 27) has sufficient features to implement a self-hosted compiler:
- Structs, pointers, arrays
- Control flow (if/elif/else, while, for, break, continue)
- Functions with extern declarations
- malloc/free for memory management
- Type system with i8, i16, i32, i64, f32, f64, pointers, structs

The strategy is to create a thin C bridge layer that exposes LLVM C++ API as simple C functions callable via `extern` from pyxc.

---

## Architecture

```
┌─────────────────────────┐
│   pyxc.pyxc             │  ← Compiler written in pyxc
│   (self-hosted)         │     - Lexer
│                         │     - Parser
│                         │     - AST
│                         │     - Codegen
└───────────┬─────────────┘
            │ extern calls
            ↓
┌─────────────────────────┐
│   llvm_bridge.c         │  ← Thin C wrapper layer
│   string_utils.c        │     Hand-written once
│   runtime.c (existing)  │
└───────────┬─────────────┘
            │ C++ API calls
            ↓
┌─────────────────────────┐
│   LLVM Libraries        │
│   (C++ API)             │
└─────────────────────────┘
```

---

## Pyxc Feature Assessment

### ✅ Available Features (Chapter 27)

**Types:**
- Primitives: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64
- Pointers: `T*`, address-of with `addr(x)`
- Arrays: `array[T, N]` with indexing
- Structs: member access `.`, arrow `->` for pointers
- Type aliases: `type Name = ...`

**Control Flow:**
- if/elif/else
- while, do-while
- for/in/range
- break, continue
- match/case

**Functions:**
- def with return types
- extern declarations
- return statements

**Memory:**
- malloc, free
- Manual memory management

**I/O:**
- print (via runtime.c)
- Can extern printf, scanf, etc.

**Operators:**
- Arithmetic: +, -, *, /, %
- Comparison: <, >, <=, >=, ==, !=
- Logical: and, or, not

**String Support:**
- String literals
- i8* type for C strings

### ❌ Missing Features (Need Workarounds)

- No dynamic arrays → Implement growable arrays manually
- No hash tables → Use linked lists or implement our own
- No inheritance/polymorphism → Use tagged unions
- No garbage collection → Manual malloc/free everywhere
- No exceptions → Return codes for error handling
- Limited string manipulation → Need C bridge functions

---

## Required C Extensions

### Category 1: String Utilities (string_utils.c)

```c
// Essential string operations for compiler
extern def strlen(s: i8*) -> i64
extern def strcmp(a: i8*, b: i8*) -> i32
extern def strcpy(dest: i8*, src: i8*) -> i8*
extern def strcat(dest: i8*, src: i8*) -> i8*
extern def strdup(s: i8*) -> i8*
extern def strchr(s: i8*, ch: i32) -> i8*
extern def strncpy(dest: i8*, src: i8*, n: i64) -> i8*
```

### Category 2: File I/O (file_utils.c)

```c
// File operations for reading source files
extern def fopen(path: i8*, mode: i8*) -> i8*
extern def fclose(file: i8*) -> i32
extern def fgetc(file: i8*) -> i32
extern def ungetc(c: i32, file: i8*) -> i32
extern def fread(ptr: i8*, size: i64, count: i64, file: i8*) -> i64
extern def feof(file: i8*) -> i32
```

### Category 3: Character Classification (Already in C stdlib)

```c
extern def isalpha(ch: i32) -> i32
extern def isdigit(ch: i32) -> i32
extern def isalnum(ch: i32) -> i32
extern def isspace(ch: i32) -> i32
extern def toupper(ch: i32) -> i32
extern def tolower(ch: i32) -> i32
```

### Category 4: LLVM Bridge (llvm_bridge.c)

**Core LLVM Operations:**

```c
// Context and module management
extern def llvm_create_context() -> i8*
extern def llvm_create_module(ctx: i8*, name: i8*) -> i8*
extern def llvm_create_builder(ctx: i8*) -> i8*
extern def llvm_dispose_context(ctx: i8*) -> void
extern def llvm_dispose_module(mod: i8*) -> void

// Type creation
extern def llvm_i1_type(ctx: i8*) -> i8*
extern def llvm_i8_type(ctx: i8*) -> i8*
extern def llvm_i16_type(ctx: i8*) -> i8*
extern def llvm_i32_type(ctx: i8*) -> i8*
extern def llvm_i64_type(ctx: i8*) -> i8*
extern def llvm_f32_type(ctx: i8*) -> i8*
extern def llvm_f64_type(ctx: i8*) -> i8*
extern def llvm_ptr_type(ctx: i8*) -> i8*
extern def llvm_void_type(ctx: i8*) -> i8*
extern def llvm_array_type(elem: i8*, count: i64) -> i8*
extern def llvm_struct_type(ctx: i8*, fields: i8**, num: i32, packed: i32, name: i8*) -> i8*
extern def llvm_function_type(ret: i8*, params: i8**, nparams: i32, vararg: i32) -> i8*

// Constants
extern def llvm_const_i32(ctx: i8*, val: i32) -> i8*
extern def llvm_const_i64(ctx: i8*, val: i64) -> i8*
extern def llvm_const_f64(ctx: i8*, val: f64) -> i8*
extern def llvm_const_null(ty: i8*) -> i8*

// Function creation
extern def llvm_create_function(mod: i8*, name: i8*, ftype: i8*) -> i8*
extern def llvm_get_param(func: i8*, idx: i32) -> i8*
extern def llvm_set_param_name(val: i8*, name: i8*) -> void

// Basic block creation
extern def llvm_create_block(ctx: i8*, name: i8*) -> i8*
extern def llvm_append_block(func: i8*, block: i8*) -> void
extern def llvm_position_at_end(builder: i8*, block: i8*) -> void
extern def llvm_get_insert_block(builder: i8*) -> i8*

// IR building - Arithmetic
extern def llvm_build_add(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_sub(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_mul(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_sdiv(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_udiv(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_srem(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_urem(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_fadd(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_fsub(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_fmul(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_fdiv(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*

// IR building - Comparisons
extern def llvm_build_icmp_eq(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_icmp_ne(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_icmp_slt(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_icmp_sle(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_icmp_sgt(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_icmp_sge(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_fcmp_olt(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_fcmp_ole(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_fcmp_ogt(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*
extern def llvm_build_fcmp_oge(builder: i8*, lhs: i8*, rhs: i8*, name: i8*) -> i8*

// IR building - Memory
extern def llvm_build_alloca(builder: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_build_load(builder: i8*, ptr: i8*, name: i8*) -> i8*
extern def llvm_build_store(builder: i8*, val: i8*, ptr: i8*) -> i8*
extern def llvm_build_gep(builder: i8*, ptr: i8*, indices: i8**, nindices: i32, name: i8*) -> i8*

// IR building - Control flow
extern def llvm_build_br(builder: i8*, dest: i8*) -> i8*
extern def llvm_build_cond_br(builder: i8*, cond: i8*, then_bb: i8*, else_bb: i8*) -> i8*
extern def llvm_build_ret(builder: i8*, val: i8*) -> i8*
extern def llvm_build_ret_void(builder: i8*) -> i8*
extern def llvm_build_phi(builder: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_add_phi_incoming(phi: i8*, val: i8*, block: i8*) -> void

// IR building - Function calls
extern def llvm_build_call(builder: i8*, func: i8*, args: i8**, nargs: i32, name: i8*) -> i8*

// IR building - Casts
extern def llvm_build_bitcast(builder: i8*, val: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_build_zext(builder: i8*, val: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_build_sext(builder: i8*, val: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_build_trunc(builder: i8*, val: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_build_fpext(builder: i8*, val: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_build_fptrunc(builder: i8*, val: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_build_sitofp(builder: i8*, val: i8*, ty: i8*, name: i8*) -> i8*
extern def llvm_build_fptosi(builder: i8*, val: i8*, ty: i8*, name: i8*) -> i8*

// Symbol table / lookup
extern def llvm_get_named_function(mod: i8*, name: i8*) -> i8*
extern def llvm_get_named_global(mod: i8*, name: i8*) -> i8*

// Code generation
extern def llvm_verify_module(mod: i8*) -> i32
extern def llvm_compile_to_object(mod: i8*, filename: i8*) -> i32
extern def llvm_dump_module(mod: i8*) -> void
extern def llvm_print_to_string(val: i8*) -> i8*
```

---

## Implementation Phases

### Phase 0: Foundation (Week 1) ✅ NEXT

**Deliverable:** Working C bridge that can compile a trivial function

**Tasks:**
1. Create `selfhost/llvm_bridge.c` with ~30 core LLVM functions
2. Create `selfhost/string_utils.c` with essential string operations
3. Create `selfhost/file_utils.c` with file I/O helpers
4. Write `selfhost/test_bridge.c` - C program that tests the bridge
5. Compile and verify bridge works

**Test Program:**
```c
// test_bridge.c
#include "llvm_bridge.h"

int main() {
    void* ctx = llvm_create_context();
    void* mod = llvm_create_module(ctx, "test");
    void* builder = llvm_create_builder(ctx);

    // Create function: i32 add(i32 %a, i32 %b)
    void* i32_ty = llvm_i32_type(ctx);
    void* param_types[2] = {i32_ty, i32_ty};
    void* fn_ty = llvm_function_type(i32_ty, param_types, 2, 0);
    void* fn = llvm_create_function(mod, "add", fn_ty);

    void* entry = llvm_create_block(ctx, "entry");
    llvm_append_block(fn, entry);
    llvm_position_at_end(builder, entry);

    void* a = llvm_get_param(fn, 0);
    void* b = llvm_get_param(fn, 1);
    void* sum = llvm_build_add(builder, a, b, "sum");
    llvm_build_ret(builder, sum);

    llvm_dump_module(mod);
    llvm_compile_to_object(mod, "test.o");

    printf("Bridge test successful!\n");
    return 0;
}
```

**Success Criteria:**
- Bridge compiles without errors
- test_bridge generates valid LLVM IR
- Produces test.o object file

---

### Phase 1: Lexer in Pyxc (Week 2-3)

**Deliverable:** `selfhost/lexer.pyxc` that tokenizes pyxc source

**Data Structures:**
```python
struct Token:
    type: i32          # tok_identifier, tok_number, etc.
    line: i32
    col: i32
    str_val: i8*       # for identifiers/strings (allocated)
    num_val: f64       # for numbers
    int_val: i64       # for integer literals

struct Lexer:
    source: i8*        # Source code string
    pos: i64           # Current position
    line: i32          # Current line
    col: i32           # Current column
    current_char: i8   # Current character
```

**Core Functions:**
```python
def create_lexer(source: i8*) -> Lexer*
def advance(lex: Lexer*) -> void
def peek(lex: Lexer*, offset: i32) -> i8
def lex_identifier(lex: Lexer*) -> Token*
def lex_number(lex: Lexer*) -> Token*
def lex_string(lex: Lexer*) -> Token*
def lex_operator(lex: Lexer*) -> Token*
def get_next_token(lex: Lexer*) -> Token*
def free_token(tok: Token*) -> void
def free_lexer(lex: Lexer*) -> void
```

**Test:**
```python
def main() -> i32:
    source: i8* = "def add(x: i32, y: i32) -> i32: return x + y"
    lex: Lexer* = create_lexer(source)

    tok: Token* = get_next_token(lex)
    while tok->type != tok_eof:
        printf("Token type=%d line=%d col=%d\n", tok->type, tok->line, tok->col)
        if tok->str_val != null:
            printf("  value=%s\n", tok->str_val)
        tok = get_next_token(lex)

    return 0
```

**Success Criteria:**
- Correctly tokenizes keywords (def, return, if, etc.)
- Handles identifiers and numbers
- Tracks line/column positions
- Memory is properly freed

---

### Phase 2: AST Structures (Week 4)

**Deliverable:** Complete AST node definitions in pyxc

**Challenge:** No inheritance in pyxc → Use tagged unions

**Base Design:**
```python
# ast.pyxc

# AST node types (enum-like constants)
const AST_NUMBER: i32 = 1
const AST_STRING: i32 = 2
const AST_VARIABLE: i32 = 3
const AST_BINARY: i32 = 4
const AST_UNARY: i32 = 5
const AST_CALL: i32 = 6
const AST_INDEX: i32 = 7
const AST_MEMBER: i32 = 8
const AST_ADDR: i32 = 9

# Expression nodes
struct NumberExprAST:
    node_type: i32     # AST_NUMBER
    value: f64
    is_int: i32        # boolean
    int_value: i64

struct StringExprAST:
    node_type: i32     # AST_STRING
    value: i8*

struct VariableExprAST:
    node_type: i32     # AST_VARIABLE
    name: i8*

struct BinaryExprAST:
    node_type: i32     # AST_BINARY
    op: i8             # operator char
    lhs: i8*           # Cast to appropriate AST type
    rhs: i8*

struct UnaryExprAST:
    node_type: i32     # AST_UNARY
    op: i8
    operand: i8*

struct CallExprAST:
    node_type: i32     # AST_CALL
    callee: i8*
    args: i8**         # Array of expression pointers
    num_args: i32

struct IndexExprAST:
    node_type: i32     # AST_INDEX
    array: i8*
    index: i8*

struct MemberExprAST:
    node_type: i32     # AST_MEMBER
    object: i8*
    member: i8*
    is_arrow: i32      # -> vs .

struct AddrExprAST:
    node_type: i32     # AST_ADDR
    operand: i8*

# Statement nodes
struct ReturnStmtAST:
    node_type: i32     # AST_RETURN
    value: i8*         # Can be null

struct IfStmtAST:
    node_type: i32     # AST_IF
    cond: i8*
    then_body: i8**    # Array of statements
    then_count: i32
    else_body: i8**
    else_count: i32

struct WhileStmtAST:
    node_type: i32     # AST_WHILE
    cond: i8*
    body: i8**
    body_count: i32

# Function nodes
struct PrototypeAST:
    name: i8*
    params: i8**       # Array of parameter names
    param_types: i8**  # Array of type names
    num_params: i32
    return_type: i8*

struct FunctionAST:
    proto: PrototypeAST*
    body: i8**         # Array of statements
    body_count: i32
```

**Factory Functions:**
```python
def create_number_expr(val: f64, is_int: i32, int_val: i64) -> NumberExprAST*:
    node: NumberExprAST* = malloc(sizeof(NumberExprAST))
    node->node_type = AST_NUMBER
    node->value = val
    node->is_int = is_int
    node->int_value = int_val
    return node

def create_variable_expr(name: i8*) -> VariableExprAST*:
    node: VariableExprAST* = malloc(sizeof(VariableExprAST))
    node->node_type = AST_VARIABLE
    node->name = strdup(name)
    return node

def create_binary_expr(op: i8, lhs: i8*, rhs: i8*) -> BinaryExprAST*:
    node: BinaryExprAST* = malloc(sizeof(BinaryExprAST))
    node->node_type = AST_BINARY
    node->op = op
    node->lhs = lhs
    node->rhs = rhs
    return node

# ... factory for each node type
```

**Memory Management:**
```python
def free_expr(expr: i8*) -> void:
    if expr == null:
        return

    base: i32* = expr
    type: i32 = *base

    if type == AST_NUMBER:
        free(expr)
    elif type == AST_STRING:
        node: StringExprAST* = expr
        free(node->value)
        free(expr)
    elif type == AST_VARIABLE:
        node: VariableExprAST* = expr
        free(node->name)
        free(expr)
    elif type == AST_BINARY:
        node: BinaryExprAST* = expr
        free_expr(node->lhs)
        free_expr(node->rhs)
        free(expr)
    # ... handle all types
```

**Success Criteria:**
- All AST node types defined
- Factory functions create nodes correctly
- Memory management functions work
- Can build and traverse simple ASTs

---

### Phase 3: Parser in Pyxc (Week 5-6)

**Deliverable:** `selfhost/parser.pyxc` that builds AST from tokens

**Data Structures:**
```python
struct Parser:
    lexer: Lexer*
    current_tok: Token*
    had_error: i32

struct OperatorInfo:
    op: i8
    precedence: i32
```

**Core Functions:**
```python
def create_parser(lex: Lexer*) -> Parser*:
    p: Parser* = malloc(sizeof(Parser))
    p->lexer = lex
    p->current_tok = get_next_token(lex)
    p->had_error = 0
    return p

def advance_parser(p: Parser*) -> void:
    if p->current_tok != null:
        free_token(p->current_tok)
    p->current_tok = get_next_token(p->lexer)

def expect(p: Parser*, type: i32) -> i32:
    if p->current_tok->type == type:
        return 1
    printf("Parse error: expected token type %d, got %d\n", type, p->current_tok->type)
    p->had_error = 1
    return 0

def parse_number_expr(p: Parser*) -> i8*:
    tok: Token* = p->current_tok
    node: i8* = create_number_expr(tok->num_val, 0, 0)
    advance_parser(p)
    return node

def parse_identifier_expr(p: Parser*) -> i8*:
    name: i8* = strdup(p->current_tok->str_val)
    advance_parser(p)

    # Check if it's a function call
    if p->current_tok->type == tok_lparen:
        advance_parser(p)  # eat '('

        # Parse arguments
        args: i8** = malloc(sizeof(i8*) * 16)  # Max 16 args
        count: i32 = 0

        while p->current_tok->type != tok_rparen:
            args[count] = parse_expression(p)
            count = count + 1

            if p->current_tok->type == tok_comma:
                advance_parser(p)

        advance_parser(p)  # eat ')'
        return create_call_expr(name, args, count)

    # Just a variable
    return create_variable_expr(name)

def parse_primary(p: Parser*) -> i8*:
    if p->current_tok->type == tok_number:
        return parse_number_expr(p)
    if p->current_tok->type == tok_identifier:
        return parse_identifier_expr(p)
    if p->current_tok->type == tok_lparen:
        return parse_paren_expr(p)
    if p->current_tok->type == tok_string:
        return parse_string_expr(p)

    printf("Parse error: unexpected token\n")
    p->had_error = 1
    return null

def get_precedence(op: i8) -> i32:
    # Operator precedence table
    if op == 42:  # '*'
        return 40
    if op == 47:  # '/'
        return 40
    if op == 43:  # '+'
        return 20
    if op == 45:  # '-'
        return 20
    if op == 60:  # '<'
        return 10
    if op == 62:  # '>'
        return 10
    return -1

def parse_bin_op_rhs(p: Parser*, min_prec: i32, lhs: i8*) -> i8*:
    while 1:
        tok_prec: i32 = get_precedence(p->current_tok->type)

        if tok_prec < min_prec:
            return lhs

        op: i8 = p->current_tok->type
        advance_parser(p)

        rhs: i8* = parse_primary(p)
        if rhs == null:
            return null

        next_prec: i32 = get_precedence(p->current_tok->type)
        if tok_prec < next_prec:
            rhs = parse_bin_op_rhs(p, tok_prec + 1, rhs)
            if rhs == null:
                return null

        lhs = create_binary_expr(op, lhs, rhs)

def parse_expression(p: Parser*) -> i8*:
    lhs: i8* = parse_primary(p)
    if lhs == null:
        return null
    return parse_bin_op_rhs(p, 0, lhs)

def parse_prototype(p: Parser*) -> PrototypeAST*:
    # Parse: def name(param1: type1, param2: type2) -> return_type
    # ... implementation ...

def parse_function(p: Parser*) -> FunctionAST*:
    if p->current_tok->type != tok_def:
        return null

    advance_parser(p)  # eat 'def'

    proto: PrototypeAST* = parse_prototype(p)
    if proto == null:
        return null

    # Expect ':'
    if not expect(p, tok_colon):
        return null
    advance_parser(p)

    # Parse body statements
    body: i8** = malloc(sizeof(i8*) * 100)  # Max 100 statements
    count: i32 = 0

    while p->current_tok->type != tok_eof:
        stmt: i8* = parse_statement(p)
        if stmt == null:
            break
        body[count] = stmt
        count = count + 1

    return create_function(proto, body, count)
```

**Success Criteria:**
- Can parse expressions with correct precedence
- Can parse function definitions
- Handles errors gracefully
- Builds correct AST

---

### Phase 4: Code Generation (Week 7-8)

**Deliverable:** `selfhost/codegen.pyxc` that emits LLVM IR

**Data Structures:**
```python
struct CodeGen:
    ctx: i8*              # LLVMContext*
    module: i8*           # Module*
    builder: i8*          # IRBuilder*
    named_values: i8*     # Symbol table (linked list)

struct SymbolEntry:
    name: i8*
    value: i8*            # LLVM Value*
    next: SymbolEntry*
```

**Core Functions:**
```python
def create_codegen(module_name: i8*) -> CodeGen*:
    cg: CodeGen* = malloc(sizeof(CodeGen))
    cg->ctx = llvm_create_context()
    cg->module = llvm_create_module(cg->ctx, module_name)
    cg->builder = llvm_create_builder(cg->ctx)
    cg->named_values = null
    return cg

def symbol_insert(cg: CodeGen*, name: i8*, value: i8*) -> void:
    entry: SymbolEntry* = malloc(sizeof(SymbolEntry))
    entry->name = strdup(name)
    entry->value = value
    entry->next = cg->named_values
    cg->named_values = entry

def symbol_lookup(cg: CodeGen*, name: i8*) -> i8*:
    curr: SymbolEntry* = cg->named_values
    while curr != null:
        if strcmp(curr->name, name) == 0:
            return curr->value
        curr = curr->next
    return null

def codegen_number(cg: CodeGen*, node: NumberExprAST*) -> i8*:
    if node->is_int:
        return llvm_const_i64(cg->ctx, node->int_value)
    else:
        return llvm_const_f64(cg->ctx, node->value)

def codegen_variable(cg: CodeGen*, node: VariableExprAST*) -> i8*:
    val: i8* = symbol_lookup(cg, node->name)
    if val == null:
        printf("Error: unknown variable %s\n", node->name)
        return null
    return llvm_build_load(cg->builder, val, node->name)

def codegen_binary(cg: CodeGen*, node: BinaryExprAST*) -> i8*:
    lhs: i8* = codegen_expr(cg, node->lhs)
    rhs: i8* = codegen_expr(cg, node->rhs)

    if lhs == null or rhs == null:
        return null

    if node->op == 43:  # '+'
        return llvm_build_add(cg->builder, lhs, rhs, "addtmp")
    if node->op == 45:  # '-'
        return llvm_build_sub(cg->builder, lhs, rhs, "subtmp")
    if node->op == 42:  # '*'
        return llvm_build_mul(cg->builder, lhs, rhs, "multmp")
    if node->op == 47:  # '/'
        return llvm_build_sdiv(cg->builder, lhs, rhs, "divtmp")

    printf("Error: unknown binary operator\n")
    return null

def codegen_call(cg: CodeGen*, node: CallExprAST*) -> i8*:
    callee: i8* = llvm_get_named_function(cg->module, node->callee)
    if callee == null:
        printf("Error: unknown function %s\n", node->callee)
        return null

    args: i8** = malloc(sizeof(i8*) * node->num_args)
    i: i32 = 0
    while i < node->num_args:
        args[i] = codegen_expr(cg, node->args[i])
        if args[i] == null:
            free(args)
            return null
        i = i + 1

    result: i8* = llvm_build_call(cg->builder, callee, args, node->num_args, "calltmp")
    free(args)
    return result

def codegen_expr(cg: CodeGen*, expr: i8*) -> i8*:
    if expr == null:
        return null

    base: i32* = expr
    type: i32 = *base

    if type == AST_NUMBER:
        return codegen_number(cg, expr)
    if type == AST_VARIABLE:
        return codegen_variable(cg, expr)
    if type == AST_BINARY:
        return codegen_binary(cg, expr)
    if type == AST_CALL:
        return codegen_call(cg, expr)

    printf("Error: unknown expression type %d\n", type)
    return null

def codegen_prototype(cg: CodeGen*, proto: PrototypeAST*) -> i8*:
    # Create function type
    i32_ty: i8* = llvm_i32_type(cg->ctx)

    # For now, assume all params are i32
    param_types: i8** = malloc(sizeof(i8*) * proto->num_params)
    i: i32 = 0
    while i < proto->num_params:
        param_types[i] = i32_ty
        i = i + 1

    fn_ty: i8* = llvm_function_type(i32_ty, param_types, proto->num_params, 0)
    fn: i8* = llvm_create_function(cg->module, proto->name, fn_ty)

    free(param_types)
    return fn

def codegen_function(cg: CodeGen*, func: FunctionAST*) -> i8*:
    # Create function
    fn: i8* = codegen_prototype(cg, func->proto)
    if fn == null:
        return null

    # Create entry block
    entry: i8* = llvm_create_block(cg->ctx, "entry")
    llvm_append_block(fn, entry)
    llvm_position_at_end(cg->builder, entry)

    # Register function parameters in symbol table
    i: i32 = 0
    while i < func->proto->num_params:
        param: i8* = llvm_get_param(fn, i)
        llvm_set_param_name(param, func->proto->params[i])

        # Allocate stack space and store param
        alloca: i8* = llvm_build_alloca(cg->builder, llvm_i32_type(cg->ctx), func->proto->params[i])
        llvm_build_store(cg->builder, param, alloca)
        symbol_insert(cg, func->proto->params[i], alloca)

        i = i + 1

    # Codegen body
    i = 0
    while i < func->body_count:
        codegen_stmt(cg, func->body[i])
        i = i + 1

    # Verify function
    if llvm_verify_function(fn):
        return fn

    printf("Error: function verification failed\n")
    return null
```

**Success Criteria:**
- Can generate LLVM IR for expressions
- Can generate functions with parameters
- Symbol table works correctly
- Generated IR is valid

---

### Phase 5: Driver & Integration (Week 9)

**Deliverable:** Complete `selfhost/pyxc.pyxc`

```python
# pyxc.pyxc - The self-hosted compiler!

def read_file(filename: i8*) -> i8*:
    file: i8* = fopen(filename, "r")
    if file == null:
        return null

    # Read entire file into memory
    # (implement buffered reading)
    # ...

    fclose(file)
    return buffer

def main(argc: i32, argv: i8**) -> i32:
    if argc < 2:
        printf("Usage: pyxc <file.pyxc>\n")
        return 1

    filename: i8* = argv[1]
    source: i8* = read_file(filename)

    if source == null:
        printf("Error: cannot open file %s\n", filename)
        return 1

    # Lexer
    lex: Lexer* = create_lexer(source)
    if lex == null:
        printf("Error: failed to create lexer\n")
        return 1

    # Parser
    parser: Parser* = create_parser(lex)
    if parser == null:
        printf("Error: failed to create parser\n")
        return 1

    func: FunctionAST* = parse_function(parser)
    if func == null or parser->had_error:
        printf("Parse error\n")
        return 1

    # Code generation
    cg: CodeGen* = create_codegen("pyxc_module")
    if cg == null:
        printf("Error: failed to create codegen\n")
        return 1

    llvm_fn: i8* = codegen_function(cg, func)
    if llvm_fn == null:
        printf("Codegen error\n")
        return 1

    # Verify module
    if llvm_verify_module(cg->module):
        printf("Module verification failed\n")
        return 1

    # Emit object file
    outfile: i8* = "output.o"
    if llvm_compile_to_object(cg->module, outfile):
        printf("Failed to compile to object file\n")
        return 1

    printf("Successfully compiled to %s\n", outfile)

    # Cleanup
    free_function(func)
    free_parser(parser)
    free_lexer(lex)
    free(source)

    return 0
```

**Success Criteria:**
- Can compile simple pyxc programs end-to-end
- Produces valid object files
- Handles errors gracefully
- Memory is properly managed

---

### Phase 6: Bootstrap! (Week 10)

**Goal:** Use pyxc to compile itself

```bash
# Stage 0: C++ pyxc compiles pyxc.pyxc
cd selfhost
../build/pyxc pyxc.pyxc -o pyxc-stage1

# Stage 1: pyxc-stage1 compiles pyxc.pyxc again
./pyxc-stage1 pyxc.pyxc -o pyxc-stage2

# Stage 2: Verify bit-identical
diff pyxc-stage1 pyxc-stage2  # Should be identical!

# Stage 3: Truly self-hosted!
./pyxc-stage2 pyxc.pyxc -o pyxc-stage3
diff pyxc-stage2 pyxc-stage3
```

**Success Criteria:**
- Stage 1 and Stage 2 are byte-identical
- All three stages produce identical output
- Self-hosting is achieved!

---

## Key Challenges & Solutions

### Challenge 1: No Dynamic Arrays

**Problem:** Need growable arrays for token lists, AST children, etc.

**Solution:** Implement simple growable array:

```python
struct ArrayList:
    data: i8**
    count: i32
    capacity: i32

def arraylist_create() -> ArrayList*:
    list: ArrayList* = malloc(sizeof(ArrayList))
    list->data = malloc(sizeof(i8*) * 8)
    list->count = 0
    list->capacity = 8
    return list

def arraylist_add(list: ArrayList*, item: i8*) -> void:
    if list->count == list->capacity:
        # Grow by 2x
        new_cap: i32 = list->capacity * 2
        new_data: i8** = malloc(sizeof(i8*) * new_cap)

        # Copy old data
        i: i32 = 0
        while i < list->count:
            new_data[i] = list->data[i]
            i = i + 1

        free(list->data)
        list->data = new_data
        list->capacity = new_cap

    list->data[list->count] = item
    list->count = list->count + 1

def arraylist_get(list: ArrayList*, index: i32) -> i8*:
    if index < 0 or index >= list->count:
        return null
    return list->data[index]

def arraylist_free(list: ArrayList*) -> void:
    free(list->data)
    free(list)
```

---

### Challenge 2: No Hash Tables

**Problem:** Need symbol tables for variable lookup

**Solution 1:** Simple linked list (slow but works)

```python
struct SymbolEntry:
    name: i8*
    value: i8*
    next: SymbolEntry*

def symbol_lookup(head: SymbolEntry*, name: i8*) -> i8*:
    curr: SymbolEntry* = head
    while curr != null:
        if strcmp(curr->name, name) == 0:
            return curr->value
        curr = curr->next
    return null
```

**Solution 2:** Simple hash table with chaining

```python
const HASH_TABLE_SIZE: i32 = 256

struct HashTable:
    buckets: SymbolEntry**  # Array of linked lists
    size: i32

def hash(name: i8*) -> i32:
    h: i32 = 0
    i: i32 = 0
    while name[i] != 0:
        h = (h * 31 + name[i]) % HASH_TABLE_SIZE
        i = i + 1
    return h

def hashtable_create() -> HashTable*:
    table: HashTable* = malloc(sizeof(HashTable))
    table->buckets = malloc(sizeof(SymbolEntry*) * HASH_TABLE_SIZE)
    table->size = HASH_TABLE_SIZE

    i: i32 = 0
    while i < HASH_TABLE_SIZE:
        table->buckets[i] = null
        i = i + 1

    return table

def hashtable_insert(table: HashTable*, name: i8*, value: i8*) -> void:
    idx: i32 = hash(name)
    entry: SymbolEntry* = malloc(sizeof(SymbolEntry))
    entry->name = strdup(name)
    entry->value = value
    entry->next = table->buckets[idx]
    table->buckets[idx] = entry

def hashtable_lookup(table: HashTable*, name: i8*) -> i8*:
    idx: i32 = hash(name)
    return symbol_lookup(table->buckets[idx], name)
```

---

### Challenge 3: No Polymorphism

**Problem:** Need different AST node types with common operations

**Solution:** Tagged unions with type discriminator

```python
# All AST nodes start with node_type field
struct ExprAST:
    node_type: i32

# Cast and dispatch based on type
def codegen_expr(cg: CodeGen*, expr: i8*) -> i8*:
    base: i32* = expr
    type: i32 = *base

    if type == AST_NUMBER:
        return codegen_number(cg, expr)
    if type == AST_BINARY:
        return codegen_binary(cg, expr)
    # ... etc
```

---

### Challenge 4: Error Handling

**Problem:** No exceptions, need to propagate errors

**Solution:** Return null and check everywhere

```python
var HadError: i32 = 0

def log_error(msg: i8*, line: i32, col: i32) -> void:
    printf("Error (line %d, col %d): %s\n", line, col, msg)
    HadError = 1

def parse_expression(p: Parser*) -> i8*:
    lhs: i8* = parse_primary(p)
    if lhs == null:
        log_error("expected expression", p->current_tok->line, p->current_tok->col)
        return null
    return parse_bin_op_rhs(p, 0, lhs)
```

---

### Challenge 5: Memory Management

**Problem:** Manual malloc/free everywhere

**Solution:** Consistent patterns + helper functions

```python
# Always pair allocate/free
def create_X() -> X*:
    x: X* = malloc(sizeof(X))
    # ... initialize ...
    return x

def free_X(x: X*) -> void:
    # Free owned pointers
    # Then free self
    free(x)

# Use arena allocator for AST nodes
struct Arena:
    blocks: i8**
    block_count: i32
    current_offset: i64

def arena_create() -> Arena*:
    # ... create arena ...

def arena_alloc(arena: Arena*, size: i64) -> i8*:
    # ... allocate from arena ...

def arena_free_all(arena: Arena*) -> void:
    # Free all blocks at once
    i: i32 = 0
    while i < arena->block_count:
        free(arena->blocks[i])
        i = i + 1
```

---

## Bootstrap Process

### Stage 0: Initial Compilation
```bash
# C++ compiler compiles pyxc.pyxc
./pyxc-cpp selfhost/pyxc.pyxc -o selfhost/pyxc-stage1
```

**What happens:**
- C++ pyxc reads pyxc.pyxc
- Generates LLVM IR for self-hosted compiler
- Links with llvm_bridge.o, string_utils.o, runtime.o
- Produces executable: pyxc-stage1

### Stage 1: First Self-Compilation
```bash
# Stage 1 compiler compiles itself
./selfhost/pyxc-stage1 selfhost/pyxc.pyxc -o selfhost/pyxc-stage2
```

**What happens:**
- pyxc-stage1 (compiled by C++) reads pyxc.pyxc
- Should generate identical LLVM IR
- Produces: pyxc-stage2

### Stage 2: Verification
```bash
# Compare binaries
diff selfhost/pyxc-stage1 selfhost/pyxc-stage2
```

**Expected:** Binaries should be identical (or very similar with timestamp differences)

### Stage 3: True Bootstrap
```bash
# Stage 2 compiles itself
./selfhost/pyxc-stage2 selfhost/pyxc.pyxc -o selfhost/pyxc-stage3
diff selfhost/pyxc-stage2 selfhost/pyxc-stage3
```

**Success:** If stage2 == stage3, we've achieved true self-hosting!

---

## Progress Tracking

### Phase 0: Foundation ⏳ IN PROGRESS
- [ ] Create llvm_bridge.h
- [ ] Implement llvm_bridge.c
- [ ] Create string_utils.c
- [ ] Create file_utils.c
- [ ] Write test_bridge.c
- [ ] Compile and verify

### Phase 1: Lexer ⏸️ NOT STARTED
- [ ] Define Token struct
- [ ] Define Lexer struct
- [ ] Implement advance/peek
- [ ] Implement lex_identifier
- [ ] Implement lex_number
- [ ] Implement lex_string
- [ ] Implement get_next_token
- [ ] Write test program
- [ ] Verify tokenization

### Phase 2: AST ⏸️ NOT STARTED
- [ ] Define AST node types (constants)
- [ ] Define expression structs
- [ ] Define statement structs
- [ ] Implement factory functions
- [ ] Implement free functions
- [ ] Write test program
- [ ] Verify AST construction

### Phase 3: Parser ⏸️ NOT STARTED
- [ ] Define Parser struct
- [ ] Implement parse_primary
- [ ] Implement parse_expression
- [ ] Implement parse_bin_op_rhs
- [ ] Implement parse_prototype
- [ ] Implement parse_function
- [ ] Implement parse_statement
- [ ] Write test program
- [ ] Verify parsing

### Phase 4: Codegen ⏸️ NOT STARTED
- [ ] Define CodeGen struct
- [ ] Implement symbol table
- [ ] Implement codegen_number
- [ ] Implement codegen_variable
- [ ] Implement codegen_binary
- [ ] Implement codegen_call
- [ ] Implement codegen_expr
- [ ] Implement codegen_prototype
- [ ] Implement codegen_function
- [ ] Write test program
- [ ] Verify IR generation

### Phase 5: Integration ⏸️ NOT STARTED
- [ ] Implement read_file
- [ ] Implement main driver
- [ ] Add error handling
- [ ] Add memory cleanup
- [ ] Write end-to-end tests
- [ ] Verify complete pipeline

### Phase 6: Bootstrap ⏸️ NOT STARTED
- [ ] Compile pyxc.pyxc with C++ pyxc → stage1
- [ ] Compile pyxc.pyxc with stage1 → stage2
- [ ] Verify stage1 == stage2
- [ ] Compile pyxc.pyxc with stage2 → stage3
- [ ] Verify stage2 == stage3
- [ ] Celebrate! 🎉

---

## Next Steps

**Immediate:** Start Phase 0 - Foundation
1. Create llvm_bridge.h with function declarations
2. Implement core LLVM operations in llvm_bridge.c
3. Create string_utils.c and file_utils.c
4. Write test_bridge.c to verify bridge works
5. Compile and test

**Then:** Move to Phase 1 - Lexer in Pyxc

---

## Notes & Observations

- This is a **long-term project** (10-12 weeks estimated)
- Each phase builds on the previous
- Can pause/resume at phase boundaries
- Bridge layer is the key to success
- Memory management will be challenging but manageable
- No need for full language support initially - start with subset

**Why this will work:**
- Pyxc has all necessary features
- Bridge approach is proven (many compilers use C FFI)
- Phases are well-defined and testable
- Can validate each phase independently

**The payoff:**
- True self-hosting - compiler compiling itself
- Deep understanding of compilation
- Foundation for future language features
- Awesome achievement! 🚀

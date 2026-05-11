# pyxc Bootstrap Plan

The goal is a self-hosted pyxc compiler: a pyxc compiler written in pyxc. This document captures the implementation strategy.

---

## What Self-Hosting Means Here

The existing C++ compiler becomes the **bootstrap compiler** — used once to compile the self-hosted compiler, then discarded. After that, pyxc compiles itself. This is the same path Go, Rust, and most self-hosted languages took.

The self-hosted compiler does not need to replicate every feature of the C++ compiler on day one. A useful first milestone is a compiler that:

- Parses pyxc source
- Builds an AST
- Emits unoptimised LLVM IR or object files
- Links and runs

The JIT/REPL, optimisation pipeline, and debug info can follow once the basic pipeline works.

---

## Why pyxc Can Do This Now

The language already has everything a recursive-descent compiler needs:

| Feature | Status |
|---|---|
| Recursion | ✅ |
| Structs | ✅ (for tokens, AST nodes, symbol table entries) |
| Traits | ✅ (for AST node polymorphism / codegen dispatch) |
| `malloc`/`free` | ✅ (heap-allocated AST nodes) |
| `ptr[int8]` + `extern` C strings | ✅ (identifier names, error messages) |
| Character literals | ✅ (critical for the lexer) |
| `while`/`switch`/`break` | ✅ (token dispatch, parser loops) |
| `extern def` | ✅ (C string functions, LLVM wrapper library) |
| Variadic `extern` | ✅ (error formatting via `printf`) |

The only missing piece is access to LLVM — which a thin wrapper library solves.

---

## The LLVM Wrapper Library

The current C++ codegen uses LLVM through C++ RAII wrappers and the ORC JIT. All of this sits on top of function calls. The plan is to expose those calls through a small C-linkage library that pyxc can reach via `extern def`.

Three source files, each compiled to a `.o` and linked with the self-hosted binary:

### `pyxc_llvm.cpp` — Core IR construction (~40–50 functions)

Context, module, builder, types, and instruction emission.

**Context / Module / Builder:**
```
pyxc_context_create() -> ptr
pyxc_module_create(name: ptr[int8], ctx: ptr) -> ptr
pyxc_builder_create(ctx: ptr) -> ptr
pyxc_builder_position_at_end(builder: ptr, block: ptr)
```

**Types:**
```
pyxc_type_int1(ctx: ptr) -> ptr
pyxc_type_int8(ctx: ptr) -> ptr
pyxc_type_int16(ctx: ptr) -> ptr
pyxc_type_int32(ctx: ptr) -> ptr
pyxc_type_int64(ctx: ptr) -> ptr
pyxc_type_float(ctx: ptr) -> ptr
pyxc_type_double(ctx: ptr) -> ptr
pyxc_type_ptr(ctx: ptr) -> ptr
pyxc_type_void(ctx: ptr) -> ptr
pyxc_function_type(ret: ptr, params: ptr, count: int32, is_vararg: int32) -> ptr
```

**Functions and blocks:**
```
pyxc_add_function(module: ptr, name: ptr[int8], ftype: ptr) -> ptr
pyxc_append_block(ctx: ptr, fn: ptr, name: ptr[int8]) -> ptr
pyxc_get_param(fn: ptr, index: int32) -> ptr
pyxc_set_param_name(param: ptr, name: ptr[int8])
```

**Instruction emission:**
```
pyxc_build_add(b: ptr, l: ptr, r: ptr, name: ptr[int8]) -> ptr
pyxc_build_sub(b: ptr, l: ptr, r: ptr, name: ptr[int8]) -> ptr
pyxc_build_mul(b: ptr, l: ptr, r: ptr, name: ptr[int8]) -> ptr
pyxc_build_sdiv / udiv / srem / urem ...
pyxc_build_fadd / fsub / fmul / fdiv ...
pyxc_build_and / or / xor / shl / ashr / lshr ...
pyxc_build_icmp(b: ptr, pred: int32, l: ptr, r: ptr, name: ptr[int8]) -> ptr
pyxc_build_fcmp(b: ptr, pred: int32, l: ptr, r: ptr, name: ptr[int8]) -> ptr
pyxc_build_alloca(b: ptr, ty: ptr, name: ptr[int8]) -> ptr
pyxc_build_load(b: ptr, ty: ptr, ptr: ptr, name: ptr[int8]) -> ptr
pyxc_build_store(b: ptr, val: ptr, ptr: ptr)
pyxc_build_gep(b: ptr, ty: ptr, ptr: ptr, indices: ptr, count: int32, name: ptr[int8]) -> ptr
pyxc_build_call(b: ptr, ftype: ptr, fn: ptr, args: ptr, count: int32, name: ptr[int8]) -> ptr
pyxc_build_ret(b: ptr, val: ptr)
pyxc_build_ret_void(b: ptr)
pyxc_build_br(b: ptr, dest: ptr)
pyxc_build_cond_br(b: ptr, cond: ptr, then: ptr, else: ptr)
pyxc_build_phi(b: ptr, ty: ptr, name: ptr[int8]) -> ptr
pyxc_add_incoming(phi: ptr, vals: ptr, blocks: ptr, count: int32)
pyxc_build_sext / zext / trunc / bitcast / sitofp / uitofp / fptosi / fptoui ...
pyxc_build_not(b: ptr, val: ptr, name: ptr[int8]) -> ptr
pyxc_const_int(ty: ptr, val: int64, sign_extend: int32) -> ptr
pyxc_const_float(ty: ptr, val: float64) -> ptr
pyxc_const_null(ty: ptr) -> ptr
pyxc_build_global_string_ptr(b: ptr, str: ptr[int8], name: ptr[int8]) -> ptr
```

**Verification and output:**
```
pyxc_verify_module(module: ptr) -> int32
pyxc_print_module_to_file(module: ptr, path: ptr[int8]) -> int32
pyxc_print_module_to_string(module: ptr) -> ptr[int8]
```

### `pyxc_jit.cpp` — ORC JIT (~4–6 functions)

The ORC JIT internals are C++ RAII, but they sit on a pointer-based API. The wrapper owns all lifetimes; pyxc holds opaque handles.

```
pyxc_jit_create() -> ptr
pyxc_jit_add_module(jit: ptr, module: ptr, ctx: ptr)
pyxc_jit_lookup(jit: ptr, name: ptr[int8]) -> ptr
pyxc_jit_destroy(jit: ptr)
```

pyxc treats the returned `ptr` as a black box — a handle to pass back to subsequent calls, never dereferenced.

### `pyxc_target.cpp` — Target machine and emission (~6–8 functions)

```
pyxc_initialize_native_target()
pyxc_target_machine_create(triple: ptr[int8], cpu: ptr[int8], features: ptr[int8], opt: int32) -> ptr
pyxc_target_machine_emit_to_file(tm: ptr, module: ptr, path: ptr[int8], filetype: int32) -> int32
pyxc_get_host_triple() -> ptr[int8]
pyxc_target_machine_destroy(tm: ptr)
```

---

## AST Polymorphism via Traits

In C++, every AST node inherits from `ExprAST` and overrides `virtual codegen()`. In pyxc, the same pattern uses traits:

```pyxc
trait Codegen:
  def codegen(self) -> ptr   # returns an LLVM Value*

struct NumberExprAST:
  value: float64
  ty:    int32

impl Codegen for NumberExprAST:
  def codegen(self) -> ptr:
    return pyxc_const_float(pyxc_type_double(TheContext), self.value)

struct BinaryExprAST:
  op:  int32
  lhs: ptr   # ptr to heap-allocated ExprAST
  rhs: ptr
  ...
```

Dynamic dispatch through trait method calls replaces virtual dispatch. The pattern is identical in structure; only the spelling changes.

---

## String Handling

Identifier names, error messages, and token text are `ptr[int8]` — null-terminated C strings. The self-hosted compiler manages them the same way the original K&R compiler did: `malloc` a buffer, copy into it, free when done. This is already fully supported in pyxc.

Useful `extern` declarations:

```pyxc
extern def strlen(s: ptr[int8]) -> int
extern def strcmp(a: ptr[int8], b: ptr[int8]) -> int32
extern def strncmp(a: ptr[int8], b: ptr[int8], n: int) -> int32
extern def strcpy(dst: ptr[int8], src: ptr[int8]) -> ptr[int8]
extern def strncpy(dst: ptr[int8], src: ptr[int8], n: int) -> ptr[int8]
extern def memcpy(dst: ptr[int8], src: ptr[int8], n: int) -> ptr[int8]
extern def printf(fmt: ptr[int8], ...) -> int32
extern def sprintf(buf: ptr[int8], fmt: ptr[int8], ...) -> int32
```

No dynamic string type is needed. The lexer, parser, and symbol table all work with `ptr[int8]`.

---

## Bootstrap Sequence

```
Stage 0 (bootstrap compiler — C++):
  pyxc.cpp → clang++ → pyxc_bootstrap

Stage 1 (library):
  pyxc_llvm.cpp   → clang++  → pyxc_llvm.o
  pyxc_jit.cpp    → clang++  → pyxc_jit.o
  pyxc_target.cpp → clang++  → pyxc_target.o

Stage 2 (self-hosted compiler source):
  compiler/lexer.pyxc
  compiler/parser.pyxc
  compiler/ast.pyxc
  compiler/codegen.pyxc
  compiler/driver.pyxc

  pyxc_bootstrap compiler/... → pyxc_self.o

Stage 3 (link):
  pyxc_self.o + pyxc_llvm.o + pyxc_jit.o + pyxc_target.o → pyxc

Stage 4 (verification):
  pyxc compiler/... → pyxc_stage4
  diff pyxc pyxc_stage4   # identical output proves correctness
```

Stage 4 is the classic bootstrap correctness check: compile the compiler with itself twice and verify the binaries match.

---

## What to Defer

The first self-hosted binary does not need everything the C++ compiler has. Suggested order:

1. **First:** lexer + parser + AST pretty-printer (no LLVM, proves the language is expressive enough)
2. **Second:** `--emit llvm-ir` via `pyxc_llvm.cpp` (unoptimised, no JIT)
3. **Third:** `--emit obj` via `pyxc_target.cpp`
4. **Fourth:** JIT / REPL via `pyxc_jit.cpp`
5. **Later:** optimisation pipeline, debug info, incremental compilation

---

## Prerequisites Before Starting

- [ ] Function pointers (needed to store callbacks, comparators, and pass parse/codegen functions as arguments to shared helpers)
- [ ] Modules and multi-file builds (the self-hosted compiler is naturally multi-file)
- [ ] `pyxc_llvm.cpp` wrapper library written and tested against the C++ compiler
- [ ] A decision on the standard library I/O interface (so the compiler's own diagnostics don't use raw `printf`)

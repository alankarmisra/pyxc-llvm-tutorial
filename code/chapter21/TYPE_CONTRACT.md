# Chapter 16 Type Contract (C Interop First)

This chapter adds a minimal type system for C interop.
The goal is practical interop now, with safety guardrails later.

## 1) Core Goal

Make `pyxc` able to pass values and pointers to C with correct ABI shape.

What we guarantee in this chapter:
- Type correctness at compile time.
- Correct target-aware data layout decisions.
- C-like runtime behavior for pointer misuse (may segfault).

What we do not guarantee in this chapter:
- Runtime memory safety.
- Bounds checks on pointer access.
- Lifetime checks.

## 2) Core Types

Built-in fixed-width scalar types:
- `i8`, `i16`, `i32`, `i64`
- `u8`, `u16`, `u32`, `u64`
- `f32`, `f64`
- `void`

Pointer type:
- `ptr[T]`

These map directly to LLVM primitive types (or pointer-to-LLVM types).

## 3) Alias Types

`type` aliases are supported:

```py
type int    = i32
type char   = i8
type float  = f32
type double = f64
```

Important ABI rule:
- C-facing aliases like `size_t`, `int`, `long`, `char` must be target-aware.
- Do not globally hardcode `size_t = i64`.
- `size_t` should follow target pointer size and be unsigned.

For chapter scope:
- We can allow a default target profile first (for example, x86_64 macOS/Linux),
  as long as the alias mapping is intentionally target-driven.

## 4) Pointer Operations

Pointer syntax and behavior:
- Address-of: `addr(x)`
- Pointer index/deref: `p[n]`

Typing rules:
1. `addr(x): ptr[T]` if `x: T` and `x` is addressable.
2. `p[n]: T` if `p: ptr[T]` and `n` is an integer type.
3. `p[n] = v` is valid if `v: T`.
4. `ptr[void]` cannot be indexed.

Semantics:
- `p[0]` is plain dereference.
- `p[n]` is pointer arithmetic access with element stride `sizeof(T)`.
- Lowering should use LLVM GEP + load/store style operations.

## 5) Safety Policy (Chapter 16)

Pointers are unsafe in runtime behavior, like C.

Allowed outcomes:
- Null dereference can crash.
- Out-of-bounds pointer access can crash or corrupt memory.
- Dangling pointer use can crash or corrupt memory.

Compiler responsibility is type correctness and ABI correctness, not runtime safety.

## 6) C Interop Boundary (v1)

Must support:
- `extern` function declarations.
- Calls with ABI-correct argument and return lowering.
- Pointer arguments and returns using `ptr[T]`.
- `void` returns.

Can defer to later:
- Varargs.
- Function pointers.
- Full struct/union edge cases.

## 7) Non-goals In This Chapter

- Ownership/lifetimes.
- Bounds checking.
- Full C expression syntax (`*p`, `p++`, etc.).
- Full pointer arithmetic operators as separate syntax (`p + n`, `p - q`).

The language still supports equivalent memory manipulation power via `p[n]`.

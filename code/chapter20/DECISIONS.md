# Chapter 16 Locked Decisions

These are pre-implementation decisions to keep Chapter 16 focused.

## 1) Target Assumption (Initial)

- Primary target for bring-up: 64-bit host target used by local LLVM toolchain.
- Alias mapping must still be encoded as target-aware logic, not hardcoded literals.

## 2) Alias Policy

- Core fixed-width types are the source of truth.
- C-like names (`int`, `char`, `size_t`, `float`, `double`, etc.) are aliases.
- User-defined aliases are allowed through `type Name = Type`.
- Chapter 16 should reject alias cycles.

## 3) Syntax Freeze (Chapter 16)

- Pointer type: `ptr[T]`
- Address-of: `addr(x)`
- Pointer access: `p[n]`
- Alias declaration: `type Name = Type`

No extra pointer/operator syntax is required in this chapter.

## 4) Type Checking Boundary

Must be checked:
- `addr(x)` only for addressable expressions.
- `p[n]` requires `p: ptr[T]` and integer `n`.
- `ptr[void]` indexing is invalid.

Not checked in Chapter 16:
- Nullness.
- Bounds.
- Lifetime validity.

## 5) Test Strategy

- Use lit tests under `code/chapter16/test/`.
- Keep tests split across:
  - positive compile/run shape
  - negative type diagnostics
- Mark new tests `XFAIL: *` until compiler changes land.
- Remove `XFAIL` incrementally as each feature is implemented.

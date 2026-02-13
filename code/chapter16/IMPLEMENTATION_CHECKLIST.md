# Chapter 15 Implementation Checklist

This is the agreed implementation target for Chapter 15.
No compiler code is changed by this document.

## Scope

- Interop-first type system.
- Python-flavored pointer syntax:
  - `ptr[T]`
  - `addr(x)`
  - `p[n]`
- C-like runtime behavior for pointer misuse.

## Order of Work

1. Parser and AST
- Add type syntax for core scalar types, `void`, and `ptr[T]`.
- Add `type Name = Type` aliases.
- Add `addr(expr)` as address-of builtin form.
- Add `p[n]` indexing for pointer access.

2. Type system
- Add core types and pointer type constructor.
- Add alias resolution.
- Enforce pointer typing rules from `TYPE_CONTRACT.md`.
- Reject indexing through `ptr[void]`.

3. Target-aware C aliases
- Add target profile for C names (`int`, `long`, `size_t`, `char`, etc.).
- Keep mapping centralized and explicit.

4. LLVM lowering
- Lower `addr(x)` to address value.
- Lower `p[n]` to element address math (GEP) + load/store.
- Ensure `void` return and pointer param lowering work in extern calls.

5. Tests
- Keep parser/type/lowering cases in `code/chapter15/test/`.
- Flip tests from `XFAIL` to passing as each feature lands.

## Done Criteria

- Chapter 15 tests for pointer type, address-of, indexing, and basic aliases pass.
- At least one extern call with pointer interop works end to end.
- Contract and implementation match with no syntax drift.

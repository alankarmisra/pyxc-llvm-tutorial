# pyxc Reference

This file is a working scratchpad of goals, roadmap items, and external references. It is intentionally pragmatic: keep it useful, keep it current, and move stale items out.

## Purpose

Pyxc is a tutorial language meant to stay close to C/Kaleidoscope semantics so it can teach language implementation step‑by‑step, K&R‑style. The immediate goal is a clean, teachable compiler path. More ambitious features can come later.

Principle: Programmers are not here for a learning adventure. They just want to get things done.

## Direction

- Keep semantics close to Kaleidoscope where it helps explain LLVM.
- Favor incremental, teachable changes over big rewrites.
- Push toward C‑like semantics first; expand toward Python later.
- Consider MLIR only if it adds clear value.

## Reference: Python‑to‑Native Compilers and Transpilers

### True native compilers (AOT/LLVM)

- Mojo — Python‑adjacent, MLIR/LLVM based, AI/systems focus.
- LPython — LLVM backend, subset of Python, JIT and AOT.
- Numba — LLVM JIT, also supports AOT via `pycc`.

### Transpilers (Python → C/C++/Rust/etc.)

- Nuitka — Python to C++, linked against libpython.
- Cython — Python superset to C/C++ extensions.
- Shed Skin — static Python subset to C++.
- Py2Many — Python to Rust/C++/Go/Zig.

### Specialty / numerical

- Taichi — DSL for physics/graphics; compiles to CPU/GPU.
- Pythran — Python+NumPy to C++.
- Mypyc — type‑hinted Python to C extensions.

Related video: [Python Compiler Comparison](https://www.youtube.com/watch?v=QWqxRchawZY)

## Type System Notes

Recommended learning path:

- Start with “Types and Programming Languages” (Pierce). Focus on Chapters 1–9. Skim proofs at first.
- Optional: Chapter 22 for type inference.
- Parallel: LLVM type system and IR representation (`llvm::Type`, `llvm::FunctionType`).
- Practical reference: “Static Program Analysis” (Møller & Schwartzbach).

Incremental implementation strategy:

- Phase 1: Explicit type annotations, type environment, basic checking.
- Phase 2: Parametric polymorphism, optional HM inference, monomorphization.
- Phase 3: User‑defined types, subtyping, local inference.

Practical tips:

- Separate semantic analysis from IR codegen.
- Consider two‑pass flow: typecheck first, then codegen.

## Error Messages

- Elm‑style error messages are a target.

## Metrics / Utilities

Non‑blank lines:

```bash
grep -v '^[[:space:]]*$' pyxc.cpp | wc -l
```

Non‑blank, non‑comment‑only lines:

```bash
grep -v '^[[:space:]]*$' pyxc.cpp | grep -v '^[[:space:]]*//' | wc -l
```

## Compiler / PL References

- [Kaleidoscope‑style LLVM tutorial](https://mukulrathi.com/create-your-own-programming-language/llvm-ir-cpp-api-tutorial/)
- [Compiler Construction (Louden)](https://csunplugged.wordpress.com/wp-content/uploads/2012/12/compiler-construction-principles-and-practice-k-c-louden-pws-1997-cmp-2002-592s.pdf)
- [Mapping high‑level constructs to LLVM IR](https://mapping-high-level-constructs-to-llvm-ir.readthedocs.io/en/latest/index.html)
- [Compilers index](https://compilers.iecc.com/index.phtml)
- [PL Zoo](https://plzoo.andrej.com/)
- [Par‑lang](https://github.com/faiface/par-lang)
- [Swift Testing (Apple)](https://developer.apple.com/xcode/swift-testing/)
- [SSA series (mcyoung)](https://mcyoung.xyz/2025/10/21/ssa-1/)

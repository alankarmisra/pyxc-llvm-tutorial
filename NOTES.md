# Personal Review Notes

Open questions and decisions that need my input before implementation. Not part of the tutorial or roadmap docs — scratch tracking only.

## Verification (Phase 22, chapters 94-95)

Context: [ROADMAP.md:382](ROADMAP.md:382) scopes Verifier Phase 1 as `requires`/`ensures`/`assert`/loop `invariant`, SMT-backed VCs, `int`/`bool`/control-flow subset, explicit unsupported diagnostics for heap-alias-heavy proofs.

How this compares to Dafny:

**Same core pipeline**
- Both are annotation-driven: `requires`/`ensures` on functions, `invariant` on loops, `assert` inline.
- Both generate verification conditions (VCs) from annotated code and control flow, then discharge them to an SMT solver (Dafny -> Boogie -> Z3; pyxc likely AST/IR -> SMT-lib -> Z3 directly, skipping the Boogie layer).
- Both verify at compile time — a failed VC is a compile error, not a runtime exception.

**Where the pyxc idea is intentionally smaller**
- Dafny verifies a full heap/type model out of the box: generic collections, sets, sequences, maps, inductive datatypes, separation-logic-style heap framing (`modifies` clauses, dynamic frames). Pyxc Phase 1 explicitly punts on heap-alias-heavy proofs — just `int`/`bool`/control-flow.
- Dafny has termination checking (`decreases` clauses) from day one. Not mentioned in the pyxc roadmap entry yet.
- Dafny compiles verified code down to a target language as a separate, unverified translation step. Pyxc's `requires`/`ensures` sit on the same AST that codegen walks — no cross-language trust gap, in principle.
- Concurrency: Dafny's core verifier is sequential-only too, so pyxc's Phase 2 (thread interleavings, memory-order rules) reaches past where core Dafny stops.

**Open decision before chapter 94:**
Does Phase 1 skip `modifies`/frame conditions entirely (any function can touch any global), or does it fake a minimal version of it? This shapes how much of "SMT-backed" is VC generation vs. just checking `int`/`bool` arithmetic facts.

## Memory safety / ownership model — not actually designed yet

Checked the roadmap: there is no scheduled phase for this. [ROADMAP.md:9](ROADMAP.md:9) only says "a future ownership/borrow-checker phase **may** enforce these statically" — no chapter number, no commitment. Pyxc's real current model is plain C: manual `malloc`/`free`, no static enforcement, no Rust-style borrow checker.

It surfaces as an unresolved blocker in two places, not as a designed feature:
- [ROADMAP.md:160](ROADMAP.md:160) — closures: capture-by-value is fine under no-GC, capture-by-reference needs lifetimes pyxc can't check yet.
- [ROADMAP.md:172](ROADMAP.md:172) — concurrency: "the safety model has to be decided before any of the rest can be designed concretely."

Underlying that: **reference vs. value semantics haven't been pinned down at the language-design level.** Not just "no borrow checker" — the more basic question of when a variable/assignment/param copies vs. aliases isn't settled yet, and that decision has to come before a borrow checker (or any lighter-weight ownership discipline) can even be scoped. This is presumably why heap-alias-heavy proofs are explicitly out of scope for Verifier Phase 1 above — there's no ownership model yet to lean on.

Feels like this needs its own language-design pass — probably before closures (Phase timing above) or concurrency get designed in detail, since both are blocked on it.

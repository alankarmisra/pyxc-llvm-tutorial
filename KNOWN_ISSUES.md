# Known Issues Found During Documentation Review (chapters 16–25)

This file tracks real compiler bugs and undocumented-but-real language features
found while auditing `docs/chapter-16.md` through `docs/chapter-25.md` against
the actual compiled behavior of each chapter's `pyxc.cpp`. It does not track
doc-only wording fixes (stale naming, fabricated code snippets, wrong chapter
cross-references) — only things that affect compiler *behavior* or reveal a
*language feature gap*, since those are the ones worth handing to another
engineer (or Codex) to actually fix in code.

**Grammar: nothing was changed.** No `.ebnf` file in any chapter (16 through
25) was edited. `git status` confirms zero `.ebnf` diffs. The `.cpp` edits
that did happen were exclusively `///`-comment banners above parser functions
(e.g. `/// casttype` → `/// cast-type`, missing `/// sizeof-expression`
banners) — text that documents the grammar in a comment, not the grammar
itself or the parsing logic. Every `.md` doc's "Grammar" / "Full Grammar"
section was checked against the real `.ebnf` file and either already matched
or was corrected to match; the real grammar was never altered to make a doc
example work. **There is no ripple effect into later chapters from this
session's work.**

---

## Compiler issue investigation

### 1. Resolved: `EmitImplicitCast` didn't implement `int → Float32`

`EmitImplicitCast` now accepts every floating-point destination allowed by
`IsAssignable`. The fix is carried from chapter 17 through chapter 44, with
signed regression coverage in every affected chapter and unsigned regression
coverage from chapter 39 onward.

**File:** `code/chapter-17/pyxc.cpp`, `EmitImplicitCast` (~line 2952), last branch.
**Also likely present unchanged in:** chapters 18–25 (whichever chapters still carry this function verbatim — needs checking per chapter).

`IsAssignable(Dest, Src)` (~line 2855) says any integer type implicitly widens
into *any* float type:

```cpp
if (IsFloatType(Dest) && IsIntType(Src))
  return true;
```

Previously, `EmitImplicitCast`, the function that actually emits the `sitofp`,
only handled a `Float64` destination:

```cpp
if (IsIntType(From) && To == ValueType::Float64)
  return Builder->CreateSIToFP(V, LLVMTypeFor(To), "sitofp");
return nullptr;
```

Previous effect: code that the type checker accepted failed at codegen with a generic,
misleading "type mismatch" error whenever an integer implicitly meets a
`Float32` (or bare `Float`) target. Confirmed with lldb (`IsAssignable`
returns `true` for `Float32, Int32`; `EmitImplicitCast` then returns
`nullptr` for the same pair) and with direct compiles:

- `var y: float32 = n` (n: int32) → fails: "Type mismatch in variable initialization"
- `var y: float = n` (n: int32) → fails, same reason
- `def f(a: int32, b: float32) -> float32: return a + b` → fails: "Type mismatch in arithmetic"
- `def g(n: int32) -> float32: return n` → fails: "Type mismatch in return"
- Passing an `int32` argument to a `float32` parameter → fails: "Argument type mismatch"
- `var y: float64 = n` (same n) → **works fine**
- `float32(n)` (explicit cast) → **works fine**, since `EmitCast` has no such restriction

**Fix:** change the last branch of `EmitImplicitCast` to check `IsFloatType(To)`
instead of `To == ValueType::Float64`, and use `LLVMTypeFor(To)` for the
destination type (the function already does this pattern elsewhere).

**Previously documented as a Known Limitation** in `docs/chapter-17.md`.

---

### 2. Not a compiler bug: Chapter 20 `for`-loop step semantics

**File:** `code/chapter-20/pyxc.cpp`, `ForExpressionNode::codegen` (~line 4486).

While trying to fix chapter 20's flagship example (which used a `while` loop
that doesn't exist in chapter 20 — see Undocumented/removed features below),
I rewrote it using `for` and hit unexplained behavior:

- `for var i: int = 0, i < 3, i + 1: total = total + p[i]` (summing struct
  fields 10, 20, 30) printed **30.0** instead of the expected **60.0**.
  A debug trace printing `i` and `p[i]` each iteration showed only two
  iterations ran, producing pairs `(0,10)` and `(1,20)` — never `(2,30)`.
- An isolated, simpler test, `for var i: int = 0, i < 5, i + 1:
  printd(float64(i))`, printed **0.000000, 1.000000, 3.000000** — it skips
  `i=2` entirely and jumps from `i=1` to `i=3`.

The third expression is the increment amount, not the next value of the loop
variable. Code generation performs `i = i + step`, so writing `i + 1` as the
step intentionally produces `0, 1, 3, 7, ...`. The correct unit-step form is:

```pyxc
for var i: int = 0, i < 5, 1:
```

Chapter 20's two unsupported `while` examples now use a valid end-pointer
`for` loop with a constant step. A regression test verifies that the example
visits all three values and produces `60.000000`.

(Note: chapter 11 onward later replaced this auto-add step mechanism with a
full update expression — `for var i: int = 0, i < 5, i = i + 1:` — so this
specific bug no longer reproduces past chapter 10. This entry is left as
written to accurately record the audit at the time it was performed.)

---

## Undocumented (but real) language features found

### 3. Number literals accept scientific notation (chapter 17+)

**File:** `code/chapter-17/pyxc.cpp`, lexer (~line 545), `SawExp` handling.

`2e3`, `1.5e-2`, etc. are valid literals starting in chapter 17 — the lexer
sets `NumberIsFloat = SawDot || SawExp`, so an exponent alone (no `.`) still
produces a float literal. Chapter 16 rejects this outright
("Unexpected name 'e3'"). This was **not mentioned anywhere** in
`docs/chapter-17.md` before this pass — no grammar bullet, no prose. Now
documented (grammar bullet + "Numeric Literal Types" section + `.ebnf`
diff), but worth double-checking whether chapters 18+ docs also missed
mentioning it, since none of the 18–25 passes were looking for this
specifically.

### 4. `for`-loop's own declared variable gets its own DWARF entry (chapter 16)

Not a feature gap, but a doc gap worth noting since it's easy to miss: when a
`for var i = ...` loop introduces a fresh variable, `EmitDebugDeclare` is
called for it from `ForExpressionNode::codegen`, same as any other local —
the original doc only mentioned two call sites (function args,
`VarStatementNode`) and missed this third one. Fixed in the doc; no code
issue here, just flagging since it's the kind of thing that's easy to miss
again if `ForExpressionNode` codegen changes later.

### 5. `ptr[int8](malloc(...))` requires an explicit same-type cast wrapper (chapter 21)

Not a bug exactly, but a real, load-bearing gap: extern function calls (like
`malloc`) don't carry pointee `StructName` metadata through, so
`ptr[int8](malloc(n))` needs the explicit cast wrapper shown — there's no
way to get a typed pointer directly out of an extern call's return value
otherwise. Documented as-is in `docs/chapter-21.md`; flagging here only
because it's the kind of ergonomics gap that a later chapter might want to
close (e.g., by threading a target type hint into cast-of-call parsing).

### 6. Chapter 38's escape-sequence "departures" paragraph needs a rewrite (docs only, no code issue)

**Files:** `docs/chapter-38.md` (needs the fix), `docs/chapter-39.md` (already correct).

While adding a cppreference citation to chapter 38's character-literal escape
list, I wrote a paragraph framing pyxc's `\0`-only null escape and
fixed-2-digit `\xNN` as unexplained "departures" from C's spec. The user
asked why, and I didn't know — Codex supplied an answer, which I verified
against the real chapter 39 source before trusting it:

- **`\0`-only was a chapter 38 staging restriction, not permanent.** Chapter
  39's `DecodeLiteralCodePoint` (~line 610, the `default:` case) parses a
  full 1-to-3-digit octal escape (`\0`, `\12`, `\101`, `\777`), confirmed by
  reading the real loop in `code/chapter-39/pyxc.cpp`. `docs/chapter-39.md`
  already documents this correctly (its own grammar diff adds
  `octal-escape`, and its prose explicitly says *"I keep `\xNN` at exactly
  two hexadecimal digits, as I defined it in Chapter 38"* — meaning chapter
  39's author already treated the 2-digit hex width as an intentional,
  carried-forward choice, not an oversight).
- **Fixed-2-digit `\xNN` is a real, permanent, deliberate design choice**,
  still true in chapter 39 (`case 'x'` there still reads exactly `High`/`Low`,
  two hex digits, same as chapter 38). The reasoning Codex gave — C's `\x`
  escape is greedy and consumes every following hex digit, so `"\x41B"` in C
  is one escape (`0x41B`) rather than `\x41` followed by `B`, and a
  fixed-width form removes that ambiguity — is consistent with the code but
  I have not confirmed it's the user's actual original reasoning (as opposed
  to a plausible reconstruction). `\u`/`\U` in chapter 39 also use fixed
  widths (4 and 8 digits), consistent with sidestepping the same class of
  greedy-parse ambiguity.

**Action item:** rewrite the "two deliberate departures" paragraph in
`docs/chapter-38.md` (the one citing
https://en.cppreference.com/c/language/escape) to say the `\0`-only
restriction is temporary/staging (superseded by chapter 39's full octal
support) rather than a permanent departure, and reframe the `\xNN`
fixed-width choice as a permanent, intentional design decision rather than
an unexplained one — but confirm with the user first whether the
greedy-parsing rationale is actually why, since that part is Codex's
reconstruction, not a fact pulled from a comment in the source.

---

## Summary for hand-off

The only compiler defect in this report was bug #1, and it is resolved. Bug #2
was an incorrect use of the loop's step expression; the affected Chapter 20
examples and their coverage are now fixed.

Everything else in this file (#3–#6) is a documentation/feature-awareness
note, not a defect to fix.

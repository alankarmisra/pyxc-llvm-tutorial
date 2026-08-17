---
description: "Internal spec: how every chapter in the Pyxc LLVM tutorial is written, commented, and tested."
---
# Pyxc Tutorial — Authoring Spec

This document is for authors and AI assistants working on the tutorial. It captures the conventions that have emerged across chapters 1–5 so that future chapters are written consistently and so that context-compacted AI sessions can resume without re-deriving conventions from scratch.

---

## 1. Repository Layout

```
pyxc-llvm-tutorial/
  docs/             # One markdown file per chapter (chapter-00 through chapter-N)
  code/
    chapter-01/     # Each chapter has its own self-contained C++ program
      pyxc.cpp
      CMakeLists.txt
      test/         # LLVM lit tests for that chapter
        lit.cfg.py
        *.pyxc
```

Each `code/chapter-N/` is a **complete, buildable snapshot**. No chapter links to code from another chapter. A reader can clone the repo, `cd code/chapter-05`, `cmake -S . -B build && cmake --build build`, and get a working binary without touching any other directory.

---

## 2. Chapter Documentation

### 2.1 Frontmatter

Every chapter markdown file begins with YAML frontmatter:

```yaml
---
description: "One sentence. Imperative mood. States what the reader builds and why it matters."
---
```

The description is used as the chapter's subtitle in the site's HTML navigation (generated outside this repo).

### 2.2 Chapter Structure

Every chapter follows this order, without deviation:

| Section | Purpose |
|---|---|
| `## What I Am Building` | Shows what's broken or missing *right now*. Includes a before/after code block so the reader can see exactly what changes. |
| `## Source Code` | A single `git clone + cd` code block. Nothing else. |
| `## Grammar` | Present whenever the chapter changes the grammar. A `grammardiff` fenced block against the previous chapter's real `.ebnf` (see [§4.1](#41-grammar-section-in-the-chapter-doc)). Placed here, second section, before any implementation content — a tool the reader uses going forward, not an appendix. |
| *(concept sections)* | One `##` per concept introduced in the chapter. |
| `## Build and Run` | The exact commands to build and run the chapter binary. |
| `## Try It` | Always present. A real, verified example — actual input, actual output from the built binary — demonstrating the chapter's payoff. Placed right before `## What's Next`. |
| `## What's Next` | One short paragraph pointing at the next chapter. |
| `## Need Help?` | Boilerplate: GitHub Issues and Discussions links, what to include in a bug report. |

**No `## Things Worth Knowing` section, ever.** Don't accumulate behavior notes, caveats, or gotchas into a trailing catch-all list. Each item belongs in prose right next to the code/section it actually originates from — a rounding caveat about a float conversion belongs in that conversion's codegen section, not bundled at the end of the chapter. If something is genuinely a surprising gotcha a reader would hit while experimenting, flag it inline at first encounter, not deferred.

**No standalone `## Error Cases` section, ever.** Same principle as above, applied to errors specifically: move each error example to sit immediately after the code that actually raises it — lexer errors right after the lexer code, parse-time errors right after the parsing code, and so on. Never show an error before the reader has seen what produces it.

### 2.3 Writing Style

**First-person, present-tense voice — "I" must be the grammatical actor, not just present somewhere in the sentence.** Every design decision is narrated as something *I* am doing right now, not as an established fact about the language. Write "I'll use an enum" or "I store precedences in a map," not "an enum is used" or "precedences are stored in a map." Passive voice and third-person description ("the parser handles X") are signs the sentence needs rewriting into what *I* decided and why. The one exception is `chapter-00.md`, which uses "we" for the shared capability the reader and author have as language users; every per-chapter body uses "I."

This is stricter than swapping "we" for "I" or occasionally adding "I" to a sentence that's otherwise still describing the code as the actor. Every sentence describing an action should have *me* doing it, in the driver's seat — not the function, the code block, or an abstract "this chapter." Concrete before/after pairs:
- "[Chapter 36] added `switch`. Multi-way conditionals ... are still written as nested `if`/`else` blocks, which stack up fast:" → "Since I haven't got an `elif` yet, I have to write everything as nested `if`s. I'm going to make my life easier by building in the `elif`."
- "After this chapter, the same logic reads cleanly:" → "After this chapter, I can write the same logic as:"
- "One new token:" → "I add one new token:"
- "Added to the keyword table:" → "And I add it to the keyword table:"
- "Previously `ParseIfStatement` parsed a single condition and body. Now I collect..." — this one's already right: even when describing what changed, *I* am the one who changed it, not the function that "now does" something different.

Test for each sentence: does it read as something happening to/in the code, or as something *I did*? If a function, a class, or "this chapter" is the subject of the verb, rewrite it so "I" is.

Don't over-correct into stiff or padded sentences trying to prove the "I" is there. "I already have `switch` from the last chapter for compile-time integer dispatch; I'll reach for `elif` for everything else — any `bool` expression, chained without nesting" reads like corporate self-narration, not a regular programmer talking. Prefer: "`switch` only works on fixed integer values. `elif` works on anything else — any expression that comes out `bool`." Keep sentences short and plain. Don't enumerate every clause from the original — cut things down, don't just relabel the subject. When in doubt, say less.

**Justify a choice against a rejected alternative.** Don't just state what the code does — show what a reader would naturally try first, why it falls short, and what you do instead. For example: "For analysis, I could just pass around the strings 'def', 'add', '(', 'x', ... but then I have to do string comparisons at each analysis stage... Instead I'll use an enum." A decision presented without the alternative it beat reads like a spec handed down from nowhere, not like someone building something and making calls as they go.

**Account for every code change from the previous chapter, not just the new concept.** Before writing a chapter, diff its `pyxc.cpp` against the prior chapter's in full, and account for every hunk somewhere in the prose, not only the part that motivated the chapter. A three-line lexer addition (new tokens) is just as much "what changed" as the headline algorithm; skipping it leaves the reader unable to find where new syntax entered the language at all. If a change is truly incidental (formatting, a rename already covered by [§3.5](#35-naming-conventions)), it's fine to leave unexplained, but that should be a deliberate judgment, not an oversight from only looking at the interesting diff.

**Before/after in "What I Am Building".** The opening section always shows a concrete problem in the current state and the improved output after the chapter. This frames every concept that follows.

```
ready> def bad(x) return x
Error: Expected ':' in function definition (token: -7)    ← before
```

```
Error (Line 1, Column 12): Expected ':' in function definition
def bad(x) return
           ^~~~                                            ← after
```

**Clarity before terminology.** Introduce the idea before naming it. Show what the code does in plain language, then give the CS or LLVM term — if it needs to be named at all. Never open a section with jargon.

The principle: new terminology is only intimidating when it arrives before the reader has the intuition. Once they understand the thing, the name is just a label. Introduce the label after the understanding, not before.

Bad:
> "This chapter builds the lexer. Its job is to read raw source text and break it into tokens."

Good:
> "The compiler needs to split source text into words and symbols before it can do anything else. This step is called lexing, and the pieces it produces are called tokens."

This applies throughout: `lexer`, `parser`, `AST`, `codegen`, `IR`, `JIT` — every term should follow its explanation, not precede it. Chapter titles and section headings should describe what the thing *does*, not what it's *called*.

**Code snippets show only what the reader needs.** Full functions are shown when the structure itself is the point. When a function is long and the interesting part is a few lines, use `// ...` to elide the boilerplate — but only when the surrounding context makes it unambiguous what was omitted. Never elide in a way that changes what the snippet appears to do.

**Real output only in REPL sessions.** Every line in a REPL session must be actual output from the binary, not invented. If the output would be too long, use `...` on a line by itself inside the code block and note that it was truncated.

### 2.4 chapter-00.md

`chapter-00.md` is the tutorial's front page — tone, motivation, and a narrative "Where We're Headed" tour grouped by phase (e.g. "Chapters 1–3," "Chapters 17–22"). It is not a per-chapter index: there is no exhaustive list of all chapters with individual links. Per-chapter navigation is handled by the site's HTML sidebar (generated outside this repo) once the site is built; a reader working from the raw Markdown only has an explicit link to Chapter 1.

When a new chapter is ready to publish, add or extend its phase's paragraph in "Where We're Headed" if it isn't covered yet. Do not add a new per-chapter bullet or link.

---

## 3. Code Comments

### 3.1 Section Banners

Major sections use a banner comment. We use a short form with 40 dashes so it fits in 80-column terminals:

```cpp
//===----------------------------------------===//
// Lexer
//===----------------------------------------===//
```

Subsections within a section use a plain double-slash comment on a blank line above the block.

### 3.2 Class-Level Block Comments

Classes that readers need to understand conceptually get a block comment *above* the class definition explaining **what** the class is, **why** it exists, and any invariants that must hold. Format:

```cpp
// ClassName - Short noun phrase describing what it represents.
//
// Paragraph explaining the contract: what fields mean, what callers
// must not do, what happens across the REPL boundary, etc.
//
// Any additional notes or invariants.
class SourceManager { ... };
```

Do not document every method inline unless the method has non-obvious behavior. Obvious getters/setters get a single-line comment. Non-obvious methods get a short block.

### 3.3 Function-Level Doc Comments

Functions that are part of the public interface of a section use `///` triple-slash doc comments:

```cpp
/// advance - Read one character from stdin, update LexerLocation and SourceManager.
///
/// This is the single point through which all character consumption flows.
/// Every token branch in gettok() calls advance() rather than getchar()
/// directly, so LexerLocation and the source buffer are always in sync.
///
/// Windows line endings (\r\n) are coalesced to a single \n so the rest of
/// the lexer never needs to handle \r.
static int advance() { ... }
```

Parser functions also get an EBNF banner after the function-level doc comment, enclosed in `[ ]` to mark it as a grammar rule, not code:

```cpp
/// ParseFunctionSignature - Parse a function signature.
///
///   function-signature = name "(" [ name { "," name } ] ")" ;
///
/// Returns nullptr on failure; leaves CurrentToken on the first token past the ')'.
static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() { ... }
```

Internal helpers (e.g., `LogError`) that are self-evident from their name and body get a one-line `//` comment.

### 3.4 Inline Comments

Inline comments explain *why*, not *what*. Code that is non-obvious or that reflects a deliberate decision gets a comment. Examples:

```cpp
// Skip horizontal whitespace. Stop at '\n' — that becomes tok_eol.
while (isspace(LastChar) && LastChar != '\n')
  LastChar = advance();

// Snapshot position for the upcoming token. See note above about tok_eol.
CurrentTokenLocation = LexerLocation;
```

Do not comment every line. A comment on every line is noise. Reserve inline comments for:
- Subtle invariants (`// LexerLocation.Line is already on the next line here`)
- Cross-references to related non-obvious code (`// re-snapshot after consuming comment + '\n'`)
- Deliberate non-obvious choices (`// false = not variadic`)

### 3.5 Naming Conventions

Naming decisions are made for what's clearest to a reader learning compilers for the first time, not for compatibility with any other tutorial and not out of deference to "that's how compiler theory names things." If a name is foundational to a whole tradition of compiler writing but is abbreviated, ambiguous, or confusing without prior context, we rename it. Pyxc already does this: the original abbreviated `ExprAST`/`PrototypeAST` style became `ExpressionNode`/`FunctionSignatureNode` because full words read clearer to newcomers than abbreviations. Don't reach for "but the reference tutorial/textbook does it this way" as a justification. Think from first principles about what a reader needs to follow along, every time, including for names that feel load-bearing or sacred elsewhere.

House style within pyxc, kept for internal consistency, not because any external tutorial uses it:

- Types and class names: `PascalCase` (`SourceManager`, `ExpressionAST`)
- Functions: `PascalCase` for parse/codegen functions (`ParseExpression`, `ParseFunctionSignature`), `camelCase` for helpers
- Local variables: `PascalCase` for LLVM types (`TheFunction`, `RetVal`), `PascalCase` for all variables in parse/codegen functions
- Loop indices: single lowercase letters are fine (`i`, `e`) but prefer descriptive names for outer loops
- Globals: `PascalCase` prefixed with `The` for LLVM singletons (`TheContext`, `TheModule`, `TheBuilder`)

We use lowercase `ch` and `idx` for loop variables in code we write ourselves, preferring readability over the capitalized single-letter convention (`C`, `I`) some compiler tutorials use. We don't "fix" LLVM API code we copy verbatim (e.g. IRBuilder call patterns), since that's third-party code, not a teaching choice we made.

**AST node suffix.** Node classes use the `Node` suffix (`ExpressionNode`, `BinaryExpressionNode`), not an `AST` suffix. Each class is one node in the tree, not the tree itself, so `Node` says what the class is more literally than `AST` does.

**Full-word identifier migration.** The table below is the canonical spelling for identifiers that started out in abbreviated form. Chapters 2 and 3 already use the right-hand column throughout. Chapters 4 and later still use the left-hand (original abbreviated) column as of this writing; migrate a chapter's identifiers to the right-hand column whenever that chapter is next substantially edited, rather than doing a mechanical repo-wide rename in one pass.

| Old (abbreviated) | New |
|---|---|
| `...AST` suffix (`ExprAST`, `BinaryExprAST`) | `...Node` suffix (`ExpressionNode`, `BinaryExpressionNode`) |
| `Op` | `Operator` |
| `LHS` | `Left` |
| `RHS` | `Right` |
| `Val` | `Value` |
| `Args` (as a field/param name) | `Arguments` |
| `BinopPrecedence` (the precedence map) | `OperatorPrecedence` |
| `GetTokPrecedence()` | `GetTokenPrecedence()` |
| `ExprPrec` (param: minimum precedence accepted) | `MinimumPrecedence` |
| `TokPrec` (precedence of the current token) | `TokenPrecedence` |
| `NextPrec` (precedence after parsing the right operand) | `NextTokenPrecedence` |
| `ParseBinOpRHS()` | `ParseBinaryOperatorRight()` |
| `LogErrorV()` | `LogErrorValue()` |
| `Doubles` (function parameter-type vector) | `ParameterTypes` |
| `FT` | `LLVMFunctionType` |
| `F` (generated LLVM function) | `TheFunction` |
| `Idx` (parameter-name loop) | `ParameterIndex` |
| `Arg` (LLVM argument loop variable) | `Argument` |

When a chapter introduces a new abbreviated identifier not yet in this table, add it here rather than leaving it as a one-off.

---

### 3.6 EBNF File

Every chapter directory contains a `pyxc.ebnf` that is the **single source of truth** for the grammar. It uses ISO EBNF notation with two conventions documented at the top:

```
{ } = zero or more
[ ] = zero or one (optional)
```

---

## 4. Thread Memory (for new sessions)

Use this as a quick orientation checklist when starting a new thread.

Repository structure
- Chapters live in `docs/chapter-XX.md`.
- Code lives in `code/chapter-XX/` with `pyxc.cpp`, `CMakeLists.txt`, `pyxc.ebnf`, and `test/`.
- `chapter-00.md` is the front page (tone, motivation, "Where We're Headed" phase tour) — not a per-chapter index. Per-chapter navigation lives in the site's HTML sidebar, generated outside this repo.

Naming conventions
- Chapter files use `chapter-XX.md` (two-digit, zero-padded).
- Code directories use `code/chapter-XX/`.
- Grammar lives in `code/chapter-XX/pyxc.ebnf`.
- Tests are `code/chapter-XX/test/*.pyxc`.

Testing and build
- Tests are LLVM lit tests.
- Tests live in `code/chapter-XX/test/`.
- Run tests with `llvm-lit -v test` from the chapter directory.
- Build with `./build.sh` (preferred) or `cmake -S . -B build && cmake --build build`.

REPL and transcripts
- REPL prompt is `ready>`.
- Only include real output in REPL transcripts; use `...` for truncation.

General workflow reminder
- Each chapter is a self-contained snapshot; do not reference code from other chapters.
- Update grammar first when adding new syntax, then parser/code.

The `.ebnf` file, the `///` grammar banners in `.cpp`, and the `## Grammar` section in the chapter `.md` must all agree — same production names, same structure. When any one changes, update the other two in the same edit session.

### 4.1 Grammar Section in the Chapter Doc

Every chapter doc that introduces grammar changes has a `## Grammar` section placed immediately after `## Source Code` — second section in the chapter, before any implementation content, framed as a tool the reader uses going forward, not an appendix.

**Structure:**
1. A path line above the code block: `` `code/chapter-N/pyxc.ebnf` ``.
2. A single fenced `grammardiff` code block: the complete grammar rendered as a unified diff against the previous chapter's real `.ebnf` file — unchanged lines get a leading space, added lines a leading `+`, removed lines a leading `-`. This is the whole grammar, not just the changed productions; the diff markers are what let a reader see at a glance what's new. Verify it by reconstructing both the "old" (context + `-` lines) and "new" (context + `+` lines) sides and diffing each against the real `code/chapter-(N-1)/pyxc.ebnf` and `code/chapter-N/pyxc.ebnf`.
3. Brief prose below the block explaining any non-obvious terminal symbols or naming the specific productions that changed.

Unchanged, purely-explanatory `(* ... *)` comment banners (e.g. the `comment`/`whitespace` doc-comments) may be omitted from the diff entirely rather than shown as unchanged context — they don't affect the grammar and just add noise.

### 4.2 Production Name Consistency

The name of a grammar production must be identical everywhere it appears: in `pyxc.ebnf`, in the `///` EBNF banner above the corresponding Parse* function in `.cpp`, in the `## Grammar` section of the `.md`, and in prose references within the `.md`.

### 4.3 Forward Declarations

When a Parse* function A calls another Parse* function B that is defined later in the file, add a forward declaration immediately above A's doc comment. Do not reorder functions — the file's top-to-bottom order reflects the grammar (lexer → parser → codegen → driver) and must be preserved.

### 4.4 lit.cfg.py

`config.name` must be `"pyxc-chapterNN"` (zero-padded to two digits, e.g. `"pyxc-chapter09"`). If the test directory contains `.pyxc` files that are not lit tests (e.g. exploratory scripts with no `# RUN:` lines), list them explicitly in `config.excludes`.

---

## 5. Tests

### 5.1 Framework

Tests use **LLVM lit** (LLVM Integrated Tester). Each chapter has:

```
code/chapter-N/test/
  lit.cfg.py       # lit configuration; sets %pyxc substitution
  *.pyxc           # one test per file
```

The `lit.cfg.py` registers the chapter's binary as `%pyxc` and runs all `.pyxc` files in the directory. The test suite name in `lit.cfg.py` must be `pyxc-chapter-N` (matching the directory name) to avoid cross-chapter confusion.

Run the tests:

```bash
llvm-lit -v code/chapter-N/test/
```

### 5.2 Test File Format

Every test file is a valid Pyxc input that pyxc reads from stdin. Lines beginning with `#` are Pyxc comments (they are consumed by the lexer and ignored). `# RUN:` lines are lit directives — they are also Pyxc comments, so they do not alter the semantic content of the test input.

```
# RUN: %pyxc < %s > %t 2>&1
# RUN: grep -q "Parsed a function definition" %t

# function signature: one-arg form; parser must accept and echo "Parsed a function definition."
def foo(x): x
```

Header format:
1. `# RUN:` directives (one per assertion)
2. Blank line
3. `#` comment explaining the grammar rule being exercised and *why* this specific input was chosen
4. The actual Pyxc input

### 5.3 Principled Test Design

Tests are derived from the grammar, not written ad hoc. For each grammar rule:

**One test per optional element (present and absent):**
```
signature_zero_args.pyxc   — def foo(): 0
signature_one_arg.pyxc     — def foo(x): x
signature_multi_args.pyxc  — def foo(x, y, z): x
```

**One test per error branch in the parser:** Every `LogError` or `return nullptr` in a Parse* function gets a test. The test checks:
1. An error was emitted (grep for `"Error"` or `"Error (Line"` for chapter ≥ 3)
2. The error message text (grep for a distinctive substring)
3. The source context (grep for the input text that should appear on the error line — see §5.5)

**One test per boundary condition:**
- Empty argument list vs. one arg vs. many args
- Expressions at different precedence levels
- Left-associativity (three-operand chain: `a - b - c` should parse as `(a-b)-c`, not `a-(b-c)`)

**One test per new lexer rule added in this chapter.**

### 5.4 Test File Naming

```
<grammar-rule>_<variant>.pyxc        — valid input tests
error_<rule>_<missing-or-bad>.pyxc   — error input tests
location_<scenario>.pyxc             — diagnostic/location tests (chapter 3+)
```

Examples:
```
call_zero_args.pyxc
call_multi_args.pyxc
error_signature_missing_name.pyxc
error_definition_missing_colon.pyxc
location_after_comment.pyxc
location_sequential_lines.pyxc
```

### 5.5 Source Context Test Caution

The source context in error messages (the `^~~~` caret line) shows `CurrentLine` **at the moment the error fires**, not the full input line. The error fires on the first unexpected token. Tokens after that point have not been consumed into `CurrentLine` yet.

**Rule:** when grepping for source context, grep for text that is definitely consumed *before* the unexpected token, not text that appears *after* it in the source.

Example — input `def foo(x) return x`:
- The error fires on `tok_return` (the missing `:` is detected when `return` is seen)
- At that point `CurrentLine` contains `"def foo(x) return "` — it does NOT yet contain `" x"`
- Correct grep: `grep -q "def foo(x) return" %t`
- Wrong grep: `grep -q "def foo(x) return x" %t`

Example — input `1 + + 2`:
- The error fires on the second `+` (unexpected token in primary expression)
- At that point `CurrentLine` contains `"1 + +"` — it does NOT yet contain `" 2"`
- Correct grep: `grep -q "1 + +" %t`

### 5.6 Line Number Tests

Do **not** grep for absolute line numbers like `"Error (Line 3,"`. The `# RUN:` and `#` comment lines at the top of the test file are valid Pyxc input (they are Pyxc comments) and count as lines, shifting the absolute line numbers of the actual test input.

Instead:
- Count the total number of errors: `grep -c "Error (Line" %t | grep -q "3"`
- Check that *an* error has a line header: `grep -q "Error (Line" %t`
- Check the source context text (which is stable regardless of absolute line number)

### 5.7 Chapter-Specific Test Additions

**Chapter 2** tests: grep for `"Error: "` (no location), the error message text, and (where safe) the source context.

**Chapter 3** tests: grep for `"Error (Line"` (location present), the error message text, and the source context (with the §5.5 caution).

Chapter 3 also requires tests for the diagnostic infrastructure itself:
- `location_sequential_lines.pyxc`: N errors on N successive lines → grep for count N
- `location_after_success.pyxc`: one error after a successful parse → count must be exactly 1
- `location_after_comment.pyxc`: error on the line after a `#` comment → source context must show the error line, not the comment

**Chapter 5+** tests: TBD. Will include IR shape tests using `grep` on `define double`, `ret double`, etc.

**Chapter 6** tests: verify JIT execution (`grep -q "Evaluated to"`), numeric correctness (grep for exact value), cross-module function calls, optimisation IR shape (grep for specific instruction patterns like `fmul double %addtmp, %addtmp`), and runtime library callability. No double-print: each module is handed to the JIT so the IR appears only once per function.

---

## 6. Chapter Checklist

Before marking a chapter ready to publish:

- [ ] `chapter-N.md` exists in `docs/` with correct frontmatter
- [ ] All required sections present in the required order (§2.2)
- [ ] "What I Am Building" has a before/after example using real output
- [ ] Any REPL session output matches actual binary output
- [ ] Counter-intuitive behaviour is flagged inline at first encounter, not deferred to a catch-all section
- [ ] `code/chapter-N/pyxc.cpp` builds cleanly with CMake
- [ ] All functions with non-trivial logic have `///` doc comments
- [ ] All Parser functions have EBNF banners
- [ ] `code/chapter-N/test/` has tests derived from the grammar (§5.3)
- [ ] All tests pass: `llvm-lit -v code/chapter-N/test/`
- [ ] `code/chapter-N/pyxc.ebnf` exists and matches the grammar implemented in `.cpp`
- [ ] `## Grammar` section in the `.md` is a `grammardiff` fenced block (unified-diff style, leading `+`/`-`/space markers) against the previous chapter's real `pyxc.ebnf`, verified by reconstructing both the old and new sides and diffing each against the real files
- [ ] All `///` EBNF banners in `.cpp` use the same production names as `pyxc.ebnf`
- [ ] All code snippets in the `.md` are verified against the actual source byte-level where feasible (not just spot-checked), and every visible change/diff has prose explaining it — nothing added or changed silently
- [ ] `lit.cfg.py` excludes any non-lit `.pyxc` files in the test directory

---

## 7. What Each Chapter Covers

For what each chapter covers, see [ROADMAP.md](../ROADMAP.md) — that's the single source of truth for chapter numbering and content, kept current as the sequence changes. Don't duplicate it here.

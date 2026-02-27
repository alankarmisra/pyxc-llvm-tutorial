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

The description is used in `chapter-00.md` as the subtitle below the chapter link.

### 2.2 Chapter Structure

Every chapter follows this order, without deviation:

| Section | Purpose |
|---|---|
| `## Where We Are` | Shows what's broken or missing *right now*. Includes a before/after code block so the reader can see exactly what changes. |
| `## Source Code` | A single `git clone + cd` code block. Nothing else. |
| *(concept sections)* | One `##` per concept introduced in the chapter. |
| `## Build and Run` | The exact three commands to build and run the chapter binary. |
| `## Try It` | Annotated REPL session. Real output only — never invented. |
| `## What We Built` | A table: one row per significant piece added. Left column: name or symbol. Right column: one-sentence description. |
| `## Known Limitations` | Bulleted list of things that are deliberately deferred. Links to the chapter that addresses each one. |
| `## What's Next` | One short paragraph pointing at the next chapter. |
| `## Need Help?` | Boilerplate: GitHub Issues and Discussions links, what to include in a bug report. |

### 2.3 Writing Style

**Before/after in "Where We Are".** The opening section always shows a concrete problem in the current state and the improved output after the chapter. This frames every concept that follows.

```
ready> def bad(x) return x
Error: Expected ':' in function definition (token: -7)    ← before
```

```
Error (Line 1, Column 12): Expected ':' in function definition
def bad(x) return
           ^~~~                                            ← after
```

**Build from the concept, label later.** Introduce the idea before naming it. Show the code, explain what each line does, *then* give the LLVM/CS term if one exists. Never open with jargon.

**Code blocks are always complete and correct.** Never truncate a code block with `// ...` unless the omission is clearly marked and the surrounding context makes it unambiguous what was left out.

**Real output only in "Try It".** Every line in the REPL session sections must be actual output from the binary, not invented. If the output would be too long, use `...` on a line by itself inside the code block and note that it was truncated.

**"What We Built" table is exhaustive.** Every function, class, or global variable introduced in the chapter gets a row. If a concept is important enough to explain in prose, it belongs in the table.

**"Known Limitations" is honest.** List every deliberate shortcut taken. Do not omit limitations to make the chapter look more complete. Each bullet should end with "Chapter N adds ..." where N is the chapter that addresses it.

### 2.4 chapter-00.md

`chapter-00.md` is the index. Each chapter appears as:

```markdown
- [Chapter N: Title](chapter-N.md) — *description from frontmatter*
```

New chapters are uncommitted from the list only once the chapter is ready to publish. The line is in the file, commented with `<!-- -->`, until the chapter doc is written.

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
/// advance - Read one character from stdin, update LexLoc and SourceManager.
///
/// This is the single point through which all character consumption flows.
/// Every token branch in gettok() calls advance() rather than getchar()
/// directly, so LexLoc and the source buffer are always in sync.
///
/// Windows line endings (\r\n) are coalesced to a single \n so the rest of
/// the lexer never needs to handle \r.
static int advance() { ... }
```

Parser functions also get an EBNF banner after the function-level doc comment, enclosed in `[ ]` to mark it as a grammar rule, not code:

```cpp
/// ParsePrototype - Parse a function prototype.
///
///   prototype ::= id '(' id* ')'
///
/// Returns nullptr on failure; leaves CurTok on the first token past the ')'.
static unique_ptr<PrototypeAST> ParsePrototype() { ... }
```

Internal helpers (e.g., `LogError`) that are self-evident from their name and body get a one-line `//` comment.

### 3.4 Inline Comments

Inline comments explain *why*, not *what*. Code that is non-obvious or that reflects a deliberate decision gets a comment. Examples:

```cpp
// Skip horizontal whitespace. Stop at '\n' — that becomes tok_eol.
while (isspace(LastChar) && LastChar != '\n')
  LastChar = advance();

// Snapshot position for the upcoming token. See note above about tok_eol.
CurLoc = LexLoc;
```

Do not comment every line. A comment on every line is noise. Reserve inline comments for:
- Subtle invariants (`// LexLoc.Line is already on the next line here`)
- Cross-references to related non-obvious code (`// re-snapshot after consuming comment + '\n'`)
- Deliberate non-obvious choices (`// false = not variadic`)

### 3.5 Naming Conventions

Follow the LLVM naming conventions used in the original Kaleidoscope tutorial because readers are often cross-referencing both:

- Types and class names: `PascalCase` (`SourceManager`, `ExprAST`)
- Functions: `PascalCase` for parse/codegen functions (`ParseExpression`, `ParsePrototype`), `camelCase` for helpers
- Local variables: `PascalCase` for LLVM types (`TheFunction`, `RetVal`), `PascalCase` for all variables in parse/codegen functions
- Loop indices: single lowercase letters are fine (`i`, `e`) but prefer descriptive names for outer loops
- Globals: `PascalCase` prefixed with `The` for LLVM singletons (`TheContext`, `TheModule`, `TheBuilder`)

The LLVM tutorial itself uses capital `C` and `I` as loop variables in the lexer. We do not; we use `ch` and `idx` (lowercase) for loop variables in our own code, but we do not "fix" LLVM-provided code we copy verbatim.

---

## 4. Tests

### 4.1 Framework

Tests use **LLVM lit** (LLVM Integrated Tester). Each chapter has:

```
code/chapter-N/test/
  lit.cfg.py       # lit configuration; sets %pyxc substitution
  *.pyxc           # one test per file
```

The `lit.cfg.py` registers the chapter's binary as `%pyxc` and runs all `.pyxc` files in the directory. The test suite name in `lit.cfg.py` must be `pyxc-chapter-N` (matching the directory name) to avoid cross-chapter confusion.

Run the tests:

```bash
llvm-lit code/chapter-N/test/
```

### 4.2 Test File Format

Every test file is a valid Pyxc input that pyxc reads from stdin. Lines beginning with `#` are Pyxc comments (they are consumed by the lexer and ignored). `# RUN:` lines are lit directives — they are also Pyxc comments, so they do not alter the semantic content of the test input.

```
# RUN: %pyxc < %s > %t 2>&1
# RUN: grep -q "Parsed a function definition" %t

# prototype: one-arg form; parser must accept and echo "Parsed a function definition."
def foo(x): return x
```

Header format:
1. `# RUN:` directives (one per assertion)
2. Blank line
3. `#` comment explaining the grammar rule being exercised and *why* this specific input was chosen
4. The actual Pyxc input

### 4.3 Principled Test Design

Tests are derived from the grammar, not written ad hoc. For each grammar rule:

**One test per optional element (present and absent):**
```
prototype_zero_args.pyxc   — def foo(): return 0
prototype_one_arg.pyxc     — def foo(x): return x
prototype_multi_args.pyxc  — def foo(x, y, z): return x
```

**One test per error branch in the parser:** Every `LogError` or `return nullptr` in a Parse* function gets a test. The test checks:
1. An error was emitted (grep for `"Error"` or `"Error (Line"` for chapter ≥ 3)
2. The error message text (grep for a distinctive substring)
3. The source context (grep for the input text that should appear on the error line — see §4.5)

**One test per boundary condition:**
- Empty argument list vs. one arg vs. many args
- Expressions at different precedence levels
- Left-associativity (three-operand chain: `a - b - c` should parse as `(a-b)-c`, not `a-(b-c)`)

**One test per new lexer rule added in this chapter.**

### 4.4 Test File Naming

```
<grammar-rule>_<variant>.pyxc        — valid input tests
error_<rule>_<missing-or-bad>.pyxc   — error input tests
location_<scenario>.pyxc             — diagnostic/location tests (chapter 3+)
```

Examples:
```
call_zero_args.pyxc
call_multi_args.pyxc
error_prototype_missing_name.pyxc
error_definition_missing_colon.pyxc
location_after_comment.pyxc
location_sequential_lines.pyxc
```

### 4.5 Source Context Test Caution

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

### 4.6 Line Number Tests

Do **not** grep for absolute line numbers like `"Error (Line 3,"`. The `# RUN:` and `#` comment lines at the top of the test file are valid Pyxc input (they are Pyxc comments) and count as lines, shifting the absolute line numbers of the actual test input.

Instead:
- Count the total number of errors: `grep -c "Error (Line" %t | grep -q "3"`
- Check that *an* error has a line header: `grep -q "Error (Line" %t`
- Check the source context text (which is stable regardless of absolute line number)

### 4.7 Chapter-Specific Test Additions

**Chapter 2** tests: grep for `"Error: "` (no location), the error message text, and (where safe) the source context.

**Chapter 3** tests: grep for `"Error (Line"` (location present), the error message text, and the source context (with the §4.5 caution).

Chapter 3 also requires tests for the diagnostic infrastructure itself:
- `location_sequential_lines.pyxc`: N errors on N successive lines → grep for count N
- `location_after_success.pyxc`: one error after a successful parse → count must be exactly 1
- `location_after_comment.pyxc`: error on the line after a `#` comment → source context must show the error line, not the comment

**Chapter 5+** tests: TBD. Will include IR shape tests using `grep` on `define double`, `ret double`, etc.

---

## 5. Chapter Checklist

Before marking a chapter ready to publish:

- [ ] `chapter-N.md` exists in `docs/` with correct frontmatter
- [ ] All sections present in the required order (§2.2)
- [ ] "Where We Are" has a before/after example using real output
- [ ] "Try It" output matches actual binary output
- [ ] "What We Built" table has one row per introduced piece
- [ ] "Known Limitations" lists all deliberate gaps with forward references
- [ ] `code/chapter-N/pyxc.cpp` builds cleanly with CMake
- [ ] All functions with non-trivial logic have `///` doc comments
- [ ] All Parser functions have EBNF banners
- [ ] `code/chapter-N/test/` has tests derived from the grammar (§4.3)
- [ ] All tests pass: `llvm-lit code/chapter-N/test/`
- [ ] `chapter-00.md` lists chapter N with the frontmatter description
- [ ] `chapter-00.md` chapter N line is **uncommented**

---

## 6. What Each Chapter Covers (Summary)

| Chapter | Title | Core addition |
|---|---|---|
| 0 | Overview | Table of contents and project description |
| 1 | Lexer | `gettok()`, tokens, REPL skeleton |
| 2 | Parser | Recursive descent, AST nodes, precedence climbing |
| 3 | Better Errors | Keyword table, number validation, source locations, caret diagnostics |
| 4 | Installation | LLVM install guide (macOS, Linux, Docker) |
| 5 | Code Generation | LLVM IR, `LLVMContext`/`Module`/`IRBuilder`, `codegen()` on AST nodes |
| 6 | *(planned)* | ORC JIT + optimization pass manager |
| 7 | *(planned)* | Control flow: `if`/`then`/`else`, `for` loop |
| 8 | *(planned)* | Mutable variables: `alloca`, mem2reg |

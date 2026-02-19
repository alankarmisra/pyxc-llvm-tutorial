# Pyxc Chapter Roadmap (Incremental Build)

This roadmap aims for small, meaningful chapters and a steady sense of progress.

## Scope and pacing

- Target size: **32 chapters** (reasonable middle ground between too dense and too fragmented)
- Chapter shape: one primary concept per chapter, runnable demo, small tests
- Build rule: chapters **7 onward** extend the previous code chapter (or nearest earlier code chapter if needed)

## Phase 1: Foundations and First End-to-End Compiler

1. Pyxc: Lexer Basics
2. Pyxc: Parser and AST
3. Pyxc: Building LLVM from Source
4. Pyxc: Command Line Interface
5. Pyxc: Code Generation to LLVM IR
6. Pyxc: Understanding LLVM IR
7. Pyxc: Optimization and JIT Execution
8. Pyxc: Unary Operators (`-`, `not`)
9. Pyxc: Binary Logical Operators (`and`, `or`)
10. Pyxc: Comparison Operators (`<`, `<=`, `>`, `>=`, `==`, `!=`)

## Phase 2: Control Flow and Language Ergonomics

11. Pyxc: Mutable Variables (`let` and `var`, or `var`-only mode)
12. Pyxc: Assignment Semantics and Variable Updates
13. Pyxc: `if` Expressions and Conditional Branching
14. Pyxc: `for/in` with `range(start, end, step)`
15. Pyxc: File Input Mode (run `.pyxc` files)
16. Pyxc: Statement Blocks with `;`
17. Pyxc: Indentation-Based Blocks
18. Pyxc: `while` Loops
19. Pyxc: `match/case` Basics
20. Pyxc: `match/case` with Guards and Defaults

## Phase 3: Native Toolchain Pipeline

21. Pyxc: Emitting Object Files
22. Pyxc: Optimization Levels (`-O0`..`-O3`) and Pass Pipelines
23. Pyxc: Generating Executables
24. Pyxc: Linking Under the Hood
25. Pyxc: Debug Information (DWARF basics)

## Phase 4: Types and Memory Model

26. Pyxc: Type System Fundamentals (LLVM/C-like primitives)
27. Pyxc: Typed Function Signatures and Type Checking
28. Pyxc: Struct Definitions and Field Access
29. Pyxc: Dynamic Memory (`malloc`/`free`)
30. Pyxc: Pointers and Pointer Arithmetic
31. Pyxc: Multi-file Compilation Units
32. Pyxc: Parallel Compilation and Final Linking

## Optional Extension Track (if desired)

- Error reporting and source spans
- Standard library bootstrap
- Pattern-matching exhaustiveness checks
- Escape analysis or stack allocation wins
- Packaging and installable CLI workflow

## Future Phases (Planned Expansion)

These come after chapter 32, once the typed + memory foundations are stable.

### Phase 5: Modules and Code Organization

33. Pyxc: Module Declarations and Namespaces
34. Pyxc: `import` Basics (single-file imports, symbol visibility)
35. Pyxc: `export`/Public API Design
36. Pyxc: Multi-file Name Resolution and Diagnostics
37. Pyxc: Cyclic Imports and Resolution Strategy
38. Pyxc: Incremental Rebuilds and Module Caching

### Phase 6: Classes and Traits

39. Pyxc: Class Syntax and Field Layout
40. Pyxc: Methods and `self`
41. Pyxc: Constructors and Initialization Rules
42. Pyxc: Visibility and Encapsulation
43. Pyxc: Traits/Interfaces (declarations and contracts)
44. Pyxc: Trait Implementations and Dispatch Strategy
45. Pyxc: Generic Traits and Constraint Checking (intro)

### Phase 7: Concurrency and Parallel Computation

46. Pyxc: Concurrency Model Overview and Safety Rules
47. Pyxc: Spawning Tasks/Threads
48. Pyxc: Shared State and Synchronization Primitives
49. Pyxc: Message Passing (channels/queues)
50. Pyxc: Parallel Loops and Work Partitioning
51. Pyxc: Determinism, Races, and Debugging Concurrent Programs
52. Pyxc: Parallel Compilation Pipeline Enhancements

## Editorial constraints for every chapter

- Start from prior chapter codebase and add only this chapter's delta
- Clearly list: grammar changes, AST changes, semantic rules, IR/codegen changes
- Include at least:
  - 1 success example
  - 1 edge-case example
  - 2-5 automated tests
- End with "What you can build now" to reinforce progress

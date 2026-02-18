# Pyxc Tutorial - Key Decisions

## Type System Design (decided ~Chapter 10)

**Near term:** Types as annotations (`x: Float`, `-> Int`) lowering directly to LLVM types.
No struct dispatch, no method calls - just thread type info through AST and pick the right
LLVM type (`i32`, `double`, etc.). Keep hardcoded binop machinery (`CreateFAdd`, etc.).

**Later (once generics exist):** Retrofit types as structs Mojo-style.
- `Int`, `Float` become library types defined as structs with `__add__` etc.
- Replace hardcoded binop machinery with struct-based method dispatch
- SIMD substitutability follows naturally from generics
- Show old vs new side-by-side in the generics chapter as a "look how far we've come" moment

**Why deferred:** Struct-based types require generics/parametric types for SIMD
substitutability to work transparently. Building that before blocks/control flow would
make the ramp-up too steep before the language feels usable.

## Completed Chapters

- **Chapter 11: "Linking: Under the Hood"** - Written. No new code. Uses `nm` and `objdump`
  on real chapter10 output to explain symbol resolution (T vs U symbols), relocation (patching
  placeholder addresses), what gets linked in (hello.o + runtime.o + libSystem), and platform
  differences (Mach-O/ELF/PE). Concrete ARM64 macOS disassembly showing `bl 0xc` → `bl 0x1000004bc`.

## Planned Chapter Order (from Chapter 12)

1. **Chapter 12:** Binops (comparisons `==`, `!=`, `<=`, `>=`, logical `and`, `or`, `not`)
2. **Chapter 13:** Fibonacci and Mandelbrot as showcase examples
3. **Chapter 14:** Blocks with `;` statement terminator (separate problem from indentation)
4. **Chapter 15:** Indentation-based blocks (purely a lexer transformation at this point)
5. **Chapter 16:** Types as annotations (direct LLVM type lowering, no struct dispatch)
6. **Chapter 17:** Structs as data containers
7. **Chapter 18:** Refactor (remove globals, allow multiple compiler instances) - do just before multi-module
8. **Chapter 19:** Multi-module compilation
9. **Chapter 20:** Generics
10. **Chapter 21:** Revisit types as structs with operator methods, SIMD substitutability

## Refactoring Chapter

Hold until just before multi-module. Motivation becomes obvious then - need multiple
compiler instances, so globals have to go. Too early = readers don't have reason to care.

## `@compile_time` Decorator

Interesting idea (like `constexpr`) - spin off a JIT interpreter instance for functions
marked with the decorator. Defer until after multi-module and generics are in place.

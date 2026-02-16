# Phase 1: Lexer in Pyxc - Progress

**Started:** 2026-02-15
**Status:** In Progress

## Goal

Implement a tokenizer (lexer) entirely in Pyxc that can:
1. Read source code character by character
2. Recognize keywords (def, extern, return, if, else)
3. Tokenize identifiers and numbers
4. Handle comments and whitespace
5. Track line/column positions for error reporting

## Files Created

### lexer.pyxc (400+ lines)

**Data Structures:**
```python
struct Token:
    type: i32        # Token type constant
    line: i32        # Line number (1-based)
    col: i32         # Column number (0-based)
    str_val: i8*     # Identifier/keyword string (allocated)
    num_val: f64     # Numeric value (float)
    int_val: i64     # Numeric value (integer)
    is_int: i32      # Boolean: integer vs float

struct Lexer:
    source: i8*      # Source code string
    pos: i64         # Current position
    line: i32        # Current line
    col: i32         # Current column
    current_char: i8 # Current character
```

**Token Types Supported:**
- `tok_eof` (-1) - End of file
- `tok_eol` (-2) - End of line
- `tok_def` (-4) - `def` keyword
- `tok_extern` (-5) - `extern` keyword
- `tok_identifier` (-6) - Variable/function names
- `tok_number` (-7) - Numeric literals
- `tok_if` (-8) - `if` keyword
- `tok_else` (-10) - `else` keyword
- `tok_return` (-11) - `return` keyword
- Single-char tokens - Operators like `+`, `-`, `*`, `/`, `(`, `)`, etc.

**Key Functions:**

```python
# Creation/destruction
def create_lexer(source: i8*) -> Lexer*
def free_lexer(lex: Lexer*) -> void
def create_token(type: i32, line: i32, col: i32) -> Token*
def free_token(tok: Token*) -> void

# Character operations
def advance(lex: Lexer*) -> void
def peek(lex: Lexer*, offset: i32) -> i8
def skip_whitespace(lex: Lexer*) -> void
def skip_comment(lex: Lexer*) -> void

# Tokenization
def lex_identifier(lex: Lexer*) -> Token*
def lex_number(lex: Lexer*) -> Token*
def get_next_token(lex: Lexer*) -> Token*

# Keywords
def get_keyword_token(word: i8*) -> i32

# Utilities
def print_token(tok: Token*) -> void
def get_token_name(type: i32) -> i8*
```

**External Dependencies:**
Uses C functions via extern declarations:
- `pyxc_strlen`, `pyxc_strcmp`, `pyxc_strdup` (from string_utils.c)
- `pyxc_isalpha`, `pyxc_isdigit`, `pyxc_isalnum`, `pyxc_isspace` (from string_utils.c)
- `malloc`, `free`, `printf` (standard library)

## Implementation Notes

### Character Handling
- **Newline tracking:** Increments line counter, resets column
- **Whitespace skipping:** Skips spaces/tabs but preserves newlines
- **Comment support:** `#` comments until end of line

### Number Parsing
- **Integer literals:** `42`, `123`
- **Float literals:** `3.14`, `0.5`
- **Simplified parser:** Direct conversion without strtod (for now)
- **Tracks both int and float values** in token

### Identifier/Keyword Recognition
- **Pattern:** `[a-zA-Z_][a-zA-Z0-9_]*`
- **Keywords checked first:** def, extern, return, if, else
- **Case sensitive:** `Def` is an identifier, not a keyword

### Memory Management
- **Tokens are heap-allocated** (malloc)
- **String values are duplicated** (must be freed)
- **Caller must free tokens** after use
- **Pattern:** create → use → free

## Current Status

✅ **Complete:**
- Data structures defined
- Token types enumerated
- Character operations implemented
- Identifier tokenization
- Number tokenization (basic)
- Keyword recognition
- Comment handling
- Memory management functions
- Debug utilities

⏸️ **Not Yet Implemented:**
- String literal support
- Multi-character operators (==, !=, <=, >=, ->)
- Error reporting (just returns tokens, no errors)
- More robust number parsing
- Indentation tracking (Python-style)

## Testing Strategy

### Test 1: Basic Keywords
```python
# test_lexer_basic.pyxc
def main() -> i32:
    source: i8* = "def extern return if else"
    # Should tokenize to: tok_def, tok_extern, tok_return, tok_if, tok_else
```

### Test 2: Simple Function
```python
# test_lexer_function.pyxc
def main() -> i32:
    source: i8* = "def add(x, y):\n    return x + y\n"
    # Should tokenize full function definition
```

### Test 3: Numbers
```python
# test_lexer_numbers.pyxc
def main() -> i32:
    source: i8* = "42 3.14 0 100"
    # Should tokenize integers and floats
```

## Next Steps

1. **Compile lexer.pyxc** with chapter27 pyxc
   ```bash
   cd code/chapter27
   ./build/pyxc ../../selfhost/lexer.pyxc -o lexer_test
   ```

2. **Run lexer test** to verify tokenization
   ```bash
   ./lexer_test
   ```

3. **Fix any compilation errors** in lexer.pyxc

4. **Create comprehensive tests** for edge cases

5. **Move to Phase 2** once lexer is solid

## Known Issues

### Syntax Compatibility
- ⚠️ Pyxc doesn't support ternary `if-then-else` expressions
- ⚠️ Must use `elif` carefully (not always supported)
- ✅ Fixed: Changed inline if to regular if/else blocks

### Number Parsing
- ⚠️ Simplified parser may not handle edge cases
- ⚠️ No support for scientific notation (1e10)
- ⚠️ No support for hex/binary literals

### Error Handling
- ⚠️ No error messages yet
- ⚠️ Invalid input just produces unexpected tokens
- 📋 TODO: Add error reporting with line/column info

## Lessons Learned

1. **Test as you go** - Don't write 400 lines without compiling
2. **Check syntax compatibility** - Pyxc is not full Python
3. **Memory management is manual** - Every malloc needs a free
4. **Extern declarations are key** - Bridge to C libraries works well

## Phase 1 Completion Checklist

- [x] Define data structures
- [x] Implement character operations
- [x] Implement identifier tokenization
- [x] Implement number tokenization
- [x] Implement keyword recognition
- [x] Add memory management
- [x] Add debug utilities
- [ ] **Compile lexer.pyxc successfully**
- [ ] **Run and verify tokenization**
- [ ] Create comprehensive tests
- [ ] Document any issues found
- [ ] Ready for Phase 2 (AST)

**Phase 1 is ~80% complete.** Once lexer.pyxc compiles and runs successfully, we can move to Phase 2!

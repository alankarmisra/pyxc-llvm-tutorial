# Python-Style Indentation Implementation

## Overview
This document explains the changes made to add Python-style indentation to your PyxcJIT compiler. All changes are marked with `// ## CLAUDE_START:` and `// ## CLAUDE_END` comments for easy identification.

## Key Changes

### 1. New Token Types (Line 70-73)
Added two new tokens to handle indentation:
- `tok_indent` (-15): Emitted when indentation increases
- `tok_dedent` (-16): Emitted when indentation decreases

### 2. Indentation Tracking Variables (Line 78-82)
Added global variables to track indentation state:
- `IndentStack`: Stack tracking nested indentation levels (initialized with 0)
- `PendingDedents`: Queue for dedents that need to be emitted
- `CurrentIndent`: Current line's indentation level (in spaces)
- `AtLineStart`: Flag indicating if we're at the start of a new line

### 3. Modified `gettok()` Function

#### Pending Dedents Handler (Line 103-109)
When multiple dedentation levels occur, we queue them up and emit one dedent token at a time on subsequent calls.

#### Indentation Processing at Line Start (Line 113-164)
The main indentation logic:

1. **Count Indentation**: Count spaces/tabs at line start (tabs = 4 spaces)
2. **Skip Blank Lines**: Ignore empty lines and comment-only lines
3. **EOF Handling**: Generate dedents for all remaining indentation when EOF is reached
4. **Compare Indentation Levels**:
   - **Increased**: Push new level to stack, emit `tok_indent`
   - **Decreased**: Pop levels and queue dedents until matching level found
   - **Same**: No token emitted, continue parsing
5. **Error Detection**: Detect mismatched dedentation levels

#### Line Start Flag Updates (Line 179, 234)
Set `AtLineStart = true` when encountering newlines or after comments.

### 4. New Parser Helper Functions

#### `ExpectIndent()` (Line 326-333)
Expects and consumes an INDENT token after eating newlines. Used when entering indented blocks (if/else, for, function bodies, etc.).

#### `ExpectDedent()` (Line 337-344)
Expects and consumes a DEDENT token after eating newlines. Used when exiting indented blocks.

### 5. Updated Parser Functions

All control structures now enforce indentation:

#### `ParseIfExpr()` (Line 349-427)
- After `if condition:` → Expect INDENT
- After then block → Expect DEDENT
- After `else:` → Expect INDENT  
- After else block → Expect DEDENT

#### `ParseForExpr()` (Line 432-502)
- After `for x in range(...):` → Expect INDENT
- After loop body → Expect DEDENT

#### `ParseVarExpr()` (Line 507-556)
- After `var x = ... in` → Expect INDENT
- After var body → Expect DEDENT

#### `ParseDefinition()` (Line 699-779)
- After `def function(...):` → Expect INDENT
- After function body → Expect DEDENT

## How It Works

### Example Code:
```python
def factorial(n):
    if n < 2:
        return 1
    else:
        return n * factorial(n - 1)
```

### Token Flow:
1. `def` → `identifier(factorial)` → `(` → `identifier(n)` → `)` → `:`
2. `EOL` → `INDENT` (body starts)
3. `if` → `identifier(n)` → `<` → `number(2)` → `:`
4. `EOL` → `INDENT` (then block)
5. `return` → `number(1)`
6. `EOL` → `DEDENT` (exit then block)
7. `else` → `:`
8. `EOL` → `INDENT` (else block)
9. `return` → ...
10. `EOL` → `DEDENT` (exit else block)
11. `EOL` → `DEDENT` (exit function body)

## Important Notes

1. **Tab Handling**: Tabs are converted to 4 spaces for consistency
2. **Blank Lines**: Empty lines and comment-only lines don't affect indentation
3. **Indentation Errors**: Mismatched dedentation levels are reported but recovery is attempted
4. **EOF Handling**: All open indentation levels are automatically closed at EOF
5. **Consistency**: All indentation must be consistent (can't mix indent levels arbitrarily)

## Testing Recommendations

Test with:
1. Simple single-level indentation
2. Nested control structures (if inside if, for inside if, etc.)
3. Mixed tabs and spaces (should work but recommend using only spaces)
4. Indentation errors (mismatched dedents)
5. Comments within indented blocks
6. Empty lines within indented blocks

## Limitations

- This implementation uses a simple space-counting approach
- Tab stops are fixed at 4 spaces
- No support for mixing tabs and spaces with different interpretations
- REPL mode may require careful handling of multi-line input

## Future Enhancements

Consider adding:
1. Configurable tab width
2. Better error messages with line numbers
3. Stricter tab/space mixing detection
4. Support for continuation lines (backslash or implicit)
5. Better REPL multi-line input handling
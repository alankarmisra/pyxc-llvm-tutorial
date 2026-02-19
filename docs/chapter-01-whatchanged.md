# Chapter 01 - What Changed

## Features Added
- Implemented lexer/token stream for identifiers, numbers, keywords, operators, comments, and newline handling.
- Added diagnostics for malformed numeric literals.
- Added line-ending handling coverage for LF/CRLF/CR inputs.

## EBNF Notes
- Initial lexer-oriented grammar baseline introduced in `pyxc.ebnf`.
- No prior chapter; this is the implementation baseline.

## Tests Added
- Added `c01_*` tokenization and lexer diagnostics tests.
- Focus areas: keyword tokens, operator tokens, whitespace/comments, newline normalization, number literals, and invalid numeric forms.

## Tests Modified
- None.

## Tests Removed
- None.

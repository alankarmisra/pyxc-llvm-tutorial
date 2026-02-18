# Chapter 05 TODO

- Document that `tok_error` is back (and token enum values shifted) plus the new `HadError` flag set by malformed number literals.
- Explain the lexer change: number literals are validated again with `strtod` + `End` check (and return `tok_error` on failure).
- Explain the newline-diagnostic behavior change: `CurLoc` for `tok_eol` stays at `LexLoc`, and `GetDiagnosticAnchorLoc` now adjusts newline diagnostics to the previous line end.
- Call out the added binary operator precedence for `>` in `BinopPrecedence`.
- Mention `InitializeModule()` and the final `TheModule->print(errs(), nullptr)` dump when `--emit-ir` is enabled at REPL exit.

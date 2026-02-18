# Chapter 02 TODO

- Document the new colorized diagnostics (`UseColor`, `Red`, `Bold`, `Reset`) and the `isatty(fileno(stderr))` gate.
- Explain the newline-token anchoring helpers: `GetNewlineTokenLoc`, `GetDiagnosticAnchorLoc`, and why `CurLoc` is overridden for `tok_eol`.
- Update the error-reporting section to match code: `FormatTokenForMessage` naming, message text, and anchoring location (uses `GetDiagnosticAnchorLoc`).
- Update the main loop description: prompt printed on `tok_eol` in `MainLoop()`, and `main()` now prints the initial prompt and primes `CurTok` via `getNextToken()`.

# Chapter 01 TODO

- Document `tok_error` in the token enum (and that `gettok()` can return it).
- Explain that the REPL silently skips `tok_error` tokens (`MainLoop()` does `continue`).
- Clarify comment handling at EOF: if a `#` comment reaches EOF without a newline, the lexer returns `tok_eof`, not `tok_eol`.

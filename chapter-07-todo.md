# Chapter 07 TODO

- Update the output examples/logic: code prints only the IR (no “Read function definition/extern/top-level expr” prefix) when `--emit-ir` is set, and prints just the numeric result for top-level expressions (no “Evaluated to …” prefix).
- Add a note that the final module-wide IR dump at REPL exit (present in Chapter 5) was removed in Chapter 7.
- Remove or fix the “Preventing Duplicate Definitions” section: the code does **not** reject duplicate definitions in `ParsePrototype()` yet.

# pcompiler (experimental)

This is a tiny experimental compiler written in `pyxc`.

It compiles a very small arithmetic expression language into a stack-machine text format.

## What it supports

- numbers
- `+`, `-`, `*`, `/`
- parentheses
- whitespace skipping

## Output format

Given input:

```text
12 + 3*(4+5) - 7
```

it emits `pcompiler.out` like:

```text
PUSH 12
PUSH 3
PUSH 4
PUSH 5
ADD
MUL
ADD
PUSH 7
SUB
```

## Run

```bash
cd code/chapter27
make
./pyxc -i ../pcompiler/pcompiler.pyxc
cat pcompiler.out
```

Notes:
- This is intentionally a prototype, not self-hosting `pyxc`.
- It demonstrates parser/codegen structure in `pyxc` itself.

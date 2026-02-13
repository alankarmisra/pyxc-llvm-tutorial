# Chapter 2: Types, Operators, and Expressions

This chapter collects the expression-level rules you use every day in Pyxc.

## 2.1 Variable Names

Identifiers begin with a letter or underscore and may contain letters, digits, and underscores.

Examples:

- `fahr`
- `celsius`
- `temp_01`
- `_internal`

## 2.2 Data Model

In current Pyxc, expressions are numeric. Runtime functions interpret numbers as needed (for example, ASCII codes for `putchard`).

## 2.3 Constants

Numeric constants are written directly:

```pyxc
0
1
32
3.14
```

## 2.4 Arithmetic Operators

Arithmetic operators:

- `+`
- `-`
- `*`
- `/`

Unary minus is supported:

```pyxc
extern def printd(x)

printd(-5)
printd(3 + 4 * 2)
printd((3 + 4) * 2)
```

## 2.5 Relational and Truth Conventions

`<` is built in and commonly used in conditions.

Truth convention:

- `0` is false
- non-zero is true

```pyxc
def is_small(x):
    if x < 10:
        return 1
    else:
        return 0
```

## 2.6 Type Conversion in Expressions

Temperature conversion is a practical example of arithmetic conversion style:

```pyxc
def f_to_c(f):
    return (5 * (f - 32)) / 9
```

Use parentheses to keep conversions obvious.

## 2.7 Increment and Decrement Patterns

Pyxc does not use `++` or `--`. Use arithmetic reassignment patterns in loops and bindings:

```pyxc
extern def printd(x)

for i in range(0, 5, 1):
    printd(i)
```

## 2.8 Bitwise and Extended Operators

Core Pyxc includes the built-in operators above. Additional operators can be introduced with decorators.

```pyxc
@unary
def !(v):
    if v:
        return 0
    else:
        return 1

@binary(precedence=10)
def >(LHS, RHS):
    return RHS < LHS
```

## 2.9 Assignment-Like Forms

Pyxc supports expression-local bindings:

```pyxc
def sumsq(a, b):
    return var x = a * a, y = b * b in x + y
```

## 2.10 Conditional Expressions by `if`

Condition-driven value selection is written with `if`/`elif`/`else`:

```pyxc
def sign(x):
    if x < 0:
        return -1
    elif x < 1:
        return 0
    else:
        return 1
```

## 2.11 Precedence and Grouping

When in doubt, add parentheses. It improves readability and avoids subtle mistakes.

```pyxc
extern def printd(x)

printd(2 + 3 * 4)
printd((2 + 3) * 4)
printd((5 - 2) < 4)
```

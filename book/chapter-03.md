# Chapter 3: Control Flow

Useful programs choose among alternatives and repeat computations. This chapter covers those control forms in Pyxc.

## 3.1 Statements and Blocks

Pyxc uses indentation to define blocks.

```pyxc
def absval(x):
    if x < 0:
        return 0 - x
    else:
        return x
```

## 3.2 If-Else

The `if` statement selects one branch:

```pyxc
extern def printd(x)

def classify(n):
    if n < 0:
        return 100
    else:
        return 200

printd(classify(-3))
printd(classify(7))
```

## 3.3 Else-If Chains

Use `elif` for multiple ordered tests:

```pyxc
def grade(x):
    if x < 60:
        return 0
    elif x < 70:
        return 1
    elif x < 80:
        return 2
    elif x < 90:
        return 3
    else:
        return 4
```

## 3.4 For Loops

Pyxc uses `for ... in range(...)` loops:

```pyxc
extern def printd(x)

for i in range(0, 5, 1):
    printd(i)
```

The step argument is optional:

```pyxc
for i in range(0, 5):
    printd(i)
```

## 3.5 Reverse Loops

Use a negative step for countdowns:

```pyxc
extern def printd(x)

for n in range(10, 0, -1):
    printd(n)
```

## 3.6 Nested Control

Control forms compose naturally:

```pyxc
extern def printd(x)

for i in range(0, 3, 1):
    if i < 1:
        printd(11)
    else:
        printd(22)
```

## 3.7 Recursion as Repetition

Recursion is another control mechanism.

```pyxc
extern def printd(x)

def fib(n):
    if n < 3:
        return 1
    else:
        return fib(n - 1) + fib(n - 2)

printd(fib(9))
```

## 3.8 A Control-Flow Example: Conversion Table Driver

```pyxc
extern def printd(x)

def f_to_c(fahr):
    return (5 * (fahr - 32)) / 9

def table(start, stop, step):
    for fahr in range(start, stop, step):
        if fahr < 0:
            printd(0)
        else:
            printd(fahr)
            printd(f_to_c(fahr))
    return 0

table(0, 301, 20)
```

This is the core control toolbox: choice (`if`), repetition (`for`), and structural decomposition (`functions` and recursion).

# Chapter 1: A Tutorial Introduction

The only way to learn a language is to write programs in it. This chapter starts with tiny programs and builds up to useful patterns.

## 1.1 Getting Started

A minimal Pyxc program calls a runtime function:

```pyxc
extern def printd(x)

printd(42)
```

`printd` prints numeric values.

## 1.2 Variables and Arithmetic

Pyxc supports numeric expressions with familiar operators:

```pyxc
extern def printd(x)

def demo():
    return printd((2 + 3) * 4)

demo()
```

## 1.3 The Fahrenheit-Celsius Table

A classic first program is a temperature-conversion table.

```pyxc
extern def printd(x)

# print: fahr, celsius
for fahr in range(0, 301, 20):
    printd(fahr)
    printd((5 * (fahr - 32)) / 9)
```

## 1.4 Symbolic Names

Use helper functions so formulas are not repeated:

```pyxc
extern def printd(x)

def f_to_c(fahr):
    return (5 * (fahr - 32)) / 9

for fahr in range(0, 301, 20):
    printd(fahr)
    printd(f_to_c(fahr))
```

## 1.5 More on Temperature Conversion

Reverse the table by stepping downward:

```pyxc
extern def printd(x)

for fahr in range(300, -1, -20):
    printd(fahr)
    printd((5 * (fahr - 32)) / 9)
```

## 1.6 Arrays (via Computed Values)

Current Pyxc focuses on numeric expressions and loops. A common pattern is to compute and print streams of values:

```pyxc
extern def printd(x)

def squares(n):
    for i in range(0, n, 1):
        printd(i * i)
    return 0

squares(10)
```

## 1.7 Functions

Functions make programs easier to read and test:

```pyxc
extern def printd(x)

def power(base, n):
    if n < 1:
        return 1
    else:
        return base * power(base, n - 1)

printd(power(2, 10))
printd(power(3, 5))
```

## 1.8 Arguments: Call by Value

Arguments are passed by value. A function gets its own local parameter values:

```pyxc
def absval(x):
    if x < 0:
        return 0 - x
    else:
        return x
```

## 1.9 Character Input and Output

Character output uses `putchard` with ASCII codes:

```pyxc
extern def putchard(c)

putchard(72)  # H
putchard(105) # i
putchard(10)  # newline
```

## 1.10 A Small Program Together

```pyxc
extern def printd(x)
extern def putchard(c)

def f_to_c(fahr):
    return (5 * (fahr - 32)) / 9

def main():
    for fahr in range(0, 101, 10):
        printd(fahr)
        printd(f_to_c(fahr))
    putchard(10)
    return 0

main()
```

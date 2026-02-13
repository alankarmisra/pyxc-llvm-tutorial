# Chapter 1 Exercises (Questions and Solutions)

## Question 1
Write a program that prints numbers `0` through `9` using a `for` loop.

### Solution
```pyxc
extern def printd(x)

for i in range(0, 10, 1):
    printd(i)
```

## Question 2
Write `def sign(x):` that returns `-1` if `x < 0`, `0` if `x` is zero, and `1` otherwise.

### Solution
```pyxc
def sign(x):
    if x < 0:
        return -1
    elif x < 1:
        return 0
    else:
        return 1
```

## Question 3
Write a function `sum_to(n)` that computes `1 + 2 + ... + n` recursively.

### Solution
```pyxc
def sum_to(n):
    if n < 1:
        return 0
    else:
        return n + sum_to(n - 1)
```

## Question 4
Write a function `countdown(n)` that prints `n, n-1, ..., 1` and then returns `0`.

### Solution
```pyxc
extern def printd(x)

def countdown(n):
    for i in range(n, 0, -1):
        printd(i)
    return 0
```

## Question 5
Using `putchard`, print the three characters `A`, `B`, and a newline.

### Solution
```pyxc
extern def putchard(c)

putchard(65)
putchard(66)
putchard(10)
```

## Question 6
Define `max2(a, b)` without using any operators other than `<`, `if`, and `return`.

### Solution
```pyxc
def max2(a, b):
    if a < b:
        return b
    else:
        return a
```

## Question 7
Write a function `is_even(n)` that returns `1` for even numbers and `0` for odd numbers. Use recursion by repeatedly subtracting `2`.

### Solution
```pyxc
def is_even(n):
    if n < 0:
        return is_even(0 - n)
    elif n < 1:
        return 1
    elif n < 2:
        return 0
    else:
        return is_even(n - 2)
```

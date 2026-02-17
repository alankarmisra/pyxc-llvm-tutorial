---
description: "A relaxed tour of LLVM IR syntax: understand types, instructions, SSA form, and how IR represents your code without writing any compiler code."
---
# 6. Pyxc: Understanding LLVM IR

## Introduction

In Chapter 5, we generated LLVM IR without stopping to really understand what it looked like. We jumped straight into building it. That worked—you got code running. But now let's slow down and actually read the IR.

This chapter is theory only. No compiling, no building, no code. Just you and the IR, learning to read it like a foreign language. By the end, you'll be able to look at LLVM IR and understand exactly what it's doing.

## What We're Looking At

Here's that simple `add` function again:

```python
def add(a, b):
    return a + b
```

And its LLVM IR:

```llvm
define double @add(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```

Let's break this down piece by piece so the syntax feels less mysterious.

## The Function Signature

```llvm
define double @add(double %a, double %b) {
```

**`define`** - This is a function *definition* (not just a declaration). It has a body.

**`double`** - The return type. This function returns a 64-bit floating-point number.

**`@add`** - The function name. The `@` prefix means "global symbol." Global symbols are visible across the entire module (file). Function names always start with `@`.

**`double %a, double %b`** - Two parameters, both doubles. The `%` prefix means "local value." Local values exist only inside this function. Parameter names always start with `%`.

So reading left to right: "Define a function that returns a double, called `add`, taking two double parameters `a` and `b`."

## The Entry Block

```llvm
entry:
```

This is a **basic block** label. A basic block is a sequence of instructions with no branches—once you enter, you execute straight through to the end. The name `entry` is conventional (you could call it anything), but it clearly marks where the function starts.

Every function needs at least one basic block. When we add `if` statements and loops later, we'll have multiple blocks with jumps between them.

## The Instruction

```llvm
  %addtmp = fadd double %a, %b
```

This line does the actual work. Let's dissect it:

**`%addtmp =`** - Create a new local value named `addtmp` to hold the result.

**`fadd`** - The instruction name. "Floating-point add."

**`double`** - The type of the operands (and the result).

**`%a, %b`** - The two values to add (our function parameters).

Reading it: "Add the double values `%a` and `%b`, and store the result in a new local value called `%addtmp`."

## The Return

```llvm
  ret double %addtmp
}
```

**`ret`** - Return from the function.

**`double %addtmp`** - Return this double value.

The closing `}` ends the function definition.

## Type Annotations Everywhere

LLVM is **strongly typed**—every value must have a type. Notice how `double` appears multiple times:

```llvm
define double @add(double %a, double %b) {
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```

This redundancy feels verbose, but it's intentional. LLVM can verify that you're not adding an integer to a pointer, or returning the wrong type. Type errors are caught early.

## Instruction Types: `fadd` vs `add`

LLVM has different instructions for different data types:

**Floating-point operations** (for `float`, `double`):
- `fadd` - floating-point addition
- `fsub` - floating-point subtraction
- `fmul` - floating-point multiplication
- `fdiv` - floating-point division
- `frem` - floating-point remainder (modulo)
- `fcmp` - floating-point comparison

**Integer operations** (for `i32`, `i64`, etc.):
- `add` - integer addition
- `sub` - integer subtraction
- `mul` - integer multiplication
- `sdiv` - signed integer division
- `udiv` - unsigned integer division
- `srem` - signed remainder
- `urem` - unsigned remainder
- `icmp` - integer comparison

Notice the pattern: `f` prefix for floating-point, no prefix (or `s`/`u` for signed/unsigned) for integers.

**Why separate instructions?** Because floating-point and integer math work differently at the hardware level. Floating-point addition handles NaN, infinity, and rounding modes. Integer addition is simpler but wraps on overflow.

## The `%` and `@` Prefixes

This is the most important syntax rule to remember:

**`%name`** - Local value (function parameter, instruction result)
- Lives only inside one function
- Examples: `%a`, `%b`, `%addtmp`, `%result`, `%1`, `%2`

**`@name`** - Global symbol (function name, global variable)
- Visible across the entire module
- Examples: `@add`, `@main`, `@factorial`, `@sin`

You can tell at a glance what's local and what's global just by looking at the prefix.

## SSA Form: The Core Idea

Here's where LLVM gets interesting. Look at this Python code:

```python
i = 5
i = 10
print(i)
```

Normal code lets you reassign variables. But LLVM IR doesn't. It uses **Static Single Assignment (SSA)** form, where every value is assigned exactly once.

The same logic in LLVM IR looks like:

```llvm
  %i.1 = 5
  %i.2 = 10
  call @print(%i.2)
```

Each "assignment" creates a new SSA value. `%i.1` is defined once. `%i.2` is defined once. They never change.

### A More Complex Example

```python
i = 5
i = 10
result = fib(i)
i = i + 1
result2 = fib(i)
j = i
```

In SSA form:

```llvm
  %i.1 = 5
  %i.2 = 10
  %result = call @fib(double %i.2)
  %i.3 = fadd double %i.2, 1.0
  %result2 = call @fib(double %i.3)
  %j = %i.3
```

Notice:
- `%i.1 = 5` is never used (dead code—a compiler can delete it)
- `%i.2 = 10` is used twice (by the first `fib` call and the `fadd`)
- `%i.3 = %i.2 + 1` is used twice (by the second `fib` call and the assignment to `%j`)

Each value has exactly one definition, but can have many uses.

## Why SSA? The Optimization Angle

SSA makes life easy for optimizers. Consider this question: "Is this value ever used?"

**Without SSA:**
```c
int x = 5;
x = 10;
x = x + 1;
return x;
```
To know if `x = 5` matters, you have to track all assignments to `x` and figure out which one "wins." That's hard.

**With SSA:**
```llvm
  %x.1 = 5
  %x.2 = 10
  %x.3 = add i32 %x.2, 1
  ret i32 %x.3
```
Is `%x.1` used? No uses → delete it. Done. SSA makes dataflow explicit.

### Common Optimizations Enabled by SSA

**Dead Code Elimination:**
```llvm
  %unused = fadd double %a, %b  ; never used anywhere
  ret double %a
```
The optimizer sees `%unused` has zero uses. Delete the instruction.

**Copy Propagation:**
```llvm
  %temp = %x
  %result = fadd double %temp, %y
```
Since `%temp` is just a copy of `%x`, replace all uses:
```llvm
  %result = fadd double %x, %y
```

**Constant Propagation:**
```llvm
  %x = 5
  %y = add i32 %x, 10
  %z = add i32 %y, 3
```
Since `%x` is always 5, and `%y` is always 15, we can fold this:
```llvm
  %z = 18
```

All of these optimizations are trivial with SSA because you can directly see definitions and uses.

## The `%addtmp` Question Answered

Back to our original function:

```llvm
define double @add(double %a, double %b) {
entry:
  %addtmp = fadd double %a, %b
  ret double %addtmp
}
```

**Why not just write:** `ret fadd double %a, %b` ?

Because SSA requires every instruction that produces a value to **define a named SSA value**. The `fadd` instruction produces a result, so it must be assigned to a name (`%addtmp`). Then `ret` uses that name.

It's verbose, but it makes everything explicit. The optimizer can see exactly where `%addtmp` is defined and where it's used.

**Is `addtmp` a keyword?** No. It's just a hint we chose. We could use:
- `%result`
- `%sum`
- `%x`
- `%0` (numbers work too)

LLVM doesn't care. The name is for readability. If you create multiple values with the same hint, LLVM automatically adds suffixes:

```llvm
  %addtmp = fadd double %a, %b
  %addtmp1 = fadd double %addtmp, %c
  %addtmp2 = fadd double %addtmp1, %d
```

This keeps every SSA value unique.

## A Slightly Bigger Example

Let's look at a more interesting function:

```python
def average(x, y):
    return (x + y) / 2.0
```

LLVM IR:

```llvm
define double @average(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  %divtmp = fdiv double %addtmp, 2.0
  ret double %divtmp
}
```

Reading it step by step:

1. **`%addtmp = fadd double %x, %y`** - Add `x` and `y`, call the result `%addtmp`
2. **`%divtmp = fdiv double %addtmp, 2.0`** - Divide `%addtmp` by the constant `2.0`, call the result `%divtmp`
3. **`ret double %divtmp`** - Return `%divtmp`

Notice that constants (like `2.0`) don't need a `%` prefix. They're not values in the SSA sense—they're literal numbers embedded in the instruction.

## Comparison: `fcmp` Returns Integers

```python
def less_than(a, b):
    return a < b
```

LLVM IR:

```llvm
define double @less_than(double %a, double %b) {
entry:
  %cmptmp = fcmp ult double %a, %b
  %booltmp = uitofp i1 %cmptmp to double
  ret double %booltmp
}
```

**Line 1:** `%cmptmp = fcmp ult double %a, %b`
- `fcmp` - floating-point comparison
- `ult` - "unordered or less than" (handles NaN correctly)
- Result type: `i1` (a 1-bit integer: 0 or 1)

**Line 2:** `%booltmp = uitofp i1 %cmptmp to double`
- `uitofp` - unsigned integer to floating point
- Converts `i1` (0 or 1) to `double` (0.0 or 1.0)

**Why the conversion?** Because our language (Pyxc) uses doubles for everything, including booleans. So `a < b` must return a double, not an integer.

## Function Calls

```python
def compute(x):
    return sin(x) + cos(x)
```

LLVM IR:

```llvm
define double @compute(double %x) {
entry:
  %calltmp = call double @sin(double %x)
  %calltmp1 = call double @cos(double %x)
  %addtmp = fadd double %calltmp, %calltmp1
  ret double %addtmp
}
```

**`call double @sin(double %x)`** - Call the function `@sin` with argument `%x`, expecting a `double` back.

Notice the `@sin` function doesn't need to be defined in this file. It could be:
- Defined elsewhere in the module
- An `extern` declaration (we'll see those next)
- A standard library function

LLVM trusts that you'll link it properly.

## External Declarations

```python
extern def sin(x)
extern def cos(x)
```

LLVM IR:

```llvm
declare double @sin(double)
declare double @cos(double)
```

**`declare`** - This function is defined *elsewhere* (not in this module). Just tell LLVM the signature so it knows how to call it.

Compare to `define`, which includes the function body. `declare` is header-only.

## A Complete Module

Putting it all together, a module might look like:

```llvm
declare double @sin(double)
declare double @cos(double)

define double @average(double %x, double %y) {
entry:
  %sum = fadd double %x, %y
  %result = fdiv double %sum, 2.0
  ret double %result
}

define double @compute(double %x) {
entry:
  %s = call double @sin(double %x)
  %c = call double @cos(double %x)
  %result = fadd double %s, %c
  ret double %result
}
```

The module contains:
- Two external declarations (`sin`, `cos`)
- Two function definitions (`average`, `compute`)

## Control Flow Preview: Basic Blocks

We haven't added `if` or loops yet, but here's a sneak peek. This code:

```python
def max(a, b):
    if a > b:
        return a
    else:
        return b
```

Becomes multiple basic blocks:

```llvm
define double @max(double %a, double %b) {
entry:
  %cmp = fcmp ogt double %a, %b
  br i1 %cmp, label %if.then, label %if.else

if.then:
  ret double %a

if.else:
  ret double %b
}
```

Notice:
- **`entry:`** - First block
- **`if.then:`** - Block for the `if` branch
- **`if.else:`** - Block for the `else` branch
- **`br`** - Branch instruction (conditional jump)

We'll cover this in detail later. For now, just notice that blocks are labeled, and you jump between them with `br`.

## Phi Nodes: SSA's Secret Weapon

What if both branches assign to the same variable?

```python
def abs(x):
    if x < 0:
        result = -x
    else:
        result = x
    return result
```

How do we write this in SSA, where values can't be reassigned?

```llvm
define double @abs(double %x) {
entry:
  %cmp = fcmp olt double %x, 0.0
  br i1 %cmp, label %if.then, label %if.else

if.then:
  %neg = fsub double 0.0, %x
  br label %merge

if.else:
  br label %merge

merge:
  %result = phi double [ %neg, %if.then ], [ %x, %if.else ]
  ret double %result
}
```

**`phi`** - "Pick the right value based on which block we came from."

The `phi` instruction says:
- If we came from `%if.then`, use `%neg`
- If we came from `%if.else`, use `%x`

This is how SSA handles control flow merges. We'll see more of this in Chapter 7.

## Reading LLVM IR Like a Pro

By now, you should be able to read LLVM IR and understand:

1. **Function signatures** - `define` vs `declare`, return types, parameters
2. **Basic blocks** - labeled chunks of straight-line code
3. **Instructions** - `fadd`, `fsub`, `call`, `ret`, etc.
4. **SSA values** - `%` for local, `@` for global
5. **Types** - `double`, `i32`, `i1`, etc.

When you see generated IR, you can trace through it instruction by instruction and understand what the program does.

## Why This Matters

You might wonder: "Why spend a whole chapter on syntax?"

Because **reading IR is debugging**. When your compiler generates wrong code, you'll need to:
1. Look at the IR it generated
2. Spot the mistake
3. Fix the codegen logic

If you can't read IR, you're flying blind. Now you can read it. In later chapters, when things break, you'll know how to inspect the IR and figure out what went wrong.

## Handwritten IR (Don't Do This)

Could you write LLVM IR by hand, as a text file?

Yes. You could:
1. Write a `.ll` file with LLVM IR syntax
2. Compile it with `llc` (LLVM's static compiler)
3. Link it
4. Run it

But this is tedious and error-prone. You'd have to:
- Manually track every SSA value name
- Ensure types match everywhere
- Handle phi nodes correctly
- Number your basic blocks

That's why we use LLVM's APIs (like `IRBuilder`). They handle the bookkeeping. You say "create an add instruction," and LLVM generates the correct IR with proper SSA names, types, and structure.

## Summary

LLVM IR is:
- **Strongly typed** - Every value has an explicit type
- **SSA form** - Every value is defined exactly once
- **Explicit** - All operations are spelled out (no hidden conversions)
- **Modular** - Functions, basic blocks, instructions

You now know:
- How to read function signatures (`define`, `declare`)
- What `%` and `@` mean
- How SSA values work
- Common instructions (`fadd`, `call`, `ret`)
- Why SSA makes optimization easy

In the next chapters, we'll go back to writing code that *generates* this IR. But now when you see the IR output, you'll understand exactly what it means.

## Further Reading

If you want to dive deeper into LLVM IR:
- [LLVM Language Reference Manual](https://llvm.org/docs/LangRef.html) - Complete IR specification
- [LLVM Programmer's Manual](https://llvm.org/docs/ProgrammersManual.html) - How to use LLVM APIs
- [LLVM's Analysis and Transform Passes](https://llvm.org/docs/Passes.html) - What optimizations LLVM can do

But don't feel like you need to read those now. You know enough to continue building the compiler.

## Need Help?

Stuck on something? Have questions about this chapter? Found an error?

- **Open an issue:** [GitHub Issues](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues) - Report bugs, errors, or problems
- **Start a discussion:** [GitHub Discussions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions) - Ask questions, share tips, or discuss the tutorial
- **Contribute:** Found a typo? Have a better explanation? [Pull requests](https://github.com/alankarmisra/pyxc-llvm-tutorial/pulls) are welcome!

**When reporting issues, please include:**
- The chapter you're working on
- Your platform (e.g., macOS 14 M2, Ubuntu 24.04, Windows 11)
- The complete error message or unexpected behavior
- What you've already tried

The goal is to make this tutorial work smoothly for everyone. Your feedback helps improve it for the next person!

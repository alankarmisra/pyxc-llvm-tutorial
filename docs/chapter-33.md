---
description: "Complete pyxc's loop story: while, do/while, break, and continue, with correct targets for nested loops and for loops."
---
# 33. pyxc: Loop Completeness

## Where We Are

[Chapter 32](chapter-32.md) added logical operators. pyxc has had `for` loops since [Chapter 8](chapter-08.md), but that is the only loop form. After this chapter, `while` and `do/while` join the language, and `break` and `continue` work correctly across nested loops:

```pyxc
extern def printd(x: float64)

def collatz(n: int) -> int:
  var x: int = n
  var steps: int = 0
  while x != 1:
    if x % 2 == 0:
      x /= 2
    else:
      x = x * 3 + 1
    steps++
  return steps

def main() -> int:
  printd(float64(collatz(27)))
  return 0
```

```
111.000000
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-33
```

## Grammar

`whilestmt` and `dowhilestmt` join `compoundstmt`. `break` and `continue` are added to `simplestmt` as standalone statements.

```ebnf
whilestmt    = "while" expression ":" suite ;                    -- new
dowhilestmt  = "do" ":" suite eols "while" expression ;          -- new
compoundstmt = ifstmt | forstmt | whilestmt | dowhilestmt ;      -- changed
simplestmt   = returnstmt | breakstmt | continuestmt
             | varstmt | assignstmt | expression ;               -- changed
breakstmt    = "break" ;                                          -- new
continuestmt = "continue" ;                                       -- new
```

Note the `do/while` form: the body comes first (indented under `do:`), the condition appears after `while` on a separate line without a trailing colon.

### Full Grammar

`code/chapter-33/pyxc.ebnf`

```ebnf
program         = [ eols ] [ top { eols top } ] [ eols ] ;
eols            = eol { eol } ;
top             = typealias | traitdef | structdef | classdef | impldef | definition | decorateddef | external | toplevelexpr ;
typealias       = "type" identifier "=" type ;
traitdef        = "trait" identifier [ "[" identifier "]" ] ":" eols traitblock ;
traitblock      = indent traitmethodsig { eols traitmethodsig } dedent ;
traitmethodsig  = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ;
structdef       = "struct" identifier ":" eols structblock ;
classdef        = "class" identifier [ "(" traitref { "," traitref } ")" ] ":" eols structblock ;
traitref        = identifier [ "[" type "]" ] ;
impldef         = "impl" traitref "for" identifier ":" eols implblock ;
implblock       = indent implmethod { eols implmethod } dedent ;
implmethod      = "def" identifier "(" [ typedparam { "," typedparam } ] ")" [ "->" type ] ":" ( simplestmt | eols block ) ;
structblock     = indent classmember { eols classmember } dedent ;
classmember     = [ visibility ] ( fielddecl | methoddef ) ;
visibility      = "public" | "private" ;
methoddef       = "def" identifier "(" [ typedparam { "," typedparam } ] ")"
                  [ "->" type ] ":" ( simplestmt | eols block ) ;
fielddecl       = identifier ":" type ;
definition      = "def" prototype [ "->" type ] ":" ( simplestmt | eols block ) ;
decorateddef    = binarydecorator eols "def" binaryopprototype [ "->" type ] ":" ( simplestmt | eols block )
                | unarydecorator  eols "def" unaryopprototype  [ "->" type ] ":" ( simplestmt | eols block ) ;
binarydecorator = "@" "binary" "(" integer ")" ;
unarydecorator  = "@" "unary" ;
binaryopprototype = customopchar "(" typedparam "," typedparam ")" ;
unaryopprototype  = customopchar "(" typedparam ")" ;
external        = "extern" "def" prototype [ "->" type ] ;
toplevelexpr    = expression ;
prototype       = identifier "(" [ typedparam { "," typedparam } ] ")" ;
typedparam      = identifier ":" type ;
ifstmt          = "if" expression ":" suite
                [ eols "else" ":" suite ] ;
whilestmt       = "while" expression ":" suite ;
dowhilestmt     = "do" ":" suite eols "while" expression ;
forstmt         = "for"
                  ( "var" identifier ":" type | identifier )
                  "=" expression "," expression "," expression ":" suite ;
varstmt         = "var" varbinding { "," varbinding } ;
assignstmt      = lvalue assignop expression ;
simplestmt      = returnstmt | breakstmt | continuestmt | varstmt | assignstmt | expression ;
compoundstmt    = ifstmt | forstmt | whilestmt | dowhilestmt ;
statement       = simplestmt | compoundstmt ;
suite           = simplestmt | compoundstmt | eols block ;
returnstmt      = "return" [ expression ] ;
breakstmt       = "break" ;
continuestmt    = "continue" ;
block           = indent statement { eols statement } dedent ;
expression      = unaryexpr binoprhs ;
binoprhs        = { binaryop unaryexpr } ;
lvalue          = identifier | fieldaccess | indexexpr ;
varbinding      = identifier ":" type [ "=" expression ] ;
unaryexpr       = unaryop unaryexpr | postfixexpr ;
unaryop         = "-" | "!" | "++" | "--" | userdefunaryop ;
postfixexpr     = primary [ postfixop ] ;
postfixop       = "++" | "--" ;
primary         = castexpr | sizeofexpr | addrexpr | arrayliteral | stringliteral | identifierexpr | fieldaccess | indexexpr | numberexpr | bool_literal | parenexpr ;
castexpr        = casttype "(" expression ")" ;
sizeofexpr      = "sizeof" "(" type ")" ;
addrexpr        = "addr" "(" lvalue ")" ;
identifierexpr  = identifier | callexpr | methodcallexpr | ctorcallexpr ;
callexpr        = identifier "(" [ expression { "," expression } ] ")" ;
methodcallexpr  = identifier "." identifier "(" [ expression { "," expression } ] ")" ;
ctorcallexpr    = identifier "(" [ expression { "," expression } ] ")" ;
fieldaccess     = identifier "." identifier { "." identifier } ;
indexexpr       = identifier "[" expression "]" ;
numberexpr      = number ;
arrayliteral    = "[" [ expression { "," expression } ] "]" ;
stringliteral   = "\"" { ? any char except " and newline ? | escape } "\"" ;
escape          = "\\" ( "\\" | "\"" | "n" | "t" | "0" ) ;
parenexpr       = "(" expression ")" ;
binaryop        = builtinbinaryop | userdefbinaryop ;
indent          = INDENT ;
dedent          = DEDENT ;

assignop        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
builtinbinaryop = "+" | "-" | "*" | "/" | "%"
                | "<" | "<=" | ">" | ">=" | "==" | "!="
                | "&&" | "||" ;
userdefbinaryop = ? any opchar defined as a custom binary operator ? ;
userdefunaryop  = ? any opchar defined as a custom unary operator ? ;
customopchar    = ? any opchar that is not "-" or a builtinbinaryop,
                    and not already defined as a custom operator ? ;
opchar          = ? any single ASCII punctuation character ? ;
identifier      = (letter | "_") { letter | digit | "_" } ;
builtintype     = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | "None" ;
aliastype       = identifier ;
structtype      = identifier ;
pointertype     = "ptr" "[" type "]" ;
type            = basetype [ arraysuffix ] ;
basetype        = builtintype | aliastype | structtype | pointertype ;
arraysuffix     = "[" integer "]" ;
casttype        = "int" | "int8" | "int16" | "int32" | "int64"
                | "float" | "float32" | "float64"
                | "bool" | pointertype ;
integer         = digit { digit } ;
number          = digit { digit } [ "." { digit } ]
                | "." digit { digit } ;
bool_literal    = "True" | "False" ;
letter          = "A".."Z" | "a".."z" ;
digit           = "0".."9" ;
eol             = "\r\n" | "\r" | "\n" ;
ws              = " " | "\t" ;
INDENT          = ? synthetic token emitted by lexer ? ;
DEDENT          = ? synthetic token emitted by lexer ? ;
```

## `while` Loop

A `while` loop evaluates the condition first. If the condition is false on entry, the body never runs.

Codegen produces three basic blocks:

```
while_cond:   evaluate condition → branch to while_body or while_after
while_body:   run body → branch back to while_cond
while_after:  continue here after exit
```

The condition must be `bool`. The `while` condition uses the same type check as `if`.

## `do/while` Loop

A `do/while` loop runs the body first, then checks the condition. The body always executes at least once.

```pyxc
var i: int = 0
do:
  i += 1
while i < 5
```

The same `WhileExprAST` node handles both forms via an `IsDoWhile` flag. With `IsDoWhile`, codegen branches directly to the body block on entry, then falls through to condition evaluation after the body:

```
while_body:   run body → fall to while_cond
while_cond:   evaluate condition → branch to while_body or while_after
while_after:  continue here after exit
```

## `break` and `continue`

`break` exits the innermost enclosing loop. `continue` skips to the next iteration of the innermost enclosing loop.

At parse time, a depth counter (`ParseLoopDepth`) tracks whether the parser is inside a loop. `break` or `continue` outside any loop is a parse error.

At codegen time, two stacks track the current targets:

| Stack | Used by |
|---|---|
| `BreakTargetStack` | `break` — points to the block after the loop |
| `LoopControlStack` | `continue` — points to the condition (while) or step (for) block |

Every loop pushes to both stacks on entry and pops on exit. `break` branches to `BreakTargetStack.back()`. `continue` branches to `LoopControlStack.back().ContinueTarget`.

### `continue` in a `for` loop

The existing `for` loop is updated in this chapter. Previously, the step expression was evaluated inline at the end of the body. Now it has a dedicated `StepBB` basic block. `continue` inside a `for` loop jumps to `StepBB`, which runs the step and then falls to the condition check — the same semantics as C.

### Nesting

The stacks make nesting correct automatically. The innermost loop always sits on top:

```pyxc
while outer_cond:       # push outer targets
  while inner_cond:     # push inner targets
    if done:
      break             # exits inner loop (top of BreakTargetStack)
    continue            # continues inner loop (top of LoopControlStack)
  # inner popped; outer is now on top again
```

### Unreachable code after `break`/`continue`

The block codegen stops emitting statements once the current basic block has a terminator. Any statements written after `break` or `continue` in the same block are silently skipped — they do not appear in the IR.

## Error Cases

**`break` outside a loop:**
```pyxc
def main() -> int:
  break  # Error: 'break' used outside of a loop or switch
  return 0
```

**`continue` outside a loop:**
```pyxc
def main() -> int:
  continue  # Error: 'continue' used outside of a loop
  return 0
```

## Things Worth Knowing

**`do/while` uses the same AST node as `while`.** `WhileExprAST` has an `IsDoWhile` flag. The only structural difference in the IR is which block is the entry target.

**`continue` target differs between loop types.** In a `while` loop, `continue` goes to the condition block. In a `for` loop, `continue` goes to the step block. The `LoopControlStack` stores the right target per loop — you do not need to think about it when writing pyxc code.

**The loop condition must be `bool`.** There is no implicit `int → bool` coercion. Use an explicit comparison: `while n != 0:` not `while n:`.

## What's Next

[Chapter 34](chapter-34.md) adds bitwise operators: `&`, `|`, `^`, `<<`, `>>`, and `~`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

We'll figure it out.

---
section: "Expression and Mutation Conveniences"
description: "Allow assignment inside an expression so patterns like while (c = getchar()) != EOF work without a separate priming read."
---
# 34. pyxc: Assignment as Expression

## What I Am Building

[Chapter 33](chapter-33.md) added variadic `extern def` declarations, completing real C interop. pyxc can call `getchar()`, but the canonical K&R idiom for reading until EOF still doesn't compile:

```pyxc
# What I want to write:
while (c = getchar()) != EOF:
    ...
```
```
Error (Line 5, Column 12): expected ')'
  while (c = 
           ^~~~
```

The problem is that `=` is a statement in pyxc — it can't appear inside an expression like a `while` condition. After this chapter it can:

```pyxc
extern def getchar() -> int32
extern def printd(x: float64)

var EOF: int32 = -1

def main() -> int:
  var c: int32
  var blanks: int
  while (c = getchar()) != EOF:
    if c == ' ':
      blanks = blanks + 1
  printd(float64(blanks))
  return 0
```

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-34
```

## Grammar

I rename `expression` to `assignment` and make it optionally recurse on itself after an assignment operator. Every `expression` in the grammar now means "possibly an assignment":

`code/chapter-34/pyxc.ebnf`

```grammardiff
*...
*variable-statement                = "var" variable-binding
*                                    { "," variable-binding } ;
-assignment-statement              = lvalue "=" expression ;
*simple-statement                  = return-statement
*                                    | break-statement
*                                    | continue-statement
*                                    | variable-statement
-                                    | assignment-statement
*                                    | expression ;
*compound-statement                = if-statement
*...
*block                             = indent statement
*                                    { statement-separator statement } dedent ;
-expression                        = logical-or ;
+expression                        = assignment ;
+assignment                        = logical-or [ "=" assignment ] ;
*logical-or                        = logical-and { "||" logical-and } ;
*logical-and                       = bitwise-or { "&&" bitwise-or } ;
*...
```

Assignment sits at the loosest level, below every operator tier. `assignment-statement` (assignment as its own statement, one per line) is unchanged — it still works exactly as before.

## Tail Assignment Check

I add a new grammar tier, `ParseAssignment`, that sits below `ParseExpression` and above `ParseLogicalOr` — the top of the fixed-tier chain from Chapter 22. It parses one full `logical-or`, then checks whether an assignment operator follows; if so, it treats what it just parsed as an lvalue and parses the right-hand side by recursing into itself:

```cpp
static unique_ptr<ExpressionNode> ParseAssignment() {
  auto Left = ParseLogicalOr();
  if (!Left)
    return nullptr;
  if (CurrentToken != tok_assign)
    return Left;
  if (!Left->isLValue())
    return LogErrorExpression("Assignment target must be assignable");

  ValueType LeftType = Left->getType();
  string LeftTypeInfo = Left->getStructName();
  getNextToken(); // eat '='

  ExpectedLiteralTypeGuard Guard(LeftType, LeftTypeInfo);
  auto Right = ParseAssignment();
  if (!Right)
    return nullptr;

  if (LeftType == ValueType::Array)
    return LogErrorExpression("Type mismatch in assignment");
  if (LeftType == ValueType::Pointer &&
      Right->getType() == ValueType::Array) {
    if (!ArrayDecaysToPointerType(Right->getStructName(), LeftTypeInfo))
      return LogErrorExpression("Type mismatch in assignment");
  } else {
    if (!IsAssignable(LeftType, Right->getType()))
      return LogErrorExpression("Type mismatch in assignment");
    if ((LeftType == ValueType::Struct || LeftType == ValueType::Pointer) &&
        LeftTypeInfo != Right->getStructName())
      return LogErrorExpression("Type mismatch in assignment");
  }

  return make_unique<AssignmentExpressionNode>(
      std::move(Left), std::move(Right), LeftType, LeftTypeInfo);
}

/// expression
///   = assignment ;
static unique_ptr<ExpressionNode> ParseExpression() {
  return ParseAssignment();
}
```

`ExpectedLiteralTypeGuard` propagates the lvalue's type into the right-hand parse, so `x = 5` when `x` is `int32` treats `5` as `int32` without needing an explicit cast.

Since `Left` is a full `logical-or` — everything up through comparisons — before I ever check for `=`, a bare `c = getchar() != EOF` doesn't do what it looks like it does:

```pyxc
while c = getchar() != EOF:
```
```
Error (Line 5, Column 29): Type mismatch in assignment
  while c = getchar() != EOF:
                            ^~~~
```

`c` gets parsed first, then everything after `=` — including the `!=` comparison — recurses as the right-hand side. So this parses as `c = (getchar() != EOF)`: I'm assigning a `bool` to an `int32`, hence the type mismatch. `while (c = getchar()) != EOF:` needs the parentheses to force `c = getchar()` to bind first.

## Lvalue Validation and Node Construction

Lvalue validation is a single check: `Left->isLValue()`. `isLValue()` is a virtual method on the base `ExpressionNode`, defaulting to `false`; `NameExpressionNode`, `FieldExpressionNode`, `MemberExpressionNode`, and `IndexExpressionNode` each override it to return `true`. `ParseAssignment` doesn't need to know which kind of lvalue it's looking at — it just asks:

```cpp
if (!Left->isLValue())
  return LogErrorExpression("Assignment target must be assignable");
```

Everything else — `x + 1`, a function call, anything whose `isLValue()` stays `false` — falls straight into that error:

```pyxc
(x + 1) = 3
```
```
Error (Line 3, Column 14): Assignment target must be assignable
  (x + 1) = 3
             ^~~~
```

That `isLValue()` check only fires once `Left` has already parsed successfully. A name that was never declared at all fails earlier, inside `ParseNameExpressionWithName`, before `ParseAssignment` ever sees an `ExpressionNode` to ask. I special-case that spot so the error names the actual problem instead of falling back to the generic "unknown variable" message:

```cppdiff
*  if (CurrentToken != tok_lparen) { // Simple variable ref.
*    ValueType Type = LookupVarType(ParsedName);
*    if (Type == ValueType::Error) {
+      if (CurrentToken == tok_assign)
+        return LogErrorExpression("Assignment to undeclared variable");
*      return LogErrorExpression("Unknown variable name");
*    }
*    ...
```

```pyxc
undeclared = 5
```
```
Error (Line 1, Column 12): Assignment to undeclared variable
undeclared = 
           ^~~~
```

Whichever lvalue kind `Left` turned out to be, the result is the same single `AssignmentExpressionNode`. Its `codegen` doesn't pattern-match on node type either: it just calls `Left->codegenAddress()`, and each lvalue class overrides `codegenAddress()` to compute its own storage address, a plain alloca for a name, a `getelementptr` for a field or index. One assignment node covers every lvalue kind because the address computation is already virtual, not because the parser branches on node type.

## The Value of an Assignment Expression

`AssignmentExpressionNode::codegen` already stored the assigned value and returned it — that part didn't need to change. What's new is that this return value is now reachable from inside a larger expression, since `ParseExpression` can produce an assignment node at any nesting level:

```pyxc
var result: int = (c = 5) + 1   # result is 6; c is 5
```

I don't print this value at the top level, though — a bare `c = 5` typed at the REPL still sets `c` silently, exactly like before this chapter.

## Right Associativity

The recursive call is to `ParseAssignment` itself, not back into the tighter precedence tiers — so `=` chains right:

```pyxc
a = b = 4   # parsed as: a = (b = 4)
```

`b = 4` evaluates first: it stores 4 into `b` and produces 4. Then `a = 4` stores that into `a`. Both end up 4.

## Parentheses Are Transparent for Lvalues

There's no `ParenExpressionNode` in pyxc. `ParseParenthesizedExpression` just parses the inner expression and returns it directly:

```cpp
static unique_ptr<ExpressionNode> ParseParenthesizedExpression() {
  getNextToken(); // eat (.
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurrentToken != tok_rparen)
    return LogErrorExpression("Expected ')'");
  getNextToken(); // eat ).
  return V;
}
```

So `(x)` and `x` produce the identical AST node — parentheses never wrap anything, they just group. That means `ParseAssignment` never even sees that parens were there; assignment through them works for free:

```pyxc
(x) = 2     # valid: same as x = 2
(p[i]) = v  # valid: same as p[i] = v
```

## Build and Run

```bash
cd code/chapter-34
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

```pyxc
ready> var a: int = 0
ready> var b: int = 0
ready> a = b = 4
ready> a
4
ready> b
4
ready> var n: int = 5
ready> n = n - 1
ready> n
4
ready>
```

## What's Next

[Chapter 35](chapter-35.md) adds compound assignment and `++`/`--`.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

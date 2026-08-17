---
description: "Add variadic extern declarations so pyxc code can call C functions like printf and scanf that take a variable number of arguments."
---
# 33. pyxc: Variadic Extern Functions

## What I Am Building

[Chapter 32](chapter-32.md) added Unicode escapes and validated UTF-8 strings. pyxc can call C functions via `extern def`, but only functions with a fixed number of typed parameters. `printf`, `scanf`, `sprintf`, and most other C I/O functions take a variable number of arguments — the `...` in their C signatures. Trying to declare them currently produces:

```pyxc
type string = ptr[int8]
extern def printf(fmt: string, ...) -> int32
```
```
Error (Line 2, Column 32): Expected parameter name in function signature
extern def printf(fmt: string, ..
                               ^~~~
```

After this chapter, variadic `extern` declarations work:

```pyxc
type string = ptr[int8]
extern def printf(fmt: string, ...) -> int32

def main() -> int:
  printf("hello world\n")
  printf("answer: %ld\n", 42)
  return 0
```

```
hello world
answer: 42
```

`%ld`, not `%d` — pyxc's `int` is 64-bit, and `printf`'s `%d` expects a 32-bit `int`. `%d` only works once I've explicitly cast the argument down to `int32`.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-33
```

## Grammar

I add a separate signature production for `extern`, since only `extern` allows `...`:

`code/chapter-33/pyxc.ebnf`

```grammardiff
*...
*                                    ( simple-statement
*                                      | end-of-lines block ) ;
-external                          = "extern" "def" function-signature [ "->" type ] ;
+external                          = "extern" "def" external-function-signature
+                                    [ "->" type ] ;
*top-level-statement               = statement ;
*function-signature                = name "(" [ parameters ] ")" ;
+external-function-signature       = name "(" [ parameters [ "," "..." ] | "..." ] ")" ;
*parameters                        = typed-parameter { "," typed-parameter } ;
*typed-parameter                   = name ":" type ;
*...
```

Regular function signatures (used by `def`) are untouched — `...` isn't valid there.

## A New Field for Variadic Functions

The structural change is a new `bool IsVariadic` field on `FunctionSignatureNode`:

```cppdiff
 class FunctionSignatureNode {
*  string Name;
*  vector<pair<string, ValueType>> Parameters;
*  vector<string> ParameterStructNames;
*  ValueType ReturnType;
*  string ReturnStructName;
+  bool IsVariadic;
*  SourceLocation Loc;
*
*public:
*  FunctionSignatureNode(const string &Name,
*                        vector<pair<string, ValueType>> Parameters,
*                        SourceLocation Loc,
*                        ValueType ReturnType = ValueType::Float64,
*                        vector<string> ParameterStructNames = {},
-                        string ReturnStructName = "")
-      : Name(Name), Parameters(std::move(Parameters)), ReturnType(ReturnType),
-        ReturnStructName(std::move(ReturnStructName)), Loc(Loc) {
+                        string ReturnStructName = "",
+                        bool IsVariadic = false)
+      : Name(Name), Parameters(std::move(Parameters)), ReturnType(ReturnType),
+        ReturnStructName(std::move(ReturnStructName)), IsVariadic(IsVariadic),
+        Loc(Loc) {
*    this->ParameterStructNames = std::move(ParameterStructNames);
*    this->ParameterStructNames.resize(this->Parameters.size());
*  }
*
*  const string &getName() const { return Name; }
*  ...
*  void setReturnType(ValueType Type) { ReturnType = Type; }
*  void setReturnStructName(const string &Name) { ReturnStructName = Name; }
+  bool isVariadic() const { return IsVariadic; }
*
*  ValueType getParameterType(size_t Index) const {
*  ...
*};
```

`IsVariadic` defaults to `false`, so every existing call site that builds a `FunctionSignatureNode` keeps working unchanged. Only the `extern def` path ever sets it to `true`.

The redeclaration check for `extern def` already compared parameter counts to catch a second, conflicting declaration of the same name; it now also compares `isVariadic()`, so declaring `printf` once as variadic and again as fixed-arity is rejected the same way a plain arity mismatch always was:

```cppdiff
*  auto Existing = FunctionSignatures.find(ProtoAST->getName());
*  if (Existing != FunctionSignatures.end() &&
-      Existing->second->getNumParameters() != ProtoAST->getNumParameters()) {
+      (Existing->second->getNumParameters() != ProtoAST->getNumParameters() ||
+       Existing->second->isVariadic() != ProtoAST->isVariadic())) {
*    LogErrorExpression((string("Conflicting extern declaration for '") +
*              ProtoAST->getName() + "'")
*                 .c_str());
*  }
```

## Allowing Variadic Arguments in Parsing

`ParseFunctionSignature` gains an `AllowVariadic` parameter that defaults to `false`. Inside the parameter loop, I check for `...` before I check for a parameter name:

```cppdiff
-static unique_ptr<FunctionSignatureNode> ParseFunctionSignature() {
+static unique_ptr<FunctionSignatureNode>
+ParseFunctionSignature(bool AllowVariadic = false) {
*  SourceLocation SignatureLoc = CurrentTokenLocation;
*
*  // Callers consume the leading 'def', so the current token must be the
*  // function name.
*  if (CurrentToken != tok_name)
*    return LogErrorSignature("Expected function name in function signature");
*  string FunctionName = Name;
*  getNextToken(); // eat function name
*
*  if (CurrentToken != tok_lparen)
*    return LogErrorSignature("Expected '(' in function signature");
*
*  vector<pair<string, ValueType>> ParameterNames;
*  vector<string> ParameterStructNames;
+  bool IsVariadic = false;
*  getNextToken(); // eat '('
*
*  if (CurrentToken != tok_rparen) {
*    while (true) {
+      if (AllowVariadic && CurrentToken == tok_dot) {
+        getNextToken(); // eat the first '.'
+        if (CurrentToken != tok_dot)
+          return LogErrorSignature(
+              "Expected '...' in variadic function signature");
+        getNextToken(); // eat the second '.'
+        if (CurrentToken != tok_dot)
+          return LogErrorSignature(
+              "Expected '...' in variadic function signature");
+        getNextToken(); // eat the third '.'
+        IsVariadic = true;
+        if (CurrentToken != tok_rparen)
+          return LogErrorSignature(
+              "Variadic marker must be last in parameter list");
+        break;
+      }
*      if (CurrentToken != tok_name)
*        return LogErrorSignature("Expected parameter name in function signature");
*      string ArgName = Name;
*      getNextToken(); // eat name
*
*      if (CurrentToken != tok_colon)
*        return LogErrorSignature(
*            "Parameter requires a type annotation (e.g., ': int32')");
*      getNextToken(); // eat ':'
*      string ArgStructName;
*      ValueType ArgType = ParseTypeToken(&ArgStructName);
*      if (ArgType == ValueType::Error)
*        return nullptr;
*      if (ArgType == ValueType::None)
*        return LogErrorSignature("Parameters cannot have None type");
*      ParameterNames.push_back({ArgName, ArgType});
*      ParameterStructNames.push_back(ArgStructName);
*
*      if (CurrentToken == tok_rparen)
*        break;
*      if (CurrentToken != tok_comma)
*        return LogErrorSignature("Expected ')' or ',' in parameter list");
*      getNextToken(); // eat ','
*    }
*  }
*
*  getNextToken(); // eat ')'
-  return make_unique<FunctionSignatureNode>(
-      FunctionName, std::move(ParameterNames), SignatureLoc, ValueType::Float64,
-      std::move(ParameterStructNames));
+  return make_unique<FunctionSignatureNode>(
+      FunctionName, std::move(ParameterNames), SignatureLoc, ValueType::Float64,
+      std::move(ParameterStructNames), "", IsVariadic);
 }
```

`...` isn't a single lexer token — the lexer just hands me three separate `tok_dot` tokens, and I consume them one at a time here. If any of the three is missing, `LogErrorSignature` fires immediately:

```pyxc
extern def bad(fmt: ptr[int8], ..) -> int32
```
```
Error (Line 1, Column 34): Expected '...' in variadic function signature
extern def bad(fmt: ptr[int8], ..) 
                                 ^~~~
```

`...` also has to be the last thing before `)` — the `break` right after setting `IsVariadic` guarantees the loop can't come back around for another parameter. And since the `...` check runs before I even look for a parameter name, `extern def f(...)` with no fixed parameters at all works the same way — it just hits that branch on the very first iteration.

## Only `extern def` Allows Variadic Arguments

`ParseFunctionDefinition` calls `ParseFunctionSignature()` with the implicit default `false`. Only `ParseExtern` passes `true`:

```cppdiff
+/// external
+///   = "extern" "def" external-function-signature [ "->" type ] ;
 static unique_ptr<FunctionSignatureNode> ParseExtern() {
   getNextToken(); // eat extern.
*  if (CurrentToken != tok_def)
*    return LogErrorSignature("Expected `def` after extern.");
*  getNextToken(); // eat def
-  auto Signature = ParseFunctionSignature();
+  auto Signature = ParseFunctionSignature(true);
*  if (!Signature)
*    return nullptr;
*  string ReturnStructName;
*  ValueType RetType = ParseOptionalReturnType(&ReturnStructName);
*  if (RetType == ValueType::Error)
*    return nullptr;
*  Signature->setReturnType(RetType);
*  Signature->setReturnStructName(ReturnStructName);
*  return Signature;
*}
```

So `...` in a regular function definition fails, because `AllowVariadic` is `false` there and the first `.` isn't a valid parameter name:

```pyxc
def bad(x: int, ...) -> int:
  return x
```
```
Error (Line 1, Column 17): Expected parameter name in function signature
def bad(x: int, ..
                ^~~~
```

There's no way around this — `...` only exists in `extern def`. pyxc has no `va_list`, `va_start`, or `va_arg`, so I can't write a variadic pyxc function myself, only declare and call variadic C ones.

## Arity Check Updated at Call Sites

The call-site arity check used to be an exact match. For a variadic signature it becomes "at least the fixed count," both at parse time and in codegen:

```cppdiff
 // Inside ParseNameExpressionWithName, right after the ')' that closes the
 // argument list:
*  // I only reach here after parsing `a()` or `a(<arguments>)`, so I eat ')'.
*  getNextToken();
*
*  if (!Signature)
*    return LogErrorExpression("Unknown function: '" + ParsedName + "'");
-  if (Signature->getNumParameters() != Arguments.size())
-    return LogErrorExpression(
-        "Incorrect number of arguments in call to '" + ParsedName +
-        "': expected " + to_string(Signature->getNumParameters()) +
-        ", got " + to_string(Arguments.size()));
+  if ((!Signature->isVariadic() &&
+       Signature->getNumParameters() != Arguments.size()) ||
+      (Signature->isVariadic() &&
+       Arguments.size() < Signature->getNumParameters()))
+    return LogErrorExpression(
+        "Incorrect number of arguments in call to '" + ParsedName +
+        "': expected " + to_string(Signature->getNumParameters()) +
+        (Signature->isVariadic() ? " or more, got " : ", got ") +
+        to_string(Arguments.size()));
*
-  for (size_t i = 0; i < Arguments.size(); ++i) {
+  for (size_t i = 0; i < Signature->getNumParameters(); ++i) {
*    ValueType ArgType = Arguments[i]->getType();
*    ValueType ParamType = Signature->getParameterType(i);
*    ...
*  }
```

The codegen-side check runs the same logic, but against the LLVM `Function` object rather than my own `FunctionSignatureNode`. LLVM's `Function` class already has an `isVarArg()` method built in, so I use that directly:

```cppdiff
 Value *CallExpressionNode::codegen() {
*  Function *CalleeF = getFunction(Callee);
*  if (!CalleeF)
*    return LogErrorValue("Unknown function: '" + Callee + "'");
*
-  if (CalleeF->arg_size() != Arguments.size())
-    return LogErrorValue(
-        "Incorrect number of arguments in call to '" + Callee +
-        "': expected " + to_string(CalleeF->arg_size()) + ", got " +
-        to_string(Arguments.size()));
+  if ((!CalleeF->isVarArg() && CalleeF->arg_size() != Arguments.size()) ||
+      (CalleeF->isVarArg() && Arguments.size() < CalleeF->arg_size()))
+    return LogErrorValue(
+        "Incorrect number of arguments in call to '" + Callee +
+        "': expected " + to_string(CalleeF->arg_size()) +
+        (CalleeF->isVarArg() ? " or more, got " : ", got ") +
+        to_string(Arguments.size()));
*
*  FunctionSignatureNode *Signature = GetFunctionSignature(Callee);
*  std::vector<Value *> ArgsV;
*  ...
*}
```

```pyxc
extern def printf(fmt: ptr[int8], ...) -> int32
printf()
```
```
Error (Line 2, Column 9): Incorrect number of arguments in call to 'printf': expected 1 or more, got 0
printf()
        ^~~~
```

Type-checking only walks the fixed parameters — `for (size_t i = 0; i < Signature->getNumParameters(); ++i)`. Anything past that is on me: I can pass whatever I want after the fixed parameters, and pyxc won't check it against anything, the same way C's own `printf` doesn't.

The argument-building loop in `CallExpressionNode::codegen` needs the same guard, for the same reason: implicit casting only makes sense against a declared parameter type, and variadic arguments don't have one.

```cppdiff
*  FunctionSignatureNode *Signature = GetFunctionSignature(Callee);
*  std::vector<Value *> ArgsV;
*  for (unsigned i = 0, e = Arguments.size(); i != e; ++i) {
*    Value *ArgVal = Arguments[i]->codegen();
*    if (!ArgVal)
*      return nullptr;
-    if (Signature) {
+    if (Signature && i < Signature->getNumParameters()) {
*      ArgVal =
*          EmitImplicitCast(ArgVal, Arguments[i]->getType(), Signature->getParameterType(i));
*      if (!ArgVal)
*        return LogErrorValue("Argument type mismatch");
*    }
*  }
```

Without the `i < Signature->getNumParameters()` check, a variadic argument past the fixed ones would fall through to `Signature->getParameterType(i)` with an out-of-range index. With the check, variadic arguments are emitted as-is, unconverted, exactly what `printf`'s calling convention expects.

## Codegen Passes the Variadic Flag Through

`FunctionSignatureNode::codegen` passes `IsVariadic` to `FunctionType::get` instead of the hardcoded `false` it used before:

```cppdiff
 Function *FunctionSignatureNode::codegen() {
*  std::vector<Type *> ParameterTypes;
*  ParameterTypes.reserve(Parameters.size());
*  for (size_t Index = 0; Index < Parameters.size(); ++Index)
*    ParameterTypes.push_back(
*        LLVMTypeFor(Parameters[Index].second, getParameterStructName(Index)));
-  FunctionType *LLVMFunctionType = FunctionType::get(
-      LLVMTypeFor(ReturnType, ReturnStructName), ParameterTypes,
-      false /* not variadic */);
+  FunctionType *LLVMFunctionType = FunctionType::get(
+      LLVMTypeFor(ReturnType, ReturnStructName), ParameterTypes,
+      IsVariadic);
*  ...
*}
```

That's the whole change on the codegen side. LLVM already knows how to emit a variadic declaration and handle the variadic calling convention at each call site — I just have to tell it `IsVariadic` is true:

```
declare i32 @printf(ptr, ...)
```

## Build and Run

```bash
cd code/chapter-33
cmake -S . -B build && cmake --build build
./build/pyxc
```

```bash
llvm-lit -v test/
```

## Try It

```pyxc
extern def printf(fmt: ptr[int8], ...) -> int32
extern def sqrt(x: float64) -> float64

def main() -> int:
  printf("sqrt(2) = %f\n", sqrt(2.0))
  printf("%ld args after the format string, this time\n", 1)
  return 0
```

```bash
pyxc --emit exe -o vararg vararg.pyxc
./vararg
```

```
sqrt(2) = 1.414214
1 args after the format string, this time
```

## What's Next

[Chapter 34](chapter-34.md) allows assignment to appear inside an expression.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

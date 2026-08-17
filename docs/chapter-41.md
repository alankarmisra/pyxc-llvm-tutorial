---
description: "Add impl blocks: implement a trait for an existing class after the class definition, separating the class layout from its trait conformance."
---
# 41. pyxc: `impl` Blocks

## What I Am Building

[Chapter 40](chapter-40.md) added traits. A class declares the traits it implements in its own header, and the compiler verifies conformance when the class body closes. That works well when I write the trait and the class together, but what if I want to implement a trait on a class that was already written, without touching its definition?

After this chapter, trait conformance can be declared outside the class body entirely:

```pyxc
extern def printd(x: float64)

trait Adder:
  def add(x: int, y: int) -> int

class Calc:
  public bias: int

impl Adder for Calc:
  def add(x: int, y: int) -> int:
    return x + y + self.bias


def main() -> int:
  var c: Calc = Calc()
  c.bias = 5
  printd(float64(c.add(3, 4)))
  return 0
```

```text
12.000000
```

The methods defined in the `impl` block become regular methods on `Calc`, callable with `c.add(...)` just like any other method.

## Source Code

```bash
git clone --depth 1 https://github.com/alankarmisra/pyxc-llvm-tutorial
cd pyxc-llvm-tutorial/code/chapter-41
```

## Grammar

One new production, `implementation-definition`, plus its own block and method productions. `implementation-method` has a full body, unlike `trait-method-signature`; methods in an `impl` block are fully defined right there:

```grammardiff
 program                           = [ end-of-lines ]
                                     [ top-level-item
                                       { end-of-lines top-level-item } ]
                                     [ end-of-lines ] ;
 end-of-lines                      = end-of-line { end-of-line } ;
 top-level-item                    = function-definition
                                     | type-alias
                                     | trait-definition
+                                    | implementation-definition
                                     | struct-definition
                                     | class-definition
                                     | external
                                     | top-level-statement ;
 struct-definition                 = "struct" name ":" end-of-lines
                                     struct-block ;
 trait-definition                  = "trait" name ":" end-of-lines
                                     trait-block ;
 trait-block                       = indent trait-method-signature
                                     { end-of-lines trait-method-signature }
                                     dedent ;
 trait-method-signature            = "def" name "(" [ parameters ] ")"
                                     [ "->" type ] ;
 class-definition                  = "class" name
                                     [ "(" trait-reference
                                       { "," trait-reference } ")" ]
                                     ":" end-of-lines
                                     class-block ;
 trait-reference                   = name ;
+implementation-definition         = "impl" trait-reference "for" name ":"
+                                    end-of-lines implementation-block ;
+implementation-block              = indent method-definition
+                                    { end-of-lines method-definition } dedent ;
 type-alias                        = "type" name "=" type ;
 struct-block                      = indent field-declaration
                                     { end-of-lines field-declaration } dedent ;
 class-block                       = indent class-member
                                     { end-of-lines class-member } dedent ;
 class-member                      = [ visibility ]
                                     ( field-declaration | method-definition ) ;
 visibility                        = "public" | "private" ;
 field-declaration                 = name ":" type ;
 method-definition                 = "def" name "(" [ parameters ] ")"
                                     [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 function-definition               = "def" function-signature [ "->" type ] ":"
                                     ( simple-statement
                                       | end-of-lines block ) ;
 external                          = "extern" "def" external-function-signature
                                     [ "->" type ] ;
 top-level-statement               = statement ;
 function-signature                = name "(" [ parameters ] ")" ;
 external-function-signature       = name "(" [ parameters [ "," "..." ] | "..." ] ")" ;
 parameters                        = typed-parameter { "," typed-parameter } ;
 typed-parameter                   = name ":" type ;
 if-statement                      = "if" expression ":" suite
                                     { [ end-of-lines ] "elif" expression ":" suite }
                                     [ [ end-of-lines ] "else" ":" suite ] ;
 for-statement                     = "for" ( "var" name ":" type | name )
                                     "=" expression ","
                                     expression "," expression ":" suite ;
 while-statement                   = "while" expression ":" suite ;
 do-while-statement                = "do" ":" suite [ end-of-lines ]
                                     "while" expression ;
 switch-statement                  = "switch" expression ":" end-of-lines
                                     indent switch-body dedent ;
 switch-body                       = switch-case
                                     { end-of-lines switch-case }
                                     [ end-of-lines default-case ] ;
 switch-case                       = "case" switch-integer
                                     { "," switch-integer } ":" suite ;
 default-case                      = "default" ":" suite ;
 variable-statement                = "var" variable-binding
                                     { "," variable-binding } ;
 simple-statement                  = return-statement
                                     | break-statement
                                     | continue-statement
                                     | variable-statement
                                     | expression ;
 compound-statement                = if-statement
                                     | for-statement
                                     | while-statement
                                     | do-while-statement
                                     | switch-statement ;
 statement                         = simple-statement | compound-statement ;
 suite                             = simple-statement
                                     | compound-statement
                                     | end-of-lines block ;
 return-statement                  = "return" [ expression ] ;
 break-statement                   = "break" ;
 continue-statement                = "continue" ;
 statement-separator               = end-of-lines | BLOCK_END ;
 block                             = indent statement
                                     { statement-separator statement } dedent ;
 expression                        = assignment ;
 assignment                        = logical-or [ assignment-operator assignment ] ;
 logical-or                        = logical-and { "||" logical-and } ;
 logical-and                       = bitwise-or { "&&" bitwise-or } ;
 bitwise-or                        = bitwise-xor { "|" bitwise-xor } ;
 bitwise-xor                       = bitwise-and { "^" bitwise-and } ;
 bitwise-and                       = equality { "&" equality } ;
 equality                          = relational { ("==" | "!=") relational } ;
 relational                        = shift { ("<" | "<=" | ">" | ">=") shift } ;
 shift                             = sum { ("<<" | ">>") sum } ;
 sum                               = term { ("+" | "-") term } ;
 term                              = unary-expression
                                     { ("*" | "/" | "%") unary-expression } ;
 lvalue                            = name
                                     { "." name | "[" expression "]" } ;
 variable-binding                  = name ":" type [ "=" expression ] ;
 unary-expression                  = ("-" | "!" | "~" | "++" | "--")
                                     unary-expression
                                     | postfix-expression ;
 postfix-expression                = primary [ "++" | "--" ] ;
 primary                           = cast-expression
                                     | sizeof-expression
                                     | address-expression
                                     | array-literal
                                     | string-literal
                                     | character-literal
                                     | name-expression
                                     | number-expression
                                     | boolean-literal
                                     | parenthesized-expression ;
 cast-expression                   = cast-type "(" expression ")" ;
 sizeof-expression                 = "sizeof" "(" type ")" ;
 address-expression                = "addr" "(" lvalue ")" ;
 array-literal                     = "[" [ expression
                                       { "," expression } ] "]" ;
 string-literal                    = '"' { string-character | escape } '"' ;
 escape                            = literal-escape ;
 string-character                  = ? any character except '"', "\\", "\r", and "\n" ? ;
 character-literal                 = "'" ( character | character-escape ) "'" ;
 character-escape                  = literal-escape ;
 literal-escape                    = "\\" ( "\\" | "'" | '"' | "?"
                                       | "a" | "b" | "f" | "n" | "r"
                                       | "t" | "v"
                                       | "x" hex-digit hex-digit
                                       | octal-digit [ octal-digit
                                         [ octal-digit ] ]
                                       | "u" hex-digit hex-digit hex-digit hex-digit
                                       | "U" hex-digit hex-digit hex-digit hex-digit
                                         hex-digit hex-digit hex-digit hex-digit ) ;
 character                         = ? any character except "'", "\\", "\r", and "\n" ? ;
 hex-digit                         = digit | "A".."F" | "a".."f" ;
 assignment-operator               = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
 octal-digit                       = "0".."7" ;
 name-expression                   = lvalue
                                     | call-expression
                                     | method-call-expression
                                     | constructor-call-expression ;
 call-expression                   = name "(" [ arguments ] ")" ;
 method-call-expression            = lvalue "." name "(" [ arguments ] ")" ;
 constructor-call-expression       = name "(" [ arguments ] ")" ;
 arguments                         = expression { "," expression } ;
 number-expression                 = number ;
 parenthesized-expression          = "(" expression ")" ;
 indent                            = INDENT ;
 dedent                            = DEDENT ;
 name                              = (letter | "_")
                                     { letter | digit | "_" } ;
 type                              = base-type [ array-suffix ] ;
 base-type                         = builtin-type | alias-type | struct-type
                                     | pointer-type ;
 pointer-type                      = "ptr" "[" type "]" ;
 array-suffix                      = "[" integer "]" ;
 builtin-type                      = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" | "None" ;
 struct-type                       = name ;
 alias-type                        = name ;
 cast-type                         = builtin-cast-type | pointer-type ;
 builtin-cast-type                 = "int" | "int8" | "int16" | "int32"
                                     | "int64" | "uint8" | "uint16"
                                     | "uint32" | "uint64"
                                     | "float" | "float32"
                                     | "float64" | "bool" ;
 number                            = ( digit { digit } [ "." { digit } ]
                                     | "." digit { digit } ) [ exponent ] ;
 switch-integer                    = [ "-" ] digit { digit } ;
 exponent                          = ( "e" | "E" ) [ "+" | "-" ]
                                     digit { digit } ;
 boolean-literal                   = "True" | "False" ;
 integer                           = digit { digit } ;
 letter                            = "A".."Z" | "a".."z" ;
 digit                             = "0".."9" ;
 end-of-line                       = "\r\n" | "\r" | "\n" ;
 (*
     A `comment` begins with "#" and continues to the end of the line. The lexer
      ignores its text and returns an end-of-line token when one follows it.
 *)
 comment                           = "#" { comment-character } ;
 comment-character                 = ? any character except "\r" and "\n" ? ;
 (*
     `whitespace` may appear before or between tokens
      and is ignored by the lexer.
 *)
 whitespace                        = " " | "\t" | "\v" | "\f" ;
 INDENT                            = ? synthetic token emitted by lexer when indentation increases ? ;
 DEDENT                            = ? synthetic token emitted by lexer when indentation decreases ? ;
 BLOCK_END                         = ? synthetic token injected into the stream by ParseBlock
                                       immediately after it consumes DEDENT ? ;

```

## New Token and Keyword

```cppdiff
*  tok_star_equal = -59,
*  tok_slash_equal = -60,
*  tok_percent_equal = -61,
*  tok_plus_plus = -62,
*  tok_minus_minus = -63,
*  tok_class = -64,
*  tok_public = -65,
*  tok_private = -66,
*  tok_trait = -67,
+  tok_impl = -68,
```

Registered in the keyword table like every other keyword. The `for` in `impl TraitName for ClassName` reuses the existing `tok_for` token, the same one `for` loop statements produce. There's no ambiguity: `impl` always precedes it, and the parser already knows it's reading an impl header at that point, not a loop.

## Reusing Trait Conformance Checking

[Chapter 40](chapter-40.md) already pulled the conformance check out into its own function, `VerifyTraitConformance`, called from `ParseAggregateDefinition` at the class body's closing `DEDENT`. That function is unchanged here:

```cpp
static bool VerifyTraitConformance(const string &ClassName,
                                   const string &TraitName) {
  const auto &Trait = TraitTypes.at(TraitName);
  const auto &Class = StructTypes.at(ClassName);
  for (const auto &Requirement : Trait.Methods) {
    string MethodName = ClassName + "." + Requirement.Name;
    FunctionSignatureNode *Implementation =
        GetFunctionSignature(MethodName);
    if (!Implementation) {
      LogErrorExpression(
          ("Class '" + ClassName + "' does not implement trait '" + TraitName +
           "' method '" + Requirement.Name + "'")
              .c_str());
      return false;
    }
    auto Visibility = Class.Methods.find(Requirement.Name);
    if (Visibility == Class.Methods.end() || !Visibility->second) {
      LogErrorExpression(
          ("Trait method '" + Requirement.Name + "' on class '" + ClassName +
           "' must be public")
              .c_str());
      return false;
    }

    bool Matches =
        Implementation->getNumParameters() ==
            Requirement.Parameters.size() + 1 &&
        Implementation->getReturnType() == Requirement.ReturnType &&
        Implementation->getReturnStructName() == Requirement.ReturnTypeInfo;
    for (size_t Index = 0; Matches && Index < Requirement.Parameters.size();
         ++Index) {
      Matches =
          Implementation->getParameterType(Index + 1) ==
              Requirement.Parameters[Index].second &&
          Implementation->getParameterStructName(Index + 1) ==
              Requirement.ParameterTypeInfo[Index];
    }
    if (!Matches) {
      LogErrorExpression(
          ("Method '" + Requirement.Name + "' on class '" + ClassName +
           "' does not match trait signature")
              .c_str());
      return false;
    }
  }
  return true;
}
```

It takes a class name and a trait name and doesn't care where they came from. The new `ParseImplementationDefinition` calls it exactly the same way `ParseAggregateDefinition` already does, once it has resolved both names itself and finished parsing the `impl` body.

## The `impl` Block Parser

`ParseImplementationDefinition` validates the header — trait exists, `for` follows, class exists and is actually a class, not a struct, trait not already implemented — then parses each method, checks conformance, and only compiles the methods once conformance passes:

```cpp
static bool ParseImplementationDefinition() {
  getNextToken(); // eat 'impl'
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected trait name after 'impl'"), false;
  string TraitName = Name;
  if (!TraitTypes.count(TraitName))
    return LogErrorExpression(("Unknown trait '" + TraitName + "'")),
           false;
  getNextToken(); // eat trait name
  if (CurrentToken != tok_for)
    return LogErrorExpression("Expected 'for' after trait name"), false;
  getNextToken(); // eat 'for'
  if (CurrentToken != tok_name)
    return LogErrorExpression("Expected class name after 'for'"), false;
  string ClassName = Name;
  auto Class = StructTypes.find(ClassName);
  if (Class == StructTypes.end())
    return LogErrorExpression(("Unknown class '" + ClassName + "'")),
           false;
  if (!Class->second.IsClass)
    return LogErrorExpression("traits can only be implemented on classes"),
           false;
  if (find(Class->second.ImplementedTraits.begin(),
           Class->second.ImplementedTraits.end(),
           TraitName) != Class->second.ImplementedTraits.end())
    return LogErrorExpression(
               ("Trait '" + TraitName + "' is already implemented for class '" +
                ClassName + "'")
                   .c_str()),
           false;
  getNextToken(); // eat class name
  if (CurrentToken != tok_colon)
    return LogErrorExpression("Expected ':' after impl header"), false;
  getNextToken(); // eat ':'
  if (CurrentToken != tok_eol)
    return LogErrorExpression("Expected newline after impl header"), false;
  consumeNewlines();
  if (CurrentToken != tok_indent)
    return LogErrorExpression("Expected an indented impl body"), false;
  getNextToken(); // eat INDENT

  vector<unique_ptr<FunctionDefinitionNode>> Methods;
  while (CurrentToken != tok_dedent && CurrentToken != tok_eof) {
    if (CurrentToken == tok_eol) {
      consumeNewlines();
      continue;
    }
    if (CurrentToken == tok_block_end) {
      getNextToken();
      continue;
    }
    if (CurrentToken != tok_def)
      return LogErrorExpression("Expected method definition in impl body"),
             false;
    auto Method = ParseMethodDefinition(ClassName, true);
    if (!Method)
      return false;
    Methods.push_back(std::move(Method));
    if (CurrentToken == tok_eol)
      consumeNewlines();
    else if (CurrentToken == tok_block_end)
      getNextToken();
  }
  if (CurrentToken != tok_dedent)
    return LogErrorExpression("Expected dedent after impl body"), false;

  StructTypes[ClassName].ImplementedTraits.push_back(TraitName);
  if (!VerifyTraitConformance(ClassName, TraitName))
    return false;
  for (auto &Method : Methods) {
    if (!Method->codegen())
      return false;
  }
  PendingTokens.push_front(tok_block_end);
  getNextToken(); // eat DEDENT, then surface block-end
  return true;
}
```

The duplicate-`impl` check runs at header-parse time, right after the class is resolved and before the class name token is even consumed — not after the body is parsed. I confirmed this by compiling two `impl Adder for Calc:` blocks back to back: the error lands on the second block's `impl` line, pointing at `Calc`, not on any line inside the body.

Every method parsed here goes through `ParseMethodDefinition` with `IsPublic` hardcoded to `true`. Satisfying a trait is a public commitment; there's no such thing as a private trait method, so `impl` doesn't offer a visibility modifier to write one. A method that's private in spirit would just fail `VerifyTraitConformance`'s public check anyway, so forcing `true` here just skips straight to the outcome that check would have produced.

Methods are parsed and registered into `FunctionSignatures` (inside `ParseMethodDefinition`) before conformance is checked, but their bodies aren't compiled to IR until after `VerifyTraitConformance` passes. If the class doesn't actually satisfy the trait, `impl`'s methods never reach `codegen()`.

`HandleImplementationDefinition` calls `ParseImplementationDefinition` with the same error-recovery pattern the struct, class, and trait handlers already use, and `tok_impl` is wired into the dispatch switch in both `MainLoop` and `FileModeLoop`.

## Methods Defined in `impl` Are Regular Methods

There's no runtime distinction between a method defined in the class body and one defined in an `impl` block. Both land in `FunctionSignatures` under the same mangled name, `ClassName.MethodName`, and are emitted as `@ClassName.MethodName` in the IR. A caller has no way to tell where the method was defined, and doesn't need to.

## Known Limitations

**The trait must already exist.** `impl Adder for Calc:` requires `Adder` to already be registered in `Traits` at the point the `impl` block is parsed.

**The class must already exist, and must be a class.** The name is looked up in `StructTypes` at parse time; a struct gives `traits can only be implemented on classes`.

**`impl` cannot be used twice for the same trait/class pair.** A second `impl Adder for Calc:` is rejected: `Trait 'Adder' is already implemented for class 'Calc'`.

## Build and Run

```bash
cd code/chapter-41
cmake -S . -B build && cmake --build build
```

```bash
llvm-lit -v test/
```

## Try It

<!-- code-merge:start -->
```pyxc
extern def printd(x: float64)

trait Adder:
  def add(x: int, y: int) -> int

class Calc:
  public bias: int

impl Adder for Calc:
  def add(x: int, y: int) -> int:
    return x + y + self.bias

def main() -> int:
  var c: Calc = Calc()
  c.bias = 5
  printd(float64(c.add(3, 4)))
  return 0
```
```text
12.000000
```
<!-- code-merge:end -->

## What's Next

[Chapter 42](chapter-42.md) adds generic traits.

## Need Help?

Build issues? Questions?

- **GitHub Issues:** [Report problems](https://github.com/alankarmisra/pyxc-llvm-tutorial/issues)
- **Discussions:** [Ask questions](https://github.com/alankarmisra/pyxc-llvm-tutorial/discussions)

Include:
- Your OS and version
- Full error message
- Output of `cmake --version`, `ninja --version`, and `llvm-config --version`

I'll help you figure it out.

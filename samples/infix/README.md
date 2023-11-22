# `infix`

This sample was designed as a tutorial in how to use the basics of
Trieste. As a multi-pass term-rewriting system, Trieste is ultimately
about performing transforms on trees. Let's start by defining some
terms:

| Name | Description |
|------|-------------|
| Term | A subtree             |
| Node | A node in the tree. Can have 0 to N children. Has one parent. |
| Match | Object used for grabbing nodes from a tree pattern match |
| Location | A string. This can either be a standalone string, or a view into a long string (i.e. an input file) |
| Token | Something in the program. This can map to something in an input file (e.g. Int, Float, String), a logical grouping term (Operator, Group), or any other tree node which is of use to the system. |
| Well Formed | Trieste provides a way to describe, using tokens, what the structure of a tree should be. A tree which adheres to this structure is considered "Well Formed" |
| Pass | A Pass is a stage of the system. It is bounded by two Well Formed definitions, one indicating what trees should look like on input to the pass, and another that describes how the pass has changed the tree. It is important to mention that the tree will change multiple times during Pass execution. |


Before we dive into how Trieste works, it may be worth describing what
it is for. Trieste is a preparation tool for a compiler. By performing
multiple passes over the input, Trieste is able to iteratively refine
the AST until it takes the form that is best suited to compilation.
What starts as a formless set of tokens provided by the parser
eventually turns into a clean and easy to process tree. It allows us to
go from this:

``` typescript
x = 5 + 10;
y = 1 - 9 + x;
print "1" x + y;
z = (5 * x) / y;
print "2" z;
```

to this:

``` mermaid
flowchart TD
  A[Top]-->B[Calculation]
  B-->C[Output]
  C-->D["string #quot;1#quot;"]
  C-->E[Int 22]
  B-->F[Output]
  F-->G["string #quot;2#quot;"]
  F-->H[Int 10]
```

Over the course of this tutorial, we will show how we can use Trieste to
transform the input program iteratively to the final AST shown.

## The `infix` Language

In order to keep the focus on Trieste as a system we will work with a
very simple language for performing and outputting mathematics using
infix operators. All variables are constants (i.e. their values do not
change) and the only action the program can take is to print a result
to the console.

The language is defined by the following grammar (we assume standard 
syntax for $string$, $number$ and $variable$):

$$
\begin{align}
Calc &\rightarrow Assign \ |\ Output\ |\ Assign\ Calc\ |\ Output\ Calc\\
Assign &\rightarrow variable\ \texttt{=}\ Expr \ ";" \\
Output &\rightarrow \texttt{print}\ string \ Expr \ ";"\\ 
Expr &\rightarrow Expr\ Op\ Expr\ |\ Operand\ |\ Open\ Expr\ Close\\
Operand &\rightarrow number\ |\ variable\\
Op &\rightarrow \texttt{+}\ |\ \texttt{-}\ |\ \texttt{*}\ |\ \texttt{/}\\
Open &\rightarrow \texttt{(}\\
Close &\rightarrow \texttt{)}\\
\end{align}
$$

which results in programs that look like this:

### [simple.infix](./examples/simple.infix)
``` typescript
x = 5;
print "x" x;
y = 2 - 1;
print "1 + 10" 1 + 10;
```

### [mixed.infix](./examples/mixed.infix)
``` typescript
x = 1 + 2 * 3 + 5.3 - 4 - 2 / 0.1;
y = 3.2 * x + 5;
print "x" x;
print "y" y;
```

### [multi_ident.infix](./examples/multi_ident.infix)
``` typescript
x = 5 + 10;
y = 1 - 9 + x;
print "1" x + y;
z = (5 * x) / y;
print "2" z;
```

## Parser

Trieste requires us to construct two objects: a `Parse` object and a
`Driver` object. The `Parse` object will be used by the parser to
turn one or more files into a tree of nodes, where each node is matched
with a corresponding token. We will come back to the `Driver` object later
in this tutorial. First, we construct the `Parse` object like so:

``` c++
Parse p(depth::file, wf_parser);
```

With the first argument we specify the level at which the parser will run:

``` c++
enum class depth
{
  file,
  directory,
  subdirectories
};
```

Either at the level of a single file, a directory of files, or a
whole directory structure with subdirectories. An important point is
that whatever level the parser runs at, it will return a single tree
containing the tokens from all files. For `infix` we'll be operating at
the level of a single file, for simplicity.  The second argument, 
`wf_parser`, is the well-formed definition for the output of the parsing
step. We will return to this definition after seeing some example parse
trees for the `infix` language. 

We then need to set up the Trieste `Rule` objects which turn text into
tokens. The constructor for a `Rule` is:

``` c++
Rule(const std::string& r, std::function<void(Make&)> effect)
```

`r` is a regular expression, and `effect` will be called when a token
matches the RegEx. While we can use this constructor, some helpful
operator overloading allows for a more compact way of specifying these
rules:

``` c++
p("start", // this indicates the 'mode' these rules are associated with
  {
    // Whitespace between tokens.
    "[[:blank:]]+" >> [](auto&) {}, // no-op

    // Line comment.
    "//[^\n]*" >> [](auto&) {}, // another no-op

    // Int.
    "[[:digit:]]+\\b" >> [](auto& m) { m.add(Int); },
    
    // String. s
    R"("[^"]*")" >> [](auto& m) { m.add(String); },

    // Print.
    "print\\b" >> [](auto& m) { m.add(Print); },

    // Add ('+' is a reserved RegEx character)
    "\\+" >> [](auto& m) { m.add(Add); },

    // Equals.
    "=" >> [](auto& m) { m.seq(Equals); },

    // Terminator.
    ";[\n]*" >> [](auto& m) { m.term({Equals}); },

    // ...
  });
```
Here, the overloaded `>>` operator takes a RegEx and an `effect` as arguments 
and returns a `Rule` object. The `()` operator (called on the `Parse` object `p`
in the code example above) is overloaded in the `Parse` class and is called with
a string (representing a 'mode') and the list of `Rule`s as arguments which are 
added to the `Parse` object. A mode is simply a string, used for controlling
the application of the rules. Currently, only the 'start' mode is used.

### Tokens

The parser rules match strings from the input program to tokens, which
must be defined for the language we are parsing. For our running example, 
the tokens (e.g. `Int`, `Print`) are defined as `TokenDef` objects:

``` c++
inline const auto Int = TokenDef("int", flag::print);
inline const auto Print = TokenDef("print");
inline const auto Add = TokenDef("+");
inline const auto Equals = TokenDef("equals");

```
A `TokenDef` can be given a flag as a second argument. 
For example, the `flag::print` indicates that, when outputting 
the tree, this token should also output the raw string from the
input file.

There are also generic tokens built into Trieste.
For example, the generic token `Group` is used by the parser
to group parsed tokens and `Top` to indicate the root of
the parse tree. Some are given below as examples:

``` c++
  inline const auto Invalid = TokenDef("invalid");
  inline const auto Top = TokenDef("top", flag::symtab);
  inline const auto Group = TokenDef("group");
  inline const auto File = TokenDef("file");
  inline const auto ErrorMsg = TokenDef("errormsg", flag::print);
```

There are several other flags:

| Flag | Description |
|------|-------------|
| `print` | Print the location when printing an AST node of this type. |
| `symtab` | Include a symbol table in an AST node of this type. |
| `defbeforeuse` | If an AST node of this type has a symbol table, definitions can only be found from later in the same source file. |
| `shadowing` | If a definition of this type is in a symbol table, it doesn't recurse into parent symbol tables. |
| `lookup` | If a definition of this type is in a symbol table, it can be found when looking up. |
| `lookdown` | If a definition of this type in a symbol table, it can be found when looking down. |

We will touch on some of these later as we encounter the nodes that
use them.


### Parsing sample

Above we see a few examples of the kinds of parsing rules we need for
our language. The simplest ones add a Token via `m.add()`. This will add
the token to the current `Group`. Let's look at the following line:

``` typescript
print "5 + 10" 5 + 10;
```

Let's look at this step by step (we'll show Whitespace once but will skip it afterwards for space). 
The current position of the cursor in the tree is indicated by the ^ symbol:


<table>
<tr><th>Step</th><th>Location</th><th>Tree</th></tr>
<tr><td>Init</td><td>

``` typescript
print "5 + 10" 5 + 10;
^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (add)</td><td>

``` typescript
print "5 + 10" 5 + 10;
     ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Print]
  C-->E(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Whitespace (no-op)</td><td>

``` typescript
print "5 + 10" 5 + 10;
      ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Print]
  C-->E(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (add)</td><td>

``` typescript
print "5 + 10" 5 + 10;
              ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Print]
  C-->E["String #quot;5 + 10#quot;"]
  C-->F(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```
</td></tr>
<tr><td>Token (add)</td><td>

``` typescript
print "5 + 10" 5 + 10;
                ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Print]
  C-->E["String #quot;5 + 10#quot;"]
  C-->F[Int 5]
  C-->G(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```
</td></tr>
<tr><td>Token (add)</td><td>

``` typescript
print "5 + 10" 5 + 10;
                  ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Print]
  C-->E["String #quot;5 + 10#quot;"]
  C-->F[Int 5]
  C-->G[Add]
  C-->H(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (add)</td><td>

``` typescript
print "5 + 10" 5 + 10;
                     ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Print]
  C-->E["String #quot;5 + 10#quot;"]
  C-->F[Int 5]
  C-->G[Add]
  C-->H[Int 10]
  C-->I(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (term)</td><td>

``` typescript
print "5 + 10" 5 + 10;
                      ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Print]
  C-->E["String #quot;5 + 10#quot;"]
  C-->F[Int 5]
  C-->G[Add]
  C-->H[Int 10]
  B-->I(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
</table>

Note that the `term` command, triggered by the semi-colon, ends the
`Group` node. In addition to the `add` and `term` functions operating on tokens
we have seen this far, we will look at the effects of the functions `push`, `pop`
and `seq` while parsing. 


### Parsing example with `push` and `pop`

`infix` includes the ability to set expression precedence using
parenthesis. How do we handle parentheses when we are parsing? Let's
look at the rules:

``` c++
auto indent = std::make_shared<std::vector<size_t>>();

/* ... */

// Parens.
"(\\()[[:blank:]]*" >> [indent](auto& m) {
  // we push a Paren node. Subsequent nodes will be added
  // as its children.
  m.push(Paren, 1);
},

"\\)" >>
  [indent](auto& m) {
    // terminate the current group
    m.term();
    // pop back up out of the Paren
    m.pop(Paren);
  },

```

We'll explore this behavior with an example:

``` typescript
1 + (2 * 3) + 4;
```

Let's see how this develops:

<table>
<tr><th>Action</th><th>Location</th><th>Tree</th></tr>
<tr><td>Init</td><td>

``` typescript
1 + (2 * 3) + 4;
^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (add x 2)</td><td>

``` typescript
1 + (2 * 3) + 4;
    ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Int 1]
  C-->E[+]
  C-->F(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (push)</td><td>

``` typescript
1 + (2 * 3) + 4;
     ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Int 1]
  C-->E[+]
  C-->F[Paren]
  F-->G[cursor]:::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (add x 3)</td><td>

``` typescript
1 + (2 * 3) + 4;
          ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Int 1]
  C-->E[+]
  C-->F[Paren]
  F-->G[Group]
  G-->H[Int 2]
  G-->I[*]
  G-->J[Int 3]
  G-->K(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (pop)</td><td>

``` typescript
1 + (2 * 3) + 4;
           ^
```

</td><td>

``` mermaid
graph TD;
  A[tTp]-->B[File]
  B-->C[Group]
  C-->D[Int 1]
  C-->E[+]
  C-->F[Paren]
  F-->G[Int 2]
  F-->H[*]
  F-->I[Int 3]
  C-->J(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (add x 2)</td><td>

``` typescript
1 + (2 * 3) + 4;
               ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Int 1]
  C-->E[+]
  C-->F[Paren]
  F-->G[Int 2]
  F-->H[*]
  F-->I[Int 3]
  C-->J[+]
  C-->K[Int 4]
  C-->L(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (term)</td><td>

``` typescript
1 + (2 * 3) + 4;
                ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[Int 1]
  C-->E[+]
  C-->F[Paren]
  F-->G[Int 2]
  F-->H[*]
  F-->I[Int 3]
  C-->J[+]
  C-->K[Int 4]
  B-->L(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
</table>

As you can see, `push` and `pop` allow us to move up and down the parse
tree, creating structure even as we parse the tokens themselves that
will help us later in the process.

### Parsing example with `seq`

Let's look at the rule definitions again in detail:

``` c++
// Equals.
"=" >> [](auto& m) { m.seq(Equals); },

// Terminator.
";[\n]*" >> [](auto& m) { m.term({Equals}); },
```

These rules, for parsing assignments, are different from the other parsing rules for
the infix language. The first rule calls `seq`. This command indicates that we are 
entering into a sequence of groups. Let's look at how we parse this line:

``` typescript
x = 5 + 3;
```

<table>
<tr><th>Action</th><th>Location</th><th>Tree</th></tr>
<tr><td>Init</td><td>

``` typescript
x = 5 + 3;
^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (add) </td><td>

``` typescript
x = 5 + 3;
  ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[Group]
  C-->D[x]
  C-->E(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (seq)</td><td>

``` typescript
x = 5 + 3;
   ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[=]
  C-->D[Group]
  D-->E[x]
  C-->F(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;

```

</td></tr>
<tr><td>Token (add x 3)</td><td>

``` typescript
x = 5 + 3;
         ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[=]
  C-->D[Group]
  D-->E[x]
  C-->F[Group]
  F-->G[Int 5]
  F-->H[Add]
  F-->I[Int 3]
  F-->J(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>Token (term)</td><td>

``` typescript
x = 5 + 3;
          ^
```

</td><td>

``` mermaid
graph TD;
  A[Top]-->B[File]
  B-->C[=]
  C-->D[Group]
  D-->E[x]
  C-->F[Group]
  F-->G[Int 5]
  F-->H[Add]
  F-->I[Int 3]
  B-->J(cursor):::cursor
  classDef cursor stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
</table>

Because we call `term` with `Equals`, it will not only
terminate the group but also terminate the sequence of values in
the `Equals`. The next call to `add` will add a new group at the same
level as the `Equals`.

## Passes

We now have a parse tree for our program. It has turned this program:

``` typescript
x = 5 + 10;
y = 1 - 9 + x;
print "1" x + y;
z = (5 * x) / y;
print "2" z;
```

into this tree:

```
top
└── file
    ├── equals
    │   ├── group
    │   │   └── ident x
    │   └── group
    │       ├── int 5
    │       ├── +
    │       └── int 10
    ├── equals
    │   ├── group
    │   │   └── ident y
    │   └── group
    │       ├── int 1
    │       ├── -
    │       ├── int 9
    │       ├── +
    │       └── ident x
    ├── group
    │   ├── print
    │   ├── string "1"
    │   ├── ident x
    │   ├── +
    │   └── ident y
    ├── equals
    │   ├── group
    │   │   └── ident z
    │   └── group
    │       ├── paren
    │       │   └── group
    │       │       ├── int 5
    │       │       ├── *
    │       │       └── ident x
    │       ├── /
    │       └── ident y
    └── group
        ├── print
        ├── string "2"
        └── ident z
```

We can specify what a **well formed** parse tree looks like in the
following way:

``` c++
// | is used to create a Choice between all the elements
// this indicates that literals can be an Int or a Float
inline const auto wf_literal = Int | Float;

inline const auto wf_parse_tokens = wf_literal | String | Paren | Print | Ident | Add | Subtract | Divide | Multiply;

// A <<= B indicates that B is a child of A
// ++ indicates that there are zero or more instances of the token
inline const auto wf_parser =
    (Top <<= File)
  | (File <<= (Group | Equals)++)
  | (Paren <<= Group++)
  | (Equals <<= Group++)
  | (Group <<= wf_parse_tokens++)
  ;
```

Each rewriting pass has a corresponding well-formedness check which is used to
ensure that the input to the next pass is as expected. If the parse
tree fails this check, Trieste will stop rewriting at that pass and
output an error indicating what is wrong with the tree.

### Pass 1: Expressions

Our first pass will take the raw tokens from the parser and organize
them into mathematical expressions. We will do this by specifying a 
`PassDef` object, which has the following constructor:

``` c++
PassDef(
      const std::string& name,
      const wf::Wellformed& wf,
      dir::flag direction,
      const std::initializer_list<detail::PatternEffect<Node>>& r)
```

The first argument is a name for the pass, `wf` is the well-formedness rules
for the pass and the `direction` flag specifies the direction of the
tree traversal (top-down, bottom-up or once). `r` in this case are rules
which change the current tree using a `Pattern`, which matches to a 
subtree, and `Effect`:

``` c++
using Effect = std::function<T(Match&)>;
```

which is a function that takes a `Match` object (i.e. a term matching
the pattern) and produces an updated subtree. Naturally, we have some
nice semantic sugar to keep us from having to call these constructors
manually. Let's look at some example rewrite rules for this pass. 
The first rule can be read as "If we find a `Top` node with a `File` 
node as its child, replace the `File` node with a `Calculation` node 
with the `File` content":

``` c++
// Here we're saying that we want to create a
// Calculation node and make all of the values in File (*_[File]) its
// children. The children from the matched term (_) are accessed by
// first retrieving the File node from the match object (_[File])
// and then its children (*_[File]).
In(Top) * T(File)[File] >>
    [](Match& _) { return Calculation << *_[File]; },

// This rule selects an Equals node with the right structure,
// i.e. a single ident being assigned. We replace it with
// an Assign node that has two children: the Ident and the
// an Expression, which will take the children of the Group.
In(Calculation) *
    (T(Equals) << ((T(Group) << T(Ident)[Id]) * T(Group)[Rhs])) >>
  [](Match& _) { return Assign << _(Id) << (Expression << *_[Rhs]); },

// This rule selects a Group that matches the Output pattern
// of `print <string> <expression>`. In this case, Any++ indicates that
// Rhs should contain all the remaining tokens in the group.
// When used here, * means nodes that are children of the In()
// node in the specified order. They can be anywhere inside
// the In() child sequence.
In(Calculation) *
    (T(Group) << (T(Print) * T(String)[Lhs] * Any++[Rhs])) >>
  [](Match& _) { return Output << _(Lhs) << (Expression << _[Rhs]); },

// This node unwraps Groups that are inside Parens, making them
// Expression nodes.
In(Expression) * (T(Paren) << T(Group)[Group]) >>
  [](Match& _) { return Expression << *_[Group]; },

```
The general shape of a rule is `Pattern >> Effect`, which produces a `PatternEffect`.
In a pattern, `T(Foo)` matches a single node `Foo`, and `C * D` matches pattern `C` followed 
by pattern `D`. `P << C` means that the pattern `C` matches the children of whatever node
is matched by pattern `P`. `In(Foo)` indicates that the parent node where the current
pattern matches is a `Foo` node. This parent node will not be part of the `Match` object: 
only subsequent terms in the pattern are part of the `Match` and can be replaced by 
the effect. We can also bind patterns to variables to conveniently refer to constituent parts
of a `Pattern`. For instance, `T(Foo)[V]` creates a pattern match of type `Foo` bound to 
the name `V`. Note that the name `V` also is a `Token` (for performance reasons) but
does not have to be the same token as it is binding.


The result of running this pass (on the parse tree) is the following tree:

```
top
└── calculation
    ├── symbols: {x y z}
    ├── assign
    │   ├── ident x
    │   └── expression
    │       ├── int 5
    │       ├── +
    │       └── int 10
    ├── assign
    │   ├── ident y
    │   └── expression
    │       ├── int 1
    │       ├── -
    │       ├── int 9
    │       ├── +
    │       └── ident x
    ├── output
    │   ├── string "1"
    │   └── expression
    │       ├── ident x
    │       ├── +
    │       └── ident y
    ├── assign
    │   ├── ident z
    │   └── expression
    │       ├── expression
    │       │   ├── int 5
    │       │   ├── *
    │       │   └── ident x
    │       ├── /
    │       └── ident y
    └── output
        ├── string "2"
        └── ident z
```

Note how there is now a symbol table for the `Calculation` node. This
is because it, and `Assign`, are defined like this:

``` c++
  inline const auto Calculation = TokenDef("calculation", flag::symtab | flag::defbeforeuse);
  inline const auto Assign = TokenDef("assign", flag::lookup | flag::shadowing);
```

Note the use of additional flags from the table above. `symtab`
tells Trieste that `Calculation` should have its own symbol
table. `defbeforeuse` tells Trieste that every symbol in a
`Calculation` has to be defined before it can be used. `lookup`
indicates that children of assign nodes can be the targets of
reference lookups, and `shadowing` indicates that the symbol
defined by an `Assign` node will take precedence over any other
which exists.

We must also specify what it means for a tree to be well-formed after
the `expressions` pass: 

``` c++
inline const auto wf_expressions_tokens =
    wf_literal | Ident | Add | Subtract | Divide | Multiply | Expression;

inline const auto wf_pass_expressions =
    (Top <<= Calculation)
  | (Calculation <<= (Assign | Output)++)
  // [Ident] here indicates that the Ident node is a symbol that should
  // be stored in the symbol table
  | (Assign <<= Ident * Expression)[Ident]
  | (Output <<= String * Expression)
  // [1] here indicates that there should be at least one token
  | (Expression <<= wf_expressions_tokens++[1])
  ;
```

This will ensure that a tree which progresses to the next step takes
this form.

Now that we have a pass, let us look at how we construct a
`Driver` object:

``` c++
static Driver d(
      "infix",
      nullptr,
      parser(),
      {
        expressions() //our only rewrite pass so far
      }
  );
```

We first provide a name for our language, and then a reference to
some command line options (none in this case). The `parser()` 
function returns the `Parse` object that we specified before.
We then include our rewrite passes, where the `PassDef` returned
by the function `expressions()` is the only one defined thus far. 

### Passes 2 and 3: Operator precedence

Now that we have our tokens roughly organized around mathematical
expressions, we need to solve the problem of operator precedence.
We want the multiply and divide operations to have higher precedence
than add and subtract. It so happens that with a multi-pass term
rewriting system like Trieste, this is very straightforward. We will
have one pass devoted to grouping all multiply and divide expressions
together (as if they were surrounded in parentheses) and then in the
next pass we will do the same for all add and subtract expressions.

Here is the first rule:

``` c++
// Group multiply and divide operations together. This rule will
// select any triplet of <arg> *|/ <arg> in an expression list and
// replace it with a single <expr> node that has the triplet as
// its children.
In(Expression) *
    (ExpressionArg[Lhs] * (T(Multiply) / T(Divide))[Op] *
      ExpressionArg[Rhs]) >>
  [](Match& _) {
    return Expression
      << (_(Op) << (Expression << _(Lhs)) << (Expression << _[Rhs]));
  },
```

with the updated well-formedness check:

``` c++
inline const auto wf_pass_multiply_divide =
  wf_pass_expressions
  | (Multiply <<= Expression * Expression)
  | (Divide <<= Expression * Expression)
  ;
```

Note that we do not have to repeat what came before, simply update
and rewrite how certain nodes should behave.

Now we want to do the same for add and subtract:

``` c++
In(Expression) *
    (ExpressionArg[Lhs] * (T(Add) / T(Subtract))[Op] *
      ExpressionArg[Rhs]) >>
  [](Match& _) {
    return Expression
      << (_(Op) << (Expression << _(Lhs)) << (Expression << _[Rhs]));
  },
```

And an updated well-formedness check:

``` c++
inline const auto wf_pass_add_subtract =
  wf_pass_expressions
  | (Add <<= Expression * Expression)
  | (Subtract <<= Expression * Expression)
  ;
```

A side effect of these operations is that our flat expression structure
where every token was a child of an expression is now a binary tree.
Let's see this develop by looking at the following expression:

``` typescript
1 + 2 * 3 - 4 + 5 / (6 - 7)
```

and how its tree evolves over time:

<table>
<tr><th>Iteration</th><th>Tree</th></tr>
<tr><td>expressions[end]</td><td>

``` mermaid
flowchart TD
  A[Expr]-->B[Int 1]
  A-->C[+]
  A-->D[Int 2]
  A-->E[*]
  A-->F[Int 3]
  A-->G[-]
  A-->H[Int 4]
  A-->I["#47;"]
  A-->J[Expr]
  J-->K[Int 6]
  J-->L[-]
  J-->M[Int 7]
```

</td></tr>
<tr><td>multiply_divide[0]</td><td>

``` mermaid
flowchart TD
  A[Expr]-->B[Int 1]
  A-->C[+]
  A-.->N[Expr]:::current
  N-.->E[*]
  E-.->DD[Expr]:::current
  DD-.->D[Int 2]
  E-.->FF[Expr]:::current
  FF-.->F[Int 3]
  A-->G[-]
  A-->H[Int 4]
  A-->I["#47;"]
  A-->J[Expr]
  J-->K[Int 6]
  J-->L[-]
  J-->M[Int 7]
  classDef current stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>multiply_divide[end]</td><td>

``` mermaid
flowchart TD
  A[Expr]-->B[Int 1]
  A-->C[+]
  A-->N[Expr]
  N-->E[*]
  E-->DD[Expr]
  DD-->D[Int 2]
  E-->FF[Expr]
  FF-->F[Int 3]
  A-->G[-]
  A-.->O[Expr]:::current
  O-.->I["#47;"]
  I-.->HH[Expr]:::current
  HH-.->H[Int 4]
  I-.->J[Expr]
  J-->K[Int 6]
  J-->L[-]
  J-->M[Int 7]
  classDef current stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>add_subtract[0]</td><td>

``` mermaid
flowchart TD
  A[Expr]-.->CC[Expr]:::current
  CC-.->C[+]
  C-.->BB[Expr]:::current
  BB-.->B[Int 1]
  C-.->N[Expr]
  N-->E[*]
  E-->DD[Expr]
  DD-->D[Int 2]
  E-->FF[Expr]
  FF-->F[Int 3]
  A-->G[-]
  A-->O[Expr]
  O-->I["#47;"]
  I-->HH[Expr]
  HH-->H[Int 4]
  I-->J[Expr]
  J-->K[Int 6]
  J-->L[-]
  J-->M[Int 7]
  classDef current stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
<tr><td>add_subtract[1]</td><td>

``` mermaid
flowchart TD
  A[Expr]-->GG[-]
  GG-.->CC[Expr]
  CC-->C[+]
  C-->BB[Expr]
  BB-->B[Int 1]
  C-->N[Expr]
  N-->E[*]
  E-->DD[Expr]
  DD-->D[Int 2]
  E-->FF[Expr]
  FF-->F[Int 3]
  GG-.->O[Expr]
  O-->I["#47;"]
  I-->HH[Expr]
  HH-->H[Int 4]
  I-->J[Expr]
  J-->K[Int 6]
  J-->L[-]
  J-->M[Int 7]
```

</td></tr>
<tr><td>add_subtract[end]</td><td>

``` mermaid
flowchart TD
  A[Expr]-->GG[-]
  GG-->CC[Expr]
  CC-->C[+]
  C-->BB[Expr]
  BB-->B[Int 1]
  C-->N[Expr]
  N-->E[*]
  E-->DD[Expr]
  DD-->D[Int 2]
  E-->FF[Expr]
  FF-->F[Int 3]
  GG-->O[Expr]
  O-->I["#47;"]
  I-->HH[Expr]
  HH-->H[Int 4]
  I-->J[Expr]
  J-->L[-]
  L-.->KK[Expr]:::current
  KK-.->K[Int 6]
  L-.->MM[Expr]:::current
  MM-.->M[Int 7]
  classDef current stroke-width:4px,stroke-dasharray:5 5;
```

</td></tr>
</table>

The resulting tree can be evaluated recursively to obtain a result
which respects the operator precedence rules.

### Pass 4: Trim

At this point there may be a few nodes where you have an
expression as a child of an expression. Before we proceed we want
to clean up the tree so that all expressions really are binary
calculation trees.  Rule:

``` c++
// End is a special pattern which indicates that there
// are no further nodes. So in this case we are matching
// an Expression which has a single Expression as a
// child.
T(Expression) << (T(Expression)[Expression] * End) >>
    [](Match& _) { return _(Expression); },
```

Well-formed check:

``` c++
inline const auto wf_pass_trim =
  wf_pass_add_subtract
  | (Expression <<= wf_operands_tokens)
  ;
```

At first this may seem like a trivial pass. Why not include it
in another pass? However, the "smaller" a pass, the less it does,
the easier it is to reason about and support. There is no downside
to having many passes, so when using Trieste err on the side of
more, smaller passes like this one.

At the end of these passes our tree looks like this:

```
top
└── calculation
    ├── symbols: {x y z}
    ├── assign
    │   ├── ident x
    │   └── expression
    │       └── +
    │           ├── expression
    │           │   └── int 5
    │           └── expression
    │               └── int 10
    ├── assign
    │   ├── ident y
    │   └── expression
    │       └── +
    │           ├── expression
    │           │   └── -
    │           │       ├── expression
    │           │       │   └── int 1
    │           │       └── expression
    │           │           └── int 9
    │           └── expression
    │               └── ident x
    ├── output
    │   ├── string "1"
    │   └── expression
    │       └── +
    │           ├── expression
    │           │   └── ident x
    │           └── expression
    │               └── ident y
    ├── assign
    │   ├── ident z
    │   └── expression
    │       └── /
    │           ├── expression
    │           │   └── *
    │           │       ├── expression
    │           │       │   └── int 5
    │           │       └── expression
    │           │           └── ident x
    │           └── expression
    │               └── ident y
    └── output
        ├── string "2"
        └── expression
            └── ident z
```

### Pass 5: Checking References

At this stage, we will check that all of our identifiers are defined.
Recall that we are building a symbol table for the calculation. Trieste
allows us to lookup identifiers that are used in an expression in the
symbol table. This means that we can verify that all identifiers being
used are properly defined before use. Also, as a bonus, Trieste will
automatically detect multiple declaration errors as part of building
the symbol table. Here is an example of a rule that checks the
identifiers:

``` c++
In(Expression) * T(Ident)[Id] >>
  [](Match& _) {
    auto id = _(Id);            // the Node object for the identifier
    auto defs = id->lookup();   // a list of matching symbols
    if (defs.size() == 0)
    {
      // there are no symbols with this identifier
      return err(id, "undefined");
    }

    return Ref << id;
  }
```

This means that from this point onwards we know that every identifier
in the tree is valid.

### Pass 6: Maths

By this point we can start collapsing the expression trees to a single
literal value. There are a few rules that will do this for us. First,
we lookup all identifiers and replace them with their values:

``` c++
T(Expression) << (T(Ref) << T(Ident)[Id])(
  [](auto& n) { return can_replace(n); }) >>
  [](Match& _) {
    auto defs = _(Id)->lookup();
    auto assign = defs.front();
    // the assign node has two children: the ident, and its value
    // this returns the second
    return assign->back();
  },
```

Note that this pass will run many times (for example, with the sample
program we've been looking at it runs 8 times) and so while this
may not be true at first, it will be eventually.

Next, we replace all expressions which have a single literal value
with a `Literal` node:

``` c++
T(Expression) << (T(Int) / T(Float))[Rhs] >>
  [](Match& _) { return Literal << _(Rhs); },
```

Now we are ready to start collapsing the maths nodes. Here is an
example of the logic for `Add`:

``` c++
int get_int(const Node& node)
{
  std::string text(node->location().view());
  return std::stoi(text);
}

double get_double(const Node& node)
{
  std::string text(node->location().view());
  return std::stod(text);
}

/* ... */

T(Add) << ((T(Literal) << T(Int)[Lhs]) * (T(Literal) << T(Int)[Rhs])) >>
  [](Match& _) {
    int lhs = get_int(_(Lhs));
    int rhs = get_int(_(Rhs));
    // ^ here means to create a new node of Token type Int with the
    // provided string as its location.
    return Int ^ std::to_string(lhs + rhs);
  },

T(Add) << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
  [](Match& _) {
    double lhs = get_double(_(Lhs));
    double rhs = get_double(_(Rhs));
    return Float ^ std::to_string(lhs + rhs);
  },
```

These rules (and similar ones for `Subtract`, `Multiply`, and `Divide`)
will run again and again until every expression has been collapsed to
a single `Literal` node as we see below:

```
top
└── calculation
    ├── assign
    │   ├── ident x
    │   └── literal
    │       └── int 15
    ├── assign
    │   ├── ident y
    │   └── literal
    │       └── int 7
    ├── output
    │   ├── string "1"
    │   └── literal
    │       └── int 22
    ├── assign
    │   ├── ident z
    │   └── literal
    │       └── int 10
    └── output
        ├── string "2"
        └── literal
            └── int 10
```

### Pass 7: Cleanup

Those `Assign` nodes do nothing at this point, and so we can remove them.
We can also elide the `Literal` node for similar reasons. Rules:

``` c++
In(Calculation) * T(Assign) >> [](Match&) -> Node { return {}; },

T(Literal) << Any[Rhs] >> [](Match& _) { return _(Rhs); },
```

The final well-formedness check:

``` c++
inline const auto wf_pass_cleanup =
  wf_pass_maths
  | (Calculation <<= Output++)
  // note the use of >>= here. This allows us to have a choice
  // as a field by giving it a temporary name.
  | (Output <<= String * (Expression >>= wf_literal))
  ;
```

and our final tree:

```
top
└── calculation
    ├── output
    │   ├── string "1"
    │   └── int 22
    └── output
        ├── string "2"
        └── int 10
```

The complete driver is declared like this:

``` c++
Driver& driver()
  {
    static Driver d(
      "infix",
      nullptr,
      parser(),
      {
        expressions(),
        multiply_divide(),
        add_subtract(),
        trim(),
        check_refs(),
        maths(),
        cleanup(),
      });

    return d;
  }
```

## Running the `infix` Executable

By defining a Driver, we can then define an executable which will do
some incredibly useful things for us:

``` c++
int main(int argc, char** argv)
{
  return infix::driver().run(argc, argv);
}
```

Usage:

```
infix
Usage: ./dist/infix/infix [OPTIONS] SUBCOMMAND

Options:
  -h,--help                   Print this help message and exit
  --help-all                  Expand all help

Subcommands:
  build                       Build a path
  test                        Run automated tests
```

### `build`
The `build` command takes an infix source file and will produce an
AST file. Its usage is:

```
Build a path
Usage: ./dist/infix/infix build [OPTIONS] path

Positionals:
  path TEXT REQUIRED          Path to compile.

Options:
  -h,--help                   Print this help message and exit
  --help-all                  Expand all help
  -l,--log_level TEXT         Set Log Level to one of Trace, Debug, Info, Warning, Output, Error, None
  -w,                         Check well-formedness.
  -p,--pass TEXT:{parse,expressions,multiply_divide,add_subtract,trim,check_refs,maths,cleanup}
                              Run up to this pass.
  -o,--output TEXT            Output path.
  --dump_passes TEXT          Dump passes to the supplied directory.
```

for example:

    ./infix build simple.infix -l Info

outputs:

```
---------
Pass	Iterations	Changes	Time (us)
expressions	2	5	210
multiply_divide	1	0	82
add_subtract	2	2	153
trim	2	2	115
check_refs	2	1	109
maths	4	10	219
cleanup	2	2	78
---------
```

and creates the file `simple.trieste` containing:

``` lisp
infix
cleanup
(top
  {}
  (calculation
    {}
    (output
      (string 3:"x")
      (int 1:5))
    (output
      (string 8:"1 + 10")
      (int 2:11))))
```

### `test`
The `test` command will perform generative testing of each pass using
the well-formedness definitions. Usage:

```
Run automated tests
Usage: ./dist/infix/infix test [OPTIONS] [start] [end]

Positionals:
  start TEXT:{parse,expressions,multiply_divide,add_subtract,trim,check_refs,maths,cleanup}
                              Start at this pass.
  end TEXT:{parse,expressions,multiply_divide,add_subtract,trim,check_refs,maths,cleanup}
                              End at this pass.

Options:
  -h,--help                   Print this help message and exit
  --help-all                  Expand all help
  -c,--seed_count UINT        Number of iterations per pass
  -s,--seed UINT              Random seed for testing
  -l,--log_level TEXT         Set Log Level to one of Trace, Debug, Info, Warning, Output, Error, None
  -d,--max_depth UINT         Maximum depth of AST to test
  -f,--failfast               Stop on first failure
```

For each pass, it will use its input WF definition to produce
`seed_count` random inputs that adhere to that definition, process them
with the pass, and check them against the output WF definition. For
example

    ./infix test -f -c 1000 -l Info

outputs:

```
Testing x1000, seed: 3316469074
Testing pass: expressions
Testing pass: multiply_divide
Testing pass: add_subtract
Testing pass: trim
Testing pass: check_refs
Testing pass: maths
Testing pass: cleanup
```

This testing can be incredibly helpful in finding errors in the WF
definitions and the rewrite rules, but also requires that you explicitly
produce error messages for all possible syntax problems in each pass.

## Errors

Yet another advantage of a multi-pass rewrite system like Trieste is
the ability to produce fine-grained errors. Indeed, as we just mentioned,
the testing system of the driver will require these messages and help
you find the various edge and corner cases in your rules. Most of these
messages will likely be in the first pass (i.e. post-parse). 

The `Error` token allows the creation of a special node which we can
use to replace the erroneous node. This will then exempt that subtree
from the well-formedness check. This is the mechanism by which we can
use the testing system to discover edge cases, i.e. the testing will
not proceed to the next pass until all of the invalid subtrees have
been marked as `Error`. `Error` nodes can conveniently be created with 
the `err` function:

``` c++
auto err(NodeRange& r, const std::string& msg)
{
  return Error << (ErrorMsg ^ msg) << (ErrorAst << r);
}

auto err(Node node, const std::string& msg)
{
  return Error << (ErrorMsg ^ msg) << (ErrorAst << node);
}
```
In addition to the rewrite rules for valid expressions (pass 1) we saw before,
we should define `Error` rules. Since the rules are matched in order, 
the first rule below matches `Paren` nodes with no children (a 
previous rule will have handled those *with* children). A few examples
of `Error` rules for the expression pass:

```c++
// Empty parenthesis
T(Paren)[Paren] >> [](Match& _) { return err(_(Paren), "Empty paren"); },

// Ditto for malformed equals nodes
T(Equals)[Equals] >>
  [](Match& _) { return err(_(Equals), "Invalid assign"); },

// Orphaned print node will catch bad output statements
T(Print)[Print] >>
  [](Match& _) { return err(_(Print), "Invalid output"); },

// Our WF definition allows this, so we need to handle it.
T(Expression)[Rhs] << End >>
  [](Match& _) { return err(_(Rhs), "Empty expression"); },

// Same with this.
In(Expression) * T(String)[String] >>
  [](Match& _) {
    return err(_(String), "Expressions cannot contain strings");
  },
```

Standard practice is to write the "positive" rules for your language,
add the errors you can think of, and then discover edge and corner
cases via the testing. This does not just happen in the early stages
of the process, however. Take, for example, the `maths` pass. Let's look
at the logic for `Divide`:

``` c++
T(Divide)
    << ((T(Literal) << T(Int)[Lhs]) * (T(Literal) << T(Int)[Rhs])) >>
  [](Match& _) {
    int lhs = get_int(_(Lhs));
    int rhs = get_int(_(Rhs));
    if (rhs == 0)
    {
      return err(_(Rhs), "Divide by zero");
    }

    return Int ^ std::to_string(lhs / rhs);
  },
```

In a very cool way, Trieste allows us to detect divide by zero errors.
However, this now means that, potentially, some of the nodes in our
AST will be of type error, resulting in empty subexpressions. This means
we need to add some error handling, for example:

```c++
// Note how we pattern match explicitly for the Error node
In(Expression) *
    (MathsOp << ((T(Expression)[Expression] << T(Error)) * T(Literal))) >>
  [](Match& _) {
    return err(_(Expression), "Invalid left hand argument");
  },
```

By finding these errors explicitly we can propagate the error up the
tree, thus eventually allowing the bad subtree to be exempted from the
WF check and allowing the testing to proceed.

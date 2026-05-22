# Trieste Reference Sheet

## Tokens

A `Token` is the fundamental label on every node in the tree. Tokens are defined as global `TokenDef` objects:

```c++
inline const auto Int    = TokenDef("int",    flag::print);
inline const auto Assign = TokenDef("assign", flag::lookup | flag::shadowing);
```

The string passed to `TokenDef` is used when printing the tree. The optional second argument is a combination of flags:

| Flag | Description |
|------|-------------|
| `flag::print` | When printing the tree, also print the node's source location text. |
| `flag::symtab` | Attach a symbol table to nodes of this type. |
| `flag::defbeforeuse` | When a node with `flag::symtab` has this flag, definitions bound in the symbol table are only visible from later (than the defining node) in the same source file. |
| `flag::lookup` | Definitions of this type can be found by `node->lookup()`. |
| `flag::lookdown` | Definitions of this type can be found by `node->lookdown()`. |
| `flag::shadowing` | A definition of this type (with `flag::lookup`) shadows results from parent symbol tables. |

Trieste defines the following built-in tokens:

| Token | Description |
|-------|-------------|
| `Top` | Root of every tree. Has `flag::symtab`. |
| `File` | Produced by the parser for each parsed file. |
| `Directory` | Produced by the parser when parsing a directory. |
| `Group` | General-purpose grouping node created by the parser. |
| `Error` | Marks an error in the tree. Rewriting does not enter error nodes. |
| `ErrorMsg` | Child of `Error` carrying the message string (has `flag::print`). |
| `ErrorAst` | Child of `Error` pointing to the offending source location. |
| `Seq` | Used in effects to return a flat sequence (the `Seq` node itself is discarded). |
| `Lift` | Used in effects to lift children to an enclosing node. |
| `NoChange` | Returned from an effect to signal that no change was made. |
| `Reapply` | Returned from an effect to signal that the rewriter should re-examine the same position. |

See the section on effects below for how the built-in tokens from `Error` and below are used.

## Parsing

The parser converts a text source into an initial tree of `Group` nodes. It is created as a `Parse` object:

```c++
Parse p(depth::file, wf_parser);
```

The first argument controls how the input path is interpreted:

| `depth` | Behaviour |
|---------|-----------|
| `depth::file` | Parse a single file. |
| `depth::directory` | Parse all files in a directory (non-recursive). |
| `depth::subdirectories` | Parse all files in a directory tree. |

Regardless of depth, the result is always a single `Top` node with one `File` (or `Directory`) child per file parsed. The optional second argument is the well-formedness specification for the output of parsing; it is checked before the first rewrite pass runs.

### Modes and Rules

Rules are grouped into named _modes_ and registered on the `Parse` object:

```c++
p("start",
  {
    "[[:blank:]]+"    >> [](auto&) {},                      // skip whitespace
    "[[:digit:]]+\\b" >> [](auto& m) { m.add(Int); },
    "="               >> [](auto& m) { m.seq(Equals); },
    ";[\n]*"          >> [](auto& m) { m.term({Equals}); },
  });
```

A rule is a regex and a `Make` effect joined by `>>`. Rules for the current mode are tried in order; the first matching rule is applied. Regexes may include capture groups — capture group 0 is the full match, groups 1, 2, … are sub-groups. Rules should cover all possible input; any unmatched input is parsed as an `Invalid` node.

The parser always starts in the `"start"` mode. An effect can switch modes with `m.mode("other")`, which takes effect for the next token.

### `Make` API

The following methods are available on the `Make` object `m` inside a rule effect.

**Inspecting the cursor**
* `m.match(index = 0)` -- the `Location` of capture group `index` from the current match.
* `m.mode()` -- get the current mode name.
* `m.mode("name")` -- switch to the named mode for subsequent rules.
* `m.in(tok)` -- true if the cursor is currently inside a node of type `tok`.
* `m.group_in(tok)` -- true if the cursor is inside a `Group` whose parent is of type `tok`.
* `m.previous(tok)` -- true if the last child of the current parent node is of type `tok`.

**Building the tree**
* `m.add(tok, index = 0)` -- append a new child node of type `tok` to the current `Group`, using the location of capture group `index`. Creates a `Group` first if there is none.
* `m.extend(tok, index = 0)` -- if the last child is already of type `tok`, extend its location to cover the current match; otherwise behave like `add`.
* `m.push(tok, index = 0)` -- like `add`, but also move the cursor into the newly created node. Subsequent calls add children to that node.
* `m.pop(tok)` -- close the current `Group` (if any) and move the cursor up to the nearest ancestor of type `tok`. Throws if none exists.
* `m.seq(tok, skip = {})` -- start a _sequence_. Lifts the current `Group` to become the first child of a new `tok` node; subsequent adds continue as further children of the `tok` node. If the parent is already `tok`, simply moves the cursor into it. Token types in `skip` are popped before the sequence is created.
* `m.term(end = {})` -- close the current `Group` and move the cursor up. For each token type in `end`, also pops that ancestor (used to close a `seq`-created node at statement boundaries).
* `m.invalid()` -- extend (or create) an `Invalid` node at the current position, recording a parse error.
* `m.error(msg, index = 0)` -- insert an `Error` node with the given message at the location of capture group `index`.

### Pre/Post Hooks

The `Parse` object supports optional callbacks for processing files and directories:

* `p.prefile(f)` -- called before each file is parsed; return `false` to skip the file.
* `p.predir(f)` -- called before each directory is entered; return `false` to skip it.
* `p.postfile(f)` -- called after each file is parsed, receiving the file's subtree.
* `p.postdir(f)` -- called after each directory is processed.
* `p.postparse(f)` -- called once after the full parse is complete, receiving the `Top` node.
* `p.done(f)` -- called as a `Make` effect when the end of input is reached (allows cleanup of unclosed structures).

### Examples

* Parsing an assignment `x = 5 + 3;` using the rules from the beginning of this section:
  ```
  Initial:   Top > File, cursor at File

  "x"        add(Ident)  → File > Group[Ident]
  "="        seq(Equals) → File > Equals > Group[Ident], cursor at Equals
  "5"        add(Int)    → Equals > Group[Ident], Group[Int ...]
  "+"        add(Add)    → Equals > Group[Ident], Group[Int, Add ...]
  "3"        add(Int)    → Equals > Group[Ident], Group[Int, Add, Int]
  ";"        term({Equals}) → closes Group, pops Equals → cursor back at File
  ```


## Term rewriting

Term rewriting logically moves a cursor through the whole term and applies all rules in every position once or until fixpoint. The visiting order and fixpoint behaviour is set via the direction flags:

* `dir::bottomup` -- Visit nodes bottom up, left to right.
* `dir::topdown`  -- Visit nodes top down, left to right.
* `dir::once`     -- Visit each position once instead of until fixpoint.

A rule consists of a pattern and an effect. For each cursor position and rule, the pattern selects zero or more sibling terms that matches the pattern and these terms are replaced by the results of the effect. Pattern matching can bind substructures of the selected terms which can be used in the effect. Unless the pass is `dir::once`, whenever a rule matches some nodes the cursor will reset to before the first child of the current parent node.

Effects are typically written as lambdas and a pattern is separated from its effect by `>>`. The following rewrite rule matches a single `Foo` node and replaces it by a `Bar` node:

```c++
T(Foo) >>
  [](Match& _) -> Node { return Bar; }
```

(Often the type annotation `-> Node` can be omitted)

### Patterns

* `T(tok)` -- a single term with token tok.
* `T(tok1, .., tokn)` -- a single term with one of the specified tokens.
* `Any` -- any single term.

* `!a` -- not a.
  * To avoid unexpected behaviour, let `a` be a pattern that consumes a single term. `!a` always consumes a single term on success, regardless of `a`. For negation that does not select anything, use `--a`.
* `a << b` -- `a` with children matching `b`.
  * To avoid unexpected behaviour, let `a` match a single term. If a selects more than one thing, `b` will be matched for the children of the first term. If `a` selects nothing, the program will crash.

* `a * b` -- `a` followed by `b`.
* `a / b` -- `a` or `b` (preferring `a`).

* `~a` -- optionally `a` (always succeeds).
* `a++` -- zero or more `a`.
  * Note that `a++` is greedy and that matching does not backtrack. For example, `Any++ * T(Bar)` will not match `Foo Foo Bar` since `Any++` consumes all three terms. In order to simulate backtracking, use negative lookahead: `(Any * --T(Bar))++ * Any * T(Bar)`.

* `a[Name]` -- match `a` and bind result to `Name` (`Name` must be a token).
  * Note that name binding cannot be done inside (negative) lookahead patterns, i.e. in `a` for `++a` or `--a`.
  * You should also avoid binding names to patterns that do not consume anything, i.e. `++a`, `--a`, `In`, `Start` or `End` as the result will always be an empty sequence.
* `a(f)` -- match `a` and require that `f` returns true for the terms selected by `a`.
  * `f` is typically written as a lambda.

* `++a` -- matches if `a` matches, but does not select anything.
* `--a` -- matches if `a` does not match, but does not select anything.

* `In(tok1, .., tokn)` -- matches if the cursor is directly inside a term with one of the listed tokens.
* `In(tok1, .., tokn)++` -- matches if the cursor is nested inside a term with one of the listed tokens (directly or recursively).

* `Start` -- matches if the cursor is before the first child of a term.
* `End` -- matches if the cursor is after the last child of a term.

#### Examples

* Match an `Int` or `Var` node, followed by a `Plus` node, followed by an `Int` or `Var` node. Bind the two operands to `Lhs` and `Rhs` respectively (note that this requires that `Lhs` and `Rhs` are tokens):
  ```c++
  T(Int, Var)[Lhs] * T(Plus) * T(Int, Var)[Rhs]`
  ```

* Match a `Plus` node whose first two children are `Int` or `Var` nodes. Bind the two children to `Lhs` and `Rhs` respectively:
  ```c++
  T(Plus) << (T(Int, Var)[Lhs] * T(Int, Var)[Rhs])
  ```
  * If you want to match a node that has *exactly* two children, use the `End` pattern:
    ```c++
    T(Plus) << (T(Int, Var)[Lhs] * T(Int, Var)[Rhs] * End)
    ```

* Match two `Int` or `Var` nodes that are direct children of a `Plus` node. Bind the two children to `Lhs` and `Rhs` respectively. Note that there may be other nodes before or after the two matched nodes:
  ```c++
  In(Plus) * T(Int, Var)[Lhs] * T(Int, Var)[Rhs]
  ```

* Match zero or more `Field` nodes followed by zero or more `Method` nodes under a `Class node. Bind each sequence to `Fields` and `Methods` respectively:
  ```c++
  In(Class) * (T(Field)++)[Fields] * (T(Method)++)[Methods]
  ```

### Effects

An effect is a C++ function that takes a single argument of type `Match&` (named `_` below) and returns a `Node`. When writing effects, the following constructs are available.

* `tok ^ b` -- create a `tok` node with the same payload as `b`.
* `tok ^ "foo"` -- create a tok term with the payload `"foo"`.
* `_.fresh()` -- generate a payload that has not been used in the context where the match happened.

* `a << b` -- `a` with `b` appended as a child.
  * The expression returns `a` and has the side effect that `b` is added as a child to `a`. This means that it can be used both in a `return` statement and as a stand-alone statement.

* `Seq` -- produce a sequence from the children of this node (the `Seq` node itself is discarded). Because of left-associativity of `<<`, `Seq << a << b << c` results in the sequence `a b c`.

* `_[Name]` -- get the sequence of terms bound to `Name`.
* `_(Name)` -- get the single term bound to `Name`.
  * If `Name` is bound to a sequence of terms, get the first of these. Note that if `Name` is bound to an empty sequence, the result is the next node where the empty sequence was matched. In other words, only use `_(Name)` when the binding comes from a pattern that match exactly one thing.
* `*_[Name]` -- get the concatenated children of the sequence of terms bound to `Name`. For each node `n` in the range, the **children** of `n` (not `n` itself) are appended. The typical use is unwrapping a `Group` node: `Foo << *_[Group]` gives a `Foo` node whose children are the former children of the bound `Group`.
  * If `Name` is bound to an empty sequence, the program crashes.
  * To append the bound nodes themselves (rather than their children), use `node << _[Name]` without the `*`.

* `Lift << tok` -- lift the children of this node to the nearest enclosing term with token `tok` (having no such enclosing term is an error). For example `Lift << Foo << a << b` will lift `a b` to be a child of the nearest enclosing `Foo` term.

* `NoChange` - don't replace the matched subtree and don't signal that a change has been made.
* `{}` - remove the matched subtree completely (replace it with an empty sequence).

* `Reapply` - replace the matched subtree and signal that the rewriter should re-examine the same position.
  * Mainly useful in `dir::once` passes, where without `Reapply` each position is only visited once.

* `Error` - signal an error, aborting after finishing the current pass iteration. An `Error` node typically has two children with tokens `ErrorMsg` and `ErrorAst`. The payload of the `ErrorMsg` node is printed as an error message and the location of the child `ErrorAst` node is used to point to the source of the error.
  * An `Error` node can have any number of `ErrorMsg` nodes and any number of other nodes which will be treated as `ErrorAst` nodes.

### Examples

Here are a few rewrite rules from the infix tutorial.

* Find `Paren` nodes directly inside `Expression` nodes and replace them with `Expression` nodes containing the children of the `Group` node inside the `Paren` node:
  ```c++
  In(Expression) * (T(Paren) << T(Group)[Group]) >>
    [](Match& _) { return Expression << *_[Group]; }
  ```
* Find `Equals` nodes which are directly inside `Calculation` nodes and whose first child is a group containing an identifier (bound to `Id`) and whose second child is a `Group` (bound to `Rhs`). Replace them with `Assign` nodes containing just the identifier and the children of the right-hand side group node:
  ```c++
  In(Calculation) *
      (T(Equals) << ((T(Group) << T(Ident)[Id]) * T(Group)[Rhs])) >>
    [](Match& _) { return Assign << _(Id) << (Expression << *_[Rhs]); }
  ```
* Find `Expression` nodes containing *only* another `Expression` node and replace it with the inner node:
  ```c++
  T(Expression) << (T(Expression)[Expression] * End) >>
    [](Match& _) { return _(Expression); }
  ```
* Find `Add` or `Subtract` nodes with no children and signal an error:
  ```c++
  (T(Add, Subtract))[Op] << End >>
    [](Match& _) { return Error << (ErrorMsg ^ "No arguments") << (ErrorAst << _(Op)); }
  ```

Note that the effect can contain arbitrary C++ code. For example, the following rule finds identifiers in expressions and checks that they are bound before replacing them with `Ref` nodes:

```c++
In(Expression) * T(Ident)[Id] >>
  [](Match& _) {
    auto id = _(Id); // the Node object for the identifier
    auto defs = id->lookup(); // a list of matching symbols
    if (defs.size() == 0)
    {
      // there are no symbols with this identifier
      return err(id, "undefined");
    }

    return Ref << id;
  }
```

(The `err` function abstracts the creation of an `Error` node)

## Well-Formedness specifications

Each rewrite pass in Trieste can have a corresponding well-formedness specification. It specifies the valid tree shape for that particular pass. The well-formedness specification is also used to create name bindings for field accesses, to bind terms to symbol tables and to generate random trees for fuzz testing. Between each pass, it is checked that the tree shape conforms with the well-formedness specification, labels are bound to nodes and symbol tables are populated. If a pass does not have a well-formedness specification, no checking is done.

A well-formedness specification is a set of **shapes** combined with `|`. Each shape binds a token to its allowed children:

```
tok <<= fields
```

`fields` is either a **repetition** or a **sequence**.

**Repetition** — the node has a variable number of children, all matching a set of tokens:
* `(tok1 | tok2 | ...)++` — zero or more children of one of the listed types.
  * Lower and upper bounds can be specified as `(...)++[n]` — at least `n` children; `(...)++[n][m]` — between `n` and `m` children inclusive.
* `~(tok1 | tok2 | ...)` — zero or one child (optional).

**Sequence** — the node has a fixed number of children, one per field, joined by `*`:

`field1 * field2 * ... * fieldN`

Each field is either:
* `tok` — one child of type `tok`, accessible by label `tok`.
* `label >>= tok` — one child of type `tok`, accessible by label `label` (where `label` is a `Token`).
* `label >>= (tok1 | tok2 | ...)` — one child of one of the listed types, accessible by label `label`. A choice must always be labelled when it appears inside a sequence.

Labels are used for defining symbol table bindings (see below) and for projecting fields out of `Nodes` with `n / tok`.

### Extending and modifying specifications:

`wf1 | wf2` merges two specifications; shapes in `wf2` override any conflicting shapes in `wf1`. This is useful when extending a previous specification:

```c++
inline const auto wf_pass_multiply_divide =
  wf_pass_expressions
  | (Multiply <<= Expression * Expression)
  | (Divide <<= Expression * Expression)
  ;
```

Existing shapes can also be removed using `wf - tok`. Note that this does not affect shapes that list `tok` as a child, it only requires that `tok` has zero children (which is the default when no shape is listed for a token). Similarly, choices can be extended or reduced. The following example defines a choice which contains the same tokens as `wf_parse_tokens` but with `String`, `Paren` and `Print` removed and `Expression` added:

```c++
inline const auto wf_expressions_tokens =
  (wf_parse_tokens - (String | Paren | Print)) | Expression;
```

### Symbol table binding:

Symbol table entries are controlled by the well-formedness specification. `(tok <<= fields)[key]` specifies that nodes of type `tok` are bound into their nearest ancestor's symbol table using the value of the child named `key` as the lookup key. `key` must be one of the field labels in the sequence. Note that the `TokenDef` of `tok` must have `flag::lookup` (or `flag::lookdown`) for lookup (or lookdown) to work.


### Examples

* Labels (specified with the `>>=` operator) can be used to access specific children of a node. Consider the shape for `Addition` nodes from infix, where each `Expression` has been labelled:
  ```c++
  (Add <<= (Lhs >>= Expression) * (Rhs >>= Expression))
  ```
  The labels (`Lhs` and `Rhs`) are other `Tokens`. The left child of an `Add` node `n` can be accessed by `n / Lhs` in subsequent passes.

* A symbol table is a map from the _value_ of one term to another and can, for example, be used to map variables to expressions. Recall the following shape from the well-formedness specification for infix:
  ```
  (Assign <<= Ident * Expression)[Ident]
  ```
  Nodes of type `Assign` will be bound to the symbol table of its closest ancestor defined with `flag::symtab`. The `Top` node is defined with `flag::symtab` and is the ancestor of all other nodes so there is always at least one symbol table. In the above example, the value of the `Ident` node is the lookup key in the symbol table. Note that the `Assign` node (not the `Ident` node) must be defined with `flag::lookup` for the lookup to work for this example. The same key can be bound to several nodes (possibly of different types) in the same symbol table if the key token type (`Ident` in the infix example) is not defined with `flag::shadowing`.



# Trieste Reference Sheet

## Parsing

TODO


## Term rewriting

Term rewriting logically moves a cursor through the whole term and applies all rules in every position once or until fixpoint. The visiting order and fixpoint behaviour is set via the direction flags:

* `dir::bottomup` — Visit nodes bottom up, left to right.
* `dir::topdown`  — Visit nodes top down, left to right.
* `dir::once`     — Visit each position once instead of until fixpoint.

A rule consists of a pattern and an effect. For each cursor position and rule, the pattern selects zero or more sibling terms and these terms are replaced by the results of the effect. Pattern matching can bind substructures of the selected terms which can be used in the effect.

Effects are typically written as lambdas and a pattern is separated from its effect by `>>`. The following rewrite rule matches a single `Foo` node and replaces it by a `Bar` node:

```c++
T(Foo) >>
  [](Match& _) -> Node { return Bar; }
```

(Often the type annotation `-> Node` can be omitted)

### Patterns

* `T(tok)` — a single term with token tok.
* `T(tok1, .., tokn)` — a single term with one of the specified tokens.
* `Any` — any single term.

* `!a` — not a.
  * To avoid unexpected behaviour, let `a` be a pattern that consumes a single term. `!a` always consumes a single term on success, regardless of `a`. For negation that does not select anything, use `--a`.
* `a << b` — `a` with children matching `b`.
  * To avoid unexpected behaviour, let `a` match a single term. If a selects more than one thing, `b` will be matched for the children of the first term. If `a` selects nothing, the program will crash.

* `a * b` — `a` followed by `b`.
* `a / b` — `a` or `b` (preferring `a`).

* `~a` — optionally `a` (always succeeds).
* `a++` — zero or more `a`.
  * Note that `a++` is greedy and that matching does not backtrack. For example, `Any++ * T(Bar)` will not match `Foo Foo Bar` since `Any++` consumes all three terms. In order to simulate backtracking, use negative lookahead: `(Any * --T(Bar))++ * Any * T(Bar)`.

* `a[Name]` — match `a` and bind result to `Name` (`Name` must be a token).
  * Note that name binding cannot be done inside (negative) lookahead patterns, i.e. in `a` for `++a` or `--a`.
  * You should also avoid binding names to patterns that do not consume anything, i.e. `++a`, `--a`, `In`, `Start` or `End` as the result will always be an empty sequence.
* `a(f)` — match `a` and require that `f` returns true for the terms selected by `a`.
  * `f` is typically written as a lambda.

* `++a` — matches if `a` matches, but does not select anything.
* `--a` — matches if `a` does not match, but does not select anything.

* `In(tok1, .., tokn)` — matches if cursor is directly inside a term with one of the listed tokens.
* `In(tok1, .., tokn)++` — matches if cursor is nested inside a term with one of the listed tokens (directly or recursively).

* `Start` — matches if the cursor is before the first child of a term.
* `End` — matches if the cursor is after the last child of a term.

### Effects

An effect is a C++ function that takes a single argument of type `Match&` which is named `_` below.

* `tok ^ b` — create a `tok` term with the payload of `b`.
* `tok ^ "foo"` — create a tok term with the payload `"foo"`.
* `a << b` — `a` with `b` appended as a child.
* `Seq` — produce a sequence from the children of this node (the `Seq` node itself is discarded). Because of left-associativity of `<<`, `Seq << a << b << c` results in the sequence `a b c`.

* `_[Name]` — get the sequence of terms bound to `Name`.
* `_(Name)` — get the term bound to `Name`.
  * If `Name` is bound to a sequence of terms, get the first of these. Note that if `Name` is bound to an empty sequence, the result is the next node where the empty sequence was matched. In other words, only use `_(Name)` when the binding comes from a pattern that match exactly one thing.
* `*_[Name]` — get the concatenated children of the sequence of terms bound to `Name`.
  * If `Name` is bound to an empty sequence, the program crashes.

* `Lift << tok` — lift the children of this node to the nearest enclosing term with token `tok` (having no such enclosing term is an error). For example `Lift << Foo << a << b` will lift `a b` to be a child of the nearest enclosing `Foo` term.

* `NoChange` - don't replace the matched subtree and don't signal that a change has been made.
* `{}` - remove the matched subtree completely (replace it with an empty sequence).

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
    [](Match& _) { return Error << ErrorMsg "No arguments" << (ErrorAst << _(Op)); }
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

A well-formedness specification consists of several shapes specifying relationships of `Tokens`:

* `tok <<= tok1 ... tokn` - `tok` has children `tok1 ... tokn`,
  where the relationship between the children is specified with the following operators:
  * `tok1 * tok2 * ... * tokn` - specifies a sequence of siblings `tok1` to `tokn`.
  * `tok1 | tok2 | ... | tokn` - specifies a choice of tokens `tok1`, `tok2`, up to `tokn`.
  * `label >>= tok` - gives the label `label` to `tok`. The `label` is itself a `Token`. Allows choice of tokens to appear in a sequence, e.g., `(label >>= tok1 | tok2) * tok3`. This is disallowed without a label. Sequences or repetitions cannot be labelled.
  * `tok++` - zero or more `Tokens`. A choice between tokens can also be repeated (`(tok1 | tok2)++`) but not sequences or labelled tokens. Repetition can be given a lower bound with `[]`, e.g., `++[2]`.
* `(tok <<= tok1 * tok2)[tok1]` - binds `tok` nodes to a symbol table with `tok1` as the lookup key.

### Example: Labels

Labels (specified with the `>>=` operator) can be used to access specific children of a node. Consider the shape for `Addition` nodes from infix, where `Expression` has been labelled:

`(Add <<= (Lhs >>= Expression) * (Rhs >>= Expression))`

The labels (`Lhs` and `Rhs`) are other `Tokens`. The left child of an `Add` node `n` can be accessed by `n/Lhs` in subsequent passes.

### Example: Symbol tables
A symbol table is a map from the _value_ of one term to another and can, for example, be used to map variables to expressions. Recall the following shape from the well-formedness specification for infix:

`(Assign <<= Ident * Expression)[Ident]`

Nodes of type `Assign` will be bound to the symbol table of its closest ancestor defined with `flag::symtab`. The `Top` node is defined with `flag::symtab` and is the ancestor of all other nodes so there is always at least one symbol table. In the above example, the value of the `Ident` node is the lookup key in the symbol table. Note that the `Assign` node (not the `Ident` node) must be defined with `flag::lookup` for the lookup to work for this example. The same key can be bound to several nodes (possibly of different types) in the same symbol table if the key token type (`Ident` in the infix example) is not defined with `flag::shadowing`.

### Random tree generation
TODO

### Checking well-formedness
TODO

TODO: lookdown, fresh variables

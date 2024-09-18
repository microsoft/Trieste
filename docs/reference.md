# Trieste Reference Sheet

## Parsing

TODO


## Term rewriting

Term rewriting logically moves a cursor through the whole term and applies all rules in every position once or until fixpoint. The visiting order and fixpoint behaviour is set via the direction flags:

* `dir::bottomup` — Visit nodes bottom up, left to right
* `dir::topdown`  — Visit nodes top down, left to right
* `dir::once`     — Visit each position once instead of until fixpoint

A rule consists of a pattern and an effect. For each cursor position and rule, the pattern selects zero or more sibling terms and these terms are replaced by the results of the effect. Pattern matching can bind substructures of the selected terms which can be used in the effects.

### Patterns

* `T(tok)` — a single term with token tok
* `T(tok1, .., tokn)` — a single term with one of the specified tokens
* `Any` — any single term

* `!a` — not a
  * To avoid unexpected behaviour, let `a` be a pattern that consumes a single term. `!a` always consumes a single term on success, regardless of `a`. For negation that does not select anything, use `--a`.
* `a << b` — `a` with children matching `b`
  * To avoid unexpected behaviour, let `a` match a single term. If a selects more than one thing, `b` will be matched for the children of the first term. If `a` selects nothing, the program will crash.

* `a * b` — `a` followed by `b`
* `a / b` — `a` or `b` (preferring `a`)

* `~a` — optionally `a` (always succeeds)
* `a++` — zero or more `a`
  * Note that `a++` is greedy and that matching does not backtrack. For example, `Any++ * T(Bar)` will not match `Foo Foo Bar` since `Any++` consumes all three terms. In order to simulate backtracking, use negative lookahead: `(Any * --T(Bar))++ * Any * T(Bar)`

* `a[Name]` — match `a` and bind result to `Name` (`Name` must be a token)
* `a(f)` — match `a` and require that `f` returns true for the terms selected by `a`
  * `f` is typically written as a lambda

* `++a` — matches if `a` matches, but does not select anything
* `--a` — matches if `a` does not match, but does not select anything

* `In(tok1, .., tokn)` — matches if cursor is directly inside a term with one of the listed tokens
* `In(tok1, .., tokn)++` — matches if cursor is nested inside a term with one of the listed tokens (directly or recursively)

* `Start` — matches if the cursor is before the first child of a term
* `End` — matches if the cursor is after the last child of a term

### Effects

An effect is a C++ function that takes a single argument of type `Match&` which is named `_` below.

* `tok ^ b` — create a `tok` term with the payload of `b`
* `tok ^ "foo"` — create a tok term with the payload `"foo"`
* `a << b` — a with b appended as a child
* `Seq` — Produce a sequence from the children of this node (the `Seq` node itself is discarded). Because of left-associativity of `<<`, `Seq << a << b << c` results in the sequence `a b c`
* `_[Name]` — get the sequence of terms bound to `Name`
* `_(Name)` — get the term bound to `Name`
  * If `Name` is bound to a sequence of terms, get the first of these
* `*_[Name]` — get the concatenated children of the sequence of terms bound to `Name`
  * If `Name` is bound to an empty sequence, the program crashes
* `Lift << tok` — Lift the children of this node to the nearest enclosing term with token `tok` (having no such enclosing term is an error). For example `Lift << Foo << a << b` will lift `a b` to be a child of the nearest enclosing `Foo` term.

## Well-Formedness specifications

Each rewrite pass in Trieste must have a corresponding well-formedness specification. It specifies the valid tree shape for that particular pass. The well-formedness specification is also used to create name bindings for field accesses, to bind terms to symbol tables and to generate random trees for fuzz testing. Between each pass, it is checked that the tree shape conforms with the well-formedness specification, labels are bound to nodes and symbol tables are populated.

A well-formedness specification consists of several shapes specifying relationships of `Tokens`:

* `tok <<= tok1 ... tokn` - `tok` have children `tok1 ... tokn`, 
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


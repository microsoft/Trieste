# Beyond Infix

The Infix language is a useful introduction to Trieste, but by its very nature as an introductory tutorial, it cannot cover more advanced concepts.
This extended tutorial covers those less-documented Trieste features, while chronicling key aspects of editing an existing Trieste-based language implementation.

Our task at hand is to extend Infix with some more advanced concepts: multiple language versions, tuples, functions (both user-defined and built-in), and destructuring assignments.
Each of these topics explores a potential difficulty and how Trieste helps resolve it:
- Multiple language versions: given time, any programming language will change, and with multiple versions "in the wild", implementations may have to accept more than one of them.
  Correctly implementing something like this also helps us manage the collection of language extensions we explore in this document, which is why we look into this first.
- Tuples: a simple form of compound data, which has interesting syntactic interactions with the existing Infix features.
- Functions: a source of diverse scoping problems we can resolve in different ways using the tools provided by Trieste.
- Destructuring assignments: introducing a new kind of definition with complex scoping rules might seem like it requires a refactor.
  We show how such problems can be avoided in Trieste.

## Multiple Language Versions

Because a core goal of this project is to handle multiple divergent extensions of Infix, we should explain the options we have available to do this.
Most commonly, multiple versions of a language follow an evolution, where one version is a superset of the other, or a specific behavior is changed but all other features stay the same.
In Trieste, this means that we can write mostly-shared code between language versions, and when building our pass structure we can accept parameters that indicate whether a given feature should be enabled or disabled.
Ideally, this choice can be restricted to a small number, or perhaps even just one, pass that translates the possible variations into a common form that can pass through the rest of the compiler without further special cases.

The Infix tooling implements the remainder of this document using the technique above, by defining the complete resulting language in its WF definitions, then selectively disabling and altering passes in order to add and remove language features.
This means that by default a given pass either supports or ignores every Infix extension.
Feature-specific changes are annotated, and we will walk you through them as you read on.

To help identify and possibly ignore features you don't want to look at yet, the Well-Formedness (WF) definitions are written in a deliberately redundant style that looks like this:
```cpp
const auto wf =
  (Top <<= Calculation)
  | (Calculation <<= (Assign | Output)++)
  | (Assign <<= Ident * Expression)[Ident]
  | (Output <<= String * Expression)
  | (Expression <<= (Add | Subtract | Multiply | Divide | Ref | Float | Int))
  | (Ref <<= Ident)
  | (Add <<= Expression * Expression)
  | (Subtract <<= Expression * Expression)
  | (Multiply <<= Expression * Expression)
  | (Divide <<= Expression * Expression)
  // --- tuples extension ---
  | (Expression <<= (Tuple | TupleIdx | TupleAppend | Add | Subtract | Multiply | Divide | Ref | Float | Int))
  | (Tuple <<= Expression++)
  | (TupleIdx <<= Expression * Expression)
  | (TupleAppend <<= Expression++)
  // ...
  ;
```

At the top, you can see the original Infix definitions untouched.
Below, marked by `--- tuples extension ---`, you can see repeated definitions that include the added feature, tuples in this case.
In this case, the tuples feature leaves many things alone (assignments, output, etc. are not touched), but overrides the `Expression` well-formedness so that it includes the added tuple expressions.
Other definitions do similar things, partially overriding the definitions above it.

A reader focusing on earlier parts of this document can safely ignore the definitions below, and read the simpler language as-is.
Similarly, rules mentioning the omitted constructs can be ignored.
If you write programs that use only features you know, rules regarding unknown features are unreachable.
The implementation will also reject inputs that use features they are not supposed to, and defaults to the original Infix language.

Note that not all language designs support this kind of layering.
Some design decisions went into ensuring the implementation was easy to follow as you read the tutorial, as opposed to supporting all variations of a feature, and we leave exploring those possibilities as an exercise for the reader.
One such feature is removing the requirement that functions are defined before they are used - many popular programming languages allow definitions in any order, and so can we with a little effort.
For more of a challenge, consider that with this feature it becomes possible to write recursive, or even mutually recursive, functions.
Either detect and reject that case as an error, or try implementing recursive function evaluation.

**Safety note when capturing data in passes:**
it is quite easy to cause memory safety bugs by accidentally holding references to objects captured during pass construction, whose lifetime might be shorter than that of the pass itself.
Make sure to copy or `shared_ptr` data into your pass, rather than holding a long-lived reference to something whose lifetime might be uncertain.
In our example, `infix::Config` is a struct with a few booleans, so it would be fine to copy around.
Larger objects might benefit from `shared_ptr` or similar machinery.
Since we specifically used `infix::Config` to enable or disable rules, we opted to pass in pointers to `ToggleYes` and `ToggleNo` functions rather than capturing the config itself.
Regardless of how you proceed, remember to use AddressSanitizer to check for problems - when developing this extension, one resource management issue only appeared during cross-platform testing.
It was immediately reproducible locally using sanitizers, however.

## Tuples in Infix

To start with, what counts as a tuple?
It's an immutable data structure that holds 0 or more ordered elements of any type (for some notion of type).
There are a few ways to write them, but they usually involve separating the elements with commas (`,`).
They are often written surrounded by parentheses, as in `(a, b, c)`, but some languages (like Python) make the parentheses optional, meaning that `a, b, c` on its own is a valid tuple.
Some languages allow trailing commas at the end of a tuple, as in `(a, b, c,)`, which can be helpful when writing multiline literals.
The empty tuple and one-element tuple are almost universally edge cases, where you need to write something like `(,)` for an empty tuple.
A one-element tuple must then be written `(x,)`, with a mandatory trailing comma to disambiguate from `(x)`, which just means `x` instead with no tuple.

This section documents how Trieste can be used to implement tuples that have this syntax, with both mandatory and optional parentheses.
Two of our methods make minimal additions to the parser and express the majority of their logic as additional rewrite rules.
We also discuss a third method, which front-loads much of the parsing workload onto the parser, trading in some language design flexibility for simpler reader rules.

### Extending the AST

A neat thing about Trieste is that, regardless of how tuples are lexically written, all versions of tuples we discuss have the same AST.
To start with, we only introduce tuple literals -- we will consider how to extract elements from a tuple and append tuples to one another later.

For just tuple literals, the AST needs two key extensions: a new token type, and extending the set of possible expressions.

The token definition is like any other, with no special flags.
```cpp
inline const auto Tuple = TokenDef("infix-tuple");
```

The AST can be extended in 2 lines, adding tuples as a type of expression, and specifying that tuple literals can have 0 or more expressions as children:
```cpp
const auto wf =
  // ...
  | (Expression <<= (Tuple | Add | Subtract | Multiply | Divide | Ref | Float | Int))
  | (Tuple <<= Expression++)
```

We will come back to this definition when adding the other two tuple primitives, but all similar changes will follow the same formula.
More importantly, this shows how little difference many lexical-level or syntax sugar-level changes can make to a Trieste based language's main AST, since a lot of nuances can be removed when reading the language, before reaching the main AST.

### The Simplest Parser Modification

Lexically speaking, the simplest way to extend the parser for reading tuples is to add a comma token that directly corresponds to the `,` character.

The token definition is standard for Trieste:
```cpp
inline const auto Comma = TokenDef("infix-comma");
```

Unlike tokens used in the public interface for Infix, however, this token can be kept as internal since it will be eliminated and replaced by either `Error` or `Tuple` expressions by the reader.

Then, we can allow the parser to generate `Comma` tokens when it sees a lexical comma:
```cpp
R"(,)" >> [](auto& m) {
  m.add(Comma);
},
```

This action follows the same template as the arithmetic operators already in the language.
No context or complex logic is used here; subsequent passes will make sense of what these commas mean.

As an aside on adding more token types, we find that defining more token types with specific syntactic meaning leads to clearer programs.
In principle, it would be possible to re-use the `Tuple` token and not introduce an additional definition, but this would make the implementation's behavior less clear.
Not only might commas be used outside tuples (imagine adding C-style function calls with comma-separated arguments to Infix!), but re-using a token weakens Trieste's patterns, fuzzing, and well-formedness facilities.

Changing something from one token type to another is easily monitored using well-formedness definitions, and can be easily detected using patterns.
Using the same token type to mean multiple things, especially when it means one thing in the input of a pass and another in the pass's output, opens us up to confusing situations.
It might hide a rule not applying at all when it should (unchanged AST satisfies output WF), and it can make it easier for a rule to apply to its own output in an infinite loop (pattern does not exclude rule output).
While not always possible, making the intended difference clear by changing a token type can help avoid these cases.

### Tuples as Low-precedence N-ary Operators

Compared to how Infix already works, the simplest way of parsing tuples, assuming we do not want to require parentheses around a tuple expression, is to treat `,` as a lowest-precedence infix operator.
That is, `x + y, y * z` becomes grouped as `(x + y) , (y * z)`.
Because of the meaning of parentheses as a grouping mechanism, `(x, y)` will always work if `x, y` works, meaning that this technique parses a strict superset of tuples that require parentheses.

This technique is not without its share of interesting semantics and edge cases however, the most prominent being the meaning of `x, y, z`: the expression should parse as a 3-element tuple, but if we just recognize `,` as a binary operator like `+` or `-`, we might parse `((x, y), z)` instead.
That is why this section title mentions "N-ary Operators".
Instead of just parsing `(expr) , (expr)` in one step, we must recognize all commas in the same expression as describing the same tuple, one which becomes larger and larger the more commas our expression contains.
We also need to deal with the special exception, the unary tuple, where a trailing comma is mandatory because only `x,` is a tuple, not `x`.

To do this, write a collection of recursive rules that leverage the fact that nested sub-expressions must have already been parsed into single nodes of type `Expression`.
This includes parenthesized groups, meaning that if the programmer actually writes `(x, y), z`, then the outermost expression will contain `(expr x , y) , z`, which "hides" the comma in the parentheses from consideration.
By adding parentheses, it remains possible to write nested tuples, rather than having all tuples unconditionally flatten themselves into one large tuple literal.

Here are the cases we use to parse tuples in this style, with rules numbered 1-5:
```cpp
// (1) nullary tuple, just a `,` on its own
T(Expression) << (T(Comma)[Comma] * End) >>
  [](Match& _) { return Expression << (Tuple ^ _(Comma)); },
// (2) two expressions comma-separated makes a tuple
In(Expression) *
    (T(Expression)[Lhs] * T(Comma) * T(Expression)[Rhs]) >>
  [](Match& _) { return (Tuple << _(Lhs) << _(Rhs)); },
// (3) a tuple, a comma, and an expression makes a longer tuple
In(Expression) *
    (T(Tuple)[Lhs] * T(Comma) * T(Expression)[Rhs]) >>
  [](Match& _) { return _(Lhs) << _(Rhs); },
// (4) the one-element tuple uses , as a postfix operator
T(Expression) <<
    (T(Expression)[Expression] * T(Comma) * End) >>
  [](Match& _) { return Expression << (Tuple << _(Expression)); },
// (5) one trailing comma at the end of a tuple is allowed (but not more)
T(Expression) << (T(Tuple)[Tuple] * T(Comma) * End) >>
  [](Match& _) { return Expression << _(Tuple); },
```

Crucially, note that several rules apply to the results of other rules, especially rule 3.
This pass makes use of Trieste's rewrite-until-done semantics.
The recursive nature of these rules requires special care when handling errors, because adding a catch-all for `,` nodes will flag errors mid-processing, preventing the compiler from parsing many kinds of tuple expression.

Instead, we put the following catch-all error rule in its own pass, so that it only detects commas that could not be consumed using the patterns above:
```cpp
In(Expression) * T(Comma)[Comma] >>
  [](Match& _) { return err(_(Comma), "Invalid use of comma"); },
```

To give an example of how these rules apply together, consider how our first example, `x, y, z`, is parsed:
- Start: `(expr x) , (expr y) , (expr z)`
- Rule 2 ⇾ `(tuple (expr x) (expr y)) , (expr z)`
- Rule 3 ⇾ `(tuple (expr x) (expr y) (expr z))`

For the same example with a trailing comma, we need rule 5 to account for that:
- Same as above ⇾ `(tuple (expr x) (expr y) (expr z)) ,`
- Rule 5 ⇾ `(tuple (expr x) (expr y) (expr z))`

Note that rule 5 can only apply once, ensuring that we do not accept nonsense expressions like `x, y,,,,,` with too many trailing commas.
We use the `End` pattern to indicate that we only accept the extra comma if it is the last token in the expression; if there was another comma after it, the pattern will not match and the extra comma should be caught by an error handling catch-all pattern.

Rules 1 and 4 handle cases where rule 2 cannot match: since rule 2 catches tuples of length 2 and above by separating 2 expressions with a comma, rule 1 catches tuples of length 0, and rule 4 catches tuples of length 1.
Rule 1 uses `End` again, and avoids `In` to ensure we catch only expressions with a single `,`.
Remember that the key difference between `In` and `<<` is that `In` (a) does not become part of the pattern and (b) does not require the pattern match to start at the beginning of the specified node.
Using `<<` lets us specifically talk about the entire contents of a node, starting at the beginning.
Similarly, rule 4 deals with unary tuples where, unlike rule 5, there cannot be an under-construction tuple to match because that requires at least 2 elements.

To show rule 1 in action, consider the expression `,`, to which no rules except 1 apply:
- Start: `,`
- Rule 1 ⇾ `(tuple)`

To show rule 4 in action, consider the expression `x ,`, where `x` is not a tuple so rule 5 does not apply:
- Start `x ,`
- Rule 4 ⇾ `(tuple x)`

Remember also that, because this pass goes after all the arithmetic operator passes, we can deal with nested arithmetic as well.
For the expression `x + y * 2 , x - y`, we get:
- Start: `(expr (x + (y * 2))) , (expr x - y)` (already parsed `+`, `-`, `*`, etc... `,` is treated as an unknown operator by those passes)
- Rule 2 ⇾ `(tuple (expr (x + (y * 2))) (expr x - y))`

**Exercise for the reader:**
can you rewrite this pass with fewer rules?
If you could find a way to consolidate rule 2 into rule 4, then things might be simpler... but you might run into trouble dealing with trailing commas in the end.
Remember that you can always add new token types, like `UnderConstructionTuple`, to disambiguate cases like this where multiple otherwise-identical situations end up with an over-general name like `Tuple`.
You can always use the testing infrastructure in this repository to validate your work.

### Tuples as a Special Case of Groups

If parentheses are mandatory for tuples in your language design, then you need to be able to tell apart which expressions are inside parentheses and which are not.
Because the rest of Infix's structure operates using `In(Expression)`, and because we want to be able to use `Expression` as a marker for nodes that are already known to be expressions (as opposed to unparsed operators and such), we wanted another way to mark an expression as "it might be a tuple".

For this, we used the marker node `TupleCandidate`.
An expression without parentheses will be translated as-is by the `expressions` pass, but for an expression with parentheses we add this `TupleCandidate` node as a marker to indicate to later passes that the expression might be a tuple expression.
The advantage of doing this is that all the other passes treat `TupleCandidate` as an unknown unary operator (in our case, with the lowest precedence), and parse all the other operators as usual while preserving the marker.

Adding the marker looks like this, replacing the original Infix `Paren` parsing rule in the `expressions` pass:
```cpp
(In(Expression) * (T(Paren)[Paren] << T(Group)[Group])) >>
  [](Match& _) {
    return Expression << (TupleCandidate ^ _(Paren)) << *_[Group];
  },
```

This conversion does almost the same thing as the original (move the parentheses' group contents directly into an `Expression` node), but it adds the prefix `TupleCandidate` to mark that this expression may or may not be a tuple later on.
Note that we use `^` to attach the `Paren` node's position to the `TupleCandidate` node, which may make debugging and error message creation easier later on.

We can also accept `()` as a valid empty tuple, since we have a marker for what a tuple might be.
This replaces the default infix behavior of rejecting `()` as invalid:
```cpp
In(Expression) * (T(Paren)[Paren] << End) >>
  [](Match& _) { return Expression << (TupleCandidate ^ _(Paren)); },
```

After parsing the arithmetic sub-expressions using the same unmodified passes, our rules for extracting tuples look like this:
```cpp
// the initial 2-element tuple case
In(Expression) * T(TupleCandidate)[TupleCandidate] *
    T(Expression)[Lhs] * T(Comma) * T(Expression)[Rhs] >>
  [](Match& _) {
    return Seq << _(TupleCandidate) << (Tuple << _(Lhs) << _(Rhs));
  },
// tuple append case, where lhs is a tuple we partially built, and rhs
// is an extra expression with a comma in between
In(Expression) * T(TupleCandidate)[TupleCandidate] * T(Tuple)[Lhs] *
    T(Comma) * T(Expression)[Rhs] >>
  [](Match& _) {
    return Seq << _(TupleCandidate) << (_(Lhs) << _(Rhs));
  },
// same as above, but if the tuple is on the other side
In(Expression) * T(TupleCandidate)[TupleCandidate] *
    T(Expression)[Lhs] * T(Comma) * T(Tuple)[Rhs] >>
  [](Match& _) {
    Node lhs = _(Lhs);
    Node rhs = _(Rhs);
    rhs->push_front(lhs);
    return Seq << _(TupleCandidate) << rhs;
  },
// the one-element tuple, where a candidate for tuple ends in a comma,
// e.g. (42,)
In(Expression) * T(TupleCandidate)[TupleCandidate] *
    T(Expression)[Expression] * T(Comma) * End >>
  [](Match& _) { return Tuple << _(Expression); },
// the not-a-tuple case. All things surrounded with parens might be
// tuples, and are marked with TupleCandidate. When it's just e.g. (x),
// it's definitely not a tuple so stop considering it.
T(Expression)
    << (T(TupleCandidate) * T(Expression)[Expression] * End) >>
  [](Match& _) { return _(Expression); },
// If a TupleCandidate has been reduced to just a tuple, then we're
// done.
T(Expression) << (T(TupleCandidate) * T(Tuple)[Tuple] * End) >>
  [](Match& _) { return Expression << _(Tuple); },
// Comma as suffix is allowed, just means the same tuple as without the
// comma
T(Expression)
    << (T(TupleCandidate) * T(Tuple)[Tuple] * T(Comma) * End) >>
  [](Match& _) { return Expression << _(Tuple); },
// Just a comma means an empty tuple
T(Expression) << (T(TupleCandidate) * T(Comma)[Comma] * End) >>
  [](Match& _) { return Expression << (Tuple ^ _(Comma)); },
// empty tuple, special case for ()
T(Expression) << (T(TupleCandidate)[TupleCandidate] * End) >>
  [](Match& _) { return Expression << (Tuple ^ _(TupleCandidate)); },
```

Compared to the previous section's tuple rules, a defining feature here is that every rule here is prefixed with `TupleCandidate`.
This means that, if there is no `TupleCandidate` marker, these rules will never apply to anything.
Using our catch-all for stray commas in the next pass, we can therefore report as errors any tuple-like constructions that are not inside parentheses.

The well-formedness definition asserts that this pass will eliminate all `TupleCandidate` nodes.
This helps find errors in the rules, where any nodes that should have been processed but weren't will trigger a well-formedness violation.
The extra rules that don't directly match the previous section are elimination cases that delete the `TupleCandidate` prefix from a fully-formed tuple expression.

**Exercise for the reader 1:**
an additional capability here is the ability to parse `()` as an empty tuple: we did not do this when treating `,` more like an operator, because we could not detect `()` later on in parsing.
You can add this capability to the other tuple parsing strategy by making a small extension to the `expression` pass that detects empty parentheses as their own token type.
Look at the behavior of the other two implementations for inspiration.

**Exercise for the reader 2:**
another way of achieving the same result as above might be to directly match the correct tuple pattern in the `expressions` pass.
Since we know that lexically all tuples must look like `(... , ... , ...)`, we can iterate over the range of tokens inside the `(...)` and extract the sub-ranges between `,` tokens into separate expressions.
That is, `(a..., b..., c...)` directly becomes `(tuple (expr a...) (expr b...) (expr c...))`.
The advantage of this approach is that we can eliminate tuple syntax very simply by adding a for loop to the existing `()` rule in the `expressions` pass.
The disadvantage is that this adds hard to observe/extend complication to that pass - if the lexical rules for commas get more complicated in any way, it would be wise to separate that logic out into further passes.
Try it out and see what you think.

### Parser Tuples

The simplest but least extensible way to support tuples in Infix is to do so directly in the parser.
This strategy requires little work since it re-uses a feature of Infix's parser (perhaps more intuitively, "tokenizer"), but consider also that because it lives directly in the parser, it is in some ways the least extensible approach.
There is no way to add a pre-processing pass that goes before the parser, so Trieste's rule layering techniques will not be available.

To add tuples to the parser, first we add a new token type, `ParserTuple`:
```cpp
inline const auto ParserTuple = TokenDef("infix-parser-tuple");
```
Like `Comma`, this token is defined as "internal" since it does not appear in a fully-parsed Infix program.

The new token type is incorporated into the existing mechanism for reading parentheses:
```cpp
R"(,)" >>
  [](auto& m) {
    if (m.in(Paren) || m.group_in(Paren) || m.group_in(ParserTuple))
    {
      m.seq(ParserTuple);
    }
    else
    {
      m.error("Commas outside parens are illegal");
    }
  },

// Parens. (unmodified from original, but included for context)
R"((\()[[:blank:]]*)" >>
  [](auto& m) {
    // we push a Paren node. Subsequent nodes will be added
    // as its children.
    m.push(Paren, 1);
  },

R"(\))" >>
  [](auto& m) {
    // terminate the current group
    m.term(terminators);
    // pop back up out of the Paren
    m.pop(Paren);
  },
```

Note that `ParserTuple` is added to `terminators`, which lists the kinds of nodes we might be adding children to while in between a pair of parentheses:
```cpp
const std::initializer_list<Token> terminators = {Equals, ParserTuple};
```

As a whole, this change to the parser lets pairs of parentheses with commas in them be converted into `(Paren (ParserTuple Group...))`.

The way this works hinges on the semantics of `m.seq(TokenType)`, due to which the above rules process `(a,b,)` as follows:
```
// m.push(Paren, 1):
// - add Paren node and put cursor inside
// - location is group 1 of regex match
(a,b,)
^

(Paren ...)

---
// cursor inside non-group, so add group and put (a) in it
(a,b,)
 ^

(Paren (Group (a) ...))

---
// m.seq(ParserTuple):
// - replace containing group with ParserTuple 
// - place existing group as first child of new ParserTuple
// - put cursor at end of ParserTuple
(a,b,)
  ^

(Paren (ParserTuple (Group (a)) ...))

---
// cursor inside non-group (ParserTuple), so add new group
// and put (a) in it
(a,b,)
   ^

(Paren (ParserTuple (Group (a)) (Group (b) ...)))

---
// m.seq(ParserTuple):
// containing group's parent is ParserTuple, so finish this
// group and put cursor at the end of ParserTuple
(a,b,)
    ^

(Paren (ParserTuple (Group (a)) (Group (b)) ...))

---
// m.term({..., ParserTuple}):
// - exit current group if any (not in this case)
// - exit ParserTuple, cursor at end of parent (Paren)
(a,b,)
     ^

(Paren (ParserTuple (Group (a)) (Group (b))) ...)

---
// m.pop(Paren):
// - exit current group if any (not in this case)
// - exit Paren, cursor at end of parent
(a,b,)
     ^

(Paren (ParserTuple (Group (a)) (Group (b))) ...)
```

There are a lot of edge cases and lazy-added groups in parser behavior, so hopefully this worked example is more informative than just a long list of possible behaviors.

The explanation above does not cover the multiple error conditions written in the `,` rule, however.
Because of the cursor-and-state-machine model in the parser, calling `m.seq(ParserTuple)` whenever we see a comma will work for valid programs, but will cause well-formedness violations on invalid programs because of how `ParserTuple` will end up "taking over" any containing group, even ones whose meaning is completely unrelated to it.
To exclude these cases, we ensure that we only try to add a `ParserTuple` node if we are inside a `Paren`, or in a group that is a child of a `Paren`, or in a group that is a child of a `ParserTuple` (that is, we're already constructing one).
If we are not in those cases, we reject `,` at the parser level by adding an error node instead.

To get these kinds of conditionals right, it was very useful to fuzz our implementation at the level of input strings.
It took multiple attempts and fuzzing-generated counter-examples to reach the conditional we present here.
Fortunately, the well-formedness checks helped the fuzzing process by flagging mistakes which lead to unexpected AST shapes.

**Danger:**
notice that grouping ranges of tokens using `m.seq` will always allow a trailing separator, such as a trailing comma in our case.
This is a feature given our tuple design, but it can have unexpected consequences for other use cases.
For example, the Infix language allows trailing `=` in assignments: `x = 1 =;` is equivalent to `x = 1;` because assignments are also implemented using `m.seq`.
As we did above, it might be possible to avoid these undesirable semantics with extra conditionals, but the more of those conditionals live in the parser, the more it may benefit readability and analyzability to express those semantics as passes and rules instead.

Once we have our parser tuples in our AST, we can extract them using rewrite rules.
Here are our commented rewrite rules from the `expressions` pass:
```cpp
// Special case: a ParserTuple with one empty group child is (,), which
// we consider a valid empty tuple.
In(Expression) *
    (T(Paren)
      << (T(ParserTuple)[ParserTuple] << ((T(Group) << End) * End))) >>
  [](Match& _) { return Expression << (ParserTuple ^ _(ParserTuple)); },
// Special case 2: a ParserTuple with no children is (), which is also a
// valid empty tuple.
In(Expression) * (T(Paren)[Paren] << End)(enable_if_parser_tuples) >>
  [](Match& _) { return Expression << (ParserTuple ^ _(Paren)); },
// This grabs any Paren whose only child is a ParserTuple, and unwraps
// that ParserTuple's nested groups into Expression nodes so future
// passes will parse their contents properly.
In(Expression) *
    (T(Paren) << (T(ParserTuple)[ParserTuple] << T(Group)++[Group]) *
        End) >>
  [](Match& _) {
    Node parser_tuple = ParserTuple;
    for (const auto& child : _[Group])
    {
      // Use NodeRange to make Expression use all of the child's
      // children, rather than just append the child. If you're on C++20
      // or above, you can directly use std::span here, or write *child
      // which behaves like a range.
      parser_tuple->push_back(
        Expression << NodeRange{child->begin(), child->end()});
    }
    return Expression << parser_tuple;
  },
```

While in general it would be possible to directly emit a `Tuple` token here, it would make the well-formedness definitions less clear for our multi-version Infix implementation.
So, we keep everything under `ParserTuple` until the other rules-based implementations are adding `Tuple` tokens of their own.
This helps our well-formedness definitions catch more errors, rather than being overly permissive and allowing `Tuple` tokens in many places.
In a language implementation that does not deliberately re-implement the same feature multiple times, this extra step would not be necessary.

Our one additional rule to extract the tuple structure looks like this:
```cpp
T(Expression) << (T(ParserTuple) << T(Expression)++[Expression]) >>
  [](Match& _) { return Expression << (Tuple << _[Expression]); },
```

**Exercise for the reader:**
while we did not implement it, it is also possible to implement tuples that do not require parentheses in the parser.
To do this, remove restrictions on where commas will be accepted in the parser (but not all the restrictions, or you'll have to deal with `x , = 1;`!), and alter the `expressions` pass to extract tuples from any additional cases.
In all of those cases the tuple elements will be pre-grouped, and as long as commas have a uniform enough meaning throughout your language, the technique may be simpler to implement than the rule-based parsing strategies discussed in previous sections.

### Evaluating a Tuple Expression

TODO: use Expression --> Literal as "pseudo-types" to track whether something has evaluated

TODO: a previous version of this tutorial used int and float nodes directly, rather than using a Literal prefix.
This is more compact for a very simple language, but it doesn't scale since it essentially makes you write out all possible nodes a value can be.
It is more scalable to describe a broad category, like our "literal" token, which allows us to generically identify evaluated and unevaluated terms even if the language changes to some extent.

TODO: how to represent (as WF) the intermediate state where we can have "stuck expressions", so we can catch errors. Yes, errors may well end up in a second pass if you're relying on fixpoints.

### Appending and Indexing

TODO: appending is a pseudo-function; our lang doesn't have functions so we special-case it with a keyword, like language builtings often are
TODO: indexing is a highest-precedence binop (excercise to reader, highlight gotchas)

TODO: for evaluation, these operations do not add any new literal types; they should all disappear in a fully evaluated program

### Error Discipline, Fuzzing, and Test Cases

TODO: fuzz testing is useful to check that your program doesn't outright crash, but it is easy to pass WF if your code emits `Error` or something valid-looking, even if it doesn't make sense.

TODO: explain the principles behind the `.expected` tests, why they matter, and some general guidance on how to write/maintain them. Consider also storing test collections as data, like JSON or YAML (which has good multiline tring embeds).

TODO: what do I even do with BFS?

--- older notes

Notes:
- it's possible to get lost combining tuple and call parsing
- "stuck terms" and the original version of the code; explain that you can do sometimes error handling in 1 pass but it doesn't really scale once you have many ways to fail (TODO: edits to the original tutorial, where there is only one way to have an evaluation succeed and cause an error (e.g. `1 / (1-1)`))
- p.s. and splitting one pass into 2 can cause `String * (Foo >>= Literal | Expression)` because one term becomes a `|`. Warn people for now, plot design improvements later.

Ideas:
- `(a, b, ...)` (note: do in parser, or have a `,` token and resolve with a pass)
- `a, b, ...` (note: interpret `,` as a binop? trouble with distinguishing tuples of tuples vs wider tuples?)
- consider more exotic delimiters? (`<<>>` from TLA+, or something)
- append, fst, snd type builtins (without explicit fns?)
- how might one implement a "prelude"? (omni-present collection of definitions that are implicitly available everywhere)

## n Ways to Design Functions

Ideas:
- call as name + syntactic tuple? e.g. `fn(a, b, ...)`
- what if the parameter position of a function call is special, e.g., named parameters or special markers?
- only currying? `fn a b ...` (function application becomes an invisible operator, which is interesting)
- first or second-class? How much does it cost to switch? (first class trivially subsumes second class, but we have to reject more programs... going second class to first class requires a fun desugaring, which you can just about do with tuples)
- lazy params (don't start with this one, but it's a nasty stress test to add it as a feature... rip off Scala's trick if we do, probably)

## Pattern Languages Lite: Destructuring Assignments

Ideas:
- Pattern in assignment, and possibly function argument position, to match and extract values from tuples, however we did tuple syntax.
- Discuss the magic `_` everyone uses.
- Can we use values (literals at least) in pattern position, as a kind of assertion? How expensive is that to change?

# Beyond Infix

The Infix language is a useful introduction to Trieste, but by its very nature as an introductory tutorial, it cannot cover more advanced concepts.
This extended tutorial covers those less-documented Trieste features, while chronicling key aspects of editing an existing Trieste-based language implementation.

Our task at hand is to extend Infix with some more advanced concepts: multiple language versions and tuples.
Each of these topics explores a potential difficulty and how Trieste helps resolve it:
- Multiple language versions: given time, any programming language will change, and with multiple versions "in the wild", implementations may have to accept more than one of them.
  Correctly implementing something like this also helps us manage the collection of language extensions we explore in this document, which is why we look into this first.
- Tuples: a simple form of compound data, which has interesting syntactic interactions with the existing Infix features.
  We will enumerate 3 primary ways one would implement tuples into Infix, and use them as a motivating example for a variety of design and testing topics.

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

> [!CAUTION]
> When capturing data in passes, it is quite easy to cause memory safety bugs by accidentally holding references to objects captured during pass construction, whose lifetime might be shorter than that of the pass itself.
> Make sure to copy or `shared_ptr` data into your pass, rather than holding a long-lived reference to something whose lifetime might be uncertain.
> In our example, `infix::Config` is a struct with a few booleans, so it would be fine to copy around.
> Larger objects might benefit from `shared_ptr` or similar machinery.
> Since we specifically used `infix::Config` to enable or disable rules, we opted to pass in pointers to `ToggleYes` and `ToggleNo` functions rather than capturing the config itself.
> Regardless of how you proceed, remember to use AddressSanitizer to check for problems - when developing this extension, one resource management issue only appeared during cross-platform testing.
> It was immediately reproducible locally using sanitizers, however.

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
Also, Trieste's fuzzer does not work on the parser.

To add tuples to the parser, first we add a new token type, `ParserTuple`:
```cpp
inline const auto ParserTuple = TokenDef("infix-parser-tuple");
```
Like `Comma`, this token is defined as "internal" since it does not appear in a fully-parsed Infix program.

The new token type is incorporated into the existing mechanism for reading parentheses, but it needs additional context to work properly.
We achieve this with a mixture of parser modes and auxiliary state variables.

First, parser modes are the names, such as `"start"` or `"parentheses"`, which label different collections of rules for parsing characters.
The original Infix parser had only the `"start"` label, but, for handling where commas are allowed, we need two states: inside parenthese and outside parentheses.
Both states have the almost the same rules, so we use a helper lambda to build both rulesets while parameterizing over the differences: the mode name, either `"start"` or `"parentheses"`, and the flag `in_parentheses`.

Second, we use a local variable stored in a shared_ptr:
```cpp
std::shared_ptr<size_t> parentheses_level = std::make_shared<size_t>(0);
```
This counts how many parentheses deep our parse is nested, so we can properly exit `"parentheses"` mode only when we have encountered enough `)` characters.

In this context, our rules look like this:
```cpp
R"(,)" >>
  [](auto& m) {
    if (in_parentheses)
    {
      m.seq(ParserTuple);
    }
    else
    {
      m.error("Invalid use of comma");
    }
  },

// Additional state handling is included here.
R"(\()" >>
  [](auto& m) {
    m.push(Paren);

    // We are inside parenthese: shift to that mode
    ++*parentheses_level;
    m.mode("parentheses");
  },

R"(\))" >>
  [](auto& m) {
    // terminate the current group
    m.term(terminators);
    // pop back up out of the Paren
    m.pop(Paren);

    if(*parentheses_level > 0)
    {
      --*parentheses_level;
    }

    if(*parentheses_level == 0)
    {
      m.mode("start");
    }
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
We avoid those by having a special mode that tracks how many parentheses deep we are, meaning that we will not call `.seq` outside of parentheses.
Be aware however that it is easy to write parser rules that go wrong on sufficiently malformed character inputs - the more complex your parser, the more useful it becomes to perform character-level input fuzzing.

> [!CAUTION]
> Notice that grouping ranges of tokens using `m.seq` will always allow a trailing separator, such as a trailing comma in our case.
> This is a feature given our tuple design, but it can have unexpected consequences for other use cases.
> For example, the Infix language used to allow trailing `=` in assignments: `x = 1 =;` was equivalent to `x = 1;` because assignments were also implemented using `m.seq`.
> It might be possible to avoid these undesirable semantics with extra conditionals, but the more of those conditionals live in the parser, the more it may benefit readability and analyzability to express those semantics as passes and rules instead.

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
In(Expression) * (T(Paren)[Paren] << End) >>
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

Now that we have parsed our tuples, we can process them through the rest of the Infix tooling.
That is, we should be able to calculate the results of expressions with tuple literals in them.

Here are some examples of what we aim to handle:
```
print "arity 0" (,);
print "arity 1" (1,);
print "arity 1 op" (2-1,);
print "arity 2" (1, 2);
print "arity 2 trailing" (1, 2,);
print "arity 3" (1, 2, 3);
print "arity 3 trailing" (1, 2, 3,);
print "arity 3 op" (1, 3-1, 1+2-1);
```

We have more variants in the test folder, but remember that we've converged on a single AST by this point, so non-significant changes like adding/removing redundant commas, or omitting parentheses, should make no difference.
The AST we will be looking at past this point will be the same.

The first step in evaluating anything is to identify what evaluated and unevaluated terms must look like.
In this case, we mark terms that haven't evaluated with `Expression`, as is already the case in our starting AST, and we mark terms that we consider values with the `Literal` parent node.
We chose "literal" as the name because these values match expressions which can be thought of as fully-evaluated constants.

We identify "literals" as follows:
- `Int` or `Float`
- `Tuple` where all the elements are `Literal`

Because we use the `Literal` marker to identify subtrees that satisfy conditions, we can write a well-formedness specification that matches the rules above exactly (simplified from a larger modification to our main Infix `wf`):
```cpp
| (Literal <<= Int | Float | Tuple)
| (Tuple <<= Literal++)
```

> [!NOTE]
> For the more theoretical minded, what we are doing here is an implementation of small-step evaluation rules.

We make a point of explaining this `Literal` rule in detail, because a previous version of this tutorial used `Int` and `Float` nodes directly, without any prefix.
This makes determining if a tree needs evaluating very simple, since you can just ask "is it one of the two kinds of number?", but in a language with any kind of compound data, like tuples, it is no longer clear whether every subtree of a compound expression has been evaluated.
One could write a predicate to check this, but not only would it be slow due to having to traverse the entire value every time, it would also be inconvenient to use within Trieste's pattern matching.

Summarizing "is this all evaluated?" as a marker node is much more extensible in comparison.
You could add more value types to Infix, and as long you also add rules for how these values interact with the existing language, the notion of what an "evaluated value" looks like will remain centrally defined.
As a result, rules for container types like tuples (and other generic language features) will not have to change in order to accommodate, say, a tuple whose element is an instance of this new value type.

Without any operations on tuples, the single rule for evaluating one is quite simple:
```cpp
T(Expression) << (T(Tuple)[Tuple] << ((T(Expression) << T(Literal))++ * End)) >>
  [](Match& _) {
    return Expression << (Literal << _(Tuple));
  },
```

Once all the tuple's elements are `Literal`, as opposed to `Expression`, then the tuple itself may be marked as `Literal` according to our rules.
The main detail to point out here is that "all children are X" must be written as `T(X)++ * End`.
The `End` pattern is important, because if we have a partially evaluated tuple where only its first two children are `Literal`, `++` will match those two _and then the pattern will succeed_.
In that case, this rule will take some partially evaluated tuples and mark them as fully evaluated.
To ensure that all the children match the pattern in question, not just some (possibly empty!) prefix, we must require that there are no further children we could have matched using `End`.

### Not Evaluating a Tuple Expression (a.k.a. "Stuck terms")

Without tuples, the only way for an Infix expression to fail was to attempt a division by 0.
This one error could be caught as a special case of the division rule, but that trick doesn't generalize.

Specifically, tuples add another mutually incompatible kind of value to the Infix language.
As a result, we now have many error cases: one for every combination of `(1, 2) + (3, 1)`, `(1, 2) - (3, 4)`, and so forth.
Add even more operators, like the primitive tuple operations we will discuss later on, and the number of possible error cases quickly becomes intractable.

So, we can't reasonably detect every possible invalid combination of values.
That's fine - we can just put a catch-all rule at the end, right?
The trouble is that this rule won't "just work" if we add it to the end of our evaluation pass:
```cpp
T(Expression)[Expression] >>
  [](Match& _) { return err(_(Expression), "Unevaluated expression"); },
```

If you put this rule at the bottom of the `maths` pass, you might think that it would only trigger once all other rules have failed for a given expression, catching all evaluation errors.
You'd be right, but not in the way you want.
The transition from `Expression` to `Literal` will propagate through the program by repeated pass evaluation, until the pass no longer causes any changes.
If for any reason we encounter `1 + <not evaluated yet>`, then no evaluation rules will accept it, so we will reach our error case and report an error.
It doesn't matter that maybe that sub-expression hasn't evaluated _yet_; it just matters that our pass took more than one iteration.

To avoid this situation and flag errors only after the `maths` pass can no longer proceed, we can split our errors out into a second pass.
We can keep all our errors, as well as our specialized error reporting (rules that match and give better messages for certain types of invalid expression).
They just need to move into a new follow-up pass, `math_errs`.

The tricky part is what happens to the well-formedness definition in this case:
```cpp
inline const auto wf_pass_maths = 
  infix::wf
  | (Literal <<= Int | Float)
  // Add Literal to the set of possible expressions; it will be the only one after the errs pass.
  | (Expression <<= (Add | Subtract | Multiply | Divide | Ref | Float | Int | Literal))
  // --- tuples extension
  | (Literal <<= Int | Float | Tuple)
  | (Expression <<= (Tuple | TupleIdx | Append | Add | Subtract | Multiply | Divide | Ref | Float | Int | Literal))
  ;
```

Rather than a clean separation between `Literal` and `Expression`, we have to initially accept both: our `maths` pass might finish with `Expression` nodes left over almost anywhere.
It's our `math_errs` pass that will flag errors in this case, so where our AST has `Expression`, we now have to accept both `Expression` and `Literal`.

We work around this by nesting `Literal` inside of `Expression`, and considering `Literal` as if it were another expression type.
It would also be possible to mark everything as `Expression | Literal`, but that is both difficult to write in Trieste and quite a noisy change from the base well-formedness definition.

**Exercise for the reader:**
while it can be cleaner and more robust to have one pass for positive rules and one pass for errors, for a simple language like Infix it might be possible to make evaluation single-pass.
Currently, `maths` is a `topdown` pass like a lot of the Infix passes, but switching it to `bottomup` might allow evaluation to run in just one iteration.

To check how many iterations a pass took, you can try to instrument your passes using logging, or by building a histogram of how many times a given action executed (keep a `shared_ptr` to a pass-wide map and update it on rule execution).
[`std::source_location`](https://en.cppreference.com/w/cpp/utility/source_location) may be of interest.
Unfortunately, Trieste does not have built-in functionality to do this automatically as of this tutorial.

### Appending and Indexing

Once we've added the `Tuple` data type and can evaluate its literals, adding more operations on tuples is relatively straightforward.
The operations we describe are:
- Appending 0 or more tuples together, as in `append((1, 2), (3,))`.
- Accessing an element of a tuple with a `.`, as in `(1, 2).1` which would evaluate to `2`.

#### Append Expressions

The `append` operation looks like a function call, but we don't need to implement general function call semantics or function values in Infix to achieve its semantics.
We treat `append` as a built-in operator that just happens to look like a function, much like built-ins of other languages might work (Go's channel, slice, and map primitives seem to follow a similar principle).

To parse `append`, we add the word "append" itself as a keyword and token type in the parser.
Then, we recycle Infix's existing tuple parsing rules (any version) in order to transform the string `append(...)`, or the tokens `(append) (paren (group ...))`, into `(append (expression (paren ...)))`.
This allows us to treat the `(...)` part as a tuple syntactically, re-using all the existing parsing rules:
```cpp
// an append operation looks like append(...); we rely on tuple parsing
// to get the (...) part right, but we can immediately recognize
// append with a () next to it.
// Marking the (...) under Expression ensures normal parsing happens to
// it later.
In(Expression) * T(Append)[Append] * T(Paren)[Paren] >>
  [](Match& _) {
    return Expression << (_(Append) << (Expression << _(Paren)));
  },
```

After all the other operations have been parsed, we can inspect the nested expression we generated above and see if it is a tuple.
If it was actually something like `append(42)`, then the first error rule (where we assert the expression is not a tuple) will trigger.
Otherwise, we can gather the tuple's sub-expressions as arguments to the `append` operation.
Note the use of `_[Expression]` to make all the matched `Expression` nodes children of our `Append` node.

```cpp
T(Append)[Append] << (T(Expression) << --T(Tuple)) >>
  [](Match& _) { return err(_(Append), "Invalid use of append"); },

T(Append)
    << (T(Expression)
        << (T(Tuple) << (T(Expression)++[Expression] * End))) >>
  [](Match& _) { return Append << _[Expression]; },
```

To evaluate these append operations, we must detect when all the operation's arguments are evaluated, and check that those evaluated arguments are tuples.
Once that is ensured, we can implement the tuple append operation using mostly standard `std::vector` manipulation:
```cpp
T(Expression) << (T(Append) << ((T(Expression) << (T(Literal) << T(Tuple)))++[Literal] * End)) >>
  [](Match& _) {
    Node combined_tuple = Tuple;
    for(Node child : _[Expression]) {
      Node child_literal = child->front();
      Node sub_tuple = child_literal->front();
      for(Node elem : *sub_tuple) {
        combined_tuple->push_back(elem);
      }
    }
    return Expression << (Literal << combined_tuple);
  },
```

Having a precise pattern is important here, because if the rule applies then we are assuming the expression we are matching has a valid result.
In more complex cases, if it's only possible to tell that a rule should not apply within the rule body, then you can back out by returning the `NoChange` token, which makes Trieste treat the rule as if it didn't match.
You can see examples of this being done at other points in the Infix implementation.

Looking into the rule body, it's interesting to notice the difference between `Tuple << children`, where `children` is a `std::vector<Node>`, and `push_back` directly into a `Tuple` node.
We use the second option here because it directly grows the `Tuple` node's internal array, whereas the first option builds a separate vector and then copies it into a new `Tuple`'s internal vector, essentially building the underlying array twice.
While it may look the same, `Literal << combined_tuple` is different because it makes a `Literal` node whose single child is `combined_tuple`, which is a single `Node`.

**Exercise for the reader:**
this approach assumes that the append operation's parameter list will parse in the same way as a tuple.
If instead we wanted to have a distinct parameter list concept, then we would need to describe separate parsing rules for that concept.
We're unlikely to run into this with just an append operation, but when adding more operations, or general purpose functions, it could become important.
In that case, it might be interesting to add [spread syntax](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Spread_syntax) to Infix.
Then, tuple values could be expanded into append (or any other) operations' parameter lists.

As we have mentioned before, when generalizing something it is often a good idea to make a new token that represents that concept.
If parameter lists become a common feature of your language, then a `ParameterList` node with general-purpose rules will likely be more effective than just copy-pasting the tuple parsing rules and making changes.
Just because Trieste doesn't have higher-order rules in a direct sense doesn't mean that you can't make something similar out of token definitions, well-formedness definitions, and generalized rules/passes.

#### Indexing Expressions

Since we chose to consider `.` as the new highest-precedence operator in Infix, most of its implementation is very similar to the binary operators that already exist, from parsing to error handling.

Its evaluation rule shows another error case like dividing by 0 - when the index is out of range:
```cpp
T(Expression) << (T(TupleIdx)[TupleIdx] << (T(Expression) << (T(Literal) << T(Tuple)[Lhs])) * (T(Expression) << (T(Literal) << T(Int)[Rhs]))) >>
  [](Match& _) {
    Node lhs = _(Lhs);
    Node rhs = _(Rhs);
    int rhs_val = get_int(rhs);

    if(rhs_val < 0 || size_t(rhs_val) >= lhs->size()) {
      return NoChange ^ ""; // error, see next pass
    }
    
    return lhs->at(rhs_val);
  },
```

Since we need to know the exact value of the left-hand argument before knowing if the right-hand index is in range, we have to use the `NoChange` token to back out from within the rule body if the indexing operation won't work.
Returning this token causes Trieste to act as-if the rule did not apply at all, and the pass will continue searching for other matching patterns.
Because there is no other way to evaluate an expression like that, we will eventually reach the `math_errs` pass and raise an error due to the invalid tuple indexing expression.

**Exercise for the reader:** we use the `.` syntax from Rust for accessing tuple members.
It would also be possible to use Python-like indexing as in `(1, 2)[1]`.
Try altering the parsing rules so that Infix accepts that syntax instead.

### Writing Tuples

One of Infix's original purposes was to demonstrate Trieste's writers, and the ability to format Infix expressions in different ways.
Our addition of tuples to the language does not affect these output stages in very interesting ways, except where we changed the data representation.

Here are the only changes that required much thought:
- The addition of `Literal` tagging means that we need to adjust the kind of data our writer will accept when rendering results of the `calculate` rewriter.
  We solved this by making two versions of the writer's well-formedness constraints, but then sharing all the underlying code.
  Since `Expression` and `Literal` are only different by those two nodes, we coded our writer methods to strip either parent node and then process the contents identically.
- New node types like `Append` and `Tuple` that are not binary operators needed special handling.
  For infix notation output, we just mirrored the input syntax.
  For postfix notation, we found that variadic postfix operators (like both of our additions) end up being ambiguous without special syntax.
  Without a system that uses parentheses as delimiters (although it seems those delimiters are exactly how some Forth-family languages handle tuples), it's not clear how many values `1 2 3 tuple` should leave on the stack.

  To fix this, we added an extra "argument" to our postfix syntax for tuple literals and append operations, which counted how many arguments each operator should expect.
  That is, our example above could be marked `1 2 3 3 tuple` to mean a 3-element tuple, or `1 2 3 1 tuple` to mean a 1-element tuple with `1` and `2` left over on the stack.
  This was mostly for logical consistency and somewhat arbitrary, since we don't re-process the postfix syntax anywhere, but if someone built a Forth-like language as an exercise, our chosen syntax should not cause any problems beyond being difficult to write for humans.

### Notes on Scoping

While we don't make significant changes to Infix's scoping behavior, there were some surprising things not mentioned in the original explanation of Infix's variable lookup behavior.
- There is no error node for a variable redefinition.
  Failing because of a name redefinition is a well-formedness error, and this is one of few cases where this is the only way such a problem can be reported.
  This redefinition error occurs whenever `flags::shadowing` is enabled and the same name appears multiple times in the same symbol table, and the well-formedness check happens before any rule could possibly call `lookup` or `lookdown` to handle the situation in any other way.
- If your language has non-traditional scoping behavior, you can still use symbol tables, but you may want to avoid `flags::defbeforeuse` and `flags::shadowing` as they have the non-customizable error behavior mentioned above.
  It is however still possible to get any given result if you write your own lookup helpers around the main lookup method, and write your own resolver to disambiguate between or flag errors regarding all the definitions the less-constrained built-in lookup will find.

### Extra Trieste Behavior Notes

Here are some stories that came up during development, but the outcome was ultimately scrapped.
They have their own educational merit however, so we discuss them here.

#### Rule Priority Inversion

Confusingly, it is possible for a rule further down the list to override a rule above it.
What you need to achieve this is `dir::bottomup`, and two rules we call "upper" and "lower" for higher and lower priority, respectively.
The lower rule needs a pattern that matches a subtree of what the upper rule matches.

For example, consider an upper rule that matches `(a (b))` and lower rule that matches `(b)`.
When processing a tree bottom-up, we will first consider the tree `(b)`.
The upper rule will reject, since the top node isn't `(a)`.
The lower rule will accept, because the tree as presented to it is a perfect match.

In this scenario, you can write rules that look like `(a (b))` has priority, and then be surprised when the lower rule rewrites the tree to `(a (b2))`, causing the upper rule to never trigger at all.
You might see this with low-priority but very general error handling rules, which could lead to the nonsensical (considering the upper rule) situation `(a (error))`.
There's no fundamental solution to this, just be aware of the possibility, and it can save you some debugging.

Separating things into multiple passes, like we did for the `maths` and `math_errs` passes, can be a starting point if this kind of situation causes trouble.

#### Sibling Trouble

Another thing we added and then removed was how to write patterns that match _only if the target has siblings_.
Consider this example, where we want to match an `Int` or `Float` node, but only if it has siblings (rationale in comment):
```cpp
// In case of int and float literals, they are trivially expressions on
// their own so we mark them as such without any further parsing
// !! but only if they're not the only thing in an expression node !!
// Otherwise, we create infinitely nested expressions by re-applying the
// rule. This pair of rules requires _something_ to be on the left or
// right of the literal, ensuring it's not alone.

// Right case: --End is a negative lookahead, ensuring that there is
// something to the right of the selected literal that isn't the end of
// the containing node
In(Expression) * T(Int, Float)[Expression] * (--End) >>
  [](Match& _) { return Expression << _(Expression); },
// Left case: Seq here splices multiple elements rather than one, so
// we keep Lhs unchanged and in the right place
In(Expression) * Any[Lhs] * T(Int, Float)[Rhs] >>
  [](Match& _) { return Seq << _(Lhs) << (Expression << _(Rhs)); },
```

We can achieve this result in 2 cases.
The first option is simplest: we match the node, and then add a negative lookahead for the `End` pattern.
If `End` succeeds, meaning there are no siblings to the right of the node we matched, then we don't match.
This effectively requires our match to have one or more siblings on the right, and can be added to any pattern with no disruption.

The tricky case is the second one, which has to detect that there is some sibling to the left of the node we matched.
Trieste doesn't have a lookbehind pattern, so instead we just match any preceding element.
That gives us a follow-up problem however: our rewrite will also replace that node we didn't want to touch.
Normally a rewrite is many-to-one when it comes to siblings - multiple siblings are matched and replaced with one single node.
This tracks with how the rule body must return a single node, so what to do if we really need to return two siblings?

This is where the built-in node `Seq` comes in.
Trieste's rewrite system handles this node specially, and rather than inserting the single node `(seq (lhs) (rhs))` as a pattern substitution, it inserts each of `Seq`'s children next to each other.
This is Trieste's work-around for not being able to directly write multiple sibling nodes.

All that said, we ultimately moved these rules to the `terminals` pass as a separate `dir::bottomup | dir::once` pass, meaning that we could safely just match `T(Int, Float)` without using more advanced patterns, since the problem was not having `dir::once`.
More, simpler passes are an ideal outcome, but if that isn't possible then here is an example of the more advanced pattern writing you might need.

#### Adding a Quick Disjunction in Well-Formeenss Definitions

When dealing with well-formedness definitions, you might want to write `X * (Y | Z)` at some point.
It can happen when you need to relax a sub-case of a well-formedness rule for one pass without perturbing the whole structure.

This won't work, because it is not allowed to directly nest `|` inside `*`.
Instead, you can follow this example:
```cpp
// The Op >>= prefix ensures the internal structure of the WF is set up properly.
// The disjunction cannot otherwise be nested inside *
// Technically it names the disjunction "Op", but we do not use that name.
| (Assign <<= Ident * (Op >>= Literal | Expression)) 
```

#### How Assignment Used to Work

This section keeps alive the old documentation on how assignment was implemented.
It was another use of .seq which ultimately made less sense, as it allowed strange shapes like `x = 1 =;`.
Its diagrams may help you understand how .seq works regardless, however.

``` c++
// Equals.
R"(=)" >> [](auto& m) { m.add(Equals); },

// Terminator.
R"(;)" >> [](auto& m) { m.term({Equals}); },
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

## Fuzzing and Test Cases

One of Trieste's selling points is its automatic support for fuzz testing your rules, using the well-formedness definitions as a basis for both generating input ASTs, and as a way of validating a pass's responses to those inputs.
The original Infix tutorial covers how to use the fuzzer in order to validate that your rules actually cover your inputs, and that is a good start in validating a language implementation written with Trieste.

There are weaknesses to Trieste's built-in fuzzing method, however.
Any fuzzing tool can only determine whether the program under test's behavior was good or bad based on what it can expect.
For Trieste-based fuzzing, this means that if your rules don't violate well-formedness and your error cases have good coverage, you will pass fuzzing even if your implementation is otherwise completely wrong.
For instance, one iteration of the tuples code passed extensive fuzzing, but it also flagged spurious errors on most tuple expressions because of a bug in how the parsing rules interacted (one rule would half parse the tuple, then another would mark that as an error).
Since error reporting is a valid outcome for a Trieste pass that will not be flagged by fuzzing, we need more precise testing as well.

To avoid this kind of situation, you need both fuzzing (to make sure your compiler's edge cases behave decently), _and_ individual test cases.
Almost all known Trieste based projects, like the in-tree JSON and YAML parsers, or the C++ [Rego](https://github.com/microsoft/rego-cpp) implementation, have extensive test suites in addition to the Trieste based fuzzing.
You can use any of their test runners as inspiration, or you can look at Infix's.
Generally, if there is such a thing, the test suite from your target language's reference implementation is a good start - all the implementations of existing languages we mention here do this.

If your language is new, however, testing is a little more difficult because there is no original reference implementation, and there is no body of existing code to use as a source of truth.
We initially developed Infix's test runner to be the minimum viable test runner for the example folder, but it quickly grew in response to our additional needs when building a new language.
The final result has 3 modes, which we will briefly motivate while offering pointers on how to explore them.
You can find each mode under a different branch of a large if-statement in the test runner's main function.
We recommend reading the [CLI11](https://cliutils.github.io/CLI11/book/) documentation in order to understand how the command-line argument handling for all our executables works.

### dir_tester, the Directory Based Tester

The first test runner we built uses `std::filesystem::recursive_directory_iterator` to scan a target directory for files ending in `.infix` which have a corresponding sibling file whose extension is `.expected`.
Because Infix offers many modes of operation, and we want to test them all, each `.expected` file will list one or more headers starting with `//!` that indicate under which configurations the rest of the file contents should be output by running Infix on the input file.
Because parsing behavior is of specific interest, some tests are marked as `parse_only` and list the parsed AST as expected output, and some tests are marked as `calculate` and list the intended evaluation result as expected output.

If adapting or recreating this tool for your own use, notice that printing a Trieste AST to a string and constructing diff-like representations takes rather little code.
Most of our test runner consists of extracting the intended configuration from the files, which can also be done using JSON, YAML, or other tooling.
This includes the Trieste-provided implementations of these two mark-up languages.
You can also store your entire test suites in YAML, like many of the other test runners we mention in this section.

One important thing to notice is how we handle a `trieste::ProcessResult`:
- `result.ast` is the final result AST if `result.ok`, or the last AST before failure.
- `result.errors` is the list of errors reported by the transformation, if any.
- `result.ok` can be false even if there are no errors, because well-formedness violations do not appear in `result.errors`.
  `result.errors.size() == 0 && !result.ok` is a shorthand for detecting well-formedness violations.

Another crucial detail is `debug_path` and associated options, which dump all the intermediate pass results into a folder of your choosing.
We used this extensively in this project, and the folder's structure essentially shows a fully-elaborated trace through the ASTs generated by different passes, as well as the inputs used.
It can even be useful to just run a Trieste system on an input without looking at its source code, then look at the ASTs it generates.
Combined with well-formedness definitions and some well-chosen examples, this utility makes understanding a Trieste transformation's behavior much more accessible than it would be otherwise.

### fuzz_tester, the Extended Fuzz Test Launcher

While the `infix_trieste` executable exists, using `trieste::Driver` to wrap `infix::reader` and automatically generate a command-line executable, it has some key limitations:
- It is difficult to handle multiple language configurations, since Infix's configuration options are parameters used to modify its pass definitions, and the Trieste driver takes an already-constructed pass as a parameter before parsing command-line arguments.
- The driver can either fuzz a Reader, a Writer, or a Rewriter.
  It cannot fuzz multiple at once, and it does not support switching between them.

To address these limitations, we opted to write our own command-line that is tailored to our development process for the Infix language.
The `trieste::Fuzzer` object, while not providing a full command-line interface, is flexible and easy to work with so our fuzzer interface did not take much work.
We limited configuration to the options we needed, and gave default arguments that matched the most common cases for Infix's development process.

From a usability perspective, because our fuzz test wrapper is coded directly in our test runner and embeds all our defaults and development process assumptions, it was easier to use it than the default Trieste driver.
While the fuzz test driver is an easy starting point for a simple language implementation, more customized tooling pays for itself quickly in only slightly larger development efforts.

> [!CAUTION]
> Make sure to print `fuzzer.start_seed()`, like the default driver does, or your seed number won't be logged, and you won't be able to reproduce fuzz test failures!

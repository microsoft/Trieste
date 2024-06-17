# Beyond Infix

The Infix language is a useful introduction to Trieste, but by its very nature as an introductory tutorial, it cannot cover more advanced concepts.
This extended tutorial covers those less-documented Trieste features, while chronicling key aspects of editing an existing Trieste-based language implementation.

Our task at hand is to extend Infix with some more advanced concepts: multiple language versions, tuples, functions (both user-defined and built-in), and destructuring assignments.
Each of these topics explores a potential difficulty and how Trieste helps resolve it:
- Multiple language versions: given time, any programming language will change, and with multiple versions "in the wild", implementations may have to accept more than one of them.
  Correctly implementing something like this also helps us manage the collection of language extensions we explore in this document, which is why we look into this first.
- Tuples: a simple form of compoud data, which has interesting syntactic interactions with the existing Infix features.
- Functions: a source of diverse scoping problems we can resolve in different ways using the tools provided by Trieste.
- Destructuring assignments: introducing a new kind of definition with complex scoping rules might seems like it requires a refactor.
  We show how such problems can be avoided in Trieste.

## Multiple Language Versions

Ideas:
- Allow feature flags at the top of the input file? Well, that means you need to support the cross-product of all features.
  Could work, but also quite messy in this case because some of the changes we propose are... drastic.
- Tree of language levels, where each level implies another level on which it is based.
  This solves the need to deal with every feature interaction that might be possible, at the expense of requiring all-or-nothing compliance.
- For this document, a core concern is being able to read all the different versions.
  If you can't do that, use a different strategy.
- Structure A: express changes as diffs on the `wf` value, etc etc.
  - Have multiple conditional copies of the pass sequence, with appropriate changes.
  - Existing passes that need altering should have extra parts mentioned explicitly... could use a marker node in the tree to gate the nodes.
    You can write unreachable patterns for "future" stuff as long as they don't conflict with the "old" versions.
  - Main downside is that we need to make lots of programmatic changes to the Trieste setup.
    Not the worst, but it deviates from the Infix setup a lot.
- Structure B: build version tags into one WF uber-definition.
  - Can make a gate node that must be parented to the rest of the tree for features to even be WF-available.
    As in `(my-feature (rest of program w/ feature))` operator| `(rest of program but can't name feature)`.
    Works specifically because there is a hierarchy of features, would be madness if we want a feature matrix.
  - Pass structure is gated on whether the feature is there, with positive and negative parent lookups if necessary to handle divergent behavior.
  - We could end up with lots of weird conditional nonsense if we're not careful, but maybe the orthogonality of features will save us from that?
- Structure C: give up and duplicate (sub-)trees of the source code.
  - Doesn't really solve the multiple language versions problem, but I did also just add this in because of the readability issue, so... hmm.
  - Maximally readable
  - Hard to maintain, Ctrl^C + Ctrl^V :(

## Tuples in Infix

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
- first or second-class? How much does it cost to switch? (first class trivially subsumes first class, but we have to reject more programs... going second class to first class requires a fun desugaring, which you can just about do with tuples)
- lazy params (don't start with this one, but it's a nasty stress test to add it as a feature... rip off Scala's trick if we do, probably)

## Pattern Languages Lite: Destructuring Assignments

Ideas:
- Pattern in assignment, and possibly function argument position, to match and extract values from tuples, however we did tuple syntax.
- Discuss the magic `_` everyone uses.
- Can we use values (literals at least) in pattern position, as a kind of assertion? How expensive is that to change?

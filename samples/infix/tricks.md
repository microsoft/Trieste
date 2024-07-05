# Tricks with Trieste

## Adding a disjunction in the wrong place

Well-formedness definitions have structural limitations that force all nodes to have names.
This means you can't directly write `X * (Y | Z)`.
You can get this effect by "naming" the disjunction, as in `X * (Foo >>= Y | Z)`.

It might often be easier to name the node directly, as in `X * Foo` followed by `Foo <<= (Y | Z)`, but this changes the tree structure.
If you need the tree structure to stay the same, for example if you are refactoring a well-formedness definition around an existing pass structure (perhaps, in our practical experience, because one pass must become two), then you can use this trick instead.

## Matching something but only when it has siblings

Note: it might be simpler to try and use a once pass, but sometimes that's not possible.

```
// In case of int and float literals, they are trivially expressions on
// their own so we mark them as such without any further parsing
// !! but only if they're not the only thing in an expression node !!
// Otherwise, we create infinitely nested expressions by re-applying the
// rule. This pair of rules requires _something_ to be on the left or
// right of the literal, ensuring it's not alone.
In(Expression) * Any[Lhs] * T(Int, Float)[Rhs] >>
  // Left case: Seq here splices multiple elements rather than one, so
  // we keep Lhs unchanged and in the right place
  [](Match& _) { return Seq << _(Lhs) << (Expression << _(Rhs)); },
// Right case: --End is a negative lookahead, ensuring that there is
// something to the right of the selected literal that isn't the end of
// the containing node
In(Expression) * T(Int, Float)[Expression] * (--End) >>
  [](Match& _) { return Expression << _(Expression); },

// Do the same thing as above for identifiers, but it's easier because
// the Ref marker simplifies telling before and after states apart.
In(Expression) * T(Ident)[Ident] >>
  [](Match& _) { return Expression << (Ref << _(Ident)); },
```

## Bottom-up, top-down, and node ranges

The difference between bottom-up and top-down passes, specifically when it comes to match priority (what will cause priority inversion depending on pass direction).

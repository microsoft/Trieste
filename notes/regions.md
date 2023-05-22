# Regions again

*TODO*
- what if I want to return `(A & in, B & out)`?
  - what's the capability of the returned tuple?
- what if a lambda wants to capture an `out` as a free variable?
  - what's the capability of the lambda?
- need a `local` or `stack` capability?
  - seems painful to write functions that accept `in | local`

## Four Capabilities

`in`
  can put things in its fields
  can be put in a field
`out`
  can't put things in its fields
  can't be put in a field
`const`
  can't put things in its fields
  can be put in a field

enter `region[T] & in`
  get `T & in` in the lambda
  ```ts
  enter (x: region[T] & in)
  {
    x: T & in => ...
  }
  ```
  free variables that are `in` become `out`
  region rc++

enter `region[T] & out`
  get `T & out` in the lambda
  ```ts
  enter (x: region[T] & out)
  {
    x: T & out => ...
  }
  ```
  free variables that are `in` become `out`
  region rc++
  *safe even if the target region is `in` in some context*
  *doesn't allow any `in` region to coexist with a non-ancestor `out` region*

lateral-enter `region[T] & out`
  get `T & in` in the lambda
  ```ts
  enter (x: region[T] & out)
  {
    x: T & in => ... // exciting
  }
  ```
  fails if region rc != 1
  *maybe not rc=1? because of cown aliasing*
  *there's already an `in` or `out` reference, so we would coexist*
  *could be separate from the alias rc*
  free variables that are `in` become `out`
  region rc++

explore `region[T] & (in | out)`
  get `T & out` in the lambda
  ```ts
  explore (x: region[T] & (in | out))
  {
    x: T & out => ...
  }
  ```
  fails if region == open region
  *would cause `in` and `out` to coexist*
  free variables that are `in` *stay* `in`
  region rc++

exit
  region rc--

```ts
class region[T]
{
  var val: in.T
  var region_rc: usize = 0

  exit(self) = (ref self.region_rc)--

  explore(self): out.T | throw AlreadyOpen
  {
    if ($regions.top == self) {throw AlreadyOpen}
    (ref self.region_rc)++
    self.val
  }

  freeze(self: in): const.T | throw NotUnique
  {

  }

  move(self): (region[T] & in) | throw NotUnique
}
```

## Viewpoint Adaption

It's ok to use `out` in a field position. It means the same thing as `in` due to viewpoint adaptation: `in.(T & out)` = `T & in`, etc.

Field access on `C & k` gives `k.(ref[C.f] & in)`. This means that references to fields of a `lin` object are `in`, not `lin`. Linearity is for the individual object, not the transitive closure.

```ts
k ∈ capability := lin | in | out | const

// Note that lin.k and in.k are the same.
lin.k   = ∅ if k = lin
          in if k = out
          k otherwise
in.k    = ∅ if k = lin
          in if k = out
          k otherwise
out.k   = out if k ∈ {lin, in}
          k otherwise
const.k = const

k.(T1 & T2) = k.T1 & k.T2
k.(T1 | T2) = k.T1 | k.T2
k.(T1, T2) = k.T1, k.T2
k.C = C
(T1 & T2).T = T1.T & T2.T
(T1 | T2).T = T1.T | T2.T
(T1, T2).T = ∅
C.T = ∅

```

## Self Types

The `self` parameter has a type `Self` that's an implicit type parameter. `Self` has an upper bound of the enclosing type intersected with either any bound specified on the parameter, or with `in | out | const` if no bound is specified.

Note that `lin` isn't included by default. Should it be?

## Function Types

Can solve it with a union type:

```ts
T1...->T2 =
  ({ (Self & in, T1...): T2 } & in)
| ({ (Self & out, T1...): T2 } & out)
| ({ (Self & const, T1...): T2 } & const)
// and maybe lin as well?

(T1->T2) & const
~~~>
( ({ (Self & in, T1...): T2 } & in)
| ({ (Self & out, T1...): T2 } & out)
| ({ (Self & const, T1...): T2 } & const))
& const
~~~>
false | false | ({ (Self & const, T1...): T2 } & const)

// could also do:
C ~~~> C & (in | out | const)
// ie use the same approach as the implicit capability union on Self

```

What about `{ (Self & (in | const), T1...): T2 } & (in | const)`? It's ok, it's a subtype of `T1...->T2`.

```ts
(A | C)->(B & D) <: (A->B | C->D) <: (A & C)->(B | D)
(A | C)->(B & D) <: (A & C)->(B | D)
```

What about C-style function pointers? It's ok, wrap them in an immutable stateless lambda.

## Ref[T]

```ts

class Ref[T]
{
  var val: T

  // we lose any disjunction that is `lin` when loading
  load(self): Self.T = self.val

  // T is unadapted, so if T is `lin`, we accept and return it as `lin`
  store(self: in, val: T): T = (self.val = val)

  // fully specified
  load[Self: Ref[T] & (in | out | const)](self: Self): Self.T = self.val

  store[Self: Ref[T] & (in | out | count)](self: Self & in, val: Self.T):
    Self.T = (self.val = val)
}
```

## No Capability

If a disjunction in a type has no capability after being fully worked out, we want it to be an efficient `readonly` type. Adding `& (in | out | const)` to it works (note that we leave out `lin`), but results in expensive dynamic reference counting.

If, at the end of compilation, all types are replaced with type parameters that have the type as an upper bound, this gives the most efficient way to handle both structural types and `readonly` capabilities.

## Example

```ts
// no need for `stack`? escape analysis for stack allocation
when(a0: cown[A], b0: cown[B])
{
  a1: region[A] & in, b1: region[B] & in =>
  // we can enter an `in` region, but it's only sendable if it has rc=1.
  // if a1 and b1 are aliases, then we can't send them.
  enter a1
  {
    a2: A & in =>

    // on enter, all free variables are `out`
    b1: region[B] & out
    enter b1
    {
      b1f: B & out =>
      ...
    }

    enter(a2.f: region[AF] & in)
    {
      // af.rc = 2, one for a2.f, one for enter
      af: AF & in =>
      // a2 is also `out` now
      a2: A & out
      // if we read ourselves, we would get an `out` region
      a2.f: region[T2] & out
    }

    // af.rc = 1
    let r0: region[AF] & in = a2.f
    // af.rc = 2
    enter(r0: region[AF] & in)
    {
      // af.rc = 3
      af: AF & in =>
      enter(b1: region[B] & out)
      {
        // b.rc = 1, no rc-inc on an out region
        // if b is an alias of a, does anything go wrong?
        b2: B & out =>
        enter(b2.f: region[BF] & out)
        {
          // bf.rc = 1, no rc-inc on an out region
          bf: BF & out =>
          ...
        }
      }
    }

    // could we `out` read an `in` region safely?
    // yes, if the `in` region is `out` - which it is, because all free 
    // variables are `out`

    // a region is only sendable if it has rc=1
    // will get collected when rc=0
  }
}
```

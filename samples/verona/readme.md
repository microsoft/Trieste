# Todo

builtins
  typetest
  match

lazy[T]
list inside TypeParams or TypeArgs along with groups or other lists

object literals
public/private
package schemes
type assertions are accidentally allowed as types
allow assignment to DontCare

## Conditionals

early exit
- breaks things, the other branch doesn't join with us
- need all moves/drops in any continuation

type test conditionals

## Build ST

two `shadowing` defs of the same symbol in the same scope should produce an error

## Key Words

get rid of the `package` keyword
- accept a string as a type, indicating a package
get rid of capabilities as keywords
- make them types in `std`?
- or just handle those `typename` nodes specially in the typechecker?
get rid of `throw` as a keyword
- it's a type, like return, break, continue
add `try`

could allow `->` as a user defined symbol
- allow as a function name
- consume only the first `->` in a lambda definition
- after `typefunc`, convert it to a symbol

## `ref` Functions

CallLHS
- separate implementation
- `f()` vs `ref f()`
- if a `ref` function has no non-ref implementation, autogenerate one that calls the `ref` function and does `load` on the result

## Lambdas

selectors and functionnames as values
- an unbound selector must be a valid functionname
- can we wrap them in a lambda?
  - `f` ~> `{ $0 -> f $0 }`
  - gives us a function that takes a single argument
  - if it has more arguments, this gets us back the partial application
- makes all types `T1...->T2` sugar for `{ apply(Self, T1...): T2 }`

type of the lambda:
- no captures, or all captures are `const` = `const`, `self: const`
- any `lin` captures = `lin`, `self: lin`
- 0 `lin`, 1 or more `in`, 0 or more `const` = `lin`, `self: in`
- don't know if any `out` captures

## Destructuring Binds and `lin`

if a tuple field is `lin`, it's not `lin` in the destructuring bind
if the tuple itself is also `lin`, can we make this work?
the same thing happens with lambda captures

## Type Inference

T0 <: T1 => T0.upper += T1, T1.lower += T0

`bind $0 $T0 (reflet $1)`
  '$1 <: $T0
`bind $0 $T0 (tuple (reflet $1) (reflet $2))`
  ('$1, '$2) <: $T0
  *tuple flatten?*
`bind $0 $T0 (lambda ...)`
  'lambda <: $T0
  *free variables? problem is moving `lin`*
`bind $0 $T0 (call (functionname f[$T1]) (args (reflet $1) (reflet $2)))`
  ('f[$T1] <: '$1->'$2->$T0)
`bind $0 $T0 (call (selector f[$T1]) (args (reflet $1) (reflet $2)))`
  ('f[$T1] <: '$1->'$2->$T0) ∨ ('$1 <: { f[$T1]: '$1->'$2->$T0 })
`bind $0 $T0 (conditional (reflet $1) lambda1 lambda2)`
  'lambda1 <: ()->$T1
  'lambda2 <: ()->$T2
  ($T1 | $T2) <: $T0
`typeassert $0 $T0`
  '$0 <: $T0

typeof (reflet $0) =
  dup(node->lookup()->at(wf / Bind / Type)) // dup drops `lin`?
typeof (move $0) =
  node->lookup()->at(wf / Bind / Type) // no dup

## Lowering

- mangled names for all types and functions
- struct for every concrete type
- static and virtual dispatch
- conditionals
- heap to stack with escape analysis

## Ellipsis

`expr...` flattens the tuple produced by `expr`
- only needed when putting `expr...` in another tuple

`T...` is a tuple of unknown arity (0 or more) where every element is a `T`
- `T...` in a tuple flattens the type into the tuple
- `T...` in a function type flattens the type into the function arguments

```ts
// multiply a tuple of things by a thing
mul[n: {*(n, n): n}, a: n...](x: n, y: a): a
{
  match y
  {
    { _: () -> () }
    { y, ys -> x * y, mul(x, ys)... }
  }
}

let xs = mul(2, (1, 2, 3)) // xs = (2, 4, 6)
```

## If...Else

pre (let x (if $1 block1 block2)) post
->
  pre:
    condbr $1 block1 block2

  block1:
    ...
    br post

  block2:
    ...
    br post

  post:
    $x = phi [block1 $0, block2 $0]
    ...

## Try...Catch

if there's no catch then insert one that drops the exception
if there's no try around a call then insert one that rethrows the exception

pre (let x (try block1 catch block2)) post
->
  pre:
    br block1.1

  block1.1:
    $0 = call f1 ...
    // intrinsic for carry flag checking?
    %ok = intrinsic.carry_flag()
    condbr $ok block1.2 cleanup1.1

  block1.2:
    $0 = call f2 ...
    %ok = intrinsic.carry_flag()
    condbr $ok post cleanup1.2

  cleanup1.1:
    // cleanup from failed block1.1
    ...
    br block2

  cleanup1.2:
    ...
    br block2

  block2:
    $0 = ...
    br post

  post:
    $x = phi [block1.2 $0, cleanup1.2 $0]
    ...

## Lookup

lookup in union and intersection types
may need to check typealias bounds during lookup
- `type foo: T` means a subtype must have a type alias `foo` that's a subtype of `T`.

## type checker

can probably do global inference

finding selectors
  don't bother: treat as a `{ selector }` constraint?
  what about arity?
    arg count gives minimum arity
    a selector with higher arity is a subtype of a selector with lower arity
    given `f(A, B, C): D`, type it as `A->B->C->D` not `(A, B, C)->D`

selecting by reffunc arity
  select the shortest, not the longest
  use tuple notation to force a longer arity

## param: values as parameters for pattern matching

named parameters
  (group ident type)
  (equals (group ident type) group*)
pattern match on type
  (type)
pattern match on value
  (expr)

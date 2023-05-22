# Control Flow

## Non-local Returns

The result of every call is checked. If it's a `nonlocal[T]`, then it's immediately returned by the function or lambda. A function first unwraps the non-local value by calling `load` on it, whereas a lambda doesn't.

This allows `throw[T]` to be implemented as a `nonlocal[throw[T]]`, such that it propagages upwards until it's explicitly caught, while `return[T]` can be implemented as `nonlocal[T]`, such that it causes the calling function to return a value of type `T`.

If a call is syntactically marked as `try`, then the check for a non-local value is suppressed.

## Altering the default behaviour

A function can be made to behave as a lambda with `try`. A lambda can be made to behave like a function as follows:

```rust
let f = { x -> ... }
let f = { x -> returning { ... } }

returning[T1, T2](f: ()->(non_local[T1] | T2)): T1 | T2
{
  match (try f())
  {
    { x: nlr[T1] -> x.load() }
    { x: T2 -> x }
  }
}

(return[A] | throw[B]) <: non_local[A | throw[B]]
(return[C] | return[D]) <: non_local[C | D]

let x = try f() // x: T1 | non_local[T2]
let y = { x1 -> x1 } x // y: T1

match x
{
  { x1: T1 -> ... }
  { x2: non_local[T2] -> ... }
}

```

## NLRCheck

```rust
type non_local[T] =
{
  trait_non_local(): ()
  load(self): Self.T
}

class return[T]: non_local[T]
{
  let value: T

  trait_non_local(): () = ()
  load(self): Self.T = self.value

  create(): return[()] = return[()]::create(())
  create(value: T): return[T] = new value
  up(value: T): return[return[T]] = return[return[T]]::create(new value)
}

class throw[T]: non_local[throw[T]]
{
  let value: T

  trait_non_local(): () = ()
  load(self): Self.throw[T] = self

  create(): throw[()] = throw[()]::create(())
  create(value: T): throw[T] = new value
}

class break
{
  create(): throw[break] = throw(new)
}

class continue
{
  create(): throw[continue] = throw(new)
}

f()
{
  try
  {
    throw x // as expected, `try` catches this
    return y // unexpected: `try` also catches this
  }

  y

  match try
  {
    ...
  }
  {
    { x: return[T] -> x }
  }
}

// TODO: establishing `value: T1 & ¬T2` in the `else` branch
catch(value: T1, handlers: T2->T3): (T1 & ¬T2) | T3
{
  if value
  {
    x: T2 -> handler value
  }
  else
  {
    value
  }
}

f()
{
  for iter
  {
    subiter ->
    for subiter
    {
      value ->
      if something
      {
        // want to return from f here
        // `if` catches it and hands it to the subiter lambda
        // the subiter catches it and returns it to `for subiter`
        // `for subiter` catches it and hands it to the iter lambda
        // the iter catches it and returns it to `for iter`
        // `for iter` catches it (explicit) and hands it to f()
        // f() catches it, unwraps it, and returns it to the caller
        return value
      }

      if something
      {
        throw "fail"
      }

      // if we end here, `for` gets a 3
      // it probably just continues the loop
      // but it could build a list of values, track the last returned value,
      // or something else
      3

      // if we end here, f() returns `value`
      return value
    }
  }

  if something
  {
    if something_else
    {
      // if we end here, f() returns `value`
      return value
    }
  }

  // immediate return if we end here
  value

  // also an immediate return, because the function unwraps it
  return value
}

for[T1, T2, T3](iter: Iterator[T1], body: T1->(non_local[T2] | T3)):
  (non_local[T2] & ¬throw[Break] & ¬throw[Continue]) | ()
{
  try
  {
    while (iter.has_next)
    {
      // If this is a non-local, it goes to the `body()` call in `while`,
      // which returns it to our `try`, which forwards it to the caller.
      body iter.next
    }
  }
}

while[T1, T2](cond: ()->Bool, body: ()->(non_local[T1] | T2)):
  (non_local[T1] & ¬throw[Break] & ¬throw[Continue]) | ()
{
  if (cond())
  {
    match try body()
    {
      { _: throw[Break] -> () }
      { _: throw[Continue] -> while cond body }
      { r: non_local[T1] -> r }
      { _: T2 -> while cond body }
    }
  }
}

catch[T, U, V](value: T, body: U->V): T | V
{
  if value
  {
    x: U ->
    // Don't unwrap any return[T]
    try body x
  }
  else
  {
    value
  }
}

```

## Conditionals

Two types of conditional are built in.

```ts
// boolean conditional
// executes the lambda if `cond` is true
if cond
{
  ...
}

// type conditional
// executes the lambda if `value <: T`
if value
{
  x: T -> ...
}
```

## Pattern Matching

The result of the match should be:
- A `return[T]` for the result of any successful match.
- A `throw[E]` for an error result of any successful match.
- A `match[T]` if not yet matched.

What's needed?
- structural pattern
- type pattern

What about exhaustive matching?

```ts
class match[T]
{
  class NoMatch{}

  let value: T

  create(value: T): match[T] & lin = new (value, true)

  |[G: {==(G, T1): Bool}, T1, T2, E](
    self: lin, guard: G, case: T1->(T2 | throw E)):
    ((match[T] | match[T \ T1] | matched[T2]) & lin) | throw E
  {
    if self.value
    {
      x: T1 ->
      if (guard == x)
      {
        matched(case x)
      }
      else
      {
        match(x)
      }
    }
    else
    {
      x: T \ T1 -> match(x)
    }
  }

  |[T1, T2, E](self: lin, case: T1->(T2 | throw E)):
    ((match[T \ T1] | matched[T2]) & lin) | throw E
  {
    if self.value
    {
      x: T1 -> matched(case x)
    }
    else
    {
      x: T \ T1 -> match(x)
    }
  }

  end(self: lin): throw NoMatch
  {
    throw NoMatch
  }
}

class matched[T]
{
  let value: T

  create(value: T): matched[T] & lin = new value

  |[G: {==(G, T1): Bool}, T1, T2](self: lin, guard: G, case: T1->T2): Self =
    self

  |[T1, T2](self: lin, case: T1->T2): Self = self

  end(self: lin): T = self.value
}

// match[type x] | matched[type case1] | matched[type case2] | throw <E>
match x
  | true { 0 }
  | { a, b -> a + b }
  end
```

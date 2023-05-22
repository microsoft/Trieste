# Dispatch

For some `(call (selector f) (args (copy a) (copy b)))`, this could have been:
- `a.f(b)`
- `a f b`
- `f(a, b)`

We need to discover all possible functions `f` that could be called. This includes:
- Functions `f` that can be looked up from the source location (static dispatch).
- Functions `f` defined on the dynamic type of `a` (dynamic dispatch).

When calling `f`, we need to rank the possible functions and choose one.

## Motivation

The supposition is that using the same syntax for static and dynamic dispatch allows for better EDSLs.

## Extending Classes

For modularity, extensions should be scoped.
- You can import the extensions with `use <type>`.

When making a dynamic call:
- We know all extension methods that are in scope.
  - For each extended selector, build a map of `type descriptor -> function pointer`.
- If the selector has been extended:
  - Look up the function pointer in the selector map.
  - If there's no entry in the selector map, look up the function pointer in the receiver's type descriptor.
  - If no `class` that's been extended with this selector is a subtype of the receiver's static type, this check can be elided.
- Else look up the selector on the receiver's type descriptor. 
- If the static type of the receiver is concrete, all dynamic dispatch including extension can be turned into static dispatch.

When making a dynamic call:
- We know all extension methods that are in scope.
  - For each extended type, build an alternate local type descriptor.
  - Keep a local map of `type descriptor -> type descriptor`.
- If the selector has been extended for any type:
  - Look up the local type descriptor in the map.
    - This can be optimised such that for any given object, the local type descriptor is only looked up once.
  - If there's no entry in the map, use the receiver's type descriptor.
- Else use the receiver's type descriptor.

```rust
type Eq =
{
  ==(self: Self, other: Self): Bool
}

class Foo
{
  var x: I32
}

Foo::==(self: Foo, other: Foo): Bool
{
  // No access to private members of Foo unless the enclosing scope has access
  // to private members of Foo.
  self.x == other.x
}

```

## Ranking

Static or Dynamic
- Dot notation and unscoped names are dynamic dispatch.
- Scoped names are static dispatch.
- Allow `::name` as a scoped name, meaning lookup only, no lookdown phase.

Arity
- Given a call of arity N, select only `f/N`.
- There must be only one `f/N` for a given `N`.
- Generate partial application functions.
- For f/3, generate f/2, f/1, f/0, if they don't exist.

No static type based overloading.

## Type Inference

Dynamic:
```ts
(bind r T1 (call (selector f/2 (typeargs)) (args a b)))
```
Result:
- `a: T2`
- `b: T3`
- `T2 <: { f/2: (T2, T3)->T1 }`

Static:
```ts
(bind r T1 (call (functionname f/2 (typeargs)) (args a b)))
```
Result:
- `a: T2`
- `b: T3`
- `f/2: T4`
- `T4 <: (T2, T3)->T1`

## Partial Application

```ts
class A
{
  class $f_0
  {
    create(): $f_0
    {
      new ()
    }

    apply(self: $f_0, a: A): R
    {
      $f_1::create(a)
    }

    apply(self: $f_0, a: A, b: B): R
    {
      $f_2::create(a, b)
    }

    apply(self: $f_0, a: A, b: B, c: C): R
    {
      A::f(a, b, c)
    }
  }

  class $f_1
  {
    let a: A

    create(a: A): $f_1
    {
      new (a)
    }

    apply(self: $f_1, b: B): R
    {
      $f_2::create(self.a, b)
    }

    apply(self: $f_1, b: B, c: C): R
    {
      A::f(self.a, b, c)
    }
  }

  class $f_2
  {
    let a: A
    let b: B

    create(a: A, b: B): $f_2
    {
      new (a, b)
    }

    apply(self: $f_2, c: C): R
    {
      A::f(self.a, self.b, c)
    }
  }

  f(a: A, b: B, c: C): R
  {
    // ...
  }

  f(a: A, b: B): $f_2
  {
    $f_2::create(a, b)
  }

  f(a: A): $f_1
  {
    $f_1::create(a)
  }

  f(): $f_0
  {
    $f_0::create()
  }
}
```

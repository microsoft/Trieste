# Type Based Dispatch

TODO: don't know how to do this yet

# Partial Application

Given the initial N₀ arguments and a sequence of applications, each of which adds Nᵢ arguments, the number of arguments at a given application j is argsⱼ = N₀ + ∑ᵢ₌₀₋ⱼ Nᵢ. Over j ∈ i..0, select the lowest arity function that takes at least argsⱼ arguments. Absorb j applications into a single call. Any remaining applications are applied to the result of the call.

# Default Arguments

A default argument at the end effectively creates a pair of functions. The implicit function is one arity shorter, and calls the longer one with the default argument.

The caller can't select the default argument, because it may depend on virtual dispatch.

```ts
f(a, b, c = 3)
{
  ...
}

let x = f(a, b) // default argument, not partial application
```

# Default Field Values

If there's no `create` method, implicitly generate one with all the fields as parameters, with each field taking the default value.

# Named Arguments

Possible format for a named argument:
`(assign (expr dot ident) (expr...))`

```ts
let x = f(.b = 2, .a = 1)
```

# Function Types

A function may be:
- `lin`: the function may only be called once.
- `in`: the function may mutate its free variables.
- `const`: the function only closes over `const` variables.
- `?`: the function is sendable, but may mutate its free variables.

An `iso` function could be `Region[A->B] & lin`.

```ts
f: A...->B

type Fun[A..., B, K] = { apply(Self & K, A...): B } & K

```

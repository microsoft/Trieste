# Todo

Type Descriptor
- sizeof: encode it as a function?
```
%1 = getementptr [0 x %T], ptr null, i64 1
%2 = ptrtoint ptr %1 to i64
```
- sizeofptr: could do this for primitive types
  - 8 (i64) for most things, 1 (i8) for I8, etc
- trace: could be "fields that might be pointers", or encoded as a function
- finalizer: a function
- `typetest`: could be a function
- with sizeof, trace, finalizer, and typetest encoded as functions, they could have well-known vtable indices, and the type descriptor is then only a vtable
- vtable: could use linear/binary search when there's no selector coloring

LLVM lowering
- types-as-values?
  - encode class type arguments as fields?
  - pass function type arguments as dynamic arguments?
    - use the default if the typearg isn't specified, or the upper bounds if there's no default
  - insert type tests (for both args and typeargs) as function prologues?
- mangling
  - flatten all names, use fully-qualified names
- `new`, `fieldref`
  - autocreate ignores field initializers if there's already a `create` method. this seems wrong
  - default field values as arguments to `new`?
- Ptr, Ref[T], primitive types need a way to find their type descriptor
- `typetest`
  - every type needs an entry for every `typetest` type
- dynamic function lookup
  - find all `selector` nodes
  - every type needs an entry for every `selector` name
- only `Ref[T]::store` does llvm `store` - it's the only thing that needs to check for immutability?
- literals: integer (including char), float, string, bool
- `copy` and `drop` on `Ptr` and `Ref[T]`
  - implementation depends on the region type
- strings can't be arrays without type-checking
- region types, cowns, `when`
- could parse LLVM literals late, allowing expr that lift to reflet and not just ident
- destructuring bind where a variable gets "the rest" or "nothing"
  - ie lhs and rhs arity don't match
  - include destructuring selectors on every `class`?
  - make them RHS only? this still breaks encapsulation
  - `destruct` method, default impl returns the class fields as a tuple

- free variables in object literals
- mixins
- match
- lazy[T]
- weak[T]?
- public/private
- package schemes
- list inside TypeParams, Params, TypeArgs along with groups or other lists

## Future Passes

- type checking
- reachability
- selector coloring
- monomorphization

## Key Words

get rid of capabilities as keywords
- make them types in `std`?
- or just handle those `typename` nodes specially in the typechecker?

## Lambdas

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
- '$1 <: $T0
`bind $0 $T0 (tuple (reflet $1) (reflet $2))`
- ('$1, '$2) <: $T0
- *tuple flatten?*
`bind $0 $T0 (lambda ...)`
- 'lambda <: $T0
- *free variables? problem is moving `lin`*
`bind $0 $T0 (call (functionname f[$T1]) (args (reflet $1) (reflet $2)))`
- ('f[$T1] <: '$1->'$2->$T0)
`bind $0 $T0 (call (selector f[$T1]) (args (reflet $1) (reflet $2)))`
- ('f[$T1] <: '$1->'$2->$T0) ∨ ('$1 <: { f[$T1]: '$1->'$2->$T0 })
`bind $0 $T0 (conditional (reflet $1) lambda1 lambda2)`
- 'lambda1 <: ()->$T1
- 'lambda2 <: ()->$T2
- ($T1 | $T2) <: $T0
`typeassert $0 $T0`
- '$0 <: $T0

typeof (reflet $0) =
- dup(node->lookup()->at(wf / Bind / Type)) // dup drops `lin`?
typeof (move $0) =
- node->lookup()->at(wf / Bind / Type) // no dup

A `var` field has both a `ref` accessor function and a non-`ref` accessor function. A `let` field has only a non-`ref` accessor field. This means a `var x: T1` field is a subtype of a `let x: T2` field if `T1 <: T2`. However, a `var x: T1` field is only a subtype of a `var x: T2` field if `(T1 <: T2) ∧ (T2 <: T1)`, because the `ref` accessors return `ref[T1]` and `ref[T2]` respectively.

## Lowering

- mangled names for all types and functions
- struct for every concrete type
- static and virtual dispatch
- heap to stack with escape analysis
- refcount op elimination

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

## Lookup

lookup in union and intersection types

may need to check typealias bounds during lookup
- `type foo: T` means a subtype must have a type alias `foo` that's a subtype of `T`.

## param: values as parameters for pattern matching

named parameters
- (group ident type)
- (equals (group ident type) group*)
pattern match on type
- (type)
pattern match on value
- (expr)

# Lookup

Given a Node that names something, find the Node that defines it.

- Names may be scoped.
  - After looking up, we may need to look down.
- Names may be overloaded.
  - We may need to return multiple results.
  - This can come from the same scope or from different scopes.
  - It can even come from lookup on an algrebraic type.
- Names may be imported from other scopes.
  - And those scopes may need additional information, such as type arguments.

Lookup may want to return more information than just the definition node. For Verona, we want to return a map of type parameter bindings as well.

```f#
// TODO: `use`, multidef, shadowing, lookdown
// multidef => not shadowing
// not multidef => shadowing

// one unidef: done
// unidef with anything else: {}
// all multidef: union with parent
let LU scope name =
  let defs =
    { def |
      def ∈ scope.map name, !def.type.defbeforeuse or (def < name) }
  if ∃def ∈ defs: !def.type.multidef then 
    if |defs| = 1 then
      defs
    else
      {}
  else
    defs ∪ (LU scope.parent name)

let LD map elems =
  match elems
  | none -> map
  | elem, elems ->
    // TODO:
    let defs = { def | def ∈ map.def, def.type.exported }

let Resolve scope elems =
  match elems
  | none -> {}
  | elem, elems ->
    let maps = LU scope elem.name elem.args
    { LD map elems | map ∈ maps }
```

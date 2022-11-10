// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "lookup.h"

#include "lang.h"
#include "wf.h"

namespace verona
{
  bool apply_typeargs(Lookup& lookup)
  {
    if (!lookup.ta)
      return true;

    if (!lookup.def->type().in({Class, TypeAlias}))
      return false;

    auto ta = lookup.ta;
    lookup.ta = {};
    auto tp =
      lookup.def->at(wf / Class / TypeParams, wf / TypeAlias / TypeParams);

    // If we accept fewer type parameters than we have type arguments, it's not
    // a valid lookup target.
    if (tp->size() < ta->size())
      return false;

    Nodes args{ta->begin(), ta->end()};
    args.resize(tp->size());

    std::transform(
      tp->begin(),
      tp->end(),
      args.begin(),
      std::inserter(lookup.bindings, lookup.bindings.end()),
      [](auto param, auto arg) { return std::make_pair(param, arg); });

    return true;
  }

  Lookups lookdown(Lookups&& lookups, Node id, Node ta, NodeSet visited = {});

  Lookups lookdown(Lookup& lookup, Node id, Node ta, NodeSet visited)
  {
    while (true)
    {
      // Check if we've visited this node before. If so, we've found a cycle.
      auto inserted = visited.insert(lookup.def);
      if (!inserted.second)
        return {};

      if (lookup.def->type().in({Class, TypeTrait}))
      {
        if (!apply_typeargs(lookup))
          return {};

        // Return all lookdowns in the found class or trait.
        Lookups result;
        auto defs = lookup.def->lookdown(id->location());

        std::transform(
          defs.cbegin(),
          defs.cend(),
          std::back_inserter(result.defs),
          [&](auto& def) {
            return Lookup{def, ta, lookup.bindings};
          });

        return result;
      }
      else if (lookup.def->type() == TypeAlias)
      {
        if (!apply_typeargs(lookup))
          return {};

        // Replace the def with our type alias and try again.
        lookup.def = lookup.def->at(wf / TypeAlias / Type);
      }
      else if (lookup.def->type() == TypeParam)
      {
        // Replace the typeparam with the bound typearg or, failing that, the
        // upper bound, and try again.
        auto it = lookup.bindings.find(lookup.def);

        if ((it != lookup.bindings.end()) && it->second)
          lookup.def = it->second;
        else
          lookup.def = lookup.def->at(wf / TypeParam / Bound);
      }
      // The remainder of cases arise from a Use, a TypeAlias, or a TypeParam.
      // They will all result in some number of TypeName resolutions.
      else if (lookup.def->type() == TypeName)
      {
        // Resolve the typename and try again. Pass `visited` into the resulting
        // lookdowns, so that each path tracks cycles independently.
        return lookdown(lookup_scopedname(lookup.def), id, ta, visited);
      }
      else if (lookup.def->type() == Type)
      {
        // Replace the def with the content of the type and try again.
        lookup.def = lookup.def->at(wf / Type / Type);
      }
      else if (lookup.def->type() == TypeView)
      {
        // Replace the def with the rhs of the view and try again.
        lookup.def = lookup.def->at(wf / TypeView / Rhs);
      }
      else if (lookup.def->type() == TypeIsect)
      {
        // TODO: return everything we find
        return {};
      }
      else if (lookup.def->type() == TypeUnion)
      {
        // TODO: return only things that are identical in all disjunctions
        return {};
      }
      else
      {
        return {};
      }
    }
  }

  Lookups lookdown(Lookups&& lookups, Node id, Node ta, NodeSet visited)
  {
    Lookups result;

    for (auto& lookup : lookups.defs)
      result.add(lookdown(lookup, id, ta, visited));

    return result;
  }

  Lookups lookup_name(Node id, Node ta)
  {
    assert(id->type().in({Ident, Symbol}));
    assert(!ta || (ta->type() == TypeArgs));

    Lookups lookups;
    auto defs = id->lookup();

    for (auto& def : defs)
    {
      // Expand Use nodes by looking down into the target type.
      if (def->type() == Use)
        lookups.add(lookdown({def->at(wf / Use / Type), {}}, id, ta));
      else
        lookups.add({def, ta});
    }

    return lookups;
  }

  Lookups lookup_scopedname(Node tn)
  {
    assert(tn->type() == TypeName);
    auto ctx = tn->at(wf / TypeName / TypeName);
    auto id = tn->at(wf / TypeName / Ident);
    auto ta = tn->at(wf / TypeName / TypeArgs);

    if (ctx->type() == TypeUnit)
      return lookup_name(id, ta);

    return lookup_scopedname_name(ctx, id, ta);
  }

  Lookups lookup_scopedname_name(Node tn, Node id, Node ta)
  {
    return lookdown(lookup_scopedname(tn), id, ta);
  }
}

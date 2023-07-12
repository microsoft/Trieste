// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "lookup.h"

#include "wf.h"

#include <deque>

namespace verona
{
  Lookup::Lookup(Node def, Node ta, NodeMap<Node> b) : def(def), bindings(b)
  {
    if (!def->type().in({Class, TypeAlias, Function}))
    {
      if (ta)
        too_many_typeargs = true;

      return;
    }

    if (!ta)
      return;

    auto tp = def->at(
      wf / Class / TypeParams,
      wf / TypeAlias / TypeParams,
      wf / Function / TypeParams);

    if (tp->size() < ta->size())
    {
      too_many_typeargs = true;
      return;
    }

    // Bind all typeparams to their corresponding typeargs.
    std::transform(
      ta->begin(),
      ta->end(),
      tp->begin(),
      std::inserter(bindings, bindings.end()),
      [](auto arg, auto param) { return std::make_pair(param, arg); });

    // Bind all remaining typeparams to fresh typevars.
    std::transform(
      tp->begin() + ta->size(),
      tp->end(),
      std::inserter(bindings, bindings.end()),
      [](auto param) {
        return std::make_pair(param, TypeVar ^ param->fresh());
      });
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

      if (lookup.def->type().in({Class, TypeTrait, Function}))
      {
        // Return all lookdowns in the found class, trait, or function.
        Lookups result;
        auto defs = lookup.def->lookdown(id->location());

        std::transform(
          defs.cbegin(),
          defs.cend(),
          std::back_inserter(result.defs),
          [&](auto& def) { return Lookup(def, ta, lookup.bindings); });

        return result;
      }
      else if (lookup.def->type() == TypeAlias)
      {
        // Replace the def with our type alias and try again.
        lookup.def = lookup.def->at(wf / TypeAlias / Type);
      }
      else if (lookup.def->type() == TypeParam)
      {
        // Replace the typeparam with the bound typearg and try again.
        auto it = lookup.bindings.find(lookup.def);

        if ((it != lookup.bindings.end()) && it->second)
          lookup.def = it->second;
        else
          return {};
      }
      // The remainder of cases arise from a Use, a TypeAlias, or a TypeParam.
      // They will all result in some number of name resolutions.
      else if (lookup.def->type() == Type)
      {
        // Replace the def with the content of the type and try again.
        lookup.def = lookup.def->at(wf / Type / Type);
      }
      else if (lookup.def->type().in(
                 {TypeClassName, TypeAliasName, TypeTraitName, TypeParamName}))
      {
        // Resolve the name and try again. Pass `visited` into the resulting
        // lookdowns, so that each path tracks cycles independently.
        return lookdown(lookup_scopedname(lookup.def), id, ta, visited);
      }
      else if (lookup.def->type() == TypeView)
      {
        // Replace the def with the rhs of the view and try again.
        lookup.def = lookup.def->back();
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
      else if (lookup.def->type().in({TypeUnit, TypeList, TypeTuple, TypeVar}))
      {
        // Nothing to do here.
        return {};
      }
      else
      {
        // This type isn't resolved yet.
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
      {
        if (def->precedes(id))
          lookups.add(lookdown(Lookup(def->at(wf / Use / Type)), id, ta));
      }
      else
      {
        lookups.add(Lookup(def, ta));
      }
    }

    return lookups;
  }

  Lookups lookup_scopedname(Node tn)
  {
    assert(tn->type().in(
      {TypeClassName,
       TypeAliasName,
       TypeParamName,
       TypeTraitName,
       FunctionName}));

    auto ctx = tn->at(0);
    auto id = tn->at(1);
    auto ta = tn->at(2);

    if (ctx->type() == TypeUnit)
      return lookup_name(id, ta);

    return lookup_scopedname_name(ctx, id, ta);
  }

  Lookups lookup_scopedname_name(Node tn, Node id, Node ta)
  {
    return lookdown(lookup_scopedname(tn), id, ta);
  }

  bool lookup_recursive(Node node)
  {
    if (node->type() != TypeAlias)
      return false;

    std::deque<std::pair<NodeSet, Lookup>> worklist;
    worklist.emplace_back(NodeSet{node}, node->at(wf / TypeAlias / Type));

    while (!worklist.empty())
    {
      auto work = worklist.front();
      auto& set = work.first;
      auto type = work.second.def;
      auto& bindings = work.second.bindings;
      worklist.pop_front();

      if (type->type() == Type)
      {
        worklist.emplace_back(
          set, Lookup(type->at(wf / Type / Type), bindings));
      }
      else if (type->type().in({TypeTuple, TypeUnion, TypeIsect, TypeView}))
      {
        for (auto& t : *type)
          worklist.emplace_back(set, Lookup(t, bindings));
      }
      else if (type->type() == TypeAliasName)
      {
        auto defs = lookup_scopedname(type);

        if (!defs.defs.empty())
        {
          auto& def = defs.defs.front();

          if (set.contains(def.def))
            return true;

          set.insert(def.def);
          def.bindings.insert(bindings.begin(), bindings.end());
          bindings.swap(def.bindings);
          worklist.emplace_back(
            set, Lookup(def.def->at(wf / TypeAlias / Type), bindings));
        }
      }
      else if (type->type() == TypeParamName)
      {
        auto defs = lookup_scopedname(type);

        if (!defs.defs.empty())
        {
          auto& def = defs.defs.front();
          auto find = bindings.find(def.def);

          if (find != bindings.end())
            worklist.emplace_back(set, Lookup(find->second, bindings));
        }
      }
    }

    return false;
  }

  bool lookup_valid_predicate(Node node)
  {
    if (node->type() == TypeSubtype)
    {
      return true;
    }
    else if (node->type().in({TypeUnion, TypeIsect}))
    {
      // Check that all children are valid predicates.
      for (auto& t : *node)
      {
        if (!lookup_valid_predicate(t))
          return false;
      }

      return true;
    }
    else if (node->type() == TypeAlias)
    {
      // We know that type aliases aren't recursive, check the definition.
      return lookup_valid_predicate(node->at(wf / TypeAlias / Type));
    }

    return false;
  }

  void extract_typeparams(Node scope, Node t, Node tp)
  {
    if (t->type().in(
          {Type,
           TypeArgs,
           TypeUnion,
           TypeIsect,
           TypeTuple,
           TypeList,
           TypeView}))
    {
      for (auto& tt : *t)
        extract_typeparams(scope, tt, tp);
    }
    else if (t->type().in({TypeClassName, TypeAliasName, TypeTraitName}))
    {
      extract_typeparams(
        scope,
        t->at(
          wf / TypeClassName / Lhs,
          wf / TypeAliasName / Lhs,
          wf / TypeTraitName / Lhs),
        tp);

      extract_typeparams(
        scope,
        t->at(
          wf / TypeClassName / TypeArgs,
          wf / TypeAliasName / TypeArgs,
          wf / TypeTraitName / TypeArgs),
        tp);
    }
    else if (t->type() == TypeParamName)
    {
      auto id = t->at(wf / TypeParamName / Ident);
      auto defs = id->lookup(scope);

      if ((defs.size() == 1) && (defs.front()->type() == TypeParam))
      {
        if (!std::any_of(tp->begin(), tp->end(), [&](auto& p) {
              return p->at(wf / TypeParam / Ident)->location() ==
                id->location();
            }))
        {
          tp << clone(defs.front());
        }
      }

      extract_typeparams(scope, t->at(wf / TypeParamName / Lhs), tp);
      extract_typeparams(scope, t->at(wf / TypeParamName / TypeArgs), tp);
    }
  }

  Node typeparams_to_typeargs(Node node, Node typeargs)
  {
    if (!node->type().in({Class, Function}))
      return typeargs;

    for (auto typeparam :
         *node->at(wf / Class / TypeParams, wf / Function / TypeParams))
    {
      typeargs
        << (Type
            << (TypeParamName
                << TypeUnit
                << clone(typeparam->at(wf / TypeParam / Ident)) << TypeArgs));
    }

    return typeargs;
  }
}

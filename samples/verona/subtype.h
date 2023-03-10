// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "lang.h"
#include "lookup.h"
#include "wf.h"

#include <trieste/ast.h>

namespace verona
{
  using namespace trieste;

  struct Btype
  {
    Node type;
    NodeMap<Node> bindings;

    Btype(Node t, NodeMap<Node> b = {}) : type(t), bindings(b)
    {
      if (type->type() == Type)
        type = type->at(wf / Type / Type);

      if (type->type().in(
            {TypeClassName, TypeTraitName, TypeAliasName, TypeParamName}))
      {
        auto defs = lookup_scopedname(type);

        if (defs.defs.empty())
          return;

        auto& def = defs.defs.front();
        type = def.def;

        std::for_each(def.bindings.begin(), def.bindings.end(), [this](auto b) {
          if (b.second->type() == TypeVar)
          {
            // If `def.bindings` binds to a typevar, use `bindings`.
            auto it = bindings.find(b.first);
            if (it != bindings.end())
              b.second = it->second;
          }
          else if (b.second->type() == TypeParam)
          {
            // If `def.bindings` binds to a typeparam, use `bindings`.
            auto it = bindings.find(b.second);
            if (it != bindings.end())
              b.second = it->second;
          }
        });

        bindings.insert(def.bindings.begin(), def.bindings.end());

        if (type->type() == TypeParam)
        {
          auto it = bindings.find(type);
          if (it != bindings.end())
            type = it->second;
        }
      }
    }

    Btype operator()(Node& t) const
    {
      return Btype(t, bindings);
    }

    Btype operator()(size_t index) const
    {
      return Btype(type->at(index), bindings);
    }

    Btype operator()(const Index& index) const
    {
      return Btype(type->at(index), bindings);
    }

    const Token& operator()() const
    {
      return type->type();
    }
  };

  bool subtype(Btype sub, Btype sup);
}

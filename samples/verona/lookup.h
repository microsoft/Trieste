// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <trieste/ast.h>

namespace verona
{
  using namespace trieste;

  struct Lookup
  {
    // The bindings are for the context of `def`. They don't include any type
    // arguments on `def` itself.
    Node def;
    Node ta;
    NodeMap<Node> bindings;
  };

  struct Lookups
  {
    std::vector<Lookup> defs;

    Lookups() = default;

    Lookups(Node def, Node ta)
    {
      if (def)
        defs.push_back({def, ta, {}});
    }

    Lookups(Lookup&& lookup)
    {
      if (lookup.def)
        defs.push_back(lookup);
    }

    bool empty() const
    {
      return defs.empty();
    }

    void add(Lookups&& other)
    {
      defs.insert(defs.end(), other.defs.begin(), other.defs.end());
    }

    bool one(const std::initializer_list<Token>& types) const
    {
      return (defs.size() == 1) && defs.front().def->type().in(types);
    }
  };

  Lookups lookup_name(Node id, Node ta);
  Lookups lookup_typename(Node tn);
  Lookups lookup_typename_name(Node tn, Node id, Node ta);
  Lookups lookup_functionname(Node fn);
}

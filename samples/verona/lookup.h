// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <trieste/ast.h>

namespace verona
{
  using namespace trieste;

  struct Lookup
  {
    // If a typearg isn't in the bindings, it wasn't specified syntactically.
    Node def;
    NodeMap<Node> bindings;
    bool too_many_typeargs = false;

    Lookup(Node def, Node ta = {}, NodeMap<Node> b = {});
    Lookup(Node def, NodeMap<Node> b) : Lookup(def, {}, b) {}
  };

  struct Lookups
  {
    std::vector<Lookup> defs;
    bool try_again;

    Lookups(bool try_again = false) : try_again(try_again) {}

    Lookups(Lookup&& def)
    {
      defs.push_back(def);
    }

    static Lookups retry()
    {
      return Lookups(true);
    }

    void add(Lookups&& other)
    {
      defs.insert(defs.end(), other.defs.begin(), other.defs.end());
    }

    bool one(const std::initializer_list<Token>& types) const
    {
      return (defs.size() == 1) && defs.front().def->type().in(types) &&
        !defs.front().too_many_typeargs;
    }
  };

  Lookups lookup_name(Node id, Node ta = {});
  Lookups lookup_typename(Node tn);
  Lookups lookup_typename_name(Node tn, Node id, Node ta = {});
  Lookups lookup_functionname(Node fn);
  bool lookup_recursive(Node alias);
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "subtype.h"

#include <cassert>

namespace verona
{
  struct BtypeDef;
  using Btype = std::shared_ptr<BtypeDef>;

  struct BtypeDef
  {
    Node node;
    NodeMap<Node> bindings;

    BtypeDef(Node t, NodeMap<Node> b = {}) : node(t), bindings(b)
    {
      // Keep unwinding until done.
      NodeSet set;

      while (true)
      {
        if (node->type() == Type)
        {
          node = node->at(wf / Type / Type);
        }
        else if (node->type().in(
                   {TypeClassName,
                    TypeTraitName,
                    TypeAliasName,
                    TypeParamName}))
        {
          auto defs = lookup_scopedname(node);

          // This won't be empty in non-testing code.
          if (defs.defs.empty())
            return;

          auto& def = defs.defs.front();
          node = def.def;
          bindings.insert(def.bindings.begin(), def.bindings.end());

          // Check for cycles.
          if (set.contains(node))
            return;
        }
        else if (node->type() == TypeParam)
        {
          // An unbound typeparam effectively binds to itself.
          set.insert(node);
          auto it = bindings.find(node);
          if (it == bindings.end())
            return;

          node = it->second;
        }
        else
        {
          return;
        }
      }
    }

    static Btype make(Node t, NodeMap<Node> b)
    {
      return std::make_shared<BtypeDef>(t, b);
    }

    Btype make(Node& t)
    {
      return make(t, bindings);
    }

    Btype make(const Index& index)
    {
      return make(node->at(index), bindings);
    }

    const Token& type() const
    {
      return node->type();
    }
  };

  Btype make(Node t, NodeMap<Node> b = {})
  {
    return BtypeDef::make(t, b);
  }

  struct Sequent
  {
    std::vector<Btype> lhs_pending;
    std::vector<Btype> rhs_pending;
    std::vector<Btype> lhs_atomic;
    std::vector<Btype> rhs_atomic;

    Sequent() = default;

    Sequent(Sequent& rhs)
    : lhs_pending(rhs.lhs_pending),
      rhs_pending(rhs.rhs_pending),
      lhs_atomic(rhs.lhs_atomic),
      rhs_atomic(rhs.rhs_atomic)
    {}

    static bool reduce(Btype l, Btype r)
    {
      Sequent seq;
      seq.lhs_pending.push_back(l);
      seq.rhs_pending.push_back(r);
      return seq.reduce();
    }

    bool reduce()
    {
      while (!rhs_pending.empty())
      {
        auto r = rhs_pending.back();
        rhs_pending.pop_back();

        if (r->type() == TypeUnion)
        {
          // G |- D, A, B
          // ---
          // G |- D, (A | B)

          // RHS union becomes RHS formulae.
          for (auto& t : *r->node)
            rhs_pending.push_back(r->make(t));
        }
        else if (r->type() == TypeIsect)
        {
          // G |- D, A
          // G |- D, B
          // ---
          // G |- D, (A & B)

          // RHS isect is a sequent split.
          for (auto& t : *r->node)
          {
            Sequent seq(*this);
            seq.rhs_pending.push_back(r->make(t));

            if (!seq.reduce())
              return false;
          }

          return true;
        }
        else if (r->type() == TypeAlias)
        {
          // Try both the typealias and the underlying type.
          rhs_pending.push_back(r->make(wf / TypeAlias / Type));
          rhs_atomic.push_back(r);
        }
        else
        {
          rhs_atomic.push_back(r);
        }
      }

      while (!lhs_pending.empty())
      {
        auto l = lhs_pending.back();
        lhs_pending.pop_back();

        if (l->type() == TypeIsect)
        {
          // G, A, B |- D
          // ---
          // G, (A & B) |- D

          // LHS isect becomes LHS formulae.
          for (auto& t : *l->node)
            lhs_pending.push_back(l->make(t));
        }
        else if (l->type() == TypeUnion)
        {
          // G, A |- D
          // G, B |- D
          // ---
          // G, (A | B) |- D

          // LHS union is a sequent split.
          for (auto& t : *l->node)
          {
            Sequent seq(*this);
            seq.lhs_pending.push_back(l->make(t));

            if (!seq.reduce())
              return false;
          }

          return true;
        }
        else if (l->type() == TypeAlias)
        {
          // Try both the typealias and the underlying type.
          lhs_pending.push_back(l->make(wf / TypeAlias / Type));
          lhs_atomic.push_back(l);
        }
        else if (l->type() == TypeParam)
        {
          // Try both the typeparam and the upper bound.
          lhs_pending.push_back(l->make(wf / TypeParam / Bound));
          lhs_atomic.push_back(l);
        }
        else
        {
          lhs_atomic.push_back(l);
        }
      }

      // If either side is empty, the sequent is trivially true.
      if (lhs_atomic.empty() || rhs_atomic.empty())
        return true;

      // G, A |- D, A
      return std::any_of(lhs_atomic.begin(), lhs_atomic.end(), [&](Btype& l) {
        return std::any_of(rhs_atomic.begin(), rhs_atomic.end(), [&](Btype& r) {
          if (l->type() == TypeVar)
          {
            // TODO: Accumulate upper bounds.
            if (r->type() == TypeVar)
            {
              // TODO: Accumulate lower bounds.
            }

            return true;
          }

          if (r->type() == TypeVar)
          {
            // TODO: Accumulate lower bounds.
            return true;
          }

          // These must be an exact match.
          if (r->type().in({TypeUnit, Lin, In_, Out, Const}))
            return l->type() == r->type();

          if (r->type() == TypeTuple)
          {
            // Tuples must be the same arity and each element must be a subtype.
            return (l->type() == TypeTuple) &&
              std::equal(
                     l->node->begin(),
                     l->node->end(),
                     r->node->begin(),
                     r->node->end(),
                     [&](auto& t, auto& u) {
                       return reduce(l->make(t), r->make(u));
                     });
          }

          // Nothing is a subtype of a TypeList. Two TypeLists may have
          // different instantiated arity, even if they have the same bounds.
          // Use a TypeParam with a TypeList upper bounds to get subtyping.
          if (r->type() == TypeList)
            return false;

          // Check for an exact match.
          if (r->type() == TypeParam)
            return (l->type() == TypeParam) && (l->node == r->node);

          if (r->type().in({TypeAlias, Class}))
          {
            // Check for an exact match.
            if ((l->type() != r->type()) || (l->node != r->node))
              return false;

            // Check for invariant type arguments in all enclosing scopes.
            auto node = r->node;

            while (node)
            {
              auto tps = node->at(
                wf / Class / TypeParams,
                wf / TypeAlias / TypeParams,
                wf / Function / TypeParams);

              for (auto& tp : *tps)
              {
                auto la = l->make(tp);
                auto ra = r->make(tp);

                if (!reduce(la, ra) || !reduce(ra, la))
                  return false;
              }

              node = node->parent({Class, TypeAlias, Function});
            }

            return true;
          }

          if (r->type() == TypeFunc)
          {
            // The LHS must accept all the arguments of the RHS and return a
            // result that is a subtype of the RHS's result.
            return reduce(
                     r->make(wf / TypeFunc / Lhs),
                     l->make(wf / TypeFunc / Lhs)) &&
              reduce(l->make(wf / TypeFunc / Rhs),
                     r->make(wf / TypeFunc / Rhs));
          }

          if (r->type() == Package)
          {
            // A package resolves to a class. Once we have package resolution,
            // compare the classes, as different strings could resolve to the
            // same package.
            return (l->type() == Package) &&
              (l->node->at(wf / Package / Id)->location() ==
               r->node->at(wf / Package / Id)->location());
          }

          if (r->type() == TypeTrait)
          {
            // TODO: check that all methods are present and have the correct
            // type. Add an assumption that (l < r) for each method.
            return false;
          }

          if (r->type() == TypeView)
          {
            // TODO: handle viewpoint adaptation
            return false;
          }

          // Shouldn't get here.
          assert(false);
          return false;
        });
      });
    }
  };

  bool subtype(Node sub, Node sup)
  {
    return Sequent::reduce(make(sub), make(sup));
  }
}

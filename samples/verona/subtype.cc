// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "subtype.h"

#include <cassert>

namespace verona
{
  struct BtypeDef;
  using Btype = std::shared_ptr<BtypeDef>;

  namespace wfsub
  {
    inline const auto Type_Type = wfPassNameArity / Type / Type;
    inline const auto TypeView_Lhs = wfPassNameArity / TypeView / Lhs;
    inline const auto TypeView_Rhs = wfPassNameArity / TypeView / Rhs;
    inline const auto TypeAlias_Type = wfPassNameArity / TypeAlias / Type;
    inline const auto TypeParam_Bound = wfPassNameArity / TypeParam / Bound;
    inline const auto TypeFunc_Lhs = wfPassNameArity / TypeFunc / Lhs;
    inline const auto TypeFunc_Rhs = wfPassNameArity / TypeFunc / Rhs;
    inline const auto Package_Id = wfPassNameArity / Package / Id;
    inline const auto TypeTrait_ClassBody =
      wfPassNameArity / TypeTrait / ClassBody;
    inline const auto Function_Ident = wfPassNameArity / Function / Ident;
    inline const auto Class_TypeParams = wfPassNameArity / Class / TypeParams;
    inline const auto TypeAlias_TypeParams =
      wfPassNameArity / TypeAlias / TypeParams;
    inline const auto Function_TypeParams =
      wfPassNameArity / Function / TypeParams;
    inline const auto Function_Params = wfPassNameArity / Function / Params;
    inline const auto Param_Type = wfPassNameArity / Param / Type;
    inline const auto Function_Type = wfPassNameArity / Function / Type;
  }

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
          node = node->at(wfsub::Type_Type);
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
          def.bindings.insert(bindings.begin(), bindings.end());
          bindings.swap(def.bindings);

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
        else if (node->type() == TypeView)
        {
          if (!reduce_view())
            return;
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

    bool reduce_view()
    {
      assert(type() == TypeView);
      auto l = make(wfsub::TypeView_Lhs);

      if (l->type().in(
            {TypeTuple, TypeList, Package, Class, TypeTrait, TypeUnit}))
      {
        node = TypeTrue;
        return true;
      }

      if (l->type().in({TypeUnion, TypeIsect}))
      {
        // (A | B).C = A.C | B.C
        // (A & B).C = A.C & B.C
        auto r = node->at(wfsub::TypeView_Rhs);
        node = l->type();

        for (auto& t : *l->node)
          node << (TypeView << -t << -r);

        return true;
      }

      if (l->type() == TypeAlias)
      {
        // This TypeView will itself be reduced.
        l = l->make(wfsub::TypeAlias_Type);
        l->bindings.insert(bindings.begin(), bindings.end());
        bindings.swap(l->bindings);
        node = TypeView << -l->node << -node->at(wfsub::TypeView_Rhs);
        return true;
      }

      if (l->type().in({TypeTrue, TypeFalse}))
      {
        node = l->node;
        return true;
      }

      auto r = make(wfsub::TypeView_Rhs);

      if (r->type().in({TypeUnion, TypeIsect, TypeTuple, TypeList}))
      {
        // A.(B & C) = A.B & A.C
        // A.(B | C) = A.B | A.C
        // A.(B, C) = A.B, A.C
        // A.(B...) = (A.B)...
        node = r->type();

        for (auto& t : *r->node)
          node << (TypeView << -l->node << -t);

        return true;
      }

      if (r->type() == TypeAlias)
      {
        // This TypeView will itself be reduced.
        r = r->make(wfsub::TypeAlias_Type);
        r->bindings.insert(bindings.begin(), bindings.end());
        bindings.swap(r->bindings);
        node = TypeView << -l->node << -r->node;
        return true;
      }

      if (r->type().in(
            {Package, Class, TypeTrait, TypeUnit, TypeTrue, TypeFalse}))
      {
        node = r->node;
        bindings = r->bindings;
        return true;
      }

      if ((l->type() == Const) || (r->type() == Const))
      {
        // Const.* = Const
        // *.Const = Const
        node = Const;
        return true;
      }

      if (l->type().in({Lin, In_}) && (r->type() == Lin))
      {
        // (Lin | In).Lin = False
        node = TypeFalse;
        return true;
      }

      if (l->type().in({Lin, In_}) && (r->type().in({In_, Out})))
      {
        // (Lin | In).(In | Out) = In
        node = In_;
        return true;
      }

      if ((l->type() == Out) && (r->type().in({Lin, In_, Out})))
      {
        // Out.(Lin | In | Out) = Out
        node = l->node;
        return true;
      }

      // TODO: TypeView on LHS or RHS?

      // At this point, the TypeView has a TypeParam or a TypeVar on either the
      // LHS or RHS, and the other side is a TypeParam, TypeVar, or K.
      return false;
    }
  };

  Btype make(Node t, NodeMap<Node> b = {})
  {
    return BtypeDef::make(t, b);
  }

  struct Assume
  {
    Btype sub;
    Btype sup;

    Assume(Btype sub, Btype sup) : sub(sub), sup(sup)
    {
      assert(sub->type().in({Class, TypeTrait}));
      assert(sup->type() == TypeTrait);
    }
  };

  struct Sequent
  {
    std::vector<Btype> lhs_pending;
    std::vector<Btype> rhs_pending;
    std::vector<Btype> lhs_atomic;
    std::vector<Btype> rhs_atomic;
    std::vector<Assume> assumptions;

    Sequent() = default;

    Sequent(Sequent& rhs)
    : lhs_pending(rhs.lhs_pending),
      rhs_pending(rhs.rhs_pending),
      lhs_atomic(rhs.lhs_atomic),
      rhs_atomic(rhs.rhs_atomic),
      assumptions(rhs.assumptions)
    {}

    void push_assume(Btype sub, Btype sup)
    {
      assumptions.emplace_back(sub, sup);
    }

    void pop_assume()
    {
      assumptions.pop_back();
    }

    bool reduce(Btype l, Btype r)
    {
      Sequent seq;
      seq.lhs_pending.push_back(l);
      seq.rhs_pending.push_back(r);
      seq.assumptions = assumptions;
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

          // G |- A < B
          // G |- A < C
          // ---
          // G |- A < (B & C)

          // A |- (B & C)
          // (A |- B), (A |- C)

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
          rhs_pending.push_back(r->make(wfsub::TypeAlias_Type));
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
          lhs_pending.push_back(l->make(wfsub::TypeAlias_Type));
          lhs_atomic.push_back(l);
        }
        else if (l->type() == TypeParam)
        {
          // Try both the typeparam and the upper bound.
          lhs_pending.push_back(l->make(wfsub::TypeParam_Bound));
          lhs_atomic.push_back(l);
        }
        else
        {
          lhs_atomic.push_back(l);
        }
      }

      // If either side is empty, the sequent is trivially false.
      // TODO: should this be an assert?
      if (lhs_atomic.empty() || rhs_atomic.empty())
        return false;

      // TODO: if we can succeed without checking TypeVars, we should do so.

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
            return exact_match(l, r);

          if (r->type().in({TypeAlias, Class}))
            return exact_match(l, r) && invariant_typeargs(l, r);

          if (r->type() == TypeFunc)
          {
            // The LHS must accept all the arguments of the RHS and return a
            // result that is a subtype of the RHS's result.
            return (l->type() == TypeFunc) &&
              reduce(r->make(wfsub::TypeFunc_Lhs),
                     l->make(wfsub::TypeFunc_Lhs)) &&
              reduce(l->make(wfsub::TypeFunc_Rhs),
                     r->make(wfsub::TypeFunc_Rhs));
          }

          if (r->type() == Package)
          {
            // A package resolves to a class. Once we have package resolution,
            // compare the classes, as different strings could resolve to the
            // same package.
            return (l->type() == Package) &&
              (l->node->at(wfsub::Package_Id)->location() ==
               r->node->at(wfsub::Package_Id)->location());
          }

          if (r->type() == TypeTrait)
          {
            if (!l->type().in({Class, TypeTrait}))
              return false;

            // If any assumption is true, the trait is satisfied.
            if (std::any_of(
                  assumptions.begin(), assumptions.end(), [&](auto& assume) {
                    // Effectively: (l <: assume.sub) && (assume.sup <: r)
                    return exact_match(r, assume.sup) &&
                      exact_match(l, assume.sub) &&
                      invariant_typeargs(r, assume.sup) &&
                      invariant_typeargs(l, assume.sub);
                  }))
            {
              return true;
            }

            bool ok = true;
            push_assume(l, r);
            auto rbody = r->node->at(wfsub::TypeTrait_ClassBody);

            for (auto rmember : *rbody)
            {
              if (rmember->type() != Function)
                continue;

              auto id = rmember->at(wfsub::Function_Ident)->location();
              auto lmembers = l->node->lookdown(id);

              // Function names are distinguished by arity at this point.
              if (lmembers.size() != 1)
              {
                ok = false;
                break;
              }

              auto lmember = lmembers.front();

              if (lmember->type() != Function)
              {
                ok = false;
                break;
              }

              // rmember.typeparams.upper <: lmember.typeparams.upper
              auto rtparams = rmember->at(wfsub::Function_TypeParams);
              auto ltparams = lmember->at(wfsub::Function_TypeParams);

              if (!std::equal(
                    rtparams->begin(),
                    rtparams->end(),
                    ltparams->begin(),
                    ltparams->end(),
                    [&](auto& rtparam, auto& ltparam) {
                      return reduce(
                        r->make(rtparam->at(wfsub::TypeParam_Bound)),
                        l->make(ltparam->at(wfsub::TypeParam_Bound)));
                    }))
              {
                ok = false;
                break;
              }

              // rmember.params <: lmember.params
              auto rparams = rmember->at(wfsub::Function_Params);
              auto lparams = lmember->at(wfsub::Function_Params);

              if (!std::equal(
                    rparams->begin(),
                    rparams->end(),
                    lparams->begin(),
                    lparams->end(),
                    [&](auto& rparam, auto& lparam) {
                      return reduce(
                        r->make(rparam->at(wfsub::Param_Type)),
                        l->make(lparam->at(wfsub::Param_Type)));
                    }))
              {
                ok = false;
                break;
              }

              // lmember.result <: rmember.result
              if (!reduce(
                    l->make(lmember->at(wfsub::Function_Type)),
                    r->make(rmember->at(wfsub::Function_Type))))
              {
                ok = false;
                break;
              }
            }

            pop_assume();
            return ok;
          }

          if (r->type() == TypeView)
          {
            // TODO: handle viewpoint adaptation
            return false;
          }

          // Shouldn't get here in non-testing code.
          return false;
        });
      });
    }

    bool exact_match(Btype& l, Btype& r)
    {
      // The type and node must match exactly.
      return (l->type() == r->type()) && (l->node == r->node);
    }

    bool invariant_typeargs(Btype& r, Btype& l)
    {
      // Check for invariant type arguments in all enclosing scopes.
      auto node = r->node;

      while (node)
      {
        if (node->type().in({Class, TypeAlias, Function}))
        {
          auto tps = node->at(
            wfsub::Class_TypeParams,
            wfsub::TypeAlias_TypeParams,
            wfsub::Function_TypeParams);

          for (auto& tp : *tps)
          {
            auto la = l->make(tp);
            auto ra = r->make(tp);

            if (!reduce(la, ra) || !reduce(ra, la))
              return false;
          }
        }

        node = node->parent({Class, TypeAlias, Function});
      }

      return true;
    }
  };

  bool subtype(Node sub, Node sup)
  {
    Sequent seq;
    return seq.reduce(make(sub), make(sup));
  }
}

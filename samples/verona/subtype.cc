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
    inline const auto Package_Id = wfPassNameArity / Package / Id;
    inline const auto TypeAlias_TypeParams =
      wfPassNameArity / TypeAlias / TypeParams;
    inline const auto TypeAlias_Type = wfPassNameArity / TypeAlias / Type;
    inline const auto TypeTrait_ClassBody =
      wfPassNameArity / TypeTrait / ClassBody;
    inline const auto Class_TypeParams = wfPassNameArity / Class / TypeParams;
    inline const auto Function_Ident = wfPassNameArity / Function / Ident;
    inline const auto Function_TypeParams =
      wfPassNameArity / Function / TypeParams;
    inline const auto Function_Params = wfPassNameArity / Function / Params;
    inline const auto Function_Type = wfPassNameArity / Function / Type;
    inline const auto Param_Type = wfPassNameArity / Param / Type;
    inline const auto TypeList_Type = wfPassNameArity / TypeList / TypeList;
  }

  struct BtypeDef
  {
    Node node;
    NodeMap<Btype> bindings;

    BtypeDef(Node t, NodeMap<Btype> b = {}) : node(t), bindings(b)
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

          // Use existing bindings if they haven't been specified here.
          auto& def = defs.defs.front();
          node = def.def;

          for (auto& bind : def.bindings)
            bindings[bind.first] = make(bind.second, b);

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

          *this = *it->second;
        }
        else
        {
          return;
        }
      }
    }

    static Btype make(Node t, NodeMap<Btype> b)
    {
      return std::make_shared<BtypeDef>(t, b);
    }

    Btype make(Node t)
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

  Btype make(Node t, NodeMap<Btype> b = {})
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
    std::vector<Btype> self;

    Sequent() = default;

    Sequent(Sequent& rhs)
    : lhs_pending(rhs.lhs_pending),
      rhs_pending(rhs.rhs_pending),
      lhs_atomic(rhs.lhs_atomic),
      rhs_atomic(rhs.rhs_atomic),
      assumptions(rhs.assumptions),
      self(rhs.self)
    {}

    void push_assume(Btype sub, Btype sup)
    {
      assumptions.emplace_back(sub, sup);
    }

    void pop_assume()
    {
      assumptions.pop_back();
    }

    void push_self(Btype s)
    {
      assert(s->type() == Class);
      self.push_back(s);
    }

    void pop_self()
    {
      self.pop_back();
    }

    bool reduce(Btype l, Btype r)
    {
      Sequent seq;
      seq.lhs_pending.push_back(l);
      seq.rhs_pending.push_back(r);
      seq.assumptions = assumptions;
      seq.self = self;
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
        else if (r->type() == TypeView)
        {
          auto [rr, done] = reduce_view(r);

          if (done)
            rhs_atomic.push_back(rr);
          else
            rhs_pending.push_back(rr);
        }
        else if (r->type() == Self)
        {
          // Try both Self and the current self type.
          rhs_atomic.push_back(r);

          if (!self.empty())
            rhs_atomic.push_back(self.back());
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
        else if (l->type() == TypeView)
        {
          auto [ll, done] = reduce_view(l);

          if (done)
            rhs_atomic.push_back(ll);
          else
            rhs_pending.push_back(ll);
        }
        else if (l->type() == Self)
        {
          // Try both Self and the current self type.
          lhs_atomic.push_back(l);

          if (!self.empty())
            lhs_atomic.push_back(self.back());
        }
        else
        {
          lhs_atomic.push_back(l);
        }
      }

      // If either side is empty, the sequent is trivially false.
      if (lhs_atomic.empty() || rhs_atomic.empty())
        return false;

      // First try without checking any TypeVars.
      // G, A |- D, A
      if (std::any_of(lhs_atomic.begin(), lhs_atomic.end(), [&](Btype& l) {
            return std::any_of(
              rhs_atomic.begin(), rhs_atomic.end(), [&](Btype& r) {
                return subtype_one(l, r);
              });
          }))
      {
        return true;
      }

      // TODO: accumulate bounds on TypeVars. This isn't right, yet.
      return std::any_of(lhs_atomic.begin(), lhs_atomic.end(), [&](Btype& l) {
        return std::any_of(rhs_atomic.begin(), rhs_atomic.end(), [&](Btype& r) {
          return typevar_bounds(l, r);
        });
      });
    }

    bool subtype_one(Btype& l, Btype& r)
    {
      // TypeFalse is a subtype of everything.
      if (l->type() == TypeFalse)
        return true;

      // TypeTrue is a supertype of everything.
      if (r->type() == TypeTrue)
        return true;

      // Skip TypeVar on either side.
      if ((l->type() == TypeVar) || (r->type() == TypeVar))
        return false;

      // These must be the same type.
      // TODO: region tracking
      if (r->type().in({Iso, Mut, Imm, Self}))
        return l->type() == r->type();

      // Tuples must be the same arity and each element must be a subtype.
      // TODO: remove TypeTuple from the language, use a trait
      if (r->type() == TypeTuple)
      {
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

      // Check for the same definition site.
      if (r->type() == TypeParam)
        return same_def_site(l, r);

      // Check for the same definition site with invariant typeargs.
      if (r->type().in({TypeAlias, Class}))
        return same_def_site(l, r) && invariant_typeargs(l, r);

      // A package resolves to a class. Once we have package resolution,
      // compare the classes, as different strings could resolve to the
      // same package.
      if (r->type() == Package)
      {
        return (l->type() == Package) &&
          (l->node->at(wfsub::Package_Id)->location() ==
           r->node->at(wfsub::Package_Id)->location());
      }

      // Check structural subtyping.
      if (r->type() == TypeTrait)
      {
        if (!l->type().in({Class, TypeTrait}))
          return false;

        // If any assumption is true, the trait is satisfied.
        if (std::any_of(
              assumptions.begin(), assumptions.end(), [&](auto& assume) {
                // Effectively: (l <: assume.sub) && (assume.sup <: r)
                return same_def_site(r, assume.sup) &&
                  same_def_site(l, assume.sub) &&
                  invariant_typeargs(r, assume.sup) &&
                  invariant_typeargs(l, assume.sub);
              }))
        {
          return true;
        }

        push_assume(l, r);

        if (l->type() == Class)
          push_self(l);

        bool ok = true;
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

          // if (!std::equal(
          //       rtparams->begin(),
          //       rtparams->end(),
          //       ltparams->begin(),
          //       ltparams->end(),
          //       [&](auto& rtparam, auto& ltparam) {
          //         return reduce(
          //           r->make(rtparam->at(wfsub::TypeParam_Bound)),
          //           l->make(ltparam->at(wfsub::TypeParam_Bound)));
          //       }))
          // {
          //   ok = false;
          //   break;
          // }

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

        // TODO: If the check succeeded, memoize it.
        pop_assume();

        if (l->type() == Class)
          pop_self();

        return ok;
      }

      // TODO: handle viewpoint adaptation
      if (r->type() == TypeView)
      {
        // TODO: the ned of a TypeView can be a TypeParam. If it is, we need to
        // be able to use that to fulfill Class / Trait / etc if the TypeView is
        // on the LHS, or to demand it if the TypeView is on the RHS.
        return false;
      }

      // Shouldn't get here in non-testing code.
      return false;
    }

    bool typevar_bounds(Btype& l, Btype& r)
    {
      bool ok = false;

      if (l->type() == TypeVar)
      {
        // TODO: l.upper += r
        ok = true;
      }

      if (r->type() == TypeVar)
      {
        // TODO: r.lower += l
        ok = true;
      }

      return ok;
    }

    bool same_def_site(Btype& l, Btype& r)
    {
      // The types must have the same definition site.
      return (l->node == r->node);
    }

    bool invariant_typeargs(Btype& l, Btype& r)
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

    std::pair<Btype, bool> reduce_view(Btype& t)
    {
      assert(t->type() == TypeView);
      auto start = t->node->begin();
      auto end = t->node->end();

      for (auto it = start; it != end; ++it)
      {
        auto lhs = NodeRange{start, it};
        auto rhs = NodeRange{it + 1, end};
        auto r = t->make(*it);

        if (r->type().in(
              {Package, Class, TypeTrait, TypeTuple, TypeTrue, TypeFalse}))
        {
          // The viewpoint path can be discarded.
          if (*it == t->node->back())
            return {r, false};

          // There is no view through this type, so treat it as true, i.e. top.
          return {t->make(TypeTrue), false};
        }
        else if (r->type() == TypeList)
        {
          // A.(B...) = (A.B)...
          if (*it == t->node->back())
            return {
              r->make(
                TypeList
                << (TypeView << -lhs << -r->node->at(wfsub::TypeList_Type))),
              false};

          // There is no view through this type, so treat it as true, i.e. top.
          return {t->make(TypeTrue), false};
        }
        else if (r->type().in({TypeUnion, TypeIsect}))
        {
          // A.(B | C).D = A.B.D | A.C.D
          // A.(B & C).D = A.B.D & A.C.D
          Node node = r->type();

          for (auto& rr : *r->node)
            node << (TypeView << -lhs << -rr << -rhs);

          return {r->make(node), false};
        }
        else if (r->type() == TypeAlias)
        {
          return {
            r->make(wfsub::TypeAlias_Type)
              ->make(TypeView << -lhs << -r->node << -rhs),
            false};
        }
        else if (r->type() == TypeView)
        {
          // A.(B.C).D = A.B.C.D
          auto node = TypeView << -lhs;

          for (auto& rr : *r->node)
            node << -rr;

          node << -rhs;
          return {r->make(node), false};
        }
      }

      // The TypeView contains only TypeParams and capabilities.
      auto t_imm = t->make(Imm);

      for (auto it = start; it != end; ++it)
      {
        auto r = t->make(*it);

        // If any step in the view is Imm, the whole view is Imm.
        if (reduce(r, t_imm))
        {
          if (*it == t->node->back())
            return {r, false};

          return {t->make(TypeIsect << Imm << -t->node->back()), false};
        }
      }

      // Indicate the TypeView needs no further reduction.
      return {t, true};
    }
  };

  bool subtype(Node sub, Node sup)
  {
    Sequent seq;
    return seq.reduce(make(sub), make(sup));
  }
}

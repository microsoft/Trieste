// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "subtype.h"

#include <cassert>

namespace verona
{
  bool invariant_ta(Btype& sub, Btype& sup)
  {
    (void)sub;
    (void)sup;
    // TODO: this is an infinite loop
    // because l and r are also in lhs and rhs, so it will be called again
    // assume l <: r, r <: l ?
    // what about unbound type parameters? should we be using the default?
    // only relevant type arguments need to be checked

    // Invariant type arguments.
    return true;
    // return (sub.bindings.size() == sup.bindings.size()) &&
    //   std::all_of(sub.bindings.begin(), sub.bindings.end(), [&](auto& l) {
    //          auto r = sup.bindings.find(l.first);
    //          return (r != sup.bindings.end()) &&
    //            subtype(sub(l.second), sup(r->second)) &&
    //            subtype(sup(r->second), sub(l.second));
    //        });
  }

  bool x_sub_union(Btype& sub, Btype& sup)
  {
    // Any satisfied disjunction is sufficient.
    assert(sup() == TypeUnion);
    for (auto& t : *sup.type)
      if (subtype(sub, sup(t)))
        return true;

    return false;
  }

  bool union_sub_x(Btype& sub, Btype& sup)
  {
    // Skip over empty types in the disjunction.
    assert(sub() == TypeUnion);
    for (auto& t : *sub.type)
      if (!(t->type() == TypeEmpty) && !subtype(sub(t), sup))
        return false;

    return true;
  }

  bool x_sub_isect(Btype& sub, Btype& sup)
  {
    // Skip over empty types in the conjunction.
    assert(sup() == TypeIsect);
    for (auto& t : *sup.type)
      if (!(t->type() == TypeEmpty) && !subtype(sub, sup(t)))
        return false;

    return true;
  }

  bool isect_sub_x(Btype& sub, Btype& sup)
  {
    // Any satisfied conjunction is sufficient.
    assert(sub() == TypeIsect);
    for (auto& t : *sub.type)
      if (subtype(sub(t), sup))
        return true;

    return false;
  }

  bool x_sub_alias(Btype& sub, Btype& sup)
  {
    assert(sup() == TypeAlias);
    return ((sub() == TypeAlias) && (sub.type == sup.type) &&
            invariant_ta(sub, sup)) ||
      subtype(sub, sup(wf / TypeAlias / Type));
  }

  bool alias_sub_x(Btype& sub, Btype& sup)
  {
    assert(sub() == TypeAlias);
    return ((sup() == TypeAlias) && (sub.type == sup.type) &&
            invariant_ta(sub, sup)) ||
      subtype(sub(wf / TypeAlias / Type), sup);
  }

  bool x_sub_tuple(Btype& sub, Btype& sup)
  {
    assert(sup() == TypeTuple);
    if ((sub() != TypeTuple) || (sub.type->size() != sup.type->size()))
      return false;

    for (size_t i = 0; i < sub.type->size(); i++)
    {
      if (!subtype(sub(i), sup(i)))
        return false;
    }

    return true;
  }

  bool x_sub_class(Btype& sub, Btype& sup)
  {
    // Must be the same class.
    assert(sup() == Class);
    return (sub() == Class) && (sub.type == sup.type) && invariant_ta(sub, sup);
  }

  bool x_sub_trait(Btype& sub, Btype& sup)
  {
    // TODO: could satisfy a trait with different parts of an isect. Could also
    // do this by separating a trait into an isect of each member.
    // Will need to assume success to avoid infinite recursion.
    assert(sup() == TypeTrait);
    (void)sub;
    (void)sup;
    return false;
  }

  bool x_sub_typeparam(Btype& sub, Btype& sup)
  {
    // Must be the same type parameter.
    assert(sup() == TypeParam);
    return (sub() == TypeParam) && (sub.type == sup.type) &&
      invariant_ta(sub, sup);
  }

  bool x_sub_view(Btype& sub, Btype& sup)
  {
    // TODO:
    assert(sup() == TypeView);
    (void)sub;
    (void)sup;
    return false;
  }

  bool x_sub_list(Btype&, Btype&)
  {
    // Nothing is a subtype of a TypeList. Two TypeLists may have different
    // instantiated arity, even if they have the same bounds. Use a TypeParam
    // with a TypeList upper bounds to get subtyping.
    return false;
  }

  bool x_sub_func(Btype& sub, Btype& sup)
  {
    // The sub function must accept all of the arguments of the sup function
    // (lhs(sup) <: lhs(sub)), and the sub function must return a subtype of the
    // sup result (rhs(sub) <: rhs(sup)).
    assert(sup() == TypeFunc);
    return (sub() == TypeFunc) &&
      subtype(
             {sup.type->at(wf / TypeFunc / Lhs), sup.bindings},
             {sub.type->at(wf / TypeFunc / Lhs), sub.bindings}) &&
      subtype(
             {sub.type->at(wf / TypeFunc / Rhs), sub.bindings},
             {sup.type->at(wf / TypeFunc / Rhs), sup.bindings});
  }

  bool x_sub_var(Btype& sub, Btype& sup)
  {
    // Must be the same type variable.
    assert(sup() == TypeVar);
    return (sub() == TypeVar) && (sub.type->location() == sup.type->location());
  }

  bool x_sub_package(Btype& sub, Btype& sup)
  {
    // A package resolves to a class. Once we have package resolution, compare
    // the classes, as different strings could resolve to the same package.
    assert(sup() == Package);
    return (sub() == Package) &&
      (sub.type->at(wf / Package / Id)->location() ==
       sup.type->at(wf / Package / Id)->location()) &&
      invariant_ta(sub, sup);
  }

  bool subtype(Btype sub, Btype sup)
  {
    // Empty types have no subtype relationship.
    if ((sub() == TypeEmpty) || (sup() == TypeEmpty))
      return false;

    if ((sub() == TypeUnion) && union_sub_x(sub, sup))
      return true;
    if ((sub() == TypeIsect) && isect_sub_x(sub, sup))
      return true;
    if ((sub() == TypeAlias) && alias_sub_x(sub, sup))
      return true;
    if ((sub() == TypeParam) && subtype(sub(wf / TypeParam / Bound), sup))
      return true;
    if ((sup() == TypeUnion) && x_sub_union(sub, sup))
      return true;
    if ((sup() == TypeIsect) && x_sub_isect(sub, sup))
      return true;
    if ((sup() == TypeAlias) && x_sub_alias(sub, sup))
      return true;
    if ((sup() == TypeTrait) && x_sub_trait(sub, sup))
      return true;
    if ((sup() == TypeParam) && x_sub_typeparam(sub, sup))
      return true;
    if ((sup() == TypeTuple) && x_sub_tuple(sub, sup))
      return true;
    if ((sup() == Class) && x_sub_class(sub, sup))
      return true;
    if ((sup() == TypeView) && x_sub_view(sub, sup))
      return true;
    if ((sup() == TypeList) && x_sub_list(sub, sup))
      return true;
    if ((sup() == TypeFunc) && x_sub_func(sub, sup))
      return true;
    if ((sup() == TypeVar) && x_sub_var(sub, sup))
      return true;
    if ((sup() == Package) && x_sub_package(sub, sup))
      return true;
    if ((sup().in({TypeUnit, Lin, In_, Out, Const})) && (sub() == sup()))
      return true;

    return false;
  }
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "typecheck.h"

#include "lang.h"
#include "wf.h"

namespace verona
{
  struct Bounds
  {
    NodeSet lower;
    NodeSet upper;
  };

  using Gamma = std::map<Location, Bounds>;

  void typecheck_function(Node node)
  {
    assert(node->type() == Function);

    Gamma g;
    auto params = node->at(wf / Function / Params);
    auto ret_type = node->at(wf / Function / Type);
    auto body = node->at(wf / Function / Block);

    for (auto& param : *params)
    {
      g[param->at(wf / Param / Ident)->location()].upper.insert(
        param->at(wf / Param / Type));
    }

    for (auto& stmt : *body)
    {
      if (stmt->type() == Bind)
      {
        auto lhs_id = stmt->at(wfPassANF / Bind / Ident)->location();
        auto rhs_stmt = stmt->at(wfPassANF / Bind / Rhs);

        if (rhs_stmt->type() == RefLet)
        {
          auto rhs_id = rhs_stmt->at(wfPassANF / RefLet / Ident)->location();
          // TODO: lhs_id <: rhs_id
        }
      }
      else if (stmt->type() == TypeAssert)
      {
        auto ident = stmt->at(wfPassANF / TypeAssert / RefLet)
                       ->at(wfPassANF / RefLet / Ident)
                       ->location();
        auto type = stmt->at(wfPassANF / TypeAssert / Type);
        g[ident].upper.insert(type);
      }
    }
  }
}

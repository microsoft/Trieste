// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "lang.h"

namespace verona
{
  PassDef traitisect()
  {
    // Turn all traits into intersections of single-function traits. Do this
    // late so that fields have already been turned into accessor functions and
    // partial application functions have already been generated.
    return {
      dir::once | dir::topdown,
      {
        T(TypeTrait)[TypeTrait] << (T(Ident) * T(ClassBody)[ClassBody]) >>
          [](Match& _) {
            // If we're inside a TypeIsect, put the new traits inside it.
            // Otherwise, create a new TypeIsect.
            Node isect =
              (_(TypeTrait)->parent()->type() == TypeIsect) ? Seq : TypeIsect;

            for (auto& member : *_(ClassBody))
            {
              if (member->type() == Function)
              {
                isect
                  << (TypeTrait << (Ident ^ _.fresh(l_trait))
                                << (ClassBody << member));
              }
            }

            // TODO: we're losing Use, Class, TypeAlias
            if (isect->empty())
              return _(TypeTrait);
            else if (isect->size() == 1)
              return isect->front();
            else
              return isect;
          },
      }};
  }
}

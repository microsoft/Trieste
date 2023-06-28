// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "lang.h"

namespace verona
{
  PassDef memberconflict()
  {
    return {
      dir::topdown | dir::once,
      {
        (T(FieldLet) / T(FieldVar))[Op] << (T(Ident)[Id]) >>
          ([](Match& _) -> Node {
            // Fields can conflict with other fields.
            auto field = _(Op);
            auto defs = field->scope()->lookdown(_(Id)->location());

            for (auto& def : defs)
            {
              if (def->type().in({FieldLet, FieldVar}) && def->precedes(field))
                return err(field, "duplicate field name")
                  << (ErrorAst ^ (def / Ident));
            }

            return NoChange;
          }),

        T(Function)[Function]
            << ((T(Ref) / T(DontCare))[Ref] * Name[Id] * T(TypeParams) *
                T(Params)[Params]) >>
          ([](Match& _) -> Node {
            // Functions can conflict with types, functions of the same arity
            // and handedness, and fields if the function is arity 1.
            auto func = _(Function);
            auto ref = _(Ref)->type();
            auto arity = _(Params)->size();
            auto defs = func->scope()->lookdown(_(Id)->location());

            for (auto& def : defs)
            {
              if (
                (def->type() == Function) && ((def / Ref)->type() == ref) &&
                ((def / Params)->size() == arity) && def->precedes(func))
              {
                return err(
                         func,
                         "this function has the same name, arity, and "
                         "handedness as "
                         "another function")
                  << (ErrorAst ^ (def / Ident));
              }
              else if (
                (def->type() == FieldLet) && (ref == DontCare) && (arity == 1))
              {
                return err(func, "this function has the same arity as a field")
                  << (ErrorAst ^ (def / Ident));
              }
              else if ((def->type() == FieldVar) && (arity == 1))
              {
                return err(func, "this function has the same arity as a field")
                  << (ErrorAst ^ (def / Ident));
              }
            }

            return NoChange;
          }),
      }};
  }
}

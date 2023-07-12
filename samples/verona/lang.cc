// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "lang.h"

#include "btype.h"
#include "lookup.h"
#include "subtype.h"
#include "wf.h"

namespace verona
{
  bool lookup(const NodeRange& n, std::initializer_list<Token> t)
  {
    return lookup_name(*n.first).one(t);
  }

  inline const auto TypeStruct = In(Type) / In(TypeList) / In(TypeTuple) /
    In(TypeView) / In(TypeUnion) / In(TypeIsect) / In(TypeSubtype);
  inline const auto TypeCaps = T(Iso) / T(Mut) / T(Imm);
  inline const auto Name = T(Ident) / T(Symbol);
  inline const auto Literal = T(String) / T(Escaped) / T(Char) / T(Bool) /
    T(Hex) / T(Bin) / T(Int) / T(Float) / T(HexFloat) / T(LLVM);

  auto typevar(auto& _)
  {
    return Type << (TypeVar ^ _.fresh(l_typevar));
  }

  auto typevar(auto& _, const Token& t)
  {
    auto n = _(t);
    return n ? n : typevar(_);
  }

  auto inherit()
  {
    return Inherit << DontCare;
  }

  auto inherit(auto& _, const Token& t)
  {
    return Inherit << (_(t) || DontCare);
  }

  auto typepred()
  {
    return TypePred << (Type << TypeTrue);
  }

  auto typepred(auto& _, const Token& t)
  {
    auto n = _(t);
    return n ? n : typepred();
  }

  PassDef structure()
  {
    return {
      // Field with a default value.
      // (equals (group let|var ident type) group)
      In(ClassBody) *
          (T(Equals)
           << ((T(Group)
                << ((T(Let) / T(Var))[Let] * T(Ident)[Id] * ~T(Type)[Type] *
                    End)) *
               T(Group)++[Rhs])) >>
        [](Match& _) {
          Node node = _(Let)->type() == Let ? FieldLet : FieldVar;
          return node << _(Id) << typevar(_, Type)
                      << (Lambda << TypeParams << Params << typevar(_)
                                 << typepred()
                                 << (Block << (Expr << (Default << _[Rhs]))));
        },

      // Field without a default value.
      // (group let|var ident type)
      In(ClassBody) *
          (T(Group)
           << ((T(Let) / T(Var))[Let] * T(Ident)[Id] * ~T(Type)[Type] * End)) >>
        [](Match& _) {
          Node node = _(Let)->type() == Let ? FieldLet : FieldVar;
          return node << _(Id) << typevar(_, Type) << DontCare;
        },

      // Function: `=` function after a `{}` function with no terminator.
      // (equals
      //  (group name typeparams params type llvmtype typepred brace ...) group)
      In(ClassBody) *
          (T(Equals)
           << ((T(Group)
                << (~T(Ref)[Ref] * ~Name[Id] * ~T(Square)[TypeParams] *
                    T(Paren)[Params] * ~T(Type)[Type] *
                    ~T(LLVMFuncType)[LLVMFuncType] * ~T(TypePred)[TypePred] *
                    T(Brace)[Block] * (Any * Any++)[Lhs])) *
               T(Group)++[Rhs])) >>
        [](Match& _) {
          _.def(Id, Ident ^ apply);
          return Seq << (Function << (_(Ref) || DontCare) << _(Id)
                                  << (TypeParams << *_[TypeParams])
                                  << (Params << *_[Params]) << typevar(_, Type)
                                  << (_(LLVMFuncType) || DontCare)
                                  << typepred(_, TypePred)
                                  << (Block << *_[Block]))
                     << (Equals << (Group << _[Lhs]) << _[Rhs]);
        },

      // Function: f[T](x: T = e): T = e
      // (equals (group name typeparams params type llvmtype typepred) group)
      In(ClassBody) *
          (T(Equals)
           << ((T(Group)
                << (~T(Ref)[Ref] * ~Name[Id] * ~T(Square)[TypeParams] *
                    T(Paren)[Params] * ~T(Type)[Type] *
                    ~T(LLVMFuncType)[LLVMFuncType] * ~T(TypePred)[TypePred] *
                    End)) *
               T(Group)++[Rhs])) >>
        [](Match& _) {
          _.def(Id, Ident ^ apply);
          return Function << (_(Ref) || DontCare) << _(Id)
                          << (TypeParams << *_[TypeParams])
                          << (Params << *_[Params]) << typevar(_, Type)
                          << (_(LLVMFuncType) || DontCare)
                          << typepred(_, TypePred)
                          << (Block << (Expr << (Default << _[Rhs])));
        },

      // Function: f[T](x: T = e): T { e }
      // (group name typeparams params type llvmtype typepred brace)
      In(ClassBody) * T(Group)
          << (~T(Ref)[Ref] * ~Name[Id] * ~T(Square)[TypeParams] *
              T(Paren)[Params] * ~T(Type)[Type] *
              ~T(LLVMFuncType)[LLVMFuncType] * ~T(TypePred)[TypePred] *
              ~T(Brace)[Block] * (Any++)[Rhs]) >>
        [](Match& _) {
          _.def(Id, Ident ^ apply);
          return Seq << (Function << (_(Ref) || DontCare) << _(Id)
                                  << (TypeParams << *_[TypeParams])
                                  << (Params << *_[Params]) << typevar(_, Type)
                                  << (_(LLVMFuncType) || DontCare)
                                  << typepred(_, TypePred)
                                  << (Block << *_[Block]))
                     << (Group << _[Rhs]);
        },

      // TypeParams.
      T(TypeParams) << (T(List)[TypeParams] * End) >>
        [](Match& _) { return TypeParams << *_[TypeParams]; },

      // TypeParam: (group ident)
      In(TypeParams) * T(Group) << (T(Ident)[Id] * End) >>
        [](Match& _) { return TypeParam << _(Id) << DontCare; },

      // TypeParam with default: (equals (group ident) group)
      In(TypeParams) * T(Equals)
          << ((T(Group) << (T(Ident)[Id] * End)) * T(Group)++[Rhs]) >>
        [](Match& _) {
          return TypeParam << _(Id) << (Type << (Default << _[Rhs]));
        },

      // ValueParam: (group ident type)
      In(TypeParams) * T(Group) << (T(Ident)[Id] * T(Type)[Type] * End) >>
        [](Match& _) { return ValueParam << _(Id) << _(Type) << Expr; },

      // ValueParam with default: (equals (group ident type) group)
      In(TypeParams) * T(Equals)
          << ((T(Group) << (T(Ident)[Id] * T(Type)[Type] * End)) *
              T(Group)++[Rhs]) >>
        [](Match& _) {
          return ValueParam << _(Id) << _(Type)
                            << (Expr << (Default << _[Rhs]));
        },

      In(TypeParams) * (!(T(TypeParam) / T(ValueParam)))[TypeParam] >>
        [](Match& _) {
          return err(
            _[TypeParam], "expected a type parameter or a value parameter");
        },

      T(ValueParam) >>
        [](Match& _) {
          return err(_[ValueParam], "value parameters aren't supported yet");
        },

      // Params.
      T(Params) << T(List)[Params] >>
        [](Match& _) { return Params << *_[Params]; },

      // Param: (group ident type)
      In(Params) * T(Group)
          << ((T(Ident) / T(DontCare))[Id] * ~T(Type)[Type] * End) >>
        [](Match& _) {
          auto id =
            (_(Id)->type() == DontCare) ? (Ident ^ _.fresh(l_param)) : _(Id);
          return Param << id << typevar(_, Type) << DontCare;
        },

      // Param: (equals (group ident type) group)
      In(Params) * T(Equals)
          << ((T(Group)
               << ((T(Ident) / T(DontCare))[Id] * ~T(Type)[Type] * End)) *
              T(Group)++[Expr]) >>
        [](Match& _) {
          auto id =
            (_(Id)->type() == DontCare) ? (Ident ^ _.fresh(l_param)) : _(Id);
          return Param << id << typevar(_, Type)
                       << (Lambda << TypeParams << Params << typevar(_)
                                  << typepred()
                                  << (Block << (Expr << (Default << _[Expr]))));
        },

      In(Params) * (!T(Param))[Param] >>
        [](Match& _) { return err(_[Param], "expected a parameter"); },

      // Use.
      (In(ClassBody) / In(Block)) * T(Group) << T(Use)[Use] * (Any++)[Type] >>
        [](Match& _) {
          return (Use ^ _(Use)) << (Type << (_[Type] || DontCare));
        },

      T(Use)[Use] << End >>
        [](Match& _) { return err(_[Use], "can't put a `use` here"); },

      // TypeAlias: (equals (group typealias typeparams typepred) group)
      (In(ClassBody) / In(Block)) * T(Equals)
          << ((T(Group)
               << (T(TypeAlias) * T(Ident)[Id] * ~T(Square)[TypeParams] *
                   ~T(TypePred)[TypePred] * End)) *
              T(Group)++[Rhs]) >>
        [](Match& _) {
          return TypeAlias << _(Id) << (TypeParams << *_[TypeParams])
                           << typepred(_, TypePred)
                           << (Type << (Default << _[Rhs]));
        },

      (In(ClassBody) / In(Block)) * T(TypeAlias)[TypeAlias] << End >>
        [](Match& _) {
          return err(_[TypeAlias], "expected a `type` definition");
        },
      T(TypeAlias)[TypeAlias] << End >>
        [](Match& _) {
          return err(_[TypeAlias], "can't put a `type` definition here");
        },

      // Class.
      // (group class ident typeparams type typepred brace ...)
      (In(Top) / In(ClassBody) / In(Block)) * T(Group)
          << (T(Class) * T(Ident)[Id] * ~T(Square)[TypeParams] *
              ~T(Type)[Type] * ~T(TypePred)[TypePred] * T(Brace)[ClassBody] *
              (Any++)[Rhs]) >>
        [](Match& _) {
          return Seq << (Class << _(Id) << (TypeParams << *_[TypeParams])
                               << inherit(_, Type) << typepred(_, TypePred)
                               << (ClassBody << *_[ClassBody]))
                     << (Group << _[Rhs]);
        },

      (In(Top) / In(ClassBody) / In(Block)) * T(Class)[Class] << End >>
        [](Match& _) { return err(_[Class], "expected a `class` definition"); },
      T(Class)[Class] << End >>
        [](Match& _) {
          return err(_[Class], "can't put a `class` definition here");
        },

      // Default initializers. These were taken off the end of an Equals.
      // Depending on how many there are, either repack them in an equals or
      // insert them directly into the parent node.
      (T(Default) << End) >> ([](Match&) -> Node { return DontCare; }),
      (T(Default) << (T(Group)[Rhs] * End)) >>
        [](Match& _) { return Seq << *_[Rhs]; },
      (T(Default) << (T(Group)++[Rhs]) * End) >>
        [](Match& _) { return Equals << _[Rhs]; },

      // Type structure.
      TypeStruct * T(Group)[Type] >> [](Match& _) { return Type << *_[Type]; },
      TypeStruct * (T(List) / T(Paren))[TypeTuple] >>
        [](Match& _) { return Type << (TypeTuple << *_[TypeTuple]); },

      // Anonymous structural types.
      TypeStruct * T(Brace)[ClassBody] >>
        [](Match& _) {
          return TypeTrait << (Ident ^ _.fresh(l_trait))
                           << (ClassBody << *_[ClassBody]);
        },

      // Strings in types are package descriptors.
      TypeStruct * (T(String) / T(Escaped))[Package] >>
        [](Match& _) { return Package << _(Package); },

      TypeStruct *
          (T(Equals) / T(Arrow) / T(Use) / T(Class) / T(TypeAlias) / T(Var) /
           T(Let) / T(Ref) / T(If) / T(Else) / T(New) / T(Try) /
           T(LLVMFuncType) / Literal)[Type] >>
        [](Match& _) { return err(_[Type], "can't put this in a type"); },

      // A group can be in a Block, Expr, ExprSeq, Tuple, or Assign.
      (In(Block) / In(Expr) / In(ExprSeq) / In(Tuple) / In(Assign)) *
          T(Group)[Group] >>
        [](Match& _) { return Expr << *_[Group]; },

      // An equals can be in a Block, ExprSeq, Tuple, or Expr.
      (In(Block) / In(ExprSeq) / In(Tuple)) * T(Equals)[Equals] >>
        [](Match& _) { return Expr << (Assign << *_[Equals]); },
      In(Expr) * T(Equals)[Equals] >>
        [](Match& _) { return Assign << *_[Equals]; },

      // A list can be in a Block, ExprSeq, or Expr.
      (In(Block) / In(ExprSeq)) * T(List)[List] >>
        [](Match& _) { return Expr << (Tuple << *_[List]); },
      In(Expr) * T(List)[List] >> [](Match& _) { return Tuple << *_[List]; },

      // Empty parens are Unit.
      In(Expr) * (T(Paren) << End) >> ([](Match&) -> Node { return Unit; }),

      // Empty expr is Unit.
      T(Expr) << End >> [](Match&) { return Expr << Unit; },

      // A tuple of arity 1 is a scalar.
      In(Expr) * (T(Tuple) << (T(Expr)[Expr] * End)) >>
        [](Match& _) { return _(Expr); },

      // A tuple of arity 0 is unit. This might happen through rewrites as well
      // as directly from syntactically empty parens.
      In(Expr) * (T(Tuple) << End) >> ([](Match&) -> Node { return Unit; }),

      // Parens with one element are an Expr. Put the group, list, or equals
      // into the expr, where it will become an expr, tuple, or assign.
      In(Expr) * ((T(Paren) << (Any[Lhs] * End))) >>
        [](Match& _) { return _(Lhs); },

      // Parens with multiple elements are an ExprSeq.
      In(Expr) * T(Paren)[Paren] >>
        [](Match& _) { return ExprSeq << *_[Paren]; },

      // Typearg structure.
      (TypeStruct / In(Expr)) * T(Square)[TypeArgs] >>
        [](Match& _) { return TypeArgs << *_[TypeArgs]; },
      T(TypeArgs) << T(List)[TypeArgs] >>
        [](Match& _) { return TypeArgs << *_[TypeArgs]; },
      In(TypeArgs) * T(Group)[Type] >>
        [](Match& _) { return Type << *_[Type]; },
      In(TypeArgs) * T(Paren)[Type] >>
        [](Match& _) { return Type << *_[Type]; },

      // Object literal.
      In(Expr) * T(New) * T(Brace)[ClassBody] >>
        [](Match& _) {
          auto class_id = _.fresh(l_class);
          return Seq << (Lift << Block
                              << (Class << (Ident ^ class_id) << TypeParams
                                        << inherit() << typepred()
                                        << (ClassBody << *_[ClassBody])))
                     << (Expr << (Ident ^ class_id) << DoubleColon
                              << (Ident ^ create) << Unit);
        },

      // Lambda { [T](x: T = e, ...): T where T => ... }
      // (brace (group typeparams params type typepred) (group arrow) ...)
      In(Expr) * T(Brace)
          << ((T(Group)
               << (~T(Square)[TypeParams] * T(Paren)[Params] * ~T(Type)[Type] *
                   ~T(TypePred)[TypePred])) *
              (T(Group) << T(Arrow)) * (Any++)[Rhs]) >>
        [](Match& _) {
          return Lambda << (TypeParams << *_[TypeParams])
                        << (Params << *_[Params]) << typevar(_, Type)
                        << typepred(_, TypePred) << (Block << _[Rhs]);
        },

      // Lambda: { a (, b...) => ... }
      // (brace (list|group) (group arrow) ...)
      In(Expr) *
          (T(Brace)
           << ((T(List) / T(Group))[Params] * (T(Group) << T(Arrow)) *
               Any++[Rhs])) >>
        [](Match& _) {
          return Lambda << TypeParams << (Params << _[Params]) << typevar(_)
                        << typepred() << (Block << _[Rhs]);
        },

      // Zero argument lambda: { ... } (brace ...)
      In(Expr) * T(Brace) << (!(T(Group) << T(Arrow)))++[Rhs] >>
        [](Match& _) {
          return Lambda << TypeParams << Params << typevar(_) << typepred()
                        << (Block << _[Rhs]);
        },

      // Var.
      In(Expr) * T(Var)[Var] * T(Ident)[Id] >>
        [](Match& _) { return Var << _(Id); },

      T(Var)[Var] << End >>
        [](Match& _) { return err(_[Var], "`var` needs an identifier"); },

      // Let.
      In(Expr) * T(Let)[Let] * T(Ident)[Id] >>
        [](Match& _) { return Let << _(Id); },

      T(Let)[Let] << End >>
        [](Match& _) { return err(_[Let], "`let` needs an identifier"); },

      // Move a ref to the last expr of a sequence.
      In(Expr) * T(Ref) * T(Expr)[Expr] >>
        [](Match& _) { return Expr << Ref << *_[Expr]; },
      In(Expr) * T(Ref) * T(Expr)[Lhs] * T(Expr)[Rhs] >>
        [](Match& _) { return Seq << _[Lhs] << Ref << _[Rhs]; },
      In(Expr) * T(Ref) * T(Expr)[Expr] * End >>
        [](Match& _) { return Expr << Ref << *_[Expr]; },

      // Lift Use, Class, TypeAlias to Block.
      In(Expr) * (T(Use) / T(Class) / T(TypeAlias))[Lift] >>
        [](Match& _) { return Lift << Block << _[Lift]; },

      // A Type at the end of an Expr is a TypeAssert. A tuple is never directly
      // wrapped in a TypeAssert, but an Expr containing a Tuple can be.
      T(Expr) << (((!T(Type))++)[Expr] * T(Type)[Type] * End) >>
        [](Match& _) {
          return Expr << (TypeAssert << (Expr << _[Expr]) << _(Type));
        },

      In(Expr) *
          (TypeCaps / T(TypePred) / T(Self) / T(Arrow) /
           T(LLVMFuncType))[Expr] >>
        [](Match& _) {
          return err(_[Expr], "can't put this in an expression");
        },

      // A Block that doesn't end with an Expr gets an implicit Unit.
      In(Block) * (!T(Expr))[Lhs] * End >>
        [](Match& _) { return Seq << _(Lhs) << (Expr << Unit); },

      // An empty Block gets an implicit Unit.
      T(Block) << End >> [](Match&) { return Block << (Expr << Unit); },

      // Remove empty and malformed groups.
      T(Group) << End >> ([](Match&) -> Node { return {}; }),
      T(Group)[Group] >> [](Match& _) { return err(_[Group], "syntax error"); },
    };
  }

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

  inline const auto TypeName =
    T(TypeClassName) / T(TypeAliasName) / T(TypeParamName) / T(TypeTraitName);

  inline const auto TypeElem = T(Type) / TypeCaps / TypeName / T(TypeTrait) /
    T(TypeTuple) / T(Self) / T(TypeList) / T(TypeView) / T(TypeIsect) /
    T(TypeUnion) / T(TypeVar) / T(Package) / T(TypeSubtype) / T(TypeTrue) /
    T(TypeFalse);

  Node makename(Lookups& defs, Node lhs, Node id, Node ta, bool func = false)
  {
    if (defs.defs.size() == 0)
      return Error << (ErrorMsg ^ "unknown type name")
                   << ((ErrorAst ^ id) << lhs << id << ta);

    if (func)
    {
      if (std::any_of(defs.defs.begin(), defs.defs.end(), [](auto& def) {
            return (def.def->type() == Function) && !def.too_many_typeargs;
          }))
      {
        return FunctionName << lhs << id << ta;
      }
      else if (std::any_of(defs.defs.begin(), defs.defs.end(), [](auto& def) {
                 return (def.def->type() == Function) && def.too_many_typeargs;
               }))
      {
        return Error << (ErrorMsg ^ "too many function type arguments")
                     << ((ErrorAst ^ id) << lhs << id << ta);
      }
    }

    if (defs.defs.size() > 1)
    {
      auto err = Error << (ErrorMsg ^ "ambiguous type name")
                       << ((ErrorAst ^ id) << lhs << id << ta);

      for (auto& def : defs.defs)
        err << (ErrorAst ^ (def.def / Ident));

      return err;
    }

    if (std::all_of(defs.defs.begin(), defs.defs.end(), [](auto& def) {
          return def.too_many_typeargs;
        }))
    {
      return Error << (ErrorMsg ^ "too many type arguments")
                   << ((ErrorAst ^ id) << lhs << id << ta);
    }

    if (defs.one({Class}))
      return TypeClassName << lhs << id << ta;
    if (defs.one({TypeAlias}))
      return TypeAliasName << lhs << id << ta;
    if (defs.one({TypeParam}))
      return TypeParamName << lhs << id << ta;
    if (defs.one({TypeTrait}))
      return TypeTraitName << lhs << id << ta;

    return Error << (ErrorMsg ^ "not a type name")
                 << ((ErrorAst ^ id) << lhs << id << ta)
                 << (ErrorAst ^ (defs.defs.front().def / Ident));
  }

  PassDef typenames()
  {
    return {
      TypeStruct * T(DontCare)[DontCare] >>
        [](Match& _) { return TypeVar ^ _.fresh(l_typevar); },

      // Names on their own must be types.
      TypeStruct * T(Ident)[Id] * ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          auto defs = lookup_name(_(Id), _(TypeArgs));
          return makename(defs, DontCare, _(Id), (_(TypeArgs) || TypeArgs));
        },

      // Scoping binds most tightly.
      TypeStruct * TypeName[Lhs] * T(DoubleColon) * T(Ident)[Id] *
          ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          auto defs = lookup_scopedname_name(_(Lhs), _(Id), _(TypeArgs));
          return makename(defs, _(Lhs), _(Id), (_(TypeArgs) || TypeArgs));
        },
    };
  }

  PassDef typeview()
  {
    return {
      // Viewpoint adaptation binds more tightly than function types.
      TypeStruct * TypeElem[Lhs] * T(Dot) * TypeElem[Rhs] >>
        [](Match& _) {
          return TypeView << (Type << _[Lhs]) << (Type << _[Rhs]);
        },

      // TypeList binds more tightly than function types.
      TypeStruct * TypeElem[Lhs] * T(Ellipsis) >>
        [](Match& _) { return TypeList << (Type << _[Lhs]); },

      TypeStruct * T(DoubleColon)[DoubleColon] >>
        [](Match& _) { return err(_[DoubleColon], "misplaced type scope"); },
      TypeStruct * T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          return err(_[TypeArgs], "type arguments on their own are not a type");
        },
      TypeStruct * T(Dot)[Dot] >>
        [](Match& _) { return err(_[Dot], "misplaced type viewpoint"); },
      TypeStruct * T(Ellipsis)[Ellipsis] >>
        [](Match& _) { return err(_[Ellipsis], "misplaced type list"); },
    };
  }

  PassDef typefunc()
  {
    return {
      TypeStruct * (TypeElem[Lhs] * T(Symbol, "->")) *
          (TypeElem * T(Symbol, "->"))++[Op] * TypeElem[Rhs] >>
        [](Match& _) {
          // T1...->T2 =
          //   ({ (Self & mut, T1...): T2 } & mut)
          // | ({ (Self & imm, T1...): T2 } & imm)
          Node r = TypeUnion;
          std::initializer_list<Token> caps = {Mut, Imm};

          for (auto& cap : caps)
          {
            auto params = Params
              << (Param << (Ident ^ _.fresh(l_param))
                        << (Type
                            << (TypeIsect << (Type << Self) << (Type << cap)))
                        << DontCare)
              << (Param << (Ident ^ _.fresh(l_param)) << (Type << clone(_(Lhs)))
                        << DontCare);

            auto it = _[Op].first;
            auto end = _[Op].second;

            while (it != end)
            {
              params
                << (Param << (Ident ^ _.fresh(l_param)) << (Type << clone(*it))
                          << DontCare);
              it = it + 2;
            }

            r
              << (Type
                  << (TypeIsect
                      << (Type
                          << (TypeTrait
                              << (Ident ^ _.fresh(l_trait))
                              << (ClassBody
                                  << (Function << DontCare << (Ident ^ apply)
                                               << TypeParams << params
                                               << (Type << clone(_(Rhs)))
                                               << DontCare << typepred()
                                               << (Block << (Expr << Unit))))))
                      << (Type << cap)));
          }

          return r;
        },

      TypeStruct * T(Symbol, "->")[Symbol] >>
        [](Match& _) { return err(_[Symbol], "misplaced function type"); },
    };
  }

  PassDef typealg()
  {
    return {
      // Build algebraic types.
      TypeStruct * TypeElem[Lhs] * T(Symbol, "&") * TypeElem[Rhs] >>
        [](Match& _) {
          return TypeIsect << (Type << _[Lhs]) << (Type << _[Rhs]);
        },
      TypeStruct * TypeElem[Lhs] * T(Symbol, "\\|") * TypeElem[Rhs] >>
        [](Match& _) {
          return TypeUnion << (Type << _[Lhs]) << (Type << _[Rhs]);
        },
      TypeStruct * TypeElem[Lhs] * T(Symbol, "<") * TypeElem[Rhs] >>
        [](Match& _) {
          return TypeSubtype << (Type << _[Lhs]) << (Type << _[Rhs]);
        },

      TypeStruct * T(Symbol)[Symbol] >>
        [](Match& _) { return err(_[Symbol], "invalid symbol in type"); },
    };
  }

  inline const auto Std = TypeClassName << DontCare << (Ident ^ standard)
                                        << TypeArgs;
  inline const auto Builtin = TypeClassName << Std << (Ident ^ builtin)
                                            << TypeArgs;
  inline const auto ClassUnit = TypeClassName << Builtin << (Ident ^ unit)
                                              << TypeArgs;

  PassDef typeflat()
  {
    return {
      // Flatten algebraic types.
      In(TypeUnion) * T(TypeUnion)[Lhs] >>
        [](Match& _) { return Seq << *_[Lhs]; },
      In(TypeIsect) * T(TypeIsect)[Lhs] >>
        [](Match& _) { return Seq << *_[Lhs]; },
      In(TypeView) * T(TypeView)[Lhs] >>
        [](Match& _) { return Seq << *_[Lhs]; },

      // Tuples of arity 1 are scalar types.
      T(TypeTuple) << (TypeElem[Op] * End) >> [](Match& _) { return _(Op); },

      // Tuples of arity 0 are the unit type.
      T(TypeTuple) << End >> [](Match&) { return clone(ClassUnit); },

      // Flatten Type nodes. The top level Type node won't go away.
      TypeStruct * T(Type) << (TypeElem[Op] * End) >>
        [](Match& _) { return _(Op); },

      T(Type)[Type] << End >>
        [](Match& _) {
          return err(_[Type], "can't use an empty type assertion");
        },

      T(Type)[Type] << (Any * Any) >>
        [](Match& _) {
          return err(_[Type], "can't use adjacency to specify a type");
        },
    };
  }

  PassDef typevalid()
  {
    return {
      dir::once | dir::topdown,
      {
        T(TypeAlias)[TypeAlias] >> ([](Match& _) -> Node {
          if (recursive_typealias(_(TypeAlias)))
            return err(_[TypeAlias], "recursive type alias");

          return NoChange;
        }),

        In(TypePred)++ * T(TypeAliasName)[TypeAliasName] *
            !(In(TypeSubtype)++) >>
          ([](Match& _) -> Node {
            if (!make_btype(_(TypeAliasName))->valid_predicate())
              return err(
                _[Type], "this type alias isn't a valid type predicate");

            return NoChange;
          }),

        In(TypePred)++ * !(In(TypeSubtype)++) *
            (TypeCaps / T(TypeClassName) / T(TypeParamName) / T(TypeTraitName) /
             T(TypeTrait) / T(TypeTuple) / T(Self) / T(TypeList) / T(TypeView) /
             T(TypeVar) / T(Package))[Type] >>
          [](Match& _) {
            return err(_[Type], "can't put this in a type predicate");
          },

        In(Inherit)++ * T(TypeAliasName)[TypeAliasName] >>
          ([](Match& _) -> Node {
            if (!make_btype(_(TypeAliasName))->valid_inherit())
              return err(
                _[Type], "this type alias isn't valid for inheritance");

            return NoChange;
          }),

        In(Inherit)++ *
            (TypeCaps / T(TypeParamName) / T(TypeTuple) / T(Self) /
             T(TypeList) / T(TypeView) / T(TypeUnion) / T(TypeVar) /
             T(Package) / T(TypeSubtype) / T(TypeTrue) / T(TypeFalse))[Type] >>
          [](Match& _) { return err(_[Type], "can't inherit from this type"); },
      }};
  }

  PassDef codereuse()
  {
    return {
      dir::once | dir::topdown,
      {
        T(Class)
            << (T(Ident)[Ident] * T(TypeParams)[TypeParams] *
                T(Inherit)[Inherit] * T(TypePred)[TypePred] *
                T(ClassBody)[ClassBody]) >>
          [](Match& _) {
            // TODO:
            // reuse stuff in Type if (a) it's not ambiguous and (b) it's not
            // already provided in ClassBody
            // need to do type substitution
            // strip Inherit from here on
            return Class << _(Ident) << _(TypeParams) << _(Inherit)
                         << _(TypePred) << _(ClassBody);
          },
      }};
  }

  auto make_conditional(Match& _)
  {
    // Pack all of the branches into a single conditional and unpack them
    // in the follow-on rules.
    auto lambda = _(Lhs);
    auto params = lambda / Params;
    Node cond = Expr;
    Node block = Block;
    Node args;

    if (params->empty())
    {
      // This is a boolean conditional.
      cond << _[Expr];
      args = Unit;
    }
    else
    {
      // This is a TypeTest conditional.
      auto id = _.fresh();
      Node lhs;
      Node type;

      if (params->size() == 1)
      {
        // This is a single parameter.
        auto lhs_id = _.fresh();
        lhs = Expr << (Let << (Ident ^ lhs_id));
        type = clone(params->front() / Type);
        args = Ident ^ lhs_id;
      }
      else
      {
        // This is multiple parameters. We need to build a TypeTuple for the
        // Cast and a Tuple both for destructuring the cast value and for the
        // arguments to be passed to the lambda on success.
        Node typetuple = TypeTuple;
        args = Tuple;
        lhs = Tuple;
        type = Type << typetuple;

        for (auto& param : *params)
        {
          auto lhs_id = _.fresh();
          args << (Expr << (Ident ^ lhs_id));
          lhs << (Expr << (Let << (Ident ^ lhs_id)));
          typetuple << clone(param / Type);
        }
      }

      cond
        << (TypeTest << (Expr
                         << (Assign << (Expr << (Let << (Ident ^ id)))
                                    << (Expr << _[Expr])))
                     << clone(type));

      block
        << (Expr
            << (Assign << (Expr << lhs)
                       << (Expr << (Cast << (Expr << (Ident ^ id)) << type))));
    }

    return Conditional << cond << (block << (Expr << lambda << args))
                       << (Block << (Expr << (Conditional << _[Rhs])));
  }

  PassDef conditionals()
  {
    return {
      // Conditionals are right-associative.
      In(Expr) * T(If) * (!T(Lambda) * (!T(Lambda))++)[Expr] * T(Lambda)[Lhs] *
          ((T(Else) * T(If) * (!T(Lambda) * (!T(Lambda))++) * T(Lambda))++ *
           ~(T(Else) * T(Lambda)))[Rhs] >>
        [](Match& _) { return make_conditional(_); },

      T(Conditional)
          << ((T(Else) * T(If) * (!T(Lambda) * (!T(Lambda))++)[Expr] *
               T(Lambda)[Lhs]) *
              Any++[Rhs]) >>
        [](Match& _) { return make_conditional(_); },

      T(Conditional) << (T(Else) * T(Lambda)[Rhs] * End) >>
        [](Match& _) { return Seq << _(Rhs) << Unit; },

      T(Conditional) << End >> ([](Match&) -> Node { return Unit; }),

      T(If)[If] >>
        [](Match& _) {
          return err(_[If], "`if` must be followed by a condition and braces");
        },

      T(Else)[Else] >>
        [](Match& _) {
          return err(
            _[Else],
            "`else` must follow an `if` and be followed by an `if` or braces");
        },
    };
  }

  PassDef reference()
  {
    return {
      // LLVM literal.
      In(Expr) * T(LLVM)[LLVM] * T(Ident)[Lhs] * T(Ident)++[Rhs] >>
        [](Match& _) {
          auto llvm = _(LLVM);
          auto rhs = _[Rhs];
          auto s = std::string()
                     .append(llvm->location().view())
                     .append(" %")
                     .append(_(Lhs)->location().view());

          for (auto& i = rhs.first; i != rhs.second; ++i)
            s.append(", %").append((*i)->location().view());

          return LLVM ^ s;
        },

      In(Expr) * T(LLVM)[Lhs] * T(LLVM)[Rhs] >>
        [](Match& _) {
          return LLVM ^
            std::string()
              .append(_(Lhs)->location().view())
              .append(" ")
              .append(_(Rhs)->location().view());
        },

      // Dot notation. Use `Id` as a selector, even if it's in scope.
      In(Expr) * T(Dot) * Name[Id] * ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          return Seq << Dot << (Selector << _[Id] << (_(TypeArgs) || TypeArgs));
        },

      // Local reference.
      In(Expr) * T(Ident)[Id]([](auto& n) { return lookup(n, {Var}); }) >>
        [](Match& _) { return RefVar << _(Id); },

      In(Expr) * T(Ident)[Id]([](auto& n) {
        return lookup(n, {Let, Param});
      }) >>
        [](Match& _) { return RefLet << _(Id); },

      // Unscoped type reference.
      In(Expr) * T(Ident)[Id]([](auto& n) {
        return lookup(n, {Class, TypeAlias, TypeParam});
      }) * ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          auto defs = lookup_name(_(Id), _(TypeArgs));
          return makename(defs, DontCare, _(Id), (_(TypeArgs) || TypeArgs));
        },

      // Unscoped reference that isn't a local or a type. Treat it as a
      // selector, even if it resolves to a Function.
      In(Expr) * Name[Id] * ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) { return Selector << _(Id) << (_(TypeArgs) || TypeArgs); },

      // Scoped lookup.
      In(Expr) *
          (TypeName[Lhs] * T(DoubleColon) * Name[Id] *
           ~T(TypeArgs)[TypeArgs])[Type] >>
        [](Match& _) {
          auto defs = lookup_scopedname_name(_(Lhs), _(Id), _(TypeArgs));
          return makename(defs, _(Lhs), _(Id), (_(TypeArgs) || TypeArgs), true);
        },

      In(Expr) * T(DoubleColon) >>
        [](Match& _) { return err(_[DoubleColon], "expected a scoped name"); },

      // Create sugar, with no arguments.
      In(Expr) * TypeName[Lhs] * ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          return FunctionName << _(Lhs) << (Ident ^ create)
                              << (_(TypeArgs) || TypeArgs);
        },

      // Lone TypeArgs are typeargs on apply.
      In(Expr) * T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          return Seq << Dot << (Selector << (Ident ^ apply) << _[TypeArgs]);
        },
    };
  }

  bool is_llvm_call(Node op, size_t arity)
  {
    // `op` must already be in the AST in order to resolve the FunctionName.
    if (op->type() == FunctionName)
    {
      auto look = lookup_scopedname(op);

      for (auto& def : look.defs)
      {
        if (
          (def.def->type() == Function) &&
          ((def.def / Params)->size() == arity) &&
          ((def.def / LLVMFuncType)->type() == LLVMFuncType))
        {
          return true;
        }
      }
    }

    return false;
  }

  auto arg(Node args, Node arg)
  {
    if (arg)
    {
      if (arg->type() == Tuple)
        args->push_back({arg->begin(), arg->end()});
      else if (arg->type() == Expr)
        args << arg;
      else if (arg->type() != Unit)
        args << (Expr << arg);
    }

    return args;
  }

  auto call(Node op, Node lhs = {}, Node rhs = {})
  {
    auto args = arg(arg(Args, lhs), rhs);

    if (!is_llvm_call(op, args->size()))
      return NLRCheck << (Call << op << args);

    return Call << op << args;
  }

  inline const auto Object0 = Literal / T(RefVar) / T(RefVarLHS) / T(RefLet) /
    T(Unit) / T(Tuple) / T(Lambda) / T(Call) / T(NLRCheck) / T(CallLHS) /
    T(Assign) / T(Expr) / T(ExprSeq) / T(DontCare) / T(Conditional) /
    T(TypeTest) / T(Cast);
  inline const auto Object = Object0 / (T(TypeAssert) << (Object0 * T(Type)));
  inline const auto Operator = T(New) / T(FunctionName) / T(Selector);
  inline const auto Apply = (Selector << (Ident ^ apply) << TypeArgs);

  PassDef reverseapp()
  {
    return {
      // Dot: reverse application. This binds most strongly.
      (Object / Operator)[Lhs] * T(Dot) * Operator[Rhs] >>
        [](Match& _) { return call(_(Rhs), _(Lhs)); },

      (Object / Operator)[Lhs] * T(Dot) * Object[Rhs] >>
        [](Match& _) { return call(clone(Apply), _(Rhs), _(Lhs)); },

      T(Dot)[Dot] >>
        [](Match& _) {
          return err(_[Dot], "must use `.` with values and operators");
        },
    };
  }

  inline const auto UnitCreate =
    (FunctionName << ClassUnit << (Ident ^ create) << TypeArgs);
  inline const auto CallUnitCreate = (Call << UnitCreate << Args);

  PassDef application()
  {
    // These rules allow expressions such as `-3 * -4` or `not a and not b` to
    // have the expected meaning.
    return {
      // Ref expressions.
      T(Ref) * T(RefVar)[RefVar] >>
        [](Match& _) { return RefVarLHS << *_[RefVar]; },

      T(Ref) * (T(NLRCheck) << T(Call)[Call]) >>
        [](Match& _) { return NLRCheck << (CallLHS << *_[Call]); },

      // Try expressions.
      T(Try) * (T(NLRCheck) << (T(Call) / T(CallLHS))[Call]) >>
        [](Match& _) { return _(Call); },

      T(Try) * T(Lambda)[Lambda] >>
        [](Match& _) {
          return Call << clone(Apply) << (Args << (Expr << _(Lambda)));
        },

      // Adjacency: application.
      In(Expr) * Object[Lhs] * Object[Rhs] >>
        [](Match& _) { return call(clone(Apply), _(Lhs), _(Rhs)); },

      // Prefix. This doesn't rewrite `Op Op`.
      In(Expr) * Operator[Op] * Object[Rhs] >>
        [](Match& _) { return call(_(Op), _(Rhs)); },

      // Infix. This doesn't rewrite with an operator on Lhs or Rhs.
      In(Expr) * Object[Lhs] * Operator[Op] * Object[Rhs] >>
        [](Match& _) { return call(_(Op), _(Lhs), _(Rhs)); },

      // Zero argument call.
      In(Expr) * Operator[Op] * --(Object / Operator) >>
        [](Match& _) { return call(_(Op)); },

      // Tuple flattening.
      In(Tuple) * T(Expr) << (Object[Lhs] * T(Ellipsis) * End) >>
        [](Match& _) { return TupleFlatten << (Expr << _(Lhs)); },

      // Use `_` (DontCare) for partial application of arbitrary arguments.
      T(Call)
          << (Operator[Op] *
              (T(Args)
               << ((T(Expr) << !T(DontCare))++ *
                   (T(Expr)
                    << (T(DontCare) /
                        (T(TypeAssert) << (T(DontCare) * T(Type))))) *
                   T(Expr)++))[Args]) >>
        [](Match& _) {
          Node params = Params;
          Node args = Args;
          auto lambda = Lambda << TypeParams << params << typevar(_)
                               << typepred()
                               << (Block << (Expr << (Call << _(Op) << args)));

          for (auto& arg : *_(Args))
          {
            auto expr = arg->front();

            if (expr->type() == DontCare)
            {
              auto id = _.fresh(l_param);
              params << (Param << (Ident ^ id) << typevar(_) << DontCare);
              args << (Expr << (RefLet << (Ident ^ id)));
            }
            else if (expr->type() == TypeAssert)
            {
              auto id = _.fresh(l_param);
              params << (Param << (Ident ^ id) << (expr / Type) << DontCare);
              args << (Expr << (RefLet << (Ident ^ id)));
            }
            else
            {
              args << arg;
            }
          }

          return lambda;
        },

      // Remove the NLRCheck from a partial application.
      T(NLRCheck) << (T(Lambda)[Lambda] * End) >>
        [](Match& _) { return _(Lambda); },

      In(Expr) * T(DontCare) >>
        [](Match& _) {
          // Remaining DontCare are discarded bindings.
          return Let << (Ident ^ _.fresh());
        },

      // Turn remaining uses of Unit into std::builtin::Unit::create()
      T(Unit) >> [](Match&) { return clone(CallUnitCreate); },

      T(Ellipsis) >>
        [](Match& _) {
          return err(_[Ellipsis], "must use `...` after a value in a tuple");
        },

      // Compact expressions.
      In(Expr) * T(Expr) << (Any[Expr] * End) >>
        [](Match& _) { return _(Expr); },
      T(Expr) << (T(Expr)[Expr] * End) >> [](Match& _) { return _(Expr); },
    };
  }

  auto on_lhs(auto pattern)
  {
    return (In(Assign) * (pattern * ++T(Expr))) / (In(TupleLHS) * pattern);
  }

  PassDef assignlhs()
  {
    return {
      // Turn a Tuple on the LHS of an assignment into a TupleLHS.
      on_lhs(T(Expr) << T(Tuple)[Lhs]) >>
        [](Match& _) { return Expr << (TupleLHS << *_[Lhs]); },

      on_lhs(T(Expr) << (T(TypeAssert) << (T(Tuple)[Lhs] * T(Type)[Type]))) >>
        [](Match& _) {
          return Expr << (TypeAssert << (TupleLHS << *_[Lhs]) << _(Type));
        },

      // Turn a Call on the LHS of an assignment into a CallLHS.
      on_lhs(T(Expr) << T(Call)[Lhs]) >>
        [](Match& _) { return Expr << (CallLHS << *_[Lhs]); },

      on_lhs(T(Expr) << (T(TypeAssert) << (T(Call)[Lhs] * T(Type)[Type]))) >>
        [](Match& _) {
          return Expr << (TypeAssert << (CallLHS << *_[Lhs]) << _(Type));
        },

      // Turn a RefVar on the LHS of an assignment into a RefVarLHS.
      on_lhs(T(Expr) << T(RefVar)[Lhs]) >>
        [](Match& _) { return Expr << (RefVarLHS << *_[Lhs]); },

      on_lhs(T(Expr) << (T(TypeAssert) << (T(RefVar)[Lhs] * T(Type)[Type]))) >>
        [](Match& _) {
          return Expr << (TypeAssert << (RefVarLHS << *_[Lhs]) << _(Type));
        },

      In(Expr) * T(Ref)[Ref] >>
        [](Match& _) {
          return err(_[Ref], "must use `ref` in front of a variable or call");
        },

      In(Expr) * T(Try)[Try] >>
        [](Match& _) {
          return err(_[Try], "must use `try` in front of a call or lambda");
        },

      T(Expr)[Expr] << (Any * Any * Any++) >>
        [](Match& _) {
          return err(_[Expr], "adjacency on this expression isn't meaningful");
        },

      In(TupleLHS) * T(TupleFlatten) >>
        [](Match& _) {
          return err(
            _[TupleFlatten],
            "can't flatten a tuple on the left-hand side of an assignment");
        },

      In(Expr) * T(Expr)[Expr] >>
        [](Match& _) {
          return err(
            _[Expr],
            "well-formedness allows this but it can't occur on written code");
        },
    };
  }

  inline const auto Cell = TypeClassName << Builtin << (Ident ^ cell)
                                         << TypeArgs;
  inline const auto CellCreate =
    (FunctionName << Cell << (Ident ^ create) << TypeArgs);
  inline const auto CallCellCreate = (Call << CellCreate << Args);
  inline const auto Load = (Selector << (Ident ^ load) << TypeArgs);

  PassDef localvar()
  {
    return {
      T(Var) << T(Ident)[Id] >>
        [](Match& _) {
          return Assign << (Expr << (Let << _(Id)))
                        << (Expr << clone(CallCellCreate));
        },

      T(RefVar)[RefVar] >>
        [](Match& _) {
          return Call << clone(Load)
                      << (Args << (Expr << (RefLet << *_[RefVar])));
        },

      T(RefVarLHS)[RefVarLHS] >>
        [](Match& _) { return RefLet << *_[RefVarLHS]; },
    };
  }

  inline const auto Store = (Selector << (Ident ^ store) << TypeArgs);

  PassDef assignment()
  {
    return {
      // Let binding.
      In(Assign) *
          (T(Expr)
           << ((T(Let) << T(Ident)[Id]) /
               (T(TypeAssert) << (T(Let) << T(Ident)[Id]) * T(Type)[Type]))) *
          T(Expr)[Rhs] * End >>
        [](Match& _) {
          return Expr
            << (Bind << (Ident ^ _(Id)) << typevar(_, Type) << _(Rhs));
        },

      // Destructuring assignment.
      In(Assign) *
          (T(Expr)
           << (T(TupleLHS)[Lhs] /
               (T(TypeAssert)
                << ((T(Expr) << T(TupleLHS)[Lhs]) * T(Type)[Type])))) *
          T(Expr)[Rhs] * End >>
        [](Match& _) {
          // let $rhs_id = Rhs
          auto rhs_id = _.fresh();
          auto rhs_e = Expr
            << (Bind << (Ident ^ rhs_id) << typevar(_) << _(Rhs));
          Node seq = ExprSeq;

          Node lhs_tuple = Tuple;
          Node rhs_tuple = Tuple;
          auto ty = _(Type);
          size_t index = 0;

          for (auto lhs_child : *_(Lhs))
          {
            auto lhs_e = lhs_child->front();

            if (lhs_e->type() == Let)
            {
              // lhs_child is already a Let.
              lhs_tuple << (Expr << (RefLet << clone(lhs_e / Ident)));
            }
            else
            {
              // let $lhs_id = lhs_child
              auto lhs_id = _.fresh();
              seq
                << (Expr
                    << (Bind << (Ident ^ lhs_id) << typevar(_) << lhs_child));
              lhs_child = Expr << (RefLet << (Ident ^ lhs_id));
              lhs_tuple << clone(lhs_child);
            }

            // $lhs_id = $rhs_id._index
            rhs_tuple
              << (Expr
                  << (Assign
                      << lhs_child
                      << (Expr
                          << (Call
                              << (Selector
                                  << (Ident ^
                                      Location("_" + std::to_string(index++)))
                                  << TypeArgs)
                              << (Args
                                  << (Expr
                                      << (RefLet << (Ident ^ rhs_id))))))));
          }

          // TypeAssert comes after the let bindings for the LHS.
          if (ty)
            seq << (Expr << (TypeAssert << lhs_tuple << ty));

          // The RHS tuple is the last expression in the sequence.
          return Expr << (seq << rhs_e << (Expr << rhs_tuple));
        },

      // Assignment to anything else.
      In(Assign) * T(Expr)[Lhs] * T(Expr)[Rhs] * End >>
        [](Match& _) { return Expr << call(clone(Store), _(Lhs), _(Rhs)); },

      // Compact assigns after they're reduced.
      T(Assign) << ((T(Expr) << Any[Lhs]) * End) >>
        [](Match& _) { return _(Lhs); },

      // An assign with an error can't be compacted, so it's an error.
      T(Assign)[Assign] << (T(Expr)++ * T(Error)) >>
        [](Match& _) { return err(_[Assign], "error inside an assignment"); },

      T(Expr)[Expr] << T(Let)[Let] >>
        [](Match& _) { return err(_[Expr], "must assign to a `let` binding"); },

      // Well-formedness allows this but it can't occur on written code.
      T(Expr)[Expr] << T(TupleLHS)[TupleLHS] >>
        [](Match& _) { return Expr << (Tuple << *_[TupleLHS]); },
    };
  }

  // This needs TypeArgs in order to be well-formed.
  inline const auto NonLocal = TypeClassName << Builtin << (Ident ^ nonlocal);

  Node nlrexpand(Match& _, Node call, bool unwrap)
  {
    // Check the call result to see if it's a non-local return. If it is,
    // optionally unwrap it and return. Otherwise, continue execution.
    auto id = _.fresh();
    auto nlr = Type
      << (clone(NonLocal)
          << (TypeArgs << (Type << (TypeVar ^ _.fresh(l_typevar)))));
    Node ret = Expr << (Cast << (Expr << (RefLet << (Ident ^ id))) << nlr);

    if (unwrap)
      ret = Expr << (Call << clone(Load) << (Args << ret));

    return ExprSeq
      << (Expr << (Bind << (Ident ^ id) << typevar(_) << (Expr << call)))
      << (Expr
          << (Conditional << (Expr
                              << (TypeTest << (Expr << (RefLet << (Ident ^ id)))
                                           << clone(nlr)))
                          << (Block << (Return << ret))
                          << (Block << (Expr << (RefLet << (Ident ^ id))))));
  }

  PassDef nlrcheck()
  {
    return {
      dir::topdown | dir::once,
      {
        T(NLRCheck) << ((T(Call) / T(CallLHS))[Call]) >>
          [](Match& _) {
            auto call = _(Call);
            return nlrexpand(
              _, call, call->parent({Lambda, Function})->type() == Function);
          },
      }};
  }

  PassDef lambda()
  {
    auto freevars = std::make_shared<std::vector<std::set<Location>>>();

    PassDef lambda = {
      dir::bottomup,
      {
        T(RefLet) << T(Ident)[Id] >> ([freevars](Match& _) -> Node {
          if (!freevars->empty())
          {
            // If we don't have a definition within the scope of the lambda,
            // then it's a free variable.
            auto id = _(Id);

            if (id->lookup(id->parent(Lambda)).empty())
              freevars->back().insert(id->location());
          }

          return NoChange;
        }),

        T(Lambda)
            << (T(TypeParams)[TypeParams] * T(Params)[Params] * T(Type)[Type] *
                T(TypePred)[TypePred] * T(Block)[Block]) >>
          [freevars](Match& _) {
            // Create the anonymous type.
            Node class_body = ClassBody;
            auto class_id = _.fresh(l_class);
            auto classdef = Class << (Ident ^ class_id) << TypeParams
                                  << inherit() << typepred() << class_body;

            // The create function will capture the free variables.
            Node create_params = Params;
            Node new_args = Args;
            auto create_func = Function
              << DontCare << (Ident ^ create) << TypeParams << create_params
              << typevar(_) << DontCare << typepred()
              << (Block << (Expr << (Call << New << new_args)));

            // The create call will instantiate the anonymous type.
            Node create_args = Args;
            auto create_call = Call
              << (FunctionName
                  << (TypeClassName << DontCare << (Ident ^ class_id)
                                    << TypeArgs)
                  << (Ident ^ create) << TypeArgs)
              << create_args;

            Node apply_body = Block;
            auto self_id = _.fresh(l_self);
            auto& fv = freevars->back();

            std::for_each(fv.begin(), fv.end(), [&](auto& fv_id) {
              // Add a field for the free variable to the anonymous type.
              auto type_id = _.fresh(l_typevar);
              class_body
                << (FieldLet << (Ident ^ fv_id) << (Type << (TypeVar ^ type_id))
                             << DontCare);

              // Add a parameter to the create function to capture the free
              // variable as a field.
              create_params
                << (Param << (Ident ^ fv_id) << (Type << (TypeVar ^ type_id))
                          << DontCare);
              new_args << (Expr << (RefLet << (Ident ^ fv_id)));

              // Add an argument to the create call. Don't load the free
              // variable, even if it was a `var`.
              create_args << (Expr << (RefLet << (Ident ^ fv_id)));

              // At the start of the lambda body, assign the field to a
              // local variable with the same name as the free variable.
              apply_body
                << (Expr
                    << (Bind
                        << (Ident ^ fv_id) << (Type << (TypeVar ^ type_id))
                        << (Expr
                            << (Call
                                << (Selector << (Ident ^ fv_id) << TypeArgs)
                                << (Args
                                    << (Expr
                                        << (RefLet << (Ident ^ self_id))))))));
            });

            // The apply function is the original lambda. Prepend a `self`-like
            // parameter with a fresh name to the lambda parameters.
            // TODO: capability for Self
            auto apply_func = Function
              << DontCare << (Ident ^ apply) << _(TypeParams)
              << (Params << (Param << (Ident ^ self_id) << (Type << Self)
                                   << DontCare)
                         << *_[Params])
              << _(Type) << DontCare << _(TypePred)
              << (apply_body << *_[Block]);

            // Add the create and apply functions to the anonymous type.
            class_body << create_func << apply_func;

            freevars->pop_back();

            return Seq << (Lift << ClassBody << classdef) << create_call;
          },
      }};

    lambda.pre(Lambda, [freevars](Node) {
      freevars->push_back({});
      return 0;
    });

    return lambda;
  }

  PassDef autofields()
  {
    return {
      dir::topdown | dir::once,
      {
        (T(FieldVar) / T(FieldLet))[Op] << (T(Ident)[Id] * T(Type)[Type]) >>
          ([](Match& _) -> Node {
            // If it's a FieldLet, generate only an RHS function. If it's a
            // FieldVar, generate an LHS function, which will autogenerate an
            // RHS function.
            auto field = _(Op);
            auto id = _(Id);
            auto self_id = _.fresh(l_self);
            Token is_ref = (field->type() == FieldVar) ? Ref : DontCare;
            auto expr = Expr
              << (FieldRef << (RefLet << (Ident ^ self_id)) << clone(id));

            if (is_ref == DontCare)
              expr = Expr << (Call << clone(Load) << (Args << expr));

            // TODO: capability for Self, return type is self.T
            auto f = Function << is_ref << clone(id) << TypeParams
                              << (Params
                                  << (Param << (Ident ^ self_id)
                                            << (Type << Self) << DontCare))
                              << clone(_(Type)) << DontCare << typepred()
                              << (Block << expr);

            return Seq << field << f;
          }),
      }};
  }

  PassDef autorhs()
  {
    return {
      dir::topdown | dir::once,
      {
        T(Function)[Function]
            << (T(Ref) * Name[Id] * T(TypeParams)[TypeParams] *
                T(Params)[Params] * T(Type)[Type] * T(DontCare) *
                T(TypePred)[TypePred] * T(Block)) >>
          ([](Match& _) -> Node {
            auto f = _(Function);
            auto id = _(Id);
            auto params = _(Params);
            auto parent = f->parent()->parent()->shared_from_this();
            Token ptype =
              (parent->type() == Class) ? TypeClassName : TypeTraitName;
            auto tn = parent / Ident;
            auto defs = parent->lookdown(id->location());
            auto found = false;

            // Check if there's an RHS function with the same name and arity.
            for (auto def : defs)
            {
              if (
                (def != f) && (def->type() == Function) &&
                ((def / Ref)->type() != Ref) &&
                ((def / Ident)->location() == id->location()) &&
                ((def / Params)->size() == params->size()))
              {
                found = true;
                break;
              }
            }

            if (found)
              return NoChange;

            // If not, create an RHS function with the same name and arity.
            Node args = Args;

            for (auto param : *params)
              args << (Expr << (RefLet << clone(param / Ident)));

            auto rhs_f = Function
              << DontCare << clone(id) << clone(_(TypeParams)) << clone(params)
              << clone(_(Type)) << DontCare << clone(_(TypePred))
              << (Block
                  << (Expr
                      << (Call << clone(Load)
                               << (Args
                                   << (Expr
                                       << (CallLHS << (FunctionName
                                                       << (ptype << DontCare
                                                                 << clone(tn)
                                                                 << TypeArgs)
                                                       << clone(id) << TypeArgs)
                                                   << args))))));

            return Seq << f << rhs_f;
          }),
      }};
  }

  PassDef autocreate()
  {
    return {
      dir::topdown | dir::once,
      {
        In(Class) * T(ClassBody)[ClassBody] >> ([](Match& _) -> Node {
          // If we already have a create function, do nothing.
          auto class_body = _(ClassBody);
          Node new_params = Params;
          Node new_args = Args;

          for (auto& node : *class_body)
          {
            if (node->type().in({FieldLet, FieldVar}))
            {
              auto id = node / Ident;
              auto ty = node / Type;
              auto def_arg = node / Default;

              // Add each field in order to the call to `new` and the create
              // function parameters.
              new_args << (Expr << (RefLet << clone(id)));
              new_params
                << ((Param ^ def_arg) << clone(id) << clone(ty) << def_arg);
            }
          }

          // Create the `new` function.
          // TODO: return Self & K?
          auto body = ClassBody
            << *_[ClassBody]
            << (Function << DontCare << (Ident ^ new_) << TypeParams
                         << new_params << typevar(_) << DontCare << typepred()
                         << (Block << (Expr << clone(CallUnitCreate))));

          if (class_body->parent()->lookdown(create).empty())
          {
            // Create the `create` function.
            body
              << (Function << DontCare << (Ident ^ create) << TypeParams
                           << clone(new_params) << typevar(_) << DontCare
                           << typepred()
                           << (Block << (Expr << (Call << New << new_args))));
          }

          return body;
        }),

        // Strip the default field values.
        T(FieldLet) << (T(Ident)[Id] * T(Type)[Type] * Any) >>
          [](Match& _) { return FieldLet << _(Id) << _(Type); },

        T(FieldVar) << (T(Ident)[Id] * T(Type)[Type] * Any) >>
          [](Match& _) { return FieldVar << _(Id) << _(Type); },
      }};
  }

  PassDef defaultargs()
  {
    return {
      dir::topdown | dir::once,
      {
        T(Function)[Function]
            << ((T(Ref) / T(DontCare))[Ref] * Name[Id] *
                T(TypeParams)[TypeParams] *
                (T(Params)
                 << ((T(Param) << (T(Ident) * T(Type) * T(DontCare)))++[Lhs] *
                     (T(Param) << (T(Ident) * T(Type) * T(Call)))++[Rhs] *
                     End)) *
                T(Type)[Type] * T(DontCare) * T(TypePred)[TypePred] *
                T(Block)[Block]) >>
          [](Match& _) {
            Node seq = Seq;
            auto ref = _(Ref);
            auto id = _(Id);
            auto tp = _(TypeParams);
            auto ty = _(Type);
            auto pred = _(TypePred);
            Node params = Params;
            Node call = (ref->type() == Ref) ? CallLHS : Call;

            auto parent = _(Function)->parent()->parent()->shared_from_this();
            auto tn = parent / Ident;
            Token ptype =
              (parent->type() == Class) ? TypeClassName : TypeTraitName;
            Node args = Args;
            auto fwd = Expr
              << (call << (FunctionName
                           << (ptype << DontCare << clone(tn) << TypeArgs)
                           << clone(id) << TypeArgs)
                       << args);

            auto lhs = _[Lhs];
            auto rhs = _[Rhs];

            // Start with parameters that have no default value.
            for (auto it = lhs.first; it != lhs.second; ++it)
            {
              auto param_id = *it / Ident;
              params << (Param << clone(param_id) << clone(*it / Type));
              args << (Expr << (RefLet << clone(param_id)));
            }

            for (auto it = rhs.first; it != rhs.second; ++it)
            {
              // At this point, the default argument is a create call on the
              // anonymous class derived from the lambda. Apply the created
              // lambda to get the default argument, checking for nonlocal.
              auto def_arg = Call << clone(Apply)
                                  << (Args << (Expr << (*it / Default)));
              def_arg = nlrexpand(_, def_arg, true);

              // Add the default argument to the forwarding call.
              args << (Expr << def_arg);

              // Add a new function that calls the arity+1 function.
              seq
                << (Function << clone(ref) << clone(id) << clone(tp)
                             << clone(params) << clone(ty) << DontCare
                             << clone(pred) << (Block << clone(fwd)));

              // Add a parameter.
              auto param_id = *it / Ident;
              params << (Param << clone(param_id) << clone(*it / Type));

              // Replace the last argument with a reference to the parameter.
              args->pop_back();
              args << (Expr << (RefLet << clone(param_id)));
            }

            // The original function, with no default arguments.
            return seq
              << (Function << ref << id << tp << params << ty << DontCare
                           << pred << _(Block));
          },

        T(Param) << (T(Ident)[Ident] * T(Type)[Type] * T(DontCare)) >>
          [](Match& _) { return Param << _(Ident) << _(Type); },

        T(Param)[Param] << (T(Ident) * T(Type) * T(Call)) >>
          [](Match& _) {
            return err(
              _[Param],
              "can't put a default value before a non-defaulted value");
          },
      }};
  }

  void extract_typeparams(Node scope, Node t, Node tp)
  {
    // This function extracts all typeparams from a type `t` that are defined
    // within `scope` and appends them to `tp` if they aren't already present.
    if (t->type().in(
          {Type,
           TypeArgs,
           TypeUnion,
           TypeIsect,
           TypeTuple,
           TypeList,
           TypeView}))
    {
      for (auto& tt : *t)
        extract_typeparams(scope, tt, tp);
    }
    else if (t->type().in({TypeClassName, TypeAliasName, TypeTraitName}))
    {
      extract_typeparams(scope, t / Lhs, tp);
      extract_typeparams(scope, t / TypeArgs, tp);
    }
    else if (t->type() == TypeParamName)
    {
      auto id = t / Ident;
      auto defs = id->lookup(scope);

      if ((defs.size() == 1) && (defs.front()->type() == TypeParam))
      {
        if (!std::any_of(tp->begin(), tp->end(), [&](auto& p) {
              return (p / Ident)->location() == id->location();
            }))
        {
          tp << clone(defs.front());
        }
      }

      extract_typeparams(scope, t / Lhs, tp);
      extract_typeparams(scope, t / TypeArgs, tp);
    }
  }

  Node typeparams_to_typeargs(Node node, Node typeargs = TypeArgs)
  {
    // This finds all typeparams in a Class or Function definition and builds
    // a TypeArgs that contains all of them, in order.
    if (!node->type().in({Class, Function}))
      return typeargs;

    for (auto typeparam : *(node / TypeParams))
    {
      typeargs
        << (Type
            << (TypeParamName << DontCare << clone(typeparam / Ident)
                              << TypeArgs));
    }

    return typeargs;
  }

  PassDef partialapp()
  {
    // This should happen after `lambda` (so that anonymous types get partial
    // application), after `autocreate` (so that constructors get partial
    // application), and after `defaultargs` (so that default arguments don't
    // get partial application).

    // This means that partial application can't be written in terms of lambdas,
    // but instead has to be anonymous classes. There's no need to check for
    // non-local returns.
    return {
      dir::bottomup | dir::once,
      {T(Function)[Function]
         << ((T(Ref) / T(DontCare))[Ref] * Name[Id] *
             T(TypeParams)[TypeParams] * T(Params)[Params] * T(Type) *
             T(DontCare) * T(TypePred)[TypePred]) >>
       [](Match& _) {
         // Create a FunctionName for a static call to the original function.
         auto f = _(Function);
         auto id = _(Id);
         auto parent = f->parent()->parent()->shared_from_this();

         auto func_name = FunctionName
           << (((parent->type() == Class) ? TypeClassName : TypeTraitName)
               << DontCare << clone(parent / Ident)
               << typeparams_to_typeargs(parent))
           << clone(id) << typeparams_to_typeargs(f);

         // Find the lowest arity that is not already defined. If an arity 5 and
         // an arity 3 function `f` are provided, an arity 4 partial application
         // will be generated that calls the arity 5 function, and arity 0-2
         // functions will be generated that call the arity 3 function.
         auto defs = parent->lookdown(id->location());
         auto params = _(Params);
         size_t start_arity = 0;
         auto end_arity = params->size();

         for (auto def : defs)
         {
           if ((def == f) || (def->type() != Function))
             continue;

           auto arity = (def / Params)->size();

           if (arity < end_arity)
             start_arity = std::max(start_arity, arity + 1);
         }

         // Create a unique anonymous class name for each arity.
         Nodes names;

         for (auto arity = start_arity; arity < end_arity; ++arity)
           names.push_back(Ident ^ _.fresh(l_class));

         Node ret = Seq;
         auto ref = _(Ref);
         Node call = (ref->type() == Ref) ? CallLHS : Call;

         for (auto arity = start_arity; arity < end_arity; ++arity)
         {
           // Create an anonymous class for each arity.
           auto name = names[arity - start_arity];
           Node class_tp = TypeParams;
           Node classbody = ClassBody;
           auto classdef = Class << clone(name) << class_tp << inherit()
                                 << typepred() << classbody;

           // The anonymous class has fields for each supplied argument and a
           // create function that captures the supplied arguments.
           Node create_params = Params;
           Node new_args = Args;
           classbody
             << (Function << DontCare << (Ident ^ create) << TypeParams
                          << create_params << typevar(_) << DontCare
                          << typepred()
                          << (Block << (Expr << (Call << New << new_args))));

           // Create a function that returns the anonymous class for each arity.
           Node func_tp = TypeParams;
           Node func_params = Params;
           Node func_args = Args;
           auto func =
             Function << clone(ref) << clone(id) << func_tp << func_params
                      << typevar(_) << DontCare << typepred()
                      << (Block
                          << (Expr
                              << (Call << (FunctionName
                                           << (TypeClassName << DontCare
                                                             << clone(name)
                                                             << TypeArgs)
                                           << (Ident ^ create) << TypeArgs)
                                       << func_args)));

           for (size_t i = 0; i < arity; ++i)
           {
             auto param = params->at(i);
             auto param_id = param / Ident;
             auto param_type = param / Type;

             extract_typeparams(f, param_type, class_tp);
             extract_typeparams(f, param_type, func_tp);

             classbody << (FieldLet << clone(param_id) << clone(param_type));
             create_params << clone(param);
             new_args << (Expr << (RefLet << clone(param_id)));
             func_params << clone(param);
             func_args << (Expr << (RefLet << clone(param_id)));
           }

           // The anonymous class has a function for each intermediate arity and
           // for the final arity.
           for (auto i = arity + 1; i <= end_arity; ++i)
           {
             // TODO: capability for Self, depends on captured param types
             auto self_id = Ident ^ _.fresh(l_self);
             Node apply_tp = TypeParams;
             Node apply_params = Params << (Param << self_id << (Type << Self));
             Node apply_pred;
             Node fwd_args = Args;

             for (size_t j = 0; j < arity; ++j)
             {
               // Include our captured arguments.
               fwd_args
                 << (Expr
                     << (Call
                         << (Selector << clone(params->at(j) / Ident)
                                      << TypeArgs)
                         << (Args << (Expr << (RefLet << clone(self_id))))));
             }

             for (auto j = arity; j < i; ++j)
             {
               // Add the additional arguments passed to this apply function.
               auto param = params->at(j);
               extract_typeparams(f, param / Type, apply_tp);
               apply_params << clone(param);
               fwd_args << (Expr << (RefLet << clone(param / Ident)));
             }

             Node fwd;

             if (i == end_arity)
             {
               // The final arity calls the original function. It has the type
               // predicate from the original function.
               apply_pred = clone(_(TypePred));
               fwd = clone(func_name);
             }
             else
             {
               // Intermediate arities call the next arity. No type predicate is
               // applied.
               apply_pred = typepred();
               fwd = FunctionName
                 << (TypeClassName
                     << DontCare << clone(names[i - start_arity])
                     << typeparams_to_typeargs(
                          apply_tp, typeparams_to_typeargs(class_tp)))
                 << (Ident ^ create) << TypeArgs;
             }

             classbody
               << (Function
                   << clone(ref) << (Ident ^ apply) << apply_tp << apply_params
                   << typevar(_) << DontCare << apply_pred
                   << (Block << (Expr << (clone(call) << fwd << fwd_args))));
           }

           ret << classdef << func;
         }

         return ret << f;
       }},
    };
  }

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

  inline const auto Liftable = T(Tuple) / T(Call) / T(CallLHS) /
    T(Conditional) / T(FieldRef) / T(TypeTest) / T(Cast) / T(Selector) /
    T(FunctionName) / Literal;

  PassDef anf()
  {
    return {
      // This liftable expr is already bound from `let x = e`.
      In(Bind) * (T(Expr) << Liftable[Lift]) >>
        [](Match& _) { return _(Lift); },

      // Lift `let x` bindings, leaving a RefLet behind.
      T(Expr) << (T(Bind)[Bind] << (T(Ident)[Id] * T(Type) * T(Expr))) >>
        [](Match& _) {
          return Seq << (Lift << Block << _(Bind))
                     << (RefLet << (Ident ^ _(Id)));
        },

      // Lift RefLet and Return.
      T(Expr) << (T(RefLet) / T(Return))[Op] >> [](Match& _) { return _(Op); },

      // Lift LLVM literals that are at the block level.
      In(Block) * (T(Expr) << T(LLVM)[LLVM]) >>
        [](Match& _) { return _(LLVM); },

      // Create a new binding for this liftable expr.
      T(Expr)
          << (Liftable[Lift] /
              (T(TypeAssert)
               << ((Liftable / T(RefLet))[Lift] * T(Type)[Type]))) >>
        [](Match& _) {
          auto id = _.fresh();
          return Seq << (Lift << Block
                              << (Bind << (Ident ^ id) << typevar(_, Type)
                                       << _(Lift)))
                     << (RefLet << (Ident ^ id));
        },

      // Compact an ExprSeq with only one element.
      T(ExprSeq) << (Any[Lhs] * End) >> [](Match& _) { return _(Lhs); },

      // Discard leading RefLets in ExprSeq.
      In(ExprSeq) * T(RefLet) * Any[Lhs] >> [](Match& _) { return _(Lhs); },
    };
  }

  PassDef defbeforeuse()
  {
    return {
      dir::topdown | dir::once,
      {
        T(RefLet) << T(Ident)[Id] >> ([](Match& _) -> Node {
          auto id = _(Id);
          auto defs = id->lookup();

          if (
            (defs.size() == 1) &&
            ((defs.front()->type() == Param) || defs.front()->precedes(id)))
            return NoChange;

          return err(_[Id], "use of uninitialized identifier");
        }),
      }};
  }

  PassDef drop()
  {
    struct track
    {
      Nodes blocks;
      Nodes stack;
      std::map<Location, bool> params;
      NodeMap<std::map<Location, bool>> lets;
      std::map<Location, std::vector<std::pair<Node, Node>>> refs;
      NodeMap<Node> parents;
      NodeMap<Nodes> children;
      NodeMap<Nodes> successors;
      bool llvm;

      track(bool llvm) : llvm(llvm) {}

      void gen(const Location& loc)
      {
        if (!llvm)
        {
          if (stack.empty())
            params[loc] = true;
          else
            lets[stack.back()][loc] = true;
        }
      }

      void ref(const Location& loc, Node node)
      {
        if (llvm)
          refs[loc].push_back({{}, node});
        else
          refs[loc].push_back({stack.back(), node});
      }

      bool is_successor(Node of, Node block)
      {
        // A successor block is a block that could execute after this one. This
        // is one of the following:
        // * A parent (any distance) block, or
        // * A child (any distance) block, or
        // * A child (any distance) block of a conditional in a parent block,
        // where the conditional follows this block.

        // Check if it's the same block or a child.
        if ((of == block) || is_child(of, block))
          return true;

        // Only check parents and successors if this isn't an early return.
        return (block->back()->type() != Return) &&
          (is_parent(of, block) || is_successor_or_child(of, block));
      }

      bool is_parent(Node of, Node block)
      {
        if (of->parent()->type() == Function)
          return false;

        auto& parent = parents.at(of);
        return (parent == block) || is_successor_or_child(parent, block) ||
          is_parent(parent, block);
      }

      bool is_child(Node of, Node block)
      {
        return std::any_of(
          children[of].begin(), children[of].end(), [&](auto& c) {
            return (c == block) || is_child(c, block);
          });
      }

      bool is_successor_or_child(Node of, Node block)
      {
        return std::any_of(
          successors[of].begin(), successors[of].end(), [&](auto& c) {
            return (c == block) || is_child(c, block);
          });
      }

      void pre_block(Node block)
      {
        if (llvm)
          return;

        if (stack.empty())
        {
          lets[block] = params;
        }
        else
        {
          auto parent = stack.back();

          for (auto& child : children[parent])
          {
            // The new child is a successor of the old children unless it's a
            // sibling block in a conditional.
            if (child->parent() != block->parent())
              successors[child].push_back(block);
          }

          children[parent].push_back(block);
          parents[block] = parent;
          lets[block] = lets[parent];
        }

        stack.push_back(block);
        blocks.push_back(block);
      }

      void post_block()
      {
        if (!llvm)
          stack.pop_back();
      }

      size_t post_function()
      {
        size_t changes = 0;

        if (llvm)
        {
          for (auto& [loc, list] : refs)
          {
            for (auto it = list.begin(); it != list.end(); ++it)
            {
              auto ref = it->second;
              auto parent = ref->parent();
              bool immediate = parent->type() == Block;

              if (immediate && (parent->back() != ref))
                parent->replace(ref);
              else
                parent->replace(ref, Move << (ref / Ident));

              changes++;
            }
          }

          return changes;
        }

        for (auto& [loc, list] : refs)
        {
          for (auto it = list.begin(); it != list.end(); ++it)
          {
            auto refblock = it->first;
            auto ref = it->second;
            auto id = ref / Ident;
            auto parent = ref->parent()->shared_from_this();
            bool immediate = parent->type() == Block;
            bool discharging = true;

            // We're the last use if there is no following use in this or any
            // successor block.
            for (auto next = it + 1; next != list.end(); ++next)
            {
              if (is_successor(refblock, next->first))
              {
                discharging = false;
                break;
              }
            }

            if (discharging && immediate && (parent->back() == ref))
              parent->replace(ref, Move << id);
            else if (discharging && immediate)
              parent->replace(ref, Drop << id);
            else if (discharging)
              parent->replace(ref, Move << id);
            else if (immediate)
              parent->replace(ref);
            else
              parent->replace(ref, Copy << id);

            // If this is a discharging use, mark the variable as discharged in
            // all predecessor and successor blocks.
            if (discharging)
            {
              bool forward = true;

              for (auto& block : blocks)
              {
                if (block == refblock)
                  forward = false;

                if (
                  forward ? is_successor(block, refblock) :
                            is_successor(refblock, block))
                {
                  lets[block][id->location()] = false;
                }
              }
            }

            changes++;
          }
        }

        for (auto& block : blocks)
        {
          auto& let = lets[block];

          for (auto& it : let)
          {
            if (it.second)
            {
              block->insert(block->begin(), Drop << (Ident ^ it.first));
              changes++;
            }
          }
        }

        return changes;
      }
    };

    auto drop_map = std::make_shared<std::vector<track>>();

    PassDef drop = {
      dir::topdown | dir::once,
      {
        (T(Param) / T(Bind)) << T(Ident)[Id] >> ([drop_map](Match& _) -> Node {
          drop_map->back().gen(_(Id)->location());
          return NoChange;
        }),

        T(RefLet)[RefLet] << T(Ident)[Id] >> ([drop_map](Match& _) -> Node {
          drop_map->back().ref(_(Id)->location(), _(RefLet));
          return NoChange;
        }),

        T(LLVM) >> ([drop_map](Match&) -> Node {
          drop_map->back().llvm = true;
          return NoChange;
        }),

        T(Call) << (T(FunctionName)[Op] * T(Args)[Args]) >>
          ([drop_map](Match& _) -> Node {
            if (is_llvm_call(_(Op), _(Args)->size()))
              drop_map->back().llvm = true;

            return NoChange;
          }),
      }};

    drop.pre(Block, [drop_map](Node node) {
      drop_map->back().pre_block(node);
      return 0;
    });

    drop.post(Block, [drop_map](Node) {
      drop_map->back().post_block();
      return 0;
    });

    drop.pre(Function, [drop_map](Node f) {
      auto llvm = (f / LLVMFuncType)->type() == LLVMFuncType;
      drop_map->push_back(track(llvm));
      return 0;
    });

    drop.post(Function, [drop_map](Node) {
      auto changes = drop_map->back().post_function();
      drop_map->pop_back();
      return changes;
    });

    return drop;
  }

  PassDef namearity()
  {
    return {
      dir::bottomup | dir::once,
      {
        T(Function)
            << ((T(Ref) / T(DontCare))[Ref] * Name[Id] *
                T(TypeParams)[TypeParams] * T(Params)[Params] * T(Type)[Type] *
                (T(LLVMFuncType) / T(DontCare))[LLVMFuncType] *
                T(TypePred)[TypePred] * T(Block)[Block]) >>
          [](Match& _) {
            auto id = _(Id);
            auto arity = _(Params)->size();
            auto name =
              std::string(id->location().view()) + "." + std::to_string(arity);

            if (_(Ref)->type() == Ref)
              name += ".ref";

            return Function << (Ident ^ name) << _(TypeParams) << _(Params)
                            << _(Type) << _(LLVMFuncType) << _(TypePred)
                            << _(Block);
          },

        (T(Call) / T(CallLHS))[Call]
            << ((T(FunctionName)
                 << ((TypeName / T(DontCare))[Lhs] * Name[Id] *
                     T(TypeArgs)[TypeArgs])) *
                T(Args)[Args]) >>
          [](Match& _) {
            auto arity = _(Args)->size();
            auto name = std::string(_(Id)->location().view()) + "." +
              std::to_string(arity);

            if (_(Call)->type() == CallLHS)
              name += ".ref";

            return Call << (FunctionName << _(Lhs) << (Ident ^ name)
                                         << _(TypeArgs))
                        << _(Args);
          },

        (T(Call) / T(CallLHS))[Call]
            << ((T(Selector) << (Name[Id] * T(TypeArgs)[TypeArgs])) *
                T(Args)[Args]) >>
          [](Match& _) {
            auto arity = _(Args)->size();
            auto name = std::string(_(Id)->location().view()) + "." +
              std::to_string(arity);

            if (_(Call)->type() == CallLHS)
              name += ".ref";

            return Call << (Selector << (Ident ^ name) << _(TypeArgs))
                        << _(Args);
          },

        T(Call) << (T(New) * T(Args)[Args]) >>
          [](Match& _) {
            auto arity = _(Args)->size();
            auto name = std::string("new.") + std::to_string(arity);
            return Call << (FunctionName << DontCare << (Ident ^ name)
                                         << TypeArgs)
                        << _(Args);
          },

        T(CallLHS)[Call] << T(New) >>
          [](Match& _) { return err(_[Call], "can't assign to new"); },
      }};
  }

  PassDef validtypeargs()
  {
    return {
      dir::bottomup | dir::once,
      {
        TypeName[Op] << ((TypeName / T(DontCare)) * T(Ident) * T(TypeArgs)) >>
          ([](Match& _) -> Node {
            if (!valid_typeargs(_(Op)))
              return err(_[Op], "invalid type arguments");

            return NoChange;
          }),
      }};
  }

  Driver& driver()
  {
    static Driver d(
      "Verona",
      parser(),
      wfParser,
      {
        {"modules", modules(), wfPassModules},
        {"structure", structure(), wfPassStructure},
        {"memberconflict", memberconflict(), wfPassStructure},
        {"typenames", typenames(), wfPassTypeNames},
        {"typeview", typeview(), wfPassTypeView},
        {"typefunc", typefunc(), wfPassTypeFunc},
        {"typealg", typealg(), wfPassTypeAlg},
        {"typeflat", typeflat(), wfPassTypeFlat},
        {"typevalid", typevalid(), wfPassTypeFlat},
        {"conditionals", conditionals(), wfPassConditionals},
        {"reference", reference(), wfPassReference},
        {"reverseapp", reverseapp(), wfPassReverseApp},
        {"application", application(), wfPassApplication},
        {"assignlhs", assignlhs(), wfPassAssignLHS},
        {"localvar", localvar(), wfPassLocalVar},
        {"assignment", assignment(), wfPassAssignment},
        {"nlrcheck", nlrcheck(), wfPassNLRCheck},
        {"lambda", lambda(), wfPassLambda},
        {"autofields", autofields(), wfPassAutoFields},
        {"autorhs", autorhs(), wfPassAutoFields},
        {"autocreate", autocreate(), wfPassAutoCreate},
        {"defaultargs", defaultargs(), wfPassDefaultArgs},
        {"partialapp", partialapp(), wfPassDefaultArgs},
        {"traitisect", traitisect(), wfPassDefaultArgs},
        {"anf", anf(), wfPassANF},
        {"defbeforeuse", defbeforeuse(), wfPassANF},
        {"drop", drop(), wfPassDrop},
        {"namearity", namearity(), wfPassNameArity},
        {"validtypeargs", validtypeargs(), wfPassNameArity},
      });

    return d;
  }
}

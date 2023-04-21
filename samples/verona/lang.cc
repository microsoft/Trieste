// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "lang.h"

#include "lookup.h"
#include "subtype.h"
#include "wf.h"

namespace verona
{
  auto err(NodeRange& r, const std::string& msg)
  {
    return Error << (ErrorMsg ^ msg) << (ErrorAst << r);
  }

  auto err(Node node, const std::string& msg)
  {
    return Error << (ErrorMsg ^ msg) << ((ErrorAst ^ node) << node);
  }

  bool lookup(const NodeRange& n, std::initializer_list<Token> t)
  {
    return lookup_name(*n.first).one(t);
  }

  PassDef modules()
  {
    return {
      // Files at the top level and directories are modules.
      ((In(Top) * T(File)[Class]) / T(Directory)[Class]) >>
        [](Match& _) {
          return Group << (Class ^ _(Class)) << (Ident ^ _(Class)->location())
                       << (Brace << *_[Class]);
        },

      // Files in a directory aren't semantically meaningful.
      In(Brace) * T(File)[File] >> [](Match& _) { return Seq << *_[File]; },

      // Type assertion. Treat an empty assertion as DontCare. Accept a brace if
      // it comes immediately after the colon or after a symbol or dot.
      In(Group) * T(Colon) *
          (~T(Brace) *
           (((T(Symbol) / T(Dot)) * T(Brace)) /
            (!(T(Brace) / T(TripleColon))))++)[Type] >>
        [](Match& _) { return Type << (_[Type] / DontCare); },

      In(Type) * T(Colon)[Colon] >>
        [](Match& _) {
          return err(_[Colon], "can't put a type assertion inside a type");
        },

      In(Group) * T(TripleColon) * ~T(LLVM)[Lhs] *
          (T(Paren)
           << ((T(List) << (T(Group) << (T(Ident) / T(LLVM)))++[Args]) /
               ~(T(Group) << (T(Ident) / T(LLVM)))[Args])) *
          T(Symbol, "->") * (T(Ident) / T(LLVM))[Return] * ~T(LLVM)[Rhs] >>
        [](Match& _) {
          return LLVMFuncType << (_(Lhs) / DontCare) << (_(Rhs) / DontCare)
                              << (LLVMList << *_[Args]) << _(Return);
        },

      T(TripleColon)[TripleColon] >>
        [](Match& _) { return err(_[TripleColon], "malformed LLVM type"); },
    };
  }

  inline const auto TypeStruct = In(Type) / In(TypeList) / In(TypeTuple) /
    In(TypeView) / In(TypeFunc) / In(TypeUnion) / In(TypeIsect) /
    In(TypeSubtype);
  inline const auto Name = T(Ident) / T(Symbol);
  inline const auto Literal = T(String) / T(Escaped) / T(Char) / T(Bool) /
    T(Hex) / T(Bin) / T(Int) / T(Float) / T(HexFloat) / T(LLVM);

  auto typevar(auto& _, const Token& t = Invalid)
  {
    auto n = _(t);
    return n ? n : Type << (TypeVar ^ _.fresh());
  }

  PassDef structure()
  {
    return {
      // Let Field:
      // (equals (group let ident type) group)
      In(ClassBody) *
          (T(Equals)
           << ((T(Group) << (T(Let) * T(Ident)[Id] * ~T(Type)[Type] * End)) *
               T(Group)++[Rhs])) >>
        [](Match& _) {
          return FieldLet << _(Id) << typevar(_, Type)
                          << (Lambda
                              << TypeParams << Params
                              << (Block << (Expr << (Default << _[Rhs]))));
        },

      // (group let ident type)
      In(ClassBody) *
          (T(Group) << (T(Let) * T(Ident)[Id] * ~T(Type)[Type] * End)) >>
        [](Match& _) {
          return FieldLet << _(Id) << typevar(_, Type) << DontCare;
        },

      // Var Field:
      // (equals (group var ident type) group)
      In(ClassBody) *
          (T(Equals)
           << ((T(Group) << (T(Var) * T(Ident)[Id] * ~T(Type)[Type] * End)) *
               T(Group)++[Rhs])) >>
        [](Match& _) {
          return FieldVar << _(Id) << typevar(_, Type)
                          << (Lambda
                              << TypeParams << Params
                              << (Block << (Expr << (Default << _[Rhs]))));
        },

      // (group var ident type)
      In(ClassBody) *
          (T(Group) << (T(Var) * T(Ident)[Id] * ~T(Type)[Type] * End)) >>
        [](Match& _) {
          return FieldVar << _(Id) << typevar(_, Type) << DontCare;
        },

      // Function: (equals (group name square parens type) group)
      In(ClassBody) *
          (T(Equals)
           << ((T(Group)
                << (~T(Ref)[Ref] * ~Name[Id] * ~T(Square)[TypeParams] *
                    T(Paren)[Params] * ~T(Type)[Type] *
                    ~T(LLVMFuncType)[LLVMFuncType])) *
               T(Group)++[Rhs])) >>
        [](Match& _) {
          _.def(Id, Ident ^ apply);
          return Function << (_(Ref) / DontCare) << _(Id)
                          << (TypeParams << *_[TypeParams])
                          << (Params << *_[Params]) << typevar(_, Type)
                          << (_(LLVMFuncType) / DontCare)
                          << (Block << (Expr << (Default << _[Rhs])));
        },

      // Function: (group name square parens type brace)
      In(ClassBody) * T(Group)
          << (~T(Ref)[Ref] * ~Name[Id] * ~T(Square)[TypeParams] *
              T(Paren)[Params] * ~T(Type)[Type] *
              ~T(LLVMFuncType)[LLVMFuncType] * ~T(Brace)[Block] *
              (Any++)[Rhs]) >>
        [](Match& _) {
          _.def(Id, Ident ^ apply);
          return Seq << (Function << (_(Ref) / DontCare) << _(Id)
                                  << (TypeParams << *_[TypeParams])
                                  << (Params << *_[Params]) << typevar(_, Type)
                                  << (_(LLVMFuncType) / DontCare)
                                  << (Block << *_[Block]))
                     << (Group << _[Rhs]);
        },

      // TypeParams.
      T(TypeParams) << T(List)[TypeParams] >>
        [](Match& _) { return TypeParams << *_[TypeParams]; },

      // TypeParam: (group ident type)
      In(TypeParams) * T(Group) << (T(Ident)[Id] * ~T(Type)[Type] * End) >>
        [](Match& _) { return TypeParam << _(Id) << typevar(_, Type) << Type; },

      // TypeParam: (equals (group ident type) group)
      In(TypeParams) * T(Equals)
          << ((T(Group) << (T(Ident)[Id] * ~T(Type)[Type] * End)) *
              T(Group)++[Rhs]) >>
        [](Match& _) {
          return TypeParam << _(Id) << typevar(_, Type)
                           << (Type << (Default << _[Rhs]));
        },

      In(TypeParams) * (!T(TypeParam))[TypeParam] >>
        [](Match& _) { return err(_[TypeParam], "expected a type parameter"); },

      // Params.
      T(Params) << T(List)[Params] >>
        [](Match& _) { return Params << *_[Params]; },

      // Param: (group ident type)
      In(Params) * T(Group)
          << ((T(Ident) / T(DontCare))[Id] * ~T(Type)[Type] * End) >>
        [](Match& _) {
          auto id = (_(Id)->type() == DontCare) ? (Ident ^ _.fresh()) : _(Id);
          return Param << id << typevar(_, Type) << DontCare;
        },

      // Param: (equals (group ident type) group)
      In(Params) * T(Equals)
          << ((T(Group)
               << ((T(Ident) / T(DontCare))[Id] * ~T(Type)[Type] * End)) *
              T(Group)++[Expr]) >>
        [](Match& _) {
          auto id = (_(Id)->type() == DontCare) ? (Ident ^ _.fresh()) : _(Id);
          return Param << id << typevar(_, Type)
                       << (Lambda << TypeParams << Params
                                  << (Block << (Expr << (Default << _[Expr]))));
        },

      In(Params) * (!T(Param))[Param] >>
        [](Match& _) { return err(_[Param], "expected a parameter"); },

      // Use.
      (In(ClassBody) / In(Block)) * T(Group) << T(Use)[Use] * (Any++)[Type] >>
        [](Match& _) {
          return (Use ^ _(Use)) << (Type << (_[Type] / DontCare));
        },

      T(Use)[Use] << End >>
        [](Match& _) { return err(_[Use], "can't put a `use` here"); },

      // TypeAlias: (group typealias ident typeparams type)
      (In(ClassBody) / In(Block)) * T(Group)
          << (T(TypeAlias) * T(Ident)[Id] * ~T(Square)[TypeParams] *
              ~T(Type)[Type] * End) >>
        [](Match& _) {
          return TypeAlias << _(Id) << (TypeParams << *_[TypeParams])
                           << typevar(_, Type) << Type;
        },

      // TypeAlias: (equals (group typealias typeparams type) group)
      (In(ClassBody) / In(Block)) * T(Equals)
          << ((T(Group)
               << (T(TypeAlias) * T(Ident)[Id] * ~T(Square)[TypeParams] *
                   ~T(Type)[Type] * End)) *
              T(Group)++[Rhs]) >>
        [](Match& _) {
          return TypeAlias << _(Id) << (TypeParams << *_[TypeParams])
                           << typevar(_, Type) << (Type << (Default << _[Rhs]));
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
      (In(Top) / In(ClassBody) / In(Block)) * T(Group)
          << (T(Class) * T(Ident)[Id] * ~T(Square)[TypeParams] *
              ~T(Type)[Type] * T(Brace)[ClassBody] * (Any++)[Rhs]) >>
        [](Match& _) {
          return Seq << (Class << _(Id) << (TypeParams << *_[TypeParams])
                               << (_(Type) / Type)
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
        [](Match& _) { return TypeTuple << *_[TypeTuple]; },

      // Lift anonymous structural types.
      TypeStruct * T(Brace)[ClassBody] >>
        [](Match& _) {
          auto id = _(ClassBody)->parent(ClassBody)->fresh();
          return Seq << (Lift << ClassBody
                              << (TypeTrait << (Ident ^ id)
                                            << (ClassBody << *_[ClassBody])))
                     << (Ident ^ id);
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
          auto class_id = _.fresh();
          return Seq << (Lift
                         << Block
                         << (Class << (Ident ^ class_id) << TypeParams << Type
                                   << (ClassBody << *_[ClassBody])))
                     << (Expr << (Ident ^ class_id) << DoubleColon
                              << (Ident ^ create) << Unit);
        },

      // Lambda: (group typeparams) (list params...) => Rhs
      In(Expr) * T(Brace)
          << (((T(Group) << T(Square)[TypeParams]) * T(List)[Params]) *
              (T(Group) << T(Arrow)) * (Any++)[Rhs]) >>
        [](Match& _) {
          return Lambda << (TypeParams << *_[TypeParams])
                        << (Params << *_[Params]) << (Block << _[Rhs]);
        },

      // Lambda: (group typeparams) (group param) => Rhs
      In(Expr) * T(Brace)
          << (((T(Group) << T(Square)[TypeParams]) * T(Group)[Param]) *
              (T(Group) << T(Arrow)) * (Any++)[Rhs]) >>
        [](Match& _) {
          return Lambda << (TypeParams << *_[TypeParams])
                        << (Params << _[Param]) << (Block << _[Rhs]);
        },

      // Lambda: (list (group typeparams? param) params...) => Rhs
      In(Expr) * T(Brace)
          << ((T(List)
               << ((T(Group) << (~T(Square)[TypeParams] * (Any++)[Param])) *
                   (Any++)[Params]))) *
            (T(Group) << T(Arrow)) * (Any++)[Rhs] >>
        [](Match& _) {
          return Lambda << (TypeParams << *_[TypeParams])
                        << (Params << (Group << _[Param]) << _[Params])
                        << (Block << _[Rhs]);
        },

      // Lambda: (group typeparams? param) => Rhs
      In(Expr) * T(Brace)
          << ((T(Group) << (~T(Square)[TypeParams] * (Any++)[Param])) *
              (T(Group) << T(Arrow)) * (Any++)[Rhs]) >>
        [](Match& _) {
          return Lambda << (TypeParams << *_[TypeParams])
                        << (Params << (Group << _[Param]) << _[Params])
                        << (Block << _[Rhs]);
        },

      // Zero argument lambda.
      In(Expr) * T(Brace) << (!(T(Group) << T(Arrow)))++[Lambda] >>
        [](Match& _) {
          return Lambda << TypeParams << Params << (Block << _[Lambda]);
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
          (T(Lin) / T(In_) / T(Out) / T(Const) / T(Arrow) /
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
      {(T(FieldLet) / T(FieldVar))[Op] << (T(Ident)[Id]) >>
         ([](Match& _) -> Node {
           auto field = _(Op);
           auto defs = field->scope()->lookdown(_(Id)->location());

           for (auto& def : defs)
           {
             if (def == field)
               continue;

             if (def->type().in({FieldLet, FieldVar}))
               return err(field, "duplicate field name");
           }

           return NoChange;
         }),

       (T(Class) / T(TypeAlias) / T(TypeTrait))[Class] << (T(Ident)[Id]) >>
         ([](Match& _) -> Node {
           auto cls = _(Class);
           auto defs = cls->scope()->lookdown(_(Id)->location());

           for (auto& def : defs)
           {
             if (def == cls)
               continue;

             if (def->type().in({Class, TypeAlias, TypeTrait}))
               return err(cls, "duplicate type name");
             else if (def->type() == Function)
               return err(cls, "this type has the same name as a function");
           }

           return NoChange;
         }),

       T(Function)[Function]
           << ((T(Ref) / T(DontCare))[Ref] * Name[Id] * T(TypeParams) *
               T(Params)[Params]) >>
         ([](Match& _) -> Node {
           auto func = _(Function);
           auto ref = _(Ref);
           auto defs = func->scope()->lookdown(_(Id)->location());

           for (auto& def : defs)
           {
             if (def == func)
               continue;

             if (def->type().in({Class, TypeAlias, TypeTrait}))
               return err(func, "this function has the same name as a type");
             else if (
               (def->type() == Function) &&
               (def->at(wf / Function / Ref)->type() == ref->type()) &&
               (def->at(wf / Function / Params)->size() == _(Params)->size()))
             {
               return err(
                 func,
                 "this function has the same name, arity, and handedness as "
                 "another function");
             }
           }

           return NoChange;
         })}};
  }

  inline const auto TypeName =
    T(TypeClassName) / T(TypeAliasName) / T(TypeParamName) / T(TypeTraitName);

  inline const auto TypeElem = T(Type) / TypeName / T(TypeTuple) / T(Lin) /
    T(In_) / T(Out) / T(Const) / T(TypeList) / T(TypeView) / T(TypeFunc) /
    T(TypeIsect) / T(TypeUnion) / T(TypeVar) / T(TypeUnit) / T(Package) /
    T(TypeSubtype) / T(TypeTrue) / T(TypeFalse);

  Node
  makename(const Lookups& defs, Node lhs, Node id, Node ta, bool func = false)
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
      {
        err << clone(def.def->at(
          wf / Class / Ident,
          wf / TypeAlias / Ident,
          wf / TypeParam / Ident,
          wf / TypeTrait / Ident));
      }

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

    return Error << (ErrorMsg ^ "can't resolve type name")
                 << ((ErrorAst ^ id) << lhs << id << ta);
  }

  PassDef typenames()
  {
    return {
      TypeStruct * T(DontCare)[DontCare] >>
        [](Match& _) { return TypeVar ^ _.fresh(); },

      // Names on their own must be types.
      TypeStruct * T(Ident)[Id] * ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          auto defs = lookup_name(_(Id), _(TypeArgs));
          return makename(defs, TypeUnit, _(Id), (_(TypeArgs) / TypeArgs));
        },

      // Scoping binds most tightly.
      TypeStruct * TypeName[Lhs] * T(DoubleColon) * T(Ident)[Id] *
          ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          auto defs = lookup_scopedname_name(_(Lhs), _(Id), _(TypeArgs));
          return makename(defs, _(Lhs), _(Id), (_(TypeArgs) / TypeArgs));
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
      // Function types bind more tightly than throw types. This is the only
      // right-associative operator.
      TypeStruct * TypeElem[Lhs] * T(Symbol, "->") * TypeElem[Rhs] *
          --T(Symbol, "->") >>
        [](Match& _) {
          return TypeFunc << (Type << _[Lhs]) << (Type << _[Rhs]);
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

  PassDef typeflat()
  {
    return {
      // Flatten algebraic types.
      In(TypeUnion) * T(TypeUnion)[Lhs] >>
        [](Match& _) { return Seq << *_[Lhs]; },
      In(TypeIsect) * T(TypeIsect)[Lhs] >>
        [](Match& _) { return Seq << *_[Lhs]; },

      // T1.(T2.T3) = (T1.T2).T3
      T(TypeView)
          << (TypeElem[Op] *
              (T(TypeView) << (TypeElem[Lhs] * TypeElem[Rhs]))) >>
        [](Match& _) {
          return TypeView << (TypeView << _(Op) << _(Lhs)) << _(Rhs);
        },

      // Tuples of arity 1 are scalar types, tuples of arity 0 are the unit
      // type.
      T(TypeTuple) << (TypeElem[Op] * End) >> [](Match& _) { return _(Op); },
      T(TypeTuple) << End >> ([](Match&) -> Node { return TypeUnit; }),

      // Flatten Type nodes. The top level Type node won't go away.
      TypeStruct * T(Type) << (TypeElem[Op] * End) >>
        [](Match& _) { return _(Op); },

      T(Type)[Type] << End >> [](Match&) { return Type << TypeUnit; },

      T(Type)[Type] << (Any * Any) >>
        [](Match& _) {
          return err(_[Type], "can't use adjacency to specify a type");
        },
    };
  }

  PassDef typerec()
  {
    return {
      dir::once | dir::topdown,
      {
        T(TypeAlias)[TypeAlias] >> ([](Match& _) -> Node {
          if (lookup_recursive(_(TypeAlias)))
            return err(_[TypeAlias], "recursive type alias");

          return NoChange;
        }),

        T(TypeParam)[TypeParam] >> ([](Match& _) -> Node {
          if (lookup_recursive(_(TypeParam)))
            return err(_[TypeParam], "recursive type parameter");

          return NoChange;
        }),
      }};
  }

  auto make_conditional(Match& _)
  {
    // Pack all of the branches into a single conditional and unpack them
    // in the follow-on rules.
    auto lambda = _(Lhs);
    auto params = lambda->at(wf / Lambda / Params);
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
        type = clone(params->front()->at(wf / Param / Type));
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
          typetuple << clone(param->at(wf / Param / Type)->front());
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
          return Seq << Dot << (Selector << _[Id] << (_(TypeArgs) / TypeArgs));
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
          return makename(defs, TypeUnit, _(Id), (_(TypeArgs) / TypeArgs));
        },

      // Unscoped reference that isn't a local or a type. Treat it as a
      // selector, even if it resolves to a Function.
      In(Expr) * Name[Id] * ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) { return Selector << _(Id) << (_(TypeArgs) / TypeArgs); },

      // Scoped lookup.
      In(Expr) *
          (TypeName[Lhs] * T(DoubleColon) * Name[Id] *
           ~T(TypeArgs)[TypeArgs])[Type] >>
        [](Match& _) {
          auto defs = lookup_scopedname_name(_(Lhs), _(Id), _(TypeArgs));
          return makename(defs, _(Lhs), _(Id), (_(TypeArgs) / TypeArgs), true);
        },

      In(Expr) * T(DoubleColon) >>
        [](Match& _) { return err(_[DoubleColon], "expected a scoped name"); },

      // Create sugar, with no arguments.
      In(Expr) * TypeName[Lhs] * ~T(TypeArgs)[TypeArgs] >>
        [](Match& _) {
          return FunctionName << _(Lhs) << (Ident ^ create)
                              << (_(TypeArgs) / TypeArgs);
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
          (def.def->at(wf / Function / Params)->size() == arity) &&
          (def.def->at(wf / Function / LLVMFuncType)->type() == LLVMFuncType))
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

      // Use DontCare for partial application of arbitrary arguments.
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
          auto lambda = Lambda << TypeParams << params
                               << (Block << (Expr << (Call << _(Op) << args)));

          for (auto& arg : *_(Args))
          {
            auto expr = arg->front();

            if (expr->type() == DontCare)
            {
              auto id = _.fresh();
              params << (Param << (Ident ^ id) << typevar(_) << DontCare);
              args << (Expr << (RefLet << (Ident ^ id)));
            }
            else if (expr->type() == TypeAssert)
            {
              auto id = _.fresh();
              params
                << (Param << (Ident ^ id) << expr->at(wf / TypeAssert / Type)
                          << DontCare);
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

  inline const auto Std = TypeClassName << TypeUnit << (Ident ^ standard)
                                        << TypeArgs;
  inline const auto Cell = TypeClassName << Std << (Ident ^ cell) << TypeArgs;
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
              lhs_tuple
                << (Expr << (RefLet << clone(lhs_e->at(wf / Let / Ident))));
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
  inline const auto NonLocal = TypeClassName << Std << (Ident ^ nonlocal);

  Node nlrexpand(Match& _, Node call, bool unwrap)
  {
    // Check the call result to see if it's a non-local return. If it is,
    // optionally unwrap it and return. Otherwise, continue execution.
    auto id = _.fresh();
    auto nlr = Type
      << (clone(NonLocal) << (TypeArgs << (Type << (TypeVar ^ _.fresh()))));
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
            << (T(TypeParams)[TypeParams] * T(Params)[Params] *
                T(Block)[Block]) >>
          [freevars](Match& _) {
            // Create the anonymous type.
            Node class_body = ClassBody;
            auto class_id = _.fresh();
            auto classdef = Class << (Ident ^ class_id) << TypeParams
                                  << (Type << TypeUnit) << class_body;

            // The create function will capture the free variables.
            Node create_params = Params;
            Node new_args = Args;
            auto create_func = Function
              << DontCare << (Ident ^ create) << TypeParams << create_params
              << (Type << (TypeVar ^ _.fresh())) << DontCare
              << (Block << (Expr << (Call << New << new_args)));

            // The create call will instantiate the anonymous type.
            Node create_args = Args;
            auto create_call = Call
              << (FunctionName
                  << (TypeClassName << TypeUnit << (Ident ^ class_id)
                                    << TypeArgs)
                  << (Ident ^ create) << TypeArgs)
              << create_args;

            Node apply_body = Block;
            auto self_id = _.fresh();
            auto& fv = freevars->back();

            std::for_each(fv.begin(), fv.end(), [&](auto& fv_id) {
              // Add a field for the free variable to the anonymous type.
              auto type_id = _.fresh();
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
            auto apply_func = Function
              << DontCare << (Ident ^ apply) << _(TypeParams)
              << (Params << (Param << (Ident ^ self_id)
                                   << (Type << (TypeVar ^ _.fresh()))
                                   << DontCare)
                         << *_[Params])
              << (Type << (TypeVar ^ _.fresh())) << DontCare
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
            auto self_id = _.fresh();
            Token is_ref = (field->type() == FieldVar) ? Ref : DontCare;
            auto expr = Expr
              << (FieldRef << (RefLet << (Ident ^ self_id)) << clone(id));

            if (is_ref == DontCare)
              expr = Expr << (Call << clone(Load) << (Args << expr));

            auto f = Function << is_ref << clone(id) << TypeParams
                              << (Params
                                  << (Param << (Ident ^ self_id) << typevar(_)
                                            << DontCare))
                              << clone(_(Type)) << DontCare << (Block << expr);

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
                T(Params)[Params] * T(Type)[Type] * T(DontCare) * T(Block)) >>
          ([](Match& _) -> Node {
            auto f = _(Function);
            auto id = _(Id);
            auto params = _(Params);
            auto parent = f->parent()->parent();
            Token ptype =
              (parent->type() == Class) ? TypeClassName : TypeTraitName;
            auto tn = parent->at(wf / Class / Ident, wf / TypeTrait / Ident);
            auto defs = parent->lookdown(id->location());
            auto found = false;

            // Check if there's an RHS function with the same name and arity.
            for (auto def : defs)
            {
              if (
                (def != f) && (def->type() == Function) &&
                (def->at(wf / Function / Ref)->type() != Ref) &&
                (def->at(wf / Function / Ident)->location() ==
                 id->location()) &&
                (def->at(wf / Function / Params)->size() == params->size()))
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
              args
                << (Expr << (RefLet << clone(param->at(wf / Param / Ident))));

            auto rhs_f =
              Function << DontCare << clone(id) << clone(_(TypeParams))
                       << clone(params) << clone(_(Type)) << DontCare
                       << (Block
                           << (Expr
                               << (Call
                                   << clone(Load)
                                   << (Args
                                       << (Expr
                                           << (CallLHS
                                               << (FunctionName
                                                   << (ptype << TypeUnit
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

          if (!class_body->parent()->lookdown(create).empty())
            return NoChange;

          // Create the create function.
          Node create_params = Params;
          Node new_args = Args;
          auto create_func = Function
            << DontCare << (Ident ^ create) << TypeParams << create_params
            << typevar(_) << DontCare
            << (Block << (Expr << (Call << New << new_args)));

          Nodes no_def;
          Nodes def;

          for (auto& node : *class_body)
          {
            if (node->type().in({FieldLet, FieldVar}))
            {
              auto id = node->at(wf / FieldLet / Ident, wf / FieldVar / Ident);
              auto ty = node->at(wf / FieldLet / Type, wf / FieldVar / Type);
              auto def_arg =
                node->at(wf / FieldLet / Default, wf / FieldVar / Default);

              // Add each field in order to the call to `new`.
              new_args << (Expr << (RefLet << clone(id)));

              // Order the parameters to the create function.
              auto param = Param << clone(id) << clone(ty) << def_arg;

              if (def_arg->type() == DontCare)
                no_def.push_back(param);
              else
                def.push_back(param);
            }
          }

          // Add the parameters to the create function, sorting parameters
          // without a default value first.
          create_params << no_def << def;
          return ClassBody << *_[ClassBody] << create_func;
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
                T(Type)[Type] * T(DontCare) * T(Block)[Block]) >>
          [](Match& _) {
            Node seq = Seq;
            auto ref = _(Ref);
            auto id = _(Id);
            auto tp = _(TypeParams);
            auto ty = _(Type);
            Node params = Params;
            Node call = (ref->type() == Ref) ? CallLHS : Call;

            auto parent = _(Function)->parent()->parent();
            auto tn = parent->at(wf / Class / Ident, wf / TypeTrait / Ident);
            Token ptype =
              (parent->type() == Class) ? TypeClassName : TypeTraitName;
            Node args = Args;
            auto fwd = Expr
              << (call << (FunctionName
                           << (ptype << TypeUnit << clone(tn) << TypeArgs)
                           << clone(id) << TypeArgs)
                       << args);

            auto lhs = _[Lhs];
            auto rhs = _[Rhs];

            // Start with parameters that have no default value.
            for (auto it = lhs.first; it != lhs.second; ++it)
            {
              auto param_id = (*it)->at(wf / Param / Ident);
              params
                << (Param << clone(param_id)
                          << clone((*it)->at(wf / Param / Type)));
              args << (Expr << (RefLet << clone(param_id)));
            }

            for (auto it = rhs.first; it != rhs.second; ++it)
            {
              // At this point, the default argument is a create call on the
              // anonymous class derived from the lambda. Apply the created
              // lambda to get the default argument, checking for nonlocal.
              auto def_arg = Call
                << clone(Apply)
                << (Args << (Expr << (*it)->at(wf / Param / Default)));
              def_arg = nlrexpand(_, def_arg, true);

              // Add the default argument to the forwarding call.
              args << (Expr << def_arg);

              // Add a new function that calls the arity+1 function.
              seq
                << (Function << clone(ref) << clone(id) << clone(tp)
                             << clone(params) << clone(ty) << DontCare
                             << (Block << clone(fwd)));

              // Add a parameter.
              auto param_id = (*it)->at(wf / Param / Ident);
              params
                << (Param << clone(param_id)
                          << clone((*it)->at(wf / Param / Type)));

              // Replace the last argument with a reference to the parameter.
              args->pop_back();
              args << (Expr << (RefLet << clone(param_id)));
            }

            // The original function, with no default arguments.
            return seq
              << (Function << ref << id << tp << params << ty << DontCare
                           << _(Block));
          },

        T(Param) << (T(Ident)[Ident] * T(Type)[Type] * T(DontCare)) >>
          [](Match& _) { return Param << _(Ident) << _(Type); },

        T(Param)[Param] << (T(Ident) * T(Type) * T(Call)) >>
          [](Match& _) {
            return err(_[Param], "can't put a default argument here");
          },
      }};
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
             T(DontCare)) >>
       [](Match& _) {
         auto f = _(Function);
         auto ref = _(Ref);
         auto id = _(Id);
         auto tp = _(TypeParams);
         auto params = _(Params);
         size_t start_arity = 0;
         auto end_arity = params->size();
         auto parent = f->parent()->parent();
         auto defs = parent->lookdown(id->location());
         auto tn = parent->at(wf / Class / Ident, wf / TypeTrait / Ident);
         Token ptype =
           (parent->type() == Class) ? TypeClassName : TypeTraitName;
         Node call = (ref->type() == Ref) ? CallLHS : Call;

         // Find the lowest arity that is not already defined. If an arity 5 and
         // an arity 3 function `f` are provided, an arity 4 partial application
         // will be generated that calls the arity 5 function, and arity 0-2
         // functions will be generated that call the arity 3 function.
         for (auto def : defs)
         {
           if ((def == f) || (def->type() != Function))
             continue;

           auto arity = def->at(wf / Function / Params)->size();

           if (arity < end_arity)
             start_arity = std::max(start_arity, arity + 1);
         }

         Nodes names;

         // Create a unique anonymous class name for each arity.
         for (auto arity = start_arity; arity < end_arity; ++arity)
           names.push_back(Ident ^ _.fresh());

         Node ret = Seq;

         for (auto arity = start_arity; arity < end_arity; ++arity)
         {
           // Create an anonymous class for each arity.
           auto name = names[arity - start_arity];
           Node classbody = ClassBody;
           auto classdef = Class << clone(name) << TypeParams
                                 << (Type << TypeUnit) << classbody;

           // The anonymous class has fields for each supplied argument and a
           // create function that captures the supplied arguments.
           Node create_params = Params;
           Node new_args = Args;
           classbody
             << (Function << DontCare << (Ident ^ create) << TypeParams
                          << create_params << typevar(_) << DontCare
                          << (Block << (Expr << (Call << New << new_args))));

           // Create a function that returns the anonymous class for each arity.
           Node func_params = Params;
           Node func_args = Args;
           auto func =
             Function << clone(ref) << clone(id) << TypeParams << func_params
                      << typevar(_) << DontCare
                      << (Block
                          << (Expr
                              << (Call << (FunctionName
                                           << (TypeClassName << TypeUnit
                                                             << clone(name)
                                                             << TypeArgs)
                                           << (Ident ^ create) << TypeArgs)
                                       << func_args)));

           for (size_t i = 0; i < arity; ++i)
           {
             auto param = params->at(i);
             auto param_id = param->at(wf / Param / Ident);
             auto param_type = param->at(wf / Param / Type);
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
             auto self_id = Ident ^ _.fresh();
             Node apply_params = Params << (Param << self_id << typevar(_));
             Node fwd_args = Args;

             for (size_t j = 0; j < arity; ++j)
             {
               // Include our captured arguments.
               fwd_args
                 << (Expr
                     << (Call
                         << (Selector
                             << clone(params->at(j)->at(wf / Param / Ident))
                             << TypeArgs)
                         << (Args << (Expr << (RefLet << clone(self_id))))));
             }

             for (auto j = arity; j < i; ++j)
             {
               // Add the additional arguments passed to this apply function.
               auto param = params->at(j);
               apply_params << clone(param);
               fwd_args
                 << (Expr << (RefLet << clone(param->at(wf / Param / Ident))));
             }

             Node fwd;

             if (i == end_arity)
             {
               // The final arity calls the original function.
               fwd = FunctionName
                 << (ptype << TypeUnit << clone(tn) << TypeArgs) << clone(id)
                 << TypeArgs;
             }
             else
             {
               // Intermediate arities call the next arity.
               fwd = FunctionName
                 << (TypeClassName << TypeUnit << clone(names[i - start_arity])
                                   << TypeArgs)
                 << (Ident ^ create) << TypeArgs;
             }

             classbody
               << (Function
                   << clone(ref) << (Ident ^ apply) << TypeParams
                   << apply_params << typevar(_) << DontCare
                   << (Block << (Expr << (clone(call) << fwd << fwd_args))));
           }

           ret << classdef << func;
         }

         return ret << f;
       }},
    };
  }

  inline const auto Liftable = T(Unit) / T(Tuple) / T(Lambda) / T(Call) /
    T(CallLHS) / T(Conditional) / T(FieldRef) / T(TypeTest) / T(Cast) /
    T(Selector) / T(FunctionName) / Literal;

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
                parent->replace(ref, Move << ref->at(wf / RefLet / Ident));

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
            auto id = ref->at(wf / RefLet / Ident);
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
      auto llvm = f->at(wf / Function / LLVMFuncType)->type() == LLVMFuncType;
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
                T(Block)[Block]) >>
          [](Match& _) {
            auto id = _(Id);
            auto arity = _(Params)->size();
            auto name =
              std::string(id->location().view()) + "." + std::to_string(arity);

            if (_(Ref)->type() == Ref)
              name += ".ref";

            return Function << (Ident ^ name) << _(TypeParams) << _(Params)
                            << _(Type) << _(LLVMFuncType) << _(Block);
          },

        (T(Call) / T(CallLHS))[Call]
            << ((T(FunctionName)
                 << ((TypeName / T(TypeUnit))[Lhs] * Name[Id] *
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
            auto name = std::string("$new.") + std::to_string(arity);
            return Call << (FunctionName << TypeUnit << (Ident ^ name)
                                         << TypeArgs)
                        << _(Args);
          },

        T(CallLHS)[Call] << T(New) >>
          [](Match& _) { return err(_[Call], "can't assign to new"); },
      }};
  }

  PassDef typesubtype()
  {
    return {
      dir::bottomup,
      {
        T(TypeSubtype) << (TypeElem[Lhs] * TypeElem[Rhs]) >>
          ([](Match& _) -> Node {
            return subtype(_(Lhs), _(Rhs)) ? TypeTrue : TypeFalse;
          }),
      }};
  }

  Driver& driver()
  {
    static Driver d(
      "Verona",
      parser(),
      wfParser(),
      {
        {"modules", modules(), wfPassModules()},
        {"structure", structure(), wfPassStructure()},
        {"memberconflict", memberconflict(), wfPassStructure()},
        {"typenames", typenames(), wfPassTypeNames()},
        {"typeview", typeview(), wfPassTypeView()},
        {"typefunc", typefunc(), wfPassTypeFunc()},
        {"typealg", typealg(), wfPassTypeAlg()},
        {"typeflat", typeflat(), wfPassTypeFlat()},
        {"typerec", typerec(), wfPassTypeFlat()},
        {"conditionals", conditionals(), wfPassConditionals()},
        {"reference", reference(), wfPassReference()},
        {"reverseapp", reverseapp(), wfPassReverseApp()},
        {"application", application(), wfPassApplication()},
        {"assignlhs", assignlhs(), wfPassAssignLHS()},
        {"localvar", localvar(), wfPassLocalVar()},
        {"assignment", assignment(), wfPassAssignment()},
        {"nlrcheck", nlrcheck(), wfPassNLRCheck()},
        {"lambda", lambda(), wfPassLambda()},
        {"autofields", autofields(), wfPassAutoFields()},
        {"autorhs", autorhs(), wfPassAutoFields()},
        {"autocreate", autocreate(), wfPassAutoCreate()},
        {"defaultargs", defaultargs(), wfPassDefaultArgs()},
        {"partialapp", partialapp(), wfPassDefaultArgs()},
        {"anf", anf(), wfPassANF()},
        {"defbeforeuse", defbeforeuse(), wfPassANF()},
        {"drop", drop(), wfPassDrop()},
        {"namearity", namearity(), wfPassNameArity()},
        {"typesubtype", typesubtype(), wfPassNameArity()},
      });

    return d;
  }
}

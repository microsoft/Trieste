#include "internal.h"

#include <charconv>

namespace{
  using namespace trieste::wf::ops;
  using namespace infix;

  // | is used to create a Choice between all the elements
  // this indicates that literals can be an Int or a Float

  // A <<= B indicates that B is a child of A
  // ++ indicates that there are zero or more instances of the token

  inline const auto wf_expressions_tokens =
    (wf_parse_tokens - (String | Paren | Print)) | Expression;

  // clang-format off
  inline const auto wf_pass_expressions =
      (Top <<= Calculation)
    | (Calculation <<= (Assign | Output)++)
    // [Ident] here indicates that the Ident node is a symbol that should
    // be stored in the symbol table  
    | (Assign <<= Ident * Expression)[Ident]
    | (Output <<= String * Expression)
    // [1] here indicates that there should be at least one token
    | (Expression <<= wf_expressions_tokens++[1])
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_multiply_divide =
    wf_pass_expressions
    | (Multiply <<= Expression * Expression)
    | (Divide <<= Expression * Expression)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_add_subtract =
    wf_pass_multiply_divide
    | (Add <<= Expression * Expression)
    | (Subtract <<= Expression * Expression)
    ;
  // clang-format on

  inline const auto wf_operands_tokens = wf_expressions_tokens - Expression;

  // clang-format off
  inline const auto wf_pass_trim =
    wf_pass_add_subtract
    | (Expression <<= wf_operands_tokens)
    ;
  //clang-format on

  inline const auto wf_check_refs_tokens = (wf_operands_tokens - Ident) | Ref;

  // clang-format off
  inline const auto wf_pass_check_refs =
    wf_pass_trim
    | (Expression <<= wf_check_refs_tokens)
    | (Ref <<= Ident)
    ;
  // clang-format on

PassDef expressions()
  {
    return {
      "expressions",
      wf_pass_expressions,
      dir::topdown,
      {
        // In() indicates this is the root node of the pattern match.
        // What we return will replace the nodes we specify after the *.
        // The [] gives us a hook in the Match to use for referring to the
        // matched entity. Here we're saying that we want to create a
        // Calculation node and make all of the values in File (*_[File]) its
        // children.
        In(Top) * T(File)[File] >>
          [](Match& _) { return Calculation << *_[File]; },

        // This rule selects an Equals node with the right structure,
        // i.e. a single ident being assigned. We replace it with
        // an Assign node that has two children: the Ident and the
        // an Expression, which will take the children of the Group.
        In(Calculation) *
            (T(Equals) << ((T(Group) << T(Ident)[Id]) * T(Group)[Rhs])) >>
          [](Match& _) { return Assign << _(Id) << (Expression << *_[Rhs]); },

        // This rule selects a Group that matches the Output pattern
        // of `print <string> <expression>`. In this case, Any++ indicates that
        // Rhs should contain all the remaining tokens in the group.
        // When used here, * means nodes that are children of the In()
        // node in the specified order. They can be anywhere inside
        // the In() child sequence.
        In(Calculation) *
            (T(Group) << (T(Print) * T(String)[Lhs] * Any++[Rhs])) >>
          [](Match& _) { return Output << _(Lhs) << (Expression << _[Rhs]); },

        // This node unwraps Groups that are inside Parens, making them
        // Expression nodes.
        In(Expression) * (T(Paren) << T(Group)[Group]) >>
          [](Match& _) { return Expression << *_[Group]; },

        // errors

        // because rules are matched in order, this catches any
        // Paren nodes that had no children (because the rule above
        // will have handled those *with* children)
        T(Paren)[Paren] >>
          [](Match& _) { return err(_(Paren), "Empty paren"); },

        // Ditto for malformed equals nodes
        T(Equals)[Equals] >>
          [](Match& _) { return err(_(Equals), "Invalid assign"); },

        // Orphaned print node will catch bad output statements
        T(Print)[Print] >>
          [](Match& _) { return err(_(Print), "Invalid output"); },

        // Our WF definition allows this, so we need to handle it.
        T(Expression)[Rhs] << End >>
          [](Match& _) { return err(_(Rhs), "Empty expression"); },

        // Same with this.
        In(Expression) * T(String)[String] >>
          [](Match& _) {
            return err(_(String), "Expressions cannot contain strings");
          },

        T(Group)[Group] >>
          [](Match& _) { return err(_[Group], "syntax error"); },
      }};
  }

  inline const auto ExpressionArg = T(Expression, Ident) / Number;

  PassDef multiply_divide()
  {
    return {
      "multiply_divide",
      wf_pass_multiply_divide,
      dir::topdown,
      {
        // Group multiply and divide operations together. This rule will
        // select any triplet of <arg> *|/ <arg> in an expression list and
        // replace it with a single <expr> node that has the triplet as
        // its children.
        In(Expression) *
            (ExpressionArg[Lhs] * (T(Multiply, Divide))[Op] *
             ExpressionArg[Rhs]) >>
          [](Match& _) {
            return Expression
              << (_(Op) << (Expression << _(Lhs)) << (Expression << _[Rhs]));
          },
        (T(Multiply, Divide))[Op] << End >>
          [](Match& _) { return err(_(Op), "No arguments"); },
      }};
  }

  PassDef add_subtract()
  {
    return {
      "add_subtract",
      wf_pass_add_subtract,
      dir::topdown,
      {
        In(Expression) *
            (ExpressionArg[Lhs] * (T(Add, Subtract))[Op] *
             ExpressionArg[Rhs]) >>
          [](Match& _) {
            return Expression
              << (_(Op) << (Expression << _(Lhs)) << (Expression << _[Rhs]));
          },
        (T(Add, Subtract))[Op] << End >>
          [](Match& _) { return err(_(Op), "No arguments"); },
      }};
  }

  PassDef trim()
  {
    return {
      "trim",
      wf_pass_trim,
      dir::topdown,
      {
        // End is a special pattern which indicates that there
        // are no further nodes. So in this case we are matching
        // an Expression which has a single Expression as a
        // child.
        T(Expression) << (T(Expression)[Expression] * End) >>
          [](Match& _) { return _(Expression); },

        T(Expression) << (Any * Any[Rhs]) >>
          [](Match& _) {
            return err(_(Rhs), "Only one value allowed per expression");
          },
      }};
  }

  inline const auto Arg = T(Int) / T(Float) / T(Ident) / T(Expression);

  PassDef check_refs()
  {
    return {
      "check_refs",
      wf_pass_check_refs,
      dir::topdown,
      {
        In(Expression) * T(Ident)[Id] >>
          [](Match& _) {
            auto id = _(Id); // the Node object for the identifier
            auto defs = id->lookup(); // a list of matching symbols
            if (defs.size() == 0)
            {
              // there are no symbols with this identifier
              return err(id, "undefined");
            }

            return Ref << id;
          },
      }};
  }
}

namespace infix
{
  Reader reader()
  {
    return {
      "infix",
      {expressions(), multiply_divide(), add_subtract(), trim(), check_refs()},
      parser(),
    };
  }
}
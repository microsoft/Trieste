#include "infix.h"
#include "internal.h"
#include "trieste/rewrite.h"

namespace
{
  using namespace trieste::wf::ops;
  using namespace infix;

  // | is used to create a Choice between all the elements
  // this indicates that literals can be an Int or a Float

  // A <<= B indicates that B is a child of A
  // ++ indicates that there are zero or more instances of the token

  inline const auto TupleCandidate = TokenDef("infix-tuple-candidate");

  inline const auto wf_expressions_tokens =
    (wf_parse_tokens - (String | Paren | Print)) |
    Expression
    // --- tuples extension ---
    | TupleCandidate;

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

  inline const auto wf_tupled_tokens =
    (wf_expressions_tokens - TupleCandidate - Comma) | Tuple;

  // clang-format off
  inline const auto wf_pass_tuple_literals =
    wf_pass_add_subtract
    | (Expression <<= wf_tupled_tokens++[1])
    | (Tuple <<= Expression++)
    ;
  // clang-format on

  inline const auto wf_operands_tokens = wf_tupled_tokens - Expression;

  // clang-format off
  inline const auto wf_pass_trim =
    wf_pass_tuple_literals
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

  PassDef expressions(const Config& config)
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

        // --- tuples only:
        // (...), a group with parens, might be a tuple.
        // It depends on how many commas there are in it.
        // We need this if tuples require parens, and we are not handling tuples
        // in the parser.
        (In(Expression) *
         (T(Paren)[Paren] << T(Group)[Group]))([config](auto&&) {
          return !config.use_parser_tuples &&
            config.tuples_require_parens; // restrict when this rule applies
        }) >>
          [](Match& _) {
            return Expression << (TupleCandidate ^ _(Paren)) << *_[Group];
          },

        // --- tuples only, parser tuples version:
        In(Expression) * (T(ParserTuple)[ParserTuple] << T(Group)++[Group]) >>
          [](Match& _) {
            Node parser_tuple = ParserTuple;
            for (const auto& child : _[Group])
            {
              parser_tuple->push_back(
                Expression << std::span(child->begin(), child->end()));
            }
            return Expression << parser_tuple;
          },

        // This node unwraps Groups that are inside Parens, making them
        // Expression nodes.
        In(Expression) * (T(Paren) << T(Group)[Group]) >>
          // notice that *_[Group] has 2 parts:
          // - _[Group] which gets the captured Group as a range of nodes
          //   (length 1)
          // - * which gets the children of that group
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
            (T(Expression)[Lhs] * (T(Multiply, Divide))[Op] *
             T(Expression)[Rhs]) >>
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
            (T(Expression)[Lhs] * (T(Add, Subtract))[Op] *
             T(Expression)[Rhs]) >>
          [](Match& _) {
            return Expression
              << (_(Op) << (Expression << _(Lhs)) << (Expression << _[Rhs]));
          },
        (T(Add, Subtract))[Op] << End >>
          [](Match& _) { return err(_(Op), "No arguments"); },
      }};
  }

  PassDef tuple_literals(const Config& config)
  {
    const auto enable_if_no_parens = [config](auto&&) {
      return config.enable_tuples && !config.tuples_require_parens &&
        !config.use_parser_tuples;
    };
    return {
      "tuple_literals",
      wf_pass_tuple_literals,
      dir::topdown,
      {
        // --- if parens are required to tuple parsing, there will be
        // --- TupleCandidate tokens and these rules will run.
        // --- if not, see next section
        // --- the initial 2-element tuple case
        In(Expression) * T(TupleCandidate) * T(Expression)[Lhs] *
            (T(Comma) << End) * T(Expression)[Rhs] >>
          [](Match& _) { return Tuple << _(Lhs) << _(Rhs); },
        // tuple append case, where lhs is a tuple we partially built, and rhs
        // is an extra expression with a comma in between
        In(Expression) * T(TupleCandidate) * T(Tuple)[Lhs] * T(Comma) *
            T(Expression)[Rhs] >>
          [](Match& _) { return _(Lhs) << _(Rhs); },
        // the one-element tuple, where a candidate for tuple ends in a comma,
        // e.g. (42,)
        In(Expression) * T(TupleCandidate) * T(Expression)[Expression] *
            T(Comma) * End >>
          [](Match& _) { return Tuple << _(Expression); },
        // the not-a-tuple case. All things surrounded with parens might be
        // tuples, and are marked with TupleCandidate. When it's just e.g. (x),
        // it's definitely not a tuple so stop considering it.
        T(Expression)
            << (T(TupleCandidate) * T(Expression)[Expression] * End) >>
          [](Match& _) { return _(Expression); },
        // empty tuple, special case for ()
        T(Expression) << (T(TupleCandidate)[TupleCandidate] << End) >>
          [](Match& _) { return Expression << (Tuple ^ _(TupleCandidate)); },
        // anything that doesn't make sense here
        In(Expression) * T(TupleCandidate)[TupleCandidate] >>
          [](Match& _) {
            return err(_(TupleCandidate), "Malformed tuple literal");
          },

        // --- if parens are not required for tupling, then our rules treat the
        // --- comma as a regular min-precedence operator instead.
        // --- notice these rules don't use TupleCandidate, and just look for
        // --- raw Comma instances 2 expressions comma-separated makes a tuple
        In(Expression) *
            (T(Expression)[Lhs] * T(Comma) *
             T(Expression)[Rhs])(enable_if_no_parens) >>
          [](Match& _) { return (Tuple << _(Lhs) << _(Rhs)); },
        // a tuple, a comma, and an expression makes a longer tuple
        In(Expression) *
            (T(Tuple)[Lhs] * T(Comma) *
             T(Expression)[Rhs])(enable_if_no_parens) >>
          [](Match& _) { return _(Lhs) << _(Rhs); },
        // the one-element tuple uses , as a postfix operator
        In(Expression) *
            (T(Expression)[Expression] * T(Comma) * End)(enable_if_no_parens) >>
          [](Match& _) { return Tuple << _(Expression); },
        // malformed tuple, didn't match rules above
        In(Expression) * T(Comma)[Comma](enable_if_no_parens) >>
          [](Match& _) { return err(_(Comma), "Malformed tuple literal"); },

        // --- if the parser is trying to read tuples directly, extract them
        T(Expression) * T(ParserTuple) << T(Expression)++[Expression] >>
          [](Match& _) { return Expression << Tuple << _[Expression]; },

        // --- catch-all error, any stray commas left over
        In(Expression) * T(Comma)[Comma] >>
          [](Match& _) { return err(_(Comma), "Invalid use of comma"); },
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
  Reader reader(const Config& config)
  {
    return {
      "infix",
      {
        expressions(config),
        multiply_divide(),
        add_subtract(),
        tuple_literals(config),
        trim(),
        check_refs(),
      },
      parser(config.use_parser_tuples),
    };
  }
}

#include "infix.h"
#include "internal.h"
#include "trieste/ast.h"
#include "trieste/pass.h"
#include "trieste/rewrite.h"
#include "trieste/source.h"

namespace
{
  using namespace trieste::wf::ops;
  using namespace infix;

  // | is used to create a Choice between all the elements
  // this indicates that literals can be an Int or a Float

  // A <<= B indicates that B is a child of A
  // ++ indicates that there are zero or more instances of the token

  inline const auto TupleCandidate = TokenDef("infix-tuple-candidate");

  // clang-format off
  inline const auto wf_expressions_tokens =
    (wf_parse_tokens - (String | Paren | Print | Ident))
    | Ref
    | Expression
    // --- tuples extension ---
    | TupleCandidate
    | ParserTuple
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_expressions =
      (Top <<= Calculation)
    | (Calculation <<= (Assign | Output)++)
    // [Ident] here indicates that the Ident node is a symbol that should
    // be stored in the symbol table  
    | (Assign <<= Ident * Expression)[Ident]
    | (Ref <<= Ident) // not checked yet, but syntactically we're sure it's an identifier
    | (Output <<= String * Expression)
    // [1] here indicates that there should be at least one token
    | (Expression <<= wf_expressions_tokens++[1])
    // --- tuples extension ---
    | (ParserTuple <<= Expression++)
    | (Append <<= Expression)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_tuple_idx =
    wf_pass_expressions
    | (TupleIdx <<= Expression * Expression)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_multiply_divide =
    wf_pass_tuple_idx
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

  inline const auto wf_tupled_tokens = wf_expressions_tokens | Tuple;

  // clang-format off
  inline const auto wf_pass_tuple_literals =
    wf_pass_add_subtract
    | (Expression <<= wf_tupled_tokens++[1])
    | (Tuple <<= Expression++)
    ;
  // clang-format on

  inline const auto wf_tupled_orphaned_tokens =
    wf_tupled_tokens - Comma - TupleCandidate - ParserTuple;

  // clang-format off
  inline const auto wf_pass_tuple_literals_orphans =
    wf_pass_tuple_literals
    | (Expression <<= wf_tupled_orphaned_tokens++[1])
    ;
  // clang-format on

  inline const auto wf_operands_tokens = wf_tupled_orphaned_tokens - Expression;

  // clang-format off
  inline const auto wf_pass_trim =
    wf_pass_tuple_literals_orphans
    | (Expression <<= wf_operands_tokens)
    ;
  //clang-format on

  // clang-format off
  inline const auto wf_pass_append_post =
    wf_pass_trim
    | (Append <<= Expression++)
    ;
  // clang-format on

  inline const auto wf_check_refs_tokens = (wf_operands_tokens - Ident) | Ref;

  // clang-format off
  inline const auto wf_pass_check_refs =
    wf_pass_append_post
    | (Expression <<= wf_check_refs_tokens)
    | (Ref <<= Ident)
    ;
  // clang-format on

  PassDef expressions(const Config& config)
  {
    // How to restrict a rule to only if tuples require parens and aren't
    // handled in the parser.

    // Note that if we captured config "by value", then we would copy the const
    // Config&. This does bad things, in that we will be storing a reference to
    // a temporary object, not the actual config value, on the pass object. The
    // situation can be avoided with more careful capturing / ensuring the
    // captures type is just Config, but it might be simpler to just generate 2
    // capture-less lambdas.
    const auto enable_if_parens_no_parser_tuples =
      config.tuples_require_parens && !config.use_parser_tuples ?
      [](NodeRange&) { return true; } :
      [](NodeRange&) { return false; };
    // Double caution: you have to use *enable_if_parens_no_parser_tuples (with
    // the dereference!) or the pass will capture a reference to this function
    // pointer, which is basically just the address of the above var on the
    // stack. That will, of course, cause undefined behavior that does not spark
    // joy.

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

        // an append operation looks like append(...); we rely on tuple parsing
        // to get the (...) part right, but we can immediately recognize
        // append with a () next to it.
        // Marking the (...) under Expression ensures normal parsing happens to
        // it later.
        In(Expression) * T(Append)[Append] * T(Paren)[Paren] >>
          [](Match& _) {
            return Expression << (_(Append) << (Expression << _(Paren)));
          },

        // (...), a group with parens, might be a tuple.
        // It depends on how many commas there are in it.
        // We need this if tuples require parens, and we are not handling tuples
        // in the parser.
        (In(Expression) * (T(Paren)[Paren] << T(Group)[Group]))(
          *enable_if_parens_no_parser_tuples) >>
          [](Match& _) {
            return Expression << (TupleCandidate ^ _(Paren)) << *_[Group];
          },

        // --- tuples only, parser tuples version:
        // Special case: a ParserTuple with one empty group child is (,), which
        // we consider a valid empty tuple.
        In(Expression) *
            (T(Paren)
             << (T(ParserTuple)[ParserTuple] << ((T(Group) << End) * End))) >>
          [](Match& _) { return Expression << (ParserTuple ^ _(ParserTuple)); },
        // This grabs any Paren whose only child is a ParserTuple, and unwraps
        // that ParserTuple's nested groups into Expression nodes so future
        // passes will parse their contents properly.
        In(Expression) *
            (T(Paren) << (T(ParserTuple)[ParserTuple] << T(Group)++[Group]) *
               End) >>
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

        // In case of int and float literals, they are trivially expressions on
        // their own so we mark them as such without any further parsing
        // !! but only if they're not the only thing in an expression node !!
        // Otherwise, we create infinitely nested expressions by re-applying the
        // rule. This pair of rules requires _something_ to be on the left or
        // right of the literal, ensuring it's not alone.
        In(Expression) * Any[Lhs] * T(Int, Float)[Rhs] >>
          // Left case: Seq here splices multiple elements rather than one, so
          // we keep Lhs unchanged and in the right place
          [](Match& _) { return Seq << _(Lhs) << (Expression << _(Rhs)); },
        // Right case: --End is a negative lookahead, ensuring that there is
        // something to the right of the selected literal that isn't the end of
        // the containing node
        In(Expression) * T(Int, Float)[Expression] * (--End) >>
          [](Match& _) { return Expression << _(Expression); },

        // Do the same thing as above for identifiers, but it's easier because
        // the Ref marker simplifies telling before and after states apart.
        In(Expression) * T(Ident)[Ident] >>
          [](Match& _) { return Expression << (Ref << _(Ident)); },

        // errors

        // a tuple append that was not caught by append(...) is invalid.
        // We can tell because a stray append will not have an expression in it.
        T(Append)[Append] << --T(Expression) >>
          [](Match& _) { return err(_(Append), "Invalid append"); },

        // because rules are matched in order, this catches any invalid
        // Paren nodes, usually ones that had no children (because the rule
        // above will have handled those *with* children), or unusually
        // malformed parser tuples.
        T(Paren)[Paren] >>
          [](Match& _) { return err(_(Paren), "Invalid paren"); },

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

  PassDef tuple_idx(const Config& config)
  {
    auto enable_if_no_tuples = config.enable_tuples ?
      [](NodeRange&) { return false; } :
      [](NodeRange&) { return true; };

    return {
      "tuple_idx",
      wf_pass_tuple_idx,
      dir::bottomup,
      {
        T(TupleIdx)[Op](*enable_if_no_tuples) >>
          [](Match& _) { return err(_(Op), "Tuples are disabled."); },

        In(Expression) *
            (T(Expression)[Lhs] * (T(TupleIdx)[Op] << End) *
             T(Expression)[Rhs]) >>
          [](Match& _) { return Expression << (_(Op) << _(Lhs) << _(Rhs)); },
        T(TupleIdx)[Op] << End >>
          [](Match& _) { return err(_(Op), "No arguments"); },
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
          [](Match& _) { return Expression << (_(Op) << _(Lhs) << _(Rhs)); },
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
          [](Match& _) { return Expression << (_(Op) << _(Lhs) << _(Rhs)); },
        (T(Add, Subtract))[Op] << End >>
          [](Match& _) { return err(_(Op), "No arguments"); },
      }};
  }

  PassDef tuple_literals(const Config& config)
  {
    const auto enable_if_no_parens = config.enable_tuples &&
        !config.tuples_require_parens && !config.use_parser_tuples ?
      [](NodeRange&) { return true; } :
      [](NodeRange&) { return false; };
    return {
      "tuple_literals",
      wf_pass_tuple_literals,
      dir::topdown,
      {
        // --- if parens are required to tuple parsing, there will be
        // --- TupleCandidate tokens and these rules will run.
        // --- if not, see next section

        // the initial 2-element tuple case
        In(Expression) * T(TupleCandidate)[TupleCandidate] *
            T(Expression)[Lhs] * T(Comma) * T(Expression)[Rhs] >>
          [](Match& _) {
            return Seq << _(TupleCandidate) << (Tuple << _(Lhs) << _(Rhs));
          },
        // tuple append case, where lhs is a tuple we partially built, and rhs
        // is an extra expression with a comma in between
        In(Expression) * T(TupleCandidate)[TupleCandidate] * T(Tuple)[Lhs] *
            T(Comma) * T(Expression)[Rhs] >>
          [](Match& _) {
            return Seq << _(TupleCandidate) << (_(Lhs) << _(Rhs));
          },
        // same as above, but if the tuple is on the other side
        In(Expression) * T(TupleCandidate)[TupleCandidate] *
            T(Expression)[Lhs] * T(Comma) * T(Tuple)[Rhs] >>
          [](Match& _) {
            Node lhs = _(Lhs);
            Node rhs = _(Rhs);
            rhs->push_front(lhs);
            return Seq << _(TupleCandidate) << rhs;
          },
        // the one-element tuple, where a candidate for tuple ends in a comma,
        // e.g. (42,)
        In(Expression) * T(TupleCandidate)[TupleCandidate] *
            T(Expression)[Expression] * T(Comma) * End >>
          [](Match& _) { return Tuple << _(Expression); },
        // the not-a-tuple case. All things surrounded with parens might be
        // tuples, and are marked with TupleCandidate. When it's just e.g. (x),
        // it's definitely not a tuple so stop considering it.
        T(Expression)
            << (T(TupleCandidate) * T(Expression)[Expression] * End) >>
          [](Match& _) { return _(Expression); },
        // If a TupleCandidate has been reduced to just a tuple, then we're
        // done.
        T(Expression) << (T(TupleCandidate) * T(Tuple)[Tuple] * End) >>
          [](Match& _) { return Expression << _(Tuple); },
        // Comma as suffix is allowed, just means the same tuple as without the
        // comma
        T(Expression)
            << (T(TupleCandidate) * T(Tuple)[Tuple] * T(Comma) * End) >>
          [](Match& _) { return Expression << _(Tuple); },
        // Just a comma means an empty tuple
        T(Expression) << (T(TupleCandidate) * T(Comma)[Comma] * End) >>
          [](Match& _) { return Expression << (Tuple ^ _(Comma)); },
        // empty tuple, special case for ()
        T(Expression) << (T(TupleCandidate)[TupleCandidate] * End) >>
          [](Match& _) { return Expression << (Tuple ^ _(TupleCandidate)); },

        // --- if parens are not required for tupling, then our rules treat the
        // --- comma as a regular min-precedence operator instead.
        // --- notice these rules don't use TupleCandidate, and just look for
        // --- raw Comma instances

        // an expression exclusively composed of one comma is a nullary tuple.
        // this is not the only way to do it, but it allows mostly sensible
        // examples like
        // ```
        // x = ,;
        // x = (,);
        // x = , + ,; // semantically bad but syntactically ok
        // ```
        // etc...
        T(Expression) << (T(Comma)[Comma] * End)(*enable_if_no_parens) >>
          [](Match& _) { return Expression << (Tuple ^ _(Comma)); },
        // 2 expressions comma-separated makes a tuple
        In(Expression) *
            (T(Expression)[Lhs] * T(Comma) *
             T(Expression)[Rhs])(*enable_if_no_parens) >>
          [](Match& _) { return (Tuple << _(Lhs) << _(Rhs)); },
        // a tuple, a comma, and an expression makes a longer tuple
        In(Expression) *
            (T(Tuple)[Lhs] * T(Comma) *
             T(Expression)[Rhs])(*enable_if_no_parens) >>
          [](Match& _) { return _(Lhs) << _(Rhs); },
        // the one-element tuple uses , as a postfix operator
        In(Expression) *
            (T(Expression)[Expression] * T(Comma) *
             End)(*enable_if_no_parens) >>
          [](Match& _) { return Tuple << _(Expression); },
        // one trailing comma at the end of a tuple is allowed (but not more)
        T(Expression) << (T(Tuple)[Tuple] * T(Comma) * End)(
          *enable_if_no_parens) >>
          [](Match& _) { return Expression << _(Tuple); },

        // --- if the parser is trying to read tuples directly, extract them
        T(Expression) << (T(ParserTuple) << T(Expression)++[Expression]) >>
          [](Match& _) { return Expression << (Tuple << _[Expression]); },
      }};
  }

  // This rule has to be distinct from tuple_literals, because the rule above
  // requires repeated iterations before it matches everything. If these rules
  // were mixed in there, then an in-progress transformation by the previous
  // pass could be flagged as an error mid-pass, breaking the compiler.
  PassDef tuple_literals_orphans()
  {
    return {
      "tuple_literals_orphans",
      wf_pass_tuple_literals_orphans,
      dir::topdown,
      {
        // if a comma remains, replace it with an error.
        // it was not able to match anything in the previous rule.
        In(Expression) * T(Comma)[Comma] >>
          [](Match& _) { return err(_(Comma), "Invalid use of comma"); },

        // likewise, all possible tuples should be either been resolved as
        // actual tuples or plain expressions; other cases are errors.
        In(Expression) * T(TupleCandidate)[TupleCandidate] >>
          [](Match& _) {
            return err(_(TupleCandidate), "Malformed tuple literal");
          },

        In(Expression) * T(ParserTuple)[ParserTuple] >>
          [](Match& _) { return err(_(ParserTuple), "Invalid tuple"); },
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

  PassDef append_post()
  {
    return {
      "append_post",
      wf_pass_append_post,
      dir::topdown | dir::once,
      {
        T(Append)[Append] << (T(Expression) << --T(Tuple)) >>
          [](Match& _) { return err(_(Append), "Invalid use of append"); },

        T(Append)
            << (T(Expression)
                << (T(Tuple) << (T(Expression)++[Expression] * End))) >>
          [](Match& _) { return Append << _[Expression]; },
      }};
  }

  PassDef check_refs()
  {
    return {
      "check_refs",
      wf_pass_check_refs,
      dir::topdown,
      {
        T(Expression) << (T(Ref) << T(Ident)[Id]) >>
          [](Match& _) {
            auto id = _(Id); // the Node object for the identifier
            auto defs = id->lookup(); // a list of matching symbols
            if (defs.size() == 0)
            {
              // there are no symbols with this identifier
              return err(id, "undefined");
            }

            // we're just checking refs; we only make a change if we find an
            // error
            return NoChange ^ "";
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
        tuple_idx(config),
        multiply_divide(),
        add_subtract(),
        tuple_literals(config),
        tuple_literals_orphans(),
        trim(),
        append_post(),
        check_refs(),
      },
      parser(config.use_parser_tuples),
    };
  }
}

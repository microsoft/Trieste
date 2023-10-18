#include "lang.h"

#include "wf.h"

#include <charconv>

namespace infix
{
  // The Error token allows the creation of a special node which we can
  // use to replace the erroneous node. This will then exempt that subtree
  // from the well-formedness check. This is the mechanism by which we can
  // use the testing system to discover edges cases, i.e. the testing will
  // not proceed to the next pass until all of the invalid subtrees have
  // been marked as `Error`.
  auto err(const NodeRange& r, const std::string& msg)
  {
    return Error << (ErrorMsg ^ msg) << (ErrorAst << r);
  }

  auto err(Node node, const std::string& msg)
  {
    return Error << (ErrorMsg ^ msg) << (ErrorAst << node);
  }

  bool exists(const NodeRange& n)
  {
    Node node = *n.first;
    auto defs = node->lookup();
    return defs.size() > 0;
  }

  bool can_replace(const NodeRange& n)
  {
    Node node = *n.first;
    auto defs = node->lookup();
    if (defs.size() == 0)
    {
      return false;
    }

    auto assign = defs.front();
    return assign->back() == Literal;
  }

  int get_int(const Node& node)
  {
    std::string text(node->location().view());
    return std::stoi(text);
  }

  double get_double(const Node& node)
  {
    std::string text(node->location().view());
    return std::stod(text);
  }

  inline const auto Number = T(Int, Float);

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

  inline const auto MathsOp = T(Add) / T(Subtract) / T(Multiply) / T(Divide);

  PassDef maths()
  {
    return {
      "maths",
      wf_pass_maths,
      dir::topdown,
      {
        T(Add) << ((T(Literal) << T(Int)[Lhs]) * (T(Literal) << T(Int)[Rhs])) >>
          [](Match& _) {
            int lhs = get_int(_(Lhs));
            int rhs = get_int(_(Rhs));
            // ^ here means to create a new node of Token type Int with the
            // provided string as its location.
            return Int ^ std::to_string(lhs + rhs);
          },

        T(Add) << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
          [](Match& _) {
            double lhs = get_double(_(Lhs));
            double rhs = get_double(_(Rhs));
            return Float ^ std::to_string(lhs + rhs);
          },

        T(Subtract)
            << ((T(Literal) << T(Int)[Lhs]) * (T(Literal) << T(Int)[Rhs])) >>
          [](Match& _) {
            int lhs = get_int(_(Lhs));
            int rhs = get_int(_(Rhs));
            return Int ^ std::to_string(lhs - rhs);
          },

        T(Subtract)
            << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
          [](Match& _) {
            double lhs = get_double(_(Lhs));
            double rhs = get_double(_(Rhs));
            return Float ^ std::to_string(lhs - rhs);
          },

        T(Multiply)
            << ((T(Literal) << T(Int)[Lhs]) * (T(Literal) << T(Int)[Rhs])) >>
          [](Match& _) {
            double lhs = get_double(_(Lhs));
            double rhs = get_double(_(Rhs));
            return Int ^ std::to_string(lhs * rhs);
          },

        T(Multiply)
            << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
          [](Match& _) {
            double lhs = get_double(_(Lhs));
            double rhs = get_double(_(Rhs));
            return Float ^ std::to_string(lhs * rhs);
          },

        T(Divide)
            << ((T(Literal) << T(Int)[Lhs]) * (T(Literal) << T(Int)[Rhs])) >>
          [](Match& _) {
            int lhs = get_int(_(Lhs));
            int rhs = get_int(_(Rhs));
            if (rhs == 0)
            {
              return err(_(Rhs), "Divide by zero");
            }

            return Int ^ std::to_string(lhs / rhs);
          },

        T(Divide)
            << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
          [](Match& _) {
            double lhs = get_double(_(Lhs));
            double rhs = get_double(_(Rhs));
            if (rhs == 0.0)
            {
              return err(_(Rhs), "Divide by zero");
            }

            return Float ^ std::to_string(lhs / rhs);
          },

        T(Expression) << (T(Ref) << T(Ident)[Id])(
          [](auto& n) { return can_replace(n); }) >>
          [](Match& _) {
            auto defs = _(Id)->lookup();
            auto assign = defs.front();
            // the assign node has two children: the ident, and its value
            // this returns the second
            return assign->back()->clone();
          },

        T(Expression) << (T(Int) / T(Float))[Rhs] >>
          [](Match& _) { return Literal << _(Rhs); },

        // errors

        T(Expression) << (T(Ref) << T(Ident)[Id])(
          [](auto& n) { return !exists(n); }) >>
          [](Match&) {
            // NB this case shouldn't happen at all
            // during this pass and as such is not
            // an error, but currently occurs during
            // generative testing.
            return Literal << (Int ^ "0");
          },

        // Note how we pattern match explicitly for the Error node
        In(Expression) *
            (MathsOp
             << ((T(Expression)[Expression] << T(Error)) * T(Literal))) >>
          [](Match& _) {
            return err(_(Expression), "Invalid left hand argument");
          },

        In(Expression) *
            (MathsOp
             << (T(Literal) * (T(Expression)[Expression] << T(Error)))) >>
          [](Match& _) {
            return err(_(Expression), "Invalid right hand argument");
          },

        In(Expression) *
            (MathsOp[Op]
             << ((T(Expression) << T(Error)) * (T(Expression) << T(Error)))) >>
          [](Match& _) { return err(_(Op), "No valid arguments"); },

        In(Calculation) *
            (T(Output)[Output] << (T(String) * (T(Expression) << T(Error)))) >>
          [](Match& _) { return err(_(Output), "Empty output expression"); },

        In(Calculation) *
            (T(Assign)[Assign] << (T(Ident) * (T(Expression) << T(Error)))) >>
          [](Match& _) { return err(_(Assign), "Empty assign expression"); },
      }};
  }

  PassDef cleanup()
  {
    return {
      "cleanup",
      wf_pass_cleanup,
      dir::topdown,
      {
        In(Calculation) * T(Assign) >> [](Match&) -> Node { return {}; },

        T(Literal) << Any[Rhs] >> [](Match& _) { return _(Rhs); },
      }};
  }

  Driver& driver()
  {
    static Driver d(
      "infix",
      nullptr,
      parser(),
      {
        expressions(),
        multiply_divide(),
        add_subtract(),
        trim(),
        check_refs(),
        maths(),
        cleanup(),
      });

    return d;
  }
}

#include "infix.h"
#include "internal.h"
#include "trieste/pass.h"
#include "trieste/token.h"
#include "trieste/wf.h"

#include <string>

namespace
{
  using namespace trieste;
  using namespace infix;

  // clang-format off
  inline const auto wf_pass_maths = 
    infix::wf
    | (Literal <<= Int | Float)
    // Add Literal to the set of possible expressions; it will be the only one after the errs pass.
    | (Expression <<= (Add | Subtract | Multiply | Divide | Ref | Float | Int | Literal))
    // --- tuples extension
    | (Literal <<= Int | Float | Tuple)
    | (Expression <<= (Tuple | TupleIdx | Append | Add | Subtract | Multiply | Divide | Ref | Float | Int | Literal))
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_math_errs = 
    wf_pass_maths
    // now errors are cleaned up, we only allow literal expressions
    | (Expression <<= Literal)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_cleanup =
    wf_pass_math_errs
    // ensure that there are no assignments, only
    // outputs here  
    | (Calculation <<= Output++) 
    ;
  // clang-format on  

  bool exists(const NodeRange& n)
  {
    return !n.front()->lookup().empty();
  }

  bool can_replace(const NodeRange& n)
  {
    auto defs = n.front()->lookup();
    if (defs.size() == 0)
    {
      return false;
    }

    auto assign = defs.front();
    return assign->back() == Expression && assign->back()->back() == Literal;
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

  inline const auto MathsOp = T(Add, Subtract, Multiply, Divide);

  PassDef maths()
  {
    return {
      "maths",
      wf_pass_maths,
      dir::topdown,
      {
        T(Add) << (T(Expression) << ((T(Literal) << T(Int)[Lhs]))) * (T(Expression) << (T(Literal) << T(Int)[Rhs])) >>
          [](Match& _) {
            auto lhs = get_int(_(Lhs));
            auto rhs = get_int(_(Rhs));

            // ^ here means to create a new node of Token type Int with the
            // provided string as its location (which means its "value").
            return Literal << (Int ^ std::to_string(lhs + rhs));
          },

        T(Add) << (T(Expression) << ((T(Literal) << Number[Lhs]))) * (T(Expression) << (T(Literal) << Number[Rhs])) >>
          [](Match& _) {
            auto lhs = get_double(_(Lhs));
            auto rhs = get_double(_(Rhs));

            return Literal << (Float ^ std::to_string(lhs + rhs));
          },

        T(Subtract) << (T(Expression) << ((T(Literal) << T(Int)[Lhs]))) * (T(Expression) << (T(Literal) << T(Int)[Rhs])) >>
          [](Match& _) {
            auto lhs = get_int(_(Lhs));
            auto rhs = get_int(_(Rhs));

            return Literal << (Int ^ std::to_string(lhs - rhs));
          },

        T(Subtract) << (T(Expression) << ((T(Literal) << Number[Lhs]))) * (T(Expression) << (T(Literal) << Number[Rhs])) >>
          [](Match& _) {
            auto lhs = get_double(_(Lhs));
            auto rhs = get_double(_(Rhs));

            return Literal << (Float ^ std::to_string(lhs - rhs));
          },

        T(Multiply) << (T(Expression) << ((T(Literal) << T(Int)[Lhs]))) * (T(Expression) << (T(Literal) << T(Int)[Rhs])) >>
          [](Match& _) {
            auto lhs = get_int(_(Lhs));
            auto rhs = get_int(_(Rhs));

            return Literal << (Int ^ std::to_string(lhs * rhs));
          },

        T(Multiply) << (T(Expression) << ((T(Literal) << Number[Lhs]))) * (T(Expression) << (T(Literal) << Number[Rhs])) >>
          [](Match& _) {
            auto lhs = get_double(_(Lhs));
            auto rhs = get_double(_(Rhs));

            return Literal << (Float ^ std::to_string(lhs * rhs));
          },

        T(Divide) << (T(Expression) << ((T(Literal) << T(Int)[Lhs]))) * (T(Expression) << (T(Literal) << T(Int)[Rhs])) >>
          [](Match& _) {
            auto lhs = get_int(_(Lhs));
            auto rhs = get_int(_(Rhs));

            if(rhs == 0) {
              return NoChange ^ "";
            }
            return Literal << (Int ^ std::to_string(lhs / rhs));
          },

        T(Divide) << (T(Expression) << ((T(Literal) << Number[Lhs]))) * (T(Expression) << (T(Literal) << Number[Rhs])) >>
          [](Match& _) {
            auto lhs = get_double(_(Lhs));
            auto rhs = get_double(_(Rhs));

            if(rhs == 0.0) {
              return NoChange ^ "";
            }
            return Literal << (Float ^ std::to_string(lhs / rhs));
          },

        T(Expression) << (T(Ref) << T(Ident)[Id])(
          [](NodeRange& n) { return can_replace(n); }) >>
          [](Match& _) {
            auto defs = _(Id)->lookup();
            auto assign = defs.front();
            // the assign node has two children: the ident, and its value
            // this returns the second
            return assign->back()->clone();
          },

        T(Expression) << Number[Rhs] >>
          [](Match& _) { return Expression << (Literal << _(Rhs)); },

        // --- tuples extension ---

        // a tuple of only literals is a literal; strip the expression prefix
        T(Expression) << (T(Tuple)[Tuple] << ((T(Expression) << T(Literal))++ * End)) >>
          [](Match& _) {
            return Expression << (Literal << _(Tuple));
          },

        // 0 or more tuples appended make an aggregate tuple
        T(Expression) << (T(Append) << ((T(Expression) << (T(Literal) << T(Tuple)))++[Expression] * End)) >>
          [](Match& _) {
            Node combined_tuple = Tuple;
            for(Node child : _[Expression]) {
              Node child_literal = child->front();
              Node sub_tuple = child_literal->front();
              for(Node elem : *sub_tuple) {
                combined_tuple->push_back(elem);
              }
            }
            return Expression << (Literal << combined_tuple);
          },

        // given a literal tuple and a literal idx, pick out the relevant tuple part or leave an error
        T(Expression) << (T(TupleIdx)[TupleIdx] << (T(Expression) << (T(Literal) << T(Tuple)[Lhs])) * (T(Expression) << (T(Literal) << T(Int)[Rhs]))) >>
          [](Match& _) {
            Node lhs = _(Lhs);
            Node rhs = _(Rhs);
            int rhs_val = get_int(rhs);

            if(rhs_val < 0 || size_t(rhs_val) >= lhs->size()) {
              return NoChange ^ ""; // error, see next pass
            }
            
            return lhs->at(rhs_val);
          },
      }};
  }

  PassDef math_errs() {
    return {
      "math_errs",
      wf_pass_math_errs,
      dir::bottomup,
      {
        // custom error pattern: we can catch specific shapes and give more useful errors in those cases
        T(Expression) << (T(Divide)[Divide] << (Any * (T(Expression) << (T(Literal) << T(Int, Float)[Rhs])))) >>
          [](Match& _) {
            auto rhs = _(Rhs);
            if((rhs == Int && get_int(rhs) == 0) || (rhs == Float && get_double(rhs) == 0)) {
              return err(_(Divide), "Divide by 0");
            } else {
              return NoChange ^ "";
            }
          },

        T(Expression) << (T(Ref) << T(Ident)[Id])(
          [](NodeRange& n) { return !exists(n); }) >>
          [](Match&) {
            // NB this case shouldn't happen at all
            // during this pass and as such is not
            // an error, but currently occurs during
            // generative testing.
            return Expression << (Literal << (Int ^ "0"));
          },

        T(Expression) << MathsOp[Op] >>
          [](Match& _) {
            return err(_(Op), "Invalid maths op");
          },

        // --- tuple errors ---

        T(Expression) << T(Tuple)[Tuple] >>
          [](Match& _) {
            return err(_(Tuple), "Couldn't evaluate tuple literal");
          },

        T(Expression) << T(Append)[Append] >>
          [](Match& _) {
            return err(_(Append), "Invalid or unevaluatable tuple append operands");
          },

        T(Expression) << T(TupleIdx)[TupleIdx] >>
          [](Match& _) {
            return err(_(TupleIdx), "Invalid or unevaluatable tuple index");
          },

        // --- catch-all: fuzz testing generates expressions that wouldn't be stuck but somehow are
        // These expressions are things like "unevaluated 1 + 1", which can only have come from the
        // fuzzer, because `1 + 1`'s evaluation is well defined in the previous pass.
        T(Expression)[Expression] << --T(Literal) >>
          [](Match& _) { return err(_(Expression), "Unevaluated expression"); },
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
      
        T(String, R"("[^"]*")")[String] >> [](Match& _) {
          Location loc = _(String)->location();
          loc.pos += 1;
          loc.len -= 2;
          return String ^ loc;
        },
      }};
  }

  // clang-format off
  const auto wf_infix_to_file =
    infix::wf
    | (Top <<= File)
    | (File <<= Path * Calculation)
    ;
  // clang-format on

  // clang-format off
  const auto wf_output_to_file =
    wf_pass_cleanup
    | (Top <<= File)
    | (File <<= Path * Calculation)
    ;
  // clang-format on

  // There are 2 versions of this function.
  // One deals in `Expression`, and the other deals in `Literal`.
  // The rule is exactly the same and the function isn't public, so we just take
  // the wf we mean to have as a parameter.
  PassDef to_file(const wf::Wellformed& wf, const std::filesystem::path& path)
  {
    return {
      "to_file",
      wf,
      dir::bottomup | dir::once,
      {
        In(Top) * T(Calculation)[Calculation] >>
          [path](Match& _) {
            return File << (Path ^ path.string()) << _(Calculation);
          },
      }};
  }

  bool write_infix(std::ostream& os, Node node)
  {
    if (node == Expression)
    {
      node = node->front();
    }

    // all we need to handle literal outputs alongside expressions
    if (node == Literal)
    {
      node = node->front();
    }

    if (node == Ref)
    {
      node = node->front();
    }

    if (node->in({Int, Float, String, Ident}))
    {
      os << node->location().view();
      return false;
    }

    if (node->in({Add, Subtract, Multiply, Divide}))
    {
      os << "(";
      if (write_infix(os, node->front()))
      {
        return true;
      }
      os << " " << node->location().view() << " ";
      if (write_infix(os, node->back()))
      {
        return true;
      }
      os << ")";

      return false;
    }

    if (node == Tuple)
    {
      os << "(";
      bool is_init = true;
      for (auto child : *node)
      {
        if (is_init)
        {
          is_init = false;
        }
        else
        {
          os << ", ";
        }
        if (write_infix(os, child))
        {
          return true;
        }
      }
      os << ",)";

      return false;
    }

    if (node == Append)
    {
      // very similar to tuples... could deduplicate at some point
      os << "append(";
      bool is_init = true;
      for (auto child : *node)
      {
        if (is_init)
        {
          is_init = false;
        }
        else
        {
          os << ", ";
        }
        if (write_infix(os, child))
        {
          return true;
        }
      }
      os << ",)";

      return false;
    }

    if (node == TupleIdx)
    {
      os << "(";
      if (write_infix(os, node->front()))
      {
        return true;
      }
      os << ").(";
      if (write_infix(os, node->back()))
      {
        return true;
      }
      os << ")";

      return false;
    }

    if (node == Assign)
    {
      if (write_infix(os, node->front()))
      {
        return true;
      }

      os << " = ";
      if (write_infix(os, node->back()))
      {
        return true;
      }

      os << ";" << std::endl;

      return false;
    }

    if (node == Output)
    {
      os << "print ";
      if (write_infix(os, node->front()))
      {
        return true;
      }

      os << " ";
      if (write_infix(os, node->back()))
      {
        return true;
      }

      os << ";" << std::endl;
      return false;
    }

    if (node == Calculation)
    {
      for (const Node& step : *node)
      {
        if (write_infix(os, step))
        {
          return true;
        }
      }

      return false;
    }

    os << "<error: unknown node type " << node->type() << ">" << std::endl;
    return true;
  }

  bool write_postfix(std::ostream& os, Node node)
  {
    if (node == Expression)
    {
      node = node->front();
    }

    if (node == Ref)
    {
      node = node->front();
    }

    if (node->in({Int, Float, String, Ident}))
    {
      os << node->location().view();
      return false;
    }

    if (node->in({Add, Subtract, Multiply, Divide, TupleIdx}))
    {
      if (write_postfix(os, node->front()))
      {
        return true;
      }

      os << " ";

      if (write_postfix(os, node->back()))
      {
        return true;
      }

      os << " " << node->location().view();

      return false;
    }

    // These 2 outputs below are a postfix encoding of variadic operations,
    // where the number of arguments is written explicitly nearest the operator.
    // I'm pretty sure Forth or its successors don't work like that...
    // not very practical, but it gets the message across for demonstation
    // purposes.

    if (node == Append)
    {
      for (const auto& elem : *node)
      {
        if (write_postfix(os, elem))
        {
          return true;
        }
        os << " ";
      }
      os << node->size() << " append";

      return false;
    }

    if (node == Tuple)
    {
      for (const auto& elem : *node)
      {
        if (write_postfix(os, elem))
        {
          return true;
        }
        os << " ";
      }
      os << node->size() << " tuple";

      return false;
    }

    if (node == Assign)
    {
      if (write_postfix(os, node->front()))
      {
        return true;
      }

      os << " ";

      if (write_postfix(os, node->back()))
      {
        return true;
      }

      os << " =" << std::endl;

      return false;
    }

    if (node == Output)
    {
      if (write_postfix(os, node->front()))
      {
        return true;
      }

      os << " ";

      if (write_postfix(os, node->back()))
      {
        return true;
      }

      os << " print" << std::endl;
      return false;
    }

    if (node == Calculation)
    {
      for (const Node& step : *node)
      {
        if (write_postfix(os, step))
        {
          return true;
        }
      }

      return false;
    }

    os << "<error: unknown node type " << node->type() << ">" << std::endl;
    return true;
  }
}

namespace infix
{
  Rewriter calculate()
  {
    return {
      "calculate",
      {maths(), math_errs(), cleanup()},
      infix::wf,
    };
  }

  Writer writer(const std::filesystem::path& path)
  {
    return {
      "infix",
      {to_file(wf_infix_to_file, path)},
      infix::wf,
      [](std::ostream& os, Node contents) {
        return write_infix(os, contents);
      }};
  }

  Writer postfix_writer(const std::filesystem::path& path)
  {
    return {
      "postfix",
      {to_file(wf_infix_to_file, path)},
      infix::wf,
      [](std::ostream& os, Node contents) {
        return write_postfix(os, contents);
      }};
  }

  Writer calculate_output_writer(const std::filesystem::path& path)
  {
    return {
      "calculate_output",
      {to_file(wf_output_to_file, path)},
      wf_pass_cleanup,
      [](std::ostream& os, Node contents) {
        assert(contents == Calculation);
        for (const auto& output : *contents)
        {
          assert(output == Output);
          assert(output->size() == 2);
          auto name = output->front();
          auto val = output->back();

          os << name->location().view() << " <- ";
          if (write_infix(os, val))
          {
            return true;
          }
          os << std::endl;
        }
        return false;
      }};
  }
}

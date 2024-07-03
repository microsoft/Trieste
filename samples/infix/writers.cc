#include "infix.h"
#include "internal.h"
#include "trieste/pass.h"
#include "trieste/token.h"

#include <string>

namespace
{
  using namespace trieste;
  using namespace infix;

  // clang-format off
  inline const auto wf_pass_maths = 
    infix::wf
    // The Op >>= prefix ensures the internal structure of the WF is set up properly.
    // The disjunction cannot otherwise be nested inside *
    // Technically it names the disjunction "Op", but we do not use that name.
    | (Assign <<= Ident * (Op >>= Literal | Expression)) 
    | (Output <<= String * (Op >>= Literal | Expression)) 
    | (Literal <<= Int | Float)
    // all the math ops, but with both literal and expression as options
    | (Add <<= (Lhs >>= Literal | Expression) * (Rhs >>= Literal | Expression))
    | (Subtract <<= (Lhs >>= Literal | Expression) * (Rhs >>= Literal | Expression))
    | (Multiply <<= (Lhs >>= Literal | Expression) * (Rhs >>= Literal | Expression))
    | (Divide <<= (Lhs >>= Literal | Expression) * (Rhs >>= Literal | Expression))
    // --- tuples extension
    | (Literal <<= Int | Float | Tuple)
    | (Tuple <<= (Literal | Expression)++)
    | (Append <<= (Literal | Expression)++)
    | (TupleIdx <<= (Lhs >>= Literal | Expression) * (Rhs >>= Literal | Expression))
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_math_errs = 
    wf_pass_maths
    | (Assign <<= Ident * Literal) 
    | (Output <<= String * Literal) 
    // --- tuples extension
    | (Tuple <<= Literal++)
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

  inline const auto MathsOp = T(Add, Subtract, Multiply, Divide);

  PassDef maths()
  {
    // TODO: revert this or no? Might be a bit technical for intro tutorial; I just wanted to see what it looked like.
    auto coerce_num_types = [](Node lhs, Node rhs, auto op) -> Node {
      auto opt_to_result = [](auto num_opt, const TokenDef& tok) -> Node {
        if(num_opt) {
          // ^ here means to create a new node of Token type Int with the
          // provided string as its location (which means its "value").
          return tok ^ std::to_string(*num_opt);
        } else {
          return NoChange ^ "";
        }
      };

      if(lhs == Int && rhs == Int) {
        return opt_to_result(
          op(get_int(lhs), get_int(rhs)),
          Int);
      } else {
        return opt_to_result(
          op(get_double(lhs), get_double(rhs)),
          Float);
      }
    };

    return {
      "maths",
      wf_pass_maths,
      dir::topdown,
      {
        T(Add) << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
          [coerce_num_types](Match& _) {
            return coerce_num_types(_(Lhs), _(Rhs), [](auto lhs, auto rhs) {
              return std::optional{lhs + rhs};
            });
          },

        T(Subtract)
            << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
          [coerce_num_types](Match& _) {
            return coerce_num_types(_(Lhs), _(Rhs), [](auto lhs, auto rhs) {
              return std::optional{lhs - rhs};
            });
          },

        T(Multiply)
            << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
          [coerce_num_types](Match& _) {
            return coerce_num_types(_(Lhs), _(Rhs), [](auto lhs, auto rhs) {
              return std::optional{lhs * rhs};
            });
          },

        T(Divide)
            << ((T(Literal) << Number[Lhs]) * (T(Literal) << Number[Rhs])) >>
          [coerce_num_types](Match& _) {
            return coerce_num_types(_(Lhs), _(Rhs), [](auto lhs, auto rhs) -> std::optional<decltype(lhs)> {
              // avoid doing divide by 0 ... we will report this in the errors pass
              if(rhs == 0) {
                return std::nullopt;
              } else {
                return std::optional{lhs / rhs};
              }
            });
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
          [](Match& _) { return Literal << _(Rhs); },

        // --- tuples extension ---

        // a tuple of only literals is a literal; strip the expression prefix
        T(Expression) << (T(Tuple)[Tuple] << (T(Literal)++ * End)) >>
          [](Match& _) {
            return Literal << _(Tuple);
          },

        // 0 or more tuples appended make an aggregate tuple
        T(Expression) << (T(Append) << ((T(Literal) << T(Tuple))++[Literal] * End)) >>
          [](Match& _) {
            Node combined_tuple = Tuple;
            for(Node child : _[Literal]) {
              Node sub_tuple = child->front();
              for(Node elem : *sub_tuple) {
                combined_tuple->push_back(elem);
              }
            }
            return Literal << combined_tuple;
          },

        // given a literal tuple and a literal idx, pick out the relevant tuple part or leave an error
        T(Expression) << (T(TupleIdx)[TupleIdx] << (T(Literal) << T(Tuple)[Lhs]) * (T(Literal) << T(Int)[Rhs])) >>
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
      dir::topdown,
      {
        // custom error pattern: we can catch specific shapes and give more useful errors in those cases
        T(Expression) << (T(Divide)[Divide] << (Any * (T(Literal) << T(Int, Float)[Rhs]))) >>
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
            return Literal << (Int ^ "0");
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
  const auto wf_to_file =
    infix::wf
    | (Top <<= File)
    | (File <<= Path * Calculation)
    ;
  // clang-format on

  PassDef to_file(const std::filesystem::path& path)
  {
    return {
      "to_file",
      wf_to_file,
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

    if (node->in({Add, Subtract, Multiply, Divide}))
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
      "infix", {to_file(path)}, infix::wf, [](std::ostream& os, Node contents) {
        return write_infix(os, contents);
      }};
  }

  Writer postfix_writer(const std::filesystem::path& path)
  {
    return {
      "postfix",
      {to_file(path)},
      infix::wf,
      [](std::ostream& os, Node contents) {
        return write_postfix(os, contents);
      }};
  }
}

#include "internal.h"

namespace
{
  using namespace trieste;
  using namespace infix;

  // clang-format off
  inline const auto wf_pass_maths = 
    infix::wf 
    | (Assign <<= Ident * Literal) 
    | (Output <<= String * Literal) 
    | (Literal <<= wf_literal)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_cleanup =
    wf_pass_maths 
    | (Calculation <<= Output++) 
    // note the use of >>= here. This allows us to have a choice
    // as a field by giving it a temporary name.
    | (Output <<= String * (Expression >>= wf_literal))
    ;
  // clang-format on  
  
  bool exists(const Node& n)
  {
    return !n->lookup().empty();
  }

  bool can_replace(const Node& n)
  {
    auto defs = n->lookup();
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

        T(Expression) << (T(Ref) << T(Ident)[Id](
          [](auto& n) { return can_replace(n.front()); })) >>
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

        T(Expression) << (T(Ref) << T(Ident)(
          [](auto& n) { return !exists(n.front()); })) >>
          [](Match&) {
            // NB this case shouldn't happen at all
            // during this pass and as such is not
            // an error, but currently occurs during
            // generative testing.
            return Literal << (Int ^ "0");
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

        T(Literal) << Any[Rhs] >> [](Match& _) { return _(Rhs); },
      
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
}

namespace infix
{
  Rewriter calculate()
  {
    return {"calculate", {maths(), cleanup()}, infix::wf};
  }
}

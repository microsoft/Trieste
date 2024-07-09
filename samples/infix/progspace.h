#pragma once

#include "bfs.h"
#include "infix.h"

#include <string_view>

namespace progspace
{
  using namespace std::string_view_literals;
  using namespace infix;

  using R = bfs::Result<trieste::Node>;
  using Env = std::set<std::string>;
  using RP = bfs::Result<std::pair<trieste::Node, Env>>;

  inline R valid_expression(Env env, int depth)
  {
    if (depth == 0)
    {
      return R(Expression << (Int ^ "1"))
        .concat([env]() {
          R ref{};
          for (auto name : env)
          {
            ref = ref.concat(
              [name]() { return R(Expression << (Ref << (Ident ^ name))); });
          }
          return ref;
        })
        .concat([]() { return R(Expression << (Tuple ^ "")); })
        .concat([]() { return R(Expression << (Append ^ "")); });
    }
    else
    {
      auto sub_expr = valid_expression(env, depth - 1);
      return (sub_expr.flat_map<trieste::Node>([sub_expr](auto lhs) {
        return R(Expression << (Tuple << lhs))
          .concat(R(Expression << (Append << lhs->clone())))
          .concat([sub_expr, lhs]() {
            return sub_expr.flat_map<trieste::Node>([lhs](auto rhs) {
              // note: we add fake locations to binops, because the
              // writer assumes their location is also their lexical
              // representation
              return R(Expression
                       << ((Add ^ "+") << lhs->clone() << rhs->clone()))
                .concat(
                  R(Expression
                    << ((Subtract ^ "-") << lhs->clone() << rhs->clone())))
                .concat(
                  R(Expression
                    << ((Multiply ^ "*") << lhs->clone() << rhs->clone())))
                .concat(
                  R(Expression
                    << ((Divide ^ "/") << lhs->clone() << rhs->clone())))
                .concat(
                  R(Expression << (Tuple << lhs->clone() << rhs->clone())))
                .concat(
                  R(Expression << (Append << lhs->clone() << rhs->clone())))
                .concat(
                  R(Expression
                    << ((TupleIdx ^ ".") << lhs->clone() << rhs->clone())));
            });
          });
      }));
    }
  }

  inline R valid_assignment(Env env, std::string name, int depth)
  {
    return valid_expression(env, depth)
      .flat_map<trieste::Node>([name](auto value) {
        return R(Assign << (Ident ^ name) << value->clone());
      });
  }

  inline R valid_calculation(int op_count, int depth)
  {
    RP assigns = RP({
      Calculation << (Assign << (Ident ^ "foo") << (Expression << (Int ^ "1"))),
      {"foo"},
    });
    std::array valid_names = {"bar", "ping", "bnorg"};
    assert(op_count < int(valid_names.size()));
    for (int i = 0; i < op_count; ++i)
    {
      auto name = valid_names[i];
      assigns = assigns.flat_map<std::pair<trieste::Node, Env>>(
        [depth, name](auto pair) {
          // could have used destructuring assignment, but this works poorly
          // with lambda captures on some builds
          auto calculation = pair.first;
          auto env = pair.second;
          auto env_post = env;
          env_post.insert(name);
          return valid_assignment(env, name, depth)
            .template map<std::pair<trieste::Node, Env>>([=](auto assign) {
              return std::pair{calculation->clone() << assign, env_post};
            });
        });
    }
    return assigns.map<trieste::Node>([](auto pair) { return pair.first; });
  }

  struct CSData
  {
    bfs::CatString str;
    bool tuple_parens_omitted;

    CSData(std::string_view str_) : CSData{bfs::CatString(str_)} {}

    CSData(std::string str_) : CSData{bfs::CatString(str_)} {}

    CSData(bfs::CatString str_) : CSData{str_, false} {}

    CSData(bfs::CatString str_, bool tuple_parens_omitted_)
    : str{str_}, tuple_parens_omitted{tuple_parens_omitted_}
    {}

    CSData parens_omitted() const
    {
      return {str, true};
    }

    CSData concat(CSData other) const
    {
      return {
        str.concat(other.str),
        tuple_parens_omitted || other.tuple_parens_omitted,
      };
    }
  };

  using CS = bfs::Result<CSData>;

  inline CS cat_cs(CS lhs, CS rhs)
  {
    return lhs.flat_map<CSData>([=](auto prefix) {
      return rhs.map<CSData>(
        [=](auto suffix) { return prefix.concat(suffix); });
    });
  }

  inline CS cat_css(std::initializer_list<CS> css)
  {
    CS result = CS{""sv};
    for (auto elem : css)
    {
      result = cat_cs(result, elem);
    }
    return result;
  }

  struct GroupPrecedence
  {
    int curr_precedence = -4;
    bool allow_assoc = false;

    GroupPrecedence with_precedence(int precedence) const
    {
      return {precedence, allow_assoc};
    }

    GroupPrecedence with_assoc(bool allow_assoc_) const
    {
      return {curr_precedence, allow_assoc_};
    }

    template<typename Fn>
    CS wrap_group(int precedence, Fn fn) const
    {
      auto grouped = cat_css({
        CS{"("sv},
        fn(GroupPrecedence{precedence}),
        CS{")"sv},
      });

      if (
        (precedence >= curr_precedence && allow_assoc) ||
        (precedence > curr_precedence))
      {
        return fn(with_precedence(precedence).with_assoc(false))
          .concat(grouped);
      }
      else
      {
        return grouped;
      }
    }
  };

  inline CS expression_strings(GroupPrecedence precedence, Node expression)
  {
    assert(expression == Expression);
    assert(expression->size() == 1);
    expression = expression->front();

    if (expression == Ref)
    {
      assert(expression->size() == 1);
      expression = expression->front();
    }

    if (expression->in({Int, Float, String, Ident}))
    {
      return CS{std::string(expression->location().view())};
    }

    if (expression->in({TupleIdx, Multiply, Divide, Add, Subtract}))
    {
      assert(expression->size() == 2);

      auto binop_wrap_precedence = [&](int level) {
        return precedence.wrap_group(level, [&](GroupPrecedence precedence_) {
          return cat_css({
            expression_strings(
              precedence_.with_assoc(true), expression->front()),
            CS{" " + std::string(expression->location().view()) + " "},
            expression_strings(
              precedence_.with_assoc(false), expression->back()),
          });
        });
      };

      if (expression == TupleIdx)
      {
        return binop_wrap_precedence(0);
      }
      else if (expression->in({Multiply, Divide}))
      {
        return binop_wrap_precedence(-1);
      }
      else if (expression->in({Add, Subtract}))
      {
        return binop_wrap_precedence(-2);
      }
      else
      {
        assert(false);
      }
    }

    // code for both tuple literals and append(...)
    auto comma_sep_children = [=](GroupPrecedence precedence_) {
      auto result = CS{""sv};
      bool first = true;
      for (auto child : *expression)
      {
        if (first)
        {
          first = false;
        }
        else
        {
          result = cat_cs(result, CS{", "sv});
        }

        result = cat_cs(result, expression_strings(precedence_, child));
      }

      if (expression->size() < 2)
      {
        result = cat_cs(result, CS{","sv});
      }
      else
      {
        // optional trailing comma (except sizes 0 and 1, where it is
        // mandatory)
        result = result.concat([=]() { return cat_cs(result, CS{","sv}); });
      }
      return result;
    };

    if (expression == Tuple)
    {
      // track whether this tuple needs or may have parens
      bfs::Result<bool> parens_omitted;
      // size 0 or 1 tuples must have ()
      if (
        expression->size() > 1 &&
        ((-3 >= precedence.curr_precedence && precedence.allow_assoc) ||
         (-3 > precedence.curr_precedence)))
      {
        parens_omitted = bfs::Result(true).concat(false);
      }
      else
      {
        parens_omitted = false;
      }
      return parens_omitted.flat_map<CSData>([=](bool parens_omitted_) {
        auto result =
          comma_sep_children(precedence.with_precedence(-3).with_assoc(false));

        if (parens_omitted_)
        {
          return result.map<CSData>(
            [](CSData result_) { return result_.parens_omitted(); });
        }
        else
        {
          return cat_css({
            CS{"("sv},
            result,
            CS{")"sv},
          });
        }
      });
    }

    if (expression == Append)
    {
      return cat_css({
        CS{"append("sv},
        comma_sep_children(precedence.with_precedence(-3).with_assoc(false)),
        CS{")"sv},
      });
    }

    return CS{"<unknown: " + expression->str() + ">"};
  }

  inline CS assign_strings(Node assign)
  {
    assert(assign == Assign);
    assert(assign->size() == 2);
    assert(assign->front() == Ident);
    assert(assign->back() == Expression);

    return cat_css({
      CS{std::string(assign->front()->location().view())},
      CS{" = "sv},
      expression_strings(GroupPrecedence{}, assign->back()),
      CS{";"sv},
    });
  }

  inline CS calculation_strings(Node calculation)
  {
    assert(calculation == Calculation);

    auto result = CS{""sv};
    for (Node child : *calculation)
    {
      result = cat_cs(result, assign_strings(child));
    }
    return result;
  }
}

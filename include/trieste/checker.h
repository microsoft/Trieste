// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "token.h"
#include "trieste.h"
#include "wf.h"

namespace trieste
{
  using namespace detail;

  class Checker
  {
  private:
    std::vector<Pass> passes_;
    const wf::Wellformed* input_wf_;
    size_t start_index_;
    size_t end_index_;

    bool check_wf_ = false;
    std::set<Token> ignored_tokens_;

    static void comma_separate_tokens(Node pattern, std::stringstream& ss)
    {
      bool first = true;
      for (auto& token_node : *pattern)
      {
        if (!first)
          ss << ", ";
        Location loc = token_node->location();
        ss << loc.view();
        first = false;
      }
    }

    // Convert a pattern to a string for error messages.
    static std::string pattern_to_string(Node pattern)
    {
      if (pattern == Top)
        pattern = pattern / Group;

      std::stringstream ss;
      if (pattern == reified::First)
      {
        ss << "Start";
      }
      else if (pattern == reified::Last)
      {
        ss << "End";
      }
      else if (pattern == reified::Any)
      {
        ss << "Any";
      }
      else if (pattern == reified::TokenMatch)
      {
        ss << "T(";
        comma_separate_tokens(pattern, ss);
        ss << ")";
      }
      else if (pattern == reified::RegexMatch)
      {
        auto token_node = pattern / reified::Token;
        Location loc = token_node->location();
        std::string regex =
          std::string((pattern / reified::Regex)->location().view());
        ss << "T(" << loc.view() << ", \"" << regex << "\")";
      }
      else if (pattern == reified::Cap)
      {
        std::string name =
          std::string((pattern / reified::Token)->location().view());
        ss << "(" << pattern_to_string(pattern / Group) << ")[" << name << "]";
      }
      else if (pattern == reified::Opt)
      {
        ss << "~(" << pattern_to_string(pattern / Group) << ")";
      }
      else if (pattern == reified::Rep)
      {
        ss << "(" << pattern_to_string(pattern / Group) << ")++";
      }
      else if (pattern == reified::Not)
      {
        ss << "!(" << pattern_to_string(pattern / Group) << ")";
      }
      else if (pattern == reified::Choice)
      {
        ss << "(" << pattern_to_string(pattern / reified::First) << ") / ("
           << pattern_to_string(pattern / reified::Last) << ")";
      }
      else if (pattern == reified::InsideStar)
      {
        ss << "In(";
        comma_separate_tokens(pattern, ss);
        ss << ")++";
      }
      else if (pattern == reified::Inside)
      {
        ss << "In(";
        comma_separate_tokens(pattern, ss);
        ss << ")";
      }
      else if (pattern == reified::Children)
      {
        ss << "(" << pattern_to_string(pattern / Group) << ") << ("
           << pattern_to_string(pattern / reified::Children) << ")";
      }
      else if (pattern == reified::Pred)
      {
        ss << "++(" << pattern_to_string(pattern / Group) << ")";
      }
      else if (pattern == reified::NegPred)
      {
        ss << "--(" << pattern_to_string(pattern / Group) << ")";
      }
      else if (pattern == reified::Action)
      {
        ss << "((" << pattern_to_string(pattern / Group)
           << ")(<unknown lambda>))";
      }
      else // Group
      {
        bool first = true;
        for (auto& child : *pattern)
        {
          if (!first)
            ss << " * ";
          ss << pattern_to_string(child);
          first = false;
        }
      }
      return ss.str();
    }

    // The multiplicity of a pattern is the expected number of nodes it matches.
    enum class Multiplicity
    {
      Zero,
      One,
      Unknown
    };

    static Multiplicity multiplicity(Node pattern)
    {
      if (pattern->type().in(
            {reified::First,
             reified::Last,
             reified::Inside,
             reified::InsideStar,
             reified::Pred,
             reified::NegPred}))
        return Multiplicity::Zero;

      if (pattern->type().in(
            {reified::Any,
             reified::RegexMatch,
             reified::TokenMatch,
             reified::Not}))
        return Multiplicity::One;

      if (pattern->type().in({reified::Opt, reified::Rep}))
        return Multiplicity::Unknown;

      if (pattern->type().in(
            {reified::Children, reified::Cap, reified::Action}))
        return multiplicity(pattern / Group);

      if (pattern == reified::Choice)
      {
        auto left = multiplicity(pattern / reified::First);
        auto right = multiplicity(pattern / reified::Last);
        return (left == right) ? left : Multiplicity::Unknown;
      }

      if (pattern == Group)
      {
        Multiplicity sum = Multiplicity::Zero;
        for (auto& child : *pattern)
        {
          auto child_mult = multiplicity(child);
          sum = sum        == Multiplicity::Zero? child_mult:
                child_mult == Multiplicity::Zero? sum:
                Multiplicity::Unknown;
        }
        return sum;
      }

      return Multiplicity::Unknown;
    }

    // Return the list of tokens matched by a pattern of multiplicity 1. If it
    // matches zero or multiple nodes, return an empty vector.
    static std::vector<Token> only_tokens(Node pattern)
    {
      if (pattern == reified::Cap || pattern == reified::Children)
      {
        pattern = pattern / Group;
      }

      if (pattern == reified::TokenMatch)
      {
        std::vector<Token> tokens;
        std::transform(
          pattern->begin(),
          pattern->end(),
          std::back_inserter(tokens),
          [](auto& token_node) {
            Location loc = token_node->location();
            return detail::find_token(loc.view());
          });
        return tokens;
      }
      else if (pattern == reified::RegexMatch)
      {
        auto token_node = pattern / reified::Token;
        Location loc = token_node->location();
        auto token = detail::find_token(loc.view());
        return {token};
      }
      else if (pattern == Group)
      {
        std::vector<Token> tokens;
        for (auto& child : *pattern)
        {
          if (multiplicity(child) == Multiplicity::Zero)
          {
            // Skip 0-multiplicity patterns
          }
          else if (multiplicity(child) == Multiplicity::One && tokens.empty())
          {
            tokens = only_tokens(child);
          }
          else
          {
            // Either multiple 1-multiplicity patterns, or a pattern with
            // multiplicity > 1
            tokens.clear();
            break;
          }
        }
        return tokens;
      }
      return {};
    }

    static bool
    tokens_are_subset(std::vector<Token> subset, std::vector<Token> superset)
    {
      std::sort(subset.begin(), subset.end());
      subset.erase(std::unique(subset.begin(), subset.end()), subset.end());

      std::sort(superset.begin(), superset.end());
      superset.erase(
        std::unique(superset.begin(), superset.end()), superset.end());

      return std::includes(
        superset.begin(), superset.end(), subset.begin(), subset.end());
    }

    static bool tokens_are_subset(std::vector<Token> subset, Node superset)
    {
      if (!superset->type().in(
            {reified::Inside, reified::InsideStar, reified::TokenMatch}))
      {
        throw std::runtime_error(
          "tokens_are_subset called with non-token-matching patterns");
      }

      std::vector<Token> superset_tokens;
      std::transform(
        superset->begin(),
        superset->end(),
        std::back_inserter(superset_tokens),
        [](auto& token_node) {
          return detail::find_token(token_node->location().view());
        });

      return tokens_are_subset(subset, superset_tokens);
    }

    static bool tokens_are_subset(Node subset, Node superset)
    {
      if (
        !subset->type().in(
          {reified::Inside, reified::InsideStar, reified::TokenMatch}) ||
        !superset->type().in(
          {reified::Inside, reified::InsideStar, reified::TokenMatch}))
      {
        throw std::runtime_error(
          "tokens_are_subset called with non-token-matching patterns");
      }

      std::vector<Token> subset_tokens;
      std::transform(
        subset->begin(),
        subset->end(),
        std::back_inserter(subset_tokens),
        [](auto& token_node) {
          return detail::find_token(token_node->location().view());
        });

      std::vector<Token> superset_tokens;
      std::transform(
        superset->begin(),
        superset->end(),
        std::back_inserter(superset_tokens),
        [](auto& token_node) {
          return detail::find_token(token_node->location().view());
        });
      return tokens_are_subset(subset_tokens, superset_tokens);
    }

    // This is used to traverse a pattern tree in a depth-first manner.
    struct StackedIterator {
      std::vector<std::tuple<Node, NodeIt>> stack;

      StackedIterator(Node root)
      {
        push(root);
      }

      bool empty() const
      {
        return stack.empty();
      }

      void push(Node node)
      {
        stack.push_back({node, node->begin()});
      }

      void pop()
      {
        stack.pop_back();
      }

      Node operator*()
      {
        return *std::get<1>(stack.back());
      }

      void operator++()
      {
        bool repeat;
        do {
          repeat = false;
          auto& [node_group, it] = stack.back();
          ++it;
          if (it == node_group->end())
          {
            pop();
            repeat = true;
          }
         } while (!empty() && repeat);
      }
    };

    // Check if a pattern is prefix of another, i.e. if it will match if the
    // longer pattern matches. This function does not aim to be complete but
    // should catch many cases.
    static bool includes_prefix(Node prefix, Node pattern)
    {
      // Assume patterns are Groups
      if (prefix != Group || pattern != Group)
        return false;

      StackedIterator prefix_it(prefix);
      StackedIterator pattern_it(pattern);
      while (!prefix_it.empty() && !pattern_it.empty())
      {
        auto prefix_node = *prefix_it;
        auto pattern_node = *pattern_it;

        if (prefix_node == reified::Cap)
        {
          prefix_it.push(prefix_node / Group);
          continue;
        }

        if (pattern_node == reified::Cap)
        {
          pattern_it.push(pattern_node / Group);
          continue;
        }

        if (
          prefix_node == reified::Inside || prefix_node == reified::InsideStar)
        {
          // Assume In appears in the same position in both patterns
          if (pattern_node->type() != prefix_node->type())
            return false;

          if (!tokens_are_subset(pattern_node, prefix_node))
            return false;
        }
        else if (prefix_node == reified::First || prefix_node == reified::Last)
        {
          // If the prefix is First or Last, the pattern must be the same
          if (pattern_node != prefix_node)
            return false;
        }
        else if (pattern_node->type().in(
                   {reified::Inside,
                    reified::InsideStar,
                    reified::First,
                    reified::Last}))
        {
          // If pattern is In, First or Last, the Prefix could be more general
          ++pattern_it;
          continue;
        }
        else if (prefix_node == reified::TokenMatch)
        {
          auto tokens = only_tokens(pattern_node);
          if (tokens.empty() || !tokens_are_subset(tokens, prefix_node))
            return false;
        }
        else if (prefix_node == reified::Children)
        {
          if (pattern_node->type() != reified::Children)
            return false;

          if (!includes_prefix(prefix_node / Group, pattern_node / Group))
            return false;

          if (!includes_prefix(
                prefix_node / reified::Children,
                pattern_node / reified::Children))
            return false;
        }
        else if (prefix_node == reified::Any)
        {
          // Any matches any one thing
          while (multiplicity(pattern_node) == Multiplicity::Zero)
          {
            ++pattern_it;
            if (pattern_it.empty())
              continue;
            pattern_node = *pattern_it;
          }

          if (multiplicity(pattern_node) != Multiplicity::One)
            return false;
        }
        else if (prefix_node == reified::Rep)
        {
          // Require repetition to be equivalent
          if (pattern_node != reified::Rep)
            return false;

          if (
            !includes_prefix(prefix_node / Group, pattern_node / Group) ||
            !includes_prefix(pattern_node / Group, prefix_node / Group))
            return false;
        }
        else if (prefix_node == reified::Opt)
        {
          // Require optional patterns to be equivalent
          if (pattern_node != reified::Opt)
            return false;

          if (
            !includes_prefix(prefix_node / Group, pattern_node / Group) ||
            !includes_prefix(pattern_node / Group, prefix_node / Group))
            return false;
        }
        else
        {
          // Unhandled pattern type in prefix. Assume no match.
          return false;
        }

        ++prefix_it;
        ++pattern_it;
      }
      return prefix_it.empty();
    }

    // Check a reified pattern for common bugs.
    static PassDef check_pattern()
    {
      return {
        "check_pattern",
        reified::pattern_wf,
        dir::topdown | dir::once,
        {
          In(reified::Pred, reified::NegPred)++ *
              T(reified::Cap)[reified::Cap] >>
            [](auto& _) -> Node {
            return Error << (ErrorAst << _(reified::Cap))
                         << (ErrorMsg ^
                             "Cannot have capture patterns inside predicates");
          },

          In(reified::Not)++* T(reified::Cap)[reified::Cap] >>
            [](auto& _) -> Node {
            return Error << (ErrorAst << _(reified::Cap))
                         << (ErrorMsg ^
                             "Cannot have capture patterns inside a negation");
          },

          In(reified::Rep)++* T(reified::Cap)[reified::Cap] >>
            [](auto& _) -> Node {
            return Error
              << (ErrorAst << _(reified::Cap))
              << (ErrorMsg ^
                  "Cannot have capture patterns inside a repetition");
          },

          T(reified::Rep) << T(Group)[Group] >> [](auto& _) -> Node {
            if (multiplicity(_(Group)) == Multiplicity::Zero)
            {
              return Error << (ErrorAst << _(Group))
                           << (ErrorMsg ^
                               ("Pattern '" + pattern_to_string(_(Group)) +
                                "' would be infinitely repeated"));
            }
            return NoChange;
          },

          T(reified::Last) * Any >> [](auto& _) -> Node {
            return Error << (ErrorAst << _(reified::Last))
                         << (ErrorMsg ^ "Cannot have pattern after 'End'");
          },

          T(reified::Cap) << T(Group)[Group] >> [](auto& _) -> Node {
            auto captured_pattern = _(Group);
            if (multiplicity(captured_pattern) == Multiplicity::Zero)
            {
              return Error << (ErrorAst << captured_pattern)
                           << (ErrorMsg ^
                               ("Capture group '" +
                                pattern_to_string(captured_pattern) +
                                "' is always empty"));
            }
            return NoChange;
          },

          T(reified::Children)
              << (T(Group)[Group] * T(Group)[reified::Children]) >>
            [](auto& _) -> Node {
            auto parent_pattern = _(Group);
            auto child_pattern = _(reified::Children);
            auto mult = multiplicity(parent_pattern);
            if (mult != Multiplicity::One)
            {
              return Error << (ErrorAst << parent_pattern)
                           << (ErrorMsg ^
                               ("Parent pattern '" +
                                pattern_to_string(parent_pattern) +
                                "' should match exactly one node"));
            }
            return NoChange;
          },

          T(reified::Not) << T(Group)[Group] >> [](auto& _) -> Node {
            if (multiplicity(_(Group)) != Multiplicity::One)
            {
              return Error << (ErrorAst << _(Group))
                           << (ErrorMsg ^
                               ("Negated pattern '" +
                                pattern_to_string(_(Group)) +
                                "' should match exactly one node. "
                                "Consider using negative lookahead instead."));
            }
            return NoChange;
          },

          // Matching on internal tokens is not allowed.
          In(reified::TokenMatch, reified::Regex) *
              T(reified::Token)[reified::Token] >>
            [](auto& _) -> Node {
            Location loc = _(reified::Token)->location();
            auto token = detail::find_token(loc.view());
            if (token.def->has(flag::internal))
            {
              return Error << (ErrorAst << _(reified::Token))
                           << (ErrorMsg ^ "Cannot match on internal tokens");
            }
            return NoChange;
          },
        }};
    }

    static bool token_appears_in_wf(wf::Wellformed& wf, Token token)
    {
      for (auto& [_, shape] : wf.shapes)
      {
        if (std::holds_alternative<wf::Fields>(shape))
        {
          auto& fields = std::get<wf::Fields>(shape);
          for (auto& field : fields.fields)
          {
            for (auto& t : field.choice.types)
            {
              if (t == token)
                return true;
            }
          }
        }
        else
        {
          auto& sequence = std::get<wf::Sequence>(shape);
          for (auto& t : sequence.choice.types)
          {
            if (t == token)
              return true;
          }
        }
      }
      return false;
    }

    // Check if all tokens mentioned in a pattern can appear according to
    // well-formedness rules.
    PassDef check_that_tokens_exist(
      wf::Wellformed& prev_wf,
      wf::Wellformed& result_wf,
      const std::set<Token>& ignored_tokens)
    {
      PassDef wf_check = {
        "check_well_formedness",
        reified::pattern_wf,
        dir::topdown | dir::once,
        {
          In(
            reified::TokenMatch,
            reified::RegexMatch,
            reified::Inside,
            reified::InsideStar) *
              T(reified::Token)[reified::Token] >>
            [&prev_wf, &result_wf, &ignored_tokens](auto& _) -> Node {
            Location loc = _(reified::Token)->location();
            auto token = detail::find_token(loc.view());

            if (
              ignored_tokens.find(token) != ignored_tokens.end() ||
              token_appears_in_wf(prev_wf, token) ||
              token_appears_in_wf(result_wf, token))
              return NoChange;

            return Error
              << (ErrorMsg ^
                  ("Token '" + std::string(token.str()) +
                   "' is not defined in well-formedness rules"));
          },
        }};
      bool check_wf = this->check_wf_;
      wf_check.cond([check_wf](Node) { return check_wf; });

      return wf_check;
    }

  public:
    Checker() {}

    Checker(const std::vector<Pass>& passes, const wf::Wellformed& input_wf)
    : passes_(passes),
      input_wf_(&input_wf),
      start_index_(1),
      end_index_(passes.size()),
      check_wf_(false)
    {}

    Checker(const Reader& reader)
    : Checker(reader.passes(), reader.parser().wf())
    {}

    Checker(const Writer& writer) : Checker(writer.passes(), writer.input_wf())
    {}

    Checker(const Rewriter& rewriter)
    : Checker(rewriter.passes(), rewriter.input_wf())
    {}

    size_t start_index() const
    {
      return start_index_;
    }

    Checker& start_index(size_t index)
    {
      start_index_ = index;
      return *this;
    }

    size_t end_index() const
    {
      return end_index_;
    }

    Checker& end_index(size_t index)
    {
      end_index_ = index;
      return *this;
    }

    bool check_against_wf() const
    {
      return check_wf_;
    }

    Checker& check_against_wf(bool value)
    {
      check_wf_ = value;
      return *this;
    }

    Checker& ignored_tokens(const std::vector<std::string>& tokens)
    {
      for (auto& token_str : tokens)
      {
        auto token = detail::find_token(token_str);
        if (token == Invalid)
        {
          logging::Error() << "Unknown token '" << token_str << "'";
          continue;
        }
        ignored_tokens_.insert(token);
      }
      return *this;
    }

    std::set<Token> ignored_tokens() const
    {
      return ignored_tokens_;
    }

    int check()
    {
      WFContext context;
      context.push_back(reified::pattern_wf);
      logging::Output() << "Checking patterns" << std::endl;

      int ret = 0;

      for (size_t index = start_index_; index <= end_index_; index++)
      {
        auto& pass = passes_.at(index - 1);
        logging::Info() << "Checking pass: " << pass->name() << std::endl;

        auto prev_wf = index == 1 ? *input_wf_ : passes_.at(index - 2)->wf();
        auto result_wf = passes_.at(index - 1)->wf();
        auto checker = Rewriter(
          "pattern checker",
          {check_pattern(),
           check_that_tokens_exist(prev_wf, result_wf, ignored_tokens_)},
          reified::pattern_wf);

        auto patterns = pass->reify_patterns();

        // Check for malformed patterns
        for (auto& pattern : patterns)
        {
          auto ok = reified::pattern_wf.check(pattern);
          if (!ok)
          {
            logging::Error err;
            err << "============" << std::endl
                << "Pass: " << pass->name() << std::endl
                << "------------" << std::endl
                << "Pattern does not conform to well-formedness rules:"
                << std::endl
                << pattern->str() << "------------" << std::endl
                << "This is most likely a bug in Trieste. Please report it."
                << std::endl;
            ret = 1;
            continue;
          }

          auto orig = pattern->clone();
          auto result = checker.rewrite(pattern);
          if (!result.ok)
          {
            logging::Error err;
            err << "------------" << std::endl
                << "Pass: " << pass->name() << std::endl
                << "------------" << std::endl
                << "Found bad pattern: " << std::endl
                << pattern_to_string(orig) << std::endl
                << "------------" << std::endl;
            result.print_errors(err);
            ret = 1;
          }
        }

        // Check for unreachable patterns
        auto prefix_it = patterns.begin();
        while (prefix_it != patterns.end())
        {
          auto pattern_it = prefix_it + 1;
          while (pattern_it != patterns.end())
          {
            if (includes_prefix(*prefix_it / Group, *pattern_it / Group))
            {
              logging::Error err;
              err << "------------" << std::endl
                  << "Pass: " << pass->name() << std::endl
                  << "------------" << std::endl
                  << "Unreachable pattern:" << std::endl
                  << pattern_to_string(*pattern_it) << std::endl
                  << std::endl
                  << "Pattern is shadowed by earlier pattern:" << std::endl
                  << pattern_to_string(*prefix_it) << std::endl
                  << "------------" << std::endl;
              ret = 1;
            }
            ++pattern_it;
          }
          ++prefix_it;
        }
      }
      context.pop_front();
      return ret;
    }
  };
}

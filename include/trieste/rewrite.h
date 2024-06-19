// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "debug.h"
#include "regex.h"
#include "token.h"

#include <array>
#include <cassert>
#include <functional>
#include <optional>
#include <snmalloc/ds_core/defines.h>

namespace trieste
{
  class PassDef;

  class Match
  {
  private:
    size_t index{0};
    std::vector<std::pair<bool, std::map<Token, NodeRange>>> captures{16};

  public:
    Match() {}
    Match(const Match&) = delete;

    Location fresh(const Location& prefix = {})
    {
      return ast::fresh(prefix);
    }

    const NodeRange& operator[](const Token& token)
    {
      static const NodeRange empty;

      for (size_t i = index;; i--)
      {
        const auto& [valid, map] = captures[i];
        if (valid)
        {
          auto it = map.find(token);
          if ((it != map.end()))
          {
            return it->second;
          }
        }
        if (i == 0)
          break;
      }

      return empty;
    }

    void set(const Token& token, const NodeRange& range)
    {
      auto& [valid, map] = captures[index];
      if (!valid)
      {
        map.clear();
        valid = true;
      }

      map[token] = range;
    }

    Node operator()(const Token& token)
    {
      for (size_t i = index;; i--)
      {
        const auto& [valid, map] = captures[i];
        if (valid)
        {
          auto it = map.find(token);
          if ((it != map.end()) && it->second.front())
          {
            return it->second.front();
          }
        }
        if (i == 0)
          break;
      }
      return nullptr;
    }

    SNMALLOC_FAST_PATH size_t add_frame()
    {
      index++;
      if (SNMALLOC_UNLIKELY(captures.size() == (size_t)index))
      {
        captures.resize(index * 2);
      }
      else
      {
        captures[index].first = false;
      }
      return index - 1;
    }

    SNMALLOC_FAST_PATH void return_to_frame(size_t new_index)
    {
      index = new_index;
    }

    SNMALLOC_FAST_PATH void reset()
    {
      index = 0;
      captures[0].first = false;
    }
  };

  namespace detail
  {
    /**
     * FastPattern tracks a quickly checkable pattern for whether a parent and
     * start node could be satisfied be a possible match.
     */
    class FastPattern
    {
      /**
       * Here are a few patterns and what they are in FastPattern:
       *
       * T(foo)        -> FastPattern({foo}, {}, false)
       *   There is no pass through, and the first can only be a `foo`.
       * Opt(T(foo))  -> FastPattern({foo}, {}, true)
       *   As this is optional, it is a pass through, and the first can be a
       * `foo` or whatever the continuation allows. Opt(T(foo)) * T(bar) ->
       * FastPattern({foo,bar}, {}, false) This can start with foo or bar, and
       * has no pass-through.
       *
       * In(foo)     -> FastPattern({}, {foo}, true)
       *   This can start with any token, and must have a parent of foo.
       * In(foo) * T(bar) -> FastPattern({bar}, {foo}, false)
       *   This can start with bar, and must have a parent of foo.
       * In(foo) / In(bar) -> FastPattern({}, {foo,bar}, true)
       *  This can start with any token, and must have a parent of foo or bar.
       */

      // Empty set means any first token can be consumed by this pattern,
      // however, if pass_through is true, then we treat it as consuming no
      // tokens.  Predicates will typically set starts to {} and pass_through to
      // true.
      std::set<Token> starts;

      // Empty set means match any parent.
      std::set<Token> parents;

      // True, if pattern can consume nothing, and hence the continuation
      // can also consume the first token.
      bool pass_through;

      FastPattern(
        std::set<Token> first_, std::set<Token> parent_, bool pass_through_)
      : starts(first_), parents(parent_), pass_through(pass_through_)
      {}

      bool any_first() const
      {
        return starts.empty() && !pass_through;
      }

    public:
      static FastPattern match_any()
      {
        return FastPattern({}, {}, false);
      }

      static FastPattern match_pred()
      {
        return FastPattern({}, {}, true);
      }

      static FastPattern match_token(std::set<Token> token)
      {
        return FastPattern(token, {}, false);
      }

      static FastPattern match_parent(std::set<Token> token)
      {
        return FastPattern({}, token, true);
      }

      static FastPattern
      match_choice(const FastPattern& lhs, const FastPattern& rhs)
      {
        bool new_pass_through = lhs.pass_through || rhs.pass_through;
        std::set<Token> new_first;
        // any_first is annihilator for choice, so special cases required.
        // otherwise, we treat it as union
        if (!rhs.any_first() && !lhs.any_first())
        {
          new_first = lhs.starts;
          new_first.insert(rhs.starts.begin(), rhs.starts.end());
        }
        else
        {
          // Any first is true of one disjunct, so set pass_through to
          // false, and first to empty, to continue the any_first property.
          new_pass_through = false;
        }

        std::set<Token> new_parent;
        // Empty is treat as universal parent, so maintain universal
        // otherwise, we treat it as union
        if (!rhs.parents.empty() && !lhs.parents.empty())
        {
          new_parent = lhs.parents;
          new_parent.insert(rhs.parents.begin(), rhs.parents.end());
        }

        return FastPattern(new_first, new_parent, new_pass_through);
      }

      static FastPattern
      match_seq(const FastPattern& lhs, const FastPattern& rhs)
      {
        std::set<Token> new_first;
        bool new_pass_through = false;
        if (lhs.pass_through)
        {
          if (rhs.any_first())
          {
            // Pass through followed by an annihilator is an annihilator.
            // Set pass through to false, and new_first to empty,
            // as this can accept any first token.
            new_pass_through = false;
          }
          else
          {
            // Union starts.
            new_first = lhs.starts;
            new_first.insert(rhs.starts.begin(), rhs.starts.end());
          }
        }
        else
        {
          // Ignore right hand side if not a pass through.
          new_first = lhs.starts;
        }

        std::set<Token> new_parent;
        // Perform intersection
        // empty is universal, so special cases required.
        if (lhs.parents.empty())
        {
          new_parent = rhs.parents;
        }
        else if (rhs.parents.empty())
        {
          new_parent = lhs.parents;
        }
        else
        {
          new_parent = lhs.parents;
          for (auto it = new_parent.begin(); it != new_parent.end();)
          {
            if (rhs.parents.find(*it) == rhs.parents.end())
            {
              it = new_parent.erase(it);
            }
            else
            {
              ++it;
            }
          }
        }

        return FastPattern(new_first, new_parent, new_pass_through);
      }

      static FastPattern match_opt(const FastPattern& pattern)
      {
        if (pattern.any_first())
          return pattern;

        return FastPattern(pattern.starts, {}, true);
      }

      const std::set<Token>& get_starts() const
      {
        return starts;
      }

      const std::set<Token>& get_parents() const
      {
        return parents;
      }
    };

    class PatternDef;
    using PatternPtr = std::shared_ptr<PatternDef>;

    class PatternDef
    {
      PatternPtr continuation{};

    public:
      virtual ~PatternDef() = default;

      virtual PatternPtr custom_rep()
      {
        return {};
      }

      virtual bool has_captures_local() const&
      {
        return false;
      }

      bool has_captures() const&
      {
        return has_captures_local() ||
          (continuation && continuation->has_captures());
      }

      PatternDef(const PatternDef& copy)
      {
        if (copy.continuation)
        {
          continuation = copy.continuation->clone();
        }
      }

      PatternDef() = default;

      virtual bool match(NodeIt&, const Node&, Match&) const& = 0;

      virtual PatternPtr clone() const& = 0;

      void set_continuation(PatternPtr next)
      {
        if (!continuation)
        {
          continuation = next;
        }
        else
        {
          continuation->set_continuation(next);
        }
      }

      SNMALLOC_FAST_PATH bool
      match_continuation(NodeIt& it, const Node& parent, Match& match) const&
      {
        if (!continuation)
          return true;
        return continuation->match(it, parent, match);
      }

      bool no_continuation() const&
      {
        return !continuation;
      }

      // If this pattern is only a TokenMatch node then return the tokens
      // Otherwise, return an empty vector
      virtual std::vector<Token> only_tokens() const
      {
        return {};
      }
    };

    using PatternPtr = std::shared_ptr<PatternDef>;

    class Cap : public PatternDef
    {
    private:
      Token name;
      PatternPtr pattern;

    public:
      Cap(const Token& name_, PatternPtr pattern_)
      : name(name_), pattern(pattern_)
      {}

      bool has_captures_local() const& override
      {
        return true;
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Cap>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        auto begin = it;

        if (!pattern->match(it, parent, match))
          return false;

        match.set(name, {begin, it});
        return match_continuation(it, parent, match);
      }
    };

    class Anything : public PatternDef
    {
    public:
      Anything() {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Anything>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        if (it == parent->end())
          return false;

        ++it;

        return match_continuation(it, parent, match);
      }
    };

    class TokenMatch : public PatternDef
    {
    private:
      std::vector<Token> types;

    public:
      TokenMatch(std::vector<Token> types_) : types(types_) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<TokenMatch>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        if (it == parent->end())
          return false;

        for (const auto& t : types)
        {
          if ((*it)->type() == t)
          {
            ++it;
            return match_continuation(it, parent, match);
          }
        }
        return false;
      }

      std::vector<Token> only_tokens() const override
      {
        if (no_continuation())
        {
          return types;
        }
        return {};
      }
    };

    class RegexMatch : public PatternDef
    {
    private:
      Token type;
      std::shared_ptr<RE2> regex;

    public:
      RegexMatch(const Token& type_, const std::string& re)
      : type(type_), regex(std::make_shared<RE2>(re))
      {}

      PatternPtr clone() const& override
      {
        return std::make_shared<RegexMatch>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        if ((it == parent->end()) || ((*it)->type() != type))
          return false;

        if (!RE2::FullMatch((*it)->location().view(), *regex))
          return false;

        ++it;
        return match_continuation(it, parent, match);
      }
    };

    class Opt : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      Opt(PatternPtr pattern_) : pattern(pattern_) {}

      bool has_captures_local() const& override
      {
        return pattern->has_captures();
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Opt>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        auto backtrack_it = it;
        auto backtrack_frame = match.add_frame();
        if (!pattern->match(it, parent, match))
        {
          it = backtrack_it;
          match.return_to_frame(backtrack_frame);
        }
        return match_continuation(it, parent, match);
      }
    };

    class Rep : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      Rep(PatternPtr pattern_) : pattern(pattern_)
      {
        if (pattern->has_captures())
          throw std::runtime_error(
            "Captures not allowed inside iteration (Pattern++)!");
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Rep>(*this);
      }

      PatternPtr custom_rep() override
      {
        // Rep(Rep(P)) -> Rep(P)
        if (no_continuation())
          return clone();
        return {};
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        NodeIt curr = it;
        auto end = parent->end();
        while ((it != end) && pattern->match(it, parent, match))
        {
          curr = it;
        }
        // Last match failed so backtrack it.
        it = curr;
        return match_continuation(it, parent, match);
      }
    };

    class Not : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      Not(PatternPtr pattern_) : pattern(pattern_)
      {
        if (pattern->has_captures())
          throw std::runtime_error(
            "Captures not allowed inside Not (~Pattern)!");
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Not>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        if (it == parent->end())
          return false;

        auto begin = it;
        it = begin + 1;

        return !pattern->match(begin, parent, match) &&
          match_continuation(it, parent, match);
      }
    };

    template<bool CapturesLeft>
    class Choice : public PatternDef
    {
    private:
      PatternPtr first;
      PatternPtr second;

    public:
      Choice(PatternPtr first_, PatternPtr second_)
      : first(first_), second(second_)
      {
        if (CapturesLeft != first->has_captures())
          throw std::runtime_error(
            "Static and dynamic view of captures disagree.");
      }

      bool has_captures_local() const& override
      {
        return first->has_captures() || second->has_captures();
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Choice>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        auto backtrack_it = it;
        size_t backtrack_frame;

        if constexpr (CapturesLeft)
          backtrack_frame = match.add_frame();

        if (first->match(it, parent, match))
        {
          return match_continuation(it, parent, match);
        }

        it = backtrack_it;

        if constexpr (CapturesLeft)
          match.return_to_frame(backtrack_frame);
        else
          snmalloc::UNUSED(backtrack_frame);

        return second->match(it, parent, match) &&
          match_continuation(it, parent, match);
      }
    };

    template<size_t N>
    class InsideStar : public PatternDef
    {
    private:
      std::array<Token, N> types;

    public:
      InsideStar(const std::array<Token, N>& types_) : types(types_) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<InsideStar>(*this);
      }

      PatternPtr custom_rep() override
      {
        throw std::runtime_error(
          "Rep(InsideStar) not allowed! ((In(T,...)++)++");
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        NodeDef* p = &*parent;

        while (p)
        {
          for (const auto& type : types)
            if (p->type() == type)
              return match_continuation(it, parent, match);

          p = p->parent();
        }

        return false;
      }
    };

    template<size_t N>
    class Inside : public PatternDef
    {
    private:
      std::array<Token, N> types;

    public:
      Inside(const std::array<Token, N>& types_) : types(types_) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Inside>(*this);
      }

      PatternPtr custom_rep() override
      {
        // Rep(Inside) -> InsideStar
        if (no_continuation())
          return std::make_shared<InsideStar<N>>(types);
        return {};
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        for (const auto& type : types)
        {
          if (parent->type() == type)
            return match_continuation(it, parent, match);
        }

        return false;
      }
    };

    class First : public PatternDef
    {
    public:
      First() {}

      PatternPtr clone() const& override
      {
        return std::make_shared<First>(*this);
      }

      PatternPtr custom_rep() override
      {
        throw std::runtime_error("Rep(First) not allowed! (Start)++");
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        return (it == parent->begin()) && match_continuation(it, parent, match);
      }
    };

    class Last : public PatternDef
    {
    public:
      Last() {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Last>(*this);
      }

      PatternPtr custom_rep() override
      {
        throw std::runtime_error("Rep(Last) not allowed! (End)++");
      }

      bool match(NodeIt& it, const Node& parent, Match&) const& override
      {
        assert(no_continuation());
        return it == parent->end();
      }
    };

    class Children : public PatternDef
    {
    private:
      PatternPtr pattern;
      PatternPtr children;

    public:
      Children(PatternPtr pattern_, PatternPtr children_)
      : pattern(pattern_), children(children_)
      {}

      bool has_captures_local() const& override
      {
        return pattern->has_captures() || children->has_captures();
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Children>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        auto begin = it;

        if (!pattern->match(it, parent, match))
          return false;

        auto it2 = (*begin)->begin();

        if (!children->match(it2, *begin, match))
          return false;

        return match_continuation(it, parent, match);
      }
    };

    class Pred : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      Pred(PatternPtr pattern_) : pattern(pattern_)
      {
        if (pattern->has_captures())
          throw std::runtime_error(
            "Captures not allowed inside Pred (++Pattern)!");
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Pred>(*this);
      }

      PatternPtr custom_rep() override
      {
        throw std::runtime_error("Rep(Pred) not allowed! (++Pattern)++");
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        auto begin = it;
        return pattern->match(begin, parent, match) &&
          match_continuation(it, parent, match);
      }
    };

    class NegPred : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      NegPred(PatternPtr pattern_) : pattern(pattern_)
      {
        if (pattern->has_captures())
          throw std::runtime_error(
            "Captures not allowed inside NegPred (--Pattern)!");
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<NegPred>(*this);
      }

      PatternPtr custom_rep() override
      {
        throw std::runtime_error("Rep(NegPred) not allowed! (--Pattern)++");
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        auto begin = it;
        return !pattern->match(begin, parent, match) &&
          match_continuation(it, parent, match);
      }
    };

    template<typename F>
    class Action : public PatternDef
    {
    private:
      F action;
      PatternPtr pattern;

    public:
      Action(F&& action_, PatternPtr pattern_)
      : action(std::forward<F>(action_)), pattern(pattern_)
      {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Action>(*this);
      }

      bool match(NodeIt& it, const Node& parent, Match& match) const& override
      {
        auto begin = it;

        if (!pattern->match(it, parent, match))
          return false;

        NodeRange range = {begin, it};
        bool result = action(range);
        return result && match_continuation(it, parent, match);
      }
    };

    template<typename T>
    using Effect = std::function<T(Match&)>;

    class Pattern
    {
    private:
      PatternPtr pattern;
      FastPattern fast_pattern;

    public:
      Pattern(PatternPtr pattern_, FastPattern fast_pattern_)
      : pattern(pattern_), fast_pattern(fast_pattern_)
      {}

      bool match(NodeIt& it, const Node& parent, Match& match) const
      {
        return pattern->match(it, parent, match);
      }

      template<typename F>
      Pattern operator()(F&& action) const
      {
        return {
          std::make_shared<Action<F>>(std::forward<F>(action), pattern),
          fast_pattern};
      }

      Pattern operator[](const Token& name) const
      {
        return {std::make_shared<Cap>(name, pattern), fast_pattern};
      }

      Pattern operator~() const
      {
        return {
          std::make_shared<Opt>(pattern), FastPattern::match_opt(fast_pattern)};
      }

      Pattern operator++() const
      {
        return {std::make_shared<Pred>(pattern), FastPattern::match_pred()};
      }

      Pattern operator--() const
      {
        return {std::make_shared<NegPred>(pattern), FastPattern::match_pred()};
      }

      Pattern operator++(int) const
      {
        auto result = pattern->custom_rep();
        if (result)
          // With a custom rep many things can happen.  We overapproximate here.
          // We could do better, but it's not worth the effort.
          return {result, FastPattern::match_any()};

        return {
          std::make_shared<Rep>(pattern), FastPattern::match_opt(fast_pattern)};
      }

      Pattern operator!() const
      {
        return {std::make_shared<Not>(pattern), FastPattern::match_pred()};
      }

      /** match RHS in the context of LHS */
      Pattern operator*(Pattern rhs) const
      {
        auto result = pattern->clone();
        result->set_continuation(rhs.pattern);
        return {result, FastPattern::match_seq(fast_pattern, rhs.fast_pattern)};
      }

      Pattern operator/(Pattern rhs) const
      {
        auto lhs_tokens = pattern->only_tokens();
        auto rhs_tokens = rhs.pattern->only_tokens();
        if (!lhs_tokens.empty() && !rhs_tokens.empty())
        {
          std::vector<Token> tokens;
          tokens.reserve(lhs_tokens.size() + rhs_tokens.size());
          tokens.insert(tokens.end(), lhs_tokens.begin(), lhs_tokens.end());
          tokens.insert(tokens.end(), rhs_tokens.begin(), rhs_tokens.end());
          return {
            std::make_shared<TokenMatch>(tokens),
            FastPattern::match_choice(fast_pattern, rhs.fast_pattern)};
        }

        if (pattern->has_captures())
          return {
            std::make_shared<Choice<true>>(pattern, rhs.pattern),
            FastPattern::match_choice(fast_pattern, rhs.fast_pattern)};
        else
          return {
            std::make_shared<Choice<false>>(pattern, rhs.pattern),
            FastPattern::match_choice(fast_pattern, rhs.fast_pattern)};
      }

      /** within LHS, match RHS */
      Pattern operator<<(Pattern rhs) const
      {
        return {std::make_shared<Children>(pattern, rhs.pattern), fast_pattern};
      }

      const std::set<Token>& get_starts() const
      {
        return fast_pattern.get_starts();
      }

      const std::set<Token>& get_parents() const
      {
        return fast_pattern.get_parents();
      }
    };

    template<typename T>
    using PatternEffect = std::pair<Located<Pattern>, Effect<T>>;

    struct RangeContents
    {
      NodeRange range;
    };

    struct RangeOr
    {
      NodeRange range;
      Node node;
    };

    struct EphemeralNode
    {
      Node node;
    };

    struct EphemeralNodeRange
    {
      NodeRange range;
    };
  }

  template<typename F>
  inline auto operator>>(detail::Located<detail::Pattern> pattern, F effect)
    -> detail::PatternEffect<decltype(effect(std::declval<Match&>()))>
  {
    return {pattern, effect};
  }

  inline const auto Any = detail::Pattern(
    std::make_shared<detail::Anything>(), detail::FastPattern::match_any());
  inline const auto Start = detail::Pattern(
    std::make_shared<detail::First>(), detail::FastPattern::match_pred());
  inline const auto End = detail::Pattern(
    std::make_shared<detail::Last>(), detail::FastPattern::match_pred());

  /** matches the token */
  inline detail::Pattern T(const Token& type)
  {
    std::vector<Token> types = {type};
    return detail::Pattern(
      std::make_shared<detail::TokenMatch>(types),
      detail::FastPattern::match_token({type}));
  }

  /** matches any of the tokens given */
  template<typename... Ts>
  inline detail::Pattern
  T(const Token& type1, const Token& type2, const Ts&... types)
  {
    std::vector<Token> types_ = {type1, type2, types...};
    return detail::Pattern(
      std::make_shared<detail::TokenMatch>(types_),
      detail::FastPattern::match_token({type1, type2, types...}));
  }

  inline detail::Pattern T(const Token& type, const std::string& r)
  {
    return detail::Pattern(
      std::make_shared<detail::RegexMatch>(type, r),
      detail::FastPattern::match_token({type}));
  }

  template<typename... Ts>
  inline detail::Pattern In(const Token& type1, const Ts&... types)
  {
    std::array<Token, 1 + sizeof...(types)> types_ = {type1, types...};
    return detail::Pattern(
      std::make_shared<detail::Inside<1 + sizeof...(types)>>(types_),
      detail::FastPattern::match_parent({type1, types...}));
  }

  inline detail::EphemeralNode operator-(Node node)
  {
    return {node};
  }

  inline detail::EphemeralNodeRange operator-(NodeRange node)
  {
    return {node};
  }

  /** get the contents of the node range */
  inline detail::RangeContents operator*(NodeRange range)
  {
    return {range};
  }

  inline detail::RangeOr operator||(NodeRange range, Node node)
  {
    return {range, node};
  }

  inline Node operator||(Node lhs, Node rhs)
  {
    return lhs ? lhs : rhs;
  }

  /** LHS, but with RHS appended */
  inline Node operator<<(Node node1, Node node2)
  {
    node1->push_back(node2);
    return node1;
  }

  inline Node operator<<(Node node, detail::EphemeralNode ephemeral)
  {
    node->push_back_ephemeral(ephemeral.node);
    return node;
  }

  inline Node operator<<(Node node, NodeRange range)
  {
    node->push_back(range);
    return node;
  }

  inline Node operator<<(Node node, detail::EphemeralNodeRange ephemeral)
  {
    node->push_back_ephemeral(ephemeral.range);
    return node;
  }

  inline Node operator<<(Node node, detail::RangeContents range_contents)
  {
    for (Node& n : range_contents.range)
    {
      node->push_back({n->begin(), n->end()});
    }

    return node;
  }

  inline Node operator<<(Node node, detail::RangeOr range_or)
  {
    if (!range_or.range.empty())
      node->push_back(range_or.range);
    else
      node->push_back(range_or.node);

    return node;
  }

  inline Node operator<<(Node node, Nodes range)
  {
    node->push_back({range.begin(), range.end()});
    return node;
  }

  inline Node operator^(const Token& type, Node node)
  {
    return NodeDef::create(type, node->location());
  }

  inline Node operator^(const Token& type, Location loc)
  {
    return NodeDef::create(type, loc);
  }

  inline Node operator^(const Token& type, const std::string& text)
  {
    return NodeDef::create(type, Location(text));
  }

  inline Node clone(Node node)
  {
    if (node)
      return node->clone();
    else
      return {};
  }

  inline Nodes clone(NodeRange range)
  {
    Nodes nodes;
    nodes.reserve(range.size());

    for (const Node& n : range)
      nodes.push_back(n->clone());

    return nodes;
  }
}

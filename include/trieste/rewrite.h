// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"

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
    Node in_node;
    bool captures_set = false;
    std::map<Token, NodeRange> captures;

  public:
    Match(Node in_node) : in_node(in_node) {}

    Location fresh(const Location& prefix = {})
    {
      return in_node->fresh(prefix);
    }

    NodeRange& operator[](const Token& token)
    {
      return captures[token];
    }

    Node operator()(const Token& token)
    {
      auto it = captures.find(token);
      if ((it != captures.end()) && *it->second.first)
        return *it->second.first;

      return {};
    }

    void operator+=(const Match& that)
    {
      captures_set = true;
      captures.insert(that.captures.begin(), that.captures.end());
    }

    SNMALLOC_FAST_PATH void reset()
    {
      if (captures_set)
      {
        captures.clear();
        captures_set = false;
      }
    }
  };

  namespace detail
  {
    class PatternDef;
    using PatternPtr = std::shared_ptr<PatternDef>;

    class PatternDef
    {
      PatternPtr continuation{};
    public:
      virtual ~PatternDef() = default;

      virtual bool custom_rep()
      {
        return false;
      }

      PatternDef(const PatternDef& copy)
      {
        if (copy.continuation)
        {
          continuation = copy.continuation->clone();
        }
      }

      PatternDef() = default;

      virtual bool match(NodeIt&, const NodeIt&, Match&) const& = 0;

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

      SNMALLOC_FAST_PATH bool match_continuation(NodeIt& it, const NodeIt& end, Match& match) const&
      {
        if (!continuation)
          return true;
        return continuation->match(it, end, match);        
      }

      bool no_continuation() const&
      {
        return !continuation;
      }
    };

    using PatternPtr = std::shared_ptr<PatternDef>;

    class Cap : public PatternDef
    {
    private:
      Token name;
      PatternPtr pattern;

    public:
      Cap(const Token& name, PatternPtr pattern) : name(name), pattern(pattern)
      {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Cap>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto begin = it;

        if (!pattern->match(it, end, match))
          return false;

        match[name] = {begin, it};
        return match_continuation(it, end, match);
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


      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if (it == end)
          return false;
        
        ++it;

        return match_continuation(it, end, match);
      }
    };

    class TokenMatch : public PatternDef
    {
    private:
      Token type;

    public:
      TokenMatch(const Token& type) : type(type) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<TokenMatch>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if ((it == end) || ((*it)->type() != type))
          return false;

        ++it;
        
        return match_continuation(it, end, match); 
      }
    };

    class RegexMatch : public PatternDef
    {
    private:
      Token type;
      std::shared_ptr<RE2> regex;

    public:
      RegexMatch(const Token& type, const std::string& r) : type(type), regex(std::make_shared<RE2>(r))
      {}

      PatternPtr clone() const& override
      {
        return std::make_shared<RegexMatch>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if ((it == end) || ((*it)->type() != type))
          return false;

        if (!RE2::FullMatch((*it)->location().view(), *regex))
          return false;

        ++it;
        return match_continuation(it, end, match); 
      }
    };

    class Opt : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      Opt(PatternPtr pattern) : pattern(pattern) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Opt>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto backtrack_it = it;
        auto backtrack_match = match;
        if (!pattern->match(it, end, match))
        {
          it = backtrack_it;
          match = backtrack_match;
        }
        return match_continuation(it, end, match); 
      }
    };

    class Rep : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      Rep(PatternPtr pattern) : pattern(pattern) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Rep>(*this);
      }

      bool custom_rep() override
      {
        // Rep(Rep(...)) is treated as Rep(...).
        return true;
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        NodeIt curr = it;
        auto match2 = match;
        while ((it != end) && pattern->match(it, end, match))
        {
          curr = it;
          match2 = match;
        }
        // Last match failed so backtrack it.
        it = curr;
        match = match2;
        return match_continuation(it, end, match); 
      }
    };

    class Not : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      Not(PatternPtr pattern) : pattern(pattern) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Not>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if (it == end)
          return false;

        auto match2 = match;
        auto begin = it;
        it = begin + 1;

        return !pattern->match(begin, end, match2) && match_continuation(it, end, match);
      }
    };

    class Choice : public PatternDef
    {
    private:
      PatternPtr first;
      PatternPtr second;

    public:
      Choice(PatternPtr first, PatternPtr second) : first(first), second(second)
      {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Choice>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto backtrack_match = match;
        auto backtrack_it = it;

        if (first->match(it, end, match))
        {
          return match_continuation(it, end, match);
        }

        match = backtrack_match;
        it = backtrack_it;

        return second->match(it, end, match) && match_continuation(it, end, match);
      }
    };

    class Inside : public PatternDef
    {
    private:
      Token type;
      bool any;

    public:
      Inside(const Token& type) : type(type), any(false) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Inside>(*this);
      }

      bool custom_rep() override
      {
        // Rep(Inside) checks for any parent, not just the immediate parent.
        any = true;
        return true;
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if (it == end)
          return false;

        auto p = (*it)->parent();

        while (p)
        {
          if (p == type)
            return match_continuation(it, end, match);

          if (!any)
            break;

          p = p->parent();
        }

        return false;
      }
    };

    class InsideN : public PatternDef
    {
    private:
      std::vector<Token> types;
      bool any;

    public:
      InsideN(const std::vector<Token>& types) : types(types), any(false) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<InsideN>(*this);
      }

      bool custom_rep() override
      {
        // Rep(InsideN) checks for any parent, not just the immediate parent.
        any = true;
        return true;
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if (it == end)
          return false;

        auto p = (*it)->parent();

        while (p)
        {
          if (p->type().in(types))
            return match_continuation(it, end, match);

          if (!any)
            break;

          p = p->parent();
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

      bool custom_rep() override
      {
        // Rep(First) is treated as First.
        return true;
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if (it == end)
          return false;

        auto p = (*it)->parent();
        return p && (it == p->begin()) && match_continuation(it, end, match);
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

      bool custom_rep() override
      {
        // Rep(Last) is treated as Last.
        return true;
      }

      bool match(NodeIt& it, const NodeIt& end, Match&) const& override
      {
        assert(no_continuation());
        return it == end;
      }
    };

    class Children : public PatternDef
    {
    private:
      PatternPtr pattern;
      PatternPtr children;

    public:
      Children(PatternPtr pattern, PatternPtr children)
      : pattern(pattern), children(children)
      {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Children>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto begin = it;

        if (!pattern->match(it, end, match))
          return false;

        auto it2 = (*begin)->begin();
        auto end2 = (*begin)->end();

        if (!children->match(it2, end2, match))
          return false;

        return match_continuation(it, end, match);
      }
    };

    class Pred : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      Pred(PatternPtr pattern) : pattern(pattern) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Pred>(*this);
      }

      bool custom_rep() override
      {
        // Rep(Pred(...)) is treated as Pred(...).
        return true;
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto begin = it;
        auto match2 = match;
        return pattern->match(begin, end, match2) && match_continuation(it, end, match);
      }
    };

    class NegPred : public PatternDef
    {
    private:
      PatternPtr pattern;

    public:
      NegPred(PatternPtr pattern) : pattern(pattern) {}

      PatternPtr clone() const& override
      {
        return std::make_shared<NegPred>(*this);
      }

      bool custom_rep() override
      {
        // Rep(NegPred(...)) is treated as NegPred(...).
        return true;
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto begin = it;
        auto match2 = match;
        return !pattern->match(begin, end, match2) && match_continuation(it, end, match);
      }
    };

    using ActionFn = std::function<bool(const NodeRange&)>;

    class Action : public PatternDef
    {
    private:
      ActionFn action;
      PatternPtr pattern;

    public:
      Action(ActionFn action, PatternPtr pattern)
      : action(action), pattern(pattern)
      {}

      PatternPtr clone() const& override
      {
        return std::make_shared<Action>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto begin = it;

        if (!pattern->match(it, end, match))
          return false;

        return action({begin, it}) && match_continuation(it, end, match);
      }
    };

    class Pattern;

    template<typename T>
    using Effect = std::function<T(Match&)>;

    template<typename T>
    using PatternEffect = std::pair<Pattern, Effect<T>>;

    class Pattern
    {
    private:
      PatternPtr pattern;

    public:
      Pattern(PatternPtr pattern) : pattern(pattern) {}

      bool match(NodeIt& it, const NodeIt& end, Match& match) const
      {
        return pattern->match(it, end, match);
      }

      Pattern operator()(ActionFn action) const
      {
        return {std::make_shared<Action>(action, pattern)};
      }

      Pattern operator[](const Token& name) const
      {
        return {std::make_shared<Cap>(name, pattern)};
      }

      Pattern operator~() const
      {
        return {std::make_shared<Opt>(pattern)};
      }

      Pattern operator++() const
      {
        return {std::make_shared<Pred>(pattern)};
      }

      Pattern operator--() const
      {
        return {std::make_shared<NegPred>(pattern)};
      }

      Pattern operator++(int) const
      {
        if (pattern->custom_rep())
          return {pattern};

        return {std::make_shared<Rep>(pattern)};
      }

      Pattern operator!() const
      {
        return {std::make_shared<Not>(pattern)};
      }

      Pattern operator*(Pattern rhs) const
      {
        auto result = pattern->clone();
        result->set_continuation(rhs.pattern);
        return std::move(result);
      }

      Pattern operator/(Pattern rhs) const
      {
        return {std::make_shared<Choice>(pattern, rhs.pattern)};
      }

      Pattern operator<<(Pattern rhs) const
      {
        return {std::make_shared<Children>(pattern, rhs.pattern)};
      }
    };

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
  inline auto operator>>(detail::Pattern pattern, F effect)
    -> detail::PatternEffect<decltype(effect(std::declval<Match&>()))>
  {
    return {pattern, effect};
  }

  inline const auto Any = detail::Pattern(std::make_shared<detail::Anything>());
  inline const auto Start = detail::Pattern(std::make_shared<detail::First>());
  inline const auto End = detail::Pattern(std::make_shared<detail::Last>());

  inline detail::Pattern T(const Token& type)
  {
    return detail::Pattern(std::make_shared<detail::TokenMatch>(type));
  }

  inline detail::Pattern T(const Token& type, const std::string& r)
  {
    return detail::Pattern(std::make_shared<detail::RegexMatch>(type, r));
  }

  inline detail::Pattern In(const Token& type)
  {
    return detail::Pattern(std::make_shared<detail::Inside>(type));
  }

  template<typename... Ts>
  inline detail::Pattern
  In(const Token& type1, const Token& type2, const Ts&... types)
  {
    std::vector<Token> t = {type1, type2, types...};
    return detail::Pattern(std::make_shared<detail::InsideN>(t));
  }

  inline detail::EphemeralNode operator-(Node node)
  {
    return {node};
  }

  inline detail::EphemeralNodeRange operator-(NodeRange node)
  {
    return {node};
  }

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
    for (auto it = range_contents.range.first;
         it != range_contents.range.second;
         ++it)
    {
      node->push_back({(*it)->begin(), (*it)->end()});
    }

    return node;
  }

  inline Node operator<<(Node node, detail::RangeOr range_or)
  {
    if (range_or.range.first != range_or.range.second)
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
    nodes.reserve(std::distance(range.first, range.second));

    for (auto it = range.first; it != range.second; ++it)
      nodes.push_back((*it)->clone());

    return nodes;
  }
}

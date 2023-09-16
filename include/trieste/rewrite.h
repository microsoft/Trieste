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
    size_t index{0};
    std::vector<std::pair<bool, std::map<Token, NodeRange>>> captures{16};
    static constexpr NodeRange empty{};

  public:
    Match(Node in_node_) : in_node(in_node_) {}

    Match(const Match&) = delete;

    Location fresh(const Location& prefix = {})
    {
      return in_node->fresh(prefix);
    }

    void set_root(Node root)
    {
      in_node = root;
    }

    const NodeRange& operator[](const Token& token)
    {
      for (size_t i = index; ; i-- )
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
      throw std::runtime_error("Looking up unset identifier!" + std::string(token.str()));
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
      for (size_t i = index; ; i-- )
      {
        const auto& [valid, map] = captures[i];
        if (valid)
        {
          auto it = map.find(token);
          if ((it != map.end()) && *it->second.first)
          {
            return *it->second.first;
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
        captures.resize(index*2);
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
        return has_captures_local() || (continuation && continuation->has_captures());
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
      Cap(const Token& name_, PatternPtr pattern_) : name(name_), pattern(pattern_)
      {}

      bool has_captures_local() const& override
      {
        return true;
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Cap>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto begin = it;

        if (!pattern->match(it, end, match))
          return false;

        match.set(name, {begin, it});
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
      TokenMatch(const Token& type_) : type(type_) {}

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
      RegexMatch(const Token& type_, const std::string& re) : type(type_), regex(std::make_shared<RE2>(re))
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
      Opt(PatternPtr pattern_) : pattern(pattern_) {}

      bool has_captures_local() const& override
      {
        return pattern->has_captures();
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Opt>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto backtrack_it = it;
        auto backtrack_frame = match.add_frame();
        if (!pattern->match(it, end, match))
        {
          it = backtrack_it;
          match.return_to_frame(backtrack_frame);
        }
        return match_continuation(it, end, match); 
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
          throw std::runtime_error("Captures not allowed inside iteration (Pattern++)!");
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

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        NodeIt curr = it;
        while ((it != end) && pattern->match(it, end, match))
        {
          curr = it;
        }
        // Last match failed so backtrack it.
        it = curr;
        return match_continuation(it, end, match); 
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
          throw std::runtime_error("Captures not allowed inside Not (~Pattern)!");
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Not>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if (it == end)
          return false;

        auto begin = it;
        it = begin + 1;

        return !pattern->match(begin, end, match) && match_continuation(it, end, match);
      }
    };

    class Choice : public PatternDef
    {
    private:
      PatternPtr first;
      PatternPtr second;

    public:
      Choice(PatternPtr first_, PatternPtr second_) : first(first_), second(second_)
      {}

      bool has_captures_local() const& override
      {
        return first->has_captures() || second->has_captures();
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Choice>(*this);
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto backtrack_it = it;
        auto backtrack_frame = match.add_frame();
        if (first->match(it, end, match))
        {
          return match_continuation(it, end, match);
        }

        it = backtrack_it;
        match.return_to_frame(backtrack_frame);

        return second->match(it, end, match) && match_continuation(it, end, match);
      }
    };

    template <size_t N>
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
        throw std::runtime_error("Rep(InsideStar) not allowed! ((In(T,...)++)++");
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if (it == end)
          return false;

        auto p = (*it)->parent();

        while (p)
        {
          for (const auto& type: types)
            if (p->type() == type)
              return match_continuation(it, end, match);

          p = p->parent();
        }

        return false;
      }
    };

    template <size_t N>
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

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        if (it == end)
          return false;

        auto p = (*it)->parent();

        for (const auto& type: types)
        {
          if (p->type() == type)
              return match_continuation(it, end, match);
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

      PatternPtr custom_rep() override
      {
        throw std::runtime_error("Rep(Last) not allowed! (End)++");

        // Rep(Last) is treated as Last.
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
      Pred(PatternPtr pattern_) : pattern(pattern_)
      {
        if (pattern->has_captures())
          throw std::runtime_error("Captures not allowed inside Pred (++Pattern)!");
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<Pred>(*this);
      }

      PatternPtr custom_rep() override
      {
        throw std::runtime_error("Rep(Pred) not allowed! (++Pattern)++");
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto begin = it;
        return pattern->match(begin, end, match) && match_continuation(it, end, match);
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
          throw std::runtime_error("Captures not allowed inside NegPred (--Pattern)!");
      }

      PatternPtr clone() const& override
      {
        return std::make_shared<NegPred>(*this);
      }

      PatternPtr custom_rep() override
      {
        throw std::runtime_error("Rep(NegPred) not allowed! (--Pattern)++");
      }

      bool match(NodeIt& it, const NodeIt& end, Match& match) const& override
      {
        auto begin = it;
        return !pattern->match(begin, end, match) && match_continuation(it, end, match);
      }
    };

    using ActionFn = std::function<bool(const NodeRange&)>;

    class Action : public PatternDef
    {
    private:
      ActionFn action;
      PatternPtr pattern;

    public:
      Action(ActionFn action_, PatternPtr pattern_)
      : action(action_), pattern(pattern_)
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
      Pattern(PatternPtr pattern_) : pattern(pattern_) {}

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
        auto result = pattern->custom_rep();
        if (result)
          return {result};

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

  template<typename... Ts>
  inline detail::Pattern
  In(const Token& type1,  const Ts&... types)
  {
    std::array<Token, 1+sizeof...(types)> types_ = {type1, types...};
    return detail::Pattern(std::make_shared<detail::Inside<1+sizeof...(types)>>(types_));
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

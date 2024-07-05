#pragma once

#include "defaultmap.h"
#include "rewrite.h"
#include "wf.h"

#include <vector>

namespace trieste
{
  namespace dir
  {
    using flag = uint32_t;
    constexpr flag bottomup = 1 << 0;
    constexpr flag topdown = 1 << 1;
    constexpr flag once = 1 << 2;
  }

  class PassDef;
  using Pass = std::shared_ptr<PassDef>;

  class PassDef
  {
  public:
    using F = std::function<size_t(Node)>;

  private:
    std::string name_;
    const wf::Wellformed& wf_ = wf::empty;
    dir::flag direction_;

    std::vector<detail::PatternEffect<Node>> rules_;
    detail::DefaultMap<
      detail::DefaultMap<std::vector<detail::PatternEffect<Node>>>>
      rule_map;

    F pre_once;
    F post_once;
    std::map<Token, F> pre_;
    std::map<Token, F> post_;

  public:
    PassDef(
      const std::string& name,
      const wf::Wellformed& wf,
      dir::flag direction = dir::topdown)
    : name_(name), wf_(wf), direction_(direction)
    {}

    PassDef(dir::flag direction = dir::topdown) : direction_(direction) {}

    PassDef(const std::initializer_list<detail::PatternEffect<Node>>& r)
    : direction_(dir::topdown), rules_(r)
    {
      compile_rules();
    }

    PassDef(
      dir::flag direction,
      const std::initializer_list<detail::PatternEffect<Node>>& r)
    : direction_(direction), rules_(r)
    {
      compile_rules();
    }

    PassDef(
      const std::string& name,
      const wf::Wellformed& wf,
      const std::initializer_list<detail::PatternEffect<Node>>& r)
    : name_(name), wf_(wf), direction_(dir::topdown), rules_(r)
    {
      compile_rules();
    }

    PassDef(
      const std::string& name,
      const wf::Wellformed& wf,
      dir::flag direction,
      const std::initializer_list<detail::PatternEffect<Node>>& r)
    : name_(name), wf_(wf), direction_(direction), rules_(r)
    {
      compile_rules();
    }

    SNMALLOC_SLOW_PATH PassDef(const PassDef&) = default;
    SNMALLOC_SLOW_PATH PassDef(PassDef&&) = default;
    SNMALLOC_SLOW_PATH ~PassDef() = default;

    operator Pass() const
    {
      return std::make_shared<PassDef>(std::move(*this));
    }

    const std::string& name()
    {
      return name_;
    }

    const wf::Wellformed& wf() const
    {
      return wf_;
    }

    void pre(F f)
    {
      pre_once = f;
    }

    void post(F f)
    {
      post_once = f;
    }

    void pre(const Token& type, F f)
    {
      pre_[type] = f;
    }

    void pre(const std::initializer_list<Token>& types, F f)
    {
      for (const auto& type : types)
        pre_[type] = f;
    }

    void post(const Token& type, F f)
    {
      post_[type] = f;
    }

    void post(const std::initializer_list<Token>& types, F f)
    {
      for (const auto& type : types)
        post_[type] = f;
    }

    template<typename... Ts>
    void rules(Ts... r)
    {
      std::vector<detail::PatternEffect<Node>> rules = {r...};
      rules_.insert(rules_.end(), rules.begin(), rules.end());
      compile_rules();
    }

    void rules(const std::initializer_list<detail::PatternEffect<Node>>& r)
    {
      rules_.insert(rules_.end(), r.begin(), r.end());
      compile_rules();
    }

    std::tuple<Node, size_t, size_t> run(Node node)
    {
      static thread_local Match match;
      ast::detail::top_node() = node;

      size_t changes = 0;
      size_t changes_sum = 0;
      size_t count = 0;

      if (pre_once)
        changes_sum += pre_once(node);

      // Because apply runs over child nodes, the top node is never visited.
      do
      {
        changes = apply(node, match);

        auto lifted = lift(node);
        if (!lifted.empty())
          throw std::runtime_error("lifted nodes with no destination");

        changes_sum += changes;
        count++;

        if (flag(dir::once))
          break;
      } while (changes > 0);

      if (post_once)
        changes_sum += post_once(node);

      return {node, count, changes_sum};
    }

  private:
    void compile_rules()
    {
      rule_map.clear();

      for (auto& rule : rules_)
      {
        const auto& starts = rule.first.value.get_starts();
        const auto& parents = rule.first.value.get_parents();

        //  This is used to add a rule under a specific parent, or to the
        //  default.
        auto add = [&](detail::DefaultMap<
                       std::vector<detail::PatternEffect<Node>>>& rules) {
          if (starts.empty())
          {
            // If there are no starts, then this rule applies to all tokens.
            rules.modify_all([&](std::vector<detail::PatternEffect<Node>>& v) {
              v.push_back(rule);
            });
          }
          else
          {
            for (const auto& start : starts)
            {
              // Add the rule to the specific start token.
              rules.modify(start).push_back(rule);
            }
          }
        };

        if (parents.empty())
        {
          rule_map.modify_all(add);
        }
        else
        {
          for (const auto& parent : parents)
          {
            add(rule_map.modify(parent));
          }
        }
      }
    }

    bool flag(dir::flag f) const
    {
      return (direction_ & f) != 0;
    }

    SNMALLOC_SLOW_PATH ptrdiff_t replace(
      Match& match,
      detail::Effect<Node>& rule_replace,
      const NodeIt& start,
      NodeIt& it,
      const Node& node)
    {
      ptrdiff_t replaced = -1;
      // Replace [start, it) with whatever the rule builds.
      auto replace = rule_replace(match);

      if (replace && (replace == NoChange))
        return replaced;

      auto loc = (*start)->location();

      for (auto i = start + 1; i < it; ++i)
        loc = loc * (*i)->location();

      it = node->erase(start, it);

      // If we return nothing, just remove the matched nodes.
      if (!replace)
      {
        replaced = 0;
      }
      else if (replace == Seq)
      {
        // Unpack the sequence.
        std::for_each(replace->begin(), replace->end(), [&](Node n) {
          n->set_location(loc);
        });

        replaced = replace->size();
        it = node->insert(it, replace->begin(), replace->end());
      }
      else
      {
        // Replace with a single node.
        replaced = 1;
        replace->set_location(loc);
        it = node->insert(it, replace);
      }

      return replaced;
    }

    SNMALLOC_FAST_PATH size_t match_children(const Node& node, Match& match)
    {
      size_t changes = 0;

      auto& rules = rule_map.get(node->type());

      // No rules apply under this specific parent, so skip it.
      if (SNMALLOC_UNLIKELY(rules.empty()))
        return changes;

      auto it = node->begin();
      // Perform matching at this level
      while (it != node->end())
      {
        ptrdiff_t replaced = -1;

        auto start = it;
        // Find rule set for this parent and start token combination.
        auto& specific_rules = rules.get((*it)->type());
        for (auto& rule : specific_rules)
        {
          match.reset();
          if (
            SNMALLOC_UNLIKELY(rule.first.value.match(it, node, match)) &&
            SNMALLOC_LIKELY(!range_contains_error(start, it)))
          {
            replaced = replace(match, rule.second, start, it, node);
            if (replaced != -1)
            {
              changes += replaced;
              break;
            }
          }
          it = start;
        }

        if (replaced == -1)
        {
          // If we didn't do anything, advance to the next node.
          ++it;
        }
        else if (flag(dir::once))
        {
          // Skip over everything we populated.
          it += replaced;
        }
        else
        {
          // Otherwise, start again from the beginning.
          it = node->begin();
        }
      }

      return changes;
    }

    template<bool Topdown, bool Pre, bool Post>
    size_t apply_special(Node root, Match& match)
    {
      size_t changes = 0;

      auto add = [&](Node& node) SNMALLOC_FAST_PATH_LAMBDA {
        // Don't examine Error or Lift nodes.
        if (node->type() & flag::internal)
          return false;

        if constexpr (Pre)
        {
          auto pre_f = pre_.find(node->type());
          if (pre_f != pre_.end())
            changes += pre_f->second(node);
        }
        if constexpr (Topdown)
          changes += match_children(node, match);

        return true;
      };

      auto remove = [&](Node& node) SNMALLOC_FAST_PATH_LAMBDA {
        if constexpr (!Topdown)
          changes += match_children(node, match);
        else
          snmalloc::UNUSED(node);
        if constexpr (Post)
        {
          auto post_f = post_.find(node->type());
          if (post_f != post_.end())
            changes += post_f->second(node);
        }
      };

      root->traverse(add, remove);

      return changes;
    }

    size_t apply(Node root, Match& match)
    {
      if (flag(dir::topdown))
      {
        if (pre_.empty())
        {
          if (post_.empty())
          {
            return apply_special<true, false, false>(root, match);
          }
          else
          {
            return apply_special<true, false, true>(root, match);
          }
        }
        else
        {
          if (post_.empty())
          {
            return apply_special<true, true, false>(root, match);
          }
          else
          {
            return apply_special<true, true, true>(root, match);
          }
        }
      }
      else
      {
        if (pre_.empty())
        {
          if (post_.empty())
          {
            return apply_special<false, false, false>(root, match);
          }
          else
          {
            return apply_special<false, false, true>(root, match);
          }
        }
        else
        {
          if (post_.empty())
          {
            return apply_special<false, true, false>(root, match);
          }
          else
          {
            return apply_special<false, true, true>(root, match);
          }
        }
      }
    }

    Nodes lift(Node node)
    {
      if (!node->get_and_reset_contains_lift())
        return {};

      Nodes uplift;
      auto it = node->begin();

      while (it != node->end())
      {
        bool advance = true;
        auto lifted = lift(*it);

        if (*it == Lift)
        {
          lifted.insert(lifted.begin(), *it);
          it = node->erase(it, it + 1);
          advance = false;
        }

        for (auto& lnode : lifted)
        {
          if (lnode->front()->type() == node->type())
          {
            it = node->insert(it, lnode->begin() + 1, lnode->end());
            it += lnode->size() - 1;
            advance = false;
          }
          else
          {
            uplift.push_back(lnode);
          }
        }

        if (advance)
          ++it;
      }

      return uplift;
    }
  };
}

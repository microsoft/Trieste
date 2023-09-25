#pragma once

#include "rewrite.h"

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

  /**
   * Maps tokens to values, with a modifiable default value.
   *
   * This is used by matching system.  If a rule applies generally, it is added
   * to all tokens, and if it applies to a specific token, it is added to that
   * token only.
   */
  template<typename T>
  class DefaultMap
  {
    // The default value for this map. This is returned when a specific value
    // has has not been set for the looked up token.
    std::shared_ptr<T> def;

    // The map of specific values for tokens.
    std::array<std::shared_ptr<T>, TokenDef::HASH_SIZE> map;

    // If this is true, then the map is empty, and the default value has not
    // been modified.
    bool empty_{true};

    bool is_index_default(size_t index) const
    {
      return &*map[index] == &*def;
    }

    size_t token_index(const Token& t) const
    {
      return t.hash();
    }

  public:
    DefaultMap() : def(std::make_shared<T>())
    {
      map.fill(def);
    }

    DefaultMap(const DefaultMap& dm) : def(std::make_shared<T>(*dm.def)), empty_(dm.empty_)
    {
      for (size_t index = 0; index < map.size(); index++)
      {
        if (dm.is_index_default(index))
          map[index] = def;
        else
          map[index] = std::make_shared<T>(*dm.map[index]);
      }
    }

    /**
     *  Modify all values in the map, including the default value.
     *
     *  This is used for adding rules that do not specify an explicit start
     * token, or an explicit parent, so they need to apply generally.
     */
    template<typename F>
    void modify_all(F f)
    {
      empty_ = false;
      for (size_t i = 0; i < map.size(); i++)
        if (!is_index_default(i))
          f(*map[i]);
      f(*def);
    }

    /**
     * Get a mutable reference to the value for a token.  If this does not have
     * a current value, first fill it with the current default value.
     */
    T& modify(const Token& t)
    {
      auto i = token_index(t);
      empty_ = false;
      // Use existing default set of rules.
      if (is_index_default(i))
        map[i] = std::make_shared<T>(*def);
      return *map[i];
    }

    /**
     * Get the value for a token. If this token has no specific value, return
     * the default value.
     */
    T& get(const Token& t)
    {
      return *map[token_index(t)];
    }

    /**
     * Clear all the values in the map, and the default value.
     */
    void clear()
    {
      empty_ = true;
      map.fill(def);
      def->clear();
    }

    /**
     * Returns true if modify has not been called since the last clear.
     */
    bool empty() const
    {
      return empty_;
    }
  };

  class PassDef;
  using Pass = std::shared_ptr<PassDef>;

  class PassDef
  {
  public:
    using F = std::function<size_t(Node)>;

  private:
    F pre_once;
    F post_once;
    std::map<Token, F> pre_;
    std::map<Token, F> post_;
    dir::flag direction_;
    std::vector<detail::PatternEffect<Node>> rules_;
    DefaultMap<DefaultMap<std::vector<detail::PatternEffect<Node>>>> rule_map;

  public:
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

    operator Pass() const
    {
      return std::make_shared<PassDef>(std::move(*this));
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

    void post(const Token& type, F f)
    {
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

    void compile_rules()
    {
      rule_map.clear();

      for (auto& rule : rules_)
      {
        const auto& starts = rule.first.get_starts();
        const auto& parents = rule.first.get_parents();

        //  This is used to add a rule under a specific parent, or to the
        //  default.
        auto add =
          [&](DefaultMap<std::vector<detail::PatternEffect<Node>>>& rules) {
            if (starts.empty())
            {
              // If there are no starts, then this rule applies to all tokens.
              rules.modify_all(
                [&](std::vector<detail::PatternEffect<Node>>& v) {
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

    std::tuple<Node, size_t, size_t> run(Node node)
    {
      static thread_local Match match(node);
      match.set_root(node);

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

    size_t match_children(const Node& node, Match& match)
    {
      size_t changes = 0;

      auto& rules = rule_map.get(node->type());

      // No rules apply under this specific parent, so skip it.
      if (rules.empty())
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
          if (SNMALLOC_UNLIKELY(rule.first.match(it, node, match)))
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

      std::vector<std::pair<Node&, NodeIt>> path;

      auto add = [&](Node& node) SNMALLOC_FAST_PATH_LAMBDA {
        // Don't examine Error or Lift nodes.
        if (node->type() & flag::internal)
          return;
        if constexpr (Pre)
        {
          auto pre_f = pre_.find(node->type());
          if (pre_f != pre_.end())
            changes += pre_f->second(node);
        }
        if constexpr (Topdown)
          changes += match_children(node, match);
        path.push_back({node, node->begin()});
      };

      auto remove = [&]() SNMALLOC_FAST_PATH_LAMBDA {
        Node& node = path.back().first;
        if constexpr (!Topdown)
          changes += match_children(node, match);
        if constexpr (Post)
        {
          auto post_f = post_.find(node->type());
          if (post_f != post_.end())
            changes += post_f->second(node);
        }
        path.pop_back();
      };

      add(root);
      while (!path.empty())
      {
        auto& [node, it] = path.back();
        if (it != node->end())
        {
          Node& curr = *it;
          it++;
          add(curr);
        }
        else
        {
          remove();
        }
      }

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

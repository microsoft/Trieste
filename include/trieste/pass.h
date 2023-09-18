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

  public:
    PassDef(dir::flag direction = dir::topdown) : direction_(direction) {}

    PassDef(const std::initializer_list<detail::PatternEffect<Node>>& r)
    : direction_(dir::topdown), rules_(r)
    {}

    PassDef(
      dir::flag direction,
      const std::initializer_list<detail::PatternEffect<Node>>& r)
    : direction_(direction), rules_(r)
    {}

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
    }

    void rules(const std::initializer_list<detail::PatternEffect<Node>>& r)
    {
      rules_.insert(rules_.end(), r.begin(), r.end());
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

    size_t match_children(const Node& node, Match& match)
    {
      size_t changes = 0;
      auto it = node->begin();
      // Perform matching at this level
      while (it != node->end())
      {
        // Don't examine Error or Lift nodes.
        if ((*it)->type().in({Error, Lift}))
        {
          ++it;
          continue;
        }

        ptrdiff_t replaced = -1;

        auto start = it;
        for (auto& rule : rules_)
        {
          match.return_to_frame(0);
          match.add_frame();
          if (rule.first.match(it, node->end(), match))
          {
            // Replace [start, it) with whatever the rule builds.
            auto replace = rule.second(match);

            if (replace && (replace == NoChange))
            {
              it = start;
              continue;
            }

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

            changes += replaced;
            break;
          }
          else
          {
            it = start;
          }
        }

        if (flag(dir::once))
        {
          // Skip over everything we populated, or if no change (-1) skip
          // forwards to next term.
          it += std::abs(replaced);
        }
        else if (replaced >= 0)
        {
          // If we did something, reexamine from the beginning.
          it = node->begin();
        }
        else
        {
          // Advance to the next node.
          ++it;
        }
      }

      return changes;
    }

    size_t apply(Node root, Match& match)
    {
      size_t changes = 0;

      std::vector<std::pair<Node, NodeIt>> path;

      auto add = [&](const Node& node) SNMALLOC_FAST_PATH_LAMBDA {
        if (node->type().in({Error, Lift}))
          return;
        auto pre_f = pre_.find(node->type());
        if (pre_f != pre_.end())
          changes += pre_f->second(node);
        if (flag(dir::topdown))
          changes += match_children(node, match);
        path.push_back({node, node->begin()});
      };

      auto remove = [&]() SNMALLOC_FAST_PATH_LAMBDA {
        Node& node = path.back().first;
        if (flag(dir::bottomup))
          changes += match_children(node, match);
        auto post_f = post_.find(node->type());
        if (post_f != post_.end())
          changes += post_f->second(node);
        path.pop_back();
      };

      add(root);
      while (!path.empty())
      {
        auto& [node, it] = path.back();
        if (it != node->end())
        {
          Node curr = *it;
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

    Nodes lift(Node node)
    {
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

#pragma once

#include "rewrite.h"

namespace trieste
{
  namespace dir
  {
    using flag = uint32_t;
    constexpr flag bottomup = 1 << 0;
    constexpr flag topdown = 1 << 1;
    constexpr flag once = 1 << 2;
  };

  class PassDef;
  using Pass = std::shared_ptr<PassDef>;

  class PassDef
  {
  public:
    using PreF = std::function<size_t(Node)>;
    using PostF = std::function<size_t(Node)>;

  private:
    std::map<Token, PreF> pre_;
    std::map<Token, PostF> post_;
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

    void pre(const Token& type, PreF f)
    {
      pre_[type] = f;
    }

    void post(const Token& type, PostF f)
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
      size_t changes = 0;
      size_t changes_sum = 0;
      size_t count = 0;

      // Because apply runs over child nodes, the top node is never visited.
      do
      {
        changes = apply(node);

        auto lifted = lift(node);
        if (!lifted.empty())
          throw std::runtime_error("lifted nodes with no destination");

        changes_sum += changes;
        count++;

        if (flag(dir::once))
          break;
      } while (changes > 0);

      return {node, count, changes_sum};
    }

  private:
    bool flag(dir::flag f) const
    {
      return (direction_ & f) != 0;
    }

    size_t apply(Node node)
    {
      if (node->type().in({Error, Lift}))
        return 0;

      size_t changes = 0;

      auto pre_f = pre_.find(node->type());
      if (pre_f != pre_.end())
        changes += pre_f->second(node);

      auto it = node->begin();

      while (it != node->end())
      {
        // Don't examine Error or Lift nodes.
        if ((*it)->type().in({Error, Lift}))
        {
          ++it;
          continue;
        }

        if (flag(dir::bottomup))
          changes += apply(*it);

        ptrdiff_t replaced = -1;

        for (auto& rule : rules_)
        {
          auto match = Match(node);
          auto start = it;

          if (rule.first.match(it, node->end(), match))
          {
            // Replace [start, it) with whatever the rule builds.
            auto replace = rule.second(match);

            if (replace && (replace->type() == NoChange))
            {
              it = start;
              continue;
            }

            auto loc = (*start)->location() * (*(it - 1))->location();
            it = node->erase(start, it);

            // If we return nothing, just remove the matched nodes.
            if (!replace)
            {
              replaced = 0;
            }
            else if (replace->type() == Seq)
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
        }

        if (flag(dir::once))
        {
          if (flag(dir::topdown) && (replaced != 0))
          {
            // Move down the tree.
            auto to = std::max(replaced, ptrdiff_t(1));

            for (ptrdiff_t i = 0; i < to; ++i)
              changes += apply(*(it + i));
          }

          // Skip over everything we examined or populated.
          if (replaced >= 0)
            it += replaced;
          else
            ++it;
        }
        else if (replaced >= 0)
        {
          // If we did something, reexamine from the beginning.
          it = node->begin();
        }
        else
        {
          // If we did nothing, move down the tree.
          if (flag(dir::topdown))
            changes += apply(*it);

          // Advance to the next node.
          ++it;
        }
      }

      auto post_f = post_.find(node->type());
      if (post_f != post_.end())
        changes += post_f->second(node);

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

        if ((*it)->type() == Lift)
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

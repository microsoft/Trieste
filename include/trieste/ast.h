// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "intrusive_ptr.h"
#include "token.h"

#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <vector>

#ifndef TRIESTE_USE_CXX17
#  include <span>
#endif

namespace trieste
{
  struct indent
  {
    size_t level;

    indent(size_t level_) : level(level_) {}
  };

  inline std::ostream& operator<<(std::ostream& out, const indent in)
  {
    for (size_t i = 0; i < in.level; i++)
      out << "  ";

    return out;
  }

  using Nodes = std::vector<Node>;
  using NodeIt = Nodes::iterator;
  using NodeSet = std::set<Node>;

  template<typename T>
  using NodeMap = std::map<Node, T>;

#ifdef TRIESTE_USE_CXX17
  class NodeRange
  {
  public:
    NodeRange() {}
    NodeRange(NodeIt first_, NodeIt second_) : first(first_), second(second_) {}
    NodeRange(NodeRange&& other) : first(other.first), second(other.second) {}
    NodeRange(const NodeRange& other) : first(other.first), second(other.second)
    {}

    NodeRange& operator=(NodeRange&& other)
    {
      first = other.first;
      second = other.second;
      return *this;
    }

    NodeRange& operator=(const NodeRange& other)
    {
      first = other.first;
      second = other.second;
      return *this;
    }

    bool empty() const
    {
      return first == second;
    }

    std::size_t size() const
    {
      return std::distance(first, second);
    }

    NodeIt begin() const
    {
      return first;
    }

    NodeIt end() const
    {
      return second;
    }

    Node front() const
    {
      return *first;
    }

    Node back() const
    {
      return *(second - 1);
    }

    Node operator[](std::size_t index) const
    {
      return *(first + index);
    }

  private:
    NodeIt first;
    NodeIt second;
  };
#else
  using NodeRange = std::span<Node>;
#endif

  class SymtabDef final : public intrusive_refcounted<SymtabDef>
  {
    friend class NodeDef;

  private:
    // The location in `symbols` is used as an identifier.
    std::map<Location, Nodes> symbols;
    std::vector<Node> includes;
    size_t next_id = 0;

  public:
    SymtabDef() = default;

    Location fresh(const Location& prefix = {})
    {
      return Location(
        std::string(prefix.view()) + "$" + std::to_string(next_id++));
    }

    void clear()
    {
      // Don't reset next_id, so that we don't reuse identifiers.
      symbols.clear();
      includes.clear();
    }

    void str(std::ostream& out, size_t level);
  };

  using Symtab = intrusive_ptr<SymtabDef>;

  struct Index
  {
    Token type;
    size_t index;

    Index() : type(Invalid), index(std::numeric_limits<size_t>::max()) {}
    Index(const Token& type_, size_t index_) : type(type_), index(index_) {}
  };

  class Flags
  {
    char flags{0};

  public:
    void set_contains_error()
    {
      flags |= 1 << 0;
    }

    void set_contains_lift()
    {
      flags |= 1 << 1;
    }

    void reset_contains_error()
    {
      flags &= ~(1 << 0);
    }

    void reset_contains_lift()
    {
      flags &= ~(1 << 1);
    }

    bool contains_error()
    {
      return flags & (1 << 0);
    }

    bool contains_lift()
    {
      return flags & (1 << 1);
    }
  };

  class NodeDef final : public intrusive_refcounted<NodeDef>
  {
  private:
    Token type_;
    Location location_;
    Symtab symtab_;
    NodeDef* parent_;
    Flags flags_{};
    Nodes children;

    NodeDef(const Token& type, Location location)
    : type_(type), location_(location), parent_(nullptr)
    {
      if (type_ & flag::symtab)
        symtab_ = Symtab::make();
    }

    void add_flags()
    {
      if (type_ == Error || flags_.contains_error())
      {
        auto curr = parent_;
        while (curr != nullptr)
        {
          if (curr->flags_.contains_error())
            break;
          curr->flags_.set_contains_error();
          curr = curr->parent_;
        }
      }
      else if (type_ == Lift || flags_.contains_lift())
      {
        auto curr = parent_;
        while (curr != nullptr)
        {
          if (curr->flags_.contains_lift())
            break;
          curr->flags_.set_contains_lift();
          curr = curr->parent_;
        }
      }
    }

  public:
    static Node create(const Token& type)
    {
      return Node(new NodeDef(type, Location{nullptr, 0, 0}));
    }

    static Node create(const Token& type, Location location)
    {
      return Node(new NodeDef(type, location));
    }

    static Node create(const Token& type, NodeRange range)
    {
      if (range.empty())
        return create(type);

      return Node(
        new NodeDef(type, range.front()->location_ * range.back()->location_));
    }

    const Token& type() const
    {
      return type_;
    }

    bool in(const std::initializer_list<Token>& list) const
    {
      return type_.in(list);
    }

    const Location& location() const
    {
      return location_;
    }

    Node parent()
    {
      return parent_ ? parent_->intrusive_ptr_from_this() : nullptr;
    }

    NodeDef* parent_unsafe()
    {
      return parent_;
    }

    Node parent(const Token& type)
    {
      return parent({type});
    }

    Node parent(const std::initializer_list<Token>& list)
    {
      auto p = parent_;

      while (p)
      {
        if (p->type_.in(list))
          return p->intrusive_ptr_from_this();

        p = p->parent_;
      }

      return {};
    }

    void set_location(const Location& loc)
    {
      traverse([&](Node& current) {
        auto& current_loc = current->location_;
        if (current_loc.source)
          return false;
        current_loc = loc;
        return true;
      });
    }

    void extend(const Location& loc)
    {
      location_ *= loc;
    }

    auto begin()
    {
      return children.begin();
    }

    auto end()
    {
      return children.end();
    }

    auto rbegin()
    {
      return children.rbegin();
    }

    auto rend()
    {
      return children.rend();
    }

    auto cbegin()
    {
      return children.cbegin();
    }

    auto cend()
    {
      return children.cend();
    }

    auto crbegin()
    {
      return children.crbegin();
    }

    auto crend()
    {
      return children.crend();
    }

    auto find_first(Token token, NodeIt begin)
    {
      assert((*begin)->parent_unsafe() == this);
      return std::find_if(
        begin, children.end(), [token](auto& n) { return n->type() == token; });
    }

    auto contains(Token token)
    {
      return find_first(token, children.begin()) != children.end();
    }

    auto find(Node node)
    {
      return std::find(children.begin(), children.end(), node);
    }

    bool empty()
    {
      return children.empty();
    }

    size_t size() const
    {
      return children.size();
    }

    const Node& at(size_t index)
    {
      return children.at(index);
    }

    const Node& front()
    {
      return children.front();
    }

    const Node& back()
    {
      return children.back();
    }

    void push_front(Node node)
    {
      if (!node)
        return;

      children.insert(children.begin(), node);
      node->parent_ = this;
      node->add_flags();
    }

    void push_back(Node node)
    {
      if (!node)
        return;

      children.push_back(node);
      node->parent_ = this;
      node->add_flags();
    }

    void push_back(NodeIt it)
    {
      push_back(*it);
    }

    void push_back(NodeRange range)
    {
      for (Node& n : range)
        push_back(n);
    }

    void push_back_ephemeral(Node node)
    {
      if (!node)
        return;

      // Don't set the parent of the new child node to `this`.
      children.push_back(node);
    }

    void push_back_ephemeral(NodeRange range)
    {
      for (Node& n : range)
        push_back_ephemeral(n);
    }

    Node pop_back()
    {
      if (children.empty())
        return {};

      auto node = children.back();
      children.pop_back();

      if (node->parent_ == this)
        node->parent_ = nullptr;

      return node;
    }

    NodeIt erase(NodeIt first, NodeIt last)
    {
      for (auto it = first; it != last; ++it)
      {
        // Only clear the parent if the node is not shared.
        if ((*it)->parent_ == this)
          (*it)->parent_ = nullptr;
      }

      return children.erase(first, last);
    }

    NodeIt insert(NodeIt pos, Node node)
    {
      if (!node)
        return pos;

      node->parent_ = this;
      node->add_flags();
      return children.insert(pos, node);
    }

    NodeIt insert(NodeIt pos, NodeIt first, NodeIt last)
    {
      if (first == last)
        return pos;

      for (auto it = first; it != last; ++it)
      {
        (*it)->parent_ = this;
        (*it)->add_flags();
      }

      return children.insert(pos, first, last);
    }

    Node scope()
    {
      auto p = parent_;

      while (p)
      {
        auto node = p->intrusive_ptr_from_this();

        if (node->symtab_)
          return node;

        p = node->parent_;
      }

      return {};
    }

    const Nodes& includes()
    {
      static Nodes empty_includes;
      if (!symtab_)
        return empty_includes;

      return symtab_->includes;
    }

    template<typename F>
    Nodes& get_symbols(Nodes& result, F&& f)
    {
      if (!symtab_)
        return result;

      for (auto& [loc, nodes] : symtab_->symbols)
        std::copy_if(nodes.begin(), nodes.end(), std::back_inserter(result), f);

      return result;
    }

    template<typename F>
    Nodes& get_symbols(const Location& loc, Nodes& result, F&& f)
    {
      if (!symtab_)
        return result;

      auto it = symtab_->symbols.find(loc);
      if (it == symtab_->symbols.end())
        return result;

      std::copy_if(
        it->second.begin(), it->second.end(), std::back_inserter(result), f);

      return result;
    }

    void clear_symbols()
    {
      if (symtab_)
        symtab_->clear();
    }

    Nodes lookup(Node until = {})
    {
      Nodes result;
      auto st = scope();

      while (st)
      {
        // If the type of the symbol table is flag::defbeforeuse, then the
        // definition has to appear earlier in the same file.
        st->get_symbols(location_, result, [&](auto& n) {
          return (n->type() & flag::lookup) &&
            (!(st->type() & flag::defbeforeuse) || n->precedes(this));
        });

        // Includes are always returned, regardless of what's being looked up.
        result.insert(
          result.end(),
          st->symtab_->includes.begin(),
          st->symtab_->includes.end());

        // If we've reached the scope limit or there are no shadowing
        // definitions, don't continue to the next scope.
        if (
          (st == until) ||
          std::any_of(result.begin(), result.end(), [](auto& n) {
            return n->type() & flag::shadowing;
          }))
          break;

        st = st->scope();
      }

      return result;
    }

    Nodes lookdown(const Location& loc)
    {
      // This is used for scoped resolution, where we're looking in this symbol
      // table specifically. Don't use includes, as those are for lookup only.
      Nodes result;
      return get_symbols(
        loc, result, [](auto& n) { return n->type() & flag::lookdown; });
    }

    Nodes look(const Location& loc)
    {
      // This is used for immediate resolution in this symtab, ignoring
      // flag::lookup and flag::lookdown.
      Nodes result;
      return get_symbols(loc, result, [](auto&) { return true; });
    }

    bool bind(const Location& loc)
    {
      // Find the enclosing scope and bind the new location to this node in the
      // symbol table.
      auto st = scope();

      if (!st)
        throw std::runtime_error("No symbol table");

      auto& entry = st->symtab_->symbols[loc];
      entry.push_back(intrusive_ptr_from_this());

      // If there are multiple definitions, none can be shadowing.
      return (entry.size() == 1) ||
        !std::any_of(entry.begin(), entry.end(), [](auto& n) {
               return n->type() & flag::shadowing;
             });
    }

    void include()
    {
      auto st = scope();

      if (!st)
        throw std::runtime_error("No symbol table");

      st->symtab_->includes.emplace_back(intrusive_ptr_from_this());
    }

    Location fresh(const Location& prefix = {})
    {
      // This actually returns a unique name, rather than a fresh one.
      if (type_ == Top)
        return symtab_->fresh(prefix);

      return parent(Top)->fresh(prefix);
    }

    Node clone() const
    {
      // This doesn't preserve the symbol table.
      auto node = create(type_, location_);

      for (auto& child : children)
        node->push_back(child->clone());

      return node;
    }

    void replace_at(std::size_t index, Node node2 = {})
    {
      replace(children.at(index), node2);
    }

    void replace(Node node1, Node node2 = {})
    {
      auto it = std::find(children.begin(), children.end(), node1);
      if (it == children.end())
        throw std::runtime_error("Node not found");

      if (node2)
      {
        if (node1->parent_ == this)
          node1->parent_ = nullptr;

        node2->parent_ = this;
        node2->add_flags();
        it->swap(node2);
      }
      else
      {
        children.erase(it);
      }
    }

    void lookup_replace(Node& node1, Node& node2)
    {
      assert(node1->parent_ == this);
      node1->parent_ = nullptr;
      node2->parent_ = this;
      node1 = node2;
      node2->add_flags();
    }

    bool equals(Node& node)
    {
      return (type_ == node->type()) &&
        (!(type_ & flag::print) || (location_ == node->location_)) &&
        (std::equal(
          children.begin(),
          children.end(),
          node->children.begin(),
          node->children.end(),
          [](auto& a, auto& b) { return a->equals(b); }));
    }

    Node common_parent(Node node)
    {
      return common_parent(node.get());
    }

    Node common_parent(NodeDef* node)
    {
      auto [p, q] = same_parent(node);

      // If p and q are the same, then one is contained within the other.
      if (p == q)
        return p->intrusive_ptr_from_this();

      // Otherwise return the common parent.
      return p->parent_->intrusive_ptr_from_this();
    }

    bool precedes(Node node)
    {
      return precedes(node.get());
    }

    bool precedes(NodeDef* node)
    {
      // Node A precedes node B iff A is to the left of B and A does not
      // dominate B and B does not dominate A.
      auto [p, q] = same_parent(node);

      // If p and q are the same, then either A dominates B or B dominates A.
      if (p == q)
        return false;

      // Check that p is to the left of q.
      auto parent = p->parent_;
      return parent->find(p->intrusive_ptr_from_this()) <
        parent->find(q->intrusive_ptr_from_this());
    }

    void str(std::ostream& out, size_t level = 0) const
    {
      auto pre = [&](Node& node) {
        if (level != 0)
          out << std::endl;

        out << indent(level) << "(" << node->type_.str();

        if (node->type_ & flag::print)
          out << " " << node->location_.view().size() << ":"
              << node->location_.view();

        if (node->symtab_)
        {
          out << std::endl;
          node->symtab_->str(out, level + 1);
        }

        level++;
        return true;
      };

      auto post = [&](Node&) {
        out << ")";
        level--;
      };

      // Cast is safe as traverse only mutates if pre and post do.
      const_cast<NodeDef*>(this)->traverse(pre, post);
    }

    /*
     * Useful for calling from inside a debugger.
     */
    std::string SNMALLOC_USED_FUNCTION str()
    {
      std::ostringstream out;
      str(out);
      return out.str();
    }

    /**
     * @brief Calculate a hash for the tree
     *
     * We use a bespoke hash function rather than calling
     * `std::hash<std::string>{}(str())` to avoid having to
     * allocate the string for the whole tree. This also gives us
     * constistent behaviour across platforms.
     */
    size_t hash()
    {
      // FNV-1a hash function
      // http://www.isthe.com/chongo/tech/comp/fnv/
      uint64_t constexpr fnv_prime = 1099511628211ULL;
      uint64_t constexpr offset_basis = 14695981039346656037ULL;
      uint64_t hash = offset_basis;

      traverse([&](Node& node) {
        for (auto c : node->type().str() + node->location().str())
        {
          hash ^= c;
          hash *= fnv_prime;
        }
        return true;
      });

      return hash;
    }

    class NopPost
    {
    public:
      void operator()(Node&) {}
    };

    /**
     * @brief This function performs a traversal of the node structure.
     *
     * It takes both a pre-order and a post-order function that are called when
     * a node is first visited, and once all its children have been visited.
     *
     * The pre-order action is expected to be of the form
     *   [..](Node& node) -> bool { .. }
     * where it returns true if the traversal should proceed to the children,
     * and false if it should not inspect the children.
     *
     * The post-order action will only be called if the pre-order action
     * returned true.
     *
     * The traversal is allowed to modify the structure below the current node
     * passed to the action, but not above.
     */
    template<typename Pre, typename Post = NopPost>
    SNMALLOC_FAST_PATH void traverse(Pre pre, Post post = NopPost())
    {
      Node root = intrusive_ptr_from_this();
      if (!pre(root))
        return;

      std::vector<std::pair<Node&, NodeIt>> path;
      path.push_back({root, root->begin()});
      while (!path.empty())
      {
        auto& [node, it] = path.back();
        if (it != node->end())
        {
          Node& curr = *it;
          it++;
          if (pre(curr))
          {
            path.push_back({curr, curr->begin()});
          }
        }
        else
        {
          post(node);
          path.pop_back();
        }
      }
    }

    /**
     * Pass a Nodes that is filled in with the found errors.
     */
    void get_errors(Nodes& errors)
    {
      traverse([&](Node& current) {
        // Only add Error nodes that do not contain further Error nodes.
        if (current->get_and_reset_contains_error())
          return true;

        if (current->type_ == Error)
          errors.push_back(current);
        return false;
      });
    }

    bool get_and_reset_contains_error()
    {
      bool result = flags_.contains_error();
      flags_.reset_contains_error();
      return result;
    }

    bool get_contains_error()
    {
      return flags_.contains_error();
    }

    bool get_and_reset_contains_lift()
    {
      bool result = flags_.contains_lift();
      flags_.reset_contains_lift();
      return result;
    }

  private:
    std::pair<NodeDef*, NodeDef*> same_parent(NodeDef* q)
    {
      auto p = this;

      // Adjust p and q to point to the same depth in the AST.
      int d1 = 0, d2 = 0;

      for (auto t = p; t; t = t->parent_)
        ++d1;
      for (auto t = q; t; t = t->parent_)
        ++d2;

      for (int i = 0; i < (d1 - d2); ++i)
        p = p->parent_;
      for (int i = 0; i < (d2 - d1); ++i)
        q = q->parent_;

      // Find the common parent.
      while (p->parent_ != q->parent_)
      {
        p = p->parent_;
        q = q->parent_;
      }

      return {p, q};
    }
  };

  constexpr void
  intrusive_refcounted_traits<NodeDef>::intrusive_inc_ref(NodeDef* node)
  {
    node->intrusive_inc_ref();
  }

  inline void
  intrusive_refcounted_traits<NodeDef>::intrusive_dec_ref(NodeDef* node)
  {
    node->intrusive_dec_ref();
  }

  inline TokenDef::operator Node() const
  {
    return NodeDef::create(Token(*this));
  }

  inline Token::operator Node() const
  {
    return NodeDef::create(*this);
  }

  inline void SymtabDef::str(std::ostream& out, size_t level)
  {
    out << indent(level) << "{";

    for (auto& [loc, sym] : symbols)
    {
      out << std::endl << indent(level + 1) << loc.view() << " =";

      if (sym.size() == 1)
      {
        out << " " << sym.back()->type().str();
      }
      else
      {
        for (auto& node : sym)
          out << std::endl << indent(level + 2) << node->type().str();
      }
    }

    for (auto& node : includes)
    {
      out << std::endl
          << indent(level + 1) << "include " << node->location().view();
    }

    out << "}";
  }

  inline bool operator==(const Node& node, const Token& type)
  {
    return node->type() == type;
  }

  inline bool operator==(const NodeDef* node, const Token& type)
  {
    return node->type() == type;
  }

  inline bool operator!=(const Node& node, const Token& type)
  {
    return node->type() != type;
  }

  inline bool operator!=(const NodeDef* node, const Token& type)
  {
    return node->type() != type;
  }

  inline std::ostream& operator<<(std::ostream& os, const NodeDef* node)
  {
    if (node)
    {
      node->str(os);
      os << std::endl;
    }

    return os;
  }

  inline std::ostream& operator<<(std::ostream& os, const Node& node)
  {
    return os << node.get();
  }

  inline std::ostream& operator<<(std::ostream& os, const NodeRange& range)
  {
    for (const Node& n : range)
      n->str(os);

    return os;
  }

  namespace ast
  {
    namespace detail
    {
      inline Node& top_node()
      {
        static thread_local Node top;
        return top;
      }
    }

    inline Node top()
    {
      return detail::top_node();
    }

    inline Location fresh(const Location& prefix = {})
    {
      return ast::top()->fresh(prefix);
    }
  }

  [[gnu::used]] inline void print(const NodeDef* node)
  {
    std::cout << node;
  }

  [[gnu::used]] inline void print(const Node& node)
  {
    std::cout << node;
  }

  inline bool range_contains_error(NodeIt start, NodeIt end)
  {
    return std::any_of(start, end, [](auto& n) {
      return n == Error || n->get_contains_error();
    });
  }
}

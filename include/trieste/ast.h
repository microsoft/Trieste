// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "token.h"

#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <vector>

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
  using NodeRange = std::pair<NodeIt, NodeIt>;
  using NodeSet = std::set<Node, std::owner_less<>>;

  template<typename T>
  using NodeMap = std::map<Node, T, std::owner_less<>>;

  class SymtabDef
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

  using Symtab = std::shared_ptr<SymtabDef>;

  struct Index
  {
    Token type;
    size_t index;

    Index() : type(Invalid), index(std::numeric_limits<size_t>::max()) {}
    Index(const Token& type_, size_t index_) : type(type_), index(index_) {}
  };

  class NodeDef : public std::enable_shared_from_this<NodeDef>
  {
  private:
    Token type_;
    Location location_;
    Symtab symtab_;
    NodeDef* parent_;
    Nodes children;

    NodeDef(const Token& type, Location location)
    : type_(type), location_(location), parent_(nullptr)
    {
      if (type_ & flag::symtab)
        symtab_ = std::make_shared<SymtabDef>();
    }

  public:
    ~NodeDef() {}

    static Node create(const Token& type)
    {
      return std::shared_ptr<NodeDef>(new NodeDef(type, {nullptr, 0, 0}));
    }

    static Node create(const Token& type, Location location)
    {
      return std::shared_ptr<NodeDef>(new NodeDef(type, location));
    }

    static Node create(const Token& type, NodeRange range)
    {
      if (range.first == range.second)
        return create(type);

      return std::shared_ptr<NodeDef>(new NodeDef(
        type, (*range.first)->location_ * (*(range.second - 1))->location_));
    }

    const Token& type()
    {
      return type_;
    }

    const Location& location()
    {
      return location_;
    }

    NodeDef* parent()
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
          return p->shared_from_this();

        p = p->parent_;
      }

      return {};
    }

    void set_location(const Location& loc)
    {
      if (!location_.source)
        location_ = loc;

      for (auto& c : children)
        c->set_location(loc);
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

    Node& at(size_t index)
    {
      return children.at(index);
    }

    Node& front()
    {
      return children.front();
    }

    Node& back()
    {
      return children.back();
    }

    void push_front(Node node)
    {
      if (!node)
        return;

      children.insert(children.begin(), node);
      node->parent_ = this;
    }

    void push_back(Node node)
    {
      if (!node)
        return;

      children.push_back(node);
      node->parent_ = this;
    }

    void push_back(NodeIt it)
    {
      push_back(*it);
    }

    void push_back(NodeRange range)
    {
      for (auto it = range.first; it != range.second; ++it)
        push_back(*it);
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
      for (auto it = range.first; it != range.second; ++it)
        push_back_ephemeral(*it);
    }

    Node pop_back()
    {
      if (children.empty())
        return {};

      auto node = children.back();
      children.pop_back();
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
      return children.insert(pos, node);
    }

    NodeIt insert(NodeIt pos, NodeIt first, NodeIt last)
    {
      if (first == last)
        return pos;

      for (auto it = first; it != last; ++it)
        (*it)->parent_ = this;

      return children.insert(pos, first, last);
    }

    Node scope()
    {
      auto p = parent_;

      while (p)
      {
        auto node = p->shared_from_this();

        if (node->symtab_)
          return node;

        p = node->parent_;
      }

      return {};
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
      entry.push_back(shared_from_this());

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

      st->symtab_->includes.emplace_back(shared_from_this());
    }

    Location fresh(const Location& prefix = {})
    {
      // This actually returns a unique name, rather than a fresh one.
      auto p = this;

      while (p->parent_)
        p = p->parent_;

      if (p->type_ != Top)
        throw std::runtime_error("No Top node");

      return p->symtab_->fresh(prefix);
    }

    Node clone()
    {
      // This doesn't preserve the symbol table.
      auto node = create(type_, location_);

      for (auto& child : children)
        node->push_back(child->clone());

      return node;
    }

    void replace(Node node1, Node node2 = {})
    {
      auto it = std::find(children.begin(), children.end(), node1);
      if (it == children.end())
        throw std::runtime_error("Node not found");

      if (node2)
      {
        node1->parent_ = nullptr;
        node2->parent_ = this;
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
        return p->shared_from_this();

      // Otherwise return the common parent.
      return p->parent_->shared_from_this();
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
      return parent->find(p->shared_from_this()) <
        parent->find(q->shared_from_this());
    }

    void str(std::ostream& out, size_t level) const
    {
      out << indent(level) << "(" << type_.str();

      if (type_ & flag::print)
        out << " " << location_.view().size() << ":" << location_.view();

      if (symtab_)
      {
        out << std::endl;
        symtab_->str(out, level + 1);
      }

      for (auto child : children)
      {
        out << std::endl;
        child->str(out, level + 1);
      }

      out << ")";
    }

    bool errors(std::ostream& out) const
    {
      bool err = false;

      for (auto& child : children)
        err = child->errors(out) || err;

      // If an error wraps another error, print only the innermost error.
      if (err || (type_ != Error))
        return err;

      for (auto& child : children)
      {
        if (child->type() == ErrorMsg)
          out << child->location().view() << std::endl;
        else
          out << child->location().origin_linecol() << std::endl
              << child->location().str();
      }

      // Trailing blank line.
      out << std::endl;
      return true;
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

  inline std::ostream& operator<<(std::ostream& os, const NodeDef* node)
  {
    if (node)
    {
      node->str(os, 0);
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
    for (auto it = range.first; it != range.second; ++it)
      (*it)->str(os, 0);

    return os;
  }

  [[gnu::used]] inline void print(const NodeDef* node)
  {
    std::cout << node;
  }

  [[gnu::used]] inline void print(const Node& node)
  {
    std::cout << node;
  }
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "token.h"

#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace trieste
{
  struct indent
  {
    size_t level;

    indent(size_t level) : level(level) {}
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

    Location fresh()
    {
      return Location("$" + std::to_string(next_id++));
    }

    void clear()
    {
      // Don't reset next_id, so that we don't reuse identifiers.
      symbols.clear();
      includes.clear();
    }

    std::string str(size_t level);
  };

  using Symtab = std::shared_ptr<SymtabDef>;

  struct Index
  {
    Token type;
    size_t index;

    constexpr Index() : type(Invalid), index(std::numeric_limits<size_t>::max())
    {}
    constexpr Index(const Token& type, size_t index) : type(type), index(index)
    {}
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
      auto p = parent_;

      while (p)
      {
        if (p->type_ == type)
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

    bool empty()
    {
      return children.empty();
    }

    size_t size()
    {
      return children.size();
    }

    Node& at(size_t index)
    {
      return children.at(index);
    }

    template<typename... Ts>
    Node& at(const Index& index, const Ts&... indices)
    {
      if (index.type != type_)
      {
        if constexpr (sizeof...(Ts) > 0)
          return at(indices...);
        else
          throw std::runtime_error("invalid index");
      }

      return children.at(index.index);
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
        push_back(it);
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
    Nodes get_symbols(F&& f)
    {
      if (!symtab_)
        return {};

      Nodes result;

      for (auto& [loc, nodes] : symtab_->symbols)
        std::copy_if(nodes.begin(), nodes.end(), std::back_inserter(result), f);

      return result;
    }

    template<typename F>
    Nodes get_symbols(const Location& loc, F&& f)
    {
      if (!symtab_)
        return {};

      auto it = symtab_->symbols.find(loc);
      if (it == symtab_->symbols.end())
        return {};

      Nodes result;
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
      return lookup(location_, until);
    }

    Nodes lookup(const Location& loc, Node until = {})
    {
      auto st = scope();
      if (!st)
        return {};

      // If the type of the symbol table is flag::defbeforeuse, then the
      // definition has to appear earlier in the same file.
      auto result = st->get_symbols(loc, [&](auto& n) {
        return (n->type() & flag::lookup) &&
          (!(st->type() & flag::defbeforeuse) || n->location_.before(loc));
      });

      // Includes are always returned, regardless of what's being looked up.
      result.insert(
        result.end(),
        st->symtab_->includes.begin(),
        st->symtab_->includes.end());

      // Sort the results by definition location, with the latest position in
      // the file coming first.
      if (st->type() & flag::defbeforeuse)
      {
        std::sort(result.begin(), result.end(), [](auto& a, auto& b) {
          return b->location_.before(a->location_);
        });
      }

      // If we haven't reached the scope limit and there are no shadowing
      // definitions, append any parent lookup results.
      if (
        (st != until) &&
        !std::any_of(result.begin(), result.end(), [](auto& n) {
          return n->type() & flag::shadowing;
        }))
      {
        auto presult = st->lookup(loc);
        result.insert(result.end(), presult.begin(), presult.end());
      }

      return result;
    }

    Nodes lookdown(const Location& loc)
    {
      // This is used for scoped resolution, where we're looking in this symbol
      // table specifically. Don't use includes, as those are for lookup only.
      return get_symbols(
        loc, [](auto& n) { return n->type() & flag::lookdown; });
    }

    Nodes look(const Location& loc)
    {
      // This is used for immediate resolution in the parent scope, ignoring
      // flag::lookup and flag::lookdown.
      return get_symbols(loc, [](auto&) { return true; });
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

    Location fresh()
    {
      // This actually returns a unique name, rather than a fresh one.
      auto p = this;

      while (p->parent_)
        p = p->parent_;

      if (p->type_ != Top)
        throw std::runtime_error("No Top node");

      return p->symtab_->fresh();
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

    std::string str(size_t level = 0)
    {
      std::stringstream ss;
      ss << indent(level) << "(" << type_.str();

      if (type_ & flag::print)
        ss << " " << location_.view().size() << ":" << location_.view();

      if (symtab_)
        ss << std::endl << symtab_->str(level + 1);

      for (auto child : children)
        ss << std::endl << child->str(level + 1);

      ss << ")";
      return ss.str();
    }

    bool errors(std::ostream& out)
    {
      bool err = false;

      for (auto& child : children)
      {
        if (child->errors(out))
          err = true;
      }

      if (!err && (type_ == Error))
      {
        auto msg = children.at(0);
        auto ast = children.at(1);
        out << ast->location().origin_linecol() << msg->location().view()
            << std::endl
            << ast->location().str() << std::endl;
        err = true;
      }

      return err;
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

  inline std::string SymtabDef::str(size_t level)
  {
    std::stringstream ss;
    ss << indent(level) << "{";

    for (auto& [loc, sym] : symbols)
    {
      ss << std::endl << indent(level + 1) << loc.view() << " =";

      if (sym.size() == 1)
      {
        ss << " " << sym.back()->type().str();
      }
      else
      {
        for (auto& node : sym)
          ss << std::endl << indent(level + 2) << node->type().str();
      }
    }

    for (auto& node : includes)
    {
      ss << std::endl
         << indent(level + 1) << "include " << node->location().view();
    }

    ss << "}";
    return ss.str();
  }

  inline std::ostream& operator<<(std::ostream& os, const Node& node)
  {
    if (node)
      os << node->str() << std::endl;
    return os;
  }

  inline std::ostream& operator<<(std::ostream& os, const NodeRange& range)
  {
    for (auto it = range.first; it != range.second; ++it)
      os << (*it)->str();
    return os;
  }
}

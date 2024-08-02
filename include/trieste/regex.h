// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "logging.h"
#include "ast.h"

#include <re2/re2.h>

namespace trieste
{
  class REMatch
  {
    friend class REIterator;

  private:
    std::vector<re2::StringPiece> match;
    std::vector<Location> locations;
    size_t matches = 0;

    bool match_regexp(const RE2& regex, re2::StringPiece& sp, Source& source)
    {
      matches = regex.NumberOfCapturingGroups() + 1;

      if (match.size() < matches)
        match.resize(matches);

      if (locations.size() < matches)
        locations.resize(matches);

      auto matched = regex.Match(
        sp,
        0,
        sp.length(),
        re2::RE2::ANCHOR_START,
        match.data(),
        static_cast<int>(matches));

      if (!matched)
      {
        return false;
      }

      for (size_t i = 0; i < matches; i++)
      {
        locations[i] = {
          source,
          static_cast<size_t>(match.at(i).data() - source->view().data()),
          match.at(i).size()};
      }

      return true;
    }

  public:
    REMatch(size_t max_capture = 0)
    {
      match.resize(max_capture + 1);
      locations.resize(max_capture + 1);
    }

    const Location& at(size_t index = 0) const
    {
      if (index >= matches)
        return locations.at(0);

      return locations.at(index);
    }

    template<typename T>
    T parse(size_t index = 0) const
    {
      if (index >= matches)
        return T();

      T t;
      RE2::Arg arg(&t);
      auto& m = match.at(index);
      arg.Parse(m.data(), m.size());
      return t;
    }
  };

  class REIterator
  {
  private:
    Source source;
    re2::StringPiece sp;

  public:
    REIterator(Source source_) : source(source_), sp(source_->view()) {}

    bool empty()
    {
      return sp.empty();
    }

    bool consume(const RE2& regex, REMatch& m)
    {
      if (!m.match_regexp(regex, sp, source))
        return false;

      sp.remove_prefix(m.at(0).len);
      return true;
    }

    Location current() const
    {
      return {
        source, static_cast<size_t>(sp.data() - source->view().data()), 1};
    }

    void skip(size_t count = 1)
    {
      sp.remove_prefix(count);
    }
  };

  inline Node build_ast(Source source, size_t pos)
  {
    auto hd = RE2("[[:space:]]*\\([[:space:]]*([^[:space:]\\(\\)]*)");
    auto st = RE2("[[:space:]]*\\{[^\\}]*\\}");
    auto id = RE2("[[:space:]]*([[:digit:]]+):");
    auto tl = RE2("[[:space:]]*\\)");

    REMatch re_match(2);
    REIterator re_iterator(source);
    re_iterator.skip(pos);

    Node top;
    Node ast;

    while (!re_iterator.empty())
    {
      // Find the type of the node. If we didn't find a node, it's an error.
      if (!re_iterator.consume(hd, re_match))
      {
        auto loc = re_iterator.current();
        logging::Error() << loc.origin_linecol() << ": expected node"
                         << std::endl
                         << loc.str() << std::endl;
        return {};
      }

      // If we don't have a valid node type, it's an error.
      auto type_loc = re_match.at(1);
      auto type = detail::find_token(type_loc.view());

      if (type == Invalid)
      {
        logging::Error() << type_loc.origin_linecol() << ": unknown type"
                         << std::endl
                         << type_loc.str() << std::endl;
        return {};
      }

      // Find the source location of the node as a netstring.
      auto ident_loc = type_loc;

      if (re_iterator.consume(id, re_match))
      {
        auto len = re_match.parse<size_t>(1);
        ident_loc =
          Location(source, re_match.at().pos + re_match.at().len, len);
        re_iterator.skip(len);
      }

      // Push the node into the AST.
      auto node = NodeDef::create(type, ident_loc);

      if (ast)
        ast->push_back(node);
      else
        top = node;

      ast = node;

      // Skip the symbol table.
      re_iterator.consume(st, re_match);

      // `)` ends the node. Otherwise, we'll add children to this node.
      while (re_iterator.consume(tl, re_match))
      {
        auto parent = ast->parent();

        if (!parent)
          return ast;

        ast = parent->intrusive_ptr_from_this();
      }
    }

    // We never finished the AST, so it's an error.
    auto loc = re_iterator.current();
    logging::Error() << loc.origin_linecol() << ": incomplete AST" << std::endl
                     << loc.str() << std::endl;
    return {};
  }
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "gen.h"
#include "regex.h"

#include <filesystem>
#include <functional>

namespace trieste
{
  class Parse;

  namespace detail
  {
    class Make;
    using ParseEffect = std::function<void(Make&)>;

    class RuleDef
    {
      friend class trieste::Parse;

    private:
      RE2 regex;
      ParseEffect effect;

    public:
      RuleDef(const std::string& s, ParseEffect effect)
      : regex(s), effect(effect)
      {}
    };

    using Rule = std::shared_ptr<RuleDef>;

    class Make
    {
      friend class trieste::Parse;

    private:
      Node top;
      Node node;
      std::string mode_;
      REMatch re_match;
      REIterator re_iterator;

    public:
      Make(const std::string& name, const Token& token, const Source& source)
      : re_match(10), re_iterator(source)
      {
        node = NodeDef::create(token, {name});
        top = node;
      }

      const Location& match(size_t index = 0) const
      {
        return re_match.at(index);
      }

      const std::string& mode()
      {
        return mode_;
      }

      void mode(const std::string& next)
      {
        mode_ = next;
      }

      bool in(const Token& type) const
      {
        return node->type() == type;
      }

      bool previous(const Token& type) const
      {
        if (!in(Group))
          return false;

        auto n = node->back();
        return n && (n->type() == type);
      }

      void error(const std::string& msg, size_t index = 0)
      {
        if (!in(Group))
          push(Group);

        node->push_back(make_error(re_match.at(index), msg));
      }

      void add(const Token& type, size_t index = 0)
      {
        if ((type != Group) && !in(Group))
          push(Group);

        node->push_back(NodeDef::create(type, re_match.at(index)));
      }

      void seq(const Token& type, std::initializer_list<Token> skip = {})
      {
        if (!in(Group))
          push(Group);

        while (node->parent()->type().in(skip))
          node = node->parent()->shared_from_this();

        auto p = node->parent();

        if (p->type() == type)
        {
          node = p->shared_from_this();
        }
        else
        {
          auto seq = NodeDef::create(type, re_match.at(0));
          auto group = p->pop_back();
          p->push_back(seq);
          seq->push_back(group);
          node = seq;
        }
      }

      void push(const Token& type, size_t index = 0)
      {
        add(type, index);
        node = node->back();
      }

      void pop(const Token& type)
      {
        if (!try_pop(type))
          invalid();
      }

      void term(std::initializer_list<Token> end = {})
      {
        try_pop(Group);

        for (auto& t : end)
          try_pop(t);
      }

      void extend_before(const Token& type)
      {
        if (!node->empty() && (node->front()->type() == type))
        {
          Location loc = re_match.at();
          loc.len = 0;
          node->front()->extend(loc);
        }
      }

      void extend(const Token& type, size_t index = 0)
      {
        if (!node->empty() && (node->back()->type() == type))
          node->back()->extend(re_match.at(index));
        else
          add(type, index);
      }

      void invalid()
      {
        extend(Invalid);
      }

    private:
      bool try_pop(const Token& type)
      {
        if (in(type))
        {
          if (!node->empty())
            node->extend(node->back()->location());

          node = node->parent()->shared_from_this();
          return true;
        }

        return false;
      }

      Node make_error(Location loc, const std::string& msg)
      {
        auto n = NodeDef::create(Error, loc);
        n->push_back(NodeDef::create(ErrorMsg, msg));
        n->push_back(NodeDef::create(ErrorAst, loc));
        return n;
      }

      Node done()
      {
        term();

        while (node->parent())
        {
          node->push_back(make_error(node->location(), "this is unclosed"));
          term();
          node = node->parent()->shared_from_this();
          term();
        }

        if (node != top)
          throw std::runtime_error("malformed AST");

        return top;
      }
    };
  }

  enum class depth
  {
    file,
    directory,
    subdirectories
  };

  class Parse
  {
  public:
    using PreF =
      std::function<bool(const Parse&, const std::filesystem::path&)>;
    using PostF =
      std::function<void(const Parse&, const std::filesystem::path&, Node)>;

  private:
    std::filesystem::path exe;
    depth depth_;

    PreF prefile_;
    PreF predir_;
    PostF postfile_;
    PostF postdir_;
    PostF postparse_;
    detail::ParseEffect done_;
    std::map<std::string, std::vector<detail::Rule>> rules;
    std::map<Token, GenLocationF> gens;

  public:
    Parse(depth depth_) : depth_(depth_) {}

    Parse& operator()(
      const std::string& mode, const std::initializer_list<detail::Rule> r)
    {
      rules[mode].insert(rules[mode].end(), r.begin(), r.end());
      return *this;
    }

    Parse& gen(std::initializer_list<std::pair<Token, GenLocationF>> g)
    {
      for (auto& [type, f] : g)
        gens[type] = f;

      return *this;
    }

    GenNodeLocationF generators() const
    {
      return [this](Rand& rnd, Node node) {
        auto find = gens.find(node->type());
        if (find == gens.end())
          return node->fresh();

        return Location(find->second(rnd));
      };
    }

    const std::filesystem::path& executable() const
    {
      return exe;
    }

    void executable(std::filesystem::path path)
    {
      exe = std::filesystem::canonical(path);
    }

    void prefile(PreF f)
    {
      prefile_ = f;
    }

    void predir(PreF f)
    {
      predir_ = f;
    }

    void postfile(PostF f)
    {
      postfile_ = f;
    }

    void postdir(PostF f)
    {
      postdir_ = f;
    }

    void postparse(PostF f)
    {
      postparse_ = f;
    }

    void done(detail::ParseEffect f)
    {
      done_ = f;
    }

    Node parse(std::filesystem::path path) const
    {
      auto ast = sub_parse(path);
      auto top = NodeDef::create(Top);
      top->push_back(ast);

      if (postparse_)
        postparse_(*this, path, top);

      return top;
    }

    Node sub_parse(std::filesystem::path& path) const
    {
      if (!std::filesystem::exists(path))
        return {};

      path = std::filesystem::canonical(path);

      if (std::filesystem::is_regular_file(path))
        return parse_file(path);

      if ((depth_ != depth::file) && std::filesystem::is_directory(path))
        return parse_directory(path);

      return {};
    }

    Node sub_parse(
      const std::string name, const Token& token, const Source& source) const
    {
      return parse_source(name, token, source);
    }

  private:
    Node parse_file(const std::filesystem::path& filename) const
    {
      if (prefile_ && !prefile_(*this, filename))
        return {};

      auto source = SourceDef::load(filename);
      auto ast = parse_source(filename.stem().string(), File, source);

      if (postfile_ && ast)
        postfile_(*this, filename, ast);

      return ast;
    }

    Node parse_source(
      const std::string name, const Token& token, const Source& source) const
    {
      if (!source)
        return {};

      auto make = detail::Make(name, token, source);

      // Find the start rules.
      auto find = rules.find("start");
      if (find == rules.end())
        throw std::runtime_error("unknown mode: start");

      auto mode = make.mode_ = find->first;

      while (!make.re_iterator.empty())
      {
        bool matched = false;

        for (auto& rule : find->second)
        {
          matched = make.re_iterator.consume(rule->regex, make.re_match);

          if (matched)
          {
            rule->effect(make);

            if (make.mode_ != mode)
            {
              find = rules.find(make.mode_);
              if (find == rules.end())
                throw std::runtime_error("unknown mode: " + make.mode_);

              mode = find->first;
            }
            break;
          }
        }

        if (!matched)
        {
          make.invalid();
          make.re_iterator.skip();
        }
      }

      if (done_)
        done_(make);

      return make.done();
    }

    Node parse_directory(const std::filesystem::path& dir) const
    {
      if (predir_ && !predir_(*this, dir))
        return {};

      Node top = NodeDef::create(Directory, {dir.stem().string()});

      for (const auto& entry : std::filesystem::directory_iterator(dir))
      {
        Node ast;

        if (std::filesystem::is_regular_file(entry.status()))
        {
          ast = parse_file(entry.path());
        }
        else if (
          (depth_ == depth::subdirectories) &&
          std::filesystem::is_directory(entry.status()))
        {
          ast = parse_directory(entry.path());
        }

        top->push_back(ast);
      }

      if (top->empty())
        return {};

      if (postdir_ && top)
        postdir_(*this, dir, top);

      return top;
    }
  };

  inline detail::Rule
  operator>>(const std::string& s, detail::ParseEffect effect)
  {
    return std::make_shared<detail::RuleDef>(s, effect);
  }

  inline std::pair<Token, GenLocationF>
  operator>>(const Token& t, GenLocationF f)
  {
    return {t, f};
  }
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "gen.h"
#include "logging.h"
#include "regex.h"
#include "wf.h"

#include <random>
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
      RuleDef(const std::string& s, ParseEffect effect_)
      : regex(s), effect(effect_)
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
        return node == type;
      }

      bool previous(const Token& type) const
      {
        if (!in(Group))
          return false;

        auto n = node->back();
        return n && (n == type);
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

        if (p == type)
        {
          node = p->shared_from_this();
        }
        else
        {
          auto group = p->pop_back();
          auto seq = NodeDef::create(type, re_match.at(0) * group->location());
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
        if (!node->empty() && (node->front() == type))
        {
          Location loc = re_match.at();
          loc.len = 0;
          node->front()->extend(loc);
        }
      }

      void extend(const Token& type, size_t index = 0)
      {
        if (!node->empty() && (node->back() == type))
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
    depth depth_;
    const wf::Wellformed& wf_ = wf::empty;
    std::filesystem::path exe;

    PreF prefile_;
    PreF predir_;
    PostF postfile_;
    PostF postdir_;
    PostF postparse_;
    detail::ParseEffect done_;
    std::map<std::string, std::vector<detail::Rule>> rules;
    std::map<Token, GenLocationF> gens;

  public:
    Parse(depth depth) : depth_(depth) {}

    Parse(depth depth, const wf::Wellformed& wf) : depth_(depth), wf_(wf) {}

    const wf::Wellformed& wf() const
    {
      return wf_;
    }

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
          return ast::fresh();

        return Location(find->second(rnd));
      };
    }

    const std::filesystem::path& executable() const
    {
      return exe;
    }

    void executable(const std::filesystem::path path)
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

    Node parse(const std::filesystem::path path) const
    {
      auto ast = sub_parse(path);
      auto top = NodeDef::create(Top);
      top->push_back(ast);

      if (postparse_)
        postparse_(*this, path, top);

      return top;
    }

    Node sub_parse(const std::filesystem::path& path) const
    {
      if (!std::filesystem::exists(path))
        return {};

      auto cpath = std::filesystem::canonical(path);

      if (std::filesystem::is_regular_file(cpath))
        return parse_file(cpath);

      if ((depth_ != depth::file) && std::filesystem::is_directory(cpath))
        return parse_directory(cpath);

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

      std::set<std::filesystem::path> dirs;
      std::set<std::filesystem::path> files;

      for (const auto& entry : std::filesystem::directory_iterator(dir))
      {
        if (
          (depth_ == depth::subdirectories) &&
          std::filesystem::is_directory(entry.status()))
        {
          dirs.insert(entry.path());
        }
        else if (std::filesystem::is_regular_file(entry.status()))
        {
          files.insert(entry.path());
        }
      }

      auto top = NodeDef::create(Directory, {dir.stem().string()});
      ast::detail::top_node() = top;

      for (auto& subdir : dirs)
        top->push_back(parse_directory(subdir));

      for (auto& file : files)
        top->push_back(parse_file(file));

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

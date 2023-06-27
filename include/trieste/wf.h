// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "gen.h"

#include <array>
#include <variant>

/* Notes on how to use the Well-formedness checker:
 *
 * If a pass redefines the shape of a node, it must also wrap any old instances
 * of that node in an Error node. Otherwise, the QuickCheck will blame that
 * pass for being ill-formed.
 */

namespace trieste
{
  namespace wf
  {
    using Nonterminal = std::function<bool(const Token&)>;

    struct Gen
    {
      Nonterminal nonterminal;
      GenNodeLocationF gloc;
      Rand rand;
      size_t max_depth;

      Gen(
        Nonterminal nonterminal,
        GenNodeLocationF gloc,
        Seed seed,
        size_t max_depth)
      : nonterminal(nonterminal), gloc(gloc), rand(seed), max_depth(max_depth)
      {}

      Result next()
      {
        return rand();
      }

      Location location(Node n)
      {
        return gloc(rand, n);
      }
    };

    struct Choice
    {
      std::vector<Token> types;

      bool check(Node node, std::ostream& out) const
      {
        if (node->type() == Error)
          return true;

        auto ok = false;

        for (auto& type : types)
        {
          if (node->type() == type)
          {
            ok = true;
            break;
          }
        }

        if (!ok)
        {
          out << node->location().origin_linecol() << "unexpected "
              << node->type().str() << ", expected a ";

          for (size_t i = 0; i < types.size(); ++i)
          {
            out << types[i].str();

            if (i < (types.size() - 2))
              out << ", ";
            if (i == (types.size() - 2))
              out << " or ";
          }

          out << std::endl
              << node->location().str() << node->str() << std::endl;
        }

        return ok;
      }

      void gen(Gen& g, size_t depth, Node node) const
      {
        Token type;

        if (depth < g.max_depth)
        {
          type = types[g.next() % types.size()];
        }
        else
        {
          auto i = g.next() % types.size();
          auto wrap = i;
          type = types[i];

          do
          {
            if (!g.nonterminal(types[i]))
            {
              type = types[i];
              break;
            }

            // If all choices are nonterminal, use the initially selected one.
            i = (i + 1) % types.size();
          } while (i != wrap);
        }

        // We may need a fresh location, so the child needs to be in the AST by
        // the time we call g.location().
        auto child = NodeDef::create(type);
        node->push_back(child);
        child->set_location(g.location(child));
      }
    };

    struct Sequence
    {
      Choice choice;
      size_t minlen;

      Index index(const Token&, const Token&) const
      {
        return {};
      }

      Sequence& operator[](size_t new_minlen)
      {
        minlen = new_minlen;
        return *this;
      }

      Sequence& operator[](const Token&)
      {
        // Do nothing.
        return *this;
      }

      bool check(Node node, std::ostream& out) const
      {
        auto has_err = false;
        auto ok = true;

        for (auto& child : *node)
        {
          has_err = has_err || (child->type() == Error);
          ok = choice.check(child, out) && ok;
        }

        if (!has_err && (node->size() < minlen))
        {
          out << node->location().origin_linecol() << "expected at least "
              << minlen << " children, found " << node->size() << std::endl
              << node->location().str() << node->str() << std::endl;
          ok = false;
        }

        return ok;
      }

      bool build_st(Node, std::ostream&) const
      {
        // Do nothing.
        return true;
      }

      void gen(Gen& g, size_t depth, Node node) const
      {
        for (size_t i = 0; i < minlen; ++i)
          choice.gen(g, depth, node);

        while (g.next() % 2)
          choice.gen(g, depth, node);
      }
    };

    struct Field
    {
      Token name;
      Choice choice;
    };

    struct Fields
    {
      std::vector<Field> fields;
      Token binding;

      Index index(const Token& type, const Token& field) const
      {
        auto i = 0;

        for (auto& f : fields)
        {
          if (f.name == field)
            return Index(type, i);

          ++i;
        }

        return {};
      }

      Fields& operator[](const Token& type)
      {
        this->binding = type;
        return *this;
      }

      bool check(Node node, std::ostream& out) const
      {
        auto field = fields.begin();
        auto end = fields.end();
        bool ok = true;
        bool has_error = false;

        for (auto& child : *node)
        {
          // A node that contains an Error node stops checking well-formedness
          // from that point.
          if (child->type() == Error)
          {
            has_error = true;
            break;
          }

          // If we run out of fields, the node is ill-formed.
          if (field == end)
            break;

          ok = field->choice.check(child, out) && ok;

          if ((binding != Invalid) && (field->name == binding))
          {
            auto defs = node->scope()->look(child->location());
            auto find = std::find(defs.begin(), defs.end(), node);

            if (find == defs.end())
            {
              out << child->location().origin_linecol()
                  << "missing symbol table binding for " << node->type().str()
                  << std::endl
                  << child->location().str() << node->str() << std::endl;
              ok = false;
            }
          }

          ++field;
        }

        if (!has_error && (node->size() != fields.size()))
        {
          out << node->location().origin_linecol() << "expected "
              << fields.size() << " children, found " << node->size()
              << std::endl
              << node->location().str() << node->str() << std::endl;
          ok = false;
        }

        return ok;
      }

      void gen(Gen& g, size_t depth, Node node) const
      {
        for (auto& field : fields)
        {
          field.choice.gen(g, depth, node);

          if (binding == field.name)
            node->bind(node->back()->location());
        }
      }

      bool build_st(Node node, std::ostream& out) const
      {
        if (binding == Invalid)
          return true;

        if (binding == Include)
        {
          node->include();
          return true;
        }

        size_t index = 0;

        for (auto& field : fields)
        {
          if (field.name == binding)
          {
            auto name = node->at(index)->location();

            if (!node->bind(name))
            {
              auto defs = node->scope()->look(name);
              out << node->location().origin_linecol()
                  << "conflicting definitions of `" << name.view()
                  << "`:" << std::endl;

              for (auto def : defs)
                out << def->location().str();

              return false;
            }

            return true;
          }

          ++index;
        }

        out << node->location().origin_linecol() << "no binding found for "
            << node->type().str() << std::endl
            << node->location().str() << node->str() << std::endl;
        return false;
      }
    };

    using ShapeT = std::variant<Sequence, Fields>;

    template<class... Ts>
    struct overload : Ts...
    {
      using Ts::operator()...;
    };

    template<class... Ts>
    overload(Ts...) -> overload<Ts...>;

    struct Shape
    {
      Token type;
      ShapeT shape;

      Shape& operator[](const Token& binding)
      {
        std::visit([&](auto& s) { s[binding]; }, shape);
        return *this;
      }
    };

    struct Wellformed
    {
      std::map<Token, ShapeT> shapes;
      Tokens types;

      operator bool() const
      {
        return !shapes.empty();
      }

      Index index(const Token& type, const Token& field) const
      {
        auto find = shapes.find(type);

        if (find == shapes.end())
          return {};

        return std::visit(
          [&](auto& shape) { return shape.index(type, field); }, find->second);
      }

      void prepend(const Shape& shape)
      {
        auto find = shapes.find(shape.type);
        if (find == shapes.end())
          append(shape);
      }

      void prepend(Shape&& shape)
      {
        auto find = shapes.find(shape.type);
        if (find == shapes.end())
          append(shape);
      }

      void append(const Shape& shape)
      {
        register_shape(shape);
        shapes[shape.type] = shape.shape;
      }

      void append(Shape&& shape)
      {
        register_shape(shape);
        shapes[shape.type] = std::move(shape.shape);
      }

      void register_shape(const Shape& shape)
      {
        register_token(types, shape.type);

        std::visit(
          overload{
            [&](const Sequence& sequence) {
              register_tokens(types, sequence.choice.types);
            },
            [&](const Fields& fields) {
              for (auto& field : fields.fields)
                register_tokens(types, field.choice.types);
            }},
          shape.shape);
      }

      bool check(Node node, std::ostream& out) const
      {
        if (!node)
          return false;

        if (node->type() == Error)
          return true;

        auto find = shapes.find(node->type());

        if (find == shapes.end())
        {
          // If the shape isn't present, assume it should be empty.
          if (node->empty())
            return true;

          out << node->location().origin_linecol()
              << "expected 0 children, found " << node->size() << std::endl
              << node->location().str() << node->str() << std::endl;
          return false;
        }

        bool ok = std::visit(
          [&](auto& shape) { return shape.check(node, out); }, find->second);

        for (auto& child : *node)
        {
          if (child->parent() != node.get())
          {
            out << child->location().origin_linecol()
                << "this node appears in the AST multiple times:" << std::endl
                << child->location().str() << child->str() << std::endl
                << node->location().origin_linecol() << "here:" << std::endl
                << node->str() << std::endl
                << child->parent()->location().origin_linecol()
                << "and here:" << std::endl
                << child->parent()->str() << std::endl
                << "Your language implementation needs to explicitly clone "
                   "nodes if they're duplicated.";
            ok = false;
          }

          ok = check(child, out) && ok;
        }

        return ok;
      }

      Node gen(GenNodeLocationF gloc, Seed seed, size_t max_depth) const
      {
        auto g = Gen(
          [this](const Token& type) {
            return shapes.find(type) != shapes.end();
          },
          gloc,
          seed,
          max_depth);

        auto node = NodeDef::create(Top);
        gen_node(g, 0, node);
        return node;
      }

      void gen_node(Gen& g, size_t depth, Node node) const
      {
        if (!node)
          return;

        // If the shape isn't present, do nothing, as we assume it should be
        // empty.
        auto find = shapes.find(node->type());
        if (find == shapes.end())
          return;

        std::visit(
          [&](auto& shape) { shape.gen(g, depth, node); }, find->second);

        for (auto& child : *node)
          gen_node(g, depth + 1, child);
      }

      bool build_st(Node node, std::ostream& out) const
      {
        if (!node)
          return false;

        if (node->type() == Error)
          return true;

        node->clear_symbols();

        bool ok = true;
        auto find = shapes.find(node->type());

        if (find != shapes.end())
        {
          ok = std::visit(
            [&](auto& shape) { return shape.build_st(node, out); },
            find->second);
        }

        for (auto& child : *node)
          ok = build_st(child, out) && ok;

        return ok;
      }

      Node build_ast(Source source, size_t pos, std::ostream& out) const
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
            out << loc.origin_linecol() << "expected node" << std::endl
                << loc.str() << std::endl;
            return {};
          }

          // If we don't have a valid node type, it's an error.
          auto type_loc = re_match.at(1);
          auto find = types.find(type_loc.view());

          if (find == types.end())
          {
            out << type_loc.origin_linecol() << "unknown type" << std::endl
                << type_loc.str() << std::endl;
            return {};
          }

          auto type = find->second;

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

            ast = parent->shared_from_this();
          }
        }

        // We never finished the AST, so it's an error.
        auto loc = re_iterator.current();
        out << loc.origin_linecol() << "incomplete AST" << std::endl
            << loc.str() << std::endl;
        return {};
      }
    };

    namespace ops
    {
      inline Choice operator|(const Token& type1, const Token& type2)
      {
        return Choice{std::vector<Token>{type1, type2}};
      }

      inline Choice operator|(const Token& type, const Choice& choice)
      {
        Choice result{choice.types};
        result.types.push_back(type);
        return result;
      }

      inline Choice operator|(const Token& type, Choice&& choice)
      {
        choice.types.push_back(type);
        return std::move(choice);
      }

      inline Choice operator|(const Choice& choice1, const Choice& choice2)
      {
        Choice result{choice1.types};
        result.types.insert(
          result.types.end(), choice2.types.begin(), choice2.types.end());
        return result;
      }

      inline Choice operator|(const Choice& choice1, Choice&& choice2)
      {
        choice2.types.insert(
          choice2.types.end(), choice1.types.begin(), choice1.types.end());
        return std::move(choice2);
      }

      inline Choice operator|(const Choice& choice, const Token& type)
      {
        return type | choice;
      }

      inline Choice operator|(Choice&& choice, const Token& type)
      {
        return type | choice;
      }

      inline Choice operator|(Choice&& choice1, const Choice& choice2)
      {
        return choice2 | choice1;
      }

      inline Sequence operator++(const Token& type, int)
      {
        return Sequence{Choice{std::vector<Token>{type}}, 0};
      }

      inline Sequence operator++(const Choice& choice, int)
      {
        return Sequence{choice, 0};
      }

      inline Sequence operator++(Choice&& choice, int)
      {
        return Sequence{choice, 0};
      }

      inline Field operator>>=(const Token& name, const Token& type)
      {
        return Field{name, Choice{std::vector<Token>{type}}};
      }

      inline Field operator>>=(const Token& name, const Choice& choice)
      {
        return Field{name, choice};
      }

      inline Field operator>>=(const Token& name, Choice&& choice)
      {
        return Field{name, choice};
      }

      inline Fields operator*(const Field& fst, const Field& snd)
      {
        return Fields{std::vector<Field>{fst, snd}, Invalid};
      }

      inline Fields operator*(const Field& fst, Field&& snd)
      {
        return Fields{std::vector<Field>{fst, snd}, Invalid};
      }

      inline Fields operator*(Field&& fst, const Field& snd)
      {
        return Fields{std::vector<Field>{fst, snd}, Invalid};
      }

      inline Fields operator*(Field&& fst, Field&& snd)
      {
        return Fields{std::vector<Field>{fst, snd}, Invalid};
      }

      inline Fields operator*(const Token& fst, const Token& snd)
      {
        return (fst >>= fst) * (snd >>= snd);
      }

      inline Fields operator*(const Field& fst, const Token& snd)
      {
        return fst * (snd >>= snd);
      }

      inline Fields operator*(Field&& fst, const Token& snd)
      {
        return fst * (snd >>= snd);
      }

      inline Fields operator*(const Token& fst, const Field& snd)
      {
        return (fst >>= fst) * snd;
      }

      inline Fields operator*(const Token& fst, Field&& snd)
      {
        return (fst >>= fst) * snd;
      }

      inline Fields operator*(const Fields& fst, const Field& snd)
      {
        auto fields = Fields{fst.fields, Invalid};
        fields.fields.push_back(snd);
        return fields;
      }

      inline Fields operator*(Fields&& fst, const Field& snd)
      {
        fst.fields.push_back(snd);
        return std::move(fst);
      }

      inline Fields operator*(Fields&& fst, const Token& snd)
      {
        return fst * (snd >>= snd);
      }

      inline Shape operator<<=(const Token& type, const Fields& fields)
      {
        return Shape{type, fields};
      }

      inline Shape operator<<=(const Token& type, const Sequence& seq)
      {
        return Shape{type, seq};
      }

      inline Shape operator<<=(const Token& type, const Field& field)
      {
        return type <<= Fields{std::vector<Field>{field}, Invalid};
      }

      inline Shape operator<<=(const Token& type, Field&& field)
      {
        return type <<= Fields{std::vector<Field>{field}, Invalid};
      }

      inline Shape operator<<=(const Token& type, const Choice& choice)
      {
        return type <<= (type >>= choice);
      }

      inline Shape operator<<=(const Token& type, Choice&& choice)
      {
        return type <<= (type >>= choice);
      }

      inline Shape operator<<=(const Token& type1, const Token& type2)
      {
        return type1 <<= (type2 >>= type2);
      }

      inline Wellformed operator|(const Wellformed& wf1, const Wellformed& wf2)
      {
        Wellformed wf;
        wf.shapes.insert(wf2.shapes.begin(), wf2.shapes.end());
        wf.types.insert(wf2.types.begin(), wf2.types.end());
        wf.shapes.insert(wf1.shapes.begin(), wf1.shapes.end());
        wf.types.insert(wf1.types.begin(), wf1.types.end());
        return wf;
      }

      inline Wellformed operator|(Wellformed&& wf1, const Wellformed& wf2)
      {
        std::for_each(
          wf2.shapes.begin(), wf2.shapes.end(), [&](const auto& shape) {
            wf1.shapes.insert_or_assign(shape.first, shape.second);
          });

        std::for_each(
          wf2.types.begin(), wf2.types.end(), [&](const auto& type) {
            wf1.types.insert_or_assign(type.first, type.second);
          });

        return std::move(wf1);
      }

      inline Wellformed operator|(const Wellformed& wf1, Wellformed&& wf2)
      {
        wf2.shapes.insert(wf1.shapes.begin(), wf1.shapes.end());
        wf2.types.insert(wf1.types.begin(), wf1.types.end());
        return std::move(wf2);
      }

      inline Wellformed operator|(Wellformed&& wf1, Wellformed&& wf2)
      {
        wf2.shapes.merge(wf1.shapes);
        wf2.types.merge(wf1.types);
        return std::move(wf2);
      }

      inline Wellformed operator|(const Wellformed& wf, const Shape& shape)
      {
        Wellformed wf2;
        wf2.append(shape);
        wf2.shapes.insert(wf.shapes.begin(), wf.shapes.end());
        wf2.types.insert(wf.types.begin(), wf.types.end());
        return wf2;
      }

      inline Wellformed operator|(const Wellformed& wf, Shape&& shape)
      {
        Wellformed wf2;
        wf2.append(shape);
        wf2.shapes.insert(wf.shapes.begin(), wf.shapes.end());
        wf2.types.insert(wf.types.begin(), wf.types.end());
        return wf2;
      }

      inline Wellformed operator|(Wellformed&& wf, const Shape& shape)
      {
        wf.append(shape);
        return std::move(wf);
      }

      inline Wellformed operator|(Wellformed&& wf, Shape&& shape)
      {
        wf.append(shape);
        return std::move(wf);
      }

      inline Wellformed operator|(const Shape& shape, const Wellformed& wf)
      {
        Wellformed wf2;
        wf2.append(shape);
        return wf2 | wf;
      }

      inline Wellformed operator|(Shape&& shape, const Wellformed& wf)
      {
        Wellformed wf2;
        wf2.append(shape);
        return wf2 | wf;
      }

      inline Wellformed operator|(const Shape& shape, Wellformed&& wf)
      {
        wf.prepend(shape);
        return std::move(wf);
      }

      inline Wellformed operator|(Shape&& shape, Wellformed&& wf)
      {
        wf.prepend(shape);
        return std::move(wf);
      }

      inline Wellformed operator|(const Shape& shape1, const Shape& shape2)
      {
        Wellformed wf;
        wf.append(shape1);
        wf.append(shape2);
        return wf;
      }

      inline Wellformed operator|(const Shape& shape1, Shape&& shape2)
      {
        Wellformed wf;
        wf.append(shape1);
        wf.append(shape2);
        return wf;
      }

      inline Wellformed operator|(Shape&& shape1, const Shape& shape2)
      {
        Wellformed wf;
        wf.append(shape1);
        wf.append(shape2);
        return wf;
      }

      inline Wellformed operator|(Shape&& shape1, Shape&& shape2)
      {
        Wellformed wf;
        wf.append(shape1);
        wf.append(shape2);
        return wf;
      }
    }
  }

  inline auto operator/(const wf::Wellformed& wf, const Token& type)
  {
    return std::make_pair(&wf, type);
  }

  inline Index operator/(
    const std::pair<const wf::Wellformed*, Token>& pair, const Token& name)
  {
    return pair.first->index(pair.second, name);
  }
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "gen.h"

#include <array>
#include <tuple>

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
    struct Gen
    {
      GenNodeLocationF gloc;
      Rand rand;
      size_t max_depth;
      std::set<Token> nonterminals;

      Gen(GenNodeLocationF gloc, Seed seed, size_t max_depth)
      : gloc(gloc), rand(seed), max_depth(max_depth)
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

    template<size_t N>
    struct Choice
    {
      std::array<Token, N> types;

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

          size_t n = 0;
          for (auto& type : types)
          {
            out << type.str();
            ++n;

            if (n <= (N - 1))
              out << ", ";
            if (n == (N - 1))
              out << "or ";
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
          type = types[g.next() % N];
        }
        else
        {
          std::vector<Token> filtered;

          for (size_t i = 0; i < N; ++i)
          {
            auto find = g.nonterminals.find(types[i]);
            if (find == g.nonterminals.end())
              filtered.push_back(types[i]);
          }

          if (filtered.size() == 0)
            type = types[g.next() % N];
          else
            type = filtered.at(g.next() % filtered.size());
        }

        // We may need a fresh location, so the child needs to be in the AST by
        // the time we call g.location().
        auto child = NodeDef::create(type);
        node->push_back(child);
        child->set_location(g.location(child));
      }

      Token find_type(const Location& type) const
      {
        for (auto& t : types)
        {
          if (t.str() == type.view())
            return t;
        }

        return Invalid;
      }
    };

    struct SequenceBase
    {};

    template<size_t N>
    struct Sequence : SequenceBase
    {
      Choice<N> types;
      size_t minlen;
      Token binding;

      CONSTEVAL Sequence(const Choice<N>& types)
      : types(types), minlen(0), binding(Invalid)
      {}
      CONSTEVAL Sequence(const Sequence<N>& seq, const Token& binding)
      : types(seq.types), minlen(seq.minlen), binding(binding)
      {}
      CONSTEVAL Sequence(const Choice<N>& types, size_t minlen, Token binding)
      : types(types), minlen(minlen), binding(binding)
      {}

      CONSTEVAL auto operator[](size_t new_minlen) const
      {
        return Sequence<N>(types, new_minlen, binding);
      }

      constexpr bool terminal() const
      {
        return false;
      }

      bool check(Node node, std::ostream& out) const
      {
        auto has_err = false;
        auto ok = true;

        for (auto& child : *node)
        {
          has_err = has_err || (child->type() == Error);
          ok = types.check(child, out) && ok;
        }

        if (!has_err && (node->size() < minlen))
        {
          out << node->location().origin_linecol() << "expected at least "
              << minlen << " children, found " << node->size() << std::endl
              << node->location().str() << node->str() << std::endl;
          ok = false;
        }

        if (!binding.in({Invalid, Include}))
        {
          out << node->location().origin_linecol() << "can't bind a "
              << node->type().str() << " sequence in the symbol table"
              << std::endl
              << node->location().str() << node->str() << std::endl;
          ok = false;
        }

        return ok;
      }

      void gen(Gen& g, size_t depth, Node node) const
      {
        for (size_t i = 0; i < minlen; ++i)
          types.gen(g, depth, node);

        while (g.next() % 2)
          types.gen(g, depth, node);
      }

      bool build_st(Node node, std::ostream&) const
      {
        if (binding == Include)
          node->include();

        return true;
      }

      Token find_type(const Location& type) const
      {
        return types.find_type(type);
      }
    };

    template<size_t N>
    Sequence(const Choice<N>& types) -> Sequence<N>;

    template<size_t N>
    Sequence(size_t minlen, const Choice<N>& types) -> Sequence<N>;

    struct FieldBase
    {};

    template<size_t N>
    struct Field : FieldBase
    {
      Token name;
      Choice<N> types;
    };

    struct FieldsBase
    {};

    template<typename... Ts>
    struct Fields : FieldsBase
    {
      static_assert(
        std::conjunction_v<std::is_base_of<FieldBase, Ts>...>, "Not a Field");

      std::tuple<Ts...> fields;
      Token binding;

      CONSTEVAL Fields() : fields(), binding(Invalid) {}

      template<size_t N>
      CONSTEVAL Fields(const Field<N>& field)
      : fields(std::make_tuple(field)), binding(Invalid)
      {}

      template<typename... Ts2, typename... Ts3>
      CONSTEVAL Fields(const Fields<Ts2...>& fst, const Fields<Ts3...>& snd)
      : fields(std::tuple_cat(fst.fields, snd.fields)), binding(Invalid)
      {}

      template<typename... Ts2>
      CONSTEVAL Fields(const Fields<Ts2...>& fields, const Token& binding)
      : fields(fields.fields), binding(binding)
      {}

      constexpr bool terminal() const
      {
        return sizeof...(Ts) == 0;
      }

      bool check(Node node, std::ostream& out) const
      {
        return check_field<0>(true, node, node->begin(), node->end(), out);
      }

      template<size_t I>
      bool check_field(
        bool ok, Node node, NodeIt child, NodeIt end, std::ostream& out) const
      {
        if (child == end)
        {
          if constexpr (I < sizeof...(Ts))
          {
            // Too few child nodes.
            out << node->location().origin_linecol()
                << "too few child nodes in " << node->type().str() << std::endl
                << node->location().str() << node->str() << std::endl;
            return false;
          }
          else
          {
            return ok;
          }
        }

        // A node that contains an Error node stops checking well-formedness
        // from that point.
        if ((*child)->type() == Error)
          return ok;

        if constexpr (I >= sizeof...(Ts))
        {
          // Too many child nodes.
          out << (*child)->location().origin_linecol()
              << "too many child nodes in " << node->type().str() << std::endl
              << (*child)->location().str() << node->str() << std::endl;
          return false;
        }
        else
        {
          auto& field = std::get<I>(fields);
          ok = field.types.check(*child, out) && ok;

          if ((binding != Invalid) && (field.name == binding))
          {
            auto defs = node->scope()->look((*child)->location());
            auto find = std::find(defs.begin(), defs.end(), node);

            if (find == defs.end())
            {
              out << (*child)->location().origin_linecol()
                  << "missing symbol table binding for " << node->type().str()
                  << std::endl
                  << (*child)->location().str() << node->str() << std::endl;
              ok = false;
            }
          }

          return check_field<I + 1>(ok, node, ++child, end, out);
        }
      }

      void gen(Gen& g, size_t depth, Node node) const
      {
        gen_field<0>(g, depth, node);
      }

      template<size_t I>
      void gen_field(Gen& g, size_t depth, Node node) const
      {
        if constexpr (I < sizeof...(Ts))
        {
          auto& field = std::get<I>(fields);
          field.types.gen(g, depth, node);

          if (binding == field.name)
            node->bind(node->back()->location());

          gen_field<I + 1>(g, depth, node);
        }
      }

      bool build_st(Node node, std::ostream& out) const
      {
        if (binding == Include)
          node->include();
        else if (binding != Invalid)
          return build_st_i<0>(node, out);

        return true;
      }

      template<size_t I>
      bool build_st_i(Node node, std::ostream& out) const
      {
        if constexpr (I < sizeof...(Ts))
        {
          auto& field = std::get<I>(fields);

          if (binding == field.name)
          {
            auto name = node->at(I)->location();

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
          else
          {
            return build_st_i<I + 1>(node, out);
          }
        }
        else
        {
          out << node->location().origin_linecol() << "no binding found for "
              << node->type().str() << std::endl
              << node->location().str() << node->str() << std::endl;
          return false;
        }
      }

      Token find_type(const Location& type) const
      {
        return find_type_i<0>(type);
      }

      template<size_t I>
      Token find_type_i(const Location& type) const
      {
        if constexpr (I < sizeof...(Ts))
        {
          auto& field = std::get<I>(fields);
          auto t = field.types.find_type(type);

          if (t != Invalid)
            return t;
          else
            return find_type_i<I + 1>(type);
        }
        else
        {
          return Invalid;
        }
      }
    };

    Fields()->Fields<>;

    template<size_t N>
    Fields(const Field<N>& field) -> Fields<Field<N>>;

    template<typename... Ts2, typename... Ts3>
    Fields(const Fields<Ts2...>&, const Fields<Ts3...>&)
      -> Fields<Ts2..., Ts3...>;

    template<typename... Ts2>
    Fields(const Fields<Ts2...>&, const Token&) -> Fields<Ts2...>;

    struct ShapeBase
    {};

    template<typename T>
    struct Shape : ShapeBase
    {
      static_assert(
        std::is_base_of_v<FieldsBase, T> || std::is_base_of_v<SequenceBase, T>,
        "Not Fields or a Sequence");

      Token type;
      T shape;

      CONSTEVAL Shape(Token type, const T& shape) : type(type), shape(shape) {}

      CONSTEVAL auto operator[](const Token& binding) const
      {
        return Shape<T>(type, T(shape, binding));
      }
    };

    template<typename T>
    Shape(Token, const T&) -> Shape<T>;

    struct WellformedBase
    {};

    struct WellformedF
    {
      std::function<bool(Node, std::ostream&)> check;
      std::function<Node(GenNodeLocationF, Seed, size_t)> gen;
      std::function<bool(Node, std::ostream&)> build_st;
      std::function<Node(Source, size_t, std::ostream&)> build_ast;

      operator bool() const
      {
        return (check != nullptr) && (gen != nullptr);
      }
    };

    template<typename... Ts>
    struct Wellformed : WellformedBase
    {
      static_assert(
        std::conjunction_v<std::is_base_of<ShapeBase, Ts>...>, "Not a Shape");

      std::tuple<Ts...> shapes;

      CONSTEVAL Wellformed() : shapes() {}

      template<typename T>
      CONSTEVAL Wellformed(const Shape<T>& shape)
      : shapes(std::make_tuple(shape))
      {}

      template<typename... Ts1, typename... Ts2>
      CONSTEVAL
      Wellformed(const Wellformed<Ts1...>& wf1, const Wellformed<Ts2...>& wf2)
      : shapes(std::tuple_cat(wf1.shapes, wf2.shapes))
      {}

      template<size_t I = 0>
      void nonterminals(std::set<Token>& set) const
      {
        if constexpr (I < sizeof...(Ts))
        {
          auto& shape = std::get<I>(shapes);

          if (!shape.shape.terminal())
            set.insert(shape.type);

          nonterminals<I + 1>(set);
        }
      }

      bool check(Node node, std::ostream& out) const
      {
        if (!node)
          return false;

        if (node->type() == Error)
          return true;

        bool ok = check_i(node, out);

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
                << "Implementation needs to explicitly clone nodes if they are "
                   "duplicated.";
            ok = false;
          }

          ok = check(child, out) && ok;
        }

        return ok;
      }

      template<size_t I = sizeof...(Ts) - 1>
      bool check_i(Node node, std::ostream& out) const
      {
        // Check from the end, such that composition overrides any previous
        // definition of a shape.
        if constexpr (I >= sizeof...(Ts))
        {
          // If the shape isn't present, assume it should be empty.
          if (node->empty())
            return true;

          // Too many child nodes.
          out << node->location().origin_linecol() << "too many child nodes in "
              << node->type().str() << std::endl
              << node->location().str() << node->str() << std::endl;
          return false;
        }
        else
        {
          auto& shape = std::get<I>(shapes);

          if (node->type() == shape.type)
            return shape.shape.check(node, out);

          return check_i<I - 1>(node, out);
        }
      }

      Node gen(GenNodeLocationF gloc, Seed seed, size_t max_depth) const
      {
        auto g = Gen(gloc, seed, max_depth);
        nonterminals(g.nonterminals);
        auto node = NodeDef::create(Top);
        gen_i(g, 0, node);
        return node;
      }

      template<size_t I = sizeof...(Ts) - 1>
      void gen_i(Gen& g, size_t depth, Node node) const
      {
        // Generate from the end, such that composition overrides any previous
        // definition of a shape. If the shape isn't present, do nothing, as we
        // assume it should be empty.
        if constexpr (I < sizeof...(Ts))
        {
          auto& shape = std::get<I>(shapes);

          if (shape.type == node->type())
          {
            shape.shape.gen(g, depth, node);

            for (auto& child : *node)
              gen_i(g, depth + 1, child);
          }
          else
          {
            gen_i<I - 1>(g, depth, node);
          }
        }
      }

      bool build_st(Node node, std::ostream& out) const
      {
        if (node->type() == Error)
          return true;

        node->clear_symbols();
        auto ok = build_st_i(node, out);

        for (auto& child : *node)
          ok = build_st(child, out) && ok;

        return ok;
      }

      template<size_t I = sizeof...(Ts) - 1>
      bool build_st_i(Node node, std::ostream& out) const
      {
        if constexpr (I < sizeof...(Ts))
        {
          auto& shape = std::get<I>(shapes);

          if (shape.type == node->type())
            return shape.shape.build_st(node, out);
          else
            return build_st_i<I - 1>(node, out);
        }

        return true;
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
          auto type = find_type_i(ast, type_loc);

          if (type == Invalid)
          {
            out << type_loc.origin_linecol() << "unknown type" << std::endl
                << type_loc.str() << std::endl;
            return {};
          }

          // Find the source location of the node as a netstring.
          auto ident_loc = type_loc;

          if (re_iterator.consume(id, re_match))
          {
            auto len = re_match.parse<size_t>(1);
            ident_loc = Location(source, ident_loc.pos + ident_loc.len, len);
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

      template<size_t I = sizeof...(Ts) - 1>
      Token find_type_i(Node node, const Location& type) const
      {
        if constexpr (I < sizeof...(Ts))
        {
          auto& shape = std::get<I>(shapes);

          if (shape.type.str() == type.view())
            return shape.type;

          if (node && (shape.type == node->type()))
          {
            auto t = shape.shape.find_type(type);

            if (t != Invalid)
              return t;
          }

          return find_type_i<I - 1>(node, type);
        }
        else
        {
          return Invalid;
        }
      }

      auto operator()() const
      {
        return WellformedF{
          [this](Node node, std::ostream& out) { return check(node, out); },
          [this](GenNodeLocationF gloc, Seed seed, size_t max_depth) {
            return gen(gloc, seed, max_depth);
          },
          [this](Node node, std::ostream& out) { return build_st(node, out); },
          [this](Source source, size_t pos, std::ostream& out) {
            return build_ast(source, pos, out);
          },
        };
      }
    };

    Wellformed()->Wellformed<>;

    template<typename T>
    Wellformed(const Shape<T>&) -> Wellformed<Shape<T>>;

    template<typename... Ts1, typename... Ts2>
    Wellformed(const Wellformed<Ts1...>&, const Wellformed<Ts2...>&)
      -> Wellformed<Ts1..., Ts2...>;

    template<size_t I = 0, typename... Ts>
    inline constexpr Index index_fields(
      const Fields<Ts...>& fields, const Token& type, const Token& name)
    {
      if constexpr (I < sizeof...(Ts))
      {
        if (std::get<I>(fields.fields).name == name)
          return Index(type, I);

        return index_fields<I + 1>(fields, type, name);
      }
      else
        return {};
    }

    template<size_t I = 0, typename... Ts>
    inline constexpr Index index_wellformed(
      const Wellformed<Ts...>& wf, const Token& type, const Token& name)
    {
      if constexpr (I < sizeof...(Ts))
      {
        auto& shape = std::get<I>(wf.shapes);

        if (type == shape.type)
        {
          // If this shape is for the right type, search it.
          if constexpr (std::is_base_of_v<FieldsBase, decltype(shape.shape)>)
            return index_fields<0>(shape.shape, shape.type, name);
          else
            // Unless it's a sequence, which doesn't have fields.
            return {};
        }
        else
          return index_wellformed<I + 1>(wf, type, name);
      }
      else
        return {};
    }

    template<size_t N>
    inline CONSTEVAL auto to_wf(const Choice<N>& choice);

    namespace ops
    {
      inline CONSTEVAL auto operator|(const Token& type1, const Token& type2)
      {
        return Choice<2>{type1, type2};
      }

      template<size_t N>
      inline CONSTEVAL auto
      operator|(const Token& type, const Choice<N>& choice)
      {
        Choice<N + 1> result;
        result.types[0] = type;
        std::copy_n(choice.types.begin(), N, result.types.begin() + 1);
        return result;
      }

      template<size_t N>
      inline CONSTEVAL auto
      operator|(const Choice<N>& choice, const Token& type)
      {
        Choice<N + 1> result;
        std::copy_n(choice.types.begin(), N, result.types.begin());
        result.types[N] = type;
        return result;
      }

      template<size_t N1, size_t N2>
      inline CONSTEVAL auto
      operator|(const Choice<N1>& choice1, const Choice<N2>& choice2)
      {
        Choice<N1 + N2> result;
        std::copy_n(choice1.types.begin(), N1, result.types.begin());
        std::copy_n(choice2.types.begin(), N2, result.types.begin() + N1);
        return result;
      }

      inline CONSTEVAL auto operator++(const Token& type, int)
      {
        return Sequence<1>(Choice<1>{type});
      }

      template<size_t N>
      inline CONSTEVAL auto operator++(const Choice<N>& choice, int)
      {
        return Sequence<N>(choice);
      }

      template<size_t N>
      inline CONSTEVAL auto
      operator>>=(const Token& name, const Choice<N>& choice)
      {
        return Fields(Field<N>{{}, name, choice});
      }

      inline CONSTEVAL auto operator>>=(const Token& name, const Token& type)
      {
        return Fields(Field<1>{{}, name, Choice<1>{type}});
      }

      template<typename... Ts1, typename... Ts2>
      inline CONSTEVAL auto
      operator*(const Fields<Ts1...>& fst, const Fields<Ts2...>& snd)
      {
        return Fields(fst, snd);
      }

      template<typename... Ts>
      inline CONSTEVAL auto
      operator*(const Fields<Ts...>& fst, const Token& snd)
      {
        return fst * (snd >>= snd);
      }

      template<typename... Ts>
      inline CONSTEVAL auto
      operator*(const Token& fst, const Fields<Ts...>& snd)
      {
        return (fst >>= fst) * snd;
      }

      inline CONSTEVAL auto operator*(const Token& fst, const Token& snd)
      {
        return (fst >>= fst) * (snd >>= snd);
      }

      template<size_t N>
      inline CONSTEVAL auto
      operator<<=(const Token& type, const Sequence<N>& seq)
      {
        return Shape(type, seq);
      }

      template<typename... Ts>
      inline CONSTEVAL auto
      operator<<=(const Token& type, const Fields<Ts...>& fields)
      {
        return Shape(type, fields);
      }

      template<size_t N>
      inline CONSTEVAL auto
      operator<<=(const Token& type, const Choice<N>& choice)
      {
        return type <<= (type >>= choice);
      }

      inline CONSTEVAL auto operator<<=(const Token& type1, const Token& type2)
      {
        return type1 <<= (type2 >>= type2);
      }

      template<typename... Ts1, typename... Ts2>
      inline CONSTEVAL auto
      operator|(const Wellformed<Ts1...>& wf1, const Wellformed<Ts2...>& wf2)
      {
        return Wellformed(wf1, wf2);
      }

      template<typename... Ts, typename T>
      inline CONSTEVAL auto
      operator|(const Wellformed<Ts...>& wf, const Shape<T>& shape)
      {
        return wf | Wellformed(shape);
      }

      template<typename T, typename... Ts>
      inline CONSTEVAL auto
      operator|(const Shape<T>& shape, const Wellformed<Ts...>& wf)
      {
        return Wellformed(shape) | wf;
      }

      template<typename T1, typename T2>
      inline CONSTEVAL auto
      operator|(const Shape<T1>& shape1, const Shape<T2>& shape2)
      {
        return Wellformed(shape1) | Wellformed(shape2);
      }
    }

    template<typename T>
    inline CONSTEVAL auto to_wf(const Shape<T>& shape)
    {
      using namespace ops;
      return Wellformed() | shape;
    }

    template<size_t I, size_t N, typename... Ts>
    inline CONSTEVAL auto
    to_wf(const Choice<N>& choice, const Wellformed<Ts...>& wf)
    {
      using namespace ops;

      if constexpr (I >= N)
        return wf;
      else
        return to_wf<I + 1>(choice, wf | choice.types[I]);
    }

    template<size_t N>
    inline CONSTEVAL auto to_wf(const Choice<N>& choice)
    {
      return to_wf<0>(choice, Wellformed());
    }

    inline CONSTEVAL auto to_wf(const Token& type)
    {
      using namespace ops;
      return Wellformed(type <<= type);
    }
  }

  template<typename... Ts>
  inline CONSTEVAL auto
  operator/(const wf::Wellformed<Ts...>& wf, const Token& type)
  {
    return std::make_pair(wf, type);
  }

  template<typename... Ts>
  inline CONSTEVAL Index operator/(
    const std::pair<wf::Wellformed<Ts...>, Token>& pair, const Token& name)
  {
    return wf::index_wellformed(pair.first, pair.second, name);
  }
}

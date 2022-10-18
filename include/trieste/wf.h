// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "xoroshiro.h"

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
      using Rand = xoroshiro::p128r32;
      using Seed = uint64_t;
      using Result = uint32_t;

      Rand rand;
      size_t max_depth;
      std::set<Token> nonterminals;

      Gen(Seed seed, size_t max_depth) : rand(seed), max_depth(max_depth) {}

      Result next()
      {
        return rand.next();
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

          out << std::endl << node->location().str() << std::endl;
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

        auto child = NodeDef::create(type, node->fresh());
        node->push_back(child);
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

      consteval Sequence(const Choice<N>& types)
      : types(types), minlen(0), binding(Invalid)
      {}
      consteval Sequence(const Sequence<N>& seq, const Token& binding)
      : types(seq.types), minlen(seq.minlen), binding(binding)
      {}
      consteval Sequence(const Choice<N>& types, size_t minlen, Token binding)
      : types(types), minlen(minlen), binding(binding)
      {}

      consteval auto operator[](size_t new_minlen) const
      {
        return Sequence<N>(types, new_minlen, binding);
      }

      constexpr bool terminal() const
      {
        return false;
      }

      bool check(Node node, std::ostream& out) const
      {
        auto ok = true;

        for (auto& child : *node)
          ok = types.check(child, out) && ok;

        if (node->size() < minlen)
        {
          out << node->location().origin_linecol() << "expected at least "
              << minlen << " children, found " << node->size() << std::endl
              << node->location().str() << std::endl;
          ok = false;
        }

        if (!binding.in({Invalid, Include}))
        {
          out << node->location().origin_linecol() << "can't bind a "
              << node->type().str() << " sequence in the symbol table"
              << std::endl
              << node->location().str() << std::endl;
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

      void build_st(Node node) const
      {
        if (binding == Include)
          node->include();
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

      consteval Fields() : fields(), binding(Invalid) {}

      template<size_t N>
      consteval Fields(const Field<N>& field)
      : fields(std::make_tuple(field)), binding(Invalid)
      {}

      template<typename... Ts2, typename... Ts3>
      consteval Fields(const Fields<Ts2...>& fst, const Fields<Ts3...>& snd)
      : fields(std::tuple_cat(fst.fields, snd.fields)), binding(Invalid)
      {}

      template<typename... Ts2>
      consteval Fields(const Fields<Ts2...>& fields, const Token& binding)
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
                << node->location().str() << std::endl;
            return false;
          }
          else
          {
            return ok;
          }
        }

        if constexpr (I >= sizeof...(Ts))
        {
          // Too many child nodes.
          out << (*child)->location().origin_linecol()
              << "too many child nodes in " << node->type().str() << std::endl
              << (*child)->location().str() << std::endl;
          return false;
        }
        else
        {
          auto& field = std::get<I>(fields);
          ok = field.types.check(*child, out) && ok;

          if ((binding != Invalid) && (field.name == binding))
          {
            auto defs = (*child)->lookup();
            auto find = std::find(defs.begin(), defs.end(), node);

            if (find == defs.end())
            {
              out << (*child)->location().origin_linecol()
                  << "missing symbol table binding for " << node->type().str()
                  << std::endl
                  << (*child)->location().str() << std::endl;
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

      void build_st(Node node) const
      {
        if (binding == Include)
          node->include();
        else if (binding != Invalid)
          build_st_i<0>(node);
      }

      template<size_t I>
      void build_st_i(Node node) const
      {
        if constexpr (I < sizeof...(Ts))
        {
          auto& field = std::get<I>(fields);

          if (binding == field.name)
            node->bind(node->at(I)->location());
          else
            build_st_i<I + 1>(node);
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

      consteval Shape(Token type, const T& shape) : type(type), shape(shape) {}

      consteval auto operator[](const Token& binding) const
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
      std::function<Node(Gen::Seed, size_t)> gen;
      std::function<void(Node)> build_st;
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

      consteval Wellformed() : shapes() {}

      template<typename T>
      consteval Wellformed(const Shape<T>& shape)
      : shapes(std::make_tuple(shape))
      {}

      template<typename... Ts1, typename... Ts2>
      consteval Wellformed(
        const Wellformed<Ts1...>& wf1, const Wellformed<Ts2...>& wf2)
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
          ok = check(child, out) && ok;

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
              << node->location().str() << std::endl;
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

      Node gen(Gen::Seed seed, size_t max_depth) const
      {
        auto g = Gen(seed, max_depth);
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

      void build_st(Node node) const
      {
        if (node->type() == Error)
          return;

        node->clear_symbols();
        build_st_i(node);

        for (auto& child : *node)
          build_st(child);
      }

      template<size_t I = sizeof...(Ts) - 1>
      void build_st_i(Node node) const
      {
        if constexpr (I < sizeof...(Ts))
        {
          auto& shape = std::get<I>(shapes);

          if (shape.type == node->type())
            shape.shape.build_st(node);
          else
            build_st_i<I - 1>(node);
        }
      }

      Node build_ast(Source source, size_t pos, std::ostream& out) const
      {
        std::regex hd("^[[:space:]]*\\(([^[:space:]\\(\\)]*)");
        std::regex st("^[[:space:]]*\\{[^\\}]*\\}");
        std::regex tl("^[[:space:]]*\\)");

        auto start = source->view().cbegin();
        auto it = start + pos;
        auto end = source->view().cend();
        std::cmatch match;
        Node top;
        Node ast;

        while (it < end)
        {
          // Find the type the node. If we didn't find a node, it's an error.
          if (!std::regex_search(it, end, match, hd))
          {
            auto loc = Location(source, it - start, 1);
            out << loc.origin_linecol() << "expected node" << std::endl
                << loc.str() << std::endl;
            return {};
          }

          // If we don't have a valid node type, it's an error.
          auto type_loc =
            Location(source, (it - start) + match.position(1), match.length(1));
          auto type = find_type_i(ast, type_loc);
          it += match.length();

          if (type == Invalid)
          {
            out << type_loc.origin_linecol() << "unknown type" << std::endl
                << type_loc.str() << std::endl;
            return {};
          }

          // Find the source location of the node as a netstring.
          auto ident_loc = type_loc;

          if (*it == ' ')
          {
            ++it;
            size_t len = 0;

            while ((*it >= '0') && (*it <= '9'))
            {
              len = (len * 10) + (*it - '0');
              ++it;
            }

            if (*it != ':')
            {
              auto loc = Location(source, it - start, 1);
              out << loc.origin_linecol() << "expected ':'" << std::endl
                  << loc.str() << std::endl;
              return {};
            }

            ++it;
            ident_loc = Location(source, it - start, len);
            it += len;
          }

          auto node = NodeDef::create(type, ident_loc);

          // Push the node into the AST.
          if (ast)
            ast->push_back(node);
          else
            top = node;

          ast = node;

          // Skip the symbol table.
          if (std::regex_search(it, end, match, st))
            it += match.length();

          // `)` ends the node. Otherwise, we'll add children to this node.
          while (std::regex_search(it, end, match, tl))
          {
            it += match.length();
            auto parent = ast->parent();

            if (!parent)
              return ast;

            ast = parent->shared_from_this();
          }
        }

        // We never finished the AST, so it's an error.
        auto loc = Location(source, it - start, 1);
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

        return Invalid;
      }

      auto operator()() const
      {
        return WellformedF{
          [this](Node node, std::ostream& out) { return check(node, out); },
          [this](Gen::Seed seed, size_t max_depth) {
            return gen(seed, max_depth);
          },
          [this](Node node) { build_st(node); },
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

          // Unless it's a sequence, which doesn't have fields.
          return {};
        }

        return index_wellformed<I + 1>(wf, type, name);
      }

      return {};
    }

    template<size_t N>
    inline consteval auto to_wf(const Choice<N>& choice);

    namespace ops
    {
      inline consteval auto operator|(const Token& type1, const Token& type2)
      {
        return Choice<2>{type1, type2};
      }

      template<size_t N>
      inline consteval auto
      operator|(const Token& type, const Choice<N>& choice)
      {
        Choice<N + 1> result;
        result.types[0] = type;
        std::copy_n(choice.types.begin(), N, result.types.begin() + 1);
        return result;
      }

      template<size_t N>
      inline consteval auto
      operator|(const Choice<N>& choice, const Token& type)
      {
        Choice<N + 1> result;
        std::copy_n(choice.types.begin(), N, result.types.begin());
        result.types[N] = type;
        return result;
      }

      template<size_t N1, size_t N2>
      inline consteval auto
      operator|(const Choice<N1>& choice1, const Choice<N2>& choice2)
      {
        Choice<N1 + N2> result;
        std::copy_n(choice1.types.begin(), N1, result.types.begin());
        std::copy_n(choice2.types.begin(), N2, result.types.begin() + N1);
        return result;
      }

      inline consteval auto operator++(const Token& type, int)
      {
        return Sequence<1>(Choice<1>{type});
      }

      template<size_t N>
      inline consteval auto operator++(const Choice<N>& choice, int)
      {
        return Sequence<N>(choice);
      }

      template<size_t N>
      inline consteval auto
      operator>>=(const Token& name, const Choice<N>& choice)
      {
        return Fields(Field<N>{{}, name, choice});
      }

      inline consteval auto operator>>=(const Token& name, const Token& type)
      {
        return Fields(Field<1>{{}, name, Choice<1>{type}});
      }

      template<typename... Ts1, typename... Ts2>
      inline consteval auto
      operator*(const Fields<Ts1...>& fst, const Fields<Ts2...>& snd)
      {
        return Fields(fst, snd);
      }

      template<typename... Ts>
      inline consteval auto
      operator*(const Fields<Ts...>& fst, const Token& snd)
      {
        return fst * (snd >>= snd);
      }

      template<typename... Ts>
      inline consteval auto
      operator*(const Token& fst, const Fields<Ts...>& snd)
      {
        return (fst >>= fst) * snd;
      }

      inline consteval auto operator*(const Token& fst, const Token& snd)
      {
        return (fst >>= fst) * (snd >>= snd);
      }

      template<size_t N>
      inline consteval auto
      operator<<=(const Token& type, const Sequence<N>& seq)
      {
        return Shape(type, seq);
      }

      template<typename... Ts>
      inline consteval auto
      operator<<=(const Token& type, const Fields<Ts...>& fields)
      {
        return Shape(type, fields);
      }

      template<size_t N>
      inline consteval auto
      operator<<=(const Token& type, const Choice<N>& choice)
      {
        return type <<= (type >>= choice);
      }

      inline consteval auto operator<<=(const Token& type1, const Token& type2)
      {
        return type1 <<= (type2 >>= type2);
      }

      template<typename... Ts1, typename... Ts2>
      inline consteval auto
      operator|(const Wellformed<Ts1...>& wf1, const Wellformed<Ts2...>& wf2)
      {
        return Wellformed(wf1, wf2);
      }

      template<typename... Ts, typename T>
      inline consteval auto
      operator|(const Wellformed<Ts...>& wf, const Shape<T>& shape)
      {
        return wf | Wellformed(shape);
      }

      template<typename T, typename... Ts>
      inline consteval auto
      operator|(const Shape<T>& shape, const Wellformed<Ts...>& wf)
      {
        return Wellformed(shape) | wf;
      }

      template<typename T1, typename T2>
      inline consteval auto
      operator|(const Shape<T1>& shape1, const Shape<T2>& shape2)
      {
        return Wellformed(shape1) | Wellformed(shape2);
      }
    }

    template<typename T>
    inline consteval auto to_wf(const Shape<T>& shape)
    {
      using namespace ops;
      return Wellformed() | shape;
    }

    template<size_t I, size_t N, typename... Ts>
    inline consteval auto
    to_wf(const Choice<N>& choice, const Wellformed<Ts...>& wf)
    {
      using namespace ops;

      if constexpr (I >= N)
        return wf;
      else
        return to_wf<I + 1>(choice, wf | choice.types[I]);
    }

    template<size_t N>
    inline consteval auto to_wf(const Choice<N>& choice)
    {
      return to_wf<0>(choice, Wellformed());
    }

    inline consteval auto to_wf(const Token& type)
    {
      using namespace ops;
      return Wellformed(type <<= type);
    }
  }

  template<typename... Ts>
  inline consteval auto
  operator/(const wf::Wellformed<Ts...>& wf, const Token& type)
  {
    return std::make_pair(wf, type);
  }

  template<typename... Ts>
  inline consteval Index operator/(
    const std::pair<wf::Wellformed<Ts...>, Token>& pair, const Token& name)
  {
    return wf::index_wellformed(pair.first, pair.second, name);
  }
}

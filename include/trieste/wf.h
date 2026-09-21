// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "gen.h"
#include "logging.h"
#include "regex.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
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
    using TokenTerminalDistance = std::map<Token, std::size_t>;
    // Types of possible keys and their field index
    using SymtabKeys = std::pair<std::vector<Token>, size_t>;

    struct Sampling
    {
      std::map<trieste::Token, trieste::Nodes>& sampled_nodes;
      size_t
        sampling_level; // 0: subtree sampling, 1..n: subtree height sampling
      bool sampling_enabled;
      size_t sampling_period; // use a sample roughly 1-in-`sampling_period`
                              // draws; 0 = never

      Sampling(
        std::map<trieste::Token, trieste::Nodes>& sampled_nodes_,
        size_t sampling_level_,
        bool sampling_enabled_,
        size_t sampling_frequency_)
      : sampled_nodes(sampled_nodes_),
        sampling_level(sampling_level_),
        sampling_enabled(sampling_enabled_),
        sampling_period(
          sampling_frequency_ > 0 ? std::floor(100.0 / sampling_frequency_) : 0)
      {}
    };
    struct Gen
    {
      TokenTerminalDistance token_terminal_distance;
      GenNodeLocationF gloc;
      Rand rand;
      size_t target_depth;
      size_t ceiling_depth;
      double alpha;
      std::map<Token, SymtabKeys> binding_keys;
      bool gen_bound_vars;
      Sampling sampling;

      /* The generator chooses which token to emit next. It makes this choice
       * using a weighted probability distribution, where the weights are based
       * on the distance to the nearest terminal node in the token graph.
       * once the tree exceeds the target depth, this distribution becomes
       * "spikier" as controlled by the value of alpha using the following
       * equations:
       *
       * $P(c|d,p) = \frac{P(d|c,p)P(c|p)}{\sum_{c' \in T} P(d|c',p)P(c'|p)}$
       *
       * $P(d|c,p) = 1 / (1 + m_c * \alpha * max(d - t))$
       *
       * where $m_c$ is the expected distance to a terminal node from the token
       * $c$ and $t$ is the target depth.
       */
      Gen(
        TokenTerminalDistance token_terminal_distance_,
        GenNodeLocationF gloc_,
        Seed seed_,
        size_t target_depth_,
        std::map<Token, SymtabKeys> binding_keys_,
        bool gen_bound_vars_,
        std::map<trieste::Token, trieste::Nodes>& sampled_nodes,
        size_t sampling_level,
        bool sampling_enabled_,
        size_t sampling_frequency_,
        double alpha_ = 1,
        size_t ceiling_multiplier_ = 2)
      : token_terminal_distance(token_terminal_distance_),
        gloc(gloc_),
        rand(seed_),
        target_depth(target_depth_),
        ceiling_depth(ceiling_multiplier_ * target_depth),
        alpha(alpha_),
        binding_keys(binding_keys_),
        gen_bound_vars(gen_bound_vars_),
        sampling(Sampling(
          sampled_nodes,
          sampling_level,
          sampling_enabled_,
          sampling_frequency_))
      {
        // Warm up RNG
        for (int i = 0; i < 10; i++)
          rand();
      }

      static bool contains_type(std::vector<Token>& tokens, Token t)
      {
        return std::find(tokens.begin(), tokens.end(), t) != tokens.end();
      }

      Token choose(const std::vector<Token>& tokens, std::size_t depth)
      {
        if (tokens.size() == 1)
        {
          return tokens[0];
        }

        if (depth <= target_depth)
        {
          std::size_t choice = rand() % tokens.size();
          return tokens[choice];
        }

        // compute 1 / (1 + alpha * (depth - target_depth) * distance)
        std::vector<double> offsets;
        std::transform(
          tokens.begin(),
          tokens.end(),
          std::back_inserter(offsets),
          [&](const Token& t) {
            if (
              token_terminal_distance.find(t) != token_terminal_distance.end())
            {
              std::size_t distance = token_terminal_distance.at(t);
              return 1.0 / (1.0 + (alpha * (depth - target_depth) * distance));
            }
            else
            {
              std::ostringstream err;
              err << "Token " << t.str()
                  << " not found in token_terminal_distance map" << std::endl;
              err << "{";
              std::string delim = "";
              for (auto const& [key, val] : token_terminal_distance)
              {
                err << delim << key.str() << ":" << val;
                delim = ", ";
              }
              err << "}" << std::endl;
              throw std::runtime_error(err.str());
            }
          });

        if (depth >= 2 * ceiling_depth)
        {
          logging::Warn()
            << "Token generation is not reaching a terminal after depth "
            << depth;
          logging::Warn()
            << "This can indicate an issue with the WF definition (for "
               "example, a cycle), or that the target depth is too shallow.";
          logging::Debug() << "P(d | c, p):";
          for (size_t i = 0; i < tokens.size(); i++)
          {
            logging::Debug() << "  " << tokens[i].str() << ": " << offsets[i];
          }
        }

        if (depth >= ceiling_depth)
        {
          // if we have reached this point P(c | d, p) as high entropy.
          // In order to encourage termination, we will start to manually choose
          // the child with the highest likelihood of terminating.
          auto max = std::max_element(offsets.begin(), offsets.end());
          return tokens[std::distance(offsets.begin(), max)];
        }

        // compute the cumulative distribution of P(d | c, p)
        std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());

        // instead of normalizing the cumulative distribution, scale the random
        // number to the sum of the probabilities
        double value = static_cast<double>(rand() - rand.min()) /
          static_cast<double>(rand.max() - rand.min()) * offsets.back();

        // finding the first element greater than the uniform random number is
        // the same as performing a weighted sampling of the P(c | d, p)
        // distribution
        auto it = std::lower_bound(offsets.begin(), offsets.end(), value);

        return tokens[std::distance(offsets.begin(), it)];
      }

      Result next()
      {
        return rand();
      }

      bool has_sample_nodes(Token t)
      {
        return sampling.sampled_nodes.find(t) != sampling.sampled_nodes.end();
      }

      size_t sampling_level()
      {
        return sampling.sampling_level;
      }

      bool sampling_enabled()
      {
        return sampling.sampling_enabled;
      }

      bool use_sample()
      {
        // A frequency of 0 (period 0) means never sample
        if (sampling.sampling_period == 0)
          return false;
        return next() % sampling.sampling_period == 0;
      }

      bool subtree_sampling()
      {
        return sampling.sampling_level == 0;
      }

    private:
      // Get all symbols in scope matching the binding type
      Nodes get_symbols_from_type(Token type, Node st)
      {
        Nodes symbols;
        while (st)
        {
          st->get_symbols(symbols, [&](auto& n) {
            auto it = binding_keys.find(n->type());
            return (
              n->type() & flag::lookup && it != binding_keys.end() &&
              contains_type(it->second.first, type));
          });
          st = st->scope();
        }
        return symbols;
      }

      // Get all symbols in scope matching the binding type and location
      Nodes get_symbols_from_loc(Location loc, Node st)
      {
        Nodes symbols;
        while (st)
        {
          st->get_symbols(
            loc, symbols, [&](auto& n) { return (n->type() & flag::lookup); });
          st = st->scope();
        }
        return symbols;
      }

      // Checks if child is a symbol table key for the parent type
      bool
      is_symtab_key(size_t child_index, Token child_type, Token parent_type)
      {
        auto it = binding_keys.find(parent_type);
        if (it == binding_keys.end())
          return false;

        std::vector<Token> key_types = it->second.first;
        auto key_field_index = it->second.second;

        if (key_types.empty())
          return false;

        return key_field_index == child_index &&
          contains_type(key_types, child_type);
      }

      // Returns true if we should generate a bound variable
      bool should_gen_bound(Node parent, Token child_type)
      {
        if (!gen_bound_vars)
          return false;
        // Check if there is a binding key, e.g. any binding key match the child
        // type
        std::vector<Token> bound_nodes = {};
        for (auto& [parent_type, binds_to] : binding_keys)
        {
          if (contains_type(binds_to.first, child_type))
            bound_nodes.push_back(parent_type);
        }
        if (bound_nodes.empty()) // No bound nodes to use
        {
          return false;
        }
        if (
          std::find(bound_nodes.begin(), bound_nodes.end(), parent->type()) ==
          bound_nodes.end())
        {
          return true; // Not at binding site
        }
        // If the node we are generating is the binding key of the parent node,
        // we should not use a bound name
        auto child_index = parent->size() - 1;
        return !(
          is_symtab_key(child_index, child_type, parent->type()) &&
          parent->type() & flag::shadowing);
      }

      bool is_in_scope(Location loc, Node scope)
      {
        auto symbols = get_symbols_from_loc(loc, scope);
        return symbols.size() != 0;
      }

    public:
      Location fresh_location(Node child)
      {
        return gloc(rand, child);
      }

      // Generate location for child node, preferring to reuse existing
      // locations of in-scope symbols of the same type if gen_bound_vars is
      // enabled and the child is not a binding key
      Location gen_location(Node parent, Node child)
      {
        auto type = child->type();
        Nodes symbols = get_symbols_from_type(type, child->scope());
        // Adding +1 to allow for fresh location with a small probability
        auto rand_symbol = static_cast<size_t>(next() % (symbols.size() + 1));
        // Prefer location from a symbol table if gen_bound_vars is enabled.
        if (rand_symbol < symbols.size() && should_gen_bound(parent, type))
        {
          auto sym = symbols[rand_symbol];
          auto key_index = binding_keys.at(sym->type()).second;
          return (sym->at(key_index))->location();
        }
        auto fresh = fresh_location(child);
        while (is_in_scope(fresh, parent->scope()))
        {
          // "Fresh" location might be in scope if the fresh machinery has
          // been used in both rewriting and generation
          fresh = fresh_location(child);
        }
        return fresh;
      }

      // Clone node without children,
      // giving it a fresh location if it's a symtab key where shadowing is
      // disallowed and the symbol is already in scope.
      Node non_shadow_clone(Node parent, Node node)
      {
        Node node_clone = NodeDef::create(node->type());
        parent->push_back(node_clone);
        auto node_index = parent->size() - 1;
        Location location;

        if (
          is_symtab_key(node_index, node->type(), parent->type()) &&
          parent->type() & flag::shadowing &&
          is_in_scope(node->location(), parent->scope()))
        {
          location = fresh_location(node_clone);
        }
        else
        {
          // Reuse location if not a shadowing symtab key in scope
          location = node->location();
        }
        if (is_symtab_key(node_index, node->type(), parent->type()))
        {
          // All nodes that serves as symtab keys should be bound so we can
          // look them up during generation
          parent->bind(location);
        }
        node_clone->set_location(location);
        return node_clone;
      }

      // Clone depth layers of parent tree
      // push all incomplete subtrees to continuations vector
      void
      clone_depth(size_t depth, Node parent, Node node, Nodes& continuations)
      {
        Node node_clone = non_shadow_clone(parent, node);

        if (depth == 0)
        {
          continuations.push_back(node_clone);
        }
        else
        {
          for (auto& child : *node)
          {
            clone_depth(depth - 1, node_clone, child, continuations);
          }
        }
      }

      void gen_sample(Node& node, Nodes& continuations)
      {
        auto& samples = sampling.sampled_nodes[node->type()];
        size_t choice = rand() % samples.size();
        auto sample = samples[choice];

        // sampling_level 0 means clone the entire subtree; otherwise clone that
        // many levels and leave the rest to be generated.
        constexpr size_t full_subtree = std::numeric_limits<size_t>::max();
        size_t levels =
          sampling.sampling_level == 0 ? full_subtree : sampling.sampling_level;

        // Copy children from sample
        for (auto& child : *sample)
          clone_depth(levels, node, child, continuations);
      }
    };
    struct Choice
    {
      std::vector<Token> types;

      TRIESTE_SLOW_PATH Choice() = default;
      TRIESTE_SLOW_PATH Choice(std::vector<Token> types_) : types{types_} {}
      TRIESTE_SLOW_PATH Choice(const Choice&) = default;
      TRIESTE_SLOW_PATH Choice(Choice&&) = default;
      TRIESTE_SLOW_PATH Choice& operator=(const Choice&) = default;
      TRIESTE_SLOW_PATH Choice& operator=(Choice&&) = default;
      TRIESTE_SLOW_PATH ~Choice() = default;

      bool accepts_type(const Token& type) const
      {
        return std::find(types.begin(), types.end(), type) != types.end();
      }

      bool check(Node node) const
      {
        if (node == Error)
          return true;

        auto ok = accepts_type(node->type());

        if (!ok)
        {
          // Note log will be output on end of scope.
          logging::Error out{};

          out << node->location().origin_linecol() << ": unexpected "
              << node->type().str() << ", expected a ";

          for (size_t i = 0; i < types.size(); ++i)
          {
            out << types[i].str();

            if (i < (types.size() - 2))
              out << ", ";
            if (i == (types.size() - 2))
              out << " or ";
          }

          out << std::endl << node->location().str() << node << std::endl;
        }

        return ok;
      }

      std::size_t expected_distance_to_terminal(
        const std::set<Token>& omit,
        std::size_t max_distance,
        std::function<std::size_t(Token)> distance) const
      {
        return std::accumulate(
                 types.begin(),
                 types.end(),
                 static_cast<std::size_t>(0),
                 [&](std::size_t acc, auto& type) {
                   if (omit.find(type) != omit.end())
                   {
                     return acc + max_distance;
                   }

                   return acc + distance(type);
                 }) /
          types.size();
      }

      void gen(Gen& g, size_t depth, Node node) const
      {
        Token type = g.choose(types, depth);

        // We may need a fresh location, so the child needs to be in the AST by
        // the time we call g.location().
        auto child = NodeDef::create(type);
        node->push_back(child);
        child->set_location(g.gen_location(node, child));
      }
    };

    struct Sequence
    {
      Choice choice;
      size_t min_len;
      size_t max_len;
      bool has_minlen = false;
      bool has_maxlen = false;

      TRIESTE_SLOW_PATH
      Sequence(Choice choice_, size_t minlen_, size_t maxlen_)
      : choice{choice_}, min_len{minlen_}, max_len(maxlen_)
      {}
      TRIESTE_SLOW_PATH Sequence() = default;
      TRIESTE_SLOW_PATH Sequence(const Sequence&) = default;
      TRIESTE_SLOW_PATH Sequence(Sequence&&) = default;
      TRIESTE_SLOW_PATH Sequence& operator=(const Sequence&) = default;
      TRIESTE_SLOW_PATH Sequence& operator=(Sequence&&) = default;
      TRIESTE_SLOW_PATH ~Sequence() = default;

      size_t index(const Token&) const
      {
        return std::numeric_limits<size_t>::max();
      }

      Sequence& operator[](size_t new_len)
      {
        if (has_minlen && has_maxlen)
          throw std::runtime_error(
            "Too many bounds when building sequence: [" +
            std::to_string(min_len) + "][" + std::to_string(max_len) + "][" +
            std::to_string(new_len) + "]");

        if (has_minlen && !has_maxlen && new_len < min_len)
          throw std::runtime_error(
            "Upper bound is below lower bound when building sequence: [" +
            std::to_string(min_len) + "][" + std::to_string(new_len) + "]");

        if (!has_minlen)
        {
          min_len = new_len;
          has_minlen = true;
        }
        else
        {
          max_len = new_len;
          has_maxlen = true;
        }
        return *this;
      }

      Sequence& operator[](const Token&)
      {
        // Do nothing.
        return *this;
      }

      bool check(Node node) const
      {
        auto has_err = false;
        auto ok = true;

        for (auto& child : *node)
        {
          has_err = has_err || (child == Error);
          ok = choice.check(child) && ok;
        }

        if (!has_err && (node->size() < min_len))
        {
          logging::Error() << node->location().origin_linecol()
                           << ": expected at least " << min_len
                           << " children, found " << node->size() << std::endl
                           << node->location().str() << node << std::endl;
          ok = false;
        }

        if (!has_err && (node->size() > max_len))
        {
          logging::Error() << node->location().origin_linecol()
                           << ": expected at most " << max_len
                           << " children, found " << node->size() << std::endl
                           << node->location().str() << node << std::endl;
          ok = false;
        }

        return ok;
      }

      bool build_st(Node&) const
      {
        // Do nothing.
        return true;
      }

      bool has_binding_for(Token) const
      {
        // Sequences have no symbol table bindings.
        return false;
      }

      void gen(Gen& g, size_t depth, Node node) const
      {
        size_t i;
        for (i = 0; i < min_len; ++i)
          choice.gen(g, depth, node);

        if (depth >= g.target_depth)
        {
          return;
        }

        while (i++ < max_len && g.next() % 2)
          choice.gen(g, depth, node);
      }
    };

    struct Field
    {
      Token name;
      Choice choice;
      bool explicit_name = true;
    };

    struct Fields
    {
      std::vector<Field> fields;
      Token binding;

      TRIESTE_SLOW_PATH Fields(std::vector<Field> fields_, Token binding_)
      : fields{fields_}, binding{binding_}
      {
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
          for (std::size_t j = i + 1; j < fields.size(); ++j)
          {
            check_collision(fields[i], fields[j]);
          }
        }
      }
      TRIESTE_SLOW_PATH Fields() = default;
      TRIESTE_SLOW_PATH Fields(const Fields&) = default;
      TRIESTE_SLOW_PATH Fields(Fields&&) = default;
      TRIESTE_SLOW_PATH Fields& operator=(const Fields&) = default;
      TRIESTE_SLOW_PATH Fields& operator=(Fields&&) = default;
      TRIESTE_SLOW_PATH ~Fields() = default;

      void append(const Field& field)
      {
        for (const auto& existing : fields)
          check_collision(existing, field);

        fields.push_back(field);
      }

      size_t index(const Token& field) const
      {
        auto i = 0;

        for (auto& f : fields)
        {
          if (f.name == field)
            return i;

          ++i;
        }

        return std::numeric_limits<size_t>::max();
      }

      Fields& operator[](const Token& type)
      {
        this->binding = type;
        return *this;
      }

      bool check(Node node) const
      {
        auto field = fields.begin();
        auto end = fields.end();
        bool ok = true;
        bool has_error = false;

        for (auto& child : *node)
        {
          // A node that contains an Error node stops checking well-formedness
          // from that point.
          if (child == Error)
          {
            has_error = true;
            break;
          }

          // If we run out of fields, the node is ill-formed.
          if (field == end)
            break;

          ok = field->choice.check(child) && ok;

          if ((binding != Invalid) && (field->name == binding))
          {
            auto defs = node->scope()->look(child->location());
            auto find = std::find(defs.begin(), defs.end(), node);

            if (find == defs.end())
            {
              logging::Error() << child->location().origin_linecol()
                               << ": missing symbol table binding for "
                               << node->type().str() << std::endl
                               << child->location().str() << node << std::endl;
              ok = false;
            }
          }

          ++field;
        }

        if (!has_error && (node->size() != fields.size()))
        {
          logging::Error() << node->location().origin_linecol() << ": expected "
                           << fields.size() << " children, found "
                           << node->size() << std::endl
                           << node->location().str() << node << std::endl;
          ok = false;
        }

        return ok;
      }

      bool has_binding_for(Token type) const
      {
        return type == binding;
      }

      void gen(Gen& g, size_t depth, Node node) const
      {
        for (auto& field : fields)
        {
          field.choice.gen(g, depth, node);
          if (binding == field.name)
          {
            node->bind(node->back()->location()); // Bind to symbol table
          }
        }
      }

      bool build_st(Node& node) const
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
            if (index >= node->size())
              // Node does not have enough children. Pretend all is fine to
              // allow error nodes, otherwise WF check will sort it out.
              return true;

            auto name = node->at(index)->location();

            if (!node->bind(name))
            {
              auto defs = node->scope()->look(name);
              logging::Error out{};

              out << node->location().origin_linecol()
                  << ": conflicting definitions of `" << name.view()
                  << "`:" << std::endl;

              for (auto def : defs)
                out << def->location().str();

              return false;
            }

            return true;
          }

          ++index;
        }

        logging::Error() << node->location().origin_linecol()
                         << ": no binding found for " << node->type().str()
                         << std::endl
                         << node->location().str() << node << std::endl;
        return false;
      }

    private:
      static void check_collision(const Field& lhs, const Field& rhs)
      {
        if (lhs.name == rhs.name && (lhs.explicit_name || rhs.explicit_name))
        {
          const auto name = lhs.name == Token{} ? "<invalid>" : lhs.name.str();
          throw std::runtime_error(
            std::string("duplicate WF field name: ") + name);
        }
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

      TRIESTE_SLOW_PATH Shape(Token type_, ShapeT shape_)
      : type{type_}, shape{shape_}
      {}
      TRIESTE_SLOW_PATH Shape() = default;
      TRIESTE_SLOW_PATH Shape(const Shape&) = default;
      TRIESTE_SLOW_PATH Shape(Shape&&) = default;
      TRIESTE_SLOW_PATH Shape& operator=(const Shape&) = default;
      TRIESTE_SLOW_PATH Shape& operator=(Shape&&) = default;
      TRIESTE_SLOW_PATH ~Shape() = default;

      Shape& operator[](const Token& binding)
      {
        std::visit([&](auto& s) { s[binding]; }, shape);
        return *this;
      }
    };

    struct Wellformed
    {
      std::map<Token, ShapeT> shapes;

      TRIESTE_SLOW_PATH Wellformed() = default;
      TRIESTE_SLOW_PATH Wellformed(const Wellformed&) = default;
      TRIESTE_SLOW_PATH Wellformed(Wellformed&&) = default;
      TRIESTE_SLOW_PATH Wellformed& operator=(const Wellformed&) = default;
      TRIESTE_SLOW_PATH Wellformed& operator=(Wellformed&&) = default;
      TRIESTE_SLOW_PATH ~Wellformed() = default;

      operator bool() const
      {
        return !shapes.empty();
      }

      size_t index(const Token& type, const Token& field) const
      {
        auto find = shapes.find(type);

        if (find == shapes.end())
          return std::numeric_limits<size_t>::max();

        return std::visit(
          [&](auto& shape) { return shape.index(field); }, find->second);
      }

      void TRIESTE_SLOW_PATH prepend(const Shape& shape)
      {
        auto find = shapes.find(shape.type);
        if (find == shapes.end())
          append(shape);
      }

      void TRIESTE_SLOW_PATH prepend(Shape&& shape)
      {
        auto find = shapes.find(shape.type);
        if (find == shapes.end())
          append(shape);
      }

      void TRIESTE_SLOW_PATH append(const Shape& shape)
      {
        shapes[shape.type] = shape.shape;
      }

      void TRIESTE_SLOW_PATH append(Shape&& shape)
      {
        shapes[shape.type] = std::move(shape.shape);
      }

      bool check(Node node) const
      {
        if (shapes.empty())
          return true;

        bool ok = true;

        node->traverse([&](auto& current) {
          if (!current)
          {
            ok = false;
            return false;
          }

          // Do not look inside error nodes.
          if (current == Error)
            return false;

          // Traverse down until there are no errors in subterms.
          if (current->get_contains_error())
            return true;

          auto find = shapes.find(current->type());

          if (find == shapes.end())
          {
            // If the shape isn't present, assume it should be empty.
            if (current->empty())
              return false;

            logging::Error()
              << current->location().origin_linecol()
              << ": expected 0 children, found " << current->size() << std::endl
              << current->location().str() << current << std::endl;
            ok = false;
            return false;
          }

          ok = std::visit(
                 [&](auto& shape) { return shape.check(current); },
                 find->second) &&
            ok;

          for (auto& child : *current)
          {
            if (child->parent_unsafe() != current.get())
            {
              logging::Error()
                << child->location().origin_linecol()
                << ": this node appears in the AST multiple times:" << std::endl
                << child->location().str() << child << std::endl
                << current->location().origin_linecol()
                << ": here:" << std::endl
                << current << std::endl
                << child->parent()->location().origin_linecol()
                << ": and here:" << std::endl
                << child->parent() << std::endl
                << "Your language implementation needs to explicitly clone "
                   "nodes if they're duplicated."
                << std::endl;
              ok = false;
            }
          }
          return true;
        });

        return ok;
      }

      void populate_binding_keys(
        std::map<trieste::Token, SymtabKeys>& binding_keys) const
      {
        for (const auto& [t, s] : shapes)
        {
          Token tok = t;
          std::visit(
            [&](auto const& shape) {
              using ShapeType = std::decay_t<decltype(shape)>;
              if constexpr (std::is_same_v<ShapeType, Fields>)
              {
                if (shape.binding != Invalid)
                {
                  auto key_index = shape.index(shape.binding);
                  auto& types = shape.fields.at(key_index).choice.types;
                  if (!types.empty())
                  {
                    binding_keys.emplace(tok, std::make_pair(types, key_index));
                  }
                }
              }
            },
            s);
        }
      }

    public:
      Node gen(
        GenNodeLocationF gloc,
        Seed seed,
        size_t target_depth,
        bool gen_bound,
        std::map<trieste::Token, trieste::Nodes>& sampled_nodes,
        size_t sampling_level,
        bool sampling_enabled,
        size_t sampling_frequency) const
      {
        // Collect map of tokens to their binding token and the corresponding
        // index in the children vector. This is used to preferentially generate
        // bound variables that are in scope.
        auto binding_keys = std::map<trieste::Token, SymtabKeys>{};
        if (gen_bound || sampling_enabled)
        {
          populate_binding_keys(binding_keys);
        }

        auto g = Gen(
          compute_minimum_distance_to_terminal(target_depth),
          gloc,
          seed,
          target_depth,
          binding_keys,
          gen_bound,
          sampled_nodes,
          sampling_level,
          sampling_enabled,
          sampling_frequency);
        auto top = NodeDef::create(Top);
        ast::detail::top_node() = top;
        gen_node(g, 0, top);
        return top;
      }

      Node
      gen(GenNodeLocationF gloc, Seed seed, size_t target_depth, bool gen_bound)
        const
      {
        std::map<trieste::Token, trieste::Nodes> empty_nodes;
        return gen(
          gloc, seed, target_depth, gen_bound, empty_nodes, 0, false, 0);
      }

      std::size_t min_dist_to_terminal(
        TokenTerminalDistance& distance,
        const std::set<Token>& prefix,
        std::size_t max_distance,
        const Token& token) const
      {
        if (distance.find(token) != distance.end())
        {
          return distance[token];
        }

        if (shapes.find(token) == shapes.end())
        {
          distance[token] = 0;
        }
        else
        {
          std::set<Token> current = prefix;
          current.insert(token);
          distance[token] = std::visit(
            [&](auto&& arg) {
              using T = std::decay_t<decltype(arg)>;
              if constexpr (std::is_same_v<T, Sequence>)
              {
                return arg.choice.expected_distance_to_terminal(
                  current,
                  max_distance,
                  std::function([&](const Token& token_) {
                    return min_dist_to_terminal(
                      distance, current, max_distance, token_);
                  }));
              }
              else if constexpr (std::is_same_v<T, Fields>)
              {
                return std::accumulate(
                  arg.fields.begin(),
                  arg.fields.end(),
                  static_cast<std::size_t>(0),
                  [&](std::size_t acc, auto& field) {
                    auto expected_dist =
                      field.choice.expected_distance_to_terminal(
                        current,
                        max_distance,
                        std::function([&](const Token& token_) {
                          return min_dist_to_terminal(
                            distance, current, max_distance, token_);
                        }));
                    return std::max(acc, expected_dist);
                  });
              }
            },
            shapes.at(token));
        }

        return distance[token];
      }

      TokenTerminalDistance
      compute_minimum_distance_to_terminal(std::size_t max_distance) const
      {
        TokenTerminalDistance distance;

        for (auto& [token, _] : shapes)
        {
          distance[token] =
            min_dist_to_terminal(distance, {}, max_distance, token);
        }

        return distance;
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

        Nodes continuations;
        if (
          g.sampling_enabled() &&
          depth < g.ceiling_depth && // Guarantees termination
          g.has_sample_nodes(node->type()) && g.use_sample() &&
          !(depth == 0 && g.subtree_sampling())) // Avoid copying entire sample
        {
          g.gen_sample(node, continuations);
          // Continue generating next layer
          for (auto child : continuations)
          {
            gen_node(g, depth + g.sampling_level(), child);
          }
          return;
        }

        // Generate based on WF
        std::visit(
          [&](auto& shape) { shape.gen(g, depth, node); }, find->second);
        // Continue generating next layer
        for (auto& child : *node)
        {
          gen_node(g, depth + 1, child);
        }
      }

      bool build_st(Node& node) const
      {
        bool ok = true;

        node->traverse([&](Node& current) {
          if (!current)
          {
            ok = false;
            return false;
          }

          // Do not look inside error nodes.
          if (current == Error)
            return false;

          current->clear_symbols();

          auto find = shapes.find(current->type());

          if (find != shapes.end())
          {
            ok = std::visit(
                   [&](auto& shape) { return shape.build_st(current); },
                   find->second) &&
              ok;
          }
          return true;
        });

        return ok;
      }
    };

    namespace ops
    {
      inline Field implicit_field(const Token& type)
      {
        return Field{type, Choice{std::vector<Token>{type}}, false};
      }

      inline Field implicit_field(const Token& name, const Choice& choice)
      {
        return Field{name, choice, false};
      }

      inline Choice operator|(const Token& type1, const Token& type2)
      {
        return Choice{std::vector<Token>{type1, type2}};
      }

      inline TRIESTE_SLOW_PATH Choice
      operator|(const Token& type, const Choice& choice)
      {
        Choice result{choice.types};
        result.types.push_back(type);
        return result;
      }

      inline TRIESTE_SLOW_PATH Choice
      operator|(const Token& type, Choice&& choice)
      {
        choice.types.push_back(type);
        return std::move(choice);
      }

      inline TRIESTE_SLOW_PATH Choice
      operator|(const Choice& choice1, const Choice& choice2)
      {
        Choice result{choice1.types};
        result.types.insert(
          result.types.end(), choice2.types.begin(), choice2.types.end());
        return result;
      }

      inline TRIESTE_SLOW_PATH Choice
      operator|(const Choice& choice1, Choice&& choice2)
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

      inline TRIESTE_SLOW_PATH Choice
      operator-(const Choice& choice, const Token& type)
      {
        Choice result{choice.types};
        result.types.erase(
          std::remove(result.types.begin(), result.types.end(), type),
          result.types.end());
        return result;
      }

      inline TRIESTE_SLOW_PATH Choice
      operator-(const Choice& choice1, const Choice& choice2)
      {
        Choice result{choice1.types};
        result.types.erase(
          std::remove_if(
            result.types.begin(),
            result.types.end(),
            [&](auto t) {
              return std::find(choice2.types.begin(), choice2.types.end(), t) !=
                choice2.types.end();
            }),
          result.types.end());
        return result;
      }

      inline Sequence operator++(const Token& type, int)
      {
        return Sequence{
          Choice{std::vector<Token>{type}},
          0,
          std::numeric_limits<size_t>::max()};
      }

      inline Sequence operator++(const Choice& choice, int)
      {
        return Sequence{choice, 0, std::numeric_limits<size_t>::max()};
      }

      inline Sequence operator++(Choice&& choice, int)
      {
        return Sequence{choice, 0, std::numeric_limits<size_t>::max()};
      }

      inline Sequence operator~(const Token& type)
      {
        return Sequence{Choice{std::vector<Token>{type}}, 0, 1};
      }

      inline Sequence operator~(const Choice& choice)
      {
        return Sequence{choice, 0, 1};
      }

      inline Sequence operator~(Choice&& choice)
      {
        return Sequence{choice, 0, 1};
      }

      inline Field operator>>=(const Token& name, const Token& type)
      {
        return Field{name, Choice{std::vector<Token>{type}}, true};
      }

      inline Field operator>>=(const Token& name, const Choice& choice)
      {
        return Field{name, choice, true};
      }

      inline Field operator>>=(const Token& name, Choice&& choice)
      {
        return Field{name, choice, true};
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
        return implicit_field(fst) * implicit_field(snd);
      }

      inline Fields operator*(const Field& fst, const Token& snd)
      {
        return fst * implicit_field(snd);
      }

      inline Fields operator*(Field&& fst, const Token& snd)
      {
        return fst * implicit_field(snd);
      }

      inline Fields operator*(const Token& fst, const Field& snd)
      {
        return implicit_field(fst) * snd;
      }

      inline Fields operator*(const Token& fst, Field&& snd)
      {
        return implicit_field(fst) * snd;
      }

      inline TRIESTE_SLOW_PATH Fields
      operator*(const Fields& fst, const Field& snd)
      {
        auto fields = fst;
        fields.append(snd);
        return fields;
      }

      inline TRIESTE_SLOW_PATH Fields operator*(Fields&& fst, const Field& snd)
      {
        fst.append(snd);
        return std::move(fst);
      }

      inline Fields operator*(Fields&& fst, const Token& snd)
      {
        return fst * implicit_field(snd);
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
        return type <<= implicit_field(type, choice);
      }

      inline Shape operator<<=(const Token& type, Choice&& choice)
      {
        return type <<= implicit_field(type, choice);
      }

      inline Shape operator<<=(const Token& type1, const Token& type2)
      {
        return type1 <<= implicit_field(type2);
      }

      inline Wellformed operator|(const Wellformed& wf1, const Wellformed& wf2)
      {
        Wellformed wf;
        wf.shapes.insert(wf2.shapes.begin(), wf2.shapes.end());
        wf.shapes.insert(wf1.shapes.begin(), wf1.shapes.end());
        return wf;
      }

      inline Wellformed operator|(Wellformed&& wf1, const Wellformed& wf2)
      {
        std::for_each(
          wf2.shapes.begin(), wf2.shapes.end(), [&](const auto& shape) {
            wf1.shapes.insert_or_assign(shape.first, shape.second);
          });

        return std::move(wf1);
      }

      inline Wellformed operator|(const Wellformed& wf1, Wellformed&& wf2)
      {
        wf2.shapes.insert(wf1.shapes.begin(), wf1.shapes.end());
        return std::move(wf2);
      }

      inline Wellformed operator|(Wellformed&& wf1, Wellformed&& wf2)
      {
        wf2.shapes.merge(wf1.shapes);
        return std::move(wf2);
      }

      inline Wellformed operator|(const Wellformed& wf, const Shape& shape)
      {
        Wellformed wf2;
        wf2.append(shape);
        wf2.shapes.insert(wf.shapes.begin(), wf.shapes.end());
        return wf2;
      }

      inline Wellformed operator|(const Wellformed& wf, Shape&& shape)
      {
        Wellformed wf2;
        wf2.append(shape);
        wf2.shapes.insert(wf.shapes.begin(), wf.shapes.end());
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

      inline Wellformed operator-(const Wellformed& wf, const Token& token)
      {
        Wellformed wf2;
        wf2.shapes.insert(wf.shapes.begin(), wf.shapes.end());
        wf2.shapes.erase(token);
        return wf2;
      }

      inline Wellformed operator-(const Wellformed& wf, Token&& token)
      {
        Wellformed wf2;
        wf2.shapes.insert(wf.shapes.begin(), wf.shapes.end());
        wf2.shapes.erase(token);
        return wf2;
      }

      inline Wellformed operator-(Wellformed&& wf, const Token& token)
      {
        wf.shapes.erase(token);
        return std::move(wf);
      }

      inline Wellformed operator-(Wellformed&& wf, Token&& token)
      {
        wf.shapes.erase(token);
        return std::move(wf);
      }
    }

    namespace detail
    {
      using WFDeque = std::deque<const Wellformed*>;
      inline thread_local std::vector<WFDeque> wf_current = {{}};

      struct WFLookup
      {
        const Wellformed* wf;
        Node node;
        std::size_t index;

        operator Node&()
        {
          return node;
        }

        WFLookup& operator=(Node rhs)
        {
          node->parent()->replace_at(index, rhs);
          node = rhs;
          return *this;
        }

        Node& operator->()
        {
          return node;
        }

        NodeDef& operator*()
        {
          return *node;
        }

        WFLookup operator/(const Token& field)
        {
          auto i = wf->index(node->type(), field);
          if (node->size() > i)
            return {wf, node->at(i), i};

          throw std::runtime_error(
            "shape `" + std::string(node->type().str()) + "` has no field `" +
            std::string(field.str()) + "`");
        }
      };

      inline void new_context()
      {
        wf_current.push_back({});
      }

      inline void end_context()
      {
        if (wf_current.size() == 1)
        {
          logging::Error() << "Cannot end the base WF context" << std::endl;
          return;
        }

        wf_current.pop_back();
      }
    }

    inline const Wellformed empty;

    inline void push_back(const Wellformed& wf)
    {
      detail::wf_current.back().push_back(&wf);
    }

    inline void pop_front()
    {
      detail::wf_current.back().pop_front();
    }
  }

  namespace reified
  {
    using namespace wf;
    using namespace ops;

    inline auto pattern = Cap | Any | TokenMatch | RegexMatch | Opt | Rep |
      Not | Choice | InsideStar | Inside | First | Last | Children | Pred |
      NegPred | Action;

    inline auto pattern_wf = (Top <<= Group) | (Cap <<= Group * Token) |
      (TokenMatch <<= Token++[1]) | (RegexMatch <<= Token * Regex) |
      (Opt <<= Group) | (Rep <<= Group) | (Not <<= Group) |
      (Choice <<= (First >>= Group) * (Last >>= Group)) |
      (InsideStar <<= Token++[1]) | (Inside <<= Token++[1]) |
      (Children <<= Group * (Children >>= Group)) | (Pred <<= Group) |
      (NegPred <<= Group) | (Action <<= Group) | (Group <<= pattern++[1]);
  }

  struct WFContext
  {
    WFContext()
    {
      wf::detail::new_context();
    }

    WFContext(const wf::Wellformed& wf) : WFContext()
    {
      push_back(wf);
    }

    WFContext(std::initializer_list<const wf::Wellformed*> wfs) : WFContext()
    {
      for (auto& wf : wfs)
      {
        push_back(*wf);
      }
    }

    ~WFContext()
    {
      wf::detail::end_context();
    }

    void push_back(const wf::Wellformed& wf)
    {
      wf::push_back(wf);
    }

    void pop_front()
    {
      wf::pop_front();
    }
  };

  inline wf::detail::WFLookup operator/(const Node& node, const Token& field)
  {
    for (auto wf : wf::detail::wf_current.back())
    {
      if (!wf)
        continue;

      auto i = wf->index(node->type(), field);

      if (i != std::numeric_limits<size_t>::max())
        return {wf, node->at(i), i};
    }

    throw std::runtime_error(
      "shape `" + std::string(node->type().str()) + "` has no field `" +
      std::string(field.str()) + "`");
  }

  inline wf::detail::WFLookup
  operator/(const wf::Wellformed& wf, const Node& node)
  {
    return {&wf, node, 0};
  }
}

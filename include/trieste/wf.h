// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "gen.h"
#include "logging.h"
#include "regex.h"

#include <algorithm>
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
    using TokenWeights = std::map<Token, std::size_t>;
    using SymtabKeys = std::pair<std::vector<Token>, size_t>;

    struct Sampling
    {
      std::map<trieste::Token, trieste::Nodes>& sampled_nodes;
      size_t
        sampling_level; // 0: subtree sampling, 1..n: subtree height sampling
      bool sampling_enabled;
      size_t sampling_frequency; // percentage of draws that use a sample

      Sampling(
        std::map<trieste::Token, trieste::Nodes>& sampled_nodes_,
        size_t sampling_level_,
        bool sampling_enabled_,
        size_t sampling_frequency_)
      : sampled_nodes(sampled_nodes_),
        sampling_level(sampling_level_),
        sampling_enabled(sampling_enabled_),
        sampling_frequency(std::min<size_t>(sampling_frequency_, 100))
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
      TokenWeights token_weights_;
      std::map<Token, SymtabKeys> binding_keys;
      bool gen_bound_vars;
      Sampling sampling;
      std::map<const std::vector<Token>*, std::vector<double>>
        weighted_offsets_cache_;

      // Tokens that are marked as references (`&Token`) somewhere in the WF.
      std::set<Token> ref_tokens;
      // Given the reference tokens that can currently be bound, computes the
      // tokens that can still be generated (see Wellformed::generatable).
      std::function<std::set<Token>(const std::set<Token>&)>
        compute_generatable;
      std::map<std::set<Token>, std::set<Token>> generatable_cache_;

      // A node whose location is assigned after the whole tree is generated.
      // A "must" hole is a reference that has to be bound. A "may" hole is a
      // position that may reuse a bound name (see gen_bound_vars).
      struct Hole
      {
        Node node;
        bool must;
        bool has_preferred = false;
        Location preferred = {};
      };
      std::vector<Hole> holes;

      // All binder nodes (with flag::lookup) created so far, whether their key
      // has been generated yet or not.
      Nodes binders;

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

      Gen& token_weights(TokenWeights token_weights)
      {
        token_weights_ = std::move(token_weights);
        return *this;
      }

      Gen& references(
        std::set<Token> ref_tokens_,
        std::function<std::set<Token>(const std::set<Token>&)> generatable_)
      {
        ref_tokens = std::move(ref_tokens_);
        compute_generatable = std::move(generatable_);
        return *this;
      }

      bool refs_enabled() const
      {
        return !ref_tokens.empty();
      }

      std::size_t weight_for(const Token& token) const
      {
        auto it = token_weights_.find(token);
        if (it == token_weights_.end())
          return 1;

        return it->second;
      }

      [[noreturn]] void throw_no_candidates(
        const std::vector<Token>& tokens,
        Token parent,
        std::size_t depth,
        const std::string& reason) const
      {
        std::ostringstream err;
        err << "Cannot choose token for parent " << parent.str() << " at depth "
            << depth << ": " << reason << std::endl;
        err << "tokens={";
        std::string delim = "";
        for (auto const& token : tokens)
        {
          err << delim << token.str() << ":" << weight_for(token);
          delim = ", ";
        }
        err << "}";
        throw std::runtime_error(err.str());
      }

      // Samples from an already-computed cumulative distribution, i.e. one
      // that has already had std::partial_sum applied to it.
      Token choose_from_cumulative(
        const std::vector<Token>& tokens,
        const std::vector<double>& cumulative,
        Token parent,
        std::size_t depth)
      {
        if (cumulative.empty() || cumulative.back() <= 0.0)
          throw_no_candidates(
            tokens, parent, depth, "no positive effective weights");

        // instead of normalising the distribution, scale the random value by
        // the sum of the weights
        double value = static_cast<double>(rand() - rand.min()) /
          static_cast<double>(rand.max() - rand.min()) * cumulative.back();

        // finding the first element greater than the uniform random number is
        // the same as performing a weighted sampling of the distribution
        auto it = std::lower_bound(cumulative.begin(), cumulative.end(), value);

        return tokens[std::distance(cumulative.begin(), it)];
      }

      Token choose_weighted(
        const std::vector<Token>& tokens,
        std::vector<double> offsets,
        Token parent,
        std::size_t depth)
      {
        // compute the cumulative distribution of the given offsets
        std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
        return choose_from_cumulative(tokens, offsets, parent, depth);
      }

      // The token-weight-only distribution for a given candidate list is the
      // same every time it is used (it only depends on the static token
      // weights, not on depth), so it is computed once per candidate list and
      // cached for the lifetime of this Gen.
      std::vector<double> compute_cumulative_weighted_offsets(
        const std::vector<Token>& tokens) const
      {
        std::vector<double> offsets;
        offsets.reserve(tokens.size());
        std::transform(
          tokens.begin(),
          tokens.end(),
          std::back_inserter(offsets),
          [&](const Token& t) { return static_cast<double>(weight_for(t)); });
        std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
        return offsets;
      }

      const std::vector<double>&
      cumulative_weighted_offsets(const std::vector<Token>& tokens)
      {
        auto found = weighted_offsets_cache_.find(&tokens);
        if (found != weighted_offsets_cache_.end())
          return found->second;

        return weighted_offsets_cache_
          .emplace(&tokens, compute_cumulative_weighted_offsets(tokens))
          .first->second;
      }

      // If `cacheable` is false, `tokens` is a temporary list (its address is
      // not stable), so nothing may be cached against it.
      Token choose(
        const std::vector<Token>& tokens,
        std::size_t depth,
        Token parent,
        bool cacheable = true)
      {
        if (tokens.empty())
          throw_no_candidates(tokens, parent, depth, "empty token list");

        if (tokens.size() == 1)
        {
          if (!token_weights_.empty() && weight_for(tokens[0]) == 0)
            throw_no_candidates(
              tokens, parent, depth, "single token has zero effective weight");
          return tokens[0];
        }

        if (depth <= target_depth)
        {
          if (!token_weights_.empty())
          {
            if (!cacheable)
              return choose_from_cumulative(
                tokens,
                compute_cumulative_weighted_offsets(tokens),
                parent,
                depth);

            return choose_from_cumulative(
              tokens, cumulative_weighted_offsets(tokens), parent, depth);
          }

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

        if (!token_weights_.empty())
        {
          // scale the offsets by the token weights
          for (size_t i = 0; i < offsets.size(); i++)
            offsets[i] *= static_cast<double>(weight_for(tokens[i]));
        }

        return choose_weighted(tokens, std::move(offsets), parent, depth);
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
        return next() % 100 < sampling.sampling_frequency;
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

      // The scopes visible from a node created as a child of `node`.
      static std::set<const NodeDef*> child_scope_chain(Node node)
      {
        Node st = (node->type() & flag::symtab) ? node : node->scope();
        return scope_chain(st);
      }

      static std::set<const NodeDef*> scope_chain(Node st)
      {
        std::set<const NodeDef*> chain;
        for (; st; st = st->scope())
          chain.insert(st.get());
        return chain;
      }

      // Whether `b` precedes (in the sense of NodeDef::precedes) a node that
      // will be appended as the last child of `node`.
      static bool precedes_new_child(const Node& b, const Node& node)
      {
        // An ancestor of the new child does not precede it.
        for (auto p = node.get(); p; p = p->parent_unsafe())
        {
          if (p == b.get())
            return false;
        }

        // Anything already below `node` is to the left of its last child.
        for (auto p = b->parent_unsafe(); p; p = p->parent_unsafe())
        {
          if (p == node.get())
            return true;
        }

        return b->precedes(node);
      }

      // The token that the binder `b` binds: its key if that has been
      // generated, otherwise the only token its key field can be.
      Token binder_key_type(const Node& b) const
      {
        auto& [types, key_index] = binding_keys.at(b->type());

        if (key_index < b->size())
          return b->at(key_index)->type();

        if (
          types.empty() ||
          std::any_of(types.begin(), types.end(), [&](auto& t) {
            return t != types.front();
          }))
          return Invalid;

        return types.front();
      }

      // Whether the binder `b` makes a reference of type `type` resolvable at
      // a position whose visible scopes are `chain`, following the rules of
      // NodeDef::lookup. `precedes` says whether `b` precedes that position.
      template<typename F>
      bool binder_counts(
        const Node& b,
        Token type,
        const std::set<const NodeDef*>& chain,
        F&& precedes) const
      {
        if (binder_key_type(b) != type)
          return false;

        auto st = b->scope();
        if (!st || chain.find(st.get()) == chain.end())
          return false;

        return !(st->type() & flag::defbeforeuse) || precedes();
      }

      bool is_in_scope(Location loc, Node scope)
      {
        auto symbols = get_symbols_from_loc(loc, scope);
        return symbols.size() != 0;
      }

    public:
      bool is_binder(Token type) const
      {
        return (type & flag::lookup) &&
          binding_keys.find(type) != binding_keys.end();
      }

      // Keep track of binders, so that references can be resolved to them.
      void register_node(const Node& node)
      {
        if (is_binder(node->type()))
          binders.push_back(node);
      }

      // How often (in percent) a fresh name is used where a bound name could
      // be reused, when at least one bound name exists.
      static constexpr size_t fresh_percentage = 10;

      // Decides whether to reuse one of `n` bound names rather than a fresh
      // name, and if so which one (in `index`). This always draws exactly one
      // random number.
      bool pick_bound(size_t n, size_t& index)
      {
        auto r = static_cast<size_t>(next());
        if (n == 0 || r % 100 < fresh_percentage)
          return false;

        index = (r / 100) % n;
        return true;
      }

      void add_hole(const Node& node, bool must)
      {
        holes.push_back({node, must});
      }

      void add_hole(const Node& node, const Location& preferred)
      {
        holes.push_back({node, true, true, preferred});
      }

      // The reference tokens that have a binder (existing or promised) that a
      // reference created as a child of `node` can resolve to.
      std::set<Token> bound_ref_tokens(Node node)
      {
        std::set<Token> bound;
        auto chain = child_scope_chain(node);

        for (auto& b : binders)
        {
          auto type = binder_key_type(b);
          if (
            ref_tokens.find(type) == ref_tokens.end() ||
            bound.find(type) != bound.end())
            continue;

          if (binder_counts(b, type, chain, [&]() {
                return precedes_new_child(b, node);
              }))
            bound.insert(type);
        }
        return bound;
      }

      // Whether the existing node `ref` can be resolved to a binder (existing
      // or promised) of its type.
      bool can_resolve(const Node& ref)
      {
        auto chain = scope_chain(ref->scope());
        return std::any_of(binders.begin(), binders.end(), [&](auto& b) {
          return binder_counts(
            b, ref->type(), chain, [&]() { return b->precedes(ref); });
        });
      }

      // Assigns locations to all holes. This happens once the whole tree has
      // been generated, so that a hole can use anything that is bound around
      // it, including later siblings where the scope allows it.
      void fill_holes()
      {
        for (auto& hole : holes)
        {
          auto& node = hole.node;

          // The hole's location is empty until it is assigned here.
          if (hole.has_preferred && !node->lookup_as(hole.preferred).empty())
          {
            node->set_location(hole.preferred);
            continue;
          }

          // Collect the bound names that lookup resolves from this node.
          std::vector<Location> candidates;
          for (auto& b : binders)
          {
            auto key_index = binding_keys.at(b->type()).second;
            if (key_index >= b->size())
              continue;

            auto key = b->at(key_index);
            if (key->type() != node->type())
              continue;

            if (!node->lookup_as(key->location()).empty())
              candidates.push_back(key->location());
          }

          if (hole.must)
          {
            if (candidates.empty())
              throw std::logic_error(
                std::string("No bound symbol available for reference ") +
                node->type().str());

            node->set_location(candidates[next() % candidates.size()]);
            continue;
          }

          size_t choice;
          if (pick_bound(candidates.size(), choice))
          {
            node->set_location(candidates[choice]);
            continue;
          }

          auto scope = node->parent()->scope();
          auto fresh = fresh_location(node);
          while (is_in_scope(fresh, scope))
            fresh = fresh_location(node);
          node->set_location(fresh);
        }

        holes.clear();
      }

      // The tokens that can be generated when the given reference tokens are
      // the ones that can be bound.
      const std::set<Token>& generatable(const std::set<Token>& bound)
      {
        auto found = generatable_cache_.find(bound);
        if (found != generatable_cache_.end())
          return found->second;

        return generatable_cache_.emplace(bound, compute_generatable(bound))
          .first->second;
      }

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

        // A position that may reuse a bound name, but is not itself a binding
        // key, is assigned once the whole tree has been generated.
        if (
          gen_bound_vars && should_gen_bound(parent, type) &&
          !is_symtab_key(parent->size() - 1, type, parent->type()))
        {
          add_hole(child, false);
          return {};
        }

        Nodes symbols = get_symbols_from_type(type, child->scope());
        // Prefer location from a symbol table if gen_bound_vars is enabled.
        size_t rand_symbol;
        if (
          pick_bound(symbols.size(), rand_symbol) &&
          should_gen_bound(parent, type))
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

        // Sampling_level 0 = entire subtree; otherwise clone sampling_level
        // levels. Clone_depth first clone the node it is given, so
        // each child gets a budget of sampling_level - 1 further levels.
        constexpr size_t full_subtree = std::numeric_limits<size_t>::max();
        size_t levels = sampling.sampling_level == 0 ?
          full_subtree :
          sampling.sampling_level - 1;

        // Copy children from sample
        for (auto& child : *sample)
          clone_depth(levels, node, child, continuations);
      }
    };
    /**
     * A token marked as a reference in a well-formedness specification, written
     * `&Token` when `wf::ops` is in scope. A reference is a leaf node whose
     * location is expected to be bound in a symbol table in scope.
     */
    struct RefToken
    {
      Token token;
    };

    struct Choice
    {
      std::vector<Token> types;

      // The subset of `types` that were marked as references (`&Token`).
      std::set<Token> refs;

      TRIESTE_SLOW_PATH Choice() = default;
      TRIESTE_SLOW_PATH Choice(std::vector<Token> types_) : types{types_} {}
      TRIESTE_SLOW_PATH Choice(std::vector<Token> types_, std::set<Token> refs_)
      : types{types_}, refs{refs_}
      {}
      TRIESTE_SLOW_PATH Choice(const RefToken& ref)
      : types{ref.token}, refs{ref.token}
      {}
      TRIESTE_SLOW_PATH Choice(const Choice&) = default;
      TRIESTE_SLOW_PATH Choice(Choice&&) = default;
      TRIESTE_SLOW_PATH Choice& operator=(const Choice&) = default;
      TRIESTE_SLOW_PATH Choice& operator=(Choice&&) = default;
      TRIESTE_SLOW_PATH ~Choice() = default;

      bool accepts_type(const Token& type) const
      {
        return std::find(types.begin(), types.end(), type) != types.end();
      }

      bool is_reference(const Token& type) const
      {
        return refs.find(type) != refs.end();
      }

      // Whether `type` can be generated, given the reference tokens that can
      // currently be bound and the tokens that can be generated as a result.
      bool can_generate(
        const Token& type,
        const std::set<Token>& bound,
        const std::set<Token>& generatable) const
      {
        if (is_reference(type))
          return bound.find(type) != bound.end();

        return generatable.find(type) != generatable.end();
      }

      // Collects the alternatives that can currently be generated as a child of
      // `node`. Returns false if that is all of them, in which case `out` is
      // not filled in. Alternatives that cannot be generated are references
      // to something that is not bound, and tokens that require such a
      // reference.
      bool available_types(Gen& g, Node node, std::vector<Token>& out) const
      {
        if (!g.refs_enabled())
          return false;

        auto bound = g.bound_ref_tokens(node);
        auto& generatable = g.generatable(bound);

        for (auto& type : types)
        {
          if (can_generate(type, bound, generatable))
            out.push_back(type);
        }

        return out.size() != types.size();
      }

      bool check(Node node) const
      {
        if (node == Error)
          return true;

        auto ok = accepts_type(node->type());

        if (ok && is_reference(node->type()) && node->lookup().empty())
        {
          logging::Error() << node->location().origin_linecol()
                           << ": unbound reference to `"
                           << node->location().view() << "`" << std::endl
                           << node->location().str() << node << std::endl;
          return false;
        }

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
        std::vector<Token> available;
        auto filtered = available_types(g, node, available);
        Token type = filtered ?
          g.choose(available, depth, node->type(), false) :
          g.choose(types, depth, node->type());

        // We may need a fresh location, so the child needs to be in the AST by
        // the time we call g.location().
        auto child = NodeDef::create(type);
        node->push_back(child);
        g.register_node(child);

        // A reference gets its location once the whole tree is generated.
        if (is_reference(type))
          g.add_hole(child, true);
        else
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

        std::vector<Token> available;
        auto filtered = choice.available_types(g, node, available);
        const auto& types = filtered ? available : choice.types;

        const auto weight_sum = std::accumulate(
          types.begin(),
          types.end(),
          size_t{0},
          [&](size_t acc, const Token& t) { return acc + g.weight_for(t); });

        if (depth >= g.target_depth || weight_sum == 0)
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
                if (shape.binding != Invalid && shape.binding != Include)
                {
                  auto key_index = shape.index(shape.binding);
                  if (key_index >= shape.fields.size())
                    return;

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
        TokenWeights token_weights,
        std::map<trieste::Token, trieste::Nodes>& sampled_nodes,
        size_t sampling_level,
        bool sampling_enabled,
        size_t sampling_frequency) const
      {
        // Collect map of tokens to their binding token and the corresponding
        // index in the children vector. This is used to preferentially generate
        // bound variables that are in scope.
        std::set<Token> ref_tokens;
        {
          std::set<Token> keys;
          collect_references(ref_tokens, keys);
        }

        auto binding_keys = std::map<trieste::Token, SymtabKeys>{};
        if (gen_bound || sampling_enabled || !ref_tokens.empty())
        {
          populate_binding_keys(binding_keys);
        }

        // References can only be generated once something is bound, so some
        // tokens may not be generatable at all.
        if (
          !ref_tokens.empty() && shapes.find(Top) != shapes.end() &&
          generatable({}).count(Top) == 0)
          throw std::runtime_error(
            "Cannot generate a tree: every tree requires a reference to "
            "something that is bound before it is used.");

        auto g =
          Gen(
            compute_minimum_distance_to_terminal(target_depth),
            gloc,
            seed,
            target_depth,
            binding_keys,
            gen_bound,
            sampled_nodes,
            sampling_level,
            sampling_enabled,
            sampling_frequency)
            .token_weights(std::move(token_weights))
            .references(ref_tokens, [this](const std::set<Token>& bound) {
              return generatable(bound);
            });
        auto top = NodeDef::create(Top);
        ast::detail::top_node() = top;
        gen_node(g, 0, top);
        g.fill_holes();
        return top;
      }

      Node gen(
        GenNodeLocationF gloc,
        Seed seed,
        size_t target_depth,
        bool gen_bound,
        TokenWeights token_weights = {}) const
      {
        std::map<trieste::Token, trieste::Nodes> empty_nodes;
        return gen(
          gloc,
          seed,
          target_depth,
          gen_bound,
          std::move(token_weights),
          empty_nodes,
          0,
          false,
          0);
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

      // Whether a `child` at `index` of a `parent` is a reference (`&Token`).
      bool
      is_ref_position(const Node& parent, size_t index, const Node& child) const
      {
        auto find = shapes.find(parent->type());
        if (find == shapes.end())
          return false;

        return std::visit(
          [&](auto& s) {
            using ShapeType = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<ShapeType, Fields>)
              return index < s.fields.size() &&
                s.fields[index].choice.is_reference(child->type());
            else
              return s.choice.is_reference(child->type());
          },
          find->second);
      }

      // Registers the binders and references of the subtree just cloned from a
      // sample into `node`, and checks that every reference in it can be
      // resolved and that every continuation can be generated. If not, the
      // sample is removed again and false is returned.
      bool
      accept_sample(Gen& g, const Node& node, const Nodes& continuations) const
      {
        auto binders_before = g.binders.size();
        Nodes refs;

        node->traverse([&](Node& current) {
          for (size_t i = 0; i < current->size(); ++i)
          {
            auto child = current->at(i);
            g.register_node(child);
            if (is_ref_position(current, i, child))
              refs.push_back(child);
          }
          return true;
        });

        auto ok = true;
        if (g.refs_enabled())
        {
          ok = std::all_of(refs.begin(), refs.end(), [&](auto& ref) {
            return g.can_resolve(ref);
          });

          ok =
            ok &&
            std::all_of(
              continuations.begin(), continuations.end(), [&](auto& cont) {
                return shapes.find(cont->type()) == shapes.end() ||
                  g.generatable(g.bound_ref_tokens(cont)).count(cont->type()) !=
                  0;
              });
        }

        if (ok)
        {
          // Replace each sampled reference by a node without a location, so
          // that it can be assigned later. The sampled location is kept if it
          // can be resolved.
          for (auto& ref : refs)
          {
            auto hole = NodeDef::create(ref->type());
            ref->parent()->replace(ref, hole);
            g.add_hole(hole, ref->location());
          }
          return true;
        }

        // Undo the sample: forget its binders, unbind its keys and remove it.
        g.binders.resize(binders_before);
        node->traverse([&](Node& current) {
          auto find = g.binding_keys.find(current->type());
          if (current != node && find != g.binding_keys.end())
          {
            auto key_index = find->second.second;
            if (key_index < current->size())
              current->unbind(current->at(key_index)->location());
          }
          return true;
        });
        node->erase(node->begin(), node->end());
        return false;
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

          if (accept_sample(g, node, continuations))
          {
            // Continue generating next layer
            for (auto child : continuations)
            {
              gen_node(g, depth + g.sampling_level(), child);
            }
            return;
          }

          // The sample would leave a reference that can never be bound, so
          // generate this node from the specification instead.
          continuations.clear();
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

      /**
       * Validates this specification, throwing std::runtime_error if it is
       * malformed. This is meant to be called once, when the specification is
       * put to use, rather than on every check.
       */
      void validate() const
      {
        validate_references();
      }

      // Collects the tokens marked as references (`&Token`) in this
      // specification, and the tokens that can be symbol table keys.
      void
      collect_references(std::set<Token>& refs, std::set<Token>& keys) const
      {
        auto add_refs = [&](const Choice& choice) {
          refs.insert(choice.refs.begin(), choice.refs.end());
        };

        for (auto& [type, shape] : shapes)
        {
          std::visit(
            [&](auto& s) {
              using ShapeType = std::decay_t<decltype(s)>;
              if constexpr (std::is_same_v<ShapeType, Fields>)
              {
                for (auto& field : s.fields)
                  add_refs(field.choice);

                if (s.binding != Invalid && s.binding != Include)
                {
                  auto key = s.index(s.binding);
                  if (key < s.fields.size())
                  {
                    auto& types = s.fields[key].choice.types;
                    keys.insert(types.begin(), types.end());
                  }
                }
              }
              else
              {
                add_refs(s.choice);
              }
            },
            shape);
        }
      }

      /**
       * Computes the tokens that can be generated when only the given
       * reference tokens can be bound. A reference alternative (`&Token`) can
       * be generated only if its token is in `bound`. A token with a shape can
       * be generated if every mandatory field has an alternative that can be
       * generated. A token with no shape is a leaf and can always be
       * generated.
       */
      std::set<Token> generatable(const std::set<Token>& bound) const
      {
        std::set<Token> result;

        // All tokens without a shape are leaves.
        for (auto& [type, shape] : shapes)
        {
          std::visit(
            [&](auto& s) {
              using ShapeType = std::decay_t<decltype(s)>;
              auto add_leaves = [&](const Choice& choice) {
                for (auto& t : choice.types)
                  if (shapes.find(t) == shapes.end())
                    result.insert(t);
              };

              if constexpr (std::is_same_v<ShapeType, Fields>)
              {
                for (auto& field : s.fields)
                  add_leaves(field.choice);
              }
              else
              {
                add_leaves(s.choice);
              }
            },
            shape);
        }

        auto any = [&](const Choice& choice) {
          return std::any_of(
            choice.types.begin(), choice.types.end(), [&](auto& t) {
              return choice.can_generate(t, bound, result);
            });
        };

        // Find the least fixed point, so that cycles (such as an expression
        // that contains expressions) need a way to terminate.
        bool changed = true;
        while (changed)
        {
          changed = false;
          for (auto& [type, shape] : shapes)
          {
            if (result.find(type) != result.end())
              continue;

            auto ok = std::visit(
              [&](auto& s) {
                using ShapeType = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<ShapeType, Fields>)
                {
                  return std::all_of(
                    s.fields.begin(), s.fields.end(), [&](auto& field) {
                      return any(field.choice);
                    });
                }
                else
                {
                  return s.min_len == 0 || any(s.choice);
                }
              },
              shape);

            if (ok)
            {
              result.insert(type);
              changed = true;
            }
          }
        }

        return result;
      }

      /**
       * Validates the reference tokens (`&Token`) of this specification.
       * Throws std::runtime_error if
       *  - a reference token is not a leaf, or
       *  - no shape has a binding key that can hold the reference token, so the
       *    reference could never be bound.
       */
      void validate_references() const
      {
        std::set<Token> refs;
        std::set<Token> keys;
        collect_references(refs, keys);

        for (auto& ref : refs)
        {
          auto find = shapes.find(ref);
          if (find != shapes.end())
          {
            auto leaf = std::visit(
              [](auto& s) {
                using ShapeType = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<ShapeType, Fields>)
                  return s.fields.empty();
                else
                  return s.max_len == 0;
              },
              find->second);

            if (!leaf)
              throw std::runtime_error(
                std::string("WF reference token must be a leaf: ") + ref.str());
          }

          if (keys.find(ref) == keys.end())
            throw std::runtime_error(
              std::string("WF reference token is never bound: ") + ref.str());
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

      inline Field implicit_field(const RefToken& ref)
      {
        return Field{ref.token, Choice{ref}, false};
      }

      inline Field implicit_field(const Token& name, const Choice& choice)
      {
        return Field{name, choice, false};
      }

      inline RefToken operator&(const Token& type)
      {
        return RefToken{type};
      }

      inline Choice operator|(const Token& type1, const Token& type2)
      {
        return Choice{std::vector<Token>{type1, type2}};
      }

      inline TRIESTE_SLOW_PATH Choice
      operator|(const Token& type, const Choice& choice)
      {
        Choice result{choice.types, choice.refs};
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
        Choice result{choice1.types, choice1.refs};
        result.types.insert(
          result.types.end(), choice2.types.begin(), choice2.types.end());
        result.refs.insert(choice2.refs.begin(), choice2.refs.end());
        return result;
      }

      inline TRIESTE_SLOW_PATH Choice
      operator|(const Choice& choice1, Choice&& choice2)
      {
        choice2.types.insert(
          choice2.types.end(), choice1.types.begin(), choice1.types.end());
        choice2.refs.insert(choice1.refs.begin(), choice1.refs.end());
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

      // Combinations involving reference tokens (`&Token`).
      inline Choice operator|(const RefToken& ref, const RefToken& other)
      {
        const Choice lhs{ref};
        const Choice rhs{other};
        return lhs | rhs;
      }

      inline Choice operator|(const RefToken& ref, const Token& type)
      {
        const Choice lhs{ref};
        const Choice rhs{std::vector<Token>{type}};
        return lhs | rhs;
      }

      inline Choice operator|(const Token& type, const RefToken& ref)
      {
        const Choice lhs{std::vector<Token>{type}};
        const Choice rhs{ref};
        return lhs | rhs;
      }

      inline Choice operator|(const RefToken& ref, const Choice& choice)
      {
        const Choice lhs{ref};
        return lhs | choice;
      }

      inline Choice operator|(const Choice& choice, const RefToken& ref)
      {
        const Choice rhs{ref};
        return choice | rhs;
      }

      inline TRIESTE_SLOW_PATH Choice
      operator-(const Choice& choice, const Token& type)
      {
        Choice result{choice.types, choice.refs};
        result.types.erase(
          std::remove(result.types.begin(), result.types.end(), type),
          result.types.end());
        result.refs.erase(type);
        return result;
      }

      inline TRIESTE_SLOW_PATH Choice
      operator-(const Choice& choice1, const Choice& choice2)
      {
        Choice result{choice1.types, choice1.refs};
        result.types.erase(
          std::remove_if(
            result.types.begin(),
            result.types.end(),
            [&](auto t) {
              return std::find(choice2.types.begin(), choice2.types.end(), t) !=
                choice2.types.end();
            }),
          result.types.end());
        for (auto& t : choice2.types)
          result.refs.erase(t);
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

      inline Sequence operator++(const RefToken& ref, int)
      {
        return Sequence{Choice{ref}, 0, std::numeric_limits<size_t>::max()};
      }

      inline Sequence operator~(const RefToken& ref)
      {
        return Sequence{Choice{ref}, 0, 1};
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

      inline Field operator>>=(const Token& name, const RefToken& ref)
      {
        return Field{name, Choice{ref}, true};
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

      // Combinations involving reference tokens (`&Token`).
      inline Fields operator*(const RefToken& fst, const RefToken& snd)
      {
        return implicit_field(fst) * implicit_field(snd);
      }

      inline Fields operator*(const RefToken& fst, const Token& snd)
      {
        return implicit_field(fst) * implicit_field(snd);
      }

      inline Fields operator*(const Token& fst, const RefToken& snd)
      {
        return implicit_field(fst) * implicit_field(snd);
      }

      inline Fields operator*(const RefToken& fst, const Field& snd)
      {
        return implicit_field(fst) * snd;
      }

      inline Fields operator*(const Field& fst, const RefToken& snd)
      {
        return fst * implicit_field(snd);
      }

      inline Fields operator*(const Fields& fst, const RefToken& snd)
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

      inline Shape operator<<=(const Token& type, const RefToken& ref)
      {
        return type <<= implicit_field(ref);
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

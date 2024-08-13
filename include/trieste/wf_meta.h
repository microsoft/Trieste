#pragma once

#include "rewrite.h"
#include "token.h"
#include "wf.h"

#include <algorithm>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <variant>

namespace trieste::wf::meta
{
  using namespace wf::ops;

  inline const auto WfMeta = TokenDef("wf-meta-meta");

  inline const auto WfNone = TokenDef("wf-meta-none");

  inline const auto WfNamespace = TokenDef("wf-meta-namespace", flag::print);

  inline const auto WfTokenDefs = TokenDef("wf-meta-tokendefs");
  inline const auto WfTokenDef = TokenDef("wf-meta-tokendef", flag::lookup);
  inline const auto WfTokenName = TokenDef("wf-meta-token-name", flag::print);
  inline const auto WfTokenFlags = TokenDef("wf-meta-token-flags");

  inline const auto WfTokenFlagPrint = TokenDef("wf-meta-token-flag-print");
  inline const auto WfTokenFlagSymtab = TokenDef("wf-meta-token-flag-symtab");
  inline const auto WfTokenFlagDefBeforeUse =
    TokenDef("wf-meta-token-flag-defbeforeuse");
  inline const auto WfTokenFlagShadowing =
    TokenDef("wf-meta-token-flag-shadowing");
  inline const auto WfTokenFlagLookup = TokenDef("wf-meta-token-flag-lookup");
  inline const auto WfTokenFlagLookdown =
    TokenDef("wf-meta-token-flag-lookdown");

  inline const auto WfShapeDefs = TokenDef("wf-meta-shapedefs");
  inline const auto WfShapeDef = TokenDef("wf-meta-shapedef");

  inline const auto WfShape = TokenDef("wf-meta-shape");
  inline const auto WfAtom = TokenDef("wf-meta-atom");
  inline const auto WfFields = TokenDef("wf-meta-fields");
  inline const auto WfFieldsBinding = TokenDef("wf-meta-fields-binding");
  inline const auto WfFieldsList = TokenDef("wf-meta-fields-list");
  inline const auto WfFieldNamedChoice = TokenDef("wf-meta-field-named-choice");
  inline const auto WfChoice = TokenDef("wf-meta-choice");
  inline const auto WfSequence = TokenDef("wf-meta-sequence");
  inline const auto WfSequenceMinLen =
    TokenDef("wf-meta-sequence-min-len", flag::print);

  // clang-format off
  inline const auto wf_wf =
      (Top <<= WfMeta)

    | (WfMeta <<= WfNamespace * WfTokenDefs * WfShapeDefs)

    | (WfTokenDefs <<= WfTokenDef++)
    | (WfTokenDef <<= WfTokenName * WfTokenFlags)[WfTokenName]
    | (WfTokenFlags <<= (
          WfTokenFlagPrint
        | WfTokenFlagSymtab
        | WfTokenFlagDefBeforeUse
        | WfTokenFlagShadowing
        | WfTokenFlagLookup
        | WfTokenFlagLookdown
      )++)

    | (WfShapeDefs <<= WfShapeDef++)
    | (WfShapeDef <<= WfTokenName * WfShape)
    | (WfShape <<= WfAtom | WfFields | WfSequence)
    | (WfFields <<= WfFieldsBinding * WfFieldsList)
    | (WfFieldsBinding <<= WfNone | WfTokenName)
    | (WfFieldsList <<= (WfTokenName | WfFieldNamedChoice)++)
    | (WfFieldNamedChoice <<= WfTokenName * WfChoice)
    | (WfChoice <<= WfTokenName++[1])
    | (WfSequence <<= WfChoice * WfSequenceMinLen)
    ;
  // clang-format on

  namespace detail
  {
    inline const std::initializer_list<
      std::pair<TokenDef::flag, const TokenDef&>>
      flag_types = {
        {flag::print, WfTokenFlagPrint},
        {flag::symtab, WfTokenFlagSymtab},
        {flag::defbeforeuse, WfTokenFlagDefBeforeUse},
        {flag::shadowing, WfTokenFlagDefBeforeUse},
        {flag::lookup, WfTokenFlagLookup},
        {flag::lookdown, WfTokenFlagLookdown},
    };

    inline const std::initializer_list<std::string_view> ns_ignores = {
      Top.name,
      Group.name,
      File.name,
      Directory.name,
    };

    inline bool should_ignore_ns(const std::string_view& name)
    {
      return std::find(
               detail::ns_ignores.begin(), detail::ns_ignores.end(), name) !=
        ns_ignores.end();
    }

    struct TokenHasher
    {
      inline size_t operator()(const Token& token) const
      {
        return std::hash<const TokenDef*>{}(token.def);
      }
    };

    inline std::unordered_set<Token, TokenHasher>
    find_reachable_tokens(const Wellformed& wf)
    {
      std::unordered_set<Token, TokenHasher> reachable_tokens(wf.shapes.size());

      std::vector<Token> todo;
      auto visit = [&](const Token& token) -> void {
        if (reachable_tokens.find(token) == reachable_tokens.end())
        {
          reachable_tokens.insert(token);
          todo.push_back(token);
        }
      };

      visit(Top);

      while (!todo.empty())
      {
        Token token = todo.back();
        todo.pop_back();

        auto it = wf.shapes.find(token);
        if (it != wf.shapes.end())
        {
          std::visit(
            overload{
              [&](const Sequence& seq) -> void {
                for (const auto& type : seq.choice.types)
                {
                  visit(type);
                }
              },
              [&](const Fields& fields) -> void {
                if (fields.binding != Invalid)
                {
                  visit(fields.binding);
                }
                for (const auto& field : fields.fields)
                {
                  visit(field.name);
                  for (const auto& type : field.choice.types)
                  {
                    visit(type);
                  }
                }
              }},
            it->second);
        }
      }

      return reachable_tokens;
    }
  }

  inline Node wf_to_node(const Wellformed& wf, std::string ns = "")
  {
    WFContext ctx{wf_wf}; // important, or node / tok lookups break

    std::string raw_ns = ns;
    if (ns != "")
    {
      ns += "-"; // so we get ns-* not ns*
    }

    auto reachable_tokens{detail::find_reachable_tokens(wf)};
    std::vector<Token> reachable_tokens_ord{
      reachable_tokens.begin(), reachable_tokens.end()};
    // lexically sort the tokens by name, so our output is stable
    std::sort(
      reachable_tokens_ord.begin(),
      reachable_tokens_ord.end(),
      [](const Token& lhs, const Token& rhs) { return lhs.str() < rhs.str(); });

    for (const auto& tok : reachable_tokens_ord)
    {
      std::string_view tok_name = tok.str();
      if (detail::should_ignore_ns(tok_name))
      {
        // We found a standard token that could be in any WF. It's not bad
        // namespacing for this not to follow the prefix.
        continue;
      }
      // starts_with but compatible with C++17
      if (tok_name.substr(0, ns.size()) != ns)
      {
        std::ostringstream msg;
        msg << "Token \"" << tok_name
            << "\" does not start with namespace prefix \"" << ns << "\"";
        throw std::runtime_error(msg.str());
      }
    }

    auto to_token_name = [&](std::string_view name) -> Node {
      if (detail::should_ignore_ns(name))
      {
        return WfTokenName ^ ("$" + std::string(name));
      }
      else
      {
        return WfTokenName ^ std::string(name.substr(ns.size()));
      }
    };

    Node token_defs = WfTokenDefs;
    for (const auto& token : reachable_tokens_ord)
    {
      Node flag_tokens = WfTokenFlags;
      for (const auto& [flag, flag_token] : detail::flag_types)
      {
        if (token & flag)
        {
          flag_tokens->push_back(flag_token);
        }
      }

      token_defs->push_back(
        WfTokenDef << to_token_name(token.str()) << flag_tokens);
    }

    Node shape_defs = WfShapeDefs;
    for (const auto& token : reachable_tokens_ord)
    {
      Node token_name = to_token_name(token.def->name);
      auto it = wf.shapes.find(token);
      if (it != wf.shapes.end())
      {
        std::visit(
          overload{
            [&](const Sequence& seq) -> void {
              Node choice = WfChoice;
              assert(seq.choice.types.size() > 0);
              for (const auto& type : seq.choice.types)
              {
                choice->push_back(to_token_name(type.str()));
              }
              Node min_len = WfSequenceMinLen ^ std::to_string(seq.minlen);
              shape_defs->push_back(
                WfShapeDef << token_name
                           << (WfShape << (WfSequence << choice << min_len)));
            },
            [&](const Fields& fields) -> void {
              Node binding = WfFieldsBinding;
              if (fields.binding != Invalid)
              {
                binding->push_back(to_token_name(fields.binding.str()));
              }
              else
              {
                binding->push_back(WfNone);
              }
              Node fields_list = WfFieldsList;
              for (const auto& field : fields.fields)
              {
                if (
                  field.choice.types.size() == 1 &&
                  field.choice.types.front() == field.name)
                {
                  fields_list->push_back(to_token_name(field.name.str()));
                }
                else
                {
                  Node name = to_token_name(field.name.str());
                  Node choice = WfChoice;
                  for (const auto& type : field.choice.types)
                  {
                    choice->push_back(to_token_name(type.str()));
                  }
                  fields_list->push_back(WfFieldNamedChoice << name << choice);
                }
              }
              shape_defs->push_back(
                WfShapeDef << token_name
                           << (WfShape
                               << (WfFields << binding << fields_list)));
            }},
          it->second);
      }
      else
      {
        shape_defs->push_back(WfShapeDef << token_name << (WfShape << WfAtom));
      }
    }

    auto result =
      Top << (WfMeta << (WfNamespace ^ raw_ns) << token_defs << shape_defs);
    if (!wf_wf.build_st(result))
    {
      throw std::runtime_error("Failed to build symbol table");
    }
    return result;
  }

  inline Wellformed node_to_wf(Node top)
  {
    WFContext ctx{wf_wf}; // important, or node / tok lookups break

    assert(top == Top);
    Node meta = top / WfMeta;
    assert(meta == WfMeta);

    std::unordered_map<std::string_view, Token> known_tokens{
      (meta / WfTokenDefs)->size()};

    std::string ns = std::string((meta / WfNamespace)->location().view());
    if (ns != "")
    {
      ns += "-";
    }

    auto namespaced_name = [&](std::string_view name) -> std::string {
      if (name.substr(0, 1) == "$" && detail::should_ignore_ns(name.substr(1)))
      {
        return std::string(name.substr(1));
      }
      else
      {
        return ns + std::string(name);
      }
    };

    for (const auto& token_def : *(meta / WfTokenDefs))
    {
      assert(token_def == WfTokenDef);

      Node name = token_def / WfTokenName;
      Node flags = token_def / WfTokenFlags;

      TokenDef::flag expected_flags = 0;
      for (const auto& flag : *flags)
      {
        for (const auto& [flag_num, flag_token] : detail::flag_types)
        {
          if (flag == flag_token)
          {
            expected_flags |= flag_num;
          }
        }
      }

      Token the_token =
        trieste::detail::find_token(namespaced_name(name->location().view()));
      if (the_token.def->fl != expected_flags)
      {
        std::ostringstream msg;
        msg << "Flags mismatch on token \"" << name->location().view()
            << " (fully qualified \""
            << namespaced_name(name->location().view()) << "\")\": expected "
            << std::hex << expected_flags << " but the token in this binary (\""
            << the_token.str() << "\") has " << the_token.def->fl;
        throw std::runtime_error(msg.str());
      }

      known_tokens.insert(std::pair{name->location().view(), the_token});
    }

    Wellformed wf;

    for (const auto& shape_def : *(meta / WfShapeDefs))
    {
      auto token_by_name = [&](const Node& name) -> Token {
        assert(name == WfTokenName);
        assert(name->lookup().size() == 1);
        return known_tokens.at(name->location().view());
      };

      auto read_choice = [&](const Node& choice) -> Choice {
        assert(choice == WfChoice);
        std::vector<Token> types;
        for (const auto& type_name : *choice)
        {
          types.push_back(token_by_name(type_name));
        }
        return Choice{types};
      };

      Node name = shape_def / WfTokenName;
      Node shape = (shape_def / WfShape)->front();

      if (shape == WfAtom)
      {
        // Pass. "atom" means tokens that are known and referenced, but have no
        // recorded shape (and are assumed to have no children)
      }
      else if (shape == WfSequence)
      {
        Node choice = shape / WfChoice;
        Node minlenNode = shape / WfSequenceMinLen;
        size_t minlen = std::stol(std::string(minlenNode->location().view()));

        wf.shapes[token_by_name(name)] = Sequence{read_choice(choice), minlen};
      }
      else if (shape == WfFields)
      {
        Node binding_opt = (shape / WfFieldsBinding)->front();
        Token binding;
        if (binding_opt == WfTokenName)
        {
          binding = token_by_name(binding_opt);
        }
        else
        {
          assert(binding_opt == WfNone);
          binding = Invalid;
        }

        Node fields_list = shape / WfFieldsList;
        std::vector<Field> fields;
        for (const auto& field : *fields_list)
        {
          if (field == WfTokenName)
          {
            fields.push_back(Field{
              token_by_name(field),
              Choice{{token_by_name(field)}},
            });
          }
          else if (field == WfFieldNamedChoice)
          {
            Node field_name = field / WfTokenName;
            Node choice = field / WfChoice;
            fields.push_back(Field{
              token_by_name(field_name),
              read_choice(choice),
            });
          }
          else
          {
            assert(false && "Unrecognized field type");
          }
        }

        wf.shapes[token_by_name(name)] = Fields{fields, binding};
      }
      else
      {
        assert(false && "Shape of unknown type");
      }
    }

    return wf;
  }
}

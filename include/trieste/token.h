// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "logging.h"
#include "source.h"

#include <atomic>
#include <map>

namespace trieste
{
  class NodeDef;

  // Certain uses of the Node alias before the full definition of NodeDef can
  // cause incomplete type errors, so this manually relocates the problematic
  // code to after NodeDef is fully defined. See the docs on the specialized
  // trait for details.
  //
  // Note: this is only needed by our C++17 implementation of NodeRange (in
  // ast.h). If we stop supporting C++17, this can be deleted.
  template<>
  struct intrusive_refcounted_traits<NodeDef>
  {
    static constexpr void intrusive_inc_ref(NodeDef*);
    inline static void intrusive_dec_ref(NodeDef*);
  };

  using Node = intrusive_ptr<NodeDef>;

  struct TokenDef;
  struct Token;

  namespace detail
  {
    void register_token(const TokenDef& def);
  }

  struct TokenDef
  {
    using flag = uint32_t;
    const char* name;
    flag fl;

    // Hash id for this token.  This is used to determine the hash function for
    // the default map for the main rewrite loop.  This is not a general purpose
    // hash function.
    uint32_t default_map_id;
    static constexpr size_t DEFAULT_MAP_TABLE_SIZE{128};

    TokenDef(const char* name_, flag fl_ = 0) : name(name_), fl(fl_)
    {
      static std::atomic<uint32_t> next_id = 0;
      default_map_id = (next_id++ % DEFAULT_MAP_TABLE_SIZE) * sizeof(void*);

      detail::register_token(*this);
    }

    TokenDef() = delete;
    TokenDef(const TokenDef&) = delete;

    operator Node() const;

    bool has(TokenDef::flag f) const
    {
      return (fl & f) != 0;
    }
  };

  struct Token
  {
    const TokenDef* def;

    Token() : def(nullptr) {}
    Token(const TokenDef& def_) : def(&def_) {}

    operator Node() const;

    /**
     * Special hash for looking up in tables of size DEFAULT_MAP_TABLE_SIZE with
     * elements of size sizeof(void*).
     */
    uint32_t default_map_hash() const
    {
      return def->default_map_id / sizeof(void*);
    }

    bool operator&(TokenDef::flag f) const
    {
      return def->has(f);
    }

    bool operator==(const Token& that) const
    {
      return def == that.def;
    }

    bool operator!=(const Token& that) const
    {
      return def != that.def;
    }

    bool operator<(const Token& that) const
    {
      return def < that.def;
    }

    bool operator>(const Token& that) const
    {
      return def > that.def;
    }

    bool operator<=(const Token& that) const
    {
      return def <= that.def;
    }

    bool operator>=(const Token& that) const
    {
      return def >= that.def;
    }

    bool in(const std::initializer_list<Token>& list) const
    {
      return std::find(list.begin(), list.end(), *this) != list.end();
    }

    bool in(const std::vector<Token>& list) const
    {
      return std::find(list.begin(), list.end(), *this) != list.end();
    }

    const char* str() const
    {
      return def->name;
    }
  };

  namespace flag
  {
    constexpr TokenDef::flag none = 0;

    // Print the location when printing an AST node of this type.
    constexpr TokenDef::flag print = 1 << 0;

    // Include a symbol table in an AST node of this type.
    constexpr TokenDef::flag symtab = 1 << 1;

    // If an AST node of this type has a symbol table, definitions can only be
    // found from later in the same source file.
    constexpr TokenDef::flag defbeforeuse = 1 << 2;

    // If a definition of this type is in a symbol table, it don't recurse into
    // parent symbol tables.
    constexpr TokenDef::flag shadowing = 1 << 3;

    // If a definition of this type is in a symbol table, it can be found when
    // looking up.
    constexpr TokenDef::flag lookup = 1 << 4;

    // If a definition of this type in a symbol table, it can be found when
    // looking down.
    constexpr TokenDef::flag lookdown = 1 << 5;

    // Used for AST nodes to represent internal Trieste features.  Rewriting
    // should not occur inside an internal node.
    constexpr TokenDef::flag internal = 1 << 6;
  }

  // Built-in grouping
  inline const auto Top = TokenDef("top", flag::symtab);
  inline const auto Directory = TokenDef("directory");
  inline const auto File = TokenDef("file");
  inline const auto Group = TokenDef("group");

  // Special tokens for effects
  inline const auto Seq = TokenDef("seq");
  inline const auto Lift = TokenDef("lift", flag::internal);
  inline const auto NoChange = TokenDef("nochange");
  inline const auto Reapply = TokenDef("reapply", flag::internal);

  // Special tokens for symbol tables
  inline const auto Include = TokenDef("include");

  // Special tokens for error handling
  inline const auto Invalid = TokenDef("invalid");
  inline const auto Error = TokenDef("error", flag::internal);
  inline const auto ErrorMsg = TokenDef("errormsg", flag::print);
  inline const auto ErrorAst = TokenDef("errorast");

  namespace detail
  {
    inline std::map<std::string_view, Token>& token_map()
    {
      static std::map<std::string_view, Token> global_map;
      return global_map;
    }

    inline void register_token(const TokenDef& def)
    {
      auto& map = token_map();
      auto it = map.find(def.name);
      if (it != map.end())
        throw std::runtime_error(
          "Duplicate token definition: " + std::string(def.name));

      Token t = def;
      map[t.str()] = t;
    }

    inline Token find_token(std::string_view str)
    {
      auto& map = token_map();
      auto it = map.find(str);

      if (it != map.end())
        return it->second;

      return Invalid;
    }
  }
}

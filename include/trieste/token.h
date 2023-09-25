// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "source.h"

#include <atomic>
#include <map>
#include <snmalloc/ds_core/defines.h>

namespace trieste
{
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
    uint32_t hash_id;
    static constexpr size_t HASH_SIZE{128};

    TokenDef(const char* name_, flag fl_ = 0) : name(name_), fl(fl_)
    {
      static std::atomic<uint32_t> next_id = 0;
      hash_id = (next_id++ % HASH_SIZE) * sizeof(void*);
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
     * Special hash for looking up in tables of size HASH_SIZE with
     * elements of size sizeof(void*).
     */
    uint32_t hash() const
    {
      SNMALLOC_ASSUME((def->hash_id & (sizeof(void*) - 1)) == 0);
      return def->hash_id / sizeof(void*);
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

  inline const auto Invalid = TokenDef("invalid");
  inline const auto Top = TokenDef("top", flag::symtab);
  inline const auto Group = TokenDef("group");
  inline const auto File = TokenDef("file");
  inline const auto Directory = TokenDef("directory");
  inline const auto Seq = TokenDef("seq");
  inline const auto Lift = TokenDef("lift", flag::internal);
  inline const auto NoChange = TokenDef("nochange");
  inline const auto Include = TokenDef("include");
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

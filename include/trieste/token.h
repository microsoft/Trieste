// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "source.h"

namespace trieste
{
  struct Token;

  struct TokenDef
  {
    using flag = uint32_t;
    const char* name;
    flag fl;

    consteval TokenDef(const char* name, flag fl = 0) : name(name), fl(fl) {}

    TokenDef() = delete;
    TokenDef(const TokenDef&) = delete;

    operator Node() const;

    constexpr bool has(TokenDef::flag f) const
    {
      return (fl & f) != 0;
    }
  };

  struct Token
  {
    const TokenDef* def;

    constexpr Token() : def(nullptr) {}
    constexpr Token(const TokenDef& def) : def(&def) {}

    operator Node() const;

    constexpr bool operator&(TokenDef::flag f) const
    {
      return (def->has(f)) != 0;
    }

    constexpr bool operator==(const Token& that) const
    {
      return def == that.def;
    }

    constexpr bool operator!=(const Token& that) const
    {
      return def != that.def;
    }

    constexpr bool operator<(const Token& that) const
    {
      return def < that.def;
    }

    constexpr bool operator>(const Token& that) const
    {
      return def > that.def;
    }

    constexpr bool operator<=(const Token& that) const
    {
      return def <= that.def;
    }

    constexpr bool operator>=(const Token& that) const
    {
      return def >= that.def;
    }

    constexpr bool in(const std::initializer_list<Token>& list) const
    {
      return std::find(list.begin(), list.end(), *this) != list.end();
    }

    constexpr const char* str() const
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
  }

  inline constexpr auto Invalid = TokenDef("invalid");
  inline constexpr auto Unclosed = TokenDef("unclosed");
  inline constexpr auto Top = TokenDef("top", flag::symtab);
  inline constexpr auto Group = TokenDef("group");
  inline constexpr auto File = TokenDef("file");
  inline constexpr auto Directory = TokenDef("directory");
  inline constexpr auto Seq = TokenDef("seq");
  inline constexpr auto Lift = TokenDef("lift");
  inline constexpr auto Include = TokenDef("include");
  inline constexpr auto Error = TokenDef("error");
  inline constexpr auto ErrorMsg = TokenDef("errormsg", flag::print);
  inline constexpr auto ErrorAst = TokenDef("errorast");
}

#pragma once

#include <trieste/driver.h>

namespace shrubbery
{
  using namespace trieste;

  inline const auto Paren   = TokenDef("paren");     // ()
  inline const auto Bracket = TokenDef("bracket");   // []
  inline const auto Brace   = TokenDef("brace");     // {}
  inline const auto Block   = TokenDef("block");     // :
  inline const auto Comma   = TokenDef("comma");     // ,
  inline const auto Semi    = TokenDef("semicolon"); // ;
  inline const auto Alt     = TokenDef("alt");       // |
  inline const auto Op      = TokenDef("op", flag::print); // Operators
  inline const auto Atom    = TokenDef("atom", flag::print); // Everything else

  // Used as identifiers
  inline const auto Id = TokenDef("id");
  inline const auto Lhs = TokenDef("lhs");
  inline const auto Rhs = TokenDef("rhs");

  Parse parser();
  Reader reader();
}

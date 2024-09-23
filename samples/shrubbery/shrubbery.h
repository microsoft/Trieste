#pragma once

#include <trieste/trieste.h>

namespace shrubbery
{
  using namespace trieste;

  inline const auto Paren   = TokenDef("shrub-paren");     // ()
  inline const auto Bracket = TokenDef("shrub-bracket");   // []
  inline const auto Brace   = TokenDef("shrub-brace");     // {}
  inline const auto Block   = TokenDef("shrub-block");     // :
  inline const auto Comma   = TokenDef("shrub-comma");     // ,
  inline const auto Semi    = TokenDef("shrub-semicolon"); // ;
  inline const auto Alt     = TokenDef("shrub-alt");       // |
  inline const auto Op      = TokenDef("shrub-op", flag::print); // Operators
  inline const auto Atom    = TokenDef("shrub-atom", flag::print); // Everything else

  // Used for final structure
  inline const auto Terms = TokenDef("shrub-terms");
  inline const auto None  = TokenDef("shrub-none");

  // Used as identifiers
  inline const auto Id = TokenDef("shrub-id");
  inline const auto Lhs = TokenDef("shrub-lhs");
  inline const auto Rhs = TokenDef("shrub-rhs");

  // clang-format off
  inline const auto wf =
    (Top <<= File)
    | (File <<= Group++)
    | (Paren <<= Group++)
    | (Bracket <<= Group++)
    | (Brace <<= Group++)
    | (Block <<= Group++)
    | (Alt <<= Block++[1])
    | (Group <<= Terms * (Block >>= Block | None) * (Alt >>= Alt | None))
    | (Terms <<= (Paren | Bracket | Brace | Op | Atom)++)
    ;
  // clang-format on

  Parse parser();
  Reader reader();
}

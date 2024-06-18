#pragma once

#include "infix.h"

namespace infix
{
  inline const auto Paren = TokenDef("infix-paren");
  inline const auto Equals = TokenDef("infix-equals");
  inline const auto Print = TokenDef("infix-print");

  inline const auto wf_literal = Int | Float;
  inline const auto wf_parse_tokens = wf_literal | String | Paren | Print |
    Ident | Add | Subtract | Divide |
    Multiply
    // --- tuples extension ---
    | Tuple;

  // clang-format off
  inline const auto wf_parser =
      (Top <<= File)
    | (File <<= (Group | Equals)++)
    | (Paren <<= Group++)
    | (Equals <<= Group++)
    | (Group <<= wf_parse_tokens++)
    ;
  // clang-format on

  inline const auto Number = T(Int, Float);

  // The Error token allows the creation of a special node which we can
  // use to replace the erroneous node. This will then exempt that subtree
  // from the well-formedness check. This is the mechanism by which we can
  // use the testing system to discover edges cases, i.e. the testing will
  // not proceed to the next pass until all of the invalid subtrees have
  // been marked as `Error`.
  inline auto err(const NodeRange& r, const std::string& msg)
  {
    return Error << (ErrorMsg ^ msg) << (ErrorAst << r);
  }

  inline auto err(Node node, const std::string& msg)
  {
    return Error << (ErrorMsg ^ msg) << (ErrorAst << node);
  }

  Parse parser(bool use_parser_tuples);
}

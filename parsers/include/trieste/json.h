// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "trieste/token.h"
#include "trieste/trieste.h"

namespace trieste
{
  namespace json
  {
    using namespace wf::ops;

    inline const auto Value = TokenDef("json-value");
    inline const auto Object = TokenDef("json-object");
    inline const auto Array = TokenDef("json-array");
    inline const auto String = TokenDef("json-string", flag::print);
    inline const auto Number = TokenDef("json-number", flag::print);
    inline const auto True = TokenDef("json-true");
    inline const auto False = TokenDef("json-false");
    inline const auto Null = TokenDef("json-null");
    inline const auto Member = TokenDef("json-member");
    inline const auto ErrorSeq = TokenDef("json-errorseq");
    inline const auto Comma = TokenDef("json-comma");
    inline const auto Colon = TokenDef("json-colon");
    inline const auto Lhs = TokenDef("json-lhs");
    inline const auto Rhs = TokenDef("json-rhs");

    // groups
    inline const auto ArrayGroup = TokenDef("json-array-group");
    inline const auto ObjectGroup = TokenDef("json-object-group");

    inline const auto wf_value_tokens =
      Object | Array | String | Number | True | False | Null;

    // clang-format off
    inline const auto wf =
      (Top <<= wf_value_tokens++[1])
      | (Object <<= Member++)
      | (Member <<= String * (Value >>= wf_value_tokens))
      | (Array <<= wf_value_tokens++)
      ;
    // clang-format on

    Reader reader(bool allow_multiple = false);
    Writer writer(
      const std::string& name,
      bool prettyprint = false,
      const std::string& indent = "  ");
    std::string to_string(
      Node json, bool prettyprint = false, const std::string& indent = "  ");
    bool equal(Node lhs_json, Node rhs_json);
  }
}

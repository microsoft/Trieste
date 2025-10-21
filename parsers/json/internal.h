#pragma once

#include "json.h"

namespace trieste
{
  namespace json
  {
    inline const auto Comma = TokenDef("json-comma");
    inline const auto Colon = TokenDef("json-colon");
    inline const auto Lhs = TokenDef("json-lhs");
    inline const auto Rhs = TokenDef("json-rhs");

    Parse parser();

    inline const auto wf_parse_tokens =
      Object | Array | String | Number | True | False | Null | Comma | Colon;

    // clang-format off
    inline const auto wf_parse =
      (Top <<= File)
      | (File <<= ~Group)
      | (Value <<= Group)
      | (Array <<= Group)
      | (Object <<= Group)
      | (Member <<= Group)
      | (Group <<= wf_parse_tokens++)
      ;
    // clang-format on

    template<typename T>
    inline auto err(Node node, const T& msg)
    {
      return Error << (ErrorMsg ^ msg) << (ErrorAst << node->clone());
    }

    template<typename T>
    inline Node err(const NodeRange& r, const T& msg)
    {
      return Error << (ErrorMsg ^ msg) << (ErrorAst << r);
    }

    template<typename T>
    inline auto err(const T& msg)
    {
      return Error << (ErrorMsg ^ msg);
    }
  }
}

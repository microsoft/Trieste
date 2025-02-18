// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "trieste/trieste.h"

namespace trieste
{
  namespace json
  {
    using namespace wf::ops;

    inline const auto Value = TokenDef("json-value");
    inline const auto Object = TokenDef("json-object", flag::symtab);
    inline const auto Array = TokenDef("json-array");
    inline const auto String = TokenDef("json-string", flag::print);
    inline const auto Number = TokenDef("json-number", flag::print);
    inline const auto True = TokenDef("json-true");
    inline const auto False = TokenDef("json-false");
    inline const auto Null = TokenDef("json-null");
    inline const auto Member = TokenDef("json-member", flag::lookdown);
    inline const auto ErrorSeq = TokenDef("json-errorseq");
    inline const auto Key = TokenDef("json-key", flag::print);

    // groups
    inline const auto ArrayGroup = TokenDef("json-array-group");
    inline const auto ObjectGroup = TokenDef("json-object-group");

    inline const auto wf_value_tokens =
      Object | Array | String | Number | True | False | Null;

    // clang-format off
    inline const auto wf =
      (Top <<= wf_value_tokens++[1])
      | (Object <<= Member++)
      | (Member <<= Key * (Value >>= wf_value_tokens))[Key]
      | (Array <<= wf_value_tokens++)
      ;
    // clang-format on

    Reader reader(bool allow_multiple = false);
    Writer writer(
      const std::filesystem::path& path,
      bool prettyprint = false,
      bool sort_keys = false,
      const std::string& indent = "  ");
    std::string to_string(
      Node json,
      bool prettyprint = false,
      bool sort_keys = false,
      const std::string& indent = "  ");
    bool equal(Node lhs_json, Node rhs_json);
    std::string unescape(const std::string_view& string);
    std::string escape(const std::string_view& string);

    Node object(const Nodes& members);
    Node member(Node key, Node value);
    Node array(const Nodes& elements);
    Node value(const std::string& value);
    Node value(double value);
    Node boolean(bool value);
    Node null();
  }
}

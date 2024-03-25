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
    inline const auto String = TokenDef("json-string");
    inline const auto Number = TokenDef("json-number");
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

    inline const auto wf_parse_tokens =
      Object | Array | String | Number | True | False | Null | Comma | Colon;

    // clang-format off
  inline const auto wf_parse =
    (Top <<= File)
    | (File <<= Group++)
    | (Value <<= Group)
    | (Array <<= Group)
    | (Object <<= Group)
    | (Member <<= Group)
    | (Group <<= wf_parse_tokens++)
    ;
    // clang-format on

    inline const auto wf_value_tokens =
      Object | Array | String | Number | True | False | Null;

    // clang-format off
  inline const auto wf_groups =
    (Top <<= wf_value_tokens)
    | (Object <<= ObjectGroup)
    | (Array <<= ArrayGroup)
    | (ObjectGroup <<= (wf_value_tokens | Colon | Comma)++)
    | (ArrayGroup <<= (wf_value_tokens | Comma)++)
    ;
    // clang-format on

    // clang-format off
  inline const auto wf_structure =
    (Top <<= wf_value_tokens)
    | (Object <<= Member++)
    | (Member <<= String * (Value >>= wf_value_tokens))
    | (Array <<= wf_value_tokens++)
    ;
    // clang-format on

    inline auto err(Node node, const std::string& msg)
    {
      return Error << (ErrorMsg ^ msg) << (ErrorAst << node->clone());
    }

    inline Node err(const NodeRange& r, const std::string& msg)
    {
      return Error << (ErrorMsg ^ msg) << (ErrorAst << r);
    }

    inline auto err(const std::string& msg)
    {
      return Error << (ErrorMsg ^ msg);
    }

    Parse parser();
    std::vector<Pass> passes();

    class JSONEmitter
    {
    public:
      JSONEmitter(bool prettyprint = false, const std::string& indent = "  ");

      void emit(std::ostream& os, const Node& value) const;

    private:
      void emit_value(
        std::ostream& os, const std::string& indent, const Node& value) const;
      void emit_object(
        std::ostream& os, const std::string& indent, const Node& value) const;
      void emit_array(
        std::ostream& os, const std::string& indent, const Node& value) const;

      bool m_prettyprint;
      std::string m_indent;
    };

    class JSONReader
    {
    public:
      JSONReader(const std::filesystem::path& path);
      JSONReader(const std::string& json);
      JSONReader(const Source& source);

      void read();

      const Node& element() const;
      bool has_errors() const;
      std::string error_message() const;

      JSONReader& debug_enabled(bool value);
      bool debug_enabled() const;

      JSONReader& debug_path(const std::filesystem::path& path);
      const std::filesystem::path& debug_path() const;

      JSONReader& well_formed_checks_enabled(bool value);
      bool well_formed_checks_enabled() const;

    private:
      Source m_source;
      Node m_element;
      bool m_debug_enabled;
      std::filesystem::path m_debug_path;
      bool m_well_formed_checks_enabled;
    };
  }
}

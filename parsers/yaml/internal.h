#pragma once

#include "trieste/yaml.h"

#include <set>
#include <string>
#include <string_view>

namespace trieste
{
  namespace yaml
  {
    Parse parser();

    enum class Chomp
    {
      Clip,
      Strip,
      Keep,
    };

    std::string
    escape_chars(const std::string_view& str, const std::set<char>& to_escape);
    std::string unescape_url_chars(const std::string_view& input);
    std::string replace_all(
      const std::string_view& v,
      const std::string_view& find,
      const std::string_view& replace);

    inline const auto wf_parse_tokens = Stream | Document | Hyphen | NewLine |
      Whitespace | Value | Int | Float | Hex | True | False | Null | Colon |
      TagDirective | Anchor | Alias | SingleQuote | DoubleQuote |
      VersionDirective | UnknownDirective | DocumentStart | DocumentEnd | Tag |
      TagPrefix | ShorthandTag | VerbatimTag | TagPrefix | TagHandle | Literal |
      Folded | IndentIndicator | ChompIndicator | Key | FlowMapping |
      FlowMappingStart | FlowMappingEnd | FlowSequence | FlowSequenceStart |
      FlowSequenceEnd | Comma | Comment | MaybeDirective;
    ;

    // clang-format off
  inline const auto wf_parse =
    (Top <<= File)
    | (File <<= Group)
    | (Stream <<= Group++)
    | (Document <<= Group)
    | (Tag <<= Group)
    | (FlowMapping <<= Group++)
    | (FlowSequence <<= Group++)
    | (TagDirective <<= Group)
    | (Group <<= wf_parse_tokens++[1])
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
  }
}

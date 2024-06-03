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

    inline const auto Whitespace = TokenDef("yaml-whitespace", flag::print);
    inline const auto Hyphen = TokenDef("yaml-hyphen");
    inline const auto Colon = TokenDef("yaml-colon");
    inline const auto Comma = TokenDef("yaml-comma");
    inline const auto NewLine = TokenDef("yaml-newline");
    inline const auto Comment = TokenDef("yaml-comment", flag::print);
    inline const auto VerbatimTag = TokenDef("yaml-verbatimtag", flag::print);
    inline const auto ShorthandTag = TokenDef("yaml-shorthandtag", flag::print);
    inline const auto Tag = TokenDef("yaml-tag");
    inline const auto IndentIndicator =
      TokenDef("yaml-indentation-indicator", flag::print);
    inline const auto FlowSequenceStart =
      TokenDef("yaml-flowseqstart", flag::print);
    inline const auto FlowSequenceEnd =
      TokenDef("yaml-flowseqend", flag::print);
    inline const auto FlowMappingStart =
      TokenDef("yaml-flowmapstart", flag::print);
    inline const auto FlowMappingEnd = TokenDef("yaml-flowmapend", flag::print);
    inline const auto MaybeDirective =
      TokenDef("yaml-maybedirective", flag::print);
    inline const auto Block = TokenDef("yaml-block");
    inline auto WhitespaceLine = TokenDef("yaml-whitespace-line", flag::print);

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

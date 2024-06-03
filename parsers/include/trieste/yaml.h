// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "trieste/trieste.h"

namespace trieste
{
  namespace yaml
  {
    using namespace wf::ops;

    inline const auto Stream =
      TokenDef("yaml-stream", flag::symtab | flag::defbeforeuse);
    inline const auto Directives = TokenDef("yaml-directives");
    inline const auto UnknownDirective =
      TokenDef("yaml-unknowndirective", flag::print);
    inline const auto VersionDirective =
      TokenDef("yaml-versiondirective", flag::print);
    inline const auto TagDirective =
      TokenDef("yaml-tagdirective", flag::lookup | flag::shadowing);
    inline const auto TagPrefix = TokenDef("yaml-tagprefix", flag::print);
    inline const auto TagHandle = TokenDef("yaml-taghandle", flag::print);
    inline const auto Documents = TokenDef("yaml-documents");
    inline const auto Document =
      TokenDef("yaml-document", flag::symtab | flag::defbeforeuse);
    inline const auto DocumentStart = TokenDef("yaml-docstart", flag::print);
    inline const auto DocumentEnd = TokenDef("yaml-docend", flag::print);
    inline const auto Sequence = TokenDef("yaml-sequence");
    inline const auto Mapping = TokenDef("yaml-mapping");
    inline const auto MappingItem = TokenDef("yaml-mappingitem");
    inline const auto Key = TokenDef("yaml-key");
    inline const auto Value = TokenDef("yaml-value", flag::print);
    inline const auto Int = TokenDef("yaml-int", flag::print);
    inline const auto Hex = TokenDef("yaml-hex", flag::print);
    inline const auto Float = TokenDef("yaml-float", flag::print);
    inline const auto Null = TokenDef("yaml-null");
    inline const auto True = TokenDef("yaml-true");
    inline const auto False = TokenDef("yaml-false");
    inline const auto SingleQuote = TokenDef("yaml-singlequote");
    inline const auto DoubleQuote = TokenDef("yaml-doublequote");
    inline const auto BlockLine = TokenDef("yaml-blockline", flag::print);
    inline const auto EmptyLine = TokenDef("yaml-emptyline");
    inline const auto Literal = TokenDef("yaml-literal");
    inline const auto Folded = TokenDef("yaml-folded");
    inline const auto AbsoluteIndent =
      TokenDef("yaml-absoluteindent", flag::print);
    inline const auto ChompIndicator =
      TokenDef("yaml-chomp-indicator", flag::print);
    inline const auto Lines = TokenDef("yaml-lines");
    inline const auto Plain = TokenDef("yaml-plain");
    inline const auto AnchorValue = TokenDef("yaml-anchorvalue", flag::lookup);
    inline const auto Anchor = TokenDef("yaml-anchor", flag::print);
    inline const auto TagValue = TokenDef("yaml-tagvalue");
    inline const auto TagName = TokenDef("yaml-tagname", flag::print);
    inline const auto Alias = TokenDef("yaml-alias", flag::print);
    inline const auto Empty = TokenDef("yaml-empty");
    inline const auto FlowMapping = TokenDef("yaml-flowmapping");
    inline const auto FlowMappingItem = TokenDef("yaml-flowmappingitem");
    inline const auto FlowSequence = TokenDef("yaml-flowsequence");

    inline const auto wf_tokens = Mapping | Sequence | Value | Int | Float |
      True | False | Hex | Null | SingleQuote | DoubleQuote | Plain |
      AnchorValue | Alias | TagValue | Literal | Folded | Empty | FlowMapping |
      FlowSequence;

    inline const auto wf_flow_tokens =
      wf_tokens - (Literal | Folded | Mapping | Sequence);

    // clang-format off
    inline const auto wf =
      (Top <<= Stream)
      | (Stream <<= Directives * Documents)
      | (Documents <<= Document++)
      | (Document <<= Directives * DocumentStart * (Value >>= wf_tokens) * DocumentEnd)
      | (Directives <<= (TagDirective | VersionDirective | UnknownDirective)++)
      | (TagDirective <<= TagPrefix * TagHandle)[TagPrefix]
      | (Mapping <<= MappingItem++[1])
      | (MappingItem <<= (Key >>= wf_tokens) * (Value >>= wf_tokens))
      | (FlowMapping <<= FlowMappingItem++)
      | (FlowMappingItem <<= (Key >>= wf_flow_tokens) * (Value >>= wf_flow_tokens))
      | (AnchorValue <<= Anchor * (Value >>= wf_tokens))[Anchor]
      | (TagValue <<= TagPrefix * TagName * (Value >>= wf_tokens))
      | (Sequence <<= wf_tokens++[1])
      | (FlowSequence <<= wf_flow_tokens++)
      | (SingleQuote <<= (BlockLine|EmptyLine)++[1])
      | (DoubleQuote <<= (BlockLine|EmptyLine)++[1])
      | (Literal <<= AbsoluteIndent * ChompIndicator * Lines)
      | (Folded <<= AbsoluteIndent * ChompIndicator * Lines)
      | (Lines <<= (BlockLine|EmptyLine)++)
      | (Plain <<= (BlockLine|EmptyLine)++[1])
      ;
    // clang-format on

    Reader reader();
    Writer event_writer(
      const std::filesystem::path& path, const std::string& newline = "\n");
    Writer writer(
      const std::filesystem::path& path,
      const std::string& newline = "\n",
      std::size_t indent = 2,
      bool canonical = false);
    Rewriter to_json();
    std::ostream& block_to_string(
      std::ostream& os, const Node& node, bool raw_quotes = false);
    std::ostream& quote_to_string(
      std::ostream& os, const Node& quote, bool raw_quotes = false);
    std::string to_string(
      Node yaml,
      const std::string& newline = "\n",
      std::size_t indent = 2,
      bool canonical = false);
  }
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "trieste/token.h"
#include "trieste/trieste.h"

namespace trieste
{
  namespace yaml
  {
    using namespace wf::ops;

    inline const auto Stream =
      TokenDef("yaml-stream", flag::symtab | flag::defbeforeuse);
    inline const auto Document =
      TokenDef("yaml-document", flag::symtab | flag::defbeforeuse);
    inline const auto Documents = TokenDef("yaml-documents");
    inline const auto Sequence = TokenDef("yaml-sequence");
    inline const auto SequenceItem = TokenDef("yaml-sequenceitem");
    inline const auto Mapping = TokenDef("yaml-mapping");
    inline const auto MappingItem = TokenDef("yaml-mappingitem");
    inline const auto MappingValue = TokenDef("yaml-mappingvalue");
    inline const auto Value = TokenDef("yaml-value", flag::print);
    inline const auto UnknownDirective =
      TokenDef("yaml-unknowndirective", flag::print);
    inline const auto VersionDirective =
      TokenDef("yaml-versiondirective", flag::print);
    inline const auto TagDirective =
      TokenDef("yaml-tagdirective", flag::lookup | flag::shadowing);
    inline const auto MaybeDirective =
      TokenDef("yaml-maybedirective", flag::print);
    inline const auto Directives = TokenDef("yaml-directives");
    inline const auto TagPrefix = TokenDef("yaml-tagprefix", flag::print);
    inline const auto TagHandle = TokenDef("yaml-taghandle", flag::print);
    inline const auto NonSpecificTag =
      TokenDef("yaml-nonspecifictag", flag::print);
    inline const auto VerbatimTag = TokenDef("yaml-verbatimtag", flag::print);
    inline const auto ShorthandTag = TokenDef("yaml-shorthandtag", flag::print);
    inline const auto Tag = TokenDef("yaml-tag");
    inline const auto TagValue = TokenDef("yaml-tagvalue");
    inline const auto TagName = TokenDef("yaml-tagname", flag::print);
    inline const auto Int = TokenDef("yaml-int", flag::print);
    inline const auto Hex = TokenDef("yaml-hex", flag::print);
    inline const auto Float = TokenDef("yaml-float", flag::print);
    inline const auto String = TokenDef("yaml-string", flag::print);
    inline const auto ErrorSeq = TokenDef("yaml-error-seq");
    inline const auto Key = TokenDef("yaml-key");
    inline const auto KeyItem = TokenDef("yaml-keyitem", flag::print);
    inline const auto Alias = TokenDef("yaml-alias", flag::print);
    inline const auto Anchor = TokenDef("yaml-anchor", flag::print);
    inline const auto AnchorValue = TokenDef("yaml-anchorvalue", flag::lookup);
    inline const auto SingleQuote = TokenDef("yaml-singlequote");
    inline const auto DoubleQuote = TokenDef("yaml-doublequote");
    inline const auto DocumentStart = TokenDef("yaml-docstart", flag::print);
    inline const auto DocumentEnd = TokenDef("yaml-docend", flag::print);
    inline const auto Whitespace = TokenDef("yaml-whitespace", flag::print);
    inline const auto Hyphen = TokenDef("yaml-hyphen");
    inline const auto Colon = TokenDef("yaml-colon");
    inline const auto Comma = TokenDef("yaml-comma");
    inline const auto NewLine = TokenDef("yaml-newline");
    inline const auto Literal = TokenDef("yaml-literal");
    inline const auto Folded = TokenDef("yaml-folded");
    inline const auto Comment = TokenDef("yaml-comment", flag::print);
    inline const auto Plain = TokenDef("yaml-plain");
    inline const auto IndentIndicator =
      TokenDef("yaml-indentation-indicator", flag::print);
    inline const auto ChompIndicator =
      TokenDef("yaml-chomp-indicator", flag::print);
    inline const auto Lines = TokenDef("yaml-lines");
    inline const auto BlockLine = TokenDef("yaml-blockline", flag::print);
    inline const auto Empty = TokenDef("yaml-empty");
    inline const auto Null = TokenDef("yaml-null");
    inline const auto True = TokenDef("yaml-true");
    inline const auto False = TokenDef("yaml-false");
    inline const auto Flow = TokenDef("yaml-flow");
    inline const auto FlowMapping = TokenDef("yaml-flowmapping");
    inline const auto FlowMappingItem = TokenDef("yaml-flowmappingitem");
    inline const auto FlowMappingItems = TokenDef("yaml-flowmappingitems");
    inline const auto FlowSequence = TokenDef("yaml-flowsequence");
    inline const auto FlowSequenceItem = TokenDef("yaml-flowsequenceitem");
    inline const auto FlowSequenceItems = TokenDef("yaml-flowsequenceitems");
    inline const auto FlowItem = TokenDef("yaml-flowitem", flag::print);
    inline const auto FlowEmpty = TokenDef("yaml-flowempty", flag::print);
    inline const auto FlowKeyEnd = TokenDef("yaml-flowkeyend");
    inline const auto FlowKeyValue = TokenDef("yaml-flowkeyvalue", flag::print);
    inline const auto FlowSequenceStart =
      TokenDef("yaml-flowseqstart", flag::print);
    inline const auto FlowSequenceEnd =
      TokenDef("yaml-flowseqend", flag::print);
    inline const auto FlowMappingStart =
      TokenDef("yaml-flowmapstart", flag::print);
    inline const auto FlowMappingEnd = TokenDef("yaml-flowmapend", flag::print);
    inline const auto Lhs = TokenDef("yaml-lhs");
    inline const auto Rhs = TokenDef("yaml-rhs");
    inline const auto AbsoluteIndent =
      TokenDef("yaml-absoluteindent", flag::print);
    inline const auto Indent = TokenDef("yaml-indent");
    inline const auto Head = TokenDef("yaml-head");
    inline const auto Tail = TokenDef("yaml-tail");
    inline const auto EmptyLine = TokenDef("yaml-emptyline");
    inline const auto WhitespaceLine =
      TokenDef("yaml-whitespace-line", flag::print);
    inline const auto Block = TokenDef("yaml-block");
    inline const auto Line = TokenDef("yaml-line");
    inline const auto Extra = TokenDef("yaml-extra");
    inline const auto ComplexKey = TokenDef("yaml-complexkey");
    inline const auto ComplexValue = TokenDef("yaml-complexvalue");
    inline const auto BlockIndent = TokenDef("yaml-blockindent");
    inline const auto ManualIndent = TokenDef("yaml-manualindent");
    inline const auto SequenceIndent = TokenDef("yaml-sequenceindent");
    inline const auto MappingIndent = TokenDef("yaml-mappingindent");
    inline const auto Placeholder = TokenDef("yaml-placeholder");
    inline const auto BlockStart = TokenDef("yaml-blockstart");

    // groups
    inline const auto StreamGroup = TokenDef("yaml-streamgroup");
    inline const auto DocumentGroup = TokenDef("yaml-documentgroup");
    inline const auto TagGroup = TokenDef("yaml-taggroup");
    inline const auto FlowGroup = TokenDef("yaml-flowgroup");
    inline const auto TagDirectiveGroup = TokenDef("yaml-tagdirectivegroup");
    inline const auto KeyGroup = TokenDef("yaml-keygroup");
    inline const auto ValueGroup = TokenDef("yaml-valuegroup");
    inline const auto BlockGroup = TokenDef("yaml-blockgroup");
    inline const auto SequenceGroup = TokenDef("yaml-sequencegroup");
    inline const auto MappingGroup = TokenDef("yaml-mappinggroup");

    inline const auto wf_tokens = Mapping | Sequence | Value | Int | Float |
      True | False | Hex | Null | SingleQuote | DoubleQuote | Plain |
      AnchorValue | Alias | TagValue | Literal | Folded | Empty | FlowMapping |
      FlowSequence;

    inline const auto wf_flow_tokens = wf_tokens - (Mapping | Sequence);

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

    Parse parser();
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

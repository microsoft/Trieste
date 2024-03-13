#pragma once

// Trieste-based rewriter for YAML

#include "trieste/token.h"

#include <trieste/trieste.h>

namespace trieste::yaml
{
  using namespace wf::ops;

  inline const auto Stream = TokenDef("yaml-stream", flag::symtab);
  inline const auto Document = TokenDef("yaml-document", flag::symtab);
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
  inline const auto FlowSequenceEnd = TokenDef("yaml-flowseqend", flag::print);
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

  // clang-format off
  inline const auto wf_groups =
    (Top <<= Stream)
    | (Stream <<= StreamGroup)
    | (Document <<= DocumentGroup)
    | (Tag <<= TagGroup)
    | (FlowMapping <<= FlowGroup)
    | (FlowSequence <<= FlowGroup)
    | (TagDirective <<= TagDirectiveGroup)
    | (StreamGroup <<= wf_parse_tokens++)
    | (DocumentGroup <<= wf_parse_tokens++)
    | (TagGroup <<= wf_parse_tokens++)
    | (FlowGroup <<= wf_parse_tokens++)
    | (TagDirectiveGroup <<= wf_parse_tokens++)
    ;

  inline const auto wf_values_tokens = (wf_parse_tokens | Placeholder) -
    (Stream | Document | TagHandle | TagPrefix | ShorthandTag | VerbatimTag |
     TagDirective | VersionDirective | UnknownDirective);

  // clang-format off
  inline const auto wf_values =
    wf_groups
    | (Stream <<= Directives * Documents)
    | (Directives <<= (TagDirective | VersionDirective | UnknownDirective)++)
    | (TagDirective <<= TagPrefix * TagHandle)
    | (Tag <<= TagPrefix * (TagName >>= ShorthandTag | VerbatimTag | NonSpecificTag))
    | (Documents <<= Document++)
    | (Document <<= Directives * DocumentGroup)
    | (DocumentGroup <<= wf_values_tokens++)
    ;
  // clang-format on

  inline const auto wf_flow_tokens = (wf_values_tokens | Placeholder) -
    (Comma | FlowMappingStart | FlowMappingEnd | FlowSequenceStart |
     FlowSequenceEnd);

  inline const auto wf_flowgroup_tokens = (wf_flow_tokens | Plain | Empty) -
    (Hyphen | Colon | Literal | Folded | IndentIndicator | ChompIndicator |
     NewLine | Placeholder | Whitespace | MaybeDirective | DocumentStart |
     DocumentEnd | Comment | Key);

  // clang-format off
  inline const auto wf_flow =
    wf_values
    | (FlowMapping <<= FlowMappingStart * FlowMappingItems * FlowMappingEnd)
    | (FlowMappingItems <<= FlowMappingItem++)
    | (FlowMappingItem <<= FlowGroup * FlowGroup)
    | (FlowSequence <<= FlowSequenceStart * FlowSequenceItems * FlowSequenceEnd)
    | (FlowSequenceItems <<= FlowSequenceItem++)
    | (FlowSequenceItem <<= FlowGroup)
    | (FlowGroup <<= wf_flowgroup_tokens++)
    | (Plain <<= BlockLine++[1])
    | (DocumentGroup <<= wf_flow_tokens++)
    ;
  // clang-format on

  inline const auto wf_lines_tokens =
    (wf_flow_tokens | BlockStart) - (NewLine | DocumentStart | DocumentEnd);

  inline const auto wf_doc_tokens = DocumentStart | DocumentEnd | Indent |
    MappingIndent | SequenceIndent | ManualIndent | BlockStart | EmptyLine |
    WhitespaceLine | BlockIndent;

  inline const auto wf_block_tokens =
    (wf_lines_tokens | Literal | Folded | IndentIndicator | ChompIndicator) -
    (Hyphen);

  inline const auto wf_lines_indent_tokens = Line | WhitespaceLine |
    BlockStart | EmptyLine | SequenceIndent | MappingIndent;

  // clang-format off
  inline const auto wf_lines =
    wf_flow
    | (DocumentGroup <<= wf_doc_tokens++)
    | (Indent <<= wf_lines_indent_tokens++[1])
    | (MappingIndent <<= wf_lines_indent_tokens++[1])
    | (SequenceIndent <<= wf_lines_indent_tokens++[1])
    | (ManualIndent <<= AbsoluteIndent)
    | (BlockIndent <<= wf_lines_indent_tokens++)
    | (Line <<= wf_lines_tokens++)
    | (BlockStart <<= wf_block_tokens++[1])
    ;
  // clang-format on

  inline const auto indents_tokens = Line | Indent | MappingIndent |
    SequenceIndent | EmptyLine | WhitespaceLine | BlockStart | BlockIndent |
    ManualIndent;

  // clang-format off
  inline const auto wf_indents =
    wf_lines
    | (SequenceIndent <<= indents_tokens++[1])
    | (MappingIndent <<= indents_tokens++[1])
    | (BlockIndent <<= indents_tokens++)
    | (Indent <<= indents_tokens++[1])
    | (ManualIndent <<= (AbsoluteIndent | indents_tokens)++[1])
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_colgroups =
    wf_indents
    | (SequenceIndent <<= SequenceGroup)
    | (MappingIndent <<= MappingGroup)
    | (SequenceGroup <<= indents_tokens++[1])
    | (MappingGroup <<= indents_tokens++[1])
    ;
  // clang-format off

  inline const auto wf_items_tokens =
    (wf_lines_tokens | Line | Indent | MappingIndent | SequenceIndent |
     ManualIndent | Empty | EmptyLine | WhitespaceLine | BlockIndent |
     DocumentStart | DocumentEnd) -
    Placeholder;

  inline const auto wf_items_value_tokens =
    wf_items_tokens - (DocumentStart | DocumentEnd);

  // clang-format off
  inline const auto wf_items =
    wf_indents
    | (MappingIndent <<= (MappingItem|ComplexKey|ComplexValue)++[1])
    | (ComplexKey <<= wf_items_value_tokens++)
    | (ComplexValue <<= wf_items_value_tokens++)
    | (SequenceIndent <<= SequenceItem++[1])
    | (MappingItem <<= KeyGroup * ValueGroup)
    | (SequenceItem <<= ValueGroup)
    | (DocumentGroup <<= wf_items_tokens++)
    | (KeyGroup <<= wf_items_value_tokens++)
    | (ValueGroup <<= wf_items_value_tokens++)
    ;
  // clang-format on

  inline const auto wf_complex_tokens = wf_items_tokens - (Key | Colon);
  inline const auto wf_complex_value_tokens =
    wf_items_value_tokens - (Key | Colon);

  // clang-format off
  inline const auto wf_complex =
    wf_items
    | (MappingIndent <<= MappingItem++[1])
    | (KeyGroup <<= wf_complex_value_tokens++)
    | (ValueGroup <<= wf_complex_value_tokens++)
    | (DocumentGroup <<= wf_complex_tokens++)
    ;
  // clang-format on

  inline const auto wf_blocks_tokens =
    (wf_complex_tokens | Plain | Literal | Folded) -
    (Indent | BlockIndent | ManualIndent | ChompIndicator | IndentIndicator |
     Hyphen | Line | MaybeDirective | BlockStart | Placeholder | EmptyLine);

  inline const auto wf_blocks_value_tokens =
    wf_blocks_tokens - (DocumentStart | DocumentEnd);

  // clang-format off
  inline const auto wf_blocks =
    wf_complex
    | (Plain <<= (BlockLine | EmptyLine)++[1])
    | (Literal <<= BlockGroup)
    | (Folded <<= BlockGroup)
    | (DocumentGroup <<= wf_blocks_tokens++)
    | (KeyGroup <<= wf_blocks_value_tokens++)
    | (ValueGroup <<= wf_blocks_value_tokens++)
    | (BlockGroup <<= (ChompIndicator | IndentIndicator | BlockLine)++)
    ;
  // clang-format on

  inline const auto wf_collections_tokens =
    (wf_blocks_tokens | Mapping | Sequence) -
    (MappingIndent | SequenceIndent | Whitespace | Comment | WhitespaceLine |
     Placeholder);

  inline const auto wf_collections_value_tokens =
    wf_collections_tokens - (DocumentStart | DocumentEnd);

  // clang-format off
  inline const auto wf_collections =
    wf_blocks
    | (Mapping <<= MappingItem++[1])
    | (Sequence <<= SequenceItem++[1])
    | (FlowMapping <<= FlowMappingItem++)
    | (FlowSequence <<= FlowSequenceItem++)
    | (DocumentGroup <<= wf_collections_tokens++)
    | (KeyGroup <<= wf_collections_value_tokens++)
    | (ValueGroup <<= wf_collections_value_tokens++)
    ;
  // clang-format on

  inline const auto wf_attributes_tokens =
    (wf_collections_tokens | AnchorValue | TagValue);

  inline const auto wf_attributes_value_tokens =
    wf_attributes_tokens - (DocumentStart | DocumentEnd);

  inline const auto wf_attributes_flow_tokens =
    wf_flowgroup_tokens | AnchorValue | TagValue;

  // clang-format off
  inline const auto wf_attributes =
    wf_collections
    | (AnchorValue <<= Anchor * (Value >>= wf_attributes_tokens))
    | (TagValue <<= TagPrefix * TagName * (Value >>= wf_attributes_tokens))
    | (DocumentGroup <<= wf_attributes_tokens++)
    | (FlowGroup <<= wf_attributes_flow_tokens++)
    | (KeyGroup <<= wf_attributes_value_tokens++)
    | (ValueGroup <<= wf_attributes_value_tokens++)
    ;
  // clang-format on

  inline const auto wf_structure_tokens = Mapping | Sequence | Value | Int |
    Float | True | False | Hex | Null | SingleQuote | DoubleQuote | Plain |
    AnchorValue | Alias | TagValue | Literal | Folded | Empty | FlowMapping |
    FlowSequence;

  inline const auto wf_structure_flow_tokens =
    wf_structure_tokens - (Mapping | Sequence);

  // clang-format off
  inline const auto wf_structure = 
    wf_attributes
    | (Document <<= Directives * DocumentStart * (Value >>= wf_structure_tokens) * DocumentEnd)
    | (SequenceItem <<= wf_structure_tokens)
    | (FlowSequenceItem <<= wf_structure_flow_tokens)
    | (FlowMappingItem <<= (Key >>= wf_structure_flow_tokens) * (Value >>= wf_structure_flow_tokens))
    | (MappingItem <<= (Key >>= wf_structure_tokens) * (Value >>= wf_structure_tokens))
    | (TagDirective <<= TagPrefix * TagHandle)[TagPrefix]
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_tags =
    wf_structure
    | (Sequence <<= wf_structure_tokens++[1])
    | (FlowSequence <<= wf_structure_flow_tokens++)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_quotes =
    wf_tags
    | (SingleQuote <<= (BlockLine|EmptyLine)++[1])
    | (DoubleQuote <<= (BlockLine|EmptyLine)++[1])
    | (Literal <<= AbsoluteIndent * ChompIndicator * Lines)
    | (Folded <<= AbsoluteIndent * ChompIndicator * Lines)
    | (Lines <<= (BlockLine|EmptyLine)++)
    ;
  // clang-format on

  inline const auto wf_anchors = wf_quotes;

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

  class YAMLEmitter
  {
  public:
    YAMLEmitter(const std::string& indent = "  ", const std::string& newline = "\n");

    void emit(std::ostream& os, const Node& value) const;
    void emit_events(std::ostream& os, const Node& value) const;

  private:
    bool emit_event(std::ostream& os, const Node& node) const;
    bool emit_value_event(std::ostream& os, const Node& node) const;
    bool
    emit_mapping_event(std::ostream& os, const Node& node, bool is_flow) const;
    bool
    emit_sequence_event(std::ostream& os, const Node& node, bool is_flow) const;
    bool emit_alias_event(std::ostream& os, const Node& node) const;
    bool emit_literal_event(std::ostream& os, const Node& node) const;
    bool emit_folded_event(std::ostream& os, const Node& node) const;
    bool emit_plain_event(std::ostream& os, const Node& node) const;
    bool emit_doublequote_event(std::ostream& os, const Node& node) const;
    bool emit_singlequote_event(std::ostream& os, const Node& node) const;

    Token get_type(const Node& node) const;
    Node handle_tag_anchor(std::ostream& os, const Node& node) const;
    void escape_char(std::ostream& os, char c) const;
    std::string escape_chars(
      const std::string_view& str, const std::set<char>& to_escape) const;
    std::string unescape_url_chars(const std::string_view& str) const;
    std::string block_to_string(const Node& node, bool raw_quotes) const;
    std::string replace_all(
      const std::string_view& v,
      const std::string_view& find,
      const std::string_view& replace) const;
    Node lookup_nearest(Node ref) const;
    void write_quote(std::ostream& os, const Node& node, bool is_single) const;

    std::string m_indent;
    std::string m_newline;
  };

  class YAMLReader
  {
  public:
    YAMLReader(const std::filesystem::path& path);
    YAMLReader(const std::string& yaml);
    YAMLReader(const Source& source);

    void read();

    const Node& stream() const;
    bool has_errors() const;
    std::string error_message() const;

    YAMLReader& debug_enabled(bool value);
    bool debug_enabled() const;

    YAMLReader& debug_path(const std::filesystem::path& path);
    const std::filesystem::path& debug_path() const;

    YAMLReader& well_formed_checks_enabled(bool value);
    bool well_formed_checks_enabled() const;

  private:
    Source m_source;
    Node m_stream;
    bool m_debug_enabled;
    std::filesystem::path m_debug_path;
    bool m_well_formed_checks_enabled;
  };
}

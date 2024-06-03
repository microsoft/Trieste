#include "internal.h"
#include "trieste/pass.h"
#include "trieste/rewrite.h"
#include "trieste/source.h"
#include "trieste/token.h"

#include <iterator>
#include <optional>

namespace
{
  using namespace trieste;
  using namespace trieste::yaml;

  const auto SequenceItem = TokenDef("yaml-sequenceitem");
  const auto Indent = TokenDef("yaml-indent");
  const auto SequenceIndent = TokenDef("yaml-sequenceindent");
  const auto MappingIndent = TokenDef("yaml-mappingindent");
  const auto ManualIndent = TokenDef("yaml-manualindent");
  const auto Line = TokenDef("yaml-line");
  const auto NonSpecificTag = TokenDef("yaml-nonspecifictag", flag::print);
  const auto Placeholder = TokenDef("yaml-placeholder");
  const auto FlowMappingItems = TokenDef("yaml-flowmappingitems");
  const auto FlowSequenceItems = TokenDef("yaml-flowsequenceitems");
  const auto FlowSequenceItem = TokenDef("yaml-flowsequenceitem");
  const auto BlockIndent = TokenDef("yaml-blockindent");
  const auto BlockStart = TokenDef("yaml-blockstart");
  const auto ComplexKey = TokenDef("yaml-complexkey");
  const auto ComplexValue = TokenDef("yaml-complexvalue");
  const auto FlowKeyValue = TokenDef("yaml-flowkeyvalue", flag::print);
  const auto Flow = TokenDef("yaml-flow");
  const auto Extra = TokenDef("yaml-extra");
  const auto FlowEmpty = TokenDef("yaml-flowempty", flag::print);

  // groups
  const auto StreamGroup = TokenDef("yaml-streamgroup");
  const auto DocumentGroup = TokenDef("yaml-documentgroup");
  const auto TagGroup = TokenDef("yaml-taggroup");
  const auto FlowGroup = TokenDef("yaml-flowgroup");
  const auto TagDirectiveGroup = TokenDef("yaml-tagdirectivegroup");
  const auto SequenceGroup = TokenDef("yaml-sequencegroup");
  const auto MappingGroup = TokenDef("yaml-mappinggroup");
  const auto KeyGroup = TokenDef("yaml-keygroup");
  const auto ValueGroup = TokenDef("yaml-valuegroup");
  const auto BlockGroup = TokenDef("yaml-blockgroup");

  // utility tokens
  const auto Lhs = TokenDef("yaml-lhs");
  const auto Rhs = TokenDef("yaml-rhs");
  const auto Head = TokenDef("yaml-head");
  const auto Tail = TokenDef("yaml-tail");

  bool is_space(char c)
  {
    return c == ' ' || c == '\t';
  }

  struct ValuePattern
  {
    ValuePattern(const std::string& pattern, Token t) : regex(pattern), type(t)
    {}

    RE2 regex;
    Token type;
  };

  std::size_t min_indent(Node node)
  {
    if (node->empty())
    {
      if (node->type() == Whitespace)
      {
        return std::string::npos;
      }
      Location loc = node->location();
      if (loc.pos > 0 || loc.len < loc.source->view().size())
      {
        return loc.linecol().second;
      }
      else
      {
        return std::string::npos;
      }
    }
    else
    {
      return std::transform_reduce(
        node->begin(),
        node->end(),
        std::string::npos,
        [](std::size_t lhs, std::size_t rhs) { return lhs < rhs ? lhs : rhs; },
        [](auto child) { return min_indent(child); });
    }
  }

  std::size_t sequence_indent(Node node)
  {
    if (node->in({Hyphen, SequenceItem}))
    {
      Location loc = node->location();
      return loc.linecol().second;
    }

    return std::transform_reduce(
      node->begin(),
      node->end(),
      std::string::npos,
      [](std::size_t lhs, std::size_t rhs) { return lhs < rhs ? lhs : rhs; },
      [](auto child) { return sequence_indent(child); });
  }

  std::size_t get_line(Node node)
  {
    Location loc = node->location();
    if (node->empty())
    {
      if (loc.pos > 0 || loc.len < loc.source->view().size())
      {
        return loc.linecol().first;
      }
      else
      {
        return std::string::npos;
      }
    }

    return get_line(node->front());
  }

  bool same_line(Node lhs, Node rhs)
  {
    return get_line(lhs) == get_line(rhs);
  }

  Node fake_whitespace(Node node)
  {
    Location loc = node->location();
    std::size_t col = loc.linecol().second;
    Location ws = loc;
    ws.pos -= col;
    ws.len = col;
    return Whitespace ^ ws;
  }

  std::optional<std::size_t> measure_indent(Node node)
  {
    if (node == SequenceIndent)
    {
      std::size_t indent = sequence_indent(node);
      if (indent == std::string::npos)
      {
        return std::nullopt;
      }
      return indent;
    }

    if (node == ManualIndent)
    {
      std::string manual(node->front()->location().view());
      return std::stoul(manual);
    }

    if (node == WhitespaceLine)
    {
      auto view = node->location().view();
      return view.find_first_not_of(' ');
    }

    if (node->empty())
    {
      return std::nullopt;
    }

    if (node->type() != Line)
    {
      return measure_indent(node->front());
    }

    Node maybe_ws = node->front();
    if (maybe_ws->type() == Whitespace)
    {
      return maybe_ws->location().len;
    }

    Location loc = maybe_ws->location();
    if (loc.pos > 0)
    {
      return loc.linecol().second;
    }

    return 0;
  }

  bool same_indent(Node lhs, Node rhs)
  {
    auto lhs_indent = measure_indent(lhs);
    auto rhs_indent = measure_indent(rhs);
    if (lhs_indent.has_value() && rhs_indent.has_value())
    {
      return lhs_indent.value() == rhs_indent.value();
    }
    return false;
  }

  bool all_empty(Node node)
  {
    if (node == EmptyLine)
    {
      return true;
    }

    if (node->empty())
    {
      return false;
    }

    for (const Node& child : *node)
    {
      if (!all_empty(child))
      {
        return false;
      }
    }

    return true;
  }

  bool less_indented(Node lhs, Node rhs)
  {
    if (all_empty(rhs))
    {
      return true;
    }

    auto lhs_indent = measure_indent(lhs);
    auto rhs_indent = measure_indent(rhs);
    if (lhs_indent.has_value() && rhs_indent.has_value())
    {
      return lhs_indent.value() < rhs_indent.value();
    }
    return false;
  }

  std::size_t detect_indent(const NodeRange& lines)
  {
    std::size_t max_empty_size = 0;
    std::size_t indent = std::string::npos;
    for (const Node& n : lines)
    {
      auto view = n->location().view();
      std::size_t pos = view.find_first_not_of(" \n");
      if (pos == std::string::npos)
      {
        max_empty_size = view.size();
      }
      else
      {
        indent = pos;
        break;
      }
    }

    if (indent == std::string::npos)
    {
      indent = max_empty_size;
    }

    if (max_empty_size > indent)
    {
      return std::string::npos;
    }

    return indent;
  }

  std::size_t indent_of(NodeDef* node)
  {
    if (node->type() == Document)
    {
      return 0;
    }

    if (node->type().in({MappingItem, SequenceItem}))
    {
      Node front = node->front();
      return front->location().linecol().second;
    }

    return indent_of(node->parent());
  }

  Location trim_start(const Location& loc, std::size_t min_indent)
  {
    Location trim = loc;
    auto view = loc.view();
    // only spaces can be used for indentation
    std::size_t indent = view.find_first_not_of(" ");
    std::size_t start = indent;
    if (start == std::string::npos)
    {
      trim.len = 0;
      return trim;
    }

    if (indent >= min_indent)
    {
      start = view.find_first_not_of(" \t", indent);
    }

    trim.pos += start;
    trim.len -= start;
    return trim;
  }

  Location trim_end(const Location& loc)
  {
    Location trim = loc;
    auto view = loc.view();
    std::size_t end = view.find_last_not_of(" \t\r\n");
    if (end == std::string::npos)
    {
      trim.len = 0;
      return trim;
    }

    if (end == view.size() - 1)
    {
      return trim;
    }

    if (view[end] == '\\' && view[end + 1] == '\t')
    {
      end += 1;
    }

    trim.len = end + 1;
    return trim;
  }

  Location trim(const Location& loc, std::size_t min_indent)
  {
    return trim_end(trim_start(loc, min_indent));
  }

  Nodes to_lines(const Location& loc, std::size_t min_indent)
  {
    Nodes result;
    if (loc.len == 0)
    {
      return result;
    }

    auto src = loc.source;
    auto view = loc.view();
    std::size_t start = 1;
    std::size_t newline = view.find("\n", start);
    if (newline == std::string::npos)
    {
      return {BlockLine ^ Location(src, loc.pos + 1, loc.len - 2)};
    }

    std::size_t end = newline;
    std::vector<Location> lines;
    lines.push_back(Location(src, loc.pos + start, end - start));

    start = newline + 1;
    while (start < view.size())
    {
      newline = view.find("\n", start);
      if (newline == std::string::npos)
      {
        end = loc.len - 1;
        lines.push_back(Location(src, loc.pos + start, end - start));
        break;
      }

      end = newline;
      lines.push_back(Location(src, loc.pos + start, end - start));
      start = newline + 1;
    }

    result.push_back(BlockLine ^ trim_end(lines.front()));
    for (std::size_t i = 1; i < lines.size() - 1; i++)
    {
      Location line = trim(lines[i], min_indent);
      if (line.len == 0)
      {
        if (line.linecol().second == 0)
        {
          result.push_back(EmptyLine ^ line);
        }
        else
        {
          result.push_back(BlockLine ^ line);
        }
      }
      else
      {
        result.push_back(BlockLine ^ line);
      }
    }

    Location end_loc = trim_start(lines.back(), min_indent);
    result.push_back(BlockLine ^ end_loc);

    if (result.size() > 1)
    {
      if (
        result[0] == BlockLine && result[0]->location().len == 0 &&
        result[1] == EmptyLine)
      {
        result.erase(result.begin(), result.begin() + 1);
      }
    }

    for (std::size_t i = 0; i < result.size(); ++i)
    {
      Location line = result[i]->location();
      if (line.len == 0)
      {
        continue;
      }

      std::size_t col = line.linecol().second;
      if (col < min_indent)
      {
        result[i] = err(result[i], "Wrong indentation");
        continue;
      }

      auto s = line.view();
      if (s.find("... ") != std::string::npos)
      {
        result[i] = err(result[i], "Scalar contains '...'");
      }

      if (s.size() >= 3 && s.rfind("...") == s.size() - 3)
      {
        result[i] = err(result[i], "Scalar contains '...'");
      }
    }

    return result;
  }

  std::string contains_invalid_elements(const Nodes& lines)
  {
    for (const Node& line : lines)
    {
      if (line->location().len == 0)
      {
        continue;
      }

      auto view = line->location().view();
      if (view.find("---") != std::string::npos)
      {
        return "Invalid element: ---";
      }

      for (std::size_t i = 0; i < view.size() - 1; ++i)
      {
        if (view[i] == '\\')
        {
          switch (view[i + 1])
          {
            case '\\':
              i += 1;
              break;

            case '"':
            case 'a':
            case '\a':
            case 'b':
            case '\b':
            case 'f':
            case '\f':
            case 'n':
            case '\n':
            case 'r':
            case '\r':
            case 't':
            case '\t':
            case 'v':
            case '\v':
            case '/':
            case 'x':
            case 'u':
            case 'U':
            case ' ':
              break;

            default:
              return "Invalid escape sequence";
          }
        }
      }
    }

    return "";
  }

  std::pair<Node, Node> handle_indent_chomp(NodeRange nodes)
  {
    if (nodes.empty())
    {
      return {nullptr, nullptr};
    }

    Node indent = nodes.front();
    Node chomp = nullptr;
    if (nodes.size() > 1)
    {
      chomp = nodes[1];
    }

    if (indent != IndentIndicator)
    {
      std::swap(indent, chomp);
    }

    return {indent, chomp};
  }

  bool all_comments(Node node)
  {
    if (node->type().in({Whitespace, EmptyLine, WhitespaceLine, Comment}))
    {
      return true;
    }

    if (node->empty())
    {
      return false;
    }

    for (const Node& child : *node)
    {
      if (!all_comments(child))
      {
        return false;
      }
    }

    return true;
  }

  Node cleanup_block(NodeRange range, std::size_t indent, Node chomp_indicator)
  {
    if (indent == std::string::npos)
    {
      return err(range, "Empty line has too many spaces");
    }

    auto end = range.end();
    for (auto it = range.begin(); it != range.end(); ++it)
    {
      Node n = *it;
      auto view = n->location().view();
      if (view.empty())
      {
        continue;
      }

      auto pos = view.find_first_not_of(" \t\r");
      if (pos == std::string::npos)
      {
        continue;
      }

      if (view[pos] != '#')
      {
        if (view.size() >= indent)
        {
          end = range.end();
          continue;
        }

        return err(range, "Invalid block scalar");
      }
      else if (pos < indent)
      {
        end = it;
      }
    }

    Nodes lines(range.begin(), end);

    return Seq << (AbsoluteIndent ^ std::to_string(indent)) << chomp_indicator
               << (Lines << lines);
  }

  std::size_t flatten_groups(Node n)
  {
    if (n->empty())
    {
      n->push_back(Group);
      return 0;
    }

    Node flat = NodeDef::create(Group);
    for (Node& group : *n)
    {
      flat->insert(flat->end(), group->begin(), group->end());
    }

    n->erase(n->begin(), n->end());
    n->push_back(flat);
    return 0;
  }

  Token find_nearest(NodeDef* node, const std::set<Token>& tokens)
  {
    if (tokens.contains(node->type()))
    {
      return node->type();
    }

    if (node == Top)
    {
      return Top;
    }

    return find_nearest(node->parent(), tokens);
  }

  std::size_t
  invalid_tokens(Node n, const std::map<Token, std::string>& token_messages)
  {
    std::size_t changes = 0;
    for (Node& child : *n)
    {
      if (token_messages.count(child->type()) > 0)
      {
        n->replace(child, err(child, token_messages.at(child->type())));
        changes += 1;
      }
      else
      {
        changes += invalid_tokens(child, token_messages);
      }
    }

    return changes;
  }

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

  inline const auto wf_flow_tokens = (wf_values_tokens | Placeholder | Empty) -
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
    WhitespaceLine | BlockIndent | Empty;

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
    | (AnchorValue <<= Anchor * (Value >>= wf_attributes_value_tokens))
    | (TagValue <<= TagPrefix * TagName * (Value >>= wf_attributes_value_tokens))
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
    | (AnchorValue <<= Anchor * (Value >>= wf_structure_tokens))
    | (TagValue <<= TagPrefix * TagName * (Value >>= wf_structure_tokens))
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

  // clang-format off
  inline const auto wf_anchors =
    wf_quotes
    | (AnchorValue <<= Anchor * (Value >>= wf_structure_tokens))[Anchor]
    ;
  // clang-format on

  const auto FlowToken =
    T(Whitespace,
      Value,
      Float,
      Int,
      Hex,
      True,
      False,
      Null,
      Hyphen,
      DoubleQuote,
      SingleQuote,
      Anchor,
      Tag,
      Alias,
      Literal,
      Folded,
      IndentIndicator,
      ChompIndicator,
      FlowMapping,
      FlowSequence,
      Empty);

  const auto LineToken =
    FlowToken / T(Comment, Colon, Key, Placeholder, MaybeDirective);
  const auto AnchorTag = T(Anchor, Tag);
  const auto inline IndentToken =
    T(Indent, BlockIndent, SequenceIndent, MappingIndent, ManualIndent);
  const auto inline IndentChomp = T(IndentIndicator, ChompIndicator);
  const auto inline BasicToken = T(Value, Int, Float, Hex, True, False, Null);
  const auto DirectiveToken =
    T(VersionDirective, TagDirective, UnknownDirective);
  const auto ValueToken =
    T(Mapping,
      Sequence,
      Value,
      Int,
      Float,
      Alias,
      Literal,
      Folded,
      Plain,
      Empty,
      DoubleQuote,
      SingleQuote,
      FlowMapping,
      FlowSequence,
      Null,
      True,
      False,
      Plain,
      Hex,
      TagValue,
      AnchorValue,
      MaybeDirective);

  PassDef groups()
  {
    PassDef groups = {
      "groups",
      wf_groups,
      dir::bottomup | dir::once,
      {
        In(Top) * (T(File) << (T(Group) << (T(Stream)[Stream] * End))) >>
          [](Match& _) { return _(Stream); },

        In(Stream) * (T(Group) * T(Group)[Group]) >>
          [](Match& _) { return err(_(Group), "Syntax error"); },

        In(Stream) * (Start * T(Group)[Group] * End) >>
          [](Match& _) { return StreamGroup << *_[Group]; },

        In(Document) * T(Group)[Group] >>
          [](Match& _) { return DocumentGroup << *_[Group]; },

        In(FlowMapping, FlowSequence) * T(Group)[Group] >>
          [](Match& _) { return FlowGroup << *_[Group]; },

        In(TagDirective) * T(Group)[Group] >>
          [](Match& _) { return TagDirectiveGroup << *_[Group]; },

        In(Tag) * T(Group)[Group] >>
          [](Match& _) { return TagGroup << *_[Group]; },

        // errors
        In(StreamGroup) * T(Stream)[Stream] >>
          [](Match& _) { return err(_(Stream), "Syntax error"); },
      }};

    groups.pre({FlowMapping, FlowSequence}, flatten_groups);
    groups.post(Stream, [](Node n) {
      if (n->empty())
      {
        n->push_back(StreamGroup);
      }
      return 0;
    });
    groups.post([](Node n) {
      return invalid_tokens(
        n, {{Group, "Syntax error"}, {File, "Syntax error"}});
    });
    return groups;
  }

  PassDef values()
  {
    std::vector<std::shared_ptr<ValuePattern>> patterns;
    patterns.push_back(std::make_shared<ValuePattern>(
      R"(\-?[[:digit:]]+\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?)", Float));
    patterns.push_back(
      std::make_shared<ValuePattern>(R"(\-?[[:digit:]]+)", Int));
    patterns.push_back(
      std::make_shared<ValuePattern>(R"(0x[[:xdigit:]]+)", Hex));
    patterns.push_back(std::make_shared<ValuePattern>(R"(true)", True));
    patterns.push_back(std::make_shared<ValuePattern>(R"(false)", False));
    patterns.push_back(std::make_shared<ValuePattern>(R"(null)", Null));

    auto pass = PassDef{
      "values",
      wf_values,
      dir::bottomup | dir::once,
      {
        In(FlowGroup) *
            (FlowToken[Lhs] * T(Comment)++ * T(Value, R"(:.*)")[Rhs]) >>
          [patterns](Match& _) {
            Location loc = _(Rhs)->location();
            Location colon = loc;
            colon.len = 1;
            loc.pos += 1;
            loc.len -= 1;

            auto view = loc.view();
            Node value = Value ^ loc;
            for (auto& pattern : patterns)
            {
              if (RE2::FullMatch(view, pattern->regex))
              {
                value = (pattern->type ^ loc);
                break;
              }
            }

            return Seq << _(Lhs) << (Colon ^ colon) << value;
          },

        In(DocumentGroup) *
            (Start * ~T(Whitespace) * T(Comment) * T(NewLine)) >>
          [](Match&) -> Node { return nullptr; },

        In(DocumentGroup) * T(DocumentStart)[DocumentStart] * ~T(NewLine) *
            ~T(Whitespace) * T(Comment) * T(NewLine) >>
          [](Match& _) { return _(DocumentStart); },

        In(StreamGroup) *
            (DirectiveToken[Head] * DirectiveToken++[Tail] *
             (T(Document)
              << (T(Directives)[Directives] * T(DocumentGroup)[Group]))) >>
          [](Match& _) {
            Node dirs = _(Directives);
            dirs << _(Head) << _[Tail];
            bool version = false;
            for (Node& dir : *dirs)
            {
              if (dir->type() == VersionDirective)
              {
                if (version)
                {
                  dirs->replace(dir, err(dir, "Duplicate YAML directive"));
                }
                else
                {
                  version = true;
                }
              }
            }
            return Document << dirs << _(Group);
          },

        In(DocumentGroup, FlowGroup) * T(Value)[Value] >>
          [patterns](Match& _) {
            auto view = _(Value)->location().view();
            for (auto& pattern : patterns)
            {
              if (RE2::FullMatch(view, pattern->regex))
              {
                return pattern->type ^ _(Value);
              }
            }

            return _(Value);
          },

        In(DocumentGroup) *
            (T(DocumentStart)[DocumentStart] *
             T(Literal, Folded, Anchor, Tag)[Value]) >>
          [](Match& _) {
            return Seq << _(DocumentStart) << (Placeholder ^ _(DocumentStart))
                       << _(Value);
          },

        In(TagGroup) * T(VerbatimTag, R"(.*[{}].*)")[VerbatimTag] >>
          [](Match& _) { return err(_(VerbatimTag), "Invalid tag"); },

        In(TagGroup) * T(ShorthandTag, R"(.*[{}\[\],].*)")[ShorthandTag] >>
          [](Match& _) { return err(_(ShorthandTag), "Invalid tag"); },

        In(Stream) * (T(StreamGroup) << (T(Document)++[Documents] * End)) >>
          [](Match& _) { return Documents << _[Documents]; },

        In(TagDirective) *
            (T(TagDirectiveGroup)
             << (T(TagPrefix)[TagPrefix] * T(TagHandle)[TagHandle] * End)) >>
          [](Match& _) { return Seq << _[TagPrefix] << _[TagHandle]; },

        In(Tag) *
            (T(TagGroup)
             << (T(TagPrefix)[TagPrefix] *
                 T(ShorthandTag, VerbatimTag)[TagName] * End)) >>
          [](Match& _) { return Seq << _[TagPrefix] << _[TagName]; },

        In(Tag) * (T(TagGroup) << (T(TagPrefix)[TagPrefix] * End)) >>
          [](Match& _) { return Seq << _[TagPrefix] << (NonSpecificTag ^ ""); },

        T(NewLine, R"(\r?\n(?:\r?\n)+)")[NewLine] >>
          [](Match& _) {
            Location loc = _(NewLine)->location();
            auto view = loc.view();
            size_t start = 0;
            size_t end = view.find('\n', start) + 1;
            Node seq =
              Seq << (NewLine ^ Location(loc.source, loc.pos + start, end));
            start = end;
            while (start < loc.len)
            {
              end = view.find('\n', start) + 1;
              seq
                << (NewLine ^
                    Location(loc.source, loc.pos + start, end - start));
              start = end;
            }
            return seq;
          },

        // errors

        In(StreamGroup) * (DirectiveToken[Value] * End) >>
          [](Match& _) {
            return err(_(Value), "Directive by itself with no document");
          },

        In(DocumentGroup) *
            (T(MaybeDirective)[MaybeDirective] * ~T(NewLine) *
             End)([](auto& n) {
              Node dir = n.front();
              Node doc = dir->parent()->parent()->shared_from_this();
              Node stream = doc->parent()->shared_from_this();
              return stream->find(doc) < stream->end() - 1;
            }) >>
          [](Match& _) {
            return err(
              _(MaybeDirective), "Directive without document end marker");
          },

        In(DocumentGroup, FlowGroup) *
            T(TagPrefix, ShorthandTag, VerbatimTag)[Tag] >>
          [](Match& _) { return err(_(Tag), "Invalid tag"); },

        In(DocumentGroup) *
            (DirectiveToken / T(Document, TagHandle, Stream))[Value] >>
          [](Match& _) { return err(_(Value), "Syntax error"); },
      }};

    pass.pre(Document, [](Node n) {
      n->insert(n->begin(), Directives);
      return 0;
    });

    pass.post(Stream, [](Node n) {
      Node directives = Directives
        << (TagDirective << (TagPrefix ^ "!") << (TagHandle ^ "!"))
        << (TagDirective << (TagPrefix ^ "!!")
                         << (TagHandle ^ "tag:yaml.org,2002:"));
      n->insert(n->begin(), directives);
      if (n->size() == 1)
      {
        n->push_back(Documents ^ "");
      }
      return 0;
    });

    pass.post(Tag, [](Node n) {
      if (n->size() == 1)
      {
        n->push_back(NonSpecificTag ^ "");
      }
      return 0;
    });

    pass.post([](Node n) {
      return invalid_tokens(
        n,
        {{StreamGroup, "Invalid stream"},
         {TagDirectiveGroup, "Invalid tag directive"},
         {TagGroup, "Invalid tag"}});
    });

    return pass;
  }

  PassDef flow()
  {
    PassDef flow = {
      "flow",
      wf_flow,
      dir::bottomup,
      {
        In(FlowMapping, FlowSequence) * T(FlowGroup)[FlowGroup] >>
          [](Match& _) { return Seq << *_[FlowGroup]; },

        In(FlowSequence) *
            (T(Value, R"(\-)")[Value] * T(Comma, FlowSequenceEnd)) >>
          [](Match& _) {
            return err(_(Value), "Plain dashes in flow sequence");
          },

        In(FlowMapping, FlowSequence) * (T(Whitespace, NewLine)) >>
          [](Match&) -> Node { return nullptr; },

        In(FlowSequence) *
            (LineToken[Value] * T(FlowSequenceEnd)[FlowSequenceEnd]) >>
          [](Match& _) {
            return Seq << _(Value) << (Comma ^ ",") << _(FlowSequenceEnd);
          },

        In(FlowMapping) *
            (LineToken[Value] * T(FlowMappingEnd)[FlowMappingEnd]) >>
          [](Match& _) {
            return Seq << _(Value) << (Comma ^ ",") << _(FlowMappingEnd);
          },

        In(FlowMapping) * (LineToken[Head] * LineToken++[Tail] * T(Comma)) >>
          [](Match& _) { return FlowKeyValue << _(Head) << _[Tail]; },

        In(FlowSequence) *
            (T(Key) * FlowToken++[Key] * T(Colon) * FlowToken++[Value] *
             T(Comment)++ * T(Comma)) >>
          [](Match& _) {
            return FlowSequenceItem
              << (FlowGroup
                  << (FlowMapping
                      << (FlowMappingItem << (FlowGroup << _[Key])
                                          << (FlowGroup << _[Value]))));
          },

        In(FlowSequence) *
            (T(Comment)++ * FlowToken[Head] * FlowToken++[Tail] *
             T(Colon)[Colon] * FlowToken++[Value] * T(Comment)++ * T(Comma)) >>
          [](Match& _) {
            if (!same_line(_(Head), _(Colon)))
            {
              return err(_(Head), "Implicit key followed by newline");
            }

            return FlowSequenceItem
              << (FlowGroup
                  << (FlowMapping
                      << (FlowMappingItem << (FlowGroup << _(Head) << _[Tail])
                                          << (FlowGroup << _[Value]))));
          },

        In(FlowSequence) *
            (T(Comment)++ * T(Colon) * FlowToken++[Value] * T(Comment)++ *
             T(Comma)) >>
          [](Match& _) {
            return FlowSequenceItem
              << (FlowGroup
                  << (FlowMapping
                      << (FlowMappingItem << (FlowGroup << Empty)
                                          << (FlowGroup << _[Value]))));
          },

        In(FlowSequence) *
            (T(Comment)++ * FlowToken[Head] * FlowToken++[Tail] * T(Comment)++ *
             T(Comma)) >>
          [](Match& _) {
            return FlowSequenceItem << (FlowGroup << _(Head) << _[Tail]);
          },

        In(FlowMapping) *
            (T(FlowKeyValue)
             << (T(Key) * FlowToken++[Key] * T(Colon) * FlowToken++[Value] *
                 End)) >>
          [](Match& _) {
            return FlowMappingItem << (FlowGroup << _[Key])
                                   << (FlowGroup << _[Value]);
          },

        In(FlowMapping) *
            (T(FlowKeyValue)
             << (FlowToken++[Key] * T(Colon) * FlowToken++[Value] * End)) >>
          [](Match& _) {
            auto value = FlowGroup << _[Value];
            if (value->empty())
            {
              value = FlowGroup << (Empty ^ "");
            }
            return FlowMappingItem << (FlowGroup << _[Key]) << value;
          },

        In(FlowMapping) * (T(FlowKeyValue) << (FlowToken++[Key] * End)) >>
          [](Match& _) {
            auto key = FlowGroup << _[Key];
            Node value = FlowGroup << (Empty ^ "");
            if (
              !key->empty() &&
              key->back()->location().view().find(':') != std::string::npos)
            {
              value = FlowGroup << (Empty ^ "");
            }
            return FlowMappingItem << key << value;
          },

        In(FlowMapping) * (T(FlowKeyValue) << (T(Key) * End)) >>
          [](Match&) {
            return FlowMappingItem << (FlowGroup << (Empty ^ ""))
                                   << (FlowGroup << (Empty ^ ""));
          },

        In(DocumentGroup) *
            ((T(FlowMapping, FlowSequence))[Value] * T(NewLine) * End) >>
          [](Match& _) { return _(Value); },

        In(FlowMappingItem, FlowSequenceItem) *
            (T(FlowGroup)
             << (T(Value)[Value] * T(Value)[Head] * T(Value)++[Tail])) >>
          [](Match& _) {
            return FlowGroup << (Plain << _(Value) << _(Head) << _[Tail]);
          },

        In(Plain) * T(Value)[Value] >>
          [](Match& _) { return BlockLine ^ _(Value); },

        In(FlowMappingItem) *
            ((T(FlowGroup) << End) * (T(FlowGroup)[Value] << Any)) >>
          [](Match& _) {
            return Seq << (FlowGroup << (Empty ^ "")) << _(Value);
          },

        In(FlowMappingItem) *
            ((T(FlowGroup)[Key] << Any) * (T(FlowGroup) << End)) >>
          [](Match& _) { return Seq << _(Key) << (FlowGroup << (Empty ^ "")); },

        In(DocumentGroup) *
            (T(Colon)[Colon] * AnchorTag++[Lhs] * T(NewLine) * ~T(Whitespace) *
             AnchorTag++[Rhs] * T(NewLine)[NewLine]) >>
          [](Match& _) {
            return Seq << _(Colon) << _[Lhs] << _[Rhs] << _(NewLine);
          },

        In(DocumentGroup) *
            (T(Colon)[Colon] * AnchorTag++[Anchor] * T(NewLine) *
             ~T(Whitespace) * T(Folded, Literal)[Block] *
             IndentChomp++[IndentIndicator] * T(NewLine)[NewLine]) >>
          [](Match& _) {
            return Seq << _(Colon) << _[Anchor] << _(Block)
                       << _[IndentIndicator] << _(NewLine);
          },

        In(FlowMapping) *
            (T(FlowMappingStart)[FlowMappingStart] *
             T(FlowMappingItem)++[FlowMappingItems] *
             T(FlowMappingEnd)[FlowMappingEnd]) >>
          [](Match& _) {
            return Seq << _(FlowMappingStart)
                       << (FlowMappingItems << _[FlowMappingItems])
                       << _(FlowMappingEnd);
          },

        In(FlowSequence) *
            (T(FlowSequenceStart)[FlowSequenceStart] *
             T(FlowSequenceItem)++[FlowSequenceItems] *
             T(FlowSequenceEnd)[FlowSequenceEnd]) >>
          [](Match& _) {
            return Seq << _(FlowSequenceStart)
                       << (FlowSequenceItems << _[FlowSequenceItems])
                       << _(FlowSequenceEnd);
          },

        In(FlowMapping) * (Start * T(FlowMappingItem)[FlowMappingItem] * End) >>
          [](Match& _) {
            return Seq << (FlowMappingStart ^ "{")
                       << (FlowMappingItems << _(FlowMappingItem))
                       << (FlowMappingEnd ^ "}");
          },

        // errors
        In(DocumentGroup) *
            (T(DocumentStart) * T(Placeholder) * AnchorTag++ * FlowToken *
             T(Colon)[Colon])(
              [](auto& n) { return same_line(n.front(), n.back()); }) >>
          [](Match& _) {
            return err(_(Colon), "Invalid mapping on document start line");
          },

        In(DocumentGroup) *
            (T(Colon) * T(NewLine) * T(Anchor)[Anchor] * T(NewLine) *
             T(Hyphen))([](auto& n) {
              auto anchor = n[2]->location().linecol().second;
              auto sequence = n[4]->location().linecol().second;
              return anchor == 0 && sequence == 0;
            }) >>
          [](Match& _) {
            return err(_(Anchor), "Invalid anchor in zero indented sequence");
          },

        In(FlowGroup, DocumentGroup) *
            (T(FlowMapping, FlowSequence)[FlowMapping] << End) >>
          [](Match& _) { return err(_(FlowMapping), "Syntax error"); },

        In(FlowGroup, DocumentGroup) *
            (T(FlowMapping)[FlowMapping] << !T(FlowMappingStart)) >>
          [](Match& _) { return err(_(FlowMapping), "Invalid flow mapping"); },

        In(FlowGroup, DocumentGroup) *
            (T(FlowMapping)[FlowMapping]
             << (T(FlowMappingStart) * !T(FlowMappingItems))) >>
          [](Match& _) { return err(_(FlowMapping), "Invalid flow mapping"); },

        In(FlowGroup, DocumentGroup) *
            (T(FlowMapping)[FlowMapping] << (T(FlowMappingStart) * End)) >>
          [](Match& _) { return err(_(FlowMapping), "Invalid flow mapping"); },

        In(FlowGroup, DocumentGroup) *
            (T(FlowMapping)[FlowMapping]
             << (T(FlowMappingStart) * T(FlowMappingItems) *
                 !T(FlowMappingEnd))) >>
          [](Match& _) { return err(_(FlowMapping), "Invalid flow mapping"); },

        In(FlowGroup, DocumentGroup) *
            (T(FlowSequence)[FlowSequence] << !T(FlowSequenceStart)) >>
          [](Match& _) {
            return err(_(FlowSequence), "Invalid flow sequence");
          },

        In(FlowGroup, DocumentGroup) *
            (T(FlowSequence)[FlowSequence]
             << (T(FlowSequenceStart) * !T(FlowSequenceItems))) >>
          [](Match& _) {
            return err(_(FlowSequence), "Invalid flow sequence");
          },

        In(FlowGroup, DocumentGroup) *
            (T(FlowSequence)[FlowSequence]
             << (T(FlowSequenceStart) * T(FlowSequenceItems) *
                 !T(FlowSequenceEnd))) >>
          [](Match& _) {
            return err(_(FlowSequence), "Invalid flow sequence");
          },

        In(FlowGroup, DocumentGroup) *
            (T(FlowSequence)[FlowSequence] << (T(FlowSequenceStart) * End)) >>
          [](Match& _) {
            return err(_(FlowSequence), "Invalid flow sequence");
          },

        In(DocumentGroup) *
            T(Comma,
              FlowMappingStart,
              FlowMappingEnd,
              FlowSequenceStart,
              FlowSequenceEnd)[Value] >>
          [](Match& _) { return err(_(Value), "Invalid flow character"); },

        In(FlowGroup) *
            T(Hyphen,
              Literal,
              Folded,
              IndentIndicator,
              ChompIndicator,
              NewLine,
              Placeholder,
              Whitespace,
              MaybeDirective,
              DocumentStart,
              DocumentEnd)[Value] >>
          [](Match& _) { return err(_(Value), "Syntax error"); },
      }};

    return flow;
  }

  PassDef lines()
  {
    return {
      "lines",
      wf_lines,
      dir::bottomup,
      {
        In(DocumentGroup) *
            (T(DocumentStart)[DocumentStart] * ~T(Whitespace) *
             (BasicToken / AnchorTag)[Value] * ~T(Whitespace) * ~T(Comment)) >>
          [](Match& _) {
            return Seq << _(DocumentStart) << (Placeholder ^ _(DocumentStart))
                       << _(Value);
          },

        In(DocumentGroup, Indent) *
            (T(Whitespace)[Whitespace] * ~T(Comment) * T(NewLine)[NewLine]) >>
          [](Match& _) {
            Location loc = _(Whitespace)->location();
            loc.len = _(NewLine)->location().pos - loc.pos;
            return WhitespaceLine ^ loc;
          },

        In(DocumentGroup) *
            (LineToken[Head] * LineToken++[Tail] * T(NewLine)) >>
          [](Match& _) { return Line << _(Head) << _[Tail]; },

        In(DocumentGroup) *
            (LineToken[Head] * LineToken++[Tail] * End)([](auto& n) {
              return n.front()->parent()->parent() == Document;
            }) >>
          [](Match& _) { return Line << _(Head) << _[Tail]; },

        In(DocumentGroup) *
            (LineToken[Head] * LineToken++[Tail] *
             T(DocumentEnd)[DocumentEnd]) >>
          [](Match& _) {
            return Seq << (Line << _(Head) << _[Tail]) << _(DocumentEnd);
          },

        In(Line) *
            ((T(FlowSequence) / T(FlowMapping))[Flow] * T(Whitespace) *
             T(Comment)) >>
          [](Match& _) { return _(Flow); },

        In(DocumentGroup, Indent) *
            (T(Line)
             << (~T(Whitespace)[Whitespace] * T(Hyphen)[Lhs] * ~T(Whitespace) *
                 T(Hyphen)[Rhs] * Any++[Tail])) >>
          [](Match& _) {
            return Seq << (Line << _(Whitespace) << _(Lhs))
                       << (Line << fake_whitespace(_(Rhs)) << _(Rhs)
                                << _[Tail]);
          },

        In(DocumentGroup, Indent) *
            (T(Line)
             << (~T(Whitespace)[Whitespace] * T(Colon)[Colon] *
                 T(Hyphen)[Hyphen] * Any++[Tail])) >>
          [](Match& _) {
            return Seq << (Line << _(Whitespace) << _(Colon))
                       << (Line << fake_whitespace(_(Hyphen)) << _(Hyphen)
                                << _[Tail]);
          },

        In(DocumentGroup, Indent) *
            (T(Line)
             << (~T(Whitespace)[Whitespace] * T(Hyphen)[Hyphen] *
                 LineToken[Key] * ~T(Whitespace) * T(Colon)[Colon] *
                 Any++[Tail])) >>
          [](Match& _) {
            return Seq << (Line << _(Whitespace) << _(Hyphen))
                       << (Line << fake_whitespace(_(Key)) << _(Key) << _(Colon)
                                << _[Tail]);
          },

        In(DocumentGroup, Indent) *
            (T(Line)
             << (~T(Whitespace)[Whitespace] * T(Hyphen)[Hyphen] *
                 ~T(Whitespace) * T(Colon)[Colon] * Any++[Tail])) >>
          [](Match& _) {
            return Seq << (Line << _(Whitespace) << _(Hyphen))
                       << (Line << fake_whitespace(_(Colon)) << _(Colon)
                                << _[Tail]);
          },

        In(DocumentGroup, Indent) *
            (T(Line)
             << (~T(Whitespace)[Whitespace] * T(Hyphen)[Hyphen] *
                 AnchorTag++[Anchor] * (T(Literal, Folded))[Block] *
                 IndentChomp++[IndentIndicator] * Any++[Tail])) >>
          [](Match& _) {
            auto [indent, chomp] = handle_indent_chomp(_[IndentIndicator]);
            Node seq = Seq
              << (SequenceIndent
                  << (Line << _(Whitespace) << _(Hyphen) << _[Anchor])
                  << (BlockStart << _(Block) << _[IndentIndicator] << _[Tail]));

            if (indent != nullptr)
            {
              std::size_t relative_indent = indent->location().view()[0] - '0';
              std::size_t absolute_indent = relative_indent;
              if (_(Whitespace) != nullptr)
              {
                absolute_indent += _(Whitespace)->location().len;
              }
              seq
                << (ManualIndent
                    << (AbsoluteIndent ^ std::to_string(absolute_indent)));
            }

            return seq;
          },

        In(DocumentGroup, Indent) *
            (T(Line)
             << (~T(Whitespace)[Whitespace] * AnchorTag++[Lhs] *
                 LineToken[Key] * ~T(Whitespace) * T(Colon)[Colon] *
                 AnchorTag++[Rhs] * (T(Literal, Folded))[Block] *
                 IndentChomp++[IndentIndicator] * Any++[Tail])) >>
          [](Match& _) {
            auto [indent, chomp] = handle_indent_chomp(_[IndentIndicator]);
            Node seq = Seq
              << (MappingIndent
                  << (Line << _(Whitespace) << _[Lhs] << _(Key) << _(Colon)
                           << _[Rhs])
                  << (BlockStart << _(Block) << _[IndentIndicator] << _[Tail]));

            if (indent != nullptr)
            {
              std::size_t relative_indent = indent->location().view()[0] - '0';
              std::size_t absolute_indent = relative_indent;
              if (_(Whitespace) != nullptr)
              {
                absolute_indent += _(Whitespace)->location().len;
              }
              seq
                << (ManualIndent
                    << (AbsoluteIndent ^ std::to_string(absolute_indent)));
            }

            return seq;
          },

        In(DocumentGroup, Indent) *
            (T(Line)
             << (~T(Whitespace, Placeholder) * (T(Literal, Folded))[Block] *
                 IndentChomp++[IndentIndicator] * Any++[Tail])) >>
          [](Match& _) {
            auto [indent, chomp] = handle_indent_chomp(_[IndentIndicator]);
            Node seq =
              Seq << (BlockStart << _(Block) << _[IndentIndicator] << _[Tail]);

            if (indent != nullptr)
            {
              std::size_t relative_indent = indent->location().view()[0] - '0';
              std::size_t absolute_indent = relative_indent;
              if (_(Whitespace) != nullptr)
              {
                absolute_indent += _(Whitespace)->location().len;
              }
              seq
                << (ManualIndent
                    << (AbsoluteIndent ^ std::to_string(absolute_indent)));
            }

            return seq;
          },

        In(DocumentGroup) * (T(Line)[Line] << (~T(Whitespace) * T(Hyphen))) >>
          [](Match& _) { return SequenceIndent << _(Line); },

        In(DocumentGroup) * (T(Line)[Line] << (FlowToken++ * T(Colon))) >>
          [](Match& _) { return MappingIndent << _(Line); },

        In(DocumentGroup) *
            (T(Line)[Line] << (T(Placeholder) * FlowToken++ * T(Colon))) >>
          [](Match& _) {
            return err(_(Line), "Mapping with anchor on document start line");
          },

        In(DocumentGroup) *
            (T(Line)[Line] << (~T(Whitespace) * (T(Key, Colon)))) >>
          [](Match& _) { return MappingIndent << _(Line); },

        In(DocumentGroup) * T(Line)[Line] >>
          [](Match& _) { return Indent << _(Line); },

        In(DocumentGroup) * T(NewLine)[NewLine] >>
          [](Match& _) { return EmptyLine ^ _(NewLine); },

        In(DocumentGroup) *
            (T(BlockStart)[BlockStart] * T(WhitespaceLine, EmptyLine)[Line]) >>
          [](Match& _) {
            return Seq << _(BlockStart) << (BlockIndent << _(Line));
          },

        In(DocumentGroup) *
            ((T(SequenceIndent, MappingIndent)[Indent]
              << (T(Line) * ~T(Whitespace) * T(BlockStart))) *
             T(WhitespaceLine, EmptyLine)[Line]) >>
          [](Match& _) { return Seq << _(Indent) << (BlockIndent << _(Line)); },

        In(DocumentGroup) *
            (T(BlockIndent)[BlockIndent] *
             T(WhitespaceLine, EmptyLine)[Line]) >>
          [](Match& _) { return BlockIndent << *_[BlockIndent] << _(Line); },

        In(MappingIndent) *
            (T(Line)
             << (~T(Whitespace)[Whitespace] * T(Key)[Key] * T(Hyphen)[Hyphen] *
                 Any++[Tail])) >>
          [](Match& _) {
            return Seq << (Line << _(Whitespace) << _(Key))
                       << (SequenceIndent
                           << (Line << fake_whitespace(_(Hyphen)) << _(Hyphen)
                                    << _[Tail]));
          },

        In(SequenceIndent) *
            (T(Line)
             << (~T(Whitespace)[Whitespace] * T(Hyphen)[Hyphen] *
                 AnchorTag++[Anchor] * T(Key)[Key] * Any++[Tail])) >>
          [](Match& _) {
            return Seq << (Line << _(Whitespace) << _(Hyphen) << _[Anchor])
                       << (MappingIndent
                           << (Line << fake_whitespace(_(Key)) << _(Key)
                                    << _[Tail]));
          },

        In(Line) *
            (~T(Whitespace)[Whitespace] * AnchorTag[Anchor] * ~AnchorTag[Tag] *
             ~T(Whitespace) * T(Comment)) >>
          [](Match& _) { return Seq << _(Whitespace) << _(Anchor) << _(Tag); },

        // errors
        In(DocumentGroup) *
            (LineToken[Value] * LineToken++ *
             T(DocumentStart)[DocumentStart]) >>
          [](Match& _) {
            return Seq << err(_(Value), "Syntax error") << _(DocumentStart);
          },

        In(DocumentGroup) *
            (T(DocumentEnd)[DocumentEnd] * LineToken[Value] * LineToken++) >>
          [](Match& _) {
            return Seq << _(DocumentEnd) << err(_(Value), "Syntax error");
          },

        In(BlockStart) * T(Hyphen)[Hyphen] >>
          [](Match& _) {
            return err(
              _(Hyphen), "Sequence item on same line as block indicator");
          },
      }};
  }

  PassDef indents()
  {
    PassDef indents = {
      "indents",
      wf_indents,
      dir::bottomup,
      {
        In(BlockStart) * (~T(Whitespace) * T(Comment)) >>
          [](Match&) -> Node { return nullptr; },

        In(DocumentGroup, Indent, MappingIndent, SequenceIndent, BlockIndent) *
            (IndentToken[Indent] * T(EmptyLine, WhitespaceLine)[Line]) >>
          [](Match& _) { return _(Indent)->type() << *_[Indent] << _(Line); },

        In(DocumentGroup, Indent, MappingIndent, SequenceIndent, BlockIndent) *
            (IndentToken[Lhs] * IndentToken[Rhs])(
              [](auto& n) { return less_indented(n.front(), n.back()); }) >>
          [](Match& _) { return _(Lhs)->type() << *_[Lhs] << _(Rhs); },

        In(DocumentGroup, Indent, MappingIndent, SequenceIndent, BlockIndent) *
            (IndentToken[Lhs] * IndentToken[Rhs])(
              [](auto& n) { return same_indent(n.front(), n.back()); }) >>
          [](Match& _) {
            if (_(Lhs)->type() == _(Rhs)->type())
            {
              return _(Lhs)->type() << *_[Lhs] << *_[Rhs];
            }
            else
            {
              return _(Lhs)->type() << *_[Lhs] << _(Rhs);
            }
          },

        In(Indent, DocumentGroup) * (T(Indent) << (T(Indent)[Indent] * End)) >>
          [](Match& _) { return _(Indent); },

        In(SequenceIndent) *
            ((T(Line)[Line] << (~T(Whitespace) * T(Hyphen) * T(Value))) *
             (T(MappingIndent, SequenceIndent))[Indent]) >>
          [](Match& _) {
            return Seq << _(Line) << (BlockIndent << *_[Indent]);
          },

        In(BlockIndent) * T(Indent, MappingIndent, SequenceIndent)[Indent] >>
          [](Match& _) { return BlockIndent << *_[Indent]; },

        In(BlockIndent) *
            (T(Line)[Line] << (T(Whitespace)[Whitespace] * Any[Value] * Any)) >>
          [](Match& _) {
            Location start = _(Value)->location();
            Location end = _(Line)->back()->location();
            Location loc = start;
            loc.len = end.pos + end.len - start.pos;
            return Line << _(Whitespace) << (Value ^ loc);
          },

        In(SequenceIndent) *
            ((T(Line)[Line] << (~T(Whitespace) * T(Hyphen))) *
             T(BlockStart)[BlockStart] *
             (T(MappingIndent, SequenceIndent, Indent))[Indent]) >>
          [](Match& _) {
            return Seq << _(Line) << _(BlockStart)
                       << (BlockIndent << *_[Indent]);
          },

        In(MappingIndent) *
            (T(Indent)
             << ((T(Line) << (~T(Whitespace) * T(Comment))) *
                 T(MappingIndent, SequenceIndent)[Indent] * End)) >>
          [](Match& _) { return _(Indent); },

        In(MappingIndent) *
            ((T(Line)
              << (~T(Whitespace)[Whitespace] * T(Key)[Key] *
                  AnchorTag++[Anchor] * FlowToken[Lhs] * Any++[Tail])) *
             (T(Line)
              << (~T(Whitespace)[Placeholder] * T(Colon)[Colon] *
                  AnchorTag++[Tag] * FlowToken[Rhs] * Any++[Extra]))) >>
          [](Match& _) {
            return Seq << (Line << _(Whitespace) << _(Key))
                       << (Indent
                           << (Line << fake_whitespace(_(Lhs)) << _[Anchor]
                                    << _(Lhs) << _[Tail]))
                       << (Line << _(Placeholder) << _(Colon))
                       << (Indent
                           << (Line << fake_whitespace(_(Rhs)) << _[Tag]
                                    << _(Rhs) << _[Extra]));
          },

        In(MappingIndent) *
            (T(Indent)
             << ((T(Line)[Line] << (~T(Whitespace) * FlowToken++ * T(Colon))) *
                 End)) >>
          [](Match& _) { return MappingIndent << _(Line); },

        In(MappingIndent) *
            (T(Indent)
             << ((T(Line)[Line] << (~T(Whitespace) * T(Hyphen))) * End)) >>
          [](Match& _) { return SequenceIndent << _(Line); },

        // errors

        In(Line) * (LineToken * T(Colon) * T(Hyphen)[Hyphen]) >>
          [](Match& _) {
            return err(_(Hyphen), "Sequence on same Line as Mapping Key");
          },

        In(Line) * (T(Hyphen) * LineToken * T(Hyphen)[Hyphen]) >>
          [](Match& _) {
            return err(
              _(Hyphen), "Invalid sequence item on same Line as previous item");
          },

        In(SequenceIndent) * T(Indent, BlockIndent)[Indent]([](auto& n) {
          Node indent = n.front();
          Node parent = indent->parent()->shared_from_this();
          return same_indent(parent, indent);
        }) >>
          [](Match& _) -> Node {
          if (all_comments(_(Indent)))
          {
            return nullptr;
          }

          return err(_(Indent), "Wrong indentation");
        },

        In(Line) *
            (T(Comment, "#[^ \t].*: .*")[Comment] * In(MappingIndent)++) >>
          [](Match& _) -> Node {
          Location loc = _(Comment)->location();
          auto col = loc.linecol().second;
          loc.pos -= col;
          loc.len = col;
          if (loc.view().find_first_not_of(" \t") == std::string::npos)
          {
            return nullptr;
          }

          return err(_(Comment), "Comment that looks like a mapping key");
        },

        T(BlockStart)[BlockStart] << End >>
          [](Match& _) { return err(_(BlockStart), "Invalid block start"); },
      }};

    return indents;
  }

  PassDef colgroups()
  {
    return {
      "colgroups",
      wf_colgroups,
      dir::bottomup | dir::once,
      {
        T(SequenceIndent)[SequenceIndent] >>
          [](Match& _) {
            return SequenceIndent << (SequenceGroup << *_[SequenceIndent]);
          },

        T(MappingIndent)[MappingIndent] >>
          [](Match& _) {
            return MappingIndent << (MappingGroup << *_[MappingIndent]);
          },
      }};
  }

  PassDef items()
  {
    PassDef items = {
      "items",
      wf_items,
      dir::bottomup,
      {
        In(DocumentGroup) * (T(Line)[Line] << T(Comment)) >>
          [](Match& _) { return EmptyLine ^ _(Line); },

        In(Line) * T(Placeholder) >> [](Match&) -> Node { return nullptr; },

        In(Line) * (T(Colon) * ValueToken++[Value] * T(Colon)[Colon]) >>
          [](Match& _) {
            return err(
              _(Colon),
              "Invalid block mapping key on same line as previous key");
          },

        In(MappingGroup) *
            (T(Line)[Line]
             << (~T(Whitespace) * T(Key)[Key] * T(Colon)[Colon] *
                 T(Value)[Value] * End)) >>
          [](Match& _) {
            return ComplexKey
              << (MappingIndent
                  << (MappingGroup << (ComplexKey << Empty)
                                   << (ComplexValue << _(Value))));
          },

        In(MappingGroup) *
            (T(Line)[Line]
             << (~T(Whitespace) * T(Key)[Key] *
                 T(FlowSequence, FlowMapping)[Flow] * T(Colon)[Colon] *
                 T(Value)[Value] * End)) >>
          [](Match& _) {
            return ComplexKey
              << (MappingIndent
                  << (MappingGroup << (ComplexKey << _(Flow))
                                   << (ComplexValue << _(Value))));
          },

        In(MappingGroup) *
            (T(Line) << (~T(Whitespace) * T(Key) * Any++[Tail])) >>
          [](Match& _) { return ComplexKey << _[Tail]; },

        In(MappingGroup) *
            (T(Line) << (~T(Whitespace) * T(Colon) * Any++[Tail])) >>
          [](Match& _) { return ComplexValue << _[Tail]; },

        In(SequenceGroup) *
            ((T(Line)
              << (~T(Whitespace) * T(Hyphen) * AnchorTag++[Anchor] *
                  Any++[Tail])) *
             IndentToken[Value]) >>
          [](Match& _) {
            Node first = Line << _[Tail];
            if (first->empty())
            {
              return SequenceItem << (ValueGroup << _[Anchor] << _(Value));
            }
            else
            {
              return SequenceItem
                << (ValueGroup << _[Anchor] << first << _(Value));
            }
          },

        In(SequenceGroup) *
            ((T(Line) << (~T(Whitespace) * T(Hyphen) * AnchorTag++[Anchor])) *
             T(BlockStart)[BlockStart] * IndentToken[Value]) >>
          [](Match& _) {
            return SequenceItem
              << (ValueGroup << _[Anchor] << _(BlockStart) << _(Value));
          },

        In(SequenceGroup) *
            (T(Line)
             << (~T(Whitespace) * T(Hyphen) * AnchorTag++[Anchor] *
                 ~ValueToken[Value] * ~T(Whitespace) * ~T(Comment) * End)) >>
          [](Match& _) {
            Node value = _(Value);
            if (value == nullptr)
            {
              value = Empty ^ "";
            }
            return SequenceItem << (ValueGroup << _[Anchor] << value);
          },

        In(SequenceGroup) *
            (T(Line) << (~T(Whitespace) * T(Hyphen) * T(Tag)[Tag] * End)) >>
          [](Match& _) {
            return SequenceItem << (ValueGroup << _(Tag) << (Empty ^ ""));
          },

        In(MappingGroup) *
            (T(Line) << (T(Whitespace, R"(.*\t.*)")[Whitespace])) >>
          [](Match& _) {
            return err(_(Whitespace), "Tab character in indentation");
          },

        In(MappingGroup) *
            ((T(Line)
              << (~T(Whitespace) * AnchorTag++[Lhs] * ValueToken[Key] *
                  ~T(Whitespace) * T(Colon) * AnchorTag++[Rhs])) *
             T(BlockStart)[BlockStart] * IndentToken[Value]) >>
          [](Match& _) {
            return MappingItem
              << (KeyGroup << _[Lhs] << _(Key))
              << (ValueGroup << _[Rhs] << _(BlockStart) << _(Value));
          },

        In(MappingGroup) *
            ((T(Line)
              << (~T(Whitespace) * AnchorTag++[Lhs] * ValueToken[Key] *
                  ~T(Whitespace) * T(Colon) * AnchorTag++[Rhs] * Any++[Tail])) *
             T(WhitespaceLine, EmptyLine)++[Whitespace] * IndentToken[Value]) >>
          [](Match& _) {
            Node first = Line << _[Tail];
            if (first->empty())
            {
              return MappingItem << (KeyGroup << _[Lhs] << _(Key))
                                 << (ValueGroup << _[Rhs] << _(Value));
            }

            return MappingItem
              << (KeyGroup << _[Lhs] << _(Key))
              << (ValueGroup << _[Rhs] << first << _[Whitespace] << _(Value));
          },

        In(MappingGroup) *
            (T(Line)
             << (~T(Whitespace) * AnchorTag++[Lhs] * ValueToken[Key] *
                 ~T(Whitespace) * T(Colon) * AnchorTag++[Rhs] *
                 ValueToken[Head] * Any++[Tail])) >>
          [](Match& _) {
            if (!_[Tail].empty())
            {
              for (const Node& n : _[Tail])
              {
                if (!all_comments(n))
                {
                  return err(n, "Trailing content on mapping item");
                }
              }
            }
            return MappingItem << (KeyGroup << _[Lhs] << _(Key))
                               << (ValueGroup << _[Rhs] << _(Head));
          },

        In(MappingGroup) *
            (T(Line)
             << (~T(Whitespace) * AnchorTag++[Lhs] * T(Colon) *
                 AnchorTag++[Rhs] * ~ValueToken[Value] * ~T(Whitespace) *
                 ~T(Comment) * End)) >>
          [](Match& _) {
            Node value = _(Value);
            if (value == nullptr)
            {
              value = Empty ^ "";
            }
            return MappingItem << (KeyGroup << _[Lhs] << Empty)
                               << (ValueGroup << _[Rhs] << value);
          },

        In(MappingGroup) * T(Line)
            << (~T(Whitespace) * AnchorTag++[Lhs] * ValueToken[Key] * T(Colon) *
                AnchorTag++[Rhs] * End) >>
          [](Match& _) {
            return MappingItem << (KeyGroup << _[Lhs] << _(Key))
                               << (ValueGroup << _[Rhs] << (Empty ^ ""));
          },

        In(MappingGroup) * T(Line)
            << (~T(Whitespace) * T(Tag)[Lhs] * T(Colon) * T(Tag)[Rhs] * End) >>
          [](Match& _) {
            return MappingItem << (KeyGroup << _(Lhs) << Empty)
                               << (ValueGroup << _(Rhs) << (Empty ^ ""));
          },

        In(MappingGroup) *
            ((T(ComplexKey, ComplexValue))[Lhs] * IndentToken[Indent]) >>
          [](Match& _) {
            return _(Lhs)->type() << (Line << *_[Lhs]) << _(Indent);
          },

        In(DocumentGroup, KeyGroup, ValueGroup) *
            (T(EmptyLine, WhitespaceLine) * End) >>
          [](Match&) -> Node { return nullptr; },

        In(MappingItem, SequenceItem) *
            (T(KeyGroup, ValueGroup)[Group]
             << (T(Line) << (T(Whitespace) * End))) >>
          [](Match& _) {
            Node group = _(Group);
            group->erase(group->begin(), group->begin() + 1);
            return group;
          },

        In(ComplexKey) * (T(Hyphen) * ValueToken[Value]) >>
          [](Match& _) { return SequenceIndent << (SequenceItem << _(Value)); },

        In(ComplexKey, ComplexValue) * (T(Line) << End) >>
          [](Match&) -> Node { return nullptr; },

        In(ComplexKey, ComplexValue) *
            (T(Line) << (AnchorTag++[Anchor] * T(Literal, Folded)[Block])) >>
          [](Match& _) {
            Node anchortag = Line << _[Anchor];
            if (anchortag->empty())
            {
              return BlockStart << _(Block);
            }

            return Seq << anchortag << (BlockStart << _(Block));
          },

        In(ComplexKey) * (AnchorTag[Anchor] * End) >>
          [](Match& _) { return Seq << _(Anchor) << (Empty ^ ""); },

        In(ComplexValue) * (AnchorTag[Anchor] * End) >>
          [](Match& _) { return Seq << _(Anchor) << (Empty ^ ""); },

        In(DocumentGroup, KeyGroup, ValueGroup) * ((T(Indent, Line)) << End) >>
          [](Match&) -> Node { return nullptr; },

        In(MappingGroup, SequenceGroup) *
            (T(Line) << (~T(Whitespace) * T(Comment))) >>
          [](Match&) -> Node { return nullptr; },

        In(MappingGroup) * (T(ComplexValue) << End) >>
          [](Match&) { return ComplexValue << (Empty ^ ""); },

        In(ComplexKey, ComplexValue) *
            ((T(Indent) << (T(Line) << (~T(Whitespace) * T(Comment)))) * End) >>
          [](Match&) -> Node { return nullptr; },

        In(SequenceGroup, MappingGroup) * T(WhitespaceLine, EmptyLine) >>
          [](Match&) -> Node { return nullptr; },

        In(SequenceGroup, MappingGroup) *
            (T(Indent) << ((T(Line) << (T(Whitespace) * End))++ * End)) >>
          [](Match&) -> Node { return nullptr; },

        In(Documents) *
            (T(Document) << (T(Directives) << End) *
               (T(DocumentGroup) << End)) >>
          [](Match&) -> Node { return nullptr; },

        In(MappingIndent) *
            (T(MappingGroup)[Group]
             << (T(MappingItem, ComplexKey, ComplexValue)++ * End)) >>
          [](Match& _) { return Seq << *_[Group]; },

        In(SequenceIndent) *
            (T(SequenceGroup)[Group] << (T(SequenceItem)++ * End)) >>
          [](Match& _) { return Seq << *_[Group]; },

        // errors

        In(BlockStart) * BasicToken[Value] >> [](Match& _) -> Node {
          return err(_(Value), "Invalid text after block scalar indicator");
        },

        In(SequenceItem) *
            (T(ValueGroup) << (T(FlowMapping, FlowSequence))[Flow])(
              [](auto& n) {
                Node group = n.front();
                Node item = group->parent()->shared_from_this();
                Node flow = group->front();
                std::size_t item_indent = item->location().linecol().second;
                std::size_t flow_indent = min_indent(flow);
                return flow_indent <= item_indent;
              }) >>
          [](Match& _) { return err(_(Flow), "Wrong indented flow"); },

        In(MappingItem) *
            (T(KeyGroup) *
             (T(ValueGroup) << (T(FlowMapping, FlowSequence)))[Flow])(
              [](auto& n) {
                Node key = n.front();
                Node value = n.back();
                Node flow = value->front();
                std::size_t item_indent = min_indent(key);
                std::size_t flow_indent = min_indent(flow);
                return flow_indent <= item_indent;
              }) >>
          [](Match& _) { return err(_(Flow), "Wrong indented flow"); },

        In(MappingItem) *
            (T(KeyGroup) << (T(FlowMapping, FlowSequence))[Flow])([](auto& n) {
              Node key = n.front();
              Node flow = key->front();
              std::size_t line0 = flow->front()->location().linecol().first;
              std::size_t line1 = flow->back()->location().linecol().first;
              return line0 != line1;
            }) >>
          [](Match& _) {
            return err(_(Flow), "Flow mapping key on two lines");
          },

        In(MappingItem) *
            (T(KeyGroup) * (T(ValueGroup) << AnchorTag++[Anchor]))([](auto& n) {
              Node key = n.front();
              Node value = n.back();
              std::size_t key_indent = min_indent(key);
              std::size_t anchortag_indent = std::string::npos;
              for (const Node& child : *value)
              {
                if (child == Anchor || child == Tag)
                {
                  anchortag_indent = std::min(
                    anchortag_indent, child->location().linecol().second);
                }
              }
              if (anchortag_indent == std::string::npos)
              {
                return false;
              }

              return key_indent == anchortag_indent;
            }) >>
          [](Match& _) { return err(_[Anchor], "Node anchor not indented"); },

        In(Line) * (T(Anchor)[Anchor] * T(Hyphen)) >>
          [](Match& _) {
            return err(_(Anchor), "Anchor before sequence entry on same line");
          },

        In(Line) *
            T(Line,
              ManualIndent,
              Indent,
              BlockIndent,
              SequenceIndent,
              MappingIndent)[Value] >>
          [](Match& _) { return err(_(Value), "Syntax error"); },

        In(ComplexKey, ComplexValue) * T(Colon)[Colon] >>
          [](Match& _) { return err(_(Colon), "Invalid mapping item"); },

        T(MappingIndent, SequenceIndent)[Indent] << End >>
          [](Match& _) { return err(_(Indent), "Syntax error"); },
      }};

    items.post([](Node n) {
      return invalid_tokens(
        n,
        {{MappingGroup, "Invalid mapping"},
         {SequenceGroup, "Invalid sequence"}});
    });

    return items;
  }

  PassDef complex()
  {
    PassDef complex = {
      "complex",
      wf_complex,
      dir::bottomup,
      {
        In(Indent) *
            (T(Line)
             << (~T(Whitespace) * AnchorTag[Anchor] * ~AnchorTag[Tag] * End)) >>
          [](Match& _) {
            Token nearest_group = find_nearest(
              _(Anchor)->parent(), {DocumentGroup, KeyGroup, ValueGroup});
            return Lift << nearest_group << _(Anchor) << _(Tag);
          },

        In(MappingIndent) *
            ((T(Line)
              << (~T(Whitespace) * AnchorTag++[Lhs] * ValueToken[Key] *
                  T(Colon) * AnchorTag++[Rhs])) *
             T(SequenceItem)++[Value]) >>
          [](Match& _) {
            return MappingItem
              << (KeyGroup << _[Lhs] << _(Key))
              << (ValueGroup << _[Rhs] << (SequenceIndent << _[Value]));
          },

        In(Document) * (T(DocumentGroup)[Group] << T(Indent))([](auto& n) {
          return all_comments(n.front()->front());
        }) >>
          [](Match& _) {
            Node g = _(Group);
            g->erase(g->begin(), g->begin() + 1);
            return g;
          },

        In(DocumentGroup, KeyGroup, ValueGroup) * (T(Line) << T(Comment)) >>
          [](Match&) -> Node { return nullptr; },

        In(ComplexKey, ComplexValue) *
            (T(SequenceItem)[Head] * T(SequenceItem)++[Tail]) >>
          [](Match& _) { return SequenceIndent << _(Head) << _[Tail]; },

        In(MappingIndent) * (T(ComplexKey)[Key] * T(ComplexValue)[Value]) >>
          [](Match& _) {
            return MappingItem << (KeyGroup << *_[Key])
                               << (ValueGroup << *_[Value]);
          },

        In(MappingIndent) * T(ComplexKey)[Key] >>
          [](Match& _) {
            return MappingItem << (KeyGroup << *_[Key])
                               << (ValueGroup << (Empty ^ ""));
          },

        In(MappingIndent) * T(ComplexValue)[Value] >>
          [](Match& _) {
            return MappingItem << (KeyGroup << Empty)
                               << (ValueGroup << *_[Value]);
          },

        In(MappingIndent, SequenceIndent) * T(Indent)[Indent]([](auto& n) {
          return all_comments(n.front());
        }) >>
          [](Match&) -> Node { return nullptr; },

        T(Indent) << End >> [](Match&) -> Node { return nullptr; },
      }};

    complex.post([](Node n) {
      return invalid_tokens(
        n, {{Key, "Invalid complex key"}, {Colon, "Invalid complex value"}});
    });

    return complex;
  }

  PassDef blocks()
  {
    PassDef blocks = {
      "blocks",
      wf_blocks,
      dir::bottomup,
      {
        In(KeyGroup, ValueGroup, DocumentGroup) *
            (T(ManualIndent)
             << (T(BlockStart)[BlockStart] * T(AbsoluteIndent) *
                 (T(Indent, EmptyLine, WhitespaceLine, Line))++[Tail])) >>
          [](Match& _) { return Seq << _(BlockStart) << _[Tail]; },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            (T(BlockStart)[BlockStart] *
             (T(ManualIndent)
              << (T(AbsoluteIndent) *
                  (T(Indent, EmptyLine, WhitespaceLine, Line))++[Tail]))) >>
          [](Match& _) { return Seq << _(BlockStart) << _[Tail]; },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            (T(Indent)
             << ((T(BlockStart)
                  << ((T(Literal, Folded))[Block] * IndentChomp++[Extra] *
                      Any++[Line])) *
                 (T(BlockIndent, Indent, Line, EmptyLine, WhitespaceLine))++
                 [Indent])) >>
          [](Match& _) {
            Node first = Line << _[Line];
            if (!first->empty())
            {
              return _(Block)->type()
                << (BlockGroup << _[Extra] << first << _[Indent]);
            }

            return _(Block)->type() << (BlockGroup << _[Extra] << _[Indent]);
          },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            ((T(BlockStart)
              << ((T(Literal, Folded))[Block] * IndentChomp++[Extra] *
                  Any++[Line])) *
             (T(BlockIndent, Indent, Line, EmptyLine, WhitespaceLine))++
             [Indent]) >>
          [](Match& _) {
            Node first = Line << _[Line];
            if (!first->empty())
            {
              return _(Block)->type()
                << (BlockGroup << _[Extra] << first << _[Indent]);
            }

            return _(Block)->type() << (BlockGroup << _[Extra] << _[Indent]);
          },

        In(BlockGroup) * (T(BlockIndent, Indent))[Indent] >>
          [](Match& _) { return Seq << *_[Indent]; },

        In(BlockGroup) * (T(Line)[Line] << Any) >>
          [](Match& _) {
            Node line = _(Line);
            Location start = line->front()->location();
            Location end = line->back()->location();
            Location loc = start;
            loc.len = end.pos + end.len - loc.pos;
            return BlockLine ^ loc;
          },

        In(BlockGroup) * T(EmptyLine)[EmptyLine] >>
          [](Match& _) {
            Location loc = _(EmptyLine)->location();
            loc.len = loc.len - 1;
            return BlockLine ^ loc;
          },

        In(BlockGroup) * T(WhitespaceLine)[WhitespaceLine] >>
          [](Match& _) { return BlockLine ^ _(WhitespaceLine); },

        In(BlockGroup) *
            (T(ChompIndicator)[ChompIndicator] *
             T(IndentIndicator)[IndentIndicator]) >>
          [](Match& _) {
            return Seq << _(IndentIndicator) << _(ChompIndicator);
          },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            (T(Indent)
             << ((T(Line)
                  << (~T(Whitespace) * AnchorTag++[Anchor] *
                      (T(DoubleQuote, SingleQuote, Alias, Int) /
                       T(Float, FlowMapping, FlowSequence))[Value] *
                      ~T(Whitespace) * End)) *
                 (T(Line) << T(Whitespace))++ * End)) >>
          [](Match& _) { return Seq << _[Anchor] << _(Value); },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            (T(Indent)[Indent]
             << ((T(Line)
                  << (~T(Whitespace) * AnchorTag++[Anchor] *
                      BasicToken++[Line] * End)) *
                 (T(
                   Line,
                   BlockIndent,
                   Indent,
                   EmptyLine,
                   WhitespaceLine,
                   MappingIndent,
                   SequenceIndent))++[Tail] *
                 End)) >>
          [](Match& _) {
            return Seq << _[Anchor] << (Plain << (Line << _[Line]) << _[Tail]);
          },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            ((T(Line)
              << (~T(Whitespace) * AnchorTag++[Anchor] * BasicToken++[Line] *
                  End)) *
             (T(
               Line,
               BlockIndent,
               Indent,
               EmptyLine,
               WhitespaceLine,
               MappingIndent,
               SequenceIndent))++[Tail]) >>
          [](Match& _) {
            return Seq << _[Anchor] << (Plain << (Line << _[Line]) << _[Tail]);
          },

        In(Plain) * (T(Indent, BlockIndent))[Indent] >>
          [](Match& _) { return Seq << *_[Indent]; },

        In(Plain) *
            ((T(Line)
              << (~T(Whitespace) * ValueToken[Value] * ~T(Whitespace) *
                  T(Comment))) *
             End) >>
          [](Match& _) { return Line << _[Value]; },

        In(Plain) * (T(Line)[Line] << Any) >>
          [](Match& _) {
            Node line = _(Line);
            if (line->front() == Whitespace)
            {
              line->erase(line->begin(), line->begin() + 1);
            }
            if (!line->empty() && line->back() == Whitespace)
            {
              line->pop_back();
            }
            if (!line->empty() && line->back() == Comment)
            {
              return err(line, "comment in multiline plain scalar");
            }
            if (line->empty())
            {
              return BlockLine ^ line;
            }

            Location start = line->front()->location();
            Location end = line->back()->location();
            Location loc = start;
            loc.len = end.pos + end.len - loc.pos;
            return BlockLine ^ loc;
          },

        In(Plain) * (T(Line)[Line] << End) >>
          [](Match&) -> Node { return EmptyLine; },

        In(Plain) * (T(WhitespaceLine, EmptyLine) * End) >>
          [](Match&) -> Node { return nullptr; },

        In(Plain) * T(WhitespaceLine)[WhitespaceLine] >>
          [](Match& _) { return EmptyLine ^ _(WhitespaceLine); },

        In(BlockGroup) * T(BlockLine, R"(.*\n.*)")[BlockLine] >>
          [](Match& _) {
            Nodes lines;
            Location loc = _(BlockLine)->location();
            std::string_view view = loc.view();
            std::size_t start = 0;
            std::size_t newline = view.find('\n');
            while (newline != std::string::npos)
            {
              lines.push_back(
                BlockLine ^
                Location(loc.source, loc.pos + start, newline - start));
              start = newline + 1;
              newline = view.find('\n', start);
            }

            if (start < view.size())
            {
              lines.push_back(
                BlockLine ^
                Location(loc.source, loc.pos + start, view.size() - start));
            }

            return Seq << lines;
          },

        In(Plain) * T(BlockLine, R"(.*[ \t])")[BlockLine] >>
          [](Match& _) {
            Location loc = _(BlockLine)->location();
            auto view = loc.view();
            auto it = std::find_if(view.rbegin(), view.rend(), [](char c) {
              return c != ' ' && c != '\t';
            });
            loc.len = std::distance(it, view.rend());
            return BlockLine ^ loc;
          },

        T(Plain) << End >> [](Match&) -> Node { return nullptr; },

        T(Literal, Folded)[Block] << End >>
          [](Match& _) { return _(Block) << BlockGroup; },

        T(Indent)
            << (T(WhitespaceLine)++ * T(MappingIndent, SequenceIndent)[Indent] *
                End) >>
          [](Match& _) { return _(Indent); },

        T(Indent)
            << ((T(Line) << (T(Comment) * End))++ *
                T(MappingIndent, SequenceIndent)[Indent] * End) >>
          [](Match& _) { return _(Indent); },

        In(DocumentGroup) * (T(Indent) << (T(Line)[Line] * End)) >>
          [](Match& _) { return Seq << *_[Line]; },

        In(KeyGroup, ValueGroup, DocumentGroup) * T(EmptyLine) >>
          [](Match&) -> Node { return nullptr; },

        // errors

        In(Plain, BlockGroup) * T(MappingIndent)[MappingIndent] >>
          [](Match& _) {
            return err(_(MappingIndent), "Invalid mapping in plain multiline");
          },

        In(Plain, BlockGroup) * T(SequenceIndent)[SequenceIndent] >>
          [](Match& _) {
            return err(
              _(SequenceIndent), "Invalid sequence in plain multiline");
          },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            T(ChompIndicator)[ChompIndicator] >>
          [](Match& _) {
            return err(_(ChompIndicator), "Invalid chomp indicator");
          },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            T(IndentIndicator)[IndentIndicator] >>
          [](Match& _) {
            return err(_(IndentIndicator), "Invalid indent indicator");
          },

        In(BlockGroup) * T(BlockLine, "\t.*")[BlockLine] >>
          [](Match& _) {
            return err(_(BlockLine), "Tab being used as indentation");
          },
      }};

    blocks.post([](Node n) {
      return invalid_tokens(
        n,
        {{Indent, "Invalid indent"},
         {ManualIndent, "Invalid block scalar indent indicator"},
         {BlockIndent, "Invalid block indent"},
         {Colon, "Invalid mapping item"},
         {Hyphen, "Invalid sequence item"},
         {Line, "Invalid indentation"},
         {Key, "Invalid complex key"},
         {MaybeDirective, "Unexpected stream directive"},
         {BlockStart, "Invalid block scalar"},
         {Placeholder, "Token on same line as document start"}});
    });

    return blocks;
  }

  PassDef collections()
  {
    return {
      "collections",
      wf_collections,
      dir::bottomup,
      {
        In(KeyGroup, ValueGroup, DocumentGroup) *
            (T(MappingIndent)[MappingIndent]) >>
          [](Match& _) { return Mapping << *_[MappingIndent]; },

        In(KeyGroup, ValueGroup, DocumentGroup) *
            (T(SequenceIndent)[SequenceIndent]) >>
          [](Match& _) { return Sequence << *_[SequenceIndent]; },

        In(KeyGroup, ValueGroup) * T(Whitespace, Comment) >>
          [](Match&) -> Node { return nullptr; },

        In(KeyGroup, ValueGroup, DocumentGroup) * T(WhitespaceLine) >>
          [](Match&) -> Node { return nullptr; },

        In(Mapping, Sequence, DocumentGroup) *
            T(EmptyLine, WhitespaceLine, Whitespace, Comment) >>
          [](Match&) -> Node { return nullptr; },

        In(FlowMapping) *
            (T(FlowMappingStart) * T(FlowMappingItems)[FlowMappingItems] *
             T(FlowMappingEnd)) >>
          [](Match& _) { return Seq << *_[FlowMappingItems]; },

        In(FlowSequence) *
            (T(FlowSequenceStart) * T(FlowSequenceItems)++[FlowSequenceItems] *
             T(FlowSequenceEnd)) >>
          [](Match& _) { return Seq << *_[FlowSequenceItems]; },

        In(MappingItem) * (T(ValueGroup)[Value] << End) >>
          [](Match& _) { return _(Value) << (Empty ^ ""); },

        // errors

        In(DocumentGroup) * T(MaybeDirective)[MaybeDirective] >>
          [](Match& _) {
            return err(
              _(MaybeDirective), "Directive without document end marker");
          },

        In(Mapping) * T(Line)[Line] >>
          [](Match& _) { return err(_(Line), "Invalid value after mapping"); },

        In(Sequence) * T(Line)[Line] >>
          [](Match& _) { return err(_(Line), "Invalid value after sequence"); },
      }};
  }

  PassDef attributes()
  {
    PassDef attributes = {
      "attributes",
      wf_attributes,
      dir::bottomup,
      {
        In(KeyGroup, ValueGroup, DocumentGroup, FlowGroup) *
            (T(Anchor)[Anchor] * ValueToken[Value]) >>
          [](Match& _) { return AnchorValue << _(Anchor) << _(Value); },

        In(KeyGroup, ValueGroup, DocumentGroup, FlowGroup) *
            (T(Tag)[Tag] * ValueToken[Value]) >>
          [](Match& _) { return TagValue << _(Tag) << _(Value); },

        In(KeyGroup, ValueGroup, DocumentGroup, FlowGroup) *
            (T(Tag)[Tag] * End) >>
          [](Match& _) { return TagValue << _(Tag) << (Empty ^ ""); },

        In(DocumentStart) * (T(Tag)[Tag] * T(DocumentEnd)[DocumentEnd]) >>
          [](Match& _) {
            return Seq << (TagValue << _(Tag) << (Empty ^ ""))
                       << _(DocumentEnd);
          },

        In(TagValue) *
            (T(Tag)
             << (T(TagPrefix)[TagPrefix] *
                 T(VerbatimTag, ShorthandTag, NonSpecificTag)[TagName])) >>
          [](Match& _) {
            return Seq << _(TagPrefix) << (TagName ^ _(TagName));
          },

        In(TagValue) *
            (T(TagPrefix, "!!")[TagPrefix] * T(TagName, "str")[TagName] *
             T(Null)) >>
          [](Match& _) {
            return Seq << _(TagPrefix) << _(TagName) << (Empty ^ "");
          },

        // errors

        In(FlowMapping) * T(FlowMappingStart)[FlowMappingStart] >>
          [](Match& _) {
            return err(
              _(FlowMappingStart), "Flow mapping without closing brace");
          },

        In(FlowSequence) * T(FlowSequenceStart)[FlowSequenceStart] >>
          [](Match& _) {
            return err(
              _(FlowSequenceStart), "Flow sequence without closing bracket");
          },

        In(KeyGroup, ValueGroup, DocumentGroup, FlowGroup) *
            (T(Anchor)[Anchor] * End) >>
          [](Match& _) { return err(_(Anchor), "Invalid anchor"); },

        In(DocumentGroup) * (T(Tag)[Tag] * T(DocumentStart)[DocumentStart]) >>
          [](Match& _) {
            return Seq << err(_(Tag), "Invalid tag") << _(DocumentStart);
          },
      }};

    return attributes;
  }

  PassDef structure()
  {
    PassDef structure = {
      "structure",
      wf_structure,
      dir::bottomup,
      {
        In(Stream) * T(DocumentEnd)[DocumentEnd] >>
          [](Match&) -> Node { return nullptr; },

        In(DocumentGroup) * (Start * ValueToken[Value]) >>
          [](Match& _) { return Seq << (DocumentStart ^ "") << _(Value); },

        In(DocumentGroup) * (T(DocumentStart)[DocumentStart] * End) >>
          [](Match& _) {
            return Seq << _(DocumentStart) << (Empty ^ "")
                       << (DocumentEnd ^ "");
          },

        In(DocumentGroup) * (T(DocumentStart)[Lhs] * T(DocumentEnd)[Rhs]) >>
          [](Match& _) { return Seq << _(Lhs) << (Empty ^ "") << _(Rhs); },

        In(DocumentGroup) * (ValueToken[Value] * End) >>
          [](Match& _) { return Seq << _(Value) << (DocumentEnd ^ ""); },

        In(Document) *
            (T(DocumentGroup)
             << (T(DocumentStart)[DocumentStart] * ValueToken[Value] *
                 T(DocumentEnd)[DocumentEnd] * End)) >>
          [](Match& _) {
            return Seq << _(DocumentStart) << _(Value) << _(DocumentEnd);
          },

        In(FlowSequenceItem) * (T(FlowGroup) << (Any[Value] * End)) >>
          [](Match& _) { return _(Value); },

        In(FlowMappingItem) * (T(FlowGroup) << (Any[Value] * End)) >>
          [](Match& _) { return Seq << _(Value); },

        In(SequenceItem) * (T(ValueGroup) << (Any[Value] * End)) >>
          [](Match& _) { return _(Value); },

        In(MappingItem) * (T(KeyGroup, ValueGroup) << (Any[Value] * End)) >>
          [](Match& _) { return _(Value); },

        // Errors
        In(Document) * (ValueToken * ValueToken[Value]) >>
          [](Match& _) { return err(_(Value), "Invalid document"); },

        In(KeyGroup, ValueGroup) * (Any * Any[Value]) >>
          [](Match& _) { return err(_(Value), "More than one value"); },

        In(
          Document,
          SequenceItem,
          MappingItem,
          FlowSequenceItem,
          FlowMappingItem) *
            T(Tag)[Tag] >>
          [](Match& _) { return err(_(Tag), "Invalid tag"); },

        In(
          Document,
          SequenceItem,
          MappingItem,
          FlowSequenceItem,
          FlowMappingItem) *
            T(Anchor)[Anchor] >>
          [](Match& _) { return err(_(Anchor), "Invalid anchor"); },

        In(AnchorValue) * (T(Anchor) * T(Anchor, Tag)[Value]) >>
          [](Match& _) { return err(_(Value), "Invalid anchor"); },

        In(TagValue) * (T(TagPrefix) * T(TagName) * T(Anchor, Tag)[Value]) >>
          [](Match& _) { return err(_(Value), "Invalid tag"); },

      }};

    structure.post([](Node n) {
      return invalid_tokens(
        n,
        {
          {DocumentGroup, "Invalid document"},
          {FlowGroup, "Invalid flow entity"},
          {KeyGroup, "Invalid mapping key"},
          {ValueGroup, "Invalid mapping value"},
        });
    });

    return structure;
  }

  PassDef tags()
  {
    PassDef pass = {
      "tags",
      wf_tags,
      dir::bottomup,
      {
        In(Sequence) * T(SequenceItem)[SequenceItem] >>
          [](Match& _) { return _(SequenceItem)->front(); },

        In(FlowSequence) * T(FlowSequenceItem)[FlowSequenceItem] >>
          [](Match& _) { return _(FlowSequenceItem)->front(); },

        In(TagValue) * T(TagPrefix)[TagPrefix]([](auto& n) {
          return n.front()->lookup().empty();
        }) >>
          [](Match& _) { return err(_(TagPrefix), "Invalid tag prefix"); },
      }};

    return pass;
  }

  PassDef quotes()
  {
    PassDef quotes =
      {"quotes",
       wf_quotes,
       dir::bottomup | dir::once,
       {
         (T(DoubleQuote)[DoubleQuote] << End) >>
           [](Match& _) {
             std::size_t indent = indent_of(_(DoubleQuote)->parent());
             if (_(DoubleQuote)->parent() != Document)
             {
               indent += 1;
             }
             Node quote = _(DoubleQuote);
             Nodes lines = to_lines(quote->location(), indent);
             std::string error_message = contains_invalid_elements(lines);
             if (!error_message.empty())
             {
               return err(quote, error_message);
             }
             return quote << lines;
           },

         (T(SingleQuote)[SingleQuote] << End) >>
           [](Match& _) {
             std::size_t indent = indent_of(_(SingleQuote)->parent());
             if (_(SingleQuote)->parent() != Document)
             {
               indent += 1;
             }
             return _(SingleQuote)
               << to_lines(_(SingleQuote)->location(), indent);
           },

         In(Literal, Folded) *
             (T(BlockGroup) << (T(BlockLine)++[BlockLine] * End)) >>
           [](Match& _) {
             std::size_t indent = detect_indent(_[BlockLine]);
             return cleanup_block(_[BlockLine], indent, ChompIndicator ^ "");
           },

         In(Literal, Folded) *
             (T(BlockGroup)
              << (T(IndentIndicator)[IndentIndicator] *
                  T(ChompIndicator)[ChompIndicator] *
                  T(BlockLine)++[BlockLine] * End)) >>
           [](Match& _) {
             std::size_t indent = indent_of(_(IndentIndicator)->parent());
             std::size_t relative_indent =
               _(IndentIndicator)->location().view()[0] - '0';
             indent += relative_indent;
             return cleanup_block(_[BlockLine], indent, _(ChompIndicator));
           },

         In(Literal, Folded) *
             (T(BlockGroup)
              << (T(IndentIndicator)[IndentIndicator] *
                  T(BlockLine)++[BlockLine] * End)) >>
           [](Match& _) {
             std::size_t indent = indent_of(_(IndentIndicator)->parent());
             std::size_t relative_indent =
               _(IndentIndicator)->location().view()[0] - '0';
             indent += relative_indent;
             return cleanup_block(_[BlockLine], indent, ChompIndicator ^ "");
           },

         In(Literal, Folded) *
             (T(BlockGroup)
              << (T(ChompIndicator)[ChompIndicator] *
                  T(BlockLine)++[BlockLine] * End)) >>
           [](Match& _) {
             std::size_t indent = detect_indent(_[BlockLine]);
             return cleanup_block(_[BlockLine], indent, _(ChompIndicator));
           },

       }};

    quotes.post([](Node n) {
      return invalid_tokens(n, {{BlockGroup, "Invalid block scalar"}});
    });

    return quotes;
  }

  PassDef anchors()
  {
    return {
      "anchors",
      wf_anchors,
      dir::bottomup,
      {
        In(SingleQuote, DoubleQuote) *
            (T(BlockLine, "")[Lhs] * T(BlockLine, "")[Rhs]) >>
          [](Match&) { return BlockLine ^ " "; },

        In(SingleQuote, DoubleQuote) *
            (T(EmptyLine)[Lhs] * T(BlockLine, "")[Rhs]) >>
          [](Match& _) { return _(Lhs); },

        In(AnchorValue) * T(AnchorValue)[AnchorValue] >>
          [](Match& _) {
            return err(_(AnchorValue), "One value cannot have two anchors");
          },

        In(AnchorValue) * T(Anchor, R"(&.*|.*[ \t])")[Anchor] >>
          [](Match& _) {
            Location loc = _(Anchor)->location();
            auto view = loc.view();
            std::size_t start = view.front() == '&' ? 1 : 0;
            std::size_t end = view.find_last_not_of(" \t\r\n");
            loc.pos += start;
            loc.len = end - start + 1;
            return Anchor ^ loc;
          },

        T(Alias, R"(\*.*)")[Alias] >>
          [](Match& _) {
            Location loc = _(Alias)->location();
            loc.pos += 1;
            loc.len -= 1;
            while (is_space(loc.view().back()))
            {
              loc.len -= 1;
            }

            return Alias ^ loc;
          },

        // errors

        In(AnchorValue) * T(Alias)[Alias] >>
          [](Match& _) { return err(_(Alias), "Anchor plus alias"); },

        In(FlowMapping, Mapping) *
            (T(MappingItem)
             << (T(DoubleQuote)[Key] << (T(BlockLine) * T(BlockLine)))) >>
          [](Match& _) { return err(_(Key), "Invalid mapping key"); },

        In(FlowMapping, Mapping) *
            (T(MappingItem)
             << (T(SingleQuote)[Key] << (T(BlockLine) * T(BlockLine)))) >>
          [](Match& _) { return err(_(Key), "Invalid mapping key"); },

        In(Mapping) *
            (T(MappingItem) * T(MappingItem)[MappingItem])(
              [](auto& n) { return same_line(n.front(), n.back()); }) >>
          [](Match& _) {
            return err(
              _(MappingItem),
              "Invalid mapping key on same line as previous key");
          },

        In(Mapping, FlowMapping) * ValueToken[Value] >>
          [](Match& _) { return err(_(Value), "Invalid mapping value"); },

        In(FlowSequence) * (Start * T(FlowEmpty)[FlowEmpty]) >>
          [](Match& _) {
            return err(
              _(FlowEmpty),
              "Flow sequence with invalid comma at the beginning");
          },
      }};
  }
}

namespace trieste
{
  namespace yaml
  {
    Reader reader()
    {
      return {
        "yaml",
        {
          groups(),
          values(),
          flow(),
          lines(),
          indents(),
          colgroups(),
          items(),
          complex(),
          blocks(),
          collections(),
          attributes(),
          structure(),
          tags(),
          quotes(),
          anchors(),
        },
        parser()};
    }
  }
}

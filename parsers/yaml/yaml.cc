#include "yaml.h"

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

  bool is_in(NodeDef* node, Token parent)
  {
    if (node == Top)
    {
      return false;
    }

    if (node == parent)
    {
      return true;
    }

    return is_in(node->parent(), parent);
  }

  struct ValuePattern
  {
    ValuePattern(const std::string& pattern, Token type)
    : regex(pattern), type(type)
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

    for (auto& child : *node)
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
    for (auto it = lines.first; it != lines.second; ++it)
    {
      auto view = (*it)->location().view();
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

      if (s.rfind("...") == s.size() - 3)
      {
        result[i] = err(result[i], "Scalar contains '...'");
      }
    }

    return result;
  }

  std::string contains_invalid_elements(Nodes lines)
  {
    for (Node line : lines)
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
    if (nodes.first == nodes.second)
    {
      return {nullptr, nullptr};
    }

    Node indent = nodes.first[0];
    Node chomp = nullptr;
    if (std::distance(nodes.first, nodes.second) > 1)
    {
      chomp = nodes.first[1];
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

    for (auto& child : *node)
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

    Nodes lines(range.first, range.second);

    Nodes::iterator end = lines.end();
    for (auto it = lines.begin(); it != lines.end(); ++it)
    {
      Node n = *it;
      auto view = n->location().view();
      if (view.empty())
      {
        continue;
      }

      auto pos = view.find_first_not_of(" \t");
      if (pos == std::string::npos)
      {
        continue;
      }

      if (view[pos] != '#')
      {
        if (view.size() >= indent)
        {
          end = lines.end();
          continue;
        }

        return err(range, "Invalid block scalar");
      }
      else if (pos < indent)
      {
        end = it;
      }
    }

    if (end != lines.end())
    {
      lines.erase(end, lines.end());
    }

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
    for (auto& group : *n)
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

  std::size_t invalid_tokens(
    Node n, std::initializer_list<Token> tokens, const std::string& message)
  {
    std::size_t changes = 0;
    for (auto child : *n)
    {
      if (child->in(tokens))
      {
        n->replace(child, err(child, message));
        changes += 1;
      }
      else
      {
        changes += invalid_tokens(child, tokens, message);
      }
    }

    return changes;
  }
}

namespace trieste::yaml
{
  const auto FlowTokens =
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
      FlowSequence);

  const auto LineTokens =
    FlowTokens / T(Comment, Colon, Key, Placeholder, MaybeDirective);
  const auto AnchorTag = T(Anchor, Tag);
  const auto inline Indents =
    T(Indent, BlockIndent, SequenceIndent, MappingIndent, ManualIndent);
  const auto inline IndentChomp = T(IndentIndicator, ChompIndicator);
  const auto inline BasicTokens = T(Value, Int, Float, Hex, True, False, Null);
  const auto DirectiveTokens =
    T(VersionDirective, TagDirective, UnknownDirective);
  const auto ValueTokens =
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
      return invalid_tokens(n, {Group, File}, "Syntax error");
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
        In(DocumentGroup) *
            (Start * ~T(Whitespace) * T(Comment) * T(NewLine)) >>
          [](Match&) -> Node { return nullptr; },

        In(DocumentGroup) * T(DocumentStart)[DocumentStart] * ~T(NewLine) *
            ~T(Whitespace) * T(Comment) * T(NewLine) >>
          [](Match& _) { return _(DocumentStart); },

        In(StreamGroup) *
            (DirectiveTokens[Head] * DirectiveTokens++[Tail] *
             (T(Document)
              << (T(Directives)[Directives] * T(DocumentGroup)[Group]))) >>
          [](Match& _) {
            Node dirs = _(Directives);
            dirs << _(Head) << _[Tail];
            bool version = false;
            for (auto dir : *dirs)
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

        In(TagGroup) * T(VerbatimTag)[VerbatimTag]([](auto& n) {
          auto view = n.first[0]->location().view();
          return view.find_first_of("{}") != std::string::npos;
        }) >>
          [](Match& _) { return err(_(VerbatimTag), "Invalid tag"); },

        In(TagGroup) * T(ShorthandTag)[ShorthandTag]([](auto& n) {
          auto view = n.first[0]->location().view();
          return view.find_first_of("{}[],") != std::string::npos;
        }) >>
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

        // errors

        In(StreamGroup) * (DirectiveTokens[Value] * End) >>
          [](Match& _) {
            return err(_(Value), "Directive by itself with no document");
          },

        In(DocumentGroup) *
            (T(MaybeDirective)[MaybeDirective] * ~T(NewLine) *
             End)([](auto& n) {
              Node dir = n.first[0];
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
            (DirectiveTokens / T(Document, TagHandle, Stream))[Value] >>
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
        n, {StreamGroup, TagDirectiveGroup, TagGroup}, "Invalid tag");
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
            (T(Value)[Value] * T(Comma, FlowSequenceEnd))([](auto& n) {
              Location loc = n.first[0]->location();
              return loc.view() == "-";
            }) >>
          [](Match& _) {
            return err(_(Value), "Plain dashes in flow sequence");
          },

        In(FlowMapping, FlowSequence) *
            (FlowTokens[Lhs] * T(Comment)++ * T(Value)[Rhs])([](auto& n) {
              Node rhs = *(n.second - 1);
              Location loc = rhs->location();
              return loc.view().front() == ':';
            }) >>
          [](Match& _) {
            Location loc = _(Rhs)->location();
            Location colon = loc;
            colon.len = 1;
            loc.pos += 1;
            loc.len -= 1;
            return Seq << _(Lhs) << (Colon ^ colon) << (Value ^ loc);
          },

        In(FlowMapping, FlowSequence) * (T(Whitespace, NewLine)) >>
          [](Match&) -> Node { return nullptr; },

        In(FlowSequence) *
            (LineTokens[Value] * T(FlowSequenceEnd)[FlowSequenceEnd]) >>
          [](Match& _) {
            return Seq << _(Value) << (Comma ^ ",") << _(FlowSequenceEnd);
          },

        In(FlowMapping) *
            (LineTokens[Value] * T(FlowMappingEnd)[FlowMappingEnd]) >>
          [](Match& _) {
            return Seq << _(Value) << (Comma ^ ",") << _(FlowMappingEnd);
          },

        In(FlowMapping) * (LineTokens[Head] * LineTokens++[Tail] * T(Comma)) >>
          [](Match& _) { return FlowKeyValue << _(Head) << _[Tail]; },

        In(FlowSequence) *
            (T(Key) * FlowTokens++[Key] * T(Colon) * FlowTokens++[Value] *
             T(Comment)++ * T(Comma)) >>
          [](Match& _) {
            return FlowSequenceItem
              << (FlowGroup
                  << (FlowMapping
                      << (FlowMappingItem << (FlowGroup << _[Key])
                                          << (FlowGroup << _[Value]))));
          },

        In(FlowSequence) *
            (T(Comment)++ * FlowTokens[Head] * FlowTokens++[Tail] *
             T(Colon)[Colon] * FlowTokens++[Value] * T(Comment)++ * T(Comma)) >>
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
            (T(Comment)++ * T(Colon) * FlowTokens++[Value] * T(Comment)++ *
             T(Comma)) >>
          [](Match& _) {
            return FlowSequenceItem
              << (FlowGroup
                  << (FlowMapping
                      << (FlowMappingItem << (FlowGroup << Empty)
                                          << (FlowGroup << _[Value]))));
          },

        In(FlowSequence) *
            (T(Comment)++ * FlowTokens[Head] * FlowTokens++[Tail] *
             T(Comment)++ * T(Comma)) >>
          [](Match& _) {
            return FlowSequenceItem << (FlowGroup << _(Head) << _[Tail]);
          },

        In(FlowMapping) *
            (T(FlowKeyValue)
             << (T(Key) * FlowTokens++[Key] * T(Colon) * FlowTokens++[Value] *
                 End)) >>
          [](Match& _) {
            return FlowMappingItem << (FlowGroup << _[Key])
                                   << (FlowGroup << _[Value]);
          },

        In(FlowMapping) *
            (T(FlowKeyValue)
             << (FlowTokens++[Key] * T(Colon) * FlowTokens++[Value] * End)) >>
          [](Match& _) {
            auto value = FlowGroup << _[Value];
            if (value->empty())
            {
              value = FlowGroup << (Null ^ "null");
            }
            return FlowMappingItem << (FlowGroup << _[Key]) << value;
          },

        In(FlowMapping) * (T(FlowKeyValue) << (FlowTokens++[Key] * End)) >>
          [](Match& _) {
            return FlowMappingItem << (FlowGroup << _[Key])
                                   << (FlowGroup << (Null ^ "null"));
          },

        In(FlowMapping) * (T(FlowKeyValue) << (T(Key) * End)) >>
          [](Match&) {
            return FlowMappingItem << (FlowGroup << (Empty ^ ""))
                                   << (FlowGroup << (Null ^ "null"));
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
            return Seq << (FlowGroup << (Null ^ "null")) << _(Value);
          },

        In(FlowMappingItem) *
            ((T(FlowGroup)[Key] << Any) * (T(FlowGroup) << End)) >>
          [](Match& _) {
            return Seq << _(Key) << (FlowGroup << (Null ^ "null"));
          },

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
            (T(DocumentStart) * T(Placeholder) * AnchorTag++ * FlowTokens *
             T(Colon)[Colon])([](auto& n) {
              Node docstart = n.first[0];
              Node colon = *(n.second - 1);
              return same_line(docstart, colon);
            }) >>
          [](Match& _) {
            return err(_(Colon), "Invalid mapping on document start line");
          },

        In(DocumentGroup) *
            (T(Colon) * T(NewLine) * T(Anchor)[Anchor] * T(NewLine) *
             T(Hyphen))([](auto& n) {
              auto anchor = n.first[2]->location().linecol().second;
              auto sequence = n.first[4]->location().linecol().second;
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
             (BasicTokens / AnchorTag)[Value] * ~T(Whitespace) * ~T(Comment)) >>
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
            (LineTokens[Head] * LineTokens++[Tail] * T(NewLine)) >>
          [](Match& _) { return Line << _(Head) << _[Tail]; },

        In(DocumentGroup) *
            (LineTokens[Head] * LineTokens++[Tail] * End)([](auto& n) {
              Node head = n.first[0];
              return head->parent()->parent() == Document;
            }) >>
          [](Match& _) { return Line << _(Head) << _[Tail]; },

        In(DocumentGroup) *
            (LineTokens[Head] * LineTokens++[Tail] *
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
                 LineTokens[Key] * ~T(Whitespace) * T(Colon)[Colon] *
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
                 LineTokens[Key] * ~T(Whitespace) * T(Colon)[Colon] *
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

        In(DocumentGroup) * (T(Line)[Line] << (FlowTokens++ * T(Colon))) >>
          [](Match& _) { return MappingIndent << _(Line); },

        In(DocumentGroup) *
            (T(Line)[Line] << (T(Placeholder) * FlowTokens++ * T(Colon))) >>
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
              << (T(Line) * T(BlockStart))) *
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
            (LineTokens[Value] * LineTokens++ *
             T(DocumentStart)[DocumentStart]) >>
          [](Match& _) {
            return Seq << err(_(Value), "Syntax error") << _(DocumentStart);
          },

        In(DocumentGroup) *
            (T(DocumentEnd)[DocumentEnd] * LineTokens[Value] * LineTokens++) >>
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

        In(DocumentGroup, Indent, MappingIndent, SequenceIndent) *
            (Indents[Indent] * T(EmptyLine, WhitespaceLine)[Line]) >>
          [](Match& _) { return _(Indent)->type() << *_[Indent] << _(Line); },

        In(DocumentGroup, Indent, MappingIndent, SequenceIndent) *
            (Indents[Lhs] * Indents[Rhs])(
              [](auto& n) { return less_indented(n.first[0], n.first[1]); }) >>
          [](Match& _) { return _(Lhs)->type() << *_[Lhs] << _(Rhs); },

        In(DocumentGroup, Indent, MappingIndent, SequenceIndent) *
            (Indents[Lhs] * Indents[Rhs])(
              [](auto& n) { return same_indent(n.first[0], n.first[1]); }) >>
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
            ((T(Line)[Line]
              << (~T(Whitespace) * T(Hyphen) * AnchorTag++ *
                  (T(Literal, Folded, Value)))) *
             (T(MappingIndent, SequenceIndent))[Indent]) >>
          [](Match& _) {
            return Seq << _(Line) << (BlockIndent << *_[Indent]);
          },

        In(MappingIndent) *
            (T(Indent)
             << ((T(Line) << (~T(Whitespace) * T(Comment))) *
                 T(MappingIndent, SequenceIndent)[Indent] * End)) >>
          [](Match& _) { return _(Indent); },

        In(MappingIndent) *
            ((T(Line)
              << (~T(Whitespace)[Whitespace] * T(Key)[Key] *
                  AnchorTag++[Anchor] * FlowTokens[Lhs] * Any++[Tail])) *
             (T(Line)
              << (~T(Whitespace)[Placeholder] * T(Colon)[Colon] *
                  AnchorTag++[Tag] * FlowTokens[Rhs] * Any++[Extra]))) >>
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
             << ((T(Line)[Line] << (~T(Whitespace) * FlowTokens++ * T(Colon))) *
                 End)) >>
          [](Match& _) { return MappingIndent << _(Line); },

        In(MappingIndent) *
            (T(Indent)
             << ((T(Line)[Line] << (~T(Whitespace) * T(Hyphen))) * End)) >>
          [](Match& _) { return SequenceIndent << _(Line); },

        // errors

        In(Line) * (LineTokens * T(Colon) * T(Hyphen)[Hyphen]) >>
          [](Match& _) {
            return err(_(Hyphen), "Sequence on same Line as Mapping Key");
          },

        In(Line) * (T(Hyphen) * LineTokens * T(Hyphen)[Hyphen]) >>
          [](Match& _) {
            return err(
              _(Hyphen), "Invalid sequence item on same Line as previous item");
          },

        In(SequenceIndent) * T(Indent, BlockIndent)[Indent]([](auto& n) {
          Node indent = n.first[0];
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

        In(Line) * T(Comment)[Comment]([](auto& n) {
          Node comment = n.first[0];
          if (!is_in(comment->parent(), MappingIndent))
          {
            return false;
          }

          auto view = comment->location().view();
          return view.find(": ") != std::string::npos;
        }) >>
          [](Match& _) -> Node {
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

        In(Line) * (T(Colon) * ValueTokens++[Value] * T(Colon)[Colon]) >>
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
             Indents[Value]) >>
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
             T(BlockStart)[BlockStart] * Indents[Value]) >>
          [](Match& _) {
            return SequenceItem
              << (ValueGroup << _[Anchor] << _(BlockStart) << _(Value));
          },

        In(SequenceGroup) *
            (T(Line)
             << (~T(Whitespace) * T(Hyphen) * AnchorTag++[Anchor] *
                 ~ValueTokens[Value] * ~T(Whitespace) * ~T(Comment) * End)) >>
          [](Match& _) {
            Node value = _(Value);
            if (value == nullptr)
            {
              value = Null ^ "null";
            }
            return SequenceItem << (ValueGroup << _[Anchor] << value);
          },

        In(SequenceGroup) *
            (T(Line) << (~T(Whitespace) * T(Hyphen) * T(Tag)[Tag] * End)) >>
          [](Match& _) {
            return SequenceItem << (ValueGroup << _(Tag) << (Null ^ "null"));
          },

        In(MappingGroup) *
            (T(Line) << (T(Whitespace)[Whitespace]))([](auto& n) {
              Node ws = n.first[0]->front();
              auto view = ws->location().view();
              return view.find('\t') != std::string::npos;
            }) >>
          [](Match& _) {
            return err(_(Whitespace), "Tab character in indentation");
          },

        In(MappingGroup) *
            ((T(Line)
              << (~T(Whitespace) * AnchorTag++[Lhs] * ValueTokens[Key] *
                  ~T(Whitespace) * T(Colon) * AnchorTag++[Rhs])) *
             T(BlockStart)[BlockStart] * Indents[Value]) >>
          [](Match& _) {
            return MappingItem
              << (KeyGroup << _[Lhs] << _(Key))
              << (ValueGroup << _[Rhs] << _(BlockStart) << _(Value));
          },

        In(MappingGroup) *
            ((T(Line)
              << (~T(Whitespace) * AnchorTag++[Lhs] * ValueTokens[Key] *
                  ~T(Whitespace) * T(Colon) * AnchorTag++[Rhs] * Any++[Tail])) *
             T(WhitespaceLine, EmptyLine)++[Whitespace] * Indents[Value]) >>
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
             << (~T(Whitespace) * AnchorTag++[Lhs] * ValueTokens[Key] *
                 ~T(Whitespace) * T(Colon) * AnchorTag++[Rhs] *
                 ValueTokens[Head] * Any++[Tail])) >>
          [](Match& _) {
            NodeRange tail = _[Tail];
            if (tail.first != tail.second)
            {
              for (auto it = tail.first; it != tail.second; ++it)
              {
                if (!all_comments(*it))
                {
                  return err(*it, "Trailing content on mapping item");
                }
              }
            }
            return MappingItem << (KeyGroup << _[Lhs] << _(Key))
                               << (ValueGroup << _[Rhs] << _(Head));
          },

        In(MappingGroup) *
            (T(Line)
             << (~T(Whitespace) * AnchorTag++[Lhs] * T(Colon) *
                 AnchorTag++[Rhs] * ~ValueTokens[Value] * ~T(Whitespace) *
                 ~T(Comment) * End)) >>
          [](Match& _) {
            Node value = _(Value);
            if (value == nullptr)
            {
              value = Null ^ "null";
            }
            return MappingItem << (KeyGroup << _[Lhs] << Empty)
                               << (ValueGroup << _[Rhs] << value);
          },

        In(MappingGroup) * T(Line)
            << (~T(Whitespace) * AnchorTag++[Lhs] * ValueTokens[Key] *
                T(Colon) * AnchorTag++[Rhs] * End) >>
          [](Match& _) {
            return MappingItem << (KeyGroup << _[Lhs] << _(Key))
                               << (ValueGroup << _[Rhs] << (Null ^ "null"));
          },

        In(MappingGroup) * T(Line)
            << (~T(Whitespace) * T(Tag)[Lhs] * T(Colon) * T(Tag)[Rhs] * End) >>
          [](Match& _) {
            return MappingItem << (KeyGroup << _(Lhs) << Empty)
                               << (ValueGroup << _(Rhs) << (Null ^ "null"));
          },

        In(MappingGroup) *
            ((T(ComplexKey, ComplexValue))[Lhs] * Indents[Indent]) >>
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

        In(ComplexKey) * (T(Hyphen) * ValueTokens[Value]) >>
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
          [](Match& _) { return Seq << _(Anchor) << (Null ^ "null"); },

        In(DocumentGroup, KeyGroup, ValueGroup) * ((T(Indent, Line)) << End) >>
          [](Match&) -> Node { return nullptr; },

        In(MappingGroup, SequenceGroup) *
            (T(Line) << (~T(Whitespace) * T(Comment))) >>
          [](Match&) -> Node { return nullptr; },

        In(MappingGroup) * (T(ComplexValue) << End) >>
          [](Match&) { return ComplexValue << (Null ^ "null"); },

        In(ComplexKey, ComplexValue) *
            ((T(Indent) << (T(Line) << (~T(Whitespace) * T(Comment)))) * End) >>
          [](Match&) -> Node { return nullptr; },

        In(SequenceGroup, MappingGroup) * T(WhitespaceLine, EmptyLine) >>
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

        In(BlockStart) * BasicTokens[Value] >> [](Match& _) -> Node {
          return err(_(Value), "Invalid text after block scalar indicator");
        },

        In(SequenceItem) *
            (T(ValueGroup) << (T(FlowMapping, FlowSequence))[Flow])(
              [](auto& n) {
                Node group = n.first[0];
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
                Node key = n.first[0];
                Node value = n.first[1];
                Node flow = value->front();
                std::size_t item_indent = min_indent(key);
                std::size_t flow_indent = min_indent(flow);
                return flow_indent <= item_indent;
              }) >>
          [](Match& _) { return err(_(Flow), "Wrong indented flow"); },

        In(MappingItem) *
            (T(KeyGroup) << (T(FlowMapping, FlowSequence))[Flow])([](auto& n) {
              Node key = n.first[0];
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
              Node key = n.first[0];
              Node value = n.first[1];
              std::size_t key_indent = min_indent(key);
              std::size_t anchortag_indent = std::string::npos;
              for (auto child : *value)
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
        n, {MappingGroup, SequenceGroup}, "Invalid mapping/sequence");
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
              << (~T(Whitespace) * AnchorTag++[Lhs] * ValueTokens[Key] *
                  T(Colon) * AnchorTag++[Rhs])) *
             T(SequenceItem)++[Value]) >>
          [](Match& _) {
            return MappingItem
              << (KeyGroup << _[Lhs] << _(Key))
              << (ValueGroup << _[Rhs] << (SequenceIndent << _[Value]));
          },

        In(Document) * (T(DocumentGroup)[Group] << T(Indent))([](auto& n) {
          Node g = n.first[0];
          return all_comments(g->front());
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
                               << (ValueGroup << (Null ^ "null"));
          },

        In(MappingIndent) * T(ComplexValue)[Value] >>
          [](Match& _) {
            return MappingItem << (KeyGroup << Empty)
                               << (ValueGroup << *_[Value]);
          },

        In(MappingIndent, SequenceIndent) * T(Indent)[Indent]([](auto& n) {
          return all_comments(n.first[0]);
        }) >>
          [](Match&) -> Node { return nullptr; },

        T(Indent) << End >> [](Match&) -> Node { return nullptr; },
      }};

    complex.post([](Node n) {
      return invalid_tokens(n, {Key, Colon}, "Syntax error");
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
                      BasicTokens++[Line] * End)) *
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
              << (~T(Whitespace) * AnchorTag++[Anchor] * BasicTokens++[Line] *
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
              << (~T(Whitespace) * ValueTokens[Value] * ~T(Whitespace) *
                  T(Comment))) *
             End) >>
          [](Match& _) { return Line << _[Value]; },

        In(Plain) * (T(Line)[Line] << Any) >>
          [](Match& _) {
            Node line = _(Line);
            if (line->front()->type() == Whitespace)
            {
              line->erase(line->begin(), line->begin() + 1);
            }
            if (line->back()->type() == Whitespace)
            {
              line->pop_back();
            }
            if (line->back() == Comment)
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

        In(BlockGroup) * T(BlockLine)[BlockLine]([](auto& n) {
          Location loc = n.first[0]->location();
          if (loc.len == 0)
          {
            return false;
          }

          return loc.view().find('\n') != std::string::npos;
        }) >>
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

        In(Plain) * T(BlockLine)[BlockLine]([](auto& n) {
          auto loc = n.first[0]->location();
          if (loc.len == 0)
          {
            return false;
          }
          char c = loc.view().back();
          return c == ' ' || c == '\t';
        }) >>
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
      }};

    blocks.post([](Node n) {
      std::size_t changes = invalid_tokens(
        n, {Indent, ManualIndent, BlockIndent}, "Invalid indent");
      changes += invalid_tokens(
        n,
        {Colon, Hyphen, Line, MaybeDirective, BlockStart, Placeholder},
        "Syntax error");
      return changes;
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
          [](Match& _) { return _(Value) << (Null ^ "null"); },

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
            (T(Anchor)[Anchor] * ValueTokens[Value]) >>
          [](Match& _) { return AnchorValue << _(Anchor) << _(Value); },

        In(KeyGroup, ValueGroup, DocumentGroup, FlowGroup) *
            (T(Tag)[Tag] * ValueTokens[Value]) >>
          [](Match& _) { return TagValue << _(Tag) << _(Value); },

        In(KeyGroup, ValueGroup, DocumentGroup, FlowGroup) *
            (T(Tag)[Tag] * End) >>
          [](Match& _) { return TagValue << _(Tag) << (Value ^ ""); },

        In(DocumentStart) * (T(Tag)[Tag] * T(DocumentEnd)[DocumentEnd]) >>
          [](Match& _) {
            return Seq << (TagValue << _(Tag) << (Value ^ ""))
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
            (T(TagPrefix)[TagPrefix] * T(TagName)[TagName] *
             T(Null))([](auto& n) {
              auto pre = n.first[0]->location().view();
              auto tag = n.first[1]->location().view();
              return pre == "!!" && tag == "str";
            }) >>
          [](Match& _) {
            return Seq << _(TagPrefix) << _(TagName) << (Value ^ "");
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

        In(DocumentGroup) * (Start * ValueTokens[Value]) >>
          [](Match& _) { return Seq << (DocumentStart ^ "") << _(Value); },

        In(DocumentGroup) * (T(DocumentStart)[DocumentStart] * End) >>
          [](Match& _) {
            return Seq << _(DocumentStart) << (Null ^ "null")
                       << (DocumentEnd ^ "");
          },

        In(DocumentGroup) * (T(DocumentStart)[Lhs] * T(DocumentEnd)[Rhs]) >>
          [](Match& _) { return Seq << _(Lhs) << (Null ^ "null") << _(Rhs); },

        In(DocumentGroup) * (ValueTokens[Value] * End) >>
          [](Match& _) { return Seq << _(Value) << (DocumentEnd ^ ""); },

        In(Document) *
            (T(DocumentGroup)
             << (T(DocumentStart)[DocumentStart] * ValueTokens[Value] *
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
        In(Document) * (ValueTokens * ValueTokens[Value]) >>
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

      }};

    structure.post([](Node n) {
      return invalid_tokens(
        n, {DocumentGroup, KeyGroup, ValueGroup, FlowGroup}, "Syntax error");
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
          Node pre = n.first[0];
          return pre->lookup().empty();
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

    quotes.post(
      [](Node n) { return invalid_tokens(n, {BlockGroup}, "Syntax error"); });

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
            (T(BlockLine)[Lhs] * T(BlockLine)[Rhs])([](auto& n) {
              return n.first[0]->location().len == 0 &&
                n.first[1]->location().len == 0;
            }) >>
          [](Match& _) { return _(Lhs); },

        In(SingleQuote, DoubleQuote) *
            (T(EmptyLine)[Lhs] * T(BlockLine)[Rhs])(
              [](auto& n) { return n.first[1]->location().len == 0; }) >>
          [](Match& _) { return _(Lhs); },

        In(AnchorValue) * T(AnchorValue)[AnchorValue] >>
          [](Match& _) {
            return err(_(AnchorValue), "One value cannot have two anchors");
          },

        In(AnchorValue) * T(Anchor)[Anchor]([](auto& n) {
          Node anchor = n.first[0];
          auto view = anchor->location().view();
          return view.front() == '&' || std::isspace(view.back());
        }) >>
          [](Match& _) {
            Location loc = _(Anchor)->location();
            auto view = loc.view();
            std::size_t start = view.front() == '&' ? 1 : 0;
            std::size_t end = view.find_last_not_of(" \t\r\n");
            loc.pos += start;
            loc.len = end - start + 1;
            return Anchor ^ loc;
          },

        T(Alias)[Alias]([](auto& n) {
          Node anchor = n.first[0];
          return anchor->location().view().front() == '*';
        }) >>
          [](Match& _) {
            Location loc = _(Alias)->location();
            loc.pos += 1;
            loc.len -= 1;
            while (std::isspace(loc.view().back()))
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
              [](auto& n) { return same_line(n.first[0], n.first[1]); }) >>
          [](Match& _) {
            return err(
              _(MappingItem),
              "Invalid mapping key on same line as previous key");
          },

        In(Mapping, FlowMapping) * ValueTokens[Value] >>
          [](Match& _) { return err(_(Value), "Invalid mapping value"); },

        In(FlowSequence) * (Start * T(FlowEmpty)[FlowEmpty]) >>
          [](Match& _) {
            return err(
              _(FlowEmpty),
              "Flow sequence with invalid comma at the beginning");
          },
      }};
  }

  std::vector<Pass> passes()
  {
    return {
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
    };
  }
} // namespace trieste::yaml

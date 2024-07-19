#include "internal.h"
#include "trieste/utf8.h"

namespace
{
  using namespace trieste;
  using namespace trieste::yaml;

  Node handle_tag_anchor(std::ostream& os, const Node& node)
  {
    Node anchor = nullptr;
    std::string tag = "";
    Node value = node;
    if (node->type() == AnchorValue)
    {
      anchor = node / Anchor;
      value = node / Value;
    }

    if (value->type() == TagValue)
    {
      std::string handle = "";
      Node prefix_node = value / TagPrefix;
      Node handle_node = prefix_node->lookup()[0];
      if (handle_node != nullptr)
      {
        handle = handle_node->back()->location().view();
      }
      Node name_node = value / TagName;

      value = value / Value;
      auto tagname = unescape_url_chars(name_node->location().view());
      std::ostringstream tags;
      if (
        tagname.size() >= 2 && tagname.front() == '<' && tagname.back() == '>')
      {
        tags << tagname;
      }
      else
      {
        tags << "<" << handle << tagname << ">";
      }
      tag = tags.str();
    }

    if (value->type() == AnchorValue)
    {
      anchor = value / Anchor;
      value = value / Value;
    }

    if (anchor != nullptr)
    {
      os << " &" << anchor->location().view();
    }

    if (!tag.empty())
    {
      os << " " << tag;
    }

    return value;
  }

  bool
  write_event(std::ostream& os, const std::string& newline, const Node& value);

  bool write_value_event(
    std::ostream& os, const std::string& newline, const Node& maybe_value)
  {
    os << "=VAL";
    Node value = handle_tag_anchor(os, maybe_value);

    if (value->type() == Empty)
    {
      os << " :";
    }
    else
    {
      os << " :" << escape_chars(value->location().view(), {'\\'});
    }
    os << newline;

    return false;
  }

  bool write_mapping_event(
    std::ostream& os,
    const std::string& newline,
    const Node& maybe_node,
    bool is_flow)
  {
    os << "+MAP";
    if (is_flow)
    {
      os << " {}";
    }

    Node node = handle_tag_anchor(os, maybe_node);

    os << newline;
    for (const Node& child : *node)
    {
      if (write_event(os, newline, child))
      {
        return true;
      }
    }
    os << "-MAP" << newline;
    return false;
  }

  bool write_sequence_event(
    std::ostream& os,
    const std::string& newline,
    const Node& maybe_node,
    bool is_flow)
  {
    os << "+SEQ";

    if (is_flow)
    {
      os << " []";
    }

    Node node = handle_tag_anchor(os, maybe_node);

    os << newline;
    for (const Node& child : *node)
    {
      if (write_event(os, newline, child))
      {
        return true;
      }
    }
    os << "-SEQ" << newline;
    return false;
  }

  bool write_alias_event(
    std::ostream& os, const std::string& newline, const Node& node)
  {
    os << "=ALI *" << node->location().view() << newline;
    return false;
  }

  bool write_literal_event(
    std::ostream& os, const std::string& newline, const Node& maybe_node)
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " |";
    block_to_string(os, node, true) << newline;
    return false;
  }

  bool write_folded_event(
    std::ostream& os, const std::string& newline, const Node& maybe_node)
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " >";
    block_to_string(os, node, true) << newline;
    return false;
  }

  bool write_plain_event(
    std::ostream& os, const std::string& newline, const Node& maybe_node)
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " :";
    block_to_string(os, node, true) << newline;
    return false;
  }

  bool write_doublequote_event(
    std::ostream& os, const std::string& newline, const Node& maybe_node)
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " \"";
    quote_to_string(os, node, true) << newline;
    return false;
  }

  bool write_singlequote_event(
    std::ostream& os, const std::string& newline, const Node& maybe_node)
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " '";
    quote_to_string(os, node, true) << newline;
    return false;
  }

  Token get_type(const Node& node)
  {
    Node value = node;
    if (node->type() == AnchorValue)
    {
      value = node->back();
    }

    if (value->type() == TagValue)
    {
      value = value->back();
    }

    if (value->type() == AnchorValue)
    {
      value = value->back();
    }

    return value->type();
  }

  bool
  write_event(std::ostream& os, const std::string& newline, const Node& node)
  {
    Token node_type = get_type(node);
    if (node_type.in({Value, Int, Float, Empty, True, False, Hex}))
    {
      return write_value_event(os, newline, node);
    }

    if (node_type == DoubleQuote)
    {
      return write_doublequote_event(os, newline, node);
    }

    if (node_type == SingleQuote)
    {
      return write_singlequote_event(os, newline, node);
    }

    if (node_type == Null)
    {
      os << "=VAL";
      handle_tag_anchor(os, node);
      os << " :" << newline;
      return false;
    }

    if (node_type == Alias)
    {
      return write_alias_event(os, newline, node);
    }

    if (node_type == Literal)
    {
      return write_literal_event(os, newline, node);
    }

    if (node_type == Folded)
    {
      return write_folded_event(os, newline, node);
    }

    if (node_type == Plain)
    {
      return write_plain_event(os, newline, node);
    }

    if (node_type.in({FlowMappingItem, MappingItem}))
    {
      if (write_event(os, newline, node->front()))
      {
        return true;
      }
      return write_event(os, newline, node->back());
    }

    if (node_type.in({Sequence, FlowSequence}))
    {
      return write_sequence_event(os, newline, node, node_type == FlowSequence);
    }

    if (node_type.in({Mapping, FlowMapping}))
    {
      return write_mapping_event(os, newline, node, node_type == FlowMapping);
    }

    if (node_type == Document)
    {
      if (node->size() == 0)
      {
        return false;
      }

      Node start = node / DocumentStart;
      Node value = node / Value;
      Node end = node / DocumentEnd;
      os << "+DOC";
      if (start->location().len > 0)
      {
        os << " " << start->location().view();
      }
      os << newline;
      if (write_event(os, newline, value))
      {
        return true;
      }
      os << "-DOC";
      if (end->location().len > 0)
      {
        os << " " << end->location().view();
      }
      os << newline;
      return false;
    }

    if (node_type == Stream)
    {
      os << "+STR" << newline;
      for (const Node& child : *node->back())
      {
        if (write_event(os, newline, child))
        {
          return true;
        }
      }
      os << "-STR" << newline;
      return false;
    }

    if (node_type == Top)
    {
      return write_event(os, newline, node->front());
    }

    if (node_type.in({TagDirective, VersionDirective, UnknownDirective}))
    {
      return false;
    }

    if (node_type == Error)
    {
      return true;
    }

    if (node_type == Anchor)
    {
      // anchor for empty node
      os << "=VAL";
      handle_tag_anchor(os, node);
      os << " :" << newline;
      return false;
    }

    std::string error =
      "to_event: Unexpected node type: " + std::string(node_type.str());
    throw std::runtime_error(error);
  }

  // clang-format off
  const auto wf_to_event_file =
    yaml::wf
    | (Top <<= File)
    | (File <<= Path * Stream)
    ;
  // clang-format on

  PassDef to_event_file(const std::filesystem::path& path)
  {
    return {
      "to_event_file",
      wf_to_event_file,
      dir::bottomup | dir::once,
      {
        In(Top) * T(Stream)[Stream] >>
          [path](Match& _) {
            return File << (Path ^ path.string()) << _(Stream);
          },
      }};
  }
}

namespace trieste
{
  namespace yaml
  {
    Writer
    event_writer(const std::filesystem::path& path, const std::string& newline)
    {
      return Writer(
        "yaml_event",
        {to_event_file(path)},
        yaml::wf,
        [newline](std::ostream& os, const Node& value) {
          return write_event(os, newline, value);
        });
    }

    std::ostream&
    block_to_string(std::ostream& os, const Node& node, bool raw_quotes)
    {
      std::set<char> escape = {'\\', '\n', '\r', '\t'};
      if (!raw_quotes)
      {
        escape.insert('"');
      }

      if (node == Plain)
      {
        for (std::size_t i = 0; i < node->size() - 1; ++i)
        {
          if (node->at(i) == EmptyLine)
          {
            os << "\\n";
            continue;
          }
          auto current = node->at(i)->location().view();
          auto next = node->at(i + 1)->location().view();
          os << escape_chars(current, escape);
          if (!std::isspace(current.front()) && !std::isspace(next.front()))
          {
            os << " ";
          }
        }
        os << escape_chars(node->back()->location().view(), escape);
        return os;
      }

      if (node->size() == 2)
      {
        return os;
      }

      Node indent_node = node / AbsoluteIndent;
      Node chomp_node = node / ChompIndicator;
      Node lines_node = node / Lines;
      std::string indent_string(indent_node->location().view());
      std::size_t indent = std::stoul(indent_string);
      Chomp chomp = Chomp::Clip;
      if (chomp_node->location().view() == "+")
      {
        chomp = Chomp::Keep;
      }
      else if (chomp_node->location().view() == "-")
      {
        chomp = Chomp::Strip;
      }

      std::vector<std::string_view> lines;
      for (const Node& line_node : *lines_node)
      {
        auto view = line_node->location().view();
        auto maybe_comment = view.find('#');
        if (maybe_comment < indent)
        {
          continue;
        }

        lines.push_back(view);
      }

      if (chomp != Chomp::Keep)
      {
        lines.erase(
          std::find_if(
            lines.rbegin(),
            lines.rend(),
            [indent](std::string_view line) { return line.size() > indent; })
            .base(),
          lines.end());
      }

      if (lines.empty())
      {
        return os;
      }

      bool is_indented = false;
      bool only_empty = true;
      for (std::size_t i = 0; i < lines.size() - 1; ++i)
      {
        auto current = lines[i];
        auto next = lines[i + 1];
        if (current == "\n" || current.size() <= indent)
        {
          os << "\\n";
          continue;
        }

        current = current.substr(indent);
        auto first_non_space = current.find_first_not_of(" \t");
        if (
          current.front() == '\t' ||
          (first_non_space > 0 && first_non_space < current.size()))
        {
          if (!is_indented)
          {
            if (!only_empty && node == Folded)
            {
              os << "\\n";
            }
            is_indented = true;
          }
        }
        else
        {
          is_indented = false;
        }

        os << escape_chars(current, escape);
        if (node == Folded)
        {
          if (is_indented)
          {
            os << "\\n";
          }
          else if (next.size() > indent)
          {
            if (next[indent] != ' ')
            {
              os << " ";
            }
          }
        }
        else if (node == Literal)
        {
          os << "\\n";
        }
        else
        {
          throw std::runtime_error("Unsupported block type");
        }

        only_empty = false;
      }

      auto last = lines.back();
      if (last.size() > indent)
      {
        last = last.substr(indent);
        if (last.front() == '\n')
        {
          switch (chomp)
          {
            case Chomp::Clip:
              os << "\\n";
              break;

            case Chomp::Keep:
              os << "\\n" << escape_chars(last, escape);
              break;

            case Chomp::Strip:
              break;
          }
        }
        else
        {
          os << escape_chars(last, escape);
          if (chomp != Chomp::Strip)
          {
            os << "\\n";
          }
        }
      }
      else
      {
        if (chomp != Chomp::Strip)
        {
          os << "\\n";
        }
      }

      return os;
    }

    std::ostream&
    quote_to_string(std::ostream& os, const Node& node, bool raw_quote)
    {
      std::set<char> escape;
      if (node == DoubleQuote)
      {
        escape = {'\t', '\r', '\n'};
      }
      else
      {
        escape = {'\\'};
      }

      if (!raw_quote)
      {
        escape.insert('"');
      }

      for (std::size_t i = 0; i < node->size() - 1; ++i)
      {
        if (node->at(i) == EmptyLine)
        {
          os << "\\n";
          continue;
        }

        Location loc = node->at(i)->location();
        auto current = loc.view();
        auto next = node->at(i + 1)->location().view();
        if (current.size() == 0)
        {
          if (i == 0)
          {
            os << " ";
          }
          else
          {
            os << "\\n";
          }
        }
        else
        {
          if (node == DoubleQuote)
          {
            if (raw_quote)
            {
              os << replace_all(escape_chars(current, escape), "\\\"", "\"");
            }
            else
            {
              os << escape_chars(current, escape);
            }
          }
          else
          {
            os << replace_all(escape_chars(current, escape), "''", "'");
          }

          if (next.size() > 0 && current.back() != '\\')
          {
            os << " ";
          }
        }
      }

      if (node->back() == EmptyLine)
      {
        os << "\\n";
      }
      else
      {
        auto last = node->back()->location().view();
        if (last.empty() && node->size() > 1)
        {
          os << " ";
        }
        else if (node == DoubleQuote)
        {
          if (raw_quote)
          {
            os << replace_all(escape_chars(last, escape), "\\\"", "\"");
          }
          else
          {
            os << escape_chars(last, escape);
          }
        }
        else
        {
          os << replace_all(escape_chars(last, escape), "''", "'");
        }
      }

      return os;
    }

    bool is_space(char c)
    {
      return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    void escape_char(std::ostream& os, char c)
    {
      switch (c)
      {
        case '\n':
          os << "\\n";
          break;

        case '\r':
          os << "\\r";
          break;

        case '\b':
          os << "\\b";
          break;

        case '\f':
          os << "\\f";
          break;

        case '\t':
          os << "\\t";
          break;

        case ' ':
        case '/':
          os << c;
          break;

        default:
          os << "\\" << c;
          break;
      }
    }

    std::string
    escape_chars(const std::string_view& str, const std::set<char>& to_escape)
    {
      std::string input = utf8::unescape_hexunicode(str);
      std::ostringstream os;
      bool escape = false;
      for (auto c : input)
      {
        if (escape)
        {
          escape_char(os, c);
          escape = false;
        }
        else
        {
          if (to_escape.find(c) != to_escape.end())
          {
            escape_char(os, c);
          }
          else if (c == '\\')
          {
            escape = true;
          }
          else if (is_space(c))
          {
            os << ' ';
          }
          else
          {
            os << c;
          }
        }
      }
      return os.str();
    }

    std::string unescape_url_chars(const std::string_view& input)
    {
      std::ostringstream output;
      auto it = input.begin();
      while (it != input.end())
      {
        if (*it == '%')
        {
          std::string hex(it + 1, it + 3);
          int code = std::stoi(hex, 0, 16);
          output << (char)code;
          it += 3;
        }
        else
        {
          output << *it;
          it++;
        }
      }

      return output.str();
    }

    std::string replace_all(
      const std::string_view& v,
      const std::string_view& find,
      const std::string_view& replace)
    {
      std::string s(v);
      auto pos = s.find(find);
      while (pos != std::string::npos)
      {
        s = s.replace(pos, find.size(), replace);
        pos = s.find(find);
      }
      return s;
    }
  }
}

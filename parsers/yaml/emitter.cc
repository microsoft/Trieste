#include "trieste/utf8.h"
#include "yaml.h"

namespace
{
  enum class Chomp
  {
    Clip,
    Strip,
    Keep,
  };

  bool is_space(char c)
  {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }
}

namespace trieste::yaml
{
  YAMLEmitter::YAMLEmitter(const std::string& indent, const std::string& newline) : m_indent(indent), m_newline(newline) {}

  void YAMLEmitter::emit(std::ostream& os, const Node&) const
  {
    os << "Not implemented";
  }

  void YAMLEmitter::emit_events(std::ostream& os, const Node& stream) const
  {
    emit_event(os, stream);
  }

  bool
  YAMLEmitter::emit_value_event(std::ostream& os, const Node& maybe_value) const
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
    os << m_newline;

    return false;
  }

  bool YAMLEmitter::emit_mapping_event(
    std::ostream& os, const Node& maybe_node, bool is_flow) const
  {
    os << "+MAP";
    if (is_flow)
    {
      os << " {}";
    }

    Node node = handle_tag_anchor(os, maybe_node);

    os << m_newline;
    for (auto child : *node)
    {
      if (emit_event(os, child))
      {
        return true;
      }
    }
    os << "-MAP" << m_newline;
    return false;
  }

  bool YAMLEmitter::emit_sequence_event(
    std::ostream& os, const Node& maybe_node, bool is_flow) const
  {
    os << "+SEQ";

    if (is_flow)
    {
      os << " []";
    }

    Node node = handle_tag_anchor(os, maybe_node);

    os << m_newline;
    for (auto child : *node)
    {
      if (emit_event(os, child))
      {
        return true;
      }
    }
    os << "-SEQ" << m_newline;
    return false;
  }

  bool YAMLEmitter::emit_alias_event(std::ostream& os, const Node& node) const
  {
    os << "=ALI *" << node->location().view() << m_newline;
    return false;
  }

  bool YAMLEmitter::emit_literal_event(
    std::ostream& os, const Node& maybe_node) const
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " |" << block_to_string(node, true) << m_newline;
    return false;
  }

  bool
  YAMLEmitter::emit_folded_event(std::ostream& os, const Node& maybe_node) const
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " >" << block_to_string(node, true) << m_newline;
    return false;
  }

  bool
  YAMLEmitter::emit_plain_event(std::ostream& os, const Node& maybe_node) const
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " :";
    std::set<char> escape = {'\\', '\n', '\r'};
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
      if (!is_space(current.front()) && !is_space(next.front()))
      {
        os << " ";
      }
    }
    os << escape_chars(node->back()->location().view(), escape) << m_newline;
    return false;
  }

  bool YAMLEmitter::emit_doublequote_event(
    std::ostream& os, const Node& maybe_node) const
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " \"";
    write_quote(os, node, true);
    os << m_newline;
    return false;
  }

  bool YAMLEmitter::emit_singlequote_event(
    std::ostream& os, const Node& maybe_node) const
  {
    os << "=VAL";

    Node node = handle_tag_anchor(os, maybe_node);

    os << " '";
    write_quote(os, node, true);
    os << m_newline;
    return false;
  }

  Token YAMLEmitter::get_type(const Node& node) const
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

  bool YAMLEmitter::emit_event(std::ostream& os, const Node& node) const
  {
    Token node_type = get_type(node);
    if (node_type.in({Value, Int, Float, Empty, True, False, Hex}))
    {
      return emit_value_event(os, node);
    }

    if (node_type == DoubleQuote)
    {
      return emit_doublequote_event(os, node);
    }

    if (node_type == SingleQuote)
    {
      return emit_singlequote_event(os, node);
    }

    if (node_type == Null)
    {
      os << "=VAL";
      handle_tag_anchor(os, node);
      os << " :" << m_newline;
      return false;
    }

    if (node_type == Alias)
    {
      return emit_alias_event(os, node);
    }

    if (node_type == Literal)
    {
      return emit_literal_event(os, node);
    }

    if (node_type == Folded)
    {
      return emit_folded_event(os, node);
    }

    if (node_type == Plain)
    {
      return emit_plain_event(os, node);
    }

    if (node_type.in({FlowMappingItem, MappingItem}))
    {
      if (emit_event(os, node->front()))
      {
        return true;
      }
      return emit_event(os, node->back());
    }

    if (node_type.in({Sequence, FlowSequence}))
    {
      return emit_sequence_event(os, node, node_type == FlowSequence);
    }

    if (node_type.in({Mapping, FlowMapping}))
    {
      return emit_mapping_event(os, node, node_type == FlowMapping);
    }

    if (node_type == Document)
    {
      if (node->size() == 0)
      {
        return false;
      }

      Node start = node->at(0);
      Node value = node->at(1);
      Node end = node->at(2);
      if (start == Directives)
      {
        start = value;
        value = end;
        end = node->at(3);
      }
      os << "+DOC";
      if (start->location().len > 0)
      {
        os << " " << start->location().view();
      }
      os << m_newline;
      if (emit_event(os, value))
      {
        return true;
      }
      os << "-DOC";
      if (end->location().len > 0)
      {
        os << " " << end->location().view();
      }
      os << m_newline;
      return false;
    }

    if (node_type == Stream)
    {
      os << "+STR" << m_newline;
      for (Node child : *node->back())
      {
        if (emit_event(os, child))
        {
          return true;
        }
      }
      os << "-STR" << m_newline;
      return false;
    }

    if (node_type == Top)
    {
      return emit_event(os, node->front());
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
      os << " :" << m_newline;
      return false;
    }

    std::string error =
      "to_event: Unexpected node type: " + std::string(node_type.str());
    throw std::runtime_error(error);
  }

  Node YAMLEmitter::handle_tag_anchor(std::ostream& os, const Node& node) const
  {
    Node anchor = nullptr;
    std::string tag = "";
    Node value = node;
    if (node->type() == AnchorValue)
    {
      anchor = node->front();
      value = node->back();
    }

    if (value->type() == TagValue)
    {
      Node tag_node;
      std::string handle = "";
      if (value->size() > 2)
      {
        Node prefix_node = value->front();
        Node handle_node = prefix_node->lookup()[0];
        if (handle_node != nullptr)
        {
          handle = handle_node->back()->location().view();
        }
        tag_node = value->at(1);
      }
      else
      {
        tag_node = value->front();
      }

      value = value->back();
      auto tagname = unescape_url_chars(tag_node->location().view());
      std::ostringstream tags;
      if (tagname.size() >= 2 && tagname.front() == '<' && tagname.back() == '>')
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
      anchor = value->front();
      value = value->back();
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

  void YAMLEmitter::escape_char(std::ostream& os, char c) const
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

  std::string YAMLEmitter::escape_chars(
    const std::string_view& str, const std::set<char>& to_escape) const
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

  std::string
  YAMLEmitter::unescape_url_chars(const std::string_view& input) const
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

  std::string
  YAMLEmitter::block_to_string(const Node& node, bool raw_quotes) const
  {
    if (node->size() == 2)
    {
      return "";
    }

    std::set<char> escape = {'\\', '\n', '\r', '\t'};
    if (!raw_quotes)
    {
      escape.insert('"');
    }
    std::ostringstream os;
    Node indent_node = node->at(0);
    Node chomp_node = node->at(1);
    Node lines_node = node->at(2);
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
    for (auto line_node : *lines_node)
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
      return "";
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

    return os.str();
  }

  std::string YAMLEmitter::replace_all(
    const std::string_view& v,
    const std::string_view& find,
    const std::string_view& replace) const
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

  void YAMLEmitter::write_quote(
    std::ostream& os, const Node& node, bool raw_quote) const
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
      if (last.empty())
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
  }

}

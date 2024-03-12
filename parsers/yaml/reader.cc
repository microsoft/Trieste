#include "trieste/source.h"
#include "trieste/utf8.h"
#include "yaml.h"

#include <stdexcept>

namespace
{
  using namespace trieste;
  using namespace trieste::yaml;

  enum class Chomp
  {
    Clip,
    Strip,
    Keep,
  };

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
        else if (std::isspace(c))
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

  std::string block_to_string(Node node, bool raw_quotes)
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

  std::string replace_all(
    const std::string_view& v,
    const std::string& find,
    const std::string& replace)
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

  Node lookup_nearest(Node ref)
  {
    Nodes vars = ref->lookup();
    Node var;
    for (Node n : vars)
    {
      if (n->location().pos <= ref->location().pos)
      {
        var = n;
      }
    }
    return var;
  }

  void write_quote(std::ostream& os, Node node, bool raw_quote)
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

  void to_json(std::ostream& os, const std::string& indent, const Node& node)
  {
    if (node->type() == Value)
    {
      os << '"' << escape_chars(node->location().view(), {'\\', '"'}) << '"';
    }
    else if (node->type().in({Int, Float, True, False, Null}))
    {
      os << node->location().view();
    }
    else if (node->type() == Hex)
    {
      std::string hex(node->location().view());
      os << std::stoull(hex, 0, 16);
    }
    else if (node->type() == String)
    {
      os << '"' << node->location().view() << '"';
    }
    else if (node->type() == DoubleQuote)
    {
      os << '"';
      write_quote(os, node, false);
      os << '"';
    }
    else if (node->type() == SingleQuote)
    {
      os << '"';
      write_quote(os, node, false);
      os << '"';
    }
    else if (node->type() == Null)
    {
      os << "null";
    }
    else if (node->type() == Alias)
    {
      Node anchorValue = lookup_nearest(node);
      if (anchorValue->size() == 1)
      {
        os << "null";
      }
      else
      {
        Node value = anchorValue->back();
        if (value->type().in({Mapping, Sequence, Alias}))
        {
          to_json(os, indent, value);
        }
        else
        {
          to_json(os, indent, value);
        }
      }
    }
    else if (node->type() == Literal)
    {
      os << '"' << block_to_string(node, false) << '"';
    }
    else if (node->type() == Folded)
    {
      os << '"' << block_to_string(node, false) << '"';
    }
    else if (node->type() == Plain)
    {
      os << '"';
      std::set<char> escape = {'\\', '"', '\n', '\r'};
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
      os << escape_chars(node->back()->location().view(), escape) << '"';
    }
    else if (node->type().in({FlowMappingItem, MappingItem}))
    {
      Node key = node->front();
      Node value = node->back();
      os << indent;
      to_json(os, indent, key);
      os << ": ";
      to_json(os, indent, value);
    }
    else if (node == AnchorValue)
    {
      if (node->size() > 1)
      {
        to_json(os, indent, node->back());
      }
      else
      {
        os << "null";
      }
    }
    else if (node == TagValue)
    {
      if (node->size() > 1)
      {
        Location prefix = node->front()->location();
        Location tag = node->at(1)->location();
        Location value = node->back()->location();
        if (
          prefix.view() == "!!" && tag.view() == "str" &&
          node->back()->type().in({Int, Float, True, False, Null}))
        {
          to_json(os, indent, Value ^ value);
        }
        else
        {
          to_json(os, indent, node->back());
        }
      }
      else
      {
        os << "null";
      }
    }
    else if (node->type().in({Sequence, FlowSequence}))
    {
      os << '[';
      if (node->size() > 0)
      {
        os << std::endl;
        std::string child_indent = indent + "  ";
        for (std::size_t i = 0; i < node->size() - 1; ++i)
        {
          os << child_indent;
          to_json(os, child_indent, node->at(i));
          os << ',' << std::endl;
        }
        os << child_indent;
        to_json(os, child_indent, node->back());
        os << std::endl;
      }
      os << indent << ']';
    }
    else if (node->type().in({Mapping, FlowMapping}))
    {
      os << '{';
      if (node->size() > 0)
      {
        os << std::endl;
        std::string child_indent = indent + "  ";
        for (std::size_t i = 0; i < node->size() - 1; ++i)
        {
          to_json(os, child_indent, node->at(i));
          os << ',' << std::endl;
        }
        to_json(os, child_indent, node->back());
        os << std::endl;
      }
      os << indent << '}';
    }
    else if (node->type() == Document)
    {
      if (node->front() == Directives)
      {
        to_json(os, indent, node->at(2));
      }
      else
      {
        to_json(os, indent, node->at(1));
      }
      os << std::endl;
    }
    else if (node->type() == Stream)
    {
      for (auto child : *node->back())
      {
        to_json(os, indent, child);
      }
    }
    else if (node->type() == Top)
    {
      to_json(os, indent, node->front());
    }
    else if (node->type().in(
               {DocumentStart,
                DocumentEnd,
                TagDirective,
                VersionDirective,
                UnknownDirective}))
    {
      return;
    }
    else
    {
      std::string error =
        "to_json: Unexpected node type: " + std::string(node->type().str());
      throw std::runtime_error(error);
    }
  }

  Node handle_tag_anchor(std::ostream& os, Node node)
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
        Node handle_node = lookup_nearest(prefix_node);
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
      if (tagname.front() == '<' && tagname.back() == '>')
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

  bool to_event(std::ostream& os, const Node& node);

  bool to_value_event(std::ostream& os, Node value)
  {
    os << "=VAL";
    value = handle_tag_anchor(os, value);

    if (value->type() == Empty)
    {
      os << " :";
    }
    else
    {
      os << " :" << escape_chars(value->location().view(), {'\\'});
    }
    os << std::endl;

    return false;
  }

  bool to_mapping_event(std::ostream& os, Node node, bool is_flow)
  {
    os << "+MAP";
    if (is_flow)
    {
      os << " {}";
    }

    node = handle_tag_anchor(os, node);

    os << std::endl;
    for (auto child : *node)
    {
      if (to_event(os, child))
      {
        return true;
      }
    }
    os << "-MAP" << std::endl;
    return false;
  }

  bool to_sequence_event(std::ostream& os, Node node, bool is_flow)
  {
    os << "+SEQ";

    if (is_flow)
    {
      os << " []";
    }

    node = handle_tag_anchor(os, node);

    os << std::endl;
    for (auto child : *node)
    {
      if (to_event(os, child))
      {
        return true;
      }
    }
    os << "-SEQ" << std::endl;
    return false;
  }

  bool to_alias_event(std::ostream& os, Node node)
  {
    os << "=ALI *" << node->location().view() << std::endl;
    return false;
  }

  bool to_literal_event(std::ostream& os, Node node)
  {
    os << "=VAL";

    node = handle_tag_anchor(os, node);

    os << " |" << block_to_string(node, true) << std::endl;
    return false;
  }

  bool to_folded_event(std::ostream& os, Node node)
  {
    os << "=VAL";

    node = handle_tag_anchor(os, node);

    os << " >" << block_to_string(node, true) << std::endl;
    return false;
  }

  bool to_plain_event(std::ostream& os, Node node)
  {
    os << "=VAL";

    node = handle_tag_anchor(os, node);

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
      if (!std::isspace(current.front()) && !std::isspace(next.front()))
      {
        os << " ";
      }
    }
    os << escape_chars(node->back()->location().view(), escape) << std::endl;
    return false;
  }

  bool to_doublequote_event(std::ostream& os, Node node)
  {
    os << "=VAL";

    node = handle_tag_anchor(os, node);

    os << " \"";
    write_quote(os, node, true);
    os << std::endl;
    return false;
  }

  bool to_singlequote_event(std::ostream& os, Node node)
  {
    os << "=VAL";

    node = handle_tag_anchor(os, node);

    os << " '";
    write_quote(os, node, true);
    os << std::endl;
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

  bool to_event(std::ostream& os, const Node& node)
  {
    Token node_type = get_type(node);
    if (node_type.in({Value, Int, Float, Empty, True, False, Hex}))
    {
      return to_value_event(os, node);
    }

    if (node_type == DoubleQuote)
    {
      return to_doublequote_event(os, node);
    }

    if (node_type == SingleQuote)
    {
      return to_singlequote_event(os, node);
    }

    if (node_type == Null)
    {
      os << "=VAL";
      handle_tag_anchor(os, node);
      os << " :" << std::endl;
      return false;
    }

    if (node_type == Alias)
    {
      return to_alias_event(os, node);
    }

    if (node_type == Literal)
    {
      return to_literal_event(os, node);
    }

    if (node_type == Folded)
    {
      return to_folded_event(os, node);
    }

    if (node_type == Plain)
    {
      return to_plain_event(os, node);
    }

    if (node_type.in({FlowMappingItem, MappingItem}))
    {
      if (to_event(os, node->front()))
      {
        return true;
      }
      return to_event(os, node->back());
    }

    if (node_type.in({Sequence, FlowSequence}))
    {
      return to_sequence_event(os, node, node_type == FlowSequence);
    }

    if (node_type.in({Mapping, FlowMapping}))
    {
      return to_mapping_event(os, node, node_type == FlowMapping);
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
      os << std::endl;
      if (to_event(os, value))
      {
        return true;
      }
      os << "-DOC";
      if (end->location().len > 0)
      {
        os << " " << end->location().view();
      }
      os << std::endl;
      return false;
    }

    if (node_type == Stream)
    {
      os << "+STR" << std::endl;
      for (Node child : *node->back())
      {
        if (to_event(os, child))
        {
          return true;
        }
      }
      os << "-STR" << std::endl;
      return false;
    }

    if (node_type == Top)
    {
      return to_event(os, node->front());
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
      os << " :" << std::endl;
      return false;
    }

    std::string error =
      "to_event: Unexpected node type: " + std::string(node_type.str());
    throw std::runtime_error(error);
  }
}

namespace trieste::yaml
{
  YAMLReader::YAMLReader(const std::filesystem::path& path)
  : YAMLReader(SourceDef::load(path))
  {}

  YAMLReader::YAMLReader(const std::string& yaml)
  : YAMLReader(SourceDef::synthetic(yaml))
  {}

  YAMLReader::YAMLReader(const Source& source)
  : m_source(source),
    m_debug_enabled(false),
    m_debug_path("."),
    m_well_formed_checks_enabled(false)
  {}

  YAMLReader& YAMLReader::debug_enabled(bool value)
  {
    m_debug_enabled = value;
    return *this;
  }

  bool YAMLReader::debug_enabled() const
  {
    return m_debug_enabled;
  }

  YAMLReader& YAMLReader::well_formed_checks_enabled(bool value)
  {
    m_well_formed_checks_enabled = value;
    return *this;
  }

  bool YAMLReader::well_formed_checks_enabled() const
  {
    return m_well_formed_checks_enabled;
  }

  YAMLReader& YAMLReader::debug_path(const std::string& path)
  {
    m_debug_path = path;
    return *this;
  }

  const std::string& YAMLReader::debug_path() const
  {
    return m_debug_path;
  }

  void YAMLReader::read()
  {
    auto ast = NodeDef::create(Top);
    Parse parse = yaml::parser();
    ast << parse.sub_parse("yaml", File, m_source);
    auto passes = yaml::passes();
    PassRange pass_range(passes, parse.wf(), "parse");
    bool ok;
    Nodes error_nodes;
    std::string failed_pass;
    {
      logging::Info summary;
      summary << "---------" << std::endl;
      std::filesystem::path debug_path;
      if (m_debug_enabled)
        debug_path = m_debug_path;
      auto p = default_process(
        summary, m_well_formed_checks_enabled, "yaml", debug_path);

      p.set_error_pass(
        [&error_nodes, &failed_pass](Nodes& errors, std::string pass_name) {
          error_nodes = errors;
          failed_pass = pass_name;
        });

      ok = p.build(ast, pass_range);
      summary << "---------" << std::endl;
    }

    if (ok)
    {
      m_stream = ast;
      return;
    }

    logging::Trace() << "Read failed: " << failed_pass;
    if (error_nodes.empty())
    {
      logging::Trace() << "No error nodes so assuming wf error";
      error_nodes.push_back(err(ast->clone(), "Failed at pass " + failed_pass));
    }

    Node error_result = NodeDef::create(ErrorSeq);
    for (auto& error : error_nodes)
    {
      error_result->push_back(error);
    }

    m_stream = error_result;
  }

  bool YAMLReader::has_errors() const
  {
    return m_stream->type() == ErrorSeq;
  }

  std::string YAMLReader::error_message() const
  {
    std::ostringstream error;
    error << m_stream;
    return error.str();
  }

  std::string YAMLReader::to_json() const
  {
    std::ostringstream os;
    ::to_json(os, "", m_stream);
    return os.str();
  }

  std::string YAMLReader::to_event() const
  {
    std::ostringstream os;
    ::to_event(os, m_stream);
    return os.str();
  }
}

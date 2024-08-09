#include "internal.h"
#include "yaml.h"

#include <string>
#include <trieste/trieste.h>
#include <trieste/utf8.h>

namespace
{
  using namespace trieste;
  using namespace trieste::yaml;

  struct WriteOptions
  {
    std::string newline;
    std::size_t indent;
    bool canonical;
    bool emit_docend;
  };

  struct Spaces
  {
    std::size_t outer;
    std::size_t inner;

    Spaces in() const
    {
      return {inner, inner};
    }

    Spaces out() const
    {
      return {outer, outer};
    }

    Spaces indent(std::size_t indent) const
    {
      return {outer, inner + indent};
    }

    std::string outer_str() const
    {
      return std::string(outer, ' ');
    }

    std::string inner_str() const
    {
      return std::string(inner, ' ');
    }
  };

  Node unwrap(const Node& node)
  {
    if (node->in({TagValue, AnchorValue}))
    {
      return unwrap(node / Value);
    }

    return node;
  }

  bool is_complex(const Node& mappingitem)
  {
    Node key = unwrap(mappingitem / Key);
    if (key->in(
          {Sequence, FlowSequence, Mapping, FlowMapping, Literal, Folded}))
    {
      return !key->empty();
    }

    if (key == DoubleQuote)
    {
      for (const Node& line : *key)
      {
        if (line->location().view().find(':') != std::string::npos)
        {
          return true;
        }
      }
    }

    return false;
  }

  bool is_in(const Node& node, std::set<Token> tokens)
  {
    NodeDef* parent = node->parent_unsafe();
    while (parent != Top)
    {
      if (tokens.count(parent->type()) > 0)
      {
        return true;
      }

      parent = parent->parent_unsafe();
    }

    return false;
  }

  bool is_sequence_out(Node node)
  {
    bool newline = false;
    NodeDef* current = node->parent_unsafe();
    if (current->in({AnchorValue, TagValue}))
    {
      newline = true;
      current = current->parent_unsafe();
    }

    if (current->in({AnchorValue, TagValue}))
    {
      current = current->parent_unsafe();
    }

    if (current->in({MappingItem, FlowMappingItem}))
    {
      newline = newline || !is_complex(current->intrusive_ptr_from_this());
    }

    return newline && !current->in({Sequence, FlowSequence});
  }

  Node handle_tag_anchor(
    std::ostream& os,
    WriteOptions& options,
    const Spaces& spaces,
    const Node& node)
  {
    Node anchor = nullptr;
    std::string tag = "";
    Node value = node;
    if (node == AnchorValue)
    {
      anchor = node / Anchor;
      value = node / Value;
    }

    if (value == TagValue)
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
      if (handle == "!")
      {
        if (tagname.find("<tag:yaml.org,2002:") == 0)
        {
          tagname = tagname.substr(19);
          tagname = "!" + tagname.substr(0, tagname.size() - 1);
        }
        else if (tagname.find("<!") == 0)
        {
          tagname = tagname.substr(2);
          tagname = tagname.substr(0, tagname.size() - 1);
        }

        tags << "!" << tagname;
      }
      else if (handle == "tag:yaml.org,2002:")
      {
        tags << "!!" << tagname;
      }
      else
      {
        tags << "!";
        if ((tagname.size() >= 2 && tagname.front() == '<' &&
             tagname.back() == '>'))
        {
          tags << tagname;
        }
        else
        {
          tags << "<" << handle << tagname << ">";
        }
      }

      tag = tags.str();
    }

    if (value == AnchorValue)
    {
      anchor = value / Anchor;
      value = value / Value;
    }

    if (anchor != nullptr)
    {
      os << "&" << anchor->location().view();
    }

    if (!tag.empty())
    {
      if (anchor != nullptr)
      {
        os << " ";
      }

      os << tag;
    }

    if (value->in({Mapping, FlowMapping}))
    {
      os << options.newline << spaces.inner_str();
    }
    else if (value->in({Sequence, FlowSequence}))
    {
      os << options.newline
         << (is_sequence_out(value) ? spaces.outer_str() : spaces.inner_str());
    }
    else if (value != Empty)
    {
      os << " ";
    }

    return value;
  }

  bool write_value(
    std::ostream& os,
    WriteOptions& options,
    const Spaces& spaces,
    const Node& value);

  bool write_sequence(
    std::ostream& os,
    WriteOptions& options,
    const Spaces& spaces,
    Node sequence)
  {
    if (sequence->size() == 0)
    {
      os << "[]" << options.newline;
      return false;
    }

    bool not_first = false;
    Spaces new_spaces = spaces.in().indent(2);
    for (const Node& item : *sequence)
    {
      if (not_first)
      {
        os << new_spaces.outer_str();
      }
      else
      {
        not_first = true;
      }

      os << "-";

      if (item != Empty)
      {
        os << " ";
      }

      if (write_value(os, options, new_spaces, item))
      {
        return true;
      }
    }

    return false;
  }

  bool write_complex(
    std::ostream& os,
    WriteOptions& options,
    const Spaces& spaces,
    const Node& mappingitem,
    bool not_first)
  {
    Node key = mappingitem / Key;
    Node value = mappingitem / Value;
    if (not_first)
    {
      os << spaces.inner_str();
    }
    os << "? ";
    if (write_value(os, options, spaces.in().indent(2), key))
    {
      return true;
    }

    if (value != Null)
    {
      os << spaces.inner_str() << ":";
      if (value != Empty)
      {
        os << " ";
      }
      return write_value(os, options, spaces.in().indent(2), value);
    }

    return false;
  }

  bool write_mapping(
    std::ostream& os,
    WriteOptions& options,
    const Spaces& spaces,
    const Node& mapping)
  {
    if (mapping->empty())
    {
      os << "{}" << options.newline;
      return false;
    }

    bool not_first = false;
    Spaces new_spaces = spaces.in().indent(options.indent);
    for (const Node& mappingitem : *mapping)
    {
      if (is_complex(mappingitem))
      {
        if (write_complex(os, options, spaces, mappingitem, not_first))
        {
          return true;
        }
        continue;
      }

      Node key = mappingitem / Key;
      Node value = mappingitem / Value;

      if (not_first)
      {
        os << spaces.inner_str();
      }
      else
      {
        not_first = true;
      }

      if (key == Alias)
      {
        os << "*";
        os << key->location().view();
        os << " ";
      }
      else if (key == Value && key->location().view().back() == ':')
      {
        os << "'" << key->location().view() << "'";
      }
      else if (key->in({TagValue, AnchorValue}) && unwrap(key) == Empty)
      {
        handle_tag_anchor(os, options, spaces, key);
        os << " ";
      }
      else
      {
        WriteOptions key_options = {};
        if (write_value(os, key_options, spaces, key))
        {
          return true;
        }
      }

      os << ":";

      if (value->in({Mapping, FlowMapping}) && !value->empty())
      {
        os << options.newline << new_spaces.inner_str();
      }
      else if (value->in({Sequence, FlowSequence}) && !value->empty())
      {
        os << options.newline
           << (is_sequence_out(value) ? new_spaces.outer_str() :
                                        new_spaces.inner_str());
      }
      else
      {
        if (value != Empty)
        {
          os << " ";
        }
      }

      if (write_value(os, options, new_spaces, value))
      {
        return true;
      }
    }

    return false;
  }

  std::string unescape_block(
    const Node& node,
    const std::string& str,
    WriteOptions& options,
    const std::string& indent)
  {
    std::ostringstream os;
    for (std::size_t i = 0; i < str.size(); i++)
    {
      if (i < str.size() - 1 && str[i] == '\\')
      {
        switch (str[i + 1])
        {
          case 'n':
            os << '\n';
            break;

          case 'r':
            os << '\r';
            break;

          case 't':
            os << '\t';
            break;

          case '"':
            os << '"';
            break;

          case '\\':
            os << '\\';
            break;

          default:
            os << str[i] << str[i + 1];
            break;
        }
        i++;
      }
      else
      {
        os << str[i];
      }
    }
    std::string result = os.str();

    if (node != Literal && !result.empty())
    {
      std::size_t pos = result.find_first_not_of('\n');
      if (pos == std::string::npos)
      {
        // string entirely of newlines
        result = result + "\n" + indent;
        RE2::GlobalReplace(&result, R"(\n)", options.newline);
        return result;
      }
      else
      {
        // the event-style escaped string undercounts newlines
        // by one, so we need to add one back in.
        RE2::GlobalReplace(&result, R"(([^\s])(\n+)([^ \n]))", "\\1\n\\2\\3");
        if (node == Folded)
        {
          // In the special case of indent-preserving newlines
          // we need to remove the extra newline that will have been
          // added at the end of the block.
          RE2::GlobalReplace(
            &result, R"((\n\n(?: [^\n]+\n)+|^(?: [^\n]+\n)+)\n)", "\\1");
        }
      }
    }

    RE2::GlobalReplace(&result, R"(\n([^\n]))", "\n" + indent + "\\1");
    RE2::GlobalReplace(&result, R"(\n)", options.newline);
    return result;
  }

  void write_block(
    std::ostream& os,
    WriteOptions& options,
    const Spaces& spaces,
    const Node& block)
  {
    std::ostringstream ss;
    block_to_string(ss, block);
    std::string indent = spaces.out().indent(options.indent).inner_str();
    std::string str = unescape_block(block, ss.str(), options, indent);
    if (str.empty())
    {
      os << '"' << '"' << options.newline;
      return;
    }

    if (block == Folded)
    {
      os << ">";
    }
    else
    {
      os << "|";
    }

    std::size_t start = 0;
    if (str.find(options.newline) == 0)
    {
      start = str.find_first_not_of(options.newline);
      if (start != std::string::npos)
      {
        start += options.indent;
      }
    }

    if (start == std::string::npos)
    {
      if (is_in(block, {MappingItem, FlowMappingItem}))
      {
        os << options.indent;
      }
    }
    else if (str[start] == ' ' || (start > 0 && str[start] == '#'))
    {
      os << options.indent;
    }

    auto chomp = (block / ChompIndicator)->location().view();
    if (chomp == "-")
    {
      os << chomp;
    }

    options.emit_docend = false;
    if (chomp == "+")
    {
      std::size_t len = options.newline.size();
      if (
        (str.size() >= 2 * len &&
         str.substr(str.size() - 2 * len, len) == options.newline) ||
        str == options.newline)
      {
        os << chomp;
        options.emit_docend = true;
      }
    }

    os << options.newline;

    if (str.find(options.newline) != 0)
    {
      os << indent;
    }

    os << str;

    if (chomp == "-")
    {
      os << options.newline;
    }
  }

  bool contains_unicode(const std::string_view& str)
  {
    return std::find_if(str.begin(), str.end(), [](char c) {
             return (uint8_t)c > 127;
           }) != str.end();
  }

  std::string plain_to_string(
    const Node& plain, WriteOptions& options, const Spaces& spaces)
  {
    std::ostringstream os;
    block_to_string(os, plain);
    std::string str = os.str();
    bool singlequote = false;
    bool doublequote = false;
    if (str.find("\\n") != std::string::npos)
    {
      if (options.canonical)
      {
        singlequote = plain->parent() != Document;
      }
      else
      {
        singlequote = true;
      }
    }

    if (str.find("---") != std::string::npos)
    {
      singlequote = true;
    }

    if (contains_unicode(str))
    {
      doublequote = true;
      singlequote = false;
      str = utf8::escape_unicode(str);
    }

    if (singlequote)
    {
      str = unescape_block(
        plain, str, options, spaces.out().indent(options.indent).inner_str());
      RE2::GlobalReplace(&str, "'", "''");
      return "'" + str + "'";
    }

    if (doublequote)
    {
      str = unescape_block(
        plain, str, options, spaces.out().indent(options.indent).inner_str());
      return '"' + str + '"';
    }

    return unescape_block(plain, str, options, spaces.inner_str());
  }

  bool write_value(
    std::ostream& os,
    WriteOptions& options,
    const Spaces& spaces,
    const Node& maybe_value)
  {
    bool tag_anchor = maybe_value->in({TagValue, AnchorValue});
    Node value;
    if (tag_anchor)
    {
      value = handle_tag_anchor(os, options, spaces, maybe_value);
    }
    else
    {
      value = maybe_value;
    }

    if (value->in({Mapping, FlowMapping}))
    {
      return write_mapping(os, options, spaces, value);
    }

    if (value->in({Sequence, FlowSequence}))
    {
      return write_sequence(
        os, options, is_sequence_out(value) ? spaces.out() : spaces, value);
    }

    if (value == Empty)
    {
      os << options.newline;
      return false;
    }

    if (value == Value)
    {
      if (contains_unicode(value->location().view()))
      {
        os << '"' << utf8::escape_unicode(value->location().view()) << '"'
           << options.newline;
        return false;
      }
      else
      {
        os << value->location().view() << options.newline;
        return false;
      }
    }

    if (value->in({Int, Float, Hex, True, False, Null}))
    {
      os << value->location().view() << options.newline;
      return false;
    }

    if (value == Plain)
    {
      os << plain_to_string(value, options, spaces) << options.newline;
      return false;
    }

    if (value == SingleQuote)
    {
      std::ostringstream single;
      quote_to_string(single, value);
      std::string single_str = unescape_block(
        value,
        single.str(),
        options,
        spaces.out().indent(options.indent).inner_str());
      RE2::GlobalReplace(&single_str, "'", "''");
      os << "'" << single_str << "'" << options.newline;
      return false;
    }

    if (value == DoubleQuote)
    {
      std::ostringstream quote;
      quote_to_string(quote, value);
      std::string quote_str = utf8::escape_unicode(quote.str());
      os << '"' << quote_str << '"' << options.newline;
      return false;
    }

    if (value->in({Literal, Folded}))
    {
      write_block(os, options, spaces.indent(options.indent), value);
      return false;
    }

    if (value == Alias)
    {
      os << "*" << value->location().view() << options.newline;
      return false;
    }

    os << "<error: unrecognized value node type: " << value->type() << ">";
    return true;
  }

  bool write_directive(
    std::ostream& os, WriteOptions& options, const Node& directive)
  {
    if (directive == VersionDirective)
    {
      os << directive->location().view() << options.newline;
    }
    return false;
  }

  bool write_document(
    std::ostream& os,
    WriteOptions& options,
    const Node& document,
    bool not_first)
  {
    Node directives = document / Directives;
    if (!directives->empty() && not_first && options.canonical)
    {
      for (const Node& directive : *directives)
      {
        if (write_directive(os, options, directive))
        {
          return true;
        }
      }
    }

    Node value = document / Value;
    Node docstart = document / DocumentStart;
    Node docend = document / DocumentEnd;

    bool emit_docstart;
    if (options.canonical)
    {
      emit_docstart = true;
      if (value == Empty && docend->location().len == 0)
      {
        value = Null ^ "null";
      }
    }
    else
    {
      emit_docstart = docstart->location().len > 0;
    }

    if (emit_docstart)
    {
      os << "---";

      if (
        value->in({Mapping, FlowMapping, Sequence, FlowSequence}) &&
        !value->empty())
      {
        os << options.newline;
      }
      else if (value != Empty)
      {
        os << " ";
      }
    }

    if (write_value(os, options, {0, 0}, value))
    {
      return true;
    }

    bool emit_docend = docend->location().len > 0 || options.emit_docend;
    if (emit_docend)
    {
      os << "..." << options.newline;
    }

    return false;
  }

  bool write_stream(std::ostream& os, WriteOptions& options, const Node& stream)
  {
    Node documents = stream / Documents;
    bool not_first = false;
    for (const Node& document : *documents)
    {
      write_document(os, options, document, not_first);
      not_first = true;
    }
    return false;
  }

  // clang-format off
  const auto wf_to_file =
    yaml::wf
    | (Top <<= File)
    | (File <<= Path * Stream)
    ;
  // clang-format on

  PassDef to_file(const std::filesystem::path& path)
  {
    return {
      "to_file",
      wf_to_file,
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
    Writer writer(
      const std::filesystem::path& path,
      const std::string& newline,
      std::size_t indent,
      bool canonical)
    {
      return Writer(
        "yaml",
        {to_file(path)},
        yaml::wf,
        [newline, indent, canonical](std::ostream& os, const Node& value) {
          WriteOptions options = {newline, indent, canonical, false};
          return write_stream(os, options, value);
        });
    }

    std::string to_string(
      Node yaml, const std::string& newline, std::size_t indent, bool canonical)
    {
      if (yaml == Top)
      {
        yaml = yaml->front();
      }

      WFContext context(yaml::wf);
      std::ostringstream os;
      WriteOptions options = {newline, indent, canonical, false};
      write_stream(os, options, yaml);
      return os.str();
    }
  }
}

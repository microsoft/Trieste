#include "internal.h"

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
  }
}

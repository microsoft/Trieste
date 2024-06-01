#include "internal.h"
#include "trieste/wf.h"

namespace
{
  using namespace trieste;
  using namespace trieste::json;

  struct WriteSettings
  {
    bool prettyprint;
    bool sort_keys;
    const std::string& indent;
  };

  void write_value(
    std::ostream& os,
    const WriteSettings& settings,
    const std::string& indent,
    const Node& value);

  void write_object(
    std::ostream& os,
    const WriteSettings& settings,
    const std::string& indent,
    const Node& object)
  {
    if (object->empty())
    {
      os << "{}";
      return;
    }

    std::string new_indent = indent + settings.indent;
    os << "{";
    if (settings.prettyprint)
    {
      os << std::endl;
    }

    std::vector<Node> members;
    if (settings.sort_keys)
    {
      std::vector<Location> keys;
      std::transform(
        object->begin(),
        object->end(),
        std::back_inserter(keys),
        [](Node member) { return (member / Key)->location(); });
      std::sort(keys.begin(), keys.end());
      for (const Location& key : keys)
      {
        Nodes defs = object->lookdown(key);
        members.insert(members.end(), defs.begin(), defs.end());
      }
    }
    else
    {
      members.insert(members.end(), object->begin(), object->end());
    }

    for (std::size_t i = 0; i < members.size(); ++i)
    {
      Node member = members[i];
      assert(member == Member);

      if (settings.prettyprint)
      {
        os << new_indent;
      }

      write_value(os, settings, new_indent, member / Key);
      os << ":";

      if (settings.prettyprint)
      {
        os << " ";
      }

      write_value(os, settings, new_indent, member / Value);

      if (i < object->size() - 1)
      {
        os << ",";
      }

      if (settings.prettyprint)
      {
        os << std::endl;
      }
    }

    if (settings.prettyprint)
    {
      os << indent;
    }

    os << "}";
  }

  void write_array(
    std::ostream& os,
    const WriteSettings& settings,
    const std::string& indent,
    const Node& array)
  {
    if (array->empty())
    {
      os << "[]";
      return;
    }

    std::string new_indent = indent + settings.indent;
    os << "[";
    if (settings.prettyprint)
    {
      os << std::endl;
    }
    for (std::size_t i = 0; i < array->size(); ++i)
    {
      Node element = array->at(i);
      if (settings.prettyprint)
      {
        os << new_indent;
      }
      write_value(os, settings, new_indent, element);

      if (i < array->size() - 1)
      {
        os << ",";
      }

      if (settings.prettyprint)
      {
        os << std::endl;
      }
    }
    if (settings.prettyprint)
    {
      os << indent;
    }
    os << "]";
  }

  void write_value(
    std::ostream& os,
    const WriteSettings& settings,
    const std::string& indent,
    const Node& value)
  {
    if (value->in({Number, String, True, False, Null}))
    {
      os << value->location().view();
    }
    else if (value == Key)
    {
      os << '"' << value->location().view() << '"';
    }
    else if (value == Object)
    {
      write_object(os, settings, indent, value);
    }
    else if (value == Array)
    {
      write_array(os, settings, indent, value);
    }
    else if (value == Top)
    {
      write_value(os, settings, indent, value->front());
    }
    else
    {
      std::ostringstream message;
      message << "Unexpected node type: " << value->type().str();
      throw std::runtime_error(message.str());
    }
  }

  const auto ValueToken = T(Object, Array, String, Number, True, False, Null);

  // clang-format off
  inline const auto wf_to_file =
    json::wf
    | (Top <<= File)
    | (File <<= Path * Contents)
    | (Contents <<= wf_value_tokens++[1])
    ;
  // clang-format on

  PassDef to_file(const std::filesystem::path& path)
  {
    Node dir = Directory;
    return {
      "to_file",
      wf_to_file,
      dir::bottomup | dir::once,
      {
        In(Top) * ValueToken++[Value] >>
          [path](Match& _) {
            return File << (Path ^ path.string()) << (Contents << _[Value]);
          },
      }};
  }
}

namespace trieste
{
  namespace json
  {
    Writer writer(
      const std::filesystem::path& path,
      bool prettyprint,
      bool sort_keys,
      const std::string& indent)
    {
      return Writer(
        "json",
        {to_file(path)},
        json::wf,
        [prettyprint, sort_keys, indent](std::ostream& os, Node contents) {
          for (Node value : *contents)
          {
            write_value(os, {prettyprint, sort_keys, indent}, "", value);
            os << std::endl;
          }
          return true;
        });
    }

    std::string to_string(
      Node json, bool prettyprint, bool sort_keys, const std::string& indent)
    {
      WFContext context(json::wf);
      std::ostringstream os;
      write_value(os, {prettyprint, sort_keys, indent}, "", json);
      return os.str();
    }
  }
}

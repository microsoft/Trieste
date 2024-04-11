#include "internal.h"
#include "trieste/wf.h"

namespace
{
  using namespace trieste;
  using namespace trieste::json;

  struct WriteSettings
  {
    bool prettyprint;
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
    for (std::size_t i = 0; i < object->size(); ++i)
    {
      Node member = object->at(i);
      assert(member == Member);
      if (settings.prettyprint)
      {
        os << new_indent;
      }

      write_value(os, settings, new_indent, member / String);
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
    Writer
    writer(const std::filesystem::path& path, bool prettyprint, const std::string& indent)
    {
      return Writer(
        "json",
        {to_file(path)},
        json::wf,
        [prettyprint, indent](std::ostream& os, Node contents) {
          for (Node value : *contents)
          {
            write_value(os, {prettyprint, indent}, "", value);
            os << std::endl;
          }
          return true;
        });
    }

    std::string
    to_string(Node json, bool prettyprint, const std::string& indent)
    {
      wf::push_back(json::wf);
      std::ostringstream os;
      write_value(os, {prettyprint, indent}, "", json);
      wf::pop_front();
      return os.str();
    }
  }
}

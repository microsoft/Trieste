#include "json.h"

namespace trieste::json
{
  JSONEmitter::JSONEmitter(bool prettyprint, const std::string& indent)
  : m_prettyprint(prettyprint), m_indent(indent)
  {}

  void JSONEmitter::emit_object(
    std::ostream& os, const std::string& indent, const Node& object)
  {
    if (object->empty())
    {
      os << "{}";
      return;
    }

    std::string new_indent = indent + "  ";
    os << "{";
    if (m_prettyprint)
    {
      os << std::endl;
    }
    for (std::size_t i = 0; i < object->size(); ++i)
    {
      Node member = object->at(i);
      if (m_prettyprint)
      {
        os << new_indent;
      }

      emit_value(os, new_indent, member->front());
      os << ":";
      if (m_prettyprint)
      {
        os << " ";
      }
      emit_value(os, new_indent, member->back());

      if (i < object->size() - 1)
      {
        os << ",";
      }
      if (m_prettyprint)
      {
        os << std::endl;
      }
    }
    if (m_prettyprint)
    {
      os << indent;
    }
    os << "}";
  }

  void JSONEmitter::emit_array(
    std::ostream& os, const std::string& indent, const Node& array)
  {
    if (array->empty())
    {
      os << "[]";
      return;
    }

    std::string new_indent = indent + "  ";
    os << "[";
    if (m_prettyprint)
    {
      os << std::endl;
    }
    for (std::size_t i = 0; i < array->size(); ++i)
    {
      Node element = array->at(i);
      if (m_prettyprint)
      {
        os << new_indent;
      }
      emit_value(os, new_indent, element);

      if (i < array->size() - 1)
      {
        os << ",";
      }

      if (m_prettyprint)
      {
        os << std::endl;
      }
    }
    if (m_prettyprint)
    {
      os << indent;
    }
    os << "]";
  }

  void JSONEmitter::emit_value(
    std::ostream& os, const std::string& indent, const Node& value)
  {
    if (value->in({Number, String, True, False, Null}))
    {
      os << value->location().view();
    }
    else if (value == Object)
    {
      emit_object(os, indent, value);
    }
    else if (value == Array)
    {
      emit_array(os, indent, value);
    }
    else
    {
      std::ostringstream message;
      message << "Unspected node type: " << value->type().str();
      throw std::runtime_error(message.str());
    }
  }

  void JSONEmitter::emit(std::ostream& os, const Node& value)
  {
    if (value == Top)
    {
      for (auto element : *value)
      {
        emit_value(os, "", element);
      }
    }
    else
    {
      emit_value(os, "", value);
    }
  }
}

#include "json.h"

#include "internal.h"
#include "trieste/utf8.h"

#include <optional>
#include <stdexcept>

// clang format on
namespace
{
  using namespace trieste;
  using namespace trieste::json;

  bool value_equal(Node lhs, Node rhs);

  bool object_equal(Node lhs, Node rhs)
  {
    if (lhs->size() != rhs->size())
    {
      return false;
    }
    std::vector<Node> lhs_members(lhs->begin(), lhs->end());
    std::vector<Node> rhs_members(rhs->begin(), rhs->end());
    std::sort(lhs_members.begin(), lhs_members.end(), [](Node x, Node y) {
      return x->front()->location().view() < y->front()->location().view();
    });
    std::sort(rhs_members.begin(), rhs_members.end(), [](Node x, Node y) {
      return x->front()->location().view() < y->front()->location().view();
    });
    for (std::size_t i = 0; i < lhs_members.size(); ++i)
    {
      if (
        lhs_members[i]->front()->location().view() !=
        rhs_members[i]->front()->location().view())
      {
        return false;
      }
      if (!value_equal(lhs_members[i]->back(), rhs_members[i]->back()))
      {
        return false;
      }
    }
    return true;
  }

  bool array_equal(Node lhs, Node rhs)
  {
    if (lhs->size() != rhs->size())
    {
      return false;
    }
    for (std::size_t i = 0; i < lhs->size(); ++i)
    {
      if (!value_equal(lhs->at(i), rhs->at(i)))
      {
        return false;
      }
    }
    return true;
  }

  bool value_equal(Node lhs, Node rhs)
  {
    if (lhs->type() != rhs->type())
    {
      return false;
    }
    if (lhs->type() == Object)
    {
      return object_equal(lhs, rhs);
    }
    if (lhs->type() == Array)
    {
      return array_equal(lhs, rhs);
    }
    if (lhs->type() == Top)
    {
      return array_equal(lhs, rhs);
    }

    if (lhs->type() == Number)
    {
      double lhs_value = std::stod(std::string(lhs->location().view()));
      double rhs_value = std::stod(std::string(rhs->location().view()));
      return lhs_value == rhs_value;
    }
    return lhs->location().view() == rhs->location().view();
  }

  struct DebugJson
  {
    Node value;
  };

  static std::ostream& operator<<(std::ostream& os, const DebugJson& debug)
  {
    return os << json::to_string(debug.value);
  }

  namespace pointer
  {
    class Pointer : public std::vector<Location>
    {
    public:
      Pointer(Location path) noexcept : m_path(path)
      {
        size_t pos = 0;
        while (pos < path.len)
        {
          auto maybe_key = next_key(path, pos);
          if (!maybe_key.has_value())
          {
            m_error = err(json::String ^ path, "Invalid pointer");
            break;
          }

          logging::Trace() << "Pointer[" << size()
                           << "] = " << maybe_key.value().view();

          push_back(maybe_key.value());
        }
      }

      const Location& path() const
      {
        return m_path;
      }

      bool is_valid() const
      {
        return m_error == nullptr;
      }

      Node error() const
      {
        return m_error;
      }

    private:
      static std::optional<Location> next_key(const Location& path, size_t& pos)
      {
        const std::string_view& path_view = path.view();
        if (path_view[pos] != '/')
        {
          return std::nullopt;
        }

        bool needs_replacement = false;
        size_t end = pos + 1;
        while (end < path.len)
        {
          if (path_view[end] == '/')
          {
            break;
          }

          if (path_view[end] == '~')
          {
            needs_replacement = true;
          }

          end += 1;
        }

        pos += 1; // remove the prepending `/`
        Location key(path.source, path.pos + pos, end - pos);
        pos = end;
        if (!needs_replacement)
        {
          return key;
        }

        auto maybe_unescaped = unescape_key(key.view());
        if (!maybe_unescaped.has_value())
        {
          return std::nullopt;
        }

        return Location(maybe_unescaped.value());
      }

      static std::optional<std::string>
      unescape_key(const std::string_view& pointer)
      {
        std::ostringstream os;
        size_t index = 0;
        while (index < pointer.size())
        {
          char c = pointer[index];
          assert(c != '/');
          if (c == '~')
          {
            if (index + 1 == pointer.size())
            {
              logging::Error() << "Invalid `~` in pointer";
              return std::nullopt;
            }

            char i = pointer[index + 1];
            if (i == '0')
            {
              os << '~';
            }
            else if (i == '1')
            {
              os << '/';
            }
            else
            {
              logging::Error() << "Invalid escape value '" << i << "'";
              return std::nullopt;
            }

            index += 2;
            continue;
          }

          os << c;
          index += 1;
        }

        return os.str();
      }

      Node m_error;
      Location m_path;
    };

    enum class Action
    {
      Insert,
      Read,
      Replace,
      Remove,
      Compare
    };

    struct DebugAction
    {
      Action action;
    };

    static std::ostream& operator<<(std::ostream& os, const DebugAction debug)
    {
      switch (debug.action)
      {
        case Action::Insert:
          return os << "insert";

        case Action::Read:
          return os << "read";

        case Action::Replace:
          return os << "replace";

        case Action::Remove:
          return os << "remove";

        case Action::Compare:
          return os << "compare";
      }

      throw std::runtime_error("Unrecognized Action value");
    }

    class Operation
    {
    public:
      Operation(Location path, Action action, Node value)
      : m_pointer(path), m_action(action), m_value(value)
      {}

      Operation(Location path, Action action) : Operation(path, action, nullptr)
      {}

      Node run(Node document) const
      {
        if (!m_pointer.is_valid())
        {
          return m_pointer.error();
        }

        WFContext ctx(json::wf);

        if (m_pointer.empty())
        {
          switch (m_action)
          {
            case Action::Read:
              return document;

            case Action::Replace:
              return m_value;

            case Action::Remove:
              return err(
                json::String ^ m_pointer.path(), "Cannot remove the root node");

            case Action::Compare:
              return compare(document, m_value);

            case Action::Insert:
              return m_value;
          }
        }

        Node current = document;
        for (size_t i = 0; i < m_pointer.size() - 1; ++i)
        {
          const Location& key = m_pointer[i];

          if (!current->in({json::Array, json::Object}))
          {
            return err(current, "Cannot index into value");
          }

          if (current == json::Object)
          {
            Nodes results = current->lookdown(key);
            if (results.empty())
            {
              return err(
                current, "No child at path: " + std::string(key.view()));
            }

            current = results.front() / json::Value;
            continue;
          }

          size_t index = 0;
          current = array_lookup(current, key, index);
          if (current == Error)
          {
            return current;
          }
        }

        if (!current->in({Object, Array}))
        {
          return err(current, "Cannot index into value");
        }

        const Location& key = m_pointer.back();
        if (current == json::Object)
        {
          return object_action(current, key);
        }

        return array_action(current, key);
      }

    private:
      static Node compare(Node actual, Node expected)
      {
        if (actual == Error)
        {
          return actual;
        }

        if (!equal(actual, expected))
        {
          std::string actual_json = json::to_string(actual, false, true);
          std::string expected_json = json::to_string(expected, false, true);
          std::ostringstream os;
          os << actual_json << " != " << expected_json;
          return err(actual, os.str());
        }

        return actual;
      }

      Node object_action(Node object, const Location& key) const
      {
        assert(object == Object);

        logging::Trace() << "Pointer: Object action " << DebugAction{m_action}
                         << " on object " << object << " at key " << key.view();

        Node existing;
        Node member;
        Nodes results = object->lookdown(key);
        if (results.empty())
        {
          existing = nullptr;
        }
        else
        {
          member = results.front();
          existing = member / json::Value;
        }

        switch (m_action)
        {
          case Action::Compare:
            if (results.empty())
            {
              return err(
                object,
                "Member does not exist with key: " + std::string(key.view()));
            }
            return compare(existing, m_value);

          case Action::Insert:
            if (existing == nullptr)
            {
              object << (Member << (Key ^ key) << m_value->clone());
              return m_value;
            }
            else
            {
              member / json::Value = m_value->clone();
              return existing;
            }

          case Action::Read:
            if (existing == nullptr)
            {
              return err(
                object,
                "Member does not exist with key: " + std::string(key.view()));
            }
            return existing;

          case Action::Remove:
            if (existing == nullptr)
            {
              return err(
                object,
                "Member does not exist with key: " + std::string(key.view()));
            }
            object->replace(member);
            return existing;

          case Action::Replace:
            if (results.empty())
            {
              return err(
                object,
                "Member does not exist with key: " + std::string(key.view()));
            }
            member / json::Value = m_value->clone();
            return existing;
        }

        throw std::runtime_error("Unsupported Action value");
      }

      Node array_action(Node array, const Location& key) const
      {
        assert(array == Array);
        size_t index = 0;

        logging::Trace() << "Pointer: Array action " << DebugAction{m_action}
                         << " on array " << array << " at index " << key.view();

        Node element = array_lookup(array, key, index);

        if (m_action == Action::Insert && index == array->size())
        {
          array << m_value->clone();
          return m_value;
        }

        if (element == Error)
        {
          return element;
        }

        switch (m_action)
        {
          case Action::Compare:
            return compare(element, m_value->clone());

          case Action::Insert:
            array->insert(array->begin() + index, m_value->clone());
            return m_value;

          case Action::Read:
            return element;

          case Action::Remove:
            array->replace(element);
            return element;

          case Action::Replace:
            array->replace_at(index, m_value->clone());
            return element;
        }

        throw std::runtime_error("Unsupported Action value");
      }

      static Node array_lookup(Node current, Location key, size_t& index)
      {
        std::string_view index_view = key.view();
        if (index_view == "-")
        {
          index = current->size();
          return err(
            json::String ^ key,
            "End-of-array selector `-` cannot appear inside a pointer, only "
            "at the end");
        }

        if (index_view.front() == '-')
        {
          return err(
            json::String ^ key,
            "unable to parse array index (prepended by `-`)");
        }

        if (index_view.front() == '0' && index_view.size() > 1)
        {
          return err(json::String ^ key, "Leading zeros");
        }

        index = 0;
        for (auto it = index_view.begin(); it < index_view.end(); ++it)
        {
          char c = *it;
          if (!std::isdigit(c))
          {
            return err(
              json::String ^ key, "unable to parse array index (not a digit)");
          }

          index =
            index * 10 + static_cast<size_t>(c) - static_cast<size_t>('0');
        }

        if (index >= current->size())
        {
          return err(
            json::Number ^ key,
            "index is greater than number of items in array");
        }

        return current->at(index);
      }

      Pointer m_pointer;
      Action m_action;
      Node m_value;
    };
  }

  namespace patch
  {
    const Location OpKey{"/op"};
    const Location PathKey{"/path"};
    const Location ValueKey{"/value"};
    const Location FromKey{"/from"};
    const Location MissingOp{"missing `op`"};
    const Location InvalidOp{"invalid `op` value"};
    const Location MissingPath{"missing `path`"};
    const Location MissingValue{"missing `value`"};
    const Location MissingFrom{"missing `from`"};

    enum class Type
    {
      Error,
      Test,
      Add,
      Remove,
      Replace,
      Copy,
      Move
    };

    const std::map<Location, Type> TypeLookup{
      {{"test"}, Type::Test},
      {{"add"}, Type::Add},
      {{"remove"}, Type::Remove},
      {{"replace"}, Type::Replace},
      {{"copy"}, Type::Copy},
      {{"move"}, Type::Move}};

    class Op
    {
    public:
      static Op from_node(Node node)
      {
        auto maybe_type = select_string(node, OpKey);
        if (!maybe_type.has_value())
        {
          return Op(node, Type::Error, MissingOp);
        }

        auto it = TypeLookup.find(maybe_type.value());
        if (it == TypeLookup.end())
        {
          return Op(node, Type::Error, InvalidOp);
        }

        Type type = it->second;

        auto maybe_path = select_string(node, PathKey);
        if (!maybe_path.has_value())
        {
          return Op(node, Type::Error, MissingPath);
        }

        const Location& path = maybe_path.value();

        switch (type)
        {
          case Type::Remove:
            return Op(node, Type::Remove, path);

          case Type::Add:
          case Type::Replace:
          case Type::Test:
          {
            Node value = select(node, ValueKey);
            if (value == Error)
            {
              return Op(node, Type::Error, MissingValue);
            }

            return Op(node, type, path, value);
          }

          case Type::Copy:
          case Type::Move:
          {
            auto maybe_from = select_string(node, FromKey);
            if (!maybe_from.has_value())
            {
              return Op(node, Type::Error, MissingFrom);
            }

            return Op(node, type, path, maybe_from.value());
          }

          default:
            return {node, Type::Error, {"Unknown error"}};
        }
      }

      bool operator<(const Op& other) const
      {
        return m_type < other.m_type;
      }

      Node apply(const Node& document) const
      {
        logging::Debug() << "Applying patch " << DebugJson{m_node};
        switch (m_type)
        {
          case Type::Test:
            return test(document);

          case Type::Add:
            return add(document);

          case Type::Remove:
            return remove(document);

          case Type::Replace:
            return replace(document);

          case Type::Move:
            return move(document);

          case Type::Copy:
            return copy(document);

          default:
            return err(m_node, "Unsupported operation");
        }
      }

      Type type() const
      {
        return m_type;
      }

      Node node() const
      {
        return m_node;
      }

      Location path() const
      {
        return m_path;
      }

    private:
      Op(Node node, Type type, Location path)
      : m_node(node), m_type(type), m_path(path)
      {}

      Op(Node node, Type type, Location path, Node value)
      : m_node(node), m_type(type), m_path(path), m_value(value)
      {}

      Op(Node node, Type type, Location path, Location from)
      : m_node(node), m_type(type), m_path(path), m_from(from)
      {}

      Node test(Node document) const
      {
        Node result =
          pointer::Operation(m_path, pointer::Action::Compare, m_value)
            .run(document);
        return result == Error ? result : document;
      }

      Node add(Node document) const
      {
        if (m_path.len == 0)
        {
          return m_value->clone();
        }

        Node result =
          pointer::Operation(m_path, pointer::Action::Insert, m_value)
            .run(document);
        return result == Error ? result : document;
      }

      Node remove(Node document) const
      {
        Node result =
          pointer::Operation(m_path, pointer::Action::Remove).run(document);
        return result == Error ? result : document;
      }

      Node replace(Node document) const
      {
        if (m_path.len == 0)
        {
          return m_value->clone();
        }

        Node result =
          pointer::Operation(m_path, pointer::Action::Replace, m_value)
            .run(document);
        return result == Error ? result : document;
      }

      Node move(Node document) const
      {
        if (m_path == m_from)
        {
          return document;
        }

        Node existing =
          pointer::Operation(m_from, pointer::Action::Remove).run(document);
        if (existing == Error)
        {
          return existing;
        }

        Node result =
          pointer::Operation(m_path, pointer::Action::Insert, existing)
            .run(document);
        return result == Error ? result : document;
      }

      Node copy(Node document) const
      {
        if (m_path == m_from)
        {
          return document;
        }

        Node existing =
          pointer::Operation(m_from, pointer::Action::Read).run(document);
        if (existing == Error)
        {
          return existing;
        }

        Node result =
          pointer::Operation(m_path, pointer::Action::Insert, existing)
            .run(document);
        return result == Error ? result : document;
      }

      Node m_node;
      Type m_type;
      Location m_path;
      Node m_value;
      Location m_from;
    };
  }
}

namespace trieste
{
  namespace json
  {
    bool equal(Node lhs, Node rhs)
    {
      return value_equal(lhs, rhs);
    }

    std::string escape(const std::string_view& string)
    {
      std::ostringstream buf;
      for (auto it = string.begin(); it != string.end(); ++it)
      {
        switch (*it)
        {
          case '\b':
            buf << '\\' << 'b';
            break;

          case '\f':
            buf << '\\' << 'f';
            break;

          case '\n':
            buf << '\\' << 'n';
            break;

          case '\r':
            buf << '\\' << 'r';
            break;

          case '\t':
            buf << '\\' << 't';
            break;

          case '\\':
            buf << '\\' << '\\';
            break;

          case '"':
            buf << '\\' << '"';
            break;

          default:
            buf << *it;
            break;
        }
      }

      return buf.str();
    }

    std::string escape_unicode(const std::string_view& string)
    {
      std::ostringstream os;
      std::size_t pos = 0;
      while (pos < string.size())
      {
        auto [r, s] = utf8::utf8_to_rune(string.substr(pos), false);
        if (r.value > 0x7FFF)
        {
          // JSON does not support escaping runes which require more than 2
          // bytes
          os << "\\uFFFD"; // BAD
          pos += s.size();
          continue;
        }
        if (r.value > 0x7F)
        {
          os << "\\u" << std::uppercase << std::setfill('0') << std::setw(4)
             << std::hex << r.value;
          pos += s.size();
          continue;
        }
        switch ((char)r.value)
        {
          case '\b':
            os << '\\' << 'b';
            break;
          case '\f':
            os << '\\' << 'f';
            break;
          case '\n':
            os << '\\' << 'n';
            break;
          case '\r':
            os << '\\' << 'r';
            break;
          case '\t':
            os << '\\' << 't';
            break;
          case '\\':
            os << '\\' << '\\';
            break;
          case '"':
            os << '\\' << '"';
            break;
          default:
            os << (char)r.value;
            break;
        }
        pos += s.size();
      }
      return os.str();
    }
    std::string unescape(const std::string_view& string)
    {
      std::string temp = utf8::unescape_hexunicode(string);
      std::string result;
      result.reserve(temp.size());
      for (auto it = temp.begin(); it != temp.end(); ++it)
      {
        if (*it == '\\')
        {
          it++;
          switch (*it)
          {
            case 'b':
              result.push_back('\b');
              break;
            case 'f':
              result.push_back('\f');
              break;
            case 'n':
              result.push_back('\n');
              break;
            case 'r':
              result.push_back('\r');
              break;
            case 't':
              result.push_back('\t');
              break;
            case '\\':
            case '/':
            case '"':
              result.push_back(*it);
              break;
            default:
              throw std::runtime_error("Invalid escape sequence");
          }
        }
        else
        {
          result.push_back(*it);
        }
      }
      return result;
    }

    Node object(const std::initializer_list<Node>& members)
    {
      Node object = Object << members;
      wf.build_st(object);
      return object;
    }

    Node member(Node key, Node value)
    {
      return Member << key << value;
    }

    Node array(const std::initializer_list<Node>& elements)
    {
      return Array << elements;
    }

    Node value(const std::string& value)
    {
      std::ostringstream os;
      os << '"' << value << '"';
      return String ^ os.str();
    }

    Node value(const double value)
    {
      std::ostringstream os;
      os << std::noshowpoint << value;
      return Number ^ os.str();
    }

    Node boolean(bool value)
    {
      return value ? (True ^ "true") : (False ^ "false");
    }

    Node null()
    {
      return (Null ^ "null");
    }

    std::optional<double> get_number(const Node& node)
    {
      if (node == json::Number)
      {
        try
        {
          std::string value(node->location().view());
          return std::stod(value);
        }
        catch (const std::exception& ex)
        {
          logging::Error() << "Unable to parse double: " << ex.what();
          return std::nullopt;
        }
      }

      logging::Error() << "Attempted to get double from " << node;
      return std::nullopt;
    }

    std::optional<bool> get_boolean(const Node& node)
    {
      if (node == json::True)
      {
        return true;
      }

      if (node == json::False)
      {
        return false;
      }

      logging::Error() << "Attempted to get boolean from " << node;
      return std::nullopt;
    }

    std::optional<Location> get_string(const Node& node)
    {
      if (node == json::String)
      {
        Location result = node->location();
        result.pos += 1;
        result.len -= 2;
        return result;
      }

      logging::Debug() << "Attempted to get string from " << node;
      return std::nullopt;
    }

    Node select(const Node& document, const Location& path)
    {
      return pointer::Operation(path, pointer::Action::Read).run(document);
    }

    std::optional<double>
    select_number(const Node& document, const Location& path)
    {
      Node node = select(document, path);
      if (node == Error)
      {
        logging::Debug() << node;
        return std::nullopt;
      }

      return get_number(node);
    }

    std::optional<bool>
    select_boolean(const Node& document, const Location& path)
    {
      Node node = select(document, path);
      if (node == Error)
      {
        logging::Debug() << node;
        return std::nullopt;
      }

      return get_boolean(node);
    }

    std::optional<Location>
    select_string(const Node& document, const Location& path)
    {
      Node node = select(document, path);
      if (node == Error)
      {
        logging::Debug() << node;
        return std::nullopt;
      }

      return get_string(node);
    }

    Node patch(const Node& document, const Node& patch)
    {
      if (patch != json::Array)
      {
        return err(patch, "Not a JSON array");
      }

      if (patch->empty())
      {
        return document;
      }

      std::vector<patch::Op> ops;

      for (const auto& node : *patch)
      {
        auto op = patch::Op::from_node(node);
        if (op.type() == patch::Type::Error)
        {
          return err(op.node(), op.path());
        }

        if (op.type() == patch::Type::Test)
        {
          Node result = op.apply(document);
          if (result == Error)
          {
            return result;
          }

          continue;
        }

        ops.push_back(op);
      }

      Node patched = document->clone();
      wf.build_st(patched);

      for (const patch::Op& op : ops)
      {
        patched = op.apply(patched);
        if (patched == Error)
        {
          return patched;
        }

        wf.build_st(patched);

        logging::Debug() << "After: " << DebugJson{patched};
      }

      return patched;
    }
  }
}

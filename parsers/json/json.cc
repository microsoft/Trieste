#include "internal.h"
#include "trieste/utf8.h"

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
          // JSON does not support escaping UTF-16 runes
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

    Node object(const Nodes& members)
    {
      return Object << members;
    }

    Node member(Node key, Node value)
    {
      return Member << key << value;
    }
    Node array(const Nodes& elements)
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
  }
}

#include "internal.h"
#include "trieste/utf8.h"

// clang format on
namespace
{
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
}

namespace trieste
{
  namespace yaml
  { 
    using namespace trieste;

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
          // an empty string view does not start with a space
          if (
            (current.empty() || !std::isspace(current.front())) &&
            (next.empty() || !std::isspace(next.front())))
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
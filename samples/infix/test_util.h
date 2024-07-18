#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace
{
  inline std::vector<std::string> split_lines(const std::string& str)
  {
    // You would think this could be implemented more simply with something like
    // std::getline(), but that function doesn't actually get "lines". It
    // approximates lines using a single separator, defaulting to "\n", and
    // would break down when using DOS line endings, for example. This
    // implementation should correctly deconstruct a string printed using
    // std::endl on any platform.
    using namespace std::string_view_literals;
    std::vector<std::string> lines;

    std::istringstream in(str);
    std::string line;

    std::size_t cursor = 0;

    auto try_match = [&](std::string_view part) -> bool {
      if (std::string_view(str).substr(cursor, part.size()) == part)
      {
        cursor += part.size();
        return true;
      }
      else
      {
        return false;
      }
    };

    while (cursor < str.size())
    {
      if (try_match("\r\n"sv) || try_match("\n"sv) || try_match("\r"sv))
      {
        lines.emplace_back(std::move(line));
        line.clear();
      }
      else
      {
        line += str.at(cursor);
        ++cursor;
      }
    }

    if (!line.empty())
    {
      lines.emplace_back(std::move(line));
    }

    return lines;
  }

  inline void trim_trailing_whitespace(std::string& str)
  {
    while (!str.empty() &&
           (str.back() == ' ' || str.back() == '\n' || str.back() == '\r'))
    {
      str.pop_back();
    }
  }

  inline void diffy_print(
    const std::string& expected, const std::string& actual, std::ostream& out)
  {
    auto expected_lines = split_lines(expected);
    auto actual_lines = split_lines(actual);

    std::size_t pos = 0;
    for (const auto& actual_line : actual_lines)
    {
      if (pos < expected_lines.size())
      {
        auto expected_line = expected_lines[pos];
        if (actual_line == expected_line)
        {
          out << "  " << actual_line << std::endl;
        }
        else
        {
          out << "! " << actual_line << std::endl;
        }
      }
      else if (pos - expected_lines.size() > 3)
      {
        out << "..." << std::endl;
        break;
      }
      else
      {
        out << "+ " << actual_line << std::endl;
      }

      ++pos;
    }
  }

  template<typename T>
  inline std::ostream& operator<<(std::ostream& out, const std::vector<T>& vec)
  {
    out << "[";
    bool first = true;
    for (const auto& elem : vec)
    {
      if (first)
      {
        first = false;
      }
      else
      {
        out << ", ";
      }
      out << std::string(elem);
    }
    out << "]";
    return out;
  }
}

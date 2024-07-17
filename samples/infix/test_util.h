#pragma once

#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace
{
  inline std::vector<std::string> split_lines(const std::string& str)
  {
    std::vector<std::string> lines;

    std::istringstream in(str);
    std::string line;
    while (std::getline(in, line))
    {
      lines.push_back(line);
    }

    return lines;
  }

  inline void trim_trailing_whitespace(std::string& str)
  {
    while (!str.empty() && std::isspace(str.back(), std::locale("")))
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

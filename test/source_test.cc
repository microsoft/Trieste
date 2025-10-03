#include "CLI/App.hpp"
#include "CLI/Error.hpp"

#include <CLI/CLI.hpp>
#include <trieste/trieste.h>

namespace
{
  using namespace std::string_view_literals;
  using namespace std::string_literals;

  std::string escape_string(const std::string str)
  {
    std::string result;
    for (auto ch : str)
    {
      // This is far from all special chars, but it works for what we emit.
      switch (ch)
      {
        case '\n':
          result += "\\n"sv;
          break;
        case '\r':
          result += "\\r"sv;
          break;
        default:
          result += ch;
      }
    }

    return result;
  }

  struct ExpectedLineCol
  {
    size_t pos;
    std::pair<size_t, size_t> expected_linecol;
  };

  std::ostream&
  operator<<(std::ostream& out, const ExpectedLineCol& expected_linecol)
  {
    out << "pos = " << expected_linecol.pos << std::endl
        << "line = " << expected_linecol.expected_linecol.first << std::endl
        << "col = " << expected_linecol.expected_linecol.second << std::endl;
    return out;
  }

  struct ExpectedLinePos
  {
    size_t line;
    std::pair<size_t, size_t> expected_linepos;
  };

  std::ostream&
  operator<<(std::ostream& out, const ExpectedLinePos& expected_linepos)
  {
    out << "line = " << expected_linepos.line << std::endl
        << "pos = " << expected_linepos.expected_linepos.first << std::endl
        << "len = " << expected_linepos.expected_linepos.second << std::endl;
    return out;
  }

  struct ExpectedLocationStr
  {
    size_t pos, len;
    bool has_newline;
    size_t last_line_idx;
    std::string expected_begin, expected_middle, expected_end;
  };

  std::ostream&
  operator<<(std::ostream& out, const ExpectedLocationStr& expected_location)
  {
    out << "pos = " << expected_location.pos << std::endl
        << "len = " << expected_location.len << std::endl
        << "has_newline = " << expected_location.has_newline << std::endl
        << "last_line_idx = " << expected_location.last_line_idx << std::endl
        << "expected_begin = \""
        << escape_string(expected_location.expected_begin) << "\"" << std::endl
        << "expected_middle = \""
        << escape_string(expected_location.expected_middle) << "\"" << std::endl
        << "expected_end = \"" << escape_string(expected_location.expected_end)
        << "\"" << std::endl;
    return out;
  }

  struct LinesTest
  {
    std::string input;

    size_t curr_line_idx;
    size_t next_line_idx;
    std::string last_visible_line;

    std::vector<ExpectedLinePos> expected_linepos;
    std::vector<ExpectedLineCol> expected_linecol;
    std::vector<ExpectedLocationStr> expected_location_str;

    bool check_all() const
    {
      auto source = trieste::SourceDef::synthetic(input, "test");
      if (source->origin() != "test")
      {
        std::cout << "origin mismatch: '" << source->origin() << "' != 'test'";
      }

      for (const auto& check : expected_linepos)
      {
        auto actual = source->linepos(check.line);

        if (actual != check.expected_linepos)
        {
          const auto& expected = check.expected_linepos;
          std::cout << "Error finding linepos(line = " << check.line
                    << ") in string \"" << escape_string(input) << "\"."
                    << std::endl
                    << "           (start, size)" << std::endl
                    << "expected = (" << expected.first << ", "
                    << expected.second << ")" << std::endl
                    << "actual   = (" << actual.first << ", " << actual.second
                    << ")" << std::endl;
          return false;
        }
      }

      for (const auto& check : expected_linecol)
      {
        auto actual = source->linecol(check.pos);

        if (actual != check.expected_linecol)
        {
          const auto& expected = check.expected_linecol;
          std::cout << "Error finding linecol(pos = " << check.pos
                    << ") in string \"" << escape_string(input) << "\"."
                    << std::endl
                    << "           (line, col)" << std::endl
                    << "expected = (" << expected.first << ", "
                    << expected.second << ")" << std::endl
                    << "actual   = (" << actual.first << ", " << actual.second
                    << ")" << std::endl;
          return false;
        }
      }

      for (const auto& check : expected_location_str)
      {
        std::string expected_str = ""s;
        if (check.has_newline)
        {
          expected_str = check.expected_begin;
          expected_str += '\n';
        }
        expected_str +=
          check.expected_middle + "\n" + check.expected_end + "\n";

        auto actual = trieste::Location(source, check.pos, check.len).str();

        if (actual != expected_str)
        {
          std::cout << "Error finding Location(source, pos = " << check.pos
                    << ", len = " << check.len << ").str() in string \""
                    << escape_string(input) << "\"." << std::endl
                    << "expected = \"" << escape_string(expected_str) << "\""
                    << std::endl
                    << "actual = \"" << escape_string(actual) << "\""
                    << std::endl;
          return false;
        }
      }

      return true;
    }

    void dump() const
    {
      std::cout << "input = \"" << escape_string(input) << "\"" << std::endl
                << "curr_line_idx = " << curr_line_idx << std::endl
                << "next_line_idx = " << next_line_idx << std::endl
                << "last_visible_line = \"" << escape_string(last_visible_line)
                << "\"" << std::endl;

      std::cout << std::endl;
      std::cout << "Expected linecol list:" << std::endl;
      for (const auto& linecol : expected_linecol)
      {
        std::cout << "---" << std::endl << linecol;
      }

      std::cout << std::endl;
      std::cout << "Expected linepos list:" << std::endl;
      for (const auto& linepos : expected_linepos)
      {
        std::cout << "---" << std::endl << linepos;
      }

      std::cout << std::endl;
      std::cout << "Expected location str list:" << std::endl;
      for (const auto& location_str : expected_location_str)
      {
        std::cout << "---" << std::endl << location_str;
      }
    }
  };

  std::vector<LinesTest>
  build_cases_size_n(size_t n, bool skip_carriage_returns)
  {
    if (n == 0)
    {
      return {
        {""s,
         0,
         1,
         ""s,
         {
           {0, {0, 0}},
         },
         {
           {0, {0, 0}},
         },
         {
           {
             0,
             0,
             false,
             0,
             ""s,
             ""s,
             ""s,
           },
         }}};
    }

    auto cases_pre = build_cases_size_n(n - 1, skip_carriage_returns);
    std::vector<LinesTest> cases;
    for (auto case_pre : cases_pre)
    {
      size_t old_size = case_pre.input.size();
      assert(!case_pre.expected_linecol.empty());
      assert(!case_pre.expected_linepos.empty());

      auto gen_strs = [](LinesTest& cs) -> void {
        size_t next_idx = 0;
        auto existing_strs = cs.expected_location_str;
        // extend all the location str combos that ended at the boundary of the
        // old input
        for (const auto& existing_location_str : existing_strs)
        {
          // we modify the existing elements when the line view (but not span)
          // overlaps the extended input
          size_t curr_idx = next_idx;
          ++next_idx;

          // ... right here:
          // Locations whose line view (but not span) overlaps the extended part
          // of input must gain a char in their line view.
          {
            auto& location_str = cs.expected_location_str.at(curr_idx);
            // ... but only if our char is not a control char or newline
            if (cs.input.back() != '\n' && cs.input.back() != '\r')
            {
              if (location_str.last_line_idx == cs.curr_line_idx)
              {
                location_str.expected_middle += cs.input.back();
              }
            }
          }

          if (
            existing_location_str.pos + existing_location_str.len + 1 !=
            cs.input.size())
          {
            // skip locations that don't end at the end of the string
            continue;
          }
          auto location_str = existing_location_str;
          location_str.len += 1;

          if (cs.input.back() == '\n')
          {
            if (!location_str.has_newline)
            {
              location_str.expected_begin =
                std::move(location_str.expected_end);
            }
            location_str.expected_middle += '\n';
            location_str.expected_end = ""s;
            location_str.has_newline = true;
            location_str.last_line_idx += 1;
          }
          else if (cs.input.back() == '\r')
          {
            // pass
          }
          else if (cs.input.back() == 'a')
          {
            // It doesn't matter if there's a newline or not.
            // Adding a char at the end always grows the '~' at the bottom.
            location_str.expected_middle += 'a';
            location_str.expected_end += '~';
          }
          else
          {
            std::abort();
          }

          cs.expected_location_str.emplace_back(std::move(location_str));
        }

        // The one extra case: when we point exactly to the end of the input.
        {
          ExpectedLocationStr location_str{
            cs.input.size(),
            0,
            false,
            cs.curr_line_idx,
            ""s,
            cs.last_visible_line,
            ""s,
          };
          for (size_t i = 0; i < cs.last_visible_line.size(); ++i)
          {
            location_str.expected_end += ' ';
          }
          cs.expected_location_str.emplace_back(std::move(location_str));
        }
      };

      // The newline character
      {
        auto cs = case_pre;
        cs.input += '\n';
        cs.expected_linepos.push_back({
          cs.next_line_idx,
          {cs.input.size(), 0},
        });
        cs.curr_line_idx += 1;
        cs.next_line_idx += 1;
        cs.last_visible_line = ""s;
        // the linebreak itself is considered to be on the next line
        cs.expected_linecol.push_back({
          old_size + 1,
          {
            case_pre.expected_linecol.back().expected_linecol.first + 1,
            0,
          },
        });
        // generate all the extra location str prints
        gen_strs(cs);

        cases.emplace_back(std::move(cs));
      }
      // not a new line:
      // - character 'a', no need to be imaginative
      // - character '\r', same effect except for visibility in err strings
      for (char ch : {'a', '\r'})
      {
        if (skip_carriage_returns && ch == '\r')
        {
          continue;
        }

        auto cs = case_pre;
        cs.input += ch;
        // a) no change in line_idx_after, following chars still on same line
        // b) queries for info on the last line get a longer span by 1
        cs.expected_linepos.back().expected_linepos.second += 1;
        // c) queries for this pos get same line as previous pos, +1 column
        cs.expected_linecol.push_back({
          cs.input.size(),
          {
            case_pre.expected_linecol.back().expected_linecol.first,
            case_pre.expected_linecol.back().expected_linecol.second + 1,
          },
        });
        // the carriage return should not be considered "visible"
        if (ch != '\r')
        {
          cs.last_visible_line += ch;
        }
        // generate all the extra location str prints
        gen_strs(cs);

        cases.emplace_back(std::move(cs));
      }
    }
    return cases;
  }
}

int main(int argc, char** argv)
{
  auto app = CLI::App("Tester for Trieste's source location code");
  size_t depth = 0;
  bool verbose = false;
  bool skip_carriage_returns = false;
  app.add_option("--depth", depth, "Maximum test string length");
  app.add_flag(
    "--verbose", verbose, "Print the test cases that were generated.");
  app.add_flag(
    "--skip-carriage-returns",
    skip_carriage_returns,
    "Disable constructing inputs with '\r' in them.");

  try
  {
    app.parse(argc, argv);
  }
  catch (CLI::ParseError& err)
  {
    app.exit(err);
  }

  std::vector<LinesTest> cases;
  for (size_t i = 0; i <= depth; ++i)
  {
    for (auto&& cs : build_cases_size_n(i, skip_carriage_returns))
    {
      cases.emplace_back(std::move(cs));
    }
  }
  {
    size_t case_count = 0;
    for (const auto& cs : cases)
    {
      case_count += cs.expected_linecol.size();
      case_count += cs.expected_linepos.size();
      case_count += cs.expected_location_str.size();
    }

    std::cout << "Generated " << cases.size() << " inputs, or " << case_count
              << " distinct test cases, up to depth " << depth << "."
              << std::endl;
  }

  if (verbose)
  {
    size_t idx = 0;
    for (const auto& cs : cases)
    {
      std::cout << std::endl;
      std::cout << "# Case " << idx << ":" << std::endl;
      cs.dump();
      ++idx;
    }
    std::cout << std::endl << std::endl;
  }

  for (const auto& cs : cases)
  {
    if (!cs.check_all())
    {
      std::cout << "Test failed, aborting." << std::endl;
      return 1;
    }
  }

  std::cout << "All " << cases.size() << " cases passed." << std::endl;
  return 0;
}

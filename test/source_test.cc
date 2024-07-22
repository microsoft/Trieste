#include "CLI/App.hpp"
#include "CLI/Error.hpp"

#include <CLI/CLI.hpp>
#include <trieste/trieste.h>

namespace
{
  using namespace std::string_view_literals;

  struct ExpectedLineCol
  {
    size_t pos;
    std::pair<size_t, size_t> expected_linecol;
  };

  struct ExpectedLinePos
  {
    size_t line;
    std::pair<size_t, size_t> expected_linepos;
  };

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

  struct LinesTest
  {
    std::string input;

    size_t line_idx_after;
    bool was_line_break;
    std::vector<ExpectedLinePos> expected_linepos;
    std::vector<ExpectedLineCol> expected_linecol;

    bool check_all() const
    {
      auto source = trieste::SourceDef::synthetic(input);

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

      return true;
    }
  };

  std::vector<LinesTest> build_cases_size_n(size_t n)
  {
    if (n == 0)
    {
      return {{
        "",
        0,
        false,
        {
          {0, {0, 0}},
        },
        {
          {0, {0, 0}},
        },
      }};
    }

    auto cases_pre = build_cases_size_n(n - 1);
    std::vector<LinesTest> cases;
    for (auto case_pre : cases_pre)
    {
      size_t old_size = case_pre.input.size();
      assert(!case_pre.expected_linecol.empty());
      assert(!case_pre.expected_linepos.empty());

      // Cases where case_pre needs "fixing" for a longer input.
      // Being at the end of the input matters and changes what output makes
      // sense.
      if (case_pre.was_line_break)
      {
        case_pre.was_line_break = false;
        // If we're adding a new char of any sort after a line break, we now
        // have an extra line, starting at size 0. But we didn't before, because
        // then we just had a trailing line break.
        case_pre.expected_linepos.push_back({
          case_pre.line_idx_after,
          {
            case_pre.input.size(),
            0,
          },
        });
        // Also, our linecol(pos = size) case shifts to the beginning of the
        // next line, now that there is one, as opposed to us trailing off
        // the end of the last line.
        case_pre.expected_linecol.back().expected_linecol = {
          case_pre.line_idx_after, 0};
      }

      // cover all line break variations
      for (auto nl : {"\r\n"sv, "\n"sv})
      {
        auto cs = case_pre;
        cs.input += nl;
        // a) chars after this line break get line number + 1
        cs.line_idx_after += 1;
        // b) chars inside the line break stay on the last line, but on extra
        // columns
        for (size_t inc = 1; inc <= nl.size(); ++inc)
        {
          cs.expected_linecol.push_back({
            old_size + inc,
            {
              case_pre.line_idx_after,
              case_pre.expected_linecol.back().expected_linecol.second + inc,
            },
          });
        }
        // c) we don't get a valid next line info if we're a trailing line
        // break, but the next char will change that
        cs.was_line_break = true;
        cases.emplace_back(std::move(cs));
      }
      // not a new line (no need to be imaginative here)
      {
        auto cs = case_pre;
        cs.input += "a"sv;
        // a) no change in line_idx_after, following chars still on same line
        // b) queries for info on the last line get a longer span by 1
        cs.expected_linepos.back().expected_linepos.second += 1;
        // c) queries for this pos get same line as previous pos, +1 column
        cs.expected_linecol.push_back({
          cs.input.size(),
          {
            case_pre.line_idx_after,
            case_pre.expected_linecol.back().expected_linecol.second + 1,
          },
        });
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
  app.add_option("--depth", depth, "Maximum test string length");

  try
  {
    app.parse(argc, argv);
  }
  catch (CLI::ParseError& err)
  {
    app.exit(err);
  }

  auto cases = build_cases_size_n(depth);

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

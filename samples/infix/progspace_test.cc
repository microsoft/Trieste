#include "CLI/App.hpp"
#include "CLI/Error.hpp"
#include "progspace.h"
#include "test_util.h"

#include <CLI/CLI.hpp>
#include <ios>
#include <sstream>

int main(int argc, char** argv)
{
  CLI::App app;
  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    app.exit(e);
  }

  struct StringTestExpected
  {
    bool tuple_parens_omitted;
    std::string str;

    // in C++20 this can be defaulted; kept like this for C++17 compat
    bool operator==(const StringTestExpected& other) const
    {
      return tuple_parens_omitted == other.tuple_parens_omitted &&
        str == other.str;
    }

    bool operator!=(const StringTestExpected& other) const
    {
      return !(*this == other);
    }

    inline explicit operator std::string() const
    {
      std::ostringstream out;
      out << "{" << std::endl
          << "  .tuple_parens_omitted = " << std::boolalpha
          << tuple_parens_omitted << ";" << std::endl
          << "  .str = \"" << str << "\";" << std::endl
          << "}";
      return out.str();
    }
  };

  auto expecteds_to_str = [](const std::vector<StringTestExpected>& vec) {
    std::ostringstream out;
    out << vec;
    return out.str();
  };

  struct StringTest
  {
    trieste::Node input;
    std::vector<StringTestExpected> expected;
  };

  using namespace infix;

  std::vector<StringTest> string_tests = {
    {
      Calculation
        << (Assign << (Ident ^ "foo")
                   << (Expression
                       << ((Add ^ "+")
                           << (Expression << (Int ^ "0"))
                           << (Expression
                               << ((Add ^ "+")
                                   << (Expression << (Int ^ "1"))
                                   << (Expression << (Int ^ "2"))))))),
      {
        // {
        //   .tuple_parens_omitted = false,
        //   .str = "foo = 0 + 1 + 2;",
        // },
        {
          false,
          "foo = 0 + (1 + 2);",
        },
        // {
        //   .tuple_parens_omitted = false,
        //   .str = "foo = (0 + 1 + 2);",
        // },
        {
          false,
          "foo = (0 + (1 + 2));",
        },
      },
    },
    {
      Calculation
        << (Assign << (Ident ^ "foo")
                   << (Expression
                       << ((Add ^ "+")
                           << (Expression
                               << ((Add ^ "+") << (Expression << (Int ^ "0"))
                                               << (Expression << (Int ^ "1"))))
                           << (Expression << (Int ^ "2"))))),
      {
        {
          false,
          "foo = 0 + 1 + 2;",
        },
        {
          false,
          "foo = (0 + 1) + 2;",
        },
        {
          false,
          "foo = (0 + 1 + 2);",
        },
        {
          false,
          "foo = ((0 + 1) + 2);",
        },
      },
    },
    {
      Calculation
        << (Assign << (Ident ^ "foo")
                   << (Expression
                       << (Tuple << (Expression << (Int ^ "1"))
                                 << (Expression << (Int ^ "2"))
                                 << (Expression << (Int ^ "3"))))),
      {
        {
          true,
          "foo = 1, 2, 3;",
        },
        {
          true,
          "foo = 1, 2, 3,;",
        },
        {
          false,
          "foo = (1, 2, 3);",
        },
        {
          false,
          "foo = (1, 2, 3,);",
        },
      },
    },
  };

  for (const auto& test : string_tests)
  {
    std::vector<StringTestExpected> actual;

    for (auto render : progspace::calculation_strings(test.input))
    {
      actual.push_back({
        render.tuple_parens_omitted,
        render.str.str(),
      });
    }

    if (test.expected != actual)
    {
      std::cout << "Unexpected stringification for:" << std::endl
                << test.input << std::endl
                << "Expected:" << std::endl
                << test.expected << std::endl
                << "Actual (diffy print):" << std::endl;
      diffy_print(
        expecteds_to_str(test.expected), expecteds_to_str(actual), std::cout);
      return 1;
    }
  }
  std::cout << "All ok." << std::endl;

  return 0;
}

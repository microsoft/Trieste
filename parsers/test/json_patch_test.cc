#include "trieste/json.h"
#include "trieste/logging.h"
#include "trieste/trieste.h"
#include "trieste/utf8.h"
#include "trieste/wf.h"

#include <CLI/CLI.hpp>
#include <cstddef>
#include <filesystem>
#include <stdexcept>

const std::string Green = "\x1b[32m";
const std::string Cyan = "\x1b[36m";
const std::string Yellow = "\x1b[33m";
const std::string Reset = "\x1b[0m";
const std::string Red = "\x1b[31m";
// const std::string White = "\x1b[37m";

using namespace trieste;

std::string replace_whitespace(const std::string& str)
{
#ifdef _WIN32
  return str;
#else
  std::ostringstream os;
  for (std::size_t i = 0; i < str.size(); ++i)
  {
    switch (str[i])
    {
      case ' ':
        os << utf8::rune(183);
        break;

      case '\t':
        os << utf8::rune(2192);
        break;

      default:
        os << str[i];
        break;
    }
  }

  return os.str();
#endif
}

void diff_line(
  const std::string& actual, const std::string& wanted, std::ostream& os)
{
  std::set<std::size_t> errors;
  std::size_t index = 0;
  std::size_t length = std::min(actual.size(), wanted.size());

  for (; index < length; ++index)
  {
    if (actual[index] != wanted[index])
    {
      errors.insert(index);
    }
  }

  length = std::max(actual.size(), wanted.size());
  for (; index < length; ++index)
  {
    errors.insert(index);
  }

  os << "wanted: " << replace_whitespace(wanted) << std::endl;
  os << "actual: " << replace_whitespace(actual) << std::endl;
  os << "        ";
  for (std::size_t i = 0; i < length; ++i)
  {
    if (errors.find(i) != errors.end())
    {
      os << "^";
    }
    else
    {
      os << " ";
    }
  }
  os << std::endl;
}

std::size_t newline_or_end(const std::string& str, std::size_t start)
{
  std::size_t newline = str.find('\n', start);
  if (newline == std::string::npos)
  {
    return str.size();
  }

  return newline;
}

void diff(
  const std::string& actual,
  const std::string& wanted,
  const std::string& label,
  std::ostream& os)
{
  os << "--- " << label << " ---" << std::endl;
  std::size_t a = 0;
  std::size_t w = 0;
  bool error = false;
  while (a < actual.size() && w < wanted.size())
  {
    std::size_t a_end = newline_or_end(actual, a);
    std::string a_line = actual.substr(a, a_end - a);

    std::size_t w_end = newline_or_end(wanted, w);
    std::string w_line = wanted.substr(w, w_end - w);

    if (a_line != w_line)
    {
      diff_line(a_line, w_line, os);
      error = true;
      break;
    }
    else
    {
      os << "  " << a_line;
    }
    os << std::endl;
    a = a_end + 1;
    w = w_end + 1;
  }

  if (!error)
  {
    while (a < actual.size())
    {
      std::size_t a_end = newline_or_end(actual, a);
      std::string a_line = actual.substr(a, a_end - a);
      os << "+ " << a_line << std::endl;
      a = a_end + 1;
    }

    while (w < wanted.size())
    {
      std::size_t w_end = newline_or_end(wanted, w);
      if (w_end == std::string::npos)
      {
        w_end = wanted.size();
      }

      std::string w_line = wanted.substr(w, w_end - w);
      os << "- " << w_line << std::endl;
      w = w_end + 1;
    }
  }

  os << "--- " << label << " ---" << std::endl;
}

bool is_ws(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void skip_whitespace(
  std::string::const_iterator& it, std::string::const_iterator end)
{
  while (it != end)
  {
    if (is_ws(*it))
    {
      it++;
    }
    else
    {
      break;
    }
  }
}

bool diff_json(const std::string& actual, const std::string& wanted)
{
  auto a = actual.begin();
  auto w = wanted.begin();
  while (a != actual.end() && w != wanted.end())
  {
    skip_whitespace(a, actual.end());
    skip_whitespace(w, wanted.end());

    if (*a != *w)
    {
      return true;
    }

    a++;
    w++;

    skip_whitespace(a, actual.end());
    skip_whitespace(w, wanted.end());
  }

  return a != actual.end() || w != wanted.end();
}

enum class Outcome
{
  Success,
  Error,
  IncorrectResult,
  ErrorMismatch,
  IncorrectSuccess,
  Skipped
};

struct TestResult
{
  Outcome outcome;
  std::string error;
};

struct TestCase
{
  Node node;
  std::string comment;
  Node doc;
  Node patch;
  Node expected;
  bool want_result;
  std::string expected_error;
  bool disabled;

  TestResult run()
  {
    if (disabled)
    {
      return {Outcome::Skipped, ""};
    }

    Node actual = json::patch(doc, patch);

    if (actual == Error)
    {
      WFContext ctx(json::wf);
      std::string actual_error((actual / ErrorMsg)->location().view());
      if (want_result)
      {
        logging::Debug() << actual;
        return {Outcome::Error, actual_error};
      }

      if (actual_error != expected_error)
      {
        std::ostringstream os;
        diff(actual_error, expected_error, "Error", os);
        return {Outcome::ErrorMismatch, os.str()};
      }

      return {Outcome::Success, ""};
    }

    if (!want_result)
    {
      return {Outcome::IncorrectSuccess, "Expected error: " + expected_error};
    }

    auto actual_json = json::to_string(actual, false, true);
    auto expected_json = json::to_string(expected, false, true);

    trieste::logging::Debug() << actual_json;

    if (diff_json(actual_json, expected_json))
    {
      std::ostringstream os;
      diff(actual_json, expected_json, "JSON", os);
      return {Outcome::IncorrectResult, os.str()};
    }

    return {Outcome::Success, ""};
  }

  static std::optional<TestCase> parse(Node node)
  {
    std::string comment(
      json::select_string(node, {"/comment"}).value_or(Location{}).view());

    Node doc = json::select(node, {"/doc"});
    if (doc == Error)
    {
      logging::Error() << doc;
      return std::nullopt;
    }

    Node patch = json::select(node, {"/patch"});
    if (patch == Error)
    {
      logging::Error() << patch;
      return std::nullopt;
    }

    bool disabled = json::select_boolean(node, {"/disabled"}).value_or(false);

    if (disabled)
    {
      return TestCase{node, comment, doc, patch, nullptr, false, "", true};
    }

    Node expected = json::select(node, {"/expected"});

    bool want_result = true;
    std::string expected_error;
    if (expected == Error)
    {
      expected = nullptr;
      want_result = false;
      auto maybe_error = json::select_string(node, {"/error"});
      if (!maybe_error.has_value())
      {
        logging::Error() << "missing error message (no expected node present)";
        return std::nullopt;
      }

      expected_error = std::string(maybe_error.value().view());
      if (comment.empty())
      {
        comment = expected_error;
      }
    }

    return TestCase{
      node,
      comment,
      doc,
      patch,
      expected,
      want_result,
      expected_error,
      disabled};
  }

  static bool
  load(std::vector<TestCase>& test_cases, const std::filesystem::path& path)
  {
    assert(path.extension() == ".json");

    if (!std::filesystem::exists(path))
    {
      logging::Error() << "Test file does not exist";
      return true;
    }

    auto result = json::reader().file(path).read();
    if (!result.ok)
    {
      logging::Error log;
      log << "Unable to load test JSON: ";
      result.print_errors(log);
      return true;
    }

    Node test_cases_array = result.ast->front();
    assert(test_cases_array == json::Array);
    for (auto element : *test_cases_array)
    {
      auto maybe_test_case = TestCase::parse(element);
      if (maybe_test_case.has_value())
      {
        auto& test_case = maybe_test_case.value();
        if (test_case.comment.empty())
        {
          test_case.comment = "unnamed" + std::to_string(test_cases.size());
        }

        test_cases.push_back(test_case);
        continue;
      }

      logging::Error() << "Unable to parse test case: " << element;
      return true;
    }

    return false;
  }
};

int main(int argc, char* argv[])
{
  CLI::App app;

  app.set_help_all_flag("--help-all", "Expand all help");

  std::filesystem::path cases_path;
  app.add_option(
    "cases,-c,--cases", cases_path, "Path to the test case JSON file");

  bool verbose{false};
  app.add_flag("-v,--verbose", verbose, "Verbose output (for debugging)");

  bool fail_first{false};
  app.add_flag(
    "-f,--fail-first", fail_first, "Stop after first test case failure");

  bool strict_messages{false};
  app.add_flag(
    "-s,--strict-messages",
    strict_messages,
    "Emit warnings when error messages do not match");

  std::string name_match;
  app.add_option(
    "-n,--name",
    name_match,
    "Note (or note substring) of specific test to run");

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  trieste::logging::Output() << "Loading test cases:";
  std::vector<TestCase> test_cases;
  if (TestCase::load(test_cases, cases_path))
  {
    return 1;
  }

  trieste::logging::Output() << test_cases.size() << " loaded";

  if (verbose)
  {
    trieste::logging::set_level<trieste::logging::Debug>();
    trieste::logging::Output() << "Verbose output enabled";
  }
  else
  {
    trieste::logging::set_level<trieste::logging::Output>();
  }

  int total = 0;
  int failures = 0;
  int warnings = 0;

  for (auto& testcase : test_cases)
  {
    if (
      !name_match.empty() &&
      testcase.comment.find(name_match) == std::string::npos)
    {
      continue;
    }

    total++;
    std::string name = testcase.comment;

    try
    {
      auto start = std::chrono::steady_clock::now();
      auto result = testcase.run();
      auto end = std::chrono::steady_clock::now();
      const std::chrono::duration<double> elapsed = end - start;

      switch (result.outcome)
      {
        case Outcome::Skipped:
          trieste::logging::Output()
            << Cyan << "  SKIP: " << Reset << name << std::fixed
            << std::setw(62 - name.length()) << std::internal
            << std::setprecision(3) << elapsed.count() << " sec";
          break;

        case Outcome::Success:
          trieste::logging::Output()
            << Green << "  PASS: " << Reset << name << std::fixed
            << std::setw(62 - name.length()) << std::internal
            << std::setprecision(3) << elapsed.count() << " sec";
          break;

        case Outcome::IncorrectSuccess:
          trieste::logging::Error()
            << Red << "  FAIL: " << Reset << name << std::fixed
            << std::setw(62 - name.length()) << std::internal
            << std::setprecision(3) << elapsed.count() << " sec" << std::endl
            << "  Expected error: " << testcase.expected_error << std::endl;
          failures++;
          break;

        case Outcome::ErrorMismatch:
          if (strict_messages)
          {
            warnings++;
            trieste::logging::Error()
              << Yellow << "  WARN: " << Reset << name << std::fixed
              << std::setw(62 - name.length()) << std::internal
              << std::setprecision(3) << elapsed.count() << " sec" << std::endl
              << result.error << std::endl;
          }
          else
          {
            trieste::logging::Output()
              << Green << "  PASS: " << Reset << name << std::fixed
              << std::setw(62 - name.length()) << std::internal
              << std::setprecision(3) << elapsed.count() << " sec";
          }
          break;

        case Outcome::Error:
        case Outcome::IncorrectResult:
          failures++;
          trieste::logging::Error()
            << Red << "  FAIL: " << Reset << name << std::fixed
            << std::setw(62 - name.length()) << std::internal
            << std::setprecision(3) << elapsed.count() << " sec" << std::endl
            << result.error << std::endl;

          if (verbose)
          {
            trieste::logging::Error() << json::to_string(testcase.node);
          }
          break;

        default:
          throw std::runtime_error("Unexpected outcome");
      }
    }
    catch (const std::exception& e)
    {
      failures++;
      trieste::logging::Error()
        << Red << "  EXCEPTION: " << Reset << name << std::endl
        << "  " << e.what() << std::endl;
    }

    if (fail_first && failures > 0)
    {
      break;
    }
  }

  if (failures != 0)
  {
    trieste::logging::Error()
      << std::endl
      << (total - failures) << " / " << total << " passed" << std::endl;
  }
  else
  {
    trieste::logging::Output()
      << std::endl
      << total << " / " << total << " passed" << std::endl;
  }

  if (warnings > 0)
  {
    trieste::logging::Output() << warnings << " warnings" << std::endl;
  }

  return failures;
}

#include "trieste/json.h"
#include "trieste/logging.h"
#include "trieste/trieste.h"
#include "trieste/utf8.h"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <fstream>
#include <type_traits>

const std::string Green = "\x1b[32m";
const std::string Yellow = "\x1b[33m";
const std::string Reset = "\x1b[0m";
const std::string Red = "\x1b[31m";
// const std::string White = "\x1b[37m";

enum class Outcome
{
  Accept,
  Reject,
  Maybe
};

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

struct TestResult
{
  bool accepted;
  std::string error;
  bool diff;
};

struct TestCase
{
  std::string name;
  std::string json;
  std::filesystem::path filename;
  Outcome outcome;

  TestResult run(const std::filesystem::path& debug_path, bool wf_checks)
  {
    auto dest = DestinationDef::synthetic();
    auto result = json::reader()
                    .synthetic(json)
                    .debug_enabled(!debug_path.empty())
                    .debug_path(debug_path)
                    .wf_check_enabled(wf_checks) >>
      json::writer("actual.json")
        .destination(dest)
        .debug_enabled(!debug_path.empty())
        .debug_path(debug_path)
        .wf_check_enabled(wf_checks);

    if (!result.ok)
    {
      logging::String err;
      result.print_errors(err);
      return {false, err.str(), false};
    }

    auto actual_json = dest->file(std::filesystem::path(".") / "actual.json");

    trieste::logging::Debug() << actual_json;

    if (diff_json(actual_json, json))
    {
      std::ostringstream os;
      diff(actual_json, json, "JSON", os);
      return {true, os.str(), true};
    }

    return {true, "", false};
  }

  static void load(
    std::vector<TestCase>& test_cases, const std::filesystem::path& file_or_dir)
  {
    if (std::filesystem::is_directory(file_or_dir))
    {
      const std::filesystem::path dir_path = file_or_dir;
      auto dir_iter = std::filesystem::directory_iterator{dir_path};
      for (auto it = std::filesystem::begin(dir_iter);
           it != std::filesystem::end(dir_iter);
           ++it)
      {
        load(test_cases, it->path());
      }
    }
    else
    {
      const std::filesystem::path file = file_or_dir;
      if (file.extension() == ".json")
      {
        std::string json = utf8::read_to_end(file, true);
        std::string name = file.stem().string();
        Outcome outcome;
        switch (name[0])
        {
          case 'y':
            outcome = Outcome::Accept;
            break;
          case 'n':
            outcome = Outcome::Reject;
            break;
          case 'i':
            outcome = Outcome::Maybe;
            break;
          default:
            throw std::runtime_error("Invalid test case name: " + name);
        }
        test_cases.push_back({name, json, file, outcome});
      }
    }
  }
};

int manual_construction_test()
{
  using namespace trieste;
  std::string name = "manual construction";
  auto start = std::chrono::steady_clock::now();
  Node object = json::object(
    {json::member("key_a_str", "value"),
     json::member("key_b_number", 42),
     json::member("key_c_bool", json::boolean(true)),
     json::member("key_d_null", json::null()),
     json::member("key_e_array", json::array({json::value(1), json::value(2)})),
     json::member(
       "key_f_object",
       json::object({json::member("key", json::value("value"))}))});
  Nodes elements;
  elements.push_back(json::value(1));
  elements.push_back(json::value("two"));
  elements.push_back(json::boolean(false));
  elements.push_back(json::null());
  Node array = json::array(elements.begin(), elements.end());
  auto end = std::chrono::steady_clock::now();
  const std::chrono::duration<double> elapsed = end - start;
  std::string expected =
    R"({"key_a_str":"value","key_b_number":42,"key_c_bool":true,"key_d_null":null,"key_e_array":[1,2],"key_f_object":{"key":"value"}})";
  std::string actual = json::to_string(object);

  logging::Debug() << "to_string: " << actual;

  if (expected != actual)
  {
    logging::Error() << Red << "  FAIL: " << Reset << name << std::fixed
                     << std::setw(62 - name.length()) << std::internal
                     << std::setprecision(3) << elapsed.count() << " sec"
                     << std::endl
                     << "  Expected: " << expected << std::endl
                     << "  Actual:   " << actual;
    return 1;
  }

  auto actual_c = json::select(object, {"/key_c_bool"});
  if (actual_c != json::True)
  {
    logging::Error() << Red << "  FAIL: " << Reset << name << std::fixed
                     << std::setw(62 - name.length()) << std::internal
                     << std::setprecision(3) << elapsed.count() << " sec"
                     << std::endl
                     << "  Expected: " << (json::True ^ "true") << std::endl
                     << "  Actual:   " << actual_c;
    return 1;
  }

  logging::Debug() << "c: " << actual_c;

  auto actual_a = json::select_string(object, {"/key_a_str"});
  if (!actual_a.has_value() || actual_a->view() != "value")
  {
    logging::Error() << Red << "  FAIL: " << Reset << name << std::fixed
                     << std::setw(62 - name.length()) << std::internal
                     << std::setprecision(3) << elapsed.count() << " sec"
                     << std::endl
                     << "  Expected: " << "value" << std::endl
                     << "  Actual:   "
                     << actual_a.value_or(Location{"<missing>"}).view();
    return 1;
  }

  logging::Debug() << "a: " << actual_a.value().view();

  auto actual_e1 = json::select_number(object, {"/key_e_array/1"});
  if (!actual_e1.has_value() || actual_e1.value() != 2)
  {
    logging::Error() << Red << "  FAIL: " << Reset << name << std::fixed
                     << std::setw(62 - name.length()) << std::internal
                     << std::setprecision(3) << elapsed.count() << " sec"
                     << std::endl
                     << "  Expected: " << 2;
    if (actual_e1.has_value())
    {
      logging::Error() << "  Actual:   " << actual_e1.value();
    }

    return 1;
  }

  logging::Debug() << "e[1]: " << actual_e1.value();

  auto actual_missing = json::select(object, {"/missingkey"});
  if (actual_missing != Error)
  {
    logging::Error() << Red << "  FAIL: " << Reset << name << std::fixed
                     << std::setw(62 - name.length()) << std::internal
                     << std::setprecision(3) << elapsed.count() << " sec"
                     << std::endl
                     << "Returned value for missing key";
  }

  logging::Debug() << "missing key: " << actual_missing;

  // JSON Patch test

  Node patched;
  {
    logging::LocalLogLevel level = logging::local_log_level<logging::Output>();
    auto reader = json::reader();
    auto doc =
      reader.synthetic(R"json({"foo": {"bar": {"baz": [{"boo": "net"}]}}})json")
        .read()
        .ast->front();
    auto patch = reader
                   .synthetic(R"json([
        {"op": "copy", "from": "/foo", "path": "/bak"},
        {"op": "replace", "path": "/foo/bar/baz/0/boo", "value": "qux"}
      ])json")
                   .read()
                   .ast->front();
    patched = json::patch(doc, patch);
  }

  if (patched == Error)
  {
    logging::Error() << Red << "  FAIL: " << Reset << name << std::fixed
                     << std::setw(62 - name.length()) << std::internal
                     << std::setprecision(3) << elapsed.count() << " sec"
                     << std::endl
                     << patched;
  }

  std::string actual_patched = json::to_string(patched);
  std::string expected_patched =
    R"json({"foo":{"bar":{"baz":[{"boo":"qux"}]}},"bak":{"bar":{"baz":[{"boo":"net"}]}}})json";
  if (actual_patched != expected_patched)
  {
    std::ostringstream error;
    diff(actual_patched, expected_patched, "JSON", error);
    logging::Error() << Red << "  FAIL: " << Reset << name << std::fixed
                     << std::setw(62 - name.length()) << std::internal
                     << std::setprecision(3) << elapsed.count() << " sec"
                     << std::endl
                     << error.str();
    return 1;
  }

  logging::Debug() << "patched: " << json::to_string(patched);

  logging::Output() << Green << "  PASS: " << Reset << name << std::fixed
                    << std::setw(62 - name.length()) << std::internal
                    << std::setprecision(3) << elapsed.count() << " sec";
  return 0;
}

int main(int argc, char* argv[])
{
  CLI::App app;

  app.set_help_all_flag("--help-all", "Expand all help");

  std::vector<std::filesystem::path> case_paths;
  app.add_option("case,-c,--case", case_paths, "Test case JSON directory");

  std::filesystem::path debug_path;
  app.add_option(
    "-a,--ast", debug_path, "Output the AST (debugging for test case parser)");

  bool wf_checks{false};
  app.add_flag("-w,--wf", wf_checks, "Enable well-formedness checks (slow)");

  bool verbose{false};
  app.add_flag("-v,--verbose", verbose, "Verbose output (for debugging)");

  bool strict{false};
  app.add_flag("-s,--strict", strict, "Strict mode (must pass all tests)");

  bool fail_first{false};
  app.add_flag(
    "-f,--fail-first", fail_first, "Stop after first test case failure");

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
  for (auto path : case_paths)
  {
    TestCase::load(test_cases, path);
  }

  std::sort(
    test_cases.begin(),
    test_cases.end(),
    [](const TestCase& lhs, const TestCase& rhs) {
      return lhs.name < rhs.name;
    });

  trieste::logging::Output() << test_cases.size() << " loaded";

  trieste::logging::LocalLogLevel loglevel =
    trieste::logging::local_log_level_from_string(verbose ? "debug" : "output");

  if (verbose)
  {
    trieste::logging::Output() << "Verbose output enabled";
  }

  int total = 0;
  int failures = 0;
  int warnings = 0;

  total++;
  if (manual_construction_test() != 0)
  {
    failures++;
  }

  for (auto& testcase : test_cases)
  {
    if (
      !name_match.empty() &&
      testcase.name.find(name_match) == std::string::npos)
    {
      continue;
    }

    total++;
    std::string name = testcase.name;

    try
    {
      auto start = std::chrono::steady_clock::now();
      auto result = testcase.run(debug_path, wf_checks);
      auto end = std::chrono::steady_clock::now();
      const std::chrono::duration<double> elapsed = end - start;

      if (result.accepted)
      {
        if (testcase.outcome == Outcome::Reject || result.diff)
        {
          trieste::logging::Error()
            << Red << "  FAIL: " << Reset << name << std::fixed
            << std::setw(62 - name.length()) << std::internal
            << std::setprecision(3) << elapsed.count() << " sec" << std::endl
            << "  Expected rejection" << std::endl
            << "(from " << testcase.filename << ")";
          failures++;
        }
        else
        {
          trieste::logging::Output()
            << Green << "  PASS: " << Reset << name << std::fixed
            << std::setw(62 - name.length()) << std::internal
            << std::setprecision(3) << elapsed.count() << " sec";
        }
      }
      else
      {
        if (
          testcase.outcome == Outcome::Accept ||
          (strict && testcase.outcome == Outcome::Maybe))
        {
          failures++;
          trieste::logging::Error()
            << Red << "  FAIL: " << Reset << name << std::fixed
            << std::setw(62 - name.length()) << std::internal
            << std::setprecision(3) << elapsed.count() << " sec" << std::endl
            << result.error << std::endl
            << "(from " << testcase.filename << ")";
        }
        else
        {
          auto color = Green;
          if (testcase.outcome == Outcome::Maybe)
          {
            color = Yellow;
            if (strict)
            {
              failures++;
            }
            else
            {
              warnings++;
            }
          }
          trieste::logging::Output()
            << color << "  PASS: " << Reset << name << std::fixed
            << std::setw(62 - name.length()) << std::internal
            << std::setprecision(3) << elapsed.count() << " sec";
        }
      }
    }
    catch (const std::exception& e)
    {
      failures++;
      trieste::logging::Error()
        << Red << "  EXCEPTION: " << Reset << name << std::endl
        << "  " << e.what() << std::endl
        << "(from " << testcase.filename << ")" << std::endl;
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

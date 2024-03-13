#include "trieste/json.h"
#include "trieste/logging.h"
#include "trieste/utf8.h"
#include "trieste/yaml.h"

#include <CLI/CLI.hpp>
#include <fstream>
#include <type_traits>

using namespace trieste::utf8;
using namespace trieste::yaml;

const std::string Green = "\x1b[32m";
const std::string Reset = "\x1b[0m";
const std::string Red = "\x1b[31m";
// const std::string White = "\x1b[37m";

std::string replace_whitespace(const std::string& str)
{
  #if defined(_WIN32)
  return str;
  #else
  std::ostringstream os;
  for (std::size_t i = 0; i < str.size(); ++i)
  {
    switch (str[i])
    {
      case ' ':
        os << rune(183);
        break;

      case '\t':
        os << rune(2192);
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
    std::size_t a_end = actual.find("\n", a);
    std::size_t w_end = wanted.find("\n", w);
    std::string a_line = actual.substr(a, a_end - a);
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
      std::size_t a_end = actual.find("\n", a);
      std::string a_line = actual.substr(a, a_end - a);
      os << "+ " << a_line << std::endl;
      a = a_end + 1;
    }

    while (w < wanted.size())
    {
      std::size_t w_end = wanted.find("\n", w);
      std::string w_line = wanted.substr(w, w_end - w);
      os << "- " << w_line << std::endl;
      w = w_end + 1;
    }
  }

  os << "--- " << label << " ---" << std::endl;
}

struct Result
{
  bool passed;
  std::string error;
};

struct TestCase
{
  std::string id;
  std::size_t index;
  std::string name;
  std::string in_yaml;
  std::string in_json;
  std::string out_yaml;
  std::string emit_yaml;
  std::string event;
  std::filesystem::path filename;
  bool error;

  Result run(const std::filesystem::path& debug_path, bool wf_checks)
  {
    YAMLEmitter emitter;
    YAMLReader reader(in_yaml);
    reader.debug_enabled(!debug_path.empty())
      .debug_path(debug_path)
      .well_formed_checks_enabled(wf_checks);
    reader.read();
    if (reader.has_errors())
    {
      return {error, reader.error_message()};
    }

    // YAML event streams are unambiguous, unique representations of
    // the YAML AST. As such, a correct event stream means the parser
    // is working.

    std::string actual_event;
    try
    {
      std::ostringstream os;
      emitter.emit_events(os, reader.stream());
      actual_event = os.str();
    }
    catch (const std::exception& ex)
    {
      return {false, ex.what()};
    }

    trieste::logging::Debug() << actual_event;

    if (event.size() > 0 && actual_event != event)
    {
      if(id == "Y79Y" && index >= 4 && index <= 9){
        // TODO
        // these tests currently have incorrect event files
        // https://github.com/yaml/yaml-test-suite/issues/126
        // remove once this issue has been resolved
        return {true, ""};
      }

      std::ostringstream os;
      diff(actual_event, event, "EVENT", os);
      return {false, os.str()};
    }

    // TODO test YAML emitter
    // TODO test YAML to JSON emitter

    return {true, ""};
  }

  static void
  load(std::vector<TestCase>& cases, const std::filesystem::path& test_dir)
  {
    std::size_t index = 0;
    std::string id = test_dir.filename().string();
    if (id == "name" || id == "tags" || id.front() == '.')
    {
      return;
    }

    std::string subpath = "00";
    auto subtest = test_dir / subpath;
    std::string subpath_long = "000";
    auto subtest_long = test_dir / subpath_long;
    if (
      std::filesystem::exists(subtest) || std::filesystem::exists(subtest_long))
    {
      bool is_long = std::filesystem::exists(subtest_long);
      if(is_long){
        subtest = subtest_long;
      }

      while (std::filesystem::exists(subtest))
      {
        load(cases, subtest);
        cases.back().index = index;
        cases.back().id = id;
        index += 1;
        subpath = std::to_string(index);
        if (is_long)
        {
          if (index < 10)
          {
            subpath = "00" + subpath;
          }
          else
          {
            subpath = "0" + subpath;
          }
        }
        else if (index < 10)
        {
          subpath = "0" + subpath;
        }

        subtest = test_dir / subpath;
      }
    }
    else
    {
      TestCase testcase;
      testcase.id = id;
      testcase.index = index;
      testcase.filename = test_dir / "in.yaml";
      testcase.name = read_to_end(test_dir / "===");
      if (testcase.name.back() == '\n')
      {
        testcase.name.pop_back();
      }
      testcase.in_yaml = read_to_end(test_dir / "in.yaml");
      testcase.in_json = read_to_end(test_dir / "in.json");
      testcase.out_yaml = read_to_end(test_dir / "out.yaml");
      testcase.emit_yaml = read_to_end(test_dir / "emit.yaml");
      testcase.event = read_to_end(test_dir / "test.event");
      testcase.error = std::filesystem::exists(test_dir / "error");
      if (!testcase.in_yaml.empty())
      {
        cases.push_back(testcase);
      }
    }
  }
};

int main(int argc, char* argv[])
{
  CLI::App app;

  app.set_help_all_flag("--help-all", "Expand all help");

  std::vector<std::filesystem::path> case_paths;
  app.add_option(
    "case,-c,--case", case_paths, "Test case YAML files or directories");

  std::filesystem::path debug_path;
  app.add_option(
    "-a,--ast", debug_path, "Output the AST (debugging for test case parser)");

  bool wf_checks{false};
  app.add_flag("-w,--wf", wf_checks, "Enable well-formedness checks (slow)");

  bool verbose{false};
  app.add_flag("-v,--verbose", verbose, "Verbose output (for debugging)");

  bool fail_first{false};
  app.add_flag(
    "-f,--fail-first", fail_first, "Stop after first test case failure");

  std::string name_match;
  app.add_option(
    "-n,--name",
    name_match,
    "Note (or note substring) of specific test to run");

  std::string id_match;
  app.add_option("-i,--id", id_match, "ID of the test or test group to run");

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  if (verbose)
  {
    trieste::logging::set_level<trieste::logging::Debug>();
    trieste::logging::Output() << "Verbose output enabled";
  }
  else
  {
    trieste::logging::set_level<trieste::logging::Output>();
  }

  trieste::logging::Output() << "Loading test cases:";
  std::vector<TestCase> test_cases;
  for (auto path : case_paths)
  {
    if (std::filesystem::is_directory(path))
    {
      for (auto& file_or_dir : std::filesystem::directory_iterator(path))
      {
        if (std::filesystem::is_directory(file_or_dir))
        {
          TestCase::load(test_cases, file_or_dir);
        }
        else
        {
          trieste::logging::Error()
            << "Not a directory: " << file_or_dir.path();
          return 1;
        }
      }
    }
    else
    {
      trieste::logging::Error() << "Not a directory: " << path;
      return 1;
    }
  }
  std::sort(
    test_cases.begin(),
    test_cases.end(),
    [](const TestCase& lhs, const TestCase& rhs) {
      if (lhs.id == rhs.id)
      {
        return lhs.index < rhs.index;
      }
      return lhs.id < rhs.id;
    });
  trieste::logging::Output() << test_cases.size() << " loaded";

  int total = 0;
  int failures = 0;
  for (auto& testcase : test_cases)
  {
    if (!id_match.empty() && testcase.id != id_match)
    {
      continue;
    }

    if (
      !name_match.empty() &&
      testcase.name.find(name_match) == std::string::npos)
    {
      continue;
    }

    total++;
    std::string id = testcase.id;
    std::string name = testcase.name;

    try
    {
      auto start = std::chrono::steady_clock::now();
      auto result = testcase.run(debug_path, wf_checks);
      auto end = std::chrono::steady_clock::now();
      const std::chrono::duration<double> elapsed = end - start;

      if (result.passed)
      {
        trieste::logging::Output()
          << Green << "  PASS: " << Reset << id << ": " << name << std::fixed
          << std::setw(62 - name.length()) << std::internal
          << std::setprecision(3) << elapsed.count() << " sec";
      }
      else
      {
        failures++;
        trieste::logging::Error()
          << Red << "  FAIL: " << Reset << id << ": " << name << std::fixed
          << std::setw(62 - name.length()) << std::internal
          << std::setprecision(3) << elapsed.count() << " sec" << std::endl
          << result.error << std::endl
          << "(from " << testcase.filename << ")";
        if (fail_first)
        {
          break;
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
      if (fail_first)
      {
        break;
      }
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

  return failures;
}

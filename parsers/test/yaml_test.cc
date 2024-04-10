#include "trieste/json.h"
#include "trieste/logging.h"
#include "trieste/utf8.h"
#include "trieste/yaml.h"

#include <CLI/CLI.hpp>
#include <fstream>
#include <type_traits>

using namespace trieste;

const std::string Green = "\x1b[32m";
const std::string Reset = "\x1b[0m";
const std::string Red = "\x1b[31m";
// const std::string White = "\x1b[37m";

std::string normalize_crlf(const std::string_view& str, bool crlf)
{
  std::ostringstream os;
  std::size_t start = 0;
  std::size_t newline = str.find('\n');
  if (newline == 0)
  {
    if (crlf)
    {
      os << '\r';
    }
    os << '\n';
    start = newline + 1;
    newline = str.find('\n', start);
  }

  while (newline != std::string::npos)
  {
    std::size_t length = newline - start;
    if (crlf)
    {
      os << str.substr(start, length);
      if (str[newline - 1] != '\r')
      {
        os << '\r';
      }
      os << '\n';
    }
    else
    {
      if (str[newline - 1] == '\r')
      {
        length -= 1;
      }

      os << str.substr(start, length);
      os << '\n';
    }
    start = newline + 1;
    newline = str.find('\n', start);
  }

  if (start < str.size())
  {
    os << str.substr(start);
  }

  return os.str();
}

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
    std::size_t w_end = newline_or_end(wanted, w);
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
      std::size_t a_end = newline_or_end(actual, a);
      std::string a_line = actual.substr(a, a_end - a);
      os << "+ " << a_line << std::endl;
      a = a_end + 1;
    }

    while (w < wanted.size())
    {
      std::size_t w_end = newline_or_end(wanted, w);
      std::string w_line = wanted.substr(w, w_end - w);
      os << "- " << w_line << std::endl;
      w = w_end + 1;
    }
  }

  os << "--- " << label << " ---" << std::endl;
}

struct TestCase
{
  struct Result
  {
    bool passed;
    std::string error;
  };

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

  // YAML event streams are unambiguous, unique representations of
  // the YAML AST. As such, a correct event stream means the parser
  // is working.
  Result compare_events(
    Node actual_yaml,
    const std::filesystem::path& debug_path,
    bool wf_checks,
    bool crlf)
  {
    std::string event_name = "actual.event";
    Destination dest = DestinationDef::synthetic();
    auto result =
      actual_yaml >> yaml::event_writer("actual.event", crlf ? "\r\n" : "\n")
                       .wf_check_enabled(wf_checks)
                       .debug_enabled(!debug_path.empty())
                       .debug_path(debug_path / "event_writer")
                       .destination(dest);

    if (!result.ok)
    {
      logging::String err;
      result.print_errors(err);
      return {false, err.str()};
    }

    auto actual_event = dest->file(std::filesystem::path(".") / "actual.event");

    trieste::logging::Debug() << actual_event;

    if (event.size() > 0 && actual_event != event)
    {
      if (id == "Y79Y" && index >= 4 && index <= 9)
      {
        // these tests currently have incorrect event files
        // open issue: https://github.com/yaml/yaml-test-suite/issues/126
        return {true, ""};
      }

      std::ostringstream os;
      diff(actual_event, event, "EVENT", os);
      return {false, os.str()};
    }

    return {true, ""};
  }

  Result compare_json(
    Node actual_yaml, const std::filesystem::path& debug_path, bool wf_checks)
  {
    if (!in_json.empty())
    {
      auto result = actual_yaml >> yaml::to_json()
                                     .wf_check_enabled(wf_checks)
                                     .debug_enabled(!debug_path.empty())
                                     .debug_path(debug_path / "yaml_to_json");

      if (!result.ok)
      {
        logging::String err;
        result.print_errors(err);
        return {false, err.str()};
      }

      Node actual_json = result.ast;

      result = json::reader(true)
                 .wf_check_enabled(wf_checks)
                 .debug_enabled(!debug_path.empty())
                 .debug_path(debug_path / "json")
                 .synthetic(in_json)
                 .read();
      if (!result.ok)
      {
        logging::String err;
        result.print_errors(err);
        return {false, err.str()};
      }

      Node wanted_json = result.ast;

      if (!json::equal(actual_json, wanted_json))
      {
        std::ostringstream os;
        diff(
          json::to_string(actual_json, true),
          json::to_string(wanted_json, true),
          "JSON",
          os);
        return {false, os.str()};
      }
    }

    return {true, ""};
  }

  Result compare_yaml(Node actual_yaml, bool crlf)
  {
    if (!out_yaml.empty() || !emit_yaml.empty())
    {
      bool emit_mode = !emit_yaml.empty();
      std::string wanted_yaml = emit_mode ? emit_yaml : out_yaml;
      std::string actual_out_yaml =
        yaml::to_string(actual_yaml, crlf ? "\r\n" : "\n", 2, emit_mode);

      if (id == "4ABK" || id == "L24T" || id == "MJS9" || id == "R4YG")
      {
        // these tests have non-standard YAML output
        // 4ABK open issue: https://github.com/yaml/yaml-test-suite/issues/133
        // L24T open issue: https://github.com/yaml/yaml-test-suite/issues/134
        // MJS9/R4YG do the same thing as L24T but is in the out.yaml so less
        // can be ignored as an alternate production.
        return {true, ""};
      }

      std::string docend = crlf ? "...\r\n" : "...\n";

      if (id == "K54U" || id == "PUW8" || id == "XLQ9")
      {
        // this test adds an otherwise unwarranted document end marker
        // open issue: https://github.com/yaml/yaml-test-suite/issues/131
        actual_out_yaml += docend;
      }

      if (id == "M7A3")
      {
        // this example has an odd emitter output which omits the document start
        // in a way the code below will not catch, so we fix it here
        auto pos = wanted_yaml.find(docend);
        wanted_yaml.replace(pos, docend.size(), docend + "--- ");
      }

      trieste::logging::Debug() << actual_out_yaml;

      if (emit_mode)
      {
        auto docstart = actual_out_yaml.substr(0, 4);
        if (docstart.back() == '\r')
        {
          docstart = actual_out_yaml.substr(0, 5);
        }

        if (wanted_yaml.find(docstart) != 0)
        {
          wanted_yaml = docstart + wanted_yaml;
        }
      }

      if (actual_out_yaml != wanted_yaml)
      {
        std::ostringstream os;
        diff(actual_out_yaml, wanted_yaml, "YAML", os);
        return {false, os.str()};
      }
    }

    return {true, ""};
  }

  Result run(const std::filesystem::path& debug_path, bool wf_checks, bool crlf)
  {
    if (id == "7T8X" || id == "JEF9" || id == "K858")
    {
      // These tests reproduce whitespace from the input and
      // check it against explicit output strings which encode
      // just the linefeed.
      crlf = false;
    }

    auto result = yaml::reader()
                    .synthetic(in_yaml)
                    .wf_check_enabled(wf_checks)
                    .debug_enabled(!debug_path.empty())
                    .debug_path(debug_path / "yaml")
                    .read();

    if (!result.ok)
    {
      logging::String err;
      result.print_errors(err);
      return {error, err.str()};
    }

    Node actual_yaml = result.ast;

    Result test_result =
      compare_events(actual_yaml, debug_path, wf_checks, crlf);
    if (!test_result.passed)
    {
      return test_result;
    }

    if (error)
    {
      return {true, ""};
    }

    test_result = compare_json(actual_yaml, debug_path, wf_checks);
    if (!test_result.passed)
    {
      return test_result;
    }

    test_result = compare_yaml(actual_yaml, crlf);
    return test_result;
  }

  static void load(
    std::vector<TestCase>& cases,
    const std::filesystem::path& test_dir,
    bool crlf)
  {
    std::size_t index = 0;
    std::string id = test_dir.filename().string();
    if (id == "name" || id == "tags" || id.front() == '.')
    {
      return;
    }

    if (id == "7T8X" || id == "JEF9" || id == "K858")
    {
      // These tests reproduce whitespace from the input and
      // check it against explicit output strings which encode
      // just the linefeed.
      crlf = false;
    }

    std::string subpath = "00";
    auto subtest = test_dir / subpath;
    std::string subpath_long = "000";
    auto subtest_long = test_dir / subpath_long;
    if (
      std::filesystem::exists(subtest) || std::filesystem::exists(subtest_long))
    {
      bool is_long = std::filesystem::exists(subtest_long);
      if (is_long)
      {
        subtest = subtest_long;
      }

      while (std::filesystem::exists(subtest))
      {
        load(cases, subtest, crlf);
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
      testcase.name = utf8::read_to_end(test_dir / "===");
      if (testcase.name.back() == '\n')
      {
        testcase.name.pop_back();
      }

      testcase.in_yaml =
        normalize_crlf(utf8::read_to_end(test_dir / "in.yaml"), crlf);
      testcase.in_json =
        normalize_crlf(utf8::read_to_end(test_dir / "in.json"), crlf);
      testcase.out_yaml =
        normalize_crlf(utf8::read_to_end(test_dir / "out.yaml"), crlf);
      testcase.emit_yaml =
        normalize_crlf(utf8::read_to_end(test_dir / "emit.yaml"), crlf);
      testcase.event =
        normalize_crlf(utf8::read_to_end(test_dir / "test.event"), crlf);
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
    "case,--case", case_paths, "Test case YAML files or directories");

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

  bool crlf{false};
  app.add_flag("--crlf", crlf, "Whether to test files in CRLF mode");

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
          TestCase::load(test_cases, file_or_dir, crlf);
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
      auto result = testcase.run(debug_path, wf_checks, crlf);
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

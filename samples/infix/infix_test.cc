#include "infix.h"
#include "test_util.h"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <trieste/fuzzer.h>
#include <trieste/trieste.h>

using namespace std::string_view_literals;

// Small test interface
struct tester
{
  // The CLI11 subcommand associated with this test.
  // Set this up in the constructor.
  // After parsing argv, bool(*subcommand) == true if this tester was selected.
  CLI::App* subcommand = nullptr;

  // Run the test. Returns 0 or non-0 like main()
  virtual int run() const = 0;
  virtual ~tester() = default;
};

// dir mode, scan a directory and check all examples
struct dir_tester : public tester
{
  std::filesystem::path test_dir;
  std::optional<std::filesystem::path> debug_path;

  dir_tester(CLI::App* app)
  {
    subcommand = app->add_subcommand("dir");
    subcommand->add_option(
      "test-dir", test_dir, "The directory containing tests.");
    subcommand->add_option(
      "--dump-passes", debug_path, "Directory to store debug ASTs.");
  }

  int run() const override
  {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(test_dir))
    {
      if (entry.is_regular_file() && entry.path().extension() == ".infix")
      {
        int idx = -1;
        auto expected_file = entry.path();
        auto update_ext = [&]() {
          ++idx;

          expected_file = entry.path();
          auto stem = expected_file.stem();

          if (idx == 0)
          {
            stem += ".expected";
          }
          else
          {
            stem += "." + std::to_string(idx) + ".expected";
          }
          expected_file.replace_filename(stem);
        };

        while (update_ext(), std::filesystem::exists(expected_file))
        {
          std::ifstream expected_reader(expected_file);
          std::vector<std::string> first_lines;
          {
            std::string first_line;
            assert(expected_reader);
            auto last_posn = expected_reader.tellg();
            while (std::getline(expected_reader, first_line) &&
                   // note: behaves like starts_with, but works in C++17
                   std::string_view(first_line).substr(0, 3) == "//!")
            {
              first_lines.push_back(first_line);
              last_posn =
                expected_reader
                  .tellg(); // un-read to here if the next line isn't args
            }
            expected_reader.seekg(last_posn);
          }

          if (first_lines.empty())
          {
            std::cout << "Test file " << expected_file
                      << "has no test arguments in it. Aborting." << std::endl;
            return 1;
          }

          std::string expected_output;
          {
            std::stringstream buffer;
            buffer << expected_reader.rdbuf();
            expected_output = buffer.str();
          }

          // If a line ends with helper, then replace that with substitution in
          // substitutions. Some patterns have all the same options and then
          // many different run modes, so this helps keep test configs short.
          auto substitute_helper =
            [&](
              std::string_view helper,
              std::initializer_list<std::string_view> substitutions) -> void {
            std::vector<std::string> first_lines_remade;

            for (const auto& first_line : first_lines)
            {
              std::size_t start_pos = first_line.size() - helper.size();
              if (
                first_line.size() >= helper.size() &&
                std::string_view(first_line).substr(start_pos, helper.size()) ==
                  helper)
              {
                for (auto substitution : substitutions)
                {
                  auto prefix = first_line.substr(0, start_pos);
                  prefix += substitution;
                  first_lines_remade.push_back(std::move(prefix));
                }
              }
              else
              {
                first_lines_remade.push_back(first_line);
              }
            }

            first_lines = std::move(first_lines_remade);
          };

          // So far the only helper is parse failures, which should affect
          // everything.
          substitute_helper(
            "expect_parse_fail"sv,
            {
              "--expect-fail parse_only",
              "--expect-fail calculate",
              "--expect-fail infix",
              "--expect-fail postfix",
            });

          for (auto first_line : first_lines)
          {
            std::cout << "Testing file " << entry.path() << ", expected "
                      << expected_file.filename() << ", "
                      << first_line.substr(4) << " ... " << std::flush;

            // config for current run
            infix::Config config;
            bool expect_fail = false;
            auto proc_options = {
              "parse_only"sv, "calculate"sv, "infix"sv, "postfix"sv};
            std::string selected_proc;

            {
              std::vector<std::string> fake_argv;
              {
                // This hack forces CLI11 to parse an initial line of the
                // file by tokenizing it as a traditional argv.
                // Fortunately we can use the vector-of-strings overload rather
                // than accurately recreate a true argc and argv.
                std::istringstream in(first_line.substr(4));
                std::string tok;
                while (in >> tok)
                {
                  fake_argv.push_back(tok);
                }
                std::reverse(fake_argv.begin(), fake_argv.end());
              }

              CLI::App config_app;
              config_app.name("//!");
              config.install_cli(&config_app);
              config_app
                .add_option(
                  "proc",
                  selected_proc,
                  "Which operation(s) to run on the code")
                ->required()
                ->transform(CLI::IsMember(proc_options));
              config_app.add_flag(
                "--expect-fail", expect_fail, "This run is supposed to fail");

              try
              {
                config_app.parse(fake_argv);
              }
              catch (const CLI::ParseError& e)
              {
                return config_app.exit(e);
              }
            }

            if (debug_path)
            {
              std::filesystem::remove_all(*debug_path);
            }

            auto reader = infix::reader(config)
                            .file(entry.path())
                            .wf_check_enabled(true)
                            .debug_enabled(bool(debug_path))
                            .debug_path(debug_path ? *debug_path / "read" : "");

            // Tricky point: std::filesystem::path uses platform-specific
            // separators, even in this context where we only care about
            // Trieste's synthetic in-memory output. So, we need to build our
            // paths relative to "." using operator/ specifically. If we just
            // write "./calculate_output", this code will fail on Windows
            // because the key we're looking up becomes ".\calculate_output"
            // instead. For convenience, we use this synth_root as a base.
            std::filesystem::path synth_root = ".";
            trieste::ProcessResult result;
            std::string actual_str;
            if (selected_proc == "parse_only")
            {
              result = reader.read();

              std::ostringstream result_to_str;
              result_to_str << result.ast;
              actual_str = result_to_str.str();
            }
            else if (selected_proc == "calculate")
            {
              auto dest = trieste::DestinationDef::synthetic();
              result = reader >>
                infix::calculate()
                  .wf_check_enabled(true)
                  .debug_enabled(bool(debug_path))
                  .debug_path(debug_path ? *debug_path / "calculate" : "") >>
                infix::calculate_output_writer().destination(dest);

              if (result.ok)
              {
                actual_str = dest->file(synth_root / "calculate_output");
              }
            }
            else if (selected_proc == "infix")
            {
              auto dest = trieste::DestinationDef::synthetic();
              result = reader >> infix::writer().destination(dest);

              if (result.ok)
              {
                actual_str = dest->file(synth_root / "infix");
              }
            }
            else if (selected_proc == "postfix")
            {
              auto dest = trieste::DestinationDef::synthetic();
              result = reader >> infix::postfix_writer().destination(dest);

              if (result.ok)
              {
                actual_str = dest->file(synth_root / "postfix");
              }
            }
            else
            {
              assert(false);
            }

            // If we failed, we actually care about the error list, not the
            // exact AST we got stuck in. This is also more stable across tuple
            // implementations and saves us from making more .expect files.
            if (!result.ok)
            {
              std::ostringstream out;

              for (const auto& err : result.errors)
              {
                // Taken from result.print_errors, but adapted for testing as
                // opposed to end-used interaction. The max report cap is gone,
                // and we insert a filename that is not dependent on where the
                // Trieste source tree is located.
                for (const auto& child : *err)
                {
                  if (child == trieste::ErrorMsg)
                    out << child->location().view() << std::endl;
                  else
                  {
                    auto [line, col] = child->location().linecol();
                    // We check only the filenames, because that's already ok if
                    // it got the file right, and trying to be smarter breaks
                    // due to weird cross-platform behavior in CI.
                    assert(
                      entry.path().filename() ==
                      std::filesystem::path(child->location().source->origin())
                        .filename());

                    out << "-- " << entry.path().filename().string() << ":"
                        << line << ":" << col << std::endl
                        << child->location().str() << std::endl;
                  }
                }
              }

              actual_str = out.str();
            }

            // Clean up trailing whitespace, which is often the source of
            // spurious test failures.
            trim_trailing_whitespace(expected_output);
            trim_trailing_whitespace(actual_str);

            // FIXME: work around location handling error that leaks \r
            // characters on Windows platform:
            expected_output = normalize_line_endings(expected_output);
            actual_str = normalize_line_endings(actual_str);

            bool ok = true;
            if (actual_str != expected_output)
            {
              std::cout << "unexpected output:" << std::endl;
              diffy_print(expected_output, actual_str, std::cout);
              ok = false;
            }
            if (expect_fail && result.ok)
            {
              std::cout << "unexpected success, last pass: " << result.last_pass
                        << std::endl;
              ok = false;
            }
            if (!expect_fail && !result.ok)
            {
              std::cout << "unexpected failure, last pass: " << result.last_pass
                        << std::endl;
              ok = false;
            }
            if (!result.ok && result.errors.size() == 0)
            {
              std::cout << "failed but no errors; WF violation from pass: "
                        << result.last_pass << std::endl;
              ok = false;
            }

            if (ok)
            {
              std::cout << "ok." << std::endl;
            }
            else
            {
              std::cout << "abort." << std::endl;
              return 1;
            }
          }
        }

        if (idx == 0)
        {
          std::cout << "Expected file " << expected_file
                    << " not found, skipping." << std::endl;
        }
        else
        {
          // We enumerated at least one. Don't yell about it, we probably just
          // ran out.
        }
      }
    }
    return 0;
  }
};

struct fuzz_tester : public tester
{
  infix::Config fuzz_config;
  std::optional<uint32_t> fuzzer_start_seed = std::nullopt;
  uint32_t fuzzer_seed_count = 100;
  bool fuzzer_fail_fast = false;

  CLI::App* fuzz_reader;
  CLI::App* fuzz_calculate;

  fuzz_tester(CLI::App* app)
  {
    subcommand = app->add_subcommand("fuzz");
    subcommand->require_subcommand(1);

    fuzz_config.install_cli(subcommand);
    subcommand->add_option(
      "--start-seed", fuzzer_start_seed, "Seed to start RNG");
    subcommand->add_option(
      "--seed-count", fuzzer_seed_count, "Number of fuzzing iterations");
    subcommand->add_flag(
      "--fail-fast", fuzzer_fail_fast, "Stop on first error");

    fuzz_reader = subcommand->add_subcommand("reader");
    fuzz_calculate = subcommand->add_subcommand("calculate");
  }

  int run() const override
  {
    fuzz_config.sanity();
    trieste::Fuzzer fuzzer;
    // The fuzzer holds references to these objects (depending on its
    // configuration). We should keep them alive for program duration, rather
    // than drop them once we've constructed the fuzzer (as might be natural by
    // passing them directly to the fuzzer constructor).
    // (!) Just passing them to the fuzzer constructor only works if the fuzzer
    // itself has temporary lifetime.
    std::unique_ptr<trieste::Rewriter> rewriter_keepalive;
    std::unique_ptr<trieste::Reader> reader_keepalive;
    if (*fuzz_reader)
    {
      std::cout << "Fuzzing reader..." << std::endl;
      reader_keepalive =
        std::make_unique<trieste::Reader>(infix::reader(fuzz_config));
      fuzzer = trieste::Fuzzer(*reader_keepalive);
    }
    else if (*fuzz_calculate)
    {
      std::cout << "Fuzzing calculate..." << std::endl;
      reader_keepalive =
        std::make_unique<trieste::Reader>(infix::reader(fuzz_config));
      rewriter_keepalive =
        std::make_unique<trieste::Rewriter>(infix::calculate());
      fuzzer = trieste::Fuzzer(
        *rewriter_keepalive, reader_keepalive->parser().generators());
    }
    else
    {
      assert(false);
    }

    std::cout << "Start seed: " << fuzzer.start_seed() << std::endl;

    int result =
      fuzzer
        .start_seed(
          fuzzer_start_seed ? *fuzzer_start_seed : fuzzer.start_seed())
        .seed_count(fuzzer_seed_count)
        .failfast(fuzzer_fail_fast)
        .test();

    if (result == 0)
    {
      std::cout << "ok." << std::endl;
      return 0;
    }
    else
    {
      std::cout << "failed." << std::endl;
      return result;
    }
  }
};

// Select one test to run out of those provided.
// Assumes test subcommands are mutually exclusive.
int run_selected_tester(std::initializer_list<const tester*> testers)
{
  for (const tester* tst : testers)
  {
    if (*tst->subcommand)
    {
      return tst->run();
    }
  }
  assert(false);
}

int main(int argc, char** argv)
{
  CLI::App app;
  app.require_subcommand(1); // not giving a subcommand is an error

  auto dir = dir_tester(&app);
  auto fuzz = fuzz_tester(&app);

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  return run_selected_tester({&dir, &fuzz});
}

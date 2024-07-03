#include "bfs.h"
#include "infix.h"
#include "progspace.h"
#include "test_util.h"
#include "trieste/fuzzer.h"
#include "trieste/token.h"
#include "trieste/writer.h"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <initializer_list>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

namespace
{
  inline bool contains_tuple_ops(trieste::Node node)
  {
    return node == infix::Tuple || node == infix::Append ||
      node == infix::TupleIdx ||
      std::any_of(node->begin(), node->end(), contains_tuple_ops);
  }
}

int main(int argc, char** argv)
{
  CLI::App app;
  std::filesystem::path test_dir;
  std::optional<std::filesystem::path> debug_path;
  // dir mode, scan a directory and check all examples
  auto dir = app.add_subcommand("dir");
  dir->add_option("test-dir", test_dir, "The directory containing tests.");
  dir->add_option(
    "--dump-passes", debug_path, "Directory to store debug ASTs.");

  infix::Config fuzz_config;
  std::optional<uint32_t> fuzzer_start_seed = std::nullopt;
  uint32_t fuzzer_seed_count = 100;
  bool fuzzer_fail_fast = false;

  // fuzz mode, fuzz test a given configuration
  auto fuzz = app.add_subcommand("fuzz");
  fuzz_config.install_cli(fuzz);
  fuzz->add_option("--start-seed", fuzzer_start_seed, "Seed to start RNG");
  fuzz->add_option(
    "--seed-count", fuzzer_seed_count, "Number of fuzzing iterations");
  fuzz->add_flag("--fail-fast", fuzzer_fail_fast, "Stop on first error");

  auto fuzz_reader = fuzz->add_subcommand("reader");
  auto fuzz_calculate = fuzz->add_subcommand("calculate");

  // use breadth-first program generation to test a comprehensive collection of
  // small programs
  auto bfs_test = app.add_subcommand("bfs_test");
  infix::Config bfs_test_config;
  unsigned int bfs_op_count = 1;
  unsigned int bfs_depth = 0;
  unsigned int bfs_test_concurrency = std::thread::hardware_concurrency();
  if (bfs_test_concurrency == 0)
  {
    bfs_test_concurrency = 1;
  }

  bfs_test_config.install_cli(bfs_test);
  bfs_test->add_option(
    "--op-count",
    bfs_op_count,
    "How many operations to generate (defaults to 1)");
  bfs_test->add_option(
    "--depth",
    bfs_depth,
    "How deeply nested should expressions be? (defaults to 0)");
  bfs_test->add_option(
    "--concurrency",
    bfs_test_concurrency,
    "How many concurrent tasks to use (defaults to " +
      std::to_string(bfs_test_concurrency) + ")");

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  if (*dir)
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
                   first_line.starts_with("//!"))
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

          for (auto first_line : first_lines)
          {
            std::cout << "Testing file " << entry.path() << ", expected "
                      << expected_file.filename() << ", "
                      << first_line.substr(4) << " ... " << std::flush;

            // config for current run
            infix::Config config;
            bool expect_fail = false;
            auto proc_options = {"parse_only", "calculate"};
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

            trieste::ProcessResult result;
            if (selected_proc == "parse_only")
            {
              result = reader.read();
            }
            else if (selected_proc == "calculate")
            {
              result = reader >>
                infix::calculate()
                  .wf_check_enabled(true)
                  .debug_enabled(bool(debug_path))
                  .debug_path(debug_path ? *debug_path / "calculate" : "");
            }
            else
            {
              assert(false);
            }

            std::string actual_str;
            {
              std::ostringstream result_to_str;
              result_to_str << result.ast;
              actual_str = result_to_str.str();
            }

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
              // TODO: print more errors?
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
  }
  else if (*fuzz)
  {
    fuzz_config.sanity();
    trieste::Fuzzer fuzzer;
    if (*fuzz_reader)
    {
      std::cout << "Fuzzing reader..." << std::endl;
      fuzzer = trieste::Fuzzer(infix::reader(fuzz_config));
    }
    else if (*fuzz_calculate)
    {
      std::cout << "Fuzzing calculate..." << std::endl;
      fuzzer = trieste::Fuzzer(
        infix::calculate(), infix::reader(fuzz_config).parser().generators());
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
  else if (*bfs_test)
  {
    bfs_test_config.sanity();
    std::cout << "Testing BFS-generated programs, up to depth " << bfs_depth
              << ". [concurrency factor = " << bfs_test_concurrency << "]"
              << std::endl;
    unsigned int ok_count = 0;

    for (unsigned int curr_bfs_depth = 0; curr_bfs_depth <= bfs_depth;
         ++curr_bfs_depth)
    {
      std::cout << "Exploring depth " << curr_bfs_depth << "..." << std::endl;
      auto valid_calcs =
        progspace::valid_calculation(bfs_op_count, curr_bfs_depth);
      auto valid_calc_str_pairs =
        valid_calcs.flat_map<std::pair<trieste::Node, progspace::CSData>>(
          [](trieste::Node calculation) {
            return progspace::calculation_strings(calculation)
              .concat([=]() {
                // also check that the "real" writer agrees with us; no desyncs!
                auto synth_dest = trieste::DestinationDef::synthetic();
                auto result = (trieste::Top << calculation) >>
                  infix::writer("infix").destination(synth_dest);
                if (!result.ok)
                {
                  std::cout
                    << "Something went wrong when trying to render this AST:"
                    << std::endl
                    << calculation << std::endl;
                  std::exit(1);
                }

                auto files = synth_dest->files();
                return progspace::CSData{
                  files.at("./infix"),
                  false, // always false for default writer
                };
              })
              .map<std::pair<trieste::Node, progspace::CSData>>(
                [=](progspace::CSData csdata) {
                  return std::pair{calculation->clone(), csdata};
                });
          });

      std::queue<std::future<std::optional<std::string>>> tasks_pending;
      auto pump_tasks_pending = [&](bool ok, std::size_t target_size) -> bool {
        while (tasks_pending.size() > target_size)
        {
          auto err_msg = tasks_pending.front().get();
          tasks_pending.pop();
          if (err_msg && ok)
          {
            std::cout << *err_msg;
            ok = false;
          }

          ++ok_count;
          auto print_count = [&]() {
            if (ok)
            {
              std::cout << ok_count << " programs ok..." << std::endl;
            }
          };
          if (ok_count > 1000)
          {
            if (ok_count % 1000 == 0)
            {
              print_count();
            }
          }
          else if (ok_count % 100 == 0)
          {
            print_count();
          }
        }
        return ok;
      };

      for (auto [calculation, csdata] : valid_calc_str_pairs)
      {
        auto task_fn = [=]() -> std::optional<std::string> {
          std::ostringstream out;
          bool prog_contains_tuple_ops = contains_tuple_ops(calculation);
          auto prog = trieste::Top << calculation;
          // this will fix up symbol tables for our generated tree (or the
          // sumbol tables will be empty and our tests will fail!)
          if (!infix::wf.build_st(prog))
          {
            out << "Problem rebuilding symbol table for this program:"
                << std::endl
                << prog << std::endl
                << "Aborting." << std::endl;
            return out.str();
          }

          std::string rendered_str = csdata.str.str();
          auto reader = infix::reader(bfs_test_config)
                          .synthetic(rendered_str)
                          .wf_check_enabled(true);

          {
            auto result = reader.read();

            bool expect_failure = false;
            if (!bfs_test_config.enable_tuples && prog_contains_tuple_ops)
            {
              expect_failure = true; // tuples are not allowed here
            }
            if (
              bfs_test_config.tuples_require_parens &&
              csdata.tuple_parens_omitted)
            {
              expect_failure = true; // omitted tuple parens must cause failure,
                                     // or at least a mis-parse (see below)
            }

            bool ok = true;
            if (!result.ok && !expect_failure)
            {
              out << "Error reparsing this AST:" << std::endl
                  << prog << std::endl;
              ok = false;
            }
            else if (result.ok && expect_failure)
            {
              // only report an unexpected success if the AST is somehow
              // perfectly right. mis-parsing counts as an error when it's due
              // to a configuration mismatch.
              if (prog->equals(result.ast))
              {
                out << "Should have had error reparsing this AST:" << std::endl
                    << prog << std::endl
                    << "Based on this string:" << std::endl
                    << rendered_str << std::endl;
                ok = false;
              }
            }

            auto result_str = result.ast->str();
            auto prog_str = prog->str();
            // if we were expecting failure, it won't match anyhow
            if (result_str != prog_str && !expect_failure)
            {
              out << "Didn't reparse the same AST." << std::endl
                  << "What we generated:" << std::endl
                  << prog_str << std::endl
                  << "----" << std::endl
                  << "What we rendered:" << std::endl
                  << rendered_str << std::endl
                  << "----" << std::endl
                  << "What we reparsed (diffy view):" << std::endl;
              diffy_print(prog_str, result_str, out);
              ok = false;
            }

            // extra checking: smoke-test calculation.
            // for something we constructed that has valid names,
            // the only error should come from type problems in maths
            if (ok)
            {
              auto process_result = prog >> infix::calculate();
              if (process_result.ok)
              {
                // that's fine, smoke test ok
              }
              else
              {
                auto diagnostic = [&]() {
                  out << "Program:" << std::endl
                      << prog << std::endl
                      << "Last state (from pass \"" << process_result.last_pass
                      << "\"):" << std::endl
                      << process_result.ast << std::endl;
                };
                if (process_result.last_pass != "math_errs")
                {
                  out << "Calculation failed somewhere other than the "
                         "math_errs pass."
                      << std::endl;
                  diagnostic();
                  ok = false;
                }
                else if (process_result.errors.size() == 0)
                {
                  // if the AST had an error in it, we would have been ok
                  // because that's coded-for
                  out << "Calculation failed due to a WF error, not a handled "
                         "error - it failed without any error nodes."
                      << std::endl;
                  diagnostic();
                  ok = false;
                }
              }
            }

            if (!ok)
            {
              out << "Aborting." << std::endl;
              return out.str();
            }
          }
          return std::nullopt;
        };

        if (!pump_tasks_pending(true, bfs_test_concurrency))
        {
          // drain and ignore all other ongoing tasks (multiple errors are
          // spammy and difficult to parse, and it won't be consistent across
          // runs anyway)
          pump_tasks_pending(false, 0);
          return 1;
        }

        tasks_pending.emplace(std::async(std::launch::async, task_fn));
      }

      if (!pump_tasks_pending(true, 0))
      {
        return 1;
      }
    }

    std::cout << "Tested " << ok_count << " programs, all ok." << std::endl;
  }
  else
  {
    assert(false);
  }

  return 0;
}

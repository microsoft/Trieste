#include "bfs.h"
#include "infix.h"
#include "progspace.h"
#include "test_util.h"
#include "trieste/fuzzer.h"
#include "trieste/reader.h"
#include "trieste/rewriter.h"
#include "trieste/token.h"
#include "trieste/writer.h"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <initializer_list>
#include <memory>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
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

using namespace std::string_view_literals;

int main(int argc, char** argv)
{
  CLI::App app;
  app.require_subcommand(1); // not giving a subcommand is an error

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
  unsigned int bfs_op_count = 1;
  unsigned int bfs_depth = 0;
  unsigned int bfs_test_concurrency = std::thread::hardware_concurrency();
  if (bfs_test_concurrency == 0)
  {
    bfs_test_concurrency = 1;
  }
  bool bfs_test_no_vt100 = false;
  bool bfs_test_run_calculate = false;

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
  bfs_test->add_flag(
    "--no-vt100",
    bfs_test_no_vt100,
    "Disable VT100 escapes that display progress by rewriting the current "
    "line");
  bfs_test->add_flag(
    "--run-calculate",
    bfs_test_run_calculate,
    "Run the calculate pass on all generated ASTs");

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
  }
  else if (*fuzz)
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
  else if (*bfs_test)
  {
    std::cout << "Testing BFS-generated programs, up to depth " << bfs_depth
              << ". [concurrency factor = " << bfs_test_concurrency << "]"
              << std::endl;
    unsigned int ok_count = 0;

    for (unsigned int curr_bfs_depth = 0; curr_bfs_depth <= bfs_depth;
         ++curr_bfs_depth)
    {
      bool first_status_print = true;
      std::cout << "Exploring depth " << curr_bfs_depth << "..." << std::endl;

      using TaskFn = std::function<std::optional<std::string>()>;

      auto valid_calcs =
        progspace::valid_calculation(bfs_op_count, curr_bfs_depth);

      auto all_tasks =
        valid_calcs.flat_map<TaskFn>([=](trieste::Node calculation) {
          auto tasks_for_calc =
            progspace::calculation_strings(calculation)
              .or_fn([=]() {
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
              .flat_map<std::pair<infix::Config, progspace::CSData>>(
                [](progspace::CSData csdata) {
                  using RR =
                    bfs::Result<std::pair<infix::Config, progspace::CSData>>;
                  return RR({
                              infix::Config{
                                false,
                                false,
                                false,
                              },
                              csdata,
                            })
                    .or_(RR({
                      infix::Config{
                        false,
                        true,
                        false,
                      },
                      csdata,
                    }))
                    .or_(RR({
                      infix::Config{
                        false,
                        true,
                        true,
                      },
                      csdata,
                    }))
                    .or_(RR({
                      infix::Config{
                        true,
                        true,
                        true,
                      },
                      csdata,
                    }));
                })
              .map<TaskFn>([=](auto pair) {
                auto config = pair.first;
                auto csdata = pair.second;
                return [=]() -> std::optional<std::string> {
                  std::ostringstream out;
                  bool prog_contains_tuple_ops =
                    contains_tuple_ops(calculation);
                  auto prog = trieste::Top << calculation->clone();
                  // this will fix up symbol tables for our generated tree (or
                  // the sumbol tables will be empty and our tests will fail!)
                  if (!infix::wf.build_st(prog))
                  {
                    out << "Problem rebuilding symbol table for this program:"
                        << std::endl
                        << prog << std::endl
                        << "Aborting." << std::endl;
                    return out.str();
                  }

                  std::string rendered_str = csdata.str.str();
                  auto reader = infix::reader(config)
                                  .synthetic(rendered_str)
                                  .wf_check_enabled(true);

                  {
                    auto result = reader.read();

                    bool expect_failure = false;
                    if (!config.enable_tuples && prog_contains_tuple_ops)
                    {
                      expect_failure = true; // tuples are not allowed here
                    }
                    if (
                      config.tuples_require_parens &&
                      csdata.tuple_parens_omitted)
                    {
                      expect_failure =
                        true; // omitted tuple parens must cause failure,
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
                      // perfectly right. mis-parsing counts as an error when
                      // it's due to a configuration mismatch.
                      if (prog->equals(result.ast))
                      {
                        out << "Should have had error reparsing this AST:"
                            << std::endl
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

                    if (!ok)
                    {
                      out << "Aborting." << std::endl;
                      return out.str();
                    }
                  }
                  return std::nullopt;
                };
              });

          if (bfs_test_run_calculate)
          {
            tasks_for_calc = tasks_for_calc.or_fn([=]() -> bfs::Result<TaskFn> {
              return bfs::Result<TaskFn>([=]() -> std::optional<std::string> {
                std::ostringstream out;
                bool ok = true;

                trieste::Node prog = trieste::Top << calculation->clone();
                if (!infix::wf.build_st(prog))
                {
                  out << "Problem rebuilding symbol table for this program:"
                      << std::endl
                      << prog << std::endl
                      << "Aborting." << std::endl;
                  return out.str();
                }

                // extra checking: smoke-test calculation.
                // for something we constructed that has valid names,
                // the only error should come from type problems in maths
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
                        << "Last state (from pass \""
                        << process_result.last_pass << "\"):" << std::endl
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
                    out
                      << "Calculation failed due to a WF error, not a handled "
                         "error - it failed without any error nodes."
                      << std::endl;
                    diagnostic();
                    ok = false;
                  }
                }

                if (!ok)
                {
                  out << "Aborting." << std::endl;
                  return out.str();
                }

                return std::nullopt;
              });
            });
          }

          return tasks_for_calc;
        });

      // This is a simplified async task queue. It makes things faster than
      // single-threaded, while not increasing complexity very much. It
      // parallelizes only the assertions, not case generation. We give it a max
      // size which indicates how many concurrent tasks we allow. Responses from
      // tasks are an optional string, which is an error message if there is
      // one, or nothing which means "carry on".

      // Notably, the queue structure ensures that we "see" task completions in
      // the order we added them. Sure, it is possible for one task to hold up
      // the completion queue and prevent retiring newer tasks that have
      // completed, but assuming most tasks are relatively uniform, and having
      // measured a roughly 2x speed-up, it seems worth it for usability to
      // report errors as-if we were running on a single thread still.
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
              // for less spammy output, we have the options of rewriting the
              // current line using a VT100 escape.
              if (!bfs_test_no_vt100)
              {
                if (first_status_print)
                {
                  first_status_print = false;
                }
                else
                {
                  // after writing first progress update, go up one line, clear,
                  // and go to start
                  std::cout << "\033[1A\033[2K\r";
                }
              }
              std::cout << ok_count << " cases ok..." << std::endl;
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

      for (auto task_fn : all_tasks)
      {
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

    std::cout << "Tested " << ok_count << " cases, all ok." << std::endl;
  }
  else
  {
    assert(false);
  }

  return 0;
}

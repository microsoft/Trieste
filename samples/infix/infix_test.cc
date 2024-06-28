#include "bfs.h"
#include "infix.h"
#include "trieste/fuzzer.h"
#include "trieste/token.h"
#include "trieste/writer.h"

#include <CLI/CLI.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
  std::vector<std::string> split_lines(const std::string& str)
  {
    std::vector<std::string> lines;

    std::istringstream in(str);
    std::string line;
    while (std::getline(in, line))
    {
      lines.push_back(line);
    }

    return lines;
  }

  void diffy_print(
    const std::string& expected, const std::string& actual, std::ostream& out)
  {
    auto expected_lines = split_lines(expected);
    auto actual_lines = split_lines(actual);

    std::size_t pos = 0;
    for (const auto& actual_line : actual_lines)
    {
      if (pos < expected_lines.size())
      {
        auto expected_line = expected_lines[pos];
        if (actual_line == expected_line)
        {
          out << "  " << actual_line << std::endl;
        }
        else
        {
          out << "! " << actual_line << std::endl;
        }
      }
      else if (pos - expected_lines.size() > 3)
      {
        out << "..." << std::endl;
        break;
      }
      else
      {
        out << "+ " << actual_line << std::endl;
      }

      ++pos;
    }
  }

  namespace progspace
  {
    using namespace infix;

    using R = bfs::Result<trieste::Node>;
    using Env = std::set<std::string>;
    using RP = bfs::Result<std::pair<trieste::Node, Env>>;

    R valid_expression(Env env, int depth)
    {
      if (depth == 0)
      {
        return R(Expression << (Int ^ "0"))
          .concat([]() { return R(Expression << (Int ^ "1")); })
          .concat([env]() {
            R ref{};
            for (const auto& name : env)
            {
              ref = ref.concat(
                [name]() { return R(Expression << (Ref << (Ident ^ name))); });
            }
            return ref;
          })
          .concat([]() { return R(Expression << (Tuple ^ "")); })
          .concat([]() { return R(Expression << (Append ^ "")); });
      }
      else
      {
        auto sub_expr = valid_expression(env, depth - 1);
        return (sub_expr.flat_map<trieste::Node>([sub_expr](auto lhs) {
          return R(Expression << (Tuple << lhs))
            .concat(R(Expression << (Append << lhs->clone())))
            .concat([sub_expr, lhs]() {
              return sub_expr.flat_map<trieste::Node>([lhs](auto rhs) {
                // note: we add fake locations to some binops, because the
                // writer assumes their location
                //       is also their lexical representation
                return R(Expression
                         << ((Add ^ "+") << lhs->clone() << rhs->clone()))
                  .concat(
                    R(Expression
                      << ((Subtract ^ "-") << lhs->clone() << rhs->clone())))
                  .concat(
                    R(Expression
                      << ((Multiply ^ "*") << lhs->clone() << rhs->clone())))
                  .concat(
                    R(Expression
                      << ((Divide ^ "/") << lhs->clone() << rhs->clone())))
                  .concat(
                    R(Expression << (Tuple << lhs->clone() << rhs->clone())))
                  .concat(
                    R(Expression << (Append << lhs->clone() << rhs->clone())))
                  .concat(R(
                    Expression << (TupleIdx << lhs->clone() << rhs->clone())));
              });
            });
        }));
      }
    }

    R valid_assignment(Env env, std::string name, int depth)
    {
      return valid_expression(env, depth)
        .flat_map<trieste::Node>([name](auto value) {
          return R(Assign << (Ident ^ name) << value->clone());
        });
    }

    R valid_calculation(int op_count, int depth)
    {
      RP assigns = RP({NodeDef::create(Calculation), {}});
      std::array valid_names = {"foo", "bar", "ping", "bnorg"};
      assert(op_count < int(valid_names.size()));
      for (int i = 0; i < op_count; ++i)
      {
        auto name = valid_names[i];
        assigns = assigns.flat_map<std::pair<trieste::Node, Env>>(
          [depth, name](auto pair) {
            auto [calculation, env] = pair;
            return valid_assignment(env, name, depth)
              .template map<std::pair<trieste::Node, Env>>(
                [calculation, env](auto assign) {
                  return std::pair{calculation->clone() << assign, env};
                });
          });
      }
      return assigns.map<trieste::Node>([](auto pair) { return pair.first; });
    }
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
  int bfs_op_count = 1;
  int bfs_depth = 0;
  bfs_test->add_option(
    "--op-count",
    bfs_op_count,
    "How many operations to generate (defaults to 1)");
  bfs_test->add_option(
    "--depth",
    bfs_depth,
    "How deeply nested should expressions be? (defaults to 0)");

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
              std::vector<char*> fake_argv;
              {
                // This hack forces CLI11 to parse an initial line of the
                // file by tokenizing it as a traditional argv. Technically, the
                // program name is "//!". This makes a complete mess of the
                // string by putting NUL everywhere, but the string will be
                // tossed afterwards anyway.
                bool was_space = true;
                for (char* ptr = first_line.data(); *ptr != 0; ++ptr)
                {
                  if (*ptr == ' ')
                  {
                    was_space = true;
                    *ptr = 0;
                  }
                  else
                  {
                    if (was_space == true)
                    {
                      fake_argv.push_back(ptr);
                    }
                    was_space = false;
                  }
                }
              }

              CLI::App config_app;
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
                config_app.parse(fake_argv.size(), fake_argv.data());
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
    std::cout << "Testing BFS-generated programs, up to depth " << bfs_depth
              << "." << std::endl;
    int ok_count = 0;

    for (int curr_bfs_depth = 0; curr_bfs_depth <= bfs_depth; ++curr_bfs_depth)
    {
      std::cout << "Exploring depth " << curr_bfs_depth << "..." << std::endl;
      auto valid_calcs =
        progspace::valid_calculation(bfs_op_count, curr_bfs_depth);

      for (auto calculation : valid_calcs)
      {
        auto prog = trieste::Top << calculation;
        // this will fix up symbol tables for our generated tree (or the sumbol
        // tables will be empty and our tests will fail!)
        if (!infix::wf.build_st(prog))
        {
          std::cout << "Problem rebuilding symbol table for this program:"
                    << std::endl
                    << prog << std::endl
                    << "Aborting." << std::endl;
          return 1;
        }

        auto synth_dest = trieste::DestinationDef::synthetic();

        {
          // TODO: this test is simple to write, but ideally I'd want a
          // generator that adds little anomalies, like
          //       optional commas, optional parens, etc. That will really
          //       stress the parser. Could re-use the bfs infra to do that,
          //       though it recreates the writer, but also the writer is quite
          //       simple...
          auto result = prog >> infix::writer("infix").destination(synth_dest);
          if (!result.ok)
          {
            std::cout << "Something went wrong when trying to render this AST:"
                      << std::endl
                      << prog << std::endl;
            return 1;
          }
        }

        auto files = synth_dest->files();
        auto rendered_str = files.at("./infix");

        infix::Config config;
        config.enable_tuples = true;
        config.sanity();
        auto reader =
          infix::reader(config).synthetic(rendered_str).wf_check_enabled(true);

        {
          auto result = reader.read();
          bool ok = true;
          if (!result.ok)
          {
            std::cout << "Error reparsing this AST:" << std::endl
                      << prog << std::endl;
            ok = false;
          }
          auto result_str = result.ast->str();
          auto prog_str = prog->str();
          if (result_str != prog_str)
          {
            std::cout << "Didn't reparse the same AST." << std::endl
                      << "What we generated:" << std::endl
                      << prog_str << std::endl
                      << "----" << std::endl
                      << "What we rendered:" << std::endl
                      << rendered_str << std::endl
                      << "----" << std::endl
                      << "What we reparsed (diffy view):" << std::endl;
            diffy_print(prog_str, result_str, std::cout);
            ok = false;
          }
          if (!ok)
          {
            std::cout << "Aborting." << std::endl;
            return 1;
          }
        }

        ++ok_count;
        auto print_count = [&]() {
          std::cout << ok_count << " programs ok..." << std::endl;
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
    }

    std::cout << "Tested " << ok_count << " programs, all ok." << std::endl;
  }
  else
  {
    assert(false);
  }

  return 0;
}

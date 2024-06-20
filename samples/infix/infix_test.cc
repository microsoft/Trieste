#include "infix.h"
#include "trieste/fuzzer.h"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

  auto fuzz = app.add_subcommand("fuzz");
  fuzz_config.install_cli(fuzz);
  fuzz->add_option("--start-seed", fuzzer_start_seed, "Seed to start RNG");
  fuzz->add_option(
    "--seed-count", fuzzer_seed_count, "Number of fuzzing iterations");
  fuzz->add_flag("--fail-fast", fuzzer_fail_fast, "Stop on first error");

  auto fuzz_reader = fuzz->add_subcommand("reader");
  auto fuzz_calculate = fuzz->add_subcommand("calculate");

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
                      << first_line.substr(4) << " ..." << std::endl;

            // config for current run
            infix::Config config;
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

            if (actual_str != expected_output)
            {
              std::cout << "Unexpected output:" << std::endl;
              std::cout << actual_str;
              std::cout << std::endl;
              std::cout << expected_output << std::endl;
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
  else
  {
    assert(false);
  }

  return 0;
}

#include "infix.h"
#include "trieste/logging.h"

#include <CLI/CLI.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
  CLI::App app;
  std::filesystem::path test_dir;
  app.add_option("test-dir", test_dir, "The directory containing tests.");
  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

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
        std::cout << "Testing file " << entry.path() << ", config " << idx
                  << " ..." << std::endl;
        infix::Config config;

        std::ifstream expected_reader(expected_file);
        std::string first_line;
        std::getline(expected_reader, first_line);
        if (first_line.starts_with("//!"))
        {
          std::vector<char*> fake_argv;
          {
            // This ... hack ... forces CLI11 to parse the first line of the
            // file by tokenizing it as a traditional argv. Technically, the
            // program name is "//!". This makes a complete mess of the string
            // by putting NUL everywhere, but the string will be tossed
            // afterwards anyway.
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
          config_app.add_flag(
            "--enable-tuples", config.enable_tuples, "Enable tuple parsing");
          config_app.add_flag(
            "--use-parser-tuples",
            config.use_parser_tuples,
            "Capture tuples in the parser");
          config_app.add_flag(
            "--tuples-require-parens",
            config.tuples_require_parens,
            "Tuples must be enclosed in parens");

          try
          {
            config_app.parse(fake_argv.size(), fake_argv.data());
          }
          catch (const CLI::ParseError& e)
          {
            return config_app.exit(e);
          }
        }

        auto reader = infix::reader(config).file(entry.path());

        trieste::ProcessResult result = reader >> infix::calculate();
        if (result.ok)
        {
          std::cout << "ok" << std::endl;
        }
        else
        {
          trieste::logging::Error err;
          result.print_errors(err);
        }
      }
      if (idx == 0)
      {
        std::cout << "Expected file " << expected_file
                  << " not found, skipping." << std::endl;
      }
      else
      {
        // We enumerated at least one. Don't yell about it, we probably just ran
        // out.
      }
    }
  }

  return 0;
}

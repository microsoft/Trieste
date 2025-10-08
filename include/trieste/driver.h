// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "fuzzer.h"
#include "checker.h"
#include "trieste.h"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <random>

namespace trieste
{
  class Driver
  {
  private:
    using PassIt = std::vector<Pass>::iterator;

    Reader reader;
    CLI::App app;
    Options* options;

  public:
    Driver(const Reader& reader_, Options* options_ = nullptr)
    : reader(reader_), app(reader_.language_name()), options(options_)
    {}

    Driver(
      const std::string& language_name_,
      Options* options_,
      Parse parser_,
      std::vector<Pass> passes_)
    : Driver({language_name_, passes_, parser_}, options_)
    {}

    int run(int argc, char** argv)
    {
      app.set_help_all_flag("--help-all", "Expand all help");
      app.require_subcommand(1);

      // Build command line options.
      auto build = app.add_subcommand("build", "Build a path");

      std::string log_level;
      build
        ->add_option(
          "-l,--log_level",
          log_level,
          "Set Log Level to one of "
          "Trace, Debug, Info, "
          "Warning, Output, Error, "
          "None")
        ->check(logging::set_log_level_from_string);

      bool wfcheck = true;
      build->add_flag("-w", wfcheck, "Check well-formedness.");

      std::vector<std::string> pass_names = reader.pass_names();
      std::string end_pass = pass_names.back();
      build->add_option("-p,--pass", end_pass, "Run up to this pass.")
        ->transform(CLI::IsMember(pass_names));

      std::filesystem::path path;
      build->add_option("path", path, "Path to compile.")->required();

      std::filesystem::path output;
      build->add_option("-o,--output", output, "Output path.");

      std::string language_name = reader.language_name();
      build->add_option(
        "-n,--language_name", language_name, "Language name to use for the output file.");

      std::filesystem::path dump_passes;
      build->add_option(
        "--dump_passes", dump_passes, "Dump passes to the supplied directory.");

      // Custom command line options when building.
      if (options)
        options->configure(*build);

      // Test command line options.
      auto test = app.add_subcommand("test", "Run automated tests");

      uint32_t test_seed_count = 100;
      test->add_option(
        "-c,--seed_count", test_seed_count, "Number of iterations per pass");

      uint32_t test_seed = std::random_device()();
      test->add_option("-s,--seed", test_seed, "Random seed for testing");

      auto pass_names_no_parse = std::vector(pass_names.begin() + 1, pass_names.end());

      std::string test_start_pass;
      test->add_option("start", test_start_pass, "Start at this pass.")
        ->transform(CLI::IsMember(pass_names_no_parse));

      std::string test_end_pass;
      test->add_option("end", test_end_pass, "End at this pass.")
        ->transform(CLI::IsMember(pass_names_no_parse));

      test
        ->add_option(
          "-l,--log_level",
          log_level,
          "Set Log Level to one of "
          "Trace, Debug, Info, "
          "Warning, Output, Error, "
          "None")
        ->check(logging::set_log_level_from_string);

      size_t test_max_depth = 10;
      test->add_option(
        "-d,--max_depth", test_max_depth, "Maximum depth of AST to test");

      bool test_failfast = false;
      test->add_flag("-f,--failfast", test_failfast, "Stop on first failure");

      // Test passes in sequence
      bool test_sequence = false; 
      test->add_flag("--sequence", test_sequence, "Run all passes on generated tree starting from START");

      std::optional<size_t> test_max_retries = std::nullopt;
      test->add_option("-r,--max_retries", test_max_retries,
                       "Maximum number of retries for finding unique trees");

      auto bound_vars = true;
      test->add_option("--gen_bound", bound_vars,
                       "Generate bound variable names if possible");

      // Subcommand to test entropy of random number generation.
      auto entropy = test->add_subcommand("debug_entropy",
                                          "Test entropy of random number generation, using seed_count seeds and max_depth warm-up");

      // Checker command line options.
      auto check = app.add_subcommand("check",
                                      "Check patterns for bugs");

      std::string check_start_pass;
      check->add_option("start", check_start_pass, "Start at this pass.")
        ->transform(CLI::IsMember(pass_names_no_parse));

      std::string check_end_pass;
      check->add_option("end", check_end_pass, "End at this pass.")
        ->transform(CLI::IsMember(pass_names_no_parse));

      check
        ->add_option(
          "-l,--log_level",
          log_level,
          "Set Log Level to one of "
          "Trace, Debug, Info, "
          "Warning, Output, Error, "
          "None")
        ->check(logging::set_log_level_from_string);

      bool check_wf = false;
      check->add_flag("-w", check_wf, "Check patterns for tokens that are not mentioned in well-formedness rules.");

      std::vector<std::string> ignored_tokens;
      check->add_option(
        "-i,--ignore_token",
        ignored_tokens,
        "Ignore this token when checking patterns against well-formedness rules.");

      try
      {
        app.parse(argc, argv);
      }
      catch (const CLI::ParseError& e)
      {
        return app.exit(e);
      }

      int ret = 0;

      if (*build)
      {
        reader.executable(argv[0])
          .file(path)
          .language_name(language_name)
          .debug_enabled(!dump_passes.empty())
          .debug_path(dump_passes)
          .wf_check_enabled(wfcheck)
          .end_pass(end_pass);
        if (path.extension() == ".trieste")
        {
          auto source = SourceDef::load(path);
          auto view = source->view();
          auto pos = std::min(view.find_first_of('\n'), view.size());
          auto pos2 = std::min(view.find_first_of('\n', pos + 1), view.size());
          auto pass = view.substr(pos + 1, pos2 - pos - 1);

          if (view.compare(0, pos, reader.language_name()) != 0)
          {
            logging::Debug() << "File " << path
                             << " does not start with the language name \""
                             << reader.language_name() << "\"" << std::endl;
          }
          reader.start_pass(pass).offset(pos2 + 1);
        }

        auto result = reader.read();

        if (output.empty())
          output = path.stem().replace_extension(".trieste");

        std::ofstream f(output, std::ios::binary | std::ios::out);

        if (f)
        {
          // Write the AST to the output file.
          f << reader.language_name() << std::endl
            << result.last_pass << std::endl
            << result.ast;
        }
        else
        {
          logging::Error() << "Could not open " << output << " for writing."
                           << std::endl;
          return 1;
        }

        if (!result.ok)
        {
          logging::Error err;
          result.print_errors(err);
          return 1;
        }
      }
      else if (*test)
      {
        if (pass_names_no_parse.empty())
        {
          logging::Error() << "No passes available for testing." << std::endl;
          return 1;
        }

        logging::Output() << "Testing x" << test_seed_count
                          << ", seed: " << test_seed << std::endl;

        if (test_start_pass.empty())
        {
          test_start_pass = pass_names_no_parse.at(0);
          test_end_pass = pass_names_no_parse.back();
        }
        else if (test_end_pass.empty())
        {
          test_end_pass = test_start_pass;
        }

        Fuzzer fuzzer = Fuzzer(reader)
          .max_retries(test_max_retries? *test_max_retries: test_seed_count * 2)
          .max_depth(test_max_depth)
          .failfast(test_failfast)
          .seed_count(test_seed_count)
          .start_index(reader.pass_index(test_start_pass))
          .end_index(reader.pass_index(test_end_pass))
          .start_seed(test_seed)
          .bound_vars(bound_vars);

        if(*entropy) 
        { 
          return fuzzer.debug_entropy();
        }
        else if (test_sequence) 
        { 
          return fuzzer.test_sequence();
        }
        else 
        {
          return fuzzer.test();
        }
      }
      else if (*check)
      {
        if (pass_names_no_parse.empty())
        {
          logging::Error() << "No passes available for checking." << std::endl;
          return 1;
        }

        if (check_start_pass.empty())
        {
          check_start_pass = pass_names_no_parse.at(0);
          check_end_pass = pass_names_no_parse.back();
        }
        else if (check_end_pass.empty())
        {
          check_end_pass = check_start_pass;
        }

        auto checker = Checker(reader)
          .start_index(reader.pass_index(check_start_pass))
          .end_index(reader.pass_index(check_end_pass))
          .check_against_wf(check_wf)
          .ignored_tokens(ignored_tokens);

        return checker.check();
      }

      return ret;
    }
  };
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "trieste.h"
#include "trieste/passes.h"

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
    Driver(const Reader& reader_, Options* options_)
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

      std::string test_start_pass;
      test->add_option("start", test_start_pass, "Start at this pass.")
        ->transform(CLI::IsMember(pass_names));

      std::string test_end_pass;
      test->add_option("end", test_end_pass, "End at this pass.")
        ->transform(CLI::IsMember(pass_names));

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

          if (view.compare(0, pos, reader.language_name()) == 0)
          {
            reader.start_pass(pass).offset(pos2 + 1);
          }
        }

        auto result = reader.read();
        if (!result.ok)
        {
          logging::Error() << result.error << std::endl;
          return -1;
        }

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
          return -1;
        }
      }
      else if (*test)
      {
        logging::Output() << "Testing x" << test_seed_count
                          << ", seed: " << test_seed << std::endl;

        if (test_start_pass.empty())
        {
          test_start_pass = pass_names.at(1);
          test_end_pass = pass_names.back();
        }
        else if (test_end_pass.empty())
        {
          test_end_pass = test_start_pass;
        }

        size_t start_pass_index = reader.pass_index(test_start_pass);
        size_t end_pass_index = reader.pass_index(test_end_pass);

        for (auto i = start_pass_index; i <= end_pass_index; i++)
        {
          auto& pass = reader.passes().at(i - 1);
          auto& wf = pass->wf();
          auto& prev =
            i > 1 ? reader.passes().at(i - 2)->wf() : reader.parser().wf();

          if (!prev || !wf)
          {
            logging::Info() << "Skipping pass: " << pass->name() << std::endl;
            continue;
          }

          logging::Info() << "Testing pass: " << pass->name() << std::endl;
          wf::push_back(prev);
          wf::push_back(wf);

          for (size_t seed = test_seed; seed < test_seed + test_seed_count;
               seed++)
          {
            auto ast =
              prev.gen(reader.parser().generators(), seed, test_max_depth);
            logging::Trace()
              << "============" << std::endl
              << "Pass: " << pass->name() << ", seed: " << seed << std::endl
              << "------------" << std::endl
              << ast << "------------" << std::endl;

            auto [new_ast, count, changes] = pass->run(ast);
            logging::Trace() << new_ast << "------------" << std::endl
                             << std::endl;

            auto ok = wf.build_st(new_ast);
            ok = wf.check(new_ast) && ok;

            if (!ok)
            {
              logging::Error err;
              if (!logging::Trace::active())
              {
                // We haven't printed what failed with Trace earlier, so do it
                // now. Regenerate the start Ast for the error message.
                err << "============" << std::endl
                    << "Pass: " << pass->name() << ", seed: " << seed
                    << std::endl
                    << "------------" << std::endl
                    << prev.gen(
                         reader.parser().generators(), seed, test_max_depth)
                    << "------------" << std::endl
                    << new_ast;
              }

              err << "============" << std::endl
                  << "Failed pass: " << pass->name() << ", seed: " << seed
                  << std::endl;
              ret = -1;

              if (test_failfast)
                return ret;
            }
          }

          wf::pop_front();
          wf::pop_front();
        }
      }

      return ret;
    }
  };
}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "logging.h"
#include "parse.h"
#include "pass.h"
#include "regex.h"
#include "wf.h"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <random>

namespace trieste
{
  struct Options
  {
    virtual void configure(CLI::App&) {}
  };

  class Driver
  {
  private:
    constexpr static auto parse_only = "parse";

    std::string language_name;
    CLI::App app;
    Options* options;
    Parse parser;
    std::vector<Pass> passes;
    std::vector<std::string> limits;

  public:
    Driver(
      const std::string& language_name_,
      Options* options_,
      Parse parser_,
      std::vector<Pass> passes_)
    : language_name(language_name_),
      app(language_name_),
      options(options_),
      parser(parser_),
      passes(passes_)
    {
      limits.push_back(parse_only);

      for (auto& pass : passes)
        limits.push_back(pass->name());
    }

    int run(int argc, char** argv)
    {
      parser.executable(argv[0]);

      app.set_help_all_flag("--help-all", "Expand all help");
      app.require_subcommand(1);

      // Build command line options.
      auto build = app.add_subcommand("build", "Build a path");

      bool diag = false;
      build->add_flag("-d,--diagnostics", diag, "Emit diagnostics.");

      if (diag)
      {
        logging::set_level<logging::Info>();
      }

      bool wfcheck = false;
      build->add_flag("-w,--wf-check", wfcheck, "Check well-formedness.");

      std::string limit = limits.back();
      build->add_option("-p,--pass", limit, "Run up to this pass.")
        ->transform(CLI::IsMember(limits));

      std::filesystem::path path;
      build->add_option("path", path, "Path to compile.")->required();

      std::filesystem::path output;
      build->add_option("-o,--output", output, "Output path.");

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
        ->transform(CLI::IsMember(limits));

      std::string test_end_pass;
      test->add_option("end", test_end_pass, "End at this pass.")
        ->transform(CLI::IsMember(limits));

      bool test_verbose = false;
      test->add_flag("-v,--verbose", test_verbose, "Verbose output");

      if (test_verbose)
      {
        logging::set_level<logging::Trace>();
      }

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
        Node ast;
        size_t start_pass = 1;
        size_t end_pass = pass_index(limit);

        if (path.extension() == ".trieste")
        {
          auto source = SourceDef::load(path);
          auto view = source->view();
          auto pos = std::min(view.find_first_of('\n'), view.size());
          auto pos2 = std::min(view.find_first_of('\n', pos + 1), view.size());
          auto pass = view.substr(pos + 1, pos2 - pos - 1);

          if (view.compare(0, pos, language_name) == 0)
          {
            // We're resuming from a specific pass.
            start_pass = pass_index(pass);
            end_pass = std::max(start_pass, end_pass);

            if (start_pass > limits.size())
            {
              logging::Error() << "Unknown pass: " << pass << std::endl;
              return -1;
            }

            // Build the AST, set up the symbol table, check well-formedness,
            // and move on to the next pass.
            ast = build_ast(source, pos2 + 1);
            bool ok = !!ast;
            start_pass++;

            // Build the symbol table and check well-formedness.
            auto& wf = passes.at(start_pass - 1)->wf();
            wf::push_back(wf);
            ok = ok && wf.build_st(ast);
            ok = ok && wf.check(ast);

            if (!ok)
              return -1;
          }
          else
          {
            // We're expecting an AST from another tool that fullfills our
            // parser AST well-formedness definition.
            start_pass = 1;
            end_pass = std::max(start_pass, end_pass);
            ast = build_ast(source, pos2 + 1);
            bool ok = !!ast;

            // Build the symbol table and check well-formedness.
            wf::push_back(parser.wf());
            ok = ok && parser.wf().build_st(ast);
            ok = ok && parser.wf().check(ast);

            if (!ok)
              return -1;
          }
        }
        else
        {
          // Parse the source path.
          if (std::filesystem::exists(path))
            ast = parser.parse(path);
          else
            logging::Error() << "File not found: " << path << std::endl;

          wf::push_back(parser.wf());
          bool ok = bool(ast);
          ok = ok && parser.wf().build_st(ast);

          if (wfcheck)
            ok = ok && parser.wf().check(ast);

          if (!ok)
          {
            end_pass = 0;
            ret = -1;
          }
        }

        for (auto i = start_pass; i <= end_pass; i++)
        {
          // Run the pass until it reaches a fixed point.
          auto& pass = passes.at(i - 1);
          auto& wf = pass->wf();
          wf::push_back(wf);

          auto [new_ast, count, changes] = pass->run(ast);
          wf::pop_front();
          ast = new_ast;

          logging::Info() << "Pass " << pass->name() << ": " << count
                          << " iterations, " << changes << " nodes rewritten."
                          << std::endl;

          if (ast->errors())
          {
            end_pass = i;
            ret = -1;
          }

          if (wf)
          {
            auto ok = wf.build_st(ast);

            if (wfcheck)
              ok = wf.check(ast) && ok;

            if (!ok)
            {
              end_pass = i;
              ret = -1;
            }
          }
        }

        wf::pop_front();

        if (output.empty())
          output = path.stem().replace_extension(".trieste");

        std::ofstream f(output, std::ios::binary | std::ios::out);

        if (f)
        {
          // Write the AST to the output file.
          f << language_name << std::endl
            << limits.at(end_pass) << std::endl
            << ast;
        }
        else
        {
          logging::Error() << "Could not open " << output << " for writing."
                           << std::endl;
          ret = -1;
        }
      }
      else if (*test)
      {
        logging::Output() << "Testing x" << test_seed_count
                          << ", seed: " << test_seed << std::endl;

        if (test_start_pass.empty())
        {
          test_start_pass = limits.at(1);
          test_end_pass = limits.back();
        }
        else if (test_end_pass.empty())
        {
          test_end_pass = test_start_pass;
        }

        size_t start_pass = pass_index(test_start_pass);
        size_t end_pass = pass_index(test_end_pass);

        for (auto i = start_pass; i <= end_pass; i++)
        {
          auto& pass = passes.at(i - 1);
          auto& wf = pass->wf();
          auto& prev = i > 1 ? passes.at(i - 2)->wf() : parser.wf();

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
            auto ast = prev.gen(parser.generators(), seed, test_max_depth);
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
              if (!test_verbose)
              {
                // We haven't printed what failed with Trace earlier, so do it
                // now. Regenerate the start Ast for the error message.
                err << "============" << std::endl
                    << "Pass: " << pass->name() << ", seed: " << seed
                    << std::endl
                    << "------------" << std::endl
                    << prev.gen(parser.generators(), seed, test_max_depth)
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

    template<typename StringLike>
    size_t pass_index(const StringLike& name_)
    {
      if (name_ == parse_only)
        return 0;

      for (size_t i = 0; i < passes.size(); i++)
      {
        if (passes[i]->name() == name_)
          return i + 1;
      }

      return std::numeric_limits<size_t>::max();
    }
  };
}

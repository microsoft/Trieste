// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "lookup.h"
#include "parse.h"
#include "pass.h"
#include "wf.h"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <random>

namespace trieste
{
  class Driver
  {
  private:
    constexpr static auto parse_only = "parse";
    using PassCheck = std::tuple<std::string, Pass, wf::WellformedF>;

    std::string name;
    CLI::App app;
    Parse parser;
    wf::WellformedF wfParser;
    std::vector<PassCheck> passes;
    std::vector<std::string> limits;

  public:
    Driver(
      const std::string& name,
      Parse parser,
      wf::WellformedF wfParser,
      std::initializer_list<PassCheck> passes)
    : name(name), app(name), parser(parser), wfParser(wfParser), passes(passes)
    {
      limits.push_back(parse_only);

      for (auto& [name_, pass, wf] : passes)
        limits.push_back(name_);
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

      bool wfcheck = false;
      build->add_flag("-w,--wf-check", wfcheck, "Check well-formedness.");

      std::string limit = limits.back();
      build->add_option("-p,--pass", limit, "Run up to this pass.")
        ->transform(CLI::IsMember(limits));

      std::filesystem::path path;
      build->add_option("path", path, "Path to compile.")->required();

      std::filesystem::path output;
      build->add_option("-o,--output", output, "Output path.");

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

          if (view.compare(0, pos, name) != 0)
          {
            std::cerr << "Not a " << name << " file" << std::endl;
            return -1;
          }

          auto pos2 = std::min(view.find_first_of('\n', pos + 1), view.size());
          auto pass = view.substr(pos + 1, pos2 - pos - 1);
          start_pass = pass_index(pass);
          end_pass = std::max(start_pass, end_pass);

          if (start_pass > limits.size())
          {
            std::cerr << "Unknown pass: " << pass << std::endl;
            return -1;
          }

          auto wf = std::get<2>(passes.at(start_pass - 1));

          if (!wf)
          {
            std::cerr << "No well-formedness check for pass: " << pass
                      << std::endl;
            return -1;
          }

          ast = wf.build_ast(source, pos2 + 1);
          start_pass++;

          if (!ast)
          {
            std::cerr << "Failed to parse AST" << std::endl;
            return -1;
          }

          wf.build_st(ast);

          if (!wf.check(ast, std::cout))
          {
            std::cerr << "Well-formedness check failed on parsed AST"
                      << std::endl;
            return -1;
          }
        }
        else
        {
          ast = parser.parse(path);

          if (wfParser && !wfParser.check(ast, std::cout))
          {
            end_pass = 0;
            ret = -1;
          }
        }

        for (auto i = start_pass; i <= end_pass; i++)
        {
          auto& [name, pass, wf] = passes.at(i - 1);
          auto [new_ast, count, changes] = pass->run(ast);
          ast = new_ast;

          if (diag)
          {
            std::cout << "Pass " << name << ": " << count << " iterations, "
                      << changes << " nodes rewritten." << std::endl;
          }

          if (wf)
            wf.build_st(ast);

          if (wfcheck && wf && !wf.check(ast, std::cout))
          {
            end_pass = i;
            ret = -1;
          }
        }

        if (ast->errors(std::cout))
          ret = -1;

        if (output.empty())
          output = path.stem().replace_extension(".trieste");

        std::ofstream f(output, std::ios::binary | std::ios::out);

        if (f)
        {
          f << name << std::endl << limits.at(end_pass) << std::endl << ast;
        }
        else
        {
          std::cerr << "Could not open " << output << " for writing."
                    << std::endl;
          ret = -1;
        }
      }
      else if (*test)
      {
        std::cout << "Testing x" << test_seed_count << ", seed: " << test_seed
                  << std::endl;

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
          auto& [name, pass, wf] = passes.at(i - 1);
          auto& prev = i > 1 ? std::get<2>(passes.at(i - 2)) : wfParser;

          if (!prev || !wf)
          {
            std::cout << "Skipping pass: " << name << std::endl;
            continue;
          }

          std::cout << "Testing pass: " << name << std::endl;

          for (size_t i = 0; i < test_seed_count; i++)
          {
            std::stringstream ss1;
            std::stringstream ss2;

            auto ast = prev.gen(test_seed + i, test_max_depth);
            ss1 << "============" << std::endl
                << "Pass: " << name << ", seed: " << (test_seed + i)
                << std::endl
                << "------------" << std::endl
                << ast << "------------" << std::endl;

            if (test_verbose)
              std::cout << ss1.str();

            auto [new_ast, count, changes] = pass->run(ast);
            ss2 << new_ast << "------------" << std::endl << std::endl;

            if (test_verbose)
              std::cout << ss2.str();

            std::stringstream ss3;

            if (!wf.check(new_ast, ss3))
            {
              if (!test_verbose)
                std::cout << ss1.str() << ss2.str();

              std::cout << ss3.str() << "============" << std::endl
                        << "Failed pass: " << name
                        << ", seed: " << (test_seed + i) << std::endl;
              ret = -1;

              if (test_failfast)
                return ret;
            }
          }
        }
      }

      return ret;
    }

    template<typename StringLike>
    size_t pass_index(const StringLike& name)
    {
      if (name == parse_only)
        return 0;

      for (size_t i = 0; i < passes.size(); i++)
      {
        if (std::get<0>(passes[i]) == name)
          return i + 1;
      }

      return std::numeric_limits<size_t>::max();
    }
  };
}

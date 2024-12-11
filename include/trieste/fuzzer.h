// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "trieste.h"

#include <random>
#include <stdexcept>

namespace trieste
{
  class Fuzzer
  {
  private:
    std::vector<Pass> passes_;
    const wf::Wellformed* input_wf_;
    GenNodeLocationF generators_;
    size_t max_depth_;
    uint32_t start_seed_;
    uint32_t seed_count_;
    bool failfast_;
    size_t start_index_;
    size_t end_index_;

  public:
    Fuzzer() {}

    Fuzzer(
      const std::vector<Pass>& passes,
      const wf::Wellformed& input_wf,
      GenNodeLocationF generators)
    : passes_(passes),
      input_wf_(&input_wf),
      generators_(generators),
      max_depth_(10),
      start_seed_(std::random_device()()),
      seed_count_(100),
      failfast_(false),
      start_index_(1),
      end_index_(passes.size() - 1)
    {}

    Fuzzer(const Reader& reader)
    : Fuzzer(
        reader.passes(), reader.parser().wf(), reader.parser().generators())
    {}

    Fuzzer(const Writer& writer, GenNodeLocationF generators)
    : Fuzzer(writer.passes(), writer.input_wf(), generators)
    {}

    Fuzzer(const Rewriter& rewriter, GenNodeLocationF generators)
    : Fuzzer(rewriter.passes(), rewriter.input_wf(), generators)
    {}

    size_t max_depth() const
    {
      return max_depth_;
    }

    Fuzzer& max_depth(size_t max_depth)
    {
      max_depth_ = max_depth;
      return *this;
    }

    uint32_t start_seed() const
    {
      return start_seed_;
    }

    Fuzzer& start_seed(uint32_t seed)
    {
      start_seed_ = seed;
      return *this;
    }

    uint32_t seed_count() const
    {
      return seed_count_;
    }

    Fuzzer& seed_count(uint32_t seed_count)
    {
      seed_count_ = seed_count;
      return *this;
    }

    bool failfast() const
    {
      return failfast_;
    }

    Fuzzer& failfast(bool failfast)
    {
      failfast_ = failfast;
      return *this;
    }

    size_t start_index() const
    {
      return start_index_;
    }

    Fuzzer& start_index(size_t start_index)
    {
      if (start_index == 0)
      {
        throw std::invalid_argument("start_index must be greater than 0");
      }

      start_index_ = start_index;
      return *this;
    }

    size_t end_index() const
    {
      return end_index_;
    }

    Fuzzer& end_index(size_t end_index)
    {
      end_index_ = end_index;
      return *this;
    }

    int test()
    {
      WFContext context;
      int ret = 0;
      for (size_t i = start_index_; i <= end_index_; i++)
      {
        auto& pass = passes_.at(i - 1);
        auto& wf = pass->wf();
        auto& prev = i > 1 ? passes_.at(i - 2)->wf() : *input_wf_;

        size_t stat_passed = 0;
        size_t stat_error = 0;
        size_t stat_failed = 0;
        std::map<std::string, size_t> error_msgs;

        if (!prev || !wf)
        {
          logging::Info() << "Skipping pass: " << pass->name() << std::endl;
          continue;
        }

        logging::Info() << "Testing pass: " << pass->name() << std::endl;
        context.push_back(prev);
        context.push_back(wf);

        for (size_t seed = start_seed_; seed < start_seed_ + seed_count_;
             seed++)
        {
          auto ast = prev.gen(generators_, seed, max_depth_);
          logging::Trace() << "============" << std::endl
                           << "Pass: " << pass->name() << ", seed: " << seed
                           << std::endl
                           << "------------" << std::endl
                           << ast << "------------" << std::endl;

          auto [new_ast, count, changes] = pass->run(ast);
          logging::Trace() << new_ast << "------------" << std::endl
                           << std::endl;

          // TODO: Why are we building the symbol tables here?
          auto ok = wf.build_st(new_ast);
          if (ok)
          {
            Nodes errors;
            new_ast->get_errors(errors);
            if (!errors.empty())
            {
              // Pass added error nodes, so doesn't need to satisfy wf.
              stat_error++;
              Node error = errors.front();
              for (auto& c : *error) {
                  if(c->type() == ErrorMsg) {
                      error_msgs[std::string(c->location().view())]++;
                      break;
                  }
              }
              continue;
            }
          }
          ok = wf.check(new_ast) && ok;

          if (!ok)
          {
            logging::Error err;
            if (!logging::Trace::active())
            {
              // We haven't printed what failed with Trace earlier, so do it
              // now. Regenerate the start Ast for the error message.
              err << "============" << std::endl
                  << "Pass: " << pass->name() << ", seed: " << seed << std::endl
                  << "------------" << std::endl
                  << prev.gen(generators_, seed, max_depth_) << "------------"
                  << std::endl
                  << new_ast;
            }

            err << "============" << std::endl
                << "Failed pass: " << pass->name() << ", seed: " << seed
                << std::endl;
            ret = 1;

            stat_failed++;

            if (failfast_)
              return ret;
          }
          if (ok) stat_passed++;
        }

        logging::Info info;

        if (stat_failed) info << "  not WF " << stat_failed << " times." << std::endl;

        if (stat_error) info << "  errored " << stat_error << " times." << std::endl;
        for (auto [msg, count] : error_msgs) {
          info << "    " << msg << ": " << count << std::endl;
        }

        if (stat_error && stat_passed) info << "  passed " << stat_passed << " times." << std::endl;

        context.pop_front();
        context.pop_front();
      }

      return ret;
    }
  };
}

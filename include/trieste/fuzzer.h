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
    size_t max_retries_;

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
      end_index_(passes.size() - 1),
      max_retries_(100)
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

    size_t max_retries() const
    {
      return max_retries_;
    }

    Fuzzer& max_retries(size_t max_retries)
    {
      max_retries_ = max_retries;
      return *this;
    }

    double calculate_entropy(std::vector<uint8_t>& byte_values) {
      std::map<uint8_t, double> freq;
      int total = byte_values.size();

      // Count occurrences of each byte value
      for (uint8_t byte : byte_values) {
        freq[byte]++;
      }

      // Compute probabilities and entropy
      double entropy = 0.0;
      for (const auto& [byte, count] : freq) {
        double p = count / total;
        entropy -= p * std::log2(p);
      }

      return entropy;
    }

    int test_entropy() {
      const uint8_t NO_BYTES = 4;
      const size_t no_samples = max_depth_;
      std::vector<std::vector<uint32_t>> seed_samples;
      for (size_t seed = start_seed_; seed < start_seed_ + seed_count_; seed++) {
        Rand rand = Rand(seed);
        std::vector<uint32_t> samples;
        for (size_t count = 0; count < no_samples; count++) {
          samples.push_back(rand());
        }
        seed_samples.push_back(samples);
      }

      for (size_t count = 0; count < no_samples; count++) {
        std::vector<uint8_t> byte_samples[NO_BYTES];
        for (size_t i = 0; i < seed_count_; i++) {
            uint32_t sample = seed_samples.at(i).at(count);
            for (int b = 0; b < NO_BYTES; b++) {
              uint8_t byte = (sample >> (8 * b)) & 0xFF;
              byte_samples[b].push_back(byte);
            }
        }

        std::string nth = count % 10 == 0 ? "1st" : count % 10 == 1 ? "2nd" : count % 10 == 2 ? "3rd" : std::to_string(count + 1) + "th";

        std::cout << "Entropy when sampling the " << nth << " value from " << seed_count_ << " adjacent starting seeds" << std::endl;
        for (int b = 0; b < NO_BYTES; b++) {
          double entropy = calculate_entropy(byte_samples[b]);
          std::cout << "== Entropy for byte " << b << ": " << entropy << " bits" << std::endl;
        }
      }

      std::vector<uint8_t> byte_samples[NO_BYTES];
      Rand rand = Rand(start_seed_);

      for (size_t count = 0; count < seed_count_; count++) {
        uint32_t sample = rand();
        for (int b = 0; b < NO_BYTES; b++) {
          uint8_t byte = (sample >> (8 * b)) & 0xFF;
          byte_samples[b].push_back(byte);
        }
      }

      std::cout << "Entropy when sampling " << seed_count_ << " values from the first seed" << std::endl;
      for (int b = 0; b < NO_BYTES; b++) {
        double entropy = calculate_entropy(byte_samples[b]);
        std::cout << "== Entropy for byte " << b << ": " << entropy << " bits" << std::endl;
      }
      return 0;
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

        size_t passed_count = 0;
        size_t trivial_count = 0;
        size_t error_count = 0;
        size_t failed_count = 0;
        std::map<std::string, size_t> error_msgs;
        std::set<size_t> ast_hashes;

        if (!prev || !wf)
        {
          logging::Info() << "Skipping pass: " << pass->name() << std::endl;
          continue;
        }

        logging::Info() << "Testing pass: " << pass->name() << std::endl;
        context.push_back(prev);
        context.push_back(wf);

        size_t retry_seed = start_seed_ + seed_count_;
        size_t retries = 0;

        for (size_t seed = start_seed_; seed < start_seed_ + seed_count_;
             seed++)
        {
          size_t actual_seed = seed;

          auto ast = prev.gen(generators_, actual_seed, max_depth_);

          size_t hash = ast->hash();
          while (ast_hashes.find(hash) != ast_hashes.end() && retries < max_retries_) {
            actual_seed = retry_seed;
            ast = prev.gen(generators_, actual_seed, max_depth_);
            hash = ast->hash();
            retry_seed++;
            retries++;
          }

          ast_hashes.insert(hash);

          logging::Trace() << "============" << std::endl
                           << "Pass: " << pass->name() << ", seed: " << actual_seed
                           << std::endl
                           << "------------" << std::endl
                           << ast << "------------" << std::endl;

          auto [new_ast, count, changes] = pass->run(ast);
          logging::Trace() << new_ast << "------------" << std::endl
                           << std::endl;

          auto ok = wf.build_st(new_ast);
          if (ok)
          {
            Nodes errors;
            new_ast->get_errors(errors);
            if (!errors.empty())
            {
              // Pass added error nodes, so doesn't need to satisfy wf.
              error_count++;
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
                  << "Pass: " << pass->name() << ", seed: " << actual_seed << std::endl
                  << "------------" << std::endl
                  << prev.gen(generators_, actual_seed, max_depth_) << "------------"
                  << std::endl
                  << new_ast;
            }

            err << "============" << std::endl
                << "Failed pass: " << pass->name() << ", seed: " << actual_seed
                << std::endl;
            ret = 1;

            failed_count++;

            if (failfast_)
              return ret;
          }
          if (ok) passed_count++;
          if (ok && changes == 0) trivial_count++;
        }

        logging::Info info;

        if (failed_count > 0) info << "  not WF " << failed_count << " times." << std::endl;

        if (error_count > 0) info << "  errored " << error_count << " times." << std::endl;
        for (auto [msg, count] : error_msgs) {
          info << "    " << msg << ": " << count << std::endl;
        }

        if ((error_count > 0 && passed_count > 0) || trivial_count > 0)
        {
          info << "  passed " << passed_count << " times." << std::endl;
          if (trivial_count > 0) info << "    trivial: " << trivial_count << std::endl;
        }

        size_t hash_unique = ast_hashes.size();
        info << "  " << ast_hashes.size() << " hash unique "
             << (hash_unique == 1? "tree": "trees")
             << " (" << retries << (retries == 1? " retry": " retries") << ")." << std::endl;

        context.pop_front();
        context.pop_front();
      }

      return ret;
    }
  };
}

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
    bool bound_vars_;
    bool test_sequence_;
    bool size_stats_;

    struct SeedContext
    {
      size_t current_seed = 0;
      size_t retry_seed = 0;
      size_t retries = 0;
      std::set<size_t> ast_hashes;
    };

    struct Survivor
    {
      Node ast;
      size_t original_seed;
      size_t total_changes;
    };

    struct PassStats
    {
      size_t passed_count = 0;
      size_t trivial_count = 0;
      size_t failed_count = 0;
      size_t error_count = 0;
      size_t change_count = 0;

      std::vector<size_t> passed_sizes;
      std::vector<size_t> passed_heights;
      std::vector<size_t> error_sizes;
      std::vector<size_t> error_heights;

      std::map<std::string, size_t> error_msgs;
      std::vector<Survivor> survivors;

      void log(bool size_stats)
      {
        logging::Info info;

        if (failed_count > 0)
          info << "  " << failed_count << " well-formedness errors." << std::endl;

        if (error_count > 0)
        {
          info << "  " << error_count << " stopped by errors." << std::endl;

          for (auto [msg, count] : error_msgs)
          {
            info << "    \"" << msg << "\": " << count << std::endl;
          }

          if (size_stats)
          {
            info << "    average size: " << avg(error_sizes) << std::endl;
            info << "    max size: " << max(error_sizes) << std::endl;
            info << "    average height: " << avg(error_heights) << std::endl;
            info << "    max height: " << max(error_heights) << std::endl;
          }
        }

        if ((error_count > 0 && passed_count > 0) || trivial_count > 0)
        {
          info << "  " << passed_count << " survivors." << std::endl;
          if (trivial_count > 0)
            info << "    " << trivial_count << " trivial." << std::endl;

          if (size_stats)
          {
            info << "    average size: " << avg(passed_sizes) << std::endl;
            info << "    max size: " << max(passed_sizes) << std::endl;
            info << "    average height: " << avg(passed_heights) << std::endl;
            info << "    max height: " << max(passed_heights) << std::endl;
          }
        }

        info << "  total changes: " << change_count << std::endl;
      }
    };

    struct SequenceStats
    {
      size_t passes_run_;
      size_t seed_count_;
      size_t initial_hash_unique_;
      size_t total_failed_;
      size_t total_errors_;
      std::vector<size_t> changes_per_pass_;
      std::vector<size_t> error_sizes_;
      std::vector<size_t> error_heights_;

      SequenceStats(size_t passes_run, size_t seed_count)
      : passes_run_(passes_run), seed_count_(seed_count), initial_hash_unique_(0), total_failed_(0), total_errors_(0)
      {}

      void log(std::vector<Survivor>& survivors, bool size_stats)
      {
        size_t total_survivors = survivors.size();
        size_t total_trivial = 0;

        std::vector<size_t> sizes;
        std::vector<size_t> heights;

        for (auto& survivor : survivors)
        {
          logging::Trace() << "Survivor from seed " << survivor.original_seed
                           << " with " << survivor.total_changes
                           << " changes:" << std::endl
                           << survivor.ast << "------------";

          if (survivor.total_changes < passes_run_)
            total_trivial++;

          if (size_stats)
          {
            sizes.push_back(survivor.ast->tree_size());
            heights.push_back(survivor.ast->tree_height());
          }
      }

      logging::Info info;
      info << "  " << seed_count_ << " initial trees (" << initial_hash_unique_
           << " hash unique)." << std::endl;

      if (total_failed_ > 0)
        info << "  " << total_failed_ << " well-formedness failures"
             << std::endl;

      if (total_errors_ > 0)
      {
        info << "  " << total_errors_ << " stopped by errors" << std::endl;

        if (size_stats)
        {
          info << "    average size: " << avg(error_sizes_) << std::endl;
          info << "    max size: " << max(error_sizes_) << std::endl;
          info << "    average height: " << avg(error_heights_) << std::endl;
          info << "    max height: " << max(error_heights_) << std::endl;
        }
      }

      info << "  " << total_survivors << " survivors ("
           << (total_survivors * 100.0 / seed_count_) << "%)." << std::endl;

      if (total_trivial > 0)
        info << "    " << total_trivial
             << " with < 1 change per pass on average ("
             << (total_trivial * 100.0 / seed_count_) << "%)." << std::endl;

      if (size_stats)
      {
        info << "    average size: " << avg(sizes) << std::endl;
        info << "    max size: " << max(sizes) << std::endl;
        info << "    average height: " << avg(heights) << std::endl;
        info << "    max height: " << max(heights) << std::endl;
      }

      info << "  average changes per pass: " << avg(changes_per_pass_)
           << std::endl;
    }
    };

    enum RunResult
    {
      OK,
      FAIL,
      ERROR
    };

    struct PassResult
    {
      Node ast;
      RunResult result;
    };

    /// @brief Generate an AST that has not been generated before (while
    /// adhering to the retry budget).
    /// @param wf The well-formedness rules to guide AST generation.
    /// @param context The seed context containing current seed and retry
    /// information.
    /// @return The generated AST node.
    Node gen_ast(const wf::Wellformed& wf, SeedContext& context)
    {
      auto ast = wf.gen(generators_, context.current_seed, max_depth_, bound_vars_);
      size_t hash = ast->hash();
      while (context.ast_hashes.find(hash) != context.ast_hashes.end() &&
             context.retries < max_retries_)
      {
        context.current_seed = context.retry_seed++;
        ast = wf.gen(generators_, context.current_seed, max_depth_, bound_vars_);
        hash = ast->hash();
        context.retries++;
      }
      context.ast_hashes.insert(hash);
      return ast;
    }

    /// @brief Run a single pass on the given AST and update statistics.
    /// @param ast The current abstract syntax tree.
    /// @param pass The pass to run.
    /// @param wf The well-formedness rules to validate the AST.
    /// @param pass_stats The recorded statistics for the pass.
    /// @return The result of running the pass, including the new AST, success status, and number of changes.
    PassResult run_pass(
      Node& ast,
      const Pass& pass,
      const wf::Wellformed& wf,
      PassStats& pass_stats)
    {
      auto [new_ast, count, changes] = pass->run(ast);

      pass_stats.change_count += changes;

      Nodes errors;
      new_ast->get_errors(errors);
      if (!errors.empty())
      {
        pass_stats.error_count++;
        if (size_stats_)
        {
          pass_stats.error_sizes.push_back(new_ast->tree_size());
          pass_stats.error_heights.push_back(new_ast->tree_height());
        }

        Node error = errors.front();
        for (auto& c : *error)
        {
          if (c->type() == ErrorMsg)
          {
            pass_stats.error_msgs[std::string(c->location().view())]++;
            break;
          }
        }
        // Pass added error nodes, so doesn't need to satisfy wf.
        return {new_ast, RunResult::ERROR};
      }

      bool ok = true;
      if (wf)
      {
        ok = wf.build_st(new_ast);
        ok = wf.check(new_ast) && ok;
      }

      if (ok)
      {
        pass_stats.passed_count++;
        if (changes == 0) pass_stats.trivial_count++;
        if (size_stats_)
        {
          pass_stats.passed_sizes.push_back(new_ast->tree_size());
          pass_stats.passed_heights.push_back(new_ast->tree_height());
        }
        return {new_ast, RunResult::OK};
      }
      else
      {
        pass_stats.failed_count++;
        return {new_ast, RunResult::FAIL};
      }
    }

    /// @brief Test a single pass over a number of generated ASTs.
    /// @param pass the pass to test
    /// @param prev the previous well-formedness spec for regenerating ASTs
    /// @param seed_context the context for seed management
    /// @return the statistics for the pass test
    PassStats test_pass(
      Pass& pass,
      const trieste::wf::Wellformed& prev,
      SeedContext& seed_context)
    {
      PassStats pass_stats;
      for (size_t seed = start_seed_; seed < start_seed_ + seed_count_; seed++)
      {
        seed_context.current_seed = seed;

        auto ast = gen_ast(prev, seed_context);

        logging::Trace() << "============" << std::endl
                         << "Pass: " << pass->name()
                         << ", seed: " << seed_context.current_seed << std::endl
                         << "------------" << std::endl
                         << ast << "------------" << std::endl;

        auto old_changes = pass_stats.change_count;
        auto [new_ast, result] = run_pass(ast, pass, pass->wf(), pass_stats);

        logging::Trace() << new_ast << "------------" << std::endl << std::endl;

        if (result == RunResult::FAIL)
        {
          logging::Error err;
          if (!logging::Trace::active())
          {
            // We haven't printed what failed with Trace earlier, so do it
            // now. Regenerate the start Ast for the error message.
            err << "============" << std::endl
                << "Pass: " << pass->name()
                << ", seed: " << seed_context.current_seed << std::endl
                << "------------" << std::endl
                << prev.gen(generators_, seed_context.current_seed, max_depth_, bound_vars_)
                << "------------" << std::endl
                << new_ast;
          }

          err << "============" << std::endl
              << "Failed pass: " << pass->name()
              << ", seed: " << seed_context.current_seed << std::endl;

          if (failfast_)
            break;
        }

        if (test_sequence_ && result == RunResult::OK)
        {
          pass_stats.survivors.push_back({new_ast, seed_context.current_seed, pass_stats.change_count - old_changes});
        }
      }

      return pass_stats;
    }

    /// @brief Test a single pass over a set of survivor ASTs.
    /// @param pass the pass to test
    /// @param survivors the survivor ASTs from the previous pass
    /// @return the statistics for the pass test
    PassStats test_pass_with_survivors(Pass& pass, std::vector<Survivor>& survivors)
    {
      PassStats pass_stats;
      for (auto& survivor : survivors)
      {
        logging::Trace() << "============" << std::endl
                         << "Pass: " << pass->name()
                         << ", survivor from seed " << survivor.original_seed << std::endl
                         << "------------" << std::endl
                         << survivor.ast << "------------" << std::endl;

        auto old_ast = survivor.ast->clone();
        auto old_changes = pass_stats.change_count;

        auto [new_ast, result] = run_pass(survivor.ast, pass, pass->wf(), pass_stats);

        logging::Trace() << new_ast << "------------" << std::endl << std::endl;

        if (result == RunResult::FAIL)
        {
          logging::Error err;
          if (!logging::Trace::active())
          {
            // We haven't printed what failed with Trace earlier, so do it
            // now. Regenerate the start Ast for the error message.
            err << "============" << std::endl
                << "Pass: " << pass->name()
                << ", survivor from seed " << survivor.original_seed << std::endl
                << "------------" << std::endl
                << old_ast
                << "------------" << std::endl
                << new_ast;
          }

          err << "============" << std::endl
              << "Failed pass: " << pass->name()
              << ", survivor from seed " << survivor.original_seed << std::endl;
          if (failfast_)
            break;
        }

        if (test_sequence_ && result == RunResult::OK)
        {
          pass_stats.survivors.push_back(
            {new_ast,
             survivor.original_seed,
             survivor.total_changes + pass_stats.change_count - old_changes});
        }
      }

      return pass_stats;
    }

    double calculate_entropy(std::vector<uint8_t>& byte_values) {
      std::map<uint8_t, double> freq;
      size_t total = byte_values.size();

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

    static size_t sum(std::vector<size_t>& v) {
      return std::accumulate(v.begin(), v.end(), (size_t) 0);
    }

    static size_t avg(std::vector<size_t>& v) {
      if (v.empty()) return 0;
      return sum(v) / v.size();
    }

    static size_t max(std::vector<size_t>& v) {
      if (v.empty()) return 0;
      return *std::max_element(v.begin(), v.end());
    }

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
      end_index_(passes.size()),
      max_retries_(100),
      bound_vars_(true),
      test_sequence_(false),
      size_stats_(false)
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

    bool test_sequence() const
    {
      return test_sequence_;
    }

    Fuzzer& test_sequence(bool test_sequence)
    {
      test_sequence_ = test_sequence;
      return *this;
    }

    bool size_stats() const
    {
      return size_stats_;
    }

    Fuzzer& size_stats(bool size_stats)
    {
      size_stats_ = size_stats;
      return *this;
    }

    int debug_entropy() {
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

    Fuzzer& bound_vars(bool gen_bound_vars) {
      bound_vars_ = gen_bound_vars;
      return *this;
    }

    int test()
    {
      if (end_index_ < start_index_)
      {
        logging::Error()
          << "pass range is empty"
          << std::endl;
        return 1;
      }
      WFContext context;
      SequenceStats sequence_stats(end_index_ - start_index_ + 1, seed_count_);
      std::vector<Survivor> survivors;

      int ret = 0;

      for (size_t i = start_index_; i <= end_index_; i++)
      {
        auto& pass = passes_.at(i - 1);
        auto& wf = pass->wf();
        auto& prev = i > 1 ? passes_.at(i - 2)->wf() : *input_wf_;

        if (!prev || !wf)
        {
          logging::Info() << "Skipping pass: " << pass->name() << std::endl;
          if (test_sequence_)
          {
            auto pass_stats = test_pass_with_survivors(pass, survivors);
            survivors = pass_stats.survivors;
          }
          continue;
        }

        logging::Info() << "Testing pass: " << pass->name();
        context.push_back(prev);
        context.push_back(wf);

        SeedContext seed_context;
        seed_context.retry_seed = start_seed_ + seed_count_;

        PassStats pass_stats;
        if (!test_sequence_ || i == start_index_)
        {
          pass_stats = test_pass(pass, prev, seed_context);
        }
        else
        {
          logging::Info() << "  " << survivors.size() << " survivors from previous pass.";
          if (survivors.empty()) {
            context.pop_front();
            context.pop_front();
            break;
          }
          pass_stats = test_pass_with_survivors(pass, survivors);
        }

        if (pass_stats.failed_count > 0)
        {
          ret = 1;
          if (failfast_) return ret;
        }

        if (test_sequence_)
        {
          survivors = pass_stats.survivors;

          sequence_stats.total_failed_ += pass_stats.failed_count;
          sequence_stats.total_errors_ += pass_stats.error_count;
          sequence_stats.changes_per_pass_.push_back(pass_stats.change_count);

          if (size_stats_)
          {
            sequence_stats.error_sizes_.insert(sequence_stats.error_sizes_.end(), pass_stats.error_sizes.begin(), pass_stats.error_sizes.end());
            sequence_stats.error_heights_.insert(sequence_stats.error_heights_.end(), pass_stats.error_heights.begin(), pass_stats.error_heights.end());
          }
        }

        size_t hash_unique = seed_context.ast_hashes.size();

        if (sequence_stats.initial_hash_unique_ == 0) sequence_stats.initial_hash_unique_ = hash_unique;

        if (hash_unique > 0)
        {
          logging::Info() << "  generated " << seed_context.ast_hashes.size()
                          << " hash unique "
                          << (hash_unique == 1 ? "tree" : "trees") << " ("
                          << seed_context.retries
                          << (seed_context.retries == 1 ? " retry" : " retries")
                          << ").";
        }

        pass_stats.log(size_stats_);

        context.pop_front();
        context.pop_front();
      }

      if (test_sequence_)
      {
        logging::Info() << "After full sequence from "
                        << passes_.at(start_index_ - 1)->name() << " to "
                        << passes_.at(end_index_ - 1)->name() << " (seed: " << start_seed_
                        << "):";

        sequence_stats.log(survivors, size_stats_);
      }

      return ret;
    }
  };
}

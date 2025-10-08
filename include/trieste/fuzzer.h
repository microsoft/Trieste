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
      bound_vars_(true)
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

          auto ast = prev.gen(generators_, actual_seed, max_depth_, bound_vars_);

          size_t hash = ast->hash();
          while (ast_hashes.find(hash) != ast_hashes.end() && retries < max_retries_) {
            actual_seed = retry_seed;
            ast = prev.gen(generators_, actual_seed, max_depth_, bound_vars_);
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
                  << prev.gen(generators_, actual_seed, max_depth_, bound_vars_) << "------------"
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

    // Error counts by message
    using ErrCount = std::map<std::string,size_t>;

    size_t avg(std::vector<size_t>& v) {
      if (v.empty()) return 0;
      return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }

    size_t max(std::vector<size_t>& v) {
      if (v.empty()) return 0;
      return *std::max_element(v.begin(), v.end());
    }

     size_t sum(std::vector<size_t>& v) {
      return std::accumulate(v.begin(), v.end(), 0.0);
    }

    int test_sequence()
      { 
        WFContext context;
        int ret = 0;
        size_t trivial_count = 0;
        size_t wf_errors = 0;
        std::map<std::string, ErrCount> error_passes; // pass name -> (error message -> count)
        std::vector<size_t> failed_ast_sizes; // tree sizes of failed runs
        std::vector<size_t> passed_ast_sizes; // tree sizes of passed runs
        std::vector<size_t> failed_ast_heights; // tree heights of failed runs
        std::vector<size_t> passed_ast_heights; // tree sizes of passed runs
        std::vector<size_t> rewrites; // total number of rewrites

        // Starting pass 
        auto& init_pass = passes_.at(start_index_ - 1);
        auto& init_wf = init_pass->wf();
        auto& gen_wf = start_index_ > 1 
              ? passes_.at(start_index_ - 2)->wf() 
              : *input_wf_;

        if (!gen_wf || !init_wf){
            logging::Error() << "cannot generate tree without a specification!"
                             << std::endl; 
          return 1; 
        }

        logging::Info() << "Fuzzing sequence from " 
                  << passes_.at(start_index_ - 1)->name() 
                  << " to "
                  << passes_.at(end_index_ - 1)->name() 
                  << std::endl
                  << "============" << std::endl;
        auto retry_seed = start_seed_ + seed_count_;
        size_t retries = 0;
        std::set<size_t> ast_hashes;

        for (size_t seed = start_seed_; 
             seed < start_seed_ + seed_count_;
             seed++)
        {
          auto actual_seed = seed;
          std::vector<size_t> sequence_rewrites; //Number of changes made by every pass in the sequence
          bool seq_ok = true;   //False if no WF-errors occured 
          bool errored = false; //True if Error nodes were added to the tree 

          // Generate initial ast  
          auto ast = gen_wf.gen(generators_, actual_seed, max_depth_);
          size_t hash = ast->hash();
          while (ast_hashes.find(hash) != ast_hashes.end() && retries < max_retries_) {
            actual_seed = retry_seed;
            ast = gen_wf.gen(generators_, actual_seed, max_depth_);
            hash = ast->hash();
            retry_seed++;
            retries++;
          }

          ast_hashes.insert(hash);  

          for (auto i = start_index_; i <= end_index_; i++)
          { 
            auto& pass = passes_.at(i - 1);
            auto& wf = pass->wf();
            auto& prev = i > 1 
                ? passes_.at(i - 2)->wf() 
                : *input_wf_;

              if (!prev || !wf)
              {
                logging::Info() << "Skipping pass: " << pass->name() << std::endl;
                continue;
              }
        
              context.push_back(prev);
              context.push_back(wf);
              
              auto ast_copy = ast->clone(); //Save clone before running pass 
            
              auto [new_ast, count, changes] = pass->run(ast);
              ast = new_ast;
              sequence_rewrites.push_back(changes); 

              logging::Trace() << "============" << std::endl
                           << "applying pass " << pass->name() 
                           << std::endl
                           << ast_copy 
                           << "------------" << std::endl
                           << new_ast << "------------" << std::endl;

              // TODO: Why are we building the symbol tables here?
              auto ok = wf.build_st(new_ast);
              if (ok)
              {
                Nodes errors;
                new_ast->get_errors(errors);
                if (!errors.empty()) {
                  Node error = errors.front();
                  errored = true; 
                  failed_ast_sizes.push_back(ast->tree_size());
                  failed_ast_heights.push_back(ast->tree_height());
                  for (auto& c : *error) {
                    if(c->type() == ErrorMsg) {
                      auto err_msg = std::string(c->location().view());
                      error_passes[pass->name()][err_msg]++; 
                      break;
                    }
                  }
                  break; // No need to run subsequent passes if Error is found
                }
              }
              ok = wf.check(new_ast) && ok; 
              // If not well-formed 
              if (!ok)
              {
                logging::Error err;
                if (!logging::Trace::active())
                {
                  // We haven't printed what failed with Trace earlier, so do it
                  // now.
                  err << "============" << std::endl
                      << "------------" << std::endl
                      << ast_copy << "------------" << std::endl
                      << "resulted in ill-formed tree: " << std::endl
                      << new_ast << "------------" << std::endl;
                }
                seq_ok = false;
                wf_errors++;
                ret = 1;

                if (failfast_)
                  return ret;
              }

              context.pop_front();
              context.pop_front();

          } //End sequence loop 
          rewrites.push_back(sum(sequence_rewrites)); //Keep track of rewrites

          if (seq_ok && !errored) {
            logging::Trace() << "============" << std::endl
                             << "Full sequence passed with tree of size: " 
                             << ast->tree_size() << std::endl 
                             << "and height: " << ast->tree_height() << std::endl
                             << ast << "------------" << std::endl;
            
            passed_ast_sizes.push_back(ast->tree_size());
            passed_ast_heights.push_back(ast->tree_height());
            
          } 
          // TODO: Arbitrary definition of trivial
          if (seq_ok && avg(sequence_rewrites) < 1) 
          {
            trivial_count++;
          }
          
        } //End generation loop 

        // Log stats 
        size_t passed_count = passed_ast_heights.size();
        size_t failed_count = failed_ast_heights.size();
        logging::Info info;
        if (wf_errors > 0) info << " not WF " << wf_errors << " times." << std::endl;

        if (!error_passes.empty())
        {
          for (size_t i = 0; i < start_index_-1; i++){ 
            info << " pass " << passes_.at(i)->name() << " not run." << std::endl;
          }
          for (size_t i = start_index_; i <= end_index_; i++){
            auto pass = passes_.at(i-1);
            auto pass_errors = error_passes.find(pass->name());
            if(pass_errors == error_passes.end())
            { 
              info << " pass " << pass->name() << " : no failures." << std::endl;
            }
            else
            {
              ErrCount err_msgs = pass_errors->second;
              const std::size_t sum = std::accumulate(
              std::begin(err_msgs),
              std::end(err_msgs),
              0,
              [](const std::size_t acc, const std::pair<const std::string, std::size_t>& c)
              { return acc + c.second; });
              info << " pass " << pass->name() << " resulted in error : " << sum << " times." << std::endl; 
              for (auto [msg,count] : err_msgs)
              {
                info << "    " << msg << ": " << count << std::endl;
              }
            }
          }
        }
        if ((!error_passes.empty() && passed_count > 0) || trivial_count > 0)
        {
          info << std::endl;
          info << " failed to run full sequence: " << failed_count << " times." << std::endl;
          info << " passed full sequence: " << passed_count << " times." << std::endl;
          if (trivial_count > 0) info << " trees with < 1 change per pass on average: " << trivial_count << std::endl;
          info << " average rewrites per pass: " << avg(rewrites) << std::endl;
        }
        size_t hash_unique = ast_hashes.size();
        info << "  " << ast_hashes.size() << " hash unique "
             << (hash_unique == 1? "tree": "trees")
             << " (" << retries << (retries == 1? " retry": " retries") << ")." << std::endl;
        info << std::endl;
        info << " failed runs: " << std::endl
             << "   average tree size: " << avg(failed_ast_sizes) << std::endl
             << "   average tree height: " << avg(failed_ast_heights) << std::endl
             << "   max tree size: " << max(failed_ast_sizes) << std::endl
             << "   max tree height: " << max(failed_ast_heights) << std::endl;
        info << " passed runs: " << std::endl
             << "   average tree size: " << avg(passed_ast_sizes) << std::endl
             << "   average tree height: " << avg(passed_ast_heights) << std::endl
             << "   max tree size: " << max(passed_ast_sizes) << std::endl
             << "   max tree height: " << avg(passed_ast_heights) << std::endl;
        return ret;
      }
  };
}

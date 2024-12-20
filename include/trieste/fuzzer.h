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

          ast_hashes.insert(new_ast->hash());

          // TODO: Why are we building the symbol tables here?
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
             << (hash_unique > 1? "trees": "tree") << "." << std::endl;

        context.pop_front();
        context.pop_front();
      }

      return ret;
    }


    int test_sequence()
      { 
        WFContext context;
        int ret = 0;
        size_t passed_count = 0;
        size_t failed_count = 0;
        size_t trivial_count = 0;
        // Passes that resulted in error and their count of different error msgs
        std::map<std::string, std::map<std::string,size_t>> error_passes;

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

        for (size_t seed = start_seed_; 
             seed < start_seed_ + seed_count_;
             seed++)
        {               
          bool changed = false; //True if at least one passed run changed the tree
          bool seq_ok = true;   //False if no WF-errors occured 
          bool errored = false; //True if Error nodes were added to the tree 

          // Generate initial ast  
          auto ast = gen_wf.gen(generators_, seed, max_depth_);

          for (auto i = start_index_; i < end_index_; i++)
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

              if (changes > 0) changed = true;

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
                ret = 1;

                if (failfast_)
                  return ret;
              }

              context.pop_front();
              context.pop_front();

          } //End sequence loop 

          if (seq_ok && !errored) passed_count++;
          if (!seq_ok) failed_count++;
          // TODO: Need different criteria for trivial 
          // e.g. (Top File) --> (Top Calculation) is a change but is not very 
          // interesting 
          if (seq_ok && !changed) trivial_count++;
          
        } //End tree generating loop 

        // Log stats 
        logging::Info info;
        if (failed_count > 0) info << "  not WF " << failed_count << " times." << std::endl;

        if (!error_passes.empty())
        {
          for (auto [pass_name, err_msgs] : error_passes) 
          {
            const std::size_t sum = std::accumulate(
              std::begin(err_msgs),
              std::end(err_msgs),
              static_cast<std::size_t>(0),
              [](const std::size_t acc, const std::pair<const std::string, std::size_t>& c)
              { return acc + c.second; });
            info << "pass " << pass_name << " resulted in error : " << sum << " times." << std::endl; 
            for (auto [msg,count] : err_msgs)
            {
              info << "    " << msg << ": " << count << std::endl;
            }
          }
        }
        if ((!error_passes.empty() && passed_count > 0) || trivial_count > 0)
        {
          info << "passed sequence: " << passed_count << " times." << std::endl;
          if (trivial_count > 0) info << "trivial: " << trivial_count << std::endl;
        }
        return ret;
      }
  };
}

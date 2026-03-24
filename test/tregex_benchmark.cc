// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <trieste/regex.h>
#include <trieste/source.h>
#include <vector>

namespace
{
  using trieste::Source;
  using trieste::SourceDef;
  using trieste::TRegex;
  using trieste::TRegexIterator;
  using trieste::TRegexMatch;

  struct BenchmarkCase
  {
    std::string name;
    int iterations;
  };

  constexpr int DefaultWarmupIters = 40;
  constexpr int QuickWarmupIters = 10;

  double median(std::vector<double> values)
  {
    if (values.empty())
      return 0.0;

    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if ((values.size() % 2) == 1)
      return values[mid];
    return (values[mid - 1] + values[mid]) / 2.0;
  }

  bool parse_int_arg(const std::string& value, int min_value, int& out)
  {
    try
    {
      const int parsed = std::stoi(value);
      if (parsed < min_value)
        return false;
      out = parsed;
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  size_t run_parser_like_once(volatile uint64_t& sink)
  {
    static const std::vector<TRegex> rules = {
      TRegex("[[:blank:]]+"),
      TRegex("[_[:alpha:]][_[:alnum:]]*"),
      TRegex("[[:digit:]]+"),
      TRegex("==|!=|<=|>="),
      TRegex("[=+\\-*/(),;{}]")};

    static const std::string input = []() {
      std::string s;
      s.reserve(8192);
      for (int i = 0; i < 300; i++)
      {
        s += "let value_";
        s += std::to_string(i);
        s += " = value_";
        s += std::to_string(i % 11);
        s += " + 42;\\n";
      }
      return s;
    }();

    Source src = SourceDef::synthetic(input, "bench");
    TRegexIterator it(src);
    TRegexMatch m(4);

    size_t tokens = 0;
    while (!it.empty())
    {
      bool matched = false;
      for (const auto& rule : rules)
      {
        if (it.consume(rule, m))
        {
          matched = true;
          tokens++;
          break;
        }
      }

      if (!matched)
        it.skip();
    }

    sink += static_cast<uint64_t>(tokens);
    return tokens;
  }

  size_t run_fullmatch_once(volatile uint64_t& sink)
  {
    static const TRegex re(
      "([_[:alpha:]][_[:alnum:]]*)([[:blank:]]+)([[:digit:]]+)");
    static const std::string input = "identifier_123    987654";

    size_t matches = 0;
    for (int i = 0; i < 1500; i++)
    {
      if (TRegex::FullMatch(input, re))
        matches++;
    }

    sink += static_cast<uint64_t>(matches);
    return matches;
  }

  size_t run_fullmatch_capture_once(volatile uint64_t& sink)
  {
    static const TRegex re(
      "([_[:alpha:]][_[:alnum:]]*)([[:blank:]]+)([[:digit:]]+)");
    static const std::string input = "identifier_123    987654";

    std::string ident;
    std::string ws;
    int number = 0;
    size_t matches = 0;

    for (int i = 0; i < 1500; i++)
    {
      if (TRegex::FullMatch(input, re, &ident, &ws, &number))
        matches++;
    }

    sink += static_cast<uint64_t>(matches);
    sink += static_cast<uint64_t>(number);
    return matches;
  }

  size_t run_partialmatch_nocapture_once(volatile uint64_t& sink)
  {
    static const TRegex re("[[:digit:]]+");
    static const std::string input = []() {
      std::string s;
      s.reserve(4096);
      for (int i = 0; i < 200; i++)
      {
        s += "some_text_";
        if (i % 7 == 0)
          s += std::to_string(i * 13);
        s += " ";
      }
      return s;
    }();

    size_t matches = 0;
    for (int i = 0; i < 400; i++)
    {
      if (TRegex::PartialMatch(input, re))
        matches++;
    }

    sink += static_cast<uint64_t>(matches);
    return matches;
  }

  size_t run_partialmatch_capture_once(volatile uint64_t& sink)
  {
    static const TRegex re("([[:alpha:]]+):([[:digit:]]+)");
    static const std::string input =
      "connect to host:8080 and proxy:3128 for service";

    std::string host;
    int port = 0;
    size_t matches = 0;

    for (int i = 0; i < 1200; i++)
    {
      if (TRegex::PartialMatch(input, re, &host, &port))
        matches++;
    }

    sink += static_cast<uint64_t>(matches);
    sink += static_cast<uint64_t>(port);
    return matches;
  }

  size_t run_globalreplace_once(volatile uint64_t& sink)
  {
    static const std::string base = []() {
      std::string s;
      s.reserve(8192);
      for (int i = 0; i < 220; i++)
      {
        s += "token";
        s += std::to_string(i % 20);
        s += "-v";
        s += std::to_string(i);
        s += " ";
      }
      return s;
    }();

    std::string text = base;
    int replaced =
      TRegex::GlobalReplace(&text, "token([[:digit:]]+)", "id_\\1");
    sink += static_cast<uint64_t>(replaced);
    sink += static_cast<uint64_t>(text.size());
    return static_cast<size_t>(replaced);
  }

  double run_case_once(const BenchmarkCase& tc, volatile uint64_t& sink)
  {
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < tc.iterations; i++)
    {
      if (tc.name == "parser_like")
        (void)run_parser_like_once(sink);
      else if (tc.name == "fullmatch")
        (void)run_fullmatch_once(sink);
      else if (tc.name == "fullmatch_capture")
        (void)run_fullmatch_capture_once(sink);
      else if (tc.name == "partialmatch_nocapture")
        (void)run_partialmatch_nocapture_once(sink);
      else if (tc.name == "partialmatch_capture")
        (void)run_partialmatch_capture_once(sink);
      else if (tc.name == "global_replace")
        (void)run_globalreplace_once(sink);
    }
    auto end = std::chrono::steady_clock::now();

    const auto elapsed_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(elapsed_ns) / static_cast<double>(tc.iterations);
  }

  const BenchmarkCase*
  find_case(const std::vector<BenchmarkCase>& cases, std::string_view name)
  {
    for (const auto& tc : cases)
    {
      if (tc.name == name)
        return &tc;
    }
    return nullptr;
  }
}

int main(int argc, char** argv)
{
  std::vector<BenchmarkCase> cases = {
    {"parser_like", 150},
    {"fullmatch", 600},
    {"fullmatch_capture", 500},
    {"partialmatch_nocapture", 400},
    {"partialmatch_capture", 500},
    {"global_replace", 280}};

  std::string focus_case;
  int focus_repeats = 9;
  int warmup_iters = DefaultWarmupIters;
  int iteration_scale_percent = 100;
  bool quick_mode = false;

  for (int i = 1; i < argc; i++)
  {
    const std::string arg = argv[i];
    if (arg.rfind("--focus-case=", 0) == 0)
    {
      focus_case = arg.substr(std::string("--focus-case=").size());
    }
    else if (arg.rfind("--focus-repeats=", 0) == 0)
    {
      const std::string value =
        arg.substr(std::string("--focus-repeats=").size());
      if (!parse_int_arg(value, 1, focus_repeats))
      {
        std::cerr << "ERROR: --focus-repeats must be an integer >= 1"
                  << std::endl;
        return 2;
      }
    }
    else if (arg == "--help")
    {
      std::cout << "Usage: trieste_tregex_benchmark [--focus-case=<name>] "
                   "[--focus-repeats=<n>]"
                << " [--warmup=<n>] [--iteration-scale=<percent>] [--quick]"
                << std::endl;
      return 0;
    }
    else if (arg == "--quick")
    {
      quick_mode = true;
      warmup_iters = QuickWarmupIters;
      focus_repeats = 3;
      iteration_scale_percent = 35;
    }
    else if (arg.rfind("--warmup=", 0) == 0)
    {
      const std::string value = arg.substr(std::string("--warmup=").size());
      if (!parse_int_arg(value, 0, warmup_iters))
      {
        std::cerr << "ERROR: --warmup must be an integer >= 0" << std::endl;
        return 2;
      }
    }
    else if (arg.rfind("--iteration-scale=", 0) == 0)
    {
      const std::string value =
        arg.substr(std::string("--iteration-scale=").size());
      if (!parse_int_arg(value, 1, iteration_scale_percent))
      {
        std::cerr << "ERROR: --iteration-scale must be an integer >= 1"
                  << std::endl;
        return 2;
      }
    }
    else
    {
      std::cerr << "ERROR: unknown argument: " << arg << std::endl;
      return 2;
    }
  }

  if (quick_mode && focus_case.empty())
    focus_repeats = 1;

  for (auto& tc : cases)
  {
    tc.iterations =
      std::max(1, (tc.iterations * iteration_scale_percent + 99) / 100);
  }

  volatile uint64_t sink = 0;

  if (!focus_case.empty())
  {
    const BenchmarkCase* tc = find_case(cases, focus_case);
    if (tc == nullptr)
    {
      std::cerr << "ERROR: unknown focus case: " << focus_case << std::endl;
      return 2;
    }

    // Warm cache paths before measurement.
    for (int i = 0; i < warmup_iters; i++)
      (void)run_case_once(*tc, sink);

    std::vector<double> ns_values;
    ns_values.reserve(static_cast<size_t>(focus_repeats));

    for (int i = 0; i < focus_repeats; i++)
      ns_values.push_back(run_case_once(*tc, sink));

    const double med = median(ns_values);

    std::cout << "BENCH focus case=" << tc->name << " repeats=" << focus_repeats
              << " iterations=" << tc->iterations << " warmup=" << warmup_iters
              << " iteration_scale=" << iteration_scale_percent
              << " quick=" << (quick_mode ? 1 : 0) << std::endl;

    std::cout << std::fixed << std::setprecision(1)
              << "BENCH focus_median tregex_ns=" << med << " sink=" << sink
              << std::endl;

    std::cout << std::fixed << std::setprecision(1) << "BENCH focus_values ";
    for (size_t i = 0; i < ns_values.size(); i++)
    {
      if (i > 0)
        std::cout << ",";
      std::cout << ns_values[i];
    }
    std::cout << std::endl;
    return 0;
  }

  std::cout << "BENCH cases=" << cases.size() << " warmup=" << warmup_iters
            << " iteration_scale=" << iteration_scale_percent
            << " quick=" << (quick_mode ? 1 : 0) << std::endl;

  for (const auto& tc : cases)
  {
    for (int i = 0; i < warmup_iters; i++)
      (void)run_case_once(tc, sink);

    const double ns = run_case_once(tc, sink);
    std::cout << std::fixed << std::setprecision(1) << "BENCH case=" << tc.name
              << " tregex_ns=" << ns << " iterations=" << tc.iterations
              << std::endl;
  }

  std::cout << "BENCH sink=" << sink << std::endl;
  return 0;
}

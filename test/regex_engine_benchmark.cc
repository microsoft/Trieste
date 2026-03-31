// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <iostream>

#if __has_include(<re2/re2.h>) && __has_include(<trieste/regex_engine.h>)
#  include <algorithm>
#  include <chrono>
#  include <cstdint>
#  include <iomanip>
#  include <re2/re2.h>
#  include <string>
#  include <string_view>
#  include <trieste/regex_engine.h>
#  include <vector>
#  define TRIESTE_REGEX_BENCHMARK_HAS_DEPS 1
#else
#  define TRIESTE_REGEX_BENCHMARK_HAS_DEPS 0
#endif

#if TRIESTE_REGEX_BENCHMARK_HAS_DEPS

namespace
{
  using trieste::regex::RegexEngine;

  struct BenchmarkCase
  {
    std::string name;
    std::string pattern;
    std::string input;
    bool use_captures = false;
  };

  constexpr size_t MaxPatternBytes = 256;
  constexpr size_t MaxInputBytes = 2048;
  constexpr int WarmupIters = 5000;
  constexpr int CompileMatchIters = 30000;
  constexpr int MatchOnlyIters = 300000;

  struct CaseResult
  {
    double trieste_compile_match_ns = 0.0;
    double re2_compile_match_ns = 0.0;
    double trieste_match_only_ns = 0.0;
    double re2_match_only_ns = 0.0;
    size_t trieste_rune_steps = 0;
    double trieste_avg_active_states = 0.0;
    size_t trieste_max_active_states = 0;
    size_t trieste_class_ref_checks = 0;
    size_t trieste_literal_checks = 0;
  };

  struct SyntaxModeSanityResult
  {
    bool ok = true;
    double extended_compile_ns = 0.0;
    double strict_compile_ns = 0.0;
    bool match_parity = true;
    bool prefix_parity = true;
  };

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

  bool validate_case_sizes(const BenchmarkCase& tc)
  {
    if (tc.pattern.size() > MaxPatternBytes)
    {
      std::cerr << "ERROR: pattern too large in case " << tc.name << std::endl;
      return false;
    }

    if (tc.input.size() > MaxInputBytes)
    {
      std::cerr << "ERROR: input too large in case " << tc.name << std::endl;
      return false;
    }

    return true;
  }

  bool validate_semantics(const BenchmarkCase& tc)
  {
    RegexEngine trieste_re(tc.pattern);
    if (!trieste_re.ok())
    {
      std::cerr << "ERROR: Trieste malformed benchmark pattern in case "
                << tc.name << std::endl;
      return false;
    }

    re2::RE2 re2_re(tc.pattern);
    if (!re2_re.ok())
    {
      std::cerr << "ERROR: RE2 malformed benchmark pattern in case " << tc.name
                << std::endl;
      return false;
    }

    bool trieste_match = false;
    bool re2_match = false;
    if (tc.use_captures)
    {
      std::vector<RegexEngine::Capture> tri_caps;
      trieste_match =
        trieste_re.find_prefix(tc.input, tri_caps) == tc.input.size();

      const int capture_count = re2_re.NumberOfCapturingGroups();
      std::vector<std::string> re2_caps(static_cast<size_t>(capture_count));
      std::vector<re2::RE2::Arg> re2_args;
      std::vector<re2::RE2::Arg*> re2_arg_ptrs;
      re2_args.reserve(static_cast<size_t>(capture_count));
      re2_arg_ptrs.reserve(static_cast<size_t>(capture_count));
      for (int i = 0; i < capture_count; i++)
      {
        re2_args.emplace_back(&re2_caps[static_cast<size_t>(i)]);
        re2_arg_ptrs.push_back(&re2_args.back());
      }
      re2_match = re2::RE2::FullMatchN(
        tc.input, re2_re, re2_arg_ptrs.data(), capture_count);

      if (capture_count != static_cast<int>(trieste_re.num_captures()))
      {
        std::cerr << "ERROR: capture-count mismatch in case " << tc.name
                  << " trieste=" << trieste_re.num_captures()
                  << " re2=" << capture_count << std::endl;
        return false;
      }
    }
    else
    {
      trieste_match = trieste_re.match(tc.input);
      re2_match = re2::RE2::FullMatch(tc.input, re2_re);
    }
    if (trieste_match != re2_match)
    {
      std::cerr << "ERROR: semantic mismatch in case " << tc.name
                << " trieste=" << trieste_match << " re2=" << re2_match
                << std::endl;
      return false;
    }

    return true;
  }

  void run_warmup(
    const BenchmarkCase& tc, volatile uint64_t& sink, bool include_compile)
  {
    if (include_compile)
    {
      RegexEngine::MatchContext tri_ctx;
      for (int i = 0; i < WarmupIters; i++)
      {
        RegexEngine trieste_re(tc.pattern);
        if (trieste_re.ok())
        {
          if (tc.use_captures)
          {
            std::vector<RegexEngine::Capture> caps;
            sink += static_cast<uint64_t>(
              trieste_re.find_prefix(tc.input, caps, tri_ctx) ==
              tc.input.size());
          }
          else
          {
            sink += static_cast<uint64_t>(trieste_re.match(tc.input, tri_ctx));
          }
        }

        re2::RE2 re2_re(tc.pattern);
        if (re2_re.ok())
        {
          if (tc.use_captures)
          {
            const int capture_count = re2_re.NumberOfCapturingGroups();
            std::vector<std::string> re2_caps(
              static_cast<size_t>(capture_count));
            std::vector<re2::RE2::Arg> re2_args;
            std::vector<re2::RE2::Arg*> re2_arg_ptrs;
            re2_args.reserve(static_cast<size_t>(capture_count));
            re2_arg_ptrs.reserve(static_cast<size_t>(capture_count));
            for (int j = 0; j < capture_count; j++)
            {
              re2_args.emplace_back(&re2_caps[static_cast<size_t>(j)]);
              re2_arg_ptrs.push_back(&re2_args.back());
            }
            sink += static_cast<uint64_t>(re2::RE2::FullMatchN(
              tc.input, re2_re, re2_arg_ptrs.data(), capture_count));
          }
          else
          {
            sink +=
              static_cast<uint64_t>(re2::RE2::FullMatch(tc.input, re2_re));
          }
        }
      }
      return;
    }

    RegexEngine trieste_re(tc.pattern);
    RegexEngine::MatchContext tri_ctx;
    re2::RE2 re2_re(tc.pattern);
    for (int i = 0; i < WarmupIters; i++)
    {
      if (tc.use_captures)
      {
        std::vector<RegexEngine::Capture> tri_caps;
        sink += static_cast<uint64_t>(
          trieste_re.find_prefix(tc.input, tri_caps, tri_ctx) ==
          tc.input.size());

        const int capture_count = re2_re.NumberOfCapturingGroups();
        std::vector<std::string> re2_caps(static_cast<size_t>(capture_count));
        std::vector<re2::RE2::Arg> re2_args;
        std::vector<re2::RE2::Arg*> re2_arg_ptrs;
        re2_args.reserve(static_cast<size_t>(capture_count));
        re2_arg_ptrs.reserve(static_cast<size_t>(capture_count));
        for (int j = 0; j < capture_count; j++)
        {
          re2_args.emplace_back(&re2_caps[static_cast<size_t>(j)]);
          re2_arg_ptrs.push_back(&re2_args.back());
        }
        sink += static_cast<uint64_t>(re2::RE2::FullMatchN(
          tc.input, re2_re, re2_arg_ptrs.data(), capture_count));
      }
      else
      {
        sink += static_cast<uint64_t>(trieste_re.match(tc.input, tri_ctx));
        sink += static_cast<uint64_t>(re2::RE2::FullMatch(tc.input, re2_re));
      }
    }
  }

  CaseResult run_case(const BenchmarkCase& tc, volatile uint64_t& sink)
  {
    CaseResult result;

    // Collect one representative instrumentation snapshot.
    {
      RegexEngine trieste_re(tc.pattern);
      RegexEngine::MatchContext ctx;
      bool ok = false;
      if (tc.use_captures)
      {
        std::vector<RegexEngine::Capture> caps;
        ok =
          trieste_re.find_prefix(tc.input, caps, ctx, true) == tc.input.size();
      }
      else
      {
        ok = trieste_re.match(tc.input, ctx);
      }
      RegexEngine::MatchStats stats = ctx.stats();
      sink += static_cast<uint64_t>(ok);
      result.trieste_rune_steps = stats.rune_steps;
      result.trieste_avg_active_states = (stats.rune_steps > 0) ?
        static_cast<double>(stats.active_states_total) /
          static_cast<double>(stats.rune_steps) :
        0.0;
      result.trieste_max_active_states = stats.max_active_states;
      result.trieste_class_ref_checks = stats.class_ref_checks;
      result.trieste_literal_checks = stats.literal_checks;
    }

    run_warmup(tc, sink, true);

    {
      RegexEngine::MatchContext tri_ctx;
      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < CompileMatchIters; i++)
      {
        RegexEngine trieste_re(tc.pattern);
        if (trieste_re.ok())
        {
          if (tc.use_captures)
          {
            std::vector<RegexEngine::Capture> caps;
            sink += static_cast<uint64_t>(
              trieste_re.find_prefix(tc.input, caps, tri_ctx) ==
              tc.input.size());
          }
          else
          {
            sink += static_cast<uint64_t>(trieste_re.match(tc.input, tri_ctx));
          }
        }
      }
      auto end = std::chrono::steady_clock::now();
      auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
      result.trieste_compile_match_ns =
        static_cast<double>(ns) / CompileMatchIters;
    }

    {
      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < CompileMatchIters; i++)
      {
        re2::RE2 re2_re(tc.pattern);
        if (re2_re.ok())
        {
          if (tc.use_captures)
          {
            const int capture_count = re2_re.NumberOfCapturingGroups();
            std::vector<std::string> re2_caps(
              static_cast<size_t>(capture_count));
            std::vector<re2::RE2::Arg> re2_args;
            std::vector<re2::RE2::Arg*> re2_arg_ptrs;
            re2_args.reserve(static_cast<size_t>(capture_count));
            re2_arg_ptrs.reserve(static_cast<size_t>(capture_count));
            for (int j = 0; j < capture_count; j++)
            {
              re2_args.emplace_back(&re2_caps[static_cast<size_t>(j)]);
              re2_arg_ptrs.push_back(&re2_args.back());
            }
            sink += static_cast<uint64_t>(re2::RE2::FullMatchN(
              tc.input, re2_re, re2_arg_ptrs.data(), capture_count));
          }
          else
          {
            sink +=
              static_cast<uint64_t>(re2::RE2::FullMatch(tc.input, re2_re));
          }
        }
      }
      auto end = std::chrono::steady_clock::now();
      auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
      result.re2_compile_match_ns = static_cast<double>(ns) / CompileMatchIters;
    }

    run_warmup(tc, sink, false);

    {
      RegexEngine trieste_re(tc.pattern);
      RegexEngine::MatchContext tri_ctx;
      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < MatchOnlyIters; i++)
      {
        if (tc.use_captures)
        {
          std::vector<RegexEngine::Capture> caps;
          sink += static_cast<uint64_t>(
            trieste_re.find_prefix(tc.input, caps, tri_ctx) == tc.input.size());
        }
        else
        {
          sink += static_cast<uint64_t>(trieste_re.match(tc.input, tri_ctx));
        }
      }
      auto end = std::chrono::steady_clock::now();
      auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
      result.trieste_match_only_ns = static_cast<double>(ns) / MatchOnlyIters;
    }

    {
      re2::RE2 re2_re(tc.pattern);
      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < MatchOnlyIters; i++)
      {
        if (tc.use_captures)
        {
          const int capture_count = re2_re.NumberOfCapturingGroups();
          std::vector<std::string> re2_caps(static_cast<size_t>(capture_count));
          std::vector<re2::RE2::Arg> re2_args;
          std::vector<re2::RE2::Arg*> re2_arg_ptrs;
          re2_args.reserve(static_cast<size_t>(capture_count));
          re2_arg_ptrs.reserve(static_cast<size_t>(capture_count));
          for (int j = 0; j < capture_count; j++)
          {
            re2_args.emplace_back(&re2_caps[static_cast<size_t>(j)]);
            re2_arg_ptrs.push_back(&re2_args.back());
          }
          sink += static_cast<uint64_t>(re2::RE2::FullMatchN(
            tc.input, re2_re, re2_arg_ptrs.data(), capture_count));
        }
        else
        {
          sink += static_cast<uint64_t>(re2::RE2::FullMatch(tc.input, re2_re));
        }
      }
      auto end = std::chrono::steady_clock::now();
      auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
      result.re2_match_only_ns = static_cast<double>(ns) / MatchOnlyIters;
    }

    return result;
  }

  SyntaxModeSanityResult run_syntax_mode_sanity(volatile uint64_t& sink)
  {
    SyntaxModeSanityResult out;
    constexpr std::string_view pattern = "a{2,3}$";

    for (int i = 0; i < WarmupIters; i++)
    {
      RegexEngine ext(pattern, RegexEngine::SyntaxMode::Extended);
      RegexEngine strict(pattern, RegexEngine::SyntaxMode::IregexpStrict);
      if (!ext.ok() || !strict.ok())
      {
        out.ok = false;
        return out;
      }
      sink += static_cast<uint64_t>(ext.match("aaa"));
      sink += static_cast<uint64_t>(strict.match("aaa"));
    }

    {
      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < CompileMatchIters; i++)
      {
        RegexEngine ext(pattern, RegexEngine::SyntaxMode::Extended);
        sink += static_cast<uint64_t>(ext.ok());
      }
      auto end = std::chrono::steady_clock::now();
      auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
      out.extended_compile_ns = static_cast<double>(ns) / CompileMatchIters;
    }

    {
      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < CompileMatchIters; i++)
      {
        RegexEngine strict(pattern, RegexEngine::SyntaxMode::IregexpStrict);
        sink += static_cast<uint64_t>(strict.ok());
      }
      auto end = std::chrono::steady_clock::now();
      auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
      out.strict_compile_ns = static_cast<double>(ns) / CompileMatchIters;
    }

    RegexEngine ext(pattern, RegexEngine::SyntaxMode::Extended);
    RegexEngine strict(pattern, RegexEngine::SyntaxMode::IregexpStrict);
    if (!ext.ok() || !strict.ok())
    {
      out.ok = false;
      return out;
    }

    const std::vector<std::string> probe_inputs = {
      "", "a", "aa", "aaa", "aaaa"};
    for (const auto& input : probe_inputs)
    {
      if (ext.match(input) != strict.match(input))
      {
        out.match_parity = false;
        out.ok = false;
      }

      if (ext.find_prefix(input, true) != strict.find_prefix(input, true))
      {
        out.prefix_parity = false;
        out.ok = false;
      }

      if (ext.find_prefix(input, false) != strict.find_prefix(input, false))
      {
        out.prefix_parity = false;
        out.ok = false;
      }
    }

    return out;
  }
}

int main(int argc, char** argv)
{
  const std::vector<BenchmarkCase> cases = {
    {"literal", "hello", "hello", false},
    {"alt_plus", "(foo|bar|baz)+qux", "foobarbazqux", false},
    {"identifier", "[_[:alpha:]][_[:alnum:]]*", "hello_world_123", false},
    {"float",
     "[[:digit:]]+\\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?",
     "12345.6789e+10",
     false},
    {"float_capture",
     "([[:digit:]]+)\\.([[:digit:]]+)(e([+-]?)([[:digit:]]+))?",
     "12345.6789e+10",
     true},
    {"word_boundary", "\\bword\\b", "word", false},
    {"json_number",
     R"(-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)",
     "-12.5e+6",
     false},
    {"json_string",
     R"("(?:[^"\\\x00-\x1F]+|\\["\\\/bfnrt]|\\u[[:xdigit:]]{4})*")",
     R"("name\u0041")",
     false},
    {"rego_comment_crlf", R"(#[^\r\n]*\r?\n)", "#line\r\n", false},
    {"rego_identifier",
     R"((?:[[:alpha:]]|_)(?:[[:alnum:]]|_)*\b)",
     "name_42",
     false},
    {"rego_raw_string", R"(`[^`]*`)", "`raw_value`", false},
    {"open_brace_optional_newline", R"({(?:\r?\n)?)", "{\n", false},
    {"tregex_helper_hd_capture",
     R"([[:space:]]*\([[:space:]]*([^[:space:]\(\)]*))",
     " (header",
     true},
    {"verona_hex_float",
     R"([-]?0x[_[:xdigit:]]+\.[_[:xdigit:]]+(?:p[+-][_[:digit:]]+)?\b)",
     "-0x1a.fp+7",
     false},
    {"verona_identifier_global", R"(\@[_[:alnum:]]*)", "@global_42", false},
    {"verona_string_escaped", "\"((?:\\\\\"|[^\"])*?)\"", "\"a\\\"b\"", false},
    {"verona_line_comment", R"(//[^\r\n]*)", "// comment", false},
    {"vc_raw_string_opener", "([']+)\"([^\"]*)", "''\"raw text", true},
    {"vc_symbol_id_prefixed", R"([=#][!#$%&*+-/<=>?@\^`|~]+)", "=+=>", false},
    {"vc_symbol_id_ops", R"([!#$%&*+-/<=>?@\^`|~]+)", "->>", false},
    {"vc_nested_comment_body", R"([^/\*]+)", "comment-body", false},
    {"vc_vararg_token", R"(\.\.\.)", "...", false},
  };

  std::string focus_case;
  int focus_repeats = 9;

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
      std::cout << "Usage: trieste_regex_engine_benchmark "
                   "[--focus-case=<name>] [--focus-repeats=<n>]"
                << std::endl;
      return 0;
    }
    else
    {
      std::cerr << "ERROR: unknown argument: " << arg << std::endl;
      return 2;
    }
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

    if (!validate_case_sizes(*tc) || !validate_semantics(*tc))
      return 2;

    std::vector<double> trieste_cm;
    std::vector<double> re2_cm;
    std::vector<double> trieste_m;
    std::vector<double> re2_m;
    trieste_cm.reserve(static_cast<size_t>(focus_repeats));
    re2_cm.reserve(static_cast<size_t>(focus_repeats));
    trieste_m.reserve(static_cast<size_t>(focus_repeats));
    re2_m.reserve(static_cast<size_t>(focus_repeats));

    CaseResult last_result;
    for (int i = 0; i < focus_repeats; i++)
    {
      const CaseResult r = run_case(*tc, sink);
      trieste_cm.push_back(r.trieste_compile_match_ns);
      re2_cm.push_back(r.re2_compile_match_ns);
      trieste_m.push_back(r.trieste_match_only_ns);
      re2_m.push_back(r.re2_match_only_ns);
      last_result = r;
    }

    const double med_trieste_cm = median(trieste_cm);
    const double med_re2_cm = median(re2_cm);
    const double med_trieste_m = median(trieste_m);
    const double med_re2_m = median(re2_m);

    std::cout << "BENCH focus case=" << tc->name << " repeats=" << focus_repeats
              << " compile_match_iters=" << CompileMatchIters
              << " match_only_iters=" << MatchOnlyIters
              << " warmup_iters=" << WarmupIters << std::endl;

    std::cout << std::fixed << std::setprecision(1) << "BENCH focus_median"
              << " trieste_cm_ns=" << med_trieste_cm
              << " re2_cm_ns=" << med_re2_cm
              << " trieste_m_ns=" << med_trieste_m << " re2_m_ns=" << med_re2_m
              << std::setprecision(2)
              << " cm_ratio=" << (med_trieste_cm / med_re2_cm)
              << " m_ratio=" << (med_trieste_m / med_re2_m)
              << " tri_steps=" << last_result.trieste_rune_steps
              << " tri_avg_active=" << last_result.trieste_avg_active_states
              << " tri_max_active=" << last_result.trieste_max_active_states
              << " tri_class_checks=" << last_result.trieste_class_ref_checks
              << " tri_lit_checks=" << last_result.trieste_literal_checks
              << " sink=" << sink << std::endl;

    const SyntaxModeSanityResult mode_sanity = run_syntax_mode_sanity(sink);
    if (!mode_sanity.ok)
    {
      std::cerr << "ERROR: strict-vs-extended syntax mode sanity check failed"
                << std::endl;
      return 2;
    }

    std::cout << std::fixed << std::setprecision(2) << "BENCH mode_sanity"
              << " strict_compile_ratio="
              << (mode_sanity.strict_compile_ns /
                  mode_sanity.extended_compile_ns)
              << " mode_match_parity=" << (mode_sanity.match_parity ? 1 : 0)
              << " mode_prefix_parity=" << (mode_sanity.prefix_parity ? 1 : 0)
              << " sink=" << sink << std::endl;
    return 0;
  }

  std::vector<CaseResult> results;
  results.reserve(cases.size());

  for (const auto& tc : cases)
  {
    if (!validate_case_sizes(tc) || !validate_semantics(tc))
      return 2;

    results.push_back(run_case(tc, sink));
  }

  std::cout << "BENCH iterations compile_match=" << CompileMatchIters
            << " match_only=" << MatchOnlyIters << " warmup=" << WarmupIters
            << " cases=" << cases.size() << std::endl;

  const SyntaxModeSanityResult mode_sanity = run_syntax_mode_sanity(sink);
  if (!mode_sanity.ok)
  {
    std::cerr << "ERROR: strict-vs-extended syntax mode sanity check failed"
              << std::endl;
    return 2;
  }

  double trieste_cm_sum = 0.0;
  double re2_cm_sum = 0.0;
  double trieste_m_sum = 0.0;
  double re2_m_sum = 0.0;

  for (size_t i = 0; i < cases.size(); i++)
  {
    const auto& tc = cases[i];
    const auto& r = results[i];
    trieste_cm_sum += r.trieste_compile_match_ns;
    re2_cm_sum += r.re2_compile_match_ns;
    trieste_m_sum += r.trieste_match_only_ns;
    re2_m_sum += r.re2_match_only_ns;

    std::cout << std::fixed << std::setprecision(1) << "BENCH case=" << tc.name
              << " trieste_cm_ns=" << r.trieste_compile_match_ns
              << " re2_cm_ns=" << r.re2_compile_match_ns
              << " trieste_m_ns=" << r.trieste_match_only_ns
              << " re2_m_ns=" << r.re2_match_only_ns << std::setprecision(2)
              << " cm_ratio="
              << (r.trieste_compile_match_ns / r.re2_compile_match_ns)
              << " m_ratio=" << (r.trieste_match_only_ns / r.re2_match_only_ns)
              << " tri_steps=" << r.trieste_rune_steps
              << " tri_avg_active=" << r.trieste_avg_active_states
              << " tri_max_active=" << r.trieste_max_active_states
              << " tri_class_checks=" << r.trieste_class_ref_checks
              << " tri_lit_checks=" << r.trieste_literal_checks << std::endl;
  }

  std::cout << std::fixed << std::setprecision(2)
            << "BENCH summary cm_ratio=" << (trieste_cm_sum / re2_cm_sum)
            << " m_ratio=" << (trieste_m_sum / re2_m_sum)
            << " strict_compile_ratio="
            << (mode_sanity.strict_compile_ns / mode_sanity.extended_compile_ns)
            << " mode_match_parity=" << (mode_sanity.match_parity ? 1 : 0)
            << " mode_prefix_parity=" << (mode_sanity.prefix_parity ? 1 : 0)
            << " sink=" << sink << std::endl;

  return 0;
}

#else

int main()
{
  std::cerr << "ERROR: benchmark dependencies are unavailable; configure with "
               "TRIESTE_ENABLE_TESTING=ON and TRIESTE_BUILD_REGEX_BENCHMARK=ON"
            << std::endl;
  return 2;
}

#endif

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "logging.h"
#include "regex_engine.h"

#include <array>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace trieste
{
  // Compiled regular expression for pattern matching.
  // Wraps RegexEngine with a high-level API providing FullMatch (entire
  // string), PartialMatch (substring), and GlobalReplace operations.
  // Regex objects are immutable after construction and safe to share
  // across threads.  Matching uses a thread-local MatchContext.
  //
  // Usage:
  //   TRegex re("(\\d+)-(\\d+)");
  //   std::string a, b;
  //   TRegex::FullMatch("12-34", re, &a, &b);
  class TRegex
  {
    friend class TRegexMatch;

  public:
    TRegex() : pattern_(), num_captures_(0)
    {
      init();
    }

    explicit TRegex(std::string_view pattern) : pattern_(pattern)
    {
      init();
    }

    explicit TRegex(const char* pattern)
    : pattern_(pattern == nullptr ? "" : pattern)
    {
      init();
    }

    TRegex(const TRegex&) = default;
    TRegex(TRegex&&) = default;
    TRegex& operator=(const TRegex&) = default;
    TRegex& operator=(TRegex&&) = default;

    bool ok() const
    {
      return engine_ != nullptr && engine_->ok();
    }

    regex::ErrorCode error_code() const
    {
      if (engine_ == nullptr)
        return regex::ErrorCode::ErrorInternalError;
      return engine_->error_code();
    }

    const char* error() const
    {
      if (engine_ == nullptr)
        return regex::error_code_string(regex::ErrorCode::ErrorInternalError);
      return engine_->error();
    }

    int NumberOfCapturingGroups() const
    {
      return static_cast<int>(num_captures_);
    }

    const std::string& pattern() const
    {
      return pattern_;
    }

    regex::RegexEngine::FirstCharInfo first_char_info() const
    {
      if (engine_ == nullptr || !engine_->ok())
        return regex::RegexEngine::FirstCharInfo::maximal();
      return engine_->first_char_info();
    }

    // Type-erased wrapper for output parameter pointers.
    // Supports: string, string_view, all integral types (except bool),
    // character types (char, signed char, unsigned char), float, and double.
    class Arg
    {
    public:
      Arg() : Arg(nullptr) {}
      Arg(std::nullptr_t)
      : arg_(nullptr), parser_(DoNothing), defaulter_(DefaultNothing)
      {}

      Arg(std::string* p)
      : arg_(p), parser_(ParseString), defaulter_(DefaultString)
      {}
      Arg(std::string_view* p)
      : arg_(p), parser_(ParseStringView), defaulter_(DefaultStringView)
      {}

      Arg(char* p)
      : arg_(p), parser_(ParseChar<char>), defaulter_(DefaultZero<char>)
      {}
      Arg(signed char* p)
      : arg_(p),
        parser_(ParseChar<signed char>),
        defaulter_(DefaultZero<signed char>)
      {}
      Arg(unsigned char* p)
      : arg_(p),
        parser_(ParseChar<unsigned char>),
        defaulter_(DefaultZero<unsigned char>)
      {}

      Arg(float* p)
      : arg_(p), parser_(ParseFloat<float>), defaulter_(DefaultZero<float>)
      {}
      Arg(double* p)
      : arg_(p), parser_(ParseFloat<double>), defaulter_(DefaultZero<double>)
      {}

      template<
        typename T,
        std::enable_if_t<
          std::is_integral_v<T> && !std::is_same_v<T, char> &&
            !std::is_same_v<T, signed char> &&
            !std::is_same_v<T, unsigned char> && !std::is_same_v<T, bool>,
          int> = 0>
      Arg(T* p) : arg_(p), parser_(ParseIntegral<T>), defaulter_(DefaultZero<T>)
      {}

      bool Parse(const char* str, size_t n) const
      {
        return parser_(str, n, arg_);
      }

      // Reset the output argument to its type's default value.
      // Used for unmatched capture groups.
      void SetDefault() const
      {
        defaulter_(arg_);
      }

    private:
      using Parser = bool (*)(const char*, size_t, void*);
      using Defaulter = void (*)(void*);

      static bool DoNothing(const char*, size_t, void*)
      {
        return true;
      }

      static void DefaultNothing(void*) {}

      static void DefaultString(void* dest)
      {
        if (dest)
          reinterpret_cast<std::string*>(dest)->clear();
      }

      static void DefaultStringView(void* dest)
      {
        if (dest)
          *reinterpret_cast<std::string_view*>(dest) = std::string_view();
      }

      template<typename T>
      static void DefaultZero(void* dest)
      {
        if (dest)
          *reinterpret_cast<T*>(dest) = T();
      }

      static bool ParseString(const char* str, size_t n, void* dest)
      {
        if (dest == nullptr)
          return true;
        reinterpret_cast<std::string*>(dest)->assign(str, n);
        return true;
      }

      // Output string_view points into the original input text.
      // Valid only while the input is alive.
      static bool ParseStringView(const char* str, size_t n, void* dest)
      {
        if (dest == nullptr)
          return true;
        *reinterpret_cast<std::string_view*>(dest) = std::string_view(str, n);
        return true;
      }

      template<typename T>
      static bool ParseChar(const char* str, size_t n, void* dest)
      {
        if (dest == nullptr)
          return true;
        if (n != 1)
          return false;
        *reinterpret_cast<T*>(dest) = static_cast<T>(str[0]);
        return true;
      }

      template<typename T>
      static bool ParseIntegral(const char* str, size_t n, void* dest)
      {
        if (dest == nullptr)
          return true;
        T value = 0;
        auto res = std::from_chars(str, str + n, value, 10);
        if (res.ec != std::errc() || res.ptr != str + n)
          return false;
        *reinterpret_cast<T*>(dest) = value;
        return true;
      }

      template<typename T>
      static bool ParseFloat(const char* str, size_t n, void* dest)
      {
        if (dest == nullptr)
          return true;
        if (n == 0 || std::isspace(static_cast<unsigned char>(str[0])))
          return false;
        // Use strtod/strtof for C++17 compatibility.
        std::string tmp(str, n);
        char* end = nullptr;
        errno = 0;
        T value;
        if constexpr (std::is_same_v<T, float>)
          value = std::strtof(tmp.c_str(), &end);
        else
          value = static_cast<T>(std::strtod(tmp.c_str(), &end));
        if (end != tmp.c_str() + tmp.size() || errno == ERANGE)
          return false;
        *reinterpret_cast<T*>(dest) = value;
        return true;
      }

      void* arg_;
      Parser parser_;
      Defaulter defaulter_;
    };

    static bool FullMatch(const std::string_view& text, const TRegex& regex)
    {
      if (regex.engine_ == nullptr || !regex.engine_->ok())
        return false;
      auto& ctx = thread_local_context();
      return regex.engine_->match(text, ctx);
    }

    // Variadic FullMatch: extracts capture groups into typed out-params.
    template<typename... A>
    static bool
    FullMatch(const std::string_view& text, const TRegex& re, A&&... a)
    {
      return Apply(FullMatchN, text, re, Arg(std::forward<A>(a))...);
    }

    // Returns true if regex matches any substring of text.
    static bool PartialMatch(const std::string_view& text, const TRegex& regex)
    {
      if (regex.engine_ == nullptr || !regex.engine_->ok())
        return false;
      return regex.engine_->search(text).found();
    }

    // Variadic PartialMatch: finds first substring match and extracts captures.
    template<typename... A>
    static bool
    PartialMatch(const std::string_view& text, const TRegex& re, A&&... a)
    {
      return Apply(PartialMatchN, text, re, Arg(std::forward<A>(a))...);
    }

    static int GlobalReplace(
      std::string* text,
      const std::string_view& pattern,
      const std::string_view& rewrite)
    {
      if (text == nullptr)
        return 0;

      TRegex regex(pattern);
      if (!regex.ok())
        return 0;

      std::string input = std::move(*text);
      std::string output;
      output.reserve(input.size());

      auto append_rewrite =
        [&](
          std::string& out,
          const std::string_view& src,
          size_t match_start,
          size_t match_len,
          const std::vector<regex::RegexEngine::Capture>& captures) {
          for (size_t i = 0; i < rewrite.size(); i++)
          {
            char ch = rewrite[i];
            if ((ch == '\\') && (i + 1 < rewrite.size()))
            {
              char next = rewrite[i + 1];
              if (std::isdigit(static_cast<unsigned char>(next)) != 0)
              {
                // Parse all consecutive digits as a decimal capture index.
                size_t idx = 0;
                size_t j = i + 1;
                while (j < rewrite.size() &&
                       std::isdigit(static_cast<unsigned char>(rewrite[j])) !=
                         0)
                {
                  size_t next_idx =
                    idx * 10 + static_cast<size_t>(rewrite[j] - '0');
                  if (next_idx > regex::MaxCaptures)
                  {
                    idx = next_idx;
                    j++;
                    break;
                  }
                  idx = next_idx;
                  j++;
                }
                if (idx == 0)
                {
                  out.append(src.substr(match_start, match_len));
                }
                else if (idx <= captures.size())
                {
                  auto cap = captures[idx - 1];
                  if (cap.matched() && cap.end >= cap.start)
                  {
                    out.append(src.substr(cap.start, cap.end - cap.start));
                  }
                }
                i = j - 1;
                continue;
              }

              out.push_back(next);
              i++;
              continue;
            }

            out.push_back(ch);
          }
        };

      size_t replacements = 0;
      size_t pos = 0;
      auto& ctx = thread_local_context();
      std::vector<regex::RegexEngine::Capture> captures;
      std::string_view input_view(input);
      while (pos <= input.size())
      {
        auto result = regex.engine_->search(input_view, captures, ctx, pos);
        if (!result.found())
        {
          output.append(input.substr(pos));
          break;
        }

        size_t match_start = result.match_start;
        size_t match_len = result.match_len;

        output.append(input.substr(pos, match_start - pos));
        append_rewrite(output, input, match_start, match_len, captures);
        replacements++;

        if (match_len == 0)
        {
          if (match_start >= input.size())
            break;

          // Advance past one full UTF-8 codepoint so we never split a
          // multi-byte sequence and always make forward progress.
          auto [r, n] = regex::decode_rune(input_view, match_start);
          output.append(input.substr(match_start, n));
          pos = match_start + n;
        }
        else
        {
          pos = match_start + match_len;
        }
      }

      *text = std::move(output);
      static_assert(
        sizeof(size_t) >= sizeof(int),
        "narrowing size_t to int in GlobalReplace return");
      if (replacements > static_cast<size_t>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
      return static_cast<int>(replacements);
    }

  private:
    static regex::RegexEngine::MatchContext& thread_local_context()
    {
      thread_local regex::RegexEngine::MatchContext ctx;
      return ctx;
    }

    // Bridges variadic Arg... to an Arg* array for FullMatchN/PartialMatchN.
    template<typename F, typename SP>
    static bool Apply(F f, SP sp, const TRegex& re)
    {
      return f(sp, re, nullptr, 0);
    }

    template<typename F, typename SP, typename... A>
    static bool Apply(F f, SP sp, const TRegex& re, const A&... a)
    {
      const Arg* const args[] = {&a...};
      return f(sp, re, args, static_cast<int>(sizeof...(a)));
    }

    static bool FullMatchN(
      const std::string_view& text,
      const TRegex& re,
      const Arg* const args[],
      int n)
    {
      if (re.engine_ == nullptr || !re.engine_->ok())
        return false;
      if (n > re.NumberOfCapturingGroups())
        return false;

      auto& ctx = thread_local_context();
      if (n == 0)
        return re.engine_->match(text, ctx);
      std::vector<regex::RegexEngine::Capture> captures;
      size_t len = re.engine_->find_prefix(text, captures, ctx, true);
      if (len != text.size())
        return false;

      int num_caps = std::min(n, static_cast<int>(captures.size()));
      for (int i = 0; i < num_caps; i++)
      {
        if (!captures[i].matched())
        {
          args[i]->SetDefault();
          continue;
        }
        const char* cap_str = text.data() + captures[i].start;
        size_t cap_len = captures[i].end - captures[i].start;
        if (!args[i]->Parse(cap_str, cap_len))
          return false;
      }
      return true;
    }

    static bool PartialMatchN(
      const std::string_view& text,
      const TRegex& re,
      const Arg* const args[],
      int n)
    {
      if (re.engine_ == nullptr || !re.engine_->ok())
        return false;
      if (n > re.NumberOfCapturingGroups())
        return false;

      std::vector<regex::RegexEngine::Capture> captures;
      auto& ctx = thread_local_context();

      auto result = re.engine_->search(text, captures, ctx);
      if (!result.found())
        return false;

      int num_caps = std::min(n, static_cast<int>(captures.size()));
      for (int i = 0; i < num_caps; i++)
      {
        if (!captures[i].matched())
        {
          args[i]->SetDefault();
          continue;
        }
        const char* cap_str = text.data() + captures[i].start;
        size_t cap_len = captures[i].end - captures[i].start;
        if (!args[i]->Parse(cap_str, cap_len))
          return false;
      }
      return true;
    }

    void init()
    {
      engine_ = std::make_shared<regex::RegexEngine>(pattern_);
      num_captures_ = engine_->num_captures();
    }

    size_t find_prefix(const std::string_view& text, bool at_start = true) const
    {
      if (engine_ == nullptr || !engine_->ok())
        return regex::RegexEngine::npos;
      auto& ctx = thread_local_context();
      size_t len = engine_->find_prefix(text, ctx, at_start);
      return len;
    }

    size_t find_prefix(
      const std::string_view& text,
      std::vector<regex::RegexEngine::Capture>& captures,
      bool at_start = true) const
    {
      auto& ctx = thread_local_context();
      return find_prefix(text, captures, ctx, at_start);
    }

    size_t find_prefix(
      const std::string_view& text,
      std::vector<regex::RegexEngine::Capture>& captures,
      regex::RegexEngine::MatchContext& ctx,
      bool at_start = true) const
    {
      if (engine_ == nullptr || !engine_->ok())
        return regex::RegexEngine::npos;
      size_t len = engine_->find_prefix(text, captures, ctx, at_start);
      return len;
    }

    std::string pattern_;
    std::shared_ptr<regex::RegexEngine> engine_;
    size_t num_captures_ = 0;
  };

  // Result of a single regex match against a Source.
  // Stores Location spans for the overall match (index 0) and each
  // capture group (indices 1..N).  Use at(i) to retrieve a Location
  // and parse<T>(i) to convert a captured span to a typed value.
  class TRegexMatch
  {
    friend class TRegexIterator;

  private:
    std::vector<Location> locations;
    size_t matches = 0;
    regex::RegexEngine::MatchContext ctx_;
    std::vector<regex::RegexEngine::Capture> captures_;

    bool match_regexp(
      const TRegex& regex,
      const std::string_view& view,
      Source& source,
      size_t offset)
    {
      matches = regex.NumberOfCapturingGroups() + 1;

      if (locations.size() < matches)
        locations.resize(matches);

      size_t matched_len = regex.find_prefix(view, captures_, ctx_);

      if (matched_len == regex::RegexEngine::npos)
      {
        return false;
      }

      locations[0] = Location(source, offset, matched_len);

      size_t capture_count =
        std::min(captures_.size(), matches > 0 ? matches - 1 : 0);
      for (size_t i = 0; i < capture_count; i++)
      {
        if (!captures_[i].matched() || (captures_[i].end < captures_[i].start))
        {
          locations[i + 1] = Location(source, offset + matched_len, 0);
          continue;
        }

        locations[i + 1] = {
          source,
          offset + captures_[i].start,
          captures_[i].end - captures_[i].start};
      }

      for (size_t i = capture_count + 1; i < matches; i++)
      {
        locations[i] = Location(source, offset, 0);
      }

      return true;
    }

  public:
    TRegexMatch(size_t max_capture = 0)
    {
      locations.resize(max_capture + 1);
    }

    bool try_match(
      const TRegex& regex,
      const std::string_view& view,
      Source& source,
      size_t offset)
    {
      return match_regexp(regex, view, source, offset);
    }

    const Location& at(size_t index = 0) const
    {
      if (index >= matches)
        return locations.at(0);

      return locations.at(index);
    }

    template<typename T>
    T parse(size_t index = 0) const
    {
      if (index >= matches)
        return T();

      auto text = at(index).view();
      if constexpr (std::is_same_v<T, std::string>)
      {
        return std::string(text);
      }
      else if constexpr (std::is_same_v<T, std::string_view>)
      {
        return text;
      }
      else if constexpr (
        std::is_same_v<T, char> || std::is_same_v<T, signed char> ||
        std::is_same_v<T, unsigned char>)
      {
        if (text.size() != 1)
          return T();
        return static_cast<T>(text[0]);
      }
      else if constexpr (std::is_integral_v<T>)
      {
        T value = 0;
        auto res =
          std::from_chars(text.data(), text.data() + text.size(), value, 10);
        if (res.ec == std::errc() && res.ptr == text.data() + text.size())
          return value;
        return T();
      }
      else if constexpr (std::is_floating_point_v<T>)
      {
        if (text.empty() || std::isspace(static_cast<unsigned char>(text[0])))
          return T();

        std::string tmp(text);
        char* end = nullptr;
        errno = 0;
        T value;
        if constexpr (std::is_same_v<T, float>)
          value = std::strtof(tmp.c_str(), &end);
        else
          value = static_cast<T>(std::strtod(tmp.c_str(), &end));

        if (end == tmp.c_str() + tmp.size() && errno != ERANGE)
          return value;

        return T();
      }
      else
      {
        return T();
      }
    }
  };

  class TRegexSet;

  // Sequential scanner that consumes prefix matches from a Source.
  // Tracks a byte position and advances it after each successful
  // consume() call.  Used by the Parse DSL to tokenise input.
  class TRegexIterator
  {
  private:
    Source source;
    size_t pos_ = 0;

  public:
    TRegexIterator(Source source_) : source(source_) {}

    bool empty()
    {
      return pos_ >= source->view().size();
    }

    bool consume(const TRegex& regex, TRegexMatch& m)
    {
      std::string_view remainder = source->view().substr(pos_);
      if (!m.match_regexp(regex, remainder, source, pos_))
        return false;

      pos_ += m.at(0).len;
      return true;
    }

    Location current() const
    {
      return {source, pos_, 1};
    }

    void skip(size_t count = 1)
    {
      auto len = source->view().size();
      pos_ = (len - pos_ < count) ? len : pos_ + count;
    }

    std::optional<int>
    consume_first_match(TRegexMatch& m, const TRegexSet& set);
  };

  // Precomputed dispatch table for multi-regex matching.  For each
  // possible first byte (0-255), stores the sorted list of regex indices
  // that could match input starting with that byte.  A separate list
  // stores indices of regexes that can match the empty string (used when
  // the input is empty).  All candidate lists are stored in a single flat
  // vector for cache efficiency; the offset table provides O(1) lookup.
  class TRegexSet
  {
  public:
    TRegexSet(const TRegex* regexes, size_t count)
    : regexes_(regexes, regexes + count)
    {
      build_dispatch_table();
    }

    explicit TRegexSet(std::initializer_list<TRegex> regexes)
    : regexes_(regexes)
    {
      build_dispatch_table();
    }

    template<typename Iter>
    TRegexSet(Iter first, Iter last) : regexes_(first, last)
    {
      build_dispatch_table();
    }

    template<typename Iter, typename Proj>
    TRegexSet(Iter first, Iter last, Proj proj)
    {
      regexes_.reserve(std::distance(first, last));
      for (auto it = first; it != last; ++it)
        regexes_.push_back(proj(*it));
      build_dispatch_table();
    }

    std::optional<int> match(
      TRegexMatch& m,
      const std::string_view& view,
      Source& source,
      size_t offset) const
    {
      if (view.empty())
      {
        if (empty_match_index_)
          m.try_match(regexes_[*empty_match_index_], view, source, offset);
        return empty_match_index_;
      }

      uint8_t first = static_cast<uint8_t>(view[0]);
      auto& candidates = regex::is_ascii(first) ? ascii_candidates_[first] :
                                                  nonascii_candidates_;

      for (auto idx : candidates)
      {
        if (m.try_match(regexes_[idx], view, source, offset))
          return static_cast<int>(idx);
      }
      return std::nullopt;
    }

    size_t size() const
    {
      return regexes_.size();
    }

    const TRegex& operator[](size_t i) const
    {
      return regexes_[i];
    }

  private:
    void build_dispatch_table()
    {
      assert(
        regexes_.size() <= std::numeric_limits<uint16_t>::max() &&
        "TRegexSet supports at most 65535 regexes");

      for (size_t i = 0; i < regexes_.size(); ++i)
      {
        auto info = regexes_[i].first_char_info();
        auto idx = static_cast<uint16_t>(i);

        if (info.can_match_empty)
        {
          if (!empty_match_index_)
            empty_match_index_ = static_cast<int>(i);

          // Empty-matchable regexes are candidates for every first byte.
          for (size_t b = 0; b < 128; ++b)
            ascii_candidates_[b].push_back(idx);
          nonascii_candidates_.push_back(idx);
          continue;
        }

        // ASCII bytes: check bitmap.
        for (size_t b = 0; b < 128; ++b)
        {
          if ((info.bitmap[b >> 6] >> (b & 63)) & 1)
            ascii_candidates_[b].push_back(idx);
        }

        // Non-ASCII first bytes share a single candidate list.
        if (info.can_match_nonascii)
        {
          nonascii_candidates_.push_back(idx);
        }
      }
    }

    std::vector<TRegex> regexes_;
    // Per-ASCII-byte candidate lists (0-127).
    std::array<std::vector<uint16_t>, 128> ascii_candidates_;
    // Shared candidate list for all non-ASCII first bytes (128-255).
    std::vector<uint16_t> nonascii_candidates_;
    // Precomputed index of the first empty-matchable regex.
    std::optional<int> empty_match_index_;
  };

  // Backwards-compatibility aliases for code written against the old
  // RE2-based API.  These will be removed in a future release.
  using RE2 [[deprecated("Use TRegex instead of RE2")]] = TRegex;
  using REMatch [[deprecated("Use TRegexMatch instead of REMatch")]] =
    TRegexMatch;
  using REIterator [[deprecated("Use TRegexIterator instead of REIterator")]] =
    TRegexIterator;

  inline std::optional<int>
  TRegexIterator::consume_first_match(TRegexMatch& m, const TRegexSet& set)
  {
    std::string_view remainder = source->view().substr(pos_);
    auto idx = set.match(m, remainder, source, pos_);
    if (idx)
      pos_ += m.at(0).len;
    return idx;
  }

  inline Node build_ast(Source source, size_t pos)
  {
    auto hd = TRegex("[[:space:]]*\\([[:space:]]*([^[:space:]\\(\\)]*)");
    auto st = TRegex("[[:space:]]*\\{[^\\}]*\\}");
    auto id = TRegex("[[:space:]]*([[:digit:]]+):");
    auto tl = TRegex("[[:space:]]*\\)");

    TRegexMatch re_match(2);
    TRegexIterator re_iterator(source);
    re_iterator.skip(pos);

    Node top;
    Node ast;

    while (!re_iterator.empty())
    {
      // Find the type of the node. If we didn't find a node, it's an error.
      if (!re_iterator.consume(hd, re_match))
      {
        auto loc = re_iterator.current();
        logging::Error() << loc.origin_linecol() << ": expected node"
                         << std::endl
                         << loc.str() << std::endl;
        return {};
      }

      // If we don't have a valid node type, it's an error.
      auto type_loc = re_match.at(1);
      auto type = detail::find_token(type_loc.view());

      if (type == Invalid)
      {
        logging::Error() << type_loc.origin_linecol() << ": unknown type"
                         << std::endl
                         << type_loc.str() << std::endl;
        return {};
      }

      // Find the source location of the node as a netstring.
      auto ident_loc = type_loc;

      if (re_iterator.consume(id, re_match))
      {
        auto len = re_match.parse<size_t>(1);
        ident_loc =
          Location(source, re_match.at().pos + re_match.at().len, len);
        re_iterator.skip(len);
      }

      // Push the node into the AST.
      auto node = NodeDef::create(type, ident_loc);

      if (ast)
        ast->push_back(node);
      else
        top = node;

      ast = node;

      // Skip the symbol table.
      re_iterator.consume(st, re_match);

      // `)` ends the node. Otherwise, we'll add children to this node.
      while (re_iterator.consume(tl, re_match))
      {
        auto parent = ast->parent();

        if (!parent)
          return ast;

        ast = parent;
      }
    }

    // We never finished the AST, so it's an error.
    auto loc = re_iterator.current();
    logging::Error() << loc.origin_linecol() << ": incomplete AST" << std::endl
                     << loc.str() << std::endl;
    return {};
  }
}

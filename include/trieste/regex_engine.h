// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "unicode_data.h"
#include "utf8.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#ifndef TRIESTE_REGEX_ENGINE_ENABLE_STATS
#  define TRIESTE_REGEX_ENGINE_ENABLE_STATS 0
#endif

namespace trieste::regex
{
  using namespace utf8;

  // =========================================================================
  // Constants & Helpers
  // =========================================================================

  // Internal postfix-notation operator sentinels. These are rune values in
  // the private-use area, never produced by user input. They drive the
  // shunting-yard parser and Thompson NFA construction.
  inline constexpr rune_t Backslash = static_cast<rune_t>('\\');
  inline constexpr rune_t LParen = static_cast<rune_t>('(');
  inline constexpr rune_t RParen = static_cast<rune_t>(')');
  inline constexpr rune_t Pipe = static_cast<rune_t>('|');
  inline constexpr rune_t Question = static_cast<rune_t>('?');
  inline constexpr rune_t Asterisk = static_cast<rune_t>('*');
  inline constexpr rune_t Plus = static_cast<rune_t>('+');
  inline constexpr rune_t Catenation = 0xAFFF00;
  inline constexpr rune_t Alternation = 0xAFFF01;
  inline constexpr rune_t ZeroOrOne = 0xAFFF02;
  inline constexpr rune_t ZeroOrMore = 0xAFFF03;
  inline constexpr rune_t OneOrMore = 0xAFFF04;
  inline constexpr rune_t Split = 0xAFFF05;
  inline constexpr rune_t WordBoundary = 0xAFFF06;
  // Lazy quantifier variants — prefer shortest match for the quantified
  // sub-expression. Only available when SyntaxMode::Extended is used.
  inline constexpr rune_t LazyZeroOrOne = 0xAFFF07;
  inline constexpr rune_t LazyZeroOrMore = 0xAFFF08;
  inline constexpr rune_t LazyOneOrMore = 0xAFFF09;
  inline constexpr rune_t StartAnchor = 0xAFFF0A;
  inline constexpr rune_t EndAnchor = 0xAFFF0B;
  inline constexpr rune_t Match = 0xAFFFFF;

  // Capture sentinels: CaptureOpen + i and CaptureClose + i mark the
  // start and end of capturing group i (0-based). Used as NFA state labels.
  inline constexpr rune_t CaptureOpen = 0xAFFE00;
  inline constexpr rune_t CaptureClose = 0xAFFD00;
  // CaptureGroup + i is a unary postfix operator that wraps a fragment
  // with CaptureOpen(i) and CaptureClose(i) epsilon states.
  inline constexpr rune_t CaptureGroup = 0xAFFC00;
  inline constexpr size_t MaxCaptures = 64;

  // Class-ref sentinels: a label in [RuneClassBase, RuneClassBase + 0xFFFF]
  // encodes an index into RegexEngine::rune_classes_.
  inline constexpr rune_t RuneClassBase = 0xBF0000;
  inline constexpr rune_t RuneClassMax = 0xBFFFFF;

  // Validate that sentinel ranges are well-formed and non-overlapping.
  // Each capture range must fit MaxCaptures entries without reaching
  // the next range, and the class-ref range must not overlap captures.
  static_assert(
    CaptureGroup + MaxCaptures <= CaptureClose,
    "CaptureGroup overflows into CaptureClose");
  static_assert(
    CaptureClose + MaxCaptures <= CaptureOpen,
    "CaptureClose overflows into CaptureOpen");
  static_assert(
    CaptureOpen + MaxCaptures <= Catenation,
    "CaptureOpen overflows into operator sentinels");
  static_assert(
    RuneClassBase <= RuneClassMax,
    "RuneClassBase must not exceed RuneClassMax");
  static_assert(
    RuneClassBase > Match,
    "RuneClass range must not overlap operator sentinels");

  // Resource limits (RFC 9485 §8 and engine-specific bounds).
  //   MaxRepetition  — maximum count in {n,m} quantifiers.
  //   MaxPostfixSize — maximum postfix expression length (after expansion).
  //   MaxStates      — maximum NFA states after Thompson construction.
  //   MaxClosureCacheEntries — maximum precomputed epsilon-closure entries.
  //   MaxGroupNesting — maximum depth of nested parenthesised groups.
  //   MaxCaptureFrameEntries — maximum capture frame arena entries.
  inline constexpr size_t MaxRepetition = 1000;
  inline constexpr size_t MaxPostfixSize = 100000;
  inline constexpr size_t MaxStates = 100000;
  inline constexpr size_t MaxClosureCacheEntries = 1'000'000;
  inline constexpr size_t MaxGroupNesting = 256;
  inline constexpr size_t MaxCaptureFrameEntries = 1'000'000;

  // Error codes reported when pattern compilation fails.
  // Use error_code_string() to obtain a human-readable message.
  enum class ErrorCode
  {
    NoError,
    ErrorBadEscape,
    ErrorMissingBracket,
    ErrorMissingParen,
    ErrorUnexpectedParen,
    ErrorTrailingBackslash,
    ErrorRepeatSize,
    ErrorRepeatOp,
    ErrorBadCharClass,
    ErrorBadCharRange,
    ErrorPatternTooLarge,
    ErrorNestingTooDeep,
    ErrorTooManyCaptures,
    ErrorStrictSyntax,
    ErrorStrictGroup,
    ErrorInternalError,
  };

  inline const char* error_code_string(ErrorCode code)
  {
    switch (code)
    {
      case ErrorCode::NoError:
        return "";
      case ErrorCode::ErrorBadEscape:
        return "invalid escape sequence";
      case ErrorCode::ErrorMissingBracket:
        return "missing closing ]";
      case ErrorCode::ErrorMissingParen:
        return "missing closing )";
      case ErrorCode::ErrorUnexpectedParen:
        return "unexpected )";
      case ErrorCode::ErrorTrailingBackslash:
        return "trailing backslash";
      case ErrorCode::ErrorRepeatSize:
        return "invalid repetition size";
      case ErrorCode::ErrorRepeatOp:
        return "invalid nested repetition";
      case ErrorCode::ErrorBadCharClass:
        return "invalid character class";
      case ErrorCode::ErrorBadCharRange:
        return "invalid character range";
      case ErrorCode::ErrorPatternTooLarge:
        return "pattern too large";
      case ErrorCode::ErrorNestingTooDeep:
        return "nesting too deep";
      case ErrorCode::ErrorTooManyCaptures:
        return "too many capture groups";
      case ErrorCode::ErrorStrictSyntax:
        return "syntax not permitted in strict iregexp mode";
      case ErrorCode::ErrorStrictGroup:
        return "groups not permitted in strict iregexp mode";
      case ErrorCode::ErrorInternalError:
        return "internal error";
    }
    return "unknown error";
  }

  inline constexpr bool is_class_ref(rune_t label)
  {
    return label >= RuneClassBase && label <= RuneClassMax;
  }

  inline constexpr size_t class_ref_index(rune_t label)
  {
    return static_cast<size_t>(label - RuneClassBase);
  }

  inline constexpr bool is_capture_open(rune_t label)
  {
    return label >= CaptureOpen && label < CaptureOpen + MaxCaptures;
  }

  inline constexpr bool is_capture_close(rune_t label)
  {
    return label >= CaptureClose && label < CaptureClose + MaxCaptures;
  }

  inline constexpr size_t capture_index(rune_t label)
  {
    if (is_capture_open(label))
      return static_cast<size_t>(label - CaptureOpen);
    return static_cast<size_t>(label - CaptureClose);
  }

  // A word character for \b boundary: [0-9A-Za-z_], matching \w.
  inline constexpr bool is_word_char(rune_t r)
  {
    return (r >= '0' && r <= '9') || (r >= 'A' && r <= 'Z') ||
      (r >= 'a' && r <= 'z') || r == '_';
  }

  inline bool is_ascii(rune_t r)
  {
    return r < 128;
  }

  // Decode one rune from utf8_str at byte offset pos.
  // Returns {rune_value, bytes_consumed}. Fast path for ASCII.
  inline std::pair<rune_t, size_t>
  decode_rune(const std::string_view& utf8_str, size_t pos)
  {
    auto byte = static_cast<unsigned char>(utf8_str[pos]);
    if (is_ascii(byte))
      return {static_cast<rune_t>(byte), 1};
    auto [r, consumed] = utf8_to_rune(utf8_str.substr(pos), false);
    return {r.value, consumed.size()};
  }

  // =========================================================================
  // Public Types
  // =========================================================================

  // A character class: a sorted, non-overlapping set of Unicode codepoint
  // ranges plus a precomputed 128-bit ASCII bitmap for fast single-byte
  // acceptance checks.
  struct RuneClass
  {
    std::vector<std::pair<rune_t, rune_t>> ranges;
    uint64_t ascii_bitmap[2] = {};

    bool contains(rune_t r) const
    {
      if (is_ascii(r))
        return (ascii_bitmap[r >> 6] >> (r & 63)) & 1;

      if (ranges.empty())
        return false;

      // Binary search for the range containing r.
      auto it = std::upper_bound(
        ranges.begin(),
        ranges.end(),
        r,
        [](rune_t val, const std::pair<rune_t, rune_t>& range) {
          return val < range.first;
        });
      if (it == ranges.begin())
        return false;
      --it;
      return r >= it->first && r <= it->second;
    }

    void add_range(rune_t lo, rune_t hi)
    {
      ranges.push_back({lo, hi});
    }

    void merge(const RuneClass& other)
    {
      ranges.insert(ranges.end(), other.ranges.begin(), other.ranges.end());
    }

    // Sort, merge overlapping ranges, and rebuild the ASCII bitmap.
    // Must be called after all add_range/merge calls are complete,
    // before the class is used for matching or complement.
    void finalize()
    {
      if (ranges.size() <= 1)
      {
        rebuild_ascii_bitmap();
        return;
      }
      std::sort(ranges.begin(), ranges.end());
      std::vector<std::pair<rune_t, rune_t>> merged;
      merged.push_back(ranges[0]);
      for (size_t i = 1; i < ranges.size(); i++)
      {
        auto& back = merged.back();
        if (ranges[i].first <= back.second + 1)
          back.second = std::max(back.second, ranges[i].second);
        else
          merged.push_back(ranges[i]);
      }
      ranges = std::move(merged);
      rebuild_ascii_bitmap();
    }

    // Add a gap range to `out`, splitting around the surrogate range
    // [0xD800, 0xDFFF] to produce only valid Unicode scalar values.
    static void add_gap_excluding_surrogates(
      rune_t gap_lo, rune_t gap_hi, std::vector<std::pair<rune_t, rune_t>>& out)
    {
      if (gap_lo < 0xD800)
      {
        if (gap_hi < 0xD800)
        {
          // full to the left of the surrogate range
          out.push_back({gap_lo, gap_hi});
          return;
        }

        // straddles left
        out.push_back({gap_lo, 0xD7FF});
        if (gap_hi > 0xDFFF)
        {
          // straddles the entire range
          out.push_back({0xE000, gap_hi});
        }
        return;
      }

      if (gap_lo < 0xE000)
      {
        // straddles right
        out.push_back({0xE000, gap_hi});
        return;
      }

      // full to the right
      out.push_back({gap_lo, gap_hi});
    }

    RuneClass complement() const
    {
      // Complement over Unicode scalar values (excluding surrogates).
      RuneClass result;
      rune_t prev = 0;
      for (auto& [lo, hi] : ranges)
      {
        if (lo > prev)
          add_gap_excluding_surrogates(prev, lo - 1, result.ranges);
        prev = hi + 1;
      }
      if (prev <= 0x10FFFF)
        add_gap_excluding_surrogates(prev, 0x10FFFF, result.ranges);
      result.rebuild_ascii_bitmap();
      return result;
    }

    static RuneClass dot()
    {
      // All Unicode scalar values (XSD semantics: matches everything incl.
      // newlines).
      RuneClass rc{{{0, 0xD7FF}, {0xE000, 0x10FFFF}}};
      rc.rebuild_ascii_bitmap();
      return rc;
    }

  private:
    void rebuild_ascii_bitmap()
    {
      ascii_bitmap[0] = 0;
      ascii_bitmap[1] = 0;
      for (auto& [lo, hi] : ranges)
      {
        if (lo >= 128)
          continue;
        rune_t clamped_hi = (hi < 128) ? hi : 127;
        for (rune_t r = lo; r <= clamped_hi; r++)
          ascii_bitmap[r >> 6] |= uint64_t(1) << (r & 63);
      }
    }
  };

  // =========================================================================
  // Public API (RegexEngine)
  // =========================================================================

  class RegexEngine
  {
  private:
    struct StateDef;
    using State = StateDef*;

    // Thread for capture-aware simulation.
    struct Thread
    {
      State state;
      size_t caps_frame;
    };

  public:
    // Controls which regex syntax features are accepted.
    //   Extended      — full syntax: lazy quantifiers, \b, \d/\w/\s, POSIX
    //                   character classes, non-capturing groups (?:...).
    //   IregexpStrict — RFC 9485 I-Regexp subset: no anchors, no lazy
    //                   quantifiers, no \b, no \d/\w/\s shorthands, no
    //                   non-capturing groups. Bare ] and } are errors.
    //                   See https://www.rfc-editor.org/rfc/rfc9485
    enum class SyntaxMode
    {
      Extended,
      IregexpStrict,
    };

    struct MatchStats
    {
      size_t rune_steps = 0;
      size_t active_states_total = 0;
      size_t max_active_states = 0;
      size_t class_ref_checks = 0;
      size_t literal_checks = 0;
    };

    // Per-call mutable state threaded through match routines.
    //
    // Usage model:
    // - Create one MatchContext per caller/thread and reuse it across calls.
    // - Reusing a context avoids repeated scratch-buffer allocations.
    // - A MatchContext is not thread-safe and must not be used concurrently.
    // - RegexEngine::match/find_prefix bind the context to the engine each
    //   call and reset per-call counters, so contexts can be reused safely
    //   across different RegexEngine instances.
    struct MatchContext
    {
      friend class RegexEngine;

    public:
      MatchStats stats() const
      {
#if TRIESTE_REGEX_ENGINE_ENABLE_STATS
        return match_stats;
#else
        return {};
#endif
      }

    private:
      const void* bound_engine = nullptr;

      // --- Scratch buffers (reused across calls to avoid allocation) ---
      std::vector<State> noncapturing_current_states;
      std::vector<State> noncapturing_next_states;
      std::vector<Thread> capturing_current_threads;
      std::vector<Thread> capturing_next_threads;
      std::vector<std::pair<State, size_t>> capture_traversal_stack_;

      // --- Visited tracking (epoch-based for large NFAs) ---
      size_t epoch_counter = 0;
      std::vector<size_t> visited_states;

      // --- Visited tracking (bitset fast-path for ≤128 NFA states) ---
      static constexpr size_t BitsetMaxStates = 128;
      uint64_t visited_bits_[2] = {0, 0};
      bool use_bitset_ = false;

      // --- Capture frame allocator ---
      std::vector<size_t> capture_frames;
      size_t capture_frame_slots = 0;

      // --- Stats accumulator ---
#if TRIESTE_REGEX_ENGINE_ENABLE_STATS
      MatchStats match_stats;
#endif

      void ensure_visited_capacity(size_t state_count)
      {
        if (visited_states.size() != state_count)
          visited_states.assign(state_count, 0);
      }

      void bind_engine(const void* engine, size_t state_count)
      {
        use_bitset_ = (state_count <= BitsetMaxStates);
        if (bound_engine != engine)
        {
          bound_engine = engine;
          epoch_counter = 0;
          visited_states.assign(state_count, 0);
          return;
        }
        ensure_visited_capacity(state_count);
      }

      void reset_capture_frames(size_t cap_slots)
      {
        capture_frame_slots = cap_slots;
        capture_frames.clear();
      }

      size_t allocate_capture_frame(size_t fill)
      {
        assert(capture_frame_slots > 0);
        if (
          capture_frames.size() + capture_frame_slots > MaxCaptureFrameEntries)
          return RegexEngine::npos;
        size_t frame = capture_frames.size() / capture_frame_slots;
        capture_frames.resize(
          capture_frames.size() + capture_frame_slots, fill);
        return frame;
      }

      size_t clone_capture_frame(size_t frame)
      {
        assert(capture_frame_slots > 0);
        assert((frame + 1) * capture_frame_slots <= capture_frames.size());
        if (
          capture_frames.size() + capture_frame_slots > MaxCaptureFrameEntries)
          return RegexEngine::npos;
        size_t out = capture_frames.size() / capture_frame_slots;
        size_t src_offset = frame * capture_frame_slots;
        size_t dst_offset = capture_frames.size();
        capture_frames.resize(dst_offset + capture_frame_slots);
        std::copy_n(
          capture_frames.data() + src_offset,
          capture_frame_slots,
          capture_frames.data() + dst_offset);
        return out;
      }

      size_t* capture_frame_data(size_t frame)
      {
        assert(capture_frame_slots > 0);
        assert((frame + 1) * capture_frame_slots <= capture_frames.size());
        return capture_frames.data() + (frame * capture_frame_slots);
      }

      const size_t* capture_frame_data(size_t frame) const
      {
        assert(capture_frame_slots > 0);
        assert((frame + 1) * capture_frame_slots <= capture_frames.size());
        return capture_frames.data() + (frame * capture_frame_slots);
      }

      void reset_match_stats()
      {
#if TRIESTE_REGEX_ENGINE_ENABLE_STATS
        match_stats = {};
#endif
      }

      void advance_epoch(size_t& epoch)
      {
        epoch_counter++;
        if (epoch_counter == 0)
        {
          epoch_counter = 1;
          std::fill(visited_states.begin(), visited_states.end(), 0);
        }
        epoch = epoch_counter;
      }

      void clear_visited_bitset()
      {
        visited_bits_[0] = 0;
        visited_bits_[1] = 0;
      }

      bool bitset_test_and_set(size_t state_index)
      {
        size_t word = state_index >> 6;
        uint64_t bit = uint64_t(1) << (state_index & 63);
        if (visited_bits_[word] & bit)
          return true;
        visited_bits_[word] |= bit;
        return false;
      }

      bool is_visited(size_t state_index, size_t epoch) const
      {
        return visited_states[state_index] == epoch;
      }

      void mark_visited(size_t state_index, size_t epoch)
      {
        visited_states[state_index] = epoch;
      }

      void record_active_states(size_t count)
      {
#if TRIESTE_REGEX_ENGINE_ENABLE_STATS
        match_stats.rune_steps++;
        match_stats.active_states_total += count;
        if (count > match_stats.max_active_states)
          match_stats.max_active_states = count;
#else
        (void)count;
#endif
      }

      void stats_inc_class_ref_checks(size_t count = 1)
      {
#if TRIESTE_REGEX_ENGINE_ENABLE_STATS
        match_stats.class_ref_checks += count;
#else
        (void)count;
#endif
      }

      void stats_inc_literal_checks(size_t count = 1)
      {
#if TRIESTE_REGEX_ENGINE_ENABLE_STATS
        match_stats.literal_checks += count;
#else
        (void)count;
#endif
      }
    };

    RegexEngine(const std::string_view& utf8_regexp)
    : RegexEngine(utf8_regexp, SyntaxMode::Extended)
    {}

    RegexEngine(const std::string_view& utf8_regexp, SyntaxMode syntax_mode)
    : error_code_(ErrorCode::NoError),
      num_captures_(0),
      syntax_mode_(syntax_mode)
    {
      std::vector<rune_t> postfix = regexp_to_postfix_runes(utf8_regexp);
      // Reserve for Thompson: at most 2 states per postfix token + 1 accept.
      // This guarantees no reallocation, keeping all State pointers stable.
      owned_states_.reserve(2 * postfix.size() + 1);
      accept_state_ = create_state(Match);
      start_state_ = postfix_to_nfa(postfix);
      detect_conditional_states();
      precompute_epsilon_closures();
      finalize_states();
      first_char_info_ = compute_first_char_info();
    }

    RegexEngine(const RegexEngine&) = delete;
    RegexEngine& operator=(const RegexEngine&) = delete;
    RegexEngine(RegexEngine&&) = default;
    RegexEngine& operator=(RegexEngine&&) = default;

    bool ok() const
    {
      return error_code_ == ErrorCode::NoError;
    }

    ErrorCode error_code() const
    {
      return error_code_;
    }

    const char* error() const
    {
      return error_code_string(error_code_);
    }

    const std::string& error_arg() const
    {
      return error_arg_;
    }

    size_t num_captures() const
    {
      return num_captures_;
    }

    SyntaxMode syntax_mode() const
    {
      return syntax_mode_;
    }

    struct FirstCharInfo
    {
      uint64_t bitmap[2];
      bool can_match_empty;
      bool can_match_nonascii;

      bool test(uint8_t byte) const
      {
        if (!regex::is_ascii(byte))
          return can_match_nonascii;
        return (bitmap[byte >> 6] >> (byte & 63)) & 1;
      }

      static FirstCharInfo minimal()
      {
        return {{0, 0}, false, false};
      }

      static FirstCharInfo maximal()
      {
        return {{~uint64_t(0), ~uint64_t(0)}, true, true};
      }
    };

    FirstCharInfo first_char_info() const
    {
      return first_char_info_;
    }

    bool match(const std::string_view& utf8_str) const
    {
      MatchContext ctx;
      return match(utf8_str, ctx);
    }

    bool match(const std::string_view& utf8_str, MatchContext& ctx) const
    {
      return find_prefix(utf8_str, ctx, true) == utf8_str.size();
    }

    // Prefix match: returns the byte length of the longest prefix of
    // utf8_str that matches the pattern. Returns npos if no prefix matches.
    static constexpr size_t npos = static_cast<size_t>(-1);

    size_t
    find_prefix(const std::string_view& utf8_str, bool at_start = true) const
    {
      MatchContext ctx;
      return find_prefix(utf8_str, ctx, at_start);
    }

    size_t find_prefix(
      const std::string_view& utf8_str,
      MatchContext& ctx,
      bool at_start = true) const
    {
      return find_prefix_noncapturing_with_context(
        utf8_str, ctx, at_start, false);
    }

    // A capture span: byte offsets [start, end) within the input string.
    struct Capture
    {
      size_t start = npos;
      size_t end = npos;

      bool matched() const
      {
        return start != npos;
      }
    };

    // Prefix match with captures. Returns the byte length of the longest
    // prefix match, or npos if no match. On success, `captures` is resized
    // to num_captures() and filled with the capture spans from the best
    // (longest) match. captures[i] corresponds to group (i+1).
    size_t find_prefix(
      const std::string_view& utf8_str,
      std::vector<Capture>& captures,
      bool at_start = true) const
    {
      MatchContext ctx;
      return find_prefix(utf8_str, captures, ctx, at_start);
    }

    size_t find_prefix(
      const std::string_view& utf8_str,
      std::vector<Capture>& captures,
      MatchContext& ctx,
      bool at_start = true) const
    {
      return find_prefix_with_context(utf8_str, captures, ctx, at_start, false);
    }

    // Result of a search() call.
    struct SearchResult
    {
      size_t match_start = npos;
      size_t match_len = 0;

      bool found() const
      {
        return match_start != npos;
      }
    };

    // Search for the first match of the pattern anywhere within utf8_str,
    // starting from byte offset start_pos. Returns a SearchResult with the
    // match position and length. On success, captures are filled with byte
    // offsets relative to utf8_str (not the suffix).
    //
    // Unlike find_prefix, search correctly tracks word-boundary context
    // (\b) as it scans through the string, so patterns like \bword\b work
    // correctly when matched against interior positions.
    SearchResult
    search(const std::string_view& utf8_str, size_t start_pos = 0) const
    {
      MatchContext ctx;
      std::vector<Capture> captures;
      return search(utf8_str, captures, ctx, start_pos);
    }

    SearchResult search(
      const std::string_view& utf8_str,
      std::vector<Capture>& captures,
      MatchContext& ctx,
      size_t start_pos = 0) const
    {
      SearchResult result;
      if (!ok())
        return result;

      for (size_t probe = start_pos; probe <= utf8_str.size();)
      {
        bool prev_word = false;
        if (probe > 0)
        {
          // The byte at probe-1 is either an ASCII char or part of a
          // multi-byte UTF-8 sequence. Since is_word_char only matches
          // ASCII word chars ([0-9A-Za-z_]), and all multi-byte leading/
          // continuation bytes have values >= 0x80, casting the raw byte
          // to rune_t gives the correct result.
          prev_word = is_word_char(static_cast<rune_t>(
            static_cast<unsigned char>(utf8_str[probe - 1])));
        }

        std::string_view suffix(
          utf8_str.data() + probe, utf8_str.size() - probe);
        size_t len = find_prefix_with_context(
          suffix, captures, ctx, probe == 0, prev_word);
        if (len != npos)
        {
          result.match_start = probe;
          result.match_len = len;
          // Adjust capture offsets to full-string coordinates.
          for (auto& cap : captures)
          {
            if (cap.matched())
            {
              cap.start += probe;
              cap.end += probe;
            }
          }
          return result;
        }

        // Advance by rune width so probes only occur at UTF-8 codepoint
        // boundaries, never in the middle of a multi-byte sequence.
        if (probe < utf8_str.size())
        {
          auto [r, n] = decode_rune(utf8_str, probe);
          probe += n;
        }
        else
        {
          break;
        }
      }
      return result;
    }

    // =========================================================================
    // Private Implementation
    // =========================================================================

  private:
    void set_error(ErrorCode code)
    {
      if (error_code_ == ErrorCode::NoError)
        error_code_ = code;
    }

    void set_error(ErrorCode code, std::string_view arg)
    {
      if (error_code_ == ErrorCode::NoError)
      {
        error_code_ = code;
        error_arg_ = arg.substr(0, 100);
      }
    }

    // Internal find_prefix variant that accepts initial prev_is_word context.
    // Used by search() to maintain correct \b semantics across probe positions.
    size_t find_prefix_with_context(
      const std::string_view& utf8_str,
      std::vector<Capture>& captures,
      MatchContext& ctx,
      bool at_start,
      bool prev_is_word) const
    {
      ctx.reset_match_stats();
      ctx.bind_engine(this, state_count_);
      captures.clear();
      if (!ok() || num_captures_ == 0)
      {
        // Non-capturing path with prev_is_word context.
        return find_prefix_noncapturing_with_context(
          utf8_str, ctx, at_start, prev_is_word);
      }

      size_t cap_slots = 2 * num_captures_;
      auto& current_threads = ctx.capturing_current_threads;
      auto& next_threads = ctx.capturing_next_threads;
      current_threads.clear();
      next_threads.clear();
      ctx.reset_capture_frames(cap_slots);

      size_t epoch = 0;

      rune_t current_rune = 0;
      size_t current_rune_bytes = 0;
      bool has_current_rune = false;
      bool next_is_word = false;
      if (!utf8_str.empty())
      {
        auto [dr, dn] = decode_rune(utf8_str, 0);
        current_rune = dr;
        current_rune_bytes = dn;
        has_current_rune = true;
        next_is_word = is_word_char(current_rune);
      }

      size_t init_caps_frame = ctx.allocate_capture_frame(npos);
      if (init_caps_frame == npos)
        return npos;
      ctx.advance_epoch(epoch);
      add_state_capturing(
        current_threads,
        start_state_,
        init_caps_frame,
        0,
        epoch,
        ctx,
        prev_is_word != next_is_word,
        at_start,
        utf8_str.empty());

      size_t best = npos;
      size_t best_caps_frame = npos;
      auto check_accept = [&](const std::vector<Thread>& threads, size_t pos) {
        for (auto& t : threads)
        {
          if (t.state == accept_state_)
          {
            best = pos;
            best_caps_frame = t.caps_frame;
            return;
          }
        }
      };

      check_accept(current_threads, 0);

      size_t pos = 0;
      while (has_current_rune && !current_threads.empty())
      {
        ctx.record_active_states(current_threads.size());
        rune_t rune_value = current_rune;
        prev_is_word = next_is_word;
        pos += current_rune_bytes;

        if (pos < utf8_str.size())
        {
          auto [dr, dn] = decode_rune(utf8_str, pos);
          current_rune = dr;
          current_rune_bytes = dn;
          next_is_word = is_word_char(current_rune);
          has_current_rune = true;
        }
        else
        {
          has_current_rune = false;
          next_is_word = false;
        }
        bool boundary = prev_is_word != next_is_word;

        ctx.advance_epoch(epoch);
        next_threads.clear();
        bool at_end_now = pos >= utf8_str.size();
        if (rune_value < 128)
        {
          for (auto& t : current_threads)
          {
            if (
              (t.state->ascii_accept[rune_value >> 6] >> (rune_value & 63)) & 1)
              add_state_capturing(
                next_threads,
                t.state->next,
                t.caps_frame,
                pos,
                epoch,
                ctx,
                boundary,
                false,
                at_end_now);
          }
        }
        else
        {
          for (auto& t : current_threads)
          {
            if (is_class_ref(t.state->label))
            {
              ctx.stats_inc_class_ref_checks();
              if (rune_classes_[class_ref_index(t.state->label)].contains(
                    rune_value))
                add_state_capturing(
                  next_threads,
                  t.state->next,
                  t.caps_frame,
                  pos,
                  epoch,
                  ctx,
                  boundary,
                  false,
                  at_end_now);
            }
            else if (t.state->label == rune_value)
            {
              ctx.stats_inc_literal_checks();
              add_state_capturing(
                next_threads,
                t.state->next,
                t.caps_frame,
                pos,
                epoch,
                ctx,
                boundary,
                false,
                at_end_now);
            }
          }
        }
        std::swap(current_threads, next_threads);
        check_accept(current_threads, pos);
      }

      if (best != npos && best_caps_frame != npos)
      {
        const size_t* best_caps = ctx.capture_frame_data(best_caps_frame);
        captures.resize(num_captures_);
        for (size_t i = 0; i < num_captures_; i++)
        {
          captures[i].start = best_caps[i * 2];
          captures[i].end = best_caps[i * 2 + 1];
        }
      }

      return best;
    }

    // Non-capturing find_prefix with prev_is_word context. Used by
    // find_prefix_with_context when the pattern has no capturing groups.
    size_t find_prefix_noncapturing_with_context(
      const std::string_view& utf8_str,
      MatchContext& ctx,
      bool at_start,
      bool prev_is_word) const
    {
      ctx.reset_match_stats();
      ctx.bind_engine(this, state_count_);
      if (!ok())
        return npos;

      auto& current_states = ctx.noncapturing_current_states;
      auto& next_states = ctx.noncapturing_next_states;
      current_states.clear();
      next_states.clear();

      size_t epoch = 0;
      rune_t current_rune = 0;
      size_t current_rune_bytes = 0;
      bool has_current_rune = false;
      bool next_is_word = false;
      size_t best = npos;

      if (!utf8_str.empty())
      {
        auto [dr, dn] = decode_rune(utf8_str, 0);
        current_rune = dr;
        current_rune_bytes = dn;
        has_current_rune = true;
        if (has_conditionals_)
          next_is_word = is_word_char(current_rune);
      }

      bool boundary = has_conditionals_ && (prev_is_word != next_is_word);
      bool at_end = has_conditionals_ && utf8_str.empty();
      start_list(
        current_states,
        start_state_,
        epoch,
        ctx,
        boundary,
        has_conditionals_ && at_start,
        at_end);

      if (is_match(current_states))
        best = 0;

      size_t pos = 0;
      while (has_current_rune && !current_states.empty())
      {
        ctx.record_active_states(current_states.size());
        rune_t rune_value = current_rune;
        if (has_conditionals_)
          prev_is_word = next_is_word;
        pos += current_rune_bytes;

        if (pos < utf8_str.size())
        {
          auto [dr, dn] = decode_rune(utf8_str, pos);
          current_rune = dr;
          current_rune_bytes = dn;
          has_current_rune = true;
          if (has_conditionals_)
            next_is_word = is_word_char(current_rune);
        }
        else
        {
          has_current_rune = false;
          if (has_conditionals_)
            next_is_word = false;
        }

        boundary = has_conditionals_ && (prev_is_word != next_is_word);
        at_end = has_conditionals_ && (pos >= utf8_str.size());
        step(
          current_states,
          rune_value,
          next_states,
          epoch,
          ctx,
          boundary,
          at_end);
        std::swap(current_states, next_states);
        if (is_match(current_states))
          best = pos;
      }

      return best;
    }

    struct StateDef
    {
      rune_t label;
      StateDef* next;
      StateDef* next_alt;
      size_t closure_index;
      uint64_t ascii_accept[2] = {};
      bool trivial_closure = false;
    };

    State create_state(
      const rune_t& label, State next = nullptr, State next_alt = nullptr)
    {
      if (owned_states_.size() >= owned_states_.capacity())
      {
        set_error(ErrorCode::ErrorInternalError);
        return accept_state_;
      }
      size_t idx = owned_states_.size();
      owned_states_.push_back(StateDef{label, next, next_alt, idx});
      return &owned_states_.back();
    }

    // Iteratively follow epsilon states, recording capture positions.
    // Uses a reusable stack in MatchContext to avoid per-call allocation
    // and recursive function-call overhead.
    void add_state_capturing(
      std::vector<Thread>& threads,
      State state,
      size_t caps_frame,
      size_t pos,
      size_t epoch,
      MatchContext& ctx,
      bool boundary_match = false,
      bool at_start = false,
      bool at_end = false) const
    {
      auto& stack = ctx.capture_traversal_stack_;
      stack.clear();
      stack.push_back({state, caps_frame});

      while (!stack.empty())
      {
        auto [s, frame] = stack.back();
        stack.pop_back();

        if (s == nullptr || ctx.is_visited(s->closure_index, epoch))
          continue;
        ctx.mark_visited(s->closure_index, epoch);

        if (s->label == Split)
        {
          stack.push_back({s->next_alt, frame});
          stack.push_back({s->next, frame});
          continue;
        }

        if (is_capture_open(s->label))
        {
          size_t idx = capture_index(s->label);
          size_t next_frame = ctx.clone_capture_frame(frame);
          if (next_frame == npos)
            continue;
          ctx.capture_frame_data(next_frame)[idx * 2] = pos;
          stack.push_back({s->next, next_frame});
          continue;
        }

        if (is_capture_close(s->label))
        {
          size_t idx = capture_index(s->label);
          size_t next_frame = ctx.clone_capture_frame(frame);
          if (next_frame == npos)
            continue;
          ctx.capture_frame_data(next_frame)[idx * 2 + 1] = pos;
          stack.push_back({s->next, next_frame});
          continue;
        }

        if (s->label == WordBoundary)
        {
          if (boundary_match)
            stack.push_back({s->next, frame});
          continue;
        }

        if (s->label == StartAnchor)
        {
          if (at_start)
            stack.push_back({s->next, frame});
          continue;
        }

        if (s->label == EndAnchor)
        {
          if (at_end)
            stack.push_back({s->next, frame});
          continue;
        }

        threads.push_back({s, frame});
      }
    }

    // Three boolean conditional flags (boundary_match, at_start, at_end)
    // produce 2^3 = 8 closure variants per state. When no conditional
    // epsilon transitions exist, only 1 closure per state is needed.
    static constexpr uint8_t FlagBoundary = 1;
    static constexpr uint8_t FlagAtStart = 2;
    static constexpr uint8_t FlagAtEnd = 4;
    static constexpr uint8_t ClosureFlagCombinations = 8;
    static_assert(ClosureFlagCombinations == (1 << 3));

    static uint8_t
    closure_flags(bool boundary_match, bool at_start, bool at_end)
    {
      return static_cast<uint8_t>(
        (boundary_match ? FlagBoundary : 0) | (at_start ? FlagAtStart : 0) |
        (at_end ? FlagAtEnd : 0));
    }

    // Construction-only scratch state for precomputing epsilon closures.
    // Keeps reusable buffers (closure, stack, seen_epoch) across calls to
    // compute_epsilon_closure so that each call clears and reuses rather
    // than allocating fresh vectors.
    struct ClosureBuilder
    {
      std::vector<State> closure;
      std::vector<State> stack;
      std::vector<size_t> seen_epoch;
      size_t traversal_epoch = 0;
    };

    void precompute_epsilon_closures()
    {
      if (!ok())
        return;

      const size_t state_count = owned_states_.size();
      const uint8_t num_flag_combos =
        has_conditionals_ ? ClosureFlagCombinations : 1;
      const size_t slot_count = state_count * num_flag_combos;

      ClosureBuilder builder;
      builder.seen_epoch.resize(state_count, 0);

      // Compute closures, record offsets, and fill the flat buffer
      // in a single pass.
      closure_cache_offsets_.resize(slot_count + 1);
      closure_cache_flat_.reserve(slot_count);
      size_t total_entries = 0;

      for (auto& state : owned_states_)
      {
        bool trivial = true;
        for (uint8_t flags = 0; flags < num_flag_combos; flags++)
        {
          const size_t slot = (state.closure_index * num_flag_combos) | flags;
          closure_cache_offsets_[slot] =
            static_cast<uint32_t>(closure_cache_flat_.size());
          compute_epsilon_closure(
            &state,
            (flags & 1) != 0,
            (flags & 2) != 0,
            (flags & 4) != 0,
            builder);
          closure_cache_flat_.insert(
            closure_cache_flat_.end(),
            builder.closure.begin(),
            builder.closure.end());
          total_entries += builder.closure.size();
          if (total_entries > MaxClosureCacheEntries)
          {
            set_error(ErrorCode::ErrorPatternTooLarge);
            return;
          }
          if (
            trivial &&
            (builder.closure.size() != 1 || builder.closure[0] != &state))
            trivial = false;
        }
        state.trivial_closure = trivial;
      }
      closure_cache_offsets_[slot_count] =
        static_cast<uint32_t>(closure_cache_flat_.size());
    }

    // Scan NFA states for conditional epsilon transitions (anchors,
    // word boundaries). When absent, simulation can skip is_word_char()
    // and pass constant false for all boundary/anchor parameters.
    void detect_conditional_states()
    {
      has_conditionals_ = false;
      for (const auto& s : owned_states_)
      {
        if (
          s.label == WordBoundary || s.label == StartAnchor ||
          s.label == EndAnchor)
        {
          has_conditionals_ = true;
          return;
        }
      }
    }

    // Finalize NFA states: populate per-state ASCII acceptance bitmaps
    // and release construction-only temporaries. States already live in
    // contiguous storage (owned_states_ is a pre-reserved vector<StateDef>),
    // so no copy or pointer remapping is needed.
    void finalize_states()
    {
      if (!ok())
        return;

      state_count_ = owned_states_.size();
      if (state_count_ == 0)
        return;

      // Populate per-state ASCII acceptance bitmaps.
      for (auto& s : owned_states_)
      {
        if (is_class_ref(s.label))
        {
          auto& rc = rune_classes_[class_ref_index(s.label)];
          s.ascii_accept[0] = rc.ascii_bitmap[0];
          s.ascii_accept[1] = rc.ascii_bitmap[1];
        }
        else if (s.label < 128)
        {
          s.ascii_accept[s.label >> 6] |= uint64_t(1) << (s.label & 63);
        }
      }
    }

    void compute_epsilon_closure(
      State state,
      bool boundary_match,
      bool at_start,
      bool at_end,
      ClosureBuilder& builder)
    {
      builder.closure.clear();
      if (state == nullptr)
        return;

      builder.stack.clear();
      builder.stack.push_back(state);

      builder.traversal_epoch++;
      if (builder.traversal_epoch == 0)
      {
        builder.traversal_epoch = 1;
        std::fill(builder.seen_epoch.begin(), builder.seen_epoch.end(), 0);
      }
      size_t current_epoch = builder.traversal_epoch;

      while (!builder.stack.empty())
      {
        auto s = builder.stack.back();
        builder.stack.pop_back();
        if (s == nullptr)
          continue;

        size_t seen_idx = s->closure_index;
        if (builder.seen_epoch[seen_idx] == current_epoch)
          continue;
        builder.seen_epoch[seen_idx] = current_epoch;

        if (s->label == Split)
        {
          builder.stack.push_back(s->next_alt);
          builder.stack.push_back(s->next);
          continue;
        }

        if (is_capture_open(s->label) || is_capture_close(s->label))
        {
          builder.stack.push_back(s->next);
          continue;
        }

        if (s->label == WordBoundary)
        {
          if (boundary_match)
            builder.stack.push_back(s->next);
          continue;
        }

        if (s->label == StartAnchor)
        {
          if (at_start)
            builder.stack.push_back(s->next);
          continue;
        }

        if (s->label == EndAnchor)
        {
          if (at_end)
            builder.stack.push_back(s->next);
          continue;
        }

        builder.closure.push_back(s);
      }
    }

    struct ClosureSpan
    {
      const State* data;
      uint32_t size;
      const State* begin() const
      {
        return data;
      }
      const State* end() const
      {
        return data + size;
      }
    };

    ClosureSpan epsilon_closure_cached(
      State state, bool boundary_match, bool at_start, bool at_end) const
    {
      if (state == nullptr)
        return {nullptr, 0};

      size_t slot;
      if (has_conditionals_)
      {
        slot = (state->closure_index * ClosureFlagCombinations) |
          closure_flags(boundary_match, at_start, at_end);
      }
      else
      {
        slot = state->closure_index;
      }
      assert(slot + 1 < closure_cache_offsets_.size());
      uint32_t begin = closure_cache_offsets_[slot];
      uint32_t end = closure_cache_offsets_[slot + 1];
      return {closure_cache_flat_.data() + begin, end - begin};
    }

    // Postfix operator sentinels (outside Unicode range, distinct from
    // literal runes so that escaped operators like \* are not confused
    // with the ZeroOrMore operator).

    struct Frag
    {
      State start;
      // Raw pointers to the `next` or `next_alt` member of States owned
      // by Frag objects on the stack. These must be patched (via patch())
      // before the owning State is moved or destroyed.
      std::vector<State*> dangling;
    };

    static inline std::vector<State*>&
    append(std::vector<State*>& lhs, std::vector<State*>& rhs)
    {
      lhs.insert(lhs.end(), rhs.begin(), rhs.end());
      return lhs;
    }

    static inline void patch(std::vector<State*>& targets, State target)
    {
      for (auto& entry : targets)
      {
        *entry = target;
      }
    }

    // Shunting-yard parser: self-contained transformer from a regex
    // pattern string to a postfix rune stream plus rune classes.
    // Owns its own error state, syntax policy, rune class table, and
    // capture counter — no back-reference to RegexEngine.
    struct PostfixBuilder
    {
      // -- Outputs (moved into RegexEngine after a successful run) --
      std::vector<rune_t> postfix;
      std::vector<RuneClass> rune_classes;
      size_t num_captures = 0;
      ErrorCode error_code = ErrorCode::NoError;
      std::string error_arg;

      // -- Configuration (immutable after construction) --
      SyntaxMode syntax_mode;

      explicit PostfixBuilder(SyntaxMode mode) : syntax_mode(mode) {}

      bool ok() const
      {
        return error_code == ErrorCode::NoError;
      }

      // -- Parser state --
      std::vector<rune_t> operator_stack;
      bool need_concat = false;
      bool escaped = false;
      size_t atom_start = 0;
      bool need_concat_before_atom = false;
      bool atom_is_quantified = false;

      struct GroupInfo
      {
        size_t postfix_index;
        bool need_concat;
        bool capturing;
        size_t capture_id; // valid only if capturing
      };
      std::vector<GroupInfo> group_start_stack;

      // -- Error reporting --

      void set_error(ErrorCode code)
      {
        if (error_code == ErrorCode::NoError)
          error_code = code;
      }

      void set_error(ErrorCode code, std::string_view arg)
      {
        if (error_code == ErrorCode::NoError)
        {
          error_code = code;
          error_arg = arg.substr(0, 100);
        }
      }

      // -- Syntax-mode policy helpers --

      bool is_extended_mode() const
      {
        return syntax_mode == SyntaxMode::Extended;
      }

      bool allow_standalone_closer_literals() const
      {
        return is_extended_mode();
      }

      bool allow_shorthand_escapes() const
      {
        return is_extended_mode();
      }

      bool allow_posix_char_classes() const
      {
        return is_extended_mode();
      }

      bool allow_word_boundary_escape() const
      {
        return is_extended_mode();
      }

      bool allow_lazy_quantifiers() const
      {
        return is_extended_mode();
      }

      bool allow_capturing_groups() const
      {
        return is_extended_mode();
      }

      bool allow_noncapturing_groups() const
      {
        return is_extended_mode();
      }

      bool allow_extended_identity_escape(rune_t ch) const
      {
        return is_extended_mode() && is_ascii_punctuation(ch);
      }

      // -- Utility helpers (static or pure) --

      static bool is_ascii_punctuation(rune_t ch)
      {
        if (ch < 0x21 || ch > 0x7E)
          return false;
        bool is_alnum = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z');
        return !is_alnum;
      }

      static bool is_ascii_digit_at(const std::string_view& pattern, size_t pos)
      {
        return pos < pattern.size() && pattern[pos] >= '0' &&
          pattern[pos] <= '9';
      }

      // Return true if ch is a valid iregexp SingleCharEsc target or
      // a short escape (\n, \r, \t).
      static bool is_class_escape(rune_t ch)
      {
        switch (ch)
        {
          case 'n':
          case 'r':
          case 't':
          case '(':
          case ')':
          case '*':
          case '+':
          case '-':
          case '.':
          case '?':
          case '[':
          case '\\':
          case ']':
          case '^':
          case '$':
          case '{':
          case '|':
          case '}':
          case '/':
          case ',':
            return true;
          default:
            return false;
        }
      }

      // Map short escape letter to its codepoint value.
      static rune_t escape_to_rune(rune_t ch)
      {
        switch (ch)
        {
          case 'n':
            return 0x0A;
          case 'r':
            return 0x0D;
          case 't':
            return 0x09;
          default:
            return ch;
        }
      }

      // Return a RuneClass for a shorthand escape (\d, \s, \w).
      // Returns true if ch is a shorthand escape letter; false otherwise.
      static bool shorthand_class(rune_t ch, RuneClass& rc)
      {
        switch (ch)
        {
          case 'd': // [0-9]
            rc.add_range('0', '9');
            rc.finalize();
            return true;
          case 's': // [ \t\n\r\f\v]
            rc.add_range('\t', '\r');
            rc.add_range(' ', ' ');
            rc.finalize();
            return true;
          case 'w': // [_0-9A-Za-z]
            rc.add_range('0', '9');
            rc.add_range('A', 'Z');
            rc.add_range('_', '_');
            rc.add_range('a', 'z');
            rc.finalize();
            return true;
          default:
            return false;
        }
      }

      // -- Rune class management --

      rune_t make_class_ref(const RuneClass& rc)
      {
        size_t idx = rune_classes.size();
        if (idx > (RuneClassMax - RuneClassBase))
        {
          set_error(ErrorCode::ErrorPatternTooLarge);
          return RuneClassBase;
        }
        rune_classes.push_back(rc);
        return RuneClassBase + static_cast<rune_t>(idx);
      }

      // -- Parsing helpers (char classes, escapes, hex digits) --

      // Parse exactly `count` hex digits from pattern at pos.
      // Returns the parsed value in `out`. Sets error on failure.
      bool parse_hex_digits(
        const std::string_view& pattern, size_t& pos, size_t count, rune_t& out)
      {
        out = 0;
        for (size_t i = 0; i < count; i++)
        {
          if (pos >= pattern.size())
          {
            set_error(ErrorCode::ErrorBadEscape);
            return false;
          }
          char ch = pattern[pos];
          rune_t digit = 0;
          if (ch >= '0' && ch <= '9')
            digit = ch - '0';
          else if (ch >= 'a' && ch <= 'f')
            digit = ch - 'a' + 10;
          else if (ch >= 'A' && ch <= 'F')
            digit = ch - 'A' + 10;
          else
          {
            set_error(ErrorCode::ErrorBadEscape);
            return false;
          }
          out = (out << 4) | digit;
          pos++;
        }
        return true;
      }

      // Parse \p{Xx} or \P{Xx} Unicode General Category escape.
      rune_t parse_unicode_category(
        const std::string_view& pattern, size_t& pos, bool negate)
      {
        if (pos >= pattern.size() || pattern[pos] != '{')
        {
          set_error(ErrorCode::ErrorBadCharClass);
          return RuneClassBase;
        }
        pos++; // skip '{'

        size_t name_start = pos;
        while (pos < pattern.size() && pattern[pos] != '}')
          pos++;

        if (pos >= pattern.size())
        {
          set_error(ErrorCode::ErrorBadCharClass);
          return RuneClassBase;
        }

        std::string_view cat_name(
          pattern.data() + name_start, pos - name_start);
        pos++; // skip '}'

        if (cat_name.empty() || cat_name.size() > 3)
        {
          set_error(ErrorCode::ErrorBadCharClass);
          return RuneClassBase;
        }

        auto info = unicode::find_category(cat_name);
        if (info.ranges == nullptr)
        {
          set_error(ErrorCode::ErrorBadCharClass);
          return RuneClassBase;
        }

        RuneClass rc;
        rc.ranges.assign(info.ranges, info.ranges + info.count);
        rc.ascii_bitmap[0] = info.ascii_bitmap.words[0];
        rc.ascii_bitmap[1] = info.ascii_bitmap.words[1];
        rc.finalize();
        if (negate)
          rc = rc.complement();
        return make_class_ref(rc);
      }

      // Parse a POSIX character class like [:alpha:] inside a bracket
      // expression. `pos` is after the '[:'; on return it points past ':]'.
      bool parse_posix_class(
        const std::string_view& pattern, size_t& pos, RuneClass& rc)
      {
        size_t name_start = pos;
        while (pos < pattern.size() && pattern[pos] != ':' &&
               pattern[pos] != ']')
          pos++;

        if (
          pos + 1 >= pattern.size() || pattern[pos] != ':' ||
          pattern[pos + 1] != ']')
        {
          set_error(ErrorCode::ErrorBadCharClass);
          return false;
        }

        std::string_view name(pattern.data() + name_start, pos - name_start);
        pos += 2; // skip ':]'

        if (name == "alpha")
        {
          rc.add_range('A', 'Z');
          rc.add_range('a', 'z');
        }
        else if (name == "digit")
        {
          rc.add_range('0', '9');
        }
        else if (name == "alnum")
        {
          rc.add_range('0', '9');
          rc.add_range('A', 'Z');
          rc.add_range('a', 'z');
        }
        else if (name == "blank")
        {
          rc.add_range(' ', ' ');
          rc.add_range('\t', '\t');
        }
        else if (name == "space")
        {
          rc.add_range('\t', '\r'); // \t \n \v \f \r
          rc.add_range(' ', ' ');
        }
        else if (name == "xdigit")
        {
          rc.add_range('0', '9');
          rc.add_range('A', 'F');
          rc.add_range('a', 'f');
        }
        else if (name == "upper")
        {
          rc.add_range('A', 'Z');
        }
        else if (name == "lower")
        {
          rc.add_range('a', 'z');
        }
        else if (name == "print")
        {
          rc.add_range(0x20, 0x7E);
        }
        else if (name == "graph")
        {
          rc.add_range(0x21, 0x7E);
        }
        else if (name == "cntrl")
        {
          rc.add_range(0x00, 0x1F);
          rc.add_range(0x7F, 0x7F);
        }
        else if (name == "punct")
        {
          // !"#$%&'()*+,-./ :;<=>?@ [\]^_` {|}~
          rc.add_range(0x21, 0x2F);
          rc.add_range(0x3A, 0x40);
          rc.add_range(0x5B, 0x60);
          rc.add_range(0x7B, 0x7E);
        }
        else if (name == "ascii")
        {
          rc.add_range(0x00, 0x7F);
        }
        else
        {
          set_error(ErrorCode::ErrorBadCharClass);
          return false;
        }

        return true;
      }

      // Parse a character class [...] expression.
      // `pos` is after the opening '['; on return it points past ']'.
      rune_t parse_char_class(const std::string_view& pattern, size_t& pos)
      {
        RuneClass rc;
        bool negated = false;

        if (pos < pattern.size() && pattern[pos] == '^')
        {
          negated = true;
          pos++;
        }

        // [^] is rejected per RFC 9485.
        if (pos >= pattern.size() || pattern[pos] == ']')
        {
          set_error(ErrorCode::ErrorMissingBracket);
          return RuneClassBase;
        }

        while (pos < pattern.size() && pattern[pos] != ']')
        {
          rune_t ch = 0;

          // POSIX character class [:name:].
          if (
            pos + 1 < pattern.size() && pattern[pos] == '[' &&
            pattern[pos + 1] == ':')
          {
            if (!allow_posix_char_classes())
            {
              set_error(ErrorCode::ErrorBadCharClass);
              return RuneClassBase;
            }
            pos += 2; // skip '[:'
            if (!parse_posix_class(pattern, pos, rc))
              return RuneClassBase;
            continue;
          }

          if (pattern[pos] == '\\')
          {
            pos++; // skip backslash
            if (pos >= pattern.size())
            {
              set_error(ErrorCode::ErrorTrailingBackslash);
              return RuneClassBase;
            }

            auto [esc_rune, esc_consumed] =
              utf8_to_rune(pattern.substr(pos), false);
            pos += esc_consumed.size();

            if (esc_rune.value == 'p' || esc_rune.value == 'P')
            {
              // \p{...}/\P{...} inside class — merge.
              bool neg = (esc_rune.value == 'P');
              auto ref = parse_unicode_category(pattern, pos, neg);
              if (!ok())
                return RuneClassBase;
              // Extract the class and merge it.
              rc.merge(rune_classes[class_ref_index(ref)]);
              continue;
            }

            // Shorthand escapes \d \D \s \S \w \W inside class — merge.
            {
              rune_t lower = esc_rune.value | 0x20;
              RuneClass shorthand;
              if (shorthand_class(lower, shorthand))
              {
                if (!allow_shorthand_escapes())
                {
                  set_error(ErrorCode::ErrorBadEscape);
                  return RuneClassBase;
                }
                if (esc_rune.value != lower)
                  shorthand = shorthand.complement();
                rc.merge(shorthand);
                continue;
              }
            }

            // Hex escape \xNN inside class.
            if (esc_rune.value == 'x')
            {
              rune_t hex_val = 0;
              if (!parse_hex_digits(pattern, pos, 2, hex_val))
                return RuneClassBase;
              ch = hex_val;
            }
            else if (is_class_escape(esc_rune.value))
            {
              ch = escape_to_rune(esc_rune.value);
            }
            else if (allow_extended_identity_escape(esc_rune.value))
            {
              ch = esc_rune.value;
            }
            else
            {
              set_error(ErrorCode::ErrorBadEscape);
              return RuneClassBase;
            }
          }
          else
          {
            auto [r, consumed] = utf8_to_rune(pattern.substr(pos), false);
            pos += consumed.size();
            ch = r.value;
          }

          // Check for range: ch-hi
          if (
            pos + 1 < pattern.size() && pattern[pos] == '-' &&
            pattern[pos + 1] != ']')
          {
            pos++; // skip '-'
            rune_t hi = 0;
            if (pattern[pos] == '\\')
            {
              pos++;
              if (pos >= pattern.size())
              {
                set_error(ErrorCode::ErrorBadEscape);
                return RuneClassBase;
              }
              auto [esc_rune, esc_consumed] =
                utf8_to_rune(pattern.substr(pos), false);
              pos += esc_consumed.size();
              if (esc_rune.value == 'x')
              {
                if (!parse_hex_digits(pattern, pos, 2, hi))
                  return RuneClassBase;
              }
              else if (is_class_escape(esc_rune.value))
              {
                hi = escape_to_rune(esc_rune.value);
              }
              else if (allow_extended_identity_escape(esc_rune.value))
              {
                hi = esc_rune.value;
              }
              else
              {
                set_error(ErrorCode::ErrorBadEscape);
                return RuneClassBase;
              }
            }
            else
            {
              auto [r, consumed] = utf8_to_rune(pattern.substr(pos), false);
              pos += consumed.size();
              hi = r.value;
            }

            if (ch > hi)
            {
              set_error(ErrorCode::ErrorBadCharRange);
              return RuneClassBase;
            }
            rc.add_range(ch, hi);
          }
          else
          {
            rc.add_range(ch, ch);
          }
        }

        if (pos >= pattern.size())
        {
          set_error(ErrorCode::ErrorMissingBracket);
          return RuneClassBase;
        }
        pos++; // skip ']'

        rc.finalize();

        if (negated)
          rc = rc.complement();

        return make_class_ref(rc);
      }

      // -- Predicates for non-trivial conditions --

      // True when no atom precedes the current token and the engine
      // allows standalone closer characters as literals.
      bool can_emit_standalone_closer() const
      {
        return !need_concat && allow_standalone_closer_literals();
      }

      // True when '{' follows an escaped-backslash single-rune atom and
      // is not the start of a range quantifier (no digit after '{').
      bool is_literal_brace_after_escape(
        const std::string_view& pattern, size_t pos) const
      {
        return need_concat && allow_standalone_closer_literals() &&
          !is_ascii_digit_at(pattern, pos) && atom_start < postfix.size() &&
          postfix.size() == atom_start + 1 && postfix[atom_start] == Backslash;
      }

      // True when the preceding concat can be undone (used by {0}).
      bool has_pending_concat_to_undo() const
      {
        return need_concat_before_atom && !operator_stack.empty() &&
          operator_stack.back() == Catenation;
      }

      // True when there is no quantifiable atom (leading quantifier or
      // double quantifier).
      bool lacks_quantifiable_atom() const
      {
        return !need_concat || atom_is_quantified;
      }

      // True when a ')' has no matching '(' on the stacks.
      bool has_unmatched_close_paren() const
      {
        return operator_stack.empty() || group_start_stack.empty();
      }

      // True when the pattern has a non-capturing group prefix (?:
      // at the current position.
      static bool
      is_noncapturing_group_prefix(const std::string_view& pattern, size_t pos)
      {
        return pos + 1 < pattern.size() && pattern[pos] == '?' &&
          pattern[pos + 1] == ':';
      }

      // -- Atom emission helpers --

      // Record that an atom begins at current postfix position.
      void begin_atom()
      {
        atom_start = postfix.size();
        need_concat_before_atom = need_concat;
        atom_is_quantified = false;
      }

      // Emit an atom: optional concat, begin_atom, push label, set
      // need_concat. Covers the repeated pattern used by most cases.
      void emit_atom(rune_t label)
      {
        if (need_concat)
          push_concat();
        begin_atom();
        postfix.push_back(label);
        need_concat = true;
      }

      // -- Operator stack helpers --

      // Push Cat onto operator_stack with correct precedence:
      // Cat (prec 2) pops other Cats but not Alternations (prec 1).
      void push_concat()
      {
        while (!operator_stack.empty() && operator_stack.back() == Catenation)
        {
          postfix.push_back(operator_stack.back());
          operator_stack.pop_back();
        }
        operator_stack.push_back(Catenation);
      }

      // Push Alternation onto operator_stack with correct precedence:
      // Alt (prec 1) pops Cats (prec 2) and other Alts (same, left-assoc).
      void push_alternation()
      {
        while (!operator_stack.empty() && operator_stack.back() != LParen)
        {
          postfix.push_back(operator_stack.back());
          operator_stack.pop_back();
        }
        operator_stack.push_back(Alternation);
      }

      // -- Quantifier suffix --

      // Parse an optional lazy suffix ('?') after a quantifier character.
      // Pushes the appropriate greedy or lazy postfix token.
      void parse_quantifier_suffix(
        rune_t greedy_op,
        rune_t lazy_op,
        const std::string_view& pattern,
        size_t& pos)
      {
        bool lazy = (pos < pattern.size() && pattern[pos] == '?');
        if (lazy)
        {
          if (!allow_lazy_quantifiers())
          {
            set_error(ErrorCode::ErrorStrictSyntax);
            return;
          }
          pos++;
        }
        postfix.push_back(lazy ? lazy_op : greedy_op);
        atom_is_quantified = true;
      }

      // -- Per-case handler methods --
      // Each returns true on success, false on error (caller returns {}).

      bool
      on_escape(rune_t rune_value, const std::string_view& pattern, size_t& pos)
      {
        escaped = false;

        // Handle \p{...} and \P{...} Unicode category escapes.
        if (rune_value == 'p' || rune_value == 'P')
        {
          bool negate = (rune_value == 'P');
          auto ref = parse_unicode_category(pattern, pos, negate);
          if (!ok())
            return false;
          emit_atom(ref);
          return true;
        }

        // Shorthand character class escapes: \d \D \s \S \w \W.
        {
          rune_t lower = rune_value | 0x20; // tolower for ASCII
          RuneClass rc;
          if (shorthand_class(lower, rc))
          {
            if (!allow_shorthand_escapes())
            {
              set_error(ErrorCode::ErrorBadEscape);
              return false;
            }
            if (rune_value != lower)
              rc = rc.complement();
            auto ref = make_class_ref(rc);
            emit_atom(ref);
            return true;
          }
        }

        // Hex escape \xNN — exactly 2 hex digits.
        if (rune_value == 'x')
        {
          rune_t hex_val = 0;
          if (!parse_hex_digits(pattern, pos, 2, hex_val))
            return false;
          emit_atom(hex_val);
          return true;
        }

        // Word boundary assertion \b.
        if (rune_value == 'b')
        {
          if (!allow_word_boundary_escape())
          {
            set_error(ErrorCode::ErrorBadEscape);
            return false;
          }
          emit_atom(WordBoundary);
          return true;
        }

        // Single-char escapes per RFC 9485.
        rune_t literal = 0;
        switch (rune_value)
        {
          case 'n':
            literal = 0x0A;
            break;
          case 'r':
            literal = 0x0D;
            break;
          case 't':
            literal = 0x09;
            break;
          // iregexp SingleCharEsc meta-characters.
          case '(':
          case ')':
          case '*':
          case '+':
          case '-':
          case '.':
          case '?':
          case '[':
          case '\\':
          case ']':
          case '^':
          case '$':
          case '{':
          case '|':
          case '}':
          case '/':
          case ',':
            literal = rune_value;
            break;
          default:
            if (allow_extended_identity_escape(rune_value))
            {
              literal = rune_value;
              break;
            }
            // Any other escape is invalid per RFC 9485.
            set_error(ErrorCode::ErrorBadEscape);
            return false;
        }

        emit_atom(literal);
        return true;
      }

      void on_backslash()
      {
        escaped = true;
      }

      void on_dot()
      {
        auto ref = make_class_ref(RuneClass::dot());
        emit_atom(ref);
      }

      bool on_char_class(const std::string_view& pattern, size_t& pos)
      {
        auto ref = parse_char_class(pattern, pos);
        if (!ok())
          return false;
        emit_atom(ref);
        return true;
      }

      void on_start_anchor()
      {
        emit_atom(StartAnchor);
      }

      void on_end_anchor()
      {
        emit_atom(EndAnchor);
      }

      bool on_close_bracket()
      {
        if (can_emit_standalone_closer())
        {
          begin_atom();
          postfix.push_back(']');
          need_concat = true;
          return true;
        }
        set_error(ErrorCode::ErrorStrictSyntax);
        return false;
      }

      bool on_open_brace(const std::string_view& pattern, size_t& pos)
      {
        // RE2-compatible literal '{' in standalone position.
        if (can_emit_standalone_closer())
        {
          begin_atom();
          postfix.push_back('{');
          need_concat = true;
          return true;
        }

        // Extended-mode compatibility: after an escaped backslash atom,
        // a non-quantifier '{' is treated as a literal '{' (e.g. "\\{").
        if (is_literal_brace_after_escape(pattern, pos))
        {
          push_concat();
          begin_atom();
          postfix.push_back('{');
          need_concat = true;
          return true;
        }

        // Parse range quantifier {n}, {n,}, {n,m}.
        if (atom_is_quantified)
        {
          // Nested quantifier like a*{2} — malformed.
          set_error(ErrorCode::ErrorRepeatOp);
          return false;
        }

        size_t n = 0, m = 0;
        bool unbounded = false;

        // Parse min.
        if (!is_ascii_digit_at(pattern, pos))
        {
          set_error(ErrorCode::ErrorRepeatOp);
          return false;
        }
        while (is_ascii_digit_at(pattern, pos))
        {
          n = n * 10 + (pattern[pos] - '0');
          pos++;
        }

        if (pos < pattern.size() && pattern[pos] == ',')
        {
          pos++;
          if (is_ascii_digit_at(pattern, pos))
          {
            while (is_ascii_digit_at(pattern, pos))
            {
              m = m * 10 + (pattern[pos] - '0');
              pos++;
            }
          }
          else
          {
            unbounded = true;
          }
        }
        else
        {
          // {n} — exact count.
          m = n;
        }

        if (pos >= pattern.size() || pattern[pos] != '}')
        {
          set_error(ErrorCode::ErrorRepeatOp);
          return false;
        }
        pos++; // skip '}'

        // Validate.
        if (n > MaxRepetition || m > MaxRepetition)
        {
          set_error(ErrorCode::ErrorRepeatSize);
          return false;
        }
        if (!unbounded && m < n)
        {
          set_error(ErrorCode::ErrorRepeatSize);
          return false;
        }

        // Extract the atom from postfix.
        std::vector<rune_t> atom(postfix.begin() + atom_start, postfix.end());
        postfix.resize(atom_start);

        // Check expansion size upfront to avoid large intermediate
        // allocations. Each copy uses atom.size() tokens plus up to
        // 2 operator tokens (ZeroOrOne/ZeroOrMore + Catenation).
        size_t copies = unbounded ? n + 1 : m;
        size_t expansion = copies * (atom.size() + 2);
        if (atom_start + expansion > MaxPostfixSize)
        {
          set_error(ErrorCode::ErrorPatternTooLarge);
          return false;
        }

        // Undo the Cat that was pushed before this atom if {0}.
        if (n == 0 && !unbounded && m == 0)
        {
          // {0} — delete the atom entirely.
          if (has_pending_concat_to_undo())
          {
            operator_stack.pop_back();
          }
          need_concat = need_concat_before_atom;
          atom_is_quantified = true;
          return true;
        }

        // Postfix expansion for range quantifiers:
        //   {n}   →  atom × n, chained with Cat
        //   {n,m} →  atom × n + (atom ZeroOrOne Cat) × (m-n)
        //   {n,}  →  atom × n + atom ZeroOrMore Cat
        //   {0}   →  (atom removed, Cat undone)

        // Emit mandatory copies (n).
        for (size_t i = 0; i < n; i++)
        {
          postfix.insert(postfix.end(), atom.begin(), atom.end());
          if (i > 0)
            postfix.push_back(Catenation);
        }

        if (unbounded)
        {
          // {n,} — n mandatory + ZeroOrMore.
          postfix.insert(postfix.end(), atom.begin(), atom.end());
          postfix.push_back(ZeroOrMore);
          if (n > 0)
            postfix.push_back(Catenation);
        }
        else if (m > n)
        {
          // {n,m} — n mandatory + (m-n) optional.
          for (size_t i = 0; i < (m - n); i++)
          {
            postfix.insert(postfix.end(), atom.begin(), atom.end());
            postfix.push_back(ZeroOrOne);
            if (n > 0 || i > 0)
              postfix.push_back(Catenation);
          }
        }

        if (postfix.size() > MaxPostfixSize)
        {
          set_error(ErrorCode::ErrorPatternTooLarge);
          return false;
        }

        need_concat = true;
        atom_is_quantified = true;
        return true;
      }

      bool on_close_brace()
      {
        if (can_emit_standalone_closer())
        {
          begin_atom();
          postfix.push_back('}');
          need_concat = true;
          return true;
        }
        set_error(ErrorCode::ErrorStrictSyntax);
        return false;
      }

      bool on_open_group(const std::string_view& pattern, size_t& pos)
      {
        // Check for non-capturing group (?:...).
        bool capturing = true;
        if (is_noncapturing_group_prefix(pattern, pos))
        {
          if (!allow_noncapturing_groups())
          {
            set_error(ErrorCode::ErrorStrictGroup);
            return false;
          }
          pos += 2; // skip '?:'
          capturing = false;
        }
        if (capturing && !allow_capturing_groups())
        {
          set_error(ErrorCode::ErrorStrictGroup);
          return false;
        }
        if (need_concat)
          push_concat();
        size_t cap_id = 0;
        if (capturing)
        {
          cap_id = num_captures++;
          if (num_captures > MaxCaptures)
          {
            set_error(ErrorCode::ErrorTooManyCaptures);
            return false;
          }
        }
        if (group_start_stack.size() >= MaxGroupNesting)
        {
          set_error(ErrorCode::ErrorNestingTooDeep);
          return false;
        }
        group_start_stack.push_back(
          {postfix.size(), need_concat, capturing, cap_id});
        operator_stack.push_back(LParen);
        need_concat = false;
        return true;
      }

      bool on_close_group()
      {
        while (!operator_stack.empty() && operator_stack.back() != LParen)
        {
          postfix.push_back(operator_stack.back());
          operator_stack.pop_back();
        }
        if (has_unmatched_close_paren())
        {
          set_error(ErrorCode::ErrorUnexpectedParen);
          return false;
        }
        operator_stack.pop_back();
        {
          auto& gi = group_start_stack.back();
          if (gi.capturing)
          {
            // Unary operator: wrap the group fragment with capture
            // epsilon states during NFA construction.
            postfix.push_back(
              CaptureGroup + static_cast<rune_t>(gi.capture_id));
          }
          atom_start = gi.postfix_index;
          need_concat_before_atom = gi.need_concat;
          group_start_stack.pop_back();
          atom_is_quantified = false;
        }
        need_concat = true;
        return true;
      }

      void on_pipe()
      {
        push_alternation();
        need_concat = false;
      }

      bool on_star(const std::string_view& pattern, size_t& pos)
      {
        if (lacks_quantifiable_atom())
        {
          set_error(ErrorCode::ErrorRepeatOp);
          return false;
        }
        parse_quantifier_suffix(ZeroOrMore, LazyZeroOrMore, pattern, pos);
        return ok();
      }

      bool on_question(const std::string_view& pattern, size_t& pos)
      {
        if (lacks_quantifiable_atom())
        {
          set_error(ErrorCode::ErrorRepeatOp);
          return false;
        }
        parse_quantifier_suffix(ZeroOrOne, LazyZeroOrOne, pattern, pos);
        return ok();
      }

      bool on_plus(const std::string_view& pattern, size_t& pos)
      {
        if (lacks_quantifiable_atom())
        {
          set_error(ErrorCode::ErrorRepeatOp);
          return false;
        }
        parse_quantifier_suffix(OneOrMore, LazyOneOrMore, pattern, pos);
        return ok();
      }

      void on_literal(rune_t rune_value)
      {
        emit_atom(rune_value);
      }

      // Drain operator stack and validate end-of-pattern state.
      bool finalize()
      {
        while (!operator_stack.empty())
        {
          if (operator_stack.back() == LParen)
          {
            set_error(ErrorCode::ErrorMissingParen);
            return false;
          }
          postfix.push_back(operator_stack.back());
          operator_stack.pop_back();
        }

        if (escaped)
        {
          set_error(ErrorCode::ErrorTrailingBackslash);
          return false;
        }

        if (postfix.size() > MaxPostfixSize)
        {
          set_error(ErrorCode::ErrorPatternTooLarge);
          return false;
        }

        return true;
      }
    };

    std::vector<rune_t> regexp_to_postfix_runes(const std::string_view& pattern)
    {
      PostfixBuilder builder(syntax_mode_);

      size_t pos = 0;
      while (pos < pattern.size())
      {
        auto [rune, consumed] = utf8_to_rune(pattern.substr(pos), false);
        pos += consumed.size();

        if (builder.escaped)
        {
          if (!builder.on_escape(rune.value, pattern, pos))
          {
            set_error(builder.error_code, builder.error_arg);
            return {};
          }
          continue;
        }

        switch (rune.value)
        {
          case Backslash:
            builder.on_backslash();
            break;

          case '.':
            builder.on_dot();
            break;

          case '[':
            if (!builder.on_char_class(pattern, pos))
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          case '^':
            builder.on_start_anchor();
            break;

          case '$':
            builder.on_end_anchor();
            break;

          case ']':
            if (!builder.on_close_bracket())
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          case '{':
            if (!builder.on_open_brace(pattern, pos))
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          case '}':
            if (!builder.on_close_brace())
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          case LParen:
            if (!builder.on_open_group(pattern, pos))
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          case RParen:
            if (!builder.on_close_group())
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          case Pipe:
            builder.on_pipe();
            break;

          case Asterisk:
            if (!builder.on_star(pattern, pos))
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          case Question:
            if (!builder.on_question(pattern, pos))
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          case Plus:
            if (!builder.on_plus(pattern, pos))
            {
              set_error(builder.error_code, builder.error_arg);
              return {};
            }
            break;

          default:
            builder.on_literal(rune.value);
            break;
        }
      }

      if (!builder.finalize())
      {
        set_error(builder.error_code, builder.error_arg);
        return {};
      }

      rune_classes_ = std::move(builder.rune_classes);
      num_captures_ = builder.num_captures;
      return std::move(builder.postfix);
    }

    // Build an NFA fragment for a quantifier (ZeroOrOne, ZeroOrMore,
    // OneOrMore). `lazy` swaps the Split branch ordering so the
    // skip path is preferred over the match path.
    enum class QuantifierKind
    {
      QZeroOrOne,
      QZeroOrMore,
      QOneOrMore
    };

    void
    build_quantifier(std::vector<Frag>& stack, QuantifierKind kind, bool lazy)
    {
      if (stack.empty())
      {
        set_error(ErrorCode::ErrorRepeatOp);
        return;
      }
      auto operand = stack.back();
      stack.pop_back();
      State state;

      // Greedy: next=match, next_alt=skip
      // Lazy:   next=skip,  next_alt=match
      auto unpatched_branch = [&]() -> State* {
        return lazy ? &state->next : &state->next_alt;
      };

      switch (kind)
      {
        case QuantifierKind::QZeroOrOne:
          if (lazy)
            state = create_state(Split, nullptr, operand.start);
          else
            state = create_state(Split, operand.start);
          operand.dangling.push_back(unpatched_branch());
          stack.push_back({state, operand.dangling});
          break;

        case QuantifierKind::QZeroOrMore:
          if (lazy)
            state = create_state(Split, nullptr, operand.start);
          else
            state = create_state(Split, operand.start);
          patch(operand.dangling, state);
          stack.push_back({state, {unpatched_branch()}});
          break;

        case QuantifierKind::QOneOrMore:
          if (lazy)
            state = create_state(Split, nullptr, operand.start);
          else
            state = create_state(Split, operand.start);
          patch(operand.dangling, state);
          stack.push_back({operand.start, {unpatched_branch()}});
          break;
      }
    }

    // Converts postfix tokens to an NFA via Thompson's construction.
    // On malformed postfix (stack underflow), sets malformed=true and
    // returns accept_state as a safe sentinel.
    State postfix_to_nfa(const std::vector<rune_t>& postfix)
    {
      std::vector<Frag> stack;
      if (postfix.empty())
      {
        return accept_state_;
      }

      if (postfix.size() > MaxStates)
      {
        set_error(ErrorCode::ErrorPatternTooLarge);
        return accept_state_;
      }

      Frag left, right, operand;
      State state;

      for (auto& label : postfix)
      {
        switch (label)
        {
          case Catenation:
            if (stack.size() < 2)
            {
              set_error(ErrorCode::ErrorInternalError);
              return accept_state_;
            }
            right = stack.back();
            stack.pop_back();
            left = stack.back();
            stack.pop_back();
            patch(left.dangling, right.start);
            stack.push_back({left.start, right.dangling});
            break;

          case Alternation:
            if (stack.size() < 2)
            {
              set_error(ErrorCode::ErrorInternalError);
              return accept_state_;
            }
            right = stack.back();
            stack.pop_back();
            left = stack.back();
            stack.pop_back();
            state = create_state(Split, left.start, right.start);
            stack.push_back({state, append(left.dangling, right.dangling)});
            break;

          case ZeroOrOne:
          case LazyZeroOrOne:
            build_quantifier(
              stack, QuantifierKind::QZeroOrOne, label == LazyZeroOrOne);
            if (!ok())
              return accept_state_;
            break;

          case ZeroOrMore:
          case LazyZeroOrMore:
            build_quantifier(
              stack, QuantifierKind::QZeroOrMore, label == LazyZeroOrMore);
            if (!ok())
              return accept_state_;
            break;

          case OneOrMore:
          case LazyOneOrMore:
            build_quantifier(
              stack, QuantifierKind::QOneOrMore, label == LazyOneOrMore);
            if (!ok())
              return accept_state_;
            break;

          default:
            // CaptureGroup + idx: wrap top fragment with capture epsilon
            // states.
            if (label >= CaptureGroup && label < CaptureGroup + MaxCaptures)
            {
              if (stack.empty())
              {
                set_error(ErrorCode::ErrorInternalError);
                return accept_state_;
              }
              operand = stack.back();
              stack.pop_back();
              size_t idx = static_cast<size_t>(label - CaptureGroup);
              auto close =
                create_state(CaptureClose + static_cast<rune_t>(idx));
              patch(operand.dangling, close);
              auto open = create_state(
                CaptureOpen + static_cast<rune_t>(idx), operand.start);
              stack.push_back({open, {&close->next}});
              break;
            }
            state = create_state(label);
            stack.push_back({state, {&state->next}});
            break;
        }
      }

      if (stack.size() != 1)
      {
        set_error(ErrorCode::ErrorInternalError);
        return accept_state_;
      }

      patch(stack.back().dangling, accept_state_);
      return stack.back().start;
    }

    bool is_match(const std::vector<State>& states) const
    {
      for (auto& state : states)
      {
        if (state == accept_state_)
        {
          return true;
        }
      }

      return false;
    }

    // Test whether a state was already visited this epoch, and mark it
    // visited if not. Returns true if already visited (caller should skip).
    inline bool
    already_visited(size_t closure_index, size_t epoch, MatchContext& ctx) const
    {
      if (ctx.use_bitset_)
        return ctx.bitset_test_and_set(closure_index);
      if (ctx.is_visited(closure_index, epoch))
        return true;
      ctx.mark_visited(closure_index, epoch);
      return false;
    }

    void add_state(
      std::vector<State>& states,
      State state,
      size_t epoch,
      MatchContext& ctx,
      bool boundary_match = false,
      bool at_start = false,
      bool at_end = false) const
    {
      if (state == nullptr)
        return;

      if (state->trivial_closure)
      {
        if (!already_visited(state->closure_index, epoch, ctx))
          states.push_back(state);
        return;
      }

      auto closure =
        epsilon_closure_cached(state, boundary_match, at_start, at_end);
      for (auto& terminal : closure)
      {
        if (!already_visited(terminal->closure_index, epoch, ctx))
          states.push_back(terminal);
      }
    }

    void start_list(
      std::vector<State>& states,
      State state,
      size_t& epoch,
      MatchContext& ctx,
      bool boundary_match = false,
      bool at_start = false,
      bool at_end = false) const
    {
      if (ctx.use_bitset_)
        ctx.clear_visited_bitset();
      else
        ctx.advance_epoch(epoch);
      states.clear();
      add_state(states, state, epoch, ctx, boundary_match, at_start, at_end);
    }

    void step(
      std::vector<State>& current_states,
      const rune_t& rune,
      std::vector<State>& next_states,
      size_t& epoch,
      MatchContext& ctx,
      bool boundary_match = false,
      bool at_end = false) const
    {
      if (ctx.use_bitset_)
        ctx.clear_visited_bitset();
      else
        ctx.advance_epoch(epoch);
      next_states.clear();
      if (rune < 128)
      {
        for (auto& state : current_states)
        {
          if ((state->ascii_accept[rune >> 6] >> (rune & 63)) & 1)
            add_state(
              next_states,
              state->next,
              epoch,
              ctx,
              boundary_match,
              false,
              at_end);
        }
      }
      else
      {
        for (auto& state : current_states)
        {
          if (is_class_ref(state->label))
          {
            ctx.stats_inc_class_ref_checks();
            if (rune_classes_[class_ref_index(state->label)].contains(rune))
              add_state(
                next_states,
                state->next,
                epoch,
                ctx,
                boundary_match,
                false,
                at_end);
          }
          else if (state->label == rune)
          {
            ctx.stats_inc_literal_checks();
            add_state(
              next_states,
              state->next,
              epoch,
              ctx,
              boundary_match,
              false,
              at_end);
          }
        }
      }
    }

    State start_state_; // Root state of the NFA.
    State accept_state_; // Terminal state; all matching paths end here.
    ErrorCode error_code_; // Error code if pattern failed to parse.
    std::string error_arg_; // Fragment of pattern that caused the error.
    bool has_conditionals_ = false; // True if NFA has anchors or \b.
    size_t num_captures_; // Number of capturing groups.
    size_t state_count_ = 0; // Total states in owned_states_.
    SyntaxMode syntax_mode_ = SyntaxMode::Extended;
    std::vector<StateDef>
      owned_states_; // Pre-reserved contiguous state storage.
    std::vector<RuneClass> rune_classes_; // Indexed by class-ref label offset.
    std::vector<State> closure_cache_flat_;
    std::vector<uint32_t> closure_cache_offsets_;
    FirstCharInfo first_char_info_;

    FirstCharInfo compute_first_char_info() const
    {
      if (!ok())
      {
        return FirstCharInfo::maximal();
      }

      FirstCharInfo info = FirstCharInfo::minimal();

      // Collect all closure states. If has_conditionals_, OR all 8 flag
      // combinations to produce a conservative superset.
      size_t num_combos = has_conditionals_ ? ClosureFlagCombinations : 1;

      for (size_t combo = 0; combo < num_combos; ++combo)
      {
        bool boundary = has_conditionals_ && (combo & FlagBoundary);
        bool at_start = has_conditionals_ ? ((combo & FlagAtStart) != 0) : true;
        bool at_end = has_conditionals_ && (combo & FlagAtEnd);

        auto closure =
          epsilon_closure_cached(start_state_, boundary, at_start, at_end);

        for (auto it = closure.begin(); it != closure.end(); ++it)
        {
          auto s = *it;

          // Accept state: marks empty-matchable, but skip its label
          // (0xAFFFFF) to avoid poisoning can_match_nonascii.
          if (s == accept_state_)
          {
            info.can_match_empty = true;
            continue;
          }

          // OR the ASCII acceptance bitmap.
          info.bitmap[0] |= s->ascii_accept[0];
          info.bitmap[1] |= s->ascii_accept[1];

          // Check for non-ASCII acceptance.
          if (is_class_ref(s->label))
          {
            auto& rc = rune_classes_[class_ref_index(s->label)];
            for (auto& range : rc.ranges)
            {
              if (range.second >= 128)
              {
                info.can_match_nonascii = true;
                break;
              }
            }
          }
          else if (s->label >= 128 && s->label != Match)
          {
            info.can_match_nonascii = true;
          }
        }
      }

      return info;
    }
  };

}

// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cstring>
#include <iostream>
#include <string>
#include <trieste/regex.h>
#include <trieste/regex_engine.h>
#include <vector>

namespace
{
  using namespace trieste::regex;

  struct TestCase
  {
    std::string name;
    std::string pattern;
    std::string input;
    bool expected;
  };

  size_t failures = 0;

  void run(const std::vector<TestCase>& tests)
  {
    for (auto& tc : tests)
    {
      RegexEngine re(tc.pattern);
      bool result = re.match(tc.input);
      if (result != tc.expected)
      {
        std::cerr << "  FAIL: " << tc.name << " — pattern /" << tc.pattern
                  << "/ vs \"" << tc.input << "\"" << " expected "
                  << tc.expected << " got " << result << std::endl;
        failures++;
      }
    }
  }

  void test_literals()
  {
    std::cout << "  literals" << std::endl;
    run({
      {"exact match", "abc", "abc", true},
      {"single char", "x", "x", true},
      {"mismatch", "abc", "abd", false},
      {"too short", "abc", "ab", false},
      {"too long", "ab", "abc", false},
    });
  }

  void test_alternation()
  {
    std::cout << "  alternation" << std::endl;
    run({
      {"left branch", "a|b", "a", true},
      {"right branch", "a|b", "b", true},
      {"neither branch", "a|b", "c", false},
      {"multi-char left", "ab|cd", "ab", true},
      {"multi-char right", "ab|cd", "cd", true},
      {"multi-char neither", "ab|cd", "ac", false},
    });
  }

  void test_empty_alternatives()
  {
    std::cout << "  empty alternatives" << std::endl;
    // The engine does not currently support empty alternatives.
    // Verify they are rejected cleanly (not a crash or hang).
    auto check_rejected = [](const char* pattern) {
      RegexEngine re(pattern);
      if (re.ok())
      {
        std::cerr << "  FAIL: pattern /" << pattern
                  << "/ should be rejected (empty alternative)" << std::endl;
        failures++;
      }
      if (re.match(""))
      {
        std::cerr << "  FAIL: rejected pattern /" << pattern
                  << "/ should not match \"\"" << std::endl;
        failures++;
      }
    };
    check_rejected("(|a)");
    check_rejected("(a|)");
    check_rejected("(a||b)");
    check_rejected("(|)");
    check_rejected("(||)");
    check_rejected("a|");
    check_rejected("|a");
    check_rejected("|");
  }

  void test_zero_or_one()
  {
    std::cout << "  zero_or_one (?)" << std::endl;
    run({
      {"present", "ab?c", "abc", true},
      {"absent", "ab?c", "ac", true},
      {"too many", "ab?c", "abbc", false},
    });
  }

  void test_zero_or_more()
  {
    std::cout << "  zero_or_more (*)" << std::endl;
    run({
      {"zero occurrences", "ab*c", "ac", true},
      {"one occurrence", "ab*c", "abc", true},
      {"many occurrences", "ab*c", "abbbc", true},
      {"wrong char", "ab*c", "adc", false},
      {"standalone star", "a*", "", true},
      {"standalone star match", "a*", "aaa", true},
    });
  }

  void test_one_or_more()
  {
    std::cout << "  one_or_more (+)" << std::endl;
    run({
      {"zero occurrences fails", "ab+c", "ac", false},
      {"one occurrence", "ab+c", "abc", true},
      {"many occurrences", "ab+c", "abbbc", true},
      {"standalone plus empty", "a+", "", false},
      {"standalone plus one", "a+", "a", true},
      {"standalone plus many", "a+", "aaa", true},
    });
  }

  void test_grouping()
  {
    std::cout << "  grouping" << std::endl;
    run({
      {"group with star", "(ab)*c", "c", true},
      {"group with star once", "(ab)*c", "abc", true},
      {"group with star twice", "(ab)*c", "ababc", true},
      {"group with plus", "(ab)+c", "c", false},
      {"group with plus once", "(ab)+c", "abc", true},
      {"group with alt", "(a|b)c", "ac", true},
      {"group with alt 2", "(a|b)c", "bc", true},
    });
  }

  void test_escaped_operators()
  {
    std::cout << "  escaped operators" << std::endl;
    run({
      {"escaped star", "a\\*b", "a*b", true},
      {"escaped star no match", "a\\*b", "aab", false},
      {"escaped plus", "a\\+b", "a+b", true},
      {"escaped question", "a\\?b", "a?b", true},
      {"escaped pipe", "a\\|b", "a|b", true},
      {"escaped paren", "\\(a\\)", "(a)", true},
      {"escaped backslash", "a\\\\b", "a\\b", true},
    });
  }

  void test_empty_pattern()
  {
    std::cout << "  empty pattern" << std::endl;
    run({
      {"empty matches empty", "", "", true},
      {"empty rejects non-empty", "", "a", false},
    });
  }

  void test_malformed_patterns()
  {
    std::cout << "  malformed patterns" << std::endl;
    auto check_malformed = [](const char* pattern) {
      RegexEngine re(pattern);
      if (re.ok())
      {
        std::cerr << "  FAIL: pattern /" << pattern << "/ should be malformed"
                  << std::endl;
        failures++;
      }
      if (re.match(""))
      {
        std::cerr << "  FAIL: malformed pattern /" << pattern
                  << "/ should not match \"\"" << std::endl;
        failures++;
      }
    };
    check_malformed("*");
    check_malformed("+");
    check_malformed("?");
    check_malformed(")");
    check_malformed("(");
    check_malformed("a)");
    check_malformed("(a");

    // Consecutive quantifiers.
    check_malformed("a**");
    check_malformed("a++");
    check_malformed("a*+");
    check_malformed("a+*");
    check_malformed("a?*");
    check_malformed("a?+");
    check_malformed("a*?*");
    check_malformed("a{2}*");
    check_malformed("a{2}+");
    check_malformed("a{2}?");

    // Nesting depth limit.
    std::string deep(257, '(');
    deep += "a";
    deep += std::string(257, ')');
    check_malformed(deep.c_str());
  }

  void test_combined()
  {
    std::cout << "  combined patterns" << std::endl;
    run({
      {"complex 1", "a(b|c)*d", "ad", true},
      {"complex 2", "a(b|c)*d", "abd", true},
      {"complex 3", "a(b|c)*d", "abcd", true},
      {"complex 4", "a(b|c)*d", "abcbcd", true},
      {"complex 5", "a(b|c)*d", "aed", false},
      {"optional group", "a(bc)?d", "ad", true},
      {"optional group present", "a(bc)?d", "abcd", true},
    });
  }

  void test_dot()
  {
    std::cout << "  dot (.)" << std::endl;
    run({
      {"dot matches letter", "a.c", "abc", true},
      {"dot matches digit", "a.c", "a1c", true},
      {"dot matches space", "a.c", "a c", true},
      {"dot matches newline", "a.c", "a\nc", true},
      {"dot matches carriage return", "a.c", "a\rc", true},
      {"dot matches tab", "a.c", "a\tc", true},
      {"dot no match empty", ".", "", false},
      {"dot matches one", ".", "x", true},
      {"dot star", ".*", "hello", true},
      {"dot star empty", ".*", "", true},
    });
    // Multi-byte UTF-8 character (é = U+00E9, 2 bytes).
    run(
      {{"dot matches multibyte",
        "a.c",
        "a\xC3\xA9"
        "c",
        true}});
    // 3-byte UTF-8 (中 = U+4E2D).
    run({{"dot matches 3byte", ".", "\xE4\xB8\xAD", true}});
    // 4-byte UTF-8 (𝄞 = U+1D11E).
    run({{"dot matches 4byte", ".", "\xF0\x9D\x84\x9E", true}});
  }

  void test_single_char_escapes()
  {
    std::cout << "  single-char escapes" << std::endl;
    run({
      {"\\n matches newline", "a\\nb", "a\nb", true},
      {"\\n no match literal n", "a\\nb", "anb", false},
      {"\\r matches CR", "\\r", "\r", true},
      {"\\t matches tab", "\\t", "\t", true},
      {"\\- matches literal dash", "\\-", "-", true},
      {"\\. matches literal dot", "\\.", ".", true},
      {"\\. no match letter", "\\.", "a", false},
      {"\\^ matches caret", "\\^", "^", true},
      {"\\{ matches brace", "\\{", "{", true},
      {"\\} matches brace", "\\}", "}", true},
      {"\\[ matches bracket", "\\[", "[", true},
      {"\\] matches bracket", "\\]", "]", true},
      // RE2-compatible standalone literals.
      {"bare { matches brace", "{", "{", true},
      {"bare } matches brace", "}", "}", true},
      {"bare ] matches bracket", "]", "]", true},
    });
  }

  void test_character_classes()
  {
    std::cout << "  character classes ([...])" << std::endl;
    run({
      {"simple class", "[abc]", "a", true},
      {"simple class b", "[abc]", "b", true},
      {"simple class miss", "[abc]", "d", false},
      {"range class", "[a-z]", "m", true},
      {"range class edge lo", "[a-z]", "a", true},
      {"range class edge hi", "[a-z]", "z", true},
      {"range class miss", "[a-z]", "A", false},
      {"negated class", "[^a-z]", "A", true},
      {"negated class miss", "[^a-z]", "m", false},
      {"leading dash", "[-a]", "-", true},
      {"leading dash a", "[-a]", "a", true},
      {"trailing dash", "[a-]", "-", true},
      {"trailing dash a", "[a-]", "a", true},
      {"class with escape", "[\\n]", "\n", true},
      {"class with escape miss", "[\\n]", "n", false},
      {"multi range", "[a-zA-Z]", "Z", true},
      {"multi range miss", "[a-zA-Z]", "5", false},
      {"class in pattern", "x[abc]y", "xby", true},
      {"class in pattern miss", "x[abc]y", "xdy", false},
    });
  }

  void test_unicode_categories()
  {
    std::cout << "  unicode categories (\\p{}/\\P{})" << std::endl;
    run({
      // Lu — uppercase letter.
      {"\\p{Lu} matches A", "\\p{Lu}", "A", true},
      {"\\p{Lu} rejects a", "\\p{Lu}", "a", false},
      // Ll — lowercase letter.
      {"\\p{Ll} matches a", "\\p{Ll}", "a", true},
      {"\\p{Ll} rejects A", "\\p{Ll}", "A", false},
      // Nd — decimal digit.
      {"\\p{Nd} matches 5", "\\p{Nd}", "5", true},
      {"\\p{Nd} rejects a", "\\p{Nd}", "a", false},
      // L — letter (major category).
      {"\\p{L} matches a", "\\p{L}", "a", true},
      {"\\p{L} matches A", "\\p{L}", "A", true},
      {"\\p{L} rejects 5", "\\p{L}", "5", false},
      // \P{} — negation.
      {"\\P{L} rejects a", "\\P{L}", "a", false},
      {"\\P{L} matches 5", "\\P{L}", "5", true},
    });
    // Multi-byte: é (U+00E9) is Ll.
    run({{"\\p{Ll} matches e-acute", "\\p{Ll}", "\xC3\xA9", true}});
    // 中 (U+4E2D) is Lo.
    run({{"\\p{Lo} matches CJK", "\\p{Lo}", "\xE4\xB8\xAD", true}});
    // Category in character class.
    run({{"[\\p{L}0-9] matches a", "[\\p{L}0-9]", "a", true}});
    run({{"[\\p{L}0-9] matches 5", "[\\p{L}0-9]", "5", true}});
    run({{"[\\p{L}0-9] rejects !", "[\\p{L}0-9]", "!", false}});
  }

  void test_range_quantifiers()
  {
    std::cout << "  range quantifiers ({n,m})" << std::endl;
    run({
      // Exact count {n}.
      {"a{3} matches aaa", "a{3}", "aaa", true},
      {"a{3} rejects aa", "a{3}", "aa", false},
      {"a{3} rejects aaaa", "a{3}", "aaaa", false},
      {"a{1} matches a", "a{1}", "a", true},
      // Range {n,m}.
      {"a{2,4} matches aa", "a{2,4}", "aa", true},
      {"a{2,4} matches aaa", "a{2,4}", "aaa", true},
      {"a{2,4} matches aaaa", "a{2,4}", "aaaa", true},
      {"a{2,4} rejects a", "a{2,4}", "a", false},
      {"a{2,4} rejects aaaaa", "a{2,4}", "aaaaa", false},
      // Unbounded {n,}.
      {"a{2,} matches aa", "a{2,}", "aa", true},
      {"a{2,} matches aaaaaaa", "a{2,}", "aaaaaaa", true},
      {"a{2,} rejects a", "a{2,}", "a", false},
      // Zero minimum.
      {"a{0,3} matches empty", "a{0,3}", "", true},
      {"a{0,3} matches a", "a{0,3}", "a", true},
      {"a{0,3} matches aaa", "a{0,3}", "aaa", true},
      {"a{0,3} rejects aaaa", "a{0,3}", "aaaa", false},
      {"a{0,} matches empty", "a{0,}", "", true},
      {"a{0,} matches aaa", "a{0,}", "aaa", true},
      // {0} — removes atom.
      {"a{0} matches empty", "a{0}", "", true},
      {"a{0} rejects a", "a{0}", "a", false},
      {"xa{0}y matches xy", "xa{0}y", "xy", true},
      // Group quantifiers.
      {"(ab){2} matches abab", "(ab){2}", "abab", true},
      {"(ab){2} rejects ab", "(ab){2}", "ab", false},
      {"(a|b){3} matches aba", "(a|b){3}", "aba", true},
      // At repetition limit.
      {"a{1000} valid", "a{1000}", std::string(1000, 'a'), true},
      // Deep nesting.
      {"deep nest", "((((((((((a))))))))))", "a", true},
      // Negated class combinations.
      {"[^a][^b] matches ba", "[^a][^b]", "ba", true},
      {"[^a][^b] rejects ab", "[^a][^b]", "ab", false},
      // Combined unicode categories in class.
      {"[\\p{L}\\p{N}] matches a", "[\\p{L}\\p{N}]", "a", true},
      {"[\\p{L}\\p{N}] matches 5", "[\\p{L}\\p{N}]", "5", true},
      {"[\\p{L}\\p{N}] rejects !", "[\\p{L}\\p{N}]", "!", false},
    });
  }

  void test_posix_classes()
  {
    std::cout << "  POSIX character classes" << std::endl;
    run({
      // [:alpha:]
      {"[[:alpha:]] matches a", "[[:alpha:]]", "a", true},
      {"[[:alpha:]] matches Z", "[[:alpha:]]", "Z", true},
      {"[[:alpha:]] rejects 1", "[[:alpha:]]", "1", false},
      {"[[:alpha:]] rejects space", "[[:alpha:]]", " ", false},
      // [:digit:]
      {"[[:digit:]] matches 0", "[[:digit:]]", "0", true},
      {"[[:digit:]] matches 9", "[[:digit:]]", "9", true},
      {"[[:digit:]] rejects a", "[[:digit:]]", "a", false},
      // [:alnum:]
      {"[[:alnum:]] matches a", "[[:alnum:]]", "a", true},
      {"[[:alnum:]] matches 5", "[[:alnum:]]", "5", true},
      {"[[:alnum:]] rejects !", "[[:alnum:]]", "!", false},
      // [:blank:]
      {"[[:blank:]] matches space", "[[:blank:]]", " ", true},
      {"[[:blank:]] matches tab", "[[:blank:]]", "\t", true},
      {"[[:blank:]] rejects newline", "[[:blank:]]", "\n", false},
      // [:space:]
      {"[[:space:]] matches space", "[[:space:]]", " ", true},
      {"[[:space:]] matches tab", "[[:space:]]", "\t", true},
      {"[[:space:]] matches newline", "[[:space:]]", "\n", true},
      {"[[:space:]] rejects a", "[[:space:]]", "a", false},
      // [:xdigit:]
      {"[[:xdigit:]] matches 0", "[[:xdigit:]]", "0", true},
      {"[[:xdigit:]] matches f", "[[:xdigit:]]", "f", true},
      {"[[:xdigit:]] matches A", "[[:xdigit:]]", "A", true},
      {"[[:xdigit:]] rejects g", "[[:xdigit:]]", "g", false},
      // [:upper:] and [:lower:]
      {"[[:upper:]] matches A", "[[:upper:]]", "A", true},
      {"[[:upper:]] rejects a", "[[:upper:]]", "a", false},
      {"[[:lower:]] matches a", "[[:lower:]]", "a", true},
      {"[[:lower:]] rejects A", "[[:lower:]]", "A", false},
      // [:print:] and [:graph:]
      {"[[:print:]] matches space", "[[:print:]]", " ", true},
      {"[[:print:]] matches A", "[[:print:]]", "A", true},
      {"[[:graph:]] rejects space", "[[:graph:]]", " ", false},
      {"[[:graph:]] matches A", "[[:graph:]]", "A", true},
      // [:cntrl:]
      {"[[:cntrl:]] matches NUL", "[[:cntrl:]]", std::string(1, '\0'), true},
      {"[[:cntrl:]] matches DEL", "[[:cntrl:]]", "\x7F", true},
      {"[[:cntrl:]] rejects A", "[[:cntrl:]]", "A", false},
      // [:punct:]
      {"[[:punct:]] matches !", "[[:punct:]]", "!", true},
      {"[[:punct:]] matches ~", "[[:punct:]]", "~", true},
      {"[[:punct:]] rejects a", "[[:punct:]]", "a", false},
      // [:ascii:]
      {"[[:ascii:]] matches NUL", "[[:ascii:]]", std::string(1, '\0'), true},
      {"[[:ascii:]] matches DEL", "[[:ascii:]]", "\x7F", true},
      // Combined with other ranges.
      {"[[:digit:]a-f] matches 5", "[[:digit:]a-f]", "5", true},
      {"[[:digit:]a-f] matches c", "[[:digit:]a-f]", "c", true},
      {"[[:digit:]a-f] rejects g", "[[:digit:]a-f]", "g", false},
      // Negated POSIX class.
      {"[^[:digit:]] matches a", "[^[:digit:]]", "a", true},
      {"[^[:digit:]] rejects 5", "[^[:digit:]]", "5", false},
      // Multiple POSIX classes in one bracket.
      {"[[:alpha:][:digit:]] a", "[[:alpha:][:digit:]]", "a", true},
      {"[[:alpha:][:digit:]] 5", "[[:alpha:][:digit:]]", "5", true},
      {"[[:alpha:][:digit:]] !", "[[:alpha:][:digit:]]", "!", false},
      // Quantified.
      {"[[:digit:]]+ matches 123", "[[:digit:]]+", "123", true},
      {"[[:blank:]]+ matches spaces", "[[:blank:]]+", "  \t ", true},
      // Used in real patterns.
      {"identifier", "[_[:alpha:]][_[:alnum:]]*", "hello_world", true},
      {"identifier rejects 1abc", "[_[:alpha:]][_[:alnum:]]*", "1abc", false},
    });

    // Malformed POSIX class.
    auto check_malformed = [](const char* pattern, const char* name) {
      RegexEngine re(pattern);
      if (re.ok())
      {
        std::cerr << "  FAIL: " << name << " — pattern /" << pattern
                  << "/ should be malformed" << std::endl;
        failures++;
      }
    };
    check_malformed("[[:bogus:]]", "unknown POSIX class [:bogus:]");
    check_malformed("[[:alpha]", "unterminated POSIX class");
  }

  void test_shorthand_escapes()
  {
    std::cout << "  shorthand escapes (\\d \\s \\w)" << std::endl;
    run({
      // \d = [0-9]
      {"\\d matches 5", "\\d", "5", true},
      {"\\d rejects a", "\\d", "a", false},
      {"\\d+ matches 123", "\\d+", "123", true},
      // \D = [^0-9]
      {"\\D matches a", "\\D", "a", true},
      {"\\D rejects 5", "\\D", "5", false},
      // \s = [ \t\n\r\f\v]
      {"\\s matches space", "\\s", " ", true},
      {"\\s matches tab", "\\s", "\t", true},
      {"\\s matches newline", "\\s", "\n", true},
      {"\\s rejects a", "\\s", "a", false},
      // \S = [^ \t\n\r\f\v]
      {"\\S matches a", "\\S", "a", true},
      {"\\S rejects space", "\\S", " ", false},
      // \w = [_0-9A-Za-z]
      {"\\w matches a", "\\w", "a", true},
      {"\\w matches _", "\\w", "_", true},
      {"\\w matches 5", "\\w", "5", true},
      {"\\w rejects !", "\\w", "!", false},
      // \W = [^_0-9A-Za-z]
      {"\\W matches !", "\\W", "!", true},
      {"\\W rejects a", "\\W", "a", false},
      // Inside character classes.
      {"[\\d] matches 5", "[\\d]", "5", true},
      {"[\\d] rejects a", "[\\d]", "a", false},
      {"[\\da-f] matches c", "[\\da-f]", "c", true},
      {"[\\da-f] matches 5", "[\\da-f]", "5", true},
      {"[\\da-f] rejects g", "[\\da-f]", "g", false},
      // Negated shorthand inside class.
      {"[^\\d] matches a", "[^\\d]", "a", true},
      {"[^\\d] rejects 5", "[^\\d]", "5", false},
      // Combined in pattern.
      {"\\w+\\s+\\d+", "\\w+\\s+\\d+", "abc 123", true},
      {"[\\s\\S] matches any", "[\\s\\S]", "x", true},
    });
  }

  void test_hex_escapes()
  {
    std::cout << "  hex escapes (\\xNN)" << std::endl;
    run({
      // Basic hex escape.
      {"\\x41 matches A", "\\x41", "A", true},
      {"\\x41 rejects B", "\\x41", "B", false},
      {"\\x61 matches a", "\\x61", "a", true},
      {"\\x00 matches NUL", "\\x00", std::string(1, '\0'), true},
      {"\\x7F matches DEL", "\\x7F", "\x7F", true},
      // In character class.
      {"[\\x41-\\x5A] matches A", "[\\x41-\\x5A]", "A", true},
      {"[\\x41-\\x5A] matches Z", "[\\x41-\\x5A]", "Z", true},
      {"[\\x41-\\x5A] rejects a", "[\\x41-\\x5A]", "a", false},
      // JSON pattern: [^\x00-\x1F]
      {"[^\\x00-\\x1F] matches space", "[^\\x00-\\x1F]", " ", true},
      {"[^\\x00-\\x1F] rejects tab", "[^\\x00-\\x1F]", "\t", false},
      {"[^\\x00-\\x1F] rejects NUL",
       "[^\\x00-\\x1F]",
       std::string(1, '\0'),
       false},
      // Combined with literals.
      {"a\\x2Db matches a-b", "a\\x2Db", "a-b", true},
    });

    // Malformed hex escapes.
    auto check_malformed = [](const char* pattern, const char* name) {
      RegexEngine re(pattern);
      if (re.ok())
      {
        std::cerr << "  FAIL: " << name << " — pattern /" << pattern
                  << "/ should be malformed" << std::endl;
        failures++;
      }
    };
    check_malformed("\\x", "\\x with no digits");
    check_malformed("\\x4", "\\x with only 1 digit");
    check_malformed("\\xGG", "\\x with non-hex digits");
  }

  void test_non_capturing_groups()
  {
    std::cout << "  non-capturing groups (?:...)" << std::endl;
    run({
      // Basic non-capturing group.
      {"(?:abc) matches abc", "(?:abc)", "abc", true},
      {"(?:abc) rejects ab", "(?:abc)", "ab", false},
      // Alternation inside.
      {"(?:a|b) matches a", "(?:a|b)", "a", true},
      {"(?:a|b) matches b", "(?:a|b)", "b", true},
      {"(?:a|b) rejects c", "(?:a|b)", "c", false},
      // Quantified non-capturing group.
      {"(?:ab)+ matches abab", "(?:ab)+", "abab", true},
      {"(?:ab)+ rejects empty", "(?:ab)+", "", false},
      {"(?:ab)* matches empty", "(?:ab)*", "", true},
      {"(?:ab){2} matches abab", "(?:ab){2}", "abab", true},
      {"(?:ab){2} rejects ab", "(?:ab){2}", "ab", false},
      // Nested non-capturing groups.
      {"(?:(?:a)b) matches ab", "(?:(?:a)b)", "ab", true},
      // Mixed with regular groups.
      {"(a)(?:b)(c) matches abc", "(a)(?:b)(c)", "abc", true},
      // Real-world pattern from infix.
      {
        "float pattern",
        "[[:digit:]]+\\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?",
        "123.456e+7",
        true,
      },
      {
        "float without exp",
        "[[:digit:]]+\\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?",
        "123.456",
        true,
      },
    });
  }

  void test_real_world_parser_patterns()
  {
    std::cout << "  real-world parser patterns" << std::endl;

    // Patterns used in Trieste parsers and downstream rego-cpp/verona-bc
    // parsers.
    run({
      {
        "json number with exponent",
        R"(-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)",
        "-12.5e+6",
        true,
      },
      {
        "json number rejects leading zero",
        R"(-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)",
        "01",
        false,
      },
      {
        "rego exponent number",
        R"(-?(?:0|[1-9][0-9]*)[eE][-+]?[0-9]+)",
        "6e-1",
        true,
      },
      {
        "json string with unicode escape",
        R"("(?:[^"\\\x00-\x1F]+|\\["\\\/bfnrt]|\\u[[:xdigit:]]{4})*")",
        R"("hi\u0041")",
        true,
      },
      {
        "json string rejects control newline",
        R"("(?:[^"\\\x00-\x1F]+|\\["\\\/bfnrt]|\\u[[:xdigit:]]{4})*")",
        "\"line\n\"",
        false,
      },
      {
        "rego raw backtick string",
        R"(`[^`]*`)",
        "`raw_value`",
        true,
      },
      {
        "rego raw backtick string malformed",
        R"(`[^`]*`)",
        "`raw_value",
        false,
      },
      {
        "rego comment with lf",
        R"(#[^\r\n]*\r?\n)",
        "# comment\n",
        true,
      },
      {
        "rego comment with crlf",
        R"(#[^\r\n]*\r?\n)",
        "# comment\r\n",
        true,
      },
      {
        "rego comment requires newline",
        R"(#[^\r\n]*\r?\n)",
        "# comment",
        false,
      },
      {
        "rego identifier",
        R"((?:[[:alpha:]]|_)(?:[[:alnum:]]|_)*\b)",
        "hello_42",
        true,
      },
      {
        "rego identifier rejects leading digit",
        R"((?:[[:alpha:]]|_)(?:[[:alnum:]]|_)*\b)",
        "1hello",
        false,
      },
      {
        "operator alternation",
        "==|!=|<=|>=",
        "<=",
        true,
      },
      {
        "open brace optional newline",
        R"({(?:\r?\n)?)",
        "{\n",
        true,
      },
      {
        "open brace optional newline no newline",
        R"({(?:\r?\n)?)",
        "{",
        true,
      },
      {
        "open brace optional newline rejects trailing text",
        R"({(?:\r?\n)?)",
        "{x",
        false,
      },
      {
        "tregex helper hd",
        R"([[:space:]]*\([[:space:]]*([^[:space:]\(\)]*))",
        " (head",
        true,
      },
      {
        "tregex helper st",
        R"([[:space:]]*\{[^\}]*\})",
        " {abc}",
        true,
      },
      {
        "tregex helper id",
        R"([[:space:]]*([[:digit:]]+):)",
        " 123:",
        true,
      },
      {
        "tregex helper tl",
        R"([[:space:]]*\))",
        " )",
        true,
      },
      {
        "verona global id",
        R"(\@[_[:alnum:]]*)",
        "@global_42",
        true,
      },
      {
        "verona local id",
        R"(\$[_[:alnum:]]*)",
        "$local_7",
        true,
      },
      {
        "verona label id",
        R"(\^[_[:alnum:]]*)",
        "^lbl9",
        true,
      },
      {
        "verona hex float",
        R"([-]?0x[_[:xdigit:]]+\.[_[:xdigit:]]+(?:p[+-][_[:digit:]]+)?\b)",
        "-0x1a.fp+7",
        true,
      },
      {
        "verona binary int",
        R"(0b[_01]+\b)",
        "0b101_010",
        true,
      },
      {
        "verona octal int",
        R"(0o[_01234567]+\b)",
        "0o7012",
        true,
      },
      {
        "verona escaped string",
        "\"((?:\\\\\"|[^\"])*?)\"",
        "\"a\\\"b\"",
        true,
      },
      {
        "verona char literal",
        R"('((?:\\'|[^'])*)')",
        "'x'",
        true,
      },
      {
        "verona line comment",
        R"(//[^\r\n]*)",
        "// comment",
        true,
      },
      {
        "vc raw string opener",
        "([']+)\"([^\"]*)",
        "''\"raw text",
        true,
      },
      {
        "vc symbol id starts with equals",
        R"([=#][!#$%&*+-/<=>?@\^`|~]+)",
        "=+=>",
        true,
      },
      {
        "vc symbol id operator class",
        R"([!#$%&*+-/<=>?@\^`|~]+)",
        "->>",
        true,
      },
      {
        "vc nested comment start",
        R"(/\*)",
        "/*",
        true,
      },
      {
        "vc nested comment end",
        R"(\*/)",
        "*/",
        true,
      },
      {
        "vc nested comment body chunk",
        R"([^/\*]+)",
        "comment-body",
        true,
      },
      {
        "vc nested comment single slash-or-star",
        R"([/\*])",
        "*",
        true,
      },
      {
        "vc triple colon",
        ":::",
        ":::",
        true,
      },
      {
        "vc vararg token",
        R"(\.\.\.)",
        "...",
        true,
      },
    });

    // Confirm capture behavior for regex.h helper patterns that rely on group
    // extraction.
    {
      RegexEngine hd(R"([[:space:]]*\([[:space:]]*([^[:space:]\(\)]*))");
      std::vector<RegexEngine::Capture> captures;
      std::string input = " (   header tail";
      size_t len = hd.find_prefix(input, captures);
      if (len == RegexEngine::npos || captures.size() != 1)
      {
        std::cerr << "  FAIL: tregex helper hd capture shape" << std::endl;
        failures++;
      }
      else
      {
        std::string got =
          input.substr(captures[0].start, captures[0].end - captures[0].start);
        if (got != "header")
        {
          std::cerr << "  FAIL: tregex helper hd capture expected header got "
                    << got << std::endl;
          failures++;
        }
      }
    }

    {
      RegexEngine id(R"([[:space:]]*([[:digit:]]+):)");
      std::vector<RegexEngine::Capture> captures;
      std::string input = "  987: rest";
      size_t len = id.find_prefix(input, captures);
      if (len != 6 || captures.size() != 1)
      {
        std::cerr << "  FAIL: tregex helper id capture shape" << std::endl;
        failures++;
      }
      else
      {
        std::string got =
          input.substr(captures[0].start, captures[0].end - captures[0].start);
        if (got != "987")
        {
          std::cerr << "  FAIL: tregex helper id capture expected 987 got "
                    << got << std::endl;
          failures++;
        }
      }
    }
  }

  void test_find_prefix()
  {
    std::cout << "  prefix matching" << std::endl;
    auto check = [](
                   const char* name,
                   const char* pattern,
                   const char* input,
                   size_t expected) {
      RegexEngine re(pattern);
      size_t result = re.find_prefix(input);
      if (result != expected)
      {
        std::cerr << "  FAIL: " << name << " — pattern /" << pattern
                  << "/ vs \"" << input << "\"" << " expected " << expected
                  << " got " << result << std::endl;
        failures++;
      }
    };
    auto npos = RegexEngine::npos;

    // Exact match = prefix match of full length.
    check("abc exact", "abc", "abcdef", 3);
    // No match.
    check("abc no match", "abc", "xyzabc", npos);
    // Greedy: longest prefix.
    check("a+ greedy", "a+", "aaabbb", 3);
    check("a* matches empty prefix", "a*", "bbb", 0);
    // Zero-length match.
    check("a? on b", "a?", "b", 0);
    check("empty pattern", "", "hello", 0);
    // Digit sequence.
    check("[[:digit:]]+ prefix", "[[:digit:]]+", "123abc", 3);
    // No prefix match.
    check("[[:digit:]]+ no prefix", "[[:digit:]]+", "abc", npos);
    // Float tokenization pattern.
    check(
      "float prefix",
      "[[:digit:]]+\\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?",
      "3.14 rest",
      4);
    check(
      "float with exp",
      "[[:digit:]]+\\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?",
      "1.5e+3 rest",
      6);
    // Identifier pattern.
    check("ident prefix", "[_[:alpha:]][_[:alnum:]]*", "hello_world = 5", 11);
    // Malformed pattern returns npos.
    {
      RegexEngine re("*bad");
      if (re.find_prefix("anything") != npos)
      {
        std::cerr << "  FAIL: malformed find_prefix should return npos"
                  << std::endl;
        failures++;
      }
    }
  }

  void test_capturing_groups()
  {
    std::cout << "  capturing groups" << std::endl;
    auto npos = RegexEngine::npos;

    // num_captures tracking.
    {
      RegexEngine re0("abc");
      if (re0.num_captures() != 0)
      {
        std::cerr << "  FAIL: abc has 0 captures, got " << re0.num_captures()
                  << std::endl;
        failures++;
      }
      RegexEngine re1("(a)(b)");
      if (re1.num_captures() != 2)
      {
        std::cerr << "  FAIL: (a)(b) has 2 captures, got " << re1.num_captures()
                  << std::endl;
        failures++;
      }
      RegexEngine re2("(a)(?:b)(c)");
      if (re2.num_captures() != 2)
      {
        std::cerr << "  FAIL: (a)(?:b)(c) has 2 captures, got "
                  << re2.num_captures() << std::endl;
        failures++;
      }
    }

    // Capture-aware find_prefix.
    auto check_cap =
      [&](
        const char* name,
        const char* pattern,
        const char* input,
        size_t expected_len,
        const std::vector<std::pair<size_t, size_t>>& expected_caps) {
        RegexEngine re(pattern);
        std::vector<RegexEngine::Capture> caps;
        size_t len = re.find_prefix(input, caps);
        if (len != expected_len)
        {
          std::cerr << "  FAIL: " << name << " — len expected " << expected_len
                    << " got " << len << std::endl;
          failures++;
          return;
        }
        if (len == npos)
          return;

        if (caps.size() != expected_caps.size())
        {
          std::cerr << "  FAIL: " << name << " — num caps expected "
                    << expected_caps.size() << " got " << caps.size()
                    << std::endl;
          failures++;
          return;
        }
        for (size_t i = 0; i < expected_caps.size(); i++)
        {
          if (
            caps[i].start != expected_caps[i].first ||
            caps[i].end != expected_caps[i].second)
          {
            std::cerr << "  FAIL: " << name << " — cap[" << i << "] expected ("
                      << expected_caps[i].first << ","
                      << expected_caps[i].second << ") got (" << caps[i].start
                      << "," << caps[i].end << ")" << std::endl;
            failures++;
          }
        }
      };

    // Single capture group.
    check_cap("(a)b", "(a)b", "ab", 2, {{0, 1}});
    // Two capture groups.
    check_cap("(a)(b)", "(a)(b)", "ab", 2, {{0, 1}, {1, 2}});
    // Non-capturing doesn't produce capture.
    check_cap("(?:a)(b)", "(?:a)(b)", "ab", 2, {{1, 2}});
    // Capture with prefix extra.
    check_cap("(\\d+) rest", "(\\d+)", "123abc", 3, {{0, 3}});
    // Nested captures: outer is group 1, inner is group 2.
    check_cap("((a))", "((a))", "a", 1, {{0, 1}, {0, 1}});
    // Alternation in capture.
    check_cap("(a|b)", "(a|b)", "b", 1, {{0, 1}});
    // No match — no captures.
    check_cap("(a)b no match", "(a)b", "cd", npos, {});
    // Pattern from parse.h: parenthesized group with capture.
    check_cap("paren capture", "(\\()[[:blank:]]*", "( ", 2, {{0, 1}});

    // --- Nested captures and mixed group types ---

    // Multi-level nested captures with distinct spans.
    // ((a)(b)): group 1 = outer "ab", group 2 = "a", group 3 = "b".
    check_cap(
      "outer captures both inner groups",
      "((a)(b))",
      "ab",
      2,
      {{0, 2}, {0, 1}, {1, 2}});

    // Triple nesting: (((a))).
    check_cap(
      "triple nesting same span", "(((a)))", "a", 1, {{0, 1}, {0, 1}, {0, 1}});

    // Nested with surrounding literal: ((a(b))c).
    // group 1 = "abc", group 2 = "ab", group 3 = "b".
    check_cap(
      "nested capture with surrounding literal",
      "((a(b))c)",
      "abc",
      3,
      {{0, 3}, {0, 2}, {1, 2}});

    // Non-capturing wrapping capturing: ((?:a)(b)).
    // group 1 = outer "ab", group 2 = "b" only ((?:a) is not captured).
    check_cap(
      "non-capturing wraps capturing", "((?:a)(b))", "ab", 2, {{0, 2}, {1, 2}});

    // Capturing wrapping non-capturing: ((?:ab)).
    // group 1 = "ab".
    check_cap("capturing wraps non-capturing", "((?:ab))", "ab", 2, {{0, 2}});

    // Non-capturing between two captures: (a)(?:x)(b).
    // group 1 = "a", group 2 = "b".
    check_cap(
      "non-capturing between two captures",
      "(a)(?:x)(b)",
      "axb",
      3,
      {{0, 1}, {2, 3}});

    // Nested non-capturing inside capturing with quantifier.
    // ((?:ab)+): group 1 = "ababab".
    check_cap(
      "quantified non-capturing inside capturing",
      "((?:ab)+)",
      "ababab",
      6,
      {{0, 6}});

    // Multiple levels of non-capturing inside capturing.
    // ((?:(?:a)b)c): group 1 = "abc".
    check_cap(
      "double non-capturing inside capturing",
      "((?:(?:a)b)c)",
      "abc",
      3,
      {{0, 3}});

    // Alternation inside nested captures.
    // ((a|b)(c|d)): group 1 = "bd", group 2 = "b", group 3 = "d".
    check_cap(
      "alternation inside nested captures",
      "((a|b)(c|d))",
      "bd",
      2,
      {{0, 2}, {0, 1}, {1, 2}});

    // Nested capture with quantifier on inner group.
    // ((a)+): group 1 = "aaa", group 2 = last "a" captured.
    check_cap(
      "quantified inner capture takes last match",
      "((a)+)",
      "aaa",
      3,
      {{0, 3}, {2, 3}});

    // --- num_captures with mixed nesting ---
    {
      // 3 capturing groups nested among non-capturing.
      RegexEngine re("(a)(?:(b)(?:c)(d))");
      if (re.num_captures() != 3)
      {
        std::cerr << "  FAIL: mixed nesting num_captures expected 3, got "
                  << re.num_captures() << std::endl;
        failures++;
      }
    }

    // --- Depth and limit tests ---

    // Deep non-capturing nesting with a capture inside (100 levels of (?:)).
    {
      std::string pattern;
      for (size_t i = 0; i < 100; i++)
        pattern += "(?:";
      pattern += "(a)";
      for (size_t i = 0; i < 100; i++)
        pattern += ")";

      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: deep non-capturing nesting — malformed: "
                  << re.error() << std::endl;
        failures++;
      }
      else
      {
        if (re.num_captures() != 1)
        {
          std::cerr
            << "  FAIL: deep non-capturing nesting — num_captures expected 1, "
               "got "
            << re.num_captures() << std::endl;
          failures++;
        }
        std::vector<RegexEngine::Capture> caps;
        size_t len = re.find_prefix("a", caps);
        if (
          len != 1 || caps.size() != 1 || caps[0].start != 0 ||
          caps[0].end != 1)
        {
          std::cerr << "  FAIL: deep non-capturing nesting — capture mismatch"
                    << std::endl;
          failures++;
        }
      }
    }

    // 256 levels of nesting (at the limit) — all non-capturing except one.
    {
      std::string pattern;
      for (size_t i = 0; i < 255; i++)
        pattern += "(?:";
      pattern += "(a)";
      for (size_t i = 0; i < 255; i++)
        pattern += ")";

      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: 256-level nesting — malformed: " << re.error()
                  << std::endl;
        failures++;
      }
      else if (!re.match("a"))
      {
        std::cerr << "  FAIL: 256-level nesting — should match 'a'"
                  << std::endl;
        failures++;
      }
    }

    // 257 levels should be rejected.
    {
      std::string pattern;
      for (size_t i = 0; i < 257; i++)
        pattern += "(?:";
      pattern += "a";
      for (size_t i = 0; i < 257; i++)
        pattern += ")";

      RegexEngine re(pattern);
      if (re.ok())
      {
        std::cerr << "  FAIL: 257-level nesting — should be rejected"
                  << std::endl;
        failures++;
      }
    }

    // Exactly 64 capturing groups (at MaxCaptures limit).
    {
      std::string pattern;
      for (size_t i = 0; i < 64; i++)
        pattern += "(";
      pattern += "a";
      for (size_t i = 0; i < 64; i++)
        pattern += ")";

      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: 64 captures — malformed: " << re.error()
                  << std::endl;
        failures++;
      }
      else
      {
        if (re.num_captures() != 64)
        {
          std::cerr << "  FAIL: 64 captures — num_captures expected 64, got "
                    << re.num_captures() << std::endl;
          failures++;
        }
        if (!re.match("a"))
        {
          std::cerr << "  FAIL: 64 captures — should match 'a'" << std::endl;
          failures++;
        }
      }
    }

    // 65 capturing groups should be rejected (exceeds MaxCaptures).
    {
      std::string pattern;
      for (size_t i = 0; i < 65; i++)
        pattern += "(";
      pattern += "a";
      for (size_t i = 0; i < 65; i++)
        pattern += ")";

      RegexEngine re(pattern);
      if (re.ok())
      {
        std::cerr << "  FAIL: 65 captures — should be rejected" << std::endl;
        failures++;
      }
      if (re.error_code() != ErrorCode::ErrorTooManyCaptures)
      {
        std::cerr << "  FAIL: 65 captures — expected ErrorTooManyCaptures, got "
                  << re.error() << std::endl;
        failures++;
      }
    }

    // Non-capturing groups pushing total nesting above 64 while captures
    // stay below 64.
    {
      std::string pattern;
      // 10 capturing groups...
      for (size_t i = 0; i < 10; i++)
        pattern += "(";
      // ...wrapped in 90 non-capturing groups (total 100 nesting levels).
      for (size_t i = 0; i < 90; i++)
        pattern += "(?:";
      pattern += "a";
      for (size_t i = 0; i < 90; i++)
        pattern += ")";
      for (size_t i = 0; i < 10; i++)
        pattern += ")";

      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: 10 caps + 90 non-cap nesting — malformed: "
                  << re.error() << std::endl;
        failures++;
      }
      else
      {
        if (re.num_captures() != 10)
        {
          std::cerr
            << "  FAIL: 10 caps + 90 non-cap — num_captures expected 10, got "
            << re.num_captures() << std::endl;
          failures++;
        }
        if (!re.match("a"))
        {
          std::cerr << "  FAIL: 10 caps + 90 non-cap — should match 'a'"
                    << std::endl;
          failures++;
        }
      }
    }
  }

  void test_word_boundary()
  {
    std::cout << "  word boundary (\\b)" << std::endl;

    auto check = [](
                   const char* name,
                   const char* pattern,
                   const char* input,
                   bool expected) {
      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: " << name << " — malformed" << std::endl;
        failures++;
        return;
      }
      bool result = re.match(input);
      if (result != expected)
      {
        std::cerr << "  FAIL: " << name << " — expected "
                  << (expected ? "match" : "no match") << " for /" << pattern
                  << "/ on \"" << input << "\"" << std::endl;
        failures++;
      }
    };

    // \b at start of word-only string.
    check("\\bword", "\\bword", "word", true);
    check("\\bword no match", "\\bword", " word", false);

    // \b at end of word-only string.
    check("word\\b", "word\\b", "word", true);
    check("word\\b no match", "word\\b", "words", false);

    // \b on both sides.
    check("\\bword\\b", "\\bword\\b", "word", true);
    check("\\bword\\b no match mid", "\\bword\\b", "words", false);

    // Word in non-word context (find_prefix can't test full match with
    // surrounding text, but match should fail for anchored patterns).
    check("\\ba\\b full", "\\ba\\b", "a", true);
    check("\\ba\\b multi-char fail", "\\ba\\b", "ab", false);

    // Empty string: boundary never fires (no word chars).
    check("\\b empty", "\\b", "", false);

    // Single word char: boundary at start and end.
    check("\\ba\\b single", "\\ba\\b", "a", true);

    // Non-word char only: \b should not match.
    check("\\b non-word", "\\b", " ", false);

    // \b with other constructs.
    check("\\b\\w+\\b", "\\b\\w+\\b", "hello", true);

    // Prefix match with word boundary.
    auto check_prefix = [](
                          const char* name,
                          const char* pattern,
                          const char* input,
                          size_t expected) {
      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: " << name << " — malformed" << std::endl;
        failures++;
        return;
      }
      size_t result = re.find_prefix(input);
      if (result != expected)
      {
        std::cerr << "  FAIL: " << name << " — expected " << expected << " got "
                  << result << std::endl;
        failures++;
      }
    };

    // \bword\b as prefix of "word rest".
    check_prefix("prefix \\bword\\b", "\\bword\\b", "word rest", 4);
    // \b\w+ matches the first word.
    check_prefix("prefix \\b\\w+\\b", "\\b\\w+\\b", "hello world", 5);
    // No boundary at start if first char is non-word.
    check_prefix(
      "prefix \\b\\w+ no match", "\\b\\w+", " hello", RegexEngine::npos);
  }

  void test_lazy_quantifiers()
  {
    std::cout << "  lazy quantifiers (*? +? ?"
                 "?)"
              << std::endl;

    auto check_match = [](
                         const char* name,
                         const char* pattern,
                         const char* input,
                         bool expected) {
      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: " << name << " — malformed" << std::endl;
        failures++;
        return;
      }
      bool result = re.match(input);
      if (result != expected)
      {
        std::cerr << "  FAIL: " << name << " — expected "
                  << (expected ? "match" : "no match") << std::endl;
        failures++;
      }
    };

    // Lazy quantifiers should still match the same strings as greedy.
    check_match("a*?b full", "a*?b", "aab", true);
    check_match("a+?b full", "a+?b", "aab", true);
    check_match("a??b full", "a??b", "ab", true);
    check_match("a??b no-a", "a??b", "b", true);
    check_match("a+? no match", "a+?b", "bbb", false);

    // Test via captures: lazy should prefer shorter submatch.
    auto check_cap = [](
                       const char* name,
                       const char* pattern,
                       const char* input,
                       size_t expected_len,
                       std::vector<std::pair<size_t, size_t>> expected_caps) {
      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: " << name << " — malformed" << std::endl;
        failures++;
        return;
      }
      std::vector<RegexEngine::Capture> caps;
      size_t len = re.find_prefix(input, caps);
      if (len != expected_len)
      {
        std::cerr << "  FAIL: " << name << " — expected len " << expected_len
                  << " got " << len << std::endl;
        failures++;
        return;
      }
      if (caps.size() != expected_caps.size())
      {
        std::cerr << "  FAIL: " << name << " — expected "
                  << expected_caps.size() << " captures, got " << caps.size()
                  << std::endl;
        failures++;
        return;
      }
      for (size_t i = 0; i < caps.size(); i++)
      {
        if (
          caps[i].start != expected_caps[i].first ||
          caps[i].end != expected_caps[i].second)
        {
          std::cerr << "  FAIL: " << name << " — capture " << i << " expected ["
                    << expected_caps[i].first << "," << expected_caps[i].second
                    << ") got [" << caps[i].start << "," << caps[i].end << ")"
                    << std::endl;
          failures++;
          return;
        }
      }
    };

    // Lazy vs greedy capture difference: in Thompson NFA, lazy affects
    // which thread arrives first at shared states, influencing captures
    // only when paths converge. For patterns like (a*?)b, the capture
    // result depends on the NFA structure. Full and prefix match outcomes
    // are identical for lazy vs greedy.
    check_cap("(a*?)b lazy cap", "(a*?)b", "aab", 3, {{0, 2}});
    // (a*)b: greedy captures "aa" before 'b' in "aab".
    check_cap("(a*)b greedy cap", "(a*)b", "aab", 3, {{0, 2}});

    // (a+?)b: same behavior as greedy in Thompson NFA for this pattern.
    check_cap("(a+?)b lazy cap", "(a+?)b", "aab", 3, {{0, 2}});
    // (a+)b: greedy captures "aa".
    check_cap("(a+)b greedy cap", "(a+)b", "aab", 3, {{0, 2}});

    // Lazy is not malformed.
    {
      RegexEngine re("a*?");
      if (!re.ok())
      {
        std::cerr << "  FAIL: a*? should not be malformed" << std::endl;
        failures++;
      }
    }
  }

  void test_start_anchor()
  {
    std::cout << "  start anchor ^" << std::endl;

    auto check_match = [](
                         const char* name,
                         const char* pattern,
                         const char* input,
                         bool expected) {
      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: " << name << " — malformed" << std::endl;
        failures++;
        return;
      }
      bool result = re.match(input);
      if (result != expected)
      {
        std::cerr << "  FAIL: " << name << " — expected "
                  << (expected ? "match" : "no match") << std::endl;
        failures++;
      }
    };

    auto check_prefix = [](
                          const char* name,
                          const char* pattern,
                          const char* input,
                          size_t expected) {
      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: " << name << " — malformed" << std::endl;
        failures++;
        return;
      }
      size_t result = re.find_prefix(input);
      if (result != expected)
      {
        std::cerr << "  FAIL: " << name << " — expected " << expected << " got "
                  << result << std::endl;
        failures++;
      }
    };

    check_match("^a matches a", "^a", "a", true);
    check_match("^a rejects ba", "^a", "ba", false);
    check_match("^abc full", "^abc", "abc", true);
    check_match("^abc rejects prefix-only", "^abc", "abcdef", false);
    check_match("escaped ^ is literal", "\\^a", "^a", true);

    // Alternation keeps the unanchored branch available.
    check_prefix("^a|b on b", "^a|b", "b", 1);
    check_prefix("^a|b on abc", "^a|b", "abc", 1);
    check_prefix("^\\w+ on abc", "^\\w+", "abc def", 3);
    check_prefix("^\\w+ no prefix", "^\\w+", " def", RegexEngine::npos);
  }

  void test_end_anchor()
  {
    std::cout << "  end anchor $" << std::endl;

    auto check_match = [](
                         const char* name,
                         const char* pattern,
                         const char* input,
                         bool expected) {
      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: " << name << " — malformed" << std::endl;
        failures++;
        return;
      }
      bool result = re.match(input);
      if (result != expected)
      {
        std::cerr << "  FAIL: " << name << " — expected "
                  << (expected ? "match" : "no match") << std::endl;
        failures++;
      }
    };

    auto check_prefix = [](
                          const char* name,
                          const char* pattern,
                          const char* input,
                          size_t expected) {
      RegexEngine re(pattern);
      if (!re.ok())
      {
        std::cerr << "  FAIL: " << name << " — malformed" << std::endl;
        failures++;
        return;
      }
      size_t result = re.find_prefix(input);
      if (result != expected)
      {
        std::cerr << "  FAIL: " << name << " — expected " << expected << " got "
                  << result << std::endl;
        failures++;
      }
    };

    // Full-match semantics.
    check_match("a$ matches a", "a$", "a", true);
    check_match("a$ rejects ab", "a$", "ab", false);
    check_match("^a$ matches a", "^a$", "a", true);
    check_match("^a$ rejects aa", "^a$", "aa", false);
    check_match("$ matches empty", "$", "", true);
    check_match("$ rejects non-empty", "$", "x", false);
    check_match("escaped $ is literal", "\\$", "$", true);
    check_match("escaped $ rejects empty", "\\$", "", false);

    // Prefix semantics from position 0.
    check_prefix("a$ prefix on a", "a$", "a", 1);
    check_prefix("a$ prefix on ab", "a$", "ab", RegexEngine::npos);
    check_prefix("$ prefix on empty", "$", "", 0);
    check_prefix("$ prefix on non-empty", "$", "abc", RegexEngine::npos);

    // Probe semantics used by global replacement loops: only the probe at
    // absolute end should match "$".
    {
      std::string input = "abc";
      for (size_t probe = 0; probe <= input.size(); probe++)
      {
        RegexEngine re("$");
        std::string_view suffix(input.data() + probe, input.size() - probe);
        size_t len = re.find_prefix(suffix, probe == 0);
        size_t expected =
          (probe == input.size()) ? size_t(0) : RegexEngine::npos;
        if (len != expected)
        {
          std::cerr << "  FAIL: $ probe at " << probe << " expected "
                    << expected << " got " << len << std::endl;
          failures++;
        }
      }
    }
  }

  void test_strict_iregexp_rejection()
  {
    std::cout << "  strict iregexp rejection" << std::endl;
    auto check_malformed = [](const char* pattern, const char* name) {
      RegexEngine re(pattern);
      if (re.ok())
      {
        std::cerr << "  FAIL: " << name << " — pattern /" << pattern
                  << "/ should be malformed" << std::endl;
        failures++;
      }
    };
    // Non-iregexp escapes.
    check_malformed("\\a", "\\a rejected");
    // Bare closers after an atom are malformed.
    check_malformed("a]", "a] rejected");
    check_malformed("a}", "a} rejected");
    // Trailing backslash.
    check_malformed("a\\", "trailing \\ rejected");
    // Nested quantifier.
    check_malformed("a*{2}", "nested quantifier a*{2}");
    check_malformed("a+{2}", "nested quantifier a+{2}");
    check_malformed("a?{2}", "nested quantifier a?{2}");
    // Invalid range.
    check_malformed("[z-a]", "inverted range [z-a]");
    // Empty class and [^].
    check_malformed("[]", "empty class []");
    check_malformed("[^]", "[^] rejected");
    // Unterminated class.
    check_malformed("[abc", "unterminated [abc");
    // Resource limit.
    check_malformed("a{1001}", "repetition > 1000");
    // Invalid category.
    check_malformed("\\p{Xx}", "unknown category \\p{Xx}");
    check_malformed("\\p{}", "empty category \\p{}");
    check_malformed("\\p{LLLL}", "4-char category \\p{LLLL}");
    // Bad brace quantifier syntax.
    check_malformed("a{", "unterminated a{");
    check_malformed("a{2,1}", "min > max a{2,1}");
    check_malformed("a{abc}", "non-numeric a{abc}");
  }

  void test_syntax_mode_differential()
  {
    std::cout << "  syntax mode differential" << std::endl;

    auto check_split_reject = [](const char* pattern, const char* name) {
      RegexEngine extended(pattern, RegexEngine::SyntaxMode::Extended);
      RegexEngine strict(pattern, RegexEngine::SyntaxMode::IregexpStrict);

      if (!extended.ok())
      {
        std::cerr << "  FAIL: extended should accept " << name << std::endl;
        failures++;
        return;
      }
      if (strict.ok())
      {
        std::cerr << "  FAIL: strict should reject " << name << std::endl;
        failures++;
      }
      if (strict.match("abc"))
      {
        std::cerr << "  FAIL: malformed strict pattern should not match for "
                  << name << std::endl;
        failures++;
      }
      if (strict.find_prefix("abc") != RegexEngine::npos)
      {
        std::cerr
          << "  FAIL: malformed strict pattern should not prefix-match for "
          << name << std::endl;
        failures++;
      }
    };

    auto check_split_match = [](
                               const char* pattern,
                               const char* input,
                               bool expected,
                               const char* name,
                               size_t expected_prefix = RegexEngine::npos,
                               bool check_prefix = false,
                               bool at_start = true) {
      RegexEngine extended(pattern, RegexEngine::SyntaxMode::Extended);
      RegexEngine strict(pattern, RegexEngine::SyntaxMode::IregexpStrict);

      if (!extended.ok() || !strict.ok())
      {
        std::cerr << "  FAIL: both modes should parse " << name << std::endl;
        failures++;
        return;
      }

      bool ext_match = extended.match(input);
      bool strict_match = strict.match(input);
      if (ext_match != expected || strict_match != expected)
      {
        std::cerr << "  FAIL: parity mismatch for " << name << " expected "
                  << expected << " got extended=" << ext_match
                  << " strict=" << strict_match << std::endl;
        failures++;
      }

      if (check_prefix)
      {
        size_t ext_prefix = extended.find_prefix(input, at_start);
        size_t strict_prefix = strict.find_prefix(input, at_start);
        if (ext_prefix != expected_prefix || strict_prefix != expected_prefix)
        {
          std::cerr << "  FAIL: prefix parity mismatch for " << name
                    << " expected " << expected_prefix
                    << " got extended=" << ext_prefix
                    << " strict=" << strict_prefix << std::endl;
          failures++;
        }
      }
    };

    // Extended-only constructs rejected in strict mode.
    check_split_reject("]", "bare ]");
    check_split_reject("{", "bare {");
    check_split_reject("}", "bare }");
    check_split_reject("\\d", "shorthand \\d");
    check_split_reject("\\D", "shorthand \\D");
    check_split_reject("\\s", "shorthand \\s");
    check_split_reject("\\S", "shorthand \\S");
    check_split_reject("\\w", "shorthand \\w");
    check_split_reject("\\W", "shorthand \\W");
    check_split_reject("\\@[_[:alnum:]]*", "identity escape \\@ identifier");
    check_split_reject("\\\"", "identity escape \\\" literal quote");
    check_split_reject(R"(\\{)", "escaped backslash then literal {");
    check_split_reject("[[:alpha:]]", "POSIX class [[:alpha:]]");
    check_split_reject("\\b", "word boundary \\b");
    check_split_reject("a*?", "lazy quantifier *?");
    check_split_reject("a+?", "lazy quantifier +?");
    check_split_reject("a??", "lazy quantifier ??");
    check_split_reject("(a)", "capturing group");
    check_split_reject("(?:a)", "non-capturing group");

    // Shared constructs remain equivalent across modes.
    check_split_match("\\p{Lu}", "A", true, "unicode category \\p{Lu}");
    check_split_match("\\P{Lu}", "a", true, "unicode category \\P{Lu}");
    check_split_match("a{2,3}", "aa", true, "range quantifier {2,3}");
    check_split_match("a{2,}", "aaaa", true, "range quantifier {2,}");
    check_split_match("a$", "a", true, "end anchor match");
    check_split_match("a$", "ab", false, "end anchor reject");
    check_split_match("^a", "a", true, "start anchor match");
    check_split_match("^a", "ba", false, "start anchor reject");
    check_split_match(".", "x", true, "dot");
    check_split_match("\\n", "\n", true, "single-char escape \\n");
    check_split_match("\\]", "]", true, "single-char escape \\]");

    // Anchor parity with prefix probe semantics.
    check_split_match(
      "$",
      "abc",
      false,
      "prefix probe at non-start",
      RegexEngine::npos,
      true,
      false);
    check_split_match("$", "", true, "prefix probe at end", 0, true, true);
  }

  void test_strict_error_codes()
  {
    std::cout << "  strict mode error codes" << std::endl;

    auto check_error =
      [](const char* pattern, ErrorCode expected, const char* name) {
        RegexEngine strict(pattern, RegexEngine::SyntaxMode::IregexpStrict);
        if (strict.ok())
        {
          std::cerr << "  FAIL: strict should reject " << name << std::endl;
          failures++;
          return;
        }
        if (strict.error_code() != expected)
        {
          std::cerr << "  FAIL: " << name << " expected error \""
                    << error_code_string(expected) << "\" but got \""
                    << strict.error() << "\"" << std::endl;
          failures++;
        }
      };

    // Lazy quantifiers → ErrorStrictSyntax
    check_error("a*?", ErrorCode::ErrorStrictSyntax, "lazy *?");
    check_error("a+?", ErrorCode::ErrorStrictSyntax, "lazy +?");
    check_error("a??", ErrorCode::ErrorStrictSyntax, "lazy ??");

    // Bare closers → ErrorStrictSyntax
    check_error("]", ErrorCode::ErrorStrictSyntax, "bare ]");
    check_error("}", ErrorCode::ErrorStrictSyntax, "bare }");

    // Groups → ErrorStrictGroup
    check_error("(a)", ErrorCode::ErrorStrictGroup, "capturing group");
    check_error("(?:a)", ErrorCode::ErrorStrictGroup, "non-capturing group");

    // Existing correct codes should be unchanged
    check_error("\\d", ErrorCode::ErrorBadEscape, "shorthand \\d");
    check_error("\\w", ErrorCode::ErrorBadEscape, "shorthand \\w");
    check_error("\\b", ErrorCode::ErrorBadEscape, "word boundary \\b");
    check_error("[[:alpha:]]", ErrorCode::ErrorBadCharClass, "POSIX class");
  }

  void test_match_context_reuse_across_engines()
  {
    std::cout << "  match context reuse across engines" << std::endl;

    RegexEngine::MatchContext ctx;

    RegexEngine first("a+");
    if (!first.ok())
    {
      std::cerr << "  FAIL: first engine unexpectedly malformed" << std::endl;
      failures++;
      return;
    }
    if (!first.match("aaaa", ctx))
    {
      std::cerr << "  FAIL: first engine expected match" << std::endl;
      failures++;
    }

    RegexEngine malformed("(");
    if (malformed.ok())
    {
      std::cerr << "  FAIL: malformed engine should be malformed" << std::endl;
      failures++;
      return;
    }
    if (malformed.match("aaaa", ctx))
    {
      std::cerr << "  FAIL: malformed engine should not match" << std::endl;
      failures++;
    }
    if (malformed.find_prefix("aaaa", ctx) != RegexEngine::npos)
    {
      std::cerr << "  FAIL: malformed engine prefix should be npos"
                << std::endl;
      failures++;
    }

    RegexEngine second("b+");
    if (!second.ok())
    {
      std::cerr << "  FAIL: second engine unexpectedly malformed" << std::endl;
      failures++;
      return;
    }
    if (!second.match("bbbb", ctx))
    {
      std::cerr << "  FAIL: second engine expected match" << std::endl;
      failures++;
    }
    if (second.match("aaaa", ctx))
    {
      std::cerr << "  FAIL: second engine should not match aaaa" << std::endl;
      failures++;
    }
  }

  void test_constructor_api_compatibility()
  {
    std::cout << "  constructor API compatibility" << std::endl;

    // Existing constructor shapes remain valid.
    RegexEngine legacy_default("a+");
    // New explicit mode constructor shapes.
    RegexEngine extended_mode("a+", RegexEngine::SyntaxMode::Extended);
    RegexEngine strict_mode("a+", RegexEngine::SyntaxMode::IregexpStrict);

    if (!legacy_default.ok() || !extended_mode.ok() || !strict_mode.ok())
    {
      std::cerr << "  FAIL: constructor compatibility instances should compile"
                << " and parse simple patterns" << std::endl;
      failures++;
    }

    if (legacy_default.syntax_mode() != RegexEngine::SyntaxMode::Extended)
    {
      std::cerr << "  FAIL: legacy constructor should default to Extended mode"
                << std::endl;
      failures++;
    }

    // With parser gating enabled, strict mode now diverges from extended mode
    // for non-iregexp constructs.
    RegexEngine staged_extended_bare("]", RegexEngine::SyntaxMode::Extended);
    RegexEngine staged_strict_bare("]", RegexEngine::SyntaxMode::IregexpStrict);
    if (!staged_extended_bare.ok())
    {
      std::cerr << "  FAIL: extended mode should accept bare ]" << std::endl;
      failures++;
    }
    if (staged_strict_bare.ok())
    {
      std::cerr << "  FAIL: strict mode should reject bare ]" << std::endl;
      failures++;
    }

    RegexEngine staged_extended_boundary(
      "\\b", RegexEngine::SyntaxMode::Extended);
    RegexEngine staged_strict_boundary(
      "\\b", RegexEngine::SyntaxMode::IregexpStrict);
    if (!staged_extended_boundary.ok())
    {
      std::cerr << "  FAIL: extended mode should accept \\b" << std::endl;
      failures++;
    }
    if (staged_strict_boundary.ok())
    {
      std::cerr << "  FAIL: strict mode should reject \\b" << std::endl;
      failures++;
    }

    auto check_mode_split = [](const char* pattern, const char* name) {
      RegexEngine extended(pattern, RegexEngine::SyntaxMode::Extended);
      RegexEngine strict(pattern, RegexEngine::SyntaxMode::IregexpStrict);
      if (!extended.ok())
      {
        std::cerr << "  FAIL: extended mode should accept " << name
                  << std::endl;
        failures++;
      }
      if (strict.ok())
      {
        std::cerr << "  FAIL: strict mode should reject " << name << std::endl;
        failures++;
      }
    };

    check_mode_split("\\d", "\\d shorthand");
    check_mode_split("[[:alpha:]]", "POSIX class [[:alpha:]]");
    check_mode_split("a*?", "lazy quantifier a*?");
    check_mode_split("(a)", "capturing group (a)");
    check_mode_split("(?:a)", "non-capturing group (?:a)");
    check_mode_split("{", "bare {");
    check_mode_split("}", "bare }");
  }

  void test_arg_parse()
  {
    using trieste::TRegex;
    std::cout << "  Arg parse types" << std::endl;

    auto check_arg = [](
                       const char* name,
                       auto* dest,
                       const char* input,
                       bool expect_ok,
                       auto expected) {
      TRegex::Arg arg(dest);
      bool ok = arg.Parse(input, std::strlen(input));
      if (ok != expect_ok)
      {
        std::cerr << "  FAIL: Arg " << name << " — Parse returned " << ok
                  << " expected " << expect_ok << std::endl;
        failures++;
      }
      else if (expect_ok && *dest != expected)
      {
        std::cerr << "  FAIL: Arg " << name << " — wrong value" << std::endl;
        failures++;
      }
    };

    int i = 0;
    check_arg("int 123", &i, "123", true, 123);
    check_arg("int -42", &i, "-42", true, -42);

    long l = 0;
    check_arg("long 999", &l, "999", true, 999L);

    unsigned int u = 0;
    check_arg("uint 456", &u, "456", true, 456u);

    float f = 0;
    check_arg("float 3.14", &f, "3.14", true, 3.14f);

    double d = 0;
    check_arg("double 2.718", &d, "2.718", true, 2.718);

    std::string s;
    TRegex::Arg sarg(&s);
    sarg.Parse("hello", 5);
    if (s != "hello")
    {
      std::cerr << "  FAIL: Arg string" << std::endl;
      failures++;
    }

    std::string_view sv;
    TRegex::Arg svarg(&sv);
    const char* src = "world";
    svarg.Parse(src, 5);
    if (sv != "world" || sv.data() != src)
    {
      std::cerr << "  FAIL: Arg string_view" << std::endl;
      failures++;
    }

    char c = 0;
    check_arg("char x", &c, "x", true, 'x');

    char c2 = 0;
    TRegex::Arg c2arg(&c2);
    if (c2arg.Parse("xy", 2))
    {
      std::cerr << "  FAIL: Arg char too long should fail" << std::endl;
      failures++;
    }

    // nullptr always succeeds
    TRegex::Arg null_arg(nullptr);
    if (!null_arg.Parse("anything", 8))
    {
      std::cerr << "  FAIL: Arg nullptr should succeed" << std::endl;
      failures++;
    }

    // overflow
    int ov = 0;
    TRegex::Arg ovarg(&ov);
    if (ovarg.Parse("99999999999999999999", 20))
    {
      std::cerr << "  FAIL: Arg int overflow should fail" << std::endl;
      failures++;
    }
  }

  void test_variadic_fullmatch()
  {
    using trieste::TRegex;
    std::cout << "  variadic FullMatch" << std::endl;

    // Extract one capture
    std::string s;
    if (!TRegex::FullMatch("hello123", TRegex("hello(\\d+)"), &s) || s != "123")
    {
      std::cerr << "  FAIL: FullMatch one capture" << std::endl;
      failures++;
    }

    // Extract int
    int n = 0;
    if (!TRegex::FullMatch("42", TRegex("(\\d+)"), &n) || n != 42)
    {
      std::cerr << "  FAIL: FullMatch int capture" << std::endl;
      failures++;
    }

    // Multiple captures
    char a = 0, b = 0, c = 0;
    if (
      !TRegex::FullMatch("abc", TRegex("(.)(.)(.)"), &a, &b, &c) || a != 'a' ||
      b != 'b' || c != 'c')
    {
      std::cerr << "  FAIL: FullMatch multi captures" << std::endl;
      failures++;
    }

    // No-arg still works
    if (!TRegex::FullMatch("abc", TRegex("abc")))
    {
      std::cerr << "  FAIL: FullMatch no-arg" << std::endl;
      failures++;
    }

    // Non-match
    std::string unused;
    if (TRegex::FullMatch("abc", TRegex("xyz"), &unused))
    {
      std::cerr << "  FAIL: FullMatch non-match should fail" << std::endl;
      failures++;
    }

    // Malformed pattern
    if (TRegex::FullMatch("abc", TRegex("[invalid"), &unused))
    {
      std::cerr << "  FAIL: FullMatch malformed should fail" << std::endl;
      failures++;
    }

    // More args than captures → false
    std::string x, y;
    if (TRegex::FullMatch("a", TRegex("(a)"), &x, &y))
    {
      std::cerr << "  FAIL: FullMatch extra args should fail" << std::endl;
      failures++;
    }

    // Skip capture with nullptr
    int v = 0;
    if (
      !TRegex::FullMatch("ab42", TRegex("(ab)(\\d+)"), nullptr, &v) || v != 42)
    {
      std::cerr << "  FAIL: FullMatch skip with nullptr" << std::endl;
      failures++;
    }
  }

  void test_partial_match()
  {
    using trieste::TRegex;
    std::cout << "  PartialMatch" << std::endl;

    // Basic substring
    if (!TRegex::PartialMatch("hello world", TRegex("world")))
    {
      std::cerr << "  FAIL: PartialMatch basic substring" << std::endl;
      failures++;
    }

    // No match
    if (TRegex::PartialMatch("abc", TRegex("xyz")))
    {
      std::cerr << "  FAIL: PartialMatch no match should fail" << std::endl;
      failures++;
    }

    // Empty text
    if (TRegex::PartialMatch("", TRegex("a")))
    {
      std::cerr << "  FAIL: PartialMatch empty text should fail" << std::endl;
      failures++;
    }

    // Empty pattern matches anywhere
    if (!TRegex::PartialMatch("abc", TRegex("")))
    {
      std::cerr << "  FAIL: PartialMatch empty pattern should match"
                << std::endl;
      failures++;
    }

    // Malformed
    if (TRegex::PartialMatch("abc", TRegex("[invalid")))
    {
      std::cerr << "  FAIL: PartialMatch malformed should fail" << std::endl;
      failures++;
    }

    // Full-string also matches
    if (!TRegex::PartialMatch("abc", TRegex("abc")))
    {
      std::cerr << "  FAIL: PartialMatch full string" << std::endl;
      failures++;
    }

    // ^ anchor hit
    if (!TRegex::PartialMatch("hello world", TRegex("^hello")))
    {
      std::cerr << "  FAIL: PartialMatch ^ anchor hit" << std::endl;
      failures++;
    }

    // ^ anchor miss
    if (TRegex::PartialMatch("say hello", TRegex("^hello")))
    {
      std::cerr << "  FAIL: PartialMatch ^ anchor miss should fail"
                << std::endl;
      failures++;
    }

    // $ anchor
    if (!TRegex::PartialMatch("hello world", TRegex("world$")))
    {
      std::cerr << "  FAIL: PartialMatch $ anchor" << std::endl;
      failures++;
    }

    // ^...$ both anchors match
    if (!TRegex::PartialMatch("hello", TRegex("^hello$")))
    {
      std::cerr << "  FAIL: PartialMatch ^...$ match" << std::endl;
      failures++;
    }

    // ^...$ too long
    if (TRegex::PartialMatch("hello world", TRegex("^hello$")))
    {
      std::cerr << "  FAIL: PartialMatch ^...$ should fail" << std::endl;
      failures++;
    }
  }

  void test_variadic_partial_match()
  {
    using trieste::TRegex;
    std::cout << "  variadic PartialMatch" << std::endl;

    // Extract digits from middle of string
    std::string s;
    if (!TRegex::PartialMatch("foo123bar", TRegex("(\\d+)"), &s) || s != "123")
    {
      std::cerr << "  FAIL: PartialMatch capture digits" << std::endl;
      failures++;
    }

    // Multiple captures
    std::string host;
    int port = 0;
    if (
      !TRegex::PartialMatch(
        "chrisr:9000", TRegex("(\\w+):(\\d+)"), &host, &port) ||
      host != "chrisr" || port != 9000)
    {
      std::cerr << "  FAIL: PartialMatch multi captures" << std::endl;
      failures++;
    }

    // Extra args → false
    std::string x, y;
    if (TRegex::PartialMatch("a", TRegex("(a)"), &x, &y))
    {
      std::cerr << "  FAIL: PartialMatch extra args should fail" << std::endl;
      failures++;
    }

    // Greedy match: a+ in "baaab" → should capture "aaa"
    std::string greedy;
    if (
      !TRegex::PartialMatch("baaab", TRegex("(a+)"), &greedy) ||
      greedy != "aaa")
    {
      std::cerr << "  FAIL: PartialMatch greedy" << std::endl;
      failures++;
    }

    // Overlapping: ab in "aab" → match at position 1
    std::string ov;
    if (!TRegex::PartialMatch("aab", TRegex("(ab)"), &ov) || ov != "ab")
    {
      std::cerr << "  FAIL: PartialMatch overlapping" << std::endl;
      failures++;
    }

    // Word boundary
    std::string wb;
    if (
      !TRegex::PartialMatch("hello world", TRegex("\\b(world)\\b"), &wb) ||
      wb != "world")
    {
      std::cerr << "  FAIL: PartialMatch word boundary" << std::endl;
      failures++;
    }

    // Skip capture with nullptr
    int v = 0;
    if (
      !TRegex::PartialMatch("key=42", TRegex("(\\w+)=(\\d+)"), nullptr, &v) ||
      v != 42)
    {
      std::cerr << "  FAIL: PartialMatch skip with nullptr" << std::endl;
      failures++;
    }
  }

  void test_tregexmatch_parse_expansion()
  {
    using trieste::TRegex;
    std::cout << "  TRegexMatch::parse expansion" << std::endl;

    // Test parse<std::string> via TRegex+TRegexMatch on a source
    auto src_data = std::string("hello42world");
    auto source = trieste::SourceDef::synthetic(src_data);
    trieste::TRegexMatch m(2);
    trieste::TRegexIterator it(source);

    TRegex re("([[:alpha:]]+)(\\d+)");
    if (!it.consume(re, m))
    {
      std::cerr << "  FAIL: TRegexMatch consume failed" << std::endl;
      failures++;
      return;
    }

    auto s = m.parse<std::string>(1);
    if (s != "hello")
    {
      std::cerr << "  FAIL: parse<string> = " << s << std::endl;
      failures++;
    }

    auto sv = m.parse<std::string_view>(1);
    if (sv != "hello")
    {
      std::cerr << "  FAIL: parse<string_view>" << std::endl;
      failures++;
    }

    auto n = m.parse<int>(2);
    if (n != 42)
    {
      std::cerr << "  FAIL: parse<int> = " << n << std::endl;
      failures++;
    }

    auto src_data_partial = std::string("123abc");
    auto source_partial = trieste::SourceDef::synthetic(src_data_partial);
    trieste::TRegexMatch partial_match(1);
    trieste::TRegexIterator partial_it(source_partial);

    TRegex partial_re("([^[:space:]]+)");
    if (!partial_it.consume(partial_re, partial_match))
    {
      std::cerr << "  FAIL: TRegexMatch partial consume failed" << std::endl;
      failures++;
      return;
    }

    auto partial_int = partial_match.parse<int>(1);
    if (partial_int != 0)
    {
      std::cerr << "  FAIL: parse<int> accepted partial numeric prefix"
                << std::endl;
      failures++;
    }
  }

  void test_global_replace_string_view_pattern()
  {
    using trieste::TRegex;
    std::cout << "  GlobalReplace string_view pattern" << std::endl;

    // The view is not null-terminated at its logical end.
    const std::string pattern_storage = "xxa+bzz";
    const std::string_view pattern(pattern_storage.data() + 2, 3);

    std::string text = "aaab";
    const int replaced = TRegex::GlobalReplace(&text, pattern, "X");

    if (replaced != 1)
    {
      std::cerr << "  FAIL: GlobalReplace should replace one match, got "
                << replaced << std::endl;
      failures++;
    }

    if (text != "X")
    {
      std::cerr << "  FAIL: GlobalReplace result text = " << text << std::endl;
      failures++;
    }
  }

  void test_unmatched_capture_reset()
  {
    using trieste::TRegex;
    std::cout << "  unmatched capture reset" << std::endl;

    // Pattern (a)|(b): matching "b" means group 1 is unmatched.
    // Output arg for group 1 must be reset to default, not left stale.

    // FullMatch: string arg reset
    {
      std::string s1 = "STALE";
      std::string s2 = "STALE";
      if (!TRegex::FullMatch("b", TRegex("(a)|(b)"), &s1, &s2))
      {
        std::cerr << "  FAIL: unmatched capture FullMatch should match"
                  << std::endl;
        failures++;
      }
      else if (!s1.empty())
      {
        std::cerr << "  FAIL: unmatched capture FullMatch s1 should be empty, "
                     "got \""
                  << s1 << "\"" << std::endl;
        failures++;
      }
      else if (s2 != "b")
      {
        std::cerr << "  FAIL: unmatched capture FullMatch s2 should be \"b\", "
                     "got \""
                  << s2 << "\"" << std::endl;
        failures++;
      }
    }

    // FullMatch: int arg reset
    {
      int n1 = 999;
      int n2 = 999;
      if (!TRegex::FullMatch("42", TRegex("(\\d+)|(\\w+)"), &n1, &n2))
      {
        std::cerr << "  FAIL: unmatched capture FullMatch int should match"
                  << std::endl;
        failures++;
      }
      else if (n1 != 42)
      {
        std::cerr << "  FAIL: unmatched capture FullMatch n1 should be 42, got "
                  << n1 << std::endl;
        failures++;
      }
      else if (n2 != 0)
      {
        std::cerr << "  FAIL: unmatched capture FullMatch n2 should be 0, got "
                  << n2 << std::endl;
        failures++;
      }
    }

    // FullMatch: string_view arg reset
    {
      std::string_view sv1 = "STALE";
      std::string_view sv2 = "STALE";
      if (!TRegex::FullMatch("b", TRegex("(a)|(b)"), &sv1, &sv2))
      {
        std::cerr
          << "  FAIL: unmatched capture FullMatch string_view should match"
          << std::endl;
        failures++;
      }
      else if (!sv1.empty())
      {
        std::cerr << "  FAIL: unmatched capture FullMatch sv1 should be empty"
                  << std::endl;
        failures++;
      }
      else if (sv2 != "b")
      {
        std::cerr << "  FAIL: unmatched capture FullMatch sv2 should be \"b\""
                  << std::endl;
        failures++;
      }
    }

    // PartialMatch: string arg reset
    {
      std::string s1 = "STALE";
      std::string s2 = "STALE";
      if (!TRegex::PartialMatch("xbx", TRegex("(a)|(b)"), &s1, &s2))
      {
        std::cerr << "  FAIL: unmatched capture PartialMatch should match"
                  << std::endl;
        failures++;
      }
      else if (!s1.empty())
      {
        std::cerr
          << "  FAIL: unmatched capture PartialMatch s1 should be empty, got \""
          << s1 << "\"" << std::endl;
        failures++;
      }
      else if (s2 != "b")
      {
        std::cerr
          << "  FAIL: unmatched capture PartialMatch s2 should be \"b\", got \""
          << s2 << "\"" << std::endl;
        failures++;
      }
    }

    // PartialMatch: int arg reset
    // Use (\d{3})|(\d{2}) so that group 1 (3-digit) is unmatched on "42".
    {
      int n1 = 999;
      int n2 = 999;
      if (!TRegex::PartialMatch("42", TRegex("(\\d{3})|(\\d{2})"), &n1, &n2))
      {
        std::cerr << "  FAIL: unmatched capture PartialMatch int should match"
                  << std::endl;
        failures++;
      }
      else if (n1 != 0)
      {
        std::cerr
          << "  FAIL: unmatched capture PartialMatch n1 should be 0, got " << n1
          << std::endl;
        failures++;
      }
      else if (n2 != 42)
      {
        std::cerr
          << "  FAIL: unmatched capture PartialMatch n2 should be 42, got "
          << n2 << std::endl;
        failures++;
      }
    }

    // Repeated calls: verify no stale data leaks across invocations
    {
      std::string s;
      s = "STALE";
      TRegex re("(a)|(b)");
      // First call: match "a" → s1 = "a"
      if (!TRegex::FullMatch("a", re, &s, nullptr) || s != "a")
      {
        std::cerr << "  FAIL: unmatched capture repeated call 1" << std::endl;
        failures++;
      }
      // Second call: match "b" → s1 should be reset to empty
      if (!TRegex::FullMatch("b", re, &s, nullptr))
      {
        std::cerr << "  FAIL: unmatched capture repeated call 2 match"
                  << std::endl;
        failures++;
      }
      else if (!s.empty())
      {
        std::cerr
          << "  FAIL: unmatched capture repeated call 2 should be empty, got \""
          << s << "\"" << std::endl;
        failures++;
      }
    }
  }

  void test_word_boundary_search()
  {
    using trieste::TRegex;
    using trieste::regex::RegexEngine;
    std::cout << "  word boundary search" << std::endl;

    // RegexEngine::search() — \bworld should NOT match inside "helloworld"
    {
      RegexEngine re("\\bworld");
      auto result = re.search("helloworld");
      if (result.found())
      {
        std::cerr
          << "  FAIL: search \\bworld should not match inside helloworld"
          << std::endl;
        failures++;
      }
    }

    // RegexEngine::search() — \bworld should match in "hello world"
    {
      RegexEngine re("\\bworld");
      auto result = re.search("hello world");
      if (!result.found() || result.match_start != 6 || result.match_len != 5)
      {
        std::cerr << "  FAIL: search \\bworld in 'hello world' expected "
                     "start=6 len=5"
                  << std::endl;
        failures++;
      }
    }

    // RegexEngine::search() — \bworld\b should match "world" at start
    {
      RegexEngine re("\\bworld\\b");
      auto result = re.search("world");
      if (!result.found() || result.match_start != 0 || result.match_len != 5)
      {
        std::cerr << "  FAIL: search \\bworld\\b on 'world'" << std::endl;
        failures++;
      }
    }

    // RegexEngine::search() — \bworld\b should not match "worldly"
    {
      RegexEngine re("\\bworld\\b");
      auto result = re.search("worldly");
      if (result.found())
      {
        std::cerr << "  FAIL: search \\bworld\\b should not match 'worldly'"
                  << std::endl;
        failures++;
      }
    }

    // RegexEngine::search() with captures — \b(\w+)\b in "hello world"
    {
      RegexEngine re("\\b(\\w+)\\b");
      RegexEngine::MatchContext ctx;
      std::vector<RegexEngine::Capture> captures;
      auto result = re.search("hello world", captures, ctx);
      if (!result.found() || result.match_start != 0 || result.match_len != 5)
      {
        std::cerr << "  FAIL: search \\b(\\w+)\\b captures position"
                  << std::endl;
        failures++;
      }
      else if (
        captures.size() != 1 || !captures[0].matched() ||
        captures[0].start != 0 || captures[0].end != 5)
      {
        std::cerr << "  FAIL: search \\b(\\w+)\\b capture value" << std::endl;
        failures++;
      }
    }

    // RegexEngine::search() with start_pos — find second word
    {
      RegexEngine re("\\b(\\w+)\\b");
      RegexEngine::MatchContext ctx;
      std::vector<RegexEngine::Capture> captures;
      auto result = re.search("hello world", captures, ctx, 5);
      if (!result.found() || result.match_start != 6 || result.match_len != 5)
      {
        std::cerr << "  FAIL: search with start_pos for second word"
                  << std::endl;
        failures++;
      }
      else if (
        captures.size() != 1 || captures[0].start != 6 || captures[0].end != 11)
      {
        std::cerr << "  FAIL: search with start_pos capture offsets"
                  << std::endl;
        failures++;
      }
    }

    // TRegex::PartialMatch — \bworld should NOT match inside "helloworld"
    if (TRegex::PartialMatch("helloworld", TRegex("\\bworld")))
    {
      std::cerr << "  FAIL: PartialMatch \\bworld should not match helloworld"
                << std::endl;
      failures++;
    }

    // TRegex::PartialMatch — \bworld should match in "hello world"
    {
      std::string cap;
      if (
        !TRegex::PartialMatch("hello world", TRegex("\\b(world)"), &cap) ||
        cap != "world")
      {
        std::cerr << "  FAIL: PartialMatch \\b(world) in 'hello world'"
                  << std::endl;
        failures++;
      }
    }

    // TRegex::PartialMatch — \b at end of word: "hello\b" in "hello world"
    if (!TRegex::PartialMatch("hello world", TRegex("hello\\b")))
    {
      std::cerr << "  FAIL: PartialMatch hello\\b in 'hello world'"
                << std::endl;
      failures++;
    }

    // TRegex::PartialMatch — \b should not match within a word
    if (TRegex::PartialMatch("helloworld", TRegex("hello\\bworld")))
    {
      std::cerr
        << "  FAIL: PartialMatch hello\\bworld should not match 'helloworld'"
        << std::endl;
      failures++;
    }

    // GlobalReplace with \b — should respect word boundaries
    {
      std::string text = "cat catalog caterpillar";
      int n = TRegex::GlobalReplace(&text, "\\bcat\\b", "dog");
      if (n != 1 || text != "dog catalog caterpillar")
      {
        std::cerr << "  FAIL: GlobalReplace \\bcat\\b expected 'dog catalog "
                     "caterpillar', got '"
                  << text << "'" << std::endl;
        failures++;
      }
    }
  }

  void test_utf8_search()
  {
    using trieste::TRegex;
    using trieste::regex::RegexEngine;
    std::cout << "  UTF-8 search" << std::endl;

    // 2-byte: é = U+00E9 = \xC3\xA9
    // 3-byte: € = U+20AC = \xE2\x82\xAC
    // 4-byte: 😀 = U+1F600 = \xF0\x9F\x98\x80

    // search() should find a pattern after multi-byte codepoints
    {
      // "café" has é at bytes 3-4; search for "f" should find byte offset 2
      // "caf" is bytes 0-2, "é" is bytes 3-4
      std::string text = "caf\xC3\xA9";
      RegexEngine re("f");
      auto result = re.search(text);
      if (!result.found() || result.match_start != 2 || result.match_len != 1)
      {
        std::cerr << "  FAIL: search 'f' in 'café' expected start=2 len=1"
                  << std::endl;
        failures++;
      }
    }

    // search() should find ASCII after a 3-byte codepoint
    {
      // "€x" = \xE2\x82\xAC x
      std::string text = "\xE2\x82\xAC x";
      RegexEngine re("x");
      auto result = re.search(text);
      if (!result.found() || result.match_start != 4 || result.match_len != 1)
      {
        std::cerr << "  FAIL: search 'x' after euro sign, expected start=4"
                  << std::endl;
        failures++;
      }
    }

    // search() should find ASCII after a 4-byte codepoint
    {
      // "😀z" = \xF0\x9F\x98\x80 z
      std::string text = "\xF0\x9F\x98\x80z";
      RegexEngine re("z");
      auto result = re.search(text);
      if (!result.found() || result.match_start != 4 || result.match_len != 1)
      {
        std::cerr << "  FAIL: search 'z' after emoji, expected start=4"
                  << std::endl;
        failures++;
      }
    }

    // search() with start_pos past a multi-byte codepoint
    {
      // "aébx" = a \xC3\xA9 b x
      std::string text = "a\xC3\xA9 bx";
      RegexEngine re("b");
      RegexEngine::MatchContext ctx;
      std::vector<RegexEngine::Capture> captures;
      auto result = re.search(text, captures, ctx, 3);
      if (!result.found() || result.match_start != 4 || result.match_len != 1)
      {
        std::cerr << "  FAIL: search 'b' with start_pos=3 in 'aé bx'"
                  << std::endl;
        failures++;
      }
    }

    // GlobalReplace with zero-length match around multi-byte codepoints
    {
      // Insert "|" at every position boundary in "a€b"
      // "a€b" = a \xE2\x82\xAC b  (5 bytes, 3 codepoints)
      // Zero-length matches occur before a, between a and €, between € and b,
      // after b → 4 insertions, result: "|a|€|b|"
      std::string text =
        "a\xE2\x82\xAC"
        "b";
      int n = TRegex::GlobalReplace(&text, "", "|");
      if (n != 4)
      {
        std::cerr << "  FAIL: GlobalReplace zero-len in 'a€b' expected 4 "
                     "replacements, got "
                  << n << std::endl;
        failures++;
      }
      std::string expected = "|a|\xE2\x82\xAC|b|";
      if (text != expected)
      {
        std::cerr << "  FAIL: GlobalReplace zero-len in 'a€b' wrong result"
                  << std::endl;
        failures++;
      }
    }

    // GlobalReplace with zero-length match around 4-byte codepoints
    {
      // "😀" = \xF0\x9F\x98\x80 (4 bytes, 1 codepoint)
      // Zero-length matches: before and after → 2 insertions, result: "|😀|"
      std::string text = "\xF0\x9F\x98\x80";
      int n = TRegex::GlobalReplace(&text, "", "|");
      if (n != 2)
      {
        std::cerr << "  FAIL: GlobalReplace zero-len in emoji expected 2, got "
                  << n << std::endl;
        failures++;
      }
      std::string expected = "|\xF0\x9F\x98\x80|";
      if (text != expected)
      {
        std::cerr << "  FAIL: GlobalReplace zero-len in emoji wrong result"
                  << std::endl;
        failures++;
      }
    }
  }
}

int main()
{
  std::cout << "Running regex engine tests..." << std::endl;
  test_literals();
  test_alternation();
  test_empty_alternatives();
  test_zero_or_one();
  test_zero_or_more();
  test_one_or_more();
  test_grouping();
  test_escaped_operators();
  test_empty_pattern();
  test_malformed_patterns();
  test_combined();
  test_dot();
  test_single_char_escapes();
  test_character_classes();
  test_unicode_categories();
  test_range_quantifiers();
  test_posix_classes();
  test_shorthand_escapes();
  test_hex_escapes();
  test_non_capturing_groups();
  test_real_world_parser_patterns();
  test_find_prefix();
  test_capturing_groups();
  test_word_boundary();
  test_lazy_quantifiers();
  test_start_anchor();
  test_end_anchor();
  test_strict_iregexp_rejection();
  test_syntax_mode_differential();
  test_strict_error_codes();
  test_match_context_reuse_across_engines();
  test_constructor_api_compatibility();
  test_arg_parse();
  test_variadic_fullmatch();
  test_partial_match();
  test_variadic_partial_match();
  test_tregexmatch_parse_expansion();
  test_global_replace_string_view_pattern();
  test_unmatched_capture_reset();
  test_word_boundary_search();
  test_utf8_search();

  if (failures > 0)
  {
    std::cerr << failures << " test(s) FAILED" << std::endl;
    return 1;
  }

  std::cout << "All tests passed!" << std::endl;
  return 0;
}

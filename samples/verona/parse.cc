// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "lang.h"

#include <fmt/core.h>
#include <random>

namespace verona
{
  constexpr size_t restart = 0;
  const std::initializer_list<Token> terminators = {Equals, List};

  std::string random_string(Rand& rnd, size_t maxlen)
  {
    std::stringstream ss;
    auto len = (maxlen > 1) ? (rnd() % maxlen) + 1 : maxlen;

    for (size_t i = 0; i < len; i++)
      ss << static_cast<char>(rnd() % 256);

    return ss.str();
  }

  double random_double(Rand& rnd)
  {
    std::uniform_real_distribution<> dist(
      std::numeric_limits<double>::min(), std::numeric_limits<double>::max());
    return dist(rnd);
  }

  std::string unquote(const std::string& s)
  {
    return ((s.size() >= 2) && (s[0] == '"') && (s[s.size() - 1] == '"')) ?
      s.substr(1, s.size() - 2) :
      s;
  }

  Parse parser()
  {
    Parse p(depth::subdirectories);
    auto depth = std::make_shared<size_t>(0);
    auto indent = std::make_shared<std::vector<size_t>>();
    indent->push_back(restart);

    p.prefile([](auto&, auto& path) { return path.extension() == ".verona"; });

    p.predir([](auto&, auto& path) {
      static auto re = std::regex(
        "^[_[:alpha:]][_[:alnum:]]*?$", std::regex_constants::optimize);
      return std::regex_match(path.filename().string(), re);
    });

    p.postparse([](auto& p, auto& path, auto ast) {
      auto stdlib = p.executable().parent_path() / "std";
      if (path != stdlib)
        ast->push_back(p.sub_parse(stdlib));
    });

    p.postfile([indent, depth](auto&, auto&, auto) {
      *depth = 0;
      indent->clear();
      indent->push_back(restart);
    });

    p("start",
      {
        // Blank lines terminate.
        "\n(?:[[:blank:]]*\n)+([[:blank:]]*)" >>
          [indent](auto& m) {
            indent->back() = m.match().length(1);
            m.term(terminators);
          },

        // A newline that starts a brace block doesn't terminate.
        "\n([[:blank:]]*(\\{)[[:blank:]]*)" >>
          [indent](auto& m) {
            indent->push_back(m.match().length(1));
            m.push(Brace, 2);
          },

        // A newline sometimes terminates.
        "\n([[:blank:]]*)" >>
          [indent](auto& m) {
            size_t col = m.match().length(1);
            auto prev = indent->back();

            // If following a brace, don't terminate, but reset indentation.
            if (m.previous(Brace))
            {
              indent->back() = col;
              return;
            }

            // Don't terminate and don't reset indentation if:
            // * in an equals or list
            // * in a group and indented
            if (m.in(Equals) || m.in(List) || (m.in(Group) && (col > prev)))
              return;

            // Otherwise, terminate and reset indentation.
            m.term(terminators);
            indent->back() = col;
          },

        // Whitespace between tokens.
        "[[:blank:]]+" >> [](auto&) {},

        // Terminator.
        ";" >> [](auto& m) { m.term(terminators); },

        // Function type or lambda.
        "=>" >>
          [indent](auto& m) {
            indent->back() = m.linecol().second + 1;
            m.term(terminators);
            m.add(Arrow);
            m.term(terminators);
          },

        // Equals.
        "=(?![!#$%&*+-/<=>?@\\^`|~])" >> [](auto& m) { m.seq(Equals); },

        // List.
        "," >> [](auto& m) { m.seq(List, {Equals}); },

        // Parens.
        "(\\()[[:blank:]]*" >>
          [indent](auto& m) {
            indent->push_back(m.linecol().second + m.match().length());
            m.push(Paren, 1);
          },

        "\\)" >>
          [indent](auto& m) {
            indent->pop_back();
            m.term(terminators);
            m.pop(Paren);
          },

        // Square brackets.
        "(\\[)[[:blank:]]*" >>
          [indent](auto& m) {
            indent->push_back(m.linecol().second + m.match().length());
            m.push(Square, 1);
          },

        "\\]" >>
          [indent](auto& m) {
            indent->pop_back();
            m.term(terminators);
            m.pop(Square);
          },

        // Curly braces.
        "(\\{)[[:blank:]]*" >>
          [indent](auto& m) {
            indent->push_back(m.linecol().second + m.match().length());
            m.push(Brace, 1);
          },

        "\\}" >>
          [indent](auto& m) {
            indent->pop_back();
            m.term(terminators);
            m.pop(Brace);
          },

        // Bool.
        "(?:true|false)\\b" >> [](auto& m) { m.add(Bool); },

        // Hex float.
        "0x[[:xdigit:]]+\\.[[:xdigit:]]+(?:p[+-][[:digit:]]+)?\\b" >>
          [](auto& m) { m.add(HexFloat); },

        // Hex.
        "0x[_[:xdigit:]]+\\b" >> [](auto& m) { m.add(Hex); },

        // Bin.
        "0b[_01]+\\b" >> [](auto& m) { m.add(Bin); },

        // Float.
        "[[:digit:]]+\\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?\\b" >>
          [](auto& m) { m.add(Float); },

        // Int.
        "[[:digit:]]+\\b" >> [](auto& m) { m.add(Int); },

        // Escaped string.
        "\"((?:\\\"|[^\"])*?)\"" >> [](auto& m) { m.add(Escaped, 1); },

        // Unescaped string.
        "('+)\"([\\s\\S]*?)\"\\1" >> [](auto& m) { m.add(String, 2); },

        // Character literal.
        "'((?:\\'|[^'])*)'" >> [](auto& m) { m.add(Char, 1); },

        // LLVM IR literal.
        ":\\[((?:[^\\]]|\\][^:])*)\\]:" >> [](auto& m) { m.add(LLVM, 1); },

        // Line comment.
        "//[^\n]*" >> [](auto&) {},

        // Nested comment.
        "/\\*" >>
          [depth](auto& m) {
            ++(*depth);
            m.mode("comment");
          },

        // Keywords.
        "use\\b" >> [](auto& m) { m.add(Use); },
        "type\\b" >> [](auto& m) { m.add(TypeAlias); },
        "class\\b" >> [](auto& m) { m.add(Class); },
        "var\\b" >> [](auto& m) { m.add(Var); },
        "let\\b" >> [](auto& m) { m.add(Let); },
        "ref\\b" >> [](auto& m) { m.add(Ref); },
        "lin\\b" >> [](auto& m) { m.add(Lin); },
        "in\\b" >> [](auto& m) { m.add(In_); },
        "out\\b" >> [](auto& m) { m.add(Out); },
        "const\\b" >> [](auto& m) { m.add(Const); },
        "if\\b" >> [](auto& m) { m.add(If); },
        "else\\b" >> [](auto& m) { m.add(Else); },
        "new\\b" >> [](auto& m) { m.add(New); },
        "try\\b" >> [](auto& m) { m.add(Try); },

        // Don't care.
        "_(?![_[:alnum:]])" >> [](auto& m) { m.add(DontCare); },

        // Reserve a sequence of underscores.
        "_(?:_)+(?![[:alnum:]])" >>
          [](auto& m) {
            m.error(
              "a sequence of two or more underscores is a reserved identifier");
          },

        // Identifier.
        "[_[:alpha:]][_[:alnum:]]*\\b" >> [](auto& m) { m.add(Ident); },

        // Ellipsis.
        "\\.\\.\\." >> [](auto& m) { m.add(Ellipsis); },

        // Dot.
        "\\." >> [](auto& m) { m.add(Dot); },

        // Triple colon.
        ":::" >> [](auto& m) { m.add(TripleColon); },

        // Double colon.
        "::" >> [](auto& m) { m.add(DoubleColon); },

        // Colon.
        ":" >> [](auto& m) { m.add(Colon); },

        // Symbol. Reserved: "'(),.:;[]_{}
        "[!#$%&*+-/<=>?@\\^`|~]+" >> [](auto& m) { m.add(Symbol); },
      });

    p("comment",
      {
        "(?:[^\\*]|\\*(?!/))*?/\\*" >> [depth](auto&) { ++(*depth); },
        "(?:[^/]|/(?!\\*))*?\\*/" >>
          [depth](auto& m) {
            if (--(*depth) == 0)
              m.mode("start");
          },
      });

    p.done([](auto& m) {
      if (m.mode() != "start")
        m.error("unterminated comment at end of file");

      m.term(terminators);
    });

    p.gen({
      Bool >> [](auto& rnd) { return rnd() % 2 ? "true" : "false"; },
      Int >> [](auto& rnd) { return std::to_string(rnd()); },
      Hex >> [](auto& rnd) { return fmt::format("{:#x}", rnd()); },
      Bin >> [](auto& rnd) { return fmt::format("{:#b}", rnd()); },
      Float >>
        [](auto& rnd) { return fmt::format("{:g}", random_double(rnd)); },
      HexFloat >>
        [](auto& rnd) { return fmt::format("{:a}", random_double(rnd)); },
      Char >>
        [](auto& rnd) {
          return unquote(fmt::format("{:?}", random_string(rnd, 1)));
        },
      Escaped >>
        [](auto& rnd) {
          return unquote(fmt::format("{:?}", random_string(rnd, 32)));
        },
      String >>
        [](auto& rnd) { return fmt::format("{}", random_string(rnd, 32)); },
    });

    return p;
  }
}

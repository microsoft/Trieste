#include "infix.h"
#include "internal.h"

namespace infix
{
  const std::initializer_list<Token> terminators = {Equals, ParserTuple};

  Parse parser(bool use_parser_tuples)
  {
    Parse p(depth::file, wf_parser);
    auto indent = std::make_shared<std::vector<size_t>>();

    p("start", // this indicates the 'mode' these rules are associated with
      {
        // Whitespace between tokens.
        "[[:blank:]]+" >> [](auto&) {}, // no-op

        // Equals.
        "=" >> [](auto& m) { m.seq(Equals); },

        // Commas: might be tuple literals, function calls.
        "," >>
          [use_parser_tuples](auto& m) {
            if (use_parser_tuples)
            {
              m.seq(ParserTuple);
            }
            else
            {
              m.add(Comma);
            }
          },

        // Tuple indexing.
        "\\." >> [](auto& m) { m.add(TupleIdx); },

        // Terminator.
        ";[\n]*" >> [](auto& m) { m.term(terminators); },

        // Parens.
        R"((\()[[:blank:]]*)" >>
          [indent](auto& m) {
            // we push a Paren node. Subsequent nodes will be added
            // as its children.
            m.push(Paren, 1);
          },

        R"(\))" >>
          [indent](auto& m) {
            // terminate the current group
            m.term(terminators);
            // pop back up out of the Paren
            m.pop(Paren); // TODO: how do we avoid being in a Paren, if we had a
                          // ParserTuple?
          },

        // Float.
        R"([[:digit:]]+\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?\b)" >>
          [](auto& m) { m.add(Float); },

        // String.
        R"("[^"]*")" >> [](auto& m) { m.add(String); },

        // Int.
        R"([[:digit:]]+\b)" >> [](auto& m) { m.add(Int); },

        // Line comment.
        "//[^\n]*" >> [](auto&) {}, // another no-op

        // Print.
        R"(print\b)" >> [](auto& m) { m.add(Print); },

        // Identifier.
        R"([_[:alpha:]][_[:alnum:]]*\b)" >> [](auto& m) { m.add(Ident); },

        // Add ('+' is a reserved RegEx character)
        R"(\+)" >> [](auto& m) { m.add(Add); },

        // Subtract
        "-" >> [](auto& m) { m.add(Subtract); },

        // Multiply ('*' is a reserved RegEx character)
        R"(\*)" >> [](auto& m) { m.add(Multiply); },

        // Divide
        "/" >> [](auto& m) { m.add(Divide); },
      });

    p.gen({
      Int >> [](auto& rnd) { return std::to_string(rnd() % 100); },
      Float >>
        [](auto& rnd) {
          std::uniform_real_distribution<> dist(-10.0, 10.0);
          return std::to_string(dist(rnd));
        },
    });

    return p;
  }
}

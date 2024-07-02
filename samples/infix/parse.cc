#include "infix.h"
#include "internal.h"
#include "trieste/token.h"

namespace infix
{
  const std::initializer_list<Token> terminators = {Equals, ParserTuple};

  Parse parser(bool use_parser_tuples)
  {
    Parse p(depth::file, wf_parser);

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
              // Point of care: it only makes sense to .seq a ParserTuple inside
              // a Paren. If we just blindly .seq here, then it is easy for even
              // slightly strange inputs to cause a WF violation, because a
              // group turned into a ParserTuple unexpectedly. So, we have to
              // directly forbid bad-looking commas in the parser if we're
              // trying to capture tuples (or other tuple like things) directly.
              if (m.in(Paren) || m.group_in(Paren) || m.group_in(ParserTuple))
              {
                // note: group_in is necessary because we will initially
                // be in a state like (Paren ...) [groups are lazy, added, so
                // just a comma in a Paren hits this case], at which point .add
                // might change us to (Paren (Group ...)). So, we are either
                // directly in a Paren, or we are in a Group in a Paren. If we
                // already did .seq(ParserTuple) once, we might be in a (Paren
                // (ParserTuple (Group ...))). We check for all 3 conditions
                // before seq-ing, otherwise this is an error.
                m.seq(ParserTuple);
              }
              else
              {
                m.error("Commas outside parens are illegal");
              }
            }
            else
            {
              m.add(Comma);
            }
          },

        // Tuple indexing.
        R"re(\.)re" >> [](auto& m) { m.add(TupleIdx); },

        // Terminator.
        R"re(;[\n]*)re" >> [](auto& m) { m.term(terminators); },

        // Parens.
        R"re((\()[[:blank:]]*)re" >>
          [](auto& m) {
            // we push a Paren node. Subsequent nodes will be added
            // as its children.
            m.push(Paren, 1);
          },

        R"re(\))re" >>
          [](auto& m) {
            // terminate the current group
            m.term(terminators);
            // pop back up out of the Paren
            m.pop(Paren);
          },

        // Float.
        R"re([[:digit:]]+\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?\b)re" >>
          [](auto& m) { m.add(Float); },

        // String.
        R"re("[^"]*")re" >> [](auto& m) { m.add(String); },

        // Int.
        R"re([[:digit:]]+\b)re" >> [](auto& m) { m.add(Int); },

        // Line comment.
        R"re(//[^\n]*\n)re" >> [](auto&) {}, // another no-op

        // Print.
        R"re(print\b)re" >> [](auto& m) { m.add(Print); },

        // Append.
        R"re(append\b)re" >> [](auto& m) { m.add(Append); },

        // Identifier.
        R"re([_[:alpha:]][_[:alnum:]]*\b)re" >> [](auto& m) { m.add(Ident); },

        // Add ('+' is a reserved RegEx character)
        R"re(\+)re" >> [](auto& m) { m.add(Add); },

        // Subtract
        "-" >> [](auto& m) { m.add(Subtract); },

        // Multiply ('*' is a reserved RegEx character)
        R"re(\*)re" >> [](auto& m) { m.add(Multiply); },

        // Divide
        R"re(/)re" >> [](auto& m) { m.add(Divide); },
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

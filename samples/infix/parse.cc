#include "infix.h"
#include "internal.h"
#include "trieste/token.h"

#include <memory>

namespace infix
{
  const std::initializer_list<Token> terminators = {Equals, ParserTuple};

  Parse parser(bool use_parser_tuples)
  {
    Parse p(depth::file, wf_parser);

    std::shared_ptr<size_t> parentheses_level = std::make_shared<size_t>(0);

    auto make_mode = [=, &p](std::string name, bool in_parentheses) -> void {
      p(name, // this indicates the 'mode' these rules are associated with
        {
          // Whitespace between tokens.
          R"(\s+)" >> [](auto&) {}, // no-op

          // Equals.
          R"(=)" >> [](auto& m) { m.seq(Equals); },

          // [tuples only] Commas: might be tuple literals, function calls.
          R"(,)" >>
            [=](auto& m) {
              if (use_parser_tuples)
              {
                if (in_parentheses)
                {
                  m.seq(ParserTuple);
                }
                else
                {
                  m.error("Invalid use of comma");
                }
              }
              else
              {
                m.add(Comma);
              }
            },

          // [tuples only] Tuple indexing.
          R"(\.)" >> [](auto& m) { m.add(TupleIdx); },

          // Terminator.
          R"(;)" >> [](auto& m) { m.term(terminators); },

          // Parens.
          R"(\()" >>
            [=](auto& m) {
              // we push a Paren node. Subsequent nodes will be added
              // as its children.
              m.push(Paren);

              // We are inside parenthese: shift to that mode
              ++*parentheses_level;
              m.mode("parentheses");
            },

          R"(\))" >>
            [=](auto& m) {
              // terminate the current group
              m.term(terminators);
              // pop back up out of the Paren
              m.pop(Paren);

              if (*parentheses_level > 0)
              {
                --*parentheses_level;
              }

              if (*parentheses_level == 0)
              {
                m.mode("start");
              }
            },

          // Float.
          R"([[:digit:]]+\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?\b)" >>
            [](auto& m) { m.add(Float); },

          // String.
          R"("[^"]*")" >> [](auto& m) { m.add(String); },

          // Int.
          R"([[:digit:]]+\b)" >> [](auto& m) { m.add(Int); },

          // Line comment.
          // Note: care is taken to handle all possible line endings: \n, \r\n
          R"(//[^\n\r]*(\r\n|\n))" >> [](auto&) {}, // another no-op

          // Print.
          R"re(print\b)re" >> [](auto& m) { m.add(Print); },

          // Append.
          R"(append\b)" >> [](auto& m) { m.add(Append); },

          // Identifier.
          R"([_[:alpha:]][_[:alnum:]]*\b)" >> [](auto& m) { m.add(Ident); },

          // Add ('+' is a reserved RegEx character)
          R"(\+)" >> [](auto& m) { m.add(Add); },

          // Subtract
          R"(-)" >> [](auto& m) { m.add(Subtract); },

          // Multiply ('*' is a reserved RegEx character)
          R"re(\*)re" >> [](auto& m) { m.add(Multiply); },

          // Divide
          R"(/)" >> [](auto& m) { m.add(Divide); },
        });
    };

    make_mode("start", false);
    make_mode("parentheses", true);

    p.gen({
      Int >> [](auto& rnd) { return std::to_string(rnd() % 100); },
      Float >>
        [](auto& rnd) {
          std::uniform_real_distribution<> dist(-10.0, 10.0);
          return std::to_string(dist(rnd));
        },
    });

    // Remember to reset any parser data you manage after parsing!
    p.postparse([=](auto, auto, auto) { *parentheses_level = 0; });

    return p;
  }
}

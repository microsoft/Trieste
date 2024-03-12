#include "json.h"

namespace trieste::json
{
  Parse parser()
  {
    Parse p(depth::file, wf_parse);
    std::shared_ptr<std::vector<char>> stack =
      std::make_shared<std::vector<char>>();

    p("start",
      {"[ \r\n\t]+" >> [](auto&) { return; },

       ":" >> [](auto& m) { m.add(Colon); },

       "," >> [](auto& m) { m.add(Comma); },

       "{" >>
         [stack](auto& m) {
           if (stack->size() > 500)
           {
             // TODO: Remove this once Trieste can handle deeper stacks
             m.error("Too many nested objects");
             return;
           }
           m.push(Object);
           m.push(Group);
           stack->push_back('{');
         },

       "}" >>
         [stack](auto& m) {
           if (stack->empty() || stack->back() != '{')
           {
             m.error("Mismatched braces");
             return;
           }
           stack->pop_back();
           m.term();
           m.pop(Object);
         },

       R"(\[)" >>
         [stack](auto& m) {
           if (stack->size() > 500)
           {
             // TODO: Remove this once Trieste can handle deeper stacks
             m.error("Too many nested objects");
             return;
           }
           m.push(Array);
           m.push(Group);
           stack->push_back('[');
         },

       "]" >>
         [stack](auto& m) {
           if (stack->empty() || stack->back() != '[')
           {
             m.error("Mismatched brackets");
             return;
           }
           stack->pop_back();
           m.term();
           m.pop(Array);
         },

       "true" >> [](auto& m) { m.add(True); },

       "false" >> [](auto& m) { m.add(False); },

       "null" >> [](auto& m) { m.add(Null); },

       R"(-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)" >>
         [](auto& m) { m.add(Number); },

       R"("(?:[^"\\\x00-\x1F]+|\\["\\\/bfnrt]|\\u[[:xdigit:]]{4})*")" >>
         [](auto& m) { m.add(String); },

       "." >> [](auto& m) { m.error("Invalid character"); }});

    p.done([stack](auto& m) {
      if (!stack->empty())
      {
        m.error("Mismatched braces or brackets");
      }
    });

    return p;
  }
}

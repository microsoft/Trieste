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

       // RE for a JSON number:
       // -? : optional minus sign
       // (?:0|[1-9][0-9]*) : either a single 0, or 1-9 followed by any digits
       // (?:\.[0-9]+)? : optionally, a single period followed by one or more digits (fraction)
       // (?:[eE][-+]?[0-9]+)? : optionally, an exponent. This can start with e or E,
       //                        have +/-/nothing, and then 1 or more digits
       R"(-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)" >>
         [](auto& m) { m.add(Number); },

       // RE for a JSON string:
       // " : a double quote followed by either:
       // 1. [^"\\\x00-\x1F]+ : one or more characters that are not a double quote, backslash,
       //                       or a control character from 00-1f
       // 2. \\["\\\/bfnrt] : a backslash followed by one of the characters ", \, /, b, f, n, r, or t
       // 3. \\u[[:xdigit:]]{4} : a backslash followed by u, followed by 4 hex digits
       // zero or more times and then
       // " : a double quote
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

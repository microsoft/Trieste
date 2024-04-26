#include "shrubbery.h"
#include "wf.h"

// Shrubbery notation constructs that are not supported:
// - Single quotes as opener/closer pairs
// - Line and column insensitivity with << and >>
// - Block comments with #// //#
// - @-notation
// - Keywords prefixed by ~
//
// Other things that could be implemented:
// - Continuing a line with backslash
// - Better parsing of strings
// - Numbers other than integers

namespace shrubbery
{
  // An Indent is a source code location where an indentation level has been
  // established. A line at some indentation level can also be continued if the
  // next line is more indented and starts with an operator. For example, the
  // code
  //
  // f(1) + 2
  //   + 3
  //   - 4
  //
  // is identical to `f(1) + 2 + 3 - 4`. The plus on the second line can
  // arbitrarily indented, but the minus must be in the same column as the plus
  // on the previous line. When parsing the program above, the Indent will be
  // ((0,0), 2), indicating that the established indentation level is line 0
  // colum 0, and that this line is continued on column 2.
  struct Indent {
      Location loc;
      size_t cont;

      Indent(Location l) : cont(0) {
        loc = l;
      }
  };

  Parse parser()
  {
    Parse p(depth::file, wf_parser);

    // A stack of established indentation levels
    auto indent = std::make_shared<std::vector<Indent>>();

    // True iff the next group should establish a new indentation level
    auto expect_indent = std::make_shared<bool>(true);

    // True iff the next group starts a new line
    auto newline = std::make_shared<bool>(false);

    // Check that the current indentation is larger than the previous
    // established indentation level
    auto check_new_indentation = [indent](auto& m) {
        if (!indent->empty()) {
            auto col = m.match().linecol().second;
            auto last_col = indent->back().loc.linecol().second;
            if (col <= last_col) {
                m.error("New indentation level must be larger than the previous");
            }
        }
    };

    // Push a new indentation level. Will only have an effect if we are expecting a new indentation
    auto push_indentation = [check_new_indentation, expect_indent, indent](auto& m) {
        if (!*expect_indent) return;

        *expect_indent = false;

        check_new_indentation(m);

        auto loc = m.match();
        indent->push_back(Indent(loc));
    };

    // Pop the latest established indentation level, unless we are currently expecting a new indentation
    auto pop_indentation = [indent, expect_indent]() {
        // If we have not set an indentation, don't pop
        if (*expect_indent) {
            *expect_indent = false;
            return;
        }
        indent->pop_back();
    };

    // Figure out which indentation level we are currently at (and ensure that it is valid)
    auto match_indentation = [pop_indentation, newline, expect_indent, indent](auto& m) {
        if (!*newline) {
            return true;
        }
        *newline = false;

        auto loc = m.match();
        auto col = loc.linecol().second;

        auto last_loc = indent->back().loc;
        auto last_col = last_loc.linecol().second;

        while (col < last_col) {
            pop_indentation();
            if (indent->empty()) {
                m.error("Indentation is before left-most group");
                indent->push_back(last_loc);
                return false;
            }

            last_loc = indent->back().loc;
            last_col = last_loc.linecol().second;

            m.term({Semi});
            if (m.in(Comma)) {
              m.error("Indentation is before the first group of a comma-separated list");
              return false;
            }

            if (m.in(Block)) {
                m.pop(Block);
                if (col < last_col) m.term();
            } else if (m.in(Alt)) {
                m.pop(Alt);
                if (col < last_col) m.term();
            }
        }

        if (col != last_col) {
            m.error("Group does not match any previous indentation");
            return false;
        }

        return true;
    };

    // Continue the current indentation level. Will only have an effect if we are expecting a new indentation
    auto continue_indentation = [push_indentation, expect_indent, indent](auto& m) {
        if (*expect_indent) {
            return false;
        }

        auto& last_indent = indent->back();
        auto last_col = last_indent.loc.linecol().second;
        auto col = m.match().linecol().second;

        // The continuation is 0 if it has not been set
        if (last_indent.cont == 0) {
            if (col <= last_col) {
                return false;
            }
            last_indent.cont = col;
        } else if (last_indent.cont != col) {
            return false;
        }
        return true;
    };

    // Terminate a given set of Tokens
    auto close_all = [pop_indentation, expect_indent, indent](auto& m, std::initializer_list<Token> tokens) {
      bool progress = true;
      while (progress) {
        progress = false;
        for (auto& token : tokens) {
          if (m.in(token) || m.group_in(token)) {
            m.term({token});
            progress = true;
            // Blocks and alternatives will have established new indentation
            // levels (unless they were just opened), so these need to be popped
            if (!indent->empty() && !*expect_indent && (token == Block || token == Alt)) {
                pop_indentation();
            } else if (*expect_indent) {
                *expect_indent = false;
            }
            break;
          }
        }
      }
      *expect_indent = false;
    };

    // Open a new pair of parentheses, brackets or braces
    auto open_pair = [match_indentation, push_indentation, expect_indent](auto& m) {
        push_indentation(m);

        if (match_indentation(m)) {
            *expect_indent = true;
            return true;
        }
        return false;
    };

    // Close a pair of parentheses, brackets or braces
    auto close_pair = [close_all, match_indentation, pop_indentation, newline](auto &m) {
        pop_indentation();
        if (match_indentation(m)) {
            // Closing parens/brackets/braces close all currently open blocks or
            // alternatives (which may in turn contain semi-colons)
            close_all(m, {Block, Alt, Semi});
            m.term({Comma});
        }
    };

    // Adds a term, defaulting to Atom (anything that is not a special character)
    auto add_term = [match_indentation, push_indentation, newline](auto &m, Token token = Atom) {
        if (*newline)
            m.term();

        push_indentation(m);

        if (match_indentation(m))
            m.add(token);
    };

    p("start",
      {
        // Whitespace between tokens.
        "[[:blank:]]+" >>
          [](auto&) { }, // no-op

        "[\r\n]+" >>
          [newline](auto&) { *newline = true; }, // no-op

        // String literals
        R"("[^"]*")" >>
          [add_term](auto& m) { add_term(m); },

        // Identifiers
        R"([[:alpha:]_][[:alnum:]_]*)" >>
          [add_term](auto& m) { add_term(m); },

        // Integers
        "[[:digit:]]+" >>
          [add_term](auto& m) { add_term(m); },

        // Operators
        R"([!#$%&<>\^?|=+\-*/.:]*[!#$%&<>\^?=*]|[!#$%&<>\^?|=+\-*/.:]+[!#$%&<>\^?|=*]|\.+|\++|-+|::+)" >>
          [add_term, continue_indentation, newline](auto& m) {
              if (*newline && continue_indentation(m)) {
                  *newline = false;
              }
              add_term(m, Op);
          },

        // Opener-closer pairs
        R"(\()" >>
          [open_pair](auto& m) { if (open_pair(m)) m.push(Paren); },

        R"(\[)" >>
          [open_pair](auto& m) { if (open_pair(m)) m.push(Bracket); },

        R"(\{)" >>
          [open_pair](auto& m) { if (open_pair(m)) m.push(Brace); },

        R"(\))" >>
          [close_pair](auto& m) {
              close_pair(m);
              m.pop(Paren);
          },

        R"(\])" >>
          [close_pair](auto& m) {
              close_pair(m);
              m.pop(Bracket);
          },

        R"(\})" >>
          [close_pair](auto& m) {
              close_pair(m);
              m.pop(Brace);
          },

        // Commas separate groups in opener-closer pairs
        "," >>
          [close_all, newline](auto& m) {
              if (*newline) {
                  m.error("A comma cannot start a line");
                  *newline = false;
              }

              // Commas close all currently open blocks or alternatives (which
              // may in turn contain semicolons)
              close_all(m, {Block, Alt, Semi});

              m.seq(Comma);
          },

        // Semicolons separate groups outside of opener-closer pairs
        ";" >>
          [newline, expect_indent](auto& m) {
              if (*newline) {
                  m.error("A semicolon cannot start a line");
                  *newline = false;
              }

              m.seq(Semi);
          },

        // A colon starts a block
        ":" >>
          [match_indentation, expect_indent](auto& m) {
              if (match_indentation(m)) {
                  m.push(Block);
                  *expect_indent = true;
              }
          },

        // Alternatives are separated by bars. They will be parsed as a sequence
        // of individual Alt nodes which are merged into one during rewriting.
        R"(\|)" >>
        [match_indentation, close_all, newline, expect_indent](auto& m) {
            // Alternatives terminate the current alternative (which may in turn
            // contain semi-colons)
            if (!*newline) close_all(m, {Alt, Semi});

            if (match_indentation(m)) {
                m.push(Alt);
                *expect_indent = true;
            }
        }
    });

    // When we are done, close any open blocks, alternatives and
    // semicolon-separated groups and empty the indentation stack
    p.done([close_all, indent](auto& m) {
        close_all(m, {Block, Alt, Semi, Comma});
        while (!indent->empty()) {
            indent->pop_back();
        }
    });

    return p;
  }
}

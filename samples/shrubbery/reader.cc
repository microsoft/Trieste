#include "shrubbery.h"
#include "wf.h"

namespace shrubbery
{

  auto err(const NodeRange& r, const std::string& msg)
  {
    return Error << (ErrorMsg ^ msg) << (ErrorAst << r);
  }

  auto err(Node node, const std::string& msg)
  {
    return Error << (ErrorMsg ^ msg) << (ErrorAst << node);
  }

  PassDef check_parsing()
  {
    return {
      "check parsing",
      wf_check_parser,
      dir::topdown,
      {
        // An empty block followed by alternatives is ignored
        ((T(Block) << End) * (T(Alt)[Alt])) >>
          [](Match& _) { return _(Alt); },

        (T(Block) << (T(Group)[Group] << T(Alt))) >>
          [](Match& _) { return Seq << *_[Group]; },

        // An empty group caused by a semicolon is ignored
        (In(Semi) * ((T(Group) << End))) >>
          [](Match&) { return Seq ^ ""; },

        // Commas must separate (non-empty) groups
        (T(Comma) << End)[Comma] >>
          [](Match& _) { return err(_[Comma], "Comma does not separate groups"); },

        (In(Comma) * (T(Group) << End)[Group]) >>
          [](Match& _) { return err(_[Group], "Comma does not separate groups"); },

        // A comma can only appear inside a paren, brace or bracket
        ((--In(Paren, Brace, Bracket)) * T(Comma)[Comma]) >>
          [](Match& _) { return err(_[Comma], "Commas can only separate groups in parentheses/braces/brackets"); },

        // Opener-closer pairs must have comma-separated groups
        (In(Paren, Brace, Bracket) * Any * Any)[Group] >>
          [](Match& _) { return err(_[Group], "Groups in parentheses/braces/brackets must be comma separated"); },

        // Opener-closer pairs cannot have semicolon-separated groups
        (In(Paren, Brace, Bracket, Comma) * T(Semi))[Semi] >>
          [](Match& _) { return err(_[Semi], "Semicolons cannot separate groups in parentheses/brackets/braces. Use commas."); },

        // Opener-closer pairs may contain empty blocks
        (--(In(Paren, Brace, Bracket, Comma, File))) * ((T(Group) << ((!T(Block))++ * (T(Block)[Block] << End)))) >>
          [](Match& _) { return err(_[Block], "Blocks may not be empty"); },

        // Top-level groups may consist of *only* an empty block
        In(File) * (T(Group) << (((!T(Block)) * (!T(Block))++ * (T(Block)[Block] << End) * End))) >>
          [](Match& _) { return err(_[Block], "Blocks may not be empty"); },

        // Alternatives cannot be empty
        T(Alt)[Alt] << End >>
          [](Match& _) { return err(_[Alt], "Alternatives may not be empty"); },
      }
    };
  }

  // Merge alternatives into a single Alt node containing blocks
  PassDef merge_alternatives()
  {
    return {
      "merge alternatives",
      wf_alternatives,
      dir::topdown,
      {
        // Move a trailing alternative into the preceding group but do not cross
        // a comma or semi-colon
        (--In(Comma, Semi) * T(Group)[Group] * (T(Group) << (T(Alt)[Alt]))) >>
          [](Match& _) { return _(Group) << _(Alt); },

        // Alternatives keep their contents in a block
        (T(Alt)[Alt] << !T(Block)) >>
          [](Match& _) { return Alt << (Block << *_[Alt]); },

        // Merge sequence of alternatives into a single alternative
        (T(Alt)[Alt] * (T(Alt) << T(Block)[Block])) >>
          [](Match& _) { return _(Alt) << _(Block); },
      }
    };
  }

  // Remove nodes for commas and semicolons and replace them by their children.
  // Also check for empty alternatives, which may only appear immediately under
  // braces and brackets
  PassDef drop_separators()
  {
    return {
      "drop separators",
      wf_drop_separators,
      dir::topdown,
      {
        (T(Comma)[Comma]) >>
          [](Match& _) { return Seq << *_[Comma]; },

        T(Semi)[Semi] >>
          [](Match& _) { return Seq << *_[Semi]; },

        // Alternatives can only appear first in a group directly under Braces or Brackets
        (--In(Brace, Bracket)) * T(Group) << T(Alt)[Alt] >>
          [](Match& _) { return err(_[Alt], "Alternative cannot appear first in a group"); },
      }
    };
  }

  Reader reader()
  {
    return {
      "shrubbery",
      {
        check_parsing(),
        merge_alternatives(),
        drop_separators()
      },
      parser(),
    };
  }
}

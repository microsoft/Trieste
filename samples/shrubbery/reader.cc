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
      dir::bottomup | dir::once,
      {
        // An empty block followed by alternatives is ignored
        (T(Group) << (!T(Block))++[Terms] * (T(Block) << End)) * (T(Group) << T(Alt))[Alt] >>
          [](Match& _) { return Seq << (Group << _[Terms])
                                    << _(Alt);
                       },

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

        // Blocks cannot be empty, except immediately under opener-closer pairs
        // and as the only term in a top-level group
        (--(In(Paren, Brace, Bracket, Comma, File))) * ((T(Group) << ((!T(Block))++ * (T(Block)[Block] << End)))) >>
          [](Match& _) { return err(_[Block], "Blocks may not be empty"); },

        In(File) * (T(Group) << (((!T(Block)) * (!T(Block))++ * (T(Block)[Block] << End) * End))) >>
          [](Match& _) { return err(_[Block], "Blocks may not be empty"); },

        // Alternatives cannot be empty
        T(Alt)[Alt] << End >>
          [](Match& _) { return err(_[Alt], "Alternatives may not be empty"); },

      }
    };
  }

  // Alternatives belong to the preceeding Group and keep their contents in
  // blocks
  PassDef merge_alternatives()
  {
    return {
      "merge alternatives",
      wf_alternatives,
      dir::bottomup | dir::once,
      {
        // Alternatives keep their contents in a block
        (T(Alt)[Alt] << !T(Block)) >>
          [](Match& _) { return Alt << (Block << *_[Alt]); },

        // Move a trailing alternatives into the preceding group but do not
        // cross a comma or semi-colon. Merge the alternatives into one
        (--In(Comma, Semi) * T(Group)[Group] * ((T(Group) << T(Alt)[Alt]) * (T(Group) << T(Alt))++[Terms])) >>
          [](Match& _) {
            Node alt = _(Alt);
            for (auto& node : _[Terms]) {
              alt << node->front()->front(); // Group->Alt->Block
            }
            return _(Group) << alt;
          },
      }
    };
  }

  // Remove nodes for commas and semicolons and replace them by their children.
  PassDef drop_separators()
  {
    return {
      "drop separators",
      wf_no_semis_or_commas,
      dir::bottomup | dir::once,
      {
        (T(Comma)[Comma]) >>
          [](Match& _) { return Seq << *_[Comma]; },

        T(Semi)[Semi] >>
          [](Match& _) { return Seq << *_[Semi]; },
      }
    };
  }

  // Check that groups starting with alternatives only appear immediately under
  // braces and brackets
  PassDef check_alternatives()
  {
    return {
      "check alternatives",
      wf_no_semis_or_commas,
      dir::bottomup | dir::once,
      {
        (--In(Brace, Bracket)) * T(Group) << T(Alt)[Alt] >>
          [](Match& _) { return err(_[Alt], "Alternative cannot appear first in a group"); },
      }
    };
  }

  // Structure groups so that they contain their atoms in a Contents node,
  // followed by a
  PassDef group_structure()
  {
    return {
      "group structure",
      wf,
      dir::bottomup | dir::once,
      {
        In(Group) * Start * (!T(Block, Alt))++[Atom] * ~T(Block)[Block] * ~T(Alt)[Alt] * End >>
          [](Match& _) {
            return Seq << (Terms << _[Atom])
                       << (_(Block)? _(Block): None)
                       << (_(Alt)? _(Alt): None);
          },

        // Groups cannot be empty
        T(Group)[Group] << End >>
          [](Match& _) { return err(_[Group], "Groups cannot be empty"); },

        // Overly permissive wf rules from before allows groups to have
        // impossible structure. To pass fuzz testing, we add this rule
        T(Group)[Group] << !T(Terms) >>
          [](Match& _) { return err(_[Group], "Should never happen"); },
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
        drop_separators(),
        check_alternatives(),
        group_structure(),
      },
      parser(),
    };
  }
}

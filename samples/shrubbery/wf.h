#pragma once

#include "shrubbery.h"

namespace shrubbery
{
  using namespace wf::ops;

  inline const auto wf_term = Paren | Bracket | Brace |
                              Block | Alt | Op | Atom;

  inline const auto wf_grouping_construct = Comma | Semi | Group;

  // clang-format off

  // After parsing, commas and semicolons can appear virtually everywhere
  inline const auto wf_parser =
      (Top <<= File)
    | (File <<= wf_grouping_construct++)
    | (Paren <<= wf_grouping_construct++)
    | (Bracket <<= wf_grouping_construct++)
    | (Brace <<= wf_grouping_construct++)
    | (Block <<= wf_grouping_construct++)
    | (Alt <<= wf_grouping_construct++)
    | (Comma <<= (Semi | Group)++)
    | (Semi <<= (Comma | Group)++)
    | (Group <<= wf_term++)
    ;

  // The first pass ensures that commas and semi-colons are in the right places
  inline const auto wf_check_parser =
      wf_parser
    | (File <<= (Group | Semi)++)
    | (Paren <<= (Group | Comma)++)
    | (Bracket <<= (Group | Comma)++)
    | (Brace <<= (Group | Comma)++)
    | (Block <<= (Group | Semi)++)
    | (Alt <<= (Group | Semi)++[1])
    | (Comma <<= Group++[1])
    | (Semi <<= Group++)
    ;

  // Merge alternatives into one node with a sequence of blocks
  inline const auto wf_alternatives =
    wf_check_parser
    | (Alt <<= Block++[1])
    ;

  // Get rid of commas and semi-colons
  inline const auto wf_no_semis_or_commas =
    wf_alternatives;

  inline const auto wf_drop_separators =
    wf_alternatives
    | (File <<= Group++)
    | (Paren <<= Group++)
    | (Bracket <<= Group++)
    | (Brace <<= Group++)
    | (Block <<= Group++)
    ;
  // clang-format on
}

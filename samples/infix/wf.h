#pragma once

#include "lang.h"

namespace infix
{
  using namespace wf::ops;

  // | is used to create a Choice between all the elements
  // this indicates that literals can be an Int or a Float
  inline const auto wf_literal = Int | Float;

  inline const auto wf_parse_tokens = wf_literal | String | Paren | Print |
    Ident | Add | Subtract | Divide | Multiply;

  // clang-format off

  // A <<= B indicates that B is a child of A
  // ++ indicates that there are zero or more instances of the token
  inline const auto wf_parser =
      (Top <<= File)
    | (File <<= (Group | Equals)++)
    | (Paren <<= Group++)
    | (Equals <<= Group++)
    | (Group <<= wf_parse_tokens++)
    ;
  // clang-format on

  inline const auto wf_expressions_tokens =
    (wf_parse_tokens - (String | Paren | Print)) | Expression;

  // clang-format off
  inline const auto wf_pass_expressions =
      (Top <<= Calculation)
    | (Calculation <<= (Assign | Output)++)
    // [Ident] here indicates that the Ident node is a symbol that should
    // be stored in the symbol table  
    | (Assign <<= Ident * Expression)[Ident]
    | (Output <<= String * Expression)
    // [1] here indicates that there should be at least one token
    | (Expression <<= wf_expressions_tokens++[1])
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_multiply_divide =
    wf_pass_expressions
    | (Multiply <<= Expression * Expression)
    | (Divide <<= Expression * Expression)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_add_subtract =
    wf_pass_multiply_divide
    | (Add <<= Expression * Expression)
    | (Subtract <<= Expression * Expression)
    ;
  // clang-format on

  inline const auto wf_operands_tokens = wf_expressions_tokens - Expression;

  // clang-format off
  inline const auto wf_pass_trim =
    wf_pass_add_subtract
    | (Expression <<= wf_operands_tokens)
    ;
  //clang-format on

  inline const auto wf_check_refs_tokens = (wf_operands_tokens - Ident) | Ref;

  // clang-format off
  inline const auto wf_pass_check_refs =
    wf_pass_trim
    | (Expression <<= wf_check_refs_tokens)
    | (Ref <<= Ident)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_maths = 
    wf_pass_check_refs 
    | (Assign <<= Ident * Literal) 
    | (Output <<= String * Literal) 
    | (Literal <<= wf_literal)
    ;
  // clang-format on

  // clang-format off
  inline const auto wf_pass_cleanup =
    wf_pass_maths 
    | (Calculation <<= Output++) 
    // note the use of >>= here. This allows us to have a choice
    // as a field by giving it a temporary name.
    | (Output <<= String * (Expression >>= wf_literal))
    ;
  // clang-format on
}
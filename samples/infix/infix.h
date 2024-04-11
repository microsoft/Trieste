#pragma once

#include <trieste/trieste.h>

namespace infix
{
  using namespace trieste;

  inline const auto Int = TokenDef("infix-int", flag::print);
  inline const auto Float = TokenDef("infix-float", flag::print);
  inline const auto String = TokenDef("infix-string", flag::print);
  inline const auto Ident = TokenDef("infix-ident", flag::print);

  inline const auto Calculation =
    TokenDef("infix-calculation", flag::symtab | flag::defbeforeuse);
  inline const auto Expression = TokenDef("infix-expression");
  inline const auto Assign =
    TokenDef("infix-assign", flag::lookup | flag::shadowing);
  inline const auto Output = TokenDef("infix-output");
  inline const auto Ref = TokenDef("infix-ref");

  inline const auto Add = TokenDef("infix-add");
  inline const auto Subtract = TokenDef("infix-subtract");
  inline const auto Multiply = TokenDef("infix-multiply");
  inline const auto Divide = TokenDef("infix-divide");
  inline const auto Literal = TokenDef("infix-literal");

  inline const auto Id = TokenDef("infix-id");
  inline const auto Op = TokenDef("infix-op");
  inline const auto Lhs = TokenDef("infix-lhs");
  inline const auto Rhs = TokenDef("infix-rhs");

  // clang-format off
  const auto wf =
    (Top <<= Calculation)
    | (Calculation <<= (Assign | Output)++)
    | (Assign <<= Ident * Expression)[Ident]
    | (Output <<= String * Expression)
    | (Expression <<= (Add | Subtract | Multiply | Divide | Ref | Float | Int))
    | (Ref <<= Ident)
    | (Add <<= Expression * Expression)
    | (Subtract <<= Expression * Expression)
    | (Multiply <<= Expression * Expression)
    | (Divide <<= Expression * Expression)
    ;
  // clang-format off

  Reader reader();
  Writer writer(const std::filesystem::path& path = "infix");
  Writer postfix_writer(const std::filesystem::path& path = "postfix");
  Rewriter calculate();
}
#pragma once

#include <trieste/driver.h>

namespace infix
{
  using namespace trieste;

  inline const auto Paren = TokenDef("paren");
  inline const auto Equals = TokenDef("equals");

  inline const auto Int = TokenDef("int", flag::print);
  inline const auto Float = TokenDef("float", flag::print);
  inline const auto String = TokenDef("string", flag::print);
  inline const auto Ident = TokenDef("ident", flag::print);

  inline const auto Print = TokenDef("print");

  inline const auto Calculation =
    TokenDef("calculation", flag::symtab | flag::defbeforeuse);
  inline const auto Expression = TokenDef("expression");
  inline const auto Assign =
    TokenDef("assign", flag::lookup | flag::shadowing);
  inline const auto Output = TokenDef("output");
  inline const auto Ref = TokenDef("ref");

  inline const auto Add = TokenDef("+");
  inline const auto Subtract = TokenDef("-");
  inline const auto Multiply = TokenDef("*");
  inline const auto Divide = TokenDef("/");
  inline const auto Literal = TokenDef("literal");

  inline const auto Id = TokenDef("id");
  inline const auto Op = TokenDef("op");
  inline const auto Lhs = TokenDef("lhs");
  inline const auto Rhs = TokenDef("rhs");

  Parse parser();
  Driver& driver();
}
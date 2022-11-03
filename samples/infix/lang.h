#pragma once

#include <trieste/driver.h>

namespace infix
{
  using namespace trieste;

  inline constexpr auto Paren = TokenDef("paren");
  inline constexpr auto Equals = TokenDef("equals");

  inline constexpr auto Int = TokenDef("int", flag::print);
  inline constexpr auto Float = TokenDef("float", flag::print);
  inline constexpr auto String = TokenDef("string", flag::print);
  inline constexpr auto Ident = TokenDef("ident", flag::print);

  inline constexpr auto Print = TokenDef("print");

  inline constexpr auto Calculation =
    TokenDef("calculation", flag::symtab | flag::defbeforeuse);
  inline constexpr auto Expression = TokenDef("expression");
  inline constexpr auto Assign =
    TokenDef("assign", flag::lookup | flag::shadowing);
  inline constexpr auto Output = TokenDef("output");
  inline constexpr auto Ref = TokenDef("ref");

  inline constexpr auto Add = TokenDef("+");
  inline constexpr auto Subtract = TokenDef("-");
  inline constexpr auto Multiply = TokenDef("*");
  inline constexpr auto Divide = TokenDef("/");
  inline constexpr auto Literal = TokenDef("literal");

  inline constexpr auto Id = TokenDef("id");
  inline constexpr auto Op = TokenDef("op");
  inline constexpr auto Lhs = TokenDef("lhs");
  inline constexpr auto Rhs = TokenDef("rhs");

  Parse parser();
  Driver& driver();
}
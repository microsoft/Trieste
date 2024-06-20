#pragma once

#include "CLI/App.hpp"
#include "trieste/token.h"

#include <CLI/CLI.hpp>
#include <trieste/trieste.h>

namespace infix
{
  using namespace trieste;

  struct Config
  {
    bool use_parser_tuples = false;
    bool enable_tuples = false;
    bool tuples_require_parens = false;

    inline void sanity() const
    {
      if (tuples_require_parens)
      {
        assert(enable_tuples);
      }
      if (use_parser_tuples)
      {
        assert(enable_tuples && tuples_require_parens);
      }
    }

    inline void install_cli(CLI::App* app)
    {
      app->add_flag("--enable-tuples", enable_tuples, "Enable tuple parsing");
      app->add_flag(
        "--use-parser-tuples",
        use_parser_tuples,
        "Capture tuples in the parser");
      app->add_flag(
        "--tuples-require-parens",
        tuples_require_parens,
        "Tuples must be enclosed in parens");
    }
  };

  inline const auto Int = TokenDef("infix-int", flag::print);
  inline const auto Float = TokenDef("infix-float", flag::print);
  inline const auto String = TokenDef("infix-string", flag::print);
  inline const auto Ident = TokenDef("infix-ident", flag::print);

  inline const auto Calculation =
    TokenDef("infix-calculation", flag::symtab | flag::defbeforeuse);
  inline const auto Expression = TokenDef("infix-expression");
  inline const auto Assign =
    TokenDef("infix-assign", flag::lookup | flag::shadowing);
  inline const auto FnDef =
    TokenDef("infix-fndef", flag::lookup | flag::shadowing | flag::symtab);
  inline const auto Output = TokenDef("infix-output");
  inline const auto Ref = TokenDef("infix-ref");

  inline const auto FnArguments = TokenDef("infix-fnarguments", flag::lookup);
  inline const auto FnBody = TokenDef("infix-fnbody");

  inline const auto Tuple = TokenDef("infix-tuple");
  inline const auto TupleIdx = TokenDef("infix-tupleidx");
  inline const auto TupleAppend = TokenDef("infix-tupleappend");
  inline const auto Add = TokenDef("infix-add");
  inline const auto Subtract = TokenDef("infix-subtract");
  inline const auto Multiply = TokenDef("infix-multiply");
  inline const auto Divide = TokenDef("infix-divide");
  inline const auto Literal = TokenDef("infix-literal");
  inline const auto FnCall = TokenDef("infix-fncall");

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
    // --- tuples extension ---
    | (Expression <<= (Tuple | TupleIdx | TupleAppend | Add | Subtract | Multiply | Divide | Ref | Float | Int))
    | (Tuple <<= Expression++)
    | (TupleIdx <<= Expression * Expression)
    | (TupleAppend <<= Expression * Expression)
    // --- functions extension --- (TODO)
    // | (Calculation <<= (Assign | Output | FnDef)++)
    // | (FnDef <<= Ident * FnArguments * FnBody)
    // | (FnArguments <<= Ident++)
    // | (FnBody <<= (Assign | Output)++)
    // | (Expression <<= (FnCall | Tuple | TupleIdx | TupleAppend | Add | Subtract | Multiply | Divide | Ref | Float | Int))
    // | (FnCall <<= Expression * Expression)
    // --- patterns extension ---
    // TODO: I don't feel like predicting this far ahead right now. With 2 versions laid out I think I have the idea.
    ;
  // clang-format off

  Reader reader(const Config& config);
  Writer writer(const std::filesystem::path& path = "infix");
  Writer postfix_writer(const std::filesystem::path& path = "postfix");
  Rewriter calculate();
}
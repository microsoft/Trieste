// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <trieste/driver.h>

namespace verona
{
  using namespace trieste;

  // Parsing structure.
  inline constexpr auto Paren = TokenDef("paren");
  inline constexpr auto Square = TokenDef("square");
  inline constexpr auto Brace = TokenDef("brace");
  inline constexpr auto List = TokenDef("list");
  inline constexpr auto Equals = TokenDef("equals");

  // Parsing literals.
  inline constexpr auto DontCare = TokenDef("dontcare");
  inline constexpr auto Dot = TokenDef("dot");
  inline constexpr auto Ellipsis = TokenDef("ellipsis");
  inline constexpr auto Colon = TokenDef("colon");
  inline constexpr auto DoubleColon = TokenDef("doublecolon");
  inline constexpr auto TripleColon = TokenDef("triplecolon");
  inline constexpr auto Arrow = TokenDef("arrow");
  inline constexpr auto Bool = TokenDef("bool", flag::print);
  inline constexpr auto Hex = TokenDef("hex", flag::print);
  inline constexpr auto Bin = TokenDef("bin", flag::print);
  inline constexpr auto Int = TokenDef("int", flag::print);
  inline constexpr auto HexFloat = TokenDef("hexfloat", flag::print);
  inline constexpr auto Float = TokenDef("float", flag::print);
  inline constexpr auto Char = TokenDef("char", flag::print);
  inline constexpr auto Escaped = TokenDef("escaped", flag::print);
  inline constexpr auto String = TokenDef("string", flag::print);
  inline constexpr auto Symbol = TokenDef("symbol", flag::print);
  inline constexpr auto Ident = TokenDef("ident", flag::print);

  // Parsing keywords.
  inline constexpr auto Class = TokenDef(
    "class", flag::symtab | flag::lookup | flag::lookdown | flag::shadowing);
  inline constexpr auto TypeAlias = TokenDef(
    "typealias",
    flag::symtab | flag::lookup | flag::lookdown | flag::shadowing);
  inline constexpr auto Use = TokenDef("use");
  inline constexpr auto Package = TokenDef("package");
  inline constexpr auto Var = TokenDef("var", flag::lookup | flag::shadowing);
  inline constexpr auto Let = TokenDef("let", flag::lookup | flag::shadowing);
  inline constexpr auto Ref = TokenDef("ref");
  inline constexpr auto Lin = TokenDef("lin");
  inline constexpr auto In_ = TokenDef("in");
  inline constexpr auto Out = TokenDef("out");
  inline constexpr auto Const = TokenDef("const");
  inline constexpr auto If = TokenDef("if");
  inline constexpr auto Else = TokenDef("else");
  inline constexpr auto New = TokenDef("new");
  inline constexpr auto Try = TokenDef("try");

  // Semantic structure.
  inline constexpr auto TypeTrait = TokenDef(
    "typetrait",
    flag::symtab | flag::lookup | flag::lookdown | flag::shadowing);
  inline constexpr auto ClassBody = TokenDef("classbody");
  inline constexpr auto FieldLet = TokenDef("fieldlet", flag::lookdown);
  inline constexpr auto FieldVar = TokenDef("fieldvar", flag::lookdown);
  inline constexpr auto Function = TokenDef(
    "function",
    flag::symtab | flag::defbeforeuse | flag::lookup | flag::lookdown);
  inline constexpr auto TypeParams = TokenDef("typeparams");
  inline constexpr auto TypeParam =
    TokenDef("typeparam", flag::lookup | flag::lookdown | flag::shadowing);
  inline constexpr auto Params = TokenDef("params");
  inline constexpr auto Param =
    TokenDef("param", flag::lookup | flag::shadowing);
  inline constexpr auto Block =
    TokenDef("block", flag::symtab | flag::defbeforeuse);

  // Type structure.
  inline constexpr auto Type = TokenDef("type");
  inline constexpr auto TypeUnit = TokenDef("typeunit");
  inline constexpr auto TypeList = TokenDef("typelist");
  inline constexpr auto TypeClassName = TokenDef("typeclassname");
  inline constexpr auto TypeAliasName = TokenDef("typealiasname");
  inline constexpr auto TypeParamName = TokenDef("typeparamname");
  inline constexpr auto TypeTraitName = TokenDef("typetraitname");
  inline constexpr auto TypeTuple = TokenDef("typetuple");
  inline constexpr auto TypeView = TokenDef("typeview");
  inline constexpr auto TypeFunc = TokenDef("typefunc");
  inline constexpr auto TypeIsect = TokenDef("typeisect");
  inline constexpr auto TypeUnion = TokenDef("typeunion");
  inline constexpr auto TypeVar = TokenDef("typevar", flag::print);
  inline constexpr auto TypeEmpty = TokenDef("typeempty");

  // Expression structure.
  inline constexpr auto Expr = TokenDef("expr");
  inline constexpr auto ExprSeq = TokenDef("exprseq");
  inline constexpr auto TypeAssert = TokenDef("typeassert");
  inline constexpr auto TypeArgs = TokenDef("typeargs");
  inline constexpr auto Lambda =
    TokenDef("lambda", flag::symtab | flag::defbeforeuse);
  inline constexpr auto Tuple = TokenDef("tuple");
  inline constexpr auto Unit = TokenDef("unit");
  inline constexpr auto Assign = TokenDef("assign");
  inline constexpr auto RefVar = TokenDef("refvar");
  inline constexpr auto RefLet = TokenDef("reflet");
  inline constexpr auto FunctionName = TokenDef("funcname");
  inline constexpr auto Selector = TokenDef("selector");
  inline constexpr auto Call = TokenDef("call");
  inline constexpr auto Args = TokenDef("args");
  inline constexpr auto TupleLHS = TokenDef("tuple-lhs");
  inline constexpr auto CallLHS = TokenDef("call-lhs");
  inline constexpr auto RefVarLHS = TokenDef("refvar-lhs");
  inline constexpr auto TupleFlatten = TokenDef("tupleflatten");
  inline constexpr auto Conditional = TokenDef("conditional");
  inline constexpr auto FieldRef = TokenDef("fieldref");
  inline constexpr auto Bind = TokenDef("bind", flag::lookup | flag::shadowing);
  inline constexpr auto Move = TokenDef("move");
  inline constexpr auto Copy = TokenDef("copy");
  inline constexpr auto Drop = TokenDef("drop");
  inline constexpr auto TypeTest = TokenDef("typetest");
  inline constexpr auto Cast = TokenDef("cast");
  inline constexpr auto Return = TokenDef("return");
  inline constexpr auto NLRCheck = TokenDef("nlrcheck");

  // LLVM-specific.
  inline constexpr auto LLVM = TokenDef("llvm", flag::print);
  inline constexpr auto LLVMList = TokenDef("llvmlist");
  inline constexpr auto LLVMFuncType = TokenDef("llvmfunctype");

  // Indexing names.
  inline constexpr auto Bound = TokenDef("bound");
  inline constexpr auto Default = TokenDef("default");

  // Rewrite identifiers.
  inline constexpr auto Id = TokenDef("Id");
  inline constexpr auto Lhs = TokenDef("Lhs");
  inline constexpr auto Rhs = TokenDef("Rhs");
  inline constexpr auto Op = TokenDef("Op");

  // Sythetic locations.
  inline const auto standard = Location("std");
  inline const auto cell = Location("cell");
  inline const auto create = Location("create");
  inline const auto apply = Location("apply");
  inline const auto load = Location("load");
  inline const auto store = Location("store");
  inline const auto nonlocal = Location("nonlocal");

  Parse parser();
  Driver& driver();
}

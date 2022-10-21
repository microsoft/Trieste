// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "lang.h"

namespace verona
{
  using namespace wf::ops;

  inline constexpr auto wfIdSym = IdSym >>= Ident | Symbol;
  inline constexpr auto wfDefault = Default >>= Expr | DontCare;

  inline constexpr auto wfLiteral =
    Bool | Int | Hex | Bin | Float | HexFloat | Char | Escaped | String;

  // clang-format off
  inline constexpr auto wfParser =
      (Top <<= (Directory | File)++)
    | (Directory <<= (Directory | File)++)
    | (File <<= (Group | List | Equals)++)
    | (Brace <<= (Group | List | Equals)++)
    | (Paren <<= (Group | List | Equals)++)
    | (Square <<= (Group | List | Equals)++)
    | (List <<= (Group | Equals)++)
    | (Equals <<= Group++)
    | (Group <<=
        (wfLiteral | Brace | Paren | Square | List | Equals | Arrow | Use |
         Class | TypeAlias | Var | Let | Ref | Throw | Lin | In_ | Out | Const |
         If | Else | New | DontCare | Ident | Ellipsis | Dot | DoubleColon |
         Symbol | Colon | Package)++)
    ;
  // clang-format on

  // Remove Colon. Add Type.
  inline constexpr auto wfModulesTokens = wfLiteral | Brace | Paren | Square |
    List | Equals | Arrow | Use | Class | TypeAlias | Var | Let | Ref | Throw |
    Lin | In_ | Out | Const | If | Else | New | DontCare | Ident | Ellipsis |
    Dot | DoubleColon | Symbol | Package | Type;

  // clang-format off
  inline constexpr auto wfPassModules =
      (Top <<= Group++)
    | (Brace <<= (Group | List | Equals)++)
    | (Paren <<= (Group | List | Equals)++)
    | (Square <<= (Group | List | Equals)++)
    | (List <<= (Group | Equals)++)
    | (Equals <<= Group++)
    | (Package <<= String | Escaped)
    | (Type <<= wfModulesTokens++)
    | (Group <<= wfModulesTokens++)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassStructure =
      (Top <<= Class++)
    | (Class <<= Ident * TypeParams * Type * ClassBody)[Ident]
    | (ClassBody <<=
        (Use | Class | TypeAlias | TypeTrait | FieldLet | FieldVar |
         Function)++)
    | (Use <<= Type)[Include]
    | (TypeAlias <<= Ident * TypeParams * (Bound >>= Type) * Type)[Ident]
    | (TypeTrait <<= Ident * ClassBody)[Ident]
    | (FieldLet <<= Ident * Type * wfDefault)[Ident]
    | (FieldVar <<= Ident * Type * wfDefault)[Ident]
    | (Function <<= wfIdSym * TypeParams * Params * Type * FuncBody)[IdSym]
    | (TypeParams <<= TypeParam++)
    | (TypeParam <<= Ident * (Bound >>= Type) * Type)[Ident]
    | (Params <<= Param++)
    | (Param <<= Ident * Type * wfDefault)[Ident]
    | (TypeTuple <<= Type++)
    | (FuncBody <<= (Use | Class | TypeAlias | Expr)++)
    | (ExprSeq <<= Expr++[2])
    | (Tuple <<= Expr++)
    | (Assign <<= Expr++[2])
    | (TypeArgs <<= Type++)
    | (Lambda <<= TypeParams * Params * FuncBody)
    | (Let <<= Ident)[Ident]
    | (Var <<= Ident)[Ident]
    | (Throw <<= Expr)
    | (TypeAssert <<= Expr * Type)
    | (Type <<=
        (Type | TypeTuple | TypeVar | TypeArgs | Package | Lin | In_ | Out |
         Const | DontCare | Ellipsis | Ident | Symbol | Dot | Throw |
         DoubleColon)++)
    | (Expr <<=
        (Expr | ExprSeq | Tuple | Assign | TypeArgs | Lambda | Let | Var |
         Throw | If | Else | New | Ref | DontCare | Ellipsis | Dot | Ident |
         Symbol | DoubleColon | wfLiteral | TypeAssert)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeView =
      wfPassStructure

    // Add TypeName, TypeView, TypeList.
    | (TypeName <<= (TypeName >>= (TypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeView <<= (Lhs >>= Type) * (Rhs >>= Type))
    | (TypeList <<= Type)

    // Remove DontCare, Ident, TypeArgs, DoubleColon, Dot, Ellipsis from Type.
    | (Type <<=
        (Type | TypeTuple | TypeVar | Package | Lin | In_ | Out | Const |
         Symbol | Throw | TypeName | TypeView | TypeList)++)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeFunc =
      wfPassTypeView

    // Add TypeFunc.
    | (TypeFunc <<= (Lhs >>= Type) * (Rhs >>= Type))
    | (Type <<=
        (Type | TypeTuple | TypeVar | Package | Lin | In_ | Out | Const |
         Symbol | Throw | TypeName | TypeView | TypeList | TypeFunc)++)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeThrow =
      wfPassTypeFunc

    // Add TypeThrow.
    | (TypeThrow <<= Type)
    | (Type <<=
        (Type | TypeTuple | TypeVar | Package | Lin | In_ | Out | Const |
         Symbol | TypeThrow | TypeName | TypeView | TypeList | TypeFunc)++)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeAlg =
      wfPassTypeThrow

    // Add TypeUnion, TypeIsect.
    | (TypeUnion <<= Type++[2])
    | (TypeIsect <<= Type++[2])

    // Remove Symbol, add TypeUnion and TypeIsect.
    | (Type <<=
        (Type | TypeTuple | TypeVar | Package | Lin | In_ | Out | Const |
         TypeThrow | TypeName | TypeView | TypeList | TypeFunc | TypeUnion |
         TypeIsect)++)
    ;
  // clang-format on

  inline constexpr auto wfTypeNoAlg = TypeTuple | TypeVar | Package | Lin |
    In_ | Out | Const | TypeName | TypeView | TypeList | TypeFunc | TypeUnit;

  inline constexpr auto wfType =
    wfTypeNoAlg | TypeUnion | TypeIsect | TypeThrow;

  // clang-format off
  inline constexpr auto wfPassTypeFlat =
      wfPassTypeAlg

    // No Type nodes inside of type structure.
    | (TypeList <<= wfType)
    | (TypeTuple <<= wfType++[2])
    | (TypeView <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeFunc <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeUnion <<= (wfTypeNoAlg | TypeIsect | TypeThrow)++[2])
    | (TypeThrow <<= wfTypeNoAlg | TypeIsect | TypeUnion)
    | (TypeIsect <<= (wfTypeNoAlg | TypeUnion | TypeThrow)++[2])

    // Types are no longer sequences.
    | (Type <<= wfType)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeDNF =
      wfPassTypeFlat

    // Disjunctive normal form.
    | (TypeUnion <<= (wfTypeNoAlg | TypeIsect | TypeThrow)++[2])
    | (TypeThrow <<= wfTypeNoAlg | TypeIsect)
    | (TypeIsect <<= wfTypeNoAlg++[2])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassReference =
      wfPassTypeDNF

    // Add RefLet, RefVar, Selector, FunctionName, TypeAssertOp.
    | (RefLet <<= Ident)
    | (RefVar <<= Ident)
    | (Selector <<= wfIdSym * TypeArgs)
    | (FunctionName <<= (TypeName >>= (TypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeAssertOp <<= (Op >>= Selector | FunctionName) * Type)

    // Remove TypeArgs, Ident, Symbol, DoubleColon.
    // Add RefVar, RefLet, Selector, FunctionName, TypeAssertOp.
    | (Expr <<=
        (Expr | ExprSeq | Tuple | Assign | Lambda | Let | Var | Throw | If |
         Else | New | Ref | DontCare | Ellipsis | Dot | wfLiteral | TypeAssert |
         RefVar | RefLet | Selector | FunctionName | TypeAssertOp)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassReverseApp =
      wfPassReference

    // Add Call, Args.
    | (Call <<=
        (Selector >>= (New | Selector | FunctionName | TypeAssertOp)) * Args)
    | (Args <<= Expr++)

    // Remove Dot. Add Call.
    | (Expr <<=
        (Expr | ExprSeq | Tuple | Assign | Lambda | Let | Var | Throw | If |
         Else | New | Ref | DontCare | Ellipsis | wfLiteral | TypeAssert |
         RefVar | RefLet | Selector | FunctionName | TypeAssertOp | Call)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassApplication =
      wfPassReverseApp

    // Add Conditional.
    | (Conditional <<= Expr * (Lhs >>= Lambda) * (Rhs >>= Conditional | Lambda))

    // Remove DontCare, Ellipsis. Add Conditional, TupleFlatten.
    | (Expr <<=
        (Expr | ExprSeq | Tuple | Assign | Lambda | Let | Var | Throw | If |
         Else | New | Ref | wfLiteral | TypeAssert | RefVar | RefLet |
         Selector | FunctionName | TypeAssertOp | Call | Conditional |
         TupleFlatten)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassAssignLHS =
      wfPassApplication

    // Add RefVarLHS, TupleLHS, CallLHS.
    | (RefVarLHS <<= Ident)
    | (TupleLHS <<= Expr++[2])
    | (CallLHS <<=
        (Selector >>= (Selector | FunctionName | TypeAssertOp)) * Args)

    // Remove Expr, Ref, If, Else, New. Add TupleLHS, CallLHS, RefVarLHS.
    | (Expr <<=
        ExprSeq | Tuple | Assign | Lambda | Let | Var | Throw | wfLiteral |
        TypeAssert | RefVar | RefLet | Selector | FunctionName | TypeAssertOp |
        Call | Conditional | TupleFlatten | TupleLHS | CallLHS | RefVarLHS)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassLocalVar =
      wfPassAssignLHS

    // Remove Var, RefVar, RefVarLHS.
    | (Expr <<=
        ExprSeq | Tuple | Assign | Lambda | Let | Throw | wfLiteral |
        TypeAssert | RefLet | Selector | FunctionName | TypeAssertOp | Call |
        Conditional | TupleFlatten | TupleLHS | CallLHS)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassAssignment =
      wfPassLocalVar

    // Add Bind.
    | (Bind <<= Ident * Type * Expr)[Ident]

    // Remove Assign, Let, TupleLHS. Add Bind.
    | (Expr <<=
        ExprSeq | Tuple | Lambda | Throw | wfLiteral | TypeAssert | RefLet |
        Selector | FunctionName | TypeAssertOp | Call | Conditional |
        TupleFlatten | CallLHS | Bind)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassANF =
      wfPassAssignment
    | (FuncBody <<=
        (Use | Class | TypeAlias | TypeAssert | Bind | RefLet | Throw)++)
    | (Tuple <<= (RefLet | TupleFlatten)++)
    | (TupleFlatten <<= RefLet)
    | (Throw <<= RefLet)
    | (Args <<= RefLet++)
    | (Conditional <<=
        (If >>= RefLet) * (Lhs >>= Lambda) * (Rhs >>= Lambda | Conditional))
    | (TypeAssert <<= Ident * Type)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          RefLet | Tuple | Lambda | Call | Conditional | CallLHS | Selector |
          FunctionName | wfLiteral))[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassDrop =
      wfPassANF

    // Add Copy, Move, Drop. Remove RefLet.
    | (Copy <<= Ident)
    | (Move <<= Ident)
    | (Drop <<= Ident)
    | (FuncBody <<=
        (Use | Class | TypeAlias | TypeAssert | Bind | Throw | Move | Drop)++)
    | (Tuple <<= (RefLet | TupleFlatten | Move)++)
    | (TupleFlatten <<= RefLet | Move)
    | (Throw <<= Copy | Move)
    | (Args <<= (Copy | Move)++)
    | (Conditional <<=
        (If >>= Copy | Move) *
        (Lhs >>= Lambda) *
        (Rhs >>= Lambda | Conditional))
    | (Bind <<= Ident * Type *
        (Rhs >>=
          Tuple | Lambda | Call | Conditional | CallLHS | Selector |
          FunctionName | wfLiteral | Copy | Move))[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wf =
      (TypeAlias <<= Ident * TypeParams * (Bound >>= Type) * (Default >>= Type))
    | (Class <<= Ident * TypeParams * Type * ClassBody)
    | (FieldLet <<= Ident * Type * Expr)
    | (FieldVar <<= Ident * Type * Expr)
    | (Function <<= wfIdSym * TypeParams * Params * (Type >>= wfType) * FuncBody)
    | (Param <<= Ident * Type * Expr)
    | (Type <<= wfType)
    | (TypeName <<= (TypeName >>= (TypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeView <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeFunc <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeThrow <<= (Type >>= wfType))
    | (TypeTrait <<= Ident * ClassBody)
    | (Package <<= (Id >>= String | Escaped))
    | (Var <<= Ident * Type)
    | (Let <<= Ident * Type)
    | (Throw <<= Expr)
    | (Lambda <<= TypeParams * Params * FuncBody)
    | (TypeAssert <<= RefLet * Type)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          RefLet | Tuple | Lambda | Call | Conditional | CallLHS | Selector |
          FunctionName | wfLiteral))
    ;
  // clang-format on
}

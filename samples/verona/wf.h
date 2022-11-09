// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "lang.h"

namespace verona
{
  using namespace wf::ops;

  inline constexpr auto wfIdSym = IdSym >>= Ident | Symbol;
  inline constexpr auto wfDefault = Default >>= Block | DontCare;

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
         Symbol | Colon)++)
    ;
  // clang-format on

  // Remove Colon. Add Type.
  inline constexpr auto wfModulesTokens = wfLiteral | Brace | Paren | Square |
    List | Equals | Arrow | Use | Class | TypeAlias | Var | Let | Ref | Throw |
    Lin | In_ | Out | Const | If | Else | New | DontCare | Ident | Ellipsis |
    Dot | DoubleColon | Symbol | Type;

  // clang-format off
  inline constexpr auto wfPassModules =
      (Top <<= Group++)
    | (Brace <<= (Group | List | Equals)++)
    | (Paren <<= (Group | List | Equals)++)
    | (Square <<= (Group | List | Equals)++)
    | (List <<= (Group | Equals)++)
    | (Equals <<= Group++)
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
    | (Function <<= wfIdSym * TypeParams * Params * Type * Block)[IdSym]
    | (TypeParams <<= TypeParam++)
    | (TypeParam <<= Ident * (Bound >>= Type) * Type)[Ident]
    | (Params <<= Param++)
    | (Param <<= Ident * Type * wfDefault)[Ident]
    | (TypeTuple <<= Type++)
    | (Block <<= (Use | Class | TypeAlias | Expr)++[1])
    | (ExprSeq <<= Expr++[2])
    | (Tuple <<= Expr++[2])
    | (Assign <<= Expr++[2])
    | (TypeArgs <<= Type++)
    | (Lambda <<= TypeParams * Params * Block)
    | (Let <<= Ident)[Ident]
    | (Var <<= Ident)[Ident]
    | (Throw <<= Expr)
    | (TypeAssert <<= Expr * Type)
    | (Package <<= String | Escaped)
    | (Type <<=
        (Type | TypeTuple | TypeVar | TypeArgs | Package | Lin | In_ | Out |
         Const | DontCare | Ellipsis | Ident | Symbol | Dot | Arrow | Throw |
         DoubleColon)++)
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | TypeArgs | If | Else |
         Lambda | Let | Var | Throw | New | Ref | DontCare | Ellipsis | Dot |
         Ident | Symbol | DoubleColon | wfLiteral | TypeAssert)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeView =
      wfPassStructure

    // Add TypeName, TypeView, TypeList.
    | (TypeName <<= (TypeName >>= (TypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeView <<= (Lhs >>= Type) * (Rhs >>= Type))
    | (TypeList <<= Type)

    // Remove DontCare, Ident, TypeArgs, DoubleColon, Dot, Ellipsis.
    | (Type <<=
        (Type | TypeTuple | TypeVar | Package | Lin | In_ | Out | Const |
         Symbol | Arrow | Throw | TypeName | TypeView | TypeList)++)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeFunc =
      wfPassTypeView

    // Remove Arrow. Add TypeFunc.
    | (TypeFunc <<= (Lhs >>= Type) * (Rhs >>= Type))
    | (Type <<=
        (Type | TypeTuple | TypeVar | Package | Lin | In_ | Out | Const |
         Symbol | Throw | TypeName | TypeView | TypeList | TypeFunc)++)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeThrow =
      wfPassTypeFunc

    // Remove Throw. Add TypeThrow.
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

    // Remove Symbol. Add TypeUnion and TypeIsect.
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
  inline constexpr auto wfPassConditionals =
      wfPassTypeDNF

    // Add Conditional, TypeTest, Cast.
    | (Conditional <<= (If >>= Expr) * Block * Block)
    | (TypeTest <<= Expr * Type)
    | (Cast <<= Expr * Type)

    // Remove If, Else. Add Conditional, TypeTest, Cast.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | TypeArgs | Lambda | Let |
         Var | Throw | New | Ref | DontCare | Ellipsis | Dot | Ident | Symbol |
         DoubleColon | wfLiteral | TypeAssert | Conditional | TypeTest |
         Cast)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassReference =
      wfPassConditionals

    // Add RefLet, RefVar, Selector, FunctionName, TypeAssertOp.
    | (RefLet <<= Ident)
    | (RefVar <<= Ident)
    | (Selector <<= wfIdSym * TypeArgs)
    | (FunctionName <<=
        (TypeName >>= (TypeName | TypeUnit)) * wfIdSym * TypeArgs)
    | (TypeAssertOp <<= (Op >>= Selector | FunctionName) * Type)

    // Remove TypeArgs, Ident, Symbol, DoubleColon.
    // Add RefVar, RefLet, Selector, FunctionName, TypeAssertOp.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | Throw |
         New | Ref | DontCare | Ellipsis | Dot | wfLiteral | TypeAssert |
         Conditional | TypeTest | Cast | RefVar | RefLet | Selector |
         FunctionName | TypeAssertOp)++[1])
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
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | Throw |
         New | Ref | DontCare | Ellipsis | wfLiteral | TypeAssert |
         Conditional | TypeTest | Cast | RefVar | RefLet | Selector |
         FunctionName | TypeAssertOp | Call)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassApplication =
      wfPassReverseApp

    // Add TupleFlatten
    | (Tuple <<= (Expr | TupleFlatten)++[2])
    | (TupleFlatten <<= Expr)

    // Remove New, DontCare, Ellipsis.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | Throw |
         Ref | wfLiteral | TypeAssert | Conditional | TypeTest | Cast | RefVar |
         RefLet | Selector | FunctionName | TypeAssertOp | Call)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassAssignLHS =
      wfPassApplication

    // Add RefVarLHS, TupleLHS, CallLHS.
    | (RefVarLHS <<= Ident)
    | (TupleLHS <<= Expr++[2])
    | (CallLHS <<=
        (Selector >>= (New | Selector | FunctionName | TypeAssertOp)) * Args)

    // Remove Expr, Ref. Add TupleLHS, CallLHS, RefVarLHS. No longer a sequence.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | Throw |
        wfLiteral | TypeAssert | Conditional | TypeTest | Cast | RefVar |
        RefLet | Selector | FunctionName | TypeAssertOp | Call | TupleLHS |
        CallLHS | RefVarLHS)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassLocalVar =
      wfPassAssignLHS

    // Remove Var, RefVar, RefVarLHS.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Assign | Lambda | Let | Throw | wfLiteral |
        TypeAssert | Conditional | TypeTest | Cast | RefLet | Selector |
        FunctionName | TypeAssertOp | Call | TupleLHS | CallLHS)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassAssignment =
      wfPassLocalVar

    // Add Bind.
    | (Bind <<= Ident * Type * Expr)[Ident]

    // Remove Assign, Let, TupleLHS. Add Bind.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Lambda | Throw | wfLiteral | TypeAssert |
        Conditional | TypeTest | Cast | RefLet | Selector | FunctionName |
        TypeAssertOp | Call | CallLHS | Bind)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassAutoCreate =
      wfPassAssignment
    | (FieldLet <<= Ident * Type)[Ident]
    | (FieldVar <<= Ident * Type)[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassLambda =
      wfPassAutoCreate

    // Remove Lambda.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Throw | wfLiteral | TypeAssert | Conditional |
        TypeTest | Cast | RefLet | Selector | FunctionName | TypeAssertOp |
        Call | CallLHS | Bind)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassDefaultArgs =
      wfPassLambda
    | (Param <<= Ident * Type)[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassANF =
      wfPassDefaultArgs
    | (Block <<= (Use | Class | TypeAlias | Bind | RefLet)++[1])
    | (Tuple <<= (RefLet | TupleFlatten)++[2])
    | (TupleFlatten <<= RefLet)
    | (Throw <<= RefLet)
    | (Args <<= RefLet++)
    | (Conditional <<= (If >>= RefLet) * Block * Block)
    | (TypeTest <<= RefLet * Type)
    | (Cast <<= RefLet * Type)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          RefLet | Unit | Tuple | Call | Conditional | TypeTest | Cast | Throw |
          CallLHS | Selector | FunctionName | wfLiteral))[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassDrop =
      wfPassANF

    // Add Copy, Move, Drop. Remove RefLet.
    | (Copy <<= Ident)
    | (Move <<= Ident)
    | (Drop <<= Ident)
    | (Block <<= (Use | Class | TypeAlias | Bind | Move | Drop)++[1])
    | (Tuple <<= (TupleFlatten | Copy | Move)++[2])
    | (TupleFlatten <<= Copy | Move)
    | (Throw <<= Copy | Move)
    | (Args <<= (Copy | Move)++)
    | (Conditional <<= (If >>= Copy | Move) * Block * Block)
    | (TypeTest <<= (Ident >>= Copy | Move) * Type)
    | (Cast <<= (Ident >>= Copy | Move) * Type)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          Unit | Tuple | Call | Conditional | TypeTest | Cast | Throw |
          CallLHS | Selector | FunctionName | wfLiteral | Copy | Move))[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wf =
      (TypeAlias <<= Ident * TypeParams * (Bound >>= Type) * (Default >>= Type))
    | (Class <<= Ident * TypeParams * Type * ClassBody)
    | (FieldLet <<= Ident * Type * Default)
    | (FieldVar <<= Ident * Type * Default)
    | (Function <<= wfIdSym * TypeParams * Params * (Type >>= wfType) * Block)
    | (Param <<= Ident * Type * Default)
    | (Type <<= wfType)
    | (TypeName <<= (TypeName >>= (TypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeView <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeFunc <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeThrow <<= (Type >>= wfType))
    | (TypeTrait <<= Ident * ClassBody)
    | (Package <<= (Id >>= String | Escaped))
    | (Var <<= Ident * Type)
    | (Let <<= Ident * Type)
    | (RefLet <<= Ident)
    | (Throw <<= Expr)
    | (Lambda <<= TypeParams * Params * Block)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          Unit | Tuple | Call | Conditional | Throw | CallLHS | Selector |
          FunctionName | wfLiteral | Copy | Move))
    ;
  // clang-format on
}

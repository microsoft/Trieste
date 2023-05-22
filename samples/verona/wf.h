// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "lang.h"

namespace verona
{
  using namespace wf::ops;

  inline const auto wfRef = Ref >>= Ref | DontCare;
  inline const auto wfName = Ident >>= Ident | Symbol;
  inline const auto wfDefault = Default >>= Lambda | DontCare;

  inline const auto wfLiteral =
    Bool | Int | Hex | Bin | Float | HexFloat | Char | Escaped | String | LLVM;

  // clang-format off
  inline const auto wfParser =
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
         Class | TypeAlias | Var | Let | Ref | Lin | In_ | Out | Const | Self |
         If | Else | New | Try | DontCare | Ident | Ellipsis | Dot |
         Colon | DoubleColon | TripleColon | Symbol)++)
    ;
  // clang-format on

  // Remove Colon. Add Type.
  inline const auto wfModulesTokens = wfLiteral | Brace | Paren | Square |
    List | Equals | Arrow | Use | Class | TypeAlias | Var | Let | Ref | Lin |
    In_ | Out | Const | Self | If | Else | New | Try | DontCare | Ident |
    Ellipsis | Dot | DoubleColon | Symbol | Type | LLVMFuncType;

  // clang-format off
  inline const auto wfPassModules =
      (Top <<= Group++)
    | (Brace <<= (Group | List | Equals)++)
    | (Paren <<= (Group | List | Equals)++)
    | (Square <<= (Group | List | Equals)++)
    | (List <<= (Group | Equals)++)
    | (Equals <<= Group++)
    | (LLVMFuncType <<=
        (Lhs >>= LLVM | DontCare) * (Rhs >>= LLVM | DontCare) *
        (Args >>= LLVMList) * (Return >>= LLVM | Ident))
    | (LLVMList <<= (LLVM | Ident)++)
    | (Type <<= wfModulesTokens++)
    | (Group <<= wfModulesTokens++)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassStructure =
      (Top <<= Class++)
    | (Class <<= Ident * TypeParams * Type * ClassBody)[Ident]
    | (ClassBody <<=
        (Use | Class | TypeAlias | FieldLet | FieldVar | Function)++)
    | (Use <<= Type)[Include]
    | (TypeAlias <<= Ident * TypeParams * (Bound >>= Type) * Type)[Ident]
    | (TypeTrait <<= Ident * ClassBody)[Ident]
    | (FieldLet <<= Ident * Type * wfDefault)[Ident]
    | (FieldVar <<= Ident * Type * wfDefault)[Ident]
    | (Function <<=
        wfRef * wfName * TypeParams * Params * Type *
        (LLVMFuncType >>= LLVMFuncType | DontCare) * Block)[Ident]
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
    | (TypeAssert <<= Expr * Type)
    | (Package <<= (Id >>= String | Escaped))
    | (LLVMFuncType <<=
        (Lhs >>= LLVM | DontCare) * (Rhs >>= LLVM | DontCare) *
        (Args >>= LLVMList) * (Return >>= LLVM | Ident))
    | (LLVMList <<= (LLVM | Ident)++)
    | (Type <<=
        (Type | TypeTrait | TypeTuple | TypeVar | TypeArgs | Package | Lin |
         In_ | Out | Const | Self | DontCare | Ellipsis | Ident | Symbol | Dot |
         DoubleColon)++)
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | TypeArgs | If | Else |
         Lambda | Let | Var | New | Try | Ref | DontCare | Ellipsis | Dot |
         Ident | Symbol | DoubleColon | wfLiteral | TypeAssert)++[1])
    ;
  // clang-format on

  inline const auto wfTypeName =
    TypeClassName | TypeTraitName | TypeAliasName | TypeParamName;

  // clang-format off
  inline const auto wfPassTypeNames =
      wfPassStructure

    // Add TypeClassName, TypeTraitName, TypeAliasName, TypeParamName, TypeView,
    // TypeList.
    | (TypeClassName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeTraitName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeAliasName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeParamName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)

    // Remove DontCare, Ident.
    | (Type <<=
        (Type | TypeTrait | TypeTuple | TypeVar | TypeArgs | Package | Lin |
         In_ | Out | Const | Self | Ellipsis | Dot | DoubleColon | Symbol |
         wfTypeName)++)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassTypeView =
      wfPassTypeNames

    // Add TypeView, TypeList.
    | (TypeView <<= Type++[2])
    | (TypeList <<= Type)

    // Remove DoubleColon, Dot, Ellipsis, TypeArgs.
    | (Type <<=
        (Type | TypeTrait | TypeTuple | TypeVar | Package | Lin | In_ | Out |
         Const | Self | Symbol | wfTypeName | TypeView | TypeList)++)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassTypeFunc =
      wfPassTypeView

    // Add TypeUnion, TypeIsect.
    | (TypeUnion <<= Type++[2])
    | (TypeIsect <<= Type++[2])

    | (Type <<=
        (Type | TypeTrait | TypeTuple | TypeVar | Package | Lin | In_ | Out |
         Const | Self | Symbol | wfTypeName | TypeView | TypeList | TypeUnion |
         TypeIsect)++)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassTypeAlg =
      wfPassTypeFunc

    // Add TypeSubtype.
    | (TypeSubtype <<= (Lhs >>= Type) * (Rhs >>= Type))

    // Remove Symbol. Add TypeSubtype.
    | (Type <<=
        (Type | TypeTrait | TypeTuple | TypeVar | Package | Lin | In_ | Out |
         Const | Self | wfTypeName | TypeView | TypeList | TypeUnion |
         TypeIsect | TypeSubtype)++)
    ;
  // clang-format on

  inline const auto wfTypeNoAlg = TypeTrait | TypeUnit | TypeTuple | TypeVar |
    Package | Lin | In_ | Out | Const | Self | wfTypeName | TypeView |
    TypeList | TypeSubtype | TypeTrue | TypeFalse;

  inline const auto wfType = wfTypeNoAlg | TypeUnion | TypeIsect;

  // clang-format off
  inline const auto wfPassTypeFlat =
      wfPassTypeAlg

    // No Type nodes inside of type structure.
    | (TypeList <<= wfType)
    | (TypeTuple <<= wfType++[2])
    | (TypeView <<= wfType++[2])
    | (TypeSubtype <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeUnion <<= (wfTypeNoAlg | TypeIsect)++[2])
    | (TypeIsect <<= (wfTypeNoAlg | TypeUnion)++[2])

    // Types are no longer sequences.
    | (Type <<= wfType)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassConditionals =
      wfPassTypeFlat

    // Add Conditional, TypeTest, Cast.
    | (Conditional <<= (If >>= Expr) * Block * Block)
    | (TypeTest <<= Expr * Type)
    | (Cast <<= Expr * Type)

    // Remove If, Else. Add Conditional, TypeTest, Cast.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | TypeArgs | Lambda | Let |
         Var | New | Try | Ref | DontCare | Ellipsis | Dot | Ident | Symbol |
         DoubleColon | wfLiteral | TypeAssert | Conditional | TypeTest |
         Cast)++[1])
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassReference =
      wfPassConditionals

    // Add RefLet, RefVar, Selector, FunctionName.
    | (RefLet <<= Ident)
    | (RefVar <<= Ident)
    | (Selector <<= wfName * TypeArgs)
    | (FunctionName <<= (Lhs >>= (wfTypeName | TypeUnit)) * wfName * TypeArgs)

    // Remove TypeArgs, Ident, Symbol, DoubleColon.
    // Add RefVar, RefLet, Selector, FunctionName.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | New |
         Try | Ref | DontCare | Ellipsis | Dot | wfLiteral | TypeAssert |
         Conditional | TypeTest | Cast | RefVar | RefLet | Selector |
         FunctionName)++[1])
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassReverseApp =
      wfPassReference

    // Add Call, Args, NLRCheck.
    | (Call <<= (Selector >>= (New | Selector | FunctionName)) * Args)
    | (Args <<= Expr++)
    | (NLRCheck <<= Call)

    // Remove Dot. Add Call, NLRCheck.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | New |
         Try | Ref | DontCare | Ellipsis | wfLiteral | TypeAssert | Conditional |
         TypeTest | Cast | RefVar | RefLet | Selector | FunctionName | Call |
         NLRCheck)++[1])
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassApplication =
      wfPassReverseApp

    // Add TupleFlatten, CallLHS, RefVarLHS.
    | (Tuple <<= (Expr | TupleFlatten)++[2])
    | (TupleFlatten <<= Expr)
    | (NLRCheck <<= Call | CallLHS)
    | (RefVarLHS <<= Ident)
    | (CallLHS <<= (Selector >>= (New | Selector | FunctionName)) * Args)

    // Remove New, Try, DontCare, Ellipsis, Selector, FunctionName,
    // Add CallLHS, RefVarLHS.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | Try |
         Ref | wfLiteral | TypeAssert | Conditional | TypeTest | Cast | RefVar |
         RefLet | Call | NLRCheck | CallLHS | RefVarLHS)++[1])
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassAssignLHS =
      wfPassApplication

    // Add TupleLHS.
    | (TupleLHS <<= Expr++[2])

    // Remove Expr, Try, Ref. Add TupleLHS. No longer a sequence.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | wfLiteral |
        TypeAssert | Conditional | TypeTest | Cast | RefVar | RefLet |
        Call | NLRCheck | CallLHS | RefVarLHS | TupleLHS)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassLocalVar =
      wfPassAssignLHS

    // Remove Var, RefVar, RefVarLHS.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Assign | Lambda | Let | wfLiteral |
        TypeAssert | Conditional | TypeTest | Cast | RefLet | Call | NLRCheck |
        TupleLHS | CallLHS)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassAssignment =
      wfPassLocalVar

    // Add Bind.
    | (Bind <<= Ident * Type * Expr)[Ident]

    // Remove Assign, Let, TupleLHS. Add Bind.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Lambda | wfLiteral | TypeAssert | Conditional |
        TypeTest | Cast | RefLet | Call | NLRCheck | CallLHS | Bind)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassNLRCheck =
      wfPassAssignment

    // Add Return.
    | (Block <<= (Use | Class | TypeAlias | Expr | Return)++[1])
    | (Return <<= Expr)

    // Remove NLRCheck.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Lambda | wfLiteral | TypeAssert | Conditional |
        TypeTest | Cast | RefLet | Call | CallLHS | Bind)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassLambda =
      wfPassNLRCheck

    // Remove Lambda.
    | (FieldLet <<= Ident * Type * (Default >>= (Call | DontCare)))[Ident]
    | (FieldVar <<= Ident * Type * (Default >>= (Call | DontCare)))[Ident]
    | (Param <<= Ident * Type * (Default >>= (Call | DontCare)))[Ident]
    | (Expr <<=
        ExprSeq | Unit | Tuple | wfLiteral | TypeAssert | Conditional |
        TypeTest | Cast | RefLet | Call | CallLHS | Bind)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassAutoFields =
      wfPassLambda

    // Add FieldRef.
    | (FieldRef <<= RefLet * Ident)
    | (Expr <<=
        ExprSeq | Unit | Tuple | wfLiteral | TypeAssert | Conditional |
        TypeTest | Cast | RefLet | Call | CallLHS | Bind | FieldRef)
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassAutoCreate =
      wfPassAutoFields
    | (FieldLet <<= Ident * Type)[Ident]
    | (FieldVar <<= Ident * Type)[Ident]
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassDefaultArgs =
      wfPassAutoCreate
    | (Param <<= Ident * Type)[Ident]
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassANF =
      wfPassDefaultArgs
    | (Block <<= (Use | Class | TypeAlias | Bind | RefLet | Return | LLVM)++[1])
    | (Return <<= RefLet)
    | (Tuple <<= (RefLet | TupleFlatten)++[2])
    | (TupleFlatten <<= RefLet)
    | (Args <<= RefLet++)
    | (Conditional <<= (If >>= RefLet) * Block * Block)
    | (TypeTest <<= RefLet * Type)
    | (Cast <<= RefLet * Type)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          RefLet | Unit | Tuple | Call | Conditional | TypeTest | Cast |
          CallLHS | FieldRef | wfLiteral))[Ident]
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassDrop =
      wfPassANF

    // Add Copy, Move, Drop. Remove RefLet.
    | (Copy <<= Ident)
    | (Move <<= Ident)
    | (Drop <<= Ident)
    | (Block <<=
        (Use | Class | TypeAlias | Bind | Return | LLVM | Move | Drop)++[1])
    | (Return <<= Move)
    | (Tuple <<= (TupleFlatten | Copy | Move)++[2])
    | (TupleFlatten <<= Copy | Move)
    | (Args <<= (Copy | Move)++)
    | (Conditional <<= (If >>= Copy | Move) * Block * Block)
    | (TypeTest <<= (Id >>= Copy | Move) * Type)
    | (Cast <<= (Id >>= Copy | Move) * Type)
    | (FieldRef <<= (Id >>= Copy | Move) * Ident)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          Unit | Tuple | Call | Conditional | TypeTest | Cast | CallLHS |
          FieldRef | wfLiteral | Copy | Move))[Ident]
    ;
  // clang-format on

  // clang-format off
  inline const auto wfPassNameArity =
      wfPassDrop

    // Remove Symbol from Function, Selector, and FunctionName.
    | (FunctionName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)
    | (Selector <<= Ident * TypeArgs)

    // Remove LHS/RHS function distinction.
    | (Function <<=
        Ident * TypeParams * Params * Type *
        (LLVMFuncType >>= LLVMFuncType | DontCare) * Block)[Ident]

    // Turn New into a function.
    | (Call <<= (Selector >>= (Selector | FunctionName)) * Args)

    // Remove CallLHS.
    | (Bind <<= Ident * Type *
        (Rhs >>=
          Unit | Tuple | Call | Conditional | TypeTest | Cast | FieldRef |
          wfLiteral | Copy | Move))[Ident]
    ;
  // clang-format on

  // clang-format off
  inline const auto wf =
      (TypeAlias <<= Ident * TypeParams * (Bound >>= Type) * Type)
    | (Use <<= Type)
    | (Class <<= Ident * TypeParams * Type * ClassBody)
    | (TypeParam <<= Ident * (Bound >>= Type) * Type)
    | (FieldLet <<= Ident * Type * Default)
    | (FieldVar <<= Ident * Type * Default)
    | (Function <<=
        wfRef * wfName * TypeParams * Params * Type *
        (LLVMFuncType >>= LLVMFuncType | DontCare) * Block)
    | (Param <<= Ident * Type * Default)
    | (TypeAssert <<= Expr * Type)
    | (Type <<= wfType)
    | (FunctionName <<= (Lhs >>= (wfTypeName | TypeUnit)) * wfName * TypeArgs)
    | (TypeClassName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeTraitName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeAliasName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeParamName <<= (Lhs >>= (wfTypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeTrait <<= Ident * ClassBody)
    | (Package <<= (Id >>= String | Escaped))
    | (Var <<= Ident * Type)
    | (Let <<= Ident * Type)
    | (RefLet <<= Ident)
    | (Lambda <<= TypeParams * Params * Block)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          Unit | Tuple | Call | Conditional | TypeTest | Cast | FieldRef |
          wfLiteral | Copy | Move))
    ;
  // clang-format on
}

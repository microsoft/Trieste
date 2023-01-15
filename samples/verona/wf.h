// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "lang.h"

namespace verona
{
  using namespace wf::ops;

  inline constexpr auto wfRef = Ref >>= Ref | DontCare;
  inline constexpr auto wfName = Ident >>= Ident | Symbol;
  inline constexpr auto wfDefault = Default >>= Lambda | DontCare;

  inline constexpr auto wfLiteral =
    Bool | Int | Hex | Bin | Float | HexFloat | Char | Escaped | String | LLVM;

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
         Class | TypeAlias | Var | Let | Ref | Lin | In_ | Out | Const |
         If | Else | New | Try | DontCare | Ident | Ellipsis | Dot |
         Colon | DoubleColon | TripleColon | Symbol)++)
    ;
  // clang-format on

  // Remove Colon. Add Type.
  inline constexpr auto wfModulesTokens = wfLiteral | Brace | Paren | Square |
    List | Equals | Arrow | Use | Class | TypeAlias | Var | Let | Ref | Lin |
    In_ | Out | Const | If | Else | New | Try | DontCare | Ident | Ellipsis |
    Dot | DoubleColon | Symbol | Type | LLVMFuncType;

  // clang-format off
  inline constexpr auto wfPassModules =
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
    | (Package <<= String | Escaped)
    | (LLVMFuncType <<=
        (Lhs >>= LLVM | DontCare) * (Rhs >>= LLVM | DontCare) *
        (Args >>= LLVMList) * (Return >>= LLVM | Ident))
    | (LLVMList <<= (LLVM | Ident)++)
    | (Type <<=
        (Type | TypeTuple | TypeVar | TypeArgs | Package | Lin | In_ | Out |
         Const | DontCare | Ellipsis | Ident | Symbol | Dot | Arrow |
         DoubleColon)++)
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | TypeArgs | If | Else |
         Lambda | Let | Var | New | Try | Ref | DontCare | Ellipsis | Dot |
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
         Symbol | Arrow | TypeName | TypeView | TypeList)++)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeFunc =
      wfPassTypeView

    // Remove Arrow. Add TypeFunc.
    | (TypeFunc <<= (Lhs >>= Type) * (Rhs >>= Type))
    | (Type <<=
        (Type | TypeTuple | TypeVar | Package | Lin | In_ | Out | Const |
         Symbol | TypeName | TypeView | TypeList | TypeFunc)++)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeAlg =
      wfPassTypeFunc

    // Add TypeUnion, TypeIsect.
    | (TypeUnion <<= Type++[2])
    | (TypeIsect <<= Type++[2])

    // Remove Symbol. Add TypeUnion and TypeIsect.
    | (Type <<=
        (Type | TypeTuple | TypeVar | Package | Lin | In_ | Out | Const |
         TypeName | TypeView | TypeList | TypeFunc | TypeUnion | TypeIsect)++)
    ;
  // clang-format on

  inline constexpr auto wfTypeNoAlg = TypeTuple | TypeVar | Package | Lin |
    In_ | Out | Const | TypeName | TypeView | TypeList | TypeFunc | TypeUnit;

  inline constexpr auto wfType = wfTypeNoAlg | TypeUnion | TypeIsect;

  // clang-format off
  inline constexpr auto wfPassTypeFlat =
      wfPassTypeAlg

    // No Type nodes inside of type structure.
    | (TypeList <<= wfType)
    | (TypeTuple <<= wfType++[2])
    | (TypeView <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeFunc <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeUnion <<= (wfTypeNoAlg | TypeIsect)++[2])
    | (TypeIsect <<= (wfTypeNoAlg | TypeUnion)++[2])

    // Types are no longer sequences.
    | (Type <<= wfType)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassTypeDNF =
      wfPassTypeFlat

    // Disjunctive normal form.
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
         Var | New | Try | Ref | DontCare | Ellipsis | Dot | Ident | Symbol |
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
    | (Selector <<= wfName * TypeArgs)
    | (FunctionName <<=
        (TypeName >>= (TypeName | TypeUnit)) * wfName * TypeArgs)
    | (TypeAssertOp <<= (Op >>= Selector | FunctionName) * Type)

    // Remove TypeArgs, Ident, Symbol, DoubleColon.
    // Add RefVar, RefLet, Selector, FunctionName, TypeAssertOp.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | New |
         Try | Ref | DontCare | Ellipsis | Dot | wfLiteral | TypeAssert |
         Conditional | TypeTest | Cast | RefVar | RefLet | Selector |
         FunctionName | TypeAssertOp)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassReverseApp =
      wfPassReference

    // Add Call, Args, NLRCheck.
    | (Call <<=
        (Selector >>= (New | Selector | FunctionName | TypeAssertOp)) * Args)
    | (Args <<= Expr++)
    | (NLRCheck <<= Call)

    // Remove Dot. Add Call, NLRCheck.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | New |
         Try | Ref | DontCare | Ellipsis | wfLiteral | TypeAssert | Conditional |
         TypeTest | Cast | RefVar | RefLet | Selector | FunctionName |
         TypeAssertOp | Call | NLRCheck)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassApplication =
      wfPassReverseApp

    // Add TupleFlatten, CallLHS, RefVarLHS.
    | (Tuple <<= (Expr | TupleFlatten)++[2])
    | (TupleFlatten <<= Expr)
    | (NLRCheck <<= Call | CallLHS)
    | (RefVarLHS <<= Ident)
    | (CallLHS <<=
        (Selector >>= (New | Selector | FunctionName | TypeAssertOp)) * Args)

    // Remove New, Try, DontCare, Ellipsis, Selector, FunctionName,
    // TypeAssertOp. Add CallLHS, RefVarLHS.
    | (Expr <<=
        (Expr | ExprSeq | Unit | Tuple | Assign | Lambda | Let | Var | Try |
         Ref | wfLiteral | TypeAssert | Conditional | TypeTest | Cast | RefVar |
         RefLet | Call | NLRCheck | CallLHS | RefVarLHS)++[1])
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassAssignLHS =
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
  inline constexpr auto wfPassLocalVar =
      wfPassAssignLHS

    // Remove Var, RefVar, RefVarLHS.
    | (Expr <<=
        ExprSeq | Unit | Tuple | Assign | Lambda | Let | wfLiteral |
        TypeAssert | Conditional | TypeTest | Cast | RefLet | Call | NLRCheck |
        TupleLHS | CallLHS)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassAssignment =
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
  inline constexpr auto wfPassNLRCheck =
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
  inline constexpr auto wfPassLambda =
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
  inline constexpr auto wfPassAutoFields =
      wfPassLambda

    // Add FieldRef.
    | (FieldRef <<= (Lhs >>= Ident) * (Rhs >>= Ident))
    | (Expr <<=
        ExprSeq | Unit | Tuple | wfLiteral | TypeAssert | Conditional |
        TypeTest | Cast | RefLet | Call | CallLHS | Bind | FieldRef)
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassAutoCreate =
      wfPassAutoFields
    | (FieldLet <<= Ident * Type)[Ident]
    | (FieldVar <<= Ident * Type)[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassDefaultArgs =
      wfPassAutoCreate
    | (Param <<= Ident * Type)[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wfPassANF =
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
  inline constexpr auto wfPassDrop =
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
    | (TypeTest <<= (Ident >>= Copy | Move) * Type)
    | (Cast <<= (Ident >>= Copy | Move) * Type)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          Unit | Tuple | Call | Conditional | TypeTest | Cast | CallLHS |
          FieldRef | wfLiteral | Copy | Move))[Ident]
    ;
  // clang-format on

  // clang-format off
  inline constexpr auto wf =
      (TypeAlias <<= Ident * TypeParams * (Bound >>= Type) * (Default >>= Type))
    | (Class <<= Ident * TypeParams * Type * ClassBody)
    | (FieldLet <<= Ident * Type * Default)
    | (FieldVar <<= Ident * Type * Default)
    | (Function <<=
        wfRef * wfName * TypeParams * Params * Type *
        (LLVMFuncType >>= LLVMFuncType | DontCare) * Block)
    | (Param <<= Ident * Type * Default)
    | (TypeAssert <<= Expr * Type)
    | (Type <<= wfType)
    | (TypeName <<= (TypeName >>= (TypeName | TypeUnit)) * Ident * TypeArgs)
    | (TypeView <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeFunc <<= (Lhs >>= wfType) * (Rhs >>= wfType))
    | (TypeTrait <<= Ident * ClassBody)
    | (Package <<= (Id >>= String | Escaped))
    | (Var <<= Ident * Type)
    | (Let <<= Ident * Type)
    | (RefLet <<= Ident)
    | (Lambda <<= TypeParams * Params * Block)
    | (Bind <<= Ident * Type *
        (Rhs >>=
          Unit | Tuple | Call | Conditional | TypeTest | Cast | CallLHS |
          FieldRef | wfLiteral | Copy | Move))
    ;
  // clang-format on
}

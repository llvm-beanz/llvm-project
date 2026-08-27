//===- TessellationTest.cpp - Tests for tessellation state attributes ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Tessellation.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using namespace feme::graphics;
using namespace llvm;

namespace {

Function *makeFunction(Module &M, StringRef Name) {
  FunctionType *FnTy = FunctionType::get(Type::getVoidTy(M.getContext()), {},
                                         /*isVarArg=*/false);
  return Function::Create(FnTy, Function::ExternalLinkage, Name, M);
}

/// A function with none of the `feme.tessellation.*` attributes carries no
/// tessellation state at all.
TEST(TessellationTest, AbsentAttributesReturnNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "no_tess");
  EXPECT_FALSE(getTessellationState(*F).has_value());
}

/// A tessellation-evaluation entry point carries `Domain`/`Partitioning`/
/// `OutputPrimitive` but not `OutputControlPointCount` (SPIR-V's
/// `OutputVertices` is a tessellation-*control*-only execution mode): the
/// round-tripped state's `OutputControlPointCount` is left at
/// `TessellationState`'s own default.
TEST(TessellationTest, RoundTripsDomainEvaluationAttributes) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "tese");
  F->addFnAttr(getTessellationDomainAttrName(), "triangle");
  F->addFnAttr(getTessellationPartitioningAttrName(), "fractional_odd");
  F->addFnAttr(getTessellationOutputPrimitiveAttrName(), "triangle_ccw");

  std::optional<TessellationState> State = getTessellationState(*F);
  ASSERT_TRUE(State.has_value());
  EXPECT_EQ(State->Domain, TessellatorDomain::Triangle);
  EXPECT_EQ(State->Partitioning, TessPartitioning::FractionalOdd);
  EXPECT_EQ(State->OutputPrimitive, TessOutputPrimitive::TriangleCcw);
  EXPECT_EQ(State->OutputControlPointCount,
            TessellationState().OutputControlPointCount);
}

/// A tessellation-control entry point carries only
/// `OutputControlPointCount`: the round-tripped state's `Domain`/
/// `Partitioning`/`OutputPrimitive` are left at their defaults.
TEST(TessellationTest, RoundTripsControlPointCountAttribute) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "tesc");
  F->addFnAttr(getTessellationOutputControlPointCountAttrName(), "4");

  std::optional<TessellationState> State = getTessellationState(*F);
  ASSERT_TRUE(State.has_value());
  EXPECT_EQ(State->OutputControlPointCount, 4u);
  EXPECT_EQ(State->Domain, TessellationState().Domain);
}

/// Every `TessellatorDomain`/`TessPartitioning`/`TessOutputPrimitive`
/// enumerator round-trips through its attribute spelling, not just the
/// first case of each.
TEST(TessellationTest, RoundTripsEveryEnumeratorSpelling) {
  LLVMContext Ctx;
  Module M("m", Ctx);

  struct Case {
    const char *Domain;
    const char *Partitioning;
    const char *OutputPrimitive;
    TessellatorDomain ExpectedDomain;
    TessPartitioning ExpectedPartitioning;
    TessOutputPrimitive ExpectedOutputPrimitive;
  };
  static constexpr Case Cases[] = {
      {"isoline", "integer", "line", TessellatorDomain::Isoline,
       TessPartitioning::Integer, TessOutputPrimitive::Line},
      {"quad", "fractional_even", "triangle_cw", TessellatorDomain::Quad,
       TessPartitioning::FractionalEven, TessOutputPrimitive::TriangleCw},
      {"triangle", "fractional_odd", "point", TessellatorDomain::Triangle,
       TessPartitioning::FractionalOdd, TessOutputPrimitive::Point},
  };
  for (unsigned I = 0; I != std::size(Cases); ++I) {
    const Case &C = Cases[I];
    Function *F = makeFunction(M, ("case" + Twine(I)).str());
    F->addFnAttr(getTessellationDomainAttrName(), C.Domain);
    F->addFnAttr(getTessellationPartitioningAttrName(), C.Partitioning);
    F->addFnAttr(getTessellationOutputPrimitiveAttrName(), C.OutputPrimitive);

    std::optional<TessellationState> State = getTessellationState(*F);
    ASSERT_TRUE(State.has_value()) << I;
    EXPECT_EQ(State->Domain, C.ExpectedDomain) << I;
    EXPECT_EQ(State->Partitioning, C.ExpectedPartitioning) << I;
    EXPECT_EQ(State->OutputPrimitive, C.ExpectedOutputPrimitive) << I;
  }
}

/// An unrecognized enumerator spelling (e.g. a typo, or a future SPIR-V
/// execution mode this milestone does not yet map) is treated as malformed
/// rather than silently defaulted.
TEST(TessellationTest, UnrecognizedDomainSpellingReturnsNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "bad_domain");
  F->addFnAttr(getTessellationDomainAttrName(), "hexagon");
  F->addFnAttr(getTessellationPartitioningAttrName(), "integer");
  F->addFnAttr(getTessellationOutputPrimitiveAttrName(), "point");
  EXPECT_FALSE(getTessellationState(*F).has_value());
}

/// A non-numeric `OutputControlPointCount` attribute is likewise malformed.
TEST(TessellationTest, NonNumericControlPointCountReturnsNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "bad_count");
  F->addFnAttr(getTessellationOutputControlPointCountAttrName(), "four");
  EXPECT_FALSE(getTessellationState(*F).has_value());
}

} // namespace

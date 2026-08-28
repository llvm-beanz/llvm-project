//===- MeshTest.cpp - Tests for mesh stage state attributes --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Mesh.h"

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

/// A function with none of the `feme.mesh.*` attributes carries no mesh
/// state at all -- true for a non-mesh entry point and for a task entry
/// point, which declares no shape beyond its workgroup size.
TEST(MeshTest, AbsentAttributesReturnNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "no_mesh");
  EXPECT_FALSE(getMeshState(*F).has_value());
}

/// A well-formed mesh entry point's output topology, maximum output vertex
/// count and maximum output primitive count round-trip.
TEST(MeshTest, RoundTripsAttributes) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "mesh");
  F->addFnAttr(getMeshOutputTopologyAttrName(), "triangles");
  F->addFnAttr(getMeshMaxOutputVerticesAttrName(), "64");
  F->addFnAttr(getMeshMaxOutputPrimitivesAttrName(), "126");

  std::optional<MeshState> State = getMeshState(*F);
  ASSERT_TRUE(State.has_value());
  EXPECT_EQ(State->OutputTopology, MeshOutputTopology::Triangles);
  EXPECT_EQ(State->MaxOutputVertices, 64u);
  EXPECT_EQ(State->MaxOutputPrimitives, 126u);
}

/// Every `MeshOutputTopology` enumerator round-trips through its attribute
/// spelling, not just the first case.
TEST(MeshTest, RoundTripsEveryEnumeratorSpelling) {
  LLVMContext Ctx;
  Module M("m", Ctx);

  struct Case {
    const char *Topology;
    MeshOutputTopology Expected;
  };
  static constexpr Case Cases[] = {
      {"points", MeshOutputTopology::Points},
      {"lines", MeshOutputTopology::Lines},
      {"triangles", MeshOutputTopology::Triangles},
  };
  for (unsigned I = 0; I != std::size(Cases); ++I) {
    const Case &C = Cases[I];
    Function *F = makeFunction(M, ("case" + Twine(I)).str());
    F->addFnAttr(getMeshOutputTopologyAttrName(), C.Topology);
    F->addFnAttr(getMeshMaxOutputVerticesAttrName(), "1");
    F->addFnAttr(getMeshMaxOutputPrimitivesAttrName(), "1");

    std::optional<MeshState> State = getMeshState(*F);
    ASSERT_TRUE(State.has_value()) << I;
    EXPECT_EQ(State->OutputTopology, C.Expected) << I;
  }
}

/// An unrecognized enumerator spelling is treated as malformed rather than
/// silently defaulted.
TEST(MeshTest, UnrecognizedOutputTopologySpellingReturnsNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "bad_topology");
  F->addFnAttr(getMeshOutputTopologyAttrName(), "hexagons");
  F->addFnAttr(getMeshMaxOutputVerticesAttrName(), "1");
  F->addFnAttr(getMeshMaxOutputPrimitivesAttrName(), "1");
  EXPECT_FALSE(getMeshState(*F).has_value());
}

/// A non-numeric `MaxOutputVertices`/`MaxOutputPrimitives` attribute is
/// likewise malformed.
TEST(MeshTest, NonNumericCountsReturnNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "bad_count");
  F->addFnAttr(getMeshOutputTopologyAttrName(), "points");
  F->addFnAttr(getMeshMaxOutputVerticesAttrName(), "many");
  F->addFnAttr(getMeshMaxOutputPrimitivesAttrName(), "1");
  EXPECT_FALSE(getMeshState(*F).has_value());
}

/// A partial attribute set (e.g. a hand-written test module missing one of
/// the three that always arrive together) is treated as absent rather than
/// guessing defaults for the missing pieces.
TEST(MeshTest, PartialAttributeSetReturnsNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "partial");
  F->addFnAttr(getMeshOutputTopologyAttrName(), "points");
  // Max output vertices and max output primitives are both missing.
  EXPECT_FALSE(getMeshState(*F).has_value());
}

} // namespace

//===- GeometryTest.cpp - Tests for geometry stage state attributes -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Geometry.h"

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

/// A function with none of the `feme.geometry.*` attributes carries no
/// geometry state at all.
TEST(GeometryTest, AbsentAttributesReturnNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "no_geom");
  EXPECT_FALSE(getGeometryState(*F).has_value());
}

/// A well-formed geometry entry point's input/output primitive and maximum
/// output vertex count round-trip; `Invocations` defaults to 1 when not
/// declared.
TEST(GeometryTest, RoundTripsAttributesWithDefaultInvocations) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "geom");
  F->addFnAttr(getGeometryInputPrimitiveAttrName(), "triangles");
  F->addFnAttr(getGeometryOutputPrimitiveAttrName(), "triangle_strip");
  F->addFnAttr(getGeometryMaxOutputVerticesAttrName(), "3");

  std::optional<GeometryState> State = getGeometryState(*F);
  ASSERT_TRUE(State.has_value());
  EXPECT_EQ(State->InputPrimitive, GeometryInputPrimitive::Triangles);
  EXPECT_EQ(State->OutputPrimitive, GeometryOutputPrimitive::TriangleStrip);
  EXPECT_EQ(State->MaxOutputVertices, 3u);
  EXPECT_EQ(State->Invocations, 1u);
}

/// An explicit `Invocations` attribute overrides the default.
TEST(GeometryTest, RoundTripsExplicitInvocations) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "geom_instanced");
  F->addFnAttr(getGeometryInputPrimitiveAttrName(), "points");
  F->addFnAttr(getGeometryOutputPrimitiveAttrName(), "points");
  F->addFnAttr(getGeometryMaxOutputVerticesAttrName(), "1");
  F->addFnAttr(getGeometryInvocationsAttrName(), "4");

  std::optional<GeometryState> State = getGeometryState(*F);
  ASSERT_TRUE(State.has_value());
  EXPECT_EQ(State->Invocations, 4u);
}

/// Every `GeometryInputPrimitive`/`GeometryOutputPrimitive` enumerator
/// round-trips through its attribute spelling, not just the first case of
/// each, and `getVerticesPerPrimitive` reports the right vertex count for
/// every input primitive.
TEST(GeometryTest, RoundTripsEveryEnumeratorSpelling) {
  LLVMContext Ctx;
  Module M("m", Ctx);

  struct Case {
    const char *Input;
    const char *Output;
    GeometryInputPrimitive ExpectedInput;
    GeometryOutputPrimitive ExpectedOutput;
    uint32_t ExpectedVerticesPerPrimitive;
  };
  static constexpr Case Cases[] = {
      {"points", "points", GeometryInputPrimitive::Points,
       GeometryOutputPrimitive::Points, 1},
      {"lines", "line_strip", GeometryInputPrimitive::Lines,
       GeometryOutputPrimitive::LineStrip, 2},
      {"lines_adjacency", "line_strip", GeometryInputPrimitive::LinesAdjacency,
       GeometryOutputPrimitive::LineStrip, 4},
      {"triangles", "triangle_strip", GeometryInputPrimitive::Triangles,
       GeometryOutputPrimitive::TriangleStrip, 3},
      {"triangles_adjacency", "triangle_strip",
       GeometryInputPrimitive::TrianglesAdjacency,
       GeometryOutputPrimitive::TriangleStrip, 6},
  };
  for (unsigned I = 0; I != std::size(Cases); ++I) {
    const Case &C = Cases[I];
    Function *F = makeFunction(M, ("case" + Twine(I)).str());
    F->addFnAttr(getGeometryInputPrimitiveAttrName(), C.Input);
    F->addFnAttr(getGeometryOutputPrimitiveAttrName(), C.Output);
    F->addFnAttr(getGeometryMaxOutputVerticesAttrName(), "1");

    std::optional<GeometryState> State = getGeometryState(*F);
    ASSERT_TRUE(State.has_value()) << I;
    EXPECT_EQ(State->InputPrimitive, C.ExpectedInput) << I;
    EXPECT_EQ(State->OutputPrimitive, C.ExpectedOutput) << I;
    EXPECT_EQ(getVerticesPerPrimitive(State->InputPrimitive),
              C.ExpectedVerticesPerPrimitive)
        << I;
  }
}

/// An unrecognized enumerator spelling is treated as malformed rather than
/// silently defaulted.
TEST(GeometryTest, UnrecognizedInputPrimitiveSpellingReturnsNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "bad_input");
  F->addFnAttr(getGeometryInputPrimitiveAttrName(), "hexagons");
  F->addFnAttr(getGeometryOutputPrimitiveAttrName(), "points");
  F->addFnAttr(getGeometryMaxOutputVerticesAttrName(), "1");
  EXPECT_FALSE(getGeometryState(*F).has_value());
}

/// A non-numeric `MaxOutputVertices` attribute is likewise malformed.
TEST(GeometryTest, NonNumericMaxOutputVerticesReturnsNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "bad_count");
  F->addFnAttr(getGeometryInputPrimitiveAttrName(), "points");
  F->addFnAttr(getGeometryOutputPrimitiveAttrName(), "points");
  F->addFnAttr(getGeometryMaxOutputVerticesAttrName(), "many");
  EXPECT_FALSE(getGeometryState(*F).has_value());
}

/// A partial attribute set (e.g. a hand-written test module missing one of
/// the three that always arrive together) is treated as absent rather than
/// guessing defaults for the missing pieces.
TEST(GeometryTest, PartialAttributeSetReturnsNullopt) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "partial");
  F->addFnAttr(getGeometryInputPrimitiveAttrName(), "points");
  // Output primitive and max output vertices are both missing.
  EXPECT_FALSE(getGeometryState(*F).has_value());
}

} // namespace

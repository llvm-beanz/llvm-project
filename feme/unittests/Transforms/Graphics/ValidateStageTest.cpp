//===- ValidateStageTest.cpp - Tests for ValidateStagePass ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/Graphics/ValidateStage.h"

#include "feme/Core/Signature.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::graphics;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("ValidateStageTest", errs());
  return M;
}

/// One `float4` input element (`ElementID` 0) and one `float4` output
/// element (`ElementID` 1), matching the operands the tests below use --
/// attached directly via `feme::dxil::setEntrySignature` rather than
/// hand-encoded metadata, since the serialized byte layout is not meant to
/// be authored by hand (see SignatureTest.cpp for that coverage instead).
void attachTestSignature(Function &F) {
  EntrySignature Sig;
  SignatureElement Input;
  Input.ElementID = 0;
  Input.Direction = SignatureDirection::Input;
  Input.ComponentCount = 4;
  Sig.Elements.push_back(Input);
  SignatureElement Output;
  Output.ElementID = 1;
  Output.Direction = SignatureDirection::Output;
  Output.ComponentCount = 4;
  Sig.Elements.push_back(Output);
  dxil::setEntrySignature(F, Sig);
}

/// Runs `ValidateStagePass` on \p M, returning every error-severity
/// diagnostic message it reported.
std::vector<std::string> validate(Module &M) {
  std::vector<std::string> Errors;
  M.getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        if (DI->getSeverity() != DS_Error)
          return;
        std::string Msg;
        raw_string_ostream OS(Msg);
        DiagnosticPrinterRawOStream Printer(OS);
        DI->print(Printer);
        static_cast<std::vector<std::string> *>(Ctx)->push_back(Msg);
      },
      &Errors);
  ModuleAnalysisManager MAM;
  ValidateStagePass().run(M, MAM);
  return Errors;
}

TEST(ValidateStageTest, ValidCallsAreNotDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %v, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);
  attachTestSignature(*M->getFunction("main"));
  EXPECT_TRUE(validate(*M).empty());
}

TEST(ValidateStageTest, DiscardInVertexStageIsIllegal) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @feme.stage.discard(i1 true)
      ret void
    }
    declare void @feme.stage.discard(i1)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("not legal"), std::string::npos);
}

TEST(ValidateStageTest, DiscardInFragmentStageIsLegal) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @feme.stage.discard(i1 true)
      ret void
    }
    declare void @feme.stage.discard(i1)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(validate(*M).empty());
}

TEST(ValidateStageTest, NonConstantElementIDIsDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %e) #0 {
      %v = call float @feme.stage.input.load.f32(i32 %e, i32 0, i32 0, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);
  attachTestSignature(*M->getFunction("main"));
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("non-constant element ID"), std::string::npos);
}

TEST(ValidateStageTest, UnknownElementIDIsDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = call float @feme.stage.input.load.f32(i32 42, i32 0, i32 0, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);
  attachTestSignature(*M->getFunction("main"));
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("unknown element 42"), std::string::npos);
}

TEST(ValidateStageTest, OutOfRangeComponentIsDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 7, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);
  attachTestSignature(*M->getFunction("main"));
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("component 7 is out of range"), std::string::npos);
}

TEST(ValidateStageTest, WrongDirectionElementIsDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);
  attachTestSignature(*M->getFunction("main"));
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("wrong direction"), std::string::npos);
}

/// (Roadmap H5b) A non-constant `Vertex` operand (`feme.stage.input.load`'s
/// 4th argument) is only legal for the geometry stage's own
/// dynamically-indexed `gl_in[i]`-shaped per-vertex inputs
/// (`FemeGeometryArgs`'s primitive-major `Inputs` layout); every other
/// stage's own recursion always seeds it with a constant, so a
/// non-constant one there is diagnosed. `ValidateStagePass::run` does not
/// yet validate the geometry stage at all (see H5e), so this can only be
/// exercised against a stage it does validate today.
TEST(ValidateStageTest, NonConstantVertexOperandIsDiagnosedOutsideGeometry) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %i) #0 {
      %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %i)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);
  attachTestSignature(*M->getFunction("main"));
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("non-constant vertex operand"), std::string::npos);
}

/// The store-side mirror of
/// `NonConstantVertexOperandIsDiagnosedOutsideGeometry`:
/// `feme.stage.output.store`'s `Vertex` operand is its 5th argument
/// (`Element, Row, Component, Val, Vertex` -- one more than
/// `feme.stage.input.load`'s own, for the value being stored).
TEST(ValidateStageTest, NonConstantVertexOperandIsDiagnosedOnOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %i) #0 {
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 1.0, i32 %i)
      ret void
    }
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);
  attachTestSignature(*M->getFunction("main"));
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("non-constant vertex operand"), std::string::npos);
}

/// (Roadmap H6g-b-c) `ValidateStagePass::run` now validates the mesh
/// stage too; an ordinary `feme.stage.output.store` call (a mesh entry's
/// per-vertex/per-primitive output write, roadmap H6b) is legal there,
/// mirroring `ValidCallsAreNotDiagnosed`'s vertex/fragment coverage.
TEST(ValidateStageTest, MeshOutputStoreIsLegal) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 1.0, i32 0)
      ret void
    }
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" }
  )");
  ASSERT_TRUE(M);
  attachTestSignature(*M->getFunction("main"));
  EXPECT_TRUE(validate(*M).empty());
}

/// (Roadmap H6g-b-c) `feme.stage.discard` remains illegal in the mesh
/// stage now that it is validated, mirroring
/// `DiscardInVertexStageIsIllegal`.
TEST(ValidateStageTest, DiscardInMeshStageIsIllegal) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @feme.stage.discard(i1 true)
      ret void
    }
    declare void @feme.stage.discard(i1)
    attributes #0 = { "feme.shader.stage"="mesh" }
  )");
  ASSERT_TRUE(M);
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("not legal"), std::string::npos);
}

/// (Roadmap H6g-b-c) The concrete gap this row closes: a mesh entry's
/// arrayed `PerPrimitiveEXT`/`PerVertexEXT` builtin interface-block store
/// `CanonicalizeStagePass::resolveOffsetWithinElement` leaves unrewritten
/// (see `CanonicalizeStageTest.
/// ConstantIndexIntoArrayedBuiltinInterfaceBlockIsLeftUnrewritten`, the
/// exact IR shape reused here) is now diagnosed instead of silently
/// reaching `feme::cpu`'s JIT as an undefined `GlobalVariable` symbol.
TEST(ValidateStageTest, UnresolvedStageIOGlobalAccessInMeshIsDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_mesh_prims = external addrspace(8) global [4 x { i32, i32 }], !feme.spirv.MemberDecorations !10
    define void @main(i32 %v) #0 {
      %p = getelementptr inbounds [4 x { i32, i32 }], ptr addrspace(8) @gl_mesh_prims, i32 0, i32 1, i32 0
      store i32 %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !10 = !{!11, !12}
    !11 = !{i32 0, !13}
    !12 = !{i32 1, !14}
    !13 = !{!15}
    !15 = !{i32 11, i32 7}
    !14 = !{!16}
    !16 = !{i32 5271}
  )");
  ASSERT_TRUE(M);
  std::vector<std::string> Errors = validate(*M);
  ASSERT_EQ(Errors.size(), 1u);
  EXPECT_NE(Errors[0].find("unresolved stage-IO global-variable access"),
            std::string::npos);
  EXPECT_NE(Errors[0].find("gl_mesh_prims"), std::string::npos);
}

/// An ordinary load/store unrelated to any stage-IO global (e.g. a local
/// `alloca`) is not diagnosed by
/// `UnresolvedStageIOGlobalAccessInMeshIsDiagnosed`'s new check: only a
/// `GlobalVariable` recognized by `isSPIRVStageIOGlobal` triggers it.
TEST(ValidateStageTest, OrdinaryAllocaAccessInMeshIsNotDiagnosed) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %v) #0 {
      %p = alloca i32
      store i32 %v, ptr %p
      %r = load i32, ptr %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(validate(*M).empty());
}

} // namespace

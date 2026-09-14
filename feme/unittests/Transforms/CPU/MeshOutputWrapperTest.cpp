//===- MeshOutputWrapperTest.cpp - Tests for MeshOutputWrapperPass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/MeshOutputWrapper.h"

#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::cpu;
using namespace llvm;
using namespace llvm::PatternMatch;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("MeshOutputWrapperTest", errs());
  return M;
}

SignatureElement makeOutputElement(uint32_t ElementID,
                                   SignatureFrequency Frequency) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Output;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.Frequency = Frequency;
  return Elt;
}

SignatureElement makeInputElement(uint32_t ElementID,
                                  SignatureSystemValue SystemValue) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.SystemValue = SystemValue;
  return Elt;
}

// A mesh entry's per-vertex output store (roadmap H6b's canonicalized
// shape, dynamic `Vertex` operand) lowers into a store addressed off
// `mesh_vertex_outputs`, and the wave body gains this pass's own trailing
// params.
TEST(MeshOutputWrapperTest, LowersPerVertexOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %vid = call i32 @llvm.dx.thread.id(i32 0)
      %vidf = uitofp i32 %vid to float
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %vidf, i32 %vid)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeOutputElement(0, SignatureFrequency::PerVertex),
  };
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  bool SawVertexOutputs = false, SawVertexLayout = false;
  bool SawPrimitiveOutputs = false, SawMaxVertices = false;
  for (const Argument &Arg : Body->args()) {
    SawVertexOutputs |= Arg.getName() == "mesh_vertex_outputs";
    SawVertexLayout |= Arg.getName() == "mesh_vertex_output_layout";
    SawPrimitiveOutputs |= Arg.getName() == "mesh_primitive_outputs";
    SawMaxVertices |= Arg.getName() == "mesh_max_output_vertices";
  }
  EXPECT_TRUE(SawVertexOutputs);
  EXPECT_TRUE(SawVertexLayout);
  EXPECT_TRUE(SawPrimitiveOutputs);
  EXPECT_TRUE(SawMaxVertices);

  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap H6g-b-d) A mesh entry that also happens to read a resource
// (e.g. `pvd.name[i]`/`ppd.name[i]` in a real `dEQP-VK.mesh_shader.ext.
// in_out.*` shader, a storage/uniform buffer read feeding the output
// store's value) must not have that unrelated `feme.cpu.resource.load.
// raw.*` call rejected by `lowerMeshStageOps`'s own catch-all: the
// `UsesStageOps` gate only requires *some* call in the function to need
// this pass's attention (the output store here), not that *every*
// remaining call is one of the two shapes it lowers.
TEST(MeshOutputWrapperTest, LeavesUnrelatedResourceLoadCallAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %vid = call i32 @llvm.dx.thread.id(i32 0)
      %loaded = call float @feme.cpu.resource.load.raw.f32(ptr null, i32 0, i32 0, i64 0, i1 true)
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %loaded, i32 %vid)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeOutputElement(0, SignatureFrequency::PerVertex),
  };
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);
  EXPECT_FALSE(SawError);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  bool SawResourceLoad = false;
  for (const Instruction &I : instructions(*Body)) {
    const auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    EXPECT_FALSE(isStageOpCall(*CI)) << *CI;
    if (const Function *Callee = CI->getCalledFunction())
      SawResourceLoad |=
          Callee->getName().starts_with("feme.cpu.resource.load.raw.");
  }
  EXPECT_TRUE(SawResourceLoad);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// A per-primitive-frequency output element (`SignatureFrequency::
// PerPrimitive`) addresses `mesh_primitive_outputs`/
// `mesh_primitive_output_layout` instead -- distinguishing the two is this
// pass's whole reason for branching per-element rather than reusing
// `VertexWrapperPass`'s own single-array addressing.
TEST(MeshOutputWrapperTest, LowersPerPrimitiveOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %pid = call i32 @llvm.dx.thread.id(i32 0)
      %pidf = uitofp i32 %pid to float
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %pidf, i32 %pid)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeOutputElement(0, SignatureFrequency::PerPrimitive),
  };
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  bool SawPrimitiveOutputsUse = false;
  Argument *PrimitiveOutputsArg = nullptr;
  for (Argument &Arg : Body->args())
    if (Arg.getName() == "mesh_primitive_outputs")
      PrimitiveOutputsArg = &Arg;
  ASSERT_TRUE(PrimitiveOutputsArg);
  for (const Use &U : PrimitiveOutputsArg->uses())
    if (isa<GetElementPtrInst>(U.getUser()) || isa<CastInst>(U.getUser()))
      SawPrimitiveOutputsUse = true;
  EXPECT_TRUE(SawPrimitiveOutputsUse);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// A mesh entry's `gl_PrimitiveTriangleIndicesEXT` store (roadmap H29r,
// `SignatureSystemValue::PrimitiveIndices`) lowers into a store addressed off
// the flat `mesh_primitive_indices` array rather than off
// `mesh_primitive_outputs`'s own structure-of-arrays attribute storage: this
// element's data lives in `FemeMeshArgs::PrimitiveIndices`, not in either
// output block. Before this row it was routed to attribute storage like any
// other per-primitive element, leaving every meshlet's index list at its
// zero-initialized default and degenerating every emitted triangle.
TEST(MeshOutputWrapperTest, LowersPrimitiveIndicesOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %pid = call i32 @llvm.dx.thread.id(i32 0)
      call void @feme.stage.output.store.i32(i32 0, i32 0, i32 2, i32 %pid, i32 %pid)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  SignatureElement Elt = makeOutputElement(0, SignatureFrequency::PerPrimitive);
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.SystemValue = SignatureSystemValue::PrimitiveIndices;
  Elt.ComponentCount = 3;
  EntrySignature Sig;
  Sig.Elements = {Elt};
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  Argument *PrimitiveIndicesArg = nullptr;
  Argument *PrimitiveOutputsArg = nullptr;
  for (Argument &Arg : Body->args()) {
    if (Arg.getName() == "mesh_primitive_indices")
      PrimitiveIndicesArg = &Arg;
    else if (Arg.getName() == "mesh_primitive_outputs")
      PrimitiveOutputsArg = &Arg;
  }
  ASSERT_TRUE(PrimitiveIndicesArg);
  ASSERT_TRUE(PrimitiveOutputsArg);

  bool SawIndexGEP = false;
  for (const Use &U : PrimitiveIndicesArg->uses())
    if (isa<GetElementPtrInst>(U.getUser()))
      SawIndexGEP = true;
  EXPECT_TRUE(SawIndexGEP);
  // The attribute-storage block is left completely untouched.
  EXPECT_TRUE(PrimitiveOutputsArg->use_empty());

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// A mesh entry with no output store at all (e.g. one that only ever writes
// `SetMeshOutputsEXT`-declared counts, which this pass now also lowers, see
// `LowersSetMeshOutputsCall` below) is left completely alone: this pass's own
// params are only useful once `EntryWrapperPass` runs, and appending them
// unconditionally to every mesh entry, used or not, is still harmless but
// unnecessary; today it still appends them (matching every other stage
// wrapper's "always append params, conditionally lower" convention, see
// VertexWrapper.cpp), which this test also pins down.
TEST(MeshOutputWrapperTest, AppendsParamsEvenWithNoOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  bool SawVertexOutputs = false;
  for (const Argument &Arg : Body->args())
    SawVertexOutputs |= Arg.getName() == "mesh_vertex_outputs";
  EXPECT_TRUE(SawVertexOutputs);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// End-to-end: `MeshOutputWrapperPass` followed by `EntryWrapperPass` (the
// same order `feme::cpu::buildPipeline` -- Pipeline.cpp -- chains them in
// for `ShaderStage::Mesh`) builds a real `feme_cpu_entry_ms_main` wrapper
// whose single argument reads as `getMeshArgsType`'s longer struct.
TEST(MeshOutputWrapperTest, ChainsIntoEntryWrapperPass) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %vid = call i32 @llvm.dx.thread.id(i32 0)
      %vidf = uitofp i32 %vid to float
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %vidf, i32 %vid)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeOutputElement(0, SignatureFrequency::PerVertex),
  };
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_ms_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_EQ(Wrapper->arg_size(), 1u);
  EXPECT_TRUE(Wrapper->getArg(0)->getType()->isPointerTy());

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap H6c-a-a-i) A mesh entry's canonicalized
// `feme.stage.set_mesh_outputs` call lowers into a pair of stores through
// this pass's new `mesh_actual_vertex_count`/`mesh_actual_primitive_count`
// trailing params, and every lane sees no leftover stage op.
TEST(MeshOutputWrapperTest, LowersSetMeshOutputsCall) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      call void @feme.stage.set_mesh_outputs(i32 3, i32 1)
      ret void
    }
    declare void @feme.stage.set_mesh_outputs(i32, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  // No output store here, so no signature elements are needed, but the
  // wrapper still requires attached (possibly empty) signature metadata
  // whenever any stage op is present -- see `lowerMeshStageOps`'s own
  // comment.
  dxil::setEntrySignature(*M->getFunction("ms_main"), EntrySignature{});

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  bool SawActualVertexCount = false, SawActualPrimitiveCount = false;
  Argument *ActualVertexCountArg = nullptr;
  Argument *ActualPrimitiveCountArg = nullptr;
  for (Argument &Arg : Body->args()) {
    if (Arg.getName() == "mesh_actual_vertex_count") {
      SawActualVertexCount = true;
      ActualVertexCountArg = &Arg;
    } else if (Arg.getName() == "mesh_actual_primitive_count") {
      SawActualPrimitiveCount = true;
      ActualPrimitiveCountArg = &Arg;
    }
  }
  EXPECT_TRUE(SawActualVertexCount);
  EXPECT_TRUE(SawActualPrimitiveCount);
  ASSERT_TRUE(ActualVertexCountArg);
  ASSERT_TRUE(ActualPrimitiveCountArg);
  EXPECT_FALSE(ActualVertexCountArg->use_empty());
  EXPECT_FALSE(ActualPrimitiveCountArg->use_empty());

  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap H85) `SetMeshOutputsEXT` is spec'd to be honored from
// invocation 0 of the workgroup only, but this test suite's own
// generated mesh shaders (e.g.
// `dEQP-VK.mesh_shader.ext.misc.group_memory_barrier_in_mesh_*`) call it
// unconditionally from every invocation, each passing its own
// (frequently stale/default) values -- so the call site's own
// reachability-derived mask alone is not a sufficient gate: this pass
// must additionally gate every write on the true flattened invocation
// id (`wave_index == 0 && Lane == 0`), not merely `Lane == 0`. This
// confirms the store's value is only ever selected from the call's
// operands when `wave_index` is provably zero, i.e. the generated
// `icmp eq i32 %wave_index, 0` feeds (via `and`) into the final
// `select`s that choose between the new value and the previously
// stored one.
TEST(MeshOutputWrapperTest, GatesSetMeshOutputsToWaveZeroLaneZero) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      call void @feme.stage.set_mesh_outputs(i32 3, i32 1)
      ret void
    }
    declare void @feme.stage.set_mesh_outputs(i32, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  dxil::setEntrySignature(*M->getFunction("ms_main"), EntrySignature{});

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);

  Argument *WaveIndexArg = nullptr;
  for (Argument &Arg : Body->args())
    if (Arg.getName() == "wave_index")
      WaveIndexArg = &Arg;
  ASSERT_TRUE(WaveIndexArg) << "expected a wave_index parameter";

  // Find `icmp eq i32 %wave_index, 0` -- the gate this fix introduces.
  ICmpInst *WaveZeroCmp = nullptr;
  for (Instruction &I : instructions(*Body))
    if (auto *Cmp = dyn_cast<ICmpInst>(&I))
      if (Cmp->getPredicate() == ICmpInst::ICMP_EQ &&
          Cmp->getOperand(0) == WaveIndexArg &&
          match(Cmp->getOperand(1), m_Zero()))
        WaveZeroCmp = Cmp;
  ASSERT_TRUE(WaveZeroCmp) << "expected an `icmp eq %wave_index, 0` gate";

  // That comparison must feed (through an `and`) both `select`s that
  // choose the stored vertex/primitive counts -- i.e. every write is
  // gated on being in wave 0, not merely on the call site's own
  // reachability mask.
  unsigned SelectsGatedByWaveZero = 0;
  for (Instruction &I : instructions(*Body)) {
    auto *Sel = dyn_cast<SelectInst>(&I);
    if (!Sel)
      continue;
    Value *Cond = Sel->getCondition();
    auto *And = dyn_cast<Instruction>(Cond);
    if (!And || And->getOpcode() != Instruction::And)
      continue;
    if (And->getOperand(0) == WaveZeroCmp ||
        And->getOperand(1) == WaveZeroCmp)
      ++SelectsGatedByWaveZero;
  }
  EXPECT_EQ(SelectsGatedByWaveZero, 2u)
      << "expected both the vertex-count and primitive-count selects to "
         "be gated on wave_index == 0";

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap H6p) A mesh entry's `gl_DrawID` read (SPIR-V's `DrawIndex`
// builtin, canonicalized into an ordinary `feme.stage.input.load` by
// `CanonicalizeStage.cpp` since it has no dedicated `llvm.spv.*`
// intrinsic) lowers into a broadcast of this pass's new `mesh_draw_id`
// trailing param to every active lane, rather than being rejected by the
// generic "unexpected stage op" catch-all -- confirms the specific gap
// found reproducing `dEQP-VK.mesh_shader.ext.api.draw.*` is now handled.
TEST(MeshOutputWrapperTest, LowersDrawIDInputLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %draw_id = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %draw_idf = uitofp i32 %draw_id to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %draw_idf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeInputElement(0, SignatureSystemValue::DrawID),
      makeOutputElement(1, SignatureFrequency::PerVertex),
  };
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);
  EXPECT_FALSE(SawError);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  Argument *DrawIDArg = nullptr;
  for (Argument &Arg : Body->args())
    if (Arg.getName() == "mesh_draw_id")
      DrawIDArg = &Arg;
  ASSERT_TRUE(DrawIDArg);
  EXPECT_FALSE(DrawIDArg->use_empty());

  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap L30) A mesh entry's own bounded payload read
// (`feme.stage.task.payload.load`, canonicalized from an ordinary load
// through the task payload's own address-space-14 global by
// `CanonicalizeStage.cpp`) lowers into a load off the new `mesh_payload`
// trailing parameter at the call's own constant byte offset, broadcasting
// that single scalar to every active lane -- mirroring
// `LowersDrawIDInputLoad` above exactly, just reading through the payload
// buffer instead of a workgroup-uniform builtin.
TEST(MeshOutputWrapperTest, LowersTaskPayloadLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %v = call float @feme.stage.task.payload.load.f32(i32 4)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %v, i32 0)
      ret void
    }
    declare float @feme.stage.task.payload.load.f32(i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeOutputElement(1, SignatureFrequency::PerVertex),
  };
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);
  EXPECT_FALSE(SawError);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  Argument *PayloadArg = nullptr;
  for (Argument &Arg : Body->args())
    if (Arg.getName() == "mesh_payload")
      PayloadArg = &Arg;
  ASSERT_TRUE(PayloadArg);
  EXPECT_FALSE(PayloadArg->use_empty());

  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap L49) The mesh-stage counterpart of
// `TaskPayloadWrapperTest.LowersPayloadLoadWithDynamicOffset`: a genuinely
// dynamic (per-invocation) payload read offset must lower into a real
// per-lane addressing loop in `lowerMeshTaskPayloadLoad`, not broadcast a
// single shared scalar read to every lane (only valid when `Offset` really
// is a compile-time constant) -- before this fix, a non-`ConstantInt`
// `Offset` here hit a fatal `cast<ConstantInt>` assertion.
TEST(MeshOutputWrapperTest, LowersTaskPayloadLoadWithDynamicOffset) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %offset = mul i32 %tid, 4
      %v = call float @feme.stage.task.payload.load.f32(i32 %offset)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %v, i32 0)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare float @feme.stage.task.payload.load.f32(i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeOutputElement(1, SignatureFrequency::PerVertex),
  };
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Handle) = true;
      },
      &SawError);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);
  EXPECT_FALSE(SawError);

  Function *Body = M->getFunction("ms_main");
  ASSERT_TRUE(Body);
  Argument *PayloadArg = nullptr;
  for (Argument &Arg : Body->args())
    if (Arg.getName() == "mesh_payload")
      PayloadArg = &Arg;
  ASSERT_TRUE(PayloadArg);
  EXPECT_FALSE(PayloadArg->use_empty());

  for (const Instruction &I : instructions(*Body))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  // Every one of the 4 lanes must read its own address off `mesh_payload`
  // (a divergent offset can put each lane's own read at a completely
  // different byte range), not just one shared broadcast read.
  unsigned NumLoadsFromPayload = 0;
  for (const Instruction &I : instructions(*Body))
    if (const auto *LI = dyn_cast<LoadInst>(&I))
      if (const auto *GEP =
              dyn_cast<GetElementPtrInst>(LI->getPointerOperand()))
        if (GEP->getPointerOperand() == PayloadArg)
          ++NumLoadsFromPayload;
  EXPECT_EQ(NumLoadsFromPayload, 4u);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}


// (i.e. anything but `gl_DrawID`) still gets a diagnostic -- a narrower one
// than the pass's generic "unexpected stage op" catch-all, distinguishing
// "recognized as an input load, but an unsupported system value" from
// "some entirely different, unlowered stage op" (roadmap H6p).
TEST(MeshOutputWrapperTest, RejectsUnsupportedInputSystemValue) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ms_main() #0 {
      %v = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %vf = uitofp i32 %v to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %vf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="mesh" "hlsl.numthreads"="4,1,1" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {
      makeInputElement(0, SignatureSystemValue::VertexID),
      makeOutputElement(1, SignatureFrequency::PerVertex),
  };
  dxil::setEntrySignature(*M->getFunction("ms_main"), Sig);

  std::string LastError;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() != DS_Error)
          return;
        *reinterpret_cast<std::string *>(Handle) = "error";
      },
      &LastError);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  MeshOutputWrapperPass().run(*M, MAM);
  EXPECT_EQ(LastError, "error");
}

} // namespace

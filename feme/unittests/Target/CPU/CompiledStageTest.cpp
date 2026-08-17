//===- CompiledStageTest.cpp - Tests for feme::cpu::CompiledStage --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Covers `feme::cpu::CompiledStage` at the fine-grained `invokeGroup`
// (roadmap milestone R21) granularity JITEngineTest doesn't exercise
// directly: a `PreparedDispatch` built once and invoked per group, including
// concurrently from multiple threads, which is the whole point of factoring
// this out of `JITEngine` (see CompiledStage.h's file comment).
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/CompiledStage.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/Signature.h"
#include "feme/Target/CPU/JITEngine.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <thread>
#include <vector>

using namespace feme;
using namespace feme::cpu;
using namespace llvm;

namespace {

// Same shape as JITEngineTest's own shader: writes each group's id (as an
// i32) to an unstructured byte-address buffer at that group's own byte
// offset, i.e. `RWByteAddressBuffer.Store(gid * 4, gid)` -- a group of
// exactly one lane (`numthreads(1,1,1)`), so each `invokeGroup` call writes
// exactly one element and every group is independent, matching the
// concurrent-invocation test below.
constexpr char ShaderIR[] = R"(
  define void @main() #0 {
    %h = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0, i1 false)
    %gid = call i32 @llvm.dx.group.id(i32 0)
    %offset = mul i32 %gid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %gid)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32, i1)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare i32 @llvm.dx.group.id(i32)
  attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
)";

Expected<std::unique_ptr<CompiledStage>> compile(Context &Ctx,
                                                 unsigned WaveSize = 4) {
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(ShaderIR, Err, Ctx.getLLVMContext());
  if (!LLVMMod)
    return createStringError(inconvertibleErrorCode(), "parse error: %s",
                             Err.getMessage().str().c_str());

  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));
  JITOptions Opts;
  Opts.WaveSize = WaveSize;
  return CompiledStage::create(Ctx, std::move(Mod), Opts);
}

TEST(CompiledStageTest, InvokeGroupRunsExactlyOneGroup) {
  Context Ctx;
  Expected<std::unique_ptr<CompiledStage>> Stage = compile(Ctx);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  EXPECT_EQ((*Stage)->getGroupSize(), (std::array<uint32_t, 3>{1, 1, 1}));

  std::vector<int32_t> Buffer(4, -1);
  FemeDescriptor Desc{};
  Desc.Data = Buffer.data();
  Desc.SizeInBytes = Buffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;

  DispatchResources Resources;
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
  PreparedDispatch Prepared = PreparedDispatch::create(
      (*Stage)->getResourceInfo(), Resources, {4, 1, 1});

  ASSERT_THAT_ERROR(
      (*Stage)->invokeGroup(Prepared, {2, 0, 0}, /*GroupShared=*/{}),
      Succeeded());

  // Only group 2's own slot was written; the dispatch's other groups were
  // never invoked, since this test calls `invokeGroup` directly rather than
  // looping over `GroupCount` the way `JITEngine::dispatch` does.
  EXPECT_EQ(Buffer, (std::vector<int32_t>{-1, -1, 2, -1}));
}

TEST(CompiledStageTest, ConcurrentInvokeGroupCallsAreSafeForIndependentGroups) {
  Context Ctx;
  Expected<std::unique_ptr<CompiledStage>> Stage = compile(Ctx);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  constexpr uint32_t NumGroups = 64;
  std::vector<int32_t> Buffer(NumGroups, -1);
  FemeDescriptor Desc{};
  Desc.Data = Buffer.data();
  Desc.SizeInBytes = Buffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;

  DispatchResources Resources;
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
  PreparedDispatch Prepared = PreparedDispatch::create(
      (*Stage)->getResourceInfo(), Resources, {NumGroups, 1, 1});

  // Every group writes its own, disjoint slot, so running them from several
  // threads at once needs no synchronization beyond joining -- exactly the
  // property `JITEngine::dispatch` relies on to hand groups to a worker
  // pool (see JITEngine.cpp).
  std::vector<std::thread> Threads;
  for (uint32_t T = 0; T != 8; ++T)
    Threads.emplace_back([&, T] {
      for (uint32_t X = T; X < NumGroups; X += 8)
        cantFail((*Stage)->invokeGroup(Prepared, {X, 0, 0},
                                       /*GroupShared=*/{}));
    });
  for (std::thread &T : Threads)
    T.join();

  std::vector<int32_t> Expected(NumGroups);
  for (uint32_t X = 0; X != NumGroups; ++X)
    Expected[X] = static_cast<int32_t>(X);
  EXPECT_EQ(Buffer, Expected);
}

TEST(CompiledStageTest, GetArtifactInfoReflectsResolvedExecutionShape) {
  Context Ctx;
  Expected<std::unique_ptr<CompiledStage>> Stage = compile(Ctx, /*WaveSize=*/8);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.Stage, feme::ShaderStage::Compute);
  EXPECT_EQ(Artifact.WaveSize, 8u);
  EXPECT_EQ(Artifact.GroupSize[0], 1u);
  EXPECT_EQ(Artifact.GroupSize[1], 1u);
  EXPECT_EQ(Artifact.GroupSize[2], 1u);
  // `ShaderIR` declares no `addrspace(3)` global.
  EXPECT_EQ(Artifact.GroupSharedSize, 0u);
}

constexpr char GroupSharedShaderIR[] = R"(
  @tile = addrspace(3) global [4 x i32] zeroinitializer, align 16

  define void @main() #0 {
    ret void
  }
  attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
)";

TEST(CompiledStageTest, GetArtifactInfoReportsGroupSharedRequirements) {
  Context Ctx;
  SMDiagnostic Err;
  auto LLVMMod =
      parseAssemblyString(GroupSharedShaderIR, Err, Ctx.getLLVMContext());
  ASSERT_TRUE(LLVMMod) << Err.getMessage().str();

  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));
  JITOptions Opts;
  Expected<std::unique_ptr<CompiledStage>> Stage =
      CompiledStage::create(Ctx, std::move(Mod), Opts);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.GroupSharedSize, 16u);
  EXPECT_EQ(Artifact.GroupSharedAlign, 16u);
}

constexpr char VertexShaderIR[] = R"(
  define void @vs_main() #0 {
    %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %vid = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
    %vidf = uitofp i32 %vid to float
    %sum = fadd float %in, %vidf
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %sum, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="vertex" }
)";

constexpr char FragmentShaderIR[] = R"(
  define void @ps_main() #0 {
    %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %dx = call float @feme.stage.derivative.x.fine.f32(float %in)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %dx, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare float @feme.stage.derivative.x.fine.f32(float)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

// Each control point reads its own `OutputControlPointID`-indexed input
// attribute and doubles it -- the common per-control-point-independent
// shape `feme::cpu::HullWrapperPass` supports (see HullWrapper.cpp).
constexpr char HullShaderIR[] = R"(
  define void @hs_main() #0 {
    %id = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
    %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %id)
    %doubled = fmul float %in, 2.0
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %doubled, i32 0)
    ret void
  }
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// Reads two output control points' attributes -- not just "its own", see
// PatchConstantWrapper.cpp -- and writes their sum as a patch-constant
// output.
constexpr char PatchConstantShaderIR[] = R"(
  define void @pc_main() #0 {
    %a = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %b = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
    %sum = fadd float %a, %b
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %sum, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// Reads one control point from the original `InputPatch` (element 0,
// `FromInputPatch`) and one from the completed `OutputPatch` (element 1),
// writing their difference -- proving the two blocks address distinct
// storage rather than aliasing one another.
constexpr char PatchConstantShaderWithInputPatchIR[] = R"(
  define void @pc_main() #0 {
    %orig = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %completed = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    %diff = fsub float %completed, %orig
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %diff, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

SignatureElement makeFloatInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.BitWidth = 32;
  return Elt;
}

SignatureElement makeFloatOutput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatInput(ElementID);
  Elt.Direction = SignatureDirection::Output;
  return Elt;
}

SignatureElement makeVertexIDInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::VertexID;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.BitWidth = 32;
  return Elt;
}

SignatureElement makeOutputControlPointIDInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::OutputControlPointID;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.BitWidth = 32;
  return Elt;
}

SignatureElement makeFloatPatchOutput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatInput(ElementID);
  Elt.Direction = SignatureDirection::PatchOutput;
  Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

SignatureElement makeFloatInputPatchInput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatInput(ElementID);
  Elt.FromInputPatch = true;
  return Elt;
}

Expected<std::unique_ptr<CompiledStage>>
compileGraphicsStage(Context &Ctx, StringRef IR, StringRef EntryName,
                     const EntrySignature &Sig, ShaderStage Stage,
                     unsigned WaveSize) {
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(IR, Err, Ctx.getLLVMContext());
  if (!LLVMMod)
    return createStringError(inconvertibleErrorCode(), "parse error: %s",
                             Err.getMessage().str().c_str());
  dxil::setEntrySignature(*LLVMMod->getFunction(EntryName), Sig);
  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));
  StageCompileOptions Opts;
  Opts.Stage = Stage;
  Opts.WaveSize = WaveSize;
  return CompiledStage::create(Ctx, std::move(Mod), Opts);
}

TEST(CompiledStageTest, InvokeVerticesRunsStageAwarePath) {
  Context Ctx;
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0), makeVertexIDInput(1), makeFloatOutput(2)};
  Expected<std::unique_ptr<CompiledStage>> Stage = compileGraphicsStage(
      Ctx, VertexShaderIR, "vs_main", Sig, ShaderStage::Vertex, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Vertex);

  FemeStageElement InputElements[2] = {};
  InputElements[0].ElementID = 0;
  InputElements[0].FirstComponent = 0;
  InputElements[0].ComponentCount = 1;
  InputElements[0].RowCount = 1;
  InputElements[0].InvocationStride = 4;
  InputElements[1].ElementID = 1;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 2;

  FemeStageElement OutputElements[3] = {};
  OutputElements[2].ElementID = 2;
  OutputElements[2].FirstComponent = 0;
  OutputElements[2].ComponentCount = 1;
  OutputElements[2].RowCount = 1;
  OutputElements[2].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 3;

  std::vector<float> Inputs = {1.0f, 2.0f, 3.0f};
  std::vector<float> Outputs(3, -1.0f);
  FemeVertexInvocation Invocations[3] = {};
  Invocations[0].VertexID = 0;
  Invocations[1].VertexID = 10;
  Invocations[2].VertexID = 20;

  VertexResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.Invocations = Invocations;
  PreparedVertexBatch Prepared =
      PreparedVertexBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeVertices(Prepared), Succeeded());
  EXPECT_EQ(Outputs[0], 1.0f);
  EXPECT_EQ(Outputs[1], 12.0f);
  EXPECT_EQ(Outputs[2], 23.0f);

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.Stage, ShaderStage::Vertex);
  EXPECT_FALSE(Artifact.Signature.empty());
}

TEST(CompiledStageTest, InvokeFragmentsRunsStageAwarePath) {
  Context Ctx;
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0), makeFloatOutput(1)};
  Expected<std::unique_ptr<CompiledStage>> Stage = compileGraphicsStage(
      Ctx, FragmentShaderIR, "ps_main", Sig, ShaderStage::Fragment, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Fragment);

  FemeStageElement InputElements[1] = {};
  InputElements[0].ElementID = 0;
  InputElements[0].FirstComponent = 0;
  InputElements[0].ComponentCount = 1;
  InputElements[0].RowCount = 1;
  InputElements[0].InvocationStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 1;

  FemeStageElement OutputElements[2] = {};
  OutputElements[1].ElementID = 1;
  OutputElements[1].FirstComponent = 0;
  OutputElements[1].ComponentCount = 1;
  OutputElements[1].RowCount = 1;
  OutputElements[1].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 2;

  std::vector<float> Inputs = {0.0f, 1.0f, 10.0f, 11.0f};
  std::vector<float> Outputs(4, -1.0f);
  FemeFragmentInvocation Invocation{};
  Invocation.LiveMask = 0xf;
  Invocation.SideEffectMask = 0x7;
  FemeFragmentResult Result{};

  FragmentResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.Invocations = ArrayRef<FemeFragmentInvocation>(&Invocation, 1);
  Resources.Results = MutableArrayRef<FemeFragmentResult>(&Result, 1);
  PreparedFragmentBatch Prepared =
      PreparedFragmentBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
  EXPECT_EQ(Outputs[0], 1.0f);
  EXPECT_EQ(Outputs[1], 1.0f);
  EXPECT_EQ(Outputs[2], 1.0f);
  EXPECT_EQ(Outputs[3], -1.0f);
  EXPECT_EQ(Result.LiveMask, 0xfu);
  EXPECT_EQ(Result.SideEffectMask, 0x7u);
}

TEST(CompiledStageTest, InvokePatchRunsStageAwarePath) {
  Context Ctx;
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0), makeOutputControlPointIDInput(1),
                  makeFloatOutput(2)};
  Expected<std::unique_ptr<CompiledStage>> Stage = compileGraphicsStage(
      Ctx, HullShaderIR, "hs_main", Sig, ShaderStage::Hull, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Hull);

  FemeStageElement InputElements[2] = {};
  InputElements[0].ElementID = 0;
  InputElements[0].FirstComponent = 0;
  InputElements[0].ComponentCount = 1;
  InputElements[0].RowCount = 1;
  InputElements[0].InvocationStride = 4;
  InputElements[1].ElementID = 1;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 2;

  FemeStageElement OutputElements[3] = {};
  OutputElements[2].ElementID = 2;
  OutputElements[2].FirstComponent = 0;
  OutputElements[2].ComponentCount = 1;
  OutputElements[2].RowCount = 1;
  OutputElements[2].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 3;

  std::vector<float> Inputs = {1.0f, 2.0f, 3.0f};
  std::vector<float> Outputs(3, -1.0f);

  PatchResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.OutputControlPointCount = 3;
  PreparedPatchBatch Prepared =
      PreparedPatchBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokePatch(Prepared), Succeeded());
  EXPECT_EQ(Outputs[0], 2.0f);
  EXPECT_EQ(Outputs[1], 4.0f);
  EXPECT_EQ(Outputs[2], 6.0f);

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.Stage, ShaderStage::Hull);
  EXPECT_FALSE(Artifact.Signature.empty());
}

TEST(CompiledStageTest, InvokePatchConstantRunsStageAwarePath) {
  Context Ctx;
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0), makeFloatPatchOutput(1)};
  Expected<std::unique_ptr<CompiledStage>> Stage = compileGraphicsStage(
      Ctx, PatchConstantShaderIR, "pc_main", Sig, ShaderStage::Hull, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Hull);

  FemeStageElement InputElements[1] = {};
  InputElements[0].ElementID = 0;
  InputElements[0].FirstComponent = 0;
  InputElements[0].ComponentCount = 1;
  InputElements[0].RowCount = 1;
  InputElements[0].InvocationStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 1;

  FemeStageElement OutputElements[2] = {};
  OutputElements[1].ElementID = 1;
  OutputElements[1].FirstComponent = 0;
  OutputElements[1].ComponentCount = 1;
  OutputElements[1].RowCount = 1;
  OutputElements[1].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 2;

  // Two output control points' worth of the completed `OutputPatch`.
  std::vector<float> Inputs = {5.0f, 7.0f};
  std::vector<float> Outputs(1, -1.0f);

  PatchConstantResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.OutputControlPointCount = 2;
  PreparedPatchConstantBatch Prepared = PreparedPatchConstantBatch::create(
      (*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokePatchConstant(Prepared), Succeeded());
  EXPECT_EQ(Outputs[0], 12.0f);

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.Stage, ShaderStage::Hull);
  EXPECT_FALSE(Artifact.Signature.empty());
}

TEST(CompiledStageTest, InvokePatchConstantReadsInputPatchSeparatelyFromOutputPatch) {
  Context Ctx;
  EntrySignature Sig;
  Sig.Elements = {makeFloatInputPatchInput(0), makeFloatInput(1),
                  makeFloatPatchOutput(2)};
  Expected<std::unique_ptr<CompiledStage>> Stage =
      compileGraphicsStage(Ctx, PatchConstantShaderWithInputPatchIR, "pc_main",
                           Sig, ShaderStage::Hull, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Hull);

  FemeStageElement InputPatchElements[1] = {};
  InputPatchElements[0].ElementID = 0;
  InputPatchElements[0].FirstComponent = 0;
  InputPatchElements[0].ComponentCount = 1;
  InputPatchElements[0].RowCount = 1;
  InputPatchElements[0].InvocationStride = 4;
  FemeStageLayout InputPatchLayout{};
  InputPatchLayout.Elements = InputPatchElements;
  InputPatchLayout.ElementCount = 1;

  FemeStageElement InputElements[2] = {};
  InputElements[1].ElementID = 1;
  InputElements[1].FirstComponent = 0;
  InputElements[1].ComponentCount = 1;
  InputElements[1].RowCount = 1;
  InputElements[1].InvocationStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 2;

  FemeStageElement OutputElements[3] = {};
  OutputElements[2].ElementID = 2;
  OutputElements[2].FirstComponent = 0;
  OutputElements[2].ComponentCount = 1;
  OutputElements[2].RowCount = 1;
  OutputElements[2].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 3;

  // The original (pre-hull) input control point's own attribute...
  std::vector<float> InputPatch = {3.0f};
  // ... distinct from the completed output control point's attribute the
  // control-point phase produced.
  std::vector<float> Inputs = {10.0f};
  std::vector<float> Outputs(1, -1.0f);

  PatchConstantResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.InputPatchLayout = &InputPatchLayout;
  Resources.InputPatch = InputPatch.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.OutputControlPointCount = 1;
  Resources.InputPatchControlPointCount = 1;
  PreparedPatchConstantBatch Prepared = PreparedPatchConstantBatch::create(
      (*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokePatchConstant(Prepared), Succeeded());
  // 10.0 (OutputPatch) - 3.0 (InputPatch): reading the wrong block would
  // instead alias the two and produce 0.
  EXPECT_EQ(Outputs[0], 7.0f);

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.Stage, ShaderStage::Hull);
  EXPECT_FALSE(Artifact.Signature.empty());
}

} // namespace

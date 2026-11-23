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
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <new>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include "llvm/Support/Windows/WindowsSupport.h"
#else
#include <sys/mman.h>
#endif

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
        @llvm.dx.resource.handlefromheap(i32 0)
    %gid = call i32 @llvm.dx.group.id(i32 0)
    %offset = mul i32 %gid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %gid)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32)
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

TEST(CompiledStageTest, UnreadExplicitContractSurvivesBothBackends) {
  for (bool HasHeap : {false, true})
    for (bool Reference : {false, true}) {
      Context Ctx;
      SMDiagnostic Err;
      auto M = parseAssemblyString(HasHeap ? ShaderIR : R"(
            define void @main() #0 { ret void }
            attributes #0 = {
              "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1"
            }
          )",
                                   Err, Ctx.getLLVMContext());
      ASSERT_TRUE(M) << Err.getMessage().str();
      auto Word = [&](uint32_t Value) {
        return ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt32Ty(Ctx.getLLVMContext()), Value));
      };
      M->getFunction("main")->setMetadata(
          "feme.cpu.rootconstantbinding",
          MDNode::get(Ctx.getLLVMContext(), {Word(7), Word(3), Word(32)}));
      JITOptions Opts;
      Opts.WaveSize = 4;
      Opts.Reference = Reference;
      auto Stage = CompiledStage::create(
          Ctx, feme::Module::fromLLVMIR(std::move(M)), Opts);
      ASSERT_THAT_EXPECTED(Stage, Succeeded());
      const auto &Info = (*Stage)->getResourceInfo();
      EXPECT_EQ(Info.RootConstantSize, 32u);
      EXPECT_EQ(Info.RootConstantSpace, 7u);
      EXPECT_EQ(Info.RootConstantRegister, 3u);
      auto Artifact = (*Stage)->getArtifactInfo();
      EXPECT_EQ(Artifact.RootConstantSize, 32u);
      EXPECT_EQ(Artifact.RootConstantSpace, 7u);
      EXPECT_EQ(Artifact.RootConstantRegister, 3u);
    }
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

TEST(CompiledStageTest,
     ConcurrentCompilationAndRepeatedExecutionOutliveContexts) {
  constexpr unsigned StageCount = 2;
  constexpr unsigned RepeatCount = 3;
  std::array<std::unique_ptr<CompiledStage>, StageCount> Stages;
  std::array<std::string, StageCount> Errors;
  std::array<std::thread, StageCount> Threads;
  for (unsigned I = 0; I != StageCount; ++I)
    Threads[I] = std::thread([&, I] {
      Context Ctx;
      auto Stage = compile(Ctx, I == 0 ? 4 : 8);
      if (!Stage) {
        Errors[I] = toString(Stage.takeError());
        return;
      }
      Stages[I] = std::move(*Stage);
    });
  for (auto &Thread : Threads)
    Thread.join();
  for (unsigned I = 0; I != StageCount; ++I) {
    ASSERT_TRUE(Errors[I].empty()) << Errors[I];
    ASSERT_NE(Stages[I], nullptr);
    EXPECT_EQ(Stages[I]->getWaveSize(), I == 0 ? 4u : 8u);
  }

  std::array<std::array<std::vector<int32_t>, RepeatCount>, StageCount>
      Snapshots;
  for (unsigned I = 0; I != StageCount; ++I)
    Threads[I] = std::thread([&, I] {
      std::vector<int32_t> Buffer(6);
      FemeDescriptor Desc{};
      Desc.Data = Buffer.data() + 1;
      Desc.SizeInBytes = 4 * sizeof(int32_t);
      Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
      Desc.Flags = FEME_DESCRIPTOR_UAV;
      DispatchResources Resources;
      Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
      PreparedDispatch Prepared = PreparedDispatch::create(
          Stages[I]->getResourceInfo(), Resources, {4, 1, 1});
      for (unsigned Repeat = 0; Repeat != RepeatCount; ++Repeat) {
        std::fill(Buffer.begin(), Buffer.end(),
                  -static_cast<int32_t>(Repeat) - 1);
        for (uint32_t Group = 0; Group != 4; ++Group)
          if (Error E = Stages[I]->invokeGroup(Prepared, {Group, 0, 0}, {})) {
            Errors[I] = toString(std::move(E));
            return;
          }
        Snapshots[I][Repeat] = Buffer;
      }
    });
  for (auto &Thread : Threads)
    Thread.join();
  for (unsigned I = 0; I != StageCount; ++I) {
    ASSERT_TRUE(Errors[I].empty()) << Errors[I];
    for (unsigned Repeat = 0; Repeat != RepeatCount; ++Repeat) {
      int32_t Guard = -static_cast<int32_t>(Repeat) - 1;
      EXPECT_EQ(Snapshots[I][Repeat],
                (std::vector<int32_t>{Guard, 0, 1, 2, 3, Guard}));
    }
  }
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

// Mirrors the SPIR-V execution model's own triple-derived `DataLayout`
// (`SPIRVToLLVMTranslator`'s, e.g. for the "vulkan" environment) that a real
// module still carries at this point (roadmap H82's `clearHostAgnosticMetadata`
// deliberately leaves it untouched until `feme::cpu::runPipeline`'s own
// `CanonicalizeStagePass` finishes) -- unlike `GroupSharedShaderIR` above,
// whose default (empty) `DataLayout` happens to already agree with most real
// hosts for a 16-byte-aligned `i32` array. This one gives no explicit
// `align` for `@tile` (a `[4 x i8]`, so its layout-derived preferred
// alignment differs between this `DataLayout` and a real host one) to
// exercise `getGroupSharedRequirements`'s own `DataLayout`-dependent
// alignment computation.
constexpr char GroupSharedSPIRVLayoutShaderIR[] = R"(
  target datalayout = "e-ve-i64:64-n8:16:32:64-G10"

  @tile = addrspace(3) global [4 x i8] zeroinitializer

  define void @main() #0 {
    ret void
  }
  attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
)";

// Regression test for roadmap H82's own datalayout-ordering bug class,
// found latent in `CompiledStage::createStage`'s own
// `getGroupSharedRequirements` call: it ran on the module's
// still-SPIR-V-triple-derived `DataLayout` (needed, unmodified, by
// `CanonicalizeStagePass` inside `runPipeline`, which had not yet run),
// rather than the real host `DataLayout` the same `groupshared` globals get
// relaid-out against later (`SIMDizePass`/`EntryWrapperPass`, both
// downstream of `runPipeline`'s own host-layout substitution) -- sizing/
// aligning the host-side scratch allocation
// (`StageArtifactInfo::GroupSharedAlign`) against the wrong layout.
TEST(CompiledStageTest,
     GetArtifactInfoUsesHostDataLayoutForGroupSharedAlignment) {
  Context Ctx;
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(GroupSharedSPIRVLayoutShaderIR, Err,
                                     Ctx.getLLVMContext());
  ASSERT_TRUE(LLVMMod) << Err.getMessage().str();

  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));
  JITOptions Opts;
  Expected<std::unique_ptr<CompiledStage>> Stage =
      CompiledStage::create(Ctx, std::move(Mod), Opts);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  Expected<DataLayout> HostDL = getHostDataLayout();
  ASSERT_THAT_EXPECTED(HostDL, Succeeded());
  unsigned ExpectedAlign =
      HostDL->getPrefTypeAlign(Type::getInt8Ty(Ctx.getLLVMContext())).value();

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.GroupSharedSize, 4u);
  EXPECT_EQ(Artifact.GroupSharedAlign, ExpectedAlign);
}

// A uniform loop with a group-sync barrier in its body -- the
// `feme::cpu::EntryWrapperPass` "LoopShape" path (`buildWrapperForLoop`),
// which (unlike the no-barrier and straight-line-barrier paths) always
// erases the original wave-body function once wrapped, since none of its
// own outlined prefix/body/suffix regions are obligated to keep that
// function's identity.
constexpr char LoopWithBarrierShaderIR[] = R"(
  @shared = internal addrspace(3) global [4 x i32] undef
  define void @main() #0 {
  entry:
    br label %header
  header:
    %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
    %cmp = icmp ult i32 %i, 4
    br i1 %cmp, label %flow, label %after
  flow:
    %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 %i
    store i32 %i, ptr addrspace(3) %ptr
    call void @llvm.dx.group.memory.barrier.with.group.sync()
    %i.next = add i32 %i, 1
    br label %header
  after:
    %h = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0)
    %ptr3 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 3
    %val = load i32, ptr addrspace(3) %ptr3
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 0, i32 poison, i32 %val)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare void @llvm.dx.group.memory.barrier.with.group.sync()
  attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
)";

// Regression test for a `CompiledStage::createStage` bug found while
// implementing roadmap H155: `GroupSize` (and, before this fix, an
// outright "entry point 'main' did not survive the CPU pipeline" error)
// used to be read from a post-pipeline `Mod.getFunction(EntryName)`
// lookup, which silently assumed a function literally named `EntryName`
// always survives `feme::cpu::EntryWrapperPass`. That only holds for its
// no-barrier and straight-line-barrier paths; its `LoopShape`/
// `BranchShape` paths always erase the original function once wrapped
// (see `LoopWithBarrierShaderIR`'s own comment above), so this exact
// shape used to fail `CompiledStage::create` outright even though
// `EntryWrapperPass` itself wrapped it correctly -- this masked every
// otherwise-fixed loop-with-barrier case in the H124e/H124e(a)/H155
// roadmap bucket behind a completely unrelated failure one layer up the
// stack. `GroupSize` is now read from the entry point before the
// pipeline runs, matching how `SideEffectFlags`/`Signature` already do.
TEST(CompiledStageTest, LoopWithBarrierSurvivesCompiledStageCreation) {
  Context Ctx;
  SMDiagnostic Err;
  auto LLVMMod =
      parseAssemblyString(LoopWithBarrierShaderIR, Err, Ctx.getLLVMContext());
  ASSERT_TRUE(LLVMMod) << Err.getMessage().str();
  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));
  StageCompileOptions Opts;
  Opts.Stage = ShaderStage::Compute;
  Opts.WaveSize = 4;
  Expected<std::unique_ptr<CompiledStage>> Stage =
      CompiledStage::create(Ctx, std::move(Mod), Opts);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  EXPECT_EQ((*Stage)->getGroupSize(), (std::array<uint32_t, 3>{4, 1, 1}));

  std::vector<int32_t> Buffer(1, -1);
  FemeDescriptor Desc{};
  Desc.Data = Buffer.data();
  Desc.SizeInBytes = Buffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;

  DispatchResources Resources;
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
  PreparedDispatch Prepared = PreparedDispatch::create(
      (*Stage)->getResourceInfo(), Resources, {1, 1, 1});

  ASSERT_THAT_ERROR(
      (*Stage)->invokeGroup(Prepared, {0, 0, 0}, /*GroupShared=*/{}),
      Succeeded());
  // The loop stores each of `I`'s 4 iterations (`0`..`3`) into `@shared`
  // at its own index, so index 3's own final value is `3`.
  EXPECT_EQ(Buffer[0], 3);
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

// Evaluates the completed patch at this invocation's own domain location:
// linearly blends control points 0 and 1 by `u` and scales the result by a
// per-patch constant -- one load from each of the domain stage's three
// input sources (see DomainWrapper.cpp).
constexpr char DomainShaderIR[] = R"(
  define void @ds_main() #0 {
    %u = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    %p0 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %p1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
    %k = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
    %d = fsub float %p1, %p0
    %s = fmul float %d, %u
    %b = fadd float %p0, %s
    %r = fmul float %b, %k
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %r, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="domain" }
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

SignatureElement makeDomainLocationInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::DomainLocation;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.ComponentCount = 3;
  Elt.BitWidth = 32;
  return Elt;
}

SignatureElement makeFloatPatchInput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatInput(ElementID);
  Elt.Direction = SignatureDirection::PatchInput;
  Elt.Frequency = SignatureFrequency::PerPatch;
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

TEST(CompiledStageTest,
     FragmentExplicitGradientsSelectMipOffsetsAndComponents) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %ux = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %vx = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
      %uy = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
      %vy = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
      %c0 = insertelement <2 x float> poison, float %u, i32 0
      %coord = insertelement <2 x float> %c0, float %v, i32 1
      %x0 = insertelement <2 x float> poison, float %ux, i32 0
      %dx = insertelement <2 x float> %x0, float %vx, i32 1
      %y0 = insertelement <2 x float> poison, float %uy, i32 0
      %dy = insertelement <2 x float> %y0, float %vy, i32 1
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 1, i32 -1>)
      %rg = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 -1, i32 1>, float %clamp)
      %rgb = call <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 1, i32 -1>)
      %rgba = call <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 1, i32 -1>)
      %g = extractelement <2 x float> %rg, i32 1
      %b = extractelement <3 x float> %rgb, i32 2
      %a = extractelement <4 x float> %rgba, i32 3
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %r, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %g, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %b, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float %a, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>)
    declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>, float)
    declare <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>)
    declare <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  auto Color = [](unsigned Mip, unsigned X, unsigned Y, unsigned C) {
    return float(1000 * C + 100 * Mip + 10 * Y + X + 1);
  };
  std::vector<float> Texels(84);
  unsigned Offset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned Size = 4u >> Mip;
    for (unsigned Y = 0; Y < Size; ++Y)
      for (unsigned X = 0; X < Size; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Texels[Offset + (Y * Size + X) * 4 + C] = Color(Mip, X, Y, C);
    Offset += Size * Size * 4;
  }
  FemeImageSubresourceLayout Mips[] = {
      {0, 64, 256, 0}, {256, 32, 64, 0}, {320, 16, 16, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels.data();
  Image.SizeInBytes = Texels.size() * sizeof(float);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Image.Width = Image.Height = 4;
  Image.Depth = Image.ArrayLayers = Image.PlaneCount = Image.SampleCount = 1;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  FemeStageElement InputElements[3]{}, OutputElements[4]{};
  for (unsigned ID = 0; ID < 4; ++ID) {
    auto Element = ID < 3 ? makeFloatInput(ID) : makeFloatOutput(ID);
    Element.ComponentCount = ID == 0 ? 2 : ID == 2 ? 1 : 4;
    Sig.Elements.push_back(Element);
    auto &Layout = ID < 3 ? InputElements[ID] : OutputElements[ID];
    Layout.ElementID = ID;
    Layout.ComponentCount = Element.ComponentCount;
    Layout.RowCount = 1;
    Layout.ComponentStride = 4;
    Layout.InvocationStride = ID < 3 ? 28 : 16;
    Layout.DataOffset = ID == 1 ? 8 : ID == 2 ? 24 : 0;
  }
  FemeStageLayout InputLayout{}, OutputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 3;
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (unsigned Axis = 0; Axis < 4; ++Axis) {
        SCOPED_TRACE(testing::Message()
                     << "wave=" << WaveSize << " quads=" << QuadCount
                     << " gradient component=" << Axis);
        std::vector<float> Inputs(28 * QuadCount);
        std::vector<float> Outputs(16 * QuadCount + 2, -42.0f);
        std::vector<FemeFragmentInvocation> Invocations(QuadCount);
        std::vector<FemeFragmentResult> Results(QuadCount);
        for (auto &Invocation : Invocations) {
          Invocation.LiveMask = 0xf;
          Invocation.SideEffectMask = 0x5;
        }
        FragmentResources Resources;
        Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
        Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
        Resources.InputLayout = &InputLayout;
        Resources.Inputs = Inputs.data();
        Resources.OutputLayout = &OutputLayout;
        Resources.Outputs = Outputs.data() + 1;
        Resources.Invocations = Invocations;
        Resources.Results = Results;
        auto Prepared = PreparedFragmentBatch::create(
            (*Stage)->getResourceInfo(), Resources);
        for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            Inputs[Lane * 7] = Repeat & 1 ? 0.75f : 0.25f;
            Inputs[Lane * 7 + 1] = 0.5f;
            unsigned Mip = (Lane / 2 + Repeat) % 3;
            Inputs[Lane * 7 + 2 + Axis] = 0.25f * float(1u << Mip);
            Inputs[Lane * 7 + 6] = ((Lane / 2 + Repeat) & 1) ? 2.0f : 0.0f;
          }
          std::fill(Outputs.begin(), Outputs.end(), -42.0f);
          ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
          EXPECT_EQ(Outputs.front(), -42.0f);
          EXPECT_EQ(Outputs.back(), -42.0f);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane)
            for (unsigned C = 0; C < 4; ++C) {
              unsigned Mip = (Lane / 2 + Repeat) % 3;
              if (C == 1)
                Mip = std::max(Mip, unsigned(Inputs[Lane * 7 + 6]));
              int Size = 4 >> Mip;
              int Sign = C == 1 ? -1 : 1;
              unsigned X =
                  std::clamp(int(Inputs[Lane * 7] * Size) + Sign, 0, Size - 1);
              unsigned Y = std::clamp(int(Inputs[Lane * 7 + 1] * Size) - Sign,
                                      0, Size - 1);
              EXPECT_EQ(Outputs[1 + Lane * 4 + C],
                        (Lane & 1) ? -42.0f : Color(Mip, X, Y, C));
            }
          for (const auto &Result : Results) {
            EXPECT_EQ(Result.LiveMask, 0xfu);
            EXPECT_EQ(Result.SideEffectMask, 0x5u);
          }
        }
      }
  }
}

TEST(CompiledStageTest, ComputeExplicitGradientsSelectDivergentMipsAndClamp) {
  constexpr char IR[] = R"(
    define void @main() #0 {
      %t = call target("dx.Texture", float, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_2t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 0)
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %bit = and i32 %tid, 1
      %odd = icmp ne i32 %bit, 0
      %g = select i1 %odd, float 1.0, float 0.25
      %clamp = select i1 %odd, float 0.0, float 2.0
      %dx = insertelement <2 x float> zeroinitializer, float %g, i32 0
      %v = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> <float 0.5, float 0.5>, <2 x float> %dx, <2 x float> zeroinitializer, <2 x i32> <i32 1, i32 -1>)
      %c = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> <float 0.5, float 0.5>, <2 x float> %dx, <2 x float> zeroinitializer, <2 x i32> <i32 -1, i32 1>, float %clamp)
      %vi = fptoui float %v to i32
      %ci = fptoui float %c to i32
      %base0 = mul i32 %tid, 8
      %base = add i32 %base0, 4
      %next = add i32 %base, 4
      call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %vi)
      call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %next, i32 poison, i32 %ci)
      ret void
    }
    declare target("dx.Texture", float, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_2t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
    declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>)
    declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>, float)
    declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
  )";
  float Texels[21];
  std::fill_n(Texels, 16, 1.0f);
  std::fill_n(Texels + 16, 4, 11.0f);
  Texels[20] = 21.0f;
  FemeImageSubresourceLayout Mips[] = {
      {0, 16, 64, 0}, {64, 8, 16, 0}, {80, 4, 4, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels;
  Image.SizeInBytes = sizeof(Texels);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Image.Width = Image.Height = 4;
  Image.Depth = Image.ArrayLayers = Image.PlaneCount = Image.SampleCount = 1;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);
  for (bool Reference : {false, true})
    for (unsigned WaveSize : {4u, 8u}) {
      SCOPED_TRACE(testing::Message()
                   << "reference=" << Reference << " wave=" << WaveSize);
      Context Ctx;
      SMDiagnostic Err;
      auto M = parseAssemblyString(IR, Err, Ctx.getLLVMContext());
      ASSERT_TRUE(M) << Err.getMessage().str();
      JITOptions Opts;
      Opts.WaveSize = WaveSize;
      Opts.Reference = Reference;
      auto Stage = CompiledStage::create(
          Ctx, feme::Module::fromLLVMIR(std::move(M)), Opts);
      ASSERT_THAT_EXPECTED(Stage, Succeeded());
      EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
      std::vector<uint32_t> Buffer(12, 42);
      FemeDescriptor Desc{};
      Desc.Data = Buffer.data();
      Desc.SizeInBytes = Buffer.size() * sizeof(uint32_t);
      Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
      Desc.Flags = FEME_DESCRIPTOR_UAV;
      DispatchResources Resources;
      Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
      Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
      Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
      auto Prepared = PreparedDispatch::create((*Stage)->getResourceInfo(),
                                               Resources, {1, 1, 1});
      for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
        std::fill(Buffer.begin(), Buffer.end(), 42);
        ASSERT_THAT_ERROR((*Stage)->invokeGroup(Prepared, {0, 0, 0}, {}),
                          Succeeded());
        EXPECT_EQ(Buffer.front(), 42u);
        EXPECT_EQ(Buffer.back(), 42u);
        for (unsigned Lane = 0; Lane < 5; ++Lane) {
          EXPECT_EQ(Buffer[1 + Lane * 2], Lane & 1 ? 21u : 1u);
          EXPECT_EQ(Buffer[2 + Lane * 2], 21u);
        }
      }
    }
}

TEST(CompiledStageTest,
     FragmentArrayExplicitGradientsSelectLayerMipAndOffsets) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %layer = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %ux = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %vx = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
      %uy = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
      %vy = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
      %c0 = insertelement <3 x float> poison, float %u, i32 0
      %c1 = insertelement <3 x float> %c0, float %v, i32 1
      %coord = insertelement <3 x float> %c1, float %layer, i32 2
      %x0 = insertelement <2 x float> poison, float %ux, i32 0
      %dx = insertelement <2 x float> %x0, float %vx, i32 1
      %y0 = insertelement <2 x float> poison, float %uy, i32 0
      %dy = insertelement <2 x float> %y0, float %vy, i32 1
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %rgba = call <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 1, i32 -1>)
      %rg = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 -1, i32 1>, float %clamp)
      %r = extractelement <4 x float> %rgba, i32 0
      %g = extractelement <2 x float> %rg, i32 1
      %b = extractelement <4 x float> %rgba, i32 2
      %a = extractelement <4 x float> %rgba, i32 3
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %r, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %g, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %b, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float %a, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, <2 x float>, <2 x float>, <2 x i32>)
    declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, <2 x float>, <2 x float>, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  auto Color = [](unsigned Layer, unsigned Mip, unsigned X, unsigned Y,
                  unsigned C) {
    return float(10000 * Layer + 1000 * C + 100 * Mip + 10 * Y + X + 1);
  };
  std::vector<float> Texels(168);
  unsigned Offset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned Size = 4u >> Mip;
    for (unsigned Layer = 0; Layer < 2; ++Layer)
      for (unsigned Y = 0; Y < Size; ++Y)
        for (unsigned X = 0; X < Size; ++X)
          for (unsigned C = 0; C < 4; ++C)
            Texels[Offset + ((Layer * Size + Y) * Size + X) * 4 + C] =
                Color(Layer, Mip, X, Y, C);
    Offset += 2 * Size * Size * 4;
  }
  FemeImageSubresourceLayout Mips[] = {
      {0, 64, 256, 0}, {512, 32, 64, 0}, {640, 16, 16, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels.data();
  Image.SizeInBytes = Texels.size() * sizeof(float);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture2DArray);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Image.Width = Image.Height = 4;
  Image.Depth = Image.PlaneCount = Image.SampleCount = 1;
  Image.ArrayLayers = 2;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  FemeStageElement InputElements[3]{}, OutputElements[4]{};
  for (unsigned ID = 0; ID < 4; ++ID) {
    auto Element = ID < 3 ? makeFloatInput(ID) : makeFloatOutput(ID);
    Element.ComponentCount = ID == 0 ? 3 : ID == 2 ? 1 : 4;
    Sig.Elements.push_back(Element);
    auto &Layout = ID < 3 ? InputElements[ID] : OutputElements[ID];
    Layout.ElementID = ID;
    Layout.ComponentCount = Element.ComponentCount;
    Layout.RowCount = 1;
    Layout.ComponentStride = 4;
    Layout.InvocationStride = ID < 3 ? 32 : 16;
    Layout.DataOffset = ID == 1 ? 12 : ID == 2 ? 28 : 0;
  }
  FemeStageLayout InputLayout{}, OutputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 3;
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (unsigned Axis = 0; Axis < 4; ++Axis) {
        SCOPED_TRACE(testing::Message()
                     << "wave=" << WaveSize << " quads=" << QuadCount
                     << " gradient component=" << Axis);
        std::vector<float> Inputs(32 * QuadCount);
        std::vector<float> Outputs(16 * QuadCount + 2, -42.0f);
        std::vector<FemeFragmentInvocation> Invocations(QuadCount);
        std::vector<FemeFragmentResult> Results(QuadCount);
        for (auto &Invocation : Invocations) {
          Invocation.LiveMask = 0xf;
          Invocation.SideEffectMask = 0x5;
        }
        FragmentResources Resources;
        Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
        Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
        Resources.InputLayout = &InputLayout;
        Resources.Inputs = Inputs.data();
        Resources.OutputLayout = &OutputLayout;
        Resources.Outputs = Outputs.data() + 1;
        Resources.Invocations = Invocations;
        Resources.Results = Results;
        auto Prepared = PreparedFragmentBatch::create(
            (*Stage)->getResourceInfo(), Resources);
        for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            Inputs[Lane * 8] = Repeat & 1 ? 0.75f : 0.25f;
            Inputs[Lane * 8 + 1] = 0.5f;
            Inputs[Lane * 8 + 2] = float((Lane ^ (Lane >> 1) ^ Repeat) & 1);
            unsigned Mip = (Lane / 2 + Repeat) % 3;
            Inputs[Lane * 8 + 3 + Axis] =
                (Axis & 1 ? -0.25f : 0.25f) * float(1u << Mip);
            Inputs[Lane * 8 + 7] = ((Lane / 2 + Repeat) & 1) ? 2.0f : 0.0f;
          }
          std::fill(Outputs.begin(), Outputs.end(), -42.0f);
          ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
          EXPECT_EQ(Outputs.front(), -42.0f);
          EXPECT_EQ(Outputs.back(), -42.0f);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane)
            for (unsigned C = 0; C < 4; ++C) {
              unsigned Mip = (Lane / 2 + Repeat) % 3;
              if (C == 1)
                Mip = std::max(Mip, unsigned(Inputs[Lane * 8 + 7]));
              int Size = 4 >> Mip;
              int Sign = C == 1 ? -1 : 1;
              unsigned X =
                  std::clamp(int(Inputs[Lane * 8] * Size) + Sign, 0, Size - 1);
              unsigned Y = std::clamp(int(Inputs[Lane * 8 + 1] * Size) - Sign,
                                      0, Size - 1);
              EXPECT_EQ(Outputs[1 + Lane * 4 + C],
                        (Lane & 1) ? -42.0f
                                   : Color(unsigned(Inputs[Lane * 8 + 2]), Mip,
                                           X, Y, C));
            }
          for (const auto &Result : Results) {
            EXPECT_EQ(Result.LiveMask, 0xfu);
            EXPECT_EQ(Result.SideEffectMask, 0x5u);
          }
        }
      }
  }
}

TEST(CompiledStageTest,
     ComputeArrayExplicitGradientsSelectDivergentLayerAndMip) {
  constexpr char IR[] = R"(
    define void @main() #0 {
      %t = call target("dx.Texture", float, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_7t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 0)
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %bit = and i32 %tid, 1
      %odd = icmp ne i32 %bit, 0
      %shift = lshr i32 %tid, 1
      %layerbit = and i32 %shift, 1
      %layer = uitofp i32 %layerbit to float
      %coord = insertelement <3 x float> <float 0.5, float 0.5, float poison>, float %layer, i32 2
      %g = select i1 %odd, float 1.0, float 0.25
      %clamp = select i1 %odd, float 0.0, float 2.0
      %dx = insertelement <2 x float> zeroinitializer, float %g, i32 0
      %v = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, <2 x float> %dx, <2 x float> zeroinitializer, <2 x i32> <i32 1, i32 -1>)
      %c = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, <2 x float> %dx, <2 x float> zeroinitializer, <2 x i32> <i32 -1, i32 1>, float %clamp)
      %vi = fptoui float %v to i32
      %ci = fptoui float %c to i32
      %base0 = mul i32 %tid, 8
      %base = add i32 %base0, 4
      %next = add i32 %base, 4
      call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %vi)
      call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %next, i32 poison, i32 %ci)
      ret void
    }
    declare target("dx.Texture", float, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_7t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
    declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, <2 x float>, <2 x float>, <2 x i32>)
    declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, <2 x float>, <2 x float>, <2 x i32>, float)
    declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
  )";
  float Texels[42];
  unsigned Offset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned Count = (4u >> Mip) * (4u >> Mip);
    for (unsigned Layer = 0; Layer < 2; ++Layer)
      std::fill_n(Texels + Offset + Layer * Count, Count,
                  float(100 * Layer + 10 * Mip + 1));
    Offset += 2 * Count;
  }
  FemeImageSubresourceLayout Mips[] = {
      {0, 16, 64, 0}, {128, 8, 16, 0}, {160, 4, 4, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels;
  Image.SizeInBytes = sizeof(Texels);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture2DArray);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Image.Width = Image.Height = 4;
  Image.Depth = Image.PlaneCount = Image.SampleCount = 1;
  Image.ArrayLayers = 2;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);
  for (bool Reference : {false, true})
    for (unsigned WaveSize : {4u, 8u}) {
      SCOPED_TRACE(testing::Message()
                   << "reference=" << Reference << " wave=" << WaveSize);
      Context Ctx;
      SMDiagnostic Err;
      auto M = parseAssemblyString(IR, Err, Ctx.getLLVMContext());
      ASSERT_TRUE(M) << Err.getMessage().str();
      JITOptions Opts;
      Opts.WaveSize = WaveSize;
      Opts.Reference = Reference;
      auto Stage = CompiledStage::create(
          Ctx, feme::Module::fromLLVMIR(std::move(M)), Opts);
      ASSERT_THAT_EXPECTED(Stage, Succeeded());
      EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
      std::vector<uint32_t> Buffer(12, 42);
      FemeDescriptor Desc{};
      Desc.Data = Buffer.data();
      Desc.SizeInBytes = Buffer.size() * sizeof(uint32_t);
      Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
      Desc.Flags = FEME_DESCRIPTOR_UAV;
      DispatchResources Resources;
      Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
      Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
      Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
      auto Prepared = PreparedDispatch::create((*Stage)->getResourceInfo(),
                                               Resources, {1, 1, 1});
      for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
        std::fill(Buffer.begin(), Buffer.end(), 42);
        ASSERT_THAT_ERROR((*Stage)->invokeGroup(Prepared, {0, 0, 0}, {}),
                          Succeeded());
        EXPECT_EQ(Buffer.front(), 42u);
        EXPECT_EQ(Buffer.back(), 42u);
        for (unsigned Lane = 0; Lane < 5; ++Lane) {
          unsigned Layer = (Lane >> 1) & 1;
          EXPECT_EQ(Buffer[1 + Lane * 2], 100 * Layer + (Lane & 1 ? 21u : 1u));
          EXPECT_EQ(Buffer[2 + Lane * 2], 100 * Layer + 21u);
        }
      }
    }
}

TEST(CompiledStageTest, FragmentBiasedSamplingSelectsMipOffsetsAndComponents) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %bias = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
      %c0 = insertelement <2 x float> poison, float %u, i32 0
      %coord = insertelement <2 x float> %c0, float %v, i32 1
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, <2 x i32> <i32 1, i32 -1>)
      %rg = call <2 x float> @llvm.dx.resource.samplebias.v2f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, <2 x i32> <i32 1, i32 -1>)
      %rgb = call <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, <2 x i32> <i32 1, i32 -1>)
      %rgba = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, <2 x i32> <i32 1, i32 -1>)
      %base = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float 0.0, <2 x i32> <i32 1, i32 -1>)
      %clamped = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, <2 x i32> <i32 1, i32 -1>, float %clamp)
      %g = extractelement <2 x float> %rg, i32 1
      %b = extractelement <3 x float> %rgb, i32 2
      %a = extractelement <4 x float> %rgba, i32 3
      %baseline = extractelement <4 x float> %base, i32 0
      %floor = extractelement <4 x float> %clamped, i32 0
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %r, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %g, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %b, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float %a, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %baseline, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %floor, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
    declare <2 x float> @llvm.dx.resource.samplebias.v2f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
    declare <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
    declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
    declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  std::vector<float> Texels(84);
  unsigned Offset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned Size = 4u >> Mip;
    for (unsigned Y = 0; Y < Size; ++Y)
      for (unsigned X = 0; X < Size; ++X)
        for (unsigned Component = 0; Component < 4; ++Component)
          Texels[Offset + (Y * Size + X) * 4 + Component] =
              float(1000 * Component + 100 * Mip + 10 * Y + X + 1);
    Offset += Size * Size * 4;
  }
  FemeImageSubresourceLayout Mips[] = {
      {0, 64, 256, 0}, {256, 32, 64, 0}, {320, 16, 16, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels.data();
  Image.SizeInBytes = Texels.size() * sizeof(float);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Image.Width = Image.Height = 4;
  Image.Depth = Image.ArrayLayers = Image.PlaneCount = Image.SampleCount = 1;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  FemeStageElement InputElements[2]{};
  FemeStageElement OutputElements[4]{};
  for (unsigned ID = 0; ID < 4; ++ID) {
    auto Element = ID < 2 ? makeFloatInput(ID) : makeFloatOutput(ID);
    Element.ComponentCount = ID == 2 ? 4 : 2;
    Sig.Elements.push_back(Element);
    FemeStageElement &LayoutElement =
        ID < 2 ? InputElements[ID] : OutputElements[ID];
    LayoutElement.ElementID = ID;
    LayoutElement.ComponentCount = Element.ComponentCount;
    LayoutElement.RowCount = 1;
    LayoutElement.ComponentStride = 4;
    LayoutElement.InvocationStride = ID < 2 ? 16 : 24;
    LayoutElement.DataOffset = ID < 2 ? ID * 8 : (ID - 2) * 16;
  }
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 2;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (bool Vertical : {false, true}) {
        SCOPED_TRACE(testing::Message()
                     << "wave=" << WaveSize << " quads=" << QuadCount
                     << " vertical=" << Vertical);
        std::vector<float> Inputs(16 * QuadCount);
        for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
          Inputs[Lane * 4] = Vertical ? 0.5f : (Lane & 1) ? 0.5f : 0.0f;
          Inputs[Lane * 4 + 1] = Vertical ? (Lane & 2) ? 0.5f : 0.0f : 0.5f;
        }
        std::vector<float> Outputs(24 * QuadCount + 2, -42.0f);
        std::vector<FemeFragmentInvocation> Invocations(QuadCount);
        std::vector<FemeFragmentResult> Results(QuadCount);
        for (auto &Invocation : Invocations) {
          Invocation.LiveMask = 0xf;
          Invocation.SideEffectMask = 0x5;
        }
        FragmentResources Resources;
        Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
        Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
        Resources.InputLayout = &InputLayout;
        Resources.Inputs = Inputs.data();
        Resources.OutputLayout = &OutputLayout;
        Resources.Outputs = Outputs.data() + 1;
        Resources.Invocations = Invocations;
        Resources.Results = Results;
        auto Prepared = PreparedFragmentBatch::create(
            (*Stage)->getResourceInfo(), Resources);
        for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            Inputs[Lane * 4 + 2] = float((Lane / 2 + Repeat) % 3) - 1.0f;
            Inputs[Lane * 4 + 3] = ((Lane / 2 + Repeat) & 1) ? 2.0f : 0.0f;
          }
          std::fill(Outputs.begin(), Outputs.end(), -42.0f);
          ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
          EXPECT_EQ(Outputs.front(), -42.0f);
          EXPECT_EQ(Outputs.back(), -42.0f);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            unsigned BiasedMip = unsigned(1.0f + Inputs[Lane * 4 + 2]);
            unsigned ClampedMip =
                std::max(BiasedMip, unsigned(Inputs[Lane * 4 + 3]));
            auto Expected = [&](unsigned Mip, unsigned Component) {
              int Size = 4 >> Mip;
              int X = std::clamp(int(Inputs[Lane * 4] * Size) + 1, 0, Size - 1);
              int Y =
                  std::clamp(int(Inputs[Lane * 4 + 1] * Size) - 1, 0, Size - 1);
              return float(1000 * Component + 100 * Mip + 10 * Y + X + 1);
            };
            for (unsigned Component = 0; Component < 6; ++Component) {
              unsigned Mip = Component < 4    ? BiasedMip
                             : Component == 4 ? 1
                                              : ClampedMip;
              EXPECT_EQ(Outputs[1 + Lane * 6 + Component],
                        (Lane & 1)
                            ? -42.0f
                            : Expected(Mip, Component < 4 ? Component : 0));
            }
          }
          for (const auto &Result : Results) {
            EXPECT_EQ(Result.LiveMask, 0xfu);
            EXPECT_EQ(Result.SideEffectMask, 0x5u);
          }
        }
      }
  }
}

TEST(CompiledStageTest, FragmentArrayBiasedSamplingSelectsLayerMipAndOffsets) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %layer = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %bias = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
      %c0 = insertelement <3 x float> poison, float %u, i32 0
      %c1 = insertelement <3 x float> %c0, float %v, i32 1
      %coord = insertelement <3 x float> %c1, float %layer, i32 2
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %biased = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %bias, <2 x i32> <i32 1, i32 -1>)
      %base = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float 0.0, <2 x i32> <i32 1, i32 -1>)
      %clamped = call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %bias, <2 x i32> <i32 1, i32 -1>, float %clamp)
      %a = extractelement <4 x float> %biased, i32 3
      %g = extractelement <2 x float> %clamped, i32 1
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %a, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %base, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %g, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
    declare <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  auto Color = [](unsigned Layer, unsigned Mip, unsigned X, unsigned Y,
                  unsigned Component) {
    return float(10000 * Layer + 1000 * Component +
                 100 * (Layer ? 2 - Mip : Mip) + 10 * Y + X + 1);
  };
  std::vector<float> Texels(168);
  unsigned Offset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned Size = 4u >> Mip;
    for (unsigned Layer = 0; Layer < 2; ++Layer)
      for (unsigned Y = 0; Y < Size; ++Y)
        for (unsigned X = 0; X < Size; ++X)
          for (unsigned C = 0; C < 4; ++C)
            Texels[Offset + ((Layer * Size + Y) * Size + X) * 4 + C] =
                Color(Layer, Mip, X, Y, C);
    Offset += 2 * Size * Size * 4;
  }
  FemeImageSubresourceLayout Mips[] = {
      {0, 64, 256, 0}, {512, 32, 64, 0}, {640, 16, 16, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels.data();
  Image.SizeInBytes = Texels.size() * sizeof(float);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture2DArray);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Image.Width = Image.Height = 4;
  Image.Depth = Image.PlaneCount = Image.SampleCount = 1;
  Image.ArrayLayers = 2;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  FemeStageElement InputElements[2]{};
  FemeStageElement OutputElements[3]{};
  for (unsigned ID = 0; ID < 3; ++ID) {
    auto Element = ID < 2 ? makeFloatInput(ID) : makeFloatOutput(ID);
    Element.ComponentCount = ID == 1 ? 2 : 3;
    Sig.Elements.push_back(Element);
    FemeStageElement &LayoutElement =
        ID < 2 ? InputElements[ID] : OutputElements[ID];
    LayoutElement.ElementID = ID;
    LayoutElement.ComponentCount = Element.ComponentCount;
    LayoutElement.RowCount = 1;
    LayoutElement.ComponentStride = 4;
    LayoutElement.InvocationStride = ID < 2 ? 20 : 12;
    LayoutElement.DataOffset = ID == 1 ? 12 : 0;
  }
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 2;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 3;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (bool Vertical : {false, true}) {
        SCOPED_TRACE(testing::Message()
                     << "wave=" << WaveSize << " quads=" << QuadCount
                     << " vertical=" << Vertical);
        std::vector<float> Inputs(20 * QuadCount);
        for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
          Inputs[Lane * 5] = Vertical ? 0.5f : (Lane & 1) ? 0.5f : 0.0f;
          Inputs[Lane * 5 + 1] = Vertical ? (Lane & 2) ? 0.5f : 0.0f : 0.5f;
        }
        std::vector<float> Outputs(12 * QuadCount + 2, -42.0f);
        std::vector<FemeFragmentInvocation> Invocations(QuadCount);
        std::vector<FemeFragmentResult> Results(QuadCount);
        for (auto &Invocation : Invocations) {
          Invocation.LiveMask = 0xf;
          Invocation.SideEffectMask = 0x5;
        }
        FragmentResources Resources;
        Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
        Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
        Resources.InputLayout = &InputLayout;
        Resources.Inputs = Inputs.data();
        Resources.OutputLayout = &OutputLayout;
        Resources.Outputs = Outputs.data() + 1;
        Resources.Invocations = Invocations;
        Resources.Results = Results;
        auto Prepared = PreparedFragmentBatch::create(
            (*Stage)->getResourceInfo(), Resources);
        for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            Inputs[Lane * 5 + 2] = float((Lane ^ (Lane >> 1) ^ Repeat) & 1);
            Inputs[Lane * 5 + 3] = float((Lane / 2 + Repeat) % 3) - 1.0f;
            Inputs[Lane * 5 + 4] = ((Lane / 2 + Repeat) & 1) ? 2.0f : 0.0f;
          }
          std::fill(Outputs.begin(), Outputs.end(), -42.0f);
          ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
          EXPECT_EQ(Outputs.front(), -42.0f);
          EXPECT_EQ(Outputs.back(), -42.0f);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            unsigned BiasedMip = unsigned(1.0f + Inputs[Lane * 5 + 3]);
            unsigned ClampedMip =
                std::max(BiasedMip, unsigned(Inputs[Lane * 5 + 4]));
            for (unsigned Output = 0; Output < 3; ++Output) {
              unsigned Mip = Output == 0   ? BiasedMip
                             : Output == 1 ? 1
                                           : ClampedMip;
              int Size = 4 >> Mip;
              unsigned X =
                  std::clamp(int(Inputs[Lane * 5] * Size) + 1, 0, Size - 1);
              unsigned Y =
                  std::clamp(int(Inputs[Lane * 5 + 1] * Size) - 1, 0, Size - 1);
              unsigned Component = Output == 0 ? 3 : Output == 1 ? 0 : 1;
              EXPECT_EQ(Outputs[1 + Lane * 3 + Output],
                        (Lane & 1) ? -42.0f
                                   : Color(unsigned(Inputs[Lane * 5 + 2]), Mip,
                                           X, Y, Component));
            }
          }
          for (const auto &Result : Results) {
            EXPECT_EQ(Result.LiveMask, 0xfu);
            EXPECT_EQ(Result.SideEffectMask, 0x5u);
          }
        }
      }
  }
}

TEST(CompiledStageTest,
     Fragment3DBiasedSamplingSelectsMipOffsetsAndComponents) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %w = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %bias = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
      %c0 = insertelement <3 x float> poison, float %u, i32 0
      %c1 = insertelement <3 x float> %c0, float %v, i32 1
      %coord = insertelement <3 x float> %c1, float %w, i32 2
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %bias, <3 x i32> <i32 1, i32 -1, i32 1>)
      %rg = call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %bias, <3 x i32> <i32 -1, i32 1, i32 -1>, float %clamp)
      %rgb = call <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %bias, <3 x i32> <i32 1, i32 -1, i32 1>)
      %base = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float 0.0, <3 x i32> <i32 1, i32 -1, i32 1>)
      %g = extractelement <2 x float> %rg, i32 1
      %b = extractelement <3 x float> %rgb, i32 2
      %a = extractelement <4 x float> %base, i32 3
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %r, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %g, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %b, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float %a, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, float, <3 x i32>)
    declare <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, float, <3 x i32>, float)
    declare <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, float, <3 x i32>)
    declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, float, <3 x i32>)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  auto Color = [](unsigned Mip, unsigned X, unsigned Y, unsigned Z,
                  unsigned Component) {
    return float(10000 * Component + 1000 * Mip + 100 * Z + 10 * Y + X + 1);
  };
  std::vector<float> Texels(292);
  unsigned Offset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned Size = 4u >> Mip;
    for (unsigned Z = 0; Z < Size; ++Z)
      for (unsigned Y = 0; Y < Size; ++Y)
        for (unsigned X = 0; X < Size; ++X)
          for (unsigned C = 0; C < 4; ++C)
            Texels[Offset + ((Z * Size + Y) * Size + X) * 4 + C] =
                Color(Mip, X, Y, Z, C);
    Offset += Size * Size * Size * 4;
  }
  FemeImageSubresourceLayout Mips[] = {
      {0, 64, 256, 0}, {1024, 32, 64, 0}, {1152, 16, 16, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels.data();
  Image.SizeInBytes = Texels.size() * sizeof(float);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture3D);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Image.Width = Image.Height = Image.Depth = 4;
  Image.ArrayLayers = Image.PlaneCount = Image.SampleCount = 1;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV = Sampler.AddressW =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  FemeStageElement InputElements[2]{};
  FemeStageElement OutputElements[3]{};
  for (unsigned ID = 0; ID < 3; ++ID) {
    auto Element = ID < 2 ? makeFloatInput(ID) : makeFloatOutput(ID);
    Element.ComponentCount = ID == 0 ? 3 : ID == 1 ? 2 : 4;
    Sig.Elements.push_back(Element);
    FemeStageElement &LayoutElement =
        ID < 2 ? InputElements[ID] : OutputElements[ID];
    LayoutElement.ElementID = ID;
    LayoutElement.ComponentCount = Element.ComponentCount;
    LayoutElement.RowCount = 1;
    LayoutElement.ComponentStride = 4;
    LayoutElement.InvocationStride = ID < 2 ? 20 : 16;
    LayoutElement.DataOffset = ID == 1 ? 12 : 0;
  }
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 2;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 3;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (bool Vertical : {false, true})
        for (unsigned Axis = 0; Axis < 3; ++Axis) {
          SCOPED_TRACE(testing::Message()
                       << "wave=" << WaveSize << " quads=" << QuadCount
                       << " vertical=" << Vertical << " axis=" << Axis);
          std::vector<float> Inputs(20 * QuadCount);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane)
            for (unsigned C = 0; C < 3; ++C)
              Inputs[Lane * 5 + C] =
                  C != Axis || (Lane & (Vertical ? 2 : 1)) ? 0.5f : 0.0f;
          std::vector<float> Outputs(16 * QuadCount + 2, -42.0f);
          std::vector<FemeFragmentInvocation> Invocations(QuadCount);
          std::vector<FemeFragmentResult> Results(QuadCount);
          for (auto &Invocation : Invocations) {
            Invocation.LiveMask = 0xf;
            Invocation.SideEffectMask = 0x5;
          }
          FragmentResources Resources;
          Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
          Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
          Resources.InputLayout = &InputLayout;
          Resources.Inputs = Inputs.data();
          Resources.OutputLayout = &OutputLayout;
          Resources.Outputs = Outputs.data() + 1;
          Resources.Invocations = Invocations;
          Resources.Results = Results;
          auto Prepared = PreparedFragmentBatch::create(
              (*Stage)->getResourceInfo(), Resources);
          for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              Inputs[Lane * 5 + 3] = float((Lane / 2 + Repeat) % 3) - 1.0f;
              Inputs[Lane * 5 + 4] = ((Lane / 2 + Repeat) & 1) ? 2.0f : 0.0f;
            }
            std::fill(Outputs.begin(), Outputs.end(), -42.0f);
            ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
            EXPECT_EQ(Outputs.front(), -42.0f);
            EXPECT_EQ(Outputs.back(), -42.0f);
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              unsigned BiasedMip = unsigned(1.0f + Inputs[Lane * 5 + 3]);
              unsigned ClampedMip =
                  std::max(BiasedMip, unsigned(Inputs[Lane * 5 + 4]));
              for (unsigned Out = 0; Out < 4; ++Out) {
                unsigned Mip = Out == 3 ? 1 : Out == 1 ? ClampedMip : BiasedMip;
                int Size = 4 >> Mip;
                int Sign = Out == 1 ? -1 : 1;
                unsigned X = std::clamp(int(Inputs[Lane * 5] * Size) + Sign, 0,
                                        Size - 1);
                unsigned Y = std::clamp(int(Inputs[Lane * 5 + 1] * Size) - Sign,
                                        0, Size - 1);
                unsigned Z = std::clamp(int(Inputs[Lane * 5 + 2] * Size) + Sign,
                                        0, Size - 1);
                EXPECT_EQ(Outputs[1 + Lane * 4 + Out],
                          (Lane & 1) ? -42.0f : Color(Mip, X, Y, Z, Out));
              }
            }
            for (const auto &Result : Results) {
              EXPECT_EQ(Result.LiveMask, 0xfu);
              EXPECT_EQ(Result.SideEffectMask, 0x5u);
            }
          }
        }
  }
}

TEST(CompiledStageTest, Fragment1DBiasedSamplingSelectsLayerMipAndOffsets) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %layer = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %bias = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
      %c0 = insertelement <2 x float> poison, float %u, i32 0
      %coord = insertelement <2 x float> %c0, float %layer, i32 1
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
      %a = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 1)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %plain = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float %u, float %bias, i32 -1)
      %plainclamp = call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float %u, float %bias, i32 -1, float %clamp)
      %array = call <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %a, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, i32 1)
      %arrayclamp = call float @llvm.dx.resource.samplebias.clamp.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %a, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, i32 1, float %clamp)
      %plainbase = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float %u, float 0.0, i32 -1)
      %arraybase = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %a, target("dx.Sampler", 0) %s, <2 x float> %coord, float 0.0, i32 1)
      %alpha = extractelement <4 x float> %plain, i32 3
      %green = extractelement <2 x float> %plainclamp, i32 1
      %blue = extractelement <3 x float> %array, i32 2
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %alpha, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %green, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %blue, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float %arrayclamp, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %plainbase, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %arraybase, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, i32)
    declare <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, i32, float)
    declare <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, i32)
    declare float @llvm.dx.resource.samplebias.clamp.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, i32, float)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, i32)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  auto Color = [](unsigned Layer, unsigned Mip, unsigned X,
                  unsigned Component) {
    return float(10000 * Layer + 1000 * Component +
                 100 * (Layer ? 2 - Mip : Mip) + X + 1);
  };
  std::vector<float> PlainTexels(28), ArrayTexels(56);
  unsigned Offset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned Size = 4u >> Mip;
    for (unsigned Layer = 0; Layer < 2; ++Layer)
      for (unsigned X = 0; X < Size; ++X)
        for (unsigned C = 0; C < 4; ++C) {
          ArrayTexels[Offset * 2 + (Layer * Size + X) * 4 + C] =
              Color(Layer, Mip, X, C);
          if (!Layer)
            PlainTexels[Offset + X * 4 + C] = Color(0, Mip, X, C);
        }
    Offset += Size * 4;
  }
  FemeImageSubresourceLayout PlainMips[] = {
      {0, 64, 64, 0}, {64, 32, 32, 0}, {96, 16, 16, 0}};
  FemeImageSubresourceLayout ArrayMips[] = {
      {0, 64, 64, 0}, {128, 32, 32, 0}, {192, 16, 16, 0}};
  FemeImageDescriptor Images[2]{};
  Images[0].Data = PlainTexels.data();
  Images[0].SizeInBytes = PlainTexels.size() * sizeof(float);
  Images[0].Dimension = static_cast<uint32_t>(ImageDimension::Texture1D);
  Images[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Images[0].Width = 4;
  Images[0].Height = Images[0].Depth = Images[0].ArrayLayers =
      Images[0].PlaneCount = Images[0].SampleCount = 1;
  Images[0].MipLevels = Images[0].MipLayoutCount = 3;
  Images[0].MipLayouts = PlainMips;
  Images[0].Flags = FEME_IMAGE_SAMPLED;
  Images[1] = Images[0];
  Images[1].Data = ArrayTexels.data();
  Images[1].SizeInBytes = ArrayTexels.size() * sizeof(float);
  Images[1].Dimension = static_cast<uint32_t>(ImageDimension::Texture1DArray);
  Images[1].ArrayLayers = 2;
  Images[1].MipLayouts = ArrayMips;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  auto Input = makeFloatInput(0);
  auto Output = makeFloatOutput(1);
  auto Baseline = makeFloatOutput(2);
  Input.ComponentCount = Output.ComponentCount = 4;
  Baseline.ComponentCount = 2;
  Sig.Elements = {Input, Output, Baseline};
  FemeStageElement InputElement{};
  InputElement.ComponentCount = 4;
  InputElement.RowCount = 1;
  InputElement.InvocationStride = 16;
  InputElement.ComponentStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = &InputElement;
  InputLayout.ElementCount = 1;
  FemeStageElement OutputElements[3]{};
  for (unsigned ID : {1u, 2u}) {
    OutputElements[ID].ElementID = ID;
    OutputElements[ID].ComponentCount = ID == 1 ? 4 : 2;
    OutputElements[ID].RowCount = 1;
    OutputElements[ID].InvocationStride = 24;
    OutputElements[ID].ComponentStride = 4;
    OutputElements[ID].DataOffset = ID == 1 ? 0 : 16;
  }
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 3;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (bool Vertical : {false, true})
        for (unsigned BaseMip : {0u, 1u}) {
          SCOPED_TRACE(testing::Message()
                       << "wave=" << WaveSize << " quads=" << QuadCount
                       << " vertical=" << Vertical << " base mip=" << BaseMip);
          std::vector<float> Inputs(16 * QuadCount);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane)
            Inputs[Lane * 4] = (Lane & (Vertical ? 2 : 1))
                                   ? 0.25f * float(1u << BaseMip)
                                   : 0.0f;
          std::vector<float> Outputs(24 * QuadCount + 2, -42.0f);
          std::vector<FemeFragmentInvocation> Invocations(QuadCount);
          std::vector<FemeFragmentResult> Results(QuadCount);
          for (auto &Invocation : Invocations) {
            Invocation.LiveMask = 0xf;
            Invocation.SideEffectMask = 0x5;
          }
          FragmentResources Resources;
          Resources.ImageHeap = Images;
          Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
          Resources.InputLayout = &InputLayout;
          Resources.Inputs = Inputs.data();
          Resources.OutputLayout = &OutputLayout;
          Resources.Outputs = Outputs.data() + 1;
          Resources.Invocations = Invocations;
          Resources.Results = Results;
          auto Prepared = PreparedFragmentBatch::create(
              (*Stage)->getResourceInfo(), Resources);
          for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              Inputs[Lane * 4 + 1] = float((Lane ^ (Lane >> 1) ^ Repeat) & 1);
              Inputs[Lane * 4 + 2] = float((Lane / 2 + Repeat) % 3) - 1.0f;
              Inputs[Lane * 4 + 3] = ((Lane / 2 + Repeat) & 1) ? 2.0f : 0.0f;
            }
            std::fill(Outputs.begin(), Outputs.end(), -42.0f);
            ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
            EXPECT_EQ(Outputs.front(), -42.0f);
            EXPECT_EQ(Outputs.back(), -42.0f);
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              unsigned BiasedMip =
                  std::clamp(int(BaseMip) + int(Inputs[Lane * 4 + 2]), 0, 2);
              unsigned ClampedMip =
                  std::max(BiasedMip, unsigned(Inputs[Lane * 4 + 3]));
              for (unsigned Out = 0; Out < 6; ++Out) {
                bool IsArray = Out == 2 || Out == 3 || Out == 5;
                unsigned Mip = Out >= 4               ? BaseMip
                               : Out == 1 || Out == 3 ? ClampedMip
                                                      : BiasedMip;
                int Size = 4 >> Mip;
                unsigned X = std::clamp(int(Inputs[Lane * 4] * Size) +
                                            (IsArray ? 1 : -1),
                                        0, Size - 1);
                unsigned Component = Out == 0   ? 3
                                     : Out == 1 ? 1
                                     : Out == 2 ? 2
                                                : 0;
                unsigned Layer = IsArray ? unsigned(Inputs[Lane * 4 + 1]) : 0;
                EXPECT_EQ(Outputs[1 + Lane * 6 + Out],
                          (Lane & 1) ? -42.0f
                                     : Color(Layer, Mip, X, Component));
              }
            }
            for (const auto &Result : Results) {
              EXPECT_EQ(Result.LiveMask, 0xfu);
              EXPECT_EQ(Result.SideEffectMask, 0x5u);
            }
          }
        }
  }
}

TEST(CompiledStageTest, FragmentCubeExplicitGradientsSelectFaceElementAndMip) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %xx = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %yx = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
      %zx = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
      %xy = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
      %yy = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 1, i32 0)
      %zy = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 2, i32 0)
      %layer = call float @feme.stage.input.load.f32(i32 3, i32 0, i32 0, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 3, i32 0, i32 1, i32 0)
      %d0 = insertelement <3 x float> poison, float %x, i32 0
      %d1 = insertelement <3 x float> %d0, float %y, i32 1
      %dir = insertelement <3 x float> %d1, float %z, i32 2
      %x0 = insertelement <3 x float> poison, float %xx, i32 0
      %x1 = insertelement <3 x float> %x0, float %yx, i32 1
      %dx = insertelement <3 x float> %x1, float %zx, i32 2
      %y0 = insertelement <3 x float> poison, float %xy, i32 0
      %y1 = insertelement <3 x float> %y0, float %yy, i32 1
      %dy = insertelement <3 x float> %y1, float %zy, i32 2
      %wide = shufflevector <3 x float> %dir, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>
      %coord = insertelement <4 x float> %wide, float %layer, i32 3
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
      %a = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 1)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %cp = call <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, <3 x float> %dx, <3 x float> %dy, i32 poison)
      %cc = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, <3 x float> %dx, <3 x float> %dy, i32 0, float %clamp)
      %ap = call <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> %coord, <3 x float> %dx, <3 x float> %dy, i32 undef)
      %ac = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> %coord, <3 x float> %dx, <3 x float> %dy, i32 poison, float %clamp)
      %cv = extractelement <4 x float> %cp, i32 3
      %ccv = extractelement <2 x float> %cc, i32 1
      %av = extractelement <3 x float> %ap, i32 2
      call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %cv, i32 0)
      call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %ccv, i32 0)
      call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float %av, i32 0)
      call void @feme.stage.output.store.f32(i32 4, i32 0, i32 3, float %ac, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32)
    declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32, float)
    declare <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <3 x float>, <3 x float>, i32)
    declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <3 x float>, <3 x float>, i32, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  auto Color = [](unsigned Face, unsigned Layer, unsigned Mip, unsigned C) {
    return float(10000 * Layer + 1000 * C + 100 * Face + 10 * Mip + 1);
  };
  std::vector<float> CubeTexels(504), ArrayTexels(1008);
  unsigned CubeOffset = 0, ArrayOffset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned FaceTexels = (4u >> Mip) * (4u >> Mip);
    for (unsigned Face = 0; Face < 12; ++Face)
      for (unsigned Texel = 0; Texel < FaceTexels; ++Texel)
        for (unsigned C = 0; C < 4; ++C) {
          float Value = Color(Face % 6, Face / 6, Mip, C);
          ArrayTexels[ArrayOffset + (Face * FaceTexels + Texel) * 4 + C] =
              Value;
          if (Face < 6)
            CubeTexels[CubeOffset + (Face * FaceTexels + Texel) * 4 + C] =
                Value;
        }
    CubeOffset += 6 * FaceTexels * 4;
    ArrayOffset += 12 * FaceTexels * 4;
  }
  FemeImageSubresourceLayout CubeMips[] = {
      {0, 64, 256, 0}, {1536, 32, 64, 0}, {1920, 16, 16, 0}};
  FemeImageSubresourceLayout ArrayMips[] = {
      {0, 64, 256, 0}, {3072, 32, 64, 0}, {3840, 16, 16, 0}};
  FemeImageDescriptor Images[2]{};
  Images[0].Data = CubeTexels.data();
  Images[0].SizeInBytes = CubeTexels.size() * sizeof(float);
  Images[0].Dimension = static_cast<uint32_t>(ImageDimension::TextureCube);
  Images[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Images[0].Width = Images[0].Height = 4;
  Images[0].Depth = Images[0].PlaneCount = Images[0].SampleCount = 1;
  Images[0].ArrayLayers = 6;
  Images[0].MipLevels = Images[0].MipLayoutCount = 3;
  Images[0].MipLayouts = CubeMips;
  Images[0].Flags = FEME_IMAGE_SAMPLED;
  Images[1] = Images[0];
  Images[1].Data = ArrayTexels.data();
  Images[1].SizeInBytes = ArrayTexels.size() * sizeof(float);
  Images[1].Dimension = static_cast<uint32_t>(ImageDimension::TextureCubeArray);
  Images[1].ArrayLayers = 12;
  Images[1].MipLayouts = ArrayMips;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  FemeStageElement InputElements[4]{}, OutputElements[5]{};
  for (unsigned ID = 0; ID < 5; ++ID) {
    auto Element = ID < 4 ? makeFloatInput(ID) : makeFloatOutput(ID);
    Element.ComponentCount = ID < 3 ? 3 : ID == 3 ? 2 : 4;
    Sig.Elements.push_back(Element);
    auto &Layout = ID < 4 ? InputElements[ID] : OutputElements[ID];
    Layout.ElementID = ID;
    Layout.ComponentCount = Element.ComponentCount;
    Layout.RowCount = 1;
    Layout.ComponentStride = 4;
    Layout.InvocationStride = ID < 4 ? 44 : 16;
    Layout.DataOffset = ID < 4 ? ID * 12 : 0;
  }
  FemeStageLayout InputLayout{}, OutputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 4;
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 5;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (unsigned Face = 0; Face < 6; ++Face)
        for (unsigned Axis = 0; Axis < 6; ++Axis) {
          SCOPED_TRACE(testing::Message()
                       << "wave=" << WaveSize << " quads=" << QuadCount
                       << " face=" << Face << " gradient component=" << Axis);
          std::vector<float> Inputs(44 * QuadCount);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane)
            Inputs[Lane * 11 + Face / 2] = Face & 1 ? -1.0f : 1.0f;
          std::vector<float> Outputs(16 * QuadCount + 2, -42.0f);
          std::vector<FemeFragmentInvocation> Invocations(QuadCount);
          std::vector<FemeFragmentResult> Results(QuadCount);
          for (auto &Invocation : Invocations) {
            Invocation.LiveMask = 0xf;
            Invocation.SideEffectMask = 0x5;
          }
          FragmentResources Resources;
          Resources.ImageHeap = Images;
          Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
          Resources.InputLayout = &InputLayout;
          Resources.Inputs = Inputs.data();
          Resources.OutputLayout = &OutputLayout;
          Resources.Outputs = Outputs.data() + 1;
          Resources.Invocations = Invocations;
          Resources.Results = Results;
          auto Prepared = PreparedFragmentBatch::create(
              (*Stage)->getResourceInfo(), Resources);
          for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              unsigned Mip = (Lane / 2 + Repeat) % 3;
              Inputs[Lane * 11 + 3 + Axis] =
                  (Axis < 3 ? 0.5f : -0.5f) * float(1u << Mip);
              Inputs[Lane * 11 + 9] = float((Lane ^ (Lane >> 1) ^ Repeat) & 1);
              Inputs[Lane * 11 + 10] = ((Lane / 2 + Repeat) & 1) ? 2.0f : 0.0f;
            }
            std::fill(Outputs.begin(), Outputs.end(), -42.0f);
            ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
            EXPECT_EQ(Outputs.front(), -42.0f);
            EXPECT_EQ(Outputs.back(), -42.0f);
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              unsigned Layer = unsigned(Inputs[Lane * 11 + 9]);
              // Cube LOD projects tangent components, not the major axis.
              unsigned Mip = Axis % 3 == Face / 2 ? 0 : (Lane / 2 + Repeat) % 3;
              unsigned ClampMip =
                  std::max(Mip, unsigned(Inputs[Lane * 11 + 10]));
              float Expected[] = {
                  Color(Face, 0, Mip, 3), Color(Face, 0, ClampMip, 1),
                  Color(Face, Layer, Mip, 2), Color(Face, Layer, ClampMip, 0)};
              for (unsigned C = 0; C < 4; ++C)
                EXPECT_EQ(Outputs[1 + Lane * 4 + C],
                          Lane & 1 ? -42.0f : Expected[C]);
            }
            for (const auto &Result : Results) {
              EXPECT_EQ(Result.LiveMask, 0xfu);
              EXPECT_EQ(Result.SideEffectMask, 0x5u);
            }
          }
        }
  }
}

TEST(CompiledStageTest, ComputeCubeExplicitGradientsSelectFaceElementAndMip) {
  constexpr char IR[] = R"(
    @directions = private constant [6 x <3 x float>] [<3 x float> <float 1.0, float 0.0, float 0.0>, <3 x float> <float -1.0, float 0.0, float 0.0>, <3 x float> <float 0.0, float 1.0, float 0.0>, <3 x float> <float 0.0, float -1.0, float 0.0>, <3 x float> <float 0.0, float 0.0, float 1.0>, <3 x float> <float 0.0, float 0.0, float -1.0>]
    define void @main() #0 {
      %t = call target("dx.Texture", float, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_5t(i32 0)
      %a = call target("dx.Texture", float, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_9t(i32 1)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 0)
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %face = urem i32 %tid, 6
      %ptr = getelementptr [6 x <3 x float>], ptr @directions, i32 0, i32 %face
      %dir = load <3 x float>, ptr %ptr
      %layeri = udiv i32 %tid, 6
      %layer0 = and i32 %layeri, 1
      %layer = uitofp i32 %layer0 to float
      %wide = shufflevector <3 x float> %dir, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>
      %coord = insertelement <4 x float> %wide, float %layer, i32 3
      %bit = and i32 %tid, 1
      %odd = icmp ne i32 %bit, 0
      %g = select i1 %odd, float 2.0, float 0.5
      %clamp = select i1 %odd, float 0.0, float 2.0
      %major = udiv i32 %face, 2
      %next = add i32 %major, 1
      %axis = urem i32 %next, 3
      %dx = insertelement <3 x float> zeroinitializer, float %g, i32 %axis
      %c = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, <3 x float> %dx, <3 x float> zeroinitializer, i32 poison)
      %v = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> %coord, <3 x float> %dx, <3 x float> zeroinitializer, i32 0, float %clamp)
      %ci = fptoui float %c to i32
      %vi = fptoui float %v to i32
      %base0 = mul i32 %tid, 8
      %base = add i32 %base0, 4
      %last = add i32 %base, 4
      call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %ci)
      call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %last, i32 poison, i32 %vi)
      ret void
    }
    declare target("dx.Texture", float, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_5t(i32)
    declare target("dx.Texture", float, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_9t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
    declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32)
    declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <3 x float>, <3 x float>, i32, float)
    declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="13,1,1" }
  )";
  std::vector<float> CubeTexels(126), ArrayTexels(252);
  unsigned CubeOffset = 0, ArrayOffset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned Count = (4u >> Mip) * (4u >> Mip);
    for (unsigned Face = 0; Face < 12; ++Face) {
      float Value = float(1000 * (Face / 6) + 100 * (Face % 6) + 10 * Mip + 1);
      std::fill_n(ArrayTexels.data() + ArrayOffset + Face * Count, Count,
                  Value);
      if (Face < 6)
        std::fill_n(CubeTexels.data() + CubeOffset + Face * Count, Count,
                    Value);
    }
    CubeOffset += 6 * Count;
    ArrayOffset += 12 * Count;
  }
  FemeImageSubresourceLayout CubeMips[] = {
      {0, 16, 64, 0}, {384, 8, 16, 0}, {480, 4, 4, 0}};
  FemeImageSubresourceLayout ArrayMips[] = {
      {0, 16, 64, 0}, {768, 8, 16, 0}, {960, 4, 4, 0}};
  FemeImageDescriptor Images[2]{};
  Images[0].Data = CubeTexels.data();
  Images[0].SizeInBytes = CubeTexels.size() * sizeof(float);
  Images[0].Dimension = static_cast<uint32_t>(ImageDimension::TextureCube);
  Images[0].Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Images[0].Width = Images[0].Height = 4;
  Images[0].Depth = Images[0].PlaneCount = Images[0].SampleCount = 1;
  Images[0].ArrayLayers = 6;
  Images[0].MipLevels = Images[0].MipLayoutCount = 3;
  Images[0].MipLayouts = CubeMips;
  Images[0].Flags = FEME_IMAGE_SAMPLED;
  Images[1] = Images[0];
  Images[1].Data = ArrayTexels.data();
  Images[1].SizeInBytes = ArrayTexels.size() * sizeof(float);
  Images[1].Dimension = static_cast<uint32_t>(ImageDimension::TextureCubeArray);
  Images[1].ArrayLayers = 12;
  Images[1].MipLayouts = ArrayMips;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  for (bool Reference : {false, true})
    for (unsigned WaveSize : {4u, 8u}) {
      SCOPED_TRACE(testing::Message()
                   << "reference=" << Reference << " wave=" << WaveSize);
      Context Ctx;
      SMDiagnostic Err;
      auto M = parseAssemblyString(IR, Err, Ctx.getLLVMContext());
      ASSERT_TRUE(M) << Err.getMessage().str();
      JITOptions Opts;
      Opts.WaveSize = WaveSize;
      Opts.Reference = Reference;
      auto Stage = CompiledStage::create(
          Ctx, feme::Module::fromLLVMIR(std::move(M)), Opts);
      ASSERT_THAT_EXPECTED(Stage, Succeeded());
      EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
      std::vector<uint32_t> Buffer(28, 42);
      FemeDescriptor Desc{};
      Desc.Data = Buffer.data();
      Desc.SizeInBytes = Buffer.size() * sizeof(uint32_t);
      Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
      Desc.Flags = FEME_DESCRIPTOR_UAV;
      DispatchResources Resources;
      Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
      Resources.ImageHeap = Images;
      Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
      auto Prepared = PreparedDispatch::create((*Stage)->getResourceInfo(),
                                               Resources, {1, 1, 1});
      for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
        std::fill(Buffer.begin(), Buffer.end(), 42);
        ASSERT_THAT_ERROR((*Stage)->invokeGroup(Prepared, {0, 0, 0}, {}),
                          Succeeded());
        EXPECT_EQ(Buffer.front(), 42u);
        EXPECT_EQ(Buffer.back(), 42u);
        for (unsigned Lane = 0; Lane < 13; ++Lane) {
          unsigned Face = Lane % 6, Layer = (Lane / 6) & 1;
          EXPECT_EQ(Buffer[1 + Lane * 2], 100 * Face + (Lane & 1 ? 21u : 1u));
          EXPECT_EQ(Buffer[2 + Lane * 2], 1000 * Layer + 100 * Face + 21u);
        }
      }
    }
}

TEST(CompiledStageTest, FragmentCubeBiasedSamplingSelectsFaceElementAndMip) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %layer = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %bias = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
      %d0 = insertelement <3 x float> poison, float %x, i32 0
      %d1 = insertelement <3 x float> %d0, float %y, i32 1
      %dir = insertelement <3 x float> %d1, float %z, i32 2
      %a0 = insertelement <4 x float> poison, float %x, i32 0
      %a1 = insertelement <4 x float> %a0, float %y, i32 1
      %a2 = insertelement <4 x float> %a1, float %z, i32 2
      %coord = insertelement <4 x float> %a2, float %layer, i32 3
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
      %a = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 1)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %cb = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, float %bias, i32 poison)
      %cc = call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, float %bias, i32 0, float %clamp)
      %ab = call <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> %coord, float %bias, i32 poison)
      %ac = call float @llvm.dx.resource.samplebias.clamp.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> %coord, float %bias, i32 0, float %clamp)
      %cbase = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, float 0.0, i32 poison)
      %abase = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> %coord, float 0.0, i32 poison)
      %cbv = extractelement <4 x float> %cb, i32 3
      %ccv = extractelement <2 x float> %cc, i32 1
      %abv = extractelement <3 x float> %ab, i32 2
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %cbv, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %ccv, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %abv, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float %ac, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %cbase, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %abase, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32)
    declare <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32, float)
    declare <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32)
    declare float @llvm.dx.resource.samplebias.clamp.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32, float)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  auto Color = [](unsigned Face, unsigned Layer, unsigned Mip, unsigned C) {
    return float(10000 * Layer + 1000 * C + 100 * Face +
                 10 * (Layer ? 2 - Mip : Mip) + 1);
  };
  std::vector<float> CubeTexels(504);
  std::vector<float> ArrayTexels(1008);
  unsigned CubeOffset = 0, ArrayOffset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned FaceTexels = (4u >> Mip) * (4u >> Mip);
    for (unsigned Face = 0; Face < 12; ++Face)
      for (unsigned Texel = 0; Texel < FaceTexels; ++Texel)
        for (unsigned C = 0; C < 4; ++C) {
          float Value = Color(Face % 6, Face / 6, Mip, C);
          ArrayTexels[ArrayOffset + (Face * FaceTexels + Texel) * 4 + C] =
              Value;
          if (Face < 6)
            CubeTexels[CubeOffset + (Face * FaceTexels + Texel) * 4 + C] =
                Value;
        }
    CubeOffset += 6 * FaceTexels * 4;
    ArrayOffset += 12 * FaceTexels * 4;
  }
  FemeImageSubresourceLayout CubeMips[] = {
      {0, 64, 256, 0}, {1536, 32, 64, 0}, {1920, 16, 16, 0}};
  FemeImageSubresourceLayout ArrayMips[] = {
      {0, 64, 256, 0}, {3072, 32, 64, 0}, {3840, 16, 16, 0}};
  FemeImageDescriptor Images[2]{};
  Images[0].Data = CubeTexels.data();
  Images[0].SizeInBytes = CubeTexels.size() * sizeof(float);
  Images[0].Dimension = static_cast<uint32_t>(ImageDimension::TextureCube);
  Images[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Images[0].Width = Images[0].Height = 4;
  Images[0].Depth = Images[0].PlaneCount = Images[0].SampleCount = 1;
  Images[0].ArrayLayers = 6;
  Images[0].MipLevels = Images[0].MipLayoutCount = 3;
  Images[0].MipLayouts = CubeMips;
  Images[0].Flags = FEME_IMAGE_SAMPLED;
  Images[1] = Images[0];
  Images[1].Data = ArrayTexels.data();
  Images[1].SizeInBytes = ArrayTexels.size() * sizeof(float);
  Images[1].Dimension = static_cast<uint32_t>(ImageDimension::TextureCubeArray);
  Images[1].ArrayLayers = 12;
  Images[1].MipLayouts = ArrayMips;
  FemeSamplerDescriptor Sampler{};
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  FemeStageElement InputElements[2]{};
  FemeStageElement OutputElements[4]{};
  for (unsigned ID = 0; ID < 4; ++ID) {
    auto Element = ID < 2 ? makeFloatInput(ID) : makeFloatOutput(ID);
    Element.ComponentCount = ID < 2 ? 3 : ID == 2 ? 4 : 2;
    Sig.Elements.push_back(Element);
    FemeStageElement &LayoutElement =
        ID < 2 ? InputElements[ID] : OutputElements[ID];
    LayoutElement.ElementID = ID;
    LayoutElement.ComponentCount = Element.ComponentCount;
    LayoutElement.RowCount = 1;
    LayoutElement.ComponentStride = 4;
    LayoutElement.InvocationStride = 24;
    LayoutElement.DataOffset = ID < 2 ? ID * 12 : (ID - 2) * 16;
  }
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 2;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (unsigned Face = 0; Face < 6; ++Face)
        for (bool Vertical : {false, true}) {
          SCOPED_TRACE(testing::Message()
                       << "wave=" << WaveSize << " quads=" << QuadCount
                       << " face=" << Face << " vertical=" << Vertical);
          std::vector<float> Inputs(24 * QuadCount);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            Inputs[Lane * 6 + Face / 2] = (Face & 1) ? -1.0f : 1.0f;
            Inputs[Lane * 6 + (Face / 2 + 1) % 3] =
                (Lane & (Vertical ? 2 : 1)) ? 0.5f : -0.5f;
          }
          std::vector<float> Outputs(24 * QuadCount + 2, -42.0f);
          std::vector<FemeFragmentInvocation> Invocations(QuadCount);
          std::vector<FemeFragmentResult> Results(QuadCount);
          for (auto &Invocation : Invocations) {
            Invocation.LiveMask = 0xf;
            Invocation.SideEffectMask = 0x5;
          }
          FragmentResources Resources;
          Resources.ImageHeap = Images;
          Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
          Resources.InputLayout = &InputLayout;
          Resources.Inputs = Inputs.data();
          Resources.OutputLayout = &OutputLayout;
          Resources.Outputs = Outputs.data() + 1;
          Resources.Invocations = Invocations;
          Resources.Results = Results;
          auto Prepared = PreparedFragmentBatch::create(
              (*Stage)->getResourceInfo(), Resources);
          for (unsigned Repeat = 0; Repeat < 3; ++Repeat) {
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              Inputs[Lane * 6 + 3] = float((Lane ^ (Lane >> 1) ^ Repeat) & 1);
              Inputs[Lane * 6 + 4] = float((Lane / 2 + Repeat) % 3) - 1.0f;
              Inputs[Lane * 6 + 5] = ((Lane / 2 + Repeat) & 1) ? 2.0f : 0.0f;
            }
            std::fill(Outputs.begin(), Outputs.end(), -42.0f);
            ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
            EXPECT_EQ(Outputs.front(), -42.0f);
            EXPECT_EQ(Outputs.back(), -42.0f);
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              unsigned Layer = unsigned(Inputs[Lane * 6 + 3]);
              unsigned Mip = unsigned(1.0f + Inputs[Lane * 6 + 4]);
              unsigned ClampMip = std::max(Mip, unsigned(Inputs[Lane * 6 + 5]));
              float Expected[] = {
                  Color(Face, 0, Mip, 3),     Color(Face, 0, ClampMip, 1),
                  Color(Face, Layer, Mip, 2), Color(Face, Layer, ClampMip, 0),
                  Color(Face, 0, 1, 0),       Color(Face, Layer, 1, 0)};
              for (unsigned C = 0; C < 6; ++C)
                EXPECT_EQ(Outputs[1 + Lane * 6 + C],
                          (Lane & 1) ? -42.0f : Expected[C]);
            }
            for (const auto &Result : Results) {
              EXPECT_EQ(Result.LiveMask, 0xfu);
              EXPECT_EQ(Result.SideEffectMask, 0x5u);
            }
          }
        }
  }
}

TEST(CompiledStageTest, FragmentComparisonUsesDerivativesAndMinLodClamp) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %c0 = insertelement <2 x float> poison, float %u, i32 0
      %coord = insertelement <2 x float> %c0, float 0.5, i32 1
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 0)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %implicit = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float 0.5, <2 x i32> zeroinitializer)
      %explicit = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float 0.5, <2 x i32> zeroinitializer)
      %clamped = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float 0.5, <2 x i32> zeroinitializer, float 2.0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %implicit, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %explicit, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %clamped, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32)
    declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
    declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  // A half-texture step between horizontal quad lanes selects mip 1.
  // Distinct depths make implicit LOD, explicit zero and shader clamp differ.
  std::vector<float> Texels(21, 0.25f);
  std::fill(Texels.begin() + 16, Texels.begin() + 20, 0.75f);
  FemeImageSubresourceLayout Mips[] = {
      {0, 16, 64, 0}, {64, 8, 16, 0}, {80, 4, 4, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels.data();
  Image.SizeInBytes = Texels.size() * sizeof(float);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Image.Width = Image.Height = 4;
  Image.Depth = Image.ArrayLayers = Image.PlaneCount = Image.SampleCount = 1;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.Flags = FEME_SAMPLER_COMPARE_ENABLE;
  Sampler.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::Less);
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  auto Output = makeFloatOutput(1);
  Output.ComponentCount = 3;
  Sig.Elements = {makeFloatInput(0), Output};
  FemeStageElement InputElement{};
  InputElement.ComponentCount = InputElement.RowCount = 1;
  InputElement.InvocationStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = &InputElement;
  InputLayout.ElementCount = 1;
  FemeStageElement OutputElements[2]{};
  OutputElements[1].ElementID = 1;
  OutputElements[1].ComponentCount = 3;
  OutputElements[1].RowCount = 1;
  OutputElements[1].InvocationStride = 12;
  OutputElements[1].ComponentStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 2;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u}) {
      SCOPED_TRACE(testing::Message()
                   << "wave=" << WaveSize << " quads=" << QuadCount);
      std::vector<float> Inputs(4 * QuadCount);
      for (unsigned Lane = 0; Lane < Inputs.size(); ++Lane)
        Inputs[Lane] = (Lane & 1) ? 0.5f : 0.0f;
      std::vector<float> Outputs(12 * QuadCount + 2, -42.0f);
      std::vector<FemeFragmentInvocation> Invocations(QuadCount);
      std::vector<FemeFragmentResult> Results(QuadCount);
      for (auto &Invocation : Invocations) {
        Invocation.LiveMask = 0xf;
        Invocation.SideEffectMask = 0x5;
      }
      FragmentResources Resources;
      Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
      Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
      Resources.InputLayout = &InputLayout;
      Resources.Inputs = Inputs.data();
      Resources.OutputLayout = &OutputLayout;
      Resources.Outputs = Outputs.data() + 1;
      Resources.Invocations = Invocations;
      Resources.Results = Results;
      auto Prepared =
          PreparedFragmentBatch::create((*Stage)->getResourceInfo(), Resources);
      for (unsigned Repeat = 0; Repeat < 2; ++Repeat) {
        ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
        EXPECT_EQ(Outputs.front(), -42.0f);
        EXPECT_EQ(Outputs.back(), -42.0f);
        for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
          EXPECT_EQ(Outputs[1 + Lane * 3], (Lane & 1) ? -42.0f : 1.0f);
          EXPECT_EQ(Outputs[2 + Lane * 3], (Lane & 1) ? -42.0f : 0.0f);
          EXPECT_EQ(Outputs[3 + Lane * 3], (Lane & 1) ? -42.0f : 0.0f);
        }
        for (const auto &Result : Results) {
          EXPECT_EQ(Result.LiveMask, 0xfu);
          EXPECT_EQ(Result.SideEffectMask, 0x5u);
        }
      }
    }
  }
}

TEST(CompiledStageTest, FragmentArrayComparisonSelectsLayerAndMip) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %layer = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %ref = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
      %c0 = insertelement <3 x float> poison, float %u, i32 0
      %c1 = insertelement <3 x float> %c0, float 0.5, i32 1
      %coord = insertelement <3 x float> %c1, float %layer, i32 2
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 0)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %implicit = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float %ref, <2 x i32> zeroinitializer)
      %explicit = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float %ref, <2 x i32> zeroinitializer)
      %clamped = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float %ref, <2 x i32> zeroinitializer, float %clamp)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %implicit, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %explicit, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %clamped, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32)
    declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <3 x float>, float, <2 x i32>)
    declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <3 x float>, float, <2 x i32>)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <3 x float>, float, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  // Mip-major storage, with complementary layer depths at each mip.
  std::vector<float> Texels(42, 0.25f);
  std::fill(Texels.begin() + 16, Texels.begin() + 36, 0.75f);
  Texels[41] = 0.75f;
  FemeImageSubresourceLayout Mips[] = {
      {0, 16, 64, 0}, {128, 8, 16, 0}, {160, 4, 4, 0}};
  FemeImageDescriptor Image{};
  Image.Data = Texels.data();
  Image.SizeInBytes = Texels.size() * sizeof(float);
  Image.Dimension = static_cast<uint32_t>(ImageDimension::Texture2DArray);
  Image.Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Image.Width = Image.Height = 4;
  Image.Depth = Image.PlaneCount = Image.SampleCount = 1;
  Image.ArrayLayers = 2;
  Image.MipLevels = Image.MipLayoutCount = 3;
  Image.MipLayouts = Mips;
  Image.Flags = FEME_IMAGE_SAMPLED;
  FemeSamplerDescriptor Sampler{};
  Sampler.Flags = FEME_SAMPLER_COMPARE_ENABLE;
  Sampler.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::Less);
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  auto Input = makeFloatInput(0);
  auto Output = makeFloatOutput(1);
  Input.ComponentCount = 4;
  Output.ComponentCount = 3;
  EntrySignature Sig;
  Sig.Elements = {Input, Output};
  FemeStageElement InputElement{};
  InputElement.ComponentCount = 4;
  InputElement.RowCount = 1;
  InputElement.InvocationStride = 16;
  InputElement.ComponentStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = &InputElement;
  InputLayout.ElementCount = 1;
  FemeStageElement OutputElements[2]{};
  OutputElements[1].ElementID = 1;
  OutputElements[1].ComponentCount = 3;
  OutputElements[1].RowCount = 1;
  OutputElements[1].InvocationStride = 12;
  OutputElements[1].ComponentStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 2;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u}) {
      SCOPED_TRACE(testing::Message()
                   << "wave=" << WaveSize << " quads=" << QuadCount);
      std::vector<float> Inputs(16 * QuadCount);
      for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
        Inputs[Lane * 4] = (Lane & 1) ? 0.5f : 0.0f;
        Inputs[Lane * 4 + 1] = ((Lane ^ (Lane >> 1)) & 1) ? 1.0f : 0.0f;
        Inputs[Lane * 4 + 2] = Lane / 4 == 1   ? 0.125f
                               : Lane / 4 == 2 ? 0.875f
                                               : 0.5f;
        Inputs[Lane * 4 + 3] = (Lane & 2) ? 0.0f : 2.0f;
      }
      std::vector<float> Outputs(12 * QuadCount + 2, -42.0f);
      std::vector<FemeFragmentInvocation> Invocations(QuadCount);
      std::vector<FemeFragmentResult> Results(QuadCount);
      for (auto &Invocation : Invocations) {
        Invocation.LiveMask = 0xf;
        Invocation.SideEffectMask = 0x5;
      }
      FragmentResources Resources;
      Resources.ImageHeap = ArrayRef<FemeImageDescriptor>(&Image, 1);
      Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
      Resources.InputLayout = &InputLayout;
      Resources.Inputs = Inputs.data();
      Resources.OutputLayout = &OutputLayout;
      Resources.Outputs = Outputs.data() + 1;
      Resources.Invocations = Invocations;
      Resources.Results = Results;
      auto Prepared =
          PreparedFragmentBatch::create((*Stage)->getResourceInfo(), Resources);
      for (unsigned Repeat = 0; Repeat < 2; ++Repeat) {
        for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane)
          Inputs[Lane * 4 + 3] = ((Lane + Repeat * 2) & 2) ? 0.0f : 2.0f;
        std::fill(Outputs.begin(), Outputs.end(), -42.0f);
        ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
        EXPECT_EQ(Outputs.front(), -42.0f);
        EXPECT_EQ(Outputs.back(), -42.0f);
        for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
          float Layer = Inputs[Lane * 4 + 1];
          float Ref = Inputs[Lane * 4 + 2];
          float Implicit = Ref < (Layer == 0 ? 0.75f : 0.25f);
          float Explicit = Ref < (Layer == 0 ? 0.25f : 0.75f);
          float Clamped = Inputs[Lane * 4 + 3] == 2.0f ? Explicit : Implicit;
          EXPECT_EQ(Outputs[1 + Lane * 3], (Lane & 1) ? -42.0f : Implicit);
          EXPECT_EQ(Outputs[2 + Lane * 3], (Lane & 1) ? -42.0f : Explicit);
          EXPECT_EQ(Outputs[3 + Lane * 3], (Lane & 1) ? -42.0f : Clamped);
        }
        for (const auto &Result : Results) {
          EXPECT_EQ(Result.LiveMask, 0xfu);
          EXPECT_EQ(Result.SideEffectMask, 0x5u);
        }
      }
    }
  }
}

TEST(CompiledStageTest, Fragment1DComparisonsUseSpatialDerivativesAndClamp) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %layer = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %ref = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
      %c0 = insertelement <2 x float> poison, float %u, i32 0
      %coord = insertelement <2 x float> %c0, float %layer, i32 1
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 0)
      %a = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32 1)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %implicit = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float %u, float %ref, i32 -1)
      %explicit = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float %u, float %ref, i32 -1)
      %clamped = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float %u, float %ref, i32 -1, float %clamp)
      %array = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %a, target("dx.Sampler", 1) %s, <2 x float> %coord, float %ref, i32 -1)
      %arrayzero = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %a, target("dx.Sampler", 1) %s, <2 x float> %coord, float %ref, i32 -1)
      %arrayclamp = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %a, target("dx.Sampler", 1) %s, <2 x float> %coord, float %ref, i32 -1, float %clamp)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %implicit, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %explicit, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %clamped, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %array, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %arrayzero, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %arrayclamp, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32)
    declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32)
    declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32, float)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, i32)
    declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, i32)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, i32, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  float PlainTexels[] = {0.25f, 0.25f, 0.25f, 0.25f, 0.75f, 0.75f, 0.25f};
  float ArrayTexels[] = {0.25f, 0.25f, 0.25f, 0.25f, 0.75f, 0.75f, 0.75f,
                         0.75f, 0.75f, 0.75f, 0.25f, 0.25f, 0.25f, 0.75f};
  FemeImageSubresourceLayout PlainMips[] = {
      {0, 16, 16, 0}, {16, 8, 8, 0}, {24, 4, 4, 0}};
  FemeImageSubresourceLayout ArrayMips[] = {
      {0, 16, 16, 0}, {32, 8, 8, 0}, {48, 4, 4, 0}};
  FemeImageDescriptor Images[2]{};
  Images[0].Data = PlainTexels;
  Images[0].SizeInBytes = sizeof(PlainTexels);
  Images[0].Dimension = static_cast<uint32_t>(ImageDimension::Texture1D);
  Images[0].Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Images[0].Width = 4;
  Images[0].Height = Images[0].Depth = Images[0].ArrayLayers =
      Images[0].PlaneCount = Images[0].SampleCount = 1;
  Images[0].MipLevels = Images[0].MipLayoutCount = 3;
  Images[0].MipLayouts = PlainMips;
  Images[0].Flags = FEME_IMAGE_SAMPLED;
  Images[1] = Images[0];
  Images[1].Data = ArrayTexels;
  Images[1].SizeInBytes = sizeof(ArrayTexels);
  Images[1].Dimension = static_cast<uint32_t>(ImageDimension::Texture1DArray);
  Images[1].ArrayLayers = 2;
  Images[1].MipLayouts = ArrayMips;
  FemeSamplerDescriptor Sampler{};
  Sampler.Flags = FEME_SAMPLER_COMPARE_ENABLE;
  Sampler.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::Less);
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  auto Input = makeFloatInput(0);
  auto PlainOutput = makeFloatOutput(1);
  auto ArrayOutput = makeFloatOutput(2);
  Input.ComponentCount = 4;
  PlainOutput.ComponentCount = ArrayOutput.ComponentCount = 3;
  EntrySignature Sig;
  Sig.Elements = {Input, PlainOutput, ArrayOutput};
  FemeStageElement InputElement{};
  InputElement.ComponentCount = 4;
  InputElement.RowCount = 1;
  InputElement.InvocationStride = 16;
  InputElement.ComponentStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = &InputElement;
  InputLayout.ElementCount = 1;
  FemeStageElement OutputElements[3]{};
  for (unsigned ID : {1u, 2u}) {
    OutputElements[ID].ElementID = ID;
    OutputElements[ID].ComponentCount = 3;
    OutputElements[ID].RowCount = 1;
    OutputElements[ID].InvocationStride = 24;
    OutputElements[ID].ComponentStride = 4;
    OutputElements[ID].DataOffset = (ID - 1) * 12;
  }
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 3;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (bool Vertical : {false, true}) {
        SCOPED_TRACE(testing::Message()
                     << "wave=" << WaveSize << " quads=" << QuadCount
                     << " vertical=" << Vertical);
        std::vector<float> Inputs(16 * QuadCount);
        for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
          Inputs[Lane * 4] = (Lane & (Vertical ? 2 : 1)) ? 0.5f : 0.0f;
          Inputs[Lane * 4 + 1] = ((Lane ^ (Lane >> 1)) & 1) ? 1.0f : 0.0f;
          Inputs[Lane * 4 + 2] = Lane / 4 == 1   ? 0.125f
                                 : Lane / 4 == 2 ? 0.875f
                                                 : 0.5f;
        }
        std::vector<float> Outputs(24 * QuadCount + 2, -42.0f);
        std::vector<FemeFragmentInvocation> Invocations(QuadCount);
        std::vector<FemeFragmentResult> Results(QuadCount);
        for (auto &Invocation : Invocations) {
          Invocation.LiveMask = 0xf;
          Invocation.SideEffectMask = 0x5;
        }
        FragmentResources Resources;
        Resources.ImageHeap = Images;
        Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
        Resources.InputLayout = &InputLayout;
        Resources.Inputs = Inputs.data();
        Resources.OutputLayout = &OutputLayout;
        Resources.Outputs = Outputs.data() + 1;
        Resources.Invocations = Invocations;
        Resources.Results = Results;
        auto Prepared = PreparedFragmentBatch::create(
            (*Stage)->getResourceInfo(), Resources);
        for (unsigned Repeat = 0; Repeat < 2; ++Repeat) {
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane)
            Inputs[Lane * 4 + 3] = ((Lane + Repeat * 2) & 2) ? 0.0f : 2.0f;
          std::fill(Outputs.begin(), Outputs.end(), -42.0f);
          ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
          EXPECT_EQ(Outputs.front(), -42.0f);
          EXPECT_EQ(Outputs.back(), -42.0f);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            float Ref = Inputs[Lane * 4 + 2];
            bool Layer = Inputs[Lane * 4 + 1] != 0;
            bool Clamped = Inputs[Lane * 4 + 3] == 2.0f;
            bool Expected[] = {Ref < 0.75f,
                               Ref < 0.25f,
                               Ref < (Clamped ? 0.25f : 0.75f),
                               Ref < (Layer ? 0.25f : 0.75f),
                               Ref < (Layer ? 0.75f : 0.25f),
                               Ref < ((Layer != Clamped) ? 0.25f : 0.75f)};
            for (unsigned Component = 0; Component < 6; ++Component)
              EXPECT_EQ(Outputs[1 + Lane * 6 + Component],
                        (Lane & 1) ? -42.0f : Expected[Component]);
          }
          for (const auto &Result : Results) {
            EXPECT_EQ(Result.LiveMask, 0xfu);
            EXPECT_EQ(Result.SideEffectMask, 0x5u);
          }
        }
      }
  }
}

TEST(CompiledStageTest, FragmentCubeComparisonsSelectFaceElementAndMip) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      %layer = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %ref = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
      %clamp = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
      %d0 = insertelement <3 x float> poison, float %x, i32 0
      %d1 = insertelement <3 x float> %d0, float %y, i32 1
      %dir = insertelement <3 x float> %d1, float %z, i32 2
      %a0 = insertelement <4 x float> poison, float %x, i32 0
      %a1 = insertelement <4 x float> %a0, float %y, i32 1
      %a2 = insertelement <4 x float> %a1, float %z, i32 2
      %coord = insertelement <4 x float> %a2, float %layer, i32 3
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32 0)
      %a = call target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32 1)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %implicit = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> %dir, float %ref, i32 poison)
      %explicit = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> %dir, float %ref, i32 poison)
      %clamped = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> %dir, float %ref, i32 poison, float %clamp)
      %array = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %a, target("dx.Sampler", 1) %s, <4 x float> %coord, float %ref, i32 poison)
      %arrayzero = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %a, target("dx.Sampler", 1) %s, <4 x float> %coord, float %ref, i32 poison)
      %arrayclamp = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %a, target("dx.Sampler", 1) %s, <4 x float> %coord, float %ref, i32 poison, float %clamp)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %implicit, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %explicit, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %clamped, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %array, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %arrayzero, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %arrayclamp, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32)
    declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5), target("dx.Sampler", 1), <3 x float>, float, i32)
    declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5), target("dx.Sampler", 1), <3 x float>, float, i32)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5), target("dx.Sampler", 1), <3 x float>, float, i32, float)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32)
    declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  std::vector<float> CubeTexels(126);
  std::vector<float> ArrayTexels(252);
  unsigned CubeOffset = 0, ArrayOffset = 0;
  for (unsigned Mip = 0; Mip < 3; ++Mip) {
    unsigned FaceTexels = (4u >> Mip) * (4u >> Mip);
    for (unsigned Face = 0; Face < 12; ++Face) {
      float Base = float(Face % 6 + 1) / 8.0f;
      float Depth = Base + (((Mip == 1) != (Face >= 6)) ? 0.03125f : -0.03125f);
      std::fill_n(ArrayTexels.begin() + ArrayOffset + Face * FaceTexels,
                  FaceTexels, Depth);
      if (Face < 6)
        std::fill_n(CubeTexels.begin() + CubeOffset + Face * FaceTexels,
                    FaceTexels, Depth);
    }
    CubeOffset += 6 * FaceTexels;
    ArrayOffset += 12 * FaceTexels;
  }
  FemeImageSubresourceLayout CubeMips[] = {
      {0, 16, 64, 0}, {384, 8, 16, 0}, {480, 4, 4, 0}};
  FemeImageSubresourceLayout ArrayMips[] = {
      {0, 16, 64, 0}, {768, 8, 16, 0}, {960, 4, 4, 0}};
  FemeImageDescriptor Images[2]{};
  Images[0].Data = CubeTexels.data();
  Images[0].SizeInBytes = CubeTexels.size() * sizeof(float);
  Images[0].Dimension = static_cast<uint32_t>(ImageDimension::TextureCube);
  Images[0].Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Images[0].Width = Images[0].Height = 4;
  Images[0].Depth = Images[0].PlaneCount = Images[0].SampleCount = 1;
  Images[0].ArrayLayers = 6;
  Images[0].MipLevels = Images[0].MipLayoutCount = 3;
  Images[0].MipLayouts = CubeMips;
  Images[0].Flags = FEME_IMAGE_SAMPLED;
  Images[1] = Images[0];
  Images[1].Data = ArrayTexels.data();
  Images[1].SizeInBytes = ArrayTexels.size() * sizeof(float);
  Images[1].Dimension = static_cast<uint32_t>(ImageDimension::TextureCubeArray);
  Images[1].ArrayLayers = 12;
  Images[1].MipLayouts = ArrayMips;
  FemeSamplerDescriptor Sampler{};
  Sampler.Flags = FEME_SAMPLER_COMPARE_ENABLE;
  Sampler.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::Less);
  Sampler.MaxLod = 2.0f;
  Sampler.AddressU = Sampler.AddressV =
      static_cast<uint32_t>(SamplerAddressMode::ClampToEdge);

  EntrySignature Sig;
  for (unsigned ID = 0; ID < 4; ++ID) {
    auto Element = ID < 2 ? makeFloatInput(ID) : makeFloatOutput(ID);
    Element.ComponentCount = 3;
    Sig.Elements.push_back(Element);
  }
  FemeStageElement InputElements[2]{};
  FemeStageElement OutputElements[4]{};
  for (unsigned I = 0; I < 2; ++I) {
    InputElements[I].ElementID = I;
    InputElements[I].ComponentCount = 3;
    InputElements[I].RowCount = 1;
    InputElements[I].InvocationStride = 24;
    InputElements[I].ComponentStride = 4;
    InputElements[I].DataOffset = I * 12;
    OutputElements[I + 2] = InputElements[I];
    OutputElements[I + 2].ElementID = I + 2;
  }
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 2;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;
  for (unsigned WaveSize : {4u, 8u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    EXPECT_TRUE((*Stage)->getResourceInfo().UsesSamplerHeap);
    for (unsigned QuadCount : {1u, 3u})
      for (unsigned Face = 0; Face < 6; ++Face)
        for (bool Vertical : {false, true}) {
          SCOPED_TRACE(testing::Message()
                       << "wave=" << WaveSize << " quads=" << QuadCount
                       << " face=" << Face << " vertical=" << Vertical);
          float Base = float(Face + 1) / 8.0f;
          std::vector<float> Inputs(24 * QuadCount);
          for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
            Inputs[Lane * 6 + Face / 2] = (Face & 1) ? -1.0f : 1.0f;
            Inputs[Lane * 6 + (Face / 2 + 1) % 3] =
                (Lane & (Vertical ? 2 : 1)) ? 0.5f : -0.5f;
            Inputs[Lane * 6 + 3] = ((Lane ^ (Lane >> 1)) & 1) ? 1.0f : 0.0f;
            Inputs[Lane * 6 + 4] = Base + (Lane / 4 == 1   ? -0.0625f
                                           : Lane / 4 == 2 ? 0.0625f
                                                           : 0.0f);
          }
          std::vector<float> Outputs(24 * QuadCount + 2, -42.0f);
          std::vector<FemeFragmentInvocation> Invocations(QuadCount);
          std::vector<FemeFragmentResult> Results(QuadCount);
          for (auto &Invocation : Invocations) {
            Invocation.LiveMask = 0xf;
            Invocation.SideEffectMask = 0x5;
          }
          FragmentResources Resources;
          Resources.ImageHeap = Images;
          Resources.SamplerHeap = ArrayRef<FemeSamplerDescriptor>(&Sampler, 1);
          Resources.InputLayout = &InputLayout;
          Resources.Inputs = Inputs.data();
          Resources.OutputLayout = &OutputLayout;
          Resources.Outputs = Outputs.data() + 1;
          Resources.Invocations = Invocations;
          Resources.Results = Results;
          auto Prepared = PreparedFragmentBatch::create(
              (*Stage)->getResourceInfo(), Resources);
          for (unsigned Repeat = 0; Repeat < 2; ++Repeat) {
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane)
              Inputs[Lane * 6 + 5] = ((Lane + Repeat * 2) & 2) ? 0.0f : 2.0f;
            std::fill(Outputs.begin(), Outputs.end(), -42.0f);
            ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
            EXPECT_EQ(Outputs.front(), -42.0f);
            EXPECT_EQ(Outputs.back(), -42.0f);
            for (unsigned Lane = 0; Lane < 4 * QuadCount; ++Lane) {
              float Ref = Inputs[Lane * 6 + 4];
              bool Layer = Inputs[Lane * 6 + 3] != 0;
              bool Clamped = Inputs[Lane * 6 + 5] == 2.0f;
              bool Expected[] = {
                  Ref < Base + 0.03125f,
                  Ref < Base - 0.03125f,
                  Ref < Base + (Clamped ? -0.03125f : 0.03125f),
                  Ref < Base + (Layer ? -0.03125f : 0.03125f),
                  Ref < Base + (Layer ? 0.03125f : -0.03125f),
                  Ref < Base + ((Layer != Clamped) ? -0.03125f : 0.03125f)};
              for (unsigned Component = 0; Component < 6; ++Component)
                EXPECT_EQ(Outputs[1 + Lane * 6 + Component],
                          (Lane & 1) ? -42.0f : Expected[Component]);
            }
            for (const auto &Result : Results) {
              EXPECT_EQ(Result.LiveMask, 0xfu);
              EXPECT_EQ(Result.SideEffectMask, 0x5u);
            }
          }
        }
  }
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

TEST(CompiledStageTest,
     InvokePatchConstantReadsInputPatchSeparatelyFromOutputPatch) {
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

TEST(CompiledStageTest, InvokeDomainRunsStageAwarePath) {
  Context Ctx;
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0), makeDomainLocationInput(1),
                  makeFloatPatchInput(2), makeFloatOutput(3)};
  Expected<std::unique_ptr<CompiledStage>> Stage = compileGraphicsStage(
      Ctx, DomainShaderIR, "ds_main", Sig, ShaderStage::Domain, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Domain);

  // The completed patch's control points, indexed by control point.
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

  // The patch constants, addressed per patch rather than per invocation.
  FemeStageElement PatchConstantElements[3] = {};
  PatchConstantElements[2].ElementID = 2;
  PatchConstantElements[2].FirstComponent = 0;
  PatchConstantElements[2].ComponentCount = 1;
  PatchConstantElements[2].RowCount = 1;
  FemeStageLayout PatchConstantLayout{};
  PatchConstantLayout.Elements = PatchConstantElements;
  PatchConstantLayout.ElementCount = 3;

  FemeStageElement OutputElements[4] = {};
  OutputElements[3].ElementID = 3;
  OutputElements[3].FirstComponent = 0;
  OutputElements[3].ComponentCount = 1;
  OutputElements[3].RowCount = 1;
  OutputElements[3].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;

  std::vector<float> Inputs = {2.0f, 6.0f};
  std::vector<float> PatchConstants = {10.0f};
  std::vector<float> Outputs(3, -1.0f);
  FemeDomainInvocation Invocations[3] = {};
  Invocations[0].DomainLocation[0] = 0.0f;
  Invocations[1].DomainLocation[0] = 0.5f;
  Invocations[2].DomainLocation[0] = 1.0f;

  DomainResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.PatchConstantLayout = &PatchConstantLayout;
  Resources.PatchConstants = PatchConstants.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.Invocations = Invocations;
  Resources.OutputControlPointCount = 2;
  PreparedDomainBatch Prepared =
      PreparedDomainBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeDomain(Prepared), Succeeded());
  // lerp(2, 6, u) * 10, evaluated at u = 0, 0.5 and 1.
  EXPECT_EQ(Outputs[0], 20.0f);
  EXPECT_EQ(Outputs[1], 40.0f);
  EXPECT_EQ(Outputs[2], 60.0f);

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.Stage, ShaderStage::Domain);
  EXPECT_FALSE(Artifact.Signature.empty());
}

Expected<sys::OwningMemoryBlock> allocateWithGuardPage() {
  unsigned PageSize = sys::Process::getPageSizeEstimate();
  std::error_code EC;
  sys::OwningMemoryBlock Memory(sys::Memory::allocateMappedMemory(
      2 * PageSize, nullptr, sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC));
  if (EC)
    return createStringError(EC, "allocating guarded stage storage");
  void *Guard =
      static_cast<uint8_t *>(Memory.base()) + Memory.allocatedSize() - PageSize;
#if defined(_WIN32)
  DWORD OldProtection;
  if (!VirtualProtect(Guard, PageSize, PAGE_NOACCESS, &OldProtection))
    EC = std::error_code(static_cast<int>(GetLastError()),
                         std::system_category());
#else
  if (mprotect(Guard, PageSize, PROT_NONE))
    EC = std::error_code(errno, std::generic_category());
#endif
  if (EC)
    return createStringError(EC, "protecting stage storage guard page");
  return std::move(Memory);
}

TEST(CompiledStageTest, PartialDomainWavesDoNotAccessPastGuardedStorage) {
  constexpr char IR[] = R"(
    define void @ds_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %p = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
      %v = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
      %sum = add i32 %p, %v
      %f = uitofp i32 %sum to float
      %r = fadd float %u, %f
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %r, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" }
  )";
  EntrySignature Sig;
  auto PrimitiveID = makeFloatInput(1);
  PrimitiveID.ComponentType = SignatureComponentType::UInt;
  PrimitiveID.SystemValue = SignatureSystemValue::PrimitiveID;
  auto ViewIndex = PrimitiveID;
  ViewIndex.ElementID = 2;
  ViewIndex.SystemValue = SignatureSystemValue::ViewIndex;
  Sig.Elements = {makeDomainLocationInput(0), PrimitiveID, ViewIndex,
                  makeFloatOutput(3)};
  FemeStageElement OutputElements[4] = {};
  OutputElements[3].ElementID = 3;
  OutputElements[3].ComponentCount = 1;
  OutputElements[3].RowCount = 1;
  OutputElements[3].InvocationStride = sizeof(float);
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;

  for (unsigned WaveSize : {4u, 8u, 16u, 32u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ds_main", Sig,
                                      ShaderStage::Domain, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    for (unsigned Count : {1u, 3u, 5u, 18u}) {
      SCOPED_TRACE(testing::Message()
                   << "wave=" << WaveSize << " count=" << Count);
      auto InvocationMemory = allocateWithGuardPage();
      auto OutputMemory = allocateWithGuardPage();
      ASSERT_THAT_EXPECTED(InvocationMemory, Succeeded());
      ASSERT_THAT_EXPECTED(OutputMemory, Succeeded());
      unsigned PageSize = sys::Process::getPageSizeEstimate();
      auto *Invocations = reinterpret_cast<FemeDomainInvocation *>(
          static_cast<uint8_t *>(InvocationMemory->base()) +
          InvocationMemory->allocatedSize() - PageSize -
          Count * sizeof(FemeDomainInvocation));
      auto *Outputs = reinterpret_cast<float *>(
          static_cast<uint8_t *>(OutputMemory->base()) +
          OutputMemory->allocatedSize() - PageSize - Count * sizeof(float));
      for (unsigned I = 0; I != Count; ++I) {
        new (&Invocations[I]) FemeDomainInvocation{};
        Invocations[I].DomainLocation[0] = I * 0.25f;
        Invocations[I].PrimitiveID = I + 10;
        Invocations[I].ViewIndex = I + 100;
        new (&Outputs[I]) float(-1.0f);
      }
      DomainResources Resources;
      Resources.Invocations =
          ArrayRef<FemeDomainInvocation>(Invocations, Count);
      Resources.OutputLayout = &OutputLayout;
      Resources.Outputs = Outputs;
      auto Prepared =
          PreparedDomainBatch::create((*Stage)->getResourceInfo(), Resources);
      ASSERT_THAT_ERROR((*Stage)->invokeDomain(Prepared), Succeeded());
      for (unsigned I = 0; I != Count; ++I)
        EXPECT_FLOAT_EQ(Outputs[I], 110.0f + I * 2.25f);
    }
  }
}

TEST(CompiledStageTest, PartialFragmentWavesDoNotAccessPastGuardedStorage) {
  constexpr char IR[] = R"(
    define void @ps_main() #0 {
      %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %in, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0), makeFloatOutput(1)};
  FemeStageElement InputElement{};
  InputElement.ComponentCount = 1;
  InputElement.RowCount = 1;
  InputElement.InvocationStride = sizeof(float);
  FemeStageLayout InputLayout{};
  InputLayout.Elements = &InputElement;
  InputLayout.ElementCount = 1;
  FemeStageElement OutputElements[2] = {};
  OutputElements[1] = InputElement;
  OutputElements[1].ElementID = 1;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 2;

  for (unsigned WaveSize : {4u, 8u, 16u, 32u}) {
    Context Ctx;
    auto Stage = compileGraphicsStage(Ctx, IR, "ps_main", Sig,
                                      ShaderStage::Fragment, WaveSize);
    ASSERT_THAT_EXPECTED(Stage, Succeeded());
    for (unsigned QuadCount : {1u, 3u, 5u}) {
      SCOPED_TRACE(testing::Message()
                   << "wave=" << WaveSize << " quads=" << QuadCount);
      auto InvocationMemory = allocateWithGuardPage();
      auto ResultMemory = allocateWithGuardPage();
      auto InputMemory = allocateWithGuardPage();
      auto OutputMemory = allocateWithGuardPage();
      ASSERT_THAT_EXPECTED(InvocationMemory, Succeeded());
      ASSERT_THAT_EXPECTED(ResultMemory, Succeeded());
      ASSERT_THAT_EXPECTED(InputMemory, Succeeded());
      ASSERT_THAT_EXPECTED(OutputMemory, Succeeded());
      auto Tail = [](const sys::OwningMemoryBlock &Memory, size_t Bytes) {
        return static_cast<uint8_t *>(Memory.base()) + Memory.allocatedSize() -
               sys::Process::getPageSizeEstimate() - Bytes;
      };
      auto *Invocations = reinterpret_cast<FemeFragmentInvocation *>(
          Tail(*InvocationMemory, QuadCount * sizeof(FemeFragmentInvocation)));
      auto *Results = reinterpret_cast<FemeFragmentResult *>(
          Tail(*ResultMemory, QuadCount * sizeof(FemeFragmentResult)));
      auto *Inputs = reinterpret_cast<float *>(
          Tail(*InputMemory, QuadCount * 4 * sizeof(float)));
      auto *Outputs = reinterpret_cast<float *>(
          Tail(*OutputMemory, QuadCount * 4 * sizeof(float)));
      for (unsigned Q = 0; Q != QuadCount; ++Q) {
        new (&Invocations[Q]) FemeFragmentInvocation{};
        bool Live = QuadCount == 1 || Q + 1 != QuadCount;
        Invocations[Q].LiveMask = Live ? 0xf : 0;
        Invocations[Q].SideEffectMask = Live ? 0x7 : 0;
        new (&Results[Q]) FemeFragmentResult{};
        Results[Q].LiveMask = Results[Q].SideEffectMask = 0xf;
        for (unsigned Lane = 0; Lane != 4; ++Lane) {
          new (&Inputs[Q * 4 + Lane]) float(Q * 20.0f + (Lane / 2) * 10.0f +
                                            (Lane % 2));
          new (&Outputs[Q * 4 + Lane]) float(-1.0f);
        }
      }
      FragmentResources Resources;
      Resources.InputLayout = &InputLayout;
      Resources.Inputs = Inputs;
      Resources.OutputLayout = &OutputLayout;
      Resources.Outputs = Outputs;
      Resources.Invocations =
          ArrayRef<FemeFragmentInvocation>(Invocations, QuadCount);
      Resources.Results =
          MutableArrayRef<FemeFragmentResult>(Results, QuadCount);
      auto Prepared =
          PreparedFragmentBatch::create((*Stage)->getResourceInfo(), Resources);
      ASSERT_THAT_ERROR((*Stage)->invokeFragments(Prepared), Succeeded());
      for (unsigned Q = 0; Q != QuadCount; ++Q) {
        EXPECT_EQ(Results[Q].LiveMask, Invocations[Q].LiveMask);
        EXPECT_EQ(Results[Q].SideEffectMask, Invocations[Q].SideEffectMask);
        for (unsigned Lane = 0; Lane != 4; ++Lane)
          EXPECT_FLOAT_EQ(Outputs[Q * 4 + Lane],
                          (Invocations[Q].SideEffectMask & (1u << Lane))
                              ? Inputs[Q * 4 + Lane]
                              : -1.0f);
      }
    }
  }
}

// One input vertex per primitive (`VerticesPerPrimitive` == 1): scales that
// vertex's own attribute (element 0) by `SV_PrimitiveID` (element 1) and
// emits/cuts a single-vertex strip onto stream 0 -- covers input load,
// `SV_PrimitiveID`, output store, `emit`, and `cut` lowering all at once
// (see GeometryWrapper.cpp).
constexpr char GeometryShaderIR[] = R"(
  define void @gs_main() #0 {
    %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %pid = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
    %pidf = uitofp i32 %pid to float
    %r = fmul float %v, %pidf
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %r, i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.cut(i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.stream.emit(i32)
  declare void @feme.stage.stream.cut(i32)
  attributes #0 = { "feme.shader.stage"="geometry" }
)";

SignatureElement makePrimitiveIDInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::PrimitiveID;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.BitWidth = 32;
  return Elt;
}

// (Roadmap H5d-a) One input vertex per primitive: scales that vertex's own
// attribute (element 0) by `10 * SV_PrimitiveID + gl_InvocationID` (elements
// 1/2), distinguishing the two system values from each other -- covers
// `InvocationID` input-load lowering (see GeometryWrapper.cpp's
// `lowerGeometryInvocationID`), which a shader declaring
// `layout(invocations = N)` for `N > 1` needs to tell its own repeated
// invocations of the same primitive apart.
constexpr char GeometryInvocationIDShaderIR[] = R"(
  define void @gs_main() #0 {
    %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %pid = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
    %iid = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
    %pid10 = mul i32 %pid, 10
    %key = add i32 %pid10, %iid
    %keyf = uitofp i32 %key to float
    %r = fmul float %v, %keyf
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %r, i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.cut(i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.stream.emit(i32)
  declare void @feme.stage.stream.cut(i32)
  attributes #0 = { "feme.shader.stage"="geometry" }
)";

SignatureElement makeInvocationIDInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::InvocationID;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.BitWidth = 32;
  return Elt;
}

TEST(CompiledStageTest, InvokeGeometryReadsInvocationIDDistinctFromPrimitiveID) {
  Context Ctx;
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0), makePrimitiveIDInput(1),
                  makeInvocationIDInput(2), makeFloatOutput(3)};
  Expected<std::unique_ptr<CompiledStage>> Stage =
      compileGraphicsStage(Ctx, GeometryInvocationIDShaderIR, "gs_main", Sig,
                          ShaderStage::Geometry, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  FemeStageElement InputElements[1] = {};
  InputElements[0].ElementID = 0;
  InputElements[0].FirstComponent = 0;
  InputElements[0].ComponentCount = 1;
  InputElements[0].RowCount = 1;
  InputElements[0].InvocationStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 1;

  FemeStageElement OutputElements[4] = {};
  OutputElements[3].ElementID = 3;
  OutputElements[3].FirstComponent = 0;
  OutputElements[3].ComponentCount = 1;
  OutputElements[3].RowCount = 1;
  OutputElements[3].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;

  // Two primitives, each run twice (`GeometryState::Invocations` == 2):
  // every row shares its primitive's own single input vertex, but carries a
  // distinct `InvocationID`.
  constexpr uint32_t RowCount = 4;
  constexpr uint32_t MaxVerticesPerStream = 1;
  constexpr uint32_t OutputScalarsPerVertex = 1;

  std::vector<float> Inputs = {10.0f, 10.0f, 20.0f, 20.0f};
  std::vector<float> Outputs(RowCount, -1.0f);
  FemeGeometryInvocation Invocations[RowCount] = {};
  Invocations[0].PrimitiveID = 2;
  Invocations[0].InvocationID = 0;
  Invocations[1].PrimitiveID = 2;
  Invocations[1].InvocationID = 1;
  Invocations[2].PrimitiveID = 5;
  Invocations[2].InvocationID = 0;
  Invocations[3].PrimitiveID = 5;
  Invocations[3].InvocationID = 1;

  std::vector<float> EmittedVertices(RowCount * MaxVerticesPerStream *
                                        OutputScalarsPerVertex,
                                    0.0f);
  std::vector<uint32_t> EmittedVertexCounts(RowCount, 0);
  std::vector<uint8_t> StripEndsAfter(RowCount * MaxVerticesPerStream, 0);

  GeometryResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.Invocations = Invocations;
  Resources.VerticesPerPrimitive = 1;
  Resources.MaxVerticesPerStream = MaxVerticesPerStream;
  Resources.OutputScalarsPerVertex = OutputScalarsPerVertex;
  Resources.EmittedVertices = EmittedVertices;
  Resources.EmittedVertexCounts = EmittedVertexCounts;
  Resources.StripEndsAfter = StripEndsAfter;
  PreparedGeometryBatch Prepared =
      PreparedGeometryBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeGeometry(Prepared), Succeeded());
  // row 0: 10 * (10*2 + 0) == 200; row 1: 10 * (10*2 + 1) == 210;
  // row 2: 20 * (10*5 + 0) == 1000; row 3: 20 * (10*5 + 1) == 1020.
  EXPECT_EQ(EmittedVertices[0], 200.0f);
  EXPECT_EQ(EmittedVertices[1], 210.0f);
  EXPECT_EQ(EmittedVertices[2], 1000.0f);
  EXPECT_EQ(EmittedVertices[3], 1020.0f);
}

TEST(CompiledStageTest, InvokeGeometryRunsStageAwarePath) {
  Context Ctx;
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0), makePrimitiveIDInput(1),
                  makeFloatOutput(2)};
  Expected<std::unique_ptr<CompiledStage>> Stage = compileGraphicsStage(
      Ctx, GeometryShaderIR, "gs_main", Sig, ShaderStage::Geometry, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Geometry);

  FemeStageElement InputElements[1] = {};
  InputElements[0].ElementID = 0;
  InputElements[0].FirstComponent = 0;
  InputElements[0].ComponentCount = 1;
  InputElements[0].RowCount = 1;
  InputElements[0].InvocationStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 1;

  FemeStageElement OutputElements[3] = {};
  OutputElements[2].ElementID = 2;
  OutputElements[2].FirstComponent = 0;
  OutputElements[2].ComponentCount = 1;
  OutputElements[2].RowCount = 1;
  OutputElements[2].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 3;

  constexpr uint32_t PrimitiveCount = 2;
  constexpr uint32_t MaxVerticesPerStream = 2;
  constexpr uint32_t OutputScalarsPerVertex = 1;

  std::vector<float> Inputs = {10.0f, 20.0f};
  std::vector<float> Outputs(PrimitiveCount, -1.0f);
  FemeGeometryInvocation Invocations[PrimitiveCount] = {};
  Invocations[0].PrimitiveID = 2;
  Invocations[1].PrimitiveID = 3;

  std::vector<float> EmittedVertices(
      PrimitiveCount * MaxVerticesPerStream * OutputScalarsPerVertex, 0.0f);
  std::vector<uint32_t> EmittedVertexCounts(PrimitiveCount, 0);
  std::vector<uint8_t> StripEndsAfter(PrimitiveCount * MaxVerticesPerStream, 0);

  GeometryResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.Invocations = Invocations;
  Resources.VerticesPerPrimitive = 1;
  Resources.MaxVerticesPerStream = MaxVerticesPerStream;
  Resources.OutputScalarsPerVertex = OutputScalarsPerVertex;
  Resources.EmittedVertices = EmittedVertices;
  Resources.EmittedVertexCounts = EmittedVertexCounts;
  Resources.StripEndsAfter = StripEndsAfter;
  PreparedGeometryBatch Prepared =
      PreparedGeometryBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeGeometry(Prepared), Succeeded());
  // 10 * 2 and 20 * 3: one vertex emitted (and immediately cut into its own
  // strip) per primitive.
  EXPECT_EQ(EmittedVertexCounts[0], 1u);
  EXPECT_EQ(EmittedVertexCounts[1], 1u);
  EXPECT_EQ(EmittedVertices[0 * MaxVerticesPerStream], 20.0f);
  EXPECT_EQ(EmittedVertices[1 * MaxVerticesPerStream], 60.0f);
  EXPECT_TRUE(StripEndsAfter[0 * MaxVerticesPerStream]);
  EXPECT_TRUE(StripEndsAfter[1 * MaxVerticesPerStream]);

  StageArtifactInfo Artifact = (*Stage)->getArtifactInfo();
  EXPECT_EQ(Artifact.Stage, ShaderStage::Geometry);
  EXPECT_FALSE(Artifact.Signature.empty());
}

// (Roadmap H21e) One input vertex per primitive: emits that vertex's own
// attribute (element 0) unchanged onto stream 0, and `10x` it onto stream
// 1, cutting both -- covers `lowerGeometryStreamEmit`/`Cut`'s own
// `[0, StreamCount)` addressing end to end (each stream's own
// `EmittedVertices`/`EmittedVertexCounts`/`StripEndsAfter` sub-range must
// be written independently of the other's).
constexpr char GeometryMultiStreamShaderIR[] = R"(
  define void @gs_main() #0 {
    %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %v, i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.cut(i32 0)
    %v10 = fmul float %v, 10.0
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %v10, i32 0)
    call void @feme.stage.stream.emit(i32 1)
    call void @feme.stage.stream.cut(i32 1)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.stream.emit(i32)
  declare void @feme.stage.stream.cut(i32)
  attributes #0 = { "feme.shader.stage"="geometry" }
)";

TEST(CompiledStageTest, InvokeGeometryCapturesIndependentStreams) {
  Context Ctx;
  EntrySignature Sig;
  SignatureElement Stream0Out = makeFloatOutput(2);
  Stream0Out.Stream = 0;
  SignatureElement Stream1Out = makeFloatOutput(3);
  Stream1Out.Stream = 1;
  Sig.Elements = {makeFloatInput(0), Stream0Out, Stream1Out};
  Expected<std::unique_ptr<CompiledStage>> Stage =
      compileGraphicsStage(Ctx, GeometryMultiStreamShaderIR, "gs_main", Sig,
                           ShaderStage::Geometry, 4);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  FemeStageElement InputElements[1] = {};
  InputElements[0].ElementID = 0;
  InputElements[0].FirstComponent = 0;
  InputElements[0].ComponentCount = 1;
  InputElements[0].RowCount = 1;
  InputElements[0].InvocationStride = 4;
  FemeStageLayout InputLayout{};
  InputLayout.Elements = InputElements;
  InputLayout.ElementCount = 1;

  FemeStageElement OutputElements[4] = {};
  OutputElements[2].ElementID = 2;
  OutputElements[2].FirstComponent = 0;
  OutputElements[2].ComponentCount = 1;
  OutputElements[2].RowCount = 1;
  OutputElements[2].InvocationStride = 4;
  OutputElements[3].ElementID = 3;
  OutputElements[3].FirstComponent = 0;
  OutputElements[3].ComponentCount = 1;
  OutputElements[3].RowCount = 1;
  OutputElements[3].InvocationStride = 4;
  FemeStageLayout OutputLayout{};
  OutputLayout.Elements = OutputElements;
  OutputLayout.ElementCount = 4;

  constexpr uint32_t PrimitiveCount = 2;
  constexpr uint32_t StreamCount = 2;
  constexpr uint32_t MaxVerticesPerStream = 2;
  constexpr uint32_t OutputScalarsPerVertex = 1;

  std::vector<float> Inputs = {5.0f, 7.0f};
  std::vector<float> Outputs(PrimitiveCount * 2, -1.0f);
  FemeGeometryInvocation Invocations[PrimitiveCount] = {};

  std::vector<float> EmittedVertices(PrimitiveCount * StreamCount *
                                         MaxVerticesPerStream *
                                         OutputScalarsPerVertex,
                                     0.0f);
  std::vector<uint32_t> EmittedVertexCounts(PrimitiveCount * StreamCount, 0);
  std::vector<uint8_t> StripEndsAfter(
      PrimitiveCount * StreamCount * MaxVerticesPerStream, 0);

  GeometryResources Resources;
  Resources.InputLayout = &InputLayout;
  Resources.Inputs = Inputs.data();
  Resources.OutputLayout = &OutputLayout;
  Resources.Outputs = Outputs.data();
  Resources.Invocations = Invocations;
  Resources.VerticesPerPrimitive = 1;
  Resources.MaxVerticesPerStream = MaxVerticesPerStream;
  Resources.OutputScalarsPerVertex = OutputScalarsPerVertex;
  Resources.StreamCount = StreamCount;
  Resources.EmittedVertices = EmittedVertices;
  Resources.EmittedVertexCounts = EmittedVertexCounts;
  Resources.StripEndsAfter = StripEndsAfter;
  PreparedGeometryBatch Prepared =
      PreparedGeometryBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeGeometry(Prepared), Succeeded());

  auto Slot = [&](uint32_t Primitive, uint32_t Stream) {
    return Primitive * StreamCount + Stream;
  };
  EXPECT_EQ(EmittedVertexCounts[Slot(0, 0)], 1u);
  EXPECT_EQ(EmittedVertexCounts[Slot(0, 1)], 1u);
  EXPECT_EQ(EmittedVertexCounts[Slot(1, 0)], 1u);
  EXPECT_EQ(EmittedVertexCounts[Slot(1, 1)], 1u);
  EXPECT_EQ(EmittedVertices[Slot(0, 0) * MaxVerticesPerStream], 5.0f);
  EXPECT_EQ(EmittedVertices[Slot(0, 1) * MaxVerticesPerStream], 50.0f);
  EXPECT_EQ(EmittedVertices[Slot(1, 0) * MaxVerticesPerStream], 7.0f);
  EXPECT_EQ(EmittedVertices[Slot(1, 1) * MaxVerticesPerStream], 70.0f);
  EXPECT_TRUE(StripEndsAfter[Slot(0, 0) * MaxVerticesPerStream]);
  EXPECT_TRUE(StripEndsAfter[Slot(0, 1) * MaxVerticesPerStream]);
  EXPECT_TRUE(StripEndsAfter[Slot(1, 0) * MaxVerticesPerStream]);
  EXPECT_TRUE(StripEndsAfter[Slot(1, 1) * MaxVerticesPerStream]);
}

// Roadmap H6c: a mesh entry point dispatches as a bounded workgroup exactly
// like compute, so `feme::cpu::EntryWrapperPass` -- the same group loop,
// groupshared allocation and barrier-region splitting compute already
// uses -- builds its `feme_cpu_entry_<name>` unmodified (see Pipeline.cpp's
// wrapper-selection comment). This writes `GroupID.x` into groupshared,
// barriers, reads it back, doubles it, and stores the result into a bound
// buffer -- covers `FemeMeshArgs`' shared `Resources`/`GroupID`/
// `GroupShared` prefix reaching a real compiled mesh entry point exactly
// the way it already reaches a compute one.
constexpr char MeshGroupSharedBarrierShaderIR[] = R"(
  @shared = internal addrspace(3) global [4 x i32] undef
  define void @ms_main() #0 {
    %h = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0)
    %gid = call i32 @llvm.dx.group.id(i32 0)
    %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
    store i32 %gid, ptr addrspace(3) %ptr
    call void @llvm.dx.group.memory.barrier.with.group.sync()
    %val = load i32, ptr addrspace(3) %ptr
    %doubled = mul i32 %val, 2
    %offset = mul i32 %gid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %doubled)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare i32 @llvm.dx.group.id(i32)
  declare void @llvm.dx.group.memory.barrier.with.group.sync()
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

// Same shape as `MeshGroupSharedBarrierShaderIR`, tagged as the task
// (amplification) stage instead -- `MeshState`'s own file comment notes the
// task stage dispatches the same bounded-workgroup way mesh does.
constexpr char TaskGroupSharedBarrierShaderIR[] = R"(
  @shared = internal addrspace(3) global [4 x i32] undef
  define void @ts_main() #0 {
    %h = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0)
    %gid = call i32 @llvm.dx.group.id(i32 0)
    %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
    store i32 %gid, ptr addrspace(3) %ptr
    call void @llvm.dx.group.memory.barrier.with.group.sync()
    %val = load i32, ptr addrspace(3) %ptr
    %doubled = mul i32 %val, 2
    %offset = mul i32 %gid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %doubled)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare i32 @llvm.dx.group.id(i32)
  declare void @llvm.dx.group.memory.barrier.with.group.sync()
  attributes #0 = { "hlsl.shader"="amplification" "hlsl.numthreads"="1,1,1" }
)";

Expected<std::unique_ptr<CompiledStage>>
compileStage(Context &Ctx, StringRef IR, ShaderStage Stage,
            unsigned WaveSize = 4) {
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(IR, Err, Ctx.getLLVMContext());
  if (!LLVMMod)
    return createStringError(inconvertibleErrorCode(), "parse error: %s",
                             Err.getMessage().str().c_str());
  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));
  StageCompileOptions Opts;
  Opts.Stage = Stage;
  Opts.WaveSize = WaveSize;
  return CompiledStage::create(Ctx, std::move(Mod), Opts);
}

TEST(CompiledStageTest, InvokeMeshReusesComputeGroupSharedAndBarrierLowering) {
  Context Ctx;
  Expected<std::unique_ptr<CompiledStage>> Stage =
      compileStage(Ctx, MeshGroupSharedBarrierShaderIR, ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Mesh);

  std::vector<int32_t> Buffer(4, -1);
  FemeDescriptor Desc{};
  Desc.Data = Buffer.data();
  Desc.SizeInBytes = Buffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;

  MeshResources Resources;
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
  Resources.GroupID = {2, 0, 0};
  Resources.GroupCount = {4, 1, 1};
  std::vector<uint8_t> GroupShared(16, 0);
  Resources.GroupShared = GroupShared;
  PreparedMeshBatch Prepared =
      PreparedMeshBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeMesh(Prepared), Succeeded());
  // Group 2 writes 2 (its own GroupID) doubled, i.e. 4, at its own slot.
  EXPECT_EQ(Buffer, (std::vector<int32_t>{-1, -1, 4, -1}));
}

TEST(CompiledStageTest, InvokeMeshRejectsANonMeshStage) {
  Context Ctx;
  Expected<std::unique_ptr<CompiledStage>> Stage =
      compile(Ctx); // ordinary compute stage.
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  MeshResources Resources;
  PreparedMeshBatch Prepared =
      PreparedMeshBatch::create((*Stage)->getResourceInfo(), Resources);
  EXPECT_THAT_ERROR((*Stage)->invokeMesh(Prepared), Failed());
}

// Roadmap H6c-a-a: a mesh entry's own canonicalized (H6b-shaped)
// `feme.stage.output.store` with a dynamic `Vertex` operand (here, this
// workgroup's own `GroupID`, standing in for a real `SetMeshOutputsEXT`-
// bounded per-vertex loop variable) reaches `MeshOutputWrapperPass`'s new
// lowering, through `EntryWrapperPass`'s reused compute-style group-loop
// wrapper, and lands in `FemeMeshArgs::VertexOutputs` -- the same buffer
// `feme::graphics::Executor::runMeshWorkgroup`'s already-built
// `MeshOutputBuilder` consumer (Executor.cpp) reads from.
constexpr char MeshOutputStoreShaderIR[] = R"(
  define void @ms_main() #0 {
    %gid = call i32 @llvm.dx.group.id(i32 0)
    %gidf = uitofp i32 %gid to float
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %gidf, i32 %gid)
    ret void
  }
  declare i32 @llvm.dx.group.id(i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

Expected<std::unique_ptr<CompiledStage>>
compileMeshStage(Context &Ctx, StringRef IR, StringRef EntryName,
                 const EntrySignature &Sig) {
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(IR, Err, Ctx.getLLVMContext());
  if (!LLVMMod)
    return createStringError(inconvertibleErrorCode(), "parse error: %s",
                             Err.getMessage().str().c_str());
  dxil::setEntrySignature(*LLVMMod->getFunction(EntryName), Sig);
  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));
  StageCompileOptions Opts;
  Opts.Stage = ShaderStage::Mesh;
  Opts.WaveSize = 4;
  return CompiledStage::create(Ctx, std::move(Mod), Opts);
}

TEST(CompiledStageTest, InvokeMeshWritesPerVertexOutputStore) {
  Context Ctx;
  EntrySignature Sig;
  SignatureElement Elt;
  Elt.ElementID = 0;
  Elt.Direction = SignatureDirection::Output;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.Frequency = SignatureFrequency::PerVertex;
  Sig.Elements = {Elt};

  Expected<std::unique_ptr<CompiledStage>> Stage =
      compileMeshStage(Ctx, MeshOutputStoreShaderIR, "ms_main", Sig);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Mesh);

  FemeStageElement VertexOutputElements[1] = {};
  VertexOutputElements[0].ElementID = 0;
  VertexOutputElements[0].FirstComponent = 0;
  VertexOutputElements[0].ComponentCount = 1;
  VertexOutputElements[0].RowCount = 1;
  VertexOutputElements[0].InvocationStride = 4;
  FemeStageLayout VertexOutputLayout{};
  VertexOutputLayout.Elements = VertexOutputElements;
  VertexOutputLayout.ElementCount = 1;

  std::vector<float> VertexOutputs(4, -1.0f);

  MeshResources Resources;
  Resources.GroupID = {2, 0, 0};
  Resources.GroupCount = {4, 1, 1};
  Resources.MaxOutputVertices = 4;
  Resources.VertexOutputLayout = &VertexOutputLayout;
  Resources.VertexOutputs = MutableArrayRef<uint8_t>(
      reinterpret_cast<uint8_t *>(VertexOutputs.data()),
      VertexOutputs.size() * sizeof(float));
  PreparedMeshBatch Prepared =
      PreparedMeshBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeMesh(Prepared), Succeeded());
  // This group's own workgroup (GroupID 2) writes its own id, as a float,
  // at slot 2 of `VertexOutputs`; every other slot stays untouched.
  EXPECT_EQ(VertexOutputs, (std::vector<float>{-1.0f, -1.0f, 2.0f, -1.0f}));
}

// Roadmap H6c-a-a-i: a mesh entry's own canonicalized
// `feme.stage.set_mesh_outputs` -- the form SPIR-V's `SetMeshOutputsEXT`
// converts directly into at the MLIR conversion level
// (`SetMeshOutputsEXTConversionPattern`, SPIRVToLLVMPatterns.cpp) -- reaches
// `MeshOutputWrapperPass`'s new lowering and lands in `FemeMeshArgs::
// ActualVertexCount`/`ActualPrimitiveCount`: the real counts
// `feme::graphics::MeshOutputBuilder::assembleMeshlet` (H6d) trims a
// meshlet's own vertex/primitive arrays down to, instead of always reading
// 0 (the gap H6c-a-a's own closing re-run discovered).
constexpr char MeshSetOutputsShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 3, i32 1)
    ret void
  }
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

TEST(CompiledStageTest, InvokeMeshWritesSetMeshOutputs) {
  Context Ctx;
  EntrySignature Sig;

  Expected<std::unique_ptr<CompiledStage>> Stage =
      compileMeshStage(Ctx, MeshSetOutputsShaderIR, "ms_main", Sig);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Mesh);

  uint32_t ActualVertexCount = 0xFFFFFFFFu;
  uint32_t ActualPrimitiveCount = 0xFFFFFFFFu;

  MeshResources Resources;
  Resources.GroupID = {0, 0, 0};
  Resources.GroupCount = {1, 1, 1};
  Resources.MaxOutputVertices = 64;
  Resources.MaxOutputPrimitives = 126;
  Resources.ActualVertexCount = &ActualVertexCount;
  Resources.ActualPrimitiveCount = &ActualPrimitiveCount;
  PreparedMeshBatch Prepared =
      PreparedMeshBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeMesh(Prepared), Succeeded());
  EXPECT_EQ(ActualVertexCount, 3u);
  EXPECT_EQ(ActualPrimitiveCount, 1u);
}

TEST(CompiledStageTest, InvokeTaskReusesComputeGroupSharedAndBarrierLowering) {
  Context Ctx;
  Expected<std::unique_ptr<CompiledStage>> Stage = compileStage(
      Ctx, TaskGroupSharedBarrierShaderIR, ShaderStage::Amplification);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Amplification);

  std::vector<int32_t> Buffer(4, -1);
  FemeDescriptor Desc{};
  Desc.Data = Buffer.data();
  Desc.SizeInBytes = Buffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;

  TaskResources Resources;
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);
  Resources.GroupID = {3, 0, 0};
  Resources.GroupCount = {4, 1, 1};
  std::vector<uint8_t> GroupShared(16, 0);
  Resources.GroupShared = GroupShared;
  PreparedTaskBatch Prepared =
      PreparedTaskBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeTask(Prepared), Succeeded());
  // Group 3 writes 3 (its own GroupID) doubled, i.e. 6, at its own slot.
  EXPECT_EQ(Buffer, (std::vector<int32_t>{-1, -1, -1, 6}));
}

// Roadmap H6c-a-b: a task entry's own canonicalized (H6i-shaped)
// `feme.stage.task.payload.store` with a compile-time-constant `offset`
// operand reaches `TaskPayloadWrapperPass`'s new lowering, through
// `EntryWrapperPass`'s reused compute-style group-loop wrapper, and lands
// in `FemeTaskArgs::Payload` -- the same buffer
// `feme::graphics::Executor::executeDraws`'s `TaskPayloadBuilder` (wired in
// `Executor.cpp`) hands to every mesh workgroup this task workgroup's own
// `EmitMeshTasksEXT` request would dispatch, through `FemeMeshArgs::
// Payload`.
constexpr char TaskPayloadStoreShaderIR[] = R"(
  define void @as_main() #0 {
    call void @feme.stage.task.payload.store.i32(i32 4, i32 1234)
    ret void
  }
  declare void @feme.stage.task.payload.store.i32(i32, i32)
  attributes #0 = { "hlsl.shader"="amplification" "hlsl.numthreads"="1,1,1" }
)";

TEST(CompiledStageTest, InvokeTaskWritesPayloadStore) {
  Context Ctx;
  Expected<std::unique_ptr<CompiledStage>> Stage =
      compileStage(Ctx, TaskPayloadStoreShaderIR, ShaderStage::Amplification);
  ASSERT_THAT_EXPECTED(Stage, Succeeded());
  EXPECT_EQ((*Stage)->getStage(), ShaderStage::Amplification);

  std::vector<uint8_t> Payload(8, 0);

  TaskResources Resources;
  Resources.GroupID = {0, 0, 0};
  Resources.GroupCount = {1, 1, 1};
  Resources.Payload = Payload;
  PreparedTaskBatch Prepared =
      PreparedTaskBatch::create((*Stage)->getResourceInfo(), Resources);

  ASSERT_THAT_ERROR((*Stage)->invokeTask(Prepared), Succeeded());
  // The store at byte offset 4 writes 1234 as a little-endian i32; byte
  // offset 0 stays untouched (this workgroup's payload struct's own other
  // member, never written).
  int32_t Written;
  std::memcpy(&Written, Payload.data() + 4, sizeof(Written));
  EXPECT_EQ(Written, 1234);
  EXPECT_EQ(std::vector<uint8_t>(Payload.begin(), Payload.begin() + 4),
           (std::vector<uint8_t>{0, 0, 0, 0}));
}

TEST(CompiledStageTest, InvokeTaskRejectsANonTaskStage) {
  Context Ctx;
  Expected<std::unique_ptr<CompiledStage>> Stage =
      compile(Ctx); // ordinary compute stage.
  ASSERT_THAT_EXPECTED(Stage, Succeeded());

  TaskResources Resources;
  PreparedTaskBatch Prepared =
      PreparedTaskBatch::create((*Stage)->getResourceInfo(), Resources);
  EXPECT_THAT_ERROR((*Stage)->invokeTask(Prepared), Failed());
}

} // namespace

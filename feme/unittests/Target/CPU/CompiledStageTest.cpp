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
#include "feme/Target/CPU/JITEngine.h"
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

TEST(CompiledStageTest,
    ConcurrentInvokeGroupCallsAreSafeForIndependentGroups) {
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

} // namespace

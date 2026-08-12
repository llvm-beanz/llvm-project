//===- JITEngineTest.cpp - Tests for feme::cpu::JITEngine ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/JITEngine.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <vector>

using namespace feme;
using namespace feme::cpu;
using namespace llvm;

namespace {

// A minimal raised compute shader: writes its dispatch thread id (as an
// i32) to an unstructured byte-address buffer at that thread's own byte
// offset, i.e. `RWByteAddressBuffer.Store(tid * 4, tid)`. Straight-line,
// uniform control flow throughout, so it is within roadmap milestone 4's
// scope end to end (Prepare -> resource lowering -> SIMDize -> wave
// lowering -> the entry wrapper). The handle's `i8` element type parameter
// is what tells feme::cpu::ResourceLoweringPass this is an unstructured
// buffer (see "Descriptor heaps" in feme/docs/FeMeCPUDesign.md), whose
// second index operand is a plain byte offset rather than a
// (element-index, sub-offset) pair.
constexpr char ShaderIR[] = R"(
  define void @main() #0 {
    %h = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0, i1 false)
    %tid = call i32 @llvm.dx.thread.id(i32 0)
    %offset = mul i32 %tid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %tid)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32, i1)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare i32 @llvm.dx.thread.id(i32)
  attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
)";

TEST(JITEngineTest, RunsThreadIdShaderAgainstARawBuffer) {
  Context Ctx;
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(ShaderIR, Err, Ctx.getLLVMContext());
  ASSERT_TRUE(LLVMMod) << "parse error: " << Err.getMessage().str();

  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));

  JITOptions Opts;
  Opts.WaveSize = 4;
  Expected<std::unique_ptr<JITEngine>> Engine =
      JITEngine::create(Ctx, std::move(Mod), Opts);
  ASSERT_THAT_EXPECTED(Engine, Succeeded());

  EXPECT_EQ((*Engine)->getWaveSize(), 4u);
  EXPECT_EQ((*Engine)->getGroupSize(), (std::array<uint32_t, 3>{4, 1, 1}));

  std::vector<int32_t> Buffer(4, -1);
  FemeDescriptor Desc{};
  Desc.Data = Buffer.data();
  Desc.SizeInBytes = Buffer.size() * sizeof(int32_t);
  Desc.Stride = 0;
  Desc.Format = 0;
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;
  Desc.Counter = nullptr;

  DispatchResources Resources;
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);

  ASSERT_THAT_ERROR((*Engine)->dispatch(Resources, {1, 1, 1}), Succeeded());

  EXPECT_EQ(Buffer, (std::vector<int32_t>{0, 1, 2, 3}));
}

} // namespace

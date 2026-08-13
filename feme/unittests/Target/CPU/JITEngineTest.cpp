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

TEST(JITEngineTest, ReferenceModeRunsTheSameShaderUnwidened) {
  Context Ctx;
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(ShaderIR, Err, Ctx.getLLVMContext());
  ASSERT_TRUE(LLVMMod) << "parse error: " << Err.getMessage().str();

  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));

  JITOptions Opts;
  Opts.Reference = true;
  Expected<std::unique_ptr<JITEngine>> Engine =
      JITEngine::create(Ctx, std::move(Mod), Opts);
  ASSERT_THAT_EXPECTED(Engine, Succeeded());

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

  // The reference path never widens anything (see the "CFG
  // restructurization test suite" section of feme/docs/FeMeCPUDesign.md),
  // so it must produce the same result the widened path
  // (RunsThreadIdShaderAgainstARawBuffer, above) does on this
  // wave-size-independent shader.
  ASSERT_THAT_ERROR((*Engine)->dispatch(Resources, {1, 1, 1}), Succeeded());

  EXPECT_EQ(Buffer, (std::vector<int32_t>{0, 1, 2, 3}));
}

// Regression test for the Mach-O-specific `asm`-label mangling escape (see
// `feme::cpu::detail::stripAsmLabelManglingEscape`'s comment in
// JITEngine.cpp): a `'\1'`-prefixed global name, exactly like Clang emits
// for an `asm`-labeled symbol on a Mach-O target, must come out with that
// leading byte stripped so it matches the plain canonical name a shader
// module's declaration uses. This is exercised directly (rather than only
// through the end-to-end JIT tests above) because those tests can only
// observe the bug on a Mach-O host; this host may not be one.
TEST(JITEngineTest, StripAsmLabelManglingEscapeDropsLeadingSOHByte) {
  LLVMContext Ctx;
  SMDiagnostic Err;
  auto M = parseAssemblyString(R"(
    define void @"\01mangled.name"() {
      ret void
    }
    @"\01mangled.global" = global i32 0
    @plain.global = global i32 0
  )",
                               Err, Ctx);
  ASSERT_TRUE(M) << "parse error: " << Err.getMessage().str();

  feme::cpu::detail::stripAsmLabelManglingEscape(*M);

  EXPECT_NE(M->getFunction("mangled.name"), nullptr);
  EXPECT_EQ(M->getFunction("\01mangled.name"), nullptr);
  EXPECT_NE(M->getGlobalVariable("mangled.global"), nullptr);
  EXPECT_NE(M->getGlobalVariable("plain.global"), nullptr);
}

} // namespace

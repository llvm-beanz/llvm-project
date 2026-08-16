//===- AOTDispatchTest.cpp - Roadmap milestone 11's completion test ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 11's completion test (see feme/docs/FeMeCPUDesign.md):
// the same shader, using a traditional binding and a native dynamic heap
// slot in one module, must produce identical results whether run through
// the JIT (feme::cpu::JITEngine, covered by
// JITEngineTest.RunsShaderMixingTraditionalAndDynamicResources) or through
// AOT runtime dispatch -- a real, retargeted object file's compiled entry
// point, called directly rather than through JITEngine's from-IR JIT
// compile.
//
// This test builds that object file the same way feme::Driver::run does for
// the CPU target (feme::cpu::runPipeline, then feme::TargetMachineBackend),
// loads it with orc::LLJIT::addObjectFile -- exercising real codegen and
// object-file loading rather than JITEngine's IR-level compile -- and calls
// its `feme_cpu_entry_<name>` symbol directly with a manually-built
// FemeDispatchArgs, matching the heap layout
// feme::cpu::BoundResourceNormalizationPass deterministically assigns (see
// "Bound-resource normalization"), which BoundResourceNormalizationTest and
// ResourceInfoTest already cover in isolation.
//
//===----------------------------------------------------------------------===//

#include "feme/Optimizer/OptimizerPipeline.h"
#include "feme/Target/CPU/Pipeline.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/TargetMachineBackend.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::cpu;
using namespace llvm;

namespace {

// Same shape as JITEngineTest's MixedResourceShaderIR: a traditional binding
// (`register(u0, space0)`) plus a native dynamic heap access, both written
// unconditionally (straight-line, uniform control flow -- roadmap milestone
// 4's scope) so every lane's thread id lands in both physical buffers.
constexpr char MixedResourceShaderIR[] = R"(
  define void @main() #0 {
    %bound = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
    %dynamic = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0, i1 false)
    %tid = call i32 @llvm.dx.thread.id(i32 0)
    %offset = mul i32 %tid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %bound, i32 %offset, i32 poison, i32 %tid)
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %dynamic, i32 %offset, i32 poison, i32 %tid)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32, i1)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare i32 @llvm.dx.thread.id(i32)
  attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
)";

/// Runs `feme::cpu::runPipeline` then `feme::TargetMachineBackend`, the same
/// two steps `feme::Driver::run` chains for the CPU target, to produce a
/// real retargeted object file for the host triple.
Expected<SmallVector<char, 0>> compileToObject(llvm::Module &M) {
  Expected<PipelineResult> Result = runPipeline(M, /*EntryPoint=*/"", 4);
  if (!Result)
    return Result.takeError();

  std::string TripleStr = sys::getDefaultTargetTriple();
  std::string LookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(Triple(TripleStr), LookupError);
  if (!TheTarget)
    return createStringError(inconvertibleErrorCode(), "%s",
                             LookupError.c_str());
  std::unique_ptr<TargetMachine> TM(TheTarget->createTargetMachine(
      Triple(TripleStr), /*CPU=*/"", /*Features=*/"", TargetOptions(),
      /*RM=*/std::nullopt));
  if (!TM)
    return createStringError(inconvertibleErrorCode(),
                             "could not create a TargetMachine for '%s'",
                             TripleStr.c_str());
  M.setTargetTriple(Triple(TripleStr));
  M.setDataLayout(TM->createDataLayout());

  OptimizerPipeline().run(M, OptimizerOptions{OptimizationLevel::O2});

  SmallVector<char, 0> Output;
  raw_svector_ostream OS(Output);
  TargetMachineBackend Backend;
  BackendOptions Opts;
  Opts.TargetTriple = TripleStr;
  if (Error E = Backend.run(M, Opts, OS))
    return std::move(E);
  return Output;
}

TEST(AOTDispatchTest, MatchesJITForShaderMixingTraditionalAndDynamicResources) {
  static llvm::once_flag InitFlag;
  llvm::call_once(InitFlag, [] {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
  });

  LLVMContext Ctx;
  SMDiagnostic Err;
  std::unique_ptr<llvm::Module> LLVMMod =
      parseAssemblyString(MixedResourceShaderIR, Err, Ctx);
  ASSERT_TRUE(LLVMMod) << "parse error: " << Err.getMessage().str();

  Expected<SmallVector<char, 0>> ObjectBytes = compileToObject(*LLVMMod);
  ASSERT_THAT_EXPECTED(ObjectBytes, Succeeded());

  auto JIT = cantFail(orc::LLJITBuilder().create());
  auto MemBuf = MemoryBuffer::getMemBufferCopy(
      StringRef(ObjectBytes->data(), ObjectBytes->size()));
  ASSERT_THAT_ERROR(JIT->addObjectFile(std::move(MemBuf)), Succeeded());

  Expected<orc::ExecutorAddr> EntryAddr = JIT->lookup("feme_cpu_entry_main");
  ASSERT_THAT_EXPECTED(EntryAddr, Succeeded());
  using EntryFnTy = void (*)(const FemeDispatchArgs *);
  auto *Entry = EntryAddr->toPtr<EntryFnTy>();

  // The reserved resource-heap prefix is exactly the one accepted range's
  // size (1, `register(u0, space0)`'s own array length), assigned base 0 --
  // see "Bound-resource normalization" -- so the native dynamic slot 0 the
  // shader itself asks for lands at physical index 1.
  std::vector<int32_t> BoundBuffer(4, -1);
  FemeDescriptor BoundDesc{};
  BoundDesc.Data = BoundBuffer.data();
  BoundDesc.SizeInBytes = BoundBuffer.size() * sizeof(int32_t);
  BoundDesc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  BoundDesc.Flags = FEME_DESCRIPTOR_UAV;

  std::vector<int32_t> DynamicBuffer(4, -1);
  FemeDescriptor DynamicDesc{};
  DynamicDesc.Data = DynamicBuffer.data();
  DynamicDesc.SizeInBytes = DynamicBuffer.size() * sizeof(int32_t);
  DynamicDesc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  DynamicDesc.Flags = FEME_DESCRIPTOR_UAV;

  FemeDescriptor PhysicalHeap[2] = {BoundDesc, DynamicDesc};

  FemeDispatchArgs Args{};
  Args.Resources.ResourceHeap = PhysicalHeap;
  Args.Resources.ResourceHeapCount = 2;
  Args.GroupCount[0] = 1;
  Args.GroupCount[1] = 1;
  Args.GroupCount[2] = 1;
  Args.GroupID[0] = 0;
  Args.GroupID[1] = 0;
  Args.GroupID[2] = 0;
  Entry(&Args);

  // Identical to JITEngineTest.RunsShaderMixingTraditionalAndDynamicResources'
  // own JIT-dispatched result for the same shader.
  EXPECT_EQ(BoundBuffer, (std::vector<int32_t>{0, 1, 2, 3}));
  EXPECT_EQ(DynamicBuffer, (std::vector<int32_t>{0, 1, 2, 3}));
}

} // namespace

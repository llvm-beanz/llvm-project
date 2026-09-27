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

#include <array>
#include <cmath>
#include <vector>

using namespace feme;
using namespace feme::cpu;
using namespace llvm;

namespace {

/// Builds a `Kind::Raw`, `FEME_DESCRIPTOR_UAV` descriptor pointing at
/// \p Buffer's storage, matching the shape every raw-buffer test below
/// dispatches against.
FemeDescriptor makeRawDescriptor(std::vector<int32_t> &Buffer) {
  FemeDescriptor Desc{};
  Desc.Data = Buffer.data();
  Desc.SizeInBytes = Buffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;
  return Desc;
}

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
        @llvm.dx.resource.handlefromheap(i32 0)
    %tid = call i32 @llvm.dx.thread.id(i32 0)
    %offset = mul i32 %tid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %tid)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32)
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

// Roadmap milestone R21: `JITOptions::NumThreads` is real now, not merely
// accepted -- this dispatches many groups (16 groups of 4 lanes each, 64
// total dispatch threads) across a real worker pool
// (`NumThreads = 4`, deliberately more than one so this would deadlock or
// corrupt `Buffer` if `dispatch` still ran everything on the calling thread
// unsynchronized) and checks every thread id landed in its own, correct
// slot -- exactly what `RunsThreadIdShaderAgainstARawBuffer`'s single-group,
// implicitly-sequential (`NumThreads == 0`'s previous no-op meaning) case
// already checks, but now exercised at a scale that only a real thread pool
// gets right.
TEST(JITEngineTest, DispatchRunsGroupsAcrossARealWorkerPool) {
  Context Ctx;
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(ShaderIR, Err, Ctx.getLLVMContext());
  ASSERT_TRUE(LLVMMod) << "parse error: " << Err.getMessage().str();

  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));

  JITOptions Opts;
  Opts.WaveSize = 4;
  Opts.NumThreads = 4;
  Expected<std::unique_ptr<JITEngine>> Engine =
      JITEngine::create(Ctx, std::move(Mod), Opts);
  ASSERT_THAT_EXPECTED(Engine, Succeeded());

  constexpr uint32_t NumGroups = 16;
  constexpr uint32_t NumThreads = NumGroups * 4;
  std::vector<int32_t> Buffer(NumThreads, -1);
  FemeDescriptor Desc{};
  Desc.Data = Buffer.data();
  Desc.SizeInBytes = Buffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;

  DispatchResources Resources;
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&Desc, 1);

  ASSERT_THAT_ERROR((*Engine)->dispatch(Resources, {NumGroups, 1, 1}),
                    Succeeded());

  std::vector<int32_t> Expected(NumThreads);
  for (uint32_t I = 0; I != NumThreads; ++I)
    Expected[I] = static_cast<int32_t>(I);
  EXPECT_EQ(Buffer, Expected);
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

// Regression test for the "Linking two modules of different target
// triples" spurious warning (see
// `feme::cpu::detail::alignRuntimeModuleTriple`'s comment in JITEngine.cpp):
// `libFeMeRuntimeCPU`'s bitcode is compiled with no explicit `-target`, so it
// may carry a triple that is textually different from -- but names the very
// same target as -- the shader module's own (already resolved) triple, e.g.
// Clang's Mach-O default spelling an OS component "macosx<ver>" where an
// explicit "-darwin<ver>" triple spells it "darwin<ver>".
// `alignRuntimeModuleTriple` must retarget the runtime module to the shader
// module's exact triple so `Linker::linkInModule` sees them as identical.
TEST(JITEngineTest, AlignRuntimeModuleTripleMatchesShaderModuleTriple) {
  LLVMContext Ctx;
  SMDiagnostic Err;
  auto RuntimeMod = parseAssemblyString("", Err, Ctx);
  ASSERT_TRUE(RuntimeMod) << "parse error: " << Err.getMessage().str();
  RuntimeMod->setTargetTriple(Triple("arm64-apple-macosx14.0.0"));

  auto ShaderMod = parseAssemblyString("", Err, Ctx);
  ASSERT_TRUE(ShaderMod) << "parse error: " << Err.getMessage().str();
  ShaderMod->setTargetTriple(Triple("arm64-apple-darwin23.4.0"));

  feme::cpu::detail::alignRuntimeModuleTriple(*RuntimeMod, *ShaderMod);

  EXPECT_EQ(RuntimeMod->getTargetTriple().str(),
            ShaderMod->getTargetTriple().str());
}

// A shader mixing a traditional binding (`register(u0, space0)`, bound to a
// single raw buffer) with a native dynamic heap access -- roadmap milestone
// 11's completion-test shape, exercised here through the JIT path. Thread
// `tid` stores `tid` into both buffers at its own byte offset (straight-line,
// uniform control flow throughout -- see roadmap milestone 4's scope note),
// so a correct dispatch writes the same values to both physical buffers the
// host supplies.
constexpr char MixedResourceShaderIR[] = R"(
  define void @main() #0 {
    %bound = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
    %dynamic = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0)
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
      @llvm.dx.resource.handlefromheap(i32)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare i32 @llvm.dx.thread.id(i32)
  attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
)";

// Covers roadmap milestone 11's completion test through the JIT dispatch
// path: the same shader, using a traditional binding and a native dynamic
// slot in one module, produces the correct result once the host supplies
// both a `BoundResourceBinding` and a logical dynamic heap (see
// "Bound-resource normalization"/"Descriptor heaps" in
// feme/docs/FeMeCPUDesign.md).
TEST(JITEngineTest, RunsShaderMixingTraditionalAndDynamicResources) {
  Context Ctx;
  SMDiagnostic Err;
  auto LLVMMod =
      parseAssemblyString(MixedResourceShaderIR, Err, Ctx.getLLVMContext());
  ASSERT_TRUE(LLVMMod) << "parse error: " << Err.getMessage().str();

  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));

  JITOptions Opts;
  Opts.WaveSize = 4;
  Expected<std::unique_ptr<JITEngine>> Engine =
      JITEngine::create(Ctx, std::move(Mod), Opts);
  ASSERT_THAT_EXPECTED(Engine, Succeeded());

  const ResourceInfo &Info = (*Engine)->getResourceInfo();
  EXPECT_EQ(Info.ReservedResourceHeapSize, 1u);
  ASSERT_EQ(Info.BoundRanges.size(), 1u);
  EXPECT_EQ(Info.BoundRanges[0].Space, 0u);
  EXPECT_EQ(Info.BoundRanges[0].BaseRegister, 0u);
  EXPECT_EQ(Info.BoundRanges[0].HeapBase, 0u);

  std::vector<int32_t> BoundBuffer(4, -1);
  FemeDescriptor BoundDesc = makeRawDescriptor(BoundBuffer);
  BoundResourceBinding Binding;
  Binding.Space = 0;
  Binding.BaseRegister = 0;
  Binding.Descriptors = ArrayRef<FemeDescriptor>(&BoundDesc, 1);

  std::vector<int32_t> DynamicBuffer(4, -1);
  FemeDescriptor DynamicDesc = makeRawDescriptor(DynamicBuffer);

  DispatchResources Resources;
  Resources.BoundResources = ArrayRef<BoundResourceBinding>(&Binding, 1);
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(&DynamicDesc, 1);

  ASSERT_THAT_ERROR((*Engine)->dispatch(Resources, {1, 1, 1}), Succeeded());

  // Both physical buffers received every lane's own thread id.
  EXPECT_EQ(BoundBuffer, (std::vector<int32_t>{0, 1, 2, 3}));
  EXPECT_EQ(DynamicBuffer, (std::vector<int32_t>{0, 1, 2, 3}));
}

} // namespace

// (Roadmap L214) End-to-end confirmation, through the real ORC JIT and this
// host's real TargetMachine (the exact codegen path FeMe's own CPU runtime
// uses in production, unlike a standalone `llc` reproducer), that the
// `llvm.get.rounding`/`llvm.set.rounding` + `dynamic`-mode constrained
// intrinsic pattern `FloatControlArithmeticPattern`
// (SPIRVToLLVMPatterns.cpp) now emits for a non-default rounding mode
// actually produces round-toward-zero *results*, not merely round-toward-
// zero-looking *IR* (which every Conversion/SPIRVToLLVM lit test already
// checks, but none of them execute anything). `A = 1.0f`, `B = 3 * 2^-25`
// (0.75 ULP above 1.0f, chosen so plain IEEE round-to-nearest-even and
// round-toward-zero provably disagree, unlike an exact-tie case where both
// directions coincide): `A + B` rounds to `0x3f800001` under the default
// round-to-nearest-even (confirmed with a standalone `fesetround`
// reproducer while writing this test) and to `0x3f800000` under
// round-toward-zero. Before this session's L214 fix, this exact IR shape
// (a *static* `towardzero`-mode constrained intrinsic) silently produced
// the round-to-nearest-even bit pattern on this host's AArch64 backend
// instead -- this test would have failed against that code.
TEST(JITEngineTest, RoundingModeRTZArithmeticProducesTowardZeroBits) {
  Context Ctx;
  SMDiagnostic Err;
  // `A`/`B` are loaded from an input raw buffer (heap index 0), not baked in
  // as literal-constant `fadd` operands: LLVM's own optimizer can (and, in
  // FeMe's real pipeline, does) constant-fold a *static*-rounding-mode
  // constrained intrinsic call whose operands are both compile-time
  // constants using its own, always-correct, backend-independent constant
  // folder (`ConstantFoldConstrainedFPCall`) -- which would silently mask
  // exactly the runtime-codegen bug this test exists to catch. Routing `A`/
  // `B` through a runtime buffer load instead forces the `fadd` to survive
  // as a genuine, unfoldable runtime operation, so this test actually
  // exercises real JIT-compiled backend codegen, not the optimizer's own
  // (already-correct) constant folder.
  constexpr char ShaderIR[] = R"(
    define void @main() #0 {
      %in = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefromheap(i32 0)
      %out = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefromheap(i32 1)
      %aLoaded = call {float, i1} @llvm.dx.resource.load.rawbuffer(
          target("dx.RawBuffer", i8, 1, 0) %in, i32 0, i32 4)
      %a = extractvalue {float, i1} %aLoaded, 0
      %bLoaded = call {float, i1} @llvm.dx.resource.load.rawbuffer(
          target("dx.RawBuffer", i8, 1, 0) %in, i32 4, i32 4)
      %b = extractvalue {float, i1} %bLoaded, 0
      %old = call i32 @llvm.get.rounding()
      call void @llvm.set.rounding(i32 0)
      %sum = call float @llvm.experimental.constrained.fadd.f32(
          float %a, float %b,
          metadata !"round.dynamic", metadata !"fpexcept.ignore")
      call void @llvm.set.rounding(i32 %old)
      %bits = bitcast float %sum to i32
      call void @llvm.dx.resource.store.rawbuffer.i32(
          target("dx.RawBuffer", i8, 1, 0) %out, i32 0, i32 poison, i32 %bits)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32)
    declare {float, i1} @llvm.dx.resource.load.rawbuffer(
        target("dx.RawBuffer", i8, 1, 0), i32, i32)
    declare void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
    declare i32 @llvm.get.rounding()
    declare void @llvm.set.rounding(i32)
    declare float @llvm.experimental.constrained.fadd.f32(float, float, metadata, metadata)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" strictfp }
  )";
  auto LLVMMod = parseAssemblyString(ShaderIR, Err, Ctx.getLLVMContext());
  ASSERT_TRUE(LLVMMod) << "parse error: " << Err.getMessage().str();

  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));

  JITOptions Opts;
  Opts.WaveSize = 4;
  Expected<std::unique_ptr<JITEngine>> Engine =
      JITEngine::create(Ctx, std::move(Mod), Opts);
  ASSERT_THAT_EXPECTED(Engine, Succeeded());

  // `A = 1.0f`, `B = 3 * 2^-25` (0.75 ULP above 1.0f): chosen so plain IEEE
  // round-to-nearest-even and round-toward-zero provably disagree (unlike
  // an exact-tie case, where both directions coincide). `A + B` rounds to
  // `0x3f800001` under the default round-to-nearest-even (confirmed with a
  // standalone `fesetround` reproducer while writing this test) and to
  // `0x3f800000` under round-toward-zero.
  std::vector<float> InputBuffer = {1.0f, 3.0f * std::ldexp(1.0f, -25)};
  FemeDescriptor InDesc{};
  InDesc.Data = InputBuffer.data();
  InDesc.SizeInBytes = InputBuffer.size() * sizeof(float);
  InDesc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  InDesc.Flags = 0; // SRV: read-only.

  std::vector<int32_t> OutputBuffer(1, -1);
  FemeDescriptor OutDesc{};
  OutDesc.Data = OutputBuffer.data();
  OutDesc.SizeInBytes = OutputBuffer.size() * sizeof(int32_t);
  OutDesc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
  OutDesc.Flags = FEME_DESCRIPTOR_UAV;

  std::array<FemeDescriptor, 2> Heap = {InDesc, OutDesc};
  DispatchResources Resources;
  Resources.ResourceHeap = ArrayRef<FemeDescriptor>(Heap);

  ASSERT_THAT_ERROR((*Engine)->dispatch(Resources, {1, 1, 1}), Succeeded());

  EXPECT_EQ(static_cast<uint32_t>(OutputBuffer[0]), 0x3f800000u)
      << "expected the round-toward-zero bit pattern (0x3f800000); got the "
         "round-to-nearest-even one instead if this is 0x3f800001, meaning "
         "the dynamic-rounding-mode FPCR fix regressed";
}

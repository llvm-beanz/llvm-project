//===- SPIRVResourceLoweringTest.cpp - Tests for the pass ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVResourceLowering.h"

#include "feme/Target/CPU/ResourceInfo.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("SPIRVResourceLoweringTest", errs());
  return M;
}

void runPass(Module &M) {
  ModuleAnalysisManager MAM;
  SPIRVResourceLoweringPass().run(M, MAM);
}

/// Returns whether \p F contains a canonical `feme.cpu.resource.load.raw.*`
/// call, i.e. whether \p F's bound handle was normalized and lowered.
bool hasResourceLoadCall(Function &F) {
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName().starts_with("feme.cpu.resource.load.raw"))
          return true;
  return false;
}

/// Returns whether \p F contains a canonical `feme.cpu.resource.*.typed.*`
/// call -- see `hasResourceLoadCall`'s comment.
bool hasResourceTypedCall(Function &F, StringRef Prefix) {
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName().starts_with(Prefix))
          return true;
  return false;
}

TEST(SPIRVResourceLoweringTest, LeavesModuleWithNoBoundHandlesUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      ret void
    }
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LowersScalarBindingToResourceLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
      %v = load float, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x float], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest, RecordsArrayRangeSizeInBoundResourceMetadata) {
  // Roadmap R26: a 4-element arrayed binding is assigned a contiguous
  // 4-slot heap range, recorded as such -- rather than the implicit
  // single-slot range this pass assigned every binding before R26.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, i32 %which) {
      %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 4, i32 %which, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
      %v = load float, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x float], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  NamedMDNode *MD = M->getNamedMetadata("feme.cpu.bound_resources");
  ASSERT_TRUE(MD);
  ASSERT_EQ(MD->getNumOperands(), 1u);
  MDNode *Entry = MD->getOperand(0);
  // {name, resource-/image-/sampler-prefix-size,
  //  (set, binding, range-size, heap-base, class)...}.
  ASSERT_EQ(Entry->getNumOperands(), 9u);
  EXPECT_EQ(cast<MDString>(Entry->getOperand(0))->getString(), "main");
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue(),
            4u); // resource-heap prefix size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(2))->getZExtValue(),
            0u); // image-heap prefix size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(3))->getZExtValue(),
            0u); // sampler-heap prefix size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(4))->getZExtValue(),
            0u); // set
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(5))->getZExtValue(),
            1u); // binding
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(6))->getZExtValue(),
            4u); // range size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(7))->getZExtValue(),
            0u); // heap base
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(8))->getZExtValue(),
            static_cast<uint64_t>(feme::cpu::BoundResourceClass::Buffer));
}

TEST(SPIRVResourceLoweringTest, LowersDynamicArrayIndexToRangeCheckedAccess) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, i32 %which) {
      %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 4, i32 %which, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
      %v = load float, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x float], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundDynamicDescriptorIndex = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction() ||
        !CI->getCalledFunction()->getName().starts_with(
            "feme.cpu.resource.load.raw"))
      continue;
    // The descriptor-index operand is not the bare `%which` argument any
    // more -- it goes through the range-check/clamp arithmetic this pass
    // inserts (see `computeClampedIndex` in SPIRVResourceLowering.cpp).
    FoundDynamicDescriptorIndex = !isa<Argument>(CI->getArgOperand(2));
  }
  EXPECT_TRUE(FoundDynamicDescriptorIndex);
}

TEST(SPIRVResourceLoweringTest, LeavesUnboundedArrayUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, i32 %which) {
      %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 0, i32 %which, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
      %v = load float, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x float], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LeavesConflictingRangeSizeUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @a(i32 %idx, i32 %which) {
      %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 2, i32 %which, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
      %v = load float, ptr %ptr
      ret void
    }
    define void @b(i32 %idx, i32 %which) {
      %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 4, i32 %which, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
      %v = load float, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x float], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_FALSE(hasResourceLoadCall(*M->getFunction("a")));
  EXPECT_FALSE(hasResourceLoadCall(*M->getFunction("b")));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

// V3: a uniform buffer block (`cbuffer`/`ConstantBuffer<T>`) shares the same
// `spirv.VulkanBuffer` handle representation a storage buffer does, but its
// sole type parameter is the block's own field struct directly (see
// `feme::spirv::convertUniformBlockType` in SPIRVToLLVMPatterns.cpp) rather
// than a runtime array, and a field access resolves to a compile-time
// struct-layout byte offset rather than `index * stride`.
TEST(SPIRVResourceLoweringTest, LowersUniformBufferFieldToResourceLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main() {
      %h = call target("spirv.VulkanBuffer", {float, i32}, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {float, i32}, 2, 0) %h, i32 0)
      %v = load float, ptr %ptr
      ret float %v
    }
    declare target("spirv.VulkanBuffer", {float, i32}, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {float, i32}, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest,
     LowersUniformBufferSecondFieldToItsOwnStructLayoutOffset) {
  // `{float, i32}`'s second field (`i32`) is naturally aligned at byte
  // offset 4 -- the field index (1) itself is not a byte offset, so this
  // confirms the pass resolves it through the struct's own layout rather
  // than passing the field index straight through.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %h = call target("spirv.VulkanBuffer", {float, i32}, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {float, i32}, 2, 0) %h, i32 1)
      %v = load i32, ptr %ptr
      ret i32 %v
    }
    declare target("spirv.VulkanBuffer", {float, i32}, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {float, i32}, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundExpectedOffset = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction() ||
        !CI->getCalledFunction()->getName().starts_with(
            "feme.cpu.resource.load.raw"))
      continue;
    // Args: {heap, heap_count, descriptor_index, byte_offset, mask}.
    auto *Offset = dyn_cast<ConstantInt>(CI->getArgOperand(3));
    FoundExpectedOffset = Offset && Offset->getZExtValue() == 4;
  }
  EXPECT_TRUE(FoundExpectedOffset);
}

TEST(SPIRVResourceLoweringTest, LeavesUniformBufferStoreUnchanged) {
  // Vulkan disallows writing `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`: a store
  // through a uniform-buffer handle is not an access shape this pass models
  // (see `hasOnlySupportedUses`), so the whole function is left alone.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      %h = call target("spirv.VulkanBuffer", {float, i32}, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {float, i32}, 2, 0) %h, i32 0)
      store float 1.0, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", {float, i32}, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {float, i32}, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LeavesUniformBufferDynamicFieldIndexUnchanged) {
  // A cbuffer field access is always statically typed, so its
  // `getpointer` index is always a compile-time constant in practice;
  // a dynamic one is not an access shape this pass models.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(i32 %field) {
      %h = call target("spirv.VulkanBuffer", {float, i32}, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {float, i32}, 2, 0) %h, i32 %field)
      %v = load float, ptr %ptr
      ret float %v
    }
    declare target("spirv.VulkanBuffer", {float, i32}, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {float, i32}, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest,
     LeavesConflictingBufferKindAtSameIdentityUnchanged) {
  // A storage buffer and a uniform buffer declared at the same (set,
  // binding) identity is a conflicting re-declaration, exactly like two
  // storage buffers disagreeing about stride/range size. The two
  // `handlefrombinding`/`getpointer` overloads need their real (LLVM-
  // mangled) intrinsic names spelled out here, since IR text does not
  // auto-mangle an overloaded intrinsic's declared name the way an
  // `IRBuilder`/`Intrinsic::getOrInsertDeclaration` caller gets for free.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @storage(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
          @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer.p0.tspirv.VulkanBuffer_a0f32_12_1t.i32(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
      %v = load float, ptr %ptr
      ret void
    }
    define float @uniform() {
      %h = call target("spirv.VulkanBuffer", {float, i32}, 2, 0)
          @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_sl_f32i32s_2_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer.p0.tspirv.VulkanBuffer_sl_f32i32s_2_0t.i32(target("spirv.VulkanBuffer", {float, i32}, 2, 0) %h, i32 0)
      %v = load float, ptr %ptr
      ret float %v
    }
    declare target("spirv.VulkanBuffer", [0 x float], 12, 1)
        @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_1t(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.p0.tspirv.VulkanBuffer_a0f32_12_1t.i32(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32)
    declare target("spirv.VulkanBuffer", {float, i32}, 2, 0)
        @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_sl_f32i32s_2_0t(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.p0.tspirv.VulkanBuffer_sl_f32i32s_2_0t.i32(target("spirv.VulkanBuffer", {float, i32}, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_FALSE(hasResourceLoadCall(*M->getFunction("storage")));
  EXPECT_FALSE(hasResourceLoadCall(*M->getFunction("uniform")));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LowersStorageTexelBufferToTypedResourceCalls) {
  // Sampled == 2 ("used without a sampler"): a storage texel buffer
  // (RWBuffer<float4> in HLSL), read-write, over the one shader element
  // shape the CPU runtime's typed-load/store helpers support: <4 x float>
  // (see classifyTexelBufferHandle's comment).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, <4 x float> %v) {
      %h = call target("spirv.Image", <4 x float>, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", <4 x float>, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load <4 x float>, ptr %ptr
      store <4 x float> %v, ptr %ptr
      ret void
    }
    declare target("spirv.Image", <4 x float>, 5, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", <4 x float>, 5, 0, 0, 0, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed"));
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.store.typed"));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest, LowersUniformTexelBufferToTypedLoadOnly) {
  // Sampled == 1 ("used with a sampler"): a uniform texel buffer
  // (Buffer<float4> in HLSL), read-only.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(i32 %idx) {
      %h = call target("spirv.Image", <4 x float>, 5, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", <4 x float>, 5, 0, 0, 0, 1, 0) %h, i32 %idx)
      %loaded = load <4 x float>, ptr %ptr
      ret <4 x float> %loaded
    }
    declare target("spirv.Image", <4 x float>, 5, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", <4 x float>, 5, 0, 0, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed"));
}

TEST(SPIRVResourceLoweringTest, LowersIntegerStorageTexelBufferToV4I32Calls) {
  // (V4) A `<4 x i32>` texel element -- the R32G32B32A32_UINT/_SINT
  // identity-format shape `isSupportedTexelElementType` accepts alongside
  // `<4 x float>` -- lowers to the `.v4i32`-mangled typed calls, the same
  // way the float shape lowers to `.v4f32` ones. The handle's own channel
  // type (`i32` here, SPIR-V's per-*channel* sampled type) stays scalar per
  // `classifyTexelBufferHandle`'s comment; only the load/store's own type
  // is the 4-wide vector.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, <4 x i32> %v) {
      %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load <4 x i32>, ptr %ptr
      store <4 x i32> %v, ptr %ptr
      ret void
    }
    declare target("spirv.Image", i32, 5, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.v4i32"));
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.store.typed.v4i32"));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest, LeavesUnsupportedTexelElementTypeUnchanged) {
  // Only <4 x float>/<4 x i32> are supported (see
  // `isSupportedTexelElementType`'s comment). A *scalar* i32 load/store -- the
  // shape a single-channel format like R32_UINT would need, since SPIR-V's own
  // image ops always return a full 4-component vector regardless of the
  // underlying format's real channel count -- is left un-normalized rather than
  // mis-lowered.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main(i32 %idx) {
      %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load i32, ptr %ptr
      ret i32 %loaded
    }
    declare target("spirv.Image", i32, 5, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

} // namespace

// --- Roadmap R30: bound 2D sampled images and samplers -------------------

namespace {

/// The IR shape `feme::spirv::SampledImagePattern` +
/// `ImageSampleImplicitLodPattern` produce for `texture.Sample(sampler, uv)`:
/// two `handlefrombinding` handles combined into a `{image, sampler}` struct,
/// unpacked again at the sample itself.
constexpr const char *SampleShader = R"(
    %pair = type { target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler") }
    define <4 x float> @main(<2 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %p0 = insertvalue %pair poison, target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, 0
      %p1 = insertvalue %pair %p0, target("spirv.Sampler") %samp, 1
      %i = extractvalue %pair %p1, 0
      %s = extractvalue %pair %p1, 1
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %i,
          target("spirv.Sampler") %s, <2 x float> %coord, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
)";

/// Returns \p F's sole call to the named canonical image helper, or null.
CallInst *findImageCall(Function &F, StringRef Name) {
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName() == Name)
          return CI;
  return nullptr;
}

/// Reads \p EntryName's `!feme.cpu.bound_resources` node, or null.
MDNode *findBoundNode(Module &M, StringRef EntryName) {
  NamedMDNode *MD = M.getNamedMetadata("feme.cpu.bound_resources");
  if (!MD)
    return nullptr;
  for (MDNode *Entry : MD->operands())
    if (cast<MDString>(Entry->getOperand(0))->getString() == EntryName)
      return Entry;
  return nullptr;
}

uint64_t mdInt(const MDNode *N, unsigned Index) {
  return mdconst::extract<ConstantInt>(N->getOperand(Index))->getZExtValue();
}

} // namespace

TEST(SPIRVResourceLoweringTest, LowersSampledImageAndSamplerPairToImageSample) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, SampleShader);
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, lod, use_explicit_lod, mask).
  EXPECT_EQ(Sample->getArgOperand(0)->getName(), "image_heap");
  EXPECT_EQ(Sample->getArgOperand(2)->getName(), "sampler_heap");
  // Both are the sole binding of their own heap, so both resolve to slot 0.
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(4))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(5))->isZero());
  // An implicit-LOD sample asks the runtime for level 0, not for `%lod`.
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(9))->isZero());

  // Neither the combined sampled-image struct nor the handles survive.
  for (Instruction &I : instructions(*F))
    EXPECT_FALSE(isa<InsertValueInst>(&I) || isa<ExtractValueInst>(&I));
}

TEST(SPIRVResourceLoweringTest, AssignsImageAndSamplerTheirOwnHeapClasses) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, SampleShader);
  ASSERT_TRUE(M);
  runPass(*M);

  MDNode *Bound = findBoundNode(*M, "main");
  ASSERT_TRUE(Bound);
  // {name, resource/image/sampler prefix sizes, then two five-field ranges}.
  ASSERT_EQ(Bound->getNumOperands(), 14u);
  EXPECT_EQ(mdInt(Bound, 1), 0u); // no buffer binding at all
  EXPECT_EQ(mdInt(Bound, 2), 1u); // one image slot
  EXPECT_EQ(mdInt(Bound, 3), 1u); // one sampler slot
  EXPECT_EQ(mdInt(Bound, 8), static_cast<uint64_t>(BoundResourceClass::Image));
  EXPECT_EQ(mdInt(Bound, 13),
            static_cast<uint64_t>(BoundResourceClass::Sampler));

  NamedMDNode *Resources = M->getNamedMetadata("feme.cpu.resources");
  ASSERT_TRUE(Resources);
  ASSERT_EQ(Resources->getNumOperands(), 1u);
  EXPECT_EQ(mdInt(Resources->getOperand(0), 2), 1u); // UsesSamplerHeap
}

TEST(SPIRVResourceLoweringTest, LowersImageFetchToImageLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord)
      %v = load <4 x float>, ptr %p
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 0, 0, 1, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2d.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(0)->getName(), "image_heap");
  // A fetch takes no sampler, so nothing reports sampler-heap usage.
  NamedMDNode *Resources = M->getNamedMetadata("feme.cpu.resources");
  ASSERT_TRUE(Resources);
  EXPECT_EQ(mdInt(Resources->getOperand(0), 2), 0u);
}

TEST(SPIRVResourceLoweringTest, ClampsAnArrayedImageBindingIndex) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %coord, i32 %which) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 4, i32 %which, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord)
      %v = load <4 x float>, ptr %p
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 0, 0, 1, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2d.v4f32");
  ASSERT_TRUE(Load);
  // A dynamic array index is range-checked exactly like a buffer's, so the
  // descriptor index is a `select`, not the raw operand.
  EXPECT_TRUE(isa<SelectInst>(Load->getArgOperand(2)));

  MDNode *Bound = findBoundNode(*M, "main");
  ASSERT_TRUE(Bound);
  EXPECT_EQ(mdInt(Bound, 2), 4u); // four reserved image slots
}

TEST(SPIRVResourceLoweringTest, LeavesAnArrayedImageHandleAlone) {
  // An arrayed image needs a layer coordinate the 2D runtime helpers do not
  // take, so the whole function is left for `checkSupportedRaisedOps` to
  // reject rather than lowered into a helper that would silently ignore it.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img, <2 x i32> %coord)
      %v = load <4 x float>, ptr %p
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 1, 0, 1, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.load.2d.v4f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LeavesANonZeroTexelOffsetSampleAlone) {
  // `runtime/CPU`'s helpers take no texel offset yet; dropping one would be
  // a real semantic change, so the sample is left unlowered instead.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord, <2 x i32> <i32 1, i32 0>)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.2d.v4f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LowersIntegerImageFetchToImageLoadV4I32) {
  // Roadmap E26: an `OpImageFetch` against an integer-channel 2D sampled
  // image (`i32` sampled type, mirroring `OpTypeImage`'s own per-channel,
  // never-a-vector, "Sampled Type" operand) lowers to the integer
  // `feme.cpu.image.load.2d.v4i32` entry point rather than the float one.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(<2 x i32> %coord) {
      %img = call target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord)
      %v = load <4 x i32>, ptr %p
      ret <4 x i32> %v
    }
    declare target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 1, 0, 0, 0, 1, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2d.v4i32");
  ASSERT_TRUE(Load);
  EXPECT_TRUE(Load->getType()->isVectorTy());
  EXPECT_EQ(Load->getArgOperand(0)->getName(), "image_heap");
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.load.2d.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LeavesAnIntegerSampledImageHandleUsedForSampleAlone) {
  // SPIR-V never legalizes a filtered `OpImageSample*` against an integer-
  // sampled image, so this pass does not try to lower one either -- the
  // whole handle (and therefore the whole function) is left unrewritten,
  // matching `LeavesAnArrayedImageHandleAlone`'s own "no partial lowering"
  // contract.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(<2 x float> %coord) {
      %img = call target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x i32> @llvm.spv.resource.sample(
          target("spirv.Image", i32, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord, <2 x i32> zeroinitializer)
      ret <4 x i32> %r
    }
    declare target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.2d.v4f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}


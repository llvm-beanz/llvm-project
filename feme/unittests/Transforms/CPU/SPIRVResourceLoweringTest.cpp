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

/// Returns the byte-offset operand of the first canonical
/// `feme.cpu.resource.load.raw.*` call in \p F, or nullptr if none exists.
Value *findRawLoadOffset(Function &F) {
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName().starts_with("feme.cpu.resource.load.raw"))
          return CI->getArgOperand(3);
  return nullptr;
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

// Regression test for roadmap H3a: addResourceEnvParams() replaces a
// function that has any bound resource handle with a brand-new Function via
// Function::Create()+copyAttributesFrom(), but GlobalObject::
// copyAttributesFrom() does not copy function-attached metadata. Any stage
// entry function using a bound resource (e.g. a UBO) was silently losing its
// !feme.signature metadata (attached earlier by CanonicalizeStagePass),
// causing "fragment stage wrapper requires attached feme.signature metadata"
// pipeline-creation failures for shaders reading gl_ViewportIndex out of a
// bound uniform block. Verify function-attached metadata survives the
// resource-env-parameter rewrite.
TEST(SPIRVResourceLoweringTest,
     PreservesFunctionMetadataAcrossEnvParamRewrite) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) !feme.signature !0 {
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

    !0 = !{!"fragment-signature-placeholder"}
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));
  MDNode *Signature = F->getMetadata("feme.signature");
  ASSERT_TRUE(Signature) << "!feme.signature metadata was lost when the "
                            "function was rewritten to add resource-env "
                            "parameters";
  ASSERT_EQ(Signature->getNumOperands(), 1u);
  EXPECT_EQ(cast<MDString>(Signature->getOperand(0))->getString(),
            "fragment-signature-placeholder");
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

// Roadmap L20 (superseding E29's own narrower scope): a storage-buffer
// store of an aggregate (here, an array -- the shape `OpSelect` produces
// choosing between two array-typed operands,
// dEQP-VK.spirv_assembly.instruction.spirv1p4.opselect.array_select's own
// case) is now decomposed into one raw store per element, exactly like
// any other supported array/struct shape (see `isSupportedRawElementType`'s
// comment): the value is never itself passed to
// `feme::cpu::createRawStore`/`mangleResourceCallName` (which still cannot
// mangle a runtime call name for anything but a scalar or fixed vector of
// half/float/double/integer), so this never risks
// `appendScalarMangling`'s own `llvm_unreachable`.
TEST(SPIRVResourceLoweringTest,
     LowersStorageBufferArrayStoreToPerElementRawStores) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, [4 x i32] %v) {
      %h = call target("spirv.VulkanBuffer", [0 x [4 x i32]], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x [4 x i32]], 12, 1) %h, i32 %idx)
      store [4 x i32] %v, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x [4 x i32]], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x [4 x i32]], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.store.raw"));
  EXPECT_TRUE(M->getNamedMetadata("feme.cpu.bound_resources"));

  unsigned NumStores = 0;
  for (Instruction &I : instructions(*F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName().starts_with("feme.cpu.resource.store.raw"))
          ++NumStores;
  EXPECT_EQ(NumStores, 4u);
}


// Roadmap H6g-b-a-i-a-i: glslang can spell a storage buffer block directly
// as a fixed-layout struct whose members are fixed-size arrays/vectors,
// rather than `dxc`'s one-member runtime-array wrapper. Once
// `spirv.AccessChain` lowering has selected one field with
// `llvm.spv.resource.getpointer`, any further array/vector descent is an
// ordinary GEP off that field pointer; this pass must fold the whole chain
// back into one raw byte offset instead of leaving the handle behind for
// `UnsupportedOps`.
TEST(SPIRVResourceLoweringTest,
     LowersDirectStorageBlockFieldAndNestedArrayAccessToResourceLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", {[4 x <4 x float>], [2 x float]}, 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %field = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {[4 x <4 x float>], [2 x float]}, 12, 1) %h, i32 1)
      %elt = getelementptr [2 x float], ptr %field, i32 0, i32 %idx
      %v = load float, ptr %elt
      ret float %v
    }
    declare target("spirv.VulkanBuffer", {[4 x <4 x float>], [2 x float]}, 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {[4 x <4 x float>], [2 x float]}, 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));

  Value *Offset = findRawLoadOffset(*F);
  ASSERT_TRUE(Offset);
  // Field 1 starts after the 64-byte `[4 x <4 x float>]` member, and the
  // nested `[2 x float]` GEP adds `idx * 4` on top of that.
  auto *Base = dyn_cast<Instruction>(Offset);
  ASSERT_TRUE(Base);
  EXPECT_EQ(Base->getOpcode(), Instruction::Add);
  auto *BaseOffset = dyn_cast<ConstantInt>(Base->getOperand(0));
  ASSERT_TRUE(BaseOffset);
  EXPECT_EQ(BaseOffset->getZExtValue(), 64u);
}

TEST(SPIRVResourceLoweringTest,
     LowersStructuredStorageBufferFieldAccessToFieldOffset) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x {<4 x float>, <4 x float>}], 12, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %elt = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x {<4 x float>, <4 x float>}], 12, 0) %h, i32 %idx)
      %field = getelementptr {<4 x float>, <4 x float>}, ptr %elt, i32 0, i32 1
      %v = load <4 x float>, ptr %field
      ret <4 x float> %v
    }
    declare target("spirv.VulkanBuffer", [0 x {<4 x float>, <4 x float>}], 12, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x {<4 x float>, <4 x float>}], 12, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));

  Value *Offset = findRawLoadOffset(*F);
  ASSERT_TRUE(Offset);
  auto *Add = dyn_cast<Instruction>(Offset);
  ASSERT_TRUE(Add);
  EXPECT_EQ(Add->getOpcode(), Instruction::Add);
  auto *FieldOffset = dyn_cast<ConstantInt>(Add->getOperand(1));
  ASSERT_TRUE(FieldOffset);
  EXPECT_EQ(FieldOffset->getZExtValue(), 16u);
}

// Roadmap H7v: a pre-1.3 storage buffer block is spelled with the `Uniform`
// storage class (2) plus a `BufferBlock` decoration rather than the
// dedicated `StorageBuffer` class (12) -- still glslang's default spelling,
// as seen in a real `dEQP-VK.binding_model.shader_access.*.storage_buffer.
// compute.*` shader's own SPIR-V. `convertBufferBlockType` (SPIRVToLLVM
// Patterns.cpp) still carries the real writability bit as this handle's
// second int parameter even in that spelling, so a `Uniform`-class struct
// handle with `Writable=1` must classify as a real (writable) storage
// buffer block, not the read-only uniform block `LowersUniformBufferField
// ToResourceLoad`'s `Writable=0` shape represents -- both share the
// identical storage-class int parameter, so only the writability bit tells
// them apart.
TEST(SPIRVResourceLoweringTest,
     LowersLegacyUniformClassStorageBlockFieldToResourceLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main() {
      %h = call target("spirv.VulkanBuffer", {<4 x float>, <4 x float>}, 2, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {<4 x float>, <4 x float>}, 2, 1) %h, i32 1)
      %v = load <4 x float>, ptr %ptr
      ret <4 x float> %v
    }
    declare target("spirv.VulkanBuffer", {<4 x float>, <4 x float>}, 2, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {<4 x float>, <4 x float>}, 2, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest,
     LowersLegacyUniformClassStorageBlockStoreToResourceStore) {
  // Unlike a real read-only uniform block, this `Writable=1` handle must
  // accept a store -- confirming the fix classifies it as a genuine
  // storage buffer block (`HandleKind::StorageStruct`), not a read-only
  // `HandleKind::Uniform` one.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<4 x float> %v) {
      %h = call target("spirv.VulkanBuffer", {<4 x float>, <4 x float>}, 2, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {<4 x float>, <4 x float>}, 2, 1) %h, i32 0)
      store <4 x float> %v, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", {<4 x float>, <4 x float>}, 2, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {<4 x float>, <4 x float>}, 2, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundStoreCall = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (CI && CI->getCalledFunction() &&
        CI->getCalledFunction()->getName().starts_with(
            "feme.cpu.resource.store.raw"))
      FoundStoreCall = true;
  }
  EXPECT_TRUE(FoundStoreCall);
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest,
     LeavesDirectStorageBlockDynamicFieldSelectorUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(i32 %field) {
      %h = call target("spirv.VulkanBuffer", {[4 x <4 x float>], [2 x float]}, 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {[4 x <4 x float>], [2 x float]}, 12, 1) %h, i32 %field)
      %v = load float, ptr %ptr
      ret float %v
    }
    declare target("spirv.VulkanBuffer", {[4 x <4 x float>], [2 x float]}, 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {[4 x <4 x float>], [2 x float]}, 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasResourceLoadCall(*F));
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

// Roadmap L16: a struct-typed direct-field member (`cbuffer CBStructs {
// X x1; X x2; }` where `X` is itself a user-defined `{i32, i32}` struct,
// `Feature/CBuffer/structs.test`'s own shape once
// `feme::spirv::convertOffsetStructTypeIgnoringDecorations` (roadmap L13a)
// legalizes the identified-struct-member conversion) needs a further
// `getelementptr` navigating into the selected field's own struct body,
// beyond `getpointer`'s own top-level (compile-time-constant) field
// selection -- previously rejected outright (`AllowGEPs` was `false` for
// `HandleKind::Uniform`), even though the identical shape was already
// supported for a direct-field *storage* block (`HandleKind::StorageStruct`).
TEST(SPIRVResourceLoweringTest,
     LowersUniformBufferNestedStructFieldToItsOwnStructLayoutOffset) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %h = call target("spirv.VulkanBuffer", {{i32, i32}, {i32, i32}}, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {{i32, i32}, {i32, i32}}, 2, 0) %h, i32 1)
      %fieldptr = getelementptr inbounds {i32, i32}, ptr %ptr, i32 0, i32 1
      %v = load i32, ptr %fieldptr
      ret i32 %v
    }
    declare target("spirv.VulkanBuffer", {{i32, i32}, {i32, i32}}, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {{i32, i32}, {i32, i32}}, 2, 0), i32)
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
    // `x2` (field 1 of the outer struct) starts at byte offset 8, and
    // `.a2` (field 1 of `X`) is naturally aligned at byte offset 4 within
    // it -- expect the combined 12, not either half alone.
    auto *Offset = dyn_cast<ConstantInt>(CI->getArgOperand(3));
    FoundExpectedOffset = Offset && Offset->getZExtValue() == 12;
  }
  EXPECT_TRUE(FoundExpectedOffset);
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
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

// Roadmap F12a: a std140 uniform buffer array (`layout(std140) uniform
// Input { uint data[16]; } ubo;`, dynamically indexed by
// `gl_GlobalInvocationID.x` -- the
// `dEQP-VK.pipeline.monolithic.push_descriptor.compute.incremental_updates*`
// shape) carries its own real `ArrayStride` (16, wider than its scalar
// `i32` element's own 4-byte natural size) as the handle's own third
// integer parameter, unlike a storage buffer's own runtime array, whose
// stride is always implicit in its element's natural size (see
// `feme::spirv::convertUniformArrayContent`'s comment in
// SPIRVToLLVMPatterns.cpp). Its dynamic array index reaches
// `llvm.spv.resource.getpointer` directly, exactly as a storage buffer
// array's own does, rather than through the (always compile-time-constant)
// field-selecting index a non-array uniform buffer's own field access
// uses.
TEST(SPIRVResourceLoweringTest,
     LowersUniformBufferArrayDynamicIndexToStrideMultipliedLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 5, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16) %h, i32 %idx)
      %v = load i32, ptr %ptr
      ret i32 %v
    }
    declare target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));

  bool FoundStrideMultiply = false;
  for (Instruction &I : instructions(F))
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      if (BO->getOpcode() == Instruction::Mul)
        if (auto *C = dyn_cast<ConstantInt>(BO->getOperand(1)))
          FoundStrideMultiply = C->getZExtValue() == 16;
  EXPECT_TRUE(FoundStrideMultiply);
}

TEST(SPIRVResourceLoweringTest, LeavesUniformBufferArrayStoreUnchanged) {
  // Vulkan disallows writing `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, exactly
  // like a non-array uniform buffer's own field (see
  // `LeavesUniformBufferStoreUnchanged`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 5, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16) %h, i32 %idx)
      store i32 1, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest,
     LeavesConflictingUniformBufferArrayStrideAtSameIdentityUnchanged) {
  // Two handles at the same (set, binding) identity disagreeing about a
  // uniform buffer array's own stride is a conflicting re-declaration,
  // exactly like two storage buffers disagreeing about theirs. The two
  // `handlefrombinding`/`getpointer` overloads need their real (LLVM-
  // mangled) intrinsic names spelled out here, matching
  // `LeavesConflictingBufferKindAtSameIdentityUnchanged`'s own reason why.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @a(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16)
          @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0i32_2_0_16t(i32 0, i32 5, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer.p0.tspirv.VulkanBuffer_a0i32_2_0_16t.i32(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16) %h, i32 %idx)
      %v = load i32, ptr %ptr
      ret i32 %v
    }
    define i32 @b(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x i32], 2, 0, 32)
          @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0i32_2_0_32t(i32 0, i32 5, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer.p0.tspirv.VulkanBuffer_a0i32_2_0_32t.i32(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 32) %h, i32 %idx)
      %v = load i32, ptr %ptr
      ret i32 %v
    }
    declare target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16)
        @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0i32_2_0_16t(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.p0.tspirv.VulkanBuffer_a0i32_2_0_16t.i32(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16), i32)
    declare target("spirv.VulkanBuffer", [0 x i32], 2, 0, 32)
        @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0i32_2_0_32t(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.p0.tspirv.VulkanBuffer_a0i32_2_0_32t.i32(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 32), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_FALSE(hasResourceLoadCall(*M->getFunction("a")));
  EXPECT_FALSE(hasResourceLoadCall(*M->getFunction("b")));
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

TEST(SPIRVResourceLoweringTest, LowersScalarI32TexelBufferToScalarTypedCalls) {
  // (Roadmap L9) A scalar `i32` load/store -- the shape a single-channel
  // format like R32_UINT/R32_SINT needs (`RWBuffer<int>` in HLSL): SPIR-V's
  // own `OpImageRead`/`OpImageFetch` always return a full 4-component
  // vector regardless of the underlying format's real channel count (see
  // `LowersIntegerStorageTexelBufferToV4I32Calls` above), but `OpImageWrite`
  // takes exactly the shader-declared element shape, a bare scalar here --
  // confirmed via a direct IR reduction (see `isSupportedTexelElementType`'s
  // own comment, SPIRVResourceLowering.cpp). Lowers to the
  // `.i32`-mangled scalar typed calls, distinct from `.v4i32`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main(i32 %idx, i32 %v) {
      %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load i32, ptr %ptr
      store i32 %v, ptr %ptr
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
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.i32"));
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.store.typed.i32"));
  EXPECT_FALSE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.v4i32"));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest, LowersScalarF32TexelBufferToScalarTypedCalls) {
  // The float counterpart of `LowersScalarI32TexelBufferToScalarTypedCalls`
  // above, e.g. `RWBuffer<float>`'s own R32_FLOAT shape.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(i32 %idx, float %v) {
      %h = call target("spirv.Image", float, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", float, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load float, ptr %ptr
      store float %v, ptr %ptr
      ret float %loaded
    }
    declare target("spirv.Image", float, 5, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", float, 5, 0, 0, 0, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.f32"));
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.store.typed.f32"));
  EXPECT_FALSE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.v4f32"));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest, LeavesUnsupportedTexelElementVectorWidthUnchanged) {
  // Only a scalar or a full <4 x T> are supported (see
  // `isSupportedTexelElementType`'s comment) -- neither `dxc` nor glslang
  // ever emits a <2 x T>/<3 x T> texel-buffer access, but a partial vector
  // is still left un-normalized rather than mis-lowered if one somehow
  // reached this pass.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <2 x i32> @main(i32 %idx) {
      %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load <2 x i32>, ptr %ptr
      ret <2 x i32> %loaded
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
  //  u, v, dudx, dudy, dvdx, dvdy, lod, use_explicit_lod, mask).
  EXPECT_EQ(Sample->getArgOperand(0)->getName(), "image_heap");
  EXPECT_EQ(Sample->getArgOperand(2)->getName(), "sampler_heap");
  // Both are the sole binding of their own heap, so both resolve to slot 0.
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(4))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(5))->isZero());
  // `main` here carries no `feme.shader.stage` attribute at all (i.e. it
  // is not recognized as a Fragment-stage entry point), so this implicit
  // sample gets zero-constant derivatives rather than a real
  // `feme.stage.derivative.*` synthesis (roadmap H7i's own Fragment-only
  // gate; see `getOrSynthesizeSample2DDerivatives`).
  for (unsigned ArgNo : {8, 9, 10, 11})
    EXPECT_TRUE(cast<ConstantFP>(Sample->getArgOperand(ArgNo))->isZero());
  // An implicit-LOD sample asks the runtime for level 0, not for `%lod`.
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(13))->isZero());

  // Neither the combined sampled-image struct nor the handles survive.
  for (Instruction &I : instructions(*F))
    EXPECT_FALSE(isa<InsertValueInst>(&I) || isa<ExtractValueInst>(&I));
}

TEST(SPIRVResourceLoweringTest,
     LowersCombinedSampledImageHandleToImageSample) {
  // Roadmap H13d: the shape `ResourceAddressOfPattern` produces for an
  // ordinary GLSL `uniform sampler2D` declaration -- a single
  // `handlefrombinding` call whose own result type is already the combined
  // `{image, sampler}` struct, with no separate `OpSampledImage`/two
  // independently-declared handles for `foldSampledImageStructs` to trace
  // an `insertvalue` chain through at all. Distinct from `SampleShader`
  // above (two handles, composed via `insertvalue`, read apart via
  // `extractvalue`): here the `extractvalue`s read directly off the one
  // combined call.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    %pair = type { target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler") }
    define <4 x float> @main(<2 x float> %coord) {
      %h = call %pair
          @llvm.spv.resource.handlefrombinding.tpair(i32 0, i32 0, i32 1, i32 0, ptr null)
      %i = extractvalue %pair %h, 0
      %s = extractvalue %pair %h, 1
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %i,
          target("spirv.Sampler") %s, <2 x float> %coord, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare %pair
        @llvm.spv.resource.handlefrombinding.tpair(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4f32");
  ASSERT_TRUE(Sample);
  EXPECT_EQ(Sample->getArgOperand(0)->getName(), "image_heap");
  EXPECT_EQ(Sample->getArgOperand(2)->getName(), "sampler_heap");
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(4))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(5))->isZero());

  // Neither the combined handle call nor its `extractvalue`s survive.
  for (Instruction &I : instructions(*F)) {
    EXPECT_FALSE(isa<ExtractValueInst>(&I));
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        EXPECT_NE(Callee->getIntrinsicID(),
                 Intrinsic::spv_resource_handlefrombinding);
  }
}

TEST(SPIRVResourceLoweringTest,
     LeavesCombinedSampledImageHandleWithOtherUseUnchanged) {
  // A combined handle used any other way (here, passed whole to another
  // function) is left entirely alone -- `collectHandles` then declines the
  // whole function, the same honest "leave unmodified rather than
  // partially rewrite" contract every other unsupported shape gets.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    %pair = type { target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler") }
    define void @main() {
      %h = call %pair
          @llvm.spv.resource.handlefrombinding.tpair(i32 0, i32 0, i32 1, i32 0, ptr null)
      call void @consume(%pair %h)
      ret void
    }
    declare void @consume(%pair)
    declare %pair
        @llvm.spv.resource.handlefrombinding.tpair(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCombinedHandle = false;
  for (Instruction &I : instructions(*F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getIntrinsicID() ==
            Intrinsic::spv_resource_handlefrombinding)
          FoundCombinedHandle = true;
  EXPECT_TRUE(FoundCombinedHandle);
}

TEST(SPIRVResourceLoweringTest,
     FragmentStageImplicitSampleSynthesizesRealDerivatives) {
  // Roadmap H7i: the same shape as `SampleShader` above, except `main`
  // now carries a real `feme.shader.stage`="fragment" attribute -- the
  // only stage an implicit-LOD `texture()`/`OpImageSampleImplicitLod` is
  // ever legal from. This must get real `feme.stage.derivative.*` calls
  // synthesized as its new derivative operands, not zero constants.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    %pair = type { target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler") }
    define <4 x float> @main(<2 x float> %coord) #0 {
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
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4f32");
  ASSERT_TRUE(Sample);
  // Argument operands 8-11 are (dudx, dudy, dvdx, dvdy); none may be a
  // plain zero constant now that a real derivative can be synthesized.
  for (unsigned ArgNo : {8, 9, 10, 11}) {
    Value *Deriv = Sample->getArgOperand(ArgNo);
    EXPECT_FALSE(isa<ConstantFP>(Deriv));
    auto *DerivCall = dyn_cast<CallInst>(Deriv);
    ASSERT_TRUE(DerivCall);
    Function *Callee = DerivCall->getCalledFunction();
    ASSERT_TRUE(Callee);
    EXPECT_TRUE(Callee->getName().starts_with("feme.stage.derivative."));
  }
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
  // Roadmap H7b-a widened this pass to accept an arrayed (`Texture2DArray`)
  // image handle, but its fetch coordinate must still carry the array
  // layer as a genuine 3rd component (`<3 x i32>`, see
  // `LowersImageArrayFetchToImageLoadArray` below) -- this handle's own use
  // still names a plain `<2 x i32>` coordinate, so it's left for
  // `checkSupportedRaisedOps` to reject rather than lowered into a helper
  // that would silently ignore the missing layer.
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
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.load.2darray.v4f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

// Roadmap H7b-a: `Texture2DArray`/`TextureCube`/`TextureCubeArray` sampled
// image handles, previously rejected outright by `classifySampledImage2DHandle`
// (every one of them hit `LeavesAnArrayedImageHandleAlone`'s old,
// unconditional-reject behavior), now lower to the corresponding widened
// `feme.cpu.image.*` entry point (see ImageCalls.h/FeMeRuntimeCPU.c).

TEST(SPIRVResourceLoweringTest, LowersSampledImageArrayToImageSampleArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timgarr(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamparr(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timgarr(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamparr(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2darray.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, array_layer, lod, use_explicit_lod, mask).
  EXPECT_EQ(Sample->arg_size(), 12u);
}

TEST(SPIRVResourceLoweringTest, LowersImageArrayFetchToImageLoadArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x i32> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timgarr(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timgarr(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img, <3 x i32> %coord)
      %v = load <4 x float>, ptr %p
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timgarr(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timgarr(
        target("spirv.Image", float, 1, 0, 1, 0, 1, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2darray.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(0)->getName(), "image_heap");
}

TEST(SPIRVResourceLoweringTest, LowersCubeSampledImageToImageSampleCube) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %dir) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timgcube(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsampcube(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %dir, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timgcube(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsampcube(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.cube.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, lod, use_explicit_lod, mask).
  EXPECT_EQ(Sample->arg_size(), 12u);
}

TEST(SPIRVResourceLoweringTest, LowersCubeArraySampledImageToImageSampleCubeArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<4 x float> %dirandlayer) {
      %img = call target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timgcubearr(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsampcubearr(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 3, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %dirandlayer, <4 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timgcubearr(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsampcubearr(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample =
      findImageCall(*F, "feme.cpu.image.sample.cubearray.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, array_layer, lod, use_explicit_lod, mask).
  EXPECT_EQ(Sample->arg_size(), 13u);
}

TEST(SPIRVResourceLoweringTest, LeavesACubeImageFetchAlone) {
  // `OpImageFetch` is illegal against `Dim::Cube` in SPIR-V -- no real
  // shader can produce this, but this pass still must not silently accept
  // it if fed one (e.g. from a hand-written or fuzzed test module).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %coord) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timgcubefetch(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timgcubefetch(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img, <2 x i32> %coord)
      %v = load <4 x float>, ptr %p
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timgcubefetch(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timgcubefetch(
        target("spirv.Image", float, 3, 0, 0, 0, 1, 0), <2 x i32>)
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

// Roadmap H19a: a plain, non-arrayed, non-multisampled *storage* image
// handle (`Sampled == 2`, no sampler), previously rejected outright by
// `classifySampledImage2DHandle`, now classifies as `HandleKind::
// StorageImage2D` and lowers `OpImageWrite`/`OpImageRead` to
// `feme.cpu.image.store.2d.*`/`load.2d.*`. Roadmap H19b below widens this
// to an arrayed storage image, lowering to `.store.2darray.*`/
// `.load.2darray.*` instead.

TEST(SPIRVResourceLoweringTest, LowersStorageImageWriteToImageStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 0, 0, 2, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Store = findImageCall(*F, "feme.cpu.image.store.2d.v4f32");
  ASSERT_TRUE(Store);
  EXPECT_EQ(Store->getArgOperand(0)->getName(), "image_heap");
  EXPECT_TRUE(Store->getType()->isVoidTy());
  // A storage image binding still reserves a slot in the image heap, not
  // the (buffer-oriented) resource heap.
  NamedMDNode *Resources = M->getNamedMetadata("feme.cpu.resources");
  ASSERT_TRUE(Resources);
  EXPECT_EQ(mdInt(Resources->getOperand(0), 2), 0u);
}

TEST(SPIRVResourceLoweringTest, LowersIntegerStorageImageWriteToImageStoreV4I32) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord, <4 x i32> %texel) {
      %img = call target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord)
      store <4 x i32> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 1, 0, 0, 0, 2, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2d.v4i32"));
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.store.2d.v4f32"));
}

TEST(SPIRVResourceLoweringTest, LowersStorageImageLoadStoreToBothCalls) {
  // The `dEQP-VK.image.load_store` CTS group's own copy-shader idiom: the
  // same handle, and the same `getpointer` call, is both loaded from and
  // stored to.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord)
      %v = load <4 x float>, ptr %p
      store <4 x float> %v, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 0, 0, 2, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.load.2d.v4f32"));
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2d.v4f32"));
}

TEST(SPIRVResourceLoweringTest, LowersArrayedStorageImageWriteToImageStoreArray) {
  // Roadmap H19b: an arrayed (`Arrayed == 1`) storage image handle
  // (`Sampled == 2`) now classifies as `HandleKind::StorageImage2D` with
  // `ImageShape::Array2D`, and its `OpImageWrite` (a `store` through
  // `llvm.spv.resource.getpointer` on a 3-component `(x, y, layer)`
  // coordinate) lowers to `feme.cpu.image.store.2darray.v4f32`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 1, 0, 2, 0) %img, <3 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 1, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerArrayedStorageImageWriteToImageStoreArrayV4I32) {
  // The integer-format counterpart: `OpTypeImage` with an integer sampled
  // type lowers to `feme.cpu.image.store.2darray.v4i32` instead.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x i32> %texel) {
      %img = call target("spirv.Image", i32, 1, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 1, 0, 1, 0, 2, 0) %img, <3 x i32> %coord)
      store <4 x i32> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", i32, 1, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 1, 0, 1, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darray.v4i32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersArrayedStorageImageLoadStoreToBothCalls) {
  // An arrayed storage image handle used for both a load (`OpImageRead`)
  // and a store (`OpImageWrite`) lowers each independently to the arrayed
  // helper pair.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 1, 0, 2, 0) %img, <3 x i32> %coord)
      %v = load <4 x float>, ptr %p
      store <4 x float> %v, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 1, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.load.2darray.v4f32"));
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersArrayedMultisampledStorageImageWriteToImageStoreArrayMSV4F32) {
  // Roadmap H19m: a multisampled (`MS == 1`) *arrayed* (`Arrayed == 1`)
  // storage image handle now classifies as `HandleKind::StorageImage2D`
  // with `ImageShape::Array2DMS`, whose 4-component `(x, y, layer,
  // sample)` coordinate lowers an `OpImageWrite` to
  // `feme.cpu.image.store.2darrayms.v4f32` -- a new, dedicated call kind,
  // since `Store2DArray`/`Store2DMS` each carry only one of `Layer`/
  // `Sample`, never both (see `ImageCalls.h`'s own `Store2DArrayMS`
  // comment). Previously (before this row) this exact handle shape was
  // rejected outright.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<4 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 1, 0, 1, 1, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 1, 1, 2, 0) %img, <4 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 1, 0, 1, 1, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 1, 1, 2, 0), <4 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darrayms.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerArrayedMultisampledStorageImageWriteToImageStoreArrayMSV4I32) {
  // The integer-format counterpart: `OpTypeImage` with an integer sampled
  // type lowers to `feme.cpu.image.store.2darrayms.v4i32` instead.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<4 x i32> %coord, <4 x i32> %texel) {
      %img = call target("spirv.Image", i32, 1, 0, 1, 1, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 1, 0, 1, 1, 2, 0) %img, <4 x i32> %coord)
      store <4 x i32> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", i32, 1, 0, 1, 1, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 1, 0, 1, 1, 2, 0), <4 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darrayms.v4i32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersArrayedMultisampledStorageImageLoadStoreToBothCalls) {
  // An arrayed multisampled storage image handle used for both a load
  // (`OpImageRead`) and a store (`OpImageWrite`) lowers each independently:
  // the load reuses `Load2DArray`'s own vocabulary (its `Sample` operand,
  // previously always `getInt32(0)`, now carries the real per-sample
  // component), while the store uses the new `Store2DArrayMS` kind.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<4 x i32> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 1, 1, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 1, 1, 2, 0) %img, <4 x i32> %coord)
      %v = load <4 x float>, ptr %p
      store <4 x float> %v, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 1, 0, 1, 1, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 1, 1, 2, 0), <4 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.load.2darray.v4f32"));
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darrayms.v4f32"));
}

// Roadmap H19g: a plain (non-arrayed) multisampled 2D storage image handle
// (`Dim == Dim2D`, `MS == 1`, `Arrayed == 0`) now classifies as
// `HandleKind::StorageImage2D` with `ImageShape::Plain2DMS`, whose
// 3-component `(x, y, sample)` coordinate -- structurally identical to
// `Array2D`'s own `(x, y, layer)` from this pass's own perspective -- lowers
// an `OpImageWrite` to `feme.cpu.image.store.2dms.v4f32` and an
// `OpImageRead` to `feme.cpu.image.load.2d.v4f32` (reusing `Plain2D`'s own
// load call, since `Load2D`'s vocabulary already carries a `Sample`
// parameter from roadmap F8c).

TEST(SPIRVResourceLoweringTest,
     LowersPlain2DMultisampledStorageImageWriteToImageStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 1, 0, 0, 1, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 1, 2, 0) %img, <3 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 1, 0, 0, 1, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 0, 1, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2dms.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersPlain2DMultisampledStorageImageWriteToImageStoreI32) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x i32> %texel) {
      %img = call target("spirv.Image", i32, 1, 0, 0, 1, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 1, 0, 0, 1, 2, 0) %img, <3 x i32> %coord)
      store <4 x i32> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", i32, 1, 0, 0, 1, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 1, 0, 0, 1, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2dms.v4i32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersPlain2DMultisampledStorageImageReadToImageLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x i32> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 1, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 1, 2, 0) %img, <3 x i32> %coord)
      %v = load <4 x float>, ptr %p
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 1, 0, 0, 1, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 1, 0, 0, 1, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.load.2d.v4f32"));
}

// Roadmap H19c: a plain (non-arrayed) 1D storage image handle (`Dim ==
// Dim1D == 0`) now classifies as `HandleKind::StorageImage2D` with
// `ImageShape::Plain1D`, and its `OpImageWrite` lowers to
// `feme.cpu.image.store.1d.v4f32`. Per the SPIR-V spec, a 1D image's own
// fetch coordinate is a bare scalar `i32`, not a 1-element vector, unlike
// every other shape this file tests -- `%coord` below is `i32`, not
// `<1 x i32>`.

TEST(SPIRVResourceLoweringTest, LowersPlain1DStorageImageWriteToImageStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 0, 0, 0, 0, 2, 0) %img, i32 %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 0, 0, 0, 0, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.1d.v4f32"));
}

TEST(SPIRVResourceLoweringTest, LowersPlain1DStorageImageLoadStoreToBothCalls) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %coord) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 0, 0, 0, 0, 2, 0) %img, i32 %coord)
      %v = load <4 x float>, ptr %p
      store <4 x float> %v, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 0, 0, 0, 0, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.load.1d.v4f32"));
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.1d.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerPlain1DStorageImageWriteToImageStoreV4I32) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %coord, <4 x i32> %texel) {
      %img = call target("spirv.Image", i32, 0, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 0, 0, 0, 0, 2, 0) %img, i32 %coord)
      store <4 x i32> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", i32, 0, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 0, 0, 0, 0, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.1d.v4i32"));
}

TEST(SPIRVResourceLoweringTest, LowersArray1DStorageImageWriteToImageStoreArray) {
  // Roadmap H19e: an arrayed 1D storage image handle (`Dim == Dim1D == 0`,
  // `Arrayed == 1`) now classifies as `HandleKind::StorageImage2D` with
  // `ImageShape::Array1D`, and its `OpImageWrite` (a `store` through
  // `llvm.spv.resource.getpointer` on a 2-component `(x, layer)`
  // coordinate) lowers to `feme.cpu.image.store.1darray.v4f32`. This was
  // previously rejected outright (H19c only added a plain, non-arrayed 1D
  // shape); H19e closes that gap.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 0, 0, 1, 0, 2, 0) %img, <2 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 0, 0, 1, 0, 2, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.1darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerArray1DStorageImageWriteToImageStoreArrayV4I32) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord, <4 x i32> %texel) {
      %img = call target("spirv.Image", i32, 0, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 0, 0, 1, 0, 2, 0) %img, <2 x i32> %coord)
      store <4 x i32> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", i32, 0, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 0, 0, 1, 0, 2, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.1darray.v4i32"));
}

TEST(SPIRVResourceLoweringTest, LowersArray1DStorageImageLoadStoreToBothCalls) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 0, 0, 1, 0, 2, 0) %img, <2 x i32> %coord)
      %v = load <4 x float>, ptr %p
      store <4 x float> %v, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 0, 0, 1, 0, 2, 0), <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.load.1darray.v4f32"));
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.1darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest, LeavesAnArrayed3DStorageImageHandleAlone) {
  // Arrayed 3D is illegal in SPIR-V and must still be rejected --
  // `classifyStorageImage2DHandle`'s only remaining rejection after
  // H19e's own widening.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 2, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 2, 0, 1, 0, 2, 0) %img, <3 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 2, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 2, 0, 1, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.store.3d.v4f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

// Roadmap H19c: a plain 3D storage image handle (`Dim == Dim3D == 2`,
// never arrayed -- SPIR-V disallows an arrayed `Dim::3D` image) now
// classifies as `HandleKind::StorageImage2D` with `ImageShape::Plain3D`,
// and its `OpImageWrite` (a 3-component `(x, y, z)` coordinate) lowers to
// `feme.cpu.image.store.3d.v4f32`.

TEST(SPIRVResourceLoweringTest, LowersPlain3DStorageImageWriteToImageStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 2, 0, 0, 0, 2, 0) %img, <3 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 2, 0, 0, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.3d.v4f32"));
}

TEST(SPIRVResourceLoweringTest, LowersPlain3DStorageImageLoadStoreToBothCalls) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 2, 0, 0, 0, 2, 0) %img, <3 x i32> %coord)
      %v = load <4 x float>, ptr %p
      store <4 x float> %v, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 2, 0, 0, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.load.3d.v4f32"));
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.3d.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerPlain3DStorageImageWriteToImageStoreV4I32) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x i32> %texel) {
      %img = call target("spirv.Image", i32, 2, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 2, 0, 0, 0, 2, 0) %img, <3 x i32> %coord)
      store <4 x i32> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", i32, 2, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 2, 0, 0, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.3d.v4i32"));
}

// Roadmap H19d: a cube storage image (`Dim == DimCube == 3`) now
// classifies as `HandleKind::StorageImage2D` with `ImageShape::Array2D`
// (not a distinct `Cube` shape) -- confirmed via a real CTS shader dump
// that a storage cube image's `imageLoad`/`imageStore` addresses its
// texel by an ordinary `(x, y, face)` triple, structurally identical to
// `Array2D`'s own `(x, y, layer)` triple, unlike a *sampled* cube's
// direction-vector addressing. So its `OpImageWrite` lowers to the same
// `feme.cpu.image.store.2darray.v4f32` an arrayed 2D storage image already
// uses (roadmap H19b), with no new call vocabulary or runtime helper
// needed.

TEST(SPIRVResourceLoweringTest, LowersCubeStorageImageWriteToImageStoreArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 3, 0, 0, 0, 2, 0) %img, <3 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 3, 0, 0, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerCubeStorageImageWriteToImageStoreArrayV4I32) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x i32> %texel) {
      %img = call target("spirv.Image", i32, 3, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", i32, 3, 0, 0, 0, 2, 0) %img, <3 x i32> %coord)
      store <4 x i32> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", i32, 3, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", i32, 3, 0, 0, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darray.v4i32"));
}

TEST(SPIRVResourceLoweringTest, LowersCubeStorageImageLoadStoreToBothCalls) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 3, 0, 0, 0, 2, 0) %img, <3 x i32> %coord)
      %v = load <4 x float>, ptr %p
      store <4 x float> %v, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 3, 0, 0, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.load.2darray.v4f32"));
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersCubeArrayStorageImageWriteToImageStoreArray) {
  // `Dim == DimCube` with `Arrayed == 1` (`imageCubeArray`'s own
  // already-flattened `layer * 6 + face` coordinate) lowers exactly the
  // same way a non-arrayed cube storage image does above -- `Arrayed`
  // does not change the coordinate shape here, only what value ends up
  // in its third component.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 3, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 3, 0, 1, 0, 2, 0) %img, <3 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 3, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 3, 0, 1, 0, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.store.2darray.v4f32"));
}

// Roadmap L20: a whole-struct load/store off a resource pointer (no
// `getelementptr` navigating into an individual field at all) is
// decomposed into one raw call per leaf field/element, reassembled with
// `insertvalue`/`extractvalue`, rather than left for `UnsupportedOps.cpp`'s
// generic diagnostic. The struct here mirrors
// `Feature/StructuredBuffer/packed.test`'s own `Doggo` struct
// (`int3 Legs; int TailState; int2 Ears;`), whose two vector fields
// convert to fixed-size LLVM *arrays* (not LLVM vectors) at the
// SPIR-V-to-LLVM layer once nested inside a tightly-packed struct -- this
// test's own array-typed fields are deliberately not `FixedVectorType`s,
// to cover that exact nested-array shape (not just a bare scalar struct).
TEST(SPIRVResourceLoweringTest,
     LowersWholeStructLoadAndStoreWithArrayFieldsToPerLeafRawCalls) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer",
              [0 x {[3 x i32], i32, [2 x i32]}], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer",
              [0 x {[3 x i32], i32, [2 x i32]}], 12, 1) %h, i32 %idx)
      %v = load {[3 x i32], i32, [2 x i32]}, ptr %ptr
      store {[3 x i32], i32, [2 x i32]} %v, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer",
        [0 x {[3 x i32], i32, [2 x i32]}], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer",
        [0 x {[3 x i32], i32, [2 x i32]}], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);

  // One raw load/store per leaf field: `Legs[0..2]`, `TailState`,
  // `Ears[0..1]` -- 3 + 1 + 2 = 6 of each, none referencing the struct
  // type itself (no `UnsupportedOps` fallback, no surviving handle).
  unsigned NumLoads = 0, NumStores = 0;
  for (Instruction &I : instructions(*F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    Function *Callee = CI->getCalledFunction();
    if (!Callee)
      continue;
    if (Callee->getName().starts_with("feme.cpu.resource.load.raw"))
      ++NumLoads;
    else if (Callee->getName().starts_with("feme.cpu.resource.store.raw"))
      ++NumStores;
  }
  EXPECT_EQ(NumLoads, 6u);
  EXPECT_EQ(NumStores, 6u);
  EXPECT_TRUE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LeavesAMultisampledCubeStorageImageHandleAlone) {
  // A multisampled cube storage image is still rejected -- H19d only
  // widens the `Dim` axis to accept `DimCube`, not `MS` (roadmap H19g's
  // scope).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, <4 x float> %texel) {
      %img = call target("spirv.Image", float, 3, 0, 0, 1, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 3, 0, 0, 1, 2, 0) %img, <3 x i32> %coord)
      store <4 x float> %texel, ptr %p
      ret void
    }
    declare target("spirv.Image", float, 3, 0, 0, 1, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg(
        target("spirv.Image", float, 3, 0, 0, 1, 2, 0), <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.store.2darray.v4f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}


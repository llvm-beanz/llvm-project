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

// Roadmap L124(e): a storage-buffer store of a struct value containing one
// of `layOutStructIfOffsetsMatch`'s own synthetic `[N x i8]` alignment-gap
// members (SPIRVToLLVMPatterns.cpp's own std140/std430 padding-insertion
// mechanism -- e.g. a `mat2`'s own 8-byte column padding under std140,
// exactly the shape `dEQP-VK.ssbo.layout.single_basic_type.std140.
// column_major_highp_mat2` produces) must not recurse into that gap
// member and emit a `feme.cpu.resource.store.raw.i8` call per padding
// byte: the gap member is never assigned a real value by any
// `insertvalue` reaching this store (it stays exactly the `poison` value
// the surrounding struct literal started as), so `lowerRawStore` must
// recognize the whole gap member's value is `poison` and skip storing it
// entirely -- both because writing an unspecified byte pattern into
// padding no SPIR-V-visible load can ever observe is a pure no-op, and
// because (this row's own original motivation) doing so would otherwise
// require a `feme.cpu.resource.store.raw.i8` runtime entry point that
// does not exist purely to write throwaway padding.
TEST(SPIRVResourceLoweringTest, SkipsRawStoreOfPoisonAlignmentGapMember) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x <{ [2 x float], [8 x i8] }>], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <{ [2 x float], [8 x i8] }>], 12, 1) %h, i32 %idx)
      %v = insertvalue <{ [2 x float], [8 x i8] }> poison, [2 x float] [float 1.0, float 2.0], 0
      store <{ [2 x float], [8 x i8] }> %v, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x <{ [2 x float], [8 x i8] }>], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <{ [2 x float], [8 x i8] }>], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  unsigned NumF32Stores = 0;
  unsigned NumI8Stores = 0;
  for (Instruction &I : instructions(*F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction()) {
        StringRef Name = Callee->getName();
        if (Name == "feme.cpu.resource.store.raw.f32")
          ++NumF32Stores;
        else if (Name == "feme.cpu.resource.store.raw.i8")
          ++NumI8Stores;
      }
  EXPECT_EQ(NumF32Stores, 2u);
  EXPECT_EQ(NumI8Stores, 0u);
}

// Roadmap H138: `<4 x i64>` (32 bytes) exceeds this target's 16-byte
// direct-value-ABI threshold (roadmap H137's own closing note), so
// `lowerRawStore` must decompose it into two `v2i64` stores rather than
// ever mangling a call name for the whole 4-wide vector -- see that
// function's own comment for why calling a hypothetical `v4i64` helper
// directly would silently produce wrong results instead of a safe link
// failure.
TEST(SPIRVResourceLoweringTest, LowersV4I64RawStoreToTwoV2I64Stores) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, <4 x i64> %v) {
      %h = call target("spirv.VulkanBuffer", [0 x <4 x i64>], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <4 x i64>], 12, 1) %h, i32 %idx)
      store <4 x i64> %v, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x <4 x i64>], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <4 x i64>], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  unsigned NumV2I64Stores = 0;
  unsigned NumOtherStores = 0;
  for (Instruction &I : instructions(*F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction()) {
        StringRef Name = Callee->getName();
        if (!Name.starts_with("feme.cpu.resource.store.raw"))
          continue;
        if (Name == "feme.cpu.resource.store.raw.v2i64")
          ++NumV2I64Stores;
        else
          ++NumOtherStores;
      }
  EXPECT_EQ(NumV2I64Stores, 2u);
  EXPECT_EQ(NumOtherStores, 0u);
}

// Roadmap H138: the odd-width sibling of the test above -- `<3 x i64>`
// (24 bytes) still exceeds the 16-byte threshold, decomposing into one
// `v2i64` store for the first two elements and one scalar `i64` store for
// the trailing element.
TEST(SPIRVResourceLoweringTest, LowersV3I64RawStoreToV2I64AndScalarI64Store) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, <3 x i64> %v) {
      %h = call target("spirv.VulkanBuffer", [0 x <3 x i64>], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <3 x i64>], 12, 1) %h, i32 %idx)
      store <3 x i64> %v, ptr %ptr
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x <3 x i64>], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <3 x i64>], 12, 1), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  unsigned NumV2I64Stores = 0;
  unsigned NumI64Stores = 0;
  for (Instruction &I : instructions(*F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction()) {
        StringRef Name = Callee->getName();
        if (Name == "feme.cpu.resource.store.raw.v2i64")
          ++NumV2I64Stores;
        else if (Name == "feme.cpu.resource.store.raw.i64")
          ++NumI64Stores;
      }
  EXPECT_EQ(NumV2I64Stores, 1u);
  EXPECT_EQ(NumI64Stores, 1u);
}

// Roadmap H138: the load-side mirror of the two tests above -- `<4 x i64>`
// decomposes into two `v2i64` loads, reassembled with `insertelement`
// rather than a single `v4i64` call.
TEST(SPIRVResourceLoweringTest, LowersV4I64RawLoadToTwoV2I64Loads) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i64> @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x <4 x i64>], 12, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <4 x i64>], 12, 0) %h, i32 %idx)
      %v = load <4 x i64>, ptr %ptr
      ret <4 x i64> %v
    }
    declare target("spirv.VulkanBuffer", [0 x <4 x i64>], 12, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <4 x i64>], 12, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  unsigned NumV2I64Loads = 0;
  unsigned NumOtherLoads = 0;
  for (Instruction &I : instructions(*F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction()) {
        StringRef Name = Callee->getName();
        if (!Name.starts_with("feme.cpu.resource.load.raw"))
          continue;
        if (Name == "feme.cpu.resource.load.raw.v2i64")
          ++NumV2I64Loads;
        else
          ++NumOtherLoads;
      }
  EXPECT_EQ(NumV2I64Loads, 2u);
  EXPECT_EQ(NumOtherLoads, 0u);
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

// (Roadmap H128) A *nested* uniform buffer array -- e.g. a real
// `uniform Block { uint data[3][4]; }` (`dEQP-VK.ubo.2_level_array.*`) --
// converts its outer dimension into `getpointer`'s own index exactly like
// `LowersUniformBufferArrayDynamicIndexToStrideMultipliedLoad` above, but
// then needs a further `getelementptr` navigating into the inner
// dimension, mirroring the real IR `feme-translate --import-spirv` +
// `feme-opt --feme-convert-spirv-to-llvm` produces for such a shape. This
// GEP was previously rejected outright (see `hasOnlySupportedUses`'s
// `AllowGEPs` comment), causing every `2_level_array`/`3_level_array` case
// to fail with `UnsupportedOps.cpp`'s generic "cannot normalize"
// diagnostic even though this lowering path handles the GEP generically.
TEST(SPIRVResourceLoweringTest,
     LowersNestedUniformBufferArrayIndexToStrideMultipliedLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main(i32 %outer, i32 %inner) {
      %h = call target("spirv.VulkanBuffer", [0 x [4 x i32]], 2, 0, 16)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 5, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x [4 x i32]], 2, 0, 16) %h, i32 %outer)
      %elt = getelementptr inbounds [4 x i32], ptr %ptr, i32 0, i32 %inner
      %v = load i32, ptr %elt
      ret i32 %v
    }
    declare target("spirv.VulkanBuffer", [0 x [4 x i32]], 2, 0, 16)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x [4 x i32]], 2, 0, 16), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
  // The GEP navigating the inner dimension should have been consumed
  // (lowered away), not left behind unresolved.
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<GetElementPtrInst>(&I));
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

TEST(SPIRVResourceLoweringTest,
    LowersUniformTexelBufferGetDimensionsToTypedCall) {
  // Roadmap H144: `Buffer<T>::GetDimensions(uint)` on a uniform (read-only)
  // texel buffer lowers to a bare `llvm.spv.resource.getdimensions.x` call
  // directly on the handle -- no `getpointer` indirection at all, unlike
  // every load/store shape the tests above cover -- and should rewrite to
  // `feme.cpu.resource.getdimensions.typed.i32`, with the handle itself
  // erased same as any other fully-lowered access.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %h = call target("spirv.Image", i32, 5, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %dim = call i32 @llvm.spv.resource.getdimensions.x(
          target("spirv.Image", i32, 5, 0, 0, 0, 1, 0) %h)
      ret i32 %dim
    }
    declare target("spirv.Image", i32, 5, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare i32 @llvm.spv.resource.getdimensions.x(target("spirv.Image", i32, 5, 0, 0, 0, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(
      hasResourceTypedCall(*F, "feme.cpu.resource.getdimensions.typed"));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
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

TEST(SPIRVResourceLoweringTest,
     LeavesUnsupportedTexelElementVectorWidthUnchanged) {
  // Only a scalar, a genuine <2 x T> (roadmap L7a), or a full <4 x T> are
  // supported (see `isSupportedTexelElementType`'s comment) -- SPIR-V has
  // no 3-channel storage texel buffer format for `dxc`/glslang to ever
  // target, so a <3 x T> is still left un-normalized rather than
  // mis-lowered if one somehow reached this pass.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <3 x i32> @main(i32 %idx) {
      %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load <3 x i32>, ptr %ptr
      ret <3 x i32> %loaded
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

TEST(SPIRVResourceLoweringTest, LowersV2I32TexelBufferToV2TypedCalls) {
  // (Roadmap L7a) A genuine <2 x i32> load/store -- the shape a 2-channel
  // format like R32G32_UINT/R32G32_SINT needs (`RWBuffer<int2>` in HLSL).
  // Confirmed reachable via a real IR reduction of
  // `Basic/Matrix/matrix_m-based_getter.test`'s own `RWBuffer<float2>
  // OutVec2` write (see `isSupportedTexelElementType`'s own comment) --
  // this project's prior "never narrower than 4 (or exactly 1)" assumption
  // was wrong for width 2. Lowers to the `.v2i32`-mangled typed calls,
  // distinct from both `.i32` and `.v4i32`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <2 x i32> @main(i32 %idx, <2 x i32> %v) {
      %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load <2 x i32>, ptr %ptr
      store <2 x i32> %v, ptr %ptr
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
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.v2i32"));
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.store.typed.v2i32"));
  EXPECT_FALSE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.i32"));
  EXPECT_FALSE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.v4i32"));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
}

TEST(SPIRVResourceLoweringTest, LowersV2F32TexelBufferToV2TypedCalls) {
  // The float counterpart of `LowersV2I32TexelBufferToV2TypedCalls` above,
  // e.g. `RWBuffer<float2>`'s own R32G32_FLOAT shape -- the exact case
  // `Basic/Matrix/matrix_m-based_getter.test`'s own real IR reduction hit.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <2 x float> @main(i32 %idx, <2 x float> %v) {
      %h = call target("spirv.Image", float, 5, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.Image", float, 5, 0, 0, 0, 2, 0) %h, i32 %idx)
      %loaded = load <2 x float>, ptr %ptr
      store <2 x float> %v, ptr %ptr
      ret <2 x float> %loaded
    }
    declare target("spirv.Image", float, 5, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", float, 5, 0, 0, 0, 2, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.v2f32"));
  EXPECT_TRUE(hasResourceTypedCall(*F, "feme.cpu.resource.store.typed.v2f32"));
  EXPECT_FALSE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.f32"));
  EXPECT_FALSE(hasResourceTypedCall(*F, "feme.cpu.resource.load.typed.v4f32"));
  EXPECT_FALSE(M->getFunction("llvm.spv.resource.handlefrombinding"));
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

TEST(SPIRVResourceLoweringTest, LowersCombinedSampledImageHandleToImageSample) {
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

// Regression test for roadmap L108: a `Dim::SubpassData` handle (a GLSL
// `subpassInput` variable's own `handlefrombinding`) is never one of the
// kinds `collectHandles` classifies, but its `OpImageRead` always converts
// straight to `feme.stage.subpass.load` without ever referencing this
// handle's own result -- so it is always dead. A real fragment shader (see
// `dEQP-VK.pipeline.pipeline_library.graphics_library.
// independent_sets_random.*.vert_frag.*`) can freely mix a dead subpass
// input handle alongside a perfectly ordinary, otherwise-normalizable
// buffer binding in the same function; the whole function must not be
// declined over the former, unlike `LeavesCombinedSampledImageHandleWithOt
// herUseUnchanged` above, whose unsupported handle actually has a use.
TEST(SPIRVResourceLoweringTest,
     LowersOrdinaryHandleWhenAnUnusedSubpassInputHandleSharesTheFunction) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %ptr = call ptr
          @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
      %v = load float, ptr %ptr
      %subpass = call target("spirv.Image", float, 6, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_6_0_0_0_2_0t(
              i32 1, i32 2, i32 1, i32 0, ptr null)
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x float], 12, 1)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32)
    declare target("spirv.Image", float, 6, 0, 0, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_6_0_0_0_2_0t(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasResourceLoadCall(*F));
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

TEST(SPIRVResourceLoweringTest, LowersImageFetchLevelToImageLoad) {
  // Roadmap L72: GLSL's `texelFetch(sampler2D, coord, lod)` always
  // supplies an explicit LOD, which `feme::spirv::ImageFetchLodPattern`
  // raises to `llvm.spv.resource.load.level` rather than the zero-mip
  // `getpointer` shape `LowersImageFetchToImageLoad` above covers -- a
  // distinct intrinsic this pass previously never recognized at all,
  // rejecting every real `texelFetch()` call against a sampled image.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %coord, i32 %lod) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x float> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord,
          i32 %lod, <2 x i32> zeroinitializer)
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x float> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", float, 1, 0, 0, 0, 1, 0), <2 x i32>, i32, <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2d.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(0)->getName(), "image_heap");
  // The caller's own real LOD threads through as the `Mip` operand,
  // rather than a hardcoded zero constant the way the `getpointer`-based
  // fetch path always passes.
  EXPECT_EQ(Load->getArgOperand(5)->getName(), "lod");
  // A fetch takes no sampler, so nothing reports sampler-heap usage.
  NamedMDNode *Resources = M->getNamedMetadata("feme.cpu.resources");
  ASSERT_TRUE(Resources);
  EXPECT_EQ(mdInt(Resources->getOperand(0), 2), 0u);
}

TEST(SPIRVResourceLoweringTest, LowersIntegerImageFetchLevelToImageLoadV4I32) {
  // Roadmap L72: mirrors `LowersIntegerImageFetchToImageLoadV4I32`'s own
  // integer-channel distinction for the new `load.level` shape.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(<2 x i32> %coord, i32 %lod) {
      %img = call target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x i32> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", i32, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord,
          i32 %lod, <2 x i32> zeroinitializer)
      ret <4 x i32> %v
    }
    declare target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x i32> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", i32, 1, 0, 0, 0, 1, 0), <2 x i32>, i32, <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2d.v4i32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(5)->getName(), "lod");
}

TEST(SPIRVResourceLoweringTest, LowersImageArrayFetchLevelToImageLoadArray) {
  // Roadmap L72: `Array2D`'s own counterpart to
  // `LowersImageFetchLevelToImageLoad` -- the coordinate's 3rd component
  // is the array layer, threaded through as `Load2DArray`'s own `Layer`
  // operand alongside the real `Mip`, mirroring
  // `LowersImageArrayFetchToImageLoadArray`'s identical zero-mip
  // precedent for the `getpointer`-based fetch path.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x i32> %coord, i32 %lod) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x float> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img, <3 x i32> %coord,
          i32 %lod, <3 x i32> zeroinitializer)
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x float> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", float, 1, 0, 1, 0, 1, 0), <3 x i32>, i32, <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2darray.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(6)->getName(), "lod");
}

TEST(SPIRVResourceLoweringTest, LowersAFetchLevelWithNonzeroOffsetToImageLoad) {
  // Roadmap L72(b): GLSL's `texelFetchOffset(sampler2D, coord, lod,
  // offset)` -- confirmed via a real `deqp-vk` SPIR-V capture to raise to
  // `llvm.spv.resource.load.level` with a real, nonzero `ConstOffset` as
  // its fourth operand (`feme::spirv::ImageFetchLodPattern` used to reject
  // this combination outright during legalization instead). Neither
  // `createLoad2D` nor its `v4i32` counterpart takes an offset operand of
  // its own, so the real offset is folded into the coordinate itself
  // before the runtime call, rather than threaded through as a separate
  // argument the way an ordinary sample's own `OffsetX`/`OffsetY` are.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %coord, i32 %lod) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x float> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord,
          i32 %lod, <2 x i32> <i32 1, i32 -2>)
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x float> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", float, 1, 0, 0, 0, 1, 0), <2 x i32>, i32, <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2d.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(5)->getName(), "lod");
  // The X/Y coordinate operands are each an `add` of the original
  // coordinate lane with the matching offset constant, not the bare
  // coordinate lane itself.
  auto *X = dyn_cast<BinaryOperator>(Load->getArgOperand(3));
  ASSERT_TRUE(X);
  EXPECT_EQ(X->getOpcode(), Instruction::Add);
  auto *Y = dyn_cast<BinaryOperator>(Load->getArgOperand(4));
  ASSERT_TRUE(Y);
  EXPECT_EQ(Y->getOpcode(), Instruction::Add);
}

TEST(SPIRVResourceLoweringTest, LowersPlain1DFetchLevelToImageLoad) {
  // Roadmap L72(c): `Plain1D`'s own counterpart to
  // `LowersImageFetchLevelToImageLoad` -- widening `isFetchLevelIntrinsic`'s
  // shape gate beyond `Plain2D`/`Array2D`, confirmed via a real re-run of
  // roadmap L72's own 1,375-case caselist to have every remaining "cannot
  // normalize" failure be exactly a `texelfetch.*1d*`/`texelfetch.*3d*`
  // variant. `Plain1D`'s own coordinate is a bare scalar `i32`, not a
  // 1-element vector (see `isCoordN`'s own comment), so no
  // `CreateExtractElement` is needed before threading it (and the real
  // `Lod`) through to `createLoad1D`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(i32 %coord, i32 %lod) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x float> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img, i32 %coord,
          i32 %lod, i32 0)
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x float> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", float, 0, 0, 0, 0, 1, 0), i32, i32, i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.1d.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(4)->getName(), "lod");
}

TEST(SPIRVResourceLoweringTest,
     LowersAPlain1DFetchLevelWithNonzeroOffsetToImageLoad) {
  // Roadmap L72(c): `Plain1D`'s own counterpart to
  // `LowersAFetchLevelWithNonzeroOffsetToImageLoad` -- `Plain1D`'s own
  // `ConstOffset` is a bare scalar `i32` (mirroring `isSupportedOffset`'s
  // own `Is1D` acceptance), folded directly into the lone `X` coordinate
  // via a plain `add` rather than an `extractelement`-then-`add` pair.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(i32 %coord, i32 %lod) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x float> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img, i32 %coord,
          i32 %lod, i32 3)
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x float> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", float, 0, 0, 0, 0, 1, 0), i32, i32, i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.1d.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(4)->getName(), "lod");
  auto *X = dyn_cast<BinaryOperator>(Load->getArgOperand(3));
  ASSERT_TRUE(X);
  EXPECT_EQ(X->getOpcode(), Instruction::Add);
}

TEST(SPIRVResourceLoweringTest, LowersArray1DFetchLevelToImageLoadArray) {
  // Roadmap L72(c): `Array1D`'s own counterpart to
  // `LowersImageArrayFetchLevelToImageLoadArray` -- the 2-wide coordinate's
  // 2nd component is the array layer, threaded through as
  // `Load1DArray`'s own `Layer` operand untouched by any real offset,
  // alongside the real `Mip`, mirroring `Array2D`'s identical precedent.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %coord, i32 %lod) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x float> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img, <2 x i32> %coord,
          i32 %lod, i32 0)
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x float> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", float, 0, 0, 1, 0, 1, 0), <2 x i32>, i32, i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.1darray.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(5)->getName(), "lod");
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerPlain1DFetchLevelToImageLoadV4I32) {
  // Roadmap L72(c): mirrors `LowersIntegerImageFetchLevelToImageLoadV4I32`'s
  // own integer-channel distinction for `Plain1D`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(i32 %coord, i32 %lod) {
      %img = call target("spirv.Image", i32, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x i32> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", i32, 0, 0, 0, 0, 1, 0) %img, i32 %coord,
          i32 %lod, i32 0)
      ret <4 x i32> %v
    }
    declare target("spirv.Image", i32, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x i32> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", i32, 0, 0, 0, 0, 1, 0), i32, i32, i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.1d.v4i32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(4)->getName(), "lod");
}

TEST(SPIRVResourceLoweringTest, LowersPlain3DFetchLevelToImageLoad) {
  // Roadmap L72(c): `Plain3D`'s own counterpart to
  // `LowersImageFetchLevelToImageLoad` -- the 3-wide coordinate's own
  // `X`/`Y`/`Z` components each fold in the matching component of a real
  // `ConstOffset` (mirroring `Array2D`'s own 2-component fold, extended to
  // a 3rd), threaded through to `createLoad3D` alongside the real `Lod`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x i32> %coord, i32 %lod) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call <4 x float> @llvm.spv.resource.load.level.timg(
          target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img, <3 x i32> %coord,
          i32 %lod, <3 x i32> <i32 1, i32 -2, i32 3>)
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <4 x float> @llvm.spv.resource.load.level.timg(
        target("spirv.Image", float, 2, 0, 0, 0, 1, 0), <3 x i32>, i32, <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.3d.v4f32");
  ASSERT_TRUE(Load);
  EXPECT_EQ(Load->getArgOperand(6)->getName(), "lod");
  auto *X = dyn_cast<BinaryOperator>(Load->getArgOperand(3));
  ASSERT_TRUE(X);
  EXPECT_EQ(X->getOpcode(), Instruction::Add);
  auto *Y = dyn_cast<BinaryOperator>(Load->getArgOperand(4));
  ASSERT_TRUE(Y);
  EXPECT_EQ(Y->getOpcode(), Instruction::Add);
  auto *Z = dyn_cast<BinaryOperator>(Load->getArgOperand(5));
  ASSERT_TRUE(Z);
  EXPECT_EQ(Z->getOpcode(), Instruction::Add);
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
  //  u, v, array_layer, dudx, dudy, dvdx, dvdy, lod, use_explicit_lod,
  //  bias, offset_x, offset_y, min_lod_clamp, mask). Roadmap L33 widens
  // this call's own arg count from 18 to 20, adding a real offset_x/
  // offset_y pair (here always zero, since the source's own `ConstOffset`
  // is `<3 x i32> zeroinitializer`).
  EXPECT_EQ(Sample->arg_size(), 20u);
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
  //  dir_x, dir_y, dir_z, ddirxdx, ddirxdy, ddirydx, ddirydy, ddirzdx,
  //  ddirzdy, lod, use_explicit_lod, bias, min_lod_clamp, mask). Roadmap
  //  L56 adds the six derivative operands; roadmap L58 adds `bias`.
  EXPECT_EQ(Sample->arg_size(), 20u);
  // `main` here carries no `feme.shader.stage` attribute (i.e. it is not
  // recognized as a Fragment-stage entry point), so this implicit cube
  // sample gets six zero-constant derivatives rather than a real
  // `feme.stage.derivative.*` synthesis, mirroring
  // `LowersSampledImageAndSamplerPairToImageSample`'s own Plain2D
  // assertion (roadmap L56's own Fragment-only gate; see
  // `getOrSynthesizeSampleCubeDerivatives`).
  for (unsigned ArgNo : {9, 10, 11, 12, 13, 14})
    EXPECT_TRUE(cast<ConstantFP>(Sample->getArgOperand(ArgNo))->isZero());
}

TEST(
    SPIRVResourceLoweringTest,
    LowersCubeSampledImageToImageSampleCubeWithRealDerivativesInFragmentStage) {
  // Roadmap L56: the same shape as `LowersCubeSampledImageToImageSampleCube`
  // above, but `main` now carries a real `feme.shader.stage` `Fragment`
  // attribute -- the one stage GLSL/HLSL's own implicit `texture()`/
  // `Sample()` is ever legal from -- so this implicit-LOD cube sample
  // must synthesize six real `feme.stage.derivative.*` calls (not zero
  // constants) for its own direction vector, mirroring
  // `getOrSynthesizeSample2DDerivatives`'s already-tested Plain2D
  // behavior but for `getOrSynthesizeSampleCubeDerivatives` instead.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %dir) #0 {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timgcube(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsampcube(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %dir, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    attributes #0 = { "feme.shader.stage"="fragment" }
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
  ASSERT_EQ(Sample->arg_size(), 20u);
  // None of the six derivative operands (arg 9-14) is a zero constant --
  // each is a real `feme.stage.derivative.*` call result instead.
  for (unsigned ArgNo : {9, 10, 11, 12, 13, 14}) {
    Value *Operand = Sample->getArgOperand(ArgNo);
    EXPECT_FALSE(isa<ConstantFP>(Operand));
    auto *DerivCall = dyn_cast<CallInst>(Operand);
    ASSERT_TRUE(DerivCall);
    Function *Callee = DerivCall->getCalledFunction();
    ASSERT_TRUE(Callee);
    EXPECT_TRUE(Callee->getName().starts_with("feme.stage.derivative."));
  }
}

TEST(SPIRVResourceLoweringTest,
     LowersCubeArraySampledImageToImageSampleCubeArray) {
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
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.cubearray.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, ddirxdx, ddirxdy, ddirydx, ddirydy, ddirzdx,
  //  ddirzdy, array_layer, lod, use_explicit_lod, bias, min_lod_clamp,
  //  mask). Roadmap L56 adds the six derivative operands; roadmap L60(a)
  //  adds the trailing bias/min_lod_clamp pair.
  EXPECT_EQ(Sample->arg_size(), 21u);
}

TEST(SPIRVResourceLoweringTest, LowersPlain1DSampledImageToImageSample1D) {
  // Roadmap L52a: `classifySampledImage2DHandle` now recognizes `Dim::1D`
  // (previously never checked at all -- see its own comment), and its
  // scalar (non-vector) coordinate is handled by `lowerImageAccesses`'s
  // own early `Plain1D`/`Array1D` special case (see its comment) rather
  // than the generic `CreateExtractElement(Coord, 0/1)` every other shape
  // shares.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(float %u) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %u, i32 0)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1d.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, du_dx, du_dy, lod, use_explicit_lod, bias, min_lod_clamp, mask).
  EXPECT_EQ(Sample->arg_size(), 15u);
}

TEST(SPIRVResourceLoweringTest, LowersArray1DSampledImageToImageSample1DArray) {
  // Roadmap L52a: the `Texture1DArray` counterpart of the test above --
  // `Array1D`'s own 2-component `(u, layer)` coordinate is a real vector
  // this time, unlike `Plain1D`'s bare scalar, so it goes through the
  // early special case's own `CreateExtractElement` branch instead.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %uandlayer) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1darr(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1darr(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %uandlayer,
          i32 0)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1darr(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1darr(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1darray.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, array_layer, du_dx, du_dy, lod, use_explicit_lod, bias,
  //  min_lod_clamp, mask).
  EXPECT_EQ(Sample->arg_size(), 16u);
}

TEST(SPIRVResourceLoweringTest, LowersSampleBiasToPlain1DBias) {
  // Roadmap L61(c): `llvm.spv.resource.samplebias` against a `Plain1D`
  // handle now lowers a real `Bias` operand through to
  // `createSample1D`, mirroring `LowersSampleBiasToPlain2DBias`'s own
  // identical `Plain2D` precedent -- previously `hasOnlySupportedImageUses`
  // rejected this shape/operand combination outright (see its own comment
  // before this fix), leaving the whole handle -- and any real CTS shader
  // combining it with an otherwise-fine buffer handle in the same
  // function -- unlowered.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(float %u, float %bias) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplebias(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %u, float %bias,
          i32 0)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1d.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 15u);

  EXPECT_EQ(Sample->getArgOperand(11)->getName(), "bias");
}

TEST(SPIRVResourceLoweringTest,
     FragmentStageImplicitSample1DSynthesizesRealDerivatives) {
  // Roadmap L63: the `Plain1D` counterpart of
  // `FragmentStageImplicitSampleSynthesizesRealDerivatives`'s own
  // `Plain2D` coverage above -- `main` carries a real
  // `feme.shader.stage`="fragment" attribute, so its implicit-LOD
  // `llvm.spv.resource.sample` against a `Plain1D` handle must get real
  // `feme.stage.derivative.*` calls synthesized as its new `DUdX`/`DUdY`
  // operands, not zero constants.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(float %u) #0 {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %u,
          i32 0)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1d(i32, i32, i32, i32, ptr)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1d.v4f32");
  ASSERT_TRUE(Sample);
  // Argument operands 7-8 are (du_dx, du_dy); neither may be a plain zero
  // constant now that a real derivative can be synthesized.
  for (unsigned ArgNo : {7, 8}) {
    Value *Deriv = Sample->getArgOperand(ArgNo);
    EXPECT_FALSE(isa<ConstantFP>(Deriv));
    auto *DerivCall = dyn_cast<CallInst>(Deriv);
    ASSERT_TRUE(DerivCall);
    Function *Callee = DerivCall->getCalledFunction();
    ASSERT_TRUE(Callee);
    EXPECT_TRUE(Callee->getName().starts_with("feme.stage.derivative."));
  }
}

TEST(SPIRVResourceLoweringTest, LowersSampleBiasClampToArray1DWithMinLodClamp) {
  // Roadmap L61(c): `llvm.spv.resource.samplebias.clamp` against an
  // `Array1D` handle now lowers both a real `Bias` operand and a real
  // `MinLod` clamp through to `createSample1DArray`, exercising the same
  // combined `HasBias`+`HasMinLodClamp` intrinsic this file's own
  // `Plain2D`/`Cube` coverage already validates, now extended to
  // `Array1D`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %uandlayer, float %bias, float %clamp) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1darr(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1darr(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplebias.clamp(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %uandlayer, float %bias,
          i32 0, float %clamp)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1darr(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1darr(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1darray.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 16u);

  EXPECT_EQ(Sample->getArgOperand(12)->getName(), "bias");
  EXPECT_EQ(Sample->getArgOperand(14)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest, LeavesAPlain1DImageFetchAlone) {
  // Roadmap L52a: unlike `Array2D`, `Plain1D`'s own `OpImageFetch`
  // (`getpointer`) path is deliberately left unlowered this session (see
  // `hasOnlySupportedImageUses`'s own comment) -- only its ordinary
  // sample intrinsic is handled above -- so a real `texelFetch(sampler1D,
  // ...)` (legal SPIR-V, unlike a Cube fetch) must still leave the whole
  // handle alone rather than half-lowering it.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(i32 %x) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1dfetch(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg1dfetch(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img, i32 %x)
      %v = load <4 x float>, ptr %p
      ret <4 x float> %v
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1dfetch(i32, i32, i32, i32, ptr)
    declare ptr @llvm.spv.resource.getpointer.timg1dfetch(
        target("spirv.Image", float, 0, 0, 0, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.load.1d.v4f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
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

TEST(SPIRVResourceLoweringTest, LowersNonZeroTexelOffsetPlain2DSample) {
  // Roadmap L26: a `Plain2D` sample's real, nonzero constant `ConstOffset`
  // is threaded through rather than rejecting the whole handle -- see
  // `isSupportedOffset`'s comment.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord, <2 x i32> <i32 1, i32 -1>)
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
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index, u,
  //  v, dudx, dudy, dvdx, dvdy, lod, use_explicit_lod, bias, offset_x,
  //  offset_y, min_lod_clamp, mask). Roadmap L58 adds `bias`.
  ASSERT_EQ(Sample->arg_size(), 19u);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(15))->getSExtValue(), 1);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(16))->getSExtValue(), -1);
}

TEST(SPIRVResourceLoweringTest, LowersNonZeroTexelOffsetArray2DSample) {
  // Roadmap L33: an ordinary (non-comparison) `Array2D` sample's real,
  // nonzero constant `ConstOffset` is now threaded through too, mirroring
  // `LowersNonZeroTexelOffsetPlain2DSample`'s own `Plain2D` precedent
  // (see `isSupportedOffset`'s comment) -- no longer left unlowered.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, <2 x i32> <i32 1, i32 -1>)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2darray.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index, u,
  //  v, array_layer, dudx, dudy, dvdx, dvdy, lod, use_explicit_lod, bias,
  //  offset_x, offset_y, min_lod_clamp, mask).
  ASSERT_EQ(Sample->arg_size(), 20u);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(16))->getSExtValue(), 1);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(17))->getSExtValue(), -1);
}

TEST(SPIRVResourceLoweringTest, LowersSampleClampToPlain2DMinLodClamp) {
  // Roadmap L26: `llvm.spv.resource.sample.clamp` (SPIR-V's own `MinLod`
  // image operand, HLSL's `Texture2D::Sample`'s trailing `clamp`
  // argument) lowers the same as a plain sample, plus a real, nonzero
  // `MinLodClamp` operand instead of the `-inf` no-op sentinel.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord, float %clamp) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample.clamp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          <2 x i32> zeroinitializer, float %clamp)
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
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 19u);
  EXPECT_EQ(Sample->getArgOperand(17)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest, LowersSampleBiasToPlain2DBias) {
  // Roadmap L58: `llvm.spv.resource.samplebias` (SPIR-V's own ordinary,
  // non-comparison `Bias` image operand) lowers the same as a plain
  // implicit-LOD sample, plus a real `Bias` operand threaded through
  // instead of the `0.0` no-op default every other implicit-LOD sample
  // uses.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord, float %bias) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplebias(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord, float %bias,
          <2 x i32> zeroinitializer)
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
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 19u);
  EXPECT_EQ(Sample->getArgOperand(14)->getName(), "bias");
}

TEST(SPIRVResourceLoweringTest, LowersSampleBiasToCubeBias) {
  // Roadmap L58: the same `Bias` operand also lowers against `Cube`,
  // matching the `MinLodClamp` (roadmap L26) precedent's scope.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, float %bias) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplebias(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, float %bias,
          <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.cube.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 20u);
  EXPECT_EQ(Sample->getArgOperand(17)->getName(), "bias");
}

TEST(SPIRVResourceLoweringTest, LowersSampleGradToPlain2DDerivatives) {
  // Roadmap L59: `llvm.spv.resource.samplegrad` (SPIR-V's own explicit
  // `Grad` image operand, GLSL's `textureGrad()`) lowers the same as a
  // plain implicit-LOD sample, but with the caller's own real (dPdx, dPdy)
  // vectors unpacked directly into `createSample2D`'s own `DUdX`/`DUdY`/
  // `DVdX`/`DVdY` screen-space-derivative operands instead of a
  // synthesized or zeroed value -- see `lowerImageAccesses`'s own `HasGrad`
  // handling.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord, <2 x float> %dpdx, <2 x float> %dpdy) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          <2 x float> %dpdx, <2 x float> %dpdy, <2 x i32> zeroinitializer)
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
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 19u);
  auto GetExtractIndex = [](Value *V) -> const ExtractElementInst * {
    return dyn_cast<ExtractElementInst>(V);
  };
  const ExtractElementInst *DUdX = GetExtractIndex(Sample->getArgOperand(8));
  const ExtractElementInst *DUdY = GetExtractIndex(Sample->getArgOperand(9));
  const ExtractElementInst *DVdX = GetExtractIndex(Sample->getArgOperand(10));
  const ExtractElementInst *DVdY = GetExtractIndex(Sample->getArgOperand(11));
  ASSERT_TRUE(DUdX && DUdY && DVdX && DVdY);
  EXPECT_EQ(DUdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DUdX->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DVdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DVdX->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DUdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DUdY->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DVdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DVdY->getIndexOperand())->getZExtValue(), 1u);
  // A `Grad` sample is never an explicit-LOD sample of its own (SPIR-V
  // forbids combining `Lod` and `Grad`); it still reaches
  // `createSample2D`'s implicit-LOD-with-real-derivatives path, so
  // `UseExplicitLod` is false, and the unused `Lod` operand is the usual
  // `0.0` placeholder.
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(13))->isZero());
}

TEST(SPIRVResourceLoweringTest, LowersSampleGradToCubeDerivatives) {
  // Roadmap L59: the same `Grad` operand also lowers against `Cube`,
  // matching `Bias`'s own roadmap L58 scope -- the real 3-component
  // direction-derivative vectors are unpacked into `createSampleCube`'s
  // own six `DDirXdX`/.../`DDirZdY` operands.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, <3 x float> %dpdx, <3 x float> %dpdy) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          <3 x float> %dpdx, <3 x float> %dpdy, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.cube.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 20u);
  auto GetExtractIndex = [](Value *V) -> const ExtractElementInst * {
    return dyn_cast<ExtractElementInst>(V);
  };
  const ExtractElementInst *DDirXdX = GetExtractIndex(Sample->getArgOperand(9));
  const ExtractElementInst *DDirXdY =
      GetExtractIndex(Sample->getArgOperand(10));
  const ExtractElementInst *DDirYdX =
      GetExtractIndex(Sample->getArgOperand(11));
  const ExtractElementInst *DDirYdY =
      GetExtractIndex(Sample->getArgOperand(12));
  const ExtractElementInst *DDirZdX =
      GetExtractIndex(Sample->getArgOperand(13));
  const ExtractElementInst *DDirZdY =
      GetExtractIndex(Sample->getArgOperand(14));
  ASSERT_TRUE(DDirXdX && DDirXdY && DDirYdX && DDirYdY && DDirZdX && DDirZdY);
  EXPECT_EQ(DDirXdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirXdX->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DDirXdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirXdY->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DDirYdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirYdX->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DDirYdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirYdY->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DDirZdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirZdX->getIndexOperand())->getZExtValue(), 2u);
  EXPECT_EQ(DDirZdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirZdY->getIndexOperand())->getZExtValue(), 2u);
}

TEST(SPIRVResourceLoweringTest, LowersSampleBiasToArray2DBias) {
  // Roadmap L60(a): the same `Bias` operand also lowers against
  // `Array2D`, widening `Bias`'s own roadmap L58 `Plain2D`/`Cube`/
  // `CubeArray`-only scope -- `createSample2DArray` now has both a real
  // screen-space derivative pair (mirroring `Plain2D`'s own) and a real
  // `Bias` operand to add to that footprint's own raw LOD, instead of
  // leaving the whole handle unlowered as before this row.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, float %bias) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplebias(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, float %bias,
          <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2darray.v4f32");
  ASSERT_TRUE(Sample);
  // Roadmap L33 widens this call's own arg count from 18 to 20 (a new
  // offset_x/offset_y pair inserted after bias, before min_lod_clamp) --
  // bias itself stays at index 15, unaffected.
  ASSERT_EQ(Sample->arg_size(), 20u);
  EXPECT_EQ(Sample->getArgOperand(15)->getName(), "bias");
}

TEST(SPIRVResourceLoweringTest, LowersSampleClampToArray2DMinLodClamp) {
  // Roadmap L60(a): the same `MinLod` clamp operand (roadmap L26) also
  // lowers against `Array2D`, widening that row's own `Plain2D`/`Cube`/
  // `CubeArray`-only scope the same way this row widens `Bias`'s.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, float %clamp) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample.clamp(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          <2 x i32> zeroinitializer, float %clamp)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2darray.v4f32");
  ASSERT_TRUE(Sample);
  // Roadmap L33 widens this call's own arg count from 18 to 20 and
  // inserts a new offset_x/offset_y pair before min_lod_clamp, shifting
  // it from index 16 to 18.
  ASSERT_EQ(Sample->arg_size(), 20u);
  EXPECT_EQ(Sample->getArgOperand(18)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest, LowersSampleGradToArray2DDerivatives) {
  // Roadmap L60(a): the same `Grad` operand also lowers against
  // `Array2D`, widening `Grad`'s own roadmap L59 `Plain2D`/`Cube`/
  // `CubeArray`-only scope -- the real (dPdx, dPdy) vectors are unpacked
  // into `createSample2DArray`'s own `DUdX`/`DUdY`/`DVdX`/`DVdY`
  // operands, mirroring `Plain2D`'s own handling.
  //
  // Roadmap L64: each derivative is 2-wide against this shape's own
  // 3-wide `(U, V, Layer)` coordinate, per SPIR-V's rule that a `Grad`
  // operand carries one component per image dimension *not counting*
  // the array layer. This test previously passed 3-wide derivatives,
  // which no real shader ever emits -- it was written to satisfy the
  // over-strict coordinate-width check L64 fixed, not to match SPIR-V.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, <2 x float> %dpdx, <2 x float> %dpdy) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          <2 x float> %dpdx, <2 x float> %dpdy, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2darray.v4f32");
  ASSERT_TRUE(Sample);
  // Roadmap L33 widens this call's own arg count from 18 to 20 (a new
  // offset_x/offset_y pair inserted after bias) -- the derivative
  // operands checked below (indices 9-12) are unaffected.
  ASSERT_EQ(Sample->arg_size(), 20u);
  auto GetExtractIndex = [](Value *V) -> const ExtractElementInst * {
    return dyn_cast<ExtractElementInst>(V);
  };
  const ExtractElementInst *DUdX = GetExtractIndex(Sample->getArgOperand(9));
  const ExtractElementInst *DUdY = GetExtractIndex(Sample->getArgOperand(10));
  const ExtractElementInst *DVdX = GetExtractIndex(Sample->getArgOperand(11));
  const ExtractElementInst *DVdY = GetExtractIndex(Sample->getArgOperand(12));
  ASSERT_TRUE(DUdX && DUdY && DVdX && DVdY);
  EXPECT_EQ(DUdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DUdX->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DUdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DUdY->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DVdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DVdX->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DVdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DVdY->getIndexOperand())->getZExtValue(), 1u);
}

TEST(SPIRVResourceLoweringTest,
     LeavesAnArray2DSampleGradWithCoordWidthDerivativesAlone) {
  // Roadmap L64: the negative counterpart of
  // `LowersSampleGradToArray2DDerivatives` above. A `Grad` derivative
  // that is as wide as the *coordinate* (3 components here, including an
  // array layer that SPIR-V never differentiates) is malformed, and must
  // still be rejected rather than silently having its trailing component
  // ignored -- the width check L64 relaxed was narrowed to exactly the
  // legal width, not removed.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, <3 x float> %dpdx, <3 x float> %dpdy) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          <3 x float> %dpdx, <3 x float> %dpdy, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.2darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LeavesAPlain2DSampleGradWithNarrowedDerivativesAlone) {
  // Roadmap L64: the mirror-image negative test. `Plain2D` is *not*
  // arrayed, so its derivatives must match its 2-wide coordinate exactly;
  // a 1-wide derivative is malformed. This pins that L64's narrowing is
  // conditional on the shape being arrayed, rather than a blanket
  // "one narrower than the coordinate" rule.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord, <1 x float> %dpdx, <1 x float> %dpdy) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          <1 x float> %dpdx, <1 x float> %dpdy, <2 x i32> zeroinitializer)
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
}

TEST(SPIRVResourceLoweringTest, LowersSampleGradToPlain1DDerivatives) {
  // Roadmap L65: `llvm.spv.resource.samplegrad` against a `Plain1D`
  // handle now lowers a real (dPdx, dPdy) derivative pair through to
  // `createSample1D`'s own `DUdX`/`DUdY` operands (added by roadmap L63
  // for synthesized implicit-LOD derivatives), mirroring `Plain2D`'s own
  // `LowersSampleGradToPlain2DDerivatives` precedent -- `Plain1D`'s
  // single addressed coordinate component is a bare scalar float (see
  // `isCoordN`'s own comment), so its `Grad` derivative is too; no
  // `ExtractElementInst` unpacking is needed the way `Plain2D`'s 2-wide
  // vector derivatives require. Previously `hasOnlySupportedImageUses`
  // rejected `Grad` against this shape outright.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(float %u, float %dpdx, float %dpdy) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %u, float %dpdx,
          float %dpdy, i32 0)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1d.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 15u);

  EXPECT_EQ(Sample->getArgOperand(7)->getName(), "dpdx");
  EXPECT_EQ(Sample->getArgOperand(8)->getName(), "dpdy");
  // A `Grad` sample is never an explicit-LOD sample of its own; the
  // unused `Lod` operand stays the usual `0.0` placeholder.
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(10))->isZero());
}

TEST(SPIRVResourceLoweringTest, LowersSampleGradToArray1DDerivatives) {
  // Roadmap L65: the `Array1D` counterpart immediately above. Roadmap
  // L64's own `GradDerivativeWidth` computation gives an arrayed shape a
  // derivative one component narrower than its own sample coordinate --
  // `Array1D`'s 2-component `(U, ArrayLayer)` coordinate therefore pairs
  // with a 1-component (bare scalar) derivative, identical in shape to
  // `Plain1D`'s own, since neither ever differentiates the array layer.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %uandlayer, float %dpdx, float %dpdy) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1darr(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1darr(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %uandlayer, float %dpdx,
          float %dpdy, i32 0)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1darr(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1darr(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1darray.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 16u);

  EXPECT_EQ(Sample->getArgOperand(8)->getName(), "dpdx");
  EXPECT_EQ(Sample->getArgOperand(9)->getName(), "dpdy");
}

TEST(SPIRVResourceLoweringTest,
     LeavesAPlain1DSampleGradWithVectorDerivativesAlone) {
  // Roadmap L65: the negative counterpart of
  // `LowersSampleGradToPlain1DDerivatives` above. `Plain1D`'s own
  // single addressed coordinate component is a bare scalar float (SPIR-V
  // never vector-wraps a 1-component coordinate, per `isCoordN`'s own
  // comment); a `Grad` derivative that is instead a 1-element *vector*
  // must still be rejected, since `isCoordN(N=1)` checks the scalar type
  // directly rather than unwrapping a `FixedVectorType`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(float %u, <1 x float> %dpdx, <1 x float> %dpdy) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %u, <1 x float> %dpdx,
          <1 x float> %dpdy, i32 0)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.1d.v4f32"));
}

TEST(SPIRVResourceLoweringTest, LowersPlain3DSampledImageToImageSample3D) {
  // Roadmap L66(a): `classifySampledImage2DHandle` now recognizes
  // `Dim::3D` (`SPIRVDim3D == 2`, previously always rejected outright),
  // and `lowerImageAccesses`'s own early `Plain3D` special case (mirroring
  // `Plain1D`/`Array1D`'s identical precedent) extracts a real 3-component
  // `(U, V, W)` coordinate and calls `createSample3D`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp3d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp3d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.3d.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, w, du_dx, du_dy, dv_dx, dv_dy, dw_dx, dw_dy, lod,
  //  use_explicit_lod, bias, offset_x, offset_y, offset_z, min_lod_clamp,
  //  mask).
  EXPECT_EQ(Sample->arg_size(), 23u);
}

TEST(SPIRVResourceLoweringTest, LowersSampleBiasClampToPlain3DWithMinLodClamp) {
  // Roadmap L67(a): `llvm.spv.resource.samplebias.clamp` against a
  // `Plain3D` handle now lowers both a real `Bias` operand and a real
  // `MinLod` clamp through to `createSample3D`, mirroring
  // `LowersSampleBiasClampToArray1DWithMinLodClamp`'s own `Array1D`
  // precedent -- previously `hasOnlySupportedImageUses` rejected this
  // shape/operand combination outright (see its own comment before this
  // fix), leaving the whole handle unlowered.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, float %bias, float %clamp) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp3d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplebias.clamp(
          target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, float %bias,
          <3 x i32> zeroinitializer, float %clamp)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp3d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.3d.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 23u);
  EXPECT_EQ(Sample->getArgOperand(17)->getName(), "bias");
  EXPECT_EQ(Sample->getArgOperand(21)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest, LowersSampleGradToPlain3D) {
  // Roadmap L67(b): `llvm.spv.resource.samplegrad` against a `Plain3D`
  // handle now lowers a real, caller-supplied per-axis derivative triple
  // through to `createSample3D` (extracted one component per axis from
  // the real `dPdx`/`dPdy` operands), mirroring `Plain1D`'s own roadmap
  // L65 precedent -- previously `hasOnlySupportedImageUses` rejected this
  // shape/operand combination outright (see the now-obsolete
  // `LeavesAPlain3DSampleGradAlone` test this one replaces), leaving the
  // whole handle unlowered.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, <3 x float> %dpdx, <3 x float> %dpdy) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp3d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, <3 x float> %dpdx,
          <3 x float> %dpdy, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp3d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.3d.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 23u);
  // (..., u, v, w, dudx, dudy, dvdx, dvdy, dwdx, dwdy, lod,
  //  use_explicit_lod, bias, offset_x, offset_y, offset_z, min_lod_clamp,
  //  mask) -- operands 9-14 are the per-axis derivative components
  // extracted from the real %dpdx/%dpdy.
  for (unsigned ArgNo : {9, 10, 11, 12, 13, 14})
    EXPECT_TRUE(isa<ExtractElementInst>(Sample->getArgOperand(ArgNo)));
}

TEST(SPIRVResourceLoweringTest, LowersSampleConstOffsetToPlain3D) {
  // Roadmap L67(c): an ordinary `Plain3D` sample's real, nonzero constant
  // `ConstOffset` is now threaded through too, mirroring
  // `LowersNonZeroTexelOffsetArray2DSample`'s own `Array2D` precedent --
  // `isSupportedOffset` now unconditionally accepts `Plain3D` (a genuine
  // `<3 x i32>` offset, matching this shape's own 3-component coordinate
  // width, unlike `Plain2D`/`Array2D`'s 2-component one) -- previously
  // left unlowered (see the now-obsolete
  // `LeavesANonZeroTexelOffsetPlain3DSampleAlone` test this one
  // replaces).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp3d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, <3 x i32> <i32 1, i32 -1, i32 2>)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp3d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.3d.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, w, du_dx, du_dy, dv_dx, dv_dy, dw_dx, dw_dy, lod,
  //  use_explicit_lod, bias, offset_x, offset_y, offset_z, min_lod_clamp,
  //  mask).
  ASSERT_EQ(Sample->arg_size(), 23u);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(18))->getSExtValue(), 1);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(19))->getSExtValue(), -1);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(20))->getSExtValue(), 2);
}

TEST(SPIRVResourceLoweringTest, LowersSampleConstOffsetToPlain1D) {
  // Roadmap L66(d): an ordinary `Plain1D` sample's real, nonzero constant
  // `ConstOffset` is now threaded through too, mirroring
  // `LowersSampleConstOffsetToPlain3D`'s own `Plain3D` precedent -- but,
  // confirmed via a real `deqp-vk` SPIR-V capture, `Plain1D`'s own
  // `ConstOffset` is a bare scalar `i32`, not a vector (unlike every
  // other supported shape), so `isSupportedOffset`'s new `Plain1D`/
  // `Array1D` branch accepts a scalar constant instead of requiring a
  // `FixedVectorType`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(float %u) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %u, i32 7)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1d(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1d.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, du_dx, du_dy, lod, use_explicit_lod, bias, offset, min_lod_clamp,
  //  mask).
  ASSERT_EQ(Sample->arg_size(), 15u);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(12))->getSExtValue(), 7);
}

TEST(SPIRVResourceLoweringTest, LowersSampleConstOffsetToArray1D) {
  // Roadmap L66(d): the `Array1D` counterpart immediately above --
  // confirmed via a real `deqp-vk` SPIR-V capture that `Array1D`'s own
  // `ConstOffset` stays a bare scalar `i32` too, despite its own
  // 2-component `(U, ArrayLayer)` coordinate: SPIR-V's own `ConstOffset`
  // dimensionality excludes the array layer, the same "+1" carve-out
  // `GradDerivativeWidth` (roadmap L64) already applies to a `Grad`
  // derivative.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %uandlayer) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1darr(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1darr(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %uandlayer, i32 7)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1darr(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1darr(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1darray.v4f32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, array_layer, du_dx, du_dy, lod, use_explicit_lod, bias, offset,
  //  min_lod_clamp, mask).
  ASSERT_EQ(Sample->arg_size(), 16u);
  EXPECT_EQ(cast<ConstantInt>(Sample->getArgOperand(13))->getSExtValue(), 7);
}

TEST(SPIRVResourceLoweringTest, LowersSampleBiasToCubeArrayBias) {
  // Roadmap L60(a): the same `Bias` operand also lowers against
  // `CubeArray`, widening `Bias`'s own roadmap L58 `Plain2D`/`Cube`-only
  // scope -- `createSampleCubeArray` (which already threads real
  // screen-space derivatives through for its implicit-LOD footprint,
  // roadmap L56) now also has a real `Bias` operand to add to that
  // footprint's own raw LOD, instead of the hardcoded `0.0f` no-op this
  // row replaces.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<4 x float> %coord, float %bias) {
      %img = call target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplebias(
          target("spirv.Image", float, 3, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord, float %bias,
          <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.cubearray.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 21u);
  EXPECT_EQ(Sample->getArgOperand(18)->getName(), "bias");
}

TEST(SPIRVResourceLoweringTest, LowersSampleClampToCubeArrayMinLodClamp) {
  // Roadmap L60(a): the same `MinLod` clamp operand (roadmap L26) also
  // lowers against `CubeArray`, widening that row's own `Plain2D`/`Cube`-
  // only scope the same way this row widens `Bias`'s.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<4 x float> %coord, float %clamp) {
      %img = call target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample.clamp(
          target("spirv.Image", float, 3, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          <2 x i32> zeroinitializer, float %clamp)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.cubearray.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 21u);
  EXPECT_EQ(Sample->getArgOperand(19)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest, LowersSampleGradToCubeArrayDerivatives) {
  // Roadmap L60(a): the same `Grad` operand also lowers against
  // `CubeArray`, widening `Grad`'s own roadmap L59 `Plain2D`/`Cube`-only
  // scope -- the real 3-component direction-derivative vectors are
  // unpacked into `createSampleCubeArray`'s own six
  // `DDirXdX`/.../`DDirZdY` operands, mirroring `Cube`'s own handling.
  //
  // Roadmap L64: each derivative is 3-wide against this shape's own
  // 4-wide `(X, Y, Z, Layer)` coordinate, for the same
  // array-layer-is-never-differentiated reason as `Array2D`'s own test
  // above; this test previously passed 4-wide derivatives.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<4 x float> %coord, <3 x float> %dpdx, <3 x float> %dpdy) {
      %img = call target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.samplegrad(
          target("spirv.Image", float, 3, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          <3 x float> %dpdx, <3 x float> %dpdy, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.cubearray.v4f32");
  ASSERT_TRUE(Sample);
  ASSERT_EQ(Sample->arg_size(), 21u);
  auto GetExtractIndex = [](Value *V) -> const ExtractElementInst * {
    return dyn_cast<ExtractElementInst>(V);
  };
  const ExtractElementInst *DDirXdX = GetExtractIndex(Sample->getArgOperand(9));
  const ExtractElementInst *DDirXdY =
      GetExtractIndex(Sample->getArgOperand(10));
  const ExtractElementInst *DDirYdX =
      GetExtractIndex(Sample->getArgOperand(11));
  const ExtractElementInst *DDirYdY =
      GetExtractIndex(Sample->getArgOperand(12));
  const ExtractElementInst *DDirZdX =
      GetExtractIndex(Sample->getArgOperand(13));
  const ExtractElementInst *DDirZdY =
      GetExtractIndex(Sample->getArgOperand(14));
  ASSERT_TRUE(DDirXdX && DDirXdY && DDirYdX && DDirYdY && DDirZdX && DDirZdY);
  EXPECT_EQ(DDirXdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirXdX->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DDirXdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirXdY->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DDirYdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirYdX->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DDirYdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirYdY->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DDirZdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirZdX->getIndexOperand())->getZExtValue(), 2u);
  EXPECT_EQ(DDirZdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirZdY->getIndexOperand())->getZExtValue(), 2u);
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpToImageSampleCmp) {
  // Roadmap L46: a `spv_resource_samplecmp` (implicit LOD) against
  // `Plain2D` with a zero offset lowers to `feme.cpu.image.samplecmp.2d.f32`,
  // passing a constant-zero `Lod` and `use_explicit_lod = false`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <3 x i32> zeroinitializer)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, lod, use_explicit_lod, dref, bias, offset_x, offset_y,
  //  min_lod_clamp, mask).
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, dudx, dudy, dvdx, dvdy, lod, use_explicit_lod, dref, bias,
  //  offset_x, offset_y, min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 20u);
  EXPECT_TRUE(cast<ConstantFP>(SampleCmp->getArgOperand(12))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(13))->isZero());
  EXPECT_EQ(SampleCmp->getArgOperand(14)->getName(), "dref");
  // No `Bias` operand on this plain `samplecmp` -- a zero constant (a
  // no-op LOD shift), matching `createSample2D`'s own fallback for an
  // ordinary sample with no `Bias` (roadmap L52(b)).
  EXPECT_TRUE(cast<ConstantFP>(SampleCmp->getArgOperand(15))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(16))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(17))->isZero());
  // No `MinLod` clamp operand on this plain `samplecmp` -- negative
  // infinity (a no-op floor), matching `createSample2D`'s own fallback
  // for an ordinary sample with no `MinLod` clamp (roadmap L52(c)).
  EXPECT_TRUE(cast<ConstantFP>(SampleCmp->getArgOperand(18))->isNegative());
  EXPECT_TRUE(
      cast<ConstantFP>(SampleCmp->getArgOperand(18))->getValue().isInfinity());
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpLevelZeroToImageSampleCmp) {
  // Roadmap L46: a `spv_resource_samplecmplevelzero` (explicit, forced
  // mip level 0) against `Plain2D` with a zero offset also lowers to
  // `feme.cpu.image.samplecmp.2d.f32`, but with `use_explicit_lod = true`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmplevelzero(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <3 x i32> zeroinitializer)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  EXPECT_TRUE(cast<ConstantFP>(SampleCmp->getArgOperand(12))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(13))->isOne());
}

TEST(SPIRVResourceLoweringTest, LowersGatherCmpToImageGatherCmp) {
  // Roadmap L7d: a `spv_resource_gather_cmp` against `Plain2D` with a
  // zero offset lowers to `feme.cpu.image.gathercmp.2d.v4f32`. Unlike
  // `spv_resource_samplecmp`'s own dref-widened `<3 x float>` coordinate
  // (glslang's own convention), `spirv.ImageDrefGather`'s own real
  // `dxc`-emitted `Coordinate` is never padded (roadmap L7d's own
  // investigation), so this uses a plain, unpadded `<2 x float>`
  // coordinate and a `<2 x i32>` offset, mirroring
  // `LowersSampleCmpDxcUnpaddedToImageSampleCmp`'s own identical
  // unpadded-coordinate precedent for the depth-comparison *sample*
  // sibling intrinsic.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather.cmp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          float %dref, <2 x i32> zeroinitializer)
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
  CallInst *GatherCmp =
      findImageCall(*F, "feme.cpu.image.gathercmp.2d.v4f32");
  ASSERT_TRUE(GatherCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, dref, offset_x, offset_y, mask).
  ASSERT_EQ(GatherCmp->arg_size(), 12u);
  EXPECT_EQ(GatherCmp->getArgOperand(8)->getName(), "dref");
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(9))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(10))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(11))->isOne());
}

TEST(SPIRVResourceLoweringTest,
     LowersGatherCmpConstOffsetToImageGatherCmpWithOffset) {
  // Roadmap L7d: a `spv_resource_gather_cmp` carrying a real, nonzero
  // constant offset (SPIR-V's own `ConstOffset` image operand) threads it
  // through to `offset_x`/`offset_y` instead of the defaulted zero
  // constants the case above uses.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather.cmp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          float %dref, <2 x i32> <i32 1, i32 -1>)
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
  CallInst *GatherCmp =
      findImageCall(*F, "feme.cpu.image.gathercmp.2d.v4f32");
  ASSERT_TRUE(GatherCmp);
  EXPECT_EQ(cast<ConstantInt>(GatherCmp->getArgOperand(9))->getSExtValue(), 1);
  EXPECT_EQ(cast<ConstantInt>(GatherCmp->getArgOperand(10))->getSExtValue(),
            -1);
}

TEST(SPIRVResourceLoweringTest, LowersGatherCmpCubeToImageGatherCmpCube) {
  // Roadmap H124r: a `Cube`-shaped `gather.cmp` now lowers to
  // `feme.cpu.image.gathercmp.cube.v4f32`, taking the 3-component
  // direction vector coordinate straight through (mirroring `SampleCube`'s
  // own convention) and no offset operand at all -- SPIR-V forbids
  // `ConstOffset` against `Dim::Cube` outright, so the intrinsic's own
  // always-zero offset operand is simply discarded rather than threaded
  // through the way `Array2D`'s real `offset_x`/`offset_y` are.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %dir, float %dref) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather.cmp(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %dir,
          float %dref, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *GatherCmp =
      findImageCall(*F, "feme.cpu.image.gathercmp.cube.v4f32");
  ASSERT_TRUE(GatherCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, dref, mask).
  ASSERT_EQ(GatherCmp->arg_size(), 11u);
  EXPECT_EQ(GatherCmp->getArgOperand(9)->getName(), "dref");
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(10))->isOne());
}

TEST(SPIRVResourceLoweringTest, LowersGatherCmpArray2DToImageGatherCmpArray2D) {
  // Roadmap H124q: `Array2D`'s own coordinate is 3-wide (`u`, `v`,
  // `array_layer`), mirroring `Sample2DArray`'s own convention, unlike
  // `Plain2D`'s 2-wide one, and lowers to
  // `feme.cpu.image.gathercmp.array2d.v4f32` rather than
  // `feme.cpu.image.gathercmp.2d.v4f32`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather.cmp(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *GatherCmp =
      findImageCall(*F, "feme.cpu.image.gathercmp.array2d.v4f32");
  ASSERT_TRUE(GatherCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, array_layer, dref, offset_x, offset_y, mask).
  ASSERT_EQ(GatherCmp->arg_size(), 13u);
  EXPECT_EQ(GatherCmp->getArgOperand(9)->getName(), "dref");
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(10))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(11))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(12))->isOne());
}

TEST(SPIRVResourceLoweringTest,
     LowersGatherCmpArray2DConstOffsetToImageGatherCmpArray2DWithOffset) {
  // Roadmap H124q: unlike the Plain2D-only-tested precedent originally
  // assumed sufficient, a real `deqp-vk`-independent case
  // (`Feature/Textures/Array.GatherCmp.test`'s own `int2(1, 0)`-offset
  // overload) needs a genuine nonzero `Array2D` `ConstOffset` to lower
  // correctly rather than being rejected outright -- confirms
  // `isSupportedOffset`'s `AllowArray2D` must be `true` for this
  // intrinsic, mirroring `LowersGatherCmpConstOffsetToImageGatherCmpWith
  // Offset`'s own identical `Plain2D` proof.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather.cmp(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <2 x i32> <i32 1, i32 0>)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *GatherCmp =
      findImageCall(*F, "feme.cpu.image.gathercmp.array2d.v4f32");
  ASSERT_TRUE(GatherCmp);
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(10))->isOne());
  EXPECT_TRUE(cast<ConstantInt>(GatherCmp->getArgOperand(11))->isZero());
}

TEST(SPIRVResourceLoweringTest, LowersGatherToImageGather) {
  // Roadmap L7g: a `spv_resource_gather` against `Plain2D` with a zero
  // offset lowers to `feme.cpu.image.gather.2d.v4f32`, mirroring
  // `LowersGatherCmpToImageGatherCmp`'s own identical unpadded `<2 x
  // float>` coordinate/`<2 x i32>` offset precedent, but threading an
  // integer `%component` selector through in place of a float `%dref`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord, i32 %component) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          i32 %component, <2 x i32> zeroinitializer)
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
  CallInst *Gather = findImageCall(*F, "feme.cpu.image.gather.2d.v4f32");
  ASSERT_TRUE(Gather);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, component, offset_x, offset_y, mask).
  ASSERT_EQ(Gather->arg_size(), 12u);
  EXPECT_EQ(Gather->getArgOperand(8)->getName(), "component");
  EXPECT_TRUE(cast<ConstantInt>(Gather->getArgOperand(9))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Gather->getArgOperand(10))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Gather->getArgOperand(11))->isOne());
}

TEST(SPIRVResourceLoweringTest,
     LowersGatherConstOffsetToImageGatherWithOffset) {
  // Roadmap L7g: a `spv_resource_gather` carrying a real, nonzero
  // constant offset (SPIR-V's own `ConstOffset` image operand) threads it
  // through to `offset_x`/`offset_y`, mirroring
  // `LowersGatherCmpConstOffsetToImageGatherCmpWithOffset`'s own identical
  // precedent for the depth-comparison sibling intrinsic.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord, i32 %component) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          i32 %component, <2 x i32> <i32 1, i32 -1>)
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
  CallInst *Gather = findImageCall(*F, "feme.cpu.image.gather.2d.v4f32");
  ASSERT_TRUE(Gather);
  EXPECT_EQ(cast<ConstantInt>(Gather->getArgOperand(9))->getSExtValue(), 1);
  EXPECT_EQ(cast<ConstantInt>(Gather->getArgOperand(10))->getSExtValue(), -1);
}

TEST(SPIRVResourceLoweringTest, LowersGatherCubeToImageGatherCube) {
  // Roadmap H124r: the `Gather` counterpart of
  // `LowersGatherCmpCubeToImageGatherCmpCube` above -- same direction-
  // vector coordinate and lack of an offset operand, but threads an
  // integer `%component` selector through in place of a float `%dref`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %dir, i32 %component) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %dir,
          i32 %component, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Gather = findImageCall(*F, "feme.cpu.image.gather.cube.v4f32");
  ASSERT_TRUE(Gather);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, component, mask).
  ASSERT_EQ(Gather->arg_size(), 11u);
  EXPECT_EQ(Gather->getArgOperand(9)->getName(), "component");
  EXPECT_TRUE(cast<ConstantInt>(Gather->getArgOperand(10))->isOne());
}

TEST(SPIRVResourceLoweringTest, LowersGatherArray2DToImageGatherArray2D) {
  // Roadmap H124q: the `Gather` counterpart of
  // `LowersGatherCmpArray2DToImageGatherCmpArray2D` above -- same 3-wide
  // `Array2D` coordinate, but threads an integer `%component` selector
  // through in place of a float `%dref`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %coord, i32 %component) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.gather(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          i32 %component, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Gather = findImageCall(*F, "feme.cpu.image.gather.array2d.v4f32");
  ASSERT_TRUE(Gather);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, array_layer, component, offset_x, offset_y, mask).
  ASSERT_EQ(Gather->arg_size(), 13u);
  EXPECT_EQ(Gather->getArgOperand(9)->getName(), "component");
  EXPECT_TRUE(cast<ConstantInt>(Gather->getArgOperand(10))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Gather->getArgOperand(11))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Gather->getArgOperand(12))->isOne());
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpLevelToImageSampleCmpWithRealLod) {
  // Roadmap L72(b): unlike `samplecmplevelzero` above, `spv_resource_
  // samplecmplevel` carries a real (non-constant, possibly nonzero) `Lod`
  // operand of its own -- confirmed necessary by a real `deqp-vk` SPIR-V
  // capture of GLSL's own `textureLodOffset(sampler2DShadow, ...)`, which
  // computes its own explicit Lod at runtime rather than baking in a
  // literal zero. This still lowers to the same
  // `feme.cpu.image.samplecmp.2d.f32` runtime entry point, with
  // `use_explicit_lod = true` (mirroring `samplecmplevelzero` above), but
  // threading the real `%lod` value through instead of a synthesized zero
  // constant.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %lod) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmplevel(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, float %lod, <3 x i32> zeroinitializer)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  EXPECT_EQ(SampleCmp->getArgOperand(12)->getName(), "lod");
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(13))->isOne());
  EXPECT_EQ(SampleCmp->getArgOperand(14)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpLevelWithNonzeroOffsetToImageSampleCmp) {
  // Roadmap L72(b): `spv_resource_samplecmplevel`'s own `ConstOffset`
  // operand threads through the same way `samplecmp`'s own does (roadmap
  // L50d), now alongside a real `Lod` rather than an implicit one.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %lod) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmplevel(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, float %lod, <3 x i32> <i32 1, i32 -8, i32 0>)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  EXPECT_EQ(SampleCmp->getArgOperand(12)->getName(), "lod");
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(13))->isOne());
  EXPECT_EQ(cast<ConstantInt>(SampleCmp->getArgOperand(16))->getSExtValue(), 1);
  EXPECT_EQ(cast<ConstantInt>(SampleCmp->getArgOperand(17))->getSExtValue(),
            -8);
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpClampToImageSampleCmpWithMinLodClamp) {
  // Roadmap L52(c): `spv_resource_samplecmp_clamp`'s own trailing `clamp`
  // operand is now recognized by `isDrefSampleIntrinsic`, mirroring
  // `isSampleIntrinsic`'s own `HasMinLodClamp` precedent for an ordinary
  // sample -- a `samplecmp_clamp` against `Plain2D` now lowers to
  // `feme.cpu.image.samplecmp.2d.f32` with a real `MinLodClamp` value
  // threaded through, instead of being left entirely unlowered.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %clamp) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp.clamp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <3 x i32> zeroinitializer, float %clamp)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 20u);
  EXPECT_EQ(SampleCmp->getArgOperand(14)->getName(), "dref");
  EXPECT_EQ(SampleCmp->getArgOperand(18)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpBiasToImageSampleCmpWithBias) {
  // Roadmap L52(b): `spv_resource_samplecmpbias` -- the depth-comparison
  // counterpart of `spv_resource_samplebias`, which
  // `ImageSampleDrefImplicitLodPattern` now emits for a
  // `OpImageSampleDrefImplicitLod` carrying a `Bias` image operand --
  // lowers to `feme.cpu.image.samplecmp.2d.f32` with the real bias value
  // threaded through, in place of the zero constant the three non-bias
  // intrinsic forms get. Its bias operand sits between the dref and the
  // offset, so the offset/clamp operand indices shift by one relative to
  // a plain `samplecmp` (see `getDrefSampleOffsetIdx`).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %bias) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpbias(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, float %bias, <3 x i32> zeroinitializer)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 20u);
  EXPECT_EQ(SampleCmp->getArgOperand(14)->getName(), "dref");
  EXPECT_EQ(SampleCmp->getArgOperand(15)->getName(), "bias");
  // The offset still reads as the zero constant supplied above, from its
  // own bias-shifted index rather than the non-bias one.
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(16))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(17))->isZero());
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpBiasClampToImageSampleCmpCubeWithBiasAndMinLodClamp) {
  // Roadmap L52(b): `spv_resource_samplecmpbias_clamp` threads both a
  // real bias and a real `MinLod` clamp through at once, and does so for
  // `Cube` as well as `Plain2D` (SPIR-V forbids a `ConstOffset` against
  // `Dim::Cube`, but neither `Bias` nor `MinLod`, so `createSampleCmpCube`
  // carries both without an offset pair of its own).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref, float %bias, float %clamp) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpbias.clamp(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, float %bias, <4 x i32> zeroinitializer, float %clamp)
      ret float %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.cube.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, ddir_x_dx, ddir_x_dy, ddir_y_dx, ddir_y_dy,
  //  ddir_z_dx, ddir_z_dy, lod, use_explicit_lod, dref, bias,
  //  min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 21u);
  EXPECT_EQ(SampleCmp->getArgOperand(17)->getName(), "dref");
  EXPECT_EQ(SampleCmp->getArgOperand(18)->getName(), "bias");
  EXPECT_EQ(SampleCmp->getArgOperand(19)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpBiasToImageSampleCmp1DWithBias) {
  // Roadmap L62: `Plain1D` now supports a `Bias` operand on a
  // depth-comparison sample, closing the deferred half of L52(b). This
  // test replaces an earlier negative one asserting the opposite (that a
  // `samplecmpbias` against `Plain1D` was left entirely unlowered),
  // deliberately inverted here rather than deleted, so the newly-supported
  // behavior is covered by exactly the case that previously documented its
  // absence.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %bias) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpbias(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, float %bias, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.1d.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, du_dx, du_dy, lod, use_explicit_lod, dref, bias, offset,
  //  min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 16u);
  EXPECT_EQ(SampleCmp->getArgOperand(11)->getName(), "dref");
  EXPECT_EQ(SampleCmp->getArgOperand(12)->getName(), "bias");
  // No `MinLod` clamp of its own: `samplecmpbias` (unlike
  // `samplecmpbias_clamp`) has no such operand, so the lowering passes
  // negative infinity, a no-op floor.
  auto *Clamp = cast<ConstantFP>(SampleCmp->getArgOperand(14));
  EXPECT_TRUE(Clamp->getValueAPF().isNegInfinity());
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpClampToImageSampleCmp1DWithMinLodClamp) {
  // Roadmap L62: the `MinLod`-clamp counterpart of the test just above --
  // `Plain1D` now threads a real `samplecmp_clamp` clamp operand through
  // too, closing the deferred half of L52(c). Also previously a negative
  // test asserting the opposite.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %clamp) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp.clamp(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <3 x i32> zeroinitializer, float %clamp)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.1d.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 16u);
  EXPECT_EQ(SampleCmp->getArgOperand(11)->getName(), "dref");
  // No `Bias` of its own: `samplecmp_clamp` has no such operand, so the
  // lowering passes a zero constant, a no-op LOD shift.
  auto *Bias = cast<ConstantFP>(SampleCmp->getArgOperand(12));
  EXPECT_TRUE(Bias->isZero());
  EXPECT_EQ(SampleCmp->getArgOperand(14)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpBiasClampToImageSampleCmpArray1D) {
  // Roadmap L62: the `Array1D` counterpart, threading both a real bias
  // and a real `MinLod` clamp at once -- and confirming `Array1D`'s own
  // `vec3(u, layer, compare)` coordinate still reads both leading
  // components as real values (roadmap L54), unaffected by the two new
  // trailing operands.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %bias, float %clamp) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpbias.clamp(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, float %bias, <3 x i32> zeroinitializer, float %clamp)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.1darray.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, array_layer, du_dx, du_dy, lod, use_explicit_lod, dref, bias,
  //  offset, min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 17u);
  EXPECT_EQ(SampleCmp->getArgOperand(12)->getName(), "dref");
  EXPECT_EQ(SampleCmp->getArgOperand(13)->getName(), "bias");
  EXPECT_EQ(SampleCmp->getArgOperand(15)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpWithNonzeroOffsetToImageSampleCmp) {
  // Roadmap L50d: `createSampleCmp2D` now has a real `OffsetX`/`OffsetY`
  // pair, mirroring `createSample2D`'s own roadmap L26 support -- a
  // `samplecmp` with a real, nonzero `ConstOffset` against `Plain2D` now
  // lowers instead of being left unrewritten.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <3 x i32> <i32 1, i32 -1, i32 0>)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 20u);
  EXPECT_EQ(cast<ConstantInt>(SampleCmp->getArgOperand(16))->getSExtValue(), 1);
  EXPECT_EQ(cast<ConstantInt>(SampleCmp->getArgOperand(17))->getSExtValue(),
            -1);
}

TEST(SPIRVResourceLoweringTest, LeavesASampleCmpCubeWithNonzeroOffsetAlone) {
  // Roadmap L50d: unlike `Plain2D`/`Array2D`, `Cube` can never carry a
  // real `ConstOffset` at all (SPIR-V forbids it against `Dim::Cube`, see
  // `isSupportedOffset`'s own comment) -- so a `samplecmp` against `Cube`
  // with a (spec-illegal, but not otherwise rejected upstream) nonzero
  // offset is still left entirely unlowered, matching
  // `isSupportedOffset`'s own always-zero requirement for every shape
  // besides `Plain2D`/`Array2D`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <4 x i32> <i32 1, i32 0, i32 0, i32 0>)
      ret float %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.samplecmp.cube.f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpArray2DToImageSampleCmpArray2D) {
  // Roadmap L48: a `samplecmp` against `Array2D` (`Dim::Dim2D`, `Arrayed
  // == 1`) with a zero offset now lowers to
  // `feme.cpu.image.samplecmp.2darray.f32`, extracting the trailing
  // component of the 4-wide Coordinate as the array layer -- the same
  // "ordinary width + 1" padding rule `SampleCmp2D` (roadmap L46) already
  // established for `Plain2D`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.2darray.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, array_layer, lod, use_explicit_lod, dref, bias, offset_x,
  //  offset_y, min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 21u);
  EXPECT_TRUE(cast<ConstantFP>(SampleCmp->getArgOperand(13))->isZero());
  EXPECT_EQ(SampleCmp->getArgOperand(15)->getName(), "dref");
  EXPECT_TRUE(cast<ConstantFP>(SampleCmp->getArgOperand(16))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(17))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(18))->isZero());
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpArray2DWithNonzeroOffsetToImageSampleCmpArray2D) {
  // Roadmap L50d: `createSampleCmpArray2D` now has a real `OffsetX`/
  // `OffsetY` pair too, mirroring `createSampleCmp2D`'s own new support
  // -- a `samplecmp` against `Array2D` with a real, nonzero `ConstOffset`
  // now lowers instead of being left unrewritten.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <3 x i32> <i32 1, i32 -1, i32 0>)
      ret float %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.2darray.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 21u);
  EXPECT_EQ(cast<ConstantInt>(SampleCmp->getArgOperand(17))->getSExtValue(), 1);
  EXPECT_EQ(cast<ConstantInt>(SampleCmp->getArgOperand(18))->getSExtValue(),
            -1);
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpCubeToImageSampleCmpCube) {
  // Roadmap L48: a `samplecmp` against `Cube` (`Dim::Cube`, `Arrayed ==
  // 0`) with a zero offset now lowers to
  // `feme.cpu.image.samplecmp.cube.f32`, the same "ordinary width (3) +
  // 1" padding rule as `Array2D` above.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.cube.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, ddir_x_dx, ddir_x_dy, ddir_y_dx, ddir_y_dy,
  //  ddir_z_dx, ddir_z_dy, lod, use_explicit_lod, dref, bias,
  //  min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 21u);
  EXPECT_EQ(SampleCmp->getArgOperand(17)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpCubeArrayToImageSampleCmpCubeArray) {
  // Roadmap L48: a `samplecmp` against `CubeArray` (`Dim::Cube`,
  // `Arrayed == 1`) with a zero offset now lowers to
  // `feme.cpu.image.samplecmp.cubearray.f32` -- `CubeArray`'s own
  // ordinary width (4) is already SPIR-V's per-instruction vector width
  // ceiling, so its own dref-sample Coordinate stays 4-wide with no
  // further padding component (confirmed via a real `deqp-vk` capture of
  // `samplercubearrayshadow_fragment`, see `hasOnlySupportedImageUses`'s
  // own comment).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %dirandlayer, float %dref) {
      %img = call target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 3, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %dirandlayer,
          float %dref, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.cubearray.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, ddir_x_dx, ddir_x_dy, ddir_y_dx, ddir_y_dy,
  //  ddir_z_dx, ddir_z_dy, array_layer, lod, use_explicit_lod, dref, bias,
  //  min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 22u);
  EXPECT_EQ(SampleCmp->getArgOperand(18)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmp1DToImageSampleCmp1D) {
  // Roadmap L54: a `samplecmp` against `Plain1D` (`Dim::1D`, `Arrayed ==
  // 0`) now lowers to `feme.cpu.image.samplecmp.1d.f32`. Unlike every
  // other shape above, `Plain1D`'s own dref-sample Coordinate is *not*
  // its ordinary width (1, a bare scalar) plus one: a real `deqp-vk`
  // SPIR-V capture of `sampler1dshadow_fragment` (see
  // `hasOnlySupportedImageUses`'s own comment) confirms it is always a
  // genuine 3-component vector (`vec3(u, <unused>, compare)`, GLSL's own
  // `sampler1DShadow` convention) -- so only the first component (`u`) is
  // read here, unlike `Array2D`'s trailing-component array layer above.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1dcmp(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1dcmp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1dcmp(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1dcmp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.1d.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, du_dx, du_dy, lod, use_explicit_lod, dref, bias, offset,
  //  min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 16u);
  EXPECT_EQ(SampleCmp->getArgOperand(11)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpArray1DToImageSampleCmpArray1D) {
  // Roadmap L54: the `Texture1DArray` counterpart of the test above --
  // real `deqp-vk` capture of `sampler1darrayshadow_fragment` confirms
  // `Array1D`'s own dref-sample Coordinate is also a genuine 3-component
  // vector (`vec3(u, layer, compare)`), but here both leading components
  // are meaningful (unlike `Plain1D`'s own middle component, always
  // unused padding).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg1darrcmp(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp1darrcmp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg1darrcmp(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp1darrcmp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.1darray.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, array_layer, du_dx, du_dy, lod, use_explicit_lod, dref, bias,
  //  offset, min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 17u);
  EXPECT_EQ(SampleCmp->getArgOperand(12)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpWithNonzeroOffsetToImageSampleCmp1D) {
  // Roadmap L66(k): `createSampleCmp1D` now has a real, bare-scalar
  // `Offset` operand, mirroring `createSample1D`'s own roadmap L66(d)
  // support (`LowersSampleConstOffsetToPlain1D` above) -- a `samplecmp`
  // with a real, nonzero `ConstOffset` against `Plain1D` now lowers
  // instead of being left unrewritten, and a real `deqp-vk` SPIR-V
  // capture confirms this `ConstOffset` is the same bare scalar `i32` an
  // ordinary sample's is, despite `Plain1D`'s own dref-widened
  // `Coordinate` staying a genuine 3-component vector.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, i32 1)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.1d.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 16u);
  EXPECT_EQ(cast<ConstantInt>(SampleCmp->getArgOperand(13))->getSExtValue(), 1);
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpWithNonzeroOffsetToImageSampleCmpArray1D) {
  // Roadmap L66(k): the `Array1D` counterpart immediately above --
  // confirmed via a real `deqp-vk` SPIR-V capture that `Array1D`'s own
  // depth-comparison `ConstOffset` also stays a bare scalar `i32`,
  // mirroring `LowersSampleConstOffsetToArray1D`'s own identical ordinary-
  // sample precedent.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, i32 1)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.1darray.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 17u);
  EXPECT_EQ(cast<ConstantInt>(SampleCmp->getArgOperand(14))->getSExtValue(), 1);
}

TEST(SPIRVResourceLoweringTest,
     LeavesASampleCmpAgainstPlain1DWithWrongCoordWidthAlone) {
  // Roadmap L54: unlike the test above, a `samplecmp` against `Plain1D`
  // whose Coordinate is *not* the real, capture-confirmed 3-wide vector
  // (e.g. a 2-wide one, the naive "ordinary width + 1" guess this session
  // disproved -- see `hasOnlySupportedImageUses`'s own comment) is still
  // left entirely unlowered.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<2 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          float %dref, <1 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.samplecmp.1d.f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LowersASampleCmpWithDxcsUnpaddedCoordWidth) {
  // Roadmap L7b: per SPIR-V's own validation rules, a depth-comparison
  // sample's Coordinate operand *may* be one component wider than its
  // shape's ordinary addressing width (`<3 x float>` for `Plain2D`),
  // which is the convention glslang's own SPIR-V output uses --
  // redundantly packing the depth-reference value alongside the
  // separate `Dref` operand (see `isDrefSampleIntrinsic`'s comment).
  // `dxc`'s own real SPIR-V output for HLSL's `SampleCmp` does *not*
  // follow this convention at all: its own Coordinate operand stays
  // exactly the shape's ordinary, unpadded `SampleCoordWidth` (a plain
  // `<2 x float>` for `Plain2D`, identical to an ordinary,
  // non-comparison sample's own Coordinate). Both widths are real,
  // spec-conformant shapes a real producer may emit, so both must lower
  // successfully -- this test (previously named
  // `LeavesASampleCmpWithNonSpecCoordWidthAlone` under the incorrect
  // assumption that only the glslang-padded width was ever valid) now
  // covers dxc's own unpadded shape as a positive case.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<2 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord,
          float %dref, <2 x i32> zeroinitializer)
      ret float %r
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
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32"));
  EXPECT_TRUE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LeavesASampleCmpWithNonSpecCoordWidthAlone) {
  // Roadmap L7b: a depth-comparison sample's Coordinate operand must be
  // either the shape's ordinary unpadded `SampleCoordWidth` (dxc's own
  // convention) or one component wider (glslang's own padded
  // convention) -- any other width is not a real shape either producer
  // emits, and is left unlowered rather than assumed valid. For
  // `Plain2D` (`SampleCoordWidth` of 2), a 4-wide coordinate matches
  // neither convention (2 nor 3).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <2 x i32> zeroinitializer)
      ret float %r
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
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest,
     LeavesAPlain1DSampleCmpWithUnpaddedCoordWidthAlone) {
  // Roadmap L7b: unlike every other shape, `Plain1D`'s own `C0`/`C1`
  // extraction in `lowerImageAccesses` is unconditional (not gated by
  // `Shape`), so a bare-scalar, dxc-style unpadded coordinate (this
  // shape's own `SampleCoordWidth` of 1) is deliberately *not* accepted
  // here even though every other shape now accepts its own unpadded
  // width -- accepting a shape this pre-existing extraction code cannot
  // actually consume would trade a crash-free rejection for a real
  // `CreateExtractElement` crash. Only the glslang-padded width (3) is
  // accepted for `Plain1D`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(float %coord, float %dref) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmp(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %coord,
          float %dref, i32 0)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.samplecmp.1d.f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpGradToImageSampleCmpWithGrad) {
  // Roadmap L66(c): `spv_resource_samplecmpgrad` -- the depth-comparison
  // counterpart of `spv_resource_samplegrad`, which
  // `ImageSampleDrefGradPattern` now emits for an
  // `OpImageSampleDrefExplicitLod` carrying a `Grad` image operand --
  // lowers to `feme.cpu.image.samplecmp.2d.f32` with the real `dPdx`/
  // `dPdy` derivative pair threaded through as `DUdX`/`DUdY`/`DVdX`/
  // `DVdY`, in place of the zero constants every other intrinsic form
  // still gets. `DUdX`/`DVdX` come from `dPdx`, `DUdY`/`DVdY` come from
  // `dPdy` (each pair's own two lanes), matching
  // `SPIRVResourceLowering.cpp`'s own extraction order.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref,
                       <2 x float> %dpdx, <2 x float> %dpdy) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <2 x float> %dpdx, <2 x float> %dpdy,
          <2 x i32> zeroinitializer)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 20u);
  auto *DUdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(8));
  auto *DUdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(9));
  auto *DVdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(10));
  auto *DVdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(11));
  EXPECT_EQ(DUdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DUdX->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DUdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DUdY->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DVdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DVdX->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DVdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DVdY->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(SampleCmp->getArgOperand(14)->getName(), "dref");
  // No `Bias` operand on this `samplecmpgrad` -- a zero constant, matching
  // `createSampleCmp2D`'s own fallback for a caller with no `Bias`.
  EXPECT_TRUE(cast<ConstantFP>(SampleCmp->getArgOperand(15))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(16))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(SampleCmp->getArgOperand(17))->isZero());
  // No `MinLod` clamp operand on this `samplecmpgrad` -- negative infinity
  // (a no-op floor), matching the non-`.clamp` fallback above.
  EXPECT_TRUE(cast<ConstantFP>(SampleCmp->getArgOperand(18))->isNegative());
  EXPECT_TRUE(
      cast<ConstantFP>(SampleCmp->getArgOperand(18))->getValue().isInfinity());
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpGradClampToImageSampleCmpWithGradAndMinLodClamp) {
  // Roadmap L66(c): `spv_resource_samplecmpgrad_clamp` additionally
  // threads a real trailing `MinLod` clamp value through, mirroring
  // `samplecmpbias_clamp`'s own identical precedent.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref,
                       <2 x float> %dpdx, <2 x float> %dpdy, float %clamp) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad.clamp(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord,
          float %dref, <2 x float> %dpdx, <2 x float> %dpdy,
          <2 x i32> zeroinitializer, float %clamp)
      ret float %r
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
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.2d.f32");
  ASSERT_TRUE(SampleCmp);
  ASSERT_EQ(SampleCmp->arg_size(), 20u);
  EXPECT_EQ(SampleCmp->getArgOperand(14)->getName(), "dref");
  EXPECT_EQ(SampleCmp->getArgOperand(18)->getName(), "clamp");
}

TEST(SPIRVResourceLoweringTest, LeavesASampleCmpGradAgainstCubeAlone) {
  // Roadmap L66(c) deliberately scopes `Grad` support to `Plain2D` only
  // -- a `samplecmpgrad` against `Cube` is left entirely unrewritten,
  // mirroring this project's own "extend one shape at a time" precedent
  // (e.g. roadmap L46's initial `Plain2D`-only depth-comparison-sample
  // scope, later widened by L48).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref,
                       <2 x float> %dpdx, <2 x float> %dpdy) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <2 x float> %dpdx, <2 x float> %dpdy,
          <4 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.samplecmp.cube.f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LowersSampleCmpGradToImageSampleCmp1DWithGrad) {
  // Roadmap L66(f): widens roadmap L66(c)'s `Plain2D`-only `Grad` support
  // to `Plain1D` too -- `hasOnlySupportedImageUses`'s own `DrefHasGrad`
  // gate now accepts this shape, and `Plain1D`'s own `dPdx`/`dPdy` are
  // already bare scalar floats (`GradDerivativeWidth`'s own 1-wide
  // precedent for this shape), read directly as `DUdX`/`DUdY` with no
  // `CreateExtractElement` needed -- unlike `Plain2D`'s 2-wide pair.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %dpdx,
                       float %dpdy) {
      %img = call target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad(
          target("spirv.Image", float, 0, 2, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
          float %dpdx, float %dpdy, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.1d.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, du_dx, du_dy, lod, use_explicit_lod, dref, bias, offset,
  //  min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 16u);
  EXPECT_EQ(SampleCmp->getArgOperand(7)->getName(), "dpdx");
  EXPECT_EQ(SampleCmp->getArgOperand(8)->getName(), "dpdy");
  EXPECT_EQ(SampleCmp->getArgOperand(11)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpGradToImageSampleCmpArray1DWithGrad) {
  // Roadmap L66(f): the `Array1D` counterpart of the test just above --
  // only `U`, never `ArrayLayer`, is ever differentiated, mirroring
  // `createSample1DArray`'s own identical precedent for an ordinary
  // sample.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref, float %dpdx,
                       float %dpdy) {
      %img = call target("spirv.Image", float, 0, 2, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad(
          target("spirv.Image", float, 0, 2, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
          float %dpdx, float %dpdy, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 2, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.1darray.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, array_layer, du_dx, du_dy, lod, use_explicit_lod, dref, bias,
  //  offset, min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 17u);
  EXPECT_EQ(SampleCmp->getArgOperand(8)->getName(), "dpdx");
  EXPECT_EQ(SampleCmp->getArgOperand(9)->getName(), "dpdy");
  EXPECT_EQ(SampleCmp->getArgOperand(12)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest,
     LeavesASampleCmpGradAgainstPlain1DWithVectorDerivativesAlone) {
  // Roadmap L66(f): unlike the positive test above, a `samplecmpgrad`
  // against `Plain1D` whose `dPdx`/`dPdy` are 2-wide vectors (the shape
  // roadmap L66(c) already validated for `Plain2D`, not the real
  // capture-confirmed bare-scalar shape for `Plain1D`) is left entirely
  // unrewritten -- `hasOnlySupportedImageUses`'s own generalized
  // derivative-width check correctly rejects this mismatch.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord, float %dref,
                       <2 x float> %dpdx, <2 x float> %dpdy) {
      %img = call target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad(
          target("spirv.Image", float, 0, 2, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
          <2 x float> %dpdx, <2 x float> %dpdy, <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.samplecmp.1d.f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpGradToImageSampleCmpArray2DWithGrad) {
  // Roadmap L66(g): widens roadmap L66(c)'s `Plain2D`-only `Grad` support
  // to `Array2D` too -- `hasOnlySupportedImageUses`'s own `DrefHasGrad`
  // gate now accepts this shape, and `Array2D`'s own `dPdx`/`dPdy` are a
  // 2-wide vector (`GradDerivativeWidth`'s own "arrayed, SampleCoordWidth
  // - 1" precedent giving the same width as `Plain2D`), extracted with
  // `CreateExtractElement` the same way `Plain2D` is -- only `U`/`V`,
  // never `ArrayLayer` (the trailing lane of the 4-wide `Coordinate`),
  // is ever differentiated, mirroring `createSample2DArray`'s own
  // identical precedent for an ordinary sample.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref,
                       <2 x float> %dpdx, <2 x float> %dpdy) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 10, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 11, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <2 x float> %dpdx, <2 x float> %dpdy,
          <3 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.2darray.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, array_layer, du_dx, du_dy, dv_dx, dv_dy, lod, use_explicit_lod,
  //  dref, bias, offset_x, offset_y, min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 21u);
  auto *DUdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(9));
  auto *DUdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(10));
  auto *DVdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(11));
  auto *DVdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(12));
  EXPECT_EQ(DUdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DUdX->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DUdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DUdY->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DVdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DVdX->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DVdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DVdY->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(SampleCmp->getArgOperand(15)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpGradToImageSampleCmpCubeWithGrad) {
  // Roadmap L66(h): widens roadmap L66(g)'s support to `Cube` too --
  // `hasOnlySupportedImageUses`'s own `DrefHasGrad` gate now accepts this
  // shape, and `Cube`'s own `dPdx`/`dPdy` are a genuine 3-wide
  // direction-vector derivative pair (`GradDerivativeWidth`'s own
  // "unarrayed, matches SampleCoordWidth" precedent, `SampleCoordWidth`
  // itself already being 3 for `Cube`), unpacked into six scalars the
  // same way `createSampleCube`'s own `HasGrad` handling unpacks an
  // ordinary sample's `Grad` operand (roadmap L59).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref,
                       <3 x float> %dpdx, <3 x float> %dpdy) {
      %img = call target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad(
          target("spirv.Image", float, 3, 2, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <3 x float> %dpdx, <3 x float> %dpdy,
          <4 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp = findImageCall(*F, "feme.cpu.image.samplecmp.cube.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, ddir_x_dx, ddir_x_dy, ddir_y_dx, ddir_y_dy,
  //  ddir_z_dx, ddir_z_dy, lod, use_explicit_lod, dref, bias,
  //  min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 21u);
  auto *DDirXdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(9));
  auto *DDirXdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(10));
  auto *DDirYdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(11));
  auto *DDirYdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(12));
  auto *DDirZdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(13));
  auto *DDirZdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(14));
  EXPECT_EQ(DDirXdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirXdX->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DDirXdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirXdY->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DDirYdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirYdX->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DDirYdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirYdY->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DDirZdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirZdX->getIndexOperand())->getZExtValue(), 2u);
  EXPECT_EQ(DDirZdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirZdY->getIndexOperand())->getZExtValue(), 2u);
  EXPECT_EQ(SampleCmp->getArgOperand(17)->getName(), "dref");
}

TEST(SPIRVResourceLoweringTest,
     LowersSampleCmpGradToImageSampleCmpCubeArrayWithGrad) {
  // Roadmap L66(i): widens roadmap L66(h)'s support to `CubeArray` too --
  // `hasOnlySupportedImageUses`'s own `DrefHasGrad` gate now accepts this
  // shape as well, and `CubeArray`'s own `dPdx`/`dPdy` needed no further
  // width change at all: `GradDerivativeWidth`'s own generalized "arrayed
  // shapes drop one component" formula already resolves `CubeArray`
  // (arrayed, `SampleCoordWidth == 4`) to the same 3-wide direction-vector
  // derivative `Cube` itself uses, unpacked into six scalars the same way
  // `Cube`'s own `HasGrad` handling above unpacks its own derivative
  // pair.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<4 x float> %coord, float %dref,
                       <3 x float> %dpdx, <3 x float> %dpdy) {
      %img = call target("spirv.Image", float, 3, 2, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call float @llvm.spv.resource.samplecmpgrad(
          target("spirv.Image", float, 3, 2, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <4 x float> %coord,
          float %dref, <3 x float> %dpdx, <3 x float> %dpdy,
          <4 x i32> zeroinitializer)
      ret float %r
    }
    declare target("spirv.Image", float, 3, 2, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *SampleCmp =
      findImageCall(*F, "feme.cpu.image.samplecmp.cubearray.f32");
  ASSERT_TRUE(SampleCmp);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, ddir_x_dx, ddir_x_dy, ddir_y_dx, ddir_y_dy,
  //  ddir_z_dx, ddir_z_dy, array_layer, lod, use_explicit_lod, dref, bias,
  //  min_lod_clamp, mask).
  ASSERT_EQ(SampleCmp->arg_size(), 22u);
  auto *DDirXdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(9));
  auto *DDirXdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(10));
  auto *DDirYdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(11));
  auto *DDirYdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(12));
  auto *DDirZdX = cast<ExtractElementInst>(SampleCmp->getArgOperand(13));
  auto *DDirZdY = cast<ExtractElementInst>(SampleCmp->getArgOperand(14));
  EXPECT_EQ(DDirXdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirXdX->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DDirXdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirXdY->getIndexOperand())->getZExtValue(), 0u);
  EXPECT_EQ(DDirYdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirYdX->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DDirYdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirYdY->getIndexOperand())->getZExtValue(), 1u);
  EXPECT_EQ(DDirZdX->getVectorOperand()->getName(), "dpdx");
  EXPECT_EQ(cast<ConstantInt>(DDirZdX->getIndexOperand())->getZExtValue(), 2u);
  EXPECT_EQ(DDirZdY->getVectorOperand()->getName(), "dpdy");
  EXPECT_EQ(cast<ConstantInt>(DDirZdY->getIndexOperand())->getZExtValue(), 2u);
  auto *ArrayLayer = cast<ExtractElementInst>(SampleCmp->getArgOperand(15));
  EXPECT_EQ(ArrayLayer->getVectorOperand()->getName(), "coord");
  EXPECT_EQ(cast<ConstantInt>(ArrayLayer->getIndexOperand())->getZExtValue(),
            3u);
  EXPECT_EQ(SampleCmp->getArgOperand(18)->getName(), "dref");
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
     LeavesAnArray2DIntegerSampledImageHandleUsedForSampleAlone) {
  // Roadmap L125(b): `Plain1D`/`Array1D` (see the "lowers" tests below) are
  // now accepted alongside `Plain2D`, but `Array2D` -- and every other
  // remaining shape (`Plain3D`/`Cube`/`CubeArray`) -- is still rejected:
  // no `createSample2DArrayI32`-style runtime helper exists for any of
  // them yet. The whole handle (and therefore the whole function) is left
  // unrewritten, matching `LeavesAnArrayedImageHandleAlone`'s own "no
  // partial lowering" contract.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(<3 x float> %uvandlayer) {
      %img = call target("spirv.Image", i32, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x i32> @llvm.spv.resource.sample(
          target("spirv.Image", i32, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %uvandlayer, <2 x i32> zeroinitializer)
      ret <4 x i32> %r
    }
    declare target("spirv.Image", i32, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.2darray.v4i32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest,
     LowersImplicitLodIntegerSampledImage1DArrayToImageSampleV4I32) {
  // Roadmap L125(b): widens `LowersIntegerSampledImage1DArrayToImageSample
  // V4I32` below's explicit-LOD acceptance to also accept an ordinary
  // implicit-LOD sample against an `Array1D` integer-channel
  // (`usampler1DArray`/`isampler1DArray`) sampled image, mirroring
  // `LowersImplicitLodIntegerSampledImage1DToImageSampleV4I32`'s own
  // identical `Plain1D` widening.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(<2 x float> %uandlayer) {
      %img = call target("spirv.Image", i32, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x i32> @llvm.spv.resource.sample(
          target("spirv.Image", i32, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %uandlayer, i32 0)
      ret <4 x i32> %r
    }
    declare target("spirv.Image", i32, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1darray.v4i32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, array_layer, lod, offset, mask). Implicit LOD defaults to 0.0.
  EXPECT_TRUE(cast<ConstantFP>(Sample->getArgOperand(8))->isZero());
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.1darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerSampledImage1DArrayToImageSampleV4I32) {
  // Roadmap L125(b): an *explicit*-LOD `OpImageSampleExplicitLod` against
  // an `Array1D` integer-channel (`usampler1DArray`/`isampler1DArray`)
  // sampled image is legal SPIR-V (restricted, per the Vulkan spec, to
  // `NEAREST` filtering) -- mirrors `LowersIntegerSampledImage1DToImage
  // SampleV4I32`'s own `Plain1D` case, lowering to `createSample1DArrayI32`
  // instead of `createSample1DI32`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(<2 x float> %uandlayer) {
      %img = call target("spirv.Image", i32, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x i32> @llvm.spv.resource.samplelevel(
          target("spirv.Image", i32, 0, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %uandlayer, float 0.0,
          i32 0)
      ret <4 x i32> %r
    }
    declare target("spirv.Image", i32, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1darray.v4i32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, array_layer, lod, offset, mask).
  EXPECT_EQ(Sample->getArgOperand(0)->getName(), "image_heap");
  EXPECT_EQ(Sample->getArgOperand(2)->getName(), "sampler_heap");
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(4))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(5))->isZero());
  EXPECT_TRUE(cast<ConstantFP>(Sample->getArgOperand(8))->isZero());
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.1darray.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersImplicitLodIntegerSampledImage1DToImageSampleV4I32) {
  // Roadmap L125(b): widens `LowersIntegerSampledImage1DToImageSampleV4I32`
  // below's explicit-LOD acceptance to also accept an ordinary implicit-LOD
  // sample against a `Plain1D` integer-channel (`usampler1D`/`isampler1D`)
  // sampled image, mirroring `LowersImplicitLodIntegerSampledImageToImage
  // SampleV4I32`'s own identical `Plain2D` widening (roadmap L125(a)).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(float %coord) {
      %img = call target("spirv.Image", i32, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x i32> @llvm.spv.resource.sample(
          target("spirv.Image", i32, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %coord, i32 0)
      ret <4 x i32> %r
    }
    declare target("spirv.Image", i32, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1d.v4i32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, lod, offset, mask). Implicit LOD defaults to 0.0.
  EXPECT_TRUE(cast<ConstantFP>(Sample->getArgOperand(7))->isZero());
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.1d.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersIntegerSampledImage1DToImageSampleV4I32) {
  // Roadmap L125(b): an *explicit*-LOD `OpImageSampleExplicitLod` against a
  // `Plain1D` integer-channel (`usampler1D`/`isampler1D`) sampled image is
  // legal SPIR-V (restricted, per the Vulkan spec, to `NEAREST` filtering)
  // -- mirrors `LowersIntegerSampledImageToImageSampleV4I32`'s own
  // `Plain2D` case (roadmap H109), lowering to `createSample1DI32` instead
  // of `createSample2DI32`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(float %coord) {
      %img = call target("spirv.Image", i32, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x i32> @llvm.spv.resource.samplelevel(
          target("spirv.Image", i32, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %coord, float 0.0, i32 0)
      ret <4 x i32> %r
    }
    declare target("spirv.Image", i32, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.1d.v4i32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, lod, offset, mask).
  EXPECT_EQ(Sample->getArgOperand(0)->getName(), "image_heap");
  EXPECT_EQ(Sample->getArgOperand(2)->getName(), "sampler_heap");
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(4))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(5))->isZero());
  EXPECT_TRUE(cast<ConstantFP>(Sample->getArgOperand(7))->isZero());
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.1d.v4f32"));
}

TEST(SPIRVResourceLoweringTest,
     LowersImplicitLodIntegerSampledImageToImageSampleV4I32) {
  // Roadmap L125(a): an *implicit*-LOD `OpImageSampleImplicitLod` against
  // an integer-channel (`usampler2D`/`isampler2D`) `Plain2D` sampled image
  // is legal SPIR-V (still restricted, per the Vulkan spec, to `NEAREST`
  // filtering) -- widening `LowersIntegerSampledImageToImageSampleV4I32`
  // below's pre-existing explicit-LOD-only acceptance. Every real CTS case
  // driving this (`dEQP-VK.pipeline.monolithic.image.*.format.
  // r8_[su]int.*`, etc.) samples a single-mip-level image, so
  // `lowerImageAccesses`'s own implicit-LOD default of a constant `0.0`
  // `Lod` (see its own comment) is exactly right here, without needing
  // real derivative-based LOD computation.
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
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4i32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, lod, offset_x, offset_y, mask). Implicit LOD defaults to 0.0.
  EXPECT_TRUE(cast<ConstantFP>(Sample->getArgOperand(8))->isZero());
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.2d.v4f32"));
}

TEST(SPIRVResourceLoweringTest, LowersIntegerSampledImageToImageSampleV4I32) {
  // Roadmap H109: an *explicit*-LOD `OpImageSampleExplicitLod` against an
  // integer-channel (`usampler2D`/`isampler2D`) sampled image is legal
  // SPIR-V (restricted, per the Vulkan spec, to `NEAREST` filtering) --
  // unlike `LeavesAnIntegerSampledImageHandleUsedForSampleAlone` above,
  // this now lowers to the new `feme.cpu.image.sample.2d.v4i32` entry
  // point instead of being rejected outright.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x i32> @main(<2 x float> %coord) {
      %img = call target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x i32> @llvm.spv.resource.samplelevel(
          target("spirv.Image", i32, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord, float 0.0,
          <2 x i32> zeroinitializer)
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
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4i32");
  ASSERT_TRUE(Sample);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  u, v, lod, offset_x, offset_y, mask).
  EXPECT_EQ(Sample->getArgOperand(0)->getName(), "image_heap");
  EXPECT_EQ(Sample->getArgOperand(2)->getName(), "sampler_heap");
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(4))->isZero());
  EXPECT_TRUE(cast<ConstantInt>(Sample->getArgOperand(5))->isZero());
  EXPECT_TRUE(cast<ConstantFP>(Sample->getArgOperand(8))->isZero());
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.sample.2d.v4f32"));
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

TEST(SPIRVResourceLoweringTest,
     LowersIntegerStorageImageWriteToImageStoreV4I32) {
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

TEST(SPIRVResourceLoweringTest,
     LowersArrayedStorageImageWriteToImageStoreArray) {
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

TEST(SPIRVResourceLoweringTest, LowersArrayedStorageImageLoadStoreToBothCalls) {
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

TEST(
    SPIRVResourceLoweringTest,
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

TEST(SPIRVResourceLoweringTest,
     LowersArray1DStorageImageWriteToImageStoreArray) {
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

TEST(SPIRVResourceLoweringTest,
     LeavesAMultisampledCubeStorageImageHandleAlone) {
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

TEST(SPIRVResourceLoweringTest,
     LowersQueryLodToImageQueryLodWithZeroDerivativesOutsideFragment) {
  // Roadmap L52e: a `Plain2D` `calculate.lod`/`calculate.lod.unclamped`
  // pair (`OpImageQueryLod`'s own two-lane legalization,
  // `ImageQueryLodPattern`) each independently lowers to its own
  // `feme.cpu.image.querylod.2d.v2f32` call (this pass does no
  // cross-call CSE of its own, see `lowerImageAccesses`'s own comment),
  // extracting lane 0 (the clamped level) or lane 1 (the raw unclamped
  // lod) from its own call respectively. `main` here carries no
  // `feme.shader.stage` attribute (not recognized as a Fragment-stage
  // entry point), so -- mirroring `SampleShader`'s own analogous
  // zero-constant-derivatives case above -- both calls' own derivative
  // pairs synthesize as zero constants rather than a real
  // `feme.stage.derivative.*` call.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <2 x float> @main(<2 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %level = call float @llvm.spv.resource.calculate.lod(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord)
      %lod = call float @llvm.spv.resource.calculate.lod.unclamped(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord)
      %r0 = insertelement <2 x float> poison, float %level, i64 0
      %r1 = insertelement <2 x float> %r0, float %lod, i64 1
      ret <2 x float> %r1
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

  // Neither original scalar intrinsic call survives; every replacement
  // `feme.cpu.image.querylod.2d.v2f32` call has all-zero derivative
  // operands (no Fragment-stage synthesis) and a true mask, and each is
  // consumed by exactly one `extractelement` picking out its own lane 0
  // or lane 1.
  unsigned NumQueryLodCalls = 0;
  unsigned NumLane0Extracts = 0, NumLane1Extracts = 0;
  for (Instruction &I : instructions(*F)) {
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      if (Function *Callee = CI->getCalledFunction()) {
        EXPECT_NE(Callee->getIntrinsicID(),
                  Intrinsic::spv_resource_calculate_lod);
        EXPECT_NE(Callee->getIntrinsicID(),
                  Intrinsic::spv_resource_calculate_lod_unclamped);
        if (Callee->getName() == "feme.cpu.image.querylod.2d.v2f32") {
          ++NumQueryLodCalls;
          // (image_heap, count, sampler_heap, count, image_index,
          //  sampler_index, dudx, dudy, dvdx, dvdy, mask). No `u`/`v`
          // operand at all.
          ASSERT_EQ(CI->arg_size(), 11u);
          for (unsigned ArgNo : {6, 7, 8, 9})
            EXPECT_TRUE(cast<ConstantFP>(CI->getArgOperand(ArgNo))->isZero());
          EXPECT_TRUE(cast<ConstantInt>(CI->getArgOperand(10))->isOne());
        }
      }
      continue;
    }
    auto *EE = dyn_cast<ExtractElementInst>(&I);
    if (!EE)
      continue;
    auto *VecCall = dyn_cast<CallInst>(EE->getVectorOperand());
    if (!VecCall || !VecCall->getCalledFunction() ||
        VecCall->getCalledFunction()->getName() !=
            "feme.cpu.image.querylod.2d.v2f32")
      continue;
    uint64_t Lane = cast<ConstantInt>(EE->getIndexOperand())->getZExtValue();
    if (Lane == 0)
      ++NumLane0Extracts;
    else if (Lane == 1)
      ++NumLane1Extracts;
  }
  EXPECT_EQ(NumQueryLodCalls, 2u);
  EXPECT_EQ(NumLane0Extracts, 1u);
  EXPECT_EQ(NumLane1Extracts, 1u);
}

TEST(SPIRVResourceLoweringTest,
     FragmentStageQueryLodSynthesizesRealDerivatives) {
  // Same shape as the test above, except `main` now carries a real
  // `feme.shader.stage`="fragment" attribute (roadmap H7i's own
  // Fragment-only derivative-synthesis gate) -- the query's own shared
  // `feme.cpu.image.querylod.2d.v2f32` call must now get real
  // `feme.stage.derivative.*` calls synthesized as its derivative
  // operands, not zero constants, mirroring
  // `FragmentStageImplicitSampleSynthesizesRealDerivatives` above.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<2 x float> %coord) #0 {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %level = call float @llvm.spv.resource.calculate.lod(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord)
      ret float %level
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
  CallInst *QueryLod = findImageCall(*F, "feme.cpu.image.querylod.2d.v2f32");
  ASSERT_TRUE(QueryLod);
  for (unsigned ArgNo : {6, 7, 8, 9}) {
    Value *Deriv = QueryLod->getArgOperand(ArgNo);
    EXPECT_FALSE(isa<ConstantFP>(Deriv));
    auto *DerivCall = dyn_cast<CallInst>(Deriv);
    ASSERT_TRUE(DerivCall);
    Function *Callee = DerivCall->getCalledFunction();
    ASSERT_TRUE(Callee);
    EXPECT_TRUE(Callee->getName().starts_with("feme.stage.derivative."));
  }
}

TEST(SPIRVResourceLoweringTest,
     LowersArray2DQueryLodToImageQueryLodSharingPlain2DFormula) {
  // Roadmap H124t: `Array2D`'s own `CalculateLevelOfDetail` (an
  // `OpImageQueryLod` against a `Texture2DArray`) -- unlike an ordinary
  // sample, this op's own coordinate is always exactly 2 components even
  // against an arrayed handle (`Texture2DArray::CalculateLevelOfDetail`'s
  // own HLSL signature takes no slice argument at all -- confirmed via a
  // real `spirv-dis` dump, `%v2float` regardless of shape -- the array
  // dimension plays no part in the LOD computation), so this reuses
  // `QueryLod2D`'s own runtime call and formula unchanged, dispatching on
  // shape the same way `LowersQueryLodToImageQueryLodWithZeroDerivativesOut
  // sideFragment` above already covers for `Plain2D`.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<2 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %level = call float @llvm.spv.resource.calculate.lod(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord)
      ret float %level
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylod.2d.v2f32"));
  EXPECT_TRUE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest,
     LowersCubeQueryLodToImageQueryLodCubeWithDirectionVector) {
  // Roadmap H124u: `Cube`'s own `CalculateLevelOfDetail` (an
  // `OpImageQueryLod` against a `TextureCube`) -- unlike `Plain2D`/
  // `Array2D` above, this op's own coordinate is a 3-component direction
  // vector (confirmed via a real `spirv-dis` dump, `%v3float`, since
  // there is no face-local 2D UV until face selection happens at
  // runtime), so this lowers to a distinct
  // `feme.cpu.image.querylod.cube.v2f32` call carrying the raw
  // direction vector `(DirX, DirY, DirZ)` plus its own 6 direction-vector
  // derivative components, rather than reusing `QueryLod2D`'s call.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<3 x float> %coord) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %level = call float @llvm.spv.resource.calculate.lod(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord)
      ret float %level
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *QueryLod = findImageCall(*F, "feme.cpu.image.querylod.cube.v2f32");
  ASSERT_TRUE(QueryLod);
  // (image_heap, count, sampler_heap, count, image_index, sampler_index,
  //  dir_x, dir_y, dir_z, ddirxdx, ddirxdy, ddirydx, ddirydy, ddirzdx,
  //  ddirzdy, mask) -- 16 operands total.
  EXPECT_EQ(QueryLod->arg_size(), 16u);
  EXPECT_TRUE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(SPIRVResourceLoweringTest, LeavesAPlain1DQueryLodHandleAlone) {
  // Roadmap L52e/H124t/H124u deliberately scope `OpImageQueryLod` support
  // to `Plain2D`/`Array2D`/`Cube` -- `Plain1D` (and every other
  // still-unwidened shape) is left entirely unlowered, the same honest
  // all-or-nothing contract every other unsupported shape gets
  // (`collectHandles` declines the whole function).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(float %coord) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %level = call float @llvm.spv.resource.calculate.lod(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, float %coord)
      ret float %level
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.querylod.2d.v2f32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

// Roadmap L66(e): a real use-after-free, found via CTS re-runs once L66's
// other sub-items began clearing more pipelines. Two functions each declare
// an *image* handle at the same (set, binding) identity but with two
// different shapes (`Plain2D` here, `Plain3D` in the other function) -- a
// conflicting re-declaration `run` correctly excludes both image handles
// from their own function's `ImageHeapIndices` (mirroring every other
// conflicting-identity test above) -- while each function's own *sampler*
// handle shares one single, non-conflicting (set, binding) identity between
// them, so each sampler handle *is* accepted. Before this fix,
// `lowerImageAccesses`'s own trailing cleanup loop unconditionally erased
// every handle in `ImageHeapIndices` -- including an accepted sampler
// handle whose paired (excluded) image handle meant the sample call
// through it was deliberately left unrewritten and still live, referencing
// that sampler handle as an operand: `Instruction::eraseFromParent` on a
// still-used `Value` aborts (`Uses remain when a value is destroyed!`) in
// an assertions-enabled build, confirmed via a minimal repro before this
// fix landed. The regression this test actually exercises is simply that
// `runPass` completes at all, rather than crashing -- the specific
// assertions below (both handles/calls survive, each function still gets
// its own sampler-only heap-index metadata) document the exact state that
// makes the old unconditional erase unsafe.
TEST(SPIRVResourceLoweringTest,
     LeavesConflictingImageShapeWithSharedSamplerBindingAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @sample_2d(<2 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg2d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample.v4f32.timg2d(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    define <4 x float> @sample_3d(<3 x float> %coord) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample.v4f32.timg3d(
          target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img,
          target("spirv.Sampler") %samp, <3 x float> %coord, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg2d(i32, i32, i32, i32, ptr)
    declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
    declare target("spirv.Sampler")
        @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
    declare <4 x float> @llvm.spv.resource.sample.v4f32.timg2d(
        target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler"),
        <2 x float>, <2 x i32>)
    declare <4 x float> @llvm.spv.resource.sample.v4f32.timg3d(
        target("spirv.Image", float, 2, 0, 0, 0, 1, 0), target("spirv.Sampler"),
        <3 x float>, <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M); // Must not crash.

  Function *F2D = M->getFunction("sample_2d");
  Function *F3D = M->getFunction("sample_3d");
  ASSERT_TRUE(F2D);
  ASSERT_TRUE(F3D);
  // Neither function's own sample ever lowers: each one's image handle is
  // the conflicting side of its own (set, binding) identity.
  EXPECT_FALSE(findImageCall(*F2D, "feme.cpu.image.sample.2d.v4f32"));
  EXPECT_FALSE(findImageCall(*F3D, "feme.cpu.image.sample.3d.v4f32"));
  // Each function's own (non-conflicting) sampler handle is still accepted
  // on its own, so each still gets bound-resource metadata (a real,
  // observable difference from `LeavesConflictingRangeSizeUnchanged`'s own
  // fully-conflicting case above, where neither handle in either function
  // is ever accepted and no metadata is attached at all).
  EXPECT_TRUE(findBoundNode(*M, "sample_2d"));
  EXPECT_TRUE(findBoundNode(*M, "sample_3d"));
}

// Roadmap L72(d): `OpImageQuerySizeLod`/`OpImageQueryLevels` (imported as
// calls against the SPIR-V importer's own synthesized
// `feme.query.size_lod.*`/`feme.query.levels.*` functions, see
// `SPIRVImporter.cpp`'s own `lowerImageQueryOpcodes`) lower to
// `feme.cpu.image.getdimensions.lod.2d.v2i32`/
// `feme.cpu.image.querylevels.i32` for a `Plain2D` sampled image.
TEST(SPIRVResourceLoweringTest, LowersPlain2DQuerySizeLodAndQueryLevels) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <2 x i32> @main(i32 %lod) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img)
      %dims = call <2 x i32> @"feme.query.size_lod.0"(
          target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, i32 %lod)
      ret <2 x i32> %dims
    }
    declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 1, 0, 0, 0, 1, 0))
    declare <2 x i32> @"feme.query.size_lod.0"(
        target("spirv.Image", float, 1, 0, 0, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.getdimensions.lod.2d.v2i32"));
}

// Roadmap L75: `OpImageQuerySizeLod`'s shape gate is now widened past
// `Plain2D` to every classifiable non-multisampled shape, dispatching to
// a distinct builder per result width/formula (see each `ImageCallKind`'s
// own doc). One positive lowering test per newly-accepted shape follows,
// mirroring L74's own per-shape `OpImageQueryLevels` test pattern; a
// `Plain2DMS`/`Array2DMS` negative test (still correctly rejected) closes
// out the group. `Array2D` here (below) previously crashed with
// `replaceAllUses of value with new value of different type!` before this
// row's own dedicated `QuerySizeLod2DArray` (v3i32) builder existed --
// see `LeavesArray2DQuerySizeLodHandleAlone`'s own now-removed history in
// roadmap L72(d).
TEST(SPIRVResourceLoweringTest, LowersArray2DQuerySizeLod) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <3 x i32> @main(i32 %lod) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %dims = call <3 x i32> @"feme.query.size_lod.0"(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img, i32 %lod)
      ret <3 x i32> %dims
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <3 x i32> @"feme.query.size_lod.0"(
        target("spirv.Image", float, 1, 0, 1, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(
      findImageCall(*F, "feme.cpu.image.getdimensions.lod.2darray.v3i32"));
}

TEST(SPIRVResourceLoweringTest, LowersPlain1DQuerySizeLod) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main(i32 %lod) {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %dims = call i32 @"feme.query.size_lod.0"(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img, i32 %lod)
      ret i32 %dims
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.size_lod.0"(
        target("spirv.Image", float, 0, 0, 0, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.getdimensions.lod.1d.i32"));
}

TEST(SPIRVResourceLoweringTest, LowersArray1DQuerySizeLod) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <2 x i32> @main(i32 %lod) {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %dims = call <2 x i32> @"feme.query.size_lod.0"(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img, i32 %lod)
      ret <2 x i32> %dims
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <2 x i32> @"feme.query.size_lod.0"(
        target("spirv.Image", float, 0, 0, 1, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(
      findImageCall(*F, "feme.cpu.image.getdimensions.lod.1darray.v2i32"));
}

TEST(SPIRVResourceLoweringTest, LowersPlain3DQuerySizeLod) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <3 x i32> @main(i32 %lod) {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %dims = call <3 x i32> @"feme.query.size_lod.0"(
          target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img, i32 %lod)
      ret <3 x i32> %dims
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <3 x i32> @"feme.query.size_lod.0"(
        target("spirv.Image", float, 2, 0, 0, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.getdimensions.lod.3d.v3i32"));
}

// Roadmap L75: `Cube` reuses the existing `QuerySizeLod2D` builder
// unchanged -- a cube face's own mip-level extent shrinks by exactly the
// same `(max(1,W>>lod), max(1,H>>lod))` formula a `Plain2D` mip level
// does, so this is a "free" shape-gate widening with no new builder.
TEST(SPIRVResourceLoweringTest, LowersCubeQuerySizeLod) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <2 x i32> @main(i32 %lod) {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %dims = call <2 x i32> @"feme.query.size_lod.0"(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img, i32 %lod)
      ret <2 x i32> %dims
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <2 x i32> @"feme.query.size_lod.0"(
        target("spirv.Image", float, 3, 0, 0, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.getdimensions.lod.2d.v2i32"));
}

TEST(SPIRVResourceLoweringTest, LowersCubeArrayQuerySizeLod) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <3 x i32> @main(i32 %lod) {
      %img = call target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %dims = call <3 x i32> @"feme.query.size_lod.0"(
          target("spirv.Image", float, 3, 0, 1, 0, 1, 0) %img, i32 %lod)
      ret <3 x i32> %dims
    }
    declare target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <3 x i32> @"feme.query.size_lod.0"(
        target("spirv.Image", float, 3, 0, 1, 0, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(
      findImageCall(*F, "feme.cpu.image.getdimensions.lod.cubearray.v3i32"));
}

// `Plain2DMS`/`Array2DMS` remain unsupported: `OpImageQuerySizeLod` is
// spec-legal against a multisampled image, but no real CTS case has
// driven that combination's own scoping yet (see the shape gate's own
// comment in `hasOnlySupportedImageUses`).
TEST(SPIRVResourceLoweringTest, LeavesPlain2DMSQuerySizeLodHandleAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <2 x i32> @main(i32 %lod) {
      %img = call target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %dims = call <2 x i32> @"feme.query.size_lod.0"(
          target("spirv.Image", float, 1, 0, 0, 1, 1, 0) %img, i32 %lod)
      ret <2 x i32> %dims
    }
    declare target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare <2 x i32> @"feme.query.size_lod.0"(
        target("spirv.Image", float, 1, 0, 0, 1, 1, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.getdimensions.lod.2d.v2i32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

// Roadmap L74: unlike `OpImageQuerySizeLod` (whose `Plain2D`-only builder
// result shape `LeavesArray2DQuerySizeLodHandleAlone` above confirms is
// still not widened), `OpImageQueryLevels`'s own scalar `i32` result never
// varies by shape, so its own shape gate is widened to every classifiable
// non-multisampled shape with no builder change at all. One test per
// shape below confirms each is now accepted, alongside a negative test
// confirming `Plain2DMS` is still correctly rejected (no
// `textureQueryLevels()` GLSL overload exists for a multisampled
// sampler).
TEST(SPIRVResourceLoweringTest, LowersArray2DQueryLevels) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img)
      ret i32 %levels
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 1, 0, 1, 0, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
}

TEST(SPIRVResourceLoweringTest, LowersPlain1DQueryLevels) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img)
      ret i32 %levels
    }
    declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 0, 0, 0, 0, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
}

TEST(SPIRVResourceLoweringTest, LowersArray1DQueryLevels) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img)
      ret i32 %levels
    }
    declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 0, 0, 1, 0, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
}

TEST(SPIRVResourceLoweringTest, LowersPlain3DQueryLevels) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img)
      ret i32 %levels
    }
    declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 2, 0, 0, 0, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
}

TEST(SPIRVResourceLoweringTest, LowersCubeQueryLevels) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img)
      ret i32 %levels
    }
    declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 3, 0, 0, 0, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
}

TEST(SPIRVResourceLoweringTest, LowersCubeArrayQueryLevels) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 3, 0, 1, 0, 1, 0) %img)
      ret i32 %levels
    }
    declare target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 3, 0, 1, 0, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
}

// Roadmap L74: the identical widening for a *storage* image handle
// (`hasOnlySupportedStorageImageUses`'s own mirror of the sampled-image
// check above) -- an `Array2D` storage image's own `OpImageQueryLevels`
// use is now accepted too.
TEST(SPIRVResourceLoweringTest, LowersArray2DStorageQueryLevels) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 1, 0, 1, 0, 2, 0) %img)
      ret i32 %levels
    }
    declare target("spirv.Image", float, 1, 0, 1, 0, 2, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 1, 0, 1, 0, 2, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
}

// Roadmap L76(a): a storage image's own `OpImageWrite`/`OpImageRead`
// Texel operand takes exactly the shader's declared `RWTexture*<T>`
// element width -- a bare scalar for a single-channel format (e.g.
// `RWTexture2D<float>`/`RWTexture2DArray<float>`), not only the full
// 4-wide vector every prior test above covers.
// `hasOnlySupportedStorageImageUses` now accepts this width via
// `storageImageTexelWidth`, and
// `widenStorageImageTexel`/`narrowStorageImageTexel` convert to/from the
// runtime's own fixed 4-wide calling convention.
TEST(SPIRVResourceLoweringTest,
     LowersScalarStorageImageWriteToWidenedImageStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord, float %texel) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord)
      store float %texel, ptr %p
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
  // The runtime call's own Texel argument (index 5: image_heap,
  // image_heap_count, image_index, x, y, [texel], mask) is always the
  // fixed `<4 x float>` width, regardless of the shader's own narrower
  // declared type.
  EXPECT_TRUE(isa<FixedVectorType>(Store->getArgOperand(5)->getType()));
  EXPECT_EQ(cast<FixedVectorType>(Store->getArgOperand(5)->getType())
                ->getNumElements(),
            4u);
}

// The exact roadmap L76(a) repro shape: `RWTexture2DArray<float>` (an
// arrayed *storage* image with a scalar single-channel element type) --
// previously rejected outright by `hasOnlySupportedStorageImageUses`'s
// hardcoded `<4 x float>`-only check, now widened the same way the plain,
// non-arrayed case above is.
TEST(SPIRVResourceLoweringTest,
     LowersScalarArrayedStorageImageWriteToWidenedImageStoreArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<3 x i32> %coord, float %texel) {
      %img = call target("spirv.Image", float, 1, 0, 1, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 1, 0, 2, 0) %img, <3 x i32> %coord)
      store float %texel, ptr %p
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

// The read-side mirror: a scalar `LoadInst` against a storage image's own
// `getpointer` result narrows the runtime call's fixed 4-wide result back
// down to the shader's own scalar type.
TEST(SPIRVResourceLoweringTest,
     LowersScalarStorageImageReadFromNarrowedImageLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(<2 x i32> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord)
      %v = load float, ptr %p
      ret float %v
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
  // The function's own terminator now returns a scalar `float` narrowed
  // from the runtime call's `<4 x float>` result, not the vector itself.
  auto *Ret = cast<ReturnInst>(F->back().getTerminator());
  EXPECT_TRUE(Ret->getReturnValue()->getType()->isFloatTy());
}

// A narrower-than-scalar-or-4-wide vector (2 components, e.g.
// `RWTexture2D<float2>`) is also a real, `dxc`-emitted shape for a
// 2-channel storage-image format -- not just the scalar (1-channel) case
// above -- and widens/narrows the identical way.
TEST(SPIRVResourceLoweringTest,
     LowersTwoComponentStorageImageWriteToWidenedImageStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord, <2 x float> %texel) {
      %img = call target("spirv.Image", float, 1, 0, 0, 0, 2, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %p = call ptr @llvm.spv.resource.getpointer.timg(
          target("spirv.Image", float, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord)
      store <2 x float> %texel, ptr %p
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
  EXPECT_EQ(cast<FixedVectorType>(Store->getArgOperand(5)->getType())
                ->getNumElements(),
            4u);
}

// Negative regression: `Plain2DMS` (a multisampled sampled image) must
// still be rejected for `OpImageQueryLevels` -- GLSL has no
// `textureQueryLevels()` overload for a multisampled sampler, so no real
// CTS case exercises this combination, and this project's own convention
// (see roadmap L73's own `LeavesPlain2DMSSampleHandleAlone` precedent) is
// to keep an unverified widening explicitly rejected rather than silently
// accepted.
TEST(SPIRVResourceLoweringTest, LeavesPlain2DMSQueryLevelsHandleAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %levels = call i32 @"feme.query.levels.0"(
          target("spirv.Image", float, 1, 0, 0, 1, 1, 0) %img)
      ret i32 %levels
    }
    declare target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.levels.0"(
        target("spirv.Image", float, 1, 0, 0, 1, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.querylevels.i32"));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

// importer's own synthesized `feme.query.samples.*` function, see
// `SPIRVImporter.cpp`'s own `lowerImageQueryOpcodes`) lowers to
// `feme.cpu.image.querysamples.i32` for a `Plain2DMS` sampled image --
// `classifySampledImage2DHandle` now produces this shape for a
// multisampled (`MS == 1`), non-arrayed 2D sampled image, unlike every
// other shape's own unconditional multisample rejection.
TEST(SPIRVResourceLoweringTest, LowersPlain2DMSQuerySamples) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samples = call i32 @"feme.query.samples.0"(
          target("spirv.Image", float, 1, 0, 0, 1, 1, 0) %img)
      ret i32 %samples
    }
    declare target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.samples.0"(
        target("spirv.Image", float, 1, 0, 0, 1, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querysamples.i32"));
}

// Same as `LowersPlain2DMSQuerySamples` above, but for an arrayed
// multisampled sampled image (`Array2DMS`) -- confirms the `Arrayed`
// operand is still correctly threaded through `classifySampledImage2DHandle`'s
// new multisample-widening path.
TEST(SPIRVResourceLoweringTest, LowersArray2DMSQuerySamples) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %img = call target("spirv.Image", float, 1, 0, 1, 1, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samples = call i32 @"feme.query.samples.0"(
          target("spirv.Image", float, 1, 0, 1, 1, 1, 0) %img)
      ret i32 %samples
    }
    declare target("spirv.Image", float, 1, 0, 1, 1, 1, 0)
        @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
    declare i32 @"feme.query.samples.0"(
        target("spirv.Image", float, 1, 0, 1, 1, 1, 0))
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(findImageCall(*F, "feme.cpu.image.querysamples.i32"));
}

// Roadmap L73's own widening of `classifySampledImage2DHandle` to accept
// `Plain2DMS` must not silently accept an ordinary filtered sample against
// that same shape -- no `runtime/CPU` helper exists to sample a
// multisampled sampled image, and SPIR-V itself never legalizes
// `OpImageSampleImplicitLod` against one. Before this test (and the
// `isSampleIntrinsic`-branch shape check it exercises) were added, this
// exact case would have been silently accepted with the wrong (too
// narrow, 2-wide rather than 3-wide) coordinate width, since
// `isSampleIntrinsic`'s own branch had no shape check at all.
TEST(SPIRVResourceLoweringTest, LeavesPlain2DMSSampleHandleAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %coord) {
      %img = call target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
          @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
      %samp = call target("spirv.Sampler")
          @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
      %r = call <4 x float> @llvm.spv.resource.sample(
          target("spirv.Image", float, 1, 0, 0, 1, 1, 0) %img,
          target("spirv.Sampler") %samp, <2 x float> %coord, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
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

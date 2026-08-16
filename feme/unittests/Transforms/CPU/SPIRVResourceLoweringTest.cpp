//===- SPIRVResourceLoweringTest.cpp - Tests for the pass ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVResourceLowering.h"

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
  // {name, prefix-size, (set, binding, range-size, heap-base)...}.
  ASSERT_EQ(Entry->getNumOperands(), 6u);
  EXPECT_EQ(cast<MDString>(Entry->getOperand(0))->getString(), "main");
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue(),
            4u); // prefix size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(2))->getZExtValue(),
            0u); // set
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(3))->getZExtValue(),
            1u); // binding
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(4))->getZExtValue(),
            4u); // range size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(5))->getZExtValue(),
            0u); // heap base
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

} // namespace

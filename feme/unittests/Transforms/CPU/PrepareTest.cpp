//===- PrepareTest.cpp - Tests for PreparePass ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Prepare.h"

#include "feme/Core/ShaderStage.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
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
    Err.print("PrepareTest", errs());
  return M;
}

void runPass(Module &M, StringRef EntryPoint = "",
             feme::ShaderStage Stage = feme::ShaderStage::Compute) {
  ModuleAnalysisManager MAM;
  PreparePass(EntryPoint, Stage).run(M, MAM);
}

TEST(PrepareTest, PromotesAllocaAndLowersSwitch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %v) #0 {
    entry:
      %a = alloca i32
      store i32 %v, ptr %a
      %loaded = load i32, ptr %a
      switch i32 %loaded, label %default [ i32 0, label %zero ]
    default:
      br label %end
    zero:
      br label %end
    end:
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (const Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<AllocaInst>(I));
    EXPECT_FALSE(isa<SwitchInst>(I));
  }
}

// Roadmap H19a: a `Function`-storage-class SPIR-V local variable (e.g. a
// `gl_GlobalInvocationID.xy` scratch temporary) lowers to a vector-typed
// `alloca` accessed one element at a time through a `getelementptr` --
// `PromotePass` (plain `mem2reg`) alone refuses to promote any `alloca`
// with a `getelementptr` use at all (`isAllocaPromotable`'s documented
// precondition), which previously left a real, divergent vector `store`
// into it for `feme::cpu::SIMDizePass` to reject outright (reduced from a
// real failing `dEQP-VK.image.load_store.with_format.2d.r32_sfloat` case).
// `SROAPass`, now run first, splits the `alloca` into per-element scalars
// that `PromotePass` can then promote, eliminating it (and its
// `getelementptr`s) entirely, leaving pure SSA `insertelement`/
// `extractelement` for `feme::cpu::SIMDizePass` to widen normally.
TEST(PrepareTest, PromotesVectorAllocaAccessedThroughGetElementPtr) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(<2 x i32> %coord) #0 {
      %a = alloca <2 x i32>
      store <2 x i32> %coord, ptr %a
      %x_ptr = getelementptr <2 x i32>, ptr %a, i32 0, i32 0
      %x = load i32, ptr %x_ptr
      %y_ptr = getelementptr <2 x i32>, ptr %a, i32 0, i32 1
      %y = load i32, ptr %y_ptr
      %sum = add i32 %x, %y
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (const Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<AllocaInst>(I));
    EXPECT_FALSE(isa<GetElementPtrInst>(I));
    EXPECT_FALSE(isa<LoadInst>(I));
    EXPECT_FALSE(isa<StoreInst>(I));
  }
}

TEST(PrepareTest, KeepsOnlySelectedEntryPoint) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    define void @other_entry() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M, "main");

  EXPECT_TRUE(M->getFunction("main"));
  EXPECT_FALSE(M->getFunction("other_entry"));
}

TEST(PrepareTest, SelectsSoleEntryPointWithoutAnOption) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_TRUE(M->getFunction("main"));
}

TEST(PrepareTest, RemovesUnreachableDefinitions) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @helper()
      ret void
    }
    define void @helper() {
      ret void
    }
    define void @unreachable_helper() {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_TRUE(M->getFunction("helper"));
  EXPECT_FALSE(M->getFunction("unreachable_helper"));
}

// Stage selection is the `feme.shader.stage` enumeration, so an entry point
// carrying only that attribute -- no `hlsl.shader` -- selects.
TEST(PrepareTest, SelectsByShaderStageAttribute) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "feme.shader.stage"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_TRUE(M->getFunction("main"));
}

// Two entry points of *different* stages are not an ambiguity: only the
// requested stage is a candidate, which is what selecting by enumeration
// rather than by "is it a compute shader?" buys.
TEST(PrepareTest, SelectsTheRequestedStageAmongSeveral) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @vertex_main() #0 {
      ret void
    }
    define void @compute_main() #1 {
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    attributes #1 = { "feme.shader.stage"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M, "", feme::ShaderStage::Vertex);

  EXPECT_TRUE(M->getFunction("vertex_main"));
  EXPECT_FALSE(M->getFunction("compute_main"));
}

// An entry point of another stage is not selectable as this one, even by
// name.
TEST(PrepareTest, RejectsAnEntryPointOfAnotherStage) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
  )");
  ASSERT_TRUE(M);

  std::string Error;
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Payload) {
        if (DI->getSeverity() != DS_Error)
          return;
        raw_string_ostream OS(*static_cast<std::string *>(Payload));
        DiagnosticPrinterRawOStream Printer(OS);
        DI->print(Printer);
      },
      &Error);
  runPass(*M, "main");

  EXPECT_EQ(Error, "feme-cpu-prepare: no compute entry point named 'main' in "
                   "this module");
  // The module is left alone rather than half-prepared.
  EXPECT_TRUE(M->getFunction("main"));
}

// `feme.shader.stage` is the model, but a module raised before it existed --
// or hand-written IR -- still selects through `hlsl.shader`.
TEST(PrepareTest, HLSLShaderAttributeIsStillAccepted) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    define void @pixel_main() #1 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
    attributes #1 = { "hlsl.shader"="pixel" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_TRUE(M->getFunction("main"));
  EXPECT_FALSE(M->getFunction("pixel_main"));
}

} // namespace

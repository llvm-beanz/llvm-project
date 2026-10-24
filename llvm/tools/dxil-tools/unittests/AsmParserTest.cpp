//===- AsmParserTest.cpp - Legacy assembly parser tests
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/AsmParser/Parser.h"
#include "dxil/AsmParser/SlotMapping.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/DebugInfoMetadata.h"
#include "dxil/IR/LLVMContext.h"
#include "dxil/IR/Module.h"
#include "dxil/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace dxil;

namespace {

TEST(AsmParser, TypedPointersForwardReferencesAndAssemblyRoundTrip) {
  LLVMContext Context;
  llvm::SMDiagnostic Error;
  auto M = parseAssemblyString(R"(
    %Pair = type { i32, i32* }
    @pair = global %Pair { i32 7, i32* @value }
    @value = global i32 42

    define i32 @choose(i1 %cond, i32* %ptr) #0 {
    entry:
      %loaded = load i32, i32* %ptr, align 4
      br i1 %cond, label %left, label %right
    left:
      %sum = call i32 @add(i32 %loaded, i32 1)
      br label %exit
    right:
      br label %exit
    exit:
      %result = phi i32 [ %sum, %left ], [ %loaded, %right ]
      ret i32 %result
    }

    define i32 @add(i32 %a, i32 %b) {
      %sum = add i32 %a, %b
      ret i32 %sum
    }
    attributes #0 = { nounwind "legacy"="yes" }
  )",
                               Error, Context);
  ASSERT_TRUE(M) << Error.getMessage().str();
  EXPECT_FALSE(verifyModule(*M));
  auto *F = M->getFunction("choose");
  ASSERT_NE(nullptr, F);
  EXPECT_TRUE(F->hasFnAttribute(Attribute::NoUnwind));
  EXPECT_EQ("yes", F->getFnAttribute("legacy").getValueAsString());
  EXPECT_TRUE(M->getGlobalVariable("value")->getType()->isPointerTy());

  std::string Text;
  llvm::raw_string_ostream OS(Text);
  M->print(OS, nullptr);
  LLVMContext RoundTripContext;
  auto RoundTrip = parseAssemblyString(Text, Error, RoundTripContext);
  ASSERT_TRUE(RoundTrip) << Error.getMessage().str();
  EXPECT_FALSE(verifyModule(*RoundTrip));
  std::string RoundTripText;
  llvm::raw_string_ostream RoundTripOS(RoundTripText);
  RoundTrip->print(RoundTripOS, nullptr);
  EXPECT_EQ(Text, RoundTripText);
}

TEST(AsmParser, FloatingPointFormats) {
  LLVMContext Context;
  llvm::SMDiagnostic Error;
  auto M = parseAssemblyString(R"(
    @half = constant half 0xH3C00
    @float = constant float 1.0
    @double = constant double 0x3FF0000000000000
    @x87 = constant x86_fp80 0xK3FFF8000000000000000
    @quad = constant fp128 0xL00000000000000003FFF000000000000
    @ppc = constant ppc_fp128 0xM3FF00000000000000000000000000000
    @negative_zero = constant double 0x8000000000000000
  )",
                               Error, Context);
  ASSERT_TRUE(M) << Error.getMessage().str();
  EXPECT_FALSE(verifyModule(*M));
  for (const char *Name : {"half", "float", "double", "x87", "quad", "ppc"}) {
    auto *Value =
        llvm::cast<ConstantFP>(M->getGlobalVariable(Name)->getInitializer());
    EXPECT_EQ(ConstantFP::get(Value->getType(), 1.0), Value) << Name;
  }
  EXPECT_TRUE(llvm::cast<ConstantFP>(
                  M->getGlobalVariable("negative_zero")->getInitializer())
                  ->isNegativeZeroValue());
}

TEST(AsmParser, LegacyDebugTagsAndMetadataCycles) {
  LLVMContext Context;
  llvm::SMDiagnostic Error;
  SlotMapping Slots;
  auto M = parseAssemblyString(R"(
    !variables = !{!0, !1}
    !cycle = !{!2}
    !0 = !DILocalVariable(tag: DW_TAG_auto_variable, scope: !3,
                         name: "local", type: !4)
    !1 = !DILocalVariable(tag: DW_TAG_arg_variable, scope: !3,
                         name: "arg", type: !4, arg: 1)
    !2 = distinct !{!2}
    !3 = !DISubprogram(name: "f", isDefinition: false, type: !5)
    !4 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
    !5 = !DISubroutineType(types: !6)
    !6 = !{null}
  )",
                               Error, Context, &Slots);
  ASSERT_TRUE(M) << Error.getMessage().str();
  EXPECT_FALSE(verifyModule(*M));
  EXPECT_EQ(DW_TAG_auto_variable,
            llvm::cast<DILocalVariable>(Slots.MetadataNodes.at(0))->getTag());
  EXPECT_EQ(DW_TAG_arg_variable,
            llvm::cast<DILocalVariable>(Slots.MetadataNodes.at(1))->getTag());
  auto *Cycle = Slots.MetadataNodes.at(2).get();
  EXPECT_EQ(Cycle, Cycle->getOperand(0));
  EXPECT_TRUE(Cycle->isResolved());

  std::string Text;
  llvm::raw_string_ostream OS(Text);
  M->print(OS, nullptr);
  EXPECT_NE(std::string::npos, Text.find("tag: DW_TAG_auto_variable"));
  EXPECT_NE(std::string::npos, Text.find("tag: DW_TAG_arg_variable"));
  LLVMContext RoundTripContext;
  auto RoundTrip = parseAssemblyString(Text, Error, RoundTripContext);
  ASSERT_TRUE(RoundTrip) << Error.getMessage().str();
  EXPECT_FALSE(verifyModule(*RoundTrip));
}

TEST(AsmParser, UnnamedGlobalSlotsAndParsingIntoModule) {
  LLVMContext Context;
  llvm::SMDiagnostic Error;
  Module M("combined", Context);
  SlotMapping Slots;
  EXPECT_FALSE(parseAssemblyInto(
      llvm::MemoryBufferRef("@0 = global i32 7\n", "first"), M, Error, &Slots));
  ASSERT_EQ(1u, Slots.GlobalValues.size());
  EXPECT_EQ(&*M.global_begin(), Slots.GlobalValues.front());
  EXPECT_FALSE(parseAssemblyInto(
      llvm::MemoryBufferRef("define i32 @f() { ret i32 42 }\n", "second"), M,
      Error));
  EXPECT_NE(nullptr, M.getFunction("f"));
  EXPECT_FALSE(verifyModule(M));
}

TEST(AsmParser, ReportsMalformedInput) {
  LLVMContext Context;
  llvm::SMDiagnostic Error;
  auto M = parseAssemblyString("define i32 @f() {\n  ret i64 0\n}\n", Error,
                               Context);
  EXPECT_FALSE(M);
  EXPECT_EQ(2, Error.getLineNo());
  EXPECT_TRUE(Error.getMessage().contains("doesn't match"));

  M = parseAssemblyString("@g = global i32* @missing\n", Error, Context);
  EXPECT_FALSE(M);
  EXPECT_TRUE(Error.getMessage().contains("undefined value '@missing'"));

  M = parseAssemblyString(
      "!test = !{!0}\n!0 = !DIBasicType(tag: DW_TAG_not_a_tag)\n", Error,
      Context);
  EXPECT_FALSE(M);
  EXPECT_TRUE(Error.getMessage().contains("invalid DWARF tag"));
}

} // namespace

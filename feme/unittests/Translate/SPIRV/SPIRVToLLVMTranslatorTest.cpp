//===- SPIRVToLLVMTranslatorTest.cpp - Tests for SPIRVToLLVMTranslator ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Parses hand-written `spirv` dialect text directly (no binary round-trip
// needed here: this test targets the Translator, not the SPIRVImporter).
static Module parseSpirvModule(Context &Ctx, llvm::StringRef SourceText) {
  mlir::MLIRContext &MLIRCtx = Ctx.getMLIRContext();
  MLIRCtx.loadDialect<mlir::spirv::SPIRVDialect>();

  mlir::ParserConfig Config(&MLIRCtx);
  mlir::OwningOpRef<mlir::spirv::ModuleOp> SourceModule =
      mlir::parseSourceString<mlir::spirv::ModuleOp>(SourceText, Config);
  EXPECT_TRUE(SourceModule);
  return Module::fromMLIR(std::move(SourceModule));
}

TEST(SPIRVToLLVMTranslatorTest, FormatNames) {
  SPIRVToLLVMTranslator Translator;
  EXPECT_EQ(Translator.getFromFormatName(), "spirv");
  EXPECT_EQ(Translator.getToFormatName(), "llvmir");
}

TEST(SPIRVToLLVMTranslatorTest, TranslatesSpirvFunctionToLLVMFunction) {
  Context Ctx;
  Module SpirvModule = parseSpirvModule(Ctx, R"mlir(
    spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
      spirv.func @foo() -> () "Inline" {
        spirv.Return
      }
      spirv.EntryPoint "Vertex" @foo
    }
  )mlir");

  SPIRVToLLVMTranslator Translator;
  llvm::Expected<Module> Result =
      Translator.translate(std::move(SpirvModule), Ctx);
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  EXPECT_EQ(Result->getKind(), Module::Kind::LLVMIR);
  llvm::Function *Foo = Result->getLLVMModule().getFunction("foo");
  ASSERT_NE(Foo, nullptr);
  EXPECT_FALSE(Foo->isDeclaration());
}

TEST(SPIRVToLLVMTranslatorTest, RejectsNonMLIRInput) {
  Context Ctx;
  llvm::LLVMContext &LLVMCtx = Ctx.getLLVMContext();
  Module NotSpirv =
      Module::fromLLVMIR(std::make_unique<llvm::Module>("m", LLVMCtx));

  SPIRVToLLVMTranslator Translator;
  llvm::Expected<Module> Result =
      Translator.translate(std::move(NotSpirv), Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(SPIRVToLLVMTranslatorTest, RejectsNonSpirvMLIROperation) {
  Context Ctx;
  mlir::MLIRContext &MLIRCtx = Ctx.getMLIRContext();
  mlir::OwningOpRef<mlir::ModuleOp> Op =
      mlir::ModuleOp::create(mlir::UnknownLoc::get(&MLIRCtx));
  Module NotSpirv = Module::fromMLIR(std::move(Op));

  SPIRVToLLVMTranslator Translator;
  llvm::Expected<Module> Result =
      Translator.translate(std::move(NotSpirv), Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

} // namespace

//===- SPIRVImporterTest.cpp - Tests for feme::SPIRVImporter -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/SPIRV/SPIRVImporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Builds a minimal, valid SPIR-V binary module by parsing hand-written
// `spirv` dialect text and serializing it with MLIR's own serializer,
// avoiding a checked-in binary fixture (see "Avoiding binary test fixtures"
// in feme/docs/Design.md).
static std::string buildMinimalSPIRVBinary() {
  mlir::MLIRContext ParseCtx;
  ParseCtx.loadDialect<mlir::spirv::SPIRVDialect>();

  static constexpr llvm::StringLiteral SourceText = R"mlir(
    spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
      spirv.func @foo() -> () "Inline" {
        spirv.Return
      }
      spirv.EntryPoint "Vertex" @foo
    }
  )mlir";

  mlir::ParserConfig Config(&ParseCtx);
  mlir::OwningOpRef<mlir::spirv::ModuleOp> SourceModule =
      mlir::parseSourceString<mlir::spirv::ModuleOp>(SourceText, Config);
  EXPECT_TRUE(SourceModule);

  llvm::SmallVector<uint32_t, 0> Binary;
  EXPECT_TRUE(mlir::succeeded(mlir::spirv::serialize(*SourceModule, Binary)));

  return std::string(reinterpret_cast<const char *>(Binary.data()),
                     Binary.size() * sizeof(uint32_t));
}

TEST(SPIRVImporterTest, GetFormatName) {
  SPIRVImporter Importer;
  EXPECT_EQ(Importer.getFormatName(), "spirv");
}

TEST(SPIRVImporterTest, ImportsValidBinaryIntoSpirvModuleOp) {
  std::string Binary = buildMinimalSPIRVBinary();

  Context Ctx;
  SPIRVImporter Importer;
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef(Binary, "spirv-test"), ImportOptions{}, Ctx);
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  EXPECT_EQ(Result->getKind(), Module::Kind::MLIR);
  auto SpirvModule =
      mlir::dyn_cast<mlir::spirv::ModuleOp>(Result->getMLIROperation());
  EXPECT_TRUE(SpirvModule);
}

TEST(SPIRVImporterTest, RejectsNonWordAlignedInput) {
  Context Ctx;
  SPIRVImporter Importer;
  // 3 bytes: not a multiple of 4, so cannot be a stream of SPIR-V words.
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef("abc", "spirv-test"), ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(SPIRVImporterTest, RejectsMalformedBinary) {
  Context Ctx;
  SPIRVImporter Importer;
  // 4 bytes of garbage: word-aligned, but not a valid SPIR-V module (wrong
  // magic number), so deserialization itself must fail.
  llvm::Expected<Module> Result =
      Importer.import(llvm::MemoryBufferRef(
                          llvm::StringRef("\xde\xad\xbe\xef", 4), "spirv-test"),
                      ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

} // namespace

//===- TargetMachineBackendTest.cpp - SPIR-V "null pipeline" test --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Exercises the SPIR-V retargeting "null pipeline" described in the
// deviation note in feme/docs/Design.md end to end:
//
//   SPIR-V binary -> SPIRVImporter -> spirv dialect
//                  -> SPIRVToLLVMTranslator -> llvm::Module
//                  -> TargetMachineBackend(spirv64) -> SPIR-V binary
//                  -> SPIRVImporter -> spirv dialect (round-tripped)
//
// validating the Translator/Backend plumbing by retargeting back to the
// format that was imported, rather than a real ISA.
//
//===----------------------------------------------------------------------===//

#include "feme/Target/TargetMachineBackend.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/SPIRV/SPIRVImporter.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

// Declared (rather than including llvm-c/Target.h, which would pull in
// every target's declarations) so this test only requires linking the
// SPIRV target's own codegen library.
extern "C" void LLVMInitializeSPIRVTargetInfo();
extern "C" void LLVMInitializeSPIRVTarget();
extern "C" void LLVMInitializeSPIRVTargetMC();
extern "C" void LLVMInitializeSPIRVAsmPrinter();

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

class TargetMachineBackendSpirvNullPipelineTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    // Only the SPIRV target's own init hooks are needed for this "null
    // pipeline" test (see feme/docs/Design.md's Retargeting to Native ISA
    // section); unlike llvm::InitializeAllTargets(), this doesn't require
    // linking every target configured into this LLVM build.
    LLVMInitializeSPIRVTargetInfo();
    LLVMInitializeSPIRVTarget();
    LLVMInitializeSPIRVTargetMC();
    LLVMInitializeSPIRVAsmPrinter();
  }
};

TEST_F(TargetMachineBackendSpirvNullPipelineTest, RoundTripsThroughLLVMIR) {
  std::string InputBinary = buildMinimalSPIRVBinary();

  Context Ctx;
  SPIRVImporter Importer;
  llvm::Expected<Module> Imported = Importer.import(
      llvm::MemoryBufferRef(InputBinary, "spirv-test"), ImportOptions{}, Ctx);
  ASSERT_THAT_EXPECTED(Imported, llvm::Succeeded());

  SPIRVToLLVMTranslator Translator;
  llvm::Expected<Module> Translated =
      Translator.translate(std::move(*Imported), Ctx);
  ASSERT_THAT_EXPECTED(Translated, llvm::Succeeded());

  BackendOptions Opts;
  Opts.TargetTriple = "spirv64-unknown-unknown";
  Opts.FileType = llvm::CodeGenFileType::ObjectFile;

  llvm::SmallVector<char, 0> OutputBinaryBuf;
  llvm::raw_svector_ostream OS(OutputBinaryBuf);
  TargetMachineBackend TheBackend;
  llvm::Error BackendErr =
      TheBackend.run(Translated->getLLVMModule(), Opts, OS);
  ASSERT_THAT_ERROR(std::move(BackendErr), llvm::Succeeded());
  ASSERT_FALSE(OutputBinaryBuf.empty());
  std::string OutputBinary(OutputBinaryBuf.begin(), OutputBinaryBuf.end());

  // Re-import the Backend's output with the same SPIRVImporter used above:
  // if the null pipeline round-tripped correctly, this must succeed and
  // recover the original entry point.
  llvm::Expected<Module> Reimported =
      Importer.import(llvm::MemoryBufferRef(OutputBinary, "spirv-roundtrip"),
                      ImportOptions{}, Ctx);
  ASSERT_THAT_EXPECTED(Reimported, llvm::Succeeded());

  auto SpirvModule =
      mlir::dyn_cast<mlir::spirv::ModuleOp>(Reimported->getMLIROperation());
  ASSERT_TRUE(SpirvModule);
  EXPECT_TRUE(SpirvModule.lookupSymbol<mlir::spirv::FuncOp>("foo"));
}

TEST_F(TargetMachineBackendSpirvNullPipelineTest, RejectsUnknownTargetTriple) {
  llvm::LLVMContext LLVMCtx;
  llvm::Module M("m", LLVMCtx);

  BackendOptions Opts;
  Opts.TargetTriple = "not-a-real-target-triple";

  llvm::SmallVector<char, 0> OutputBuf;
  llvm::raw_svector_ostream OS(OutputBuf);
  TargetMachineBackend TheBackend;
  llvm::Error Err = TheBackend.run(M, Opts, OS);
  EXPECT_THAT_ERROR(std::move(Err), llvm::Failed());
}

} // namespace

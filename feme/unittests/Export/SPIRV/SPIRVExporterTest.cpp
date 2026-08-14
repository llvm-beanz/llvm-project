//===- SPIRVExporterTest.cpp - Tests for feme::SPIRVExporter -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Export/SPIRV/SPIRVExporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Note: the "actually lowers to a SPIR-V binary module" case is
// deliberately not covered here, for the same reason DriverTest.cpp's
// similar note gives: it requires a registered SPIRV LLVM target, which
// this unittest binary does not initialize -- see
// test/Tools/feme/feme-*.test for that end-to-end coverage instead.

TEST(SPIRVExporterTest, GetFormatName) {
  SPIRVExporter Exporter;
  EXPECT_EQ(Exporter.getFormatName(), "spirv");
}

TEST(SPIRVExporterTest, FailsGracefullyWithoutARegisteredSPIRVTarget) {
  // This unittest binary does not call llvm::InitializeAllTargets() (see
  // DriverTest.cpp's identical note): exportModule must fail with a clean
  // Error, not crash, when the SPIRV target it delegates to
  // (TargetMachineBackend) isn't registered.
  Context Ctx;
  Module Mod = Module::fromLLVMIR(std::make_unique<llvm::Module>(
      "spirv-exporter-test", Ctx.getLLVMContext()));

  SPIRVExporter Exporter;
  llvm::SmallVector<char, 0> Output;
  llvm::raw_svector_ostream OS(Output);
  llvm::Error Err = Exporter.exportModule(Mod, ExportOptions{}, Ctx, OS);
  EXPECT_THAT_ERROR(std::move(Err), llvm::Failed());
}

} // namespace

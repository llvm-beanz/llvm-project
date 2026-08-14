//===- DXILExporterTest.cpp - Tests for feme::DXILExporter ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Export/DXIL/DXILExporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Note: the "actually lowers to a DXContainer" case is deliberately not
// covered here, for the same reason DriverTest.cpp's similar note gives:
// it requires a registered DirectX LLVM target, which this unittest binary
// does not initialize -- see test/Tools/feme/feme-*.test for that
// end-to-end coverage instead.

TEST(DXILExporterTest, GetFormatName) {
  DXILExporter Exporter;
  EXPECT_EQ(Exporter.getFormatName(), "dxil");
}

TEST(DXILExporterTest, FailsGracefullyWithoutARegisteredDirectXTarget) {
  // This unittest binary does not call llvm::InitializeAllTargets() (see
  // DriverTest.cpp's identical note): exportModule must fail with a clean
  // Error, not crash, when the DirectX target it delegates to
  // (TargetMachineBackend) isn't registered.
  Context Ctx;
  Module Mod = Module::fromLLVMIR(std::make_unique<llvm::Module>(
      "dxil-exporter-test", Ctx.getLLVMContext()));

  DXILExporter Exporter;
  llvm::SmallVector<char, 0> Output;
  llvm::raw_svector_ostream OS(Output);
  llvm::Error Err = Exporter.exportModule(Mod, ExportOptions{}, Ctx, OS);
  EXPECT_THAT_ERROR(std::move(Err), llvm::Failed());
}

} // namespace

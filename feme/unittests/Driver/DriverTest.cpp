//===- DriverTest.cpp - Tests for feme::Driver ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Driver/Driver.h"

#include "feme/Core/Context.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Note: the "actually imports/retargets a real DXIL or SPIR-V module" cases
// are deliberately not covered here. They require both a real binary-format
// input (see the precedent in DXILImporterTest.cpp/SPIRVImporterTest.cpp)
// and a registered LLVM target (DirectX/SPIRV/AMDGPU) to retarget to, which
// this unittest binary does not initialize -- see
// `test/Tools/feme/feme-*.test`, which exercise those end to end through
// the `feme` CLI (built into a full LLVM build with those targets
// registered) instead.

TEST(DriverTest, RejectsUnsupportedFromFormat) {
  Context Ctx;
  Driver D(Ctx);

  frontend::DriverOptions Opts;
  Opts.From = "dxbc"; // Not yet implemented -- see feme/docs/Design.md.
  Opts.Target = "spirv";

  llvm::Expected<DriverResult> Result =
      D.run(llvm::MemoryBufferRef("", "driver-test"), Opts);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(DriverTest, RejectsMissingTarget) {
  // A minimal, but real and valid, LLVM bitcode module: DXILImporter accepts
  // raw bitcode on its own (no DXContainer needed), so this reaches Driver's
  // --target resolution rather than failing at the import step, unlike
  // an empty/malformed buffer would.
  llvm::LLVMContext LLVMCtx;
  llvm::Module M("driver-test", LLVMCtx);
  llvm::SmallVector<char, 0> Bitcode;
  llvm::raw_svector_ostream OS(Bitcode);
  llvm::WriteBitcodeToFile(M, OS);

  Context Ctx;
  Driver D(Ctx);

  frontend::DriverOptions Opts;
  Opts.From = "dxil";
  // Opts.Target is not set.

  llvm::Expected<DriverResult> Result =
      D.run(llvm::MemoryBufferRef(
                llvm::StringRef(Bitcode.data(), Bitcode.size()), "driver-test"),
            Opts);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(DriverTest, RejectsMalformedInputForRequestedFromFormat) {
  Context Ctx;
  Driver D(Ctx);

  frontend::DriverOptions Opts;
  Opts.From = "spirv";
  Opts.Target = "spirv";

  // 4 bytes of garbage: word-aligned, but not a valid SPIR-V module, so
  // Importer::import itself must fail before Driver gets anywhere near
  // translation/retargeting.
  llvm::Expected<DriverResult> Result =
      D.run(llvm::MemoryBufferRef(llvm::StringRef("\xde\xad\xbe\xef", 4),
                                  "driver-test"),
            Opts);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

} // namespace

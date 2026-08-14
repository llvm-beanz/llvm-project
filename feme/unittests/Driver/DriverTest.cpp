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
#include "llvm/ObjectYAML/DXContainerYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
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

TEST(DriverTest, RejectsUndetectableFormat) {
  Context Ctx;
  Driver D(Ctx);

  frontend::DriverOptions Opts;
  Opts.Target = "spirv";

  // Empty input matches none of Driver's format-detection magic numbers
  // (see feme::detectFormat), so this exercises that rather than any
  // particular Importer's own parsing.
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
  // Opts.Target is not set.

  llvm::Expected<DriverResult> Result =
      D.run(llvm::MemoryBufferRef(
                llvm::StringRef(Bitcode.data(), Bitcode.size()), "driver-test"),
            Opts);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(DriverTest, RejectsMalformedInputForDetectedSPIRVFormat) {
  Context Ctx;
  Driver D(Ctx);

  frontend::DriverOptions Opts;
  Opts.Target = "spirv";

  // Starts with the SPIR-V magic number (see feme::detectFormat) but is
  // shorter than the mandatory 5-word header, so Importer::import itself
  // must fail before Driver gets anywhere near translation/retargeting.
  llvm::Expected<DriverResult> Result =
      D.run(llvm::MemoryBufferRef(
                llvm::StringRef("\x03\x02\x23\x07\x00\x00\x00\x00", 8),
                "driver-test"),
            Opts);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(DriverTest, RejectsMalformedInputForDetectedDXContainerFormat) {
  Context Ctx;
  Driver D(Ctx);

  frontend::DriverOptions Opts;
  Opts.Target = "dxil";

  // Starts with the "DXBC" container magic (see feme::detectFormat), a
  // distinct detection path from the raw-bitcode one RejectsMissingTarget
  // exercises, but is not a well-formed DXContainer, so DXILImporter itself
  // must fail before Driver gets anywhere near translation/retargeting.
  llvm::Expected<DriverResult> Result =
      D.run(llvm::MemoryBufferRef("DXBC", "driver-test"), Opts);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(DriverTest, RejectsDXBCContainerWithNoShaderBytecodePart) {
  Context Ctx;
  Driver D(Ctx);

  frontend::DriverOptions Opts;
  Opts.Target = "dxil";

  // A well-formed but empty DXContainer: feme::detectFormat's inner-part
  // scan (see the DXBC section of feme/docs/Design.md) finds neither a
  // "SHEX"/"SHDR" part (which would select feme::DXBCImporter) nor a "DXIL"
  // part, so this still falls back to feme::DXILImporter, which then fails
  // on the missing DXIL part -- exercising that fallback rather than
  // feme::DXBCImporter's own parsing.
  llvm::DXContainerYAML::Object Obj;
  Obj.Header.Hash.assign(16, llvm::yaml::Hex8(0));
  Obj.Header.Version.Major = 1;
  Obj.Header.Version.Minor = 0;
  Obj.Header.PartCount = 0;

  llvm::SmallString<0> Binary;
  llvm::raw_svector_ostream OS(Binary);
  ASSERT_TRUE(llvm::yaml::yaml2dxcontainer(
      Obj, OS, [](const llvm::Twine &Msg) { FAIL() << Msg.str(); }));

  llvm::Expected<DriverResult> Result =
      D.run(llvm::MemoryBufferRef(OS.str(), "driver-test"), Opts);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

} // namespace

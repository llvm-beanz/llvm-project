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
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Note: the "imports a valid SPIR-V binary into a `spirv.module`" case is
// deliberately not covered here. It requires building a real serialized
// SPIR-V binary, which is exactly the kind of binary-format round trip
// `feme-translate` exists to exercise via `lit`/`FileCheck` instead (see
// "Testing Strategy" in feme/docs/Design.md); see
// `test/Feme/Import/SPIRV/spirv-import.mlir`.

TEST(SPIRVImporterTest, GetFormatName) {
  SPIRVImporter Importer;
  EXPECT_EQ(Importer.getFormatName(), "spirv");
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

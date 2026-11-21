//===- DXBCImporterTest.cpp - Tests for feme::DXBCImporter ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/DXBC/DXBCImporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "llvm/ObjectYAML/DXContainerYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Note: the "imports a real SHEX/SHDR part" case is deliberately not
// covered here, matching DXILImporterTest.cpp's precedent: building a real
// tokenized-bytecode fixture is exactly what `dxbc-as`/`feme-translate
// --import-dxbc` exist to exercise via `lit`/`FileCheck` instead (see
// "Testing Strategy" in feme/docs/Design.md) -- see
// `test/Import/DXBC/dxbc-import.dxasm`.

TEST(DXBCImporterTest, GetFormatName) {
  DXBCImporter Importer;
  EXPECT_EQ(Importer.getFormatName(), "dxbc");
}

TEST(DXBCImporterTest, DetectsShaderPartsWithoutParsingBytecode) {
  for (const char *Name : {"SHEX", "SHDR", "PRIV"}) {
    llvm::DXContainerYAML::Object Obj;
    Obj.Header.Hash.assign(16, llvm::yaml::Hex8(0));
    Obj.Header.Version.Major = 1;
    Obj.Header.Version.Minor = 0;
    Obj.Header.PartCount = 1;
    Obj.Parts.emplace_back(Name, 0);
    llvm::SmallString<0> Binary;
    llvm::raw_svector_ostream OS(Binary);
    ASSERT_TRUE(llvm::yaml::yaml2dxcontainer(
        Obj, OS, [](const llvm::Twine &Msg) { FAIL() << Msg.str(); }));
    auto Legacy =
        containsDXBCShader(llvm::MemoryBufferRef(OS.str(), "format-detection"));
    ASSERT_THAT_EXPECTED(Legacy, llvm::Succeeded());
    EXPECT_EQ(*Legacy, llvm::StringRef(Name) != "PRIV");
  }
}

TEST(DXBCImporterTest, DetectionRejectsMalformedContainers) {
  auto Legacy =
      containsDXBCShader(llvm::MemoryBufferRef("DXBC", "truncated-container"));
  EXPECT_THAT_EXPECTED(Legacy, llvm::Failed());
  auto NonContainer = containsDXBCShader(
      llvm::MemoryBufferRef("not a container", "format-detection"));
  ASSERT_THAT_EXPECTED(NonContainer, llvm::Succeeded());
  EXPECT_FALSE(*NonContainer);
}

TEST(DXBCImporterTest, RejectsDXContainerWithNoShaderBytecodePart) {
  llvm::DXContainerYAML::Object Obj;
  Obj.Header.Hash.assign(16, llvm::yaml::Hex8(0));
  Obj.Header.Version.Major = 1;
  Obj.Header.Version.Minor = 0;
  Obj.Header.PartCount = 0;

  llvm::SmallString<0> Binary;
  llvm::raw_svector_ostream OS(Binary);
  ASSERT_TRUE(llvm::yaml::yaml2dxcontainer(
      Obj, OS, [](const llvm::Twine &Msg) { FAIL() << Msg.str(); }));

  auto Legacy =
      containsDXBCShader(llvm::MemoryBufferRef(OS.str(), "empty-container"));
  ASSERT_THAT_EXPECTED(Legacy, llvm::Succeeded());
  EXPECT_FALSE(*Legacy);

  Context Ctx;
  DXBCImporter Importer;
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef(OS.str(), "dxbc-test"), ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(DXBCImporterTest, RejectsInputThatIsNotADXContainer) {
  Context Ctx;
  DXBCImporter Importer;
  llvm::Expected<Module> Result =
      Importer.import(llvm::MemoryBufferRef("not a DXContainer", "dxbc-test"),
                      ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

} // namespace

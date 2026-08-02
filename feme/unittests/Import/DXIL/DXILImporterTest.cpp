//===- DXILImporterTest.cpp - Tests for feme::DXILImporter ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/DXIL/DXILImporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "llvm/ObjectYAML/DXContainerYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Note: the "imports raw DXIL bitcode" and "imports bitcode wrapped in a
// DXContainer" cases are deliberately not covered here. Both require
// building a real DXIL/DXContainer binary, which is exactly the kind of
// binary-format round trip `feme-translate` exists to exercise via
// `lit`/`FileCheck` instead (see "Testing Strategy" in
// feme/docs/Design.md) -- and, since DXIL is a distinct, frozen-version
// dialect of LLVM IR rather than current LLVM IR, a real `DXContainer`
// produced by an actual DXIL-targeting backend (`llc` with a `dxil-...`
// triple) is a materially better fixture than one hand-assembled in-process
// from current-LLVM-IR bitcode; see `test/Feme/Import/DXIL/dxil-import.ll`
// and `test/Feme/Import/DXIL/dxil-import-container.ll`.

TEST(DXILImporterTest, GetFormatName) {
  DXILImporter Importer;
  EXPECT_EQ(Importer.getFormatName(), "dxil");
}

TEST(DXILImporterTest, RejectsDXContainerWithNoDXILPart) {
  llvm::DXContainerYAML::Object Obj;
  Obj.Header.Hash.assign(16, llvm::yaml::Hex8(0));
  Obj.Header.Version.Major = 1;
  Obj.Header.Version.Minor = 0;
  Obj.Header.PartCount = 0;

  llvm::SmallString<0> Binary;
  llvm::raw_svector_ostream OS(Binary);
  ASSERT_TRUE(llvm::yaml::yaml2dxcontainer(
      Obj, OS, [](const llvm::Twine &Msg) { FAIL() << Msg.str(); }));

  Context Ctx;
  DXILImporter Importer;
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef(OS.str(), "dxil-test"), ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(DXILImporterTest, RejectsInputThatIsNeitherContainerNorBitcode) {
  Context Ctx;
  DXILImporter Importer;
  llvm::Expected<Module> Result =
      Importer.import(llvm::MemoryBufferRef("not dxil or bitcode", "dxil-test"),
                      ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

} // namespace

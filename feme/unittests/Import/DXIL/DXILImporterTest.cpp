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
#include "llvm/AsmParser/Parser.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/ObjectYAML/DXContainerYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

// Assembles a minimal, valid module's worth of LLVM bitcode by parsing
// hand-written IR text and writing it out with LLVM's own bitcode writer,
// avoiding a checked-in binary fixture (see "Avoiding binary test fixtures"
// in feme/docs/Design.md).
static std::string buildMinimalBitcode() {
  static constexpr llvm::StringLiteral SourceText = R"ll(
    define i32 @add(i32 %a, i32 %b) {
      %sum = add i32 %a, %b
      ret i32 %sum
    }
  )ll";

  llvm::LLVMContext ParseCtx;
  llvm::SMDiagnostic Err;
  std::unique_ptr<llvm::Module> M =
      llvm::parseAssemblyString(SourceText, Err, ParseCtx);
  EXPECT_TRUE(M) << Err.getMessage().str();

  llvm::SmallString<0> Bitcode;
  llvm::raw_svector_ostream OS(Bitcode);
  llvm::WriteBitcodeToFile(*M, OS);
  return std::string(Bitcode.begin(), Bitcode.end());
}

// Wraps \p Bitcode in a minimal DXContainer with a single "DXIL" program
// part, using LLVM's own (already-upstream) DXContainer YAML emitter rather
// than hand-encoding container bytes (see "Avoiding binary test fixtures"
// in feme/docs/Design.md).
static std::string buildDXContainer(llvm::StringRef Bitcode) {
  llvm::DXContainerYAML::Object Obj;
  Obj.Header.Hash.assign(16, llvm::yaml::Hex8(0));
  Obj.Header.Version.Major = 1;
  Obj.Header.Version.Minor = 0;
  Obj.Header.PartCount = 1;

  // The outer container Part.Size (the `DXIL` PartHeader's size field) is
  // not auto-computed by yaml2dxcontainer, unlike Program.Size; it must
  // equal the ProgramHeader plus the bitcode it embeds.
  uint32_t PartSize = sizeof(llvm::dxbc::ProgramHeader) + Bitcode.size();
  llvm::DXContainerYAML::Part Part("DXIL", PartSize);
  llvm::DXContainerYAML::DXILProgram Program{};
  Program.MajorVersion = 6;
  Program.MinorVersion = 5;
  Program.ShaderKind = 6; // Library shader, arbitrary for this test.
  Program.DXILMajorVersion = 1;
  Program.DXILMinorVersion = 5;
  Program.DXIL.emplace(Bitcode.size());
  llvm::copy(llvm::ArrayRef(reinterpret_cast<const uint8_t *>(Bitcode.data()),
                            Bitcode.size()),
             Program.DXIL->begin());
  Part.Program = Program;
  Obj.Parts.push_back(Part);

  llvm::SmallString<0> Binary;
  llvm::raw_svector_ostream OS(Binary);
  bool Succeeded = llvm::yaml::yaml2dxcontainer(
      Obj, OS, [](const llvm::Twine &Msg) { FAIL() << Msg.str(); });
  EXPECT_TRUE(Succeeded);
  return std::string(Binary.begin(), Binary.end());
}

TEST(DXILImporterTest, GetFormatName) {
  DXILImporter Importer;
  EXPECT_EQ(Importer.getFormatName(), "dxil");
}

TEST(DXILImporterTest, ImportsRawBitcode) {
  std::string Bitcode = buildMinimalBitcode();

  Context Ctx;
  DXILImporter Importer;
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef(Bitcode, "dxil-test"), ImportOptions{}, Ctx);
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  EXPECT_EQ(Result->getKind(), Module::Kind::LLVMIR);
  EXPECT_TRUE(Result->getLLVMModule().getFunction("add"));
}

TEST(DXILImporterTest, ImportsBitcodeWrappedInDXContainer) {
  std::string Bitcode = buildMinimalBitcode();
  std::string Container = buildDXContainer(Bitcode);

  Context Ctx;
  DXILImporter Importer;
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef(Container, "dxil-test"), ImportOptions{}, Ctx);
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  EXPECT_EQ(Result->getKind(), Module::Kind::LLVMIR);
  EXPECT_TRUE(Result->getLLVMModule().getFunction("add"));
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

//===- ResourceInfoTest.cpp - Tests for feme::cpu::ResourceInfo ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/ResourceInfo.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("ResourceInfoTest", errs());
  return M;
}

TEST(ResourceInfoTest, FromModuleReadsMetadata) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    !feme.cpu.resources = !{!0}
    !0 = !{!"main", i32 0, i1 false, i32 3, i32 5}
  )");
  ASSERT_TRUE(M);

  std::optional<ResourceInfo> Info = ResourceInfo::fromModule(*M, "main");
  ASSERT_TRUE(Info);
  EXPECT_EQ(Info->EntryName, "main");
  EXPECT_EQ(Info->RootConstantSize, 0u);
  EXPECT_FALSE(Info->UsesSamplerHeap);
  EXPECT_EQ(Info->StaticHeapIndices, (std::vector<uint32_t>{3, 5}));
}

TEST(ResourceInfoTest, FromModuleMissingEntryIsNullopt) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    !feme.cpu.resources = !{!0}
    !0 = !{!"other", i32 0, i1 false}
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(ResourceInfo::fromModule(*M, "main").has_value());
}

TEST(ResourceInfoTest, FromModuleNoMetadataIsNullopt) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, "");
  ASSERT_TRUE(M);
  EXPECT_FALSE(ResourceInfo::fromModule(*M, "main").has_value());
}

TEST(ResourceInfoTest, ArtifactSymbolName) {
  EXPECT_EQ(getArtifactSymbolName("main"), "feme_cpu_info_main");
}

TEST(ResourceInfoTest, SerializeParseRoundTrips) {
  ArtifactInfo Info;
  Info.WaveSize = 8;
  Info.GroupSize[0] = 64;
  Info.GroupSize[1] = 1;
  Info.GroupSize[2] = 1;
  Info.RootConstantSize = 16;
  Info.Flags = FEME_CPU_ARTIFACT_USES_SAMPLER_HEAP;
  Info.StaticHeapIndices = {2, 7, 9};

  std::vector<uint8_t> Bytes = serializeArtifact(Info);
  Expected<ArtifactInfo> Parsed = parseArtifact(Bytes);
  ASSERT_THAT_EXPECTED(Parsed, Succeeded());
  EXPECT_EQ(Parsed->WaveSize, 8u);
  EXPECT_EQ(Parsed->GroupSize[0], 64u);
  EXPECT_EQ(Parsed->GroupSize[1], 1u);
  EXPECT_EQ(Parsed->GroupSize[2], 1u);
  EXPECT_EQ(Parsed->RootConstantSize, 16u);
  EXPECT_EQ(Parsed->Flags,
            static_cast<uint32_t>(FEME_CPU_ARTIFACT_USES_SAMPLER_HEAP));
  EXPECT_EQ(Parsed->StaticHeapIndices, (std::vector<uint32_t>{2, 7, 9}));
}

TEST(ResourceInfoTest, ParseRejectsTooShort) {
  std::vector<uint8_t> Bytes = {1, 2, 3};
  Expected<ArtifactInfo> Parsed = parseArtifact(Bytes);
  EXPECT_THAT_EXPECTED(std::move(Parsed), Failed());
}

TEST(ResourceInfoTest, ParseRejectsWrongVersion) {
  ArtifactInfo Info;
  std::vector<uint8_t> Bytes = serializeArtifact(Info);
  // Corrupt the version field (the first little-endian uint32_t).
  Bytes[0] = 0xFF;
  Expected<ArtifactInfo> Parsed = parseArtifact(Bytes);
  EXPECT_THAT_ERROR(
      Parsed.takeError(),
      Failed<StringError>(testing::Property(
          &StringError::getMessage, testing::HasSubstr("ABI version"))));
}

TEST(ResourceInfoTest, ParseRejectsInconsistentHeapIndexCount) {
  ArtifactInfo Info;
  Info.StaticHeapIndices = {1, 2, 3};
  std::vector<uint8_t> Bytes = serializeArtifact(Info);
  Bytes.pop_back(); // Truncate one heap index short.
  Bytes.pop_back();
  Bytes.pop_back();
  Bytes.pop_back();
  Expected<ArtifactInfo> Parsed = parseArtifact(Bytes);
  EXPECT_THAT_EXPECTED(std::move(Parsed), Failed());
}

TEST(ResourceInfoTest, EmitAndReadArtifactGlobalRoundTrips) {
  LLVMContext Ctx;
  Module M("M", Ctx);
  ArtifactInfo Info;
  Info.WaveSize = 4;
  Info.RootConstantSize = 8;
  Info.StaticHeapIndices = {0, 1};

  GlobalVariable *GV = emitArtifactGlobal(M, "main", Info);
  ASSERT_TRUE(GV);
  EXPECT_EQ(GV->getName(), "feme_cpu_info_main");

  std::optional<ArtifactInfo> RoundTripped = readArtifactGlobal(M, "main");
  ASSERT_TRUE(RoundTripped);
  EXPECT_EQ(RoundTripped->WaveSize, 4u);
  EXPECT_EQ(RoundTripped->RootConstantSize, 8u);
  EXPECT_EQ(RoundTripped->StaticHeapIndices, (std::vector<uint32_t>{0, 1}));
}

TEST(ResourceInfoTest, ReadArtifactGlobalMissingIsNullopt) {
  LLVMContext Ctx;
  Module M("M", Ctx);
  EXPECT_FALSE(readArtifactGlobal(M, "main").has_value());
}

TEST(ResourceInfoTest, FromResourceInfoCarriesFieldsOver) {
  ResourceInfo RI;
  RI.EntryName = "main";
  RI.RootConstantSize = 32;
  RI.UsesSamplerHeap = true;
  RI.StaticHeapIndices = {4};

  ArtifactInfo Artifact = ArtifactInfo::fromResourceInfo(RI);
  EXPECT_EQ(Artifact.RootConstantSize, 32u);
  EXPECT_EQ(Artifact.Flags,
            static_cast<uint32_t>(FEME_CPU_ARTIFACT_USES_SAMPLER_HEAP));
  EXPECT_EQ(Artifact.StaticHeapIndices, (std::vector<uint32_t>{4}));
  // Execution-shape fields aren't sourced from ResourceInfo yet.
  EXPECT_EQ(Artifact.WaveSize, 0u);
}

} // namespace

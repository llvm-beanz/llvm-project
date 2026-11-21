//===- RootBindingPlanTest.cpp - Tests for DirectX root layouts ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/RootBindingPlan.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <vector>

using namespace llvm;
using namespace feme::dxil;

namespace {

struct ParameterData {
  uint32_t Kind;
  uint32_t Visibility;
  std::vector<uint32_t> Data;
};

void appendWord(std::vector<uint8_t> &Bytes, uint32_t Word) {
  size_t Offset = Bytes.size();
  Bytes.resize(Offset + 4);
  support::endian::write32le(Bytes.data() + Offset, Word);
}

void setWord(std::vector<uint8_t> &Bytes, unsigned Index, uint32_t Word) {
  support::endian::write32le(Bytes.data() + Index * 4, Word);
}

std::vector<uint8_t> root(uint32_t Version, ArrayRef<ParameterData> Parameters,
                          ArrayRef<std::vector<uint32_t>> Samplers = {}) {
  std::vector<uint8_t> Bytes;
  for (uint32_t Word : {Version, uint32_t(Parameters.size()), 24u,
                        uint32_t(Samplers.size()), 0u, 0u})
    appendWord(Bytes, Word);
  size_t Payload = 24 + 12 * Parameters.size();
  for (const ParameterData &Parameter : Parameters) {
    for (uint32_t Word :
         {Parameter.Kind, Parameter.Visibility, uint32_t(Payload)})
      appendWord(Bytes, Word);
    Payload += Parameter.Data.size() * 4;
  }
  for (const ParameterData &Parameter : Parameters) {
    size_t Offset = Bytes.size();
    for (auto [Index, Word] : enumerate(Parameter.Data))
      appendWord(Bytes, Parameter.Kind == 0 && Index == 1 ? uint32_t(Offset + 8)
                                                          : Word);
  }
  setWord(Bytes, 4, Bytes.size());
  for (const auto &Sampler : Samplers)
    for (uint32_t Word : Sampler)
      appendWord(Bytes, Word);
  return Bytes;
}

ParameterData table(uint32_t Version, std::vector<uint32_t> Ranges,
                    uint32_t Visibility = 0) {
  uint32_t Count = Ranges.size() / (Version == 1 ? 5 : 6);
  std::vector<uint32_t> Data{Count, 0};
  Data.insert(Data.end(), Ranges.begin(), Ranges.end());
  return {0, Visibility, std::move(Data)};
}

std::vector<uint32_t> sampler(uint32_t Register = 3, uint32_t Space = 7,
                              uint32_t Visibility = 0) {
  return {0x55, 1,          2,          3,        0x3f800000, 16,        4,
          2,    0x40000000, 0x40800000, Register, Space,      Visibility};
}

void expectError(ArrayRef<uint8_t> Bytes, StringRef Message) {
  auto Plan = RootBindingPlan::parse(Bytes);
  ASSERT_FALSE(bool(Plan));
  EXPECT_TRUE(StringRef(toString(Plan.takeError())).contains(Message));
}

TEST(RootBindingPlanTest, AllParameterKindsAndOwnedStorage) {
  auto Bytes = root(2,
                    {{1, 0, {3, 7, 4}},
                     {2, 0, {4, 7, 4}},
                     {3, 0, {3, 7, 8}},
                     {4, 0, {3, 7, 2}},
                     table(2, {2, 2, 5, 7, 4, UINT32_MAX})},
                    {sampler()});
  auto Plan = RootBindingPlan::parse(Bytes);
  ASSERT_TRUE(bool(Plan));
  std::fill(Bytes.begin(), Bytes.end(), 0);
  Bytes.clear();
  EXPECT_EQ(Plan->getVersion(), 2u);
  EXPECT_EQ(Plan->getDWORDCost(), 11u);
  ASSERT_EQ(Plan->getParameters().size(), 5u);
  EXPECT_EQ(Plan->getParameters()[0].ConstantCount, 4u);
  EXPECT_EQ(Plan->getParameters()[1].Bindings[0].Flags, 4u);
  EXPECT_EQ(Plan->getParameters()[4].Bindings[0].TableOffset, 0u);
  ASSERT_EQ(Plan->getStaticSamplers().size(), 1u);
  const auto &Sampler = Plan->getStaticSamplers()[0];
  EXPECT_EQ(Sampler.AddressV, 2u);
  EXPECT_EQ(Sampler.AddressW, 3u);
  EXPECT_EQ(Sampler.MipLODBias, 1.f);
  EXPECT_EQ(Sampler.MinLOD, 2.f);
  EXPECT_EQ(Sampler.MaxLOD, 4.f);
  EXPECT_TRUE(Plan->findBinding(dxil::ResourceClass::CBuffer, 7, 6,
                                dxbc::ShaderVisibility::All));
  EXPECT_FALSE(Plan->findBinding(dxil::ResourceClass::CBuffer, 7, 7,
                                 dxbc::ShaderVisibility::All));
  std::string Printed;
  raw_string_ostream OS(Printed);
  Plan->print(OS);
  EXPECT_TRUE(
      StringRef(Printed).contains("dwords=11 parameters=5 static-samplers=1"));
  EXPECT_TRUE(StringRef(Printed).contains(
      "cbv: space=7 register=5 count=2 offset=0 flags=4"));
}

TEST(RootBindingPlanTest, Version10AndAppendOffsets) {
  auto Plan = RootBindingPlan::parse(
      root(1, {table(1, {0, 2, 3, 7, 5, 1, 1, 3, 7, UINT32_MAX,
                         2, 2, 5, 1, 1, 0, 1, 9, 0, UINT32_MAX})}));
  ASSERT_TRUE(bool(Plan));
  const auto &Bindings = Plan->getParameters()[0].Bindings;
  ASSERT_EQ(Bindings.size(), 4u);
  EXPECT_EQ(Bindings[0].TableOffset, 5u);
  EXPECT_EQ(Bindings[1].TableOffset, 7u);
  EXPECT_EQ(Bindings[2].TableOffset, 1u);
  EXPECT_EQ(Bindings[3].TableOffset, 3u);
  EXPECT_EQ(Bindings[0].Flags, 0u);
  auto Descriptor = RootBindingPlan::parse(root(1, {{2, 0, {3, 7}}}));
  ASSERT_TRUE(bool(Descriptor));
  EXPECT_EQ(Descriptor->getParameters()[0].Bindings[0].Flags, 0u);
}

TEST(RootBindingPlanTest, SeparateClassesAndDisjointVisibility) {
  auto Plan = RootBindingPlan::parse(root(2, {{2, 1, {0, 0, 0}},
                                              {2, 5, {0, 0, 0}},
                                              {3, 0, {0, 0, 0}},
                                              {4, 0, {0, 0, 0}}}));
  ASSERT_TRUE(bool(Plan));
  auto *Vertex = Plan->findBinding(dxil::ResourceClass::CBuffer, 0, 0,
                                   dxbc::ShaderVisibility::Vertex);
  auto *Pixel = Plan->findBinding(dxil::ResourceClass::CBuffer, 0, 0,
                                  dxbc::ShaderVisibility::Pixel);
  ASSERT_TRUE(Vertex);
  ASSERT_TRUE(Pixel);
  EXPECT_NE(Vertex, Pixel);
  EXPECT_FALSE(Plan->findBinding(dxil::ResourceClass::CBuffer, 0, 0,
                                 dxbc::ShaderVisibility::All));
  expectError(root(2, {{2, 1, {0, 0, 0}}, {2, 0, {0, 0, 0}}}),
              "bindings overlap");
  expectError(root(2, {{1, 0, {0, 0, 1}}, {2, 0, {0, 0, 0}}}),
              "bindings overlap");
}

TEST(RootBindingPlanTest, UnboundedRangesAndExplicitFollowingOffsets) {
  auto Plan = RootBindingPlan::parse(
      root(2, {table(2, {0, UINT32_MAX, 3, 7, 0, 5, 1, 1, 0, 0, 0, 0})}));
  ASSERT_TRUE(bool(Plan));
  EXPECT_TRUE(Plan->findBinding(dxil::ResourceClass::SRV, 7, UINT32_MAX,
                                dxbc::ShaderVisibility::All));
  EXPECT_FALSE(Plan->findBinding(dxil::ResourceClass::SRV, 7, 2,
                                 dxbc::ShaderVisibility::All));
  expectError(root(2, {table(2, {0, UINT32_MAX, 3, 7, 0, 0, 1, 1, 0, 0, 0,
                                 UINT32_MAX})}),
              "APPEND offset");
}

TEST(RootBindingPlanTest, RejectsDescriptorRangeConflictsAndOverflow) {
  expectError(root(2, {table(2, {0, 2, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1})}),
              "offsets overlap");
  expectError(root(2, {table(2, {0, 2, 0, 0, 0, 0, 0, 1, 1, 0, 0, 2})}),
              "bindings overlap");
  expectError(root(2, {table(2, {0, 1, 0, 0, 0, 0, 3, 1, 0, 0, 0, 1})}),
              "share a table");
  expectError(root(2, {table(2, {0, 2, UINT32_MAX, 0, 0, 0})}),
              "register range overflows");
  expectError(root(2, {table(2, {0, 2, 0, 0, 0, UINT32_MAX - 1})}),
              "offset overflows");
  expectError(root(2, {table(2, {0, 0, 0, 0, 0, 0})}), "type or count");
  expectError(root(2, {table(2, {})}), "nonempty");
}

TEST(RootBindingPlanTest, RootCostAndFlagValidation) {
  auto Plan = RootBindingPlan::parse(root(2, {{1, 0, {0, 0, 64}}}));
  ASSERT_TRUE(bool(Plan));
  EXPECT_EQ(Plan->getDWORDCost(), 64u);
  expectError(root(2, {{1, 0, {0, 0, 65}}}), "64 DWORDs");
  expectError(root(2, {{1, 0, {0, 0, 64}}, {3, 0, {0, 0, 0}}}), "64 DWORDs");
  expectError(root(2, {{1, 0, {0, 0, 0}}}), "nonempty");
  expectError(root(2, {{2, 0, {0, 0, 6}}}), "descriptor flags");
  expectError(root(2, {table(2, {0, 1, 0, 0, 6, 0})}), "range flags");
  expectError(root(2, {table(2, {0, 1, 0, 0, 0x10001, 0})}), "range flags");
  expectError(root(2, {table(2, {3, 1, 0, 0, 2, 0})}), "range flags");
  auto Bytes = root(2, {});
  setWord(Bytes, 5, 0x80000000);
  expectError(Bytes, "unknown root flags");
  setWord(Bytes, 5, 0xc00);
  EXPECT_TRUE(bool(RootBindingPlan::parse(Bytes)));
}

TEST(RootBindingPlanTest, StaticSamplerConflictsAndEnums) {
  expectError(root(2, {table(2, {3, 2, 2, 7, 0, 0})}, {sampler()}),
              "overlaps a descriptor range");
  expectError(root(2, {}, {sampler(), sampler()}), "bindings overlap");
  auto Plan =
      RootBindingPlan::parse(root(2, {}, {sampler(3, 7, 1), sampler(3, 7, 5)}));
  ASSERT_TRUE(bool(Plan));
  EXPECT_EQ(Plan->getDWORDCost(), 0u);
  auto Bad = sampler();
  Bad[0] = UINT32_MAX;
  expectError(root(2, {}, {Bad}), "sampler enum");
  expectError(root(2, {{2, 99, {0, 0, 0}}}), "visibility");
}

TEST(RootBindingPlanTest, ChecksEveryTruncatedPrefixAndArrayOverflow) {
  auto Bytes = root(2, {table(2, {0, 2, 0, 0, 0, 0})}, {sampler()});
  for (size_t Size = 0; Size != Bytes.size(); ++Size) {
    auto Plan = RootBindingPlan::parse(ArrayRef(Bytes).take_front(Size));
    ASSERT_FALSE(bool(Plan)) << Size;
    consumeError(Plan.takeError());
  }
  for (unsigned Field : {1, 2, 3, 4, 8, 9, 10}) {
    auto Bad = Bytes;
    setWord(Bad, Field, UINT32_MAX);
    expectError(Bad, "bounds");
  }
  expectError(root(0, {}), "version");
  expectError(root(3, {}), "versions 1.0 and 1.1");
  expectError(root(2, {{99, 0, {0, 0, 0}}}), "parameter type");
}

TEST(RootBindingPlanTest, HonorsNoncontiguousAndBackwardRangeOffsets) {
  auto Bytes = root(2, {table(2, {0, 2, 0, 0, 0, 0})});
  Bytes.insert(Bytes.begin() + 44, 4, 0xff);
  setWord(Bytes, 10, 48);
  auto Plan = RootBindingPlan::parse(Bytes);
  ASSERT_TRUE(bool(Plan));
  EXPECT_EQ(Plan->getParameters()[0].Bindings[0].Count, 2u);
  auto Backward = root(2, {table(2, {0, 2, 0, 0, 0, 0})});
  std::rotate(Backward.begin() + 36, Backward.begin() + 44, Backward.end());
  setWord(Backward, 8, 60);
  setWord(Backward, 16, 36);
  Plan = RootBindingPlan::parse(Backward);
  ASSERT_TRUE(bool(Plan));
  EXPECT_EQ(Plan->getParameters()[0].Bindings[0].Count, 2u);
}

TEST(RootBindingPlanTest, StaticSamplerMayPrecedeParameterPayload) {
  auto Bytes = root(2, {{1, 0, {0, 0, 4}}}, {sampler()});
  std::rotate(Bytes.begin() + 36, Bytes.begin() + 48, Bytes.end());
  setWord(Bytes, 4, 36);
  setWord(Bytes, 8, 88);
  auto Plan = RootBindingPlan::parse(Bytes);
  ASSERT_TRUE(bool(Plan));
  EXPECT_EQ(Plan->getParameters()[0].ConstantCount, 4u);
  EXPECT_EQ(Plan->getStaticSamplers()[0].ShaderRegister, 3u);
}

std::string container(ArrayRef<uint8_t> Root, unsigned Parts) {
  std::vector<uint8_t> Bytes{'D', 'X', 'B', 'C'};
  Bytes.resize(20);
  for (uint32_t Word :
       {1u, uint32_t(32 + 4 * Parts + Parts * (8 + Root.size())), Parts})
    appendWord(Bytes, Word);
  for (unsigned I = 0; I != Parts; ++I)
    appendWord(Bytes, 32 + 4 * Parts + I * (8 + Root.size()));
  for (unsigned I = 0; I != Parts; ++I) {
    Bytes.insert(Bytes.end(), {'R', 'T', 'S', '0'});
    appendWord(Bytes, Root.size());
    Bytes.insert(Bytes.end(), Root.begin(), Root.end());
  }
  return std::string(reinterpret_cast<const char *>(Bytes.data()),
                     Bytes.size());
}

TEST(RootBindingPlanTest, ReadsBlobsContainersAndReportsMissingParts) {
  auto Bytes = root(2, {{1, 0, {0, 0, 4}}});
  auto Plan = RootBindingPlan::read(toStringRef(Bytes));
  ASSERT_TRUE(bool(Plan));
  Plan = RootBindingPlan::read(container(Bytes, 1));
  ASSERT_TRUE(bool(Plan));
  EXPECT_EQ(Plan->getDWORDCost(), 4u);
  for (unsigned Count : {0, 2}) {
    Plan = RootBindingPlan::read(container(Bytes, Count));
    ASSERT_FALSE(bool(Plan));
    std::string Message = toString(Plan.takeError());
    EXPECT_TRUE(
        StringRef(Message).contains(Count ? "More than one RTS0" : "no RTS0"));
  }
  Plan = RootBindingPlan::read("DXBC");
  ASSERT_FALSE(bool(Plan));
  consumeError(Plan.takeError());
}

TEST(RootBindingPlanTest,
     ImportsOwnedContainerRootsAndPreservesEntryOverrides) {
  LLVMContext C;
  Module M("container", C);
  auto *F = Function::Create(FunctionType::get(Type::getVoidTy(C), false),
                             GlobalValue::ExternalLinkage, "main", M);
  IRBuilder<> B(BasicBlock::Create(C, "entry", F));
  B.CreateRetVoid();
  F->addFnAttr("hlsl.shader", "compute");
  auto *Helper = Function::Create(F->getFunctionType(),
                                  GlobalValue::InternalLinkage, "helper", M);
  IRBuilder<>(BasicBlock::Create(C, "entry", Helper)).CreateRetVoid();
  auto Bytes = root(2, {{1, 0, {3, 7, 4}}});
  std::string Input = container(Bytes, 1);
  EXPECT_THAT_ERROR(importContainerRootSignature(M, Input), Succeeded());
  std::fill(Input.begin(), Input.end(), '#');
  auto Plan = getRootBindingPlan(*F);
  ASSERT_TRUE(bool(Plan));
  ASSERT_TRUE(*Plan);
  EXPECT_EQ((*Plan)->getParameters()[0].ConstantCount, 4u);
  EXPECT_FALSE(Helper->getMetadata(getRootSignatureMDKind()));
  setRootSignature(*F, root(2, {{1, 0, {3, 7, 8}}}));
  EXPECT_THAT_ERROR(importContainerRootSignature(M, container(Bytes, 1)),
                    Succeeded());
  Plan = getRootBindingPlan(*F);
  ASSERT_TRUE(bool(Plan));
  ASSERT_TRUE(*Plan);
  EXPECT_EQ((*Plan)->getParameters()[0].ConstantCount, 8u);
}

TEST(RootBindingPlanTest, ContainerImportDistinguishesAbsenceAndInvalidInput) {
  LLVMContext C;
  Module M("container", C);
  auto Bytes = root(2, {{1, 0, {3, 7, 4}}});
  EXPECT_THAT_ERROR(importContainerRootSignature(M, "not a container"),
                    Succeeded());
  EXPECT_THAT_ERROR(importContainerRootSignature(M, container(Bytes, 0)),
                    Succeeded());
  EXPECT_THAT_ERROR(importContainerRootSignature(M, "DXBC"), Failed());
  EXPECT_THAT_ERROR(importContainerRootSignature(M, container(Bytes, 2)),
                    Failed());
}

TEST(RootBindingPlanTest, CompleteCoverageRejectsClassSpaceVisibilityAndHoles) {
  auto Plan = RootBindingPlan::parse(
      root(2, {table(2, {1, 2, 3, 7, 0, 0, 1, 2, 6, 7, 0, 2}, 1)}));
  ASSERT_TRUE(bool(Plan));
  using Class = dxil::ResourceClass;
  using Stage = dxbc::ShaderVisibility;
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::UAV, 7, 3, 2, Stage::Vertex),
      Succeeded());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::UAV, 7, 3, 3, Stage::Vertex), Failed());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::UAV, 7, 3, 5, Stage::Vertex), Failed());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::SRV, 7, 3, 2, Stage::Vertex), Failed());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::UAV, 8, 3, 2, Stage::Vertex), Failed());
  EXPECT_THAT_ERROR(Plan->checkBindingCoverage(Class::UAV, 7, 3, 2, Stage::All),
                    Failed());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::UAV, 7, 3, 2, Stage::Pixel), Failed());
}

TEST(RootBindingPlanTest, RejectsAdjacentRangesAndChecksUnboundedEndpoints) {
  using Class = dxil::ResourceClass;
  using Stage = dxbc::ShaderVisibility;
  auto Plan = RootBindingPlan::parse(
      root(2, {table(2, {0, 1, 4, 7, 0, 0, 0, 1, 3, 7, 0, 1, 0, UINT32_MAX, 8,
                         7, 0, 2})}));
  ASSERT_TRUE(bool(Plan));
  EXPECT_THAT_ERROR(Plan->checkBindingCoverage(Class::SRV, 7, 3, 2, Stage::All),
                    Failed());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::SRV, 7, 8, UINT32_MAX, Stage::All),
      Succeeded());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::SRV, 7, UINT32_MAX, 1, Stage::All),
      Succeeded());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::SRV, 7, UINT32_MAX, 2, Stage::All),
      Failed());
  EXPECT_THAT_ERROR(Plan->checkBindingCoverage(Class::SRV, 7, 3, 0, Stage::All),
                    Failed());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::SRV, 7, 3, UINT32_MAX, Stage::All),
      Failed());
}

TEST(RootBindingPlanTest, StaticSamplersParticipateInCoverage) {
  auto Plan =
      RootBindingPlan::parse(root(2, {}, {sampler(3, 7, 1), sampler(4, 7, 1)}));
  ASSERT_TRUE(bool(Plan));
  using Class = dxil::ResourceClass;
  using Stage = dxbc::ShaderVisibility;
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::Sampler, 7, 3, 2, Stage::Vertex),
      Failed());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::Sampler, 7, 3, 1, Stage::Vertex),
      Succeeded());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::Sampler, 7, 3, 3, Stage::Vertex),
      Failed());
  EXPECT_THAT_ERROR(
      Plan->checkBindingCoverage(Class::Sampler, 7, 3, 1, Stage::All),
      Failed());
}

CallInst *addBinding(Module &M, Function &F, TargetExtType *Ty,
                     uint32_t Register = 3) {
  IRBuilder<> B(F.getEntryBlock().getTerminator());
  auto *Decl = Intrinsic::getOrInsertDeclaration(
      &M, Intrinsic::dx_resource_handlefrombinding, {Ty});
  auto *Handle = B.CreateCall(Decl, {B.getInt32(7), B.getInt32(Register),
                                     B.getInt32(1), B.getInt32(0),
                                     ConstantPointerNull::get(B.getPtrTy())});
  auto *Use = Function::Create(FunctionType::get(B.getVoidTy(), {Ty}, false),
                               GlobalValue::ExternalLinkage, "consume", M);
  B.CreateCall(Use, {Handle});
  return Handle;
}

Function *makeShaderFunction(Module &M, StringRef Name) {
  auto *F = Function::Create(
      FunctionType::get(Type::getVoidTy(M.getContext()), false),
      GlobalValue::ExternalLinkage, Name, M);
  IRBuilder<>(BasicBlock::Create(M.getContext(), "entry", F)).CreateRetVoid();
  return F;
}

CallInst *addHeapHandle(Module &M, Function &F, TargetExtType *Ty) {
  IRBuilder<> B(F.getEntryBlock().getTerminator());
  auto *Decl = Intrinsic::getOrInsertDeclaration(
      &M, Intrinsic::dx_resource_handlefromheap, {Ty});
  auto *Handle = B.CreateCall(Decl, {B.getInt32(0)});
  auto *Use = Function::Create(FunctionType::get(B.getVoidTy(), {Ty}, false),
                               GlobalValue::ExternalLinkage, "consume", M);
  B.CreateCall(Use, {Handle});
  return Handle;
}

TEST(RootBindingPlanTest, NativeHeapFlagsDistinguishResourcesAndSamplers) {
  LLVMContext C;
  auto *I32 = Type::getInt32Ty(C);
  SmallVector<TargetExtType *, 6> Types{
      TargetExtType::get(C, "dx.RawBuffer", {Type::getInt8Ty(C)}, {1, 0}),
      TargetExtType::get(C, "dx.TypedBuffer", {I32}, {0, 0, 0}),
      TargetExtType::get(C, "dx.Texture", {I32}, {0, 0, 0, 2}),
      TargetExtType::get(C, "dx.CBuffer", {ArrayType::get(I32, 4)}),
      TargetExtType::get(C, "dx.Sampler", {}, {0}),
      TargetExtType::get(C, "dx.Sampler", {}, {1})};
  for (auto [Index, Ty] : enumerate(Types)) {
    Module M("native-heap", C);
    auto *F = makeShaderFunction(M, "main");
    addHeapHandle(M, *F, Ty);
    for (uint32_t Version : {1u, 2u})
      for (uint32_t Flags : {0u, 0x400u, 0x800u, 0xc00u}) {
        auto Bytes = root(Version, {});
        setWord(Bytes, 5, Flags);
        auto Plan = RootBindingPlan::parse(Bytes);
        ASSERT_TRUE(bool(Plan));
        auto E = Plan->checkShaderBindings(*F, dxbc::ShaderVisibility::All);
        bool Sampler = Index >= 4;
        if (Flags & (Sampler ? 0x800u : 0x400u)) {
          EXPECT_THAT_ERROR(std::move(E), Succeeded());
          continue;
        }
        EXPECT_TRUE(StringRef(toString(std::move(E)))
                        .contains(Sampler
                                      ? "SAMPLER_HEAP_DIRECTLY_INDEXED"
                                      : "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED"));
      }
  }
}

TEST(RootBindingPlanTest, NativeHeapFlagsCheckHelpersButIgnoreDeadHandles) {
  LLVMContext C;
  Module M("native-helpers", C);
  auto *Entry = makeShaderFunction(M, "main");
  auto *Helper = makeShaderFunction(M, "helper");
  auto *Other = makeShaderFunction(M, "unrelated");
  auto *Ty =
      TargetExtType::get(C, "dx.RawBuffer", {Type::getInt8Ty(C)}, {1, 0});
  auto *Handle = addHeapHandle(M, *Helper, Ty);
  addHeapHandle(M, *Other, Ty);
  IRBuilder<>(Entry->getEntryBlock().getTerminator()).CreateCall(Helper);
  auto Bytes = root(2, {});
  auto Plan = RootBindingPlan::parse(Bytes);
  ASSERT_TRUE(bool(Plan));
  std::string Message =
      toString(Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::All));
  EXPECT_TRUE(StringRef(Message).contains("native resource heap access"));
  EXPECT_TRUE(StringRef(Message).contains("in 'helper'"));
  setWord(Bytes, 5, 0x400);
  auto Enabled = RootBindingPlan::parse(Bytes);
  ASSERT_TRUE(bool(Enabled));
  EXPECT_THAT_ERROR(
      Enabled->checkShaderBindings(*Entry, dxbc::ShaderVisibility::All),
      Succeeded());
  cast<Instruction>(*Handle->user_begin())->eraseFromParent();
  EXPECT_THAT_ERROR(
      Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::All),
      Succeeded());
}

TEST(RootBindingPlanTest, StaticSamplerModesCoverAllFilterReductionKinds) {
  LLVMContext C;
  const uint32_t Filters[]{
#define FILTER(Value, Name) Value,
#include "llvm/BinaryFormat/DXContainerConstants.def"
  };
  for (uint32_t Mode : {0u, 1u, 2u, 3u}) {
    Module M("static-sampler", C);
    auto *F = makeShaderFunction(M, "main");
    addBinding(M, *F, TargetExtType::get(C, "dx.Sampler", {}, {Mode}));
    for (uint32_t Version : {1u, 2u}) {
      auto Table = RootBindingPlan::parse(root(
          Version,
          {table(Version, Version == 1
                              ? std::vector<uint32_t>{3, 1, 3, 7, 0}
                              : std::vector<uint32_t>{3, 1, 3, 7, 0, 0})}));
      ASSERT_TRUE(bool(Table));
      EXPECT_THAT_ERROR(
          Table->checkShaderBindings(*F, dxbc::ShaderVisibility::All),
          Succeeded());
      for (uint32_t Filter : Filters) {
        auto Static = sampler();
        Static[0] = Filter;
        auto Plan = RootBindingPlan::parse(root(Version, {}, {Static}));
        ASSERT_TRUE(bool(Plan));
        auto E = Plan->checkShaderBindings(*F, dxbc::ShaderVisibility::All);
        bool Comparison = Filter >= 0x80 && Filter < 0x100;
        if (Mode == (Comparison ? 1u : 0u)) {
          EXPECT_THAT_ERROR(std::move(E), Succeeded());
          continue;
        }
        std::string Message = toString(std::move(E));
        EXPECT_TRUE(StringRef(Message).contains("static sampler mismatch"));
        EXPECT_TRUE(
            StringRef(Message).contains("space=7 register=3 in 'main'"));
      }
    }
  }
}

TEST(RootBindingPlanTest, StaticSamplerModesRespectVisibilityAndReachability) {
  LLVMContext C;
  Module M("sampler-helpers", C);
  auto *Entry = makeShaderFunction(M, "main");
  auto *Helper = makeShaderFunction(M, "helper");
  auto *Other = makeShaderFunction(M, "unrelated");
  auto *Ty = TargetExtType::get(C, "dx.Sampler", {}, {1});
  auto *Handle = addBinding(M, *Helper, Ty);
  addBinding(M, *Other, Ty, 9);
  IRBuilder<>(Entry->getEntryBlock().getTerminator()).CreateCall(Helper);
  auto Comparison = sampler(3, 7, 5);
  Comparison[0] = 0x80;
  auto Plan =
      RootBindingPlan::parse(root(2, {}, {sampler(3, 7, 1), Comparison}));
  ASSERT_TRUE(bool(Plan));
  EXPECT_THAT_ERROR(
      Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::Pixel),
      Succeeded());
  auto Message = toString(
      Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::Vertex));
  EXPECT_TRUE(StringRef(Message).contains("static sampler mismatch"));
  EXPECT_TRUE(StringRef(Message).contains("in 'helper'"));
  EXPECT_THAT_ERROR(
      Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::All), Failed());
  cast<Instruction>(*Handle->user_begin())->eraseFromParent();
  EXPECT_THAT_ERROR(
      Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::Vertex),
      Succeeded());

  auto *Native = makeShaderFunction(M, "native");
  addHeapHandle(M, *Native, Ty);
  auto Bytes = root(2, {}, {sampler()});
  setWord(Bytes, 5, 0x800);
  auto NativePlan = RootBindingPlan::parse(Bytes);
  ASSERT_TRUE(bool(NativePlan));
  EXPECT_THAT_ERROR(
      NativePlan->checkShaderBindings(*Native, dxbc::ShaderVisibility::All),
      Succeeded());
}

TEST(RootBindingPlanTest, ValidatesResourceClassesFromCanonicalHandleTypes) {
  LLVMContext C;
  auto *I32 = Type::getInt32Ty(C);
  SmallVector<TargetExtType *, 8> Types{
      TargetExtType::get(C, "dx.RawBuffer", {I32}, {0, 0}),
      TargetExtType::get(C, "dx.RawBuffer", {I32}, {1, 0}),
      TargetExtType::get(C, "dx.TypedBuffer", {I32}, {0, 0, 0}),
      TargetExtType::get(C, "dx.Texture", {I32}, {1, 0, 0, 2}),
      TargetExtType::get(C, "dx.MSTexture", {I32}, {0, 4, 0, 3}),
      TargetExtType::get(C, "dx.FeedbackTexture", {}, {0, 2}),
      TargetExtType::get(C, "dx.CBuffer", {ArrayType::get(I32, 4)}),
      TargetExtType::get(C, "dx.Sampler", {}, {0})};
  uint32_t Classes[] = {0, 1, 0, 1, 0, 1, 2, 3};
  for (auto [Index, Ty] : enumerate(Types)) {
    Module M("binding", C);
    auto *F = makeShaderFunction(M, "main");
    addBinding(M, *F, Ty);
    auto Plan = RootBindingPlan::parse(
        root(2, {table(2, {Classes[Index], 1, 3, 7, 0, 0})}));
    ASSERT_TRUE(bool(Plan));
    EXPECT_THAT_ERROR(
        Plan->checkShaderBindings(*F, dxbc::ShaderVisibility::All),
        Succeeded());
    auto Wrong = RootBindingPlan::parse(
        root(2, {table(2, {(Classes[Index] + 1) % 4, 1, 3, 7, 0, 0})}));
    ASSERT_TRUE(bool(Wrong));
    EXPECT_THAT_ERROR(
        Wrong->checkShaderBindings(*F, dxbc::ShaderVisibility::All), Failed());
  }
}

TEST(RootBindingPlanTest, RootDescriptorsRequireRawOrStructuredBuffers) {
  LLVMContext C;
  auto *I8 = Type::getInt8Ty(C);
  auto *I32 = Type::getInt32Ty(C);
  SmallVector<TargetExtType *, 8> Types{
      TargetExtType::get(C, "dx.RawBuffer", {I8}, {0, 0}),
      TargetExtType::get(C, "dx.RawBuffer", {I32}, {0, 0}),
      TargetExtType::get(C, "dx.RawBuffer", {I8}, {1, 0}),
      TargetExtType::get(C, "dx.RawBuffer", {I32}, {1, 0}),
      TargetExtType::get(C, "dx.TypedBuffer", {I32}, {0, 0, 0}),
      TargetExtType::get(C, "dx.TypedBuffer", {I32}, {1, 0, 0}),
      TargetExtType::get(C, "dx.Texture", {I32}, {0, 0, 0, 2}),
      TargetExtType::get(C, "dx.Texture", {I32}, {1, 0, 0, 2}),
      TargetExtType::get(C, "dx.MSTexture", {I32}, {0, 4, 0, 3}),
      TargetExtType::get(C, "dx.FeedbackTexture", {}, {0, 2})};
  for (auto [Index, Ty] : enumerate(Types)) {
    Module M("descriptor-kind", C);
    auto *F = makeShaderFunction(M, "main");
    addBinding(M, *F, Ty);
    bool Writable =
        Index == 2 || Index == 3 || Index == 5 || Index == 7 || Index == 9;
    for (uint32_t Version : {1u, 2u}) {
      std::vector<uint32_t> Data{3, 7};
      if (Version == 2)
        Data.push_back(0);
      auto Plan = RootBindingPlan::parse(
          root(Version, {{Writable ? 4u : 3u, 0, Data}}));
      ASSERT_TRUE(bool(Plan));
      auto E = Plan->checkShaderBindings(*F, dxbc::ShaderVisibility::All);
      if (Index < 4) {
        EXPECT_THAT_ERROR(std::move(E), Succeeded());
        continue;
      }
      EXPECT_TRUE(StringRef(toString(std::move(E)))
                      .contains("requires a raw or structured buffer"));
      auto Table = RootBindingPlan::parse(root(
          Version,
          {table(Version,
                 Version == 1
                     ? std::vector<uint32_t>{Writable ? 1u : 0u, 1, 3, 7, 0}
                     : std::vector<uint32_t>{Writable ? 1u : 0u, 1, 3, 7, 0,
                                             0})}));
      ASSERT_TRUE(bool(Table));
      EXPECT_THAT_ERROR(
          Table->checkShaderBindings(*F, dxbc::ShaderVisibility::All),
          Succeeded());
    }
  }
}

TEST(RootBindingPlanTest, DescriptorKindsUseVisibleParametersAndHelpers) {
  LLVMContext C;
  Module M("descriptor-helpers", C);
  auto *Entry = makeShaderFunction(M, "main");
  auto *Helper = makeShaderFunction(M, "helper");
  auto *Other = makeShaderFunction(M, "unrelated");
  auto *Ty =
      TargetExtType::get(C, "dx.TypedBuffer", {Type::getInt32Ty(C)}, {0, 0, 0});
  addBinding(M, *Helper, Ty);
  addBinding(M, *Other, Ty, 9);
  IRBuilder<>(Entry->getEntryBlock().getTerminator()).CreateCall(Helper);
  auto Plan = RootBindingPlan::parse(
      root(2, {{3, 1, {3, 7, 0}}, table(2, {0, 1, 3, 7, 0, 0}, 5)}));
  ASSERT_TRUE(bool(Plan));
  EXPECT_THAT_ERROR(
      Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::Pixel),
      Succeeded());
  std::string Message = toString(
      Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::Vertex));
  EXPECT_TRUE(StringRef(Message).contains("root descriptor mismatch"));
  EXPECT_TRUE(StringRef(Message).contains("in 'helper'"));
}

TEST(RootBindingPlanTest, ChecksReachableHelpersButNotUnrelatedFunctions) {
  LLVMContext C;
  Module M("helpers", C);
  auto *Entry = makeShaderFunction(M, "main");
  auto *Helper = makeShaderFunction(M, "helper");
  auto *Other = makeShaderFunction(M, "unrelated");
  auto *Ty =
      TargetExtType::get(C, "dx.RawBuffer", {Type::getInt8Ty(C)}, {0, 0});
  auto *Handle = addBinding(M, *Helper, Ty);
  addBinding(M, *Other, Ty, 9);
  IRBuilder<> B(Entry->getEntryBlock().getTerminator());
  B.CreateCall(Helper);
  IRBuilder<>(Helper->getEntryBlock().getTerminator()).CreateCall(Helper);
  auto Plan = RootBindingPlan::parse(root(2, {table(2, {0, 1, 3, 7, 0, 0})}));
  ASSERT_TRUE(bool(Plan));
  EXPECT_THAT_ERROR(
      Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::All),
      Succeeded());
  Handle->setArgOperand(1, B.getInt32(4));
  auto Error = Plan->checkShaderBindings(*Entry, dxbc::ShaderVisibility::All);
  ASSERT_TRUE(bool(Error));
  EXPECT_TRUE(StringRef(toString(std::move(Error))).contains("in 'helper'"));
}

TEST(RootBindingPlanTest, RejectsDynamicDeclarationsAndIndirectCalls) {
  LLVMContext C;
  Module M("dynamic", C);
  auto *F = makeShaderFunction(M, "main");
  auto *Ty =
      TargetExtType::get(C, "dx.RawBuffer", {Type::getInt8Ty(C)}, {0, 0});
  auto *Handle = addBinding(M, *F, Ty);
  Handle->setArgOperand(0, PoisonValue::get(Type::getInt32Ty(C)));
  auto Plan = RootBindingPlan::parse(root(2, {table(2, {0, 1, 3, 7, 0, 0})}));
  ASSERT_TRUE(bool(Plan));
  EXPECT_THAT_ERROR(Plan->checkShaderBindings(*F, dxbc::ShaderVisibility::All),
                    Failed());
  auto *Indirect = makeShaderFunction(M, "indirect");
  IRBuilder<> B(Indirect->getEntryBlock().getTerminator());
  B.CreateCall(Indirect->getFunctionType(), PoisonValue::get(B.getPtrTy()), {});
  EXPECT_THAT_ERROR(
      Plan->checkShaderBindings(*Indirect, dxbc::ShaderVisibility::All),
      Failed());
}

TEST(RootBindingPlanTest, RejectsMalformedLiveBindingsButIgnoresDeadHandles) {
  LLVMContext C;
  auto Plan = RootBindingPlan::parse(root(2, {table(2, {0, 1, 3, 7, 0, 0})}));
  ASSERT_TRUE(bool(Plan));
  auto *I8 = Type::getInt8Ty(C);
  SmallVector<TargetExtType *, 4> Types{
      TargetExtType::get(C, "dx.RawBuffer", {I8}, {0}),
      TargetExtType::get(C, "dx.RawBuffer", {I8}, {3, 0}),
      TargetExtType::get(C, "dx.Sampler"), TargetExtType::get(C, "dx.Unknown")};
  for (auto *Ty : Types) {
    Module M("malformed", C);
    auto *F = makeShaderFunction(M, "main");
    auto *Handle = addBinding(M, *F, Ty);
    std::string Message =
        toString(Plan->checkShaderBindings(*F, dxbc::ShaderVisibility::All));
    EXPECT_TRUE(StringRef(Message).contains(
        Ty == Types.back() ? "unsupported resource type" : "malformed"));
    cast<Instruction>(*Handle->user_begin())->eraseFromParent();
    EXPECT_THAT_ERROR(
        Plan->checkShaderBindings(*F, dxbc::ShaderVisibility::All),
        Succeeded());
  }
}

TEST(RootBindingPlanTest, CanonicalMetadataDistinguishesAbsentAndInvalid) {
  LLVMContext Context;
  Module M("root", Context);
  auto *F = Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                             GlobalValue::ExternalLinkage, "main", M);
  auto Plan = getRootBindingPlan(*F);
  ASSERT_TRUE(bool(Plan));
  EXPECT_FALSE(Plan->has_value());
  setRootSignature(*F, root(2, {{1, 0, {3, 7, 4}}}));
  Plan = getRootBindingPlan(*F);
  ASSERT_TRUE(bool(Plan));
  ASSERT_TRUE(Plan->has_value());
  EXPECT_EQ((*Plan)->getDWORDCost(), 4u);
  setRootSignature(*F, root(2, {{1, 0, {3, 7, 65}}}));
  Plan = getRootBindingPlan(*F);
  ASSERT_FALSE(bool(Plan));
  EXPECT_TRUE(StringRef(toString(Plan.takeError())).contains("64 DWORDs"));
  F->setMetadata(getRootSignatureMDKind(),
                 MDNode::get(Context, MDString::get(Context, "bad")));
  Plan = getRootBindingPlan(*F);
  ASSERT_FALSE(bool(Plan));
  EXPECT_TRUE(
      StringRef(toString(Plan.takeError())).contains("malformed canonical"));
}

} // namespace

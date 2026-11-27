//===- WarpShaderCompiler.cpp - Raw DDI bytecode to shared CPU JIT
//----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "..\..\utils\windows-adapter-bootstrap\WarpShaderCompilerABI.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "feme/Direct3D/ComputePlan.h"
#include "feme/Graphics/Pipeline.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/DXSA/BinaryParser.h"
#include "feme/Transforms/DXIL/MetadataRaising.h"
#include "feme/Transforms/DXIL/OpRaising.h"
#include "feme/Translate/DXSA/DXSATranslator.h"

#include "llvm/IR/PassManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/VCSRevision.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <limits>
#include <new>

namespace {
constexpr std::int32_t InvalidArgument = static_cast<std::int32_t>(0x80070057u);
constexpr std::int32_t CompileFailure = static_cast<std::int32_t>(0x80004005u);
constexpr std::int32_t OutOfMemory = static_cast<std::int32_t>(0x8007000eu);

struct CompiledShader {
  std::shared_ptr<feme::cpu::CompiledStage> Stage;
};

std::int32_t report(llvm::Error Error) {
  llvm::logAllUnhandledErrors(std::move(Error), llvm::errs(),
                              "FeMe WARP shader compiler: ");
  return CompileFailure;
}
} // namespace

const char *FeMeWarpCompilerRevision() {
#ifdef LLVM_REVISION
  return LLVM_REVISION;
#else
  llvm::errs() << "FeMe WARP compiler: build revision unavailable\n";
  return nullptr;
#endif
}

std::int32_t FeMeWarpCompileRawDXBC(const std::uint32_t *Tokens,
                                    std::uint32_t Count, void **Compiled,
                                    FeMeWarpCompiledShaderInfo *Info) {
  if (Compiled)
    *Compiled = nullptr;
  if (!Tokens || !Compiled || !Info || Info->Size != sizeof(*Info) ||
      Info->BridgeVersion != 1 || Count < 2 || Count > 4 * 1024 * 1024 ||
      Tokens[1] != Count) {
    llvm::errs() << "FeMe WARP shader compiler: invalid token buffer or ABI\n";
    return InvalidArgument;
  }
  feme::ShaderStage Stage;
  switch (Tokens[0] >> 16) {
  case 0:
    Stage = feme::ShaderStage::Fragment;
    break;
  case 1:
    Stage = feme::ShaderStage::Vertex;
    break;
  case 2:
    Stage = feme::ShaderStage::Geometry;
    break;
  case 3:
    Stage = feme::ShaderStage::Hull;
    break;
  case 4:
    Stage = feme::ShaderStage::Domain;
    break;
  case 5:
    Stage = feme::ShaderStage::Compute;
    break;
  default:
    llvm::errs() << "FeMe WARP shader compiler: unsupported DXBC stage\n";
    return InvalidArgument;
  }
  feme::Context Context;
  Context.getMLIRContext().getOrLoadDialect<feme::dxsa::DXSADialect>();
  llvm::SourceMgr Source;
  Source.AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef(reinterpret_cast<const char *>(Tokens),
                          static_cast<std::size_t>(Count) * sizeof(*Tokens)),
          "WARP DDI shader", false),
      llvm::SMLoc());
  auto Parsed = feme::dxsa::deserialize(Source, &Context.getMLIRContext());
  if (!Parsed) {
    llvm::errs() << "FeMe WARP shader compiler: token deserialization failed\n";
    return CompileFailure;
  }
  feme::dxsa::DXSAToLLVMIRTranslator Translator;
  auto Module =
      Translator.translate(feme::Module::fromMLIR(std::move(Parsed)), Context);
  if (!Module)
    return report(Module.takeError());
  llvm::ModuleAnalysisManager Analyses;
  feme::dxil::OpRaisingPass().run(Module->getLLVMModule(), Analyses);
  feme::dxil::MetadataRaisingPass().run(Module->getLLVMModule(), Analyses);
  feme::cpu::StageCompileOptions Options;
  Options.Stage = Stage;
  Options.WaveSize = 4;
  auto Result =
      feme::cpu::CompiledStage::create(Context, std::move(*Module), Options);
  if (!Result)
    return report(Result.takeError());
  if (!(*Result)->EntryFn) {
    llvm::errs() << "FeMe WARP shader compiler: missing native entry point\n";
    return CompileFailure;
  }
  FeMeWarpCompiledShaderInfo Output;
  Output.ArtifactVersion = feme::cpu::ArtifactAbiVersion;
  Output.Stage = static_cast<std::uint32_t>((*Result)->getStage());
  Output.WaveSize = (*Result)->getWaveSize();
  Output.HasNativeEntry = 1;
  *Info = Output;
  *Compiled = new CompiledShader{std::move(*Result)};
  return 0;
}

void FeMeWarpDestroyCompiledShader(void *Compiled) {
  delete static_cast<CompiledShader *>(Compiled);
}

std::int32_t FeMeWarpCreateVertexPipeline(
    void *Shader, const FeMeWarpVertexPipelineDesc *Desc, void **Pipeline) {
  if (Pipeline)
    *Pipeline = nullptr;
  if (!Shader || !Desc || !Pipeline || Desc->Size != sizeof(*Desc) ||
      Desc->Topology > 2 || Desc->Cull > 2 || Desc->FrontCounterClockwise > 1 ||
      Desc->Wireframe > 1 || Desc->DepthClamp > 1) {
    llvm::errs() << "FeMe WARP compiler: invalid vertex pipeline descriptor\n";
    return InvalidArgument;
  }
  auto &Stage = static_cast<CompiledShader *>(Shader)->Stage;
  if (Stage->getStage() != feme::ShaderStage::Vertex) {
    llvm::errs() << "FeMe WARP compiler: pipeline requires a vertex stage\n";
    return InvalidArgument;
  }
  using namespace feme::graphics;
  constexpr PrimitiveTopology Topologies[] = {PrimitiveTopology::PointList,
                                              PrimitiveTopology::LineList,
                                              PrimitiveTopology::TriangleList};
  constexpr CullMode Culls[] = {CullMode::None, CullMode::Front,
                                CullMode::Back};
  RasterState Raster;
  Raster.Cull = Culls[Desc->Cull];
  Raster.Front = Desc->FrontCounterClockwise ? FrontFace::CounterClockwise
                                             : FrontFace::Clockwise;
  Raster.Polygon = Desc->Wireframe ? PolygonMode::Line : PolygonMode::Fill;
  Raster.DepthClampEnable = Desc->DepthClamp != 0;
  auto Result = std::make_unique<GraphicsPipeline>(
      Stage, nullptr, Topologies[Desc->Topology], Raster, DepthState{},
      BlendMode::Replace, 1, std::vector<AttachmentFormat>{});
  Result->setSampleMask(Desc->SampleMask);
  *Pipeline = Result.release();
  return 0;
}

void FeMeWarpDestroyVertexPipeline(void *Pipeline) {
  delete static_cast<feme::graphics::GraphicsPipeline *>(Pipeline);
}

std::int32_t FeMeWarpCreateComputePipeline(void *Shader, void **Pipeline) {
  if (Pipeline)
    *Pipeline = nullptr;
  if (!Shader || !Pipeline) {
    llvm::errs() << "FeMe WARP compiler: missing compute pipeline input\n";
    return InvalidArgument;
  }
  auto &Stage = static_cast<CompiledShader *>(Shader)->Stage;
  if (Stage->getStage() != feme::ShaderStage::Compute) {
    llvm::errs() << "FeMe WARP compiler: pipeline requires a compute stage\n";
    return InvalidArgument;
  }
  *Pipeline = new (std::nothrow) CompiledShader{Stage};
  if (!*Pipeline) {
    llvm::errs() << "FeMe WARP compiler: compute pipeline allocation failed\n";
    return OutOfMemory;
  }
  return 0;
}

std::int32_t FeMeWarpRetainComputePipeline(void *Pipeline, void **Retained) {
  return FeMeWarpCreateComputePipeline(Pipeline, Retained);
}

void FeMeWarpDestroyComputePipeline(void *Pipeline) {
  delete static_cast<CompiledShader *>(Pipeline);
}

std::int32_t FeMeWarpDispatchCompute(void *Pipeline,
                                     const FeMeWarpDispatchDesc *Desc) {
  if (!Pipeline || !Desc || Desc->Size != sizeof(*Desc) || Desc->Version != 1 ||
      Desc->BufferCount > 64 || (Desc->BufferCount && !Desc->Buffers)) {
    llvm::errs() << "FeMe WARP dispatch: invalid descriptor or ABI\n";
    return InvalidArgument;
  }
  auto &Stage = *static_cast<CompiledShader *>(Pipeline)->Stage;
  const auto &Info = Stage.getResourceInfo();
  if (Stage.getStage() != feme::ShaderStage::Compute || Info.RootConstantSize ||
      Info.UsesSamplerHeap || Info.ReservedImageHeapSize ||
      Info.ReservedSamplerHeapSize) {
    llvm::errs() << "FeMe WARP dispatch: unsupported stage or bindings\n";
    return InvalidArgument;
  }
  feme::direct3d::ComputeDispatchPlan Plan;
  Plan.GroupSize = Stage.getGroupSize();
  Plan.GroupCount = {Desc->GroupCount[0], Desc->GroupCount[1],
                     Desc->GroupCount[2]};
  Plan.WaveSize = Stage.getWaveSize();
  bool Empty = false;
  for (auto Count : Plan.GroupCount) {
    if (Count > 65535) {
      llvm::errs() << "FeMe WARP dispatch: group count exceeds D3D12 limit\n";
      return InvalidArgument;
    }
    Empty |= Count == 0;
  }
  if (!Empty)
    if (auto Error = feme::direct3d::validateComputeDispatchPlan(Plan))
      return report(std::move(Error));

  std::array<feme::cpu::FemeDescriptor, 64> Descriptors{};
  std::array<feme::cpu::BoundResourceBinding, 64> Bindings{};
  for (std::uint32_t I = 0; I < Desc->BufferCount; ++I) {
    const auto &Buffer = Desc->Buffers[I];
    if (!Buffer.Data || !Buffer.SizeInBytes || Buffer.Kind > 1 ||
        (Buffer.Namespace != 1 && Buffer.Namespace != 2) ||
        (Buffer.Kind == 0 && Buffer.Stride) ||
        (Buffer.Kind == 1 && (!Buffer.Stride || Buffer.Stride % 4 ||
                              Buffer.SizeInBytes % Buffer.Stride))) {
      llvm::errs() << "FeMe WARP dispatch: invalid buffer binding\n";
      return InvalidArgument;
    }
    auto &Descriptor = Descriptors[I];
    Descriptor.Data = Buffer.Data;
    Descriptor.SizeInBytes = Buffer.SizeInBytes;
    Descriptor.Kind = static_cast<std::uint32_t>(
        Buffer.Kind ? feme::cpu::ResourceKind::Structured
                    : feme::cpu::ResourceKind::Raw);
    Descriptor.Stride = Buffer.Stride;
    Descriptor.Flags =
        Buffer.Namespace == 2 ? feme::cpu::FEME_DESCRIPTOR_UAV : 0;
    Bindings[I] = {
        Buffer.Space,
        Buffer.Register,
        {&Descriptor, 1},
        static_cast<feme::cpu::BoundResourceNamespace>(Buffer.Namespace)};
  }
  llvm::ArrayRef<feme::cpu::BoundResourceBinding> Bound(Bindings.data(),
                                                        Desc->BufferCount);
  if (auto Error = feme::cpu::validateBoundResourceBindings(Info, Bound))
    return report(std::move(Error));
  for (const auto &Range : Info.BoundRanges) {
    bool Found = false;
    for (const auto &Binding : Bound)
      Found |= Binding.Space == Range.Space &&
               Binding.BaseRegister == Range.BaseRegister &&
               Binding.Namespace == Range.Namespace;
    if (Range.Class != feme::cpu::BoundResourceClass::Buffer ||
        Range.RangeSize != 1 || !Found) {
      llvm::errs() << "FeMe WARP dispatch: required buffer range not bound\n";
      return InvalidArgument;
    }
  }
  for (auto Index : Info.StaticHeapIndices)
    if (Index >= Info.ReservedResourceHeapSize) {
      llvm::errs()
          << "FeMe WARP dispatch: dynamic descriptor heap unsupported\n";
      return InvalidArgument;
    }
  if (Empty)
    return 0;

  const auto &Shared = Stage.GroupSharedReqs;
  if (!Shared.Alignment || (Shared.Alignment & (Shared.Alignment - 1)) ||
      Shared.Alignment > std::numeric_limits<std::size_t>::max() ||
      Shared.Size >
          std::numeric_limits<std::size_t>::max() - (Shared.Alignment - 1)) {
    llvm::errs() << "FeMe WARP dispatch: invalid shared-memory extent\n";
    return InvalidArgument;
  }
  std::size_t Bytes = static_cast<std::size_t>(Shared.Size);
  std::size_t Space =
      Bytes ? Bytes + static_cast<std::size_t>(Shared.Alignment - 1) : 0;
  std::unique_ptr<std::uint8_t[]> Allocation;
  void *Memory = nullptr;
  if (Space) {
    Allocation.reset(new (std::nothrow) std::uint8_t[Space]);
    if (!Allocation) {
      llvm::errs() << "FeMe WARP dispatch: shared-memory allocation failed\n";
      return OutOfMemory;
    }
    Memory = Allocation.get();
    if (!std::align(static_cast<std::size_t>(Shared.Alignment), Bytes, Memory,
                    Space)) {
      llvm::errs() << "FeMe WARP dispatch: shared-memory alignment failed\n";
      return InvalidArgument;
    }
  }
  feme::cpu::DispatchResources Resources;
  Resources.BoundResources = Bound;
  auto Prepared =
      feme::cpu::PreparedDispatch::create(Info, Resources, Plan.GroupCount);
  for (std::uint32_t Z = 0; Z < Plan.GroupCount[2]; ++Z)
    for (std::uint32_t Y = 0; Y < Plan.GroupCount[1]; ++Y)
      for (std::uint32_t X = 0; X < Plan.GroupCount[0]; ++X) {
        if (Bytes)
          std::memset(Memory, 0, Bytes);
        if (auto Error =
                Stage.invokeGroup(Prepared, {X, Y, Z},
                                  {static_cast<std::uint8_t *>(Memory), Bytes}))
          return report(std::move(Error));
      }
  return 0;
}

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

namespace {
constexpr std::int32_t InvalidArgument = static_cast<std::int32_t>(0x80070057u);
constexpr std::int32_t CompileFailure = static_cast<std::int32_t>(0x80004005u);

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
  *Pipeline = new CompiledShader{Stage};
  return 0;
}

void FeMeWarpDestroyComputePipeline(void *Pipeline) {
  delete static_cast<CompiledShader *>(Pipeline);
}

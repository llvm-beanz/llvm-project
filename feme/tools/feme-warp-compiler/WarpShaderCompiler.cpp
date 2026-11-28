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
#include "feme/Import/DXIL/DXILImporter.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/DXSA/BinaryParser.h"
#include "feme/Transforms/DXIL/MetadataRaising.h"
#include "feme/Transforms/DXIL/OpRaising.h"
#include "feme/Translate/DXSA/DXSATranslator.h"

#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/VCSRevision.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>

#include <dxgiformat.h>

namespace {
constexpr std::int32_t InvalidArgument = static_cast<std::int32_t>(0x80070057u);
constexpr std::int32_t CompileFailure = static_cast<std::int32_t>(0x80004005u);
constexpr std::int32_t OutOfMemory = static_cast<std::int32_t>(0x8007000eu);

feme::cpu::ResourceFormat typedBufferFormat(std::uint32_t Format) {
  switch (Format) {
  case DXGI_FORMAT_R32_FLOAT:
    return feme::cpu::ResourceFormat::R32_FLOAT;
  case DXGI_FORMAT_R32_UINT:
    return feme::cpu::ResourceFormat::R32_UINT;
  case DXGI_FORMAT_R32_SINT:
    return feme::cpu::ResourceFormat::R32_SINT;
  case DXGI_FORMAT_R32G32_FLOAT:
    return feme::cpu::ResourceFormat::R32G32_FLOAT;
  case DXGI_FORMAT_R32G32_UINT:
    return feme::cpu::ResourceFormat::R32G32_UINT;
  case DXGI_FORMAT_R32G32_SINT:
    return feme::cpu::ResourceFormat::R32G32_SINT;
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
    return feme::cpu::ResourceFormat::R32G32B32A32_FLOAT;
  case DXGI_FORMAT_R32G32B32A32_UINT:
    return feme::cpu::ResourceFormat::R32G32B32A32_UINT;
  case DXGI_FORMAT_R32G32B32A32_SINT:
    return feme::cpu::ResourceFormat::R32G32B32A32_SINT;
  case DXGI_FORMAT_R8_UNORM:
    return feme::cpu::ResourceFormat::R8_UNORM;
  case DXGI_FORMAT_R8_UINT:
    return feme::cpu::ResourceFormat::R8_UINT;
  case DXGI_FORMAT_R8_SINT:
    return feme::cpu::ResourceFormat::R8_SINT;
  case DXGI_FORMAT_R16_FLOAT:
    return feme::cpu::ResourceFormat::R16_FLOAT;
  case DXGI_FORMAT_R16_UINT:
    return feme::cpu::ResourceFormat::R16_UINT;
  case DXGI_FORMAT_R16_SINT:
    return feme::cpu::ResourceFormat::R16_SINT;
  case DXGI_FORMAT_R8G8B8A8_UNORM:
    return feme::cpu::ResourceFormat::R8G8B8A8_UNORM;
  case DXGI_FORMAT_R8G8B8A8_UINT:
    return feme::cpu::ResourceFormat::R8G8B8A8_UINT;
  case DXGI_FORMAT_R8G8B8A8_SINT:
    return feme::cpu::ResourceFormat::R8G8B8A8_SINT;
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
    return feme::cpu::ResourceFormat::R16G16B16A16_FLOAT;
  case DXGI_FORMAT_R16G16B16A16_UINT:
    return feme::cpu::ResourceFormat::R16G16B16A16_UINT;
  case DXGI_FORMAT_R16G16B16A16_SINT:
    return feme::cpu::ResourceFormat::R16G16B16A16_SINT;
  default:
    return feme::cpu::ResourceFormat::Unknown;
  }
}

std::uint32_t typedBufferElementSize(feme::cpu::ResourceFormat Format) {
  using feme::cpu::ResourceFormat;
  switch (Format) {
  case ResourceFormat::R8_UNORM:
  case ResourceFormat::R8_UINT:
  case ResourceFormat::R8_SINT:
    return 1;
  case ResourceFormat::R16_FLOAT:
  case ResourceFormat::R16_UINT:
  case ResourceFormat::R16_SINT:
    return 2;
  case ResourceFormat::R8G8B8A8_UNORM:
  case ResourceFormat::R8G8B8A8_UINT:
  case ResourceFormat::R8G8B8A8_SINT:
  case ResourceFormat::R32_FLOAT:
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32_SINT:
    return 4;
  case ResourceFormat::R32G32_FLOAT:
  case ResourceFormat::R32G32_UINT:
  case ResourceFormat::R32G32_SINT:
  case ResourceFormat::R16G16B16A16_FLOAT:
  case ResourceFormat::R16G16B16A16_UINT:
  case ResourceFormat::R16G16B16A16_SINT:
    return 8;
  case ResourceFormat::R32G32B32A32_FLOAT:
  case ResourceFormat::R32G32B32A32_UINT:
  case ResourceFormat::R32G32B32A32_SINT:
    return 16;
  default:
    return 0;
  }
}

std::uint32_t componentSwizzle(std::uint32_t Mapping) {
  if (Mapping == FeMeWarpDefaultComponentMapping)
    return 0;
  std::uint32_t Swizzle = 0;
  for (unsigned I = 0; I != 4; ++I) {
    auto Source = (Mapping >> (I * 3)) & 7;
    auto Channel =
        Source < 4 ? static_cast<feme::cpu::ImageComponentSwizzle>(Source + 3)
        : Source == 4 ? feme::cpu::ImageComponentSwizzle::Zero
                      : feme::cpu::ImageComponentSwizzle::One;
    Swizzle |= static_cast<std::uint32_t>(Channel) << (I * 8);
  }
  return Swizzle;
}

std::optional<feme::cpu::ImageDimension>
imageDimension(FeMeWarpImageDimension Dimension) {
  using D = feme::cpu::ImageDimension;
  switch (Dimension) {
  case FeMeWarpImageDimension::Texture1D:
    return D::Texture1D;
  case FeMeWarpImageDimension::Texture1DArray:
    return D::Texture1DArray;
  case FeMeWarpImageDimension::Texture2D:
    return D::Texture2D;
  case FeMeWarpImageDimension::Texture2DArray:
    return D::Texture2DArray;
  case FeMeWarpImageDimension::Texture3D:
    return D::Texture3D;
  case FeMeWarpImageDimension::TextureCube:
    return D::TextureCube;
  case FeMeWarpImageDimension::TextureCubeArray:
    return D::TextureCubeArray;
  }
  return std::nullopt;
}

bool prepareImage(const FeMeWarpImageBinding &Image,
                  feme::cpu::FemeImageDescriptor &Descriptor,
                  std::array<feme::cpu::FemeImageSubresourceLayout,
                             FeMeWarpMaxImageMipLevels> &Layouts) {
  auto Dimension = imageDimension(Image.Dimension);
  auto Format = typedBufferFormat(Image.Format);
  auto ElementSize = typedBufferElementSize(Format);
  if (!Dimension || Image.Namespace < 1 || Image.Namespace > 2 ||
      (!Image.Data != !Image.SizeInBytes) || Image.PlaneCount != 1 ||
      Image.SampleCount != 1 ||
      !FeMeWarpValidComponentMapping(Image.Shader4ComponentMapping) ||
      (Image.Namespace == 2 &&
       Image.Shader4ComponentMapping != FeMeWarpDefaultComponentMapping))
    return false;
  if (!Image.Data)
    return !Image.Width && !Image.Height && !Image.Depth && !Image.MipLevels &&
           !Image.MipLayoutCount && !Image.MipLayouts &&
           !Image.FirstDepthSlice && (!Image.Format || ElementSize);
  using D = feme::cpu::ImageDimension;
  bool Volume = *Dimension == D::Texture3D;
  bool OneDimensional =
      *Dimension == D::Texture1D || *Dimension == D::Texture1DArray;
  bool Cube = *Dimension == D::TextureCube || *Dimension == D::TextureCubeArray;
  bool Array = *Dimension == D::Texture1DArray ||
               *Dimension == D::Texture2DArray || Cube;
  if (!ElementSize || !Image.Width || !Image.Height || !Image.Depth ||
      Image.Width > (Volume ? 2048u : 16384u) ||
      Image.Height > (Volume ? 2048u : 16384u) || Image.Depth > 2048 ||
      (OneDimensional && Image.Height != 1) || (!Volume && Image.Depth != 1) ||
      !Image.ArrayLayers || Image.ArrayLayers > 2048 ||
      (!Array && Image.ArrayLayers != 1) ||
      (Cube && (Image.Namespace != 1 || Image.Width != Image.Height ||
                Image.ArrayLayers % 6)) ||
      (*Dimension == D::TextureCube && Image.ArrayLayers != 6) ||
      !Image.MipLevels || Image.MipLevels > Layouts.size() ||
      Image.MipLayoutCount != Image.MipLevels || !Image.MipLayouts)
    return false;
  if (Image.FirstDepthSlice &&
      (!Volume || Image.Namespace != 2 || Image.MipLevels != 1 ||
       Image.FirstDepthSlice >= Image.Depth))
    return false;
  unsigned MaxMips = 0;
  for (auto Extent = std::max(Image.Width, std::max(Image.Height, Image.Depth));
       Extent; Extent >>= 1)
    ++MaxMips;
  if (Image.MipLevels > MaxMips)
    return false;
  for (unsigned I = 0; I < Image.MipLevels; ++I) {
    const auto &Layout = Image.MipLayouts[I];
    auto Width = std::max(1u, Image.Width >> I);
    auto Height = std::max(1u, Image.Height >> I);
    auto Slices = Volume ? std::max(1u, Image.Depth >> I) : Image.ArrayLayers;
    std::uint64_t RowBytes = std::uint64_t(Width) * ElementSize;
    if (Layout.Offset >= Image.SizeInBytes || Layout.RowPitch < RowBytes ||
        Layout.RowPitch > Image.SizeInBytes ||
        Layout.SlicePitch > Image.SizeInBytes)
      return false;
    std::uint64_t Remaining = Image.SizeInBytes - Layout.Offset;
    if (RowBytes > Remaining ||
        (Height > 1 && Layout.RowPitch > (Remaining - RowBytes) / (Height - 1)))
      return false;
    std::uint64_t SliceBytes =
        RowBytes + std::uint64_t(Height - 1) * Layout.RowPitch;
    if (Layout.SlicePitch < SliceBytes ||
        (Slices > 1 &&
         Layout.SlicePitch > (Remaining - SliceBytes) / (Slices - 1)))
      return false;
    Layouts[I] = {Layout.Offset, Layout.RowPitch, Layout.SlicePitch, 0};
  }
  Descriptor.Data = Image.Data;
  Descriptor.SizeInBytes = Image.SizeInBytes;
  Descriptor.Dimension = static_cast<std::uint32_t>(*Dimension);
  Descriptor.Format = static_cast<std::uint32_t>(Format);
  Descriptor.Width = Image.Width;
  Descriptor.Height = Image.Height;
  Descriptor.Depth = Image.Depth;
  Descriptor.FirstDepthSlice = Image.FirstDepthSlice;
  Descriptor.MipLevels = Image.MipLevels;
  Descriptor.ArrayLayers = Image.ArrayLayers;
  Descriptor.PlaneCount = Descriptor.SampleCount = 1;
  Descriptor.Flags = Image.Namespace == 1 ? feme::cpu::FEME_IMAGE_SAMPLED
                                          : feme::cpu::FEME_IMAGE_STORAGE;
  if (Image.Namespace == 1)
    Descriptor.Flags |= feme::cpu::FEME_IMAGE_LOAD_SWIZZLE;
  else
    Descriptor.Flags |= feme::cpu::FEME_IMAGE_SATURATE_INTEGER_STORES |
                        feme::cpu::FEME_IMAGE_OOB_FORMAT_DEFAULTS;
  Descriptor.MipLayouts = Layouts.data();
  Descriptor.MipLayoutCount = Image.MipLevels;
  Descriptor.Swizzle = componentSwizzle(Image.Shader4ComponentMapping);
  return true;
}

bool prepareSampler(const FeMeWarpSamplerBinding &Sampler,
                    feme::cpu::FemeSamplerDescriptor &Descriptor) {
  if (!FeMeWarpValidSampler(Sampler))
    return false;
  Descriptor.MinFilter = (Sampler.Filter >> 4) & 1;
  Descriptor.MagFilter = (Sampler.Filter >> 2) & 1;
  Descriptor.MipFilter = Sampler.Filter & 1;
  Descriptor.AddressU = Sampler.AddressU - 1;
  Descriptor.AddressV = Sampler.AddressV - 1;
  Descriptor.AddressW = Sampler.AddressW - 1;
  Descriptor.LodBias = Sampler.MipLODBias;
  Descriptor.MinLod = Sampler.MinLOD;
  Descriptor.MaxLod = Sampler.MaxLOD;
  Descriptor.CompareFunc = Sampler.ComparisonFunc - 1;
  std::copy(std::begin(Sampler.BorderColor), std::end(Sampler.BorderColor),
            Descriptor.BorderColor);
  Descriptor.MaxAnisotropy = Sampler.MaxAnisotropy;
  auto Reduction = (Sampler.Filter >> 7) & 3;
  Descriptor.ReductionMode = Reduction > 1 ? Reduction - 1 : 0;
  Descriptor.Flags =
      (Reduction == 1 ? feme::cpu::FEME_SAMPLER_COMPARE_ENABLE : 0) |
      (Sampler.Filter & 0x40 ? feme::cpu::FEME_SAMPLER_ANISOTROPY_ENABLE : 0);
  return true;
}

template <typename Input>
feme::cpu::BoundResourceNamespace bindingNamespace(const Input &Binding) {
  if constexpr (std::is_same_v<Input, FeMeWarpSamplerBinding>)
    return feme::cpu::BoundResourceNamespace::Unspecified;
  else
    return static_cast<feme::cpu::BoundResourceNamespace>(Binding.Namespace);
}

template <typename Input>
bool validateBindingIdentities(llvm::ArrayRef<Input> Inputs) {
  for (unsigned I = 0; I < Inputs.size(); ++I)
    for (unsigned J = 0; J < I; ++J)
      if (bindingNamespace(Inputs[I]) == bindingNamespace(Inputs[J]) &&
          Inputs[I].Space == Inputs[J].Space &&
          Inputs[I].Register == Inputs[J].Register) {
        llvm::errs() << "FeMe WARP dispatch: duplicate descriptor binding\n";
        return false;
      }
  return true;
}

template <typename Input, typename Descriptor, typename Binding>
bool bindRanges(const feme::cpu::ResourceInfo &Info,
                feme::cpu::BoundResourceClass Class,
                llvm::ArrayRef<Input> Inputs,
                const std::array<Descriptor, 64> &InputDescriptors,
                std::array<Descriptor, 64> &Descriptors,
                std::array<Binding, 64> &Bindings,
                std::uint32_t &BindingCount) {
  std::uint32_t DescriptorCount = 0;
  for (const auto &Range : Info.BoundRanges) {
    if (Range.Class != Class)
      continue;
    if (!Range.RangeSize ||
        Range.RangeSize > Descriptors.size() - DescriptorCount ||
        Range.BaseRegister >
            std::numeric_limits<std::uint32_t>::max() - (Range.RangeSize - 1)) {
      llvm::errs()
          << "FeMe WARP dispatch: unsupported descriptor range extent\n";
      return false;
    }
    auto *RangeDescriptors = Descriptors.data() + DescriptorCount;
    for (unsigned I = 0; I < Range.RangeSize; ++I) {
      bool Matched = false;
      for (unsigned J = 0; J < Inputs.size(); ++J)
        if (Range.Namespace == bindingNamespace(Inputs[J]) &&
            Range.Space == Inputs[J].Space &&
            Range.BaseRegister + I == Inputs[J].Register) {
          RangeDescriptors[I] = InputDescriptors[J];
          Matched = true;
          break;
        }
      if (!Matched) {
        llvm::errs()
            << "FeMe WARP dispatch: required descriptor range not bound"
            << " (class=" << static_cast<unsigned>(Class)
            << ", space=" << Range.Space
            << ", register=" << Range.BaseRegister + I << ")\n";
        return false;
      }
    }
    auto &BoundBinding = Bindings[BindingCount++];
    BoundBinding.Space = Range.Space;
    BoundBinding.BaseRegister = Range.BaseRegister;
    BoundBinding.Descriptors = {RangeDescriptors, Range.RangeSize};
    if constexpr (!std::is_same_v<Binding, feme::cpu::BoundSamplerBinding>)
      BoundBinding.Namespace = Range.Namespace;
    DescriptorCount += Range.RangeSize;
  }
  return true;
}

struct CompiledShader {
  std::shared_ptr<feme::cpu::CompiledStage> Stage;
  bool UsesNativeImageOrSamplerHeap = false;
};

std::int32_t report(llvm::Error Error) {
  llvm::logAllUnhandledErrors(std::move(Error), llvm::errs(),
                              "FeMe WARP shader compiler: ");
  return CompileFailure;
}

std::int32_t compileStage(feme::Context &Context, feme::Module Module,
                          feme::ShaderStage Stage, void **Compiled,
                          FeMeWarpCompiledShaderInfo *Info) {
  llvm::ModuleAnalysisManager Analyses;
  feme::dxil::OpRaisingPass().run(Module.getLLVMModule(), Analyses);
  feme::dxil::MetadataRaisingPass().run(Module.getLLVMModule(), Analyses);
  bool NativeImageHeap = false;
  for (const auto &F : Module.getLLVMModule())
    if (F.getIntrinsicID() == llvm::Intrinsic::dx_resource_handlefromheap)
      if (auto *Ty = llvm::dyn_cast<llvm::TargetExtType>(F.getReturnType()))
        if (Ty->getName() == "dx.Texture" || Ty->getName() == "dx.MSTexture" ||
            Ty->getName() == "dx.Sampler")
          NativeImageHeap |= !F.use_empty();
  feme::cpu::StageCompileOptions Options;
  Options.Stage = Stage;
  Options.WaveSize = 4;
  Options.PromoteInferredRootConstants = false;
  auto Result =
      feme::cpu::CompiledStage::create(Context, std::move(Module), Options);
  if (!Result)
    return report(Result.takeError());
  if (!(*Result)->EntryFn) {
    llvm::errs() << "FeMe WARP shader compiler: missing native entry point\n";
    return CompileFailure;
  }
  auto *Object =
      new (std::nothrow) CompiledShader{std::move(*Result), NativeImageHeap};
  if (!Object) {
    llvm::errs()
        << "FeMe WARP shader compiler: native wrapper allocation failed\n";
    return OutOfMemory;
  }
  FeMeWarpCompiledShaderInfo Output;
  Output.ArtifactVersion = feme::cpu::ArtifactAbiVersion;
  Output.Stage = static_cast<std::uint32_t>(Object->Stage->getStage());
  Output.WaveSize = Object->Stage->getWaveSize();
  Output.HasNativeEntry = 1;
  *Info = Output;
  *Compiled = Object;
  return 0;
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
      Info->BridgeVersion != FeMeWarpCompilerBridgeVersion || Count < 2 ||
      Count > 4 * 1024 * 1024 || Tokens[1] != Count) {
    llvm::errs() << "FeMe WARP shader compiler: invalid token buffer or ABI\n";
    return InvalidArgument;
  }
  auto Major = (Tokens[0] >> 4) & 0xf;
  if (Major != 4 && Major != 5) {
    llvm::errs() << "FeMe WARP shader compiler: expected legacy DXBC tokens\n";
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
  return compileStage(Context, std::move(*Module), Stage, Compiled, Info);
}

std::int32_t FeMeWarpCompileDXILProgram(const std::uint32_t *Program,
                                        std::uint32_t Count, void **Compiled,
                                        FeMeWarpCompiledShaderInfo *Info) {
  if (Compiled)
    *Compiled = nullptr;
  if (!Program || !Compiled || !Info || Info->Size != sizeof(*Info) ||
      Info->BridgeVersion != FeMeWarpCompilerBridgeVersion || Count < 6 ||
      Count > 4 * 1024 * 1024) {
    llvm::errs() << "FeMe WARP DXIL compiler: invalid program buffer or ABI\n";
    return InvalidArgument;
  }
  llvm::dxbc::ProgramHeader Header;
  std::memcpy(&Header, Program, sizeof(Header));
  constexpr std::size_t BitcodeBase =
      offsetof(llvm::dxbc::ProgramHeader, Bitcode);
  std::size_t Bytes = static_cast<std::size_t>(Count) * sizeof(*Program);
  if (Header.Size != Count || Header.getMajorVersion() != 6 ||
      (Header.ShaderKind != 1 && Header.ShaderKind != 5) || Header.Unused ||
      std::memcmp(Header.Bitcode.Magic, "DXIL", 4) ||
      Header.Bitcode.MajorVersion != 1 || Header.Bitcode.Unused ||
      Header.Bitcode.Offset < sizeof(Header.Bitcode) ||
      Header.Bitcode.Offset > Bytes - BitcodeBase || !Header.Bitcode.Size ||
      Header.Bitcode.Size > Bytes - BitcodeBase - Header.Bitcode.Offset) {
    llvm::errs()
        << "FeMe WARP DXIL compiler: invalid supported-stage program header\n";
    return InvalidArgument;
  }
  auto *Data = reinterpret_cast<const char *>(Program) + BitcodeBase +
               Header.Bitcode.Offset;
  feme::Context Context;
  feme::DXILImporter Importer;
  auto Module = Importer.import(
      llvm::MemoryBufferRef(llvm::StringRef(Data, Header.Bitcode.Size),
                            "WARP DDI DXIL program"),
      {}, Context);
  if (!Module)
    return report(Module.takeError());
  auto *Model = Module->getLLVMModule().getNamedMetadata("dx.shaderModel");
  auto *Description =
      Model && Model->getNumOperands() == 1 ? Model->getOperand(0) : nullptr;
  auto *Kind = Description && Description->getNumOperands() == 3
                   ? llvm::dyn_cast<llvm::MDString>(Description->getOperand(0))
                   : nullptr;
  auto *Major = Kind ? llvm::mdconst::dyn_extract<llvm::ConstantInt>(
                           Description->getOperand(1))
                     : nullptr;
  auto *Minor = Kind ? llvm::mdconst::dyn_extract<llvm::ConstantInt>(
                           Description->getOperand(2))
                     : nullptr;
  bool Vertex = Header.ShaderKind == 1;
  if (!Kind || Kind->getString() != (Vertex ? "vs" : "cs") || !Major ||
      !Minor || !Major->getType()->isIntegerTy(32) ||
      !Minor->getType()->isIntegerTy(32) ||
      Major->getZExtValue() != Header.getMajorVersion() ||
      Minor->getZExtValue() != Header.getMinorVersion()) {
    llvm::errs()
        << "FeMe WARP DXIL compiler: program/metadata stage mismatch\n";
    return InvalidArgument;
  }
  return compileStage(Context, std::move(*Module),
                      Vertex ? feme::ShaderStage::Vertex
                             : feme::ShaderStage::Compute,
                      Compiled, Info);
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
  *Pipeline = new (std::nothrow) CompiledShader{
      Stage,
      static_cast<CompiledShader *>(Shader)->UsesNativeImageOrSamplerHeap};
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
  if (!Pipeline || !Desc || Desc->Size != sizeof(*Desc) ||
      Desc->Version != FeMeWarpCompilerBridgeVersion ||
      Desc->BufferCount > 64 || (Desc->BufferCount && !Desc->Buffers) ||
      Desc->ImageCount > 64 || (Desc->ImageCount && !Desc->Images) ||
      Desc->SamplerCount > 64 || (Desc->SamplerCount && !Desc->Samplers)) {
    llvm::errs() << "FeMe WARP dispatch: invalid descriptor or ABI\n";
    return InvalidArgument;
  }
  auto &Stage = *static_cast<CompiledShader *>(Pipeline)->Stage;
  const auto &Info = Stage.getResourceInfo();
  if (Stage.getStage() != feme::ShaderStage::Compute || Info.RootConstantSize ||
      static_cast<CompiledShader *>(Pipeline)->UsesNativeImageOrSamplerHeap) {
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

  std::array<feme::cpu::FemeDescriptor, 64> InputDescriptors{}, Descriptors{};
  std::array<feme::cpu::BoundResourceBinding, 64> Bindings{};
  for (std::uint32_t I = 0; I < Desc->BufferCount; ++I) {
    const auto &Buffer = Desc->Buffers[I];
    auto Format = typedBufferFormat(Buffer.Format);
    auto ElementSize = typedBufferElementSize(Format);
    if ((!Buffer.Data != !Buffer.SizeInBytes) ||
        !FeMeWarpValidComponentMapping(Buffer.Shader4ComponentMapping) ||
        (Buffer.Shader4ComponentMapping != FeMeWarpDefaultComponentMapping &&
         (Buffer.Kind != 2 || Buffer.Namespace != 1)) ||
        (!Buffer.CounterData != !Buffer.CounterSizeInBytes) ||
        (Buffer.CounterData &&
         (Buffer.Kind != 1 || Buffer.Namespace != 2 || !Buffer.Data ||
          Buffer.CounterSizeInBytes != sizeof(std::uint32_t) ||
          reinterpret_cast<std::uintptr_t>(Buffer.CounterData) %
              alignof(std::uint32_t))) ||
        Buffer.Kind > 2 || (Buffer.Namespace < 1 || Buffer.Namespace > 3) ||
        (Buffer.Namespace == 3 && Buffer.Kind != 0) ||
        (Buffer.Kind == 0 && Buffer.Stride) ||
        (Buffer.Kind != 2 && Buffer.Format) ||
        (Buffer.Kind == 2 && (Buffer.Stride || !ElementSize ||
                              Buffer.SizeInBytes % ElementSize)) ||
        (Buffer.Kind == 1 && (!Buffer.Stride || Buffer.Stride % 4 ||
                              Buffer.SizeInBytes % Buffer.Stride))) {
      llvm::errs() << "FeMe WARP dispatch: invalid buffer binding\n";
      return InvalidArgument;
    }
    auto &Descriptor = InputDescriptors[I];
    Descriptor.Data = Buffer.Data;
    Descriptor.SizeInBytes = Buffer.SizeInBytes;
    Descriptor.Kind = static_cast<std::uint32_t>(
        Buffer.Kind == 2   ? feme::cpu::ResourceKind::Typed
        : Buffer.Kind == 1 ? feme::cpu::ResourceKind::Structured
                           : feme::cpu::ResourceKind::Raw);
    Descriptor.Stride = Buffer.Stride;
    Descriptor.Format = static_cast<std::uint32_t>(Format);
    Descriptor.Swizzle = componentSwizzle(Buffer.Shader4ComponentMapping);
    Descriptor.Flags =
        Buffer.Namespace == 2 ? feme::cpu::FEME_DESCRIPTOR_UAV : 0;
    if (Buffer.Namespace == 2 && Buffer.Kind == 2)
      Descriptor.Flags |= feme::cpu::FEME_DESCRIPTOR_SATURATE_INTEGER_STORES;
    Descriptor.Counter = Buffer.CounterData;
    if (Descriptor.Counter)
      Descriptor.Flags |= feme::cpu::FEME_DESCRIPTOR_HAS_COUNTER;
  }
  std::array<feme::cpu::FemeImageDescriptor, 64> InputImages{}, Images{};
  std::array<std::array<feme::cpu::FemeImageSubresourceLayout,
                        FeMeWarpMaxImageMipLevels>,
             64>
      ImageLayouts{};
  std::array<feme::cpu::BoundImageBinding, 64> ImageBindings{};
  std::array<feme::cpu::FemeSamplerDescriptor, 64> InputSamplers{}, Samplers{};
  std::array<feme::cpu::BoundSamplerBinding, 64> SamplerBindings{};
  for (unsigned I = 0; I < Desc->ImageCount; ++I)
    if (!prepareImage(Desc->Images[I], InputImages[I], ImageLayouts[I])) {
      llvm::errs() << "FeMe WARP dispatch: invalid image binding\n";
      return InvalidArgument;
    }
  for (unsigned I = 0; I < Desc->SamplerCount; ++I)
    if (!prepareSampler(Desc->Samplers[I], InputSamplers[I])) {
      llvm::errs() << "FeMe WARP dispatch: invalid sampler binding\n";
      return InvalidArgument;
    }
  llvm::ArrayRef<FeMeWarpBufferBinding> BufferInputs(Desc->Buffers,
                                                     Desc->BufferCount);
  llvm::ArrayRef<FeMeWarpImageBinding> ImageInputs(Desc->Images,
                                                   Desc->ImageCount);
  llvm::ArrayRef<FeMeWarpSamplerBinding> SamplerInputs(Desc->Samplers,
                                                       Desc->SamplerCount);
  std::uint32_t BindingCount = 0, ImageBindingCount = 0,
                SamplerBindingCount = 0;
  if (!validateBindingIdentities(BufferInputs) ||
      !validateBindingIdentities(ImageInputs) ||
      !validateBindingIdentities(SamplerInputs) ||
      !bindRanges(Info, feme::cpu::BoundResourceClass::Buffer, BufferInputs,
                  InputDescriptors, Descriptors, Bindings, BindingCount) ||
      !bindRanges(Info, feme::cpu::BoundResourceClass::Image, ImageInputs,
                  InputImages, Images, ImageBindings, ImageBindingCount) ||
      !bindRanges(Info, feme::cpu::BoundResourceClass::Sampler, SamplerInputs,
                  InputSamplers, Samplers, SamplerBindings,
                  SamplerBindingCount))
    return InvalidArgument;
  llvm::ArrayRef<feme::cpu::BoundResourceBinding> Bound(Bindings.data(),
                                                        BindingCount);
  if (auto Error = feme::cpu::validateBoundResourceBindings(Info, Bound))
    return report(std::move(Error));
  llvm::ArrayRef<feme::cpu::BoundImageBinding> BoundImages(ImageBindings.data(),
                                                           ImageBindingCount);
  if (auto Error = feme::cpu::validateBoundImageBindings(Info, BoundImages))
    return report(std::move(Error));
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
  Resources.BoundImages = BoundImages;
  Resources.BoundSamplers = {SamplerBindings.data(), SamplerBindingCount};
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

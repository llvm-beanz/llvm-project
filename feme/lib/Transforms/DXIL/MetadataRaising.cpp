//===- MetadataRaising.cpp - Raise dx.* module metadata ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/MetadataRaising.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

using namespace llvm;
using namespace feme::dxil;

namespace {

/// The tag values DXIL uses in an entry point's flat "properties" metadata
/// list (a sequence of tag/value pairs). These are DXIL's frozen wire-format
/// encoding -- see `DxilMDHelper::kDxil*Tag` in the DirectX Shader Compiler
/// -- so hard-coding the handful this pass consumes is safe.
enum EntryPropTag : unsigned {
  NumThreadsTag = 4,
  ShaderKindTag = 8,
  WaveSizeTag = 11,
  // `DxilMDHelper::kDxilEntryRootSigTag`: an entry's serialized root
  // signature, stored as a byte blob (see `applyEntryProps`'s
  // `RootSignature` handling and `feme::dxil::setRootSignature`) rather
  // than by reference to a separate named metadata node. FeMe does not
  // parse the blob itself yet (roadmap W2); this only preserves it.
  EntryRootSigTag = 12,
};

/// Maps a DXIL shader model profile string (the first operand of
/// `!dx.shaderModel`) to the target triple environment naming the same
/// pipeline stage.
std::optional<Triple::EnvironmentType> getEnvForProfile(StringRef Profile) {
  return StringSwitch<std::optional<Triple::EnvironmentType>>(Profile)
      .Case("ps", Triple::Pixel)
      .Case("vs", Triple::Vertex)
      .Case("gs", Triple::Geometry)
      .Case("hs", Triple::Hull)
      .Case("ds", Triple::Domain)
      .Case("cs", Triple::Compute)
      .Case("lib", Triple::Library)
      .Case("ms", Triple::Mesh)
      .Case("as", Triple::Amplification)
      .Default(std::nullopt);
}

/// Maps a DXIL `ShaderKind` (the value of an entry point's `ShaderKindTag`
/// property, only present in library shader models where each entry declares
/// its own stage) to the corresponding triple environment. Written out
/// explicitly rather than relying on the two enumerations happening to share
/// an ordering today.
std::optional<Triple::EnvironmentType> getEnvForShaderKind(uint64_t Kind) {
  switch (Kind) {
  case 0:
    return Triple::Pixel;
  case 1:
    return Triple::Vertex;
  case 2:
    return Triple::Geometry;
  case 3:
    return Triple::Hull;
  case 4:
    return Triple::Domain;
  case 5:
    return Triple::Compute;
  case 6:
    return Triple::Library;
  case 7:
    return Triple::RayGeneration;
  case 8:
    return Triple::Intersection;
  case 9:
    return Triple::AnyHit;
  case 10:
    return Triple::ClosestHit;
  case 11:
    return Triple::Miss;
  case 12:
    return Triple::Callable;
  case 13:
    return Triple::Mesh;
  case 14:
    return Triple::Amplification;
  default:
    return std::nullopt;
  }
}

/// Reads \p MD as a constant integer, or returns `std::nullopt` if it isn't
/// one (i.e. the metadata isn't shaped the way DXIL's writer produces).
std::optional<uint64_t> getMDInt(const Metadata *MD) {
  const auto *CAM = dyn_cast_or_null<ConstantAsMetadata>(MD);
  if (!CAM)
    return std::nullopt;
  const auto *CI = dyn_cast<ConstantInt>(CAM->getValue());
  if (!CI)
    return std::nullopt;
  return CI->getZExtValue();
}

/// The shader model and stage a DXIL module was compiled for, recovered from
/// its `!dx.shaderModel` metadata (`!{!"cs", i32 6, i32 5}`).
struct ShaderModel {
  Triple::EnvironmentType Env;
  uint64_t Major;
  uint64_t Minor;
};

std::optional<ShaderModel> getShaderModel(const Module &M) {
  const NamedMDNode *SMNode = M.getNamedMetadata("dx.shaderModel");
  if (!SMNode || SMNode->getNumOperands() != 1)
    return std::nullopt;
  const MDNode *SM = SMNode->getOperand(0);
  if (SM->getNumOperands() != 3)
    return std::nullopt;

  const auto *Profile = dyn_cast<MDString>(SM->getOperand(0));
  if (!Profile)
    return std::nullopt;
  std::optional<Triple::EnvironmentType> Env =
      getEnvForProfile(Profile->getString());
  std::optional<uint64_t> Major = getMDInt(SM->getOperand(1));
  std::optional<uint64_t> Minor = getMDInt(SM->getOperand(2));
  if (!Env || !Major || !Minor)
    return std::nullopt;
  return ShaderModel{*Env, *Major, *Minor};
}

/// Applies the `hlsl.*` function attributes described by an entry point's
/// flat tag/value property list \p Props to \p F, records its root
/// signature bytes (if any -- see `feme::dxil::setRootSignature`), and
/// returns the stage the entry declares for itself, if any (only library
/// shader models carry a per-entry `ShaderKindTag`).
std::optional<Triple::EnvironmentType> applyEntryProps(Function &F,
                                                       const MDNode *Props) {
  std::optional<Triple::EnvironmentType> Env;
  if (!Props)
    return Env;

  for (unsigned I = 0, E = Props->getNumOperands(); I + 1 < E; I += 2) {
    std::optional<uint64_t> Tag = getMDInt(Props->getOperand(I));
    if (!Tag)
      continue;
    const Metadata *Value = Props->getOperand(I + 1);

    if (*Tag == ShaderKindTag) {
      if (std::optional<uint64_t> Kind = getMDInt(Value))
        Env = getEnvForShaderKind(*Kind);
      continue;
    }

    if (*Tag == NumThreadsTag) {
      const auto *Dims = dyn_cast_or_null<MDNode>(Value);
      if (!Dims || Dims->getNumOperands() != 3)
        continue;
      SmallString<32> NumThreads;
      raw_svector_ostream OS(NumThreads);
      bool Complete = true;
      for (unsigned D = 0; D != 3; ++D) {
        std::optional<uint64_t> Dim = getMDInt(Dims->getOperand(D));
        if (!Dim) {
          Complete = false;
          break;
        }
        OS << (D ? "," : "") << *Dim;
      }
      if (Complete)
        F.addFnAttr("hlsl.numthreads", NumThreads);
      continue;
    }

    if (*Tag == WaveSizeTag) {
      // SM 6.6 encodes a single preferred size, SM 6.8 a (min, max,
      // preferred) range; `DXILMetadataAnalysis` always expects the latter's
      // three-component spelling, so widen the single-value form to it.
      const auto *Sizes = dyn_cast_or_null<MDNode>(Value);
      if (!Sizes ||
          (Sizes->getNumOperands() != 1 && Sizes->getNumOperands() != 3))
        continue;
      SmallVector<uint64_t, 3> Values;
      for (const MDOperand &Op : Sizes->operands())
        if (std::optional<uint64_t> Size = getMDInt(Op.get()))
          Values.push_back(*Size);
      if (Values.size() != Sizes->getNumOperands())
        continue;
      if (Values.size() == 1)
        Values.assign({Values[0], 0, 0});
      SmallString<32> WaveSize;
      raw_svector_ostream OS(WaveSize);
      OS << Values[0] << "," << Values[1] << "," << Values[2];
      F.addFnAttr("hlsl.wavesize", WaveSize);
      continue;
    }

    if (*Tag == EntryRootSigTag) {
      // The blob is a single-operand `MDNode` around a `ConstantDataArray`
      // of bytes (see `feme::dxil::setRootSignature`'s comment for the
      // matching DXC encoding); preserved verbatim, since FeMe does not
      // parse a root signature's contents yet (roadmap W2).
      const auto *Blob = dyn_cast_or_null<MDNode>(Value);
      const auto *CAM = Blob && Blob->getNumOperands() == 1
                            ? dyn_cast_or_null<ConstantAsMetadata>(
                                  Blob->getOperand(0))
                            : nullptr;
      const auto *CDA =
          CAM ? dyn_cast<ConstantDataArray>(CAM->getValue()) : nullptr;
      if (CDA && CDA->getElementType()->isIntegerTy(8)) {
        StringRef Raw = CDA->getRawDataValues();
        feme::dxil::setRootSignature(
            F, ArrayRef(reinterpret_cast<const uint8_t *>(Raw.data()),
                       Raw.size()));
      }
      continue;
    }
  }
  return Env;
}

} // namespace

PreservedAnalyses MetadataRaisingPass::run(Module &M, ModuleAnalysisManager &) {
  std::optional<ShaderModel> SM = getShaderModel(M);
  if (!SM)
    return PreservedAnalyses::all();

  SmallString<48> TripleStr;
  raw_svector_ostream TripleOS(TripleStr);
  TripleOS << "dxil-unknown-shadermodel" << SM->Major << "." << SM->Minor << "-"
           << Triple::getEnvironmentTypeName(SM->Env);
  M.setTargetTriple(Triple(TripleStr.str()));

  if (NamedMDNode *Entries = M.getNamedMetadata("dx.entryPoints")) {
    for (const MDNode *Entry : Entries->operands()) {
      if (Entry->getNumOperands() != 5)
        continue;
      const auto *FnMD =
          dyn_cast_or_null<ConstantAsMetadata>(Entry->getOperand(0).get());
      auto *F = FnMD ? dyn_cast<Function>(FnMD->getValue()) : nullptr;
      if (!F)
        continue;

      std::optional<Triple::EnvironmentType> EntryEnv = applyEntryProps(
          *F, dyn_cast_or_null<MDNode>(Entry->getOperand(4).get()));
      Triple::EnvironmentType Env = EntryEnv.value_or(SM->Env);
      F->addFnAttr("hlsl.shader", Triple::getEnvironmentTypeName(Env));

      // The stage this entry point declares, recorded as the checked
      // `feme.shader.stage` enumeration described by "Stage identity" in
      // feme/docs/FeMeGraphicsDesign.md, and diagnosed against the module
      // triple's own environment: a `lib` shader model lets each entry
      // declare its own stage, but a stage-specific profile disagreeing
      // with an entry's `ShaderKind` is malformed input, not a stage FeMe
      // should silently pick a winner for.
      std::optional<feme::ShaderStage> Stage =
          feme::getShaderStageForEnvironment(Env);
      if (!Stage) {
        M.getContext().emitError("feme-dxil-raise-metadata: entry point '" +
                                 F->getName() + "' declares no shader stage");
        continue;
      }
      if (!feme::isShaderStageCompatibleWithEnvironment(*Stage, SM->Env)) {
        M.getContext().emitError(
            "feme-dxil-raise-metadata: entry point '" + F->getName() +
            "' declares stage '" + feme::getShaderStageName(*Stage) +
            "', which disagrees with the module's '" +
            Triple::getEnvironmentTypeName(SM->Env) + "' shader model");
        continue;
      }
      feme::setShaderStage(*F, *Stage);

      // Preserve the entry's input/output/patch-constant signature rows
      // (roadmap R18) into feme's source-independent `feme::EntrySignature`
      // model before the `Signatures` operand below is erased along with
      // the rest of `!dx.entryPoints`; see "Signature reflection" in
      // feme/docs/FeMeGraphicsDesign.md.
      const auto *Signatures =
          dyn_cast_or_null<MDNode>(Entry->getOperand(2).get());
      feme::dxil::setEntrySignature(
          *F, feme::dxil::convertEntrySignature(Signatures, *Stage));
    }
  }

  // Everything below is regenerated from scratch by `DXILTranslateMetadata`
  // when re-emitting DXIL, and is meaningless for any other target; leaving
  // it in place would make the backend append to (rather than replace) these
  // nodes. `dx.valver` is deliberately kept: `DXILMetadataAnalysis` reads the
  // original validator version back out of it.
  for (StringRef Name :
       {"dx.shaderModel", "dx.version", "dx.entryPoints", "dx.resources"})
    if (NamedMDNode *Node = M.getNamedMetadata(Name))
      M.eraseNamedMetadata(Node);

  return PreservedAnalyses::none();
}

//===- SignatureImport.cpp - DXIL !dx.entryPoints signature import ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme;

namespace {

/// `DXIL::SemanticKind`, as stored in a signature element's metadata (field
/// `kDxilSignatureElementSystemValue`). Mirrors the encoding
/// `feme::dxsa::translateToLLVMIR` writes in
/// feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp, since both read and
/// write the same DXIL wire format; see DirectXShaderCompiler's
/// `DXIL::SemanticKind` for the authoritative definition.
enum class DXILSemanticKind : unsigned {
  Arbitrary = 0,
  VertexID = 1,
  InstanceID = 2,
  Position = 3,
  RenderTargetArrayIndex = 4,
  ViewPortArrayIndex = 5,
  ClipDistance = 6,
  CullDistance = 7,
  OutputControlPointID = 8,
  DomainLocation = 9,
  PrimitiveID = 10,
  GSInstanceID = 11,
  SampleIndex = 12,
  IsFrontFace = 13,
  Coverage = 14,
  InnerCoverage = 15,
  Target = 16,
  Depth = 17,
  DepthLessEqual = 18,
  DepthGreaterEqual = 19,
  StencilRef = 20,
  TessFactor = 25,
  InsideTessFactor = 26,
  Barycentrics = 28,
  ShadingRate = 29,
  CullPrimitive = 30,
};

/// `DXIL::ComponentType`, as stored in a signature element's metadata (field
/// `kDxilSignatureElementType`). See the same DXSA translator file for the
/// writer side of this encoding.
enum class DXILComponentType : unsigned {
  I16 = 2,
  U16 = 3,
  I32 = 4,
  U32 = 5,
  F16 = 8,
  F32 = 9,
  F64 = 10,
  SNormF32 = 11,
  UNormF32 = 12,
};

/// Maps a DXIL signature element's `SystemValue` to the
/// `feme::SignatureSystemValue` it corresponds to, or `None` if it names a
/// system value FeMe does not model yet (tessellation, geometry/mesh and
/// shading-rate builtins are out of scope until their own milestones; see
/// `feme::SignatureSystemValue`'s comment). `Target` (a fragment shader's
/// render-target output) is deliberately mapped to `None` too: unlike the
/// other system values, it identifies an ordinary user-varying output
/// selected by `Location`, not a builtin FeMe needs to special-case.
feme::SignatureSystemValue getSystemValue(DXILSemanticKind Kind) {
  switch (Kind) {
  case DXILSemanticKind::VertexID:
    return feme::SignatureSystemValue::VertexID;
  case DXILSemanticKind::InstanceID:
    return feme::SignatureSystemValue::InstanceID;
  case DXILSemanticKind::Position:
    return feme::SignatureSystemValue::Position;
  case DXILSemanticKind::RenderTargetArrayIndex:
    return feme::SignatureSystemValue::RenderTargetArrayIndex;
  case DXILSemanticKind::ViewPortArrayIndex:
    return feme::SignatureSystemValue::ViewportArrayIndex;
  case DXILSemanticKind::ClipDistance:
    return feme::SignatureSystemValue::ClipDistance;
  case DXILSemanticKind::CullDistance:
    return feme::SignatureSystemValue::CullDistance;
  case DXILSemanticKind::PrimitiveID:
    return feme::SignatureSystemValue::PrimitiveID;
  case DXILSemanticKind::SampleIndex:
    return feme::SignatureSystemValue::SampleIndex;
  case DXILSemanticKind::IsFrontFace:
    return feme::SignatureSystemValue::IsFrontFace;
  case DXILSemanticKind::Coverage:
    return feme::SignatureSystemValue::Coverage;
  // `DepthLessEqual`/`DepthGreaterEqual` are conservative-depth variants of
  // plain `Depth`; FeMe does not yet distinguish them (see
  // `SignatureSystemValue`), so all three collapse onto `Depth`.
  case DXILSemanticKind::Depth:
  case DXILSemanticKind::DepthLessEqual:
  case DXILSemanticKind::DepthGreaterEqual:
    return feme::SignatureSystemValue::Depth;
  case DXILSemanticKind::StencilRef:
    return feme::SignatureSystemValue::StencilRef;
  case DXILSemanticKind::Arbitrary:
  case DXILSemanticKind::OutputControlPointID:
  case DXILSemanticKind::DomainLocation:
  case DXILSemanticKind::GSInstanceID:
  case DXILSemanticKind::InnerCoverage:
  case DXILSemanticKind::Target:
  case DXILSemanticKind::TessFactor:
  case DXILSemanticKind::InsideTessFactor:
  case DXILSemanticKind::Barycentrics:
  case DXILSemanticKind::ShadingRate:
  case DXILSemanticKind::CullPrimitive:
    return feme::SignatureSystemValue::None;
  }
  return feme::SignatureSystemValue::None;
}

/// Maps a DXIL signature element's `Type` to the `(ComponentType, BitWidth)`
/// pair FeMe's model splits it into. The normalized variants (`SNormF32`/
/// `UNormF32`) have no separate representation yet, so they read back as
/// plain `F32`; FeMe has no signature consumer that distinguishes them
/// today.
std::pair<feme::SignatureComponentType, uint32_t>
getComponentType(DXILComponentType Type) {
  switch (Type) {
  case DXILComponentType::I16:
    return {feme::SignatureComponentType::SInt, 16};
  case DXILComponentType::U16:
    return {feme::SignatureComponentType::UInt, 16};
  case DXILComponentType::I32:
    return {feme::SignatureComponentType::SInt, 32};
  case DXILComponentType::U32:
    return {feme::SignatureComponentType::UInt, 32};
  case DXILComponentType::F16:
    return {feme::SignatureComponentType::Float, 16};
  case DXILComponentType::F32:
  case DXILComponentType::SNormF32:
  case DXILComponentType::UNormF32:
    return {feme::SignatureComponentType::Float, 32};
  case DXILComponentType::F64:
    return {feme::SignatureComponentType::Float, 64};
  }
  return {feme::SignatureComponentType::Float, 32};
}

/// Maps a DXIL signature element's `InterpMode` (`DXIL::InterpolationMode`,
/// which matches `D3D10_SB_INTERPOLATION_MODE`) to
/// `feme::SignatureInterpolationMode`. `Undefined` (an element interpolation
/// does not apply to, e.g. a system-value input) collapses onto `Flat`
/// alongside `Constant`, since FeMe's model has no separate "not
/// interpolated at all" state.
feme::SignatureInterpolationMode getInterpolationMode(uint64_t Mode) {
  switch (Mode) {
  case 0: // Undefined
  case 1: // Constant
    return feme::SignatureInterpolationMode::Flat;
  case 2: // Linear
    return feme::SignatureInterpolationMode::Perspective;
  case 3: // LinearCentroid
    return feme::SignatureInterpolationMode::PerspectiveCentroid;
  case 4: // LinearNoperspective
    return feme::SignatureInterpolationMode::NoPerspective;
  case 5: // LinearNoperspectiveCentroid
    return feme::SignatureInterpolationMode::NoPerspectiveCentroid;
  case 6: // LinearSample
    return feme::SignatureInterpolationMode::PerspectiveSample;
  case 7: // LinearNoperspectiveSample
    return feme::SignatureInterpolationMode::NoPerspectiveSample;
  default:
    return feme::SignatureInterpolationMode::Flat;
  }
}

/// Reads \p MD as a constant integer, or returns `std::nullopt` if it isn't
/// shaped the way DXIL's writer produces (mirrors the identically-named
/// helper in MetadataRaising.cpp; kept local since neither file exports
/// it).
std::optional<uint64_t> getMDInt(const Metadata *MD) {
  const auto *CAM = dyn_cast_or_null<ConstantAsMetadata>(MD);
  if (!CAM)
    return std::nullopt;
  const auto *CI = dyn_cast<ConstantInt>(CAM->getValue());
  if (!CI)
    return std::nullopt;
  return CI->getZExtValue();
}

/// The field indices of a DXIL signature element's 11-operand metadata
/// tuple (`DxilMDHelper::kDxilSignatureElement*`).
enum SignatureElementField : unsigned {
  ElementIDField = 0,
  NameField = 1,
  TypeField = 2,
  SystemValueField = 3,
  IndexVectorField = 4,
  InterpModeField = 5,
  RowsField = 6,
  ColsField = 7,
  StartRowField = 8,
  StartColField = 9,
  NumSignatureElementFields = 11,
};

/// Converts one signature-element row (one operand of an input, output or
/// patch-constant signature list) to a `feme::SignatureElement`, or
/// `std::nullopt` if it is not shaped like DXIL's writer produces (rather
/// than crashing on malformed input).
std::optional<feme::SignatureElement>
convertSignatureElement(const Metadata *RowMD, feme::SignatureDirection Dir,
                        uint32_t ElementID) {
  const auto *Row = dyn_cast_or_null<MDNode>(RowMD);
  if (!Row || Row->getNumOperands() != NumSignatureElementFields)
    return std::nullopt;

  const auto *Name = dyn_cast_or_null<MDString>(Row->getOperand(NameField));
  std::optional<uint64_t> Type = getMDInt(Row->getOperand(TypeField));
  std::optional<uint64_t> SystemValue =
      getMDInt(Row->getOperand(SystemValueField));
  std::optional<uint64_t> InterpMode =
      getMDInt(Row->getOperand(InterpModeField));
  std::optional<uint64_t> Rows = getMDInt(Row->getOperand(RowsField));
  std::optional<uint64_t> Cols = getMDInt(Row->getOperand(ColsField));
  std::optional<uint64_t> StartRow = getMDInt(Row->getOperand(StartRowField));
  std::optional<uint64_t> StartCol = getMDInt(Row->getOperand(StartColField));
  if (!Name || !Type || !SystemValue || !InterpMode || !Rows || !Cols ||
      !StartRow || !StartCol)
    return std::nullopt;

  feme::SignatureElement Elt;
  // DXIL numbers each of the input, output and patch-constant signatures
  // independently starting at 0, so its own element ID is not unique across
  // all three; `feme::EntrySignature` requires a single ID space for the
  // whole entry point (`feme::verifySignature`'s uniqueness check), so this
  // renumbers by combined position instead of reusing DXIL's ID field.
  Elt.ElementID = ElementID;
  Elt.Direction = Dir;
  Elt.SemanticName = Name->getString().str();

  const auto Kind = static_cast<DXILSemanticKind>(*SystemValue);
  Elt.SystemValue = getSystemValue(Kind);
  // An ordinary user varying is identified by its packed register (Vulkan's
  // notion of "location"); a system value has none, per
  // `SignatureElement::Location`'s comment.
  if (Elt.SystemValue == feme::SignatureSystemValue::None)
    Elt.Location = static_cast<uint32_t>(*StartRow);

  if (const auto *IndexVector =
          dyn_cast_or_null<MDNode>(Row->getOperand(IndexVectorField).get()))
    if (IndexVector->getNumOperands() != 0)
      if (std::optional<uint64_t> FirstIndex =
              getMDInt(IndexVector->getOperand(0)))
        Elt.SemanticIndex = static_cast<uint32_t>(*FirstIndex);

  std::tie(Elt.ComponentType, Elt.BitWidth) =
      getComponentType(static_cast<DXILComponentType>(*Type));
  Elt.FirstComponent = static_cast<uint32_t>(*StartCol);
  Elt.ComponentCount = static_cast<uint32_t>(*Cols);
  Elt.RowCount = static_cast<uint32_t>(*Rows);
  Elt.Interpolation = getInterpolationMode(*InterpMode);
  Elt.Frequency = (Dir == feme::SignatureDirection::PatchInput ||
                  Dir == feme::SignatureDirection::PatchOutput)
                     ? feme::SignatureFrequency::PerPatch
                     : feme::SignatureFrequency::PerVertex;
  return Elt;
}

/// Appends every row of \p SignatureMD (one of an entry's input, output or
/// patch-constant signature lists) to \p Sig as elements with direction
/// \p Dir, numbered from \p NextID upward (see `convertSignatureElement`'s
/// comment on why DXIL's own per-list IDs aren't reused). Does nothing if
/// \p SignatureMD is null, which DXIL uses for an empty signature.
void convertSignature(const MDOperand &SignatureMD,
                      feme::SignatureDirection Dir, uint32_t &NextID,
                      feme::EntrySignature &Sig) {
  const auto *List = dyn_cast_or_null<MDNode>(SignatureMD.get());
  if (!List)
    return;
  for (const MDOperand &Row : List->operands()) {
    std::optional<feme::SignatureElement> Elt =
        convertSignatureElement(Row.get(), Dir, NextID);
    if (!Elt)
      continue;
    Sig.Elements.push_back(std::move(*Elt));
    ++NextID;
  }
}

} // namespace

feme::EntrySignature
feme::dxil::convertEntrySignature(const MDNode *Signatures,
                                  feme::ShaderStage Stage) {
  feme::EntrySignature Sig;
  if (!Signatures || Signatures->getNumOperands() != 3)
    return Sig;

  uint32_t NextID = 0;
  convertSignature(Signatures->getOperand(0), SignatureDirection::Input,
                   NextID, Sig);
  convertSignature(Signatures->getOperand(1), SignatureDirection::Output,
                   NextID, Sig);
  // A domain shader consumes patch-constant rows a hull shader produced;
  // every other stage carrying this list (in practice, only the hull shader
  // itself) produces them.
  SignatureDirection PatchDir = Stage == feme::ShaderStage::Domain
                                    ? SignatureDirection::PatchInput
                                    : SignatureDirection::PatchOutput;
  convertSignature(Signatures->getOperand(2), PatchDir, NextID, Sig);
  return Sig;
}

StringRef feme::dxil::getEntrySignatureMDKind() { return "feme.signature"; }

/// Wraps \p Bytes as function metadata: a single-operand `MDNode` around a
/// `ConstantDataArray<i8>`, the same encoding DXC's classic
/// `DxilMDHelper::EmitSerializedRootSignature` uses for an opaque byte blob
/// in metadata.
static MDNode *wrapBytes(LLVMContext &Ctx, ArrayRef<uint8_t> Bytes) {
  Constant *V = ConstantDataArray::get(Ctx, Bytes);
  return MDNode::get(Ctx, {ConstantAsMetadata::get(V)});
}

/// The inverse of `wrapBytes`: reads a single-operand `MDNode` around a
/// `ConstantDataArray<i8>` back to its raw bytes, or `std::nullopt` if
/// \p MD isn't shaped that way.
static std::optional<std::vector<uint8_t>> unwrapBytes(const MDNode *MD) {
  if (!MD || MD->getNumOperands() != 1)
    return std::nullopt;
  const auto *CAM = dyn_cast_or_null<ConstantAsMetadata>(MD->getOperand(0));
  if (!CAM)
    return std::nullopt;
  const auto *CDA = dyn_cast<ConstantDataArray>(CAM->getValue());
  if (!CDA || !CDA->getElementType()->isIntegerTy(8))
    return std::nullopt;
  StringRef Raw = CDA->getRawDataValues();
  return std::vector<uint8_t>(Raw.bytes_begin(), Raw.bytes_end());
}

void feme::dxil::setEntrySignature(Function &F, const EntrySignature &Sig) {
  std::vector<uint8_t> Bytes = serializeSignature(Sig);
  F.setMetadata(getEntrySignatureMDKind(), wrapBytes(F.getContext(), Bytes));
}

std::optional<EntrySignature>
feme::dxil::getEntrySignature(const Function &F) {
  const MDNode *MD = F.getMetadata(getEntrySignatureMDKind());
  std::optional<std::vector<uint8_t>> Bytes = unwrapBytes(MD);
  if (!Bytes)
    return std::nullopt;
  Expected<EntrySignature> Sig = parseSignature(*Bytes);
  if (!Sig) {
    consumeError(Sig.takeError());
    return std::nullopt;
  }
  return *Sig;
}

StringRef feme::dxil::getRootSignatureMDKind() {
  return "feme.dxil.rootsignature";
}

void feme::dxil::setRootSignature(Function &F, ArrayRef<uint8_t> Bytes) {
  F.setMetadata(getRootSignatureMDKind(), wrapBytes(F.getContext(), Bytes));
}

std::optional<std::vector<uint8_t>>
feme::dxil::getRootSignature(const Function &F) {
  return unwrapBytes(F.getMetadata(getRootSignatureMDKind()));
}

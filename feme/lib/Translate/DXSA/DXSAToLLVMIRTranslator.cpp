//===- DXSAToLLVMIRTranslator.cpp - dxsa dialect -> DXIL -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lowers a decoded DXBC program (the `dxsa` dialect) to DXIL-shaped LLVM IR.
//
// DXBC is a 4-component-vector ISA; DXIL is scalar. The translation is
// therefore component-wise: every dxsa instruction expands to one LLVM
// computation per component its destination write mask enables, reading
// each source through that component's swizzle. Signature registers are not
// materialized at all -- an input read becomes a `dx.op.loadInput` call and
// an output write a `dx.op.storeOutput` call, which is what makes the result
// DXIL rather than generic LLVM IR.
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/DXSA/DXSAToLLVMIRTranslator.h"

#include "feme/Dialect/DXSA/IR/DXSA.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"

using namespace feme;
using namespace feme::dxsa;

namespace {

//===----------------------------------------------------------------------===//
// DXIL constants
//===----------------------------------------------------------------------===//

/// The subset of `DXIL::OpCode` this translation emits. Values are fixed by
/// the DXIL specification and are what a `dx.op.*` call's first argument
/// carries.
enum class DXILOp : unsigned {
  LoadInput = 4,
  StoreOutput = 5,
  FAbs = 6,
  Saturate = 7,
  Cos = 12,
  Sin = 13,
  Exp = 21,
  Frc = 22,
  Log = 23,
  Sqrt = 24,
  Rsqrt = 25,
  RoundNe = 26,
  RoundNi = 27,
  RoundPi = 28,
  RoundZ = 29,
  Bfrev = 30,
  Countbits = 31,
  FirstbitLo = 32,
  FirstbitHi = 33,
  FirstbitSHi = 34,
  FMax = 35,
  FMin = 36,
  IMax = 37,
  IMin = 38,
  UMax = 39,
  UMin = 40,
  FMad = 46,
  IMad = 48,
  UMad = 49,
  Ibfe = 51,
  Ubfe = 52,
  Bfi = 53,
  Dot2 = 54,
  Dot3 = 55,
  Dot4 = 56,
  CreateHandle = 57,
  CBufferLoadLegacy = 59,
  Sample = 60,
  SampleBias = 61,
  SampleLevel = 62,
  SampleGrad = 63,
  SampleCmp = 64,
  SampleCmpLevelZero = 65,
  TextureLoad = 66,
  TextureStore = 67,
  BufferLoad = 68,
  BufferStore = 69,
  CheckAccessFullyMapped = 71,
  TextureGather = 73,
  TextureGatherCmp = 74,
  CalculateLOD = 81,
  Discard = 82,
  DerivCoarseX = 83,
  DerivCoarseY = 84,
  DerivFineX = 85,
  DerivFineY = 86,
  SampleIndex = 90,
  Coverage = 91,
  InnerCoverage = 92,
  ThreadId = 93,
  GroupId = 94,
  ThreadIdInGroup = 95,
  FlattenedThreadIdInGroup = 96,
  CycleCounterLegacy = 109,
  BitcastI32toF32 = 126,
  BitcastF32toI32 = 127,
  LegacyF32ToF16 = 130,
  LegacyF16ToF32 = 131,
};

/// `DXIL::ResourceClass`, as named by `dx.op.createHandle`'s first argument.
enum class ResourceClass : unsigned { SRV = 0, UAV = 1, CBV = 2, Sampler = 3 };

/// `DXIL::ResourceKind`, as recorded in a resource's `!dx.resources` entry.
enum class ResourceKind : unsigned {
  Texture1D = 1,
  Texture2D = 2,
  Texture2DMS = 3,
  Texture3D = 4,
  TextureCube = 5,
  Texture1DArray = 6,
  Texture2DArray = 7,
  Texture2DMSArray = 8,
  TextureCubeArray = 9,
  TypedBuffer = 10,
  RawBuffer = 11,
  StructuredBuffer = 12,
  Sampler = 14,
};

/// The `DXIL::ResourceKind` a DXBC resource dimension names.
ResourceKind toResourceKind(ResourceDimension Dim) {
  switch (Dim) {
  case ResourceDimension::buffer:
    return ResourceKind::TypedBuffer;
  case ResourceDimension::texture1d:
    return ResourceKind::Texture1D;
  case ResourceDimension::texture2d:
    return ResourceKind::Texture2D;
  case ResourceDimension::texture2dms:
    return ResourceKind::Texture2DMS;
  case ResourceDimension::texture3d:
    return ResourceKind::Texture3D;
  case ResourceDimension::texturecube:
    return ResourceKind::TextureCube;
  case ResourceDimension::texture1darray:
    return ResourceKind::Texture1DArray;
  case ResourceDimension::texture2darray:
    return ResourceKind::Texture2DArray;
  case ResourceDimension::texture2dmsarray:
    return ResourceKind::Texture2DMSArray;
  case ResourceDimension::texturecubearray:
    return ResourceKind::TextureCubeArray;
  }
  return ResourceKind::Texture2D;
}

/// Whether a resource of \p Kind is a buffer, which is addressed by an
/// index rather than by coordinates and has no mip level.
bool isBuffer(ResourceKind Kind) {
  return Kind == ResourceKind::TypedBuffer || Kind == ResourceKind::RawBuffer ||
         Kind == ResourceKind::StructuredBuffer;
}

/// The number of coordinates an address in a resource of \p Kind has, not
/// counting the mip level or sample index a load also takes.
unsigned coordinateCount(ResourceKind Kind) {
  switch (Kind) {
  case ResourceKind::TypedBuffer:
  case ResourceKind::RawBuffer:
  case ResourceKind::StructuredBuffer:
  case ResourceKind::Texture1D:
    return 1;
  case ResourceKind::Texture1DArray:
  case ResourceKind::Texture2D:
  case ResourceKind::Texture2DMS:
    return 2;
  case ResourceKind::Texture2DArray:
  case ResourceKind::Texture2DMSArray:
  case ResourceKind::Texture3D:
  case ResourceKind::TextureCube:
    return 3;
  case ResourceKind::TextureCubeArray:
    return 4;
  case ResourceKind::Sampler:
    break;
  }
  return 2;
}

/// The number of *spatial* coordinates an address in a resource of \p Kind
/// has: its dimensions without the array slice, which is what a level-of-
/// detail calculation is interested in.
unsigned spatialCount(ResourceKind Kind) {
  switch (Kind) {
  case ResourceKind::Texture1D:
  case ResourceKind::Texture1DArray:
    return 1;
  case ResourceKind::Texture3D:
  case ResourceKind::TextureCube:
  case ResourceKind::TextureCubeArray:
    return 3;
  default:
    break;
  }
  return 2;
}

/// The number of texel offsets an address in a resource of \p Kind takes;
/// an array slice does not get one, and a cube map has none at all.
unsigned offsetCount(ResourceKind Kind) {
  switch (Kind) {
  case ResourceKind::Texture1D:
  case ResourceKind::Texture1DArray:
    return 1;
  case ResourceKind::Texture2D:
  case ResourceKind::Texture2DArray:
  case ResourceKind::Texture2DMS:
  case ResourceKind::Texture2DMSArray:
    return 2;
  case ResourceKind::Texture3D:
    return 3;
  default:
    break;
  }
  return 0;
}

/// `DXIL::ComponentType`, as stored in a signature element's metadata.
enum class DXILComponentType : unsigned {
  I16 = 2,
  U16 = 3,
  I32 = 4,
  U32 = 5,
  F16 = 8,
  F32 = 9,
  F64 = 10,
  SNormF32 = 11,
  UNormF32 = 12
};

/// `DXIL::SemanticKind`, as stored in a signature element's metadata.
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

/// The `SV_`-prefixed name DXIL gives a system value in its signature
/// metadata.
llvm::StringRef semanticName(DXILSemanticKind Kind) {
  switch (Kind) {
  case DXILSemanticKind::VertexID:
    return "SV_VertexID";
  case DXILSemanticKind::InstanceID:
    return "SV_InstanceID";
  case DXILSemanticKind::Position:
    return "SV_Position";
  case DXILSemanticKind::RenderTargetArrayIndex:
    return "SV_RenderTargetArrayIndex";
  case DXILSemanticKind::ViewPortArrayIndex:
    return "SV_ViewportArrayIndex";
  case DXILSemanticKind::ClipDistance:
    return "SV_ClipDistance";
  case DXILSemanticKind::CullDistance:
    return "SV_CullDistance";
  case DXILSemanticKind::OutputControlPointID:
    return "SV_OutputControlPointID";
  case DXILSemanticKind::DomainLocation:
    return "SV_DomainLocation";
  case DXILSemanticKind::PrimitiveID:
    return "SV_PrimitiveID";
  case DXILSemanticKind::GSInstanceID:
    return "SV_GSInstanceID";
  case DXILSemanticKind::SampleIndex:
    return "SV_SampleIndex";
  case DXILSemanticKind::IsFrontFace:
    return "SV_IsFrontFace";
  case DXILSemanticKind::Coverage:
    return "SV_Coverage";
  case DXILSemanticKind::InnerCoverage:
    return "SV_InnerCoverage";
  case DXILSemanticKind::Target:
    return "SV_Target";
  case DXILSemanticKind::Depth:
    return "SV_Depth";
  case DXILSemanticKind::DepthLessEqual:
    return "SV_DepthLessEqual";
  case DXILSemanticKind::DepthGreaterEqual:
    return "SV_DepthGreaterEqual";
  case DXILSemanticKind::StencilRef:
    return "SV_StencilRef";
  case DXILSemanticKind::TessFactor:
    return "SV_TessFactor";
  case DXILSemanticKind::InsideTessFactor:
    return "SV_InsideTessFactor";
  case DXILSemanticKind::Barycentrics:
    return "SV_Barycentrics";
  case DXILSemanticKind::ShadingRate:
    return "SV_ShadingRate";
  case DXILSemanticKind::CullPrimitive:
    return "SV_CullPrimitive";
  case DXILSemanticKind::Arbitrary:
    break;
  }
  return "";
}

/// Maps a DXBC system-value name onto the DXIL semantic kind that names the
/// same value. The two enumerations describe the same set of system values
/// but number them differently, and DXIL folds the eight per-edge and
/// per-inside tessellation factors into two kinds.
DXILSemanticKind toSemanticKind(SystemValueName Name) {
  switch (Name) {
  case SystemValueName::position:
    return DXILSemanticKind::Position;
  case SystemValueName::clipDistance:
    return DXILSemanticKind::ClipDistance;
  case SystemValueName::cullDistance:
    return DXILSemanticKind::CullDistance;
  case SystemValueName::renderTargetArrayIndex:
    return DXILSemanticKind::RenderTargetArrayIndex;
  case SystemValueName::viewportArrayIndex:
    return DXILSemanticKind::ViewPortArrayIndex;
  case SystemValueName::vertexID:
    return DXILSemanticKind::VertexID;
  case SystemValueName::primitiveID:
    return DXILSemanticKind::PrimitiveID;
  case SystemValueName::instanceID:
    return DXILSemanticKind::InstanceID;
  case SystemValueName::isFrontFace:
    return DXILSemanticKind::IsFrontFace;
  case SystemValueName::sampleIndex:
    return DXILSemanticKind::SampleIndex;
  case SystemValueName::finalQuadUeq0EdgeTessFactor:
  case SystemValueName::finalQuadVeq0EdgeTessFactor:
  case SystemValueName::finalQuadUeq1EdgeTessFactor:
  case SystemValueName::finalQuadVeq1EdgeTessFactor:
  case SystemValueName::finalTriUeq0EdgeTessFactor:
  case SystemValueName::finalTriVeq0EdgeTessFactor:
  case SystemValueName::finalTriWeq0EdgeTessFactor:
  case SystemValueName::finalLineDetailTessFactor:
  case SystemValueName::finalLineDensityTessFactor:
    return DXILSemanticKind::TessFactor;
  case SystemValueName::finalQuadUInsideTessFactor:
  case SystemValueName::finalQuadVInsideTessFactor:
  case SystemValueName::finalTriInsideTessFactor:
    return DXILSemanticKind::InsideTessFactor;
  case SystemValueName::barycentrics:
    return DXILSemanticKind::Barycentrics;
  case SystemValueName::shadingRate:
    return DXILSemanticKind::ShadingRate;
  case SystemValueName::cullPrimitive:
    return DXILSemanticKind::CullPrimitive;
  }
  return DXILSemanticKind::Arbitrary;
}

/// Maps a container-level `llvm::dxbc::D3DSystemValue` (as read from a real
/// `ISGN`/`OSGN`/`ISG1`/`OSG1` signature) onto the DXIL semantic kind that
/// names the same value. Like `toSemanticKind` above, these are two
/// enumerations for the same set of system values with different numbering.
DXILSemanticKind toSemanticKind(llvm::dxbc::D3DSystemValue Value) {
  switch (Value) {
  case llvm::dxbc::D3DSystemValue::Position:
    return DXILSemanticKind::Position;
  case llvm::dxbc::D3DSystemValue::ClipDistance:
    return DXILSemanticKind::ClipDistance;
  case llvm::dxbc::D3DSystemValue::CullDistance:
    return DXILSemanticKind::CullDistance;
  case llvm::dxbc::D3DSystemValue::RenderTargetArrayIndex:
    return DXILSemanticKind::RenderTargetArrayIndex;
  case llvm::dxbc::D3DSystemValue::ViewPortArrayIndex:
    return DXILSemanticKind::ViewPortArrayIndex;
  case llvm::dxbc::D3DSystemValue::VertexID:
    return DXILSemanticKind::VertexID;
  case llvm::dxbc::D3DSystemValue::PrimitiveID:
    return DXILSemanticKind::PrimitiveID;
  case llvm::dxbc::D3DSystemValue::InstanceID:
    return DXILSemanticKind::InstanceID;
  case llvm::dxbc::D3DSystemValue::IsFrontFace:
    return DXILSemanticKind::IsFrontFace;
  case llvm::dxbc::D3DSystemValue::SampleIndex:
    return DXILSemanticKind::SampleIndex;
  case llvm::dxbc::D3DSystemValue::FinalQuadEdgeTessfactor:
  case llvm::dxbc::D3DSystemValue::FinalTriEdgeTessfactor:
  case llvm::dxbc::D3DSystemValue::FinalLineDetailTessfactor:
  case llvm::dxbc::D3DSystemValue::FinalLineDensityTessfactor:
    return DXILSemanticKind::TessFactor;
  case llvm::dxbc::D3DSystemValue::FinalQuadInsideTessfactor:
  case llvm::dxbc::D3DSystemValue::FinalTriInsideTessfactor:
    return DXILSemanticKind::InsideTessFactor;
  case llvm::dxbc::D3DSystemValue::Barycentrics:
    return DXILSemanticKind::Barycentrics;
  case llvm::dxbc::D3DSystemValue::ShadingRate:
    return DXILSemanticKind::ShadingRate;
  case llvm::dxbc::D3DSystemValue::CullPrimitive:
    return DXILSemanticKind::CullPrimitive;
  case llvm::dxbc::D3DSystemValue::Target:
    return DXILSemanticKind::Target;
  case llvm::dxbc::D3DSystemValue::Coverage:
    return DXILSemanticKind::Coverage;
  case llvm::dxbc::D3DSystemValue::InnerCoverage:
    return DXILSemanticKind::InnerCoverage;
  case llvm::dxbc::D3DSystemValue::Depth:
    return DXILSemanticKind::Depth;
  case llvm::dxbc::D3DSystemValue::DepthGE:
    return DXILSemanticKind::DepthGreaterEqual;
  case llvm::dxbc::D3DSystemValue::DepthLE:
    return DXILSemanticKind::DepthLessEqual;
  case llvm::dxbc::D3DSystemValue::StencilRef:
    return DXILSemanticKind::StencilRef;
  case llvm::dxbc::D3DSystemValue::Undefined:
    break;
  }
  return DXILSemanticKind::Arbitrary;
}

/// Returns the DXIL operation that reads \p Kind directly, for the system
/// values DXIL gives an operation of their own rather than reading through
/// `loadInput`.
static std::optional<std::pair<llvm::StringRef, DXILOp>>
systemValueRead(DXILSemanticKind Kind) {
  switch (Kind) {
  case DXILSemanticKind::SampleIndex:
    return std::make_pair("sampleIndex", DXILOp::SampleIndex);
  case DXILSemanticKind::Coverage:
    return std::make_pair("coverage", DXILOp::Coverage);
  default:
    return std::nullopt;
  }
}

/// Returns the DXIL semantic naming \p Type when it is one of the three
/// pixel shader depth outputs, which are registerless operands of their own
/// rather than an `o#` register.
std::optional<DXILSemanticKind> registerlessKind(OperandType Type) {
  switch (Type) {
  case OperandType::oDepth:
    return DXILSemanticKind::Depth;
  case OperandType::oDepthGE:
    return DXILSemanticKind::DepthGreaterEqual;
  case OperandType::oDepthLE:
    return DXILSemanticKind::DepthLessEqual;
  case OperandType::oStencilRef:
    return DXILSemanticKind::StencilRef;
  default:
    return std::nullopt;
  }
}

//===----------------------------------------------------------------------===//
// Signature model
//===----------------------------------------------------------------------===//

/// Maps a container-level `dxbc::SigComponentType` onto `DXILComponentType`.
/// DXIL only ever stores signature elements as 32-bit `dx.op.loadInput`/
/// `storeOutput` calls (min-precision packing is a separate, not-yet-modeled
/// concern -- see "What is left" in `agent_thoughts.md`), so every integer
/// width narrower or wider than 32 bits still maps to `I32`/`U32`.
DXILComponentType toComponentType(llvm::dxbc::SigComponentType Type) {
  switch (Type) {
  case llvm::dxbc::SigComponentType::UInt32:
  case llvm::dxbc::SigComponentType::UInt64:
    return DXILComponentType::U32;
  case llvm::dxbc::SigComponentType::SInt32:
  case llvm::dxbc::SigComponentType::SInt64:
    return DXILComponentType::I32;
  case llvm::dxbc::SigComponentType::UInt16:
    return DXILComponentType::U16;
  case llvm::dxbc::SigComponentType::SInt16:
    return DXILComponentType::I16;
  case llvm::dxbc::SigComponentType::Float16:
    return DXILComponentType::F16;
  case llvm::dxbc::SigComponentType::Unknown:
  case llvm::dxbc::SigComponentType::Float32:
  case llvm::dxbc::SigComponentType::Float64:
    break;
  }
  return DXILComponentType::F32;
}

/// The LLVM type a signature element of \p Type is stored and loaded at.
llvm::Type *componentLLVMType(DXILComponentType Type,
                              llvm::LLVMContext &Context) {
  switch (Type) {
  case DXILComponentType::F16:
    return llvm::Type::getHalfTy(Context);
  case DXILComponentType::I16:
  case DXILComponentType::U16:
    return llvm::Type::getInt16Ty(Context);
  case DXILComponentType::I32:
  case DXILComponentType::U32:
    return llvm::Type::getInt32Ty(Context);
  case DXILComponentType::F32:
  case DXILComponentType::F64:
  case DXILComponentType::SNormF32:
  case DXILComponentType::UNormF32:
    break;
  }
  return llvm::Type::getFloatTy(Context);
}

/// One entry of the input or output signature. DXBC declares a signature
/// register piecewise -- one declaration per contiguous component group of
/// one register -- and each such group is one DXIL signature element.
/// The LLVM type a minimum-precision operand is held at. DXIL narrows
/// `min16f` to `half` and both 16-bit integer forms to `i16`, and records
/// that the shader uses them in the "low-precision data types present"
/// shader flag -- which is distinct from the flag asking for *native*
/// 16-bit types, a shader model 6.2 feature DXBC has no way to request.
llvm::Type *minPrecisionType(OperandMinPrecisionAttr MinPrecision,
                             llvm::LLVMContext &Context) {
  if (!MinPrecision)
    return nullptr;
  switch (MinPrecision.getValue()) {
  case OperandMinPrecision::min16f:
  case OperandMinPrecision::min2_8f:
    return llvm::Type::getHalfTy(Context);
  case OperandMinPrecision::min16i:
  case OperandMinPrecision::min16u:
    return llvm::Type::getInt16Ty(Context);
  }
  return nullptr;
}

/// True when \p MinPrecision names an unsigned integer, which is what
/// decides whether widening it back to 32 bits zero- or sign-extends.
bool isUnsignedPrecision(OperandMinPrecisionAttr MinPrecision) {
  return MinPrecision && MinPrecision.getValue() == OperandMinPrecision::min16u;
}

/// The DXIL signature component type a minimum-precision declaration gives
/// its element.
DXILComponentType toComponentType(OperandMinPrecisionAttr MinPrecision) {
  switch (MinPrecision.getValue()) {
  case OperandMinPrecision::min16f:
  case OperandMinPrecision::min2_8f:
    return DXILComponentType::F16;
  case OperandMinPrecision::min16i:
    return DXILComponentType::I16;
  case OperandMinPrecision::min16u:
    return DXILComponentType::U16;
  }
  return DXILComponentType::F16;
}

struct SignatureElement {
  std::string Name;
  /// The register (signature row) the element lives in.
  unsigned Row = 0;
  /// How many consecutive registers the element occupies.
  unsigned Rows = 1;
  /// The first component of the register the element covers.
  unsigned StartCol = 0;
  /// How many components the element covers.
  unsigned Cols = 0;
  DXILSemanticKind Kind = DXILSemanticKind::Arbitrary;
  /// `DXIL::InterpolationMode`, which matches D3D10_SB_INTERPOLATION_MODE.
  unsigned InterpolationMode = 0;
  /// One semantic index for each row the element occupies.
  llvm::SmallVector<unsigned, 4> SemanticIndices;
  /// `DXIL::ComponentType`. DXBC registers are typeless, so
  /// declaration-synthesized elements always default to `F32` (see
  /// "Building complete legacy DXBC containers for testing" in
  /// feme/docs/Design.md); real container elements carry their actual type.
  DXILComponentType Type = DXILComponentType::F32;
};

/// The input or output signature of a shader, plus the reverse mapping from
/// a (register, component) pair back to the element covering it.
class Signature {
public:
  void add(SignatureElement Element) {
    addToLookup(Element, Elements.size());
    Elements.push_back(std::move(Element));
  }

  /// Appends an element that occupies no register, so that it does not
  /// shadow the (row, component) an `o#` register really lives at.
  void addUnindexed(SignatureElement Element) {
    Elements.push_back(std::move(Element));
  }

  /// Combines the elements occupying \p Count rows of the same index range.
  void collapseRange(unsigned Start, unsigned Count, unsigned StartCol,
                     unsigned Cols) {
    if (Count < 2)
      return;

    llvm::SmallVector<unsigned, 4> Indices;
    std::optional<std::pair<unsigned, unsigned>> Shape;
    for (unsigned Row = Start; Row != Start + Count; ++Row) {
      std::optional<unsigned> Index = find(Row, StartCol);
      if (!Index)
        return;
      const SignatureElement &Element = Elements[*Index];
      if (Element.Row != Row || Element.Rows != 1 ||
          Element.StartCol > StartCol ||
          Element.StartCol + Element.Cols < StartCol + Cols)
        return;
      if (!Indices.empty()) {
        const SignatureElement &First = Elements[Indices.front()];
        if (Element.Kind != First.Kind || Element.Type != First.Type ||
            Element.InterpolationMode != First.InterpolationMode)
          return;
      }
      std::pair<unsigned, unsigned> ElementShape{Element.StartCol,
                                                 Element.Cols};
      if (Shape && *Shape != ElementShape)
        return;
      Shape = ElementShape;
      Indices.push_back(*Index);
    }

    SignatureElement &First = Elements[Indices.front()];
    llvm::SmallVector<unsigned, 4> SemanticIndices;
    for (unsigned Index : Indices)
      SemanticIndices.append(Elements[Index].SemanticIndices);
    First.Rows = Count;
    First.SemanticIndices = std::move(SemanticIndices);

    llvm::SmallDenseSet<unsigned, 4> Removed(Indices.begin() + 1,
                                             Indices.end());
    llvm::SmallVector<SignatureElement, 8> Collapsed;
    for (auto [Index, Element] : llvm::enumerate(Elements))
      if (!Removed.contains(Index))
        Collapsed.push_back(std::move(Element));
    Elements = std::move(Collapsed);
    rebuildLookup();
  }

  /// Returns the index of the element covering \p Row's \p Col component,
  /// or nullopt if no declaration covers it.
  std::optional<unsigned> find(unsigned Row, unsigned Col) const {
    auto It = Lookup.find(key(Row, Col));
    if (It == Lookup.end())
      return std::nullopt;
    return It->second;
  }

  llvm::ArrayRef<SignatureElement> elements() const { return Elements; }
  llvm::MutableArrayRef<SignatureElement> mutableElements() { return Elements; }
  bool empty() const { return Elements.empty(); }

private:
  void addToLookup(const SignatureElement &Element, unsigned Index) {
    for (unsigned Row = Element.Row, RowEnd = Element.Row + Element.Rows;
         Row != RowEnd; ++Row)
      for (unsigned Col = Element.StartCol,
                    ColEnd = Element.StartCol + Element.Cols;
           Col != ColEnd; ++Col)
        Lookup[key(Row, Col)] = Index;
  }

  void rebuildLookup() {
    Lookup.clear();
    for (auto [Index, Element] : llvm::enumerate(Elements))
      addToLookup(Element, Index);
  }

  static uint64_t key(unsigned Row, unsigned Col) {
    return (static_cast<uint64_t>(Row) << 2) | Col;
  }

  llvm::SmallVector<SignatureElement, 8> Elements;
  llvm::DenseMap<uint64_t, unsigned> Lookup;
};

//===----------------------------------------------------------------------===//
// Translator
//===----------------------------------------------------------------------===//

/// How an instruction reads and writes its operands; defined below, with
/// the lowering table it indexes.
struct OpLowering;

class Translator {
public:
  Translator(llvm::LLVMContext &Context, mlir::ModuleOp Source,
             llvm::ArrayRef<ContainerSignatureElement> RealInputSignature,
             llvm::ArrayRef<ContainerSignatureElement> RealOutputSignature)
      : Context(Context), Source(Source),
        Module(std::make_unique<llvm::Module>("dxbc", Context)),
        Builder(Context), AllocaBuilder(Context),
        RealInputSignature(RealInputSignature),
        RealOutputSignature(RealOutputSignature) {}

  std::unique_ptr<llvm::Module> run(dxsa::ModuleOp Shader);

private:
  llvm::LLVMContext &Context;
  mlir::ModuleOp Source;
  std::unique_ptr<llvm::Module> Module;
  llvm::IRBuilder<> Builder;
  /// Insertion point for `Temps`' allocas, pinned to the top of the entry
  /// block so that they precede every use no matter where the instruction
  /// that first needed the slot lives.
  llvm::IRBuilder<> AllocaBuilder;
  llvm::Function *EntryFn = nullptr;
  /// Real signature elements read from a full `DXContainer`, overriding
  /// `collectDeclarations`'s synthesis when non-empty (see
  /// `ContainerSignatureElement`).
  llvm::ArrayRef<ContainerSignatureElement> RealInputSignature;
  llvm::ArrayRef<ContainerSignatureElement> RealOutputSignature;

  Signature Inputs;
  Signature Outputs;
  /// The first register of each declared index range, by its length. A
  /// signature register read at a run-time row names its element by the
  /// range's first register, and the row relative to it.
  struct InputRange {
    unsigned Start;
    unsigned Count;
    unsigned StartCol;
    unsigned Cols;
  };
  llvm::SmallVector<InputRange, 4> InputRanges;
  /// A resource the shader declares, in the terms `dx.op.createHandle` and
  /// `!dx.resources` describe it.
  struct Resource {
    ResourceClass Class = ResourceClass::SRV;
    /// The index of the declaration within its resource class, which is
    /// what a handle is bound by -- not the register it binds to.
    unsigned Range = 0;
    unsigned Bind = 0;
    unsigned Space = 0;
    unsigned Size = 1;
    ResourceKind Kind = ResourceKind::Texture2D;
    /// The component type a typed resource returns, as a DXIL component
    /// type; a structured buffer's element stride otherwise.
    DXILComponentType Component = DXILComponentType::F32;
    unsigned Stride = 0;
    unsigned SampleCount = 0;
    llvm::Value *Handle = nullptr;
    std::string Name;
  };
  /// Every declared resource, keyed by (class, register number).
  llvm::DenseMap<uint64_t, Resource> Resources;
  /// The output signature element each registerless output -- the depth
  /// and stencil ones -- resolves to. They name no register, so they
  /// cannot be found by (row, component) the way the others are.
  llvm::DenseMap<unsigned, unsigned> RegisterlessOutputs;
  /// The dedicated DXIL operation reading each input signature element that
  /// has one, keyed by element index. DXIL names a few system values with
  /// an operation of their own rather than through `loadInput`.
  llvm::DenseMap<unsigned, std::pair<llvm::StringRef, DXILOp>> SystemValueReads;
  /// The `cbufferLoadLegacy` result for each (constant buffer, row, type)
  /// the instruction being translated reads. One legacy load returns a
  /// whole 16-byte row, so an instruction naming several components of one
  /// row loads it once.
  llvm::DenseMap<std::tuple<const void *, llvm::Value *, llvm::Type *>,
                 llvm::Value *>
      RowCache;
  /// The handle each dynamically bound resource operand of the instruction
  /// being translated resolved to.
  llvm::DenseMap<const void *, llvm::Value *> HandleCache;
  /// The stack slot backing each temp register component, keyed by
  /// `register * 4 + component`, created on first use and promoted to SSA
  /// once the whole program has been translated. DXBC temps are mutable
  /// 32-bit locations whose live ranges cross the blocks structured control
  /// flow introduces, which is exactly what an `alloca` plus mem2reg
  /// models; the promoted phi nodes inherit the slot's name.
  llvm::DenseMap<uint64_t, llvm::AllocaInst *> Temps;
  /// The stack array backing each declared indexable temp register. DXBC
  /// gives `x#` no type either, so one array per element type is created
  /// and the ones nothing selected are dropped again; that also reproduces
  /// the names dxilconv's arrays end up with, which LLVM's own value-name
  /// uniquing assigns.
  struct IndexableTemp {
    llvm::DenseMap<llvm::Type *, llvm::AllocaInst *> Arrays;
    unsigned Components = 0;
    unsigned Elements = 0;
  };
  llvm::DenseMap<unsigned, IndexableTemp> IndexableTemps;
  /// The type inferred for each temp register component; see
  /// `inferTempTypes`.
  llvm::DenseMap<uint64_t, llvm::Type *> TempTypes;
  /// Values already read for the instruction being translated, keyed by the
  /// source operand, the component it reads and the type it is read at. A
  /// DXBC instruction may name the same source component several times
  /// through its swizzle, and each such mention is one value, not several.
  llvm::DenseMap<std::tuple<const void *, unsigned, unsigned, llvm::Type *>,
                 llvm::Value *>
      SourceCache;
  /// The clamped form of each value the instruction being translated
  /// saturates, so that a component named twice by a swizzle is clamped
  /// once.
  llvm::DenseMap<llvm::Value *, llvm::Value *> SaturateCache;
  /// Shader stage name for `!dx.shaderModel` ("ps", "vs", ...).
  llvm::StringRef Stage = "ps";
  /// `!dx.entryPoints`' shader flags mask. `AllResourcesBound` is set for
  /// every shader: shader model 5.x binds its resources for the whole
  /// draw, which is exactly what the flag asserts.
  uint64_t ShaderFlags = 0x100;
  /// The shader's one `cycleCounterLegacy` call, if it reads the counter.
  llvm::Value *CycleCounter = nullptr;

  /// One entry per open `if`, `loop` or `switch` construct. DXBC control
  /// flow is structured and properly nested, so a stack is all the state
  /// the branch targets need.
  struct Scope {
    enum class Kind { If, Loop, Switch };
    Kind K;
    unsigned Id = 0;
    /// The construct's exit, and the target of `break` in a loop or switch.
    llvm::BasicBlock *EndBB = nullptr;
    /// `if`: the false arm, which becomes `EndBB` when there is no `else`.
    llvm::BasicBlock *FalseBB = nullptr;
    bool SawElse = false;
    /// `loop`: the block a new iteration starts at, and `continue`'s target.
    llvm::BasicBlock *HeaderBB = nullptr;
    /// `switch`: the dispatch, and its as-yet-unattached default arm.
    llvm::SwitchInst *Dispatch = nullptr;
    llvm::BasicBlock *DefaultBB = nullptr;
    /// `switch`: the case values naming the group about to be opened.
    llvm::SmallVector<llvm::ConstantInt *, 4> PendingCases;
    unsigned CaseGroups = 0;
    /// `loop`/`switch`: how many `break`s have been seen, which is what
    /// numbers the fall-through block a conditional `break` needs.
    unsigned Breaks = 0;
    unsigned Continues = 0;
  };
  llvm::SmallVector<Scope, 4> Scopes;
  unsigned IfCount = 0;
  unsigned LoopCount = 0;
  unsigned SwitchCount = 0;
  unsigned RetcCount = 0;
  /// Blocks created but not yet inserted into the function; a block is
  /// inserted only when translation reaches it, which is what puts them in
  /// the order dxilconv emits.
  llvm::SmallVector<llvm::BasicBlock *, 8> Pending;

  /// Returns (creating on first use) the named struct type DXIL gives an
  /// opaque resource handle.
  llvm::StructType *handleTy();
  /// Returns (creating on first use) the named struct type a legacy
  /// constant buffer load returns: one 16-byte row, as four components of
  /// \p Element.
  llvm::StructType *cbufferRetTy(llvm::Type *Element);
  /// Returns (creating on first use) the named struct type holding the two
  /// 32-bit halves of a 64-bit cycle counter.
  llvm::StructType *twoI32Ty();

  llvm::Type *floatTy() { return llvm::Type::getFloatTy(Context); }
  llvm::Type *halfTy() { return llvm::Type::getHalfTy(Context); }
  llvm::Type *i16Ty() { return llvm::Type::getInt16Ty(Context); }
  llvm::Type *i32Ty() { return llvm::Type::getInt32Ty(Context); }
  llvm::Type *i8Ty() { return llvm::Type::getInt8Ty(Context); }
  llvm::Type *i1Ty() { return llvm::Type::getInt1Ty(Context); }

  mlir::InFlightDiagnostic unsupported(mlir::Operation *Op) {
    return Op->emitError("dxsa -> DXIL translation does not support '")
           << Op->getName().getStringRef() << "' yet";
  }

  //===--------------------------------------------------------------------===//
  // Declarations
  //===--------------------------------------------------------------------===//

  bool collectDeclarations(dxsa::ModuleOp Shader);
  /// Fills in the parts of a signature element that live in the shader's
  /// declarations rather than in a legacy signature part.
  void refineFromDeclarations(dxsa::ModuleOp Shader);
  /// Chooses a concrete LLVM type for every temp register component, which
  /// fixes the type of its stack slot and so of the phi nodes mem2reg
  /// creates for it. DXBC temps are typeless 32-bit slots, so the type is
  /// inferred from the instructions around them: one with definite
  /// floating-point or integer semantics votes for its own type, a `mov`
  /// to or from a signature register votes for that element's type, and a
  /// component with no vote either way stays `i32`, the width DXBC itself
  /// defines.
  void inferTempTypes(dxsa::ModuleOp Shader);
  void addSignatureElement(Signature &Sig, DstOperandAttr Operand,
                           llvm::StringRef NamePrefix, DXILSemanticKind Kind,
                           unsigned InterpolationMode);
  /// Populates \p Sig directly from real container signature elements,
  /// bypassing declaration-based synthesis (see `ContainerSignatureElement`).
  static void addRealSignatureElements(
      Signature &Sig, llvm::ArrayRef<ContainerSignatureElement> Elements,
      llvm::DenseMap<unsigned, unsigned> *Registerless = nullptr);

  //===--------------------------------------------------------------------===//
  // Operands
  //===--------------------------------------------------------------------===//

  /// Returns the component index \p Src reads when producing destination
  /// component \p DstComp.
  static unsigned sourceComponent(SrcOperandAttr Src, unsigned DstComp);
  /// Returns the first immediate index of \p Operand, i.e. the register
  /// number, or nullopt if it is not a plain immediate.
  static std::optional<unsigned> registerNumber(OperandIndexAttr Index);

  /// Records every resource the shader declares.
  void collectResources(dxsa::ModuleOp Shader);
  /// Emits the `dx.op.createHandle` call for every declared resource. DXIL
  /// binds a resource once, at the top of the entry point, and refers to
  /// the resulting handle from every access.
  void createResourceHandles(dxsa::ModuleOp Shader);
  /// The key \c Resources gives a resource of \p Class bound at \p Id.
  static uint64_t resourceKey(ResourceClass Class, unsigned Id) {
    return (uint64_t(Class) << 32) | Id;
  }
  /// Returns the resource an operand names, or null (having emitted a
  /// diagnostic) when the shader never declared it.
  const Resource *findResource(ResourceClass Class, OperandIndexAttr Index,
                               mlir::Operation *Op);
  /// The handle an operand resolves to: the one bound at the entry point,
  /// or -- when shader model 5.1's declared range is indexed at run time --
  /// one bound at the access.
  /// \p Trailing is how many index slots follow the register selector --
  /// one for a constant buffer, which also indexes the row, none for the
  /// resources an operand names bare.
  llvm::Value *resourceHandle(ResourceClass Class, OperandIndexAttr Index,
                              mlir::Operation *Op, unsigned Slot,
                              unsigned Trailing = 0, bool NonUniform = false);
  /// The `!dx.resources` entry for every resource of \p Class, or null when
  /// the shader declares none.
  llvm::MDNode *emitResourceBindings(ResourceClass Class);
  /// Evaluates one index slot of an operand: an immediate, a register read
  /// at run time, or their sum.
  llvm::Value *readIndex(IndexAttr Index, unsigned Slot, mlir::Operation *Op);
  /// Reads component \p Comp of the constant buffer row \p Src names.
  llvm::Value *readConstantBuffer(SrcOperandAttr Src, unsigned Comp,
                                  llvm::Type *Ty, mlir::Operation *Op,
                                  unsigned Slot);

  /// Returns (creating on first use) the stack slot backing temp register
  /// component \p Comp of register \p Reg, in the 32-bit bank or the
  /// 16-bit one. DXBC's minimum-precision registers are a bank of their
  /// own: `r0.y` read at `min16f` is not the `r0.y` a 32-bit instruction
  /// wrote, which is why dxilconv names them `dx.v16.r*` and `dx.v32.r*`.
  llvm::AllocaInst *tempSlot(unsigned Reg, unsigned Comp, bool Narrow = false);
  /// The key \c Temps and \c TempTypes give a temp register component.
  static uint64_t tempKey(unsigned Reg, unsigned Comp, bool Narrow) {
    return (uint64_t(Narrow) << 32) | (uint64_t(Reg) * 4 + Comp);
  }

  /// Allocates the stack array backing every declared indexable temp.
  void createIndexableTemps(dxsa::ModuleOp Shader);
  /// Returns the address of component \p Comp of the element \p Index
  /// selects from indexable temp register \p Reg, at element type \p Ty.
  llvm::Value *indexableTempAddress(OperandIndexAttr Index, unsigned Comp,
                                    llvm::Type *Ty, mlir::Operation *Op,
                                    unsigned Slot);

  llvm::Value *readSource(SrcOperandAttr Src, unsigned DstComp, llvm::Type *Ty,
                          mlir::Operation *Op, unsigned Slot = 0);
  llvm::Value *coerce(llvm::Value *Value, llvm::Type *Ty);
  /// Converts \p Value between a minimum-precision type and its 32-bit
  /// counterpart. \p Unsigned picks between zero- and sign-extension when
  /// widening an integer; narrowing needs no such choice.
  llvm::Value *convertPrecision(llvm::Value *Value, llvm::Type *Ty,
                                bool Unsigned);
  /// The alignment an element of a stack array is accessed at.
  static llvm::Align elementAlign(llvm::Type *Ty) {
    return llvm::Align(Ty->getPrimitiveSizeInBits() == 16 ? 2 : 4);
  }
  /// The 32-bit counterpart of \p Ty, which is \p Ty itself unless it is
  /// one of the minimum-precision types.
  llvm::Type *widen(llvm::Type *Ty) {
    if (Ty->isHalfTy())
      return floatTy();
    return Ty->isIntegerTy(16) ? i32Ty() : Ty;
  }
  /// The type an operand's storage holds, which is its minimum-precision
  /// type when it has one and \p Default otherwise.
  llvm::Type *storageType(OperandMinPrecisionAttr MinPrecision,
                          llvm::Type *Default) {
    llvm::Type *Narrow = minPrecisionType(MinPrecision, Context);
    return Narrow ? Narrow : Default;
  }
  /// Writes \p Components to \p Dst, converting each to the destination's
  /// own precision. \p ValuePrecision names the minimum precision the
  /// values are already at, when they are at one, so that widening an
  /// integer knows whether to zero- or sign-extend.
  bool writeDestination(DstOperandAttr Dst,
                        llvm::ArrayRef<llvm::Value *> Components,
                        mlir::Operation *Op,
                        OperandMinPrecisionAttr ValuePrecision = {});
  /// The destination components an instruction computes, in order.
  static llvm::SmallVector<unsigned, 4>
  destinationComponents(DstOperandAttr Dst);

  //===--------------------------------------------------------------------===//
  // Instructions
  //===--------------------------------------------------------------------===//

  bool translateBody(dxsa::ModuleOp Shader);
  bool translateInstruction(mlir::Operation *Op);
  /// Translates the control-flow instructions, which are the ones that
  /// need the block structure rather than a value computation. Sets
  /// \p Handled when \p Op was one of them.
  bool translateControlFlow(mlir::Operation *Op, bool &Handled);
  bool translateMovC(mlir::Operation *Op, DstOperandAttr Dst,
                     SrcOperandAttr Cond, SrcOperandAttr True,
                     SrcOperandAttr False, bool Saturate);
  /// How a member of the sampling family is shaped: which DXIL operation
  /// it is, and what it appends to the shared
  /// (resource, sampler, coordinates, offsets) prefix.
  struct SampleForm {
    DXILOp Op = DXILOp::Sample;
    llvm::StringRef Name;
    /// Source operands appended after the offsets, in order.
    llvm::SmallVector<SrcOperandAttr, 4> Extra;
    /// A trailing LOD clamp, which defaults to zero when the instruction
    /// has no `_cl` form.
    bool Clamp = false;
    /// `gather4`'s channel, which its sampler operand's swizzle names.
    bool Channel = false;
    /// At most two offsets, which is all a gather takes.
    bool NarrowOffsets = false;
    /// `sample_d`'s gradients: the x and y derivative operands, each
    /// appended as three spatial components.
    llvm::SmallVector<SrcOperandAttr, 2> Gradients;
    /// `gather4_po`'s offsets, which come from a source operand rather
    /// than from the instruction's immediate suffix.
    SrcOperandAttr OffsetSource;
    /// The instruction's own LOD clamp, which a `_cl` form carries as an
    /// operand and every other form leaves null.
    SrcOperandAttr ClampValue;
    /// Where a `_s` form writes the Tiled Resources feedback status the
    /// resource return's fifth field carries. Null on the plain forms,
    /// and `null` on a `_s` form whose shader discards the status.
    DstOperandAttr Feedback;
  };

  /// Returns (creating on first use) the named struct type a resource load
  /// returns: four components of \p Element plus a mapping status.
  llvm::StructType *resRetTy(llvm::Type *Element);
  /// Translates one member of the sampling family.
  bool translateSample(mlir::Operation *Op, DstOperandAttr Dst,
                       SrcOperandAttr Address, SrcOperandAttr SRV,
                       SrcOperandAttr Sampler, SampleOffsetAttr Offset,
                       const SampleForm &Form);
  /// Reads the \p Count coordinates \p Address holds, padded to four with
  /// `undef` -- which is the shape every sampling operation takes.
  bool readCoordinates(SrcOperandAttr Address, unsigned Count, unsigned Total,
                       llvm::SmallVectorImpl<llvm::Value *> &Args,
                       mlir::Operation *Op);
  /// Writes the components \p Dst enables from the resource-load result
  /// \p Value, picking each through \p SRV's swizzle.
  bool writeResourceResult(DstOperandAttr Dst, SrcOperandAttr SRV,
                           llvm::Value *Value, mlir::Operation *Op);
  /// Writes the Tiled Resources feedback status \p Result's fifth field
  /// carries to \p Feedback, which a `_s` instruction whose shader
  /// discards the status leaves `null`.
  bool writeFeedbackStatus(DstOperandAttr Feedback, llvm::Value *Result,
                           mlir::Operation *Op);
  /// The class the resource operand \p View names: a raw or structured
  /// access reaches both shader resource views and unordered access views,
  /// and only the register the operand names says which.
  static ResourceClass viewClass(SrcOperandAttr View) {
    return View.getType() == OperandType::u ? ResourceClass::UAV
                                            : ResourceClass::SRV;
  }
  /// The LLVM type a resource load's result components have, which is the
  /// declared component type narrowed to the three DXIL overloads a
  /// resource return comes in.
  llvm::Type *resourceElementType(const Resource &R) {
    return componentLLVMType(R.Component == DXILComponentType::I32 ||
                                     R.Component == DXILComponentType::U32
                                 ? R.Component
                                 : DXILComponentType::F32,
                             Context);
  }
  /// Translates `ld`, `ldms` and `ld_uav_typed`, which read a resource at
  /// an integer address and take no sampler. \p SampleIndex is `ldms`'s
  /// sample index, which occupies the slot a mip level takes otherwise; an
  /// unordered access view has neither, and passes `undef` for both that
  /// slot and the texel offsets.
  bool translateResourceLoad(mlir::Operation *Op, ResourceClass Class,
                             DstOperandAttr Dst, SrcOperandAttr Address,
                             SrcOperandAttr View, SampleOffsetAttr Offset,
                             SrcOperandAttr SampleIndex,
                             DstOperandAttr Feedback);
  /// Translates the `store_uav_typed`, `store_raw` and `store_structured`
  /// family. \p ByteOffset is the offset within a structured buffer's
  /// element, which the other two leave null.
  bool translateResourceStore(mlir::Operation *Op, DstOperandAttr UAV,
                              SrcOperandAttr Address, SrcOperandAttr ByteOffset,
                              SrcOperandAttr Value);

  /// Translates `sincos`, whose two destinations are written from one
  /// source and either of which may be `null`.
  bool translateSincos(mlir::Operation *Op, DstOperandAttr Sin,
                       DstOperandAttr Cos, SrcOperandAttr Src, bool Saturate);

  //===--------------------------------------------------------------------===//
  // Blocks
  //===--------------------------------------------------------------------===//

  /// Creates a block that is inserted into the function only once
  /// translation reaches it (see `Pending`).
  llvm::BasicBlock *deferredBlock(const llvm::Twine &Name);
  /// Appends \p BB to the function and continues emitting into it.
  void startBlock(llvm::BasicBlock *BB);
  /// Terminates the current block with a branch to \p BB, unless it is
  /// already terminated -- which it is when a `break`, `continue` or `ret`
  /// left the rest of the construct unreachable.
  void branchTo(llvm::BasicBlock *BB);
  /// Reads \p Cond as the i1 an LLVM branch, `select` or `dx.op.discard`
  /// wants. \p TestNonZero picks between the `_nz` and `_z` spellings.
  llvm::Value *readCondition(SrcOperandAttr Cond, bool TestNonZero,
                             mlir::Operation *Op);
  /// Returns the innermost scope a `break` applies to, or null (having
  /// emitted a diagnostic) if there is none.
  Scope *breakScope(mlir::Operation *Op);
  /// Returns the innermost enclosing loop, or null (having emitted a
  /// diagnostic) if there is none.
  Scope *loopScope(mlir::Operation *Op);
  /// Opens the `switch` case group named by the case values accumulated so
  /// far, if any are pending.
  void openPendingCaseGroup();
  bool translateUnary(mlir::Operation *Op, DstOperandAttr Dst,
                      SrcOperandAttr Src, bool Saturate);
  bool translateBinary(mlir::Operation *Op, DstOperandAttr Dst,
                       SrcOperandAttr Lhs, SrcOperandAttr Rhs, bool Saturate);
  bool translateMad(mlir::Operation *Op, DstOperandAttr Dst, SrcOperandAttr Lhs,
                    SrcOperandAttr Rhs, SrcOperandAttr Acc, bool Saturate);
  /// Translates an instruction that lowers to one `dx.op` call per
  /// destination component, taking one argument from each source.
  bool translateVariadic(mlir::Operation *Op, DstOperandAttr Dst,
                         llvm::ArrayRef<SrcOperandAttr> Sources, bool Saturate);
  bool translateDot(mlir::Operation *Op, DstOperandAttr Dst, SrcOperandAttr Lhs,
                    SrcOperandAttr Rhs, unsigned Lanes, bool Saturate);
  llvm::Value *saturate(llvm::Value *Value, bool Enabled);
  /// The types an instruction reads its operands and computes its result
  /// at, given how it lowers. A minimum-precision destination narrows both
  /// -- unless DXIL only defines the operation at 32 bits, in which case
  /// the result is narrowed on its way to the destination instead. A
  /// comparison writes a 32-bit mask whatever it compares, so it takes its
  /// width from its operands agreeing on one.
  std::pair<llvm::Type *, llvm::Type *>
  operationTypes(const OpLowering &Lowering, DstOperandAttr Dst,
                 llvm::ArrayRef<SrcOperandAttr> Sources);

  /// Folds `ftoi`/`ftou` of a literal. DXBC clamps an out-of-range or NaN
  /// conversion where LLVM's `fptosi`/`fptoui` leave it poison, so a
  /// literal has to be converted here rather than left to LLVM's folder.
  /// Returns null when \p Source is not a floating-point literal.
  llvm::Constant *foldFloatToInt(llvm::Instruction::CastOps Cast,
                                 llvm::Value *Source);

  /// Returns \p Op's mnemonic without the `dxsa.` dialect prefix.
  static llvm::StringRef mnemonicOf(mlir::Operation *Op) {
    return Op->getName().stripDialect();
  }

  /// The element type a `mov` copies at. DXBC registers are typeless, so
  /// this is whatever the source component already holds, or -- for a
  /// literal, a signature read or a constant buffer read, none of which
  /// have a type of their own -- whatever the destination holds.
  llvm::Type *movElementType(DstOperandAttr Dst, SrcOperandAttr Src,
                             unsigned DstComp);

  /// The `!dx.shaderModel` stage name for \p Type.
  static llvm::StringRef stageName(ProgramType Type);

  //===--------------------------------------------------------------------===//
  // dx.op call helpers
  //===--------------------------------------------------------------------===//

  /// Returns (creating on first use) the `dx.op.<Name>.<overload>` function
  /// with the given signature. DXIL names an operation's overload by its
  /// return type, or by its first non-opcode argument for void operations.
  llvm::Function *dxOp(llvm::StringRef Name, llvm::Type *ReturnTy,
                       llvm::ArrayRef<llvm::Type *> Args,
                       llvm::Type *OverloadTy);
  /// Emits a `dx.op.<Name>` call. \p OverloadTy names the operation's
  /// overload when it cannot be read off the call itself -- an operation
  /// returning a struct is overloaded on the struct's element type -- and
  /// `NoOverload` marks the operations that are not overloaded at all.
  /// Whether the instruction being translated carries DXBC's `precise`
  /// modifier, which forbids reassociating its arithmetic. It is cleared
  /// while an operand is read, because a source read is a computation of
  /// its own that the modifier does not reach.
  bool Precise = false;

  llvm::Value *emitDXOp(llvm::StringRef Name, DXILOp Op, llvm::Type *ReturnTy,
                        llvm::ArrayRef<llvm::Value *> Args,
                        llvm::Type *OverloadTy = nullptr);
  static llvm::Type *noOverload() {
    return reinterpret_cast<llvm::Type *>(std::uintptr_t(-1));
  }

  //===--------------------------------------------------------------------===//
  // Metadata
  //===--------------------------------------------------------------------===//

  /// Rewrites the temp register stack slots into SSA values. The promoted
  /// values keep the slot's name, so a temp live across a branch surfaces
  /// as a `dx.v32.r<n>.<m>` phi node.
  void promoteTemps(llvm::Function &Entry);
  /// Recovers the `i1` a comparison produced from the 32-bit boolean mask
  /// DXBC stores it as. A DXBC condition is a full-width all-ones/all-zeroes
  /// mask, so a test of one against zero is exactly the comparison that
  /// produced it -- but only once the temp register carrying it has been
  /// promoted, which is why this runs after `promoteTemps`.
  void foldConditionMasks(llvm::Function &Entry);

  void emitMetadata(llvm::Function *Entry);
  llvm::MDNode *emitSignature(const Signature &Sig);
};

//===----------------------------------------------------------------------===//
// Declarations
//===----------------------------------------------------------------------===//

std::optional<unsigned> Translator::registerNumber(OperandIndexAttr Index) {
  if (!Index || Index.empty())
    return std::nullopt;
  IndexAttr First = Index[0];
  if (!First.getImm() || First.getRelative())
    return std::nullopt;
  return static_cast<unsigned>(First.getImm().getInt());
}

void Translator::addSignatureElement(Signature &Sig, DstOperandAttr Operand,
                                     llvm::StringRef NamePrefix,
                                     DXILSemanticKind Kind,
                                     unsigned InterpolationMode) {
  std::optional<unsigned> Row = registerNumber(Operand.getIndex());
  if (!Row)
    return;

  unsigned Mask = Operand.getMask()
                      ? static_cast<unsigned>(Operand.getMask().getValue())
                      : 0xF;
  SignatureElement Element;
  Element.Row = *Row;
  Element.StartCol = llvm::countr_zero(Mask);
  Element.Cols = llvm::popcount(Mask);
  Element.Kind = Kind;
  Element.InterpolationMode = InterpolationMode;
  Element.SemanticIndices.push_back(0);
  Element.Name = Kind == DXILSemanticKind::Arbitrary
                     ? (NamePrefix + llvm::Twine(Sig.elements().size())).str()
                     : semanticName(Kind);
  Sig.add(std::move(Element));
}

/// The `!dx.entryPoints` shader flag each DXBC global flag implies. DXBC's
/// `enableMinimumPrecision` asks for minimum precision, which DXIL records
/// as "low-precision data types present" -- a different flag from the one
/// asking for *native* 16-bit types, which is a shader model 6.2 feature
/// DXBC cannot request.
uint64_t toShaderFlags(GlobalFlags Flags) {
  uint64_t Result = 0;
  auto has = [&](GlobalFlags Flag) {
    return (static_cast<unsigned>(Flags) & static_cast<unsigned>(Flag)) != 0;
  };
  if (has(GlobalFlags::skipOptimization))
    Result |= 0x1; // DisableOptimizations
  if (has(GlobalFlags::enableDoublePrecisionFloatOps))
    Result |= 0x4; // UseDoubles
  if (has(GlobalFlags::forceEarlyDepthStencil))
    Result |= 0x8; // ForceEarlyDepthStencil
  if (has(GlobalFlags::enableRawAndStructuredBuffers))
    Result |= 0x10; // UseRawAndStructuredBuffers
  if (has(GlobalFlags::enableMinimumPrecision))
    Result |= 0x20; // LowPrecisionPresent
  if (has(GlobalFlags::enableDoubleExtensions))
    Result |= 0x40; // UseDoubleExtensions
  if (has(GlobalFlags::enableShaderExtensions))
    Result |= 0x80; // UseMSAD
  return Result;
}

bool Translator::collectDeclarations(dxsa::ModuleOp Shader) {
  for (mlir::Operation &Op : *Shader.getBodyBlock()) {
    if (auto Dcl = llvm::dyn_cast<dxsa::DclGlobalFlags>(&Op))
      ShaderFlags |= toShaderFlags(Dcl.getFlags());
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclIndexRange>(&Op)) {
      DstOperandAttr Operand = Dcl.getOperandAttr();
      std::optional<unsigned> Start = registerNumber(Operand.getIndex());
      if (!Start || Operand.getType() != OperandType::v)
        continue;
      unsigned Mask = Operand.getMask()
                          ? static_cast<unsigned>(Operand.getMask().getValue())
                          : 0xF;
      InputRanges.push_back({*Start, Dcl.getCount(),
                             static_cast<unsigned>(llvm::countr_zero(Mask)),
                             static_cast<unsigned>(llvm::popcount(Mask))});
    }
  }

  if (!RealInputSignature.empty()) {
    addRealSignatureElements(Inputs, RealInputSignature);
  } else {
    for (mlir::Operation &Op : *Shader.getBodyBlock()) {
      if (auto Dcl = llvm::dyn_cast<dxsa::DclInputPs>(&Op))
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN",
                            DXILSemanticKind::Arbitrary,
                            static_cast<unsigned>(Dcl.getMode()));
      else if (auto Dcl = llvm::dyn_cast<dxsa::DclInput>(&Op))
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN",
                            DXILSemanticKind::Arbitrary, 0);
      else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputPsSiv>(&Op))
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN",
                            toSemanticKind(Dcl.getName()),
                            static_cast<unsigned>(Dcl.getMode()));
      else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputPsSgv>(&Op)) {
        DXILSemanticKind Kind = toSemanticKind(Dcl.getName());
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN", Kind, 0);
      } else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputSiv>(&Op))
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN",
                            toSemanticKind(Dcl.getName()), 0);
      else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputSgv>(&Op))
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN",
                            toSemanticKind(Dcl.getName()), 0);
    }
  }
  for (const InputRange &Range : InputRanges)
    Inputs.collapseRange(Range.Start, Range.Count, Range.StartCol, Range.Cols);

  if (!RealOutputSignature.empty()) {
    addRealSignatureElements(Outputs, RealOutputSignature,
                             &RegisterlessOutputs);
    refineFromDeclarations(Shader);
    return true;
  }

  bool IsPixelShader = !Shader.getProgramType() ||
                       Shader.getProgramType() == ProgramType::pixel_shader;
  for (mlir::Operation &Op : *Shader.getBodyBlock()) {
    if (auto Dcl = llvm::dyn_cast<dxsa::DclOutput>(&Op)) {
      DstOperandAttr Operand = Dcl.getOperandAttr();
      // A depth output names no register, so it is recorded by operand kind
      // rather than by the (row, component) the others are found through.
      if (std::optional<DXILSemanticKind> Kind =
              registerlessKind(Operand.getType())) {
        RegisterlessOutputs[unsigned(*Kind)] = Outputs.elements().size();
        SignatureElement Element;
        Element.Row = ~0u;
        Element.StartCol = ~0u;
        Element.Cols = 1;
        Element.Kind = *Kind;
        Element.Name = semanticName(*Kind);
        Element.SemanticIndices.push_back(0);
        if (Operand.getMinPrecision())
          Element.Type = toComponentType(Operand.getMinPrecision());
        Outputs.addUnindexed(std::move(Element));
        continue;
      }
      addSignatureElement(Outputs, Operand, "OUT",
                          IsPixelShader ? DXILSemanticKind::Target
                                        : DXILSemanticKind::Arbitrary,
                          0);
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclOutputSiv>(&Op))
      addSignatureElement(Outputs, Dcl.getOperandAttr(), "OUT",
                          toSemanticKind(Dcl.getName()), 0);
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclOutputSgv>(&Op))
      addSignatureElement(Outputs, Dcl.getOperandAttr(), "OUT",
                          toSemanticKind(Dcl.getName()), 0);
  }
  refineFromDeclarations(Shader);
  return true;
}

void Translator::refineFromDeclarations(dxsa::ModuleOp Shader) {
  // Two things a signature element carries live in the declaration rather
  // than in the legacy signature part: minimum precision, which the
  // pre-DXIL layout predates and records as a 32-bit type, and a pixel
  // shader input's interpolation mode, which it has no field for at all.
  // A system value DXIL reads through a dedicated operation is named by
  // its declaration, not by the signature part.
  auto systemValue = [&](DstOperandAttr Operand, SystemValueName Name) {
    auto Read = systemValueRead(toSemanticKind(Name));
    std::optional<unsigned> Row = registerNumber(Operand.getIndex());
    if (!Read || !Row)
      return;
    unsigned Mask = Operand.getMask()
                        ? static_cast<unsigned>(Operand.getMask().getValue())
                        : 0xF;
    for (unsigned Comp = 0; Comp < 4; ++Comp)
      if (Mask & (1u << Comp))
        if (std::optional<unsigned> Element = Inputs.find(*Row, Comp))
          SystemValueReads[*Element] = *Read;
  };

  auto refine = [&](Signature &Sig,
                    llvm::ArrayRef<ContainerSignatureElement> Real,
                    DstOperandAttr Operand,
                    std::optional<unsigned> Interpolation) {
    if (!Operand || (!Operand.getMinPrecision() && !Interpolation))
      return;
    // A registerless output is found by its semantic rather than by the
    // register it does not have.
    if (std::optional<DXILSemanticKind> Kind =
            registerlessKind(Operand.getType())) {
      auto Found = RegisterlessOutputs.find(unsigned(*Kind));
      if (Found != RegisterlessOutputs.end() && Operand.getMinPrecision() &&
          Real.empty())
        Sig.mutableElements()[Found->second].Type =
            toComponentType(Operand.getMinPrecision());
      return;
    }
    std::optional<unsigned> Row = registerNumber(Operand.getIndex());
    if (!Row)
      return;
    unsigned Mask = Operand.getMask()
                        ? static_cast<unsigned>(Operand.getMask().getValue())
                        : 0xF;
    for (unsigned Comp = 0; Comp < 4; ++Comp) {
      if (!(Mask & (1u << Comp)))
        continue;
      std::optional<unsigned> Index = Sig.find(*Row, Comp);
      if (!Index)
        continue;
      SignatureElement &Element = Sig.mutableElements()[*Index];
      if (Operand.getMinPrecision() && Real.empty())
        Element.Type = toComponentType(Operand.getMinPrecision());
      if (Interpolation)
        Element.InterpolationMode = *Interpolation;
    }
  };
  for (mlir::Operation &Op : *Shader.getBodyBlock()) {
    if (auto Dcl = llvm::dyn_cast<dxsa::DclInputPs>(&Op))
      refine(Inputs, RealInputSignature, Dcl.getOperandAttr(),
             static_cast<unsigned>(Dcl.getMode()));
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputPsSiv>(&Op))
      refine(Inputs, RealInputSignature, Dcl.getOperandAttr(),
             static_cast<unsigned>(Dcl.getMode()));
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclInput>(&Op))
      refine(Inputs, RealInputSignature, Dcl.getOperandAttr(), std::nullopt);
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputPsSgv>(&Op)) {
      refine(Inputs, RealInputSignature, Dcl.getOperandAttr(), std::nullopt);
      systemValue(Dcl.getOperandAttr(), Dcl.getName());
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputSiv>(&Op))
      refine(Inputs, RealInputSignature, Dcl.getOperandAttr(), std::nullopt);
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputSgv>(&Op)) {
      refine(Inputs, RealInputSignature, Dcl.getOperandAttr(), std::nullopt);
      systemValue(Dcl.getOperandAttr(), Dcl.getName());
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclOutput>(&Op))
      refine(Outputs, RealOutputSignature, Dcl.getOperandAttr(), std::nullopt);
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclOutputSiv>(&Op))
      refine(Outputs, RealOutputSignature, Dcl.getOperandAttr(), std::nullopt);
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclOutputSgv>(&Op))
      refine(Outputs, RealOutputSignature, Dcl.getOperandAttr(), std::nullopt);
  }
}

void Translator::addRealSignatureElements(
    Signature &Sig, llvm::ArrayRef<ContainerSignatureElement> Elements,
    llvm::DenseMap<unsigned, unsigned> *Registerless) {
  for (const ContainerSignatureElement &El : Elements) {
    SignatureElement Element;
    Element.Row = El.Register;
    Element.StartCol = llvm::countr_zero(El.Mask);
    Element.Cols = llvm::popcount(El.Mask);
    Element.Kind =
        toSemanticKind(static_cast<llvm::dxbc::D3DSystemValue>(El.SystemValue));
    Element.Name = Element.Kind == DXILSemanticKind::Arbitrary
                       ? El.Name
                       : semanticName(Element.Kind);
    Element.SemanticIndices.push_back(El.Index);
    Element.Type =
        toComponentType(static_cast<llvm::dxbc::SigComponentType>(El.CompType));
    // `fxc` gives an element that names no register the all-ones register
    // number, and DXIL keeps that as the -1 its metadata carries.
    if (El.Register == ~0u) {
      if (Registerless)
        (*Registerless)[unsigned(Element.Kind)] = Sig.elements().size();
      Element.Row = ~0u;
      Element.StartCol = ~0u;
      Element.Cols = 1;
      Sig.addUnindexed(std::move(Element));
      continue;
    }
    Sig.add(std::move(Element));
  }
}

//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

unsigned Translator::sourceComponent(SrcOperandAttr Src, unsigned DstComp) {
  SwizzleAttr Swizzle = Src.getSwizzle();
  if (!Swizzle)
    return DstComp;
  llvm::ArrayRef<unsigned> Components = Swizzle.getComponents();
  if (Components.size() == 1)
    return Components[0];
  return Components[DstComp];
}

llvm::SmallVector<unsigned, 4>
Translator::destinationComponents(DstOperandAttr Dst) {
  llvm::SmallVector<unsigned, 4> Result;
  // A registerless output is a single scalar with no write mask.
  if (registerlessKind(Dst.getType()))
    return {0};
  unsigned Mask =
      Dst.getMask() ? static_cast<unsigned>(Dst.getMask().getValue()) : 0x1;
  for (unsigned I = 0; I < 4; ++I)
    if (Mask & (1u << I))
      Result.push_back(I);
  return Result;
}

llvm::Value *Translator::coerce(llvm::Value *Value, llvm::Type *Ty) {
  if (!Value || Value->getType() == Ty)
    return Value;
  // Re-interpreting a value back at the type it was produced at is not a
  // conversion at all; DXBC's typeless registers make those round trips
  // common enough to be worth not emitting.
  if (auto *Call = llvm::dyn_cast<llvm::CallInst>(Value))
    if (Call->arg_size() == 2 && Call->getArgOperand(1)->getType() == Ty &&
        Call->getCalledFunction() &&
        Call->getCalledFunction()->getName().starts_with("dx.op.bitcast"))
      return Call->getArgOperand(1);
  // A literal has no runtime representation to reinterpret; its bits are
  // just written the other way round.
  if (auto *Constant = llvm::dyn_cast<llvm::Constant>(Value))
    return llvm::ConstantExpr::getBitCast(Constant, Ty);
  // DXIL predates LLVM's own `bitcast` between these types being legal in
  // a shader, and names the reinterpretation as an operation instead.
  if (Value->getType()->isFloatTy() && Ty->isIntegerTy(32))
    return emitDXOp("bitcastF32toI32", DXILOp::BitcastF32toI32, Ty, {Value});
  if (Value->getType()->isIntegerTy(32) && Ty->isFloatTy())
    return emitDXOp("bitcastI32toF32", DXILOp::BitcastI32toF32, Ty, {Value});
  return Builder.CreateBitCast(Value, Ty);
}

llvm::Value *Translator::convertPrecision(llvm::Value *Value, llvm::Type *Ty,
                                          bool Unsigned) {
  if (!Value || Value->getType() == Ty)
    return Value;
  // Changing a value's width is not arithmetic, so it has no
  // floating-point semantics of its own to relax.
  llvm::IRBuilderBase::FastMathFlagGuard Guard(Builder);
  Builder.clearFastMathFlags();
  llvm::Type *From = Value->getType();
  if (From->isHalfTy() && Ty->isFloatTy())
    return Builder.CreateFPExt(Value, Ty);
  if (From->isFloatTy() && Ty->isHalfTy())
    return Builder.CreateFPTrunc(Value, Ty);
  if (From->isIntegerTy(16) && Ty->isIntegerTy(32))
    return Unsigned ? Builder.CreateZExt(Value, Ty)
                    : Builder.CreateSExt(Value, Ty);
  if (From->isIntegerTy(32) && Ty->isIntegerTy(16))
    return Builder.CreateTrunc(Value, Ty);
  if (From->getPrimitiveSizeInBits() == Ty->getPrimitiveSizeInBits())
    return coerce(Value, Ty);
  // The integer and floating-point families do not meet at 16 bits, so a
  // reinterpretation between them has to happen at 32.
  if (From->getPrimitiveSizeInBits() == 16)
    return coerce(convertPrecision(Value, widen(From), Unsigned), Ty);
  return convertPrecision(coerce(Value, widen(Ty)), Ty, Unsigned);
}

//===----------------------------------------------------------------------===//
// Resources
//===----------------------------------------------------------------------===//

llvm::StructType *Translator::handleTy() {
  if (auto *Existing =
          llvm::StructType::getTypeByName(Context, "dx.types.Handle"))
    return Existing;
  return llvm::StructType::create(Context, {Builder.getPtrTy()},
                                  "dx.types.Handle");
}

llvm::StructType *Translator::cbufferRetTy(llvm::Type *Element) {
  llvm::StringRef Suffix = Element->isFloatTy() ? "f32" : "i32";
  std::string Name = ("dx.types.CBufRet." + Suffix).str();
  if (auto *Existing = llvm::StructType::getTypeByName(Context, Name))
    return Existing;
  llvm::Type *Fields[] = {Element, Element, Element, Element};
  return llvm::StructType::create(Context, Fields, Name);
}

llvm::StructType *Translator::twoI32Ty() {
  if (auto *Existing =
          llvm::StructType::getTypeByName(Context, "dx.types.twoi32"))
    return Existing;
  llvm::Type *Fields[] = {i32Ty(), i32Ty()};
  return llvm::StructType::create(Context, Fields, "dx.types.twoi32");
}

/// The DXIL component type a DXBC resource return type names.
static DXILComponentType toComponentType(ResourceReturnType Type) {
  switch (Type) {
  case ResourceReturnType::Unorm:
    return DXILComponentType::UNormF32;
  case ResourceReturnType::Snorm:
    return DXILComponentType::SNormF32;
  case ResourceReturnType::Sint:
    return DXILComponentType::I32;
  case ResourceReturnType::Uint:
    return DXILComponentType::U32;
  case ResourceReturnType::Float:
    return DXILComponentType::F32;
  }
  return DXILComponentType::F32;
}

void Translator::collectResources(dxsa::ModuleOp Shader) {
  // A handle names its resource by the index of the declaration within its
  // class, so the declarations are numbered per class in program order.
  llvm::DenseMap<unsigned, unsigned> Ranges;
  auto record = [&](ResourceClass Class, unsigned Id,
                    std::optional<uint32_t> Lbound,
                    std::optional<uint32_t> Ubound,
                    std::optional<uint32_t> Space) -> Resource & {
    unsigned &Range = Ranges[unsigned(Class)];
    Resource &R = Resources[resourceKey(Class, Id)];
    R.Class = Class;
    R.Range = Range;
    // Shader model 5.1 binds a range of registers and names the range by
    // an identifier of its own; before that the identifier is the register.
    R.Bind = Lbound.value_or(Id);
    R.Space = Space.value_or(0);
    R.Size = Ubound ? *Ubound - R.Bind + 1 : 1;
    R.Name = (llvm::StringRef(Class == ResourceClass::SRV   ? "T"
                              : Class == ResourceClass::UAV ? "U"
                              : Class == ResourceClass::CBV ? "CB"
                                                            : "S") +
              llvm::Twine(Range))
                 .str();
    ++Range;
    return R;
  };

  for (mlir::Operation &Op : *Shader.getBodyBlock()) {
    if (auto Dcl = llvm::dyn_cast<dxsa::DclConstantBuffer>(&Op)) {
      Resource &R = record(ResourceClass::CBV, Dcl.getId(), Dcl.getLbound(),
                           Dcl.getUbound(), Dcl.getSpace());
      // A constant buffer's "stride" is its size in bytes: one 16-byte row
      // per declared vector.
      R.Stride = Dcl.getSize() * 16;
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclSampler>(&Op)) {
      Resource &R = record(ResourceClass::Sampler, Dcl.getId(), Dcl.getLbound(),
                           Dcl.getUbound(), Dcl.getSpace());
      R.Kind = ResourceKind::Sampler;
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclResource>(&Op)) {
      Resource &R = record(ResourceClass::SRV, Dcl.getId(), Dcl.getLbound(),
                           Dcl.getUbound(), Dcl.getSpace());
      R.Kind = toResourceKind(Dcl.getDim());
      R.Component = toComponentType(Dcl.getX());
      R.SampleCount = Dcl.getSampleCount().value_or(0);
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclUavTyped>(&Op)) {
      Resource &R = record(ResourceClass::UAV, Dcl.getId(), Dcl.getLbound(),
                           Dcl.getUbound(), Dcl.getSpace());
      R.Kind = toResourceKind(Dcl.getDim());
      R.Component = toComponentType(Dcl.getX());
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclResourceRaw>(&Op)) {
      Resource &R = record(ResourceClass::SRV, Dcl.getId(), Dcl.getLbound(),
                           Dcl.getUbound(), Dcl.getSpace());
      R.Kind = ResourceKind::RawBuffer;
      R.Component = DXILComponentType::U32;
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclUavRaw>(&Op)) {
      Resource &R = record(ResourceClass::UAV, Dcl.getId(), Dcl.getLbound(),
                           Dcl.getUbound(), Dcl.getSpace());
      R.Kind = ResourceKind::RawBuffer;
      R.Component = DXILComponentType::U32;
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclResourceStructured>(&Op)) {
      Resource &R = record(ResourceClass::SRV, Dcl.getId(), Dcl.getLbound(),
                           Dcl.getUbound(), Dcl.getSpace());
      R.Kind = ResourceKind::StructuredBuffer;
      R.Component = DXILComponentType::U32;
      R.Stride = Dcl.getStructByteStride();
    } else if (auto Dcl = llvm::dyn_cast<dxsa::DclUavStructured>(&Op)) {
      Resource &R = record(ResourceClass::UAV, Dcl.getId(), Dcl.getLbound(),
                           Dcl.getUbound(), Dcl.getSpace());
      R.Kind = ResourceKind::StructuredBuffer;
      R.Component = DXILComponentType::U32;
      R.Stride = Dcl.getStructByteStride();
    }
  }
}

void Translator::createResourceHandles(dxsa::ModuleOp Shader) {
  // DXIL binds the resource classes in order -- SRVs, UAVs, constant
  // buffers, samplers -- and each class in the order its declarations were
  // numbered, which is not the order they appear in.
  llvm::SmallVector<Resource *, 8> Declared;
  for (auto &[Key, R] : Resources)
    Declared.push_back(&R);
  llvm::sort(Declared, [](const Resource *L, const Resource *R) {
    return std::tie(L->Class, L->Range) < std::tie(R->Class, R->Range);
  });
  for (Resource *R : Declared)
    R->Handle = emitDXOp("createHandle", DXILOp::CreateHandle, handleTy(),
                         {llvm::ConstantInt::get(i8Ty(), unsigned(R->Class)),
                          llvm::ConstantInt::get(i32Ty(), R->Range),
                          llvm::ConstantInt::get(i32Ty(), R->Bind),
                          llvm::ConstantInt::get(i1Ty(), 0)},
                         noOverload());
}

const Translator::Resource *Translator::findResource(ResourceClass Class,
                                                     OperandIndexAttr Index,
                                                     mlir::Operation *Op) {
  std::optional<unsigned> Id = registerNumber(Index);
  auto Found = Id ? Resources.find(resourceKey(Class, *Id)) : Resources.end();
  if (Found == Resources.end()) {
    unsupported(Op) << ": access to an undeclared resource";
    return nullptr;
  }
  return &Found->second;
}

llvm::Value *Translator::resourceHandle(ResourceClass Class,
                                        OperandIndexAttr Index,
                                        mlir::Operation *Op, unsigned Slot,
                                        unsigned Trailing, bool NonUniform) {
  const Resource *R = findResource(Class, Index, Op);
  if (!R)
    return nullptr;
  // Shader model 5.1 binds a range of registers, so the operand also picks
  // the register within that range -- and may do so at run time, in which
  // case the handle is bound at the access rather than at the entry point.
  if (Index.size() < 2 + Trailing || !Index[1].getRelative())
    return R->Handle;
  llvm::Value *&Cached = HandleCache[Index.getAsOpaquePointer()];
  if (Cached)
    return Cached;
  llvm::Value *Bind = readIndex(Index[1], Slot, Op);
  if (!Bind)
    return nullptr;
  Cached = emitDXOp("createHandle", DXILOp::CreateHandle, handleTy(),
                    {llvm::ConstantInt::get(i8Ty(), unsigned(Class)),
                     llvm::ConstantInt::get(i32Ty(), R->Range), Bind,
                     llvm::ConstantInt::get(i1Ty(), NonUniform)},
                    noOverload());
  return Cached;
}

llvm::Value *Translator::readIndex(IndexAttr Index, unsigned Slot,
                                   mlir::Operation *Op) {
  mlir::IntegerAttr Offset = Index.getImm();
  SrcOperandAttr Relative = Index.getRelative();
  if (!Relative)
    return Offset ? llvm::ConstantInt::get(i32Ty(), Offset.getInt()) : nullptr;
  llvm::Value *Value = readSource(Relative, 0, i32Ty(), Op, Slot);
  if (!Value || !Offset || Offset.getInt() == 0)
    return Value;
  return Builder.CreateAdd(Value,
                           llvm::ConstantInt::get(i32Ty(), Offset.getInt()));
}

llvm::Value *Translator::readConstantBuffer(SrcOperandAttr Src, unsigned Comp,
                                            llvm::Type *Ty, mlir::Operation *Op,
                                            unsigned Slot) {
  OperandIndexAttr Index = Src.getIndex();
  // A `cb#` operand indexes the buffer and then the row; shader model 5.1
  // inserts the register within the declared range in between.
  if (!Index || Index.size() < 2) {
    unsupported(Op) << ": constant buffer operand indexing";
    return nullptr;
  }
  llvm::Value *Handle =
      resourceHandle(ResourceClass::CBV, Index, Op, Slot, /*Trailing=*/1);
  if (!Handle)
    return nullptr;
  llvm::Value *Row = readIndex(Index[Index.size() - 1], Slot, Op);
  if (!Row)
    return nullptr;

  llvm::Type *Element = Ty->isFloatTy() ? floatTy() : i32Ty();
  auto Key = std::make_tuple(Src.getAsOpaquePointer(), Row, Element);
  llvm::Value *&Loaded = RowCache[Key];
  if (!Loaded)
    Loaded = emitDXOp("cbufferLoadLegacy", DXILOp::CBufferLoadLegacy,
                      cbufferRetTy(Element), {Handle, Row}, Element);
  return Builder.CreateExtractValue(Loaded, Comp);
}

llvm::AllocaInst *Translator::tempSlot(unsigned Reg, unsigned Comp,
                                       bool Narrow) {
  uint64_t Key = tempKey(Reg, Comp, Narrow);
  llvm::AllocaInst *&Slot = Temps[Key];
  if (Slot)
    return Slot;
  auto It = TempTypes.find(Key);
  llvm::Type *Ty =
      It == TempTypes.end() ? (Narrow ? i16Ty() : i32Ty()) : It->second;
  // DXBC numbers a temp by register and component; DXIL's own temp-register
  // intrinsics flatten that to a single index, which is the name dxilconv
  // gives the promoted value.
  AllocaBuilder.SetInsertPointPastAllocas(EntryFn);
  Slot =
      AllocaBuilder.CreateAlloca(Ty, nullptr,
                                 llvm::Twine(Narrow ? "dx.v16.r" : "dx.v32.r") +
                                     llvm::Twine(uint64_t(Reg) * 4 + Comp));
  return Slot;
}

void Translator::createIndexableTemps(dxsa::ModuleOp Shader) {
  for (mlir::Operation &Op : *Shader.getBodyBlock()) {
    auto Dcl = llvm::dyn_cast<dxsa::DclIndexableTemp>(&Op);
    if (!Dcl)
      continue;
    IndexableTemp &Array = IndexableTemps[Dcl.getId()];
    // A register declared twice -- once per hull shader phase, say -- is
    // one array, sized to hold either declaration.
    Array.Components = std::max(Array.Components, Dcl.getNumComponents());
    Array.Elements = std::max(Array.Elements, Dcl.getSize());
  }
  // An array accessed at minimum precision needs the narrow element types
  // too; one that never is does not, and dxilconv does not allocate them.
  llvm::DenseSet<unsigned> Narrow;
  for (mlir::Operation &Op : *Shader.getBodyBlock())
    for (mlir::NamedAttribute Attr : Op.getAttrs()) {
      if (auto Src = llvm::dyn_cast<SrcOperandAttr>(Attr.getValue())) {
        if (Src.getType() == OperandType::x && Src.getMinPrecision())
          if (std::optional<unsigned> Reg = registerNumber(Src.getIndex()))
            Narrow.insert(*Reg);
      } else if (auto Dst = llvm::dyn_cast<DstOperandAttr>(Attr.getValue())) {
        if (Dst.getType() == OperandType::x && Dst.getMinPrecision())
          if (std::optional<unsigned> Reg = registerNumber(Dst.getIndex()))
            Narrow.insert(*Reg);
      }
    }

  llvm::SmallVector<unsigned, 4> Registers;
  for (const auto &[Reg, Array] : IndexableTemps)
    Registers.push_back(Reg);
  llvm::sort(Registers);
  for (unsigned Reg : Registers) {
    IndexableTemp &Array = IndexableTemps[Reg];
    unsigned Size = Array.Elements * Array.Components;
    llvm::SmallVector<llvm::Type *, 4> Elements = {floatTy(), i32Ty()};
    if (Narrow.contains(Reg)) {
      Elements.push_back(halfTy());
      Elements.push_back(i16Ty());
    }
    for (llvm::Type *Element : Elements) {
      bool Is16 = Element->getPrimitiveSizeInBits() == 16;
      auto *Alloca = AllocaBuilder.CreateAlloca(
          llvm::ArrayType::get(Element, Size), nullptr,
          llvm::Twine(Is16 ? "dx.v16.x" : "dx.v32.x") + llvm::Twine(Reg));
      Alloca->setAlignment(llvm::Align(4));
      Array.Arrays[Element] = Alloca;
    }
  }
}

llvm::Value *Translator::indexableTempAddress(OperandIndexAttr Index,
                                              unsigned Comp, llvm::Type *Ty,
                                              mlir::Operation *Op,
                                              unsigned Slot) {
  std::optional<unsigned> Reg = registerNumber(Index);
  auto Declared = Reg ? IndexableTemps.find(*Reg) : IndexableTemps.end();
  if (!Reg || Index.size() < 2 || Declared == IndexableTemps.end()) {
    unsupported(Op) << ": access to an undeclared indexable temp";
    return nullptr;
  }
  const IndexableTemp &Array = Declared->second;
  llvm::AllocaInst *Storage = Array.Arrays.lookup(Ty);
  if (!Storage) {
    unsupported(Op) << ": indexable temp element type";
    return nullptr;
  }

  // The array is flat, so the element index scales by the declared
  // component count and the component index is added on.
  llvm::Value *Element = readIndex(Index[1], Slot, Op);
  llvm::Value *Offset = nullptr;
  if (auto *Constant = llvm::dyn_cast_or_null<llvm::ConstantInt>(Element)) {
    Offset = llvm::ConstantInt::get(
        i32Ty(), Constant->getZExtValue() * Array.Components + Comp);
  } else if (Element) {
    Offset = Builder.CreateMul(
        Element, llvm::ConstantInt::get(i32Ty(), Array.Components));
    Offset = Builder.CreateAdd(Offset, llvm::ConstantInt::get(i32Ty(), Comp));
  } else {
    Offset = llvm::ConstantInt::get(i32Ty(), Comp);
  }
  return Builder.CreateGEP(Storage->getAllocatedType(), Storage,
                           {llvm::ConstantInt::get(i32Ty(), 0), Offset});
}

llvm::Value *Translator::readSource(SrcOperandAttr Src, unsigned DstComp,
                                    llvm::Type *Ty, mlir::Operation *Op,
                                    unsigned Slot) {
  // A minimum-precision operand is stored narrow and widened when the
  // instruction reading it is a 32-bit one.
  OperandMinPrecisionAttr MinPrecision = Src.getMinPrecision();
  llvm::Type *Requested = Ty;
  Ty = storageType(MinPrecision, Ty);

  llvm::SaveAndRestore<bool> NotPrecise(Precise, false);
  unsigned Comp = sourceComponent(Src, DstComp);
  // Attributes are uniqued, so two operands that read the same register
  // through the same swizzle are one attribute; the slot keeps them apart,
  // because DXBC reads each operand of an instruction separately.
  auto Key = std::make_tuple(Src.getAsOpaquePointer(), Slot, Comp, Ty);
  if (llvm::Value *Cached = SourceCache.lookup(Key))
    return Cached;

  llvm::Value *Result = nullptr;
  switch (Src.getType()) {
  case OperandType::l: {
    llvm::ArrayRef<int32_t> Values = Src.getValues32().asArrayRef();
    if (Values.empty()) {
      unsupported(Op) << ": empty immediate";
      return nullptr;
    }
    // A single-component immediate broadcasts; a four-component one is
    // indexed by the swizzle like any other source.
    int32_t Bits = Values.size() == 1 ? Values[0] : Values[Comp];
    // A DXBC literal is always 32 bits wide; an instruction running at
    // minimum precision converts it rather than reinterpreting it.
    Result = Ty->isFloatingPointTy()
                 ? llvm::ConstantFP::get(
                       floatTy(), llvm::APFloat(llvm::bit_cast<float>(Bits)))
                 : static_cast<llvm::Value *>(
                       llvm::ConstantInt::get(i32Ty(), uint32_t(Bits)));
    Result = convertPrecision(Result, Ty, isUnsignedPrecision(MinPrecision));
    break;
  }
  case OperandType::r: {
    std::optional<unsigned> Reg = registerNumber(Src.getIndex());
    if (!Reg) {
      unsupported(Op) << ": indexed temp register";
      return nullptr;
    }
    // Reading a temp component the shader never wrote is legal DXBC and
    // yields an undefined value, which is what promoting an uninitialized
    // slot produces.
    llvm::AllocaInst *Slot = tempSlot(*Reg, Comp, bool(MinPrecision));
    Result = Builder.CreateLoad(Slot->getAllocatedType(), Slot);
    break;
  }
  case OperandType::v: {
    OperandIndexAttr Indices = Src.getIndex();
    std::optional<unsigned> Reg = registerNumber(Indices);
    // A register indexed at run time names its element by the first
    // register of the declared range it falls in, and the row relative to
    // that register.
    llvm::Value *Row = nullptr;
    if (!Reg && Indices && !Indices.empty() && Indices[0].getRelative()) {
      unsigned Base = Indices[0].getImm() ? Indices[0].getImm().getInt() : 0;
      for (const InputRange &Range : InputRanges)
        if (Base >= Range.Start && Base < Range.Start + Range.Count)
          Base = Range.Start;
      llvm::Value *Value = readIndex(Indices[0], Slot, Op);
      if (!Value)
        return nullptr;
      Reg = Base;
      Row = Builder.CreateSub(Value, llvm::ConstantInt::get(i32Ty(), Base));
    }
    if (!Reg) {
      unsupported(Op) << ": indexed input register";
      return nullptr;
    }
    std::optional<unsigned> Element = Inputs.find(*Reg, Comp);
    if (!Element) {
      unsupported(Op) << ": read of undeclared input register";
      return nullptr;
    }
    if (!Row)
      Row = llvm::ConstantInt::get(i32Ty(),
                                   *Reg - Inputs.elements()[*Element].Row);
    if (auto Read = SystemValueReads.find(*Element);
        Read != SystemValueReads.end()) {
      Result = emitDXOp(Read->second.first, Read->second.second, i32Ty(), {});
      break;
    }
    // The element has a width of its own; an instruction running at a
    // narrower one converts what it read.
    llvm::Type *ElementTy = MinPrecision ? Ty : widen(Ty);
    unsigned Col = Comp - Inputs.elements()[*Element].StartCol;
    Result = emitDXOp("loadInput", DXILOp::LoadInput, ElementTy,
                      {llvm::ConstantInt::get(i32Ty(), *Element), Row,
                       llvm::ConstantInt::get(i8Ty(), Col),
                       llvm::UndefValue::get(i32Ty())});
    break;
  }
  case OperandType::vThreadID:
  case OperandType::vThreadGroupID:
  case OperandType::vThreadIDInGroup: {
    // These name a three-component value directly rather than a signature
    // register, so the swizzle picks the dimension the operation returns.
    auto [Name, DXOp] =
        Src.getType() == OperandType::vThreadID
            ? std::make_pair("threadId", DXILOp::ThreadId)
        : Src.getType() == OperandType::vThreadGroupID
            ? std::make_pair("groupId", DXILOp::GroupId)
            : std::make_pair("threadIdInGroup", DXILOp::ThreadIdInGroup);
    Result =
        emitDXOp(Name, DXOp, i32Ty(), {llvm::ConstantInt::get(i32Ty(), Comp)});
    break;
  }
  case OperandType::vThreadIDInGroupFlattened:
    Result = emitDXOp("flattenedThreadIdInGroup",
                      DXILOp::FlattenedThreadIdInGroup, i32Ty(), {});
    break;
  case OperandType::vCoverage:
    Result = emitDXOp("coverage", DXILOp::Coverage, i32Ty(), {});
    break;
  case OperandType::vInnerCoverage:
    Result = emitDXOp("innerCoverage", DXILOp::InnerCoverage, i32Ty(), {});
    break;
  case OperandType::cycleCounter: {
    // The counter is a 64-bit value returned as a pair of 32-bit halves,
    // which the swizzle picks between.
    llvm::StructType *Pair = twoI32Ty();
    llvm::Value *&Counter = CycleCounter;
    if (!Counter)
      Counter = emitDXOp("cycleCounterLegacy", DXILOp::CycleCounterLegacy, Pair,
                         {}, noOverload());
    Result = Builder.CreateExtractValue(Counter, Comp & 1);
    break;
  }
  case OperandType::cb: {
    // A constant buffer row is 16 bytes of 32-bit slots whatever the
    // instruction reading it runs at, so it is never narrowed on the way
    // out only to be widened again.
    Result = readConstantBuffer(Src, Comp, widen(Requested), Op, Slot);
    if (!Result)
      return nullptr;
    break;
  }
  case OperandType::x: {
    llvm::Value *Address =
        indexableTempAddress(Src.getIndex(), Comp, Ty, Op, Slot);
    if (!Address)
      return nullptr;
    auto *Load = Builder.CreateLoad(Ty, Address);
    Load->setAlignment(elementAlign(Ty));
    Result = Load;
    break;
  }
  default:
    unsupported(Op) << ": source operand kind";
    return nullptr;
  }

  // Each operand kind produced its value at the width its own storage
  // holds; this is where it meets the width the instruction runs at.
  Result =
      convertPrecision(Result, Requested, isUnsignedPrecision(MinPrecision));

  OperandModifierAttr Modifier = Src.getModifier();
  if (!Modifier) {
    SourceCache[Key] = Result;
    return Result;
  }
  bool Abs = Modifier.getValue() == OperandModifier::abs ||
             Modifier.getValue() == OperandModifier::abs_neg;
  bool Neg = Modifier.getValue() == OperandModifier::neg ||
             Modifier.getValue() == OperandModifier::abs_neg;
  if (Abs) {
    if (!Ty->isFloatTy()) {
      unsupported(Op) << ": absolute-value modifier on an integer operand";
      return nullptr;
    }
    Result = emitDXOp("unary", DXILOp::FAbs, Ty, {Result});
  }
  if (Neg)
    Result =
        Ty->isFloatTy()
            ? Builder.CreateFSub(llvm::ConstantFP::getNegativeZero(Ty), Result)
            : Builder.CreateSub(llvm::ConstantInt::get(i32Ty(), 0), Result);
  SourceCache[Key] = Result;
  return Result;
}

bool Translator::writeDestination(DstOperandAttr Dst,
                                  llvm::ArrayRef<llvm::Value *> Components,
                                  mlir::Operation *Op,
                                  OperandMinPrecisionAttr ValuePrecision) {
  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);
  // A value computed at a width the destination does not hold is converted
  // as it is written, which is where DXBC's `min16f as def32` conversions
  // and the 32-bit-only operations' narrow destinations meet. The
  // conversion belongs with its own store rather than ahead of all of
  // them, which is the order dxilconv emits.
  llvm::Type *Narrow = minPrecisionType(Dst.getMinPrecision(), Context);
  llvm::Type *Wide = nullptr;
  if (!Narrow && ValuePrecision)
    Wide = minPrecisionType(ValuePrecision, Context)->isHalfTy() ? floatTy()
                                                                 : i32Ty();
  auto adjust = [&](llvm::Value *Value) {
    if (llvm::Type *Target = Narrow ? Narrow : Wide)
      return convertPrecision(Value, Target,
                              isUnsignedPrecision(ValuePrecision));
    return Value;
  };
  if (std::optional<DXILSemanticKind> Kind = registerlessKind(Dst.getType())) {
    auto Element = RegisterlessOutputs.find(unsigned(*Kind));
    if (Element == RegisterlessOutputs.end()) {
      unsupported(Op) << ": write to an undeclared registerless output";
      return false;
    }
    llvm::Value *Value =
        RealOutputSignature.empty()
            ? coerce(adjust(Components[0]), floatTy())
            : convertPrecision(
                  Components[0],
                  componentLLVMType(Outputs.elements()[Element->second].Type,
                                    Context),
                  isUnsignedPrecision(ValuePrecision));
    emitDXOp("storeOutput", DXILOp::StoreOutput, llvm::Type::getVoidTy(Context),
             {llvm::ConstantInt::get(i32Ty(), Element->second),
              llvm::ConstantInt::get(i32Ty(), 0),
              llvm::ConstantInt::get(i8Ty(), 0), Value});
    return true;
  }
  switch (Dst.getType()) {
  case OperandType::null:
    return true;
  case OperandType::r: {
    std::optional<unsigned> Reg = registerNumber(Dst.getIndex());
    if (!Reg) {
      unsupported(Op) << ": indexed temp destination";
      return false;
    }
    for (auto [I, Comp] : llvm::enumerate(Comps)) {
      llvm::AllocaInst *Slot = tempSlot(*Reg, Comp, bool(Narrow));
      Builder.CreateStore(
          coerce(adjust(Components[I]), Slot->getAllocatedType()), Slot);
    }
    return true;
  }
  case OperandType::x: {
    for (auto [I, Comp] : llvm::enumerate(Comps)) {
      // The address is computed before the value is converted, which is
      // the order dxilconv emits.
      llvm::Type *Element = Narrow ? Narrow : Components[I]->getType();
      llvm::Value *Address =
          indexableTempAddress(Dst.getIndex(), Comp, Element, Op, /*Slot=*/0);
      if (!Address)
        return false;
      Builder.CreateStore(adjust(Components[I]), Address)
          ->setAlignment(elementAlign(Element));
    }
    return true;
  }
  case OperandType::o: {
    std::optional<unsigned> Reg = registerNumber(Dst.getIndex());
    if (!Reg) {
      unsupported(Op) << ": indexed output destination";
      return false;
    }
    for (auto [I, Comp] : llvm::enumerate(Comps)) {
      std::optional<unsigned> Element = Outputs.find(*Reg, Comp);
      if (!Element) {
        unsupported(Op) << ": write to an undeclared output register";
        return false;
      }
      const SignatureElement &Info = Outputs.elements()[*Element];
      unsigned Col = Comp - Info.StartCol;
      // A signature element's component type says what the store is
      // overloaded on -- but only a real container carries it. A type
      // synthesized from declarations alone would just be `F32` for every
      // element (see feme/docs/Design.md), so the value's own type is the
      // better guess.
      // A signature element's component type says what the store is
      // overloaded on -- but only a real container carries it. A type
      // synthesized from declarations alone would just be `F32` for every
      // element (see feme/docs/Design.md), so the value's own type is the
      // better guess.
      llvm::Value *Value =
          RealOutputSignature.empty()
              ? adjust(Components[I])
              : convertPrecision(Components[I],
                                 componentLLVMType(Info.Type, Context),
                                 isUnsignedPrecision(ValuePrecision));
      emitDXOp("storeOutput", DXILOp::StoreOutput,
               llvm::Type::getVoidTy(Context),
               {llvm::ConstantInt::get(i32Ty(), *Element),
                llvm::ConstantInt::get(i32Ty(), 0),
                llvm::ConstantInt::get(i8Ty(), Col), Value});
    }
    return true;
  }
  default:
    unsupported(Op) << ": destination operand kind";
    return false;
  }
}

//===----------------------------------------------------------------------===//
// dx.op calls
//===----------------------------------------------------------------------===//

llvm::Function *Translator::dxOp(llvm::StringRef Name, llvm::Type *ReturnTy,
                                 llvm::ArrayRef<llvm::Type *> Args,
                                 llvm::Type *OverloadTy) {
  // An operation that is not overloaded -- one whose operand types are
  // fixed by the DXIL specification -- is named without a suffix.
  std::string FullName = OverloadTy ? ("dx.op." + Name + "." +
                                       (OverloadTy->isHalfTy()        ? "f16"
                                        : OverloadTy->isFloatTy()     ? "f32"
                                        : OverloadTy->isDoubleTy()    ? "f64"
                                        : OverloadTy->isIntegerTy(1)  ? "i1"
                                        : OverloadTy->isIntegerTy(16) ? "i16"
                                                                      : "i32"))
                                          .str()
                                    : ("dx.op." + Name).str();
  llvm::SmallVector<llvm::Type *, 8> Params;
  Params.push_back(i32Ty()); // the DXIL opcode
  llvm::append_range(Params, Args);
  auto *Ty = llvm::FunctionType::get(ReturnTy, Params, /*isVarArg=*/false);
  auto *Fn = llvm::cast<llvm::Function>(
      Module->getOrInsertFunction(FullName, Ty).getCallee());
  // Every dx.op this translation emits is a pure computation except for the
  // signature stores, which are the shader's observable output.
  Fn->addFnAttr(llvm::Attribute::NoUnwind);
  if (!ReturnTy->isVoidTy()) {
    Fn->setMemoryEffects(llvm::MemoryEffects::none());
    // Saying so is what lets a call nothing reads be deleted.
    Fn->addFnAttr(llvm::Attribute::WillReturn);
  }
  return Fn;
}

llvm::Value *Translator::emitDXOp(llvm::StringRef Name, DXILOp Op,
                                  llvm::Type *ReturnTy,
                                  llvm::ArrayRef<llvm::Value *> Args,
                                  llvm::Type *Overload) {
  llvm::SmallVector<llvm::Type *, 8> ArgTys;
  llvm::SmallVector<llvm::Value *, 8> CallArgs;
  CallArgs.push_back(llvm::ConstantInt::get(i32Ty(), unsigned(Op)));
  for (llvm::Value *Arg : Args) {
    ArgTys.push_back(Arg->getType());
    CallArgs.push_back(Arg);
  }
  // A void operation is overloaded on its value argument, which is last;
  // `discard`'s `i1` argument is not an overload, it is the operation's
  // fixed signature.
  llvm::Type *OverloadTy = Overload == noOverload()      ? nullptr
                           : Overload                    ? Overload
                           : Name.starts_with("bitcast") ? nullptr
                           : !ReturnTy->isVoidTy()       ? ReturnTy
                           : Name == "discard"           ? nullptr
                                                         : ArgTys.back();
  // A dx.op call names a specific operation with fixed semantics; the
  // relaxed floating-point rules apply to the native LLVM arithmetic the
  // translation emits around it, not to the call itself.
  llvm::IRBuilderBase::FastMathFlagGuard Guard(Builder);
  Builder.clearFastMathFlags();
  llvm::CallInst *Call =
      Builder.CreateCall(dxOp(Name, ReturnTy, ArgTys, OverloadTy), CallArgs);
  if (Precise)
    Call->setMetadata(
        "dx.precise",
        llvm::MDNode::get(Context, llvm::ConstantAsMetadata::get(
                                       llvm::ConstantInt::get(i32Ty(), 1))));
  return Call;
}

llvm::StringRef Translator::stageName(ProgramType Type) {
  switch (Type) {
  case ProgramType::pixel_shader:
    return "ps";
  case ProgramType::vertex_shader:
    return "vs";
  case ProgramType::geometry_shader:
    return "gs";
  case ProgramType::hull_shader:
    return "hs";
  case ProgramType::domain_shader:
    return "ds";
  case ProgramType::compute_shader:
    return "cs";
  case ProgramType::mesh_shader:
    return "ms";
  case ProgramType::amplification_shader:
    return "as";
  }
  return "ps";
}

llvm::Type *Translator::movElementType(DstOperandAttr Dst, SrcOperandAttr Src,
                                       unsigned DstComp) {
  // A modifier is arithmetic, so it forces a floating-point reading of the
  // bits whatever the registers involved are otherwise used for.
  // `mov` is a 32-bit copy even between minimum-precision operands: the
  // source is widened as it is read and the destination narrows it again,
  // which is what DXBC's `min16f as def32` / `def32 as min16f` operand
  // annotations describe.
  if (llvm::Type *Narrow = minPrecisionType(Src.getMinPrecision(), Context))
    return widen(Narrow);
  if (Src.getModifier())
    return floatTy();
  // An indexable temp is a flat array of raw 32-bit slots, so a copy to or
  // from one carries bits rather than a value of some type.
  if (Src.getType() == OperandType::x ||
      (Dst && Dst.getType() == OperandType::x))
    return i32Ty();
  if (Src.getType() == OperandType::r)
    if (std::optional<unsigned> Reg = registerNumber(Src.getIndex()))
      return tempSlot(*Reg, sourceComponent(Src, DstComp))->getAllocatedType();

  // A signature register does have a type of its own: the one its
  // signature element declares.
  if (Src.getType() == OperandType::v)
    if (std::optional<unsigned> Reg = registerNumber(Src.getIndex()))
      if (std::optional<unsigned> Element =
              Inputs.find(*Reg, sourceComponent(Src, DstComp)))
        return Inputs.elements()[*Element].Type == DXILComponentType::F32
                   ? floatTy()
                   : i32Ty();
  if (Dst && Dst.getType() == OperandType::r)
    if (std::optional<unsigned> Reg = registerNumber(Dst.getIndex()))
      return tempSlot(*Reg, DstComp)->getAllocatedType();
  return floatTy();
}

llvm::Constant *Translator::foldFloatToInt(llvm::Instruction::CastOps Cast,
                                           llvm::Value *Source) {
  auto *Literal = llvm::dyn_cast<llvm::ConstantFP>(Source);
  if (!Literal)
    return nullptr;
  double Value = Literal->getValueAPF().convertToFloat();
  bool Signed = Cast == llvm::Instruction::FPToSI;
  double Low = Signed ? double(std::numeric_limits<int32_t>::min()) : 0.0;
  double High = Signed ? double(std::numeric_limits<int32_t>::max())
                       : double(std::numeric_limits<uint32_t>::max());
  if (std::isnan(Value))
    Value = 0.0;
  Value = std::min(std::max(Value, Low), High);
  return llvm::ConstantInt::get(i32Ty(), Signed ? uint32_t(int32_t(Value))
                                                : uint32_t(Value));
}

llvm::Value *Translator::saturate(llvm::Value *Value, bool Enabled) {
  if (!Enabled || !Value)
    return Value;
  // A source component named twice by a swizzle is read once, so clamping
  // it is one operation too.
  llvm::Value *&Cached = SaturateCache[Value];
  if (!Cached)
    Cached = emitDXOp("unary", DXILOp::Saturate, Value->getType(), {Value});
  return Cached;
}

//===----------------------------------------------------------------------===//
// Instruction lowering
//===----------------------------------------------------------------------===//

/// How an instruction reads and writes its operands, and the LLVM/DXIL
/// construct it lowers to.
struct OpLowering {
  enum class Form {
    /// A native LLVM instruction, selected by \c Binop.
    Native,
    /// A `dx.op.<Name>` call carrying \c Op.
    Call,
    /// A cast instruction, selected by \c Cast.
    Cast,
    /// A comparison followed by a sign extension to the DXBC boolean.
    Compare,
    /// `1.0 / x`, which DXIL leaves to a plain division.
    Reciprocal,
    /// `0 - x`.
    Negate,
  };

  Form Kind = Form::Native;
  /// Operand and result element type: true if float, false if i32.
  bool FloatOperands = false;
  bool FloatResult = false;
  llvm::Instruction::BinaryOps Binop = llvm::Instruction::BinaryOpsEnd;
  llvm::Instruction::CastOps Cast = llvm::Instruction::CastOpsEnd;
  llvm::CmpInst::Predicate Predicate = llvm::CmpInst::BAD_ICMP_PREDICATE;
  DXILOp Op = DXILOp::LoadInput;
  llvm::StringRef Name;
  /// True for the `dx.op` calls that carry no `.<overload>` suffix.
  bool NoOverload = false;
  /// False for the operations DXIL only defines at 32 bits, which a
  /// minimum-precision destination has to convert the result of.
  bool NarrowCapable = true;
};

static OpLowering nativeOp(llvm::Instruction::BinaryOps Binop, bool Float) {
  OpLowering L;
  L.Kind = OpLowering::Form::Native;
  L.FloatOperands = L.FloatResult = Float;
  L.Binop = Binop;
  return L;
}

static OpLowering callOp(DXILOp Op, llvm::StringRef Name, bool Float) {
  OpLowering L;
  L.Kind = OpLowering::Form::Call;
  L.FloatOperands = L.FloatResult = Float;
  L.Op = Op;
  L.Name = Name;
  return L;
}

/// A `dx.op` call whose result type differs from its operand type.
static OpLowering convertOp(DXILOp Op, llvm::StringRef Name, bool FloatIn,
                            bool FloatOut) {
  OpLowering L = callOp(Op, Name, FloatIn);
  L.FloatResult = FloatOut;
  // These conversions fix both of their types, so DXIL does not overload
  // them on either.
  L.NoOverload = true;
  L.NarrowCapable = false;
  return L;
}

static OpLowering simpleOp(OpLowering::Form Kind, bool Float) {
  OpLowering L;
  L.Kind = Kind;
  L.FloatOperands = L.FloatResult = Float;
  return L;
}

static OpLowering castOp(llvm::Instruction::CastOps Cast, bool FloatIn,
                         bool FloatOut) {
  OpLowering L;
  L.Kind = OpLowering::Form::Cast;
  L.FloatOperands = FloatIn;
  L.FloatResult = FloatOut;
  L.Cast = Cast;
  L.NarrowCapable = false;
  return L;
}

/// A `dx.op` call DXIL only defines at 32 bits.
static OpLowering wideOp(DXILOp Op, llvm::StringRef Name, bool Float) {
  OpLowering L = callOp(Op, Name, Float);
  L.NarrowCapable = false;
  return L;
}

static OpLowering compareOp(llvm::CmpInst::Predicate Predicate, bool Float) {
  OpLowering L;
  L.Kind = OpLowering::Form::Compare;
  L.FloatOperands = Float;
  L.Predicate = Predicate;
  return L;
}

/// Returns how \p Name (a `dxsa` op mnemonic without the dialect prefix and
/// without any `_sat` suffix) lowers, or nullopt if it is not handled yet.
static std::optional<OpLowering> lookupLowering(llvm::StringRef Name) {
  using BO = llvm::Instruction;
  using CI = llvm::CmpInst;
  static const llvm::StringMap<OpLowering> Table = {
      // Floating-point arithmetic with a native LLVM equivalent.
      {"add", nativeOp(BO::FAdd, true)},
      {"mul", nativeOp(BO::FMul, true)},
      {"div", nativeOp(BO::FDiv, true)},
      // Floating-point arithmetic with no native equivalent.
      {"min", callOp(DXILOp::FMin, "binary", true)},
      {"max", callOp(DXILOp::FMax, "binary", true)},
      {"frc", callOp(DXILOp::Frc, "unary", true)},
      {"exp", callOp(DXILOp::Exp, "unary", true)},
      {"log", callOp(DXILOp::Log, "unary", true)},
      {"sqrt", callOp(DXILOp::Sqrt, "unary", true)},
      {"rsq", callOp(DXILOp::Rsqrt, "unary", true)},
      {"round_ne", callOp(DXILOp::RoundNe, "unary", true)},
      {"round_ni", callOp(DXILOp::RoundNi, "unary", true)},
      {"round_pi", callOp(DXILOp::RoundPi, "unary", true)},
      {"round_z", callOp(DXILOp::RoundZ, "unary", true)},
      {"rcp", simpleOp(OpLowering::Form::Reciprocal, true)},
      // Derivatives. DXBC's unqualified forms are the coarse ones.
      {"deriv_rtx", callOp(DXILOp::DerivCoarseX, "unary", true)},
      {"deriv_rty", callOp(DXILOp::DerivCoarseY, "unary", true)},
      {"deriv_rtx_coarse", callOp(DXILOp::DerivCoarseX, "unary", true)},
      {"deriv_rty_coarse", callOp(DXILOp::DerivCoarseY, "unary", true)},
      {"deriv_rtx_fine", callOp(DXILOp::DerivFineX, "unary", true)},
      {"deriv_rty_fine", callOp(DXILOp::DerivFineY, "unary", true)},
      // Integer arithmetic.
      {"iadd", nativeOp(BO::Add, false)},
      {"ineg", simpleOp(OpLowering::Form::Negate, false)},
      {"and", nativeOp(BO::And, false)},
      {"or", nativeOp(BO::Or, false)},
      {"xor", nativeOp(BO::Xor, false)},
      {"ishl", nativeOp(BO::Shl, false)},
      {"ishr", nativeOp(BO::AShr, false)},
      {"ushr", nativeOp(BO::LShr, false)},
      {"imin", callOp(DXILOp::IMin, "binary", false)},
      {"imax", callOp(DXILOp::IMax, "binary", false)},
      {"umin", callOp(DXILOp::UMin, "binary", false)},
      {"umax", callOp(DXILOp::UMax, "binary", false)},
      {"bfrev", wideOp(DXILOp::Bfrev, "unaryBits", false)},
      {"countbits", wideOp(DXILOp::Countbits, "unaryBits", false)},
      {"firstbit_lo", wideOp(DXILOp::FirstbitLo, "unaryBits", false)},
      {"firstbit_hi", wideOp(DXILOp::FirstbitHi, "unaryBits", false)},
      {"firstbit_shi", wideOp(DXILOp::FirstbitSHi, "unaryBits", false)},
      // Conversions.
      {"itof", castOp(BO::SIToFP, false, true)},
      {"utof", castOp(BO::UIToFP, false, true)},
      {"ftoi", castOp(BO::FPToSI, true, false)},
      {"ftou", castOp(BO::FPToUI, true, false)},
      // Half-precision packing, which DXIL keeps as a 32-bit-typed
      // operation carrying a 16-bit value.
      {"ubfe", wideOp(DXILOp::Ubfe, "tertiary", false)},
      {"ibfe", wideOp(DXILOp::Ibfe, "tertiary", false)},
      {"bfi", wideOp(DXILOp::Bfi, "quaternary", false)},
      {"f32tof16", convertOp(DXILOp::LegacyF32ToF16, "legacyF32ToF16", true,
                             /*FloatOut=*/false)},
      {"f16tof32", convertOp(DXILOp::LegacyF16ToF32, "legacyF16ToF32",
                             /*FloatIn=*/false, true)},
      // Comparisons. DXBC's boolean is an all-ones/all-zeroes 32-bit mask.
      {"eq", compareOp(CI::FCMP_OEQ, true)},
      {"ne", compareOp(CI::FCMP_UNE, true)},
      {"lt", compareOp(CI::FCMP_OLT, true)},
      {"ge", compareOp(CI::FCMP_OGE, true)},
      {"ieq", compareOp(CI::ICMP_EQ, false)},
      {"ine", compareOp(CI::ICMP_NE, false)},
      {"ilt", compareOp(CI::ICMP_SLT, false)},
      {"ige", compareOp(CI::ICMP_SGE, false)},
      {"ult", compareOp(CI::ICMP_ULT, false)},
      {"uge", compareOp(CI::ICMP_UGE, false)},
  };
  auto It = Table.find(Name);
  if (It == Table.end())
    return std::nullopt;
  return It->second;
}

/// Returns how \p Op types its operands and its result, or nullopt when the
/// instruction imposes no type on either (`mov` and the conditional moves
/// copy bits) or is not modelled at all. A comparison reads floating point
/// and writes an integer mask, so the two are tracked separately.
static std::optional<OpLowering> typedLowering(llvm::StringRef Name) {
  Name.consume_back("_sat");
  if (Name == "mov" || Name == "movc")
    return std::nullopt;
  if (Name == "mad" || Name.starts_with("dp"))
    return nativeOp(llvm::Instruction::FAdd, /*Float=*/true);
  if (Name == "imad" || Name == "umad")
    return nativeOp(llvm::Instruction::Add, /*Float=*/false);
  return lookupLowering(Name);
}

void Translator::inferTempTypes(dxsa::ModuleOp Shader) {
  // A minimum-precision annotation is not a vote but a statement: the
  // register is that width, and every access to it agrees.
  for (mlir::Operation &Op : *Shader.getBodyBlock())
    for (mlir::NamedAttribute Attr : Op.getAttrs()) {
      OperandType Kind;
      OperandIndexAttr Index;
      OperandMinPrecisionAttr MinPrecision;
      llvm::SmallVector<unsigned, 4> Comps;
      if (auto Src = llvm::dyn_cast<SrcOperandAttr>(Attr.getValue())) {
        Kind = Src.getType();
        Index = Src.getIndex();
        MinPrecision = Src.getMinPrecision();
        for (unsigned Comp = 0; Comp < 4; ++Comp)
          Comps.push_back(sourceComponent(Src, Comp));
      } else if (auto Dst = llvm::dyn_cast<DstOperandAttr>(Attr.getValue())) {
        Kind = Dst.getType();
        Index = Dst.getIndex();
        MinPrecision = Dst.getMinPrecision();
        Comps = destinationComponents(Dst);
      } else {
        continue;
      }
      llvm::Type *Narrow = minPrecisionType(MinPrecision, Context);
      if (Kind != OperandType::r || !Narrow)
        continue;
      if (std::optional<unsigned> Reg = registerNumber(Index))
        for (unsigned Comp : Comps)
          TempTypes[tempKey(*Reg, Comp, /*Narrow=*/true)] = Narrow;
    }

  // Votes cast for each temp component, as (floating point, integer).
  llvm::DenseMap<uint64_t, std::pair<unsigned, unsigned>> Votes;
  auto vote = [&](std::optional<unsigned> Reg, unsigned Comp, bool Float) {
    if (!Reg)
      return;
    std::pair<unsigned, unsigned> &V = Votes[uint64_t(*Reg) * 4 + Comp];
    ++(Float ? V.first : V.second);
  };
  // A signature register's component type is recorded in the signature, so
  // a `mov` that copies one to or from a temp fixes that temp's type too.
  auto signatureIsFloat = [](const Signature &Sig, unsigned Reg,
                             unsigned Comp) -> std::optional<bool> {
    std::optional<unsigned> Element = Sig.find(Reg, Comp);
    if (!Element)
      return std::nullopt;
    return Sig.elements()[*Element].Type == DXILComponentType::F32;
  };

  for (mlir::Operation &Op : *Shader.getBodyBlock()) {
    llvm::StringRef Name = mnemonicOf(&Op);
    if (Name.starts_with("dcl_"))
      continue;

    auto Dst = Op.getAttrOfType<DstOperandAttr>("dst");
    llvm::SmallVector<unsigned, 4> Comps =
        Dst ? destinationComponents(Dst) : llvm::SmallVector<unsigned, 4>{0};
    llvm::SmallVector<SrcOperandAttr, 4> Srcs;
    for (mlir::NamedAttribute Attr : Op.getAttrs())
      if (auto Src = llvm::dyn_cast<SrcOperandAttr>(Attr.getValue()))
        Srcs.push_back(Src);

    // A register used to index another operand holds an integer, whatever
    // else the shader does with it.
    auto voteIndices = [&](OperandIndexAttr Indices) {
      if (!Indices)
        return;
      for (IndexAttr Slot : Indices)
        if (SrcOperandAttr Relative = Slot.getRelative())
          if (Relative.getType() == OperandType::r)
            vote(registerNumber(Relative.getIndex()),
                 sourceComponent(Relative, 0), /*Float=*/false);
    };
    if (Dst)
      voteIndices(Dst.getIndex());
    for (SrcOperandAttr Src : Srcs)
      voteIndices(Src.getIndex());

    // A conditional control-flow instruction tests its condition as an
    // integer bit pattern.
    if (Name.ends_with("_z") || Name.ends_with("_nz") || Name == "switch")
      for (SrcOperandAttr Src : Srcs)
        if (Src.getType() == OperandType::r && !Src.getMinPrecision())
          vote(registerNumber(Src.getIndex()), sourceComponent(Src, 0), false);

    if (std::optional<OpLowering> Lowering = typedLowering(Name)) {
      // A minimum-precision operand names the narrow bank, whose type the
      // annotation already fixed; it says nothing about the 32-bit one.
      if (Dst && Dst.getType() == OperandType::r && !Dst.getMinPrecision())
        for (unsigned Comp : Comps)
          vote(registerNumber(Dst.getIndex()), Comp, Lowering->FloatResult);
      for (SrcOperandAttr Src : Srcs)
        if (Src.getType() == OperandType::r && !Src.getMinPrecision())
          for (unsigned Comp : Comps)
            vote(registerNumber(Src.getIndex()), sourceComponent(Src, Comp),
                 Lowering->FloatOperands);
      continue;
    }

    if (Name != "mov" && Name != "mov_sat")
      continue;
    auto Src = Op.getAttrOfType<SrcOperandAttr>("src");
    if (!Dst || !Src)
      continue;
    if (Dst.getType() == OperandType::r && Src.getType() == OperandType::l) {
      // DXBC spells an immediate as a 32-bit bit pattern, so moving one into
      // a temp is evidence the temp holds raw bits rather than a float.
      for (unsigned Comp : Comps)
        vote(registerNumber(Dst.getIndex()), Comp, /*Float=*/false);
    } else if (Dst.getType() == OperandType::r &&
               Src.getType() == OperandType::v) {
      std::optional<unsigned> SrcReg = registerNumber(Src.getIndex());
      for (unsigned Comp : Comps)
        if (SrcReg)
          if (std::optional<bool> Float =
                  signatureIsFloat(Inputs, *SrcReg, sourceComponent(Src, Comp)))
            vote(registerNumber(Dst.getIndex()), Comp, *Float);
    } else if (Dst.getType() == OperandType::o &&
               Src.getType() == OperandType::r) {
      std::optional<unsigned> DstReg = registerNumber(Dst.getIndex());
      for (unsigned Comp : Comps)
        if (DstReg)
          if (std::optional<bool> Float =
                  signatureIsFloat(*&Outputs, *DstReg, Comp))
            vote(registerNumber(Src.getIndex()), sourceComponent(Src, Comp),
                 *Float);
    }
  }

  for (auto [Key, V] : Votes)
    if (!TempTypes.count(Key))
      TempTypes[Key] = V.first > 0 && V.first >= V.second ? floatTy() : i32Ty();
}

std::pair<llvm::Type *, llvm::Type *>
Translator::operationTypes(const OpLowering &Lowering, DstOperandAttr Dst,
                           llvm::ArrayRef<SrcOperandAttr> Sources) {
  llvm::Type *SrcTy = Lowering.FloatOperands ? floatTy() : i32Ty();
  llvm::Type *DstTy = Lowering.FloatResult ? floatTy() : i32Ty();
  if (!Lowering.NarrowCapable)
    return {SrcTy, DstTy};

  if (Lowering.Kind == OpLowering::Form::Compare) {
    OperandMinPrecisionAttr Common = Sources.empty()
                                         ? OperandMinPrecisionAttr()
                                         : Sources[0].getMinPrecision();
    for (SrcOperandAttr Src : Sources)
      if (Src.getMinPrecision() != Common)
        return {SrcTy, DstTy};
    return {storageType(Common, SrcTy), DstTy};
  }

  OperandMinPrecisionAttr Precision =
      Dst ? Dst.getMinPrecision() : OperandMinPrecisionAttr();
  return {storageType(Precision, SrcTy), storageType(Precision, DstTy)};
}

bool Translator::translateUnary(mlir::Operation *Op, DstOperandAttr Dst,
                                SrcOperandAttr Src, bool Saturate) {
  llvm::StringRef Name = mnemonicOf(Op);
  Name.consume_back("_sat");
  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);

  // `mov` is a pure copy: its operand modifiers already did the work, and
  // DXBC gives it no type of its own, so keep whatever the source holds.
  if (Name == "mov") {
    // Every component is read before any is clamped, which is the order
    // the operand tokens are laid out in.
    llvm::SmallVector<llvm::Value *, 4> Values;
    for (unsigned Comp : Comps) {
      llvm::Value *Value =
          readSource(Src, Comp, movElementType(Dst, Src, Comp), Op);
      if (!Value)
        return false;
      Values.push_back(Value);
    }
    for (llvm::Value *&Value : Values)
      Value = saturate(Value, Saturate);
    return writeDestination(Dst, Values, Op, Src.getMinPrecision());
  }

  std::optional<OpLowering> Lowering = lookupLowering(Name);
  if (!Lowering) {
    unsupported(Op);
    return false;
  }

  auto [SrcTy, DstTy] = operationTypes(*Lowering, Dst, {Src});
  llvm::SmallVector<llvm::Value *, 4> Sources;
  for (unsigned Comp : Comps) {
    llvm::Value *Value = readSource(Src, Comp, SrcTy, Op);
    if (!Value)
      return false;
    Sources.push_back(Value);
  }

  llvm::SmallVector<llvm::Value *, 4> Values;
  for (llvm::Value *Source : Sources) {
    llvm::Value *Value;
    switch (Lowering->Kind) {
    case OpLowering::Form::Call:
      Value = emitDXOp(Lowering->Name, Lowering->Op, DstTy, {Source},
                       Lowering->NoOverload ? noOverload() : nullptr);
      break;
    case OpLowering::Form::Cast: {
      // A cast has no floating-point semantics of its own to relax.
      llvm::IRBuilderBase::FastMathFlagGuard Guard(Builder);
      Builder.clearFastMathFlags();
      Value = foldFloatToInt(Lowering->Cast, Source);
      if (!Value)
        Value = Builder.CreateCast(Lowering->Cast, Source, DstTy);
      break;
    }
    case OpLowering::Form::Reciprocal:
      Value = Builder.CreateFDiv(llvm::ConstantFP::get(DstTy, 1.0), Source);
      break;
    case OpLowering::Form::Negate:
      Value = Builder.CreateSub(llvm::ConstantInt::get(DstTy, 0), Source);
      break;
    default:
      unsupported(Op);
      return false;
    }
    Values.push_back(saturate(Value, Saturate));
  }
  return writeDestination(Dst, Values, Op);
}

bool Translator::translateBinary(mlir::Operation *Op, DstOperandAttr Dst,
                                 SrcOperandAttr Lhs, SrcOperandAttr Rhs,
                                 bool Saturate) {
  llvm::StringRef Name = mnemonicOf(Op);
  Name.consume_back("_sat");
  std::optional<OpLowering> Lowering = lookupLowering(Name);
  if (!Lowering) {
    unsupported(Op);
    return false;
  }

  SrcOperandAttr Operands[] = {Lhs, Rhs};
  auto [SrcTy, DstTy] = operationTypes(*Lowering, Dst, Operands);
  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);

  // Every source component is read before any result is computed, matching
  // how the operands are laid out in the instruction.
  llvm::SmallVector<llvm::Value *, 4> L, R;
  for (unsigned Comp : Comps) {
    llvm::Value *Value = readSource(Lhs, Comp, SrcTy, Op, /*Slot=*/0);
    if (!Value)
      return false;
    L.push_back(Value);
  }
  for (unsigned Comp : Comps) {
    llvm::Value *Value = readSource(Rhs, Comp, SrcTy, Op, /*Slot=*/1);
    if (!Value)
      return false;
    R.push_back(Value);
  }

  llvm::SmallVector<llvm::Value *, 4> Values;
  for (auto [Left, Right] : llvm::zip(L, R)) {
    llvm::Value *Value;
    switch (Lowering->Kind) {
    case OpLowering::Form::Native:
      // DXBC shifts use only the low five bits of the shift amount, where
      // LLVM leaves an out-of-range shift poison.
      if (Lowering->Binop == llvm::Instruction::Shl ||
          Lowering->Binop == llvm::Instruction::AShr ||
          Lowering->Binop == llvm::Instruction::LShr)
        Right = Builder.CreateAnd(Right, llvm::ConstantInt::get(i32Ty(), 31));
      Value = Builder.CreateBinOp(Lowering->Binop, Left, Right);
      break;
    case OpLowering::Form::Call:
      Value = emitDXOp(Lowering->Name, Lowering->Op, DstTy, {Left, Right});
      break;
    case OpLowering::Form::Compare: {
      llvm::Value *Cmp =
          Lowering->FloatOperands
              ? Builder.CreateFCmp(Lowering->Predicate, Left, Right)
              : Builder.CreateICmp(Lowering->Predicate, Left, Right);
      Value = Builder.CreateSExt(Cmp, i32Ty());
      break;
    }
    default:
      unsupported(Op);
      return false;
    }
    Values.push_back(saturate(Value, Saturate));
  }
  return writeDestination(Dst, Values, Op);
}

bool Translator::translateMad(mlir::Operation *Op, DstOperandAttr Dst,
                              SrcOperandAttr Lhs, SrcOperandAttr Rhs,
                              SrcOperandAttr Acc, bool Saturate) {
  llvm::StringRef Name = mnemonicOf(Op);
  bool Float = Name == "mad";
  DXILOp DXOp = Float            ? DXILOp::FMad
                : Name == "imad" ? DXILOp::IMad
                                 : DXILOp::UMad;
  llvm::Type *Ty = Float ? floatTy() : i32Ty();
  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);

  llvm::SmallVector<llvm::Value *, 12> Sources;
  SrcOperandAttr Operands[] = {Lhs, Rhs, Acc};
  for (auto [Slot, Src] : llvm::enumerate(Operands))
    for (unsigned Comp : Comps) {
      llvm::Value *Value = readSource(Src, Comp, Ty, Op, Slot);
      if (!Value)
        return false;
      Sources.push_back(Value);
    }

  llvm::SmallVector<llvm::Value *, 4> Values;
  for (unsigned I = 0, E = Comps.size(); I != E; ++I) {
    llvm::Value *Value = emitDXOp(
        "tertiary", DXOp, Ty, {Sources[I], Sources[E + I], Sources[2 * E + I]});
    Values.push_back(saturate(Value, Saturate));
  }
  return writeDestination(Dst, Values, Op);
}

bool Translator::translateVariadic(mlir::Operation *Op, DstOperandAttr Dst,
                                   llvm::ArrayRef<SrcOperandAttr> Sources,
                                   bool Saturate) {
  llvm::StringRef Name = mnemonicOf(Op);
  Name.consume_back("_sat");
  std::optional<OpLowering> Lowering = lookupLowering(Name);
  if (!Lowering || Lowering->Kind != OpLowering::Form::Call) {
    unsupported(Op);
    return false;
  }
  auto [SrcTy, DstTy] = operationTypes(*Lowering, Dst, Sources);
  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);

  // Every source component is read before any result is computed, matching
  // how the operands are laid out in the instruction.
  llvm::SmallVector<llvm::SmallVector<llvm::Value *, 4>, 4> Read;
  for (auto [Slot, Src] : llvm::enumerate(Sources)) {
    Read.emplace_back();
    for (unsigned Comp : Comps) {
      llvm::Value *Value = readSource(Src, Comp, SrcTy, Op, Slot);
      if (!Value)
        return false;
      Read.back().push_back(Value);
    }
  }

  llvm::SmallVector<llvm::Value *, 4> Values;
  for (unsigned I = 0, E = Comps.size(); I != E; ++I) {
    llvm::SmallVector<llvm::Value *, 4> Args;
    for (const auto &Source : Read)
      Args.push_back(Source[I]);
    Values.push_back(saturate(
        emitDXOp(Lowering->Name, Lowering->Op, DstTy, Args), Saturate));
  }
  return writeDestination(Dst, Values, Op);
}

bool Translator::translateDot(mlir::Operation *Op, DstOperandAttr Dst,
                              SrcOperandAttr Lhs, SrcOperandAttr Rhs,
                              unsigned Lanes, bool Saturate) {
  DXILOp DXOp = Lanes == 2   ? DXILOp::Dot2
                : Lanes == 3 ? DXILOp::Dot3
                             : DXILOp::Dot4;
  llvm::StringRef Name = Lanes == 2 ? "dot2" : Lanes == 3 ? "dot3" : "dot4";

  // A dot product reduces all `Lanes` components of both sources to one
  // scalar, which is then broadcast to every enabled destination component.
  llvm::SmallVector<llvm::Value *, 8> Args;
  SrcOperandAttr Operands[] = {Lhs, Rhs};
  for (auto [Slot, Src] : llvm::enumerate(Operands))
    for (unsigned Comp = 0; Comp < Lanes; ++Comp) {
      llvm::Value *Value = readSource(Src, Comp, floatTy(), Op, Slot);
      if (!Value)
        return false;
      Args.push_back(Value);
    }

  llvm::Value *Value =
      saturate(emitDXOp(Name, DXOp, floatTy(), Args), Saturate);
  llvm::SmallVector<llvm::Value *, 4> Values(destinationComponents(Dst).size(),
                                             Value);
  return writeDestination(Dst, Values, Op);
}

bool Translator::translateSincos(mlir::Operation *Op, DstOperandAttr Sin,
                                 DstOperandAttr Cos, SrcOperandAttr Src,
                                 bool Saturate) {
  // Both results are computed from the same source components, so the
  // union of the two write masks is read once up front.
  llvm::SmallVector<unsigned, 4> Wanted;
  for (DstOperandAttr Dst : {Sin, Cos}) {
    if (Dst.getType() == OperandType::null)
      continue;
    for (unsigned Comp : destinationComponents(Dst))
      if (!llvm::is_contained(Wanted, Comp))
        Wanted.push_back(Comp);
  }
  llvm::sort(Wanted);
  for (unsigned Comp : Wanted)
    if (!readSource(Src, Comp, floatTy(), Op))
      return false;

  for (auto [Dst, DXOp] :
       {std::make_pair(Sin, DXILOp::Sin), std::make_pair(Cos, DXILOp::Cos)}) {
    if (Dst.getType() == OperandType::null)
      continue;
    llvm::SmallVector<llvm::Value *, 4> Values;
    for (unsigned Comp : destinationComponents(Dst)) {
      llvm::Value *Source = readSource(Src, Comp, floatTy(), Op);
      if (!Source)
        return false;
      Values.push_back(
          saturate(emitDXOp("unary", DXOp, floatTy(), {Source}), Saturate));
    }
    if (!writeDestination(Dst, Values, Op))
      return false;
  }
  return true;
}

llvm::StructType *Translator::resRetTy(llvm::Type *Element) {
  llvm::StringRef Suffix = Element->isHalfTy()        ? "f16"
                           : Element->isFloatTy()     ? "f32"
                           : Element->isIntegerTy(16) ? "i16"
                                                      : "i32";
  std::string Name = ("dx.types.ResRet." + Suffix).str();
  if (auto *Existing = llvm::StructType::getTypeByName(Context, Name))
    return Existing;
  llvm::Type *Fields[] = {Element, Element, Element, Element, i32Ty()};
  return llvm::StructType::create(Context, Fields, Name);
}

bool Translator::readCoordinates(SrcOperandAttr Address, unsigned Count,
                                 unsigned Total,
                                 llvm::SmallVectorImpl<llvm::Value *> &Args,
                                 mlir::Operation *Op) {
  for (unsigned I = 0; I < Total; ++I) {
    if (I >= Count) {
      Args.push_back(llvm::UndefValue::get(floatTy()));
      continue;
    }
    llvm::Value *Value = readSource(Address, I, floatTy(), Op, /*Slot=*/0);
    if (!Value)
      return false;
    Args.push_back(Value);
  }
  return true;
}

bool Translator::writeResourceResult(DstOperandAttr Dst, SrcOperandAttr SRV,
                                     llvm::Value *Value, mlir::Operation *Op) {
  // A `_s` load whose shader wants only the mapping status names no
  // destination for the data.
  if (Dst.getType() == OperandType::null)
    return true;
  llvm::SmallVector<llvm::Value *, 4> Components;
  for (unsigned Comp : destinationComponents(Dst))
    Components.push_back(
        Builder.CreateExtractValue(Value, sourceComponent(SRV, Comp)));
  return writeDestination(Dst, Components, Op, Dst.getMinPrecision());
}

bool Translator::translateSample(mlir::Operation *Op, DstOperandAttr Dst,
                                 SrcOperandAttr Address, SrcOperandAttr SRV,
                                 SrcOperandAttr Sampler,
                                 SampleOffsetAttr Offset,
                                 const SampleForm &Form) {
  const Resource *Texture =
      findResource(ResourceClass::SRV, SRV.getIndex(), Op);
  if (!Texture)
    return false;
  llvm::Value *TextureHandle =
      resourceHandle(ResourceClass::SRV, SRV.getIndex(), Op, /*Slot=*/1,
                     /*Trailing=*/0, bool(SRV.getNonUniform()));
  llvm::Value *SamplerHandle =
      resourceHandle(ResourceClass::Sampler, Sampler.getIndex(), Op, /*Slot=*/2,
                     /*Trailing=*/0, bool(Sampler.getNonUniform()));
  if (!TextureHandle || !SamplerHandle)
    return false;

  llvm::SmallVector<llvm::Value *, 16> Args = {TextureHandle, SamplerHandle};
  bool IsLOD = Form.Op == DXILOp::CalculateLOD;
  if (!readCoordinates(Address,
                       IsLOD ? spatialCount(Texture->Kind)
                             : coordinateCount(Texture->Kind),
                       IsLOD ? 3 : 4, Args, Op))
    return false;

  // A texel offset slot the resource's dimensionality does not reach takes
  // `undef`, and one it does takes zero when the instruction named no
  // offsets. The two families disagree on where a cube map's dimensionality
  // stops: `gather4` treats a cube as having no offsets at all, where the
  // sampling operations count its three spatial coordinates -- which is
  // moot for the value passed, since no cube map accepts a non-zero offset.
  unsigned Offsets = Form.NarrowOffsets
                         ? std::min(offsetCount(Texture->Kind), 2u)
                         : spatialCount(Texture->Kind);
  unsigned Slots = Form.NarrowOffsets ? 2 : 3;
  if (!IsLOD) {
    int32_t Values[3] = {Offset ? Offset.getU() : 0, Offset ? Offset.getV() : 0,
                         Offset ? Offset.getW() : 0};
    for (unsigned I = 0; I < Slots; ++I) {
      if (I >= Offsets) {
        Args.push_back(llvm::UndefValue::get(i32Ty()));
        continue;
      }
      if (!Form.OffsetSource) {
        Args.push_back(llvm::ConstantInt::getSigned(i32Ty(), Values[I]));
        continue;
      }
      llvm::Value *Value = readSource(Form.OffsetSource, I, i32Ty(), Op,
                                      /*Slot=*/1);
      if (!Value)
        return false;
      Args.push_back(Value);
    }
  }

  // The derivatives are spatial, so a resource's array slice gets none.
  unsigned Derivatives = spatialCount(Texture->Kind);
  for (auto [Index, Src] : llvm::enumerate(Form.Gradients))
    for (unsigned I = 0; I < 3; ++I) {
      if (I >= Derivatives) {
        Args.push_back(llvm::UndefValue::get(floatTy()));
        continue;
      }
      llvm::Value *Value = readSource(Src, I, floatTy(), Op, Index + 5);
      if (!Value)
        return false;
      Args.push_back(Value);
    }
  // A gather names its channel before whatever else it appends, where a
  // comparing sample names its reference value first.
  if (Form.Channel)
    Args.push_back(
        llvm::ConstantInt::get(i32Ty(), sourceComponent(Sampler, 0)));
  for (SrcOperandAttr Src : Form.Extra) {
    llvm::Value *Value = readSource(Src, 0, floatTy(), Op, /*Slot=*/3);
    if (!Value)
      return false;
    Args.push_back(Value);
  }
  if (Form.ClampValue) {
    llvm::Value *Value =
        readSource(Form.ClampValue, 0, floatTy(), Op, /*Slot=*/4);
    if (!Value)
      return false;
    Args.push_back(Value);
  } else if (Form.Clamp) {
    Args.push_back(llvm::ConstantFP::get(floatTy(), 0.0));
  }
  if (IsLOD)
    Args.push_back(llvm::ConstantInt::get(i1Ty(), 1));

  llvm::Type *Element = resourceElementType(*Texture);
  if (IsLOD) {
    llvm::Value *Value = emitDXOp(Form.Name, Form.Op, floatTy(), Args);
    llvm::SmallVector<llvm::Value *, 4> Components(
        destinationComponents(Dst).size(), Value);
    return writeDestination(Dst, Components, Op);
  }
  llvm::Value *Value =
      emitDXOp(Form.Name, Form.Op, resRetTy(Element), Args, Element);
  if (!writeResourceResult(Dst, SRV, Value, Op))
    return false;
  return writeFeedbackStatus(Form.Feedback, Value, Op);
}

bool Translator::translateResourceLoad(
    mlir::Operation *Op, ResourceClass Class, DstOperandAttr Dst,
    SrcOperandAttr Address, SrcOperandAttr View, SampleOffsetAttr Offset,
    SrcOperandAttr SampleIndex, DstOperandAttr Feedback) {
  const Resource *Texture = findResource(Class, View.getIndex(), Op);
  if (!Texture)
    return false;
  llvm::Value *Handle =
      resourceHandle(Class, View.getIndex(), Op, /*Slot=*/1,
                     /*Trailing=*/0, bool(View.getNonUniform()));
  if (!Handle)
    return false;
  // An unordered access view is a single level with no texel offsets.
  bool Levelled = Class != ResourceClass::UAV;

  llvm::StringRef Name = "textureLoad";
  DXILOp DXOp = DXILOp::TextureLoad;
  llvm::SmallVector<llvm::Value *, 12> Args = {Handle};
  if (isBuffer(Texture->Kind)) {
    // A buffer has no mip level. It is addressed by one index, except for
    // a structured buffer, which names the element and the byte offset
    // within it separately.
    llvm::Value *Index = readSource(Address, 0, i32Ty(), Op);
    if (!Index)
      return false;
    Args.push_back(Index);
    llvm::Value *Offset = llvm::UndefValue::get(i32Ty());
    if (SampleIndex) {
      Offset = readSource(SampleIndex, 0, i32Ty(), Op, /*Slot=*/2);
      if (!Offset)
        return false;
    }
    Args.push_back(Offset);
    Name = "bufferLoad";
    DXOp = DXILOp::BufferLoad;
  } else {
    unsigned Coordinates = coordinateCount(Texture->Kind);
    llvm::SmallVector<llvm::Value *, 3> Coords;
    for (unsigned I = 0; I < 3; ++I) {
      if (I >= Coordinates) {
        Coords.push_back(llvm::UndefValue::get(i32Ty()));
        continue;
      }
      llvm::Value *Value = readSource(Address, I, i32Ty(), Op);
      if (!Value)
        return false;
      Coords.push_back(Value);
    }
    // `ldms` names its sample index in the slot from which `ld` reads a
    // mip level, which is the address's last component whatever the
    // resource's dimensionality.
    llvm::Value *Level = llvm::UndefValue::get(i32Ty());
    if (Levelled) {
      Level = SampleIndex ? readSource(SampleIndex, 0, i32Ty(), Op, /*Slot=*/2)
                          : readSource(Address, 3, i32Ty(), Op);
      if (!Level)
        return false;
    }
    Args.push_back(Level);
    llvm::append_range(Args, Coords);

    unsigned Offsets = Levelled ? offsetCount(Texture->Kind) : 0;
    int32_t Values[3] = {Offset ? Offset.getU() : 0, Offset ? Offset.getV() : 0,
                         Offset ? Offset.getW() : 0};
    for (unsigned I = 0; I < 3; ++I)
      Args.push_back(I < Offsets
                         ? llvm::ConstantInt::getSigned(i32Ty(), Values[I])
                         : llvm::UndefValue::get(i32Ty()));
  }

  // A destination register at minimum precision asks the load to return
  // its components already narrowed.
  llvm::Type *Element =
      storageType(Dst.getMinPrecision(), resourceElementType(*Texture));
  llvm::Value *Value = emitDXOp(Name, DXOp, resRetTy(Element), Args, Element);
  if (!writeResourceResult(Dst, View, Value, Op))
    return false;
  return writeFeedbackStatus(Feedback, Value, Op);
}

bool Translator::translateResourceStore(mlir::Operation *Op, DstOperandAttr UAV,
                                        SrcOperandAttr Address,
                                        SrcOperandAttr ByteOffset,
                                        SrcOperandAttr Value) {
  const Resource *View = findResource(ResourceClass::UAV, UAV.getIndex(), Op);
  if (!View)
    return false;
  llvm::Value *Handle =
      resourceHandle(ResourceClass::UAV, UAV.getIndex(), Op, /*Slot=*/0,
                     /*Trailing=*/0, /*NonUniform=*/false);
  if (!Handle)
    return false;

  llvm::SmallVector<llvm::Value *, 12> Args = {Handle};
  bool IsBuffer = isBuffer(View->Kind);
  if (IsBuffer) {
    llvm::Value *Index = readSource(Address, 0, i32Ty(), Op, /*Slot=*/1);
    if (!Index)
      return false;
    Args.push_back(Index);
    llvm::Value *Offset = llvm::UndefValue::get(i32Ty());
    if (ByteOffset) {
      Offset = readSource(ByteOffset, 0, i32Ty(), Op, /*Slot=*/2);
      if (!Offset)
        return false;
    }
    Args.push_back(Offset);
  } else {
    unsigned Coordinates = coordinateCount(View->Kind);
    for (unsigned I = 0; I < 3; ++I) {
      if (I >= Coordinates) {
        Args.push_back(llvm::UndefValue::get(i32Ty()));
        continue;
      }
      llvm::Value *Coord = readSource(Address, I, i32Ty(), Op, /*Slot=*/1);
      if (!Coord)
        return false;
      Args.push_back(Coord);
    }
  }

  // Four component slots travel with the write mask that selects the ones
  // reaching memory; the slots the mask leaves out are `undef`.
  unsigned Mask = 0;
  for (unsigned Comp : destinationComponents(UAV))
    Mask |= 1u << Comp;
  llvm::Type *Element =
      storageType(Value.getMinPrecision(), resourceElementType(*View));
  for (unsigned I = 0; I < 4; ++I) {
    if (!(Mask & (1u << I))) {
      Args.push_back(llvm::UndefValue::get(Element));
      continue;
    }
    llvm::Value *Component = readSource(Value, I, Element, Op, /*Slot=*/3);
    if (!Component)
      return false;
    Args.push_back(Component);
  }
  Args.push_back(llvm::ConstantInt::get(i8Ty(), Mask));

  emitDXOp(IsBuffer ? "bufferStore" : "textureStore",
           IsBuffer ? DXILOp::BufferStore : DXILOp::TextureStore,
           llvm::Type::getVoidTy(Context), Args, Element);
  return true;
}

bool Translator::writeFeedbackStatus(DstOperandAttr Feedback,
                                     llvm::Value *Result, mlir::Operation *Op) {
  if (!Feedback || Feedback.getType() == OperandType::null)
    return true;
  llvm::Value *Status = Builder.CreateExtractValue(Result, 4);
  llvm::SmallVector<llvm::Value *, 4> Components(
      destinationComponents(Feedback).size(), Status);
  return writeDestination(Feedback, Components, Op);
}

//===----------------------------------------------------------------------===//
// Control flow
//===----------------------------------------------------------------------===//
llvm::BasicBlock *Translator::deferredBlock(const llvm::Twine &Name) {
  auto *BB = llvm::BasicBlock::Create(Context, Name);
  Pending.push_back(BB);
  return BB;
}

void Translator::startBlock(llvm::BasicBlock *BB) {
  if (!BB->getParent()) {
    llvm::Function *Fn = Builder.GetInsertBlock()->getParent();
    Fn->insert(Fn->end(), BB);
    llvm::erase(Pending, BB);
  }
  Builder.SetInsertPoint(BB);
}

/// Returns whether \p BB already ends in a terminator, which it does when a
/// `break`, `continue` or `ret` cut the rest of the construct short.
static bool isTerminated(llvm::BasicBlock *BB) {
  return !BB->empty() && BB->back().isTerminator();
}

void Translator::branchTo(llvm::BasicBlock *BB) {
  if (!isTerminated(Builder.GetInsertBlock()))
    Builder.CreateBr(BB);
}

llvm::Value *Translator::readCondition(SrcOperandAttr Cond, bool TestNonZero,
                                       mlir::Operation *Op) {
  llvm::Value *Value = readSource(Cond, 0, i32Ty(), Op);
  if (!Value)
    return nullptr;
  return Builder.CreateICmp(TestNonZero ? llvm::CmpInst::ICMP_NE
                                        : llvm::CmpInst::ICMP_EQ,
                            Value, llvm::ConstantInt::get(i32Ty(), 0));
}

Translator::Scope *Translator::breakScope(mlir::Operation *Op) {
  for (Scope &S : llvm::reverse(Scopes))
    if (S.K == Scope::Kind::Loop || S.K == Scope::Kind::Switch)
      return &S;
  Op->emitError("'") << Op->getName().getStringRef()
                     << "' outside of a loop or switch";
  return nullptr;
}

Translator::Scope *Translator::loopScope(mlir::Operation *Op) {
  for (Scope &S : llvm::reverse(Scopes))
    if (S.K == Scope::Kind::Loop)
      return &S;
  Op->emitError("'") << Op->getName().getStringRef() << "' outside of a loop";
  return nullptr;
}

void Translator::openPendingCaseGroup() {
  Scope &S = Scopes.back();
  if (S.PendingCases.empty())
    return;
  llvm::BasicBlock *Group =
      deferredBlock("switch" + llvm::Twine(S.Id) + ".casegroup" +
                    llvm::Twine(S.CaseGroups++));
  startBlock(Group);
  for (llvm::ConstantInt *Value : S.PendingCases)
    S.Dispatch->addCase(Value, Group);
  S.PendingCases.clear();
}

bool Translator::translateControlFlow(mlir::Operation *Op, bool &Handled) {
  Handled = true;

  auto conditional = [&](bool TestNonZero) -> llvm::Value * {
    return readCondition(Op->getAttrOfType<SrcOperandAttr>("cond"), TestNonZero,
                         Op);
  };

  if (llvm::isa<dxsa::IfZ, dxsa::IfNz>(Op)) {
    llvm::Value *Cond = conditional(llvm::isa<dxsa::IfNz>(Op));
    if (!Cond)
      return false;
    Scope S;
    S.K = Scope::Kind::If;
    S.Id = IfCount++;
    llvm::BasicBlock *Then = deferredBlock("if" + llvm::Twine(S.Id) + ".then");
    // The false arm is the `else` block when one follows and the
    // construct's exit otherwise, which is only known at the `endif`.
    S.FalseBB = deferredBlock("if" + llvm::Twine(S.Id) + ".else");
    Builder.CreateCondBr(Cond, Then, S.FalseBB);
    Scopes.push_back(std::move(S));
    startBlock(Then);
    return true;
  }

  if (llvm::isa<dxsa::Else>(Op)) {
    if (Scopes.empty() || Scopes.back().K != Scope::Kind::If) {
      Op->emitError("'dxsa.else' outside of an if construct");
      return false;
    }
    Scope &S = Scopes.back();
    S.SawElse = true;
    S.EndBB = deferredBlock("if" + llvm::Twine(S.Id) + ".end");
    branchTo(S.EndBB);
    startBlock(S.FalseBB);
    return true;
  }

  if (llvm::isa<dxsa::Endif>(Op)) {
    if (Scopes.empty() || Scopes.back().K != Scope::Kind::If) {
      Op->emitError("'dxsa.endif' outside of an if construct");
      return false;
    }
    Scope S = Scopes.pop_back_val();
    if (!S.SawElse) {
      S.EndBB = S.FalseBB;
      S.EndBB->setName("if" + llvm::Twine(S.Id) + ".end");
    }
    branchTo(S.EndBB);
    startBlock(S.EndBB);
    return true;
  }

  if (llvm::isa<dxsa::Loop>(Op)) {
    Scope S;
    S.K = Scope::Kind::Loop;
    S.Id = LoopCount++;
    S.HeaderBB = deferredBlock("loop" + llvm::Twine(S.Id));
    S.EndBB = deferredBlock("loop" + llvm::Twine(S.Id) + ".end");
    branchTo(S.HeaderBB);
    Scopes.push_back(std::move(S));
    startBlock(Scopes.back().HeaderBB);
    return true;
  }

  if (llvm::isa<dxsa::Endloop>(Op)) {
    if (Scopes.empty() || Scopes.back().K != Scope::Kind::Loop) {
      Op->emitError("'dxsa.endloop' outside of a loop");
      return false;
    }
    Scope S = Scopes.pop_back_val();
    branchTo(S.HeaderBB);
    startBlock(S.EndBB);
    return true;
  }

  if (llvm::isa<dxsa::Break>(Op)) {
    Scope *S = breakScope(Op);
    if (!S)
      return false;
    // A `break` that just closes a switch case falls out of the construct
    // anyway, and does not name a block of its own; only one that cuts a
    // case short does, which is what the counter numbers.
    mlir::Operation *Next = Op->getNextNode();
    if (!Next || !llvm::isa<dxsa::Case, dxsa::Default, dxsa::Endswitch>(Next))
      ++S->Breaks;
    branchTo(S->EndBB);
    // Anything up to the end of the construct is unreachable, but still has
    // to be translated somewhere.
    startBlock(deferredBlock("afterbreak"));
    return true;
  }

  if (llvm::isa<dxsa::BreakcZ, dxsa::BreakcNz>(Op)) {
    Scope *S = breakScope(Op);
    if (!S)
      return false;
    llvm::Value *Cond = conditional(llvm::isa<dxsa::BreakcNz>(Op));
    if (!Cond)
      return false;
    // Materialize to a `std::string` immediately (`.str()`), rather than
    // storing the `llvm::Twine` expression itself in a local variable:
    // `Twine`'s own concatenation nodes hold pointers to their operand
    // sub-`Twine`s, which are temporaries destroyed at the end of this
    // full expression -- storing the result and using it afterward (as
    // `deferredBlock(Name)` below does) is a stack-use-after-scope,
    // confirmed by a real ASan failure translating loop1.dxasm.
    std::string Name =
        (S->K == Scope::Kind::Loop
             ? "loop" + llvm::Twine(S->Id) + ".breakc" + llvm::Twine(S->Breaks)
             : "switch" + llvm::Twine(S->Id) + ".break" +
                   llvm::Twine(S->Breaks))
            .str();
    ++S->Breaks;
    llvm::BasicBlock *Fallthrough = deferredBlock(Name);
    Builder.CreateCondBr(Cond, S->EndBB, Fallthrough);
    startBlock(Fallthrough);
    return true;
  }

  if (llvm::isa<dxsa::Continue>(Op)) {
    Scope *S = loopScope(Op);
    if (!S)
      return false;
    ++S->Continues;
    branchTo(S->HeaderBB);
    startBlock(deferredBlock("aftercontinue"));
    return true;
  }

  if (llvm::isa<dxsa::ContinuecZ, dxsa::ContinuecNz>(Op)) {
    Scope *S = loopScope(Op);
    if (!S)
      return false;
    llvm::Value *Cond = conditional(llvm::isa<dxsa::ContinuecNz>(Op));
    if (!Cond)
      return false;
    llvm::BasicBlock *Fallthrough = deferredBlock(
        "loop" + llvm::Twine(S->Id) + ".continuec" + llvm::Twine(S->Continues));
    ++S->Continues;
    Builder.CreateCondBr(Cond, S->HeaderBB, Fallthrough);
    startBlock(Fallthrough);
    return true;
  }

  if (auto Switch = llvm::dyn_cast<dxsa::Switch>(Op)) {
    llvm::Value *Selector = readSource(Switch.getSelector(), 0, i32Ty(), Op);
    if (!Selector)
      return false;
    Scope S;
    S.K = Scope::Kind::Switch;
    S.Id = SwitchCount++;
    S.EndBB = deferredBlock("switch" + llvm::Twine(S.Id) + ".end");
    S.DefaultBB = deferredBlock("switch" + llvm::Twine(S.Id) + ".default");
    S.Dispatch = Builder.CreateSwitch(Selector, S.DefaultBB);
    Scopes.push_back(std::move(S));
    // No block is open until the first `case` or `default`; the dispatch
    // itself terminated the one the switch was reached in.
    startBlock(deferredBlock("beforecase"));
    return true;
  }

  if (auto Case = llvm::dyn_cast<dxsa::Case>(Op)) {
    if (Scopes.empty() || Scopes.back().K != Scope::Kind::Switch) {
      Op->emitError("'dxsa.case' outside of a switch construct");
      return false;
    }
    llvm::ArrayRef<int32_t> Values =
        Case.getOperand().getValues32().asArrayRef();
    if (Values.empty()) {
      Op->emitError("'dxsa.case' without a label");
      return false;
    }
    // Consecutive `case`s share one block, so the group is not opened until
    // an instruction other than a `case` follows.
    Scopes.back().PendingCases.push_back(llvm::ConstantInt::get(
        llvm::cast<llvm::IntegerType>(i32Ty()), uint32_t(Values[0])));
    return true;
  }

  if (llvm::isa<dxsa::Default>(Op)) {
    if (Scopes.empty() || Scopes.back().K != Scope::Kind::Switch) {
      Op->emitError("'dxsa.default' outside of a switch construct");
      return false;
    }
    Scope &S = Scopes.back();
    startBlock(S.DefaultBB);
    S.DefaultBB = nullptr;
    return true;
  }

  if (llvm::isa<dxsa::Endswitch>(Op)) {
    if (Scopes.empty() || Scopes.back().K != Scope::Kind::Switch) {
      Op->emitError("'dxsa.endswitch' outside of a switch construct");
      return false;
    }
    Scope S = Scopes.pop_back_val();
    // A switch with no `default` falls out of the construct instead.
    if (S.DefaultBB) {
      S.Dispatch->setDefaultDest(S.EndBB);
      llvm::erase(Pending, S.DefaultBB);
      delete S.DefaultBB;
    }
    branchTo(S.EndBB);
    startBlock(S.EndBB);
    return true;
  }

  if (llvm::isa<dxsa::RetcZ, dxsa::RetcNz>(Op)) {
    llvm::Value *Cond = conditional(llvm::isa<dxsa::RetcNz>(Op));
    if (!Cond)
      return false;
    unsigned Id = RetcCount++;
    llvm::BasicBlock *Return = deferredBlock("retc" + llvm::Twine(Id));
    llvm::BasicBlock *Fallthrough =
        deferredBlock("afterretc" + llvm::Twine(Id));
    Builder.CreateCondBr(Cond, Return, Fallthrough);
    startBlock(Return);
    Builder.CreateRetVoid();
    startBlock(Fallthrough);
    return true;
  }

  if (llvm::isa<dxsa::DiscardZ, dxsa::DiscardNz>(Op)) {
    llvm::Value *Cond = conditional(llvm::isa<dxsa::DiscardNz>(Op));
    if (!Cond)
      return false;
    emitDXOp("discard", DXILOp::Discard, llvm::Type::getVoidTy(Context),
             {Cond});
    return true;
  }

  if (llvm::isa<dxsa::Ret>(Op)) {
    Builder.CreateRetVoid();
    startBlock(deferredBlock("afterret"));
    return true;
  }

  Handled = false;
  return true;
}

bool Translator::translateMovC(mlir::Operation *Op, DstOperandAttr Dst,
                               SrcOperandAttr Cond, SrcOperandAttr True,
                               SrcOperandAttr False, bool Saturate) {
  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);
  llvm::SmallVector<llvm::Value *, 4> Values;
  for (unsigned Comp : Comps) {
    llvm::Value *Test = readSource(Cond, Comp, i32Ty(), Op);
    if (!Test)
      return false;
    // A conditional move copies bits, so it takes its type from whichever
    // source has one, exactly like `mov`.
    llvm::Type *Ty = movElementType(Dst, True, Comp);
    if (Ty == floatTy())
      Ty = movElementType(Dst, False, Comp);
    llvm::Value *Selected =
        Builder.CreateICmpNE(Test, llvm::ConstantInt::get(i32Ty(), 0));
    llvm::Value *Left = readSource(True, Comp, Ty, Op, /*Slot=*/1);
    llvm::Value *Right = readSource(False, Comp, Ty, Op, /*Slot=*/2);
    if (!Left || !Right)
      return false;
    // Choosing between two values has no floating-point semantics to relax.
    llvm::IRBuilderBase::FastMathFlagGuard Guard(Builder);
    Builder.clearFastMathFlags();
    Values.push_back(
        saturate(Builder.CreateSelect(Selected, Left, Right), Saturate));
  }
  return writeDestination(Dst, Values, Op);
}

bool Translator::translateInstruction(mlir::Operation *Op) {
  SourceCache.clear();
  RowCache.clear();
  HandleCache.clear();
  SaturateCache.clear();
  llvm::StringRef Name = mnemonicOf(Op);
  bool Saturate = Name.consume_back("_sat");

  // `precise` names the destination components whose arithmetic may not be
  // reassociated; dxilconv applies it to the whole instruction.
  auto Mask = Op->getAttrOfType<ComponentMaskAttr>("precise");
  llvm::SaveAndRestore<bool> IsPrecise(
      Precise, Mask && static_cast<unsigned>(Mask.getValue()) != 0);
  llvm::IRBuilderBase::FastMathFlagGuard Relaxed(Builder);
  if (Precise)
    Builder.clearFastMathFlags();

  // Declarations carry no code.
  if (Name.starts_with("dcl_"))
    return true;

  // Consecutive `case`s share one block, so the group they name is opened
  // by the first instruction that is not itself a `case`.
  if (!Scopes.empty() && Scopes.back().K == Scope::Kind::Switch &&
      !llvm::isa<dxsa::Case>(Op))
    openPendingCaseGroup();

  bool Handled = false;
  if (!translateControlFlow(Op, Handled))
    return false;
  if (Handled)
    return true;

  if (auto Sel = llvm::dyn_cast<dxsa::MovC>(Op))
    return translateMovC(Op, Sel.getDst(), Sel.getCondition(), Sel.getSrc1(),
                         Sel.getSrc2(), Saturate);
  if (auto Sel = llvm::dyn_cast<dxsa::MovCSat>(Op))
    return translateMovC(Op, Sel.getDst(), Sel.getCondition(), Sel.getSrc1(),
                         Sel.getSrc2(), Saturate);

  if (auto SC = llvm::dyn_cast<dxsa::Sincos>(Op))
    return translateSincos(Op, SC.getSin(), SC.getCos(), SC.getOperandAttr(),
                           Saturate);
  if (auto SC = llvm::dyn_cast<dxsa::SincosSat>(Op))
    return translateSincos(Op, SC.getSin(), SC.getCos(), SC.getOperandAttr(),
                           Saturate);

  if (auto Mad = llvm::dyn_cast<dxsa::Mad>(Op))
    return translateMad(Op, Mad.getDst(), Mad.getLhs(), Mad.getRhs(),
                        Mad.getAcc(), Saturate);
  if (auto Mad = llvm::dyn_cast<dxsa::Imad>(Op))
    return translateMad(Op, Mad.getDst(), Mad.getLhs(), Mad.getRhs(),
                        Mad.getAcc(), Saturate);
  if (auto Mad = llvm::dyn_cast<dxsa::UMad>(Op))
    return translateMad(Op, Mad.getDst(), Mad.getLhs(), Mad.getRhs(),
                        Mad.getAcc(), Saturate);
  if (auto S = llvm::dyn_cast<dxsa::Sample>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::Sample;
    Form.Name = "sample";
    Form.Clamp = true;
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleClampFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::Sample;
    Form.Name = "sample";
    Form.Clamp = true;
    Form.ClampValue = S.getClampFeedback().getLodClamp();
    Form.Feedback = S.getClampFeedback().getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleL>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleLevel;
    Form.Name = "sampleLevel";
    Form.Extra = {S.getSrcLod()};
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleLFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleLevel;
    Form.Name = "sampleLevel";
    Form.Extra = {S.getSrcLod()};
    Form.Feedback = S.getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleB>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleBias;
    Form.Name = "sampleBias";
    Form.Extra = {S.getSrcLodBias()};
    Form.Clamp = true;
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleBClampFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleBias;
    Form.Name = "sampleBias";
    Form.Extra = {S.getSrcLodBias()};
    Form.Clamp = true;
    Form.ClampValue = S.getClampFeedback().getLodClamp();
    Form.Feedback = S.getClampFeedback().getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleD>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleGrad;
    Form.Name = "sampleGrad";
    Form.Clamp = true;
    Form.Gradients = {S.getSrcXDerivatives(), S.getSrcYDerivatives()};
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleDClampFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleGrad;
    Form.Name = "sampleGrad";
    Form.Clamp = true;
    Form.Gradients = {S.getSrcXDerivatives(), S.getSrcYDerivatives()};
    Form.ClampValue = S.getClampFeedback().getLodClamp();
    Form.Feedback = S.getClampFeedback().getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleC>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleCmp;
    Form.Name = "sampleCmp";
    Form.Extra = {S.getSrcReferenceValue()};
    Form.Clamp = true;
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleCClampFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleCmp;
    Form.Name = "sampleCmp";
    Form.Extra = {S.getSrcReferenceValue()};
    Form.Clamp = true;
    Form.ClampValue = S.getClampFeedback().getLodClamp();
    Form.Feedback = S.getClampFeedback().getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleCLZ>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleCmpLevelZero;
    Form.Name = "sampleCmpLevelZero";
    Form.Extra = {S.getSrcReferenceValue()};
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::SampleCLZFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::SampleCmpLevelZero;
    Form.Name = "sampleCmpLevelZero";
    Form.Extra = {S.getSrcReferenceValue()};
    Form.Feedback = S.getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::Gather4>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::TextureGather;
    Form.Name = "textureGather";
    Form.Channel = true;
    Form.NarrowOffsets = true;
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::Gather4Feedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::TextureGather;
    Form.Name = "textureGather";
    Form.Channel = true;
    Form.NarrowOffsets = true;
    Form.Feedback = S.getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::Gather4C>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::TextureGatherCmp;
    Form.Name = "textureGatherCmp";
    Form.Extra = {S.getSrcReferenceValue()};
    Form.Channel = true;
    Form.NarrowOffsets = true;
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::Gather4CFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::TextureGatherCmp;
    Form.Name = "textureGatherCmp";
    Form.Extra = {S.getSrcReferenceValue()};
    Form.Channel = true;
    Form.NarrowOffsets = true;
    Form.Feedback = S.getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           S.getOffset().value_or(SampleOffsetAttr()), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::Gather4PO>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::TextureGather;
    Form.Name = "textureGather";
    Form.Channel = true;
    Form.NarrowOffsets = true;
    Form.OffsetSource = S.getSrcOffset();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           SampleOffsetAttr(), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::Gather4POFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::TextureGather;
    Form.Name = "textureGather";
    Form.Channel = true;
    Form.NarrowOffsets = true;
    Form.OffsetSource = S.getSrcOffset();
    Form.Feedback = S.getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           SampleOffsetAttr(), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::Gather4POC>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::TextureGatherCmp;
    Form.Name = "textureGatherCmp";
    Form.Extra = {S.getSrcReferenceValue()};
    Form.Channel = true;
    Form.NarrowOffsets = true;
    Form.OffsetSource = S.getSrcOffset();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           SampleOffsetAttr(), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::Gather4POCFeedback>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::TextureGatherCmp;
    Form.Name = "textureGatherCmp";
    Form.Extra = {S.getSrcReferenceValue()};
    Form.Channel = true;
    Form.NarrowOffsets = true;
    Form.OffsetSource = S.getSrcOffset();
    Form.Feedback = S.getFeedback();
    return translateSample(Op, S.getDst(), S.getSrcAddress(),
                           S.getSrcResource(), S.getSrcSampler(),
                           SampleOffsetAttr(), Form);
  }
  if (auto S = llvm::dyn_cast<dxsa::LOD>(Op)) {
    SampleForm Form;
    Form.Op = DXILOp::CalculateLOD;
    Form.Name = "calculateLOD";
    return translateSample(Op, S.getDst(), S.getSrc0(), S.getSrc1(),
                           S.getSrc2(), SampleOffsetAttr(), Form);
  }
  if (auto L = llvm::dyn_cast<dxsa::Ld>(Op))
    return translateResourceLoad(Op, ResourceClass::SRV, L.getDst(),
                                 L.getSrcAddress(), L.getSrcResource(),
                                 L.getOffset().value_or(SampleOffsetAttr()),
                                 SrcOperandAttr(), DstOperandAttr());
  if (auto L = llvm::dyn_cast<dxsa::LdFeedback>(Op))
    return translateResourceLoad(Op, ResourceClass::SRV, L.getDst(),
                                 L.getSrcAddress(), L.getSrcResource(),
                                 L.getOffset().value_or(SampleOffsetAttr()),
                                 SrcOperandAttr(), L.getFeedback());
  if (auto L = llvm::dyn_cast<dxsa::Ld2dms>(Op))
    return translateResourceLoad(Op, ResourceClass::SRV, L.getDst(),
                                 L.getSrcAddress(), L.getSrcResource(),
                                 L.getOffset().value_or(SampleOffsetAttr()),
                                 L.getSampleIndex(), DstOperandAttr());
  if (auto L = llvm::dyn_cast<dxsa::Ld2dmsFeedback>(Op))
    return translateResourceLoad(Op, ResourceClass::SRV, L.getDst(),
                                 L.getSrcAddress(), L.getSrcResource(),
                                 L.getOffset().value_or(SampleOffsetAttr()),
                                 L.getSampleIndex(), L.getFeedback());
  if (auto L = llvm::dyn_cast<dxsa::LdUavTyped>(Op))
    return translateResourceLoad(
        Op, ResourceClass::UAV, L.getDst(), L.getSrcAddress(), L.getSrcUav(),
        SampleOffsetAttr(), SrcOperandAttr(), DstOperandAttr());
  if (auto L = llvm::dyn_cast<dxsa::LdUavTypedFeedback>(Op))
    return translateResourceLoad(
        Op, ResourceClass::UAV, L.getDst(), L.getSrcAddress(), L.getSrcUav(),
        SampleOffsetAttr(), SrcOperandAttr(), L.getFeedback());
  if (auto S = llvm::dyn_cast<dxsa::StoreUavTyped>(Op))
    return translateResourceStore(Op, S.getDstUav(), S.getSrcAddress(),
                                  SrcOperandAttr(), S.getSrcValue());
  if (auto S = llvm::dyn_cast<dxsa::StoreRaw>(Op))
    return translateResourceStore(Op, S.getDst(), S.getSrcByteOffset(),
                                  SrcOperandAttr(), S.getSrcValue());
  if (auto S = llvm::dyn_cast<dxsa::StoreStructured>(Op))
    return translateResourceStore(Op, S.getDst(), S.getSrcAddress(),
                                  S.getSrcByteOffset(), S.getSrcValue());
  if (auto L = llvm::dyn_cast<dxsa::LdRaw>(Op))
    return translateResourceLoad(
        Op, viewClass(L.getSrc()), L.getDst(), L.getSrcByteOffset(), L.getSrc(),
        SampleOffsetAttr(), SrcOperandAttr(), DstOperandAttr());
  if (auto L = llvm::dyn_cast<dxsa::LdRawFeedback>(Op))
    return translateResourceLoad(
        Op, viewClass(L.getSrc()), L.getDst(), L.getSrcByteOffset(), L.getSrc(),
        SampleOffsetAttr(), SrcOperandAttr(), L.getFeedback());
  if (auto L = llvm::dyn_cast<dxsa::LdStructured>(Op))
    return translateResourceLoad(
        Op, viewClass(L.getSrc()), L.getDst(), L.getSrcAddress(), L.getSrc(),
        SampleOffsetAttr(), L.getSrcByteOffset(), DstOperandAttr());
  if (auto L = llvm::dyn_cast<dxsa::LdStructuredFeedback>(Op))
    return translateResourceLoad(
        Op, viewClass(L.getSrc()), L.getDst(), L.getSrcAddress(), L.getSrc(),
        SampleOffsetAttr(), L.getSrcByteOffset(), L.getFeedback());
  if (auto Check = llvm::dyn_cast<dxsa::CheckAccessFullyMapped>(Op)) {
    llvm::Value *Status = readSource(Check.getSrc(), 0, i32Ty(), Op);
    if (!Status)
      return false;
    llvm::Value *Mapped =
        emitDXOp("checkAccessFullyMapped", DXILOp::CheckAccessFullyMapped,
                 i1Ty(), {Status}, i32Ty());
    // The result is a condition, and DXBC spells a condition as a mask.
    llvm::Value *Mask = Builder.CreateSExt(Mapped, i32Ty());
    llvm::SmallVector<llvm::Value *, 4> Components(
        destinationComponents(Check.getDst()).size(), Mask);
    return writeDestination(Check.getDst(), Components, Op);
  }

  if (auto Bits = llvm::dyn_cast<dxsa::UBFE>(Op)) {
    SrcOperandAttr Sources[] = {Bits.getSrc0(), Bits.getSrc1(), Bits.getSrc2()};
    return translateVariadic(Op, Bits.getDst(), Sources, Saturate);
  }
  if (auto Bits = llvm::dyn_cast<dxsa::IBFE>(Op)) {
    SrcOperandAttr Sources[] = {Bits.getSrc0(), Bits.getSrc1(), Bits.getSrc2()};
    return translateVariadic(Op, Bits.getDst(), Sources, Saturate);
  }
  if (auto Dot = llvm::dyn_cast<dxsa::Dp2>(Op))
    return translateDot(Op, Dot.getDst(), Dot.getLhs(), Dot.getRhs(), 2,
                        Saturate);
  if (auto Dot = llvm::dyn_cast<dxsa::Dp3>(Op))
    return translateDot(Op, Dot.getDst(), Dot.getLhs(), Dot.getRhs(), 3,
                        Saturate);
  if (auto Dot = llvm::dyn_cast<dxsa::Dp4>(Op))
    return translateDot(Op, Dot.getDst(), Dot.getLhs(), Dot.getRhs(), 4,
                        Saturate);

  // The remaining handled opcodes are plain unary/binary shapes, which the
  // dialect models with a uniform attribute layout.
  if (Op->getNumOperands() == 0) {
    if (auto Dst = Op->getAttrOfType<DstOperandAttr>("dst")) {
      if (auto Src = Op->getAttrOfType<SrcOperandAttr>("src"))
        return translateUnary(Op, Dst, Src, Saturate);
      auto Lhs = Op->getAttrOfType<SrcOperandAttr>("lhs");
      auto Rhs = Op->getAttrOfType<SrcOperandAttr>("rhs");
      if (Lhs && Rhs)
        return translateBinary(Op, Dst, Lhs, Rhs, Saturate);
    }
  }
  unsupported(Op);
  return false;
}

bool Translator::translateBody(dxsa::ModuleOp Shader) {
  for (mlir::Operation &Op : *Shader.getBodyBlock())
    if (!translateInstruction(&Op))
      return false;
  // A DXBC program need not end in `ret`.
  if (!isTerminated(Builder.GetInsertBlock()))
    Builder.CreateRetVoid();
  return true;
}

//===----------------------------------------------------------------------===//
// Cleanup
//===----------------------------------------------------------------------===//

void Translator::promoteTemps(llvm::Function &Entry) {
  llvm::SmallVector<std::pair<uint64_t, llvm::AllocaInst *>, 16> Ordered(
      Temps.begin(), Temps.end());
  if (Ordered.empty())
    return;
  // Promote in slot order so that the phi nodes of a block, which mem2reg
  // creates one alloca at a time, come out in a stable order.
  llvm::sort(Ordered, llvm::less_first());
  llvm::SmallVector<llvm::AllocaInst *, 16> Slots;
  for (auto [Key, Slot] : Ordered)
    Slots.push_back(Slot);
  llvm::DominatorTree DT(Entry);
  llvm::PromoteMemToReg(Slots, DT);
  Temps.clear();
}

/// Returns the value \p I reinterprets, when it is one of DXIL's bitcast
/// operations, or null when it is not.
static llvm::Value *bitcastSource(llvm::Instruction *I) {
  auto *Call = llvm::dyn_cast<llvm::CallInst>(I);
  if (!Call || Call->arg_size() != 2 || !Call->getCalledFunction() ||
      !Call->getCalledFunction()->getName().starts_with("dx.op.bitcast"))
    return nullptr;
  return Call->getArgOperand(1);
}

void Translator::foldConditionMasks(llvm::Function &Entry) {
  // A set, because one widened comparison can feed several tests, and each
  // of them nominates it for removal.
  llvm::SmallSetVector<llvm::Instruction *, 8> Dead;

  // A value stored into a temp register of the other type and read back
  // comes out unchanged; the pair only existed because the slot's type had
  // to be picked once for the whole shader.
  for (llvm::BasicBlock &BB : Entry) {
    for (llvm::Instruction &I : llvm::make_early_inc_range(BB)) {
      llvm::Value *Source = bitcastSource(&I);
      if (!Source)
        continue;
      // Promoting the temp registers can turn the load a reinterpretation
      // was emitted for into a literal, which has no runtime
      // representation to reinterpret.
      if (auto *Literal = llvm::dyn_cast<llvm::Constant>(Source)) {
        I.replaceAllUsesWith(
            llvm::ConstantExpr::getBitCast(Literal, I.getType()));
        I.eraseFromParent();
        continue;
      }
      auto *Inner = llvm::dyn_cast<llvm::Instruction>(Source);
      if (!Inner)
        continue;
      llvm::Value *Original = bitcastSource(Inner);
      if (!Original || Original->getType() != I.getType())
        continue;
      I.replaceAllUsesWith(Original);
      I.eraseFromParent();
      Dead.insert(Inner);
    }
  }
  for (llvm::Instruction *I : Dead)
    if (I->use_empty())
      I->eraseFromParent();
  Dead.clear();

  // A resource is bound at the entry point for every declaration, but one
  // only ever accessed through a run-time binding never uses that handle.
  // An indexable temp is allocated at every element type it might be
  // accessed at, and only the ones something did access are kept.
  for (llvm::Instruction &I :
       llvm::make_early_inc_range(Entry.getEntryBlock())) {
    if (!I.use_empty())
      continue;
    if (llvm::isa<llvm::AllocaInst>(&I)) {
      I.eraseFromParent();
      continue;
    }
    auto *Call = llvm::dyn_cast<llvm::CallInst>(&I);
    if (Call && Call->getCalledFunction() &&
        Call->getCalledFunction()->getName() == "dx.op.createHandle")
      Call->eraseFromParent();
  }

  // A DXBC instruction computes every component its write mask names, and
  // nothing has to read them all -- a swizzle can leave a component of the
  // register it wrote unreachable. dxilconv drops what such a component
  // computed, so a shader's value numbering only counts what it uses.
  for (llvm::BasicBlock &BB : Entry)
    for (llvm::Instruction &I : llvm::make_early_inc_range(BB))
      llvm::RecursivelyDeleteTriviallyDeadInstructions(&I);

  for (llvm::BasicBlock &BB : Entry) {
    for (llvm::Instruction &I : llvm::make_early_inc_range(BB)) {
      auto *Compare = llvm::dyn_cast<llvm::ICmpInst>(&I);
      if (!Compare || !Compare->isEquality())
        continue;
      auto *Zero = llvm::dyn_cast<llvm::ConstantInt>(Compare->getOperand(1));
      auto *Mask = llvm::dyn_cast<llvm::SExtInst>(Compare->getOperand(0));
      if (!Zero || !Zero->isZero() || !Mask ||
          !Mask->getSrcTy()->isIntegerTy(1))
        continue;
      llvm::Value *Bit = Mask->getOperand(0);
      if (Compare->getPredicate() == llvm::CmpInst::ICMP_EQ) {
        Builder.SetInsertPoint(Compare);
        Bit = Builder.CreateNot(Bit);
      }
      Compare->replaceAllUsesWith(Bit);
      Compare->eraseFromParent();
      Dead.insert(Mask);
    }
  }
  // The sign extension only existed to widen the comparison to a DXBC
  // boolean; dropping it is safe when nothing else reads the mask.
  for (llvm::Instruction *I : Dead)
    if (I->use_empty())
      I->eraseFromParent();
}

//===----------------------------------------------------------------------===//
// Metadata
//===----------------------------------------------------------------------===//

llvm::MDNode *Translator::emitResourceBindings(ResourceClass Class) {
  llvm::SmallVector<Resource *, 8> Bound;
  for (auto &[Key, R] : Resources)
    if (R.Class == Class)
      Bound.push_back(&R);
  if (Bound.empty())
    return nullptr;
  llvm::sort(Bound, [](const Resource *L, const Resource *R) {
    return L->Range < R->Range;
  });

  llvm::SmallVector<llvm::Metadata *, 8> Entries;
  for (const Resource *R : Bound) {
    auto *Symbol = llvm::UndefValue::get(
        llvm::PointerType::get(Context, Class == ResourceClass::CBV ? 2 : 1));
    llvm::SmallVector<llvm::Metadata *, 9> Fields = {
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i32Ty(), R->Range)),
        llvm::ConstantAsMetadata::get(Symbol),
        llvm::MDString::get(Context, R->Name),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i32Ty(), R->Space)),
        llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), R->Bind)),
        llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), R->Size)),
    };
    if (Class == ResourceClass::CBV) {
      // A constant buffer's last field is its size in bytes.
      Fields.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(i32Ty(), R->Stride)));
      Fields.push_back(nullptr);
    } else if (Class == ResourceClass::Sampler) {
      // A sampler's is its kind: 0 for the default, 1 for comparison.
      Fields.push_back(
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 0)));
      Fields.push_back(nullptr);
    } else {
      Fields.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(i32Ty(), unsigned(R->Kind))));
      if (Class == ResourceClass::UAV) {
        // A UAV's shape metadata carries three extra `i1` flags after its
        // kind that an SRV's does not (see `ResourceInfo::write` in
        // llvm/lib/Analysis/DXILResource.cpp): globally-coherent, whether it
        // has an associated counter, and rasterizer-ordered. `dxsa`'s
        // `dcl_uav_*` opcodes do parse the equivalent access-flag modifiers
        // (`globallyCoherent`, `hasOrderPreservingCounter`,
        // `rasterizerOrdered`, see dxbc-as.md's "Operands" section), but
        // this translation does not yet read them off the declaration (see
        // agent_thoughts.md); emitting `false` for all three matches every
        // shader this translation currently handles, none of which uses
        // those flags, and -- critically -- keeps this metadata node's
        // shape the 11 operands `feme::dxil::ResourceMetadata` (and DXIL
        // consumers generally) require for a UAV, unlike the shorter
        // (SRV-shaped) 9-operand node this emitted before, which no
        // `dx.op.createHandle` referencing an SRV-shaped UAV binding could
        // ever be raised back out of.
        llvm::Metadata *False =
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::getFalse(Context));
        Fields.push_back(False);
        Fields.push_back(False);
        Fields.push_back(False);
      } else {
        Fields.push_back(llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i32Ty(), R->SampleCount)));
      }
      // Tag 0 names the element type a typed resource returns.
      Fields.push_back(llvm::MDNode::get(
          Context,
          {llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 0)),
           llvm::ConstantAsMetadata::get(
               llvm::ConstantInt::get(i32Ty(), unsigned(R->Component)))}));
    }
    Entries.push_back(llvm::MDNode::get(Context, Fields));
  }
  return llvm::MDNode::get(Context, Entries);
}

llvm::MDNode *Translator::emitSignature(const Signature &Sig) {
  if (Sig.empty())
    return nullptr;
  llvm::SmallVector<llvm::Metadata *, 8> Elements;
  for (auto [Index, Element] : llvm::enumerate(Sig.elements())) {
    llvm::SmallVector<llvm::Metadata *, 4> Indices;
    for (unsigned SemanticIndex : Element.SemanticIndices)
      Indices.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(i32Ty(), SemanticIndex)));
    auto *SemanticIndices = llvm::MDNode::get(Context, Indices);
    llvm::Metadata *Fields[] = {
        llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), Index)),
        llvm::MDString::get(Context, Element.Name),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i8Ty(), unsigned(Element.Type))),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i8Ty(), unsigned(Element.Kind))),
        SemanticIndices,
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i8Ty(), Element.InterpolationMode)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i32Ty(), Element.Rows)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i8Ty(), Element.Cols)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i32Ty(), Element.Row)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i8Ty(), Element.StartCol & 0xFF)),
        nullptr,
    };
    Elements.push_back(llvm::MDNode::get(Context, Fields));
  }
  return llvm::MDNode::get(Context, Elements);
}

void Translator::emitMetadata(llvm::Function *Entry) {
  auto *Version = llvm::MDNode::get(
      Context,
      {llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 1)),
       llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 0))});
  Module->getOrInsertNamedMetadata("dx.version")->addOperand(Version);
  Module->getOrInsertNamedMetadata("dx.valver")->addOperand(Version);
  Module->getOrInsertNamedMetadata("dx.shaderModel")
      ->addOperand(llvm::MDNode::get(
          Context,
          {llvm::MDString::get(Context, Stage),
           llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 6)),
           llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 0))}));

  llvm::Metadata *Bindings[] = {emitResourceBindings(ResourceClass::SRV),
                                emitResourceBindings(ResourceClass::UAV),
                                emitResourceBindings(ResourceClass::CBV),
                                emitResourceBindings(ResourceClass::Sampler)};
  llvm::MDNode *Resources = nullptr;
  if (llvm::any_of(Bindings, [](llvm::Metadata *M) { return M != nullptr; })) {
    Resources = llvm::MDNode::get(Context, Bindings);
    Module->getOrInsertNamedMetadata("dx.resources")->addOperand(Resources);
  }

  llvm::Metadata *Signatures[] = {emitSignature(Inputs), emitSignature(Outputs),
                                  nullptr};
  auto *Properties = llvm::MDNode::get(
      Context,
      {llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 0)),
       llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
           llvm::Type::getInt64Ty(Context), ShaderFlags))});
  llvm::Metadata *EntryFields[] = {
      llvm::ConstantAsMetadata::get(Entry),
      llvm::MDString::get(Context, "main"),
      llvm::MDNode::get(Context, Signatures),
      Resources,
      Properties,
  };
  Module->getOrInsertNamedMetadata("dx.entryPoints")
      ->addOperand(llvm::MDNode::get(Context, EntryFields));
  Module->getOrInsertNamedMetadata("llvm.ident")
      ->addOperand(llvm::MDNode::get(
          Context, {llvm::MDString::get(Context, "feme dxbc2dxil")}));
}

//===----------------------------------------------------------------------===//
// Driver
//===----------------------------------------------------------------------===//

std::unique_ptr<llvm::Module> Translator::run(dxsa::ModuleOp Shader) {
  if (auto Type = Shader.getProgramType())
    Stage = stageName(*Type);

  if (!collectDeclarations(Shader))
    return nullptr;

  auto *EntryTy = llvm::FunctionType::get(llvm::Type::getVoidTy(Context),
                                          /*isVarArg=*/false);
  auto *Entry = llvm::Function::Create(
      EntryTy, llvm::GlobalValue::ExternalLinkage, "main", Module.get());
  auto *EntryBB = llvm::BasicBlock::Create(Context, "entry", Entry);
  Builder.SetInsertPoint(EntryBB);
  EntryFn = Entry;
  AllocaBuilder.SetInsertPoint(EntryBB);
  inferTempTypes(Shader);
  createIndexableTemps(Shader);
  collectResources(Shader);
  createResourceHandles(Shader);
  // DXBC has no strict floating-point semantics; every arithmetic
  // instruction is free to be reassociated and contracted.
  llvm::FastMathFlags FMF;
  FMF.setFast();
  Builder.setFastMathFlags(FMF);

  if (!translateBody(Shader))
    return nullptr;
  if (!Scopes.empty()) {
    Shader.emitError("unterminated control flow construct");
    return nullptr;
  }
  // Blocks a construct turned out not to need were never inserted.
  for (llvm::BasicBlock *BB : Pending)
    delete BB;
  Pending.clear();

  // A `break`, `continue` or `ret` in the middle of a construct leaves the
  // rest of it unreachable; dropping those blocks is what makes the result
  // look like the source rather than like its block structure. They are
  // only well-formed enough to delete once terminated.
  for (llvm::BasicBlock &BB : *Entry)
    if (!isTerminated(&BB)) {
      Builder.SetInsertPoint(&BB);
      Builder.CreateUnreachable();
    }
  llvm::EliminateUnreachableBlocks(*Entry);
  promoteTemps(*Entry);
  // Promotion leaves the reads that only fed a promoted slot behind, and
  // a DXBC shader can compute values it never uses; neither survives in
  // dxilconv's output.
  llvm::SmallVector<llvm::WeakTrackingVH, 8> Dead;
  for (llvm::BasicBlock &BB : *Entry)
    for (llvm::Instruction &I : BB)
      if (llvm::isInstructionTriviallyDead(&I))
        Dead.emplace_back(&I);
  llvm::RecursivelyDeleteTriviallyDeadInstructions(Dead);
  foldConditionMasks(*Entry);
  emitMetadata(Entry);
  return std::move(Module);
}

} // namespace

//===----------------------------------------------------------------------===//
// Entry point
//===----------------------------------------------------------------------===//

std::unique_ptr<llvm::Module> feme::dxsa::translateToLLVMIR(
    mlir::ModuleOp Source, llvm::LLVMContext &Context,
    llvm::ArrayRef<ContainerSignatureElement> RealInputSignature,
    llvm::ArrayRef<ContainerSignatureElement> RealOutputSignature) {
  dxsa::ModuleOp Shader;
  for (mlir::Operation &Op : Source.getBodyRegion().front()) {
    if (auto Candidate = llvm::dyn_cast<dxsa::ModuleOp>(&Op)) {
      if (Shader) {
        Op.emitError("expected exactly one dxsa.module");
        return nullptr;
      }
      Shader = Candidate;
    }
  }
  if (!Shader) {
    Source.emitError("expected a dxsa.module");
    return nullptr;
  }

  Translator T(Context, Source, RealInputSignature, RealOutputSignature);
  return T.run(Shader);
}

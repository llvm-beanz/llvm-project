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
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Dominators.h"
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
  Dot2 = 54,
  Dot3 = 55,
  Dot4 = 56,
  CreateHandle = 57,
  CBufferLoadLegacy = 59,
  Discard = 82,
  SampleIndex = 90,
  Coverage = 91,
  ThreadId = 93,
  GroupId = 94,
  ThreadIdInGroup = 95,
  FlattenedThreadIdInGroup = 96,
  BitcastI32toF32 = 126,
  BitcastF32toI32 = 127,
};

/// `DXIL::ResourceClass`, as named by `dx.op.createHandle`'s first argument.
enum class ResourceClass : unsigned { SRV = 0, UAV = 1, CBV = 2, Sampler = 3 };

/// `DXIL::ComponentType`, as stored in a signature element's metadata.
enum class DXILComponentType : unsigned { I32 = 4, U32 = 5, F32 = 9 };

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
  TessFactor = 18,
  InsideTessFactor = 19,
  DepthLessEqual = 20,
  DepthGreaterEqual = 21,
  Barycentrics = 23,
  ShadingRate = 24,
  CullPrimitive = 25,
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
  case llvm::dxbc::D3DSystemValue::Undefined:
  case llvm::dxbc::D3DSystemValue::Depth:
  case llvm::dxbc::D3DSystemValue::DepthGE:
  case llvm::dxbc::D3DSystemValue::DepthLE:
  case llvm::dxbc::D3DSystemValue::StencilRef:
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
std::optional<DXILSemanticKind> depthKind(OperandType Type) {
  switch (Type) {
  case OperandType::oDepth:
    return DXILSemanticKind::Depth;
  case OperandType::oDepthGE:
    return DXILSemanticKind::DepthGreaterEqual;
  case OperandType::oDepthLE:
    return DXILSemanticKind::DepthLessEqual;
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
  case llvm::dxbc::SigComponentType::UInt16:
  case llvm::dxbc::SigComponentType::UInt64:
    return DXILComponentType::U32;
  case llvm::dxbc::SigComponentType::SInt32:
  case llvm::dxbc::SigComponentType::SInt16:
  case llvm::dxbc::SigComponentType::SInt64:
    return DXILComponentType::I32;
  case llvm::dxbc::SigComponentType::Unknown:
  case llvm::dxbc::SigComponentType::Float16:
  case llvm::dxbc::SigComponentType::Float32:
  case llvm::dxbc::SigComponentType::Float64:
    break;
  }
  return DXILComponentType::F32;
}

/// One entry of the input or output signature. DXBC declares a signature
/// register piecewise -- one declaration per contiguous component group of
/// one register -- and each such group is one DXIL signature element.
struct SignatureElement {
  std::string Name;
  /// The register (signature row) the element lives in.
  unsigned Row = 0;
  /// The first component of the register the element covers.
  unsigned StartCol = 0;
  /// How many components the element covers.
  unsigned Cols = 0;
  DXILSemanticKind Kind = DXILSemanticKind::Arbitrary;
  /// `DXIL::InterpolationMode`, which matches D3D10_SB_INTERPOLATION_MODE.
  unsigned InterpolationMode = 0;
  /// The semantic index (e.g. the `3` in `SV_TessFactor3`, or in multiple
  /// arbitrary elements sharing one HLSL-source name across registers).
  /// Declaration-synthesized elements do not track this and leave it 0;
  /// real container elements (`ContainerSignatureElement::Index`) do.
  unsigned Index = 0;
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
    for (unsigned Col = Element.StartCol, End = Element.StartCol + Element.Cols;
         Col != End; ++Col)
      Lookup[key(Element.Row, Col)] = Elements.size();
    Elements.push_back(std::move(Element));
  }

  /// Appends an element that occupies no register, so that it does not
  /// shadow the (row, component) an `o#` register really lives at.
  void addUnindexed(SignatureElement Element) {
    Elements.push_back(std::move(Element));
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
  bool empty() const { return Elements.empty(); }

private:
  static uint64_t key(unsigned Row, unsigned Col) {
    return (static_cast<uint64_t>(Row) << 2) | Col;
  }

  llvm::SmallVector<SignatureElement, 8> Elements;
  llvm::DenseMap<uint64_t, unsigned> Lookup;
};

//===----------------------------------------------------------------------===//
// Translator
//===----------------------------------------------------------------------===//

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
  /// The `dx.op.createHandle` call for each declared constant buffer, keyed
  /// by the `cb#` number the operands name it with.
  llvm::DenseMap<unsigned, llvm::Value *> ConstantBuffers;
  /// The index of each constant buffer's declaration within its resource
  /// class, which is what a handle is bound by.
  llvm::DenseMap<unsigned, unsigned> ConstantBufferRanges;
  /// The output signature element each depth output resolves to. A depth
  /// output names no register, so it cannot be found by (row, component)
  /// the way the others are.
  llvm::DenseMap<unsigned, unsigned> DepthOutputs;
  /// The dedicated DXIL operation reading each input signature element that
  /// has one, keyed by element index. DXIL names a few system values with
  /// an operation of their own rather than through `loadInput`.
  llvm::DenseMap<unsigned, std::pair<llvm::StringRef, DXILOp>>
      SystemValueReads;
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
  /// The type inferred for each temp register component; see
  /// `inferTempTypes`.
  llvm::DenseMap<uint64_t, llvm::Type *> TempTypes;
  /// Values already read for the instruction being translated, keyed by the
  /// source operand, the component it reads and the type it is read at. A
  /// DXBC instruction may name the same source component several times
  /// through its swizzle, and each such mention is one value, not several.
  llvm::DenseMap<std::tuple<const void *, unsigned, llvm::Type *>,
                 llvm::Value *>
      SourceCache;
  /// Shader stage name for `!dx.shaderModel` ("ps", "vs", ...).
  llvm::StringRef Stage = "ps";

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

  llvm::Type *floatTy() { return llvm::Type::getFloatTy(Context); }
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
  static void
  addRealSignatureElements(Signature &Sig,
                           llvm::ArrayRef<ContainerSignatureElement> Elements);

  //===--------------------------------------------------------------------===//
  // Operands
  //===--------------------------------------------------------------------===//

  /// Returns the component index \p Src reads when producing destination
  /// component \p DstComp.
  static unsigned sourceComponent(SrcOperandAttr Src, unsigned DstComp);
  /// Returns the first immediate index of \p Operand, i.e. the register
  /// number, or nullopt if it is not a plain immediate.
  static std::optional<unsigned> registerNumber(OperandIndexAttr Index);

  /// Emits the `dx.op.createHandle` call for every declared resource. DXIL
  /// binds a resource once, at the top of the entry point, and refers to
  /// the resulting handle from every access.
  void createResourceHandles(dxsa::ModuleOp Shader);
  /// Evaluates one index slot of an operand: an immediate, a register read
  /// at run time, or their sum.
  llvm::Value *readIndex(IndexAttr Index, mlir::Operation *Op);
  /// Reads component \p Comp of the constant buffer row \p Src names.
  llvm::Value *readConstantBuffer(SrcOperandAttr Src, unsigned Comp,
                                  llvm::Type *Ty, mlir::Operation *Op);

  /// Returns (creating on first use) the stack slot backing temp register
  /// component \p Comp of register \p Reg.
  llvm::AllocaInst *tempSlot(unsigned Reg, unsigned Comp);

  llvm::Value *readSource(SrcOperandAttr Src, unsigned DstComp, llvm::Type *Ty,
                          mlir::Operation *Op);
  llvm::Value *coerce(llvm::Value *Value, llvm::Type *Ty);
  bool writeDestination(DstOperandAttr Dst,
                        llvm::ArrayRef<llvm::Value *> Components,
                        mlir::Operation *Op);
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
  bool translateDot(mlir::Operation *Op, DstOperandAttr Dst, SrcOperandAttr Lhs,
                    SrcOperandAttr Rhs, unsigned Lanes, bool Saturate);
  llvm::Value *saturate(llvm::Value *Value, bool Enabled);

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
  Element.Name = Kind == DXILSemanticKind::Arbitrary
                     ? (NamePrefix + llvm::Twine(Sig.elements().size())).str()
                     : semanticName(Kind);
  Sig.add(std::move(Element));
}

bool Translator::collectDeclarations(dxsa::ModuleOp Shader) {
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
        if (auto Read = systemValueRead(Kind))
          SystemValueReads[Inputs.elements().size()] = *Read;
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN", Kind, 0);
      }
      else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputSiv>(&Op))
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN",
                            toSemanticKind(Dcl.getName()), 0);
      else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputSgv>(&Op))
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN",
                            toSemanticKind(Dcl.getName()), 0);
    }
  }

  if (!RealOutputSignature.empty()) {
    addRealSignatureElements(Outputs, RealOutputSignature);
    return true;
  }

  bool IsPixelShader = !Shader.getProgramType() ||
                       Shader.getProgramType() == ProgramType::pixel_shader;
  for (mlir::Operation &Op : *Shader.getBodyBlock()) {
    if (auto Dcl = llvm::dyn_cast<dxsa::DclOutput>(&Op)) {
      DstOperandAttr Operand = Dcl.getOperandAttr();
      // A depth output names no register, so it is recorded by operand kind
      // rather than by the (row, component) the others are found through.
      if (std::optional<DXILSemanticKind> Kind = depthKind(Operand.getType())) {
        DepthOutputs[unsigned(Operand.getType())] = Outputs.elements().size();
        SignatureElement Element;
        Element.Cols = 1;
        Element.Kind = *Kind;
        Element.Name = semanticName(*Kind);
        Outputs.addUnindexed(std::move(Element));
        continue;
      }
      addSignatureElement(Outputs, Operand, "OUT",
                          IsPixelShader ? DXILSemanticKind::Target
                                        : DXILSemanticKind::Arbitrary,
                          0);
    }
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclOutputSiv>(&Op))
      addSignatureElement(Outputs, Dcl.getOperandAttr(), "OUT",
                          toSemanticKind(Dcl.getName()), 0);
    else if (auto Dcl = llvm::dyn_cast<dxsa::DclOutputSgv>(&Op))
      addSignatureElement(Outputs, Dcl.getOperandAttr(), "OUT",
                          toSemanticKind(Dcl.getName()), 0);
  }
  return true;
}

void Translator::addRealSignatureElements(
    Signature &Sig, llvm::ArrayRef<ContainerSignatureElement> Elements) {
  for (const ContainerSignatureElement &El : Elements) {
    SignatureElement Element;
    Element.Row = El.Register;
    Element.StartCol = llvm::countr_zero(El.Mask);
    Element.Cols = llvm::popcount(El.Mask);
    Element.Kind = toSemanticKind(static_cast<llvm::dxbc::D3DSystemValue>(
        El.SystemValue));
    Element.Name = Element.Kind == DXILSemanticKind::Arbitrary
                       ? El.Name
                       : semanticName(Element.Kind);
    Element.Index = El.Index;
    Element.Type =
        toComponentType(static_cast<llvm::dxbc::SigComponentType>(El.CompType));
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
  if (depthKind(Dst.getType()))
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

//===----------------------------------------------------------------------===//
// Resources
//===----------------------------------------------------------------------===//

llvm::StructType *Translator::handleTy() {
  if (auto *Existing = llvm::StructType::getTypeByName(Context,
                                                       "dx.types.Handle"))
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

void Translator::createResourceHandles(dxsa::ModuleOp Shader) {
  // `createHandle`'s third argument is the index of the declaration within
  // its resource class, not the register the declaration binds to.
  unsigned Range = 0;
  for (mlir::Operation &Op : *Shader.getBodyBlock()) {
    auto Dcl = llvm::dyn_cast<dxsa::DclConstantBuffer>(&Op);
    if (!Dcl)
      continue;
    // Shader model 5.1 binds a range of registers and names the range by
    // an identifier of its own; before that the identifier is the register.
    unsigned Id = Dcl.getId();
    unsigned Bind = Dcl.getLbound().value_or(Id);
    ConstantBufferRanges[Id] = Range;
    ConstantBuffers[Id] = emitDXOp(
        "createHandle", DXILOp::CreateHandle, handleTy(),
        {llvm::ConstantInt::get(i8Ty(), unsigned(ResourceClass::CBV)),
         llvm::ConstantInt::get(i32Ty(), Range++),
         llvm::ConstantInt::get(i32Ty(), Bind),
         llvm::ConstantInt::get(i1Ty(), 0)},
        noOverload());
  }
}

llvm::Value *Translator::readIndex(IndexAttr Index, mlir::Operation *Op) {
  mlir::IntegerAttr Offset = Index.getImm();
  SrcOperandAttr Relative = Index.getRelative();
  if (!Relative)
    return Offset ? llvm::ConstantInt::get(i32Ty(), Offset.getInt()) : nullptr;
  llvm::Value *Value = readSource(Relative, 0, i32Ty(), Op);
  if (!Value || !Offset || Offset.getInt() == 0)
    return Value;
  return Builder.CreateAdd(Value,
                           llvm::ConstantInt::get(i32Ty(), Offset.getInt()));
}

llvm::Value *Translator::readConstantBuffer(SrcOperandAttr Src, unsigned Comp,
                                            llvm::Type *Ty,
                                            mlir::Operation *Op) {
  OperandIndexAttr Index = Src.getIndex();
  // A `cb#` operand indexes the buffer and then the row; shader model 5.1
  // inserts the register within the declared range in between.
  if (!Index || Index.size() < 2) {
    unsupported(Op) << ": constant buffer operand indexing";
    return nullptr;
  }
  std::optional<unsigned> Id = registerNumber(Index);
  auto Declared = Id ? ConstantBuffers.find(*Id) : ConstantBuffers.end();
  if (Declared == ConstantBuffers.end()) {
    unsupported(Op) << ": read of an undeclared constant buffer";
    return nullptr;
  }

  llvm::Value *Row = readIndex(Index[Index.size() - 1], Op);
  if (!Row)
    return nullptr;

  // Shader model 5.1 binds a range of registers, so the operand also picks
  // the register within that range -- and may do so at run time, in which
  // case the handle is bound at the access rather than at the entry point.
  llvm::Value *Handle = Declared->second;
  if (Index.size() >= 3 && Index[1].getRelative()) {
    llvm::Value *&Cached = HandleCache[Src.getAsOpaquePointer()];
    if (!Cached) {
      llvm::Value *Bind = readIndex(Index[1], Op);
      if (!Bind)
        return nullptr;
      Cached = emitDXOp(
          "createHandle", DXILOp::CreateHandle, handleTy(),
          {llvm::ConstantInt::get(i8Ty(), unsigned(ResourceClass::CBV)),
           llvm::ConstantInt::get(i32Ty(), ConstantBufferRanges.lookup(*Id)),
           Bind, llvm::ConstantInt::get(i1Ty(), 0)},
          noOverload());
    }
    Handle = Cached;
  }

  llvm::Type *Element = Ty->isFloatTy() ? floatTy() : i32Ty();
  auto Key = std::make_tuple(Src.getAsOpaquePointer(), Row, Element);
  llvm::Value *&Loaded = RowCache[Key];
  if (!Loaded)
    Loaded = emitDXOp("cbufferLoadLegacy", DXILOp::CBufferLoadLegacy,
                      cbufferRetTy(Element), {Handle, Row}, Element);
  return Builder.CreateExtractValue(Loaded, Comp);
}

llvm::AllocaInst *Translator::tempSlot(unsigned Reg, unsigned Comp) {
  uint64_t Key = uint64_t(Reg) * 4 + Comp;
  llvm::AllocaInst *&Slot = Temps[Key];
  if (Slot)
    return Slot;
  auto It = TempTypes.find(Key);
  llvm::Type *Ty = It == TempTypes.end() ? i32Ty() : It->second;
  // DXBC numbers a temp by register and component; DXIL's own temp-register
  // intrinsics flatten that to a single index, which is the name dxilconv
  // gives the promoted value.
  AllocaBuilder.SetInsertPointPastAllocas(EntryFn);
  Slot = AllocaBuilder.CreateAlloca(Ty, nullptr,
                                    "dx.v32.r" + llvm::Twine(Key));
  return Slot;
}

llvm::Value *Translator::readSource(SrcOperandAttr Src, unsigned DstComp,
                                    llvm::Type *Ty, mlir::Operation *Op) {
  // A minimum-precision operand changes the width every computation
  // reading it is done at, which this translation does not model yet.
  if (Src.getMinPrecision()) {
    unsupported(Op) << ": minimum-precision source operand";
    return nullptr;
  }

  unsigned Comp = sourceComponent(Src, DstComp);
  auto Key = std::make_tuple(Src.getAsOpaquePointer(), Comp, Ty);
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
    Result = Ty->isFloatTy()
                 ? llvm::ConstantFP::get(
                       Ty, llvm::APFloat(llvm::bit_cast<float>(Bits)))
                 : static_cast<llvm::Value *>(
                       llvm::ConstantInt::get(i32Ty(), uint32_t(Bits)));
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
    llvm::AllocaInst *Slot = tempSlot(*Reg, Comp);
    Result = Builder.CreateLoad(Slot->getAllocatedType(), Slot);
    break;
  }
  case OperandType::v: {
    std::optional<unsigned> Reg = registerNumber(Src.getIndex());
    if (!Reg) {
      unsupported(Op) << ": indexed input register";
      return nullptr;
    }
    std::optional<unsigned> Element = Inputs.find(*Reg, Comp);
    if (!Element) {
      unsupported(Op) << ": read of undeclared input register";
      return nullptr;
    }
    if (auto Read = SystemValueReads.find(*Element);
        Read != SystemValueReads.end()) {
      Result = emitDXOp(Read->second.first, Read->second.second, i32Ty(), {});
      break;
    }
    unsigned Col = Comp - Inputs.elements()[*Element].StartCol;
    Result = emitDXOp("loadInput", DXILOp::LoadInput, Ty,
                      {llvm::ConstantInt::get(i32Ty(), *Element),
                       llvm::ConstantInt::get(i32Ty(), 0),
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
    Result = emitDXOp(Name, DXOp, i32Ty(),
                      {llvm::ConstantInt::get(i32Ty(), Comp)});
    break;
  }
  case OperandType::vThreadIDInGroupFlattened:
    Result = emitDXOp("flattenedThreadIdInGroup",
                      DXILOp::FlattenedThreadIdInGroup, i32Ty(), {});
    break;
  case OperandType::vCoverage:
    Result = emitDXOp("coverage", DXILOp::Coverage, i32Ty(), {});
    break;
  case OperandType::cb:
    Result = readConstantBuffer(Src, Comp, Ty, Op);
    if (!Result)
      return nullptr;
    break;
  default:
    unsupported(Op) << ": source operand kind";
    return nullptr;
  }

  Result = coerce(Result, Ty);

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
                                  mlir::Operation *Op) {
  if (Dst.getMinPrecision()) {
    unsupported(Op) << ": minimum-precision destination operand";
    return false;
  }

  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);
  if (depthKind(Dst.getType())) {
    auto Element = DepthOutputs.find(unsigned(Dst.getType()));
    if (Element == DepthOutputs.end()) {
      unsupported(Op) << ": write to an undeclared depth output";
      return false;
    }
    emitDXOp("storeOutput", DXILOp::StoreOutput, llvm::Type::getVoidTy(Context),
             {llvm::ConstantInt::get(i32Ty(), Element->second),
              llvm::ConstantInt::get(i32Ty(), 0),
              llvm::ConstantInt::get(i8Ty(), 0),
              coerce(Components[0], floatTy())});
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
      llvm::AllocaInst *Slot = tempSlot(*Reg, Comp);
      Builder.CreateStore(coerce(Components[I], Slot->getAllocatedType()),
                          Slot);
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
      llvm::Value *Value =
          RealOutputSignature.empty()
              ? Components[I]
              : coerce(Components[I], Info.Type == DXILComponentType::F32
                                          ? floatTy()
                                          : i32Ty());
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
  std::string FullName =
      OverloadTy ? ("dx.op." + Name + "." +
                    (OverloadTy->isFloatTy()      ? "f32"
                     : OverloadTy->isDoubleTy()   ? "f64"
                     : OverloadTy->isIntegerTy(1) ? "i1"
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
  if (!ReturnTy->isVoidTy())
    Fn->setMemoryEffects(llvm::MemoryEffects::none());
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
  llvm::Type *OverloadTy = Overload == noOverload() ? nullptr
                           : Overload               ? Overload
                           : Name.starts_with("bitcast") ? nullptr
                           : !ReturnTy->isVoidTy()       ? ReturnTy
                           : Name == "discard"           ? nullptr
                                                         : ArgTys.back();
  // A dx.op call names a specific operation with fixed semantics; the
  // relaxed floating-point rules apply to the native LLVM arithmetic the
  // translation emits around it, not to the call itself.
  llvm::IRBuilderBase::FastMathFlagGuard Guard(Builder);
  Builder.clearFastMathFlags();
  return Builder.CreateCall(dxOp(Name, ReturnTy, ArgTys, OverloadTy), CallArgs);
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
  if (Src.getModifier())
    return floatTy();
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
  return emitDXOp("unary", DXILOp::Saturate, Value->getType(), {Value});
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

static OpLowering castOp(llvm::Instruction::CastOps Cast, bool FloatIn,
                         bool FloatOut) {
  OpLowering L;
  L.Kind = OpLowering::Form::Cast;
  L.FloatOperands = FloatIn;
  L.FloatResult = FloatOut;
  L.Cast = Cast;
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
      // Integer arithmetic.
      {"iadd", nativeOp(BO::Add, false)},
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
      {"bfrev", callOp(DXILOp::Bfrev, "unaryBits", false)},
      {"countbits", callOp(DXILOp::Countbits, "unaryBits", false)},
      {"firstbit_lo", callOp(DXILOp::FirstbitLo, "unaryBits", false)},
      {"firstbit_hi", callOp(DXILOp::FirstbitHi, "unaryBits", false)},
      {"firstbit_shi", callOp(DXILOp::FirstbitSHi, "unaryBits", false)},
      // Conversions.
      {"itof", castOp(BO::SIToFP, false, true)},
      {"utof", castOp(BO::UIToFP, false, true)},
      {"ftoi", castOp(BO::FPToSI, true, false)},
      {"ftou", castOp(BO::FPToUI, true, false)},
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
        if (Src.getType() == OperandType::r)
          vote(registerNumber(Src.getIndex()), sourceComponent(Src, 0), false);

    if (std::optional<OpLowering> Lowering = typedLowering(Name)) {
      if (Dst && Dst.getType() == OperandType::r)
        for (unsigned Comp : Comps)
          vote(registerNumber(Dst.getIndex()), Comp, Lowering->FloatResult);
      for (SrcOperandAttr Src : Srcs)
        if (Src.getType() == OperandType::r)
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
          if (std::optional<bool> Float = signatureIsFloat(
                  Inputs, *SrcReg, sourceComponent(Src, Comp)))
            vote(registerNumber(Dst.getIndex()), Comp, *Float);
    } else if (Dst.getType() == OperandType::o &&
               Src.getType() == OperandType::r) {
      std::optional<unsigned> DstReg = registerNumber(Dst.getIndex());
      for (unsigned Comp : Comps)
        if (DstReg)
          if (std::optional<bool> Float = signatureIsFloat(*&Outputs, *DstReg,
                                                           Comp))
            vote(registerNumber(Src.getIndex()), sourceComponent(Src, Comp),
                 *Float);
    }
  }

  for (auto [Key, V] : Votes)
    TempTypes[Key] = V.first > 0 && V.first >= V.second ? floatTy() : i32Ty();
}

bool Translator::translateUnary(mlir::Operation *Op, DstOperandAttr Dst,
                                SrcOperandAttr Src, bool Saturate) {
  llvm::StringRef Name = mnemonicOf(Op);
  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);

  // `mov` is a pure copy: its operand modifiers already did the work, and
  // DXBC gives it no type of its own, so keep whatever the source holds.
  if (Name == "mov") {
    llvm::SmallVector<llvm::Value *, 4> Values;
    for (unsigned Comp : Comps) {
      llvm::Value *Value =
          readSource(Src, Comp, movElementType(Dst, Src, Comp), Op);
      if (!Value)
        return false;
      Values.push_back(saturate(Value, Saturate));
    }
    return writeDestination(Dst, Values, Op);
  }

  std::optional<OpLowering> Lowering = lookupLowering(Name);
  if (!Lowering) {
    unsupported(Op);
    return false;
  }

  llvm::Type *SrcTy = Lowering->FloatOperands ? floatTy() : i32Ty();
  llvm::Type *DstTy = Lowering->FloatResult ? floatTy() : i32Ty();
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
      Value = emitDXOp(Lowering->Name, Lowering->Op, DstTy, {Source});
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
  std::optional<OpLowering> Lowering = lookupLowering(mnemonicOf(Op));
  if (!Lowering) {
    unsupported(Op);
    return false;
  }

  llvm::Type *SrcTy = Lowering->FloatOperands ? floatTy() : i32Ty();
  llvm::Type *DstTy = Lowering->FloatResult ? floatTy() : i32Ty();
  llvm::SmallVector<unsigned, 4> Comps = destinationComponents(Dst);

  // Every source component is read before any result is computed, matching
  // how the operands are laid out in the instruction.
  llvm::SmallVector<llvm::Value *, 4> L, R;
  for (unsigned Comp : Comps) {
    llvm::Value *Value = readSource(Lhs, Comp, SrcTy, Op);
    if (!Value)
      return false;
    L.push_back(Value);
  }
  for (unsigned Comp : Comps) {
    llvm::Value *Value = readSource(Rhs, Comp, SrcTy, Op);
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
  for (SrcOperandAttr Src : {Lhs, Rhs, Acc})
    for (unsigned Comp : Comps) {
      llvm::Value *Value = readSource(Src, Comp, Ty, Op);
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
  for (SrcOperandAttr Src : {Lhs, Rhs})
    for (unsigned Comp = 0; Comp < Lanes; ++Comp) {
      llvm::Value *Value = readSource(Src, Comp, floatTy(), Op);
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
  llvm::BasicBlock *Group = deferredBlock(
      "switch" + llvm::Twine(S.Id) + ".casegroup" + llvm::Twine(S.CaseGroups++));
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
    llvm::BasicBlock *Then =
        deferredBlock("if" + llvm::Twine(S.Id) + ".then");
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
    llvm::Twine Name = S->K == Scope::Kind::Loop
                           ? "loop" + llvm::Twine(S->Id) + ".breakc" +
                                 llvm::Twine(S->Breaks)
                           : "switch" + llvm::Twine(S->Id) + ".break" +
                                 llvm::Twine(S->Breaks);
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
    llvm::ArrayRef<int32_t> Values = Case.getOperand().getValues32().asArrayRef();
    if (Values.empty()) {
      Op->emitError("'dxsa.case' without a label");
      return false;
    }
    // Consecutive `case`s share one block, so the group is not opened until
    // an instruction other than a `case` follows.
    Scopes.back().PendingCases.push_back(
        llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i32Ty()),
                               uint32_t(Values[0])));
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
    llvm::Value *Selected = Builder.CreateICmpNE(
        Test, llvm::ConstantInt::get(i32Ty(), 0));
    llvm::Value *Left = readSource(True, Comp, Ty, Op);
    llvm::Value *Right = readSource(False, Comp, Ty, Op);
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
  llvm::StringRef Name = mnemonicOf(Op);
  bool Saturate = Name.consume_back("_sat");

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

  if (auto Mad = llvm::dyn_cast<dxsa::Mad>(Op))
    return translateMad(Op, Mad.getDst(), Mad.getLhs(), Mad.getRhs(),
                        Mad.getAcc(), Saturate);
  if (auto Mad = llvm::dyn_cast<dxsa::Imad>(Op))
    return translateMad(Op, Mad.getDst(), Mad.getLhs(), Mad.getRhs(),
                        Mad.getAcc(), Saturate);
  if (auto Mad = llvm::dyn_cast<dxsa::UMad>(Op))
    return translateMad(Op, Mad.getDst(), Mad.getLhs(), Mad.getRhs(),
                        Mad.getAcc(), Saturate);
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
  llvm::SmallVector<llvm::Instruction *, 8> Dead;

  // A value stored into a temp register of the other type and read back
  // comes out unchanged; the pair only existed because the slot's type had
  // to be picked once for the whole shader.
  for (llvm::BasicBlock &BB : Entry) {
    for (llvm::Instruction &I : llvm::make_early_inc_range(BB)) {
      llvm::Value *Source = bitcastSource(&I);
      if (!Source)
        continue;
      auto *Inner = llvm::dyn_cast<llvm::Instruction>(Source);
      if (!Inner)
        continue;
      llvm::Value *Original = bitcastSource(Inner);
      if (!Original || Original->getType() != I.getType())
        continue;
      I.replaceAllUsesWith(Original);
      I.eraseFromParent();
      Dead.push_back(Inner);
    }
  }
  for (llvm::Instruction *I : Dead)
    if (I->use_empty())
      I->eraseFromParent();
  Dead.clear();

  // A resource is bound at the entry point for every declaration, but one
  // only ever accessed through a run-time binding never uses that handle.
  for (llvm::Instruction &I :
       llvm::make_early_inc_range(Entry.getEntryBlock())) {
    auto *Call = llvm::dyn_cast<llvm::CallInst>(&I);
    if (Call && Call->use_empty() && Call->getCalledFunction() &&
        Call->getCalledFunction()->getName() == "dx.op.createHandle")
      Call->eraseFromParent();
  }

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
      Dead.push_back(Mask);
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

llvm::MDNode *Translator::emitSignature(const Signature &Sig) {
  if (Sig.empty())
    return nullptr;
  llvm::SmallVector<llvm::Metadata *, 8> Elements;
  for (auto [Index, Element] : llvm::enumerate(Sig.elements())) {
    auto *SemanticIndices = llvm::MDNode::get(
        Context, {llvm::ConstantAsMetadata::get(
                     llvm::ConstantInt::get(i32Ty(), Element.Index))});
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
        llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 1)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i8Ty(), Element.Cols)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i32Ty(), Element.Row)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i8Ty(), Element.StartCol)),
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

  llvm::Metadata *Signatures[] = {emitSignature(Inputs), emitSignature(Outputs),
                                  nullptr};
  auto *Properties = llvm::MDNode::get(
      Context,
      {llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty(), 0)),
       llvm::ConstantAsMetadata::get(
           llvm::ConstantInt::get(llvm::Type::getInt64Ty(Context), 0))});
  llvm::Metadata *EntryFields[] = {
      llvm::ConstantAsMetadata::get(Entry),
      llvm::MDString::get(Context, "main"),
      llvm::MDNode::get(Context, Signatures),
      nullptr,
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
  inferTempTypes(Shader);
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

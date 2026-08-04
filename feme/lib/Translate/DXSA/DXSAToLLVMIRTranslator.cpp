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
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#include <cmath>
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
};

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
  TessFactor = 18,
  InsideTessFactor = 19,
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
  /// Real signature elements read from a full `DXContainer`, overriding
  /// `collectDeclarations`'s synthesis when non-empty (see
  /// `ContainerSignatureElement`).
  llvm::ArrayRef<ContainerSignatureElement> RealInputSignature;
  llvm::ArrayRef<ContainerSignatureElement> RealOutputSignature;

  Signature Inputs;
  Signature Outputs;
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
  /// Insertion point for `Temps`' allocas, pinned to the entry block.
  llvm::IRBuilder<> AllocaBuilder;
  /// Values already read for the instruction being translated, keyed by the
  /// source operand, the component it reads and the type it is read at. A
  /// DXBC instruction may name the same source component several times
  /// through its swizzle, and each such mention is one value, not several.
  llvm::DenseMap<std::tuple<const void *, unsigned, llvm::Type *>,
                 llvm::Value *>
      SourceCache;
  /// Shader stage name for `!dx.shaderModel` ("ps", "vs", ...).
  llvm::StringRef Stage = "ps";

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
  /// this is whatever the source component already holds; a signature read
  /// or a literal has no type of its own and defaults to float.
  llvm::Type *movElementType(SrcOperandAttr Src, unsigned DstComp);

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
  llvm::Value *emitDXOp(llvm::StringRef Name, DXILOp Op, llvm::Type *ReturnTy,
                        llvm::ArrayRef<llvm::Value *> Args);

  //===--------------------------------------------------------------------===//
  // Metadata
  //===--------------------------------------------------------------------===//

  /// Rewrites the temp register stack slots into SSA values. The promoted
  /// values keep the slot's name, so a temp live across a branch surfaces
  /// as a `dx.v32.r<n>.<m>` phi node.
  void promoteTemps(llvm::Function &Entry);

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
      else if (auto Dcl = llvm::dyn_cast<dxsa::DclInputPsSgv>(&Op))
        addSignatureElement(Inputs, Dcl.getOperandAttr(), "IN",
                            toSemanticKind(Dcl.getName()), 0);
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
    if (auto Dcl = llvm::dyn_cast<dxsa::DclOutput>(&Op))
      addSignatureElement(Outputs, Dcl.getOperandAttr(), "OUT",
                          IsPixelShader ? DXILSemanticKind::Target
                                        : DXILSemanticKind::Arbitrary,
                          0);
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
  return Builder.CreateBitCast(Value, Ty);
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
    unsigned Col = Comp - Inputs.elements()[*Element].StartCol;
    Result = emitDXOp("loadInput", DXILOp::LoadInput, Ty,
                      {llvm::ConstantInt::get(i32Ty(), *Element),
                       llvm::ConstantInt::get(i32Ty(), 0),
                       llvm::ConstantInt::get(i8Ty(), Col),
                       llvm::UndefValue::get(i32Ty())});
    break;
  }
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
      unsigned Col = Comp - Outputs.elements()[*Element].StartCol;
      emitDXOp("storeOutput", DXILOp::StoreOutput,
               llvm::Type::getVoidTy(Context),
               {llvm::ConstantInt::get(i32Ty(), *Element),
                llvm::ConstantInt::get(i32Ty(), 0),
                llvm::ConstantInt::get(i8Ty(), Col), Components[I]});
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
  llvm::StringRef Suffix = OverloadTy->isFloatTy()      ? "f32"
                           : OverloadTy->isDoubleTy()   ? "f64"
                           : OverloadTy->isIntegerTy(1) ? "i1"
                                                        : "i32";
  std::string FullName = ("dx.op." + Name + "." + Suffix).str();
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
                                  llvm::ArrayRef<llvm::Value *> Args) {
  llvm::SmallVector<llvm::Type *, 8> ArgTys;
  llvm::SmallVector<llvm::Value *, 8> CallArgs;
  CallArgs.push_back(llvm::ConstantInt::get(i32Ty(), unsigned(Op)));
  for (llvm::Value *Arg : Args) {
    ArgTys.push_back(Arg->getType());
    CallArgs.push_back(Arg);
  }
  // A void operation is overloaded on its value argument, which is last.
  llvm::Type *OverloadTy = ReturnTy->isVoidTy() ? ArgTys.back() : ReturnTy;
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

llvm::Type *Translator::movElementType(SrcOperandAttr Src, unsigned DstComp) {
  if (Src.getModifier())
    return floatTy();
  if (Src.getType() != OperandType::r)
    return floatTy();
  std::optional<unsigned> Reg = registerNumber(Src.getIndex());
  if (!Reg)
    return floatTy();
  return tempSlot(*Reg, sourceComponent(Src, DstComp))->getAllocatedType();
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

/// Returns whether \p Op's operands and result are floating point, or
/// nullopt when the instruction imposes no type on them (`mov` and the
/// conditional moves copy bits) or is not modelled at all.
static std::optional<bool> hasFloatOperands(llvm::StringRef Name) {
  Name.consume_back("_sat");
  if (Name == "mov" || Name == "movc")
    return std::nullopt;
  if (Name == "mad" || Name.starts_with("dp"))
    return true;
  if (Name == "imad" || Name == "umad")
    return false;
  if (std::optional<OpLowering> Lowering = lookupLowering(Name))
    return Lowering->FloatOperands;
  return std::nullopt;
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

    // A conditional control-flow instruction tests its condition as an
    // integer bit pattern.
    if (Name.ends_with("_z") || Name.ends_with("_nz") || Name == "switch")
      for (SrcOperandAttr Src : Srcs)
        if (Src.getType() == OperandType::r)
          vote(registerNumber(Src.getIndex()), sourceComponent(Src, 0), false);

    if (std::optional<bool> Float = hasFloatOperands(Name)) {
      if (Dst && Dst.getType() == OperandType::r)
        for (unsigned Comp : Comps)
          vote(registerNumber(Dst.getIndex()), Comp, *Float);
      for (SrcOperandAttr Src : Srcs)
        if (Src.getType() == OperandType::r)
          for (unsigned Comp : Comps)
            vote(registerNumber(Src.getIndex()), sourceComponent(Src, Comp),
                 *Float);
      continue;
    }

    if (Name != "mov" && Name != "mov_sat")
      continue;
    auto Src = Op.getAttrOfType<SrcOperandAttr>("src");
    if (!Dst || !Src)
      continue;
    if (Dst.getType() == OperandType::r && Src.getType() == OperandType::v) {
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
      llvm::Value *Value = readSource(Src, Comp, movElementType(Src, Comp), Op);
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

bool Translator::translateInstruction(mlir::Operation *Op) {
  SourceCache.clear();
  llvm::StringRef Name = mnemonicOf(Op);
  bool Saturate = Name.consume_back("_sat");

  if (llvm::isa<dxsa::Ret>(Op)) {
    Builder.CreateRetVoid();
    return true;
  }
  // Declarations carry no code.
  if (Name.starts_with("dcl_"))
    return true;

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
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateRetVoid();
  return true;
}

//===----------------------------------------------------------------------===//
// Cleanup
//===----------------------------------------------------------------------===//

void Translator::promoteTemps(llvm::Function &Entry) {
  llvm::SmallVector<llvm::AllocaInst *, 16> Slots;
  for (auto [Key, Slot] : Temps)
    Slots.push_back(Slot);
  if (Slots.empty())
    return;
  // Promote in slot order so that the phi nodes of a block, which mem2reg
  // creates one alloca at a time, come out in a stable order.
  llvm::sort(Slots, [](const llvm::AllocaInst *L, const llvm::AllocaInst *R) {
    return L->getName() < R->getName();
  });
  llvm::DominatorTree DT(Entry);
  llvm::PromoteMemToReg(Slots, DT);
  Temps.clear();
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
  AllocaBuilder.SetInsertPoint(EntryBB);
  inferTempTypes(Shader);
  // DXBC has no strict floating-point semantics; every arithmetic
  // instruction is free to be reassociated and contracted.
  llvm::FastMathFlags FMF;
  FMF.setFast();
  Builder.setFastMathFlags(FMF);

  if (!translateBody(Shader))
    return nullptr;

  promoteTemps(*Entry);
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

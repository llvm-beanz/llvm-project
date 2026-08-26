//===- CanonicalizeStage.cpp - Canonicalize vertex/fragment stage IR ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/Graphics/CanonicalizeStage.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme;
using namespace feme::graphics;

namespace {

/// `feme::dxil::convertEntrySignature` numbers each of DXIL's input, output
/// and patch-constant lists from 0 upward, in the same order the source
/// list's rows appear (see SignatureImport.cpp's `convertSignature`), so
/// the Nth `Sig.Elements` entry (0-based) with direction \p Dir is exactly
/// what DXIL's own per-list signature ID N names. This reconstructs that
/// per-direction-index -> combined `ElementID` table so a `loadInput`/
/// `storeOutput` call's signature-ID operand (DXIL's own per-list ID, not
/// feme's combined one) can be resolved back through it.
SmallVector<uint32_t> collectElementIDsByDirection(const EntrySignature &Sig,
                                                   SignatureDirection Dir) {
  SmallVector<uint32_t> IDs;
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Dir)
      IDs.push_back(Elt.ElementID);
  return IDs;
}

std::optional<uint32_t> resolveElementID(ArrayRef<uint32_t> IDsByDirection,
                                         uint64_t DXILID) {
  if (DXILID >= IDsByDirection.size())
    return std::nullopt;
  return IDsByDirection[DXILID];
}

std::optional<uint64_t> getConstInt(const Value *V) {
  if (const auto *CI = dyn_cast<ConstantInt>(V))
    return CI->getZExtValue();
  return std::nullopt;
}

/// Calls \p Raise on every `CallInst` that calls a `dx.op.*` function whose
/// opcode operand (its first argument) is \p Opcode, snapshotting each
/// function's user list first so \p Raise may erase/replace calls freely.
bool forEachDXOpCall(Function &F, unsigned Opcode,
                     function_ref<bool(CallInst &)> Raise) {
  bool Changed = false;
  Module &M = *F.getParent();
  for (Function &Callee : llvm::make_early_inc_range(M.functions())) {
    if (!Callee.isDeclaration() || !Callee.getName().starts_with("dx.op."))
      continue;
    for (User *U : llvm::make_early_inc_range(Callee.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getFunction() != &F || CI->getCalledFunction() != &Callee)
        continue;
      if (CI->arg_size() == 0 || getConstInt(CI->getArgOperand(0)) != Opcode)
        continue;
      Changed |= Raise(*CI);
    }
  }
  return Changed;
}

/// Calls \p Raise on every `CallInst` in \p F calling the LLVM intrinsic
/// \p ID (already raised out of DXIL's `dx.op.*`/SPIR-V's `llvm.spv.*`
/// calling convention, by `feme::dxil::OpRaisingPass` or MLIR's SPIR-V ->
/// LLVM conversion respectively).
bool forEachIntrinsicCall(Function &F, Intrinsic::ID ID,
                          function_ref<bool(CallInst &)> Raise) {
  bool Changed = false;
  Module &M = *F.getParent();
  for (Function &Callee : llvm::make_early_inc_range(M.functions())) {
    if (Callee.getIntrinsicID() != ID)
      continue;
    for (User *U : llvm::make_early_inc_range(Callee.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getFunction() != &F)
        continue;
      Changed |= Raise(*CI);
    }
  }
  return Changed;
}

/// Casts \p V to `i32` if it is not already, for the `i8` column operand
/// `dx.op.loadInput`/`storeOutput`/the pull-model interpolation family use,
/// so every `feme.stage.*` builder can uniformly take `i32` row/component
/// operands (see StageOps.h).
Value *toI32(IRBuilderBase &B, Value *V) {
  if (V->getType()->isIntegerTy(32))
    return V;
  return B.CreateZExt(V, B.getInt32Ty());
}

/// Rewrites \p F's `dx.op.loadInput`/`storeOutput` calls (opcodes 4 and 5;
/// unraised by `feme::dxil::OpRaisingPass`, since they need signature
/// context that pass does not have) into `feme.stage.input.load`/
/// `output.store`, and its already-raised `llvm.dx.discard`/derivative/
/// quad-read intrinsic calls into their `feme.stage.*` peers. Also raises
/// `IsHelperLane` (221) and the pull-model interpolation family
/// (`EvalCentroid`/`EvalSampleIndex`/`EvalSnapped`, opcodes 89/88/87)
/// directly, since neither has an LLVM intrinsic form to raise through
/// first.
bool canonicalizeDXILStage(Function &F, const EntrySignature &Sig) {
  bool Changed = false;
  SmallVector<uint32_t> InputIDs =
      collectElementIDsByDirection(Sig, SignatureDirection::Input);
  SmallVector<uint32_t> OutputIDs =
      collectElementIDsByDirection(Sig, SignatureDirection::Output);

  Changed |= forEachDXOpCall(F, 4, [&](CallInst &CI) { // LoadInput
    if (CI.arg_size() != 5)
      return false;
    std::optional<uint64_t> DXILID = getConstInt(CI.getArgOperand(1));
    if (!DXILID)
      return false;
    std::optional<uint32_t> ElementID = resolveElementID(InputIDs, *DXILID);
    if (!ElementID)
      return false;
    IRBuilder<> B(&CI);
    Value *Row = toI32(B, CI.getArgOperand(2));
    Value *Col = toI32(B, CI.getArgOperand(3));
    Value *Vertex = toI32(B, CI.getArgOperand(4));
    CallInst *New = createStageInputLoad(B, CI.getType(), *ElementID, Row, Col,
                                         Vertex, CI.getName());
    CI.replaceAllUsesWith(New);
    CI.eraseFromParent();
    return true;
  });

  Changed |= forEachDXOpCall(F, 5, [&](CallInst &CI) { // StoreOutput
    if (CI.arg_size() != 5)
      return false;
    std::optional<uint64_t> DXILID = getConstInt(CI.getArgOperand(1));
    if (!DXILID)
      return false;
    std::optional<uint32_t> ElementID = resolveElementID(OutputIDs, *DXILID);
    if (!ElementID)
      return false;
    IRBuilder<> B(&CI);
    Value *Row = toI32(B, CI.getArgOperand(2));
    Value *Col = toI32(B, CI.getArgOperand(3));
    Value *Val = CI.getArgOperand(4);
    Value *Vertex = B.getInt32(0);
    createStageOutputStore(B, *ElementID, Row, Col, Val, Vertex);
    CI.eraseFromParent();
    return true;
  });

  Changed |= forEachDXOpCall(F, 221, [](CallInst &CI) { // IsHelperLane
    IRBuilder<> B(&CI);
    CallInst *New = createStageIsHelper(B);
    CI.replaceAllUsesWith(New);
    CI.eraseFromParent();
    return true;
  });

  auto raiseEval = [&](CallInst &CI, StageOpKind Kind, unsigned ExpectedArgs) {
    if (CI.arg_size() != ExpectedArgs)
      return false;
    std::optional<uint64_t> DXILID = getConstInt(CI.getArgOperand(1));
    if (!DXILID)
      return false;
    std::optional<uint32_t> ElementID = resolveElementID(InputIDs, *DXILID);
    if (!ElementID)
      return false;
    IRBuilder<> B(&CI);
    // Operand 2 (row) always selects the same row an ordinary input load
    // of this element would; the pull model still evaluates one signature
    // element, just at a different location than its declared
    // interpolation, so it does not appear as a separate `feme.stage.*`
    // operand (see StageOpKind::InterpolateAt*'s comment).
    Value *Col = toI32(B, CI.getArgOperand(3));
    CallInst *New = nullptr;
    switch (Kind) {
    case StageOpKind::InterpolateAtCentroid:
      New = createStageInterpolateAtCentroid(B, CI.getType(), *ElementID, Col);
      break;
    case StageOpKind::InterpolateAtSample:
      New = createStageInterpolateAtSample(B, CI.getType(), *ElementID, Col,
                                           toI32(B, CI.getArgOperand(4)));
      break;
    case StageOpKind::InterpolateAtOffset:
      New = createStageInterpolateAtOffset(B, CI.getType(), *ElementID, Col,
                                           toI32(B, CI.getArgOperand(4)),
                                           toI32(B, CI.getArgOperand(5)));
      break;
    default:
      llvm_unreachable("not an interpolate-at StageOpKind");
    }
    CI.replaceAllUsesWith(New);
    CI.eraseFromParent();
    return true;
  };
  Changed |= forEachDXOpCall(F, 89, [&](CallInst &CI) { // EvalCentroid
    return raiseEval(CI, StageOpKind::InterpolateAtCentroid, 4);
  });
  Changed |= forEachDXOpCall(F, 88, [&](CallInst &CI) { // EvalSampleIndex
    return raiseEval(CI, StageOpKind::InterpolateAtSample, 5);
  });
  Changed |= forEachDXOpCall(F, 87, [&](CallInst &CI) { // EvalSnapped
    return raiseEval(CI, StageOpKind::InterpolateAtOffset, 6);
  });

  // The remaining ops are already raised to generic `llvm.dx.*` intrinsics
  // by `feme::dxil::OpRaisingPass` (context-free, so it does not need to
  // know this is a fragment entry point); this pass only needs to rename
  // them into the `feme.stage.*` family, since they're already legal LLVM
  // IR shaped exactly like their `feme.stage.*` peer.
  Changed |= forEachIntrinsicCall(F, Intrinsic::dx_discard, [](CallInst &CI) {
    IRBuilder<> B(&CI);
    createStageDiscard(B, CI.getArgOperand(0));
    CI.eraseFromParent();
    return true;
  });
  static const std::pair<Intrinsic::ID, StageOpKind> DerivativeMappings[] = {
      {Intrinsic::dx_ddx_fine, StageOpKind::DerivativeXFine},
      {Intrinsic::dx_ddy_fine, StageOpKind::DerivativeYFine},
      {Intrinsic::dx_ddx_coarse, StageOpKind::DerivativeXCoarse},
      {Intrinsic::dx_ddy_coarse, StageOpKind::DerivativeYCoarse},
  };
  for (const auto &Mapping : DerivativeMappings) {
    Intrinsic::ID ID = Mapping.first;
    StageOpKind Kind = Mapping.second;
    Changed |= forEachIntrinsicCall(F, ID, [&](CallInst &CI) {
      IRBuilder<> B(&CI);
      CallInst *New = createStageDerivative(B, Kind, CI.getArgOperand(0));
      CI.replaceAllUsesWith(New);
      CI.eraseFromParent();
      return true;
    });
  }
  static const std::pair<Intrinsic::ID, uint8_t> QuadReadMappings[] = {
      {Intrinsic::dx_quad_read_across_x, 0},
      {Intrinsic::dx_quad_read_across_y, 1},
      {Intrinsic::dx_quad_read_across_diagonal, 2},
  };
  for (const auto &Mapping : QuadReadMappings) {
    Intrinsic::ID ID = Mapping.first;
    uint8_t Direction = Mapping.second;
    Changed |= forEachIntrinsicCall(F, ID, [&](CallInst &CI) {
      IRBuilder<> B(&CI);
      CallInst *New = createStageQuadRead(B, CI.getArgOperand(0), Direction);
      CI.replaceAllUsesWith(New);
      CI.eraseFromParent();
      return true;
    });
  }
  return Changed;
}

/// The SPIR-V decoration codes `feme::spirv::attachStageIODecorations`
/// writes into a stage-IO global's `!spirv.Decorations` metadata (see
/// `buildStageIODecorationsAttr` in
/// feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp, the writer side
/// of this same encoding).
enum SPIRVDecorationCode : uint32_t {
  SPIRVDecorationBuiltIn = 11,
  SPIRVDecorationNoPerspective = 13,
  SPIRVDecorationFlat = 14,
  SPIRVDecorationPatch = 15,
  SPIRVDecorationCentroid = 16,
  SPIRVDecorationSample = 17,
  SPIRVDecorationLocation = 30,
  SPIRVDecorationComponent = 31,
  SPIRVDecorationIndex = 32,
  SPIRVDecorationPerPrimitiveEXT = 5271,
};

std::optional<uint64_t> getConstMDInt(const Metadata *MD) {
  const auto *CAM = dyn_cast_or_null<ConstantAsMetadata>(MD);
  if (!CAM)
    return std::nullopt;
  const auto *CI = dyn_cast<ConstantInt>(CAM->getValue());
  if (!CI)
    return std::nullopt;
  return CI->getZExtValue();
}

/// A stage-IO global's decorations, parsed out of its `!spirv.Decorations`
/// metadata (see `SPIRVDecorationCode`).
struct ParsedSPIRVDecorations {
  std::optional<uint32_t> BuiltIn;
  std::optional<uint32_t> Location;
  std::optional<uint32_t> Component;
  /// The `Index` decoration's operand (Vulkan's dual-source-blend model,
  /// SPIR-V/GLSL's `Index` layout qualifier): 0 if absent, matching
  /// `SignatureElement::Index`'s own "0 for every ordinary output"
  /// default.
  uint32_t Index = 0;
  bool NoPerspective = false;
  bool Flat = false;
  bool Patch = false;
  bool Centroid = false;
  bool Sample = false;
  bool PerPrimitive = false;
};

ParsedSPIRVDecorations parseSPIRVDecorations(const MDNode *MD) {
  ParsedSPIRVDecorations Result;
  if (!MD)
    return Result;
  for (const MDOperand &Op : MD->operands()) {
    const auto *Entry = dyn_cast_or_null<MDNode>(Op.get());
    if (!Entry || Entry->getNumOperands() == 0)
      continue;
    std::optional<uint64_t> Code = getConstMDInt(Entry->getOperand(0));
    if (!Code)
      continue;
    std::optional<uint64_t> Arg = Entry->getNumOperands() > 1
                                      ? getConstMDInt(Entry->getOperand(1))
                                      : std::nullopt;
    switch (*Code) {
    case SPIRVDecorationBuiltIn:
      if (Arg)
        Result.BuiltIn = static_cast<uint32_t>(*Arg);
      break;
    case SPIRVDecorationLocation:
      if (Arg)
        Result.Location = static_cast<uint32_t>(*Arg);
      break;
    case SPIRVDecorationComponent:
      if (Arg)
        Result.Component = static_cast<uint32_t>(*Arg);
      break;
    case SPIRVDecorationIndex:
      if (Arg)
        Result.Index = static_cast<uint32_t>(*Arg);
      break;
    case SPIRVDecorationNoPerspective:
      Result.NoPerspective = true;
      break;
    case SPIRVDecorationFlat:
      Result.Flat = true;
      break;
    case SPIRVDecorationPatch:
      Result.Patch = true;
      break;
    case SPIRVDecorationCentroid:
      Result.Centroid = true;
      break;
    case SPIRVDecorationSample:
      Result.Sample = true;
      break;
    case SPIRVDecorationPerPrimitiveEXT:
      Result.PerPrimitive = true;
      break;
    default:
      // Any decoration this milestone does not model yet (e.g. a future
      // array-of-blocks per-vertex/per-primitive shape) is preserved on
      // the global itself and simply not reflected into
      // `feme::SignatureElement`, which has no field for it yet.
      break;
    }
  }
  return Result;
}

/// (Roadmap H2d) Parses a builtin interface block's (e.g. `gl_PerVertex`)
/// own `feme.spirv.MemberDecorations` metadata --
/// `feme::spirv::attachStageIOMemberDecorations`'s
/// `!{!{i32 memberIndex, !{decoration...}}, ...}` shape (see
/// StageIODecorations.cpp) -- into a per-struct-member-index table of
/// `ParsedSPIRVDecorations`, reusing `parseSPIRVDecorations` for each
/// member's own decoration list (the same shape `!spirv.Decorations`
/// itself uses). A member with no entry (this milestone's own filtering,
/// or simply an undecorated member) is absent from the result, and
/// `DenseMap::lookup` then yields a default-constructed
/// `ParsedSPIRVDecorations` (an ordinary, unlinkable varying) for it.
DenseMap<unsigned, ParsedSPIRVDecorations>
parseSPIRVMemberDecorations(const MDNode *MD) {
  DenseMap<unsigned, ParsedSPIRVDecorations> Result;
  if (!MD)
    return Result;
  for (const MDOperand &Op : MD->operands()) {
    const auto *Entry = dyn_cast_or_null<MDNode>(Op.get());
    if (!Entry || Entry->getNumOperands() != 2)
      continue;
    std::optional<uint64_t> Index = getConstMDInt(Entry->getOperand(0));
    if (!Index)
      continue;
    Result[static_cast<unsigned>(*Index)] =
        parseSPIRVDecorations(dyn_cast_or_null<MDNode>(Entry->getOperand(1)));
  }
  return Result;
}

/// The `feme::SignatureSystemValue` a SPIR-V `BuiltIn` decoration's value
/// names, or `None` for a builtin FeMe's signature model has no
/// representation for yet (`PointSize`, `PointCoord`, `SamplePosition`,
/// `DeviceIndex`, ...), which is then treated as an
/// ordinary -- and, having no `Location` either, unlinkable -- varying and
/// diagnosed by `feme::graphics::ValidateStagePass`/the executor rather
/// than silently mapped onto an unrelated system value. Numbering is the
/// SPIR-V specification's own `BuiltIn` enumeration; see
/// `buildStageIODecorationsAttr` in
/// feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp, the writer side
/// of this encoding.
///
/// `FragCoord` maps to `Position` because both APIs' fragment stages spell
/// the rasterizer-supplied window-space position that way (SV_Position in
/// Direct3D, `gl_FragCoord` in SPIR-V), and the CPU stage ABI sources it
/// from the fragment invocation record under that one identity. The two
/// vertex-index spellings likewise collapse: `VertexId`/`InstanceId` are
/// the OpenGL-flavored, non-base-relative forms of `VertexIndex`/
/// `InstanceIndex`, and Vulkan only ever produces the latter pair.
///
/// (Roadmap H2) `ViewIndex` (`gl_ViewIndex`, multiview's own builtin) now
/// maps to `SignatureSystemValue::ViewIndex` -- the "multiview ... family"
/// this comment used to list as unrepresented is down to just the device-
/// index one (`DeviceIndex`, `VK_KHR_device_group`, still unimplemented).
///
/// (Roadmap H2d) `ClipDistance`/`CullDistance` (`gl_ClipDistance`/
/// `gl_CullDistance`, `PointSize` (`gl_PointSize`) likewise) map to `None`:
/// none of the three has a real ABI-field consumer anywhere downstream
/// (`shaderClipDistance`/`shaderCullDistance` are still `VK_FALSE`, see
/// roadmap H7), the same "unmodeled system value" treatment an
/// unrecognized DXIL semantic already gets (`SignatureImport.cpp`'s own
/// `getSystemValue` default case). Before this milestone these two SPIR-V
/// `BuiltIn`s were unreachable in practice -- GLSL/SPIR-V never declares
/// either as a standalone variable, only as `gl_PerVertex` interface-block
/// members (H2a/H2c), which this function never saw until H2d's own
/// per-member decomposition -- so this is a change in *what* gets produced,
/// not in any previously-observable behavior.
SignatureSystemValue getSystemValueForBuiltIn(uint32_t BuiltIn) {
  switch (BuiltIn) {
  case 0:  // Position
  case 15: // FragCoord
    return SignatureSystemValue::Position;
  case 5:  // VertexId
  case 42: // VertexIndex
    return SignatureSystemValue::VertexID;
  case 6:  // InstanceId
  case 43: // InstanceIndex
    return SignatureSystemValue::InstanceID;
  case 7: // PrimitiveId
    return SignatureSystemValue::PrimitiveID;
  case 9: // Layer
    return SignatureSystemValue::RenderTargetArrayIndex;
  case 10: // ViewportIndex
    return SignatureSystemValue::ViewportArrayIndex;
  case 17: // FrontFacing
    return SignatureSystemValue::IsFrontFace;
  case 18: // SampleId
    return SignatureSystemValue::SampleIndex;
  case 20: // SampleMask
    return SignatureSystemValue::Coverage;
  case 22: // FragDepth
    return SignatureSystemValue::Depth;
  case 4424: // BaseVertex
    return SignatureSystemValue::BaseVertex;
  case 4425: // BaseInstance
    return SignatureSystemValue::BaseInstance;
  case 4426: // DrawIndex
    return SignatureSystemValue::DrawID;
  case 5014: // FragStencilRefEXT
    return SignatureSystemValue::StencilRef;
  case 4440: // ViewIndex
    return SignatureSystemValue::ViewIndex;
  default:
    return SignatureSystemValue::None;
  }
}

/// The interpolation-mode pairing "InterpolationMode" in
/// feme/docs/FeMeGraphicsDesign.md documents, from a stage-IO variable's
/// parsed boolean qualifiers -- mirrors `getInterpolationMode` in
/// feme/lib/Transforms/DXIL/SignatureImport.cpp, whose DXIL `InterpMode`
/// input this is the SPIR-V-decoration-shaped equivalent of.
SignatureInterpolationMode
getInterpolationMode(const ParsedSPIRVDecorations &D) {
  if (D.Flat)
    return SignatureInterpolationMode::Flat;
  if (D.NoPerspective) {
    if (D.Centroid)
      return SignatureInterpolationMode::NoPerspectiveCentroid;
    if (D.Sample)
      return SignatureInterpolationMode::NoPerspectiveSample;
    return SignatureInterpolationMode::NoPerspective;
  }
  if (D.Centroid)
    return SignatureInterpolationMode::PerspectiveCentroid;
  if (D.Sample)
    return SignatureInterpolationMode::PerspectiveSample;
  return SignatureInterpolationMode::Perspective;
}

/// The `(ComponentType, BitWidth)` pair a stage-IO global's scalar element
/// type corresponds to, mirroring `getComponentType` in SignatureImport.cpp
/// (DXIL's equivalent, from its own `DXIL::ComponentType` instead of an
/// LLVM `Type`).
std::pair<SignatureComponentType, uint32_t> getComponentType(Type *Scalar) {
  if (Scalar->isFloatTy())
    return {SignatureComponentType::Float, 32};
  if (Scalar->isDoubleTy())
    return {SignatureComponentType::Float, 64};
  if (Scalar->isHalfTy())
    return {SignatureComponentType::Float, 16};
  if (auto *IntTy = dyn_cast<IntegerType>(Scalar))
    return {SignatureComponentType::SInt, IntTy->getBitWidth()};
  // FeMe's model has no representation for anything else (aggregates,
  // pointers, ...) yet; default to a plain `f32` rather than crashing, for
  // `feme::graphics::ValidateStagePass` to flag as a mismatch instead.
  return {SignatureComponentType::Float, 32};
}

/// Peels a single-member `StructType` down to its one member's own type,
/// repeatedly. glslang wraps a `varying`-block *member* -- even a single
/// scalar/vector/matrix one -- in an outer one-member struct at the SPIR-V
/// level (`dEQP-VK.glsl.linkage.varying.struct.*`'s own shape: `{ [4 x <2 x
/// float>] }` for a `mat4x2` member, confirmed by inspecting the imported
/// global's LLVM type directly), the same "aggregate" half of this
/// milestone's "matrix/aggregate stage IO" bucket the plain-matrix
/// (`ArrayType`) handling above does not by itself cover. A struct with
/// more than one member has no single well-defined row/component shape and
/// is left alone (returned as-is, to fail exactly as before this change).
Type *peelSingleMemberStruct(Type *Ty) {
  while (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->getNumElements() != 1)
      break;
    Ty = ST->getElementType(0);
  }
  return Ty;
}

/// The per-row (a matrix's per-column, or a plain scalar/vector's own)
/// shape of a stage-IO variable's value type: its scalar element type and
/// how many of them make up one row. A plain scalar or `FixedVectorType`
/// has exactly one row (\p RowCount left at 1); an `ArrayType` -- the shape
/// SPIRVToLLVM's `spirv.MatrixType` conversion produces (see
/// SPIRVToLLVMPatterns.cpp's "MLIR upstream has no `spirv.MatrixType`"
/// comment: a matrix becomes `!llvm.array<Columns x VectorType>|scalar>`)
/// -- has one row per array element, each row itself a scalar or vector.
/// `ValueTy` is unwrapped through any single-member struct first (see
/// `peelSingleMemberStruct`).
struct StageIORowShape {
  Type *Scalar;
  unsigned ComponentCount;
  unsigned RowCount;
};

StageIORowShape getStageIORowShape(Type *ValueTy) {
  unsigned RowCount = 1;
  Type *PerRowTy = peelSingleMemberStruct(ValueTy);
  if (auto *ArrTy = dyn_cast<ArrayType>(PerRowTy)) {
    RowCount = ArrTy->getNumElements();
    PerRowTy = ArrTy->getElementType();
  }
  if (auto *VecTy = dyn_cast<FixedVectorType>(PerRowTy))
    return {VecTy->getElementType(), VecTy->getNumElements(), RowCount};
  return {PerRowTy, /*ComponentCount=*/1, RowCount};
}

/// Whether \p GV is a stage-IO variable -- the shape
/// `StageIOGlobalVariablePattern`/`feme::spirv::attachStageIODecorations`
/// (feme/lib/Conversion/SPIRVToLLVM/) produce: address space 7 (`Input`) or
/// 8 (`Output`), carrying either whole-variable `!spirv.Decorations`
/// metadata or (roadmap H2d) a builtin interface block's own per-member
/// `feme.spirv.MemberDecorations` metadata -- the shape a block variable
/// (e.g. `gl_PerVertex`) gets instead, having no whole-variable decoration
/// of its own (roadmap H2c). Sets \p AddrSpace to \p GV's address space
/// when true.
bool isSPIRVStageIOGlobal(const GlobalVariable *GV, unsigned &AddrSpace) {
  if (!GV)
    return false;
  AddrSpace = GV->getAddressSpace();
  if (AddrSpace != 7 && AddrSpace != 8)
    return false;
  return GV->getMetadata("spirv.Decorations") != nullptr ||
         GV->getMetadata("feme.spirv.MemberDecorations") != nullptr;
}

/// Recursively loads \p Ty's value out of stage-IO element \p ElementID,
/// one scalar `feme.stage.input.load` at a time: a single-member struct is
/// peeled first (see `peelSingleMemberStruct`) and rebuilt with
/// `insertvalue`; an array (a matrix's columns) is loaded one `Row` per
/// element and rebuilt with `insertvalue`; a vector is loaded one
/// `Component` per element and rebuilt with `insertelement`; anything else
/// is one scalar load. Mirrors `getStageIORowShape`'s own type recursion.
Value *loadStageIOValue(IRBuilderBase &B, Type *Ty, uint32_t ElementID,
                        Value *Row, Value *Component, Value *Zero,
                        const Twine &Name) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->getNumElements() == 1) {
      Value *Inner = loadStageIOValue(B, ST->getElementType(0), ElementID, Row,
                                      Component, Zero, Name);
      return B.CreateInsertValue(PoisonValue::get(ST), Inner, 0);
    }
  } else if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    Value *New = PoisonValue::get(ArrTy);
    for (unsigned R = 0, RE = ArrTy->getNumElements(); R != RE; ++R) {
      Value *RowVal = loadStageIOValue(B, ArrTy->getElementType(), ElementID,
                                       B.getInt32(R), Component, Zero, Name);
      New = B.CreateInsertValue(New, RowVal, R);
    }
    return New;
  } else if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    Value *New = PoisonValue::get(VecTy);
    for (unsigned C = 0, CE = VecTy->getNumElements(); C != CE; ++C) {
      Value *Elt = loadStageIOValue(B, VecTy->getElementType(), ElementID, Row,
                                    B.getInt32(C), Zero, Name);
      New = B.CreateInsertElement(New, Elt, C);
    }
    return New;
  }
  return createStageInputLoad(B, Ty, ElementID, Row, Component, Zero, Name);
}

/// The store-side mirror of `loadStageIOValue`: decomposes \p Val (of type
/// \p Ty) into one scalar `feme.stage.output.store` per (struct member,
/// row, component), the same recursion in reverse (`extractvalue`/
/// `extractelement` instead of `insertvalue`/`insertelement`).
void storeStageIOValue(IRBuilderBase &B, Value *Val, Type *Ty,
                       uint32_t ElementID, Value *Row, Value *Component,
                       Value *Zero) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->getNumElements() == 1) {
      storeStageIOValue(B, B.CreateExtractValue(Val, 0), ST->getElementType(0),
                        ElementID, Row, Component, Zero);
      return;
    }
  } else if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    for (unsigned R = 0, RE = ArrTy->getNumElements(); R != RE; ++R)
      storeStageIOValue(B, B.CreateExtractValue(Val, R),
                        ArrTy->getElementType(), ElementID, B.getInt32(R),
                        Component, Zero);
    return;
  } else if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    for (unsigned C = 0, CE = VecTy->getNumElements(); C != CE; ++C)
      storeStageIOValue(B, B.CreateExtractElement(Val, C),
                        VecTy->getElementType(), ElementID, Row, B.getInt32(C),
                        Zero);
    return;
  }
  createStageOutputStore(B, ElementID, Row, Component, Val, Zero);
}

/// (Roadmap H2d) The entry point into `loadStageIOValue`'s per-(struct
/// member, row, component) recursion for one stage-IO global: \p MemberIDs
/// holds one `ElementID` per struct member of a builtin interface block
/// (e.g. `gl_PerVertex`), or a single one for every other stage-IO global
/// (a plain scalar/vector/matrix/single-member-struct-wrapped value, still
/// one `SignatureElement`). An interface block's own outer struct layer is
/// unwrapped here, one level up from `loadStageIOValue`'s own recursion,
/// since each of its members routes through its own `ElementID` rather
/// than sharing the one `loadStageIOValue` alone would assume.
Value *loadStageIOBlockValue(IRBuilderBase &B, Type *Ty,
                             ArrayRef<uint32_t> MemberIDs, Value *Row,
                             Value *Component, Value *Zero, const Twine &Name) {
  if (MemberIDs.size() == 1)
    return loadStageIOValue(B, Ty, MemberIDs[0], Row, Component, Zero, Name);
  auto *ST = cast<StructType>(Ty);
  Value *New = PoisonValue::get(ST);
  for (unsigned I = 0, E = MemberIDs.size(); I != E; ++I) {
    Value *MemberVal = loadStageIOValue(B, ST->getElementType(I), MemberIDs[I],
                                        Row, Component, Zero, Name);
    New = B.CreateInsertValue(New, MemberVal, I);
  }
  return New;
}

/// The store-side mirror of `loadStageIOBlockValue`.
void storeStageIOBlockValue(IRBuilderBase &B, Value *Val, Type *Ty,
                            ArrayRef<uint32_t> MemberIDs, Value *Row,
                            Value *Component, Value *Zero) {
  if (MemberIDs.size() == 1) {
    storeStageIOValue(B, Val, Ty, MemberIDs[0], Row, Component, Zero);
    return;
  }
  auto *ST = cast<StructType>(Ty);
  for (unsigned I = 0, E = MemberIDs.size(); I != E; ++I)
    storeStageIOValue(B, B.CreateExtractValue(Val, I), ST->getElementType(I),
                      MemberIDs[I], Row, Component, Zero);
}

/// Rewrites \p F's SPIR-V-derived stage IR into `feme.stage.*`: its
/// `Input`/`Output` interface-variable loads/stores (address space 7/8,
/// see `isSPIRVStageIOGlobal`) into `feme.stage.input.load`/
/// `output.store` -- building and attaching this entry's
/// `feme::EntrySignature` along the way, the piece roadmap R19 explicitly
/// deferred to this milestone (see "Signature reflection" in
/// feme/docs/FeMeGraphicsDesign.md) -- and its already-legalized
/// `llvm.spv.discard`/derivative/quad-read intrinsic calls into their
/// `feme.stage.*` peers, mirroring `canonicalizeDXILStage`'s handling of the
/// same operations' DXIL-derived forms.
bool canonicalizeSPIRVStage(Function &F) {
  bool Changed = false;

  // Discover this entry's stage-IO globals in two passes -- inputs, then
  // outputs -- so their assigned `ElementID`s land in the same
  // inputs-before-outputs order `feme::dxil::convertEntrySignature` already
  // uses (see `collectElementIDsByDirection`'s comment), keeping the two
  // formats' numbering conventions consistent.
  SmallVector<GlobalVariable *> InputGlobals, OutputGlobals;
  DenseSet<GlobalVariable *> Seen;
  for (Instruction &I : instructions(F)) {
    GlobalVariable *GV = nullptr;
    if (auto *LI = dyn_cast<LoadInst>(&I))
      GV = dyn_cast<GlobalVariable>(LI->getPointerOperand());
    else if (auto *SI = dyn_cast<StoreInst>(&I))
      GV = dyn_cast<GlobalVariable>(SI->getPointerOperand());
    unsigned AddrSpace = 0;
    if (!isSPIRVStageIOGlobal(GV, AddrSpace) || !Seen.insert(GV).second)
      continue;
    (AddrSpace == 7 ? InputGlobals : OutputGlobals).push_back(GV);
  }

  DenseMap<GlobalVariable *, SmallVector<uint32_t, 1>> ElementIDs;
  if (!InputGlobals.empty() || !OutputGlobals.empty()) {
    EntrySignature Sig = dxil::getEntrySignature(F).value_or(EntrySignature{});
    uint32_t NextID = Sig.Elements.size();
    // Appends one `SignatureElement` for \p GV (or one of its struct
    // members, for a builtin interface block -- see the `addElements`
    // lambda below), sourced from \p D's decorations and \p ValueTy's own
    // scalar/vector/matrix/single-member-struct shape.
    auto addElement = [&](GlobalVariable *GV, SignatureDirection Dir,
                          const ParsedSPIRVDecorations &D, Type *ValueTy) {
      SignatureElement Elt;
      Elt.ElementID = NextID;
      Elt.Direction = Dir;
      Elt.Location = D.Location;
      Elt.Index = D.Index;
      if (D.BuiltIn)
        Elt.SystemValue = getSystemValueForBuiltIn(*D.BuiltIn);

      StageIORowShape Shape = getStageIORowShape(ValueTy);
      std::tie(Elt.ComponentType, Elt.BitWidth) =
          getComponentType(Shape.Scalar);
      Elt.FirstComponent = D.Component.value_or(0);
      Elt.ComponentCount = Shape.ComponentCount;
      Elt.RowCount = Shape.RowCount;
      Elt.Interpolation = getInterpolationMode(D);
      Elt.Frequency = D.PerPrimitive ? SignatureFrequency::PerPrimitive
                      : D.Patch      ? SignatureFrequency::PerPatch
                                     : SignatureFrequency::PerVertex;

      Sig.Elements.push_back(Elt);
      ElementIDs[GV].push_back(NextID);
      ++NextID;
    };
    auto addElements = [&](ArrayRef<GlobalVariable *> Globals,
                           SignatureDirection Dir) {
      for (GlobalVariable *GV : Globals) {
        // (Roadmap H2d) A builtin interface block (e.g. `gl_PerVertex`)
        // carries no whole-variable decoration of its own -- SPIR-V
        // decorates each of its struct members individually -- so it
        // decomposes into one `SignatureElement` per member instead of
        // the single one every other stage-IO global gets.
        if (const MDNode *MemberMD =
                GV->getMetadata("feme.spirv.MemberDecorations")) {
          auto *ST = cast<StructType>(GV->getValueType());
          DenseMap<unsigned, ParsedSPIRVDecorations> MemberDecorations =
              parseSPIRVMemberDecorations(MemberMD);
          for (unsigned I = 0, E = ST->getNumElements(); I != E; ++I)
            addElement(GV, Dir, MemberDecorations.lookup(I),
                       ST->getElementType(I));
          continue;
        }
        ParsedSPIRVDecorations D =
            parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
        addElement(GV, Dir, D, GV->getValueType());
      }
    };
    addElements(InputGlobals, SignatureDirection::Input);
    addElements(OutputGlobals, SignatureDirection::Output);
    dxil::setEntrySignature(F, Sig);
    Changed = true;
  }

  for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
    IRBuilder<> B(&I);
    Value *Zero = B.getInt32(0);
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      auto *GV = dyn_cast<GlobalVariable>(LI->getPointerOperand());
      auto It = GV ? ElementIDs.find(GV) : ElementIDs.end();
      if (It == ElementIDs.end())
        continue;
      // A scalar interface variable is one `feme.stage.input.load`; a
      // vector/matrix/single-member-struct-wrapped one is decomposed one
      // scalar at a time and rebuilt with `insertelement`/`insertvalue`,
      // matching both the `feme.stage.*` family's own per-(row, component)
      // operands and the scalar shape DXIL's `loadInput` always produces --
      // and, in turn, what `feme::cpu::SIMDizePass` widens (a whole
      // divergent aggregate/vector value has no widened form there). A
      // builtin interface block routes each of its own members through
      // its own `ElementID` first (roadmap H2d). See
      // `loadStageIOBlockValue`/`loadStageIOValue`/`getStageIORowShape`'s
      // shared type recursion.
      Value *New = loadStageIOBlockValue(B, LI->getType(), It->second, Zero,
                                         Zero, Zero, LI->getName());
      LI->replaceAllUsesWith(New);
      LI->eraseFromParent();
      Changed = true;
    } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
      auto *GV = dyn_cast<GlobalVariable>(SI->getPointerOperand());
      auto It = GV ? ElementIDs.find(GV) : ElementIDs.end();
      if (It == ElementIDs.end())
        continue;
      Value *Val = SI->getValueOperand();
      storeStageIOBlockValue(B, Val, Val->getType(), It->second, Zero, Zero,
                             Zero);
      SI->eraseFromParent();
      Changed = true;
    }
  }

  // `llvm.spv.discard` (SPIR-V's `OpKill`) is unconditional, unlike DXIL's
  // `Discard`/`feme.stage.discard`, which both always take a condition; a
  // constant-true condition preserves that meaning exactly.
  Changed |= forEachIntrinsicCall(F, Intrinsic::spv_discard, [](CallInst &CI) {
    IRBuilder<> B(&CI);
    createStageDiscard(B, B.getTrue());
    CI.eraseFromParent();
    return true;
  });

  // `llvm.spv.demote.to.helper.invocation` (SPIR-V's
  // `OpDemoteToHelperInvocation`, roadmap E11) is likewise unconditional,
  // and -- unlike `llvm.spv.discard`/`OpKill` -- non-terminating: it only
  // narrows the invocation's side-effect mask, matching
  // `feme.stage.demote`'s own semantics exactly (see StageOps.h), so it
  // needs no further adjustment beyond the same constant-true condition.
  Changed |= forEachIntrinsicCall(F, Intrinsic::spv_demote_to_helper_invocation,
                                  [](CallInst &CI) {
                                    IRBuilder<> B(&CI);
                                    createStageDemote(B, B.getTrue());
                                    CI.eraseFromParent();
                                    return true;
                                  });

  // SPIR-V's plain `OpDPdx`/`OpDPdy` (raised as `llvm.spv.ddx`/`.ddy`) leave
  // fine-vs-coarse precision to the implementation; this conservatively
  // maps them to the fine variant, matching `feme.stage.derivative.*`'s two
  // *explicit*-precision forms exactly and never coarsening precision the
  // source did not ask for.
  static const std::pair<Intrinsic::ID, StageOpKind> SPIRVDerivativeMappings[] =
      {
          {Intrinsic::spv_ddx, StageOpKind::DerivativeXFine},
          {Intrinsic::spv_ddy, StageOpKind::DerivativeYFine},
          {Intrinsic::spv_ddx_fine, StageOpKind::DerivativeXFine},
          {Intrinsic::spv_ddy_fine, StageOpKind::DerivativeYFine},
          {Intrinsic::spv_ddx_coarse, StageOpKind::DerivativeXCoarse},
          {Intrinsic::spv_ddy_coarse, StageOpKind::DerivativeYCoarse},
      };
  for (const auto &Mapping : SPIRVDerivativeMappings) {
    Intrinsic::ID ID = Mapping.first;
    StageOpKind Kind = Mapping.second;
    Changed |= forEachIntrinsicCall(F, ID, [&](CallInst &CI) {
      IRBuilder<> B(&CI);
      CallInst *New = createStageDerivative(B, Kind, CI.getArgOperand(0));
      CI.replaceAllUsesWith(New);
      CI.eraseFromParent();
      return true;
    });
  }

  static const std::pair<Intrinsic::ID, uint8_t> SPIRVQuadReadMappings[] = {
      {Intrinsic::spv_quad_read_across_x, 0},
      {Intrinsic::spv_quad_read_across_y, 1},
      {Intrinsic::spv_quad_read_across_diagonal, 2},
  };
  for (const auto &Mapping : SPIRVQuadReadMappings) {
    Intrinsic::ID ID = Mapping.first;
    uint8_t Direction = Mapping.second;
    Changed |= forEachIntrinsicCall(F, ID, [&](CallInst &CI) {
      IRBuilder<> B(&CI);
      CallInst *New = createStageQuadRead(B, CI.getArgOperand(0), Direction);
      CI.replaceAllUsesWith(New);
      CI.eraseFromParent();
      return true;
    });
  }

  return Changed;
}

} // namespace

PreservedAnalyses CanonicalizeStagePass::run(Module &M,
                                             ModuleAnalysisManager &AM) {
  bool Changed = false;
  for (Function &F : M) {
    std::optional<ShaderStage> Stage = getShaderStage(F);
    // G0 covers the vertex and fragment stages only (see the design's
    // "Canonical stage operations": "only operations required by
    // implemented stages are legal").
    if (!Stage ||
        (*Stage != ShaderStage::Vertex && *Stage != ShaderStage::Fragment))
      continue;

    // An absent signature (e.g. a hand-written test exercising only the
    // signature-independent rewrites below) is treated as an empty one:
    // `loadInput`/`storeOutput` then simply fail to resolve (left
    // unmodified, for `feme::graphics::ValidateStagePass` to diagnose),
    // while discard/derivative/quad-read/helper-lane rewriting -- which
    // needs no signature at all -- still proceeds.
    EntrySignature Sig = dxil::getEntrySignature(F).value_or(EntrySignature{});
    Changed |= canonicalizeDXILStage(F, Sig);
    Changed |= canonicalizeSPIRVStage(F);
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

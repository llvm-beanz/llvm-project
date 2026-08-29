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
#include "feme/Transforms/Graphics/StageIOGlobal.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
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
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

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
  case 8: // InvocationId
    return SignatureSystemValue::InvocationID;
  case 11: // TessLevelOuter
    return SignatureSystemValue::TessLevelOuter;
  case 12: // TessLevelInner
    return SignatureSystemValue::TessLevelInner;
  case 13: // TessCoord
    return SignatureSystemValue::TessCoord;
  case 14: // PatchVertices
    return SignatureSystemValue::PatchVertices;
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

/// (Roadmap H6i) Whether \p GV is a task entry's own bounded payload
/// variable: address space 14, the FeMe-only convention
/// `TaskPayloadGlobalVariablePattern`
/// (feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp) converts a
/// `TaskPayloadWorkgroupEXT`-storage-class `spirv.GlobalVariable` to
/// (roadmap H6h) -- unlike `isSPIRVStageIOGlobal`'s `Input`/`Output`
/// globals, this carries no `!spirv.Decorations`/
/// `!feme.spirv.MemberDecorations` metadata of its own (it is raw,
/// task-defined memory, not a signature element), so address space alone
/// identifies it.
bool isTaskPayloadGlobal(const GlobalVariable *GV) {
  return GV && GV->getAddressSpace() == 14;
}

/// (Roadmap H2e) Tracks, per (`ElementID`, `Row`, `Component`) leaf scalar
/// of an `Output`-direction stage-IO element, the shadow `AllocaInst` its
/// stores and read-back loads are redirected through. Unlike DXIL's
/// `storeOutput` (genuinely write-only), SPIR-V's `Output` storage class
/// permits reading back a value already written earlier in the same
/// invocation (e.g. a compound `gl_Position.x += 1.0`-shaped update), which
/// `feme.stage.input.load`/`.output.store`'s Input-vs-Output dichotomy has
/// no representation for. Routing both sides through an ordinary
/// `AllocaInst` instead -- one per leaf scalar, since that is the
/// granularity `loadStageIOValue`/`storeStageIOValue`'s own recursion
/// already decomposes every access to -- lets `PromoteMemToReg`
/// (`canonicalizeSPIRVStage`, once every instruction has been rewritten)
/// do the dominance-correct SSA construction a hand-rolled "last stored
/// value" forward walk could not: a read-back on one control-flow path may
/// not be dominated by a write on another, exactly the shape a compiler's
/// own `mem2reg` pass -- not a linear scan -- is built to resolve.
class ShadowValueMap {
public:
  explicit ShadowValueMap(Function &F) : F(F) {}

  AllocaInst *getOrCreate(uint32_t ElementID, Value *Row, Value *Component,
                          Type *Ty) {
    Key K{ElementID, cast<ConstantInt>(Row)->getZExtValue(),
          cast<ConstantInt>(Component)->getZExtValue()};
    AllocaInst *&Slot = Allocas[K];
    if (!Slot) {
      IRBuilder<> EntryBuilder(&F.getEntryBlock(),
                               F.getEntryBlock().getFirstInsertionPt());
      Slot = EntryBuilder.CreateAlloca(Ty, nullptr, "feme.stage.output.shadow");
    }
    return Slot;
  }

  bool empty() const { return Allocas.empty(); }

  /// All shadow allocas created so far, for `PromoteMemToReg` to convert to
  /// SSA form once every instruction has been rewritten.
  SmallVector<AllocaInst *, 8> takeAllocas() const {
    SmallVector<AllocaInst *, 8> Result;
    for (const auto &KV : Allocas)
      Result.push_back(KV.second);
    return Result;
  }

private:
  using Key = std::tuple<uint32_t, uint64_t, uint64_t>;
  Function &F;
  DenseMap<Key, AllocaInst *> Allocas;
};

/// Recursively loads \p Ty's value out of stage-IO element \p ElementID,
/// one scalar `feme.stage.input.load` at a time: a single-member struct is
/// peeled first (see `peelSingleMemberStruct`) and rebuilt with
/// `insertvalue`; an array (a matrix's columns) is loaded one `Row` per
/// element and rebuilt with `insertvalue`; a vector is loaded one
/// `Component` per element and rebuilt with `insertelement`; anything else
/// is one scalar load. Mirrors `getStageIORowShape`'s own type recursion.
/// (Roadmap H2e) When \p Shadow is non-null -- an `Output`-direction
/// element being read back -- the terminal scalar load reads that leaf's
/// own shadow alloca instead of emitting a (semantically wrong-direction)
/// `feme.stage.input.load`.
Value *loadStageIOValue(IRBuilderBase &B, Type *Ty, uint32_t ElementID,
                        Value *Row, Value *Component, Value *Zero,
                        const Twine &Name, ShadowValueMap *Shadow) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->getNumElements() == 1) {
      Value *Inner = loadStageIOValue(B, ST->getElementType(0), ElementID, Row,
                                      Component, Zero, Name, Shadow);
      return B.CreateInsertValue(PoisonValue::get(ST), Inner, 0);
    }
  } else if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    Value *New = PoisonValue::get(ArrTy);
    for (unsigned R = 0, RE = ArrTy->getNumElements(); R != RE; ++R) {
      Value *RowVal =
          loadStageIOValue(B, ArrTy->getElementType(), ElementID, B.getInt32(R),
                           Component, Zero, Name, Shadow);
      New = B.CreateInsertValue(New, RowVal, R);
    }
    return New;
  } else if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    Value *New = PoisonValue::get(VecTy);
    for (unsigned C = 0, CE = VecTy->getNumElements(); C != CE; ++C) {
      Value *Elt = loadStageIOValue(B, VecTy->getElementType(), ElementID, Row,
                                    B.getInt32(C), Zero, Name, Shadow);
      New = B.CreateInsertElement(New, Elt, C);
    }
    return New;
  }
  if (Shadow)
    return B.CreateLoad(Ty, Shadow->getOrCreate(ElementID, Row, Component, Ty),
                        Name);
  return createStageInputLoad(B, Ty, ElementID, Row, Component, Zero, Name);
}

/// The store-side mirror of `loadStageIOValue`: decomposes \p Val (of type
/// \p Ty) into one scalar `feme.stage.output.store` per (struct member,
/// row, component), the same recursion in reverse (`extractvalue`/
/// `extractelement` instead of `insertvalue`/`insertelement`). (Roadmap
/// H2e) When \p Shadow is non-null, each terminal scalar store also writes
/// through to that leaf's own shadow alloca, so a later read-back of the
/// same element (see `loadStageIOValue`) resolves to it.
void storeStageIOValue(IRBuilderBase &B, Value *Val, Type *Ty,
                       uint32_t ElementID, Value *Row, Value *Component,
                       Value *Zero, ShadowValueMap *Shadow) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->getNumElements() == 1) {
      storeStageIOValue(B, B.CreateExtractValue(Val, 0), ST->getElementType(0),
                        ElementID, Row, Component, Zero, Shadow);
      return;
    }
  } else if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    for (unsigned R = 0, RE = ArrTy->getNumElements(); R != RE; ++R)
      storeStageIOValue(B, B.CreateExtractValue(Val, R),
                        ArrTy->getElementType(), ElementID, B.getInt32(R),
                        Component, Zero, Shadow);
    return;
  } else if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    for (unsigned C = 0, CE = VecTy->getNumElements(); C != CE; ++C)
      storeStageIOValue(B, B.CreateExtractElement(Val, C),
                        VecTy->getElementType(), ElementID, Row, B.getInt32(C),
                        Zero, Shadow);
    return;
  }
  createStageOutputStore(B, ElementID, Row, Component, Val, Zero);
  if (Shadow)
    B.CreateStore(Val, Shadow->getOrCreate(ElementID, Row, Component, Ty));
}

/// (Roadmap H2g) SPIR-V's clip-space Y increases downward, matching
/// Vulkan's own window-space convention exactly -- but
/// `feme::graphics::Executor::executeDraws`'s viewport transform
/// (`projectVertex`) assumes the opposite, Y-up convention (a real
/// `dEQP-VK.multiview` run found it flips `NdcY` before scaling into
/// window space), matching DXIL/HLSL's own clip space instead -- the only
/// other producer of a `SignatureSystemValue::Position` *output* (a
/// fragment stage's `Position` *input*, `gl_FragCoord`/`SV_Position`, is
/// already a genuine window-space value in both APIs and must not be
/// touched here, which is why this is only ever called from the store
/// side). A SPIR-V vertex shader's `gl_Position` write is negated here
/// first, so it reaches the shared executor already in the same Y-up
/// convention DXIL's `SV_Position` output has, rather than the two
/// producers disagreeing and only one of them (DXIL) coming out the
/// executor's own flip the right way up. \p Component is the statically-
/// known component this store addresses, or `nullptr` for a whole-vector
/// store (`resolveStageIOAccess`'s own convention, shared with
/// `storeStageIOValue`'s own recursion): a whole vector negates lane 1;
/// a single scalar component only negates when it is component 1 (a
/// dynamically-indexed `gl_Position[i]` write, `Component` not a constant,
/// is left alone -- vanishingly rare for a system-value position write,
/// and unsupported by this milestone).
Value *negateSystemValuePositionY(IRBuilderBase &B, Value *Val,
                                  Value *Component) {
  if (!Component) {
    auto *VecTy = dyn_cast<FixedVectorType>(Val->getType());
    if (!VecTy || VecTy->getNumElements() <= 1)
      return Val;
    Value *NegY = B.CreateFNeg(B.CreateExtractElement(Val, 1));
    return B.CreateInsertElement(Val, NegY, 1);
  }
  auto *CI = dyn_cast<ConstantInt>(Component);
  if (CI && CI->getZExtValue() == 1)
    return B.CreateFNeg(Val);
  return Val;
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
                             Value *Component, Value *Zero, const Twine &Name,
                             ShadowValueMap *Shadow) {
  if (MemberIDs.size() == 1)
    return loadStageIOValue(B, Ty, MemberIDs[0], Row, Component, Zero, Name,
                            Shadow);
  auto *ST = cast<StructType>(Ty);
  Value *New = PoisonValue::get(ST);
  for (unsigned I = 0, E = MemberIDs.size(); I != E; ++I) {
    Value *MemberVal = loadStageIOValue(B, ST->getElementType(I), MemberIDs[I],
                                        Row, Component, Zero, Name, Shadow);
    New = B.CreateInsertValue(New, MemberVal, I);
  }
  return New;
}

/// The store-side mirror of `loadStageIOBlockValue`.
void storeStageIOBlockValue(IRBuilderBase &B, Value *Val, Type *Ty,
                            ArrayRef<uint32_t> MemberIDs, Value *Row,
                            Value *Component, Value *Zero,
                            ShadowValueMap *Shadow) {
  if (MemberIDs.size() == 1) {
    storeStageIOValue(B, Val, Ty, MemberIDs[0], Row, Component, Zero, Shadow);
    return;
  }
  auto *ST = cast<StructType>(Ty);
  for (unsigned I = 0, E = MemberIDs.size(); I != E; ++I)
    storeStageIOValue(B, B.CreateExtractValue(Val, I), ST->getElementType(I),
                      MemberIDs[I], Row, Component, Zero, Shadow);
}

/// The stage-IO global \p Ptr addresses, and the byte offset within it:
/// unwraps any chain of (possibly `ConstantExpr`) `getelementptr`s via
/// `Value::stripAndAccumulateConstantOffsets`, since LLVM canonicalizes a
/// builtin interface block's own per-member/per-component access into a
/// raw byte-offset form (`getelementptr (i8, ptr @block, i64 N)`, a
/// `ConstantExpr` rather than a `GetElementPtrInst` -- confirmed against a
/// real `dEQP-VK.multiview` vertex shader's `gl_Position.y` write) rather
/// than the struct-member-indexed shape (`getelementptr StructTy, ptr
/// @block, i32 0, i32 M`) a naive `GetElementPtrInst`-only walk would
/// expect. Returns `std::nullopt` if \p Ptr does not resolve to a constant
/// offset from a `GlobalVariable` at all (e.g. a dynamically-indexed
/// access, left unresolved for `feme::graphics::ValidateStagePass` to
/// diagnose, matching every other unresolved case in this pass).
std::optional<std::pair<GlobalVariable *, uint64_t>>
getStageIOBaseAndOffset(Value *Ptr, const DataLayout &DL) {
  APInt Offset(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
  Value *Base = Ptr->stripAndAccumulateConstantOffsets(
      DL, Offset, /*AllowNonInbounds=*/true);
  auto *GV = dyn_cast<GlobalVariable>(Base);
  if (!GV)
    return std::nullopt;
  return std::make_pair(GV, Offset.getZExtValue());
}

/// (Roadmap H5b/H5f) Whether \p GV is the exact shape a geometry entry's
/// per-vertex-arrayed `Input` global takes (`gl_in[]`-shaped: either the
/// `gl_PerVertex` builtin block itself, or a plain user-defined varying --
/// GLSL/SPIR-V always arrays *every* input of a geometry entry point at
/// `VerticesPerPrimitive`-many elements for that stage): a stage-IO
/// `Input`-storage-class (address space 7) global whose own declared type
/// is directly an `ArrayType`. This is a purely structural check -- it
/// cannot (and, matching `getDynamicVertexIndexedAccess`'s own precedent,
/// does not try to) tell this shape apart from a real per-vertex matrix
/// attribute of some other stage, which takes the exact same IR shape;
/// `feme::graphics::ValidateStagePass`'s `validateVertex` is what actually
/// diagnoses a non-Geometry stage's use of the resulting non-constant
/// `Vertex` operand. Shared between `getDynamicVertexIndexedAccess`'s own
/// non-constant-index recognition and `resolveStageIOAccess`'s ordinary
/// constant-offset path, so a *constant* `gl_in[k]` index is folded into
/// the same `Vertex` operand a non-constant one is (roadmap H5f), not
/// into `Row`. Sets \p AddrSpace to \p GV's address space when true.
///
/// (Roadmap H6b) Deliberately kept `Input`-only (unlike
/// `isDynamicIndexedArrayGlobal` below, which also accepts `Output`):
/// unlike `Input`, a real `Output`-storage-class array already has a
/// legitimate constant-per-row access pattern in production use today (an
/// ordinary matrix output store's own per-row `getelementptr` --
/// `RewritesSPIRVArrayOutputStorePerElementByteOffset`'s own test
/// coverage), so folding a *constant* `Output`-array index into `Vertex`
/// here the same way `Input`'s is would misroute a real matrix's own
/// constant row index. Nothing analogous exists for `Input` (no
/// pre-existing SPIR-V import ever produces a per-row `getelementptr` into
/// a real `Input` matrix; `RewritesSPIRVMatrixInputLoadOneRowAtATime`
/// loads the whole matrix in one instruction and lets
/// `loadStageIOValue`'s own recursion split it apart instead), so H5f's
/// constant-fold extension stays safe there. See "Roadmap H6: what H6b
/// found, and why it stops here" in VulkanCTSReport.md.
bool isPerVertexArrayInputGlobal(const GlobalVariable *GV,
                                 unsigned &AddrSpace) {
  if (!isSPIRVStageIOGlobal(GV, AddrSpace) || AddrSpace != 7)
    return false;
  return isa<ArrayType>(GV->getValueType());
}

/// (Roadmap H6b) Whether \p GV is a stage-IO global's per-vertex- or
/// per-primitive-arrayed `Input` (address space 7, geometry's `gl_in[]`)
/// *or* `Output` (address space 8, a mesh entry's own
/// `gl_MeshVerticesEXT[]`/`gl_MeshPrimitivesEXT[]`, or a plain
/// user-defined `PerVertexEXT`/`PerPrimitiveEXT` varying) array -- the
/// same structural shape `isPerVertexArrayInputGlobal` recognizes, just
/// not restricted to `Input`. Used *only* by
/// `getDynamicVertexIndexedAccess`'s own genuinely-non-constant-index
/// recognition: unlike a constant array index (see
/// `isPerVertexArrayInputGlobal`'s own comment on why that stays
/// `Input`-only), a real shader's matrix/array access is essentially
/// always constant-indexed (GLSL/SPIR-V unrolls or otherwise folds a
/// compile-time-fixed dimension), so a *non-constant* index into an
/// `Output`-storage array is not expected to collide with any real,
/// already-supported matrix-output shape -- it is exactly the shape a
/// mesh entry's own per-vertex/per-primitive output write takes (indexed
/// by the invocation's own output slot, not a compile-time constant).
/// Sets \p AddrSpace to \p GV's address space when true.
bool isDynamicIndexedArrayGlobal(const GlobalVariable *GV,
                                 unsigned &AddrSpace) {
  if (!isSPIRVStageIOGlobal(GV, AddrSpace) ||
      (AddrSpace != 7 && AddrSpace != 8))
    return false;
  return isa<ArrayType>(GV->getValueType());
}

/// (Roadmap H5b/H6b) A geometry entry point's own per-vertex inputs
/// (`gl_in[]`-shaped: either the `gl_PerVertex` builtin block itself, or a
/// plain user-defined varying -- GLSL/SPIR-V always arrays *every* input
/// of a geometry entry point at `VerticesPerPrimitive`-many elements for
/// that stage) are read through a genuinely dynamic index, the shader's
/// own loop-carried vertex-in-primitive counter -- unlike a matrix's `Row`
/// dimension (`getStageIORowShape`'s own `RowCount`), which is always a
/// compile-time-fixed `ArrayType` extent. (Roadmap H6b) A mesh entry
/// point's own per-vertex/per-primitive outputs
/// (`gl_MeshVerticesEXT[]`/`gl_MeshPrimitivesEXT[]`-shaped, or a plain
/// user-defined `PerVertexEXT`/`PerPrimitiveEXT` varying) are *written*
/// through the exact same shape, indexed by the invocation's own
/// per-vertex/per-primitive output slot rather than a loop-carried
/// counter -- the mirror image of geometry's read side, on `Output`
/// storage (address space 8) rather than `Input` (7). Either way,
/// `getStageIOBaseAndOffset`'s own `stripAndAccumulateConstantOffsets`
/// walk cannot fold a non-constant GEP index at all, so it stops at (and
/// returns) the GEP itself rather than the underlying global -- exactly
/// why a `gl_in[i]`- or `gl_MeshVerticesEXT[i]`-shaped access resolved to
/// `std::nullopt` (left unrewritten) before this.
///
/// This recognizes that one specific shape instead: a `GetElementPtrInst`
/// whose pointer operand is directly a stage-IO (`Input`- or
/// `Output`-storage-class, address space 7 or 8) global variable's own
/// outer array dimension -- its first index constant zero (ordinary
/// pointer-to-aggregate arithmetic), its second a non-constant `Value*`
/// (the vertex/primitive index) -- with every further index, if any,
/// constant (a builtin interface block's own member, or a matrix row
/// within that one vertex's own value), resolved into a byte offset the
/// same way `resolveRowComponent` already does for the ordinary
/// constant-offset path, just starting one array dimension in. (Roadmap
/// H5f) A constant vertex index is *not* left unresolved here:
/// `resolveStageIOAccess`'s own ordinary constant-offset path
/// (`getStageIOBaseAndOffset`) folds it in too, using
/// `isPerVertexArrayInputGlobal` (below) to recognize the same global
/// shape and route that constant index through `Vertex` there as well,
/// for consistency with the dynamic case this function handles.
/// Returns `std::nullopt` if \p Ptr is not this exact shape.
std::optional<std::tuple<GlobalVariable *, Value *, uint64_t>>
getDynamicVertexIndexedAccess(Value *Ptr, const DataLayout &DL) {
  auto *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  if (!GEP)
    return std::nullopt;
  auto *GV = dyn_cast<GlobalVariable>(GEP->getPointerOperand());
  unsigned AddrSpace = 0;
  if (!isDynamicIndexedArrayGlobal(GV, AddrSpace))
    return std::nullopt;
  auto *ArrTy = cast<ArrayType>(GV->getValueType());
  if (GEP->getNumIndices() < 2)
    return std::nullopt;

  auto IdxIt = GEP->idx_begin();
  auto *OuterIdx = dyn_cast<ConstantInt>(*IdxIt);
  if (!OuterIdx || !OuterIdx->isZero())
    return std::nullopt;
  Value *VertexIndex = *++IdxIt;
  if (isa<Constant>(VertexIndex))
    return std::nullopt; // The ordinary constant-offset path handles this.

  Type *CurTy = ArrTy->getElementType();
  uint64_t ByteOffset = 0;
  for (++IdxIt; IdxIt != GEP->idx_end(); ++IdxIt) {
    auto *CI = dyn_cast<ConstantInt>(*IdxIt);
    if (!CI)
      return std::nullopt; // Only the vertex index may be non-constant.
    uint64_t Idx = CI->getZExtValue();
    if (auto *ST = dyn_cast<StructType>(CurTy)) {
      const StructLayout *SL = DL.getStructLayout(ST);
      ByteOffset += SL->getElementOffset(Idx);
      CurTy = ST->getElementType(Idx);
    } else if (auto *InnerArrTy = dyn_cast<ArrayType>(CurTy)) {
      ByteOffset += Idx * DL.getTypeAllocSize(InnerArrTy->getElementType());
      CurTy = InnerArrTy->getElementType();
    } else {
      return std::nullopt;
    }
  }
  return std::make_tuple(GV, VertexIndex, ByteOffset);
}

/// The stage-IO global \p Ptr addresses, trying both
/// `getStageIOBaseAndOffset`'s constant-offset resolution and (roadmap
/// H5b) `getDynamicVertexIndexedAccess`'s dynamic-vertex-indexed one --
/// every place that only needs to discover *which* global a load/store
/// touches (as opposed to `resolveStageIOAccess`'s full per-instruction
/// resolution) goes through this so neither discovery loop below misses a
/// geometry entry's own `gl_in[i]`-shaped access.
GlobalVariable *getStageIOGlobal(Value *Ptr, const DataLayout &DL) {
  if (auto BaseAndOffset = getStageIOBaseAndOffset(Ptr, DL))
    return BaseAndOffset->first;
  if (auto Dyn = getDynamicVertexIndexedAccess(Ptr, DL))
    return std::get<0>(*Dyn);
  return nullptr;
}

/// One load/store's resolved stage-IO target: the `ElementID` it
/// addresses and the `Row`/`Component` operands to seed
/// `loadStageIOValue`/`storeStageIOValue`'s own recursion with (`nullptr`
/// selects the caller's own default, an ordinary `i32 0`) -- or, for a
/// whole builtin-interface-block aggregate access, every member's
/// `ElementID` at once, for `loadStageIOBlockValue`/
/// `storeStageIOBlockValue`'s own per-member decomposition.
/// (Roadmap H2e) Whether \p ElementIDs' global is `Output`-direction,
/// checked so a load resolving to one can be routed through
/// `ShadowValueMap` instead of a wrong-direction `feme.stage.input.load`.
/// (Roadmap H5b) \p Vertex is non-null for a dynamically-indexed
/// `gl_in[i]`-shaped access, the `Value*` to seed
/// `loadStageIOValue`/`storeStageIOValue`'s own `Vertex` operand with in
/// place of the caller's own default (an ordinary constant `i32 0`).
struct StageIOAccess {
  ArrayRef<uint32_t> ElementIDs;
  Value *Row = nullptr;
  Value *Component = nullptr;
  Value *Vertex = nullptr;
  bool IsOutput = false;
};

enum class SPIRVCanonicalPhase {
  Ordinary,
  HullControlPoint,
  HullPatchConstant,
};

struct SPIRVElementInfo {
  SignatureDirection Direction = SignatureDirection::Input;
  SignatureFrequency Frequency = SignatureFrequency::PerVertex;
  bool FromInputPatch = false;
  bool IsOutput = false;
};

bool isTessFactorSystemValue(SignatureSystemValue Sys) {
  return Sys == SignatureSystemValue::TessFactorEdge ||
         Sys == SignatureSystemValue::TessFactorInside;
}

bool isPatchOutputDecoration(const ParsedSPIRVDecorations &D) {
  if (!D.BuiltIn)
    return D.Patch;
  SignatureSystemValue Sys = getSystemValueForBuiltIn(*D.BuiltIn);
  return D.Patch || isTessFactorSystemValue(Sys);
}

bool isSPIRVGroupSyncBarrier(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return false;
  switch (Callee->getIntrinsicID()) {
  case Intrinsic::spv_group_memory_barrier_with_group_sync:
  case Intrinsic::spv_device_memory_barrier_with_group_sync:
  case Intrinsic::spv_all_memory_barrier_with_group_sync:
    return true;
  default:
    break;
  }
  // (roadmap H4b) A real SPIR-V import's own `OpControlBarrier` (the shape
  // a genuine tessellation-control module -- as opposed to the intrinsic
  // form above, which only ever comes from a DXIL/HLSL `GroupMemoryBarrier
  // WithGroupSync()` call -- actually produces) does not lower through
  // either of the `llvm.spv.*.barrier.with.group.sync` intrinsics above at
  // all: `feme::spirv::populateSPIRVToLLVMTargetPatterns` installs no
  // pattern of its own for `spirv::ControlBarrierOp`, so it falls through to
  // MLIR upstream's own default (`mlir/lib/Conversion/SPIRVToLLVM/
  // SPIRVToLLVM.cpp`'s `ControlBarrierPattern`), which lowers it to a call
  // to this exact mangled external declaration
  // (`__spirv_ControlBarrier(int ExecutionScope, int MemoryScope, int
  // Semantics)`) instead. Without recognizing this shape too,
  // `splitTessellationControlEntry` silently never finds the barrier a real
  // SPIR-V tessellation-control module's group sync actually compiles to,
  // and no patch-constant phase is ever split out of one -- a real,
  // previously-undiscovered gap in roadmap H4a's own barrier detection
  // (found while testing H4b's real `vkCreateGraphicsPipelines` path end to
  // end against an actual SPIR-V tessellation-control module rather than
  // the hand-written already-intrinsic-shaped LLVM IR H4a's own unit tests
  // used). Every control barrier is the one splitting point this pass
  // cares about regardless of its own execution/memory scope operands, so
  // no attempt is made to parse them.
  return Callee->getName() == "_Z22__spirv_ControlBarrieriii";
}

SPIRVElementInfo classifySPIRVElement(ShaderStage Stage,
                                      SPIRVCanonicalPhase Phase,
                                      unsigned AddrSpace,
                                      const ParsedSPIRVDecorations &D) {
  SPIRVElementInfo Info;
  Info.Frequency = D.PerPrimitive ? SignatureFrequency::PerPrimitive
                   : D.Patch      ? SignatureFrequency::PerPatch
                                  : SignatureFrequency::PerVertex;
  SignatureSystemValue Sys = D.BuiltIn ? getSystemValueForBuiltIn(*D.BuiltIn)
                                       : SignatureSystemValue::None;
  if (Stage == ShaderStage::Hull &&
      Phase == SPIRVCanonicalPhase::HullPatchConstant) {
    if (AddrSpace == 7) {
      Info.Direction = SignatureDirection::Input;
      Info.FromInputPatch = Sys == SignatureSystemValue::None ||
                            Sys == SignatureSystemValue::PatchVertices;
      if (Sys == SignatureSystemValue::PatchVertices)
        Info.Frequency = SignatureFrequency::PerPatch;
      return Info;
    }
    if (isPatchOutputDecoration(D)) {
      Info.Direction = SignatureDirection::PatchOutput;
      Info.Frequency = SignatureFrequency::PerPatch;
      Info.IsOutput = true;
      return Info;
    }
    Info.Direction = SignatureDirection::Input;
    return Info;
  }

  if (Stage == ShaderStage::Domain) {
    if (AddrSpace == 8) {
      Info.Direction = SignatureDirection::Output;
      Info.IsOutput = true;
      return Info;
    }
    if (Sys == SignatureSystemValue::DomainLocation ||
        Sys == SignatureSystemValue::PatchVertices) {
      Info.Direction = SignatureDirection::Input;
      if (Sys == SignatureSystemValue::PatchVertices)
        Info.Frequency = SignatureFrequency::PerPatch;
      return Info;
    }
    if (isPatchOutputDecoration(D)) {
      Info.Direction = SignatureDirection::PatchInput;
      Info.Frequency = SignatureFrequency::PerPatch;
      return Info;
    }
    Info.Direction = SignatureDirection::Input;
    return Info;
  }

  Info.Direction =
      AddrSpace == 7 ? SignatureDirection::Input : SignatureDirection::Output;
  Info.IsOutput = AddrSpace == 8;
  if (Stage == ShaderStage::Hull && Sys == SignatureSystemValue::PatchVertices)
    Info.Frequency = SignatureFrequency::PerPatch;
  return Info;
}

bool usesSPIRVStageIO(Function &F) {
  const DataLayout &DL = F.getParent()->getDataLayout();
  for (Instruction &I : instructions(F)) {
    GlobalVariable *GV = nullptr;
    if (auto *LI = dyn_cast<LoadInst>(&I))
      GV = getStageIOGlobal(LI->getPointerOperand(), DL);
    else if (auto *SI = dyn_cast<StoreInst>(&I))
      GV = getStageIOGlobal(SI->getPointerOperand(), DL);
    unsigned AddrSpace = 0;
    if (isSPIRVStageIOGlobal(GV, AddrSpace))
      return true;
  }
  return false;
}

/// The synthetic `Location` `splitTessellationControlEntry` (roadmap H4c)
/// should hand out to the first captured pre-barrier value it threads
/// through a new patch-shared global -- one past the highest `Location`
/// already decorating any stage-IO global (address space 7/8) anywhere in
/// \p M -- so a fabricated `Location` can never collide with a real
/// varying's own, in either the control-point or the patch-constant
/// phase's own independently-numbered signature.
unsigned computeNextSyntheticLocation(Module &M) {
  unsigned NextLocation = 0;
  auto Bump = [&](const ParsedSPIRVDecorations &D) {
    if (D.Location)
      NextLocation = std::max(NextLocation, *D.Location + 1);
  };
  for (const GlobalVariable &GV : M.globals()) {
    unsigned AddrSpace = 0;
    if (!isSPIRVStageIOGlobal(&GV, AddrSpace))
      continue;
    Bump(parseSPIRVDecorations(GV.getMetadata("spirv.Decorations")));
    if (const MDNode *MemberMD = GV.getMetadata("feme.spirv.MemberDecorations"))
      for (const auto &KV : parseSPIRVMemberDecorations(MemberMD))
        Bump(KV.second);
  }
  return NextLocation;
}

/// A `!spirv.Decorations` metadata node carrying a single `Location`
/// decoration, the same `{(code, arg)...}` shape `parseSPIRVDecorations`
/// reads (see `SPIRVDecorationCode`) -- built by hand here rather than by
/// the SPIR-V-to-LLVM conversion this pass otherwise only ever consumes,
/// since \p Location names a global this pass itself fabricates.
MDNode *createLocationDecoration(LLVMContext &Ctx, uint32_t Location) {
  Type *I32 = Type::getInt32Ty(Ctx);
  Metadata *Entry[] = {
      ConstantAsMetadata::get(ConstantInt::get(I32, SPIRVDecorationLocation)),
      ConstantAsMetadata::get(ConstantInt::get(I32, Location))};
  return MDNode::get(Ctx, {MDNode::get(Ctx, Entry)});
}

/// (Roadmap H4f) Whether every `Output`-direction (address-space-8)
/// stage-IO global \p F stores to is patch-frequency (`Patch`-decorated
/// or a tess-factor `BuiltIn`), and it stores to at least one -- the
/// shape a no-barrier tessellation-control entry point legally takes
/// whenever `OutputVertices == 1` (a single control-point invocation
/// needs no cross-invocation synchronization, so nothing meaningfully
/// distinguishes "per control point" from "per patch" here).
/// `dEQP-VK.tessellation.winding.*`'s own `layout(vertices = 1) out;`
/// tessellation-control shader, which writes only
/// `gl_TessLevelInner`/`gl_TessLevelOuter` and never touches `gl_out[]`
/// at all, is exactly this shape.
bool isPatchConstantOnlyEntry(Function &F) {
  const DataLayout &DL = F.getParent()->getDataLayout();
  bool SawPatchOutput = false;
  for (Instruction &I : instructions(F)) {
    auto *SI = dyn_cast<StoreInst>(&I);
    if (!SI)
      continue;
    auto BaseAndOffset = getStageIOBaseAndOffset(SI->getPointerOperand(), DL);
    if (!BaseAndOffset)
      continue;
    GlobalVariable *GV = BaseAndOffset->first;
    unsigned AddrSpace = 0;
    if (!isSPIRVStageIOGlobal(GV, AddrSpace) || AddrSpace != 8)
      continue;
    // A builtin interface block (e.g. `gl_PerVertex`) is always an
    // ordinary per-vertex output in practice -- `gl_TessLevelInner`/
    // `gl_TessLevelOuter` are plain globals, never interface-block
    // members -- so conservatively treat one as not patch-frequency
    // rather than teach this check the per-member decoration lookup
    // `canonicalizeSPIRVStage`'s own `addElements` lambda already has.
    if (GV->getMetadata("feme.spirv.MemberDecorations"))
      return false;
    ParsedSPIRVDecorations D =
        parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
    if (!isPatchOutputDecoration(D))
      return false;
    SawPatchOutput = true;
  }
  return SawPatchOutput;
}

/// (Roadmap H4f) `splitTessellationControlEntry`'s own barrier-based split
/// has nothing to split when \p F has no group-sync barrier at all, but
/// `compileAndValidateStages` (GraphicsPipeline.cpp) unconditionally
/// expects a `<entry>.patchconstant` sibling to exist regardless. When
/// `isPatchConstantOnlyEntry` holds, \p F is semantically already "the
/// patch-constant phase", so its whole body is moved into a new
/// `<entry>.patchconstant` clone, and \p F itself is replaced with a
/// trivial, empty control-point phase -- having no per-vertex output of
/// its own to produce. Any other no-barrier shape (a mix of patch- and
/// vertex-frequency writes, only legal for `OutputVertices == 1` too, but
/// not sound to auto-split the same way: with more than one output
/// control point and no barrier there is no legal way for one invocation
/// to see another's data either, so nothing but the current invocation's
/// own writes could safely be duplicated this way, which this simpler
/// check does not attempt to reason about) is left with no
/// patch-constant phase at all, matching this function's previous,
/// barrier-only behavior.
bool splitBarrierlessTessellationControlEntry(Function &F,
                                              Function *&PatchConstantPhase) {
  if (!isPatchConstantOnlyEntry(F))
    return true;

  PatchConstantPhase =
      Function::Create(F.getFunctionType(), F.getLinkage(), F.getAddressSpace(),
                       (F.getName() + ".patchconstant").str(), F.getParent());
  PatchConstantPhase->setComdat(F.getComdat());

  ValueToValueMapTy VMap;
  for (auto [OldArg, NewArg] : zip(F.args(), PatchConstantPhase->args())) {
    NewArg.takeName(&OldArg);
    VMap[&OldArg] = &NewArg;
  }
  SmallVector<ReturnInst *, 1> Returns;
  CloneFunctionInto(PatchConstantPhase, &F, VMap,
                    CloneFunctionChangeType::LocalChangesOnly, Returns);

  // `F` itself becomes the trivial control-point phase: with
  // `OutputVertices == 1` and every one of its stage-IO writes already
  // moved to the patch-constant clone above, it has nothing left to
  // produce. It is left with no `!feme.signature` metadata of its own --
  // `feme::cpu::CompiledStage::create` already treats an entirely absent
  // signature identically to an explicitly empty one (roadmap H4g).
  F.deleteBody();
  ReturnInst::Create(F.getContext(), BasicBlock::Create(F.getContext(), "", &F));
  return true;
}

bool splitTessellationControlEntry(Function &F, Function *&PatchConstantPhase) {
  PatchConstantPhase = nullptr;
  SmallVector<CallInst *, 2> Barriers;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I); CI && isSPIRVGroupSyncBarrier(*CI))
      Barriers.push_back(CI);
  if (Barriers.empty())
    return splitBarrierlessTessellationControlEntry(F, PatchConstantPhase);
  if (Barriers.size() != 1) {
    F.getContext().emitError(
        "feme-canonicalize-stage: tessellation-control SPIR-V entry points "
        "currently support exactly one group-sync barrier");
    return false;
  }

  CallInst *Barrier = Barriers[0];
  if (!Barrier->getNextNode()) {
    F.getContext().emitError(
        Barrier, "feme-canonicalize-stage: tessellation-control SPIR-V entry "
                 "point has no post-barrier patch-constant region");
    return false;
  }

  BasicBlock *BarrierBlock = Barrier->getParent();
  BasicBlock *PatchEntry = BarrierBlock->splitBasicBlock(
      std::next(Barrier->getIterator()), F.getName() + ".patchconst.entry");

  SmallPtrSet<BasicBlock *, 8> Region;
  SmallVector<BasicBlock *, 8> WorkList(1, PatchEntry);
  while (!WorkList.empty()) {
    BasicBlock *BB = WorkList.pop_back_val();
    if (!Region.insert(BB).second)
      continue;
    for (BasicBlock *Succ : successors(BB))
      WorkList.push_back(Succ);
  }

  for (BasicBlock *BB : Region)
    for (BasicBlock *Pred : predecessors(BB))
      if (!Region.contains(Pred) && Pred != BarrierBlock) {
        F.getContext().emitError(
            BB->getTerminator(),
            "feme-canonicalize-stage: tessellation-control SPIR-V entry "
            "point's patch-constant region must have exactly one entry edge "
            "from the barrier");
        return false;
      }

  // (Roadmap H4c) Every SSA value the patch-constant region reads back
  // that was defined before the barrier -- the common shape a GLSL-
  // compiled tessellation-control shader's own per-patch tessellation
  // factor takes, computed from data derived from the control-point body
  // (e.g. its own output position) and read back after `OpControlBarrier`
  // once `PromoteMemToReg` (or the SPIR-V producer's own optimizer) has
  // turned what would otherwise be a reload of that invocation's own
  // stored output into a bare cross-barrier SSA use. Collected in
  // first-use order, deduplicated by value, so each is threaded through
  // exactly one new patch-shared global below rather than erroring as
  // before.
  SmallVector<Instruction *, 4> Captured;
  SmallPtrSet<Instruction *, 4> CapturedSeen;
  for (BasicBlock *BB : Region)
    for (Instruction &I : *BB)
      for (Value *Op : I.operands()) {
        auto *OpI = dyn_cast<Instruction>(Op);
        if (!OpI || Region.contains(OpI->getParent()))
          continue;
        if (CapturedSeen.insert(OpI).second)
          Captured.push_back(OpI);
      }

  PatchConstantPhase =
      Function::Create(F.getFunctionType(), F.getLinkage(), F.getAddressSpace(),
                       (F.getName() + ".patchconstant").str(), F.getParent());
  PatchConstantPhase->copyAttributesFrom(&F);
  PatchConstantPhase->setComdat(F.getComdat());
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto [Kind, Node] : MDs)
    PatchConstantPhase->setMetadata(Kind, Node);

  ValueToValueMapTy VMap;
  for (auto [OldArg, NewArg] : zip(F.args(), PatchConstantPhase->args())) {
    NewArg.takeName(&OldArg);
    VMap[&OldArg] = &NewArg;
  }

  // (Roadmap H4c) Thread each captured value through one new address-space-8
  // (`Output`) global, given a synthetic `Location` decoration: a store
  // right after the value's own definition, still in the control-point
  // phase that has it as an SSA value, paired with a load at the very
  // start of the patch-constant phase. This is exactly the shape a real
  // per-vertex output (e.g. `gl_out[i].gl_Position`) already takes when
  // its own patch-constant-phase read-back falls through
  // `classifySPIRVElement`'s default case below (address space 8, no
  // `Patch`/tess-factor `BuiltIn` decoration) -- so `canonicalizeSPIRVStage`
  // (run separately on each phase once this split returns) reflects both
  // ends as an ordinary linked `Output` (control-point phase) /
  // `SignatureDirection::Input`, non-`FromInputPatch` (patch-constant
  // phase) `SignatureElement` pair, and
  // `feme::graphics::linkStageElements`'s existing hull-output ->
  // patch-constant-`OutputPatch` linkage (`PatchPipeline.cpp`) carries the
  // value across for free, with no new linkage mechanism needed. This is
  // always sound regardless of whether the captured computation itself
  // reads another invocation's own output: SPIR-V only gives that read
  // defined behavior *after* a barrier establishes visibility, so any
  // value defined *before* the one barrier this pass splits at can only
  // ever depend on this invocation's own state.
  if (!Captured.empty()) {
    BasicBlock *CaptureEntry =
        BasicBlock::Create(F.getContext(), "patchconst.captures", PatchConstantPhase);
    IRBuilder<> CaptureBuilder(CaptureEntry);
    unsigned NextLocation = computeNextSyntheticLocation(*F.getParent());
    for (Instruction *V : Captured) {
      Type *Ty = V->getType();
      unsigned Location = NextLocation++;
      MDNode *Decoration = createLocationDecoration(F.getContext(), Location);
      auto *GV = new GlobalVariable(
          *F.getParent(), Ty, /*isConstant=*/false,
          GlobalValue::PrivateLinkage, UndefValue::get(Ty),
          F.getName() + ".patchconst.capture." + Twine(Location),
          /*InsertBefore=*/nullptr, GlobalValue::NotThreadLocal,
          /*AddressSpace=*/8);
      GV->setMetadata("spirv.Decorations", Decoration);

      std::optional<BasicBlock::iterator> InsertPt =
          V->getInsertionPointAfterDef();
      assert(InsertPt && "captured value has no valid insertion point");
      // `getInsertionPointAfterDef` may name a different block than
      // `V`'s own (a `PHINode`/`InvokeInst`'s result is only available in
      // its parent/normal-destination block respectively).
      BasicBlock *InsertBB;
      if (auto *PN = dyn_cast<PHINode>(V))
        InsertBB = PN->getParent();
      else if (auto *II = dyn_cast<InvokeInst>(V))
        InsertBB = II->getNormalDest();
      else
        InsertBB = V->getParent();
      IRBuilder<> StoreBuilder(InsertBB, *InsertPt);
      StoreBuilder.CreateStore(V, GV);

      VMap[V] = CaptureBuilder.CreateLoad(Ty, GV, V->getName() + ".captured");
    }
  }

  SmallVector<BasicBlock *, 8> OrderedRegion;
  for (BasicBlock &BB : F)
    if (Region.contains(&BB))
      OrderedRegion.push_back(&BB);
  for (BasicBlock *BB : OrderedRegion)
    VMap[BB] = CloneBasicBlock(BB, VMap, "", PatchConstantPhase);
  for (BasicBlock *BB : OrderedRegion) {
    BasicBlock *Cloned = cast<BasicBlock>(VMap[BB]);
    for (Instruction &I : *Cloned)
      RemapInstruction(&I, VMap, RF_NoModuleLevelChanges);
  }

  // The captures block (if any) is the function's real entry; branch it
  // into the cloned post-barrier region, which -- since `OrderedRegion`
  // preserves `F`'s own block order and `PatchEntry` is always the first
  // block in `Region` by construction -- is `PatchConstantPhase`'s first
  // *cloned* block.
  if (!Captured.empty())
    UncondBrInst::Create(cast<BasicBlock>(VMap[PatchEntry]),
                         &PatchConstantPhase->front());

  Barrier->eraseFromParent();
  Instruction *OldTerm = BarrierBlock->getTerminator();
  ReturnInst::Create(F.getContext(), BarrierBlock);
  OldTerm->eraseFromParent();
  DeleteDeadBlocks(OrderedRegion);
  return true;
}

/// The (row, component) pair `loadStageIOValue`/`storeStageIOValue` need to
/// seed their own recursion with, from \p Residual -- a byte offset within
/// one stage-IO member's own declared type \p MemberTy -- mirroring
/// `getStageIORowShape`'s own type recursion (a single-member struct
/// peeled, then an array's rows, then a vector's components).
std::pair<uint64_t, uint64_t>
resolveRowComponent(Type *MemberTy, uint64_t Residual, const DataLayout &DL) {
  Type *PerRowTy = peelSingleMemberStruct(MemberTy);
  uint64_t Row = 0;
  if (auto *ArrTy = dyn_cast<ArrayType>(PerRowTy)) {
    uint64_t RowSize = DL.getTypeAllocSize(ArrTy->getElementType());
    if (RowSize) {
      Row = Residual / RowSize;
      Residual -= Row * RowSize;
    }
    PerRowTy = ArrTy->getElementType();
  }
  uint64_t Component = 0;
  if (auto *VecTy = dyn_cast<FixedVectorType>(PerRowTy)) {
    uint64_t CompSize = DL.getTypeAllocSize(VecTy->getElementType());
    if (CompSize)
      Component = Residual / CompSize;
  }
  return {Row, Component};
}

/// Resolves \p Ptr -- a load/store's pointer operand -- against \p
/// ElementIDs (one entry per stage-IO global, one `ElementID` per struct
/// member for a builtin interface block, a single one for everything
/// else) into a `StageIOAccess`, or `std::nullopt` if \p Ptr does not
/// address a recognized stage-IO global at all.
///
/// The shared tail of `resolveStageIOAccess`'s two entry shapes (an
/// ordinary constant-byte-offset access rooted directly at a stage-IO
/// global, and, roadmap H5b, a dynamically-vertex-indexed one rooted one
/// array dimension in): resolves \p ByteOffset within \p ElemTy -- the
/// stage-IO global's own value type in the former case, or one array
/// element's (one vertex's own) value type in the latter -- into a
/// `StageIOAccess`, exactly as the pre-H5b body of this function did
/// inline. \p Vertex is threaded through unchanged: non-null (the dynamic
/// vertex index) for the H5b path, `nullptr` (the caller's own default,
/// an ordinary constant `i32 0`) for the ordinary one.
///
/// A plain stage-IO global (a single `ElementID`) is addressed exactly
/// like one struct member below: a whole-value access at offset 0 (the
/// common case, \p Row/\p Component left null so `loadStageIOValue`/
/// `storeStageIOValue` decompose \p ValueTy -- itself the global's own
/// array/vector/matrix shape when \p ValueTy names the whole thing --
/// starting from row/component 0), or a single (\p Row, \p Component)
/// selected by \p ByteOffset into \p ElemTy (roadmap H4d:
/// `gl_TessLevelOuter[i]`/`gl_TessLevelInner[i]`'s own per-row write for
/// `i != 0`, exactly the shape a real GLSL-compiled tessellation-control
/// shader takes -- previously rejected here as an unmodeled shape, leaving
/// every such store's global reference unrewritten and undefined at JIT
/// time). A builtin interface block (multiple `ElementID`s) has two
/// addressable shapes instead: (1) the whole block loaded/stored as one
/// aggregate value (\p ValueTy exactly matches \p ElemTy) -- every
/// member's `ElementID`; (2) a single member (or one row/component within
/// it, `gl_ClipDistance`/`gl_CullDistance`'s own per-element access, or
/// `gl_Position`'s own per-component one) selected by \p ByteOffset into
/// \p ElemTy's own `StructLayout`. \p IsOutput (roadmap H2e) lets a caller
/// tell a genuinely-input load from an `Output`-direction read-back.
///
/// (Roadmap H6c-a-a-iii) A multi-`ElementID` builtin interface block is
/// only ever modeled here as a plain (non-arrayed) `StructType` -- the
/// shape every such block took until a mesh entry's arrayed
/// `PerPrimitiveEXT`/`PerVertexEXT` builtin block (e.g. an array-of-struct
/// per-primitive output block, one struct per primitive) reached this far
/// for the first time via H6c-a-a-i's own closing re-run. That shape is
/// simply not modeled yet: gracefully returns `std::nullopt` (leaving the
/// access unrewritten, exactly like any other unrecognized pointer
/// `resolveStageIOAccess` rejects) instead of asserting via
/// `cast<StructType>`, which previously aborted the whole process outright.
std::optional<StageIOAccess>
resolveOffsetWithinElement(Type *ElemTy, ArrayRef<uint32_t> IDs,
                           uint64_t ByteOffset, Type *ValueTy,
                           const DataLayout &DL, bool IsOutput, Value *Vertex) {
  LLVMContext &Ctx = ElemTy->getContext();
  auto AsConstant = [&](uint64_t V) -> Value * {
    return V ? ConstantInt::get(Type::getInt32Ty(Ctx), V) : nullptr;
  };
  if (IDs.size() == 1) {
    if (ValueTy == ElemTy)
      return StageIOAccess{IDs, nullptr, nullptr, Vertex, IsOutput};
    auto [Row, Component] = resolveRowComponent(ElemTy, ByteOffset, DL);
    return StageIOAccess{IDs, AsConstant(Row), AsConstant(Component), Vertex,
                         IsOutput};
  }

  auto *ST = dyn_cast<StructType>(ElemTy);
  if (!ST)
    return std::nullopt;
  if (ValueTy == ST)
    return StageIOAccess{IDs, nullptr, nullptr, Vertex, IsOutput};

  const StructLayout *SL = DL.getStructLayout(ST);
  unsigned Member = SL->getElementContainingOffset(ByteOffset);
  uint64_t Residual = ByteOffset - SL->getElementOffset(Member);
  auto [Row, Component] =
      resolveRowComponent(ST->getElementType(Member), Residual, DL);
  return StageIOAccess{IDs.slice(Member, 1), AsConstant(Row),
                       AsConstant(Component), Vertex, IsOutput};
}

/// Resolves \p Ptr -- a load/store's pointer operand -- against \p
/// ElementIDs (one entry per stage-IO global, one `ElementID` per struct
/// member for a builtin interface block, a single one for everything
/// else) into a `StageIOAccess`, or `std::nullopt` if \p Ptr does not
/// address a recognized stage-IO global at all. Tries
/// `getDynamicVertexIndexedAccess`'s dynamically-vertex-indexed shape
/// (roadmap H5b) first, falling back to `getStageIOBaseAndOffset`'s
/// ordinary constant-byte-offset one -- see `resolveOffsetWithinElement`'s
/// own comment for how each resolves from there. (Roadmap H5f) The
/// constant-offset fallback itself peels a per-vertex-arrayed `Input`
/// global's own outer array dimension into `Vertex` too, via
/// `isPerVertexArrayInputGlobal`, so a *constant* `gl_in[k]` index is
/// routed the same way a non-constant one already is, rather than folding
/// into `Row` -- deliberately `Input`-only (see that helper's own
/// comment on why a *constant* `Output`-array index is not folded the
/// same way, roadmap H6b). \p OutputGlobals (roadmap H2e) is checked to
/// set the
/// result's `IsOutput`, so a caller can tell a genuinely-input load from
/// an `Output`-direction read-back.
std::optional<StageIOAccess> resolveStageIOAccess(
    Value *Ptr, Type *ValueTy, const DataLayout &DL,
    const DenseMap<GlobalVariable *, SmallVector<uint32_t, 1>> &ElementIDs,
    const DenseSet<GlobalVariable *> &OutputGlobals) {
  if (std::optional<std::tuple<GlobalVariable *, Value *, uint64_t>> Dyn =
          getDynamicVertexIndexedAccess(Ptr, DL)) {
    auto [GV, VertexIndex, ByteOffset] = *Dyn;
    auto It = ElementIDs.find(GV);
    if (It == ElementIDs.end())
      return std::nullopt;
    Type *ElemTy = cast<ArrayType>(GV->getValueType())->getElementType();
    return resolveOffsetWithinElement(ElemTy, It->second, ByteOffset, ValueTy,
                                      DL, OutputGlobals.contains(GV),
                                      VertexIndex);
  }

  std::optional<std::pair<GlobalVariable *, uint64_t>> BaseAndOffset =
      getStageIOBaseAndOffset(Ptr, DL);
  if (!BaseAndOffset)
    return std::nullopt;
  auto [GV, ByteOffset] = *BaseAndOffset;
  auto It = ElementIDs.find(GV);
  if (It == ElementIDs.end())
    return std::nullopt;

  // (Roadmap H5f) A *constant*-indexed `gl_in[k]`-shaped access folds
  // entirely into a plain byte offset above -- `getStageIOBaseAndOffset`'s
  // constant-offset walk has no trouble with a constant array index --
  // landing here rather than `getDynamicVertexIndexedAccess`'s own
  // non-constant path. Peel that same outer per-vertex array dimension
  // for consistency: fold the constant vertex index into `Vertex`, not an
  // ordinary `Row` the way `resolveOffsetWithinElement` below would
  // otherwise read it as (exactly what this global's shape did before
  // this row). Left alone for a whole-global aggregate access (\p ValueTy
  // names the entire array, e.g. copying every vertex's value at once),
  // which has no single vertex to peel out.
  unsigned AddrSpace = 0;
  if (isPerVertexArrayInputGlobal(GV, AddrSpace) &&
      ValueTy != GV->getValueType()) {
    auto *ArrTy = cast<ArrayType>(GV->getValueType());
    Type *ElemTy = ArrTy->getElementType();
    uint64_t VertexSize = DL.getTypeAllocSize(ElemTy);
    if (VertexSize) {
      uint64_t VertexIdx = ByteOffset / VertexSize;
      uint64_t Residual = ByteOffset % VertexSize;
      Value *Vertex =
          ConstantInt::get(Type::getInt32Ty(GV->getContext()), VertexIdx);
      return resolveOffsetWithinElement(ElemTy, It->second, Residual, ValueTy,
                                        DL, OutputGlobals.contains(GV), Vertex);
    }
  }

  return resolveOffsetWithinElement(GV->getValueType(), It->second, ByteOffset,
                                    ValueTy, DL, OutputGlobals.contains(GV),
                                    /*Vertex=*/nullptr);
}

/// Rewrites \p F's SPIR-V-derived stage IR into `feme.stage.*`: its
/// `Input`/`Output` interface-variable loads/stores (address space 7/8,
/// see `isSPIRVStageIOGlobal`) into `feme.stage.input.load`/
/// `output.store` -- building and attaching this entry's
/// `feme::EntrySignature` along the way, the piece roadmap R19 explicitly
/// deferred to this milestone (see "Signature reflection" in
/// feme/docs/FeMeGraphicsDesign.md) -- its already-legalized
/// `llvm.spv.discard`/derivative/quad-read intrinsic calls into their
/// `feme.stage.*` peers, mirroring `canonicalizeDXILStage`'s handling of the
/// same operations' DXIL-derived forms, and (roadmap H6i) a task entry's
/// bounded payload writes (address space 14, see `isTaskPayloadGlobal`)
/// into `feme.stage.task.payload.store`.
bool canonicalizeSPIRVStage(Function &F, ShaderStage Stage,
                            SPIRVCanonicalPhase Phase) {
  bool Changed = false;

  // Discover this entry's stage-IO globals in two passes -- inputs, then
  // outputs -- so their assigned `ElementID`s land in the same
  // inputs-before-outputs order `feme::dxil::convertEntrySignature` already
  // uses (see `collectElementIDsByDirection`'s comment), keeping the two
  // formats' numbering conventions consistent.
  SmallVector<GlobalVariable *> InputGlobals, OutputGlobals;
  DenseSet<GlobalVariable *> Seen;
  const DataLayout &DL = F.getParent()->getDataLayout();
  for (Instruction &I : instructions(F)) {
    GlobalVariable *GV = nullptr;
    if (auto *LI = dyn_cast<LoadInst>(&I))
      GV = getStageIOGlobal(LI->getPointerOperand(), DL);
    else if (auto *SI = dyn_cast<StoreInst>(&I))
      GV = getStageIOGlobal(SI->getPointerOperand(), DL);
    unsigned AddrSpace = 0;
    if (!isSPIRVStageIOGlobal(GV, AddrSpace) || !Seen.insert(GV).second)
      continue;
    ParsedSPIRVDecorations D =
        parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
    SPIRVElementInfo Info = classifySPIRVElement(Stage, Phase, AddrSpace, D);
    (Info.IsOutput ? OutputGlobals : InputGlobals).push_back(GV);
  }

  DenseMap<GlobalVariable *, SmallVector<uint32_t, 1>> ElementIDs;
  // Hoisted out of the `if` below (rather than left a block-local, as
  // every other `Sig` in this file is) so the store-rewriting loop further
  // down can look an `ElementID` back up to its own `SystemValue` --
  // needed for `negateSystemValuePositionY`'s own Position-specific check.
  EntrySignature Sig;
  if (!InputGlobals.empty() || !OutputGlobals.empty()) {
    Sig = dxil::getEntrySignature(F).value_or(EntrySignature{});
    uint32_t NextID = Sig.Elements.size();
    // Appends one `SignatureElement` for \p GV (or one of its struct
    // members, for a builtin interface block -- see the `addElements`
    // lambda below), sourced from \p D's decorations and \p ValueTy's own
    // scalar/vector/matrix/single-member-struct shape. (Roadmap H5f) \p
    // RowCountIsVertexArray records whether \p ValueTy's own outer array
    // dimension (folded into `RowCount` below by `getStageIORowShape`) is
    // actually a geometry entry's per-vertex array rather than a real
    // matrix's row dimension -- true only for a whole (non-block)
    // per-vertex-arrayed `Input` global (`isPerVertexArrayInputGlobal`),
    // never for a builtin interface block's own per-member element, whose
    // `ValueTy` has already had that same dimension peeled off by the
    // caller before it ever reaches here. (Roadmap H6j) A mesh entry's own
    // plain per-vertex/per-primitive `Output` global gets the same
    // peeled-`ValueTy`/default-`false` treatment as a builtin block's own
    // member (see `addElements`'s own comment on why, unlike `Input`, that
    // array dimension cannot just be folded into `RowCount` and flagged
    // here instead).
    auto addElement = [&](GlobalVariable *GV, unsigned AddrSpace,
                          const ParsedSPIRVDecorations &D, Type *ValueTy,
                          bool RowCountIsVertexArray = false) {
      SPIRVElementInfo Info = classifySPIRVElement(Stage, Phase, AddrSpace, D);
      SignatureElement Elt;
      Elt.ElementID = NextID;
      Elt.Direction = Info.Direction;
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
      Elt.RowCountIsVertexArray = RowCountIsVertexArray;
      Elt.Interpolation = getInterpolationMode(D);
      Elt.Frequency = Info.Frequency;
      Elt.FromInputPatch = Info.FromInputPatch;

      Sig.Elements.push_back(Elt);
      ElementIDs[GV].push_back(NextID);
      ++NextID;
    };
    auto addElements = [&](ArrayRef<GlobalVariable *> Globals) {
      for (GlobalVariable *GV : Globals) {
        unsigned AddrSpace = 0;
        [[maybe_unused]] bool IsStageIO = isSPIRVStageIOGlobal(GV, AddrSpace);
        assert(IsStageIO &&
               "expected previously-collected SPIR-V stage-IO global");
        // (Roadmap H2d) A builtin interface block (e.g. `gl_PerVertex`)
        // carries no whole-variable decoration of its own -- SPIR-V
        // decorates each of its struct members individually -- so it
        // decomposes into one `SignatureElement` per member instead of
        // the single one every other stage-IO global gets. (Roadmap
        // H5b/H6b) A geometry entry's own per-vertex block
        // (`gl_in[]`-shaped) or a mesh entry's own per-vertex/
        // per-primitive block (`gl_MeshVerticesEXT[]`/
        // `gl_MeshPrimitivesEXT[]`-shaped) is this same shape one array
        // dimension further out -- the block type itself, not the array
        // wrapping `VerticesPerPrimitive`/`OutputVertices`/
        // `OutputPrimitivesEXT`-many of it, is what carries one
        // `ElementID` per member; the array dimension is the
        // dynamically-indexed `Vertex` operand instead
        // (`getDynamicVertexIndexedAccess`'s own peeling on the access
        // side), never folded into any member's own `RowCount`.
        if (const MDNode *MemberMD =
                GV->getMetadata("feme.spirv.MemberDecorations")) {
          Type *BlockTy = GV->getValueType();
          if (auto *ArrTy = dyn_cast<ArrayType>(BlockTy))
            BlockTy = ArrTy->getElementType();
          auto *ST = cast<StructType>(BlockTy);
          DenseMap<unsigned, ParsedSPIRVDecorations> MemberDecorations =
              parseSPIRVMemberDecorations(MemberMD);
          for (unsigned I = 0, E = ST->getNumElements(); I != E; ++I)
            addElement(GV, AddrSpace, MemberDecorations.lookup(I),
                       ST->getElementType(I));
          continue;
        }
        // (Roadmap H5f) A plain (non-block) per-vertex-arrayed `Input`
        // global's whole declared type -- including its outer per-vertex
        // array dimension -- still becomes this element's `RowCount` via
        // `getStageIORowShape` below, exactly as before H5b; `Elt.
        // RowCountIsVertexArray` marks that dimension as a per-vertex
        // array's own extent rather than a real matrix's row count, so a
        // consumer can tell the two apart regardless of whether the
        // shader's own index into it happens to be constant (folded into
        // `Row` by `resolveOffsetWithinElement`) or dynamic (`Vertex`).
        // Nothing downstream ever links an `Input` element's `RowCount`
        // against another stage's, so leaving the array dimension folded
        // in (rather than peeled off, as a builtin block's own member is
        // above) is harmless here.
        //
        // (Roadmap H6j) A mesh entry's own plain per-vertex/per-primitive
        // `Output` global (e.g. a user-defined `PerVertexEXT`/
        // `PerPrimitiveEXT` varying such as `layout(location=0) out vec4
        // v_color[];`) cannot take the same "leave it folded in, flag it"
        // treatment: unlike `Input`, this element's `RowCount` *is*
        // linked, by `Location`, against the fragment stage's
        // corresponding (unarrayed, one-per-fragment-invocation) input --
        // `feme::graphics::executeDraws`/`GraphicsPipeline.cpp`'s own
        // `validateStageInterfaces` -- and neither consults
        // `RowCountIsVertexArray` when comparing the two. Left folded in,
        // this element's `RowCount` would wrongly be `OutputVertices`/
        // `OutputPrimitivesEXT` instead of the fragment-visible per-vertex
        // shape (e.g. 1 for a `vec4`), disagreeing with the fragment
        // input's own `RowCount` despite both sides sharing the same
        // `layout(location=...)` -- exactly the `vkQueueSubmit`-time
        // "disagree on component/row count or type" mismatch this row
        // fixes. A mesh stage has no ordinary, unindexed output to write a
        // real matrix into to begin with (every mesh `Output` write is
        // through this same per-vertex/per-primitive array), so this
        // dimension is never a genuine matrix row count to preserve --
        // peel it off the same way a builtin block's own array dimension
        // already is above, leaving `RowCountIsVertexArray` at its default
        // `false` to match.
        unsigned UnusedAddrSpace = 0;
        ParsedSPIRVDecorations D =
            parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
        Type *ValueTy = GV->getValueType();
        bool RowCountIsVertexArray =
            isPerVertexArrayInputGlobal(GV, UnusedAddrSpace);
        if (Stage == ShaderStage::Mesh && AddrSpace == 8) {
          if (auto *ArrTy = dyn_cast<ArrayType>(ValueTy))
            ValueTy = ArrTy->getElementType();
          RowCountIsVertexArray = false;
        }
        addElement(GV, AddrSpace, D, ValueTy,
                   /*RowCountIsVertexArray=*/RowCountIsVertexArray);
      }
    };
    addElements(InputGlobals);
    addElements(OutputGlobals);
    dxil::setEntrySignature(F, Sig);
    Changed = true;
  } else if (Stage == ShaderStage::Geometry) {
    // (Roadmap H5e-d) A geometry entry compiled from an `emit`-count shape
    // that ends its primitive without ever emitting on that particular
    // stream/count combination (e.g. a CTS `dEQP-VK.geometry.emit.*_emit_
    // 0_end_1` case) calls only `EndPrimitive` -- lowered to a masked
    // stream-cut op by `SPIRVToLLVMPatterns` -- and neither reads nor
    // writes a single stage-IO global, so the discovery loop above finds
    // both `InputGlobals`/`OutputGlobals` empty and the branch above never
    // runs, leaving this entry with no `!feme.signature` metadata at all.
    // `feme::cpu::GeometryWrapperPass` (`GeometryWrapper.cpp`) cannot
    // tolerate that absence the way `loadInput`/`storeOutput` resolution
    // elsewhere does: any geometry entry that uses so much as one stage op
    // -- a stream cut included -- hard-requires an attached signature to
    // look element IDs up in, so it errors out instead of quietly treating
    // a missing one as empty. Attach an explicit empty signature here,
    // scoped to `Geometry` only: unlike `Vertex`/`Fragment` (dispatched to
    // both `canonicalizeDXILStage` and this function by
    // `CanonicalizeStagePass::run`, so an empty-globals function reaching
    // here could still be a DXIL-origin entry deliberately left
    // signature-less, exactly the ambiguity roadmap H4g's own rejected fix
    // ran into), a geometry entry is only ever routed through this
    // (SPIR-V-only) function, so there is no DXIL-origin ambiguity to
    // preserve.
    Sig = dxil::getEntrySignature(F).value_or(EntrySignature{});
    dxil::setEntrySignature(F, Sig);
    Changed = true;
  }

  // (Roadmap H2e) An `Output`-direction global's own read-back load (see
  // `ShadowValueMap`'s own comment) is routed through a per-leaf-scalar
  // shadow alloca instead of a wrong-direction `feme.stage.input.load`;
  // `OutputGlobalSet` lets `resolveStageIOAccess` tell the two apart.
  DenseSet<GlobalVariable *> OutputGlobalSet(OutputGlobals.begin(),
                                             OutputGlobals.end());
  ShadowValueMap ShadowValues(F);

  for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
    IRBuilder<> B(&I);
    Value *Zero = B.getInt32(0);
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      std::optional<StageIOAccess> Access =
          resolveStageIOAccess(LI->getPointerOperand(), LI->getType(), DL,
                               ElementIDs, OutputGlobalSet);
      if (!Access)
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
      // shared type recursion. An `Output`-direction load (roadmap H2e) is
      // a read-back rather than a genuine input, so it is routed through
      // `ShadowValues` instead.
      Value *Row = Access->Row ? Access->Row : Zero;
      Value *Component = Access->Component ? Access->Component : Zero;
      // (Roadmap H5b) A dynamically-indexed `gl_in[i]`-shaped access
      // threads its own vertex index through as the `Vertex` operand in
      // place of the ordinary constant `Zero` every other stage-IO access
      // uses.
      Value *Vertex = Access->Vertex ? Access->Vertex : Zero;
      Value *New = loadStageIOBlockValue(
          B, LI->getType(), Access->ElementIDs, Row, Component, Vertex,
          LI->getName(), Access->IsOutput ? &ShadowValues : nullptr);
      LI->replaceAllUsesWith(New);
      LI->eraseFromParent();
      Changed = true;
    } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
      Value *Val = SI->getValueOperand();
      std::optional<StageIOAccess> Access =
          resolveStageIOAccess(SI->getPointerOperand(), Val->getType(), DL,
                               ElementIDs, OutputGlobalSet);
      if (!Access) {
        // (Roadmap H6i) A task entry's bounded payload write -- an
        // ordinary store through a (possibly GEP'd) address-space-14
        // global, `TaskPayloadGlobalVariablePattern`'s own import shape
        // (roadmap H6h) -- resolves no `StageIOAccess` at all (it is raw
        // task-defined memory, not a signature element), so it falls
        // through to here instead. `getStageIOBaseAndOffset` (already
        // generic over any address space) still recovers its constant
        // byte offset, letting it canonicalize into
        // `feme.stage.task.payload.store` by that offset directly, rather
        // than being left an unrewritten raw store the way a genuinely
        // unresolvable stage-IO access is.
        if (auto BaseAndOffset =
                getStageIOBaseAndOffset(SI->getPointerOperand(), DL)) {
          if (isTaskPayloadGlobal(BaseAndOffset->first)) {
            createStageTaskPayloadStore(B, BaseAndOffset->second, Val);
            SI->eraseFromParent();
            Changed = true;
          }
        }
        continue;
      }
      Value *Row = Access->Row ? Access->Row : Zero;
      Value *Component = Access->Component ? Access->Component : Zero;
      // (Roadmap H2g) A single-element `gl_Position`/`gl_PerVertex.
      // gl_Position` write needs its own Y component negated before it
      // reaches the executor's own (oppositely-conventioned) viewport
      // transform -- see `negateSystemValuePositionY`'s own comment. A
      // whole-block store (`Access->ElementIDs.size() != 1`, e.g. copying
      // an entire `gl_PerVertex` between array elements) is left alone:
      // unreached by any stage this milestone implements.
      if (Access->ElementIDs.size() == 1 &&
          Access->ElementIDs[0] < Sig.Elements.size() &&
          Sig.Elements[Access->ElementIDs[0]].SystemValue ==
              SignatureSystemValue::Position)
        Val = negateSystemValuePositionY(B, Val, Access->Component);
      // Every store this pass resolves is to an `Output`-direction global
      // (an `Input` one is never written to in SPIR-V); also tracking it
      // through `ShadowValues` (roadmap H2e) lets a later read-back of the
      // same element resolve to it.
      // (Roadmap H6b) A dynamically-indexed mesh-entry per-vertex/
      // per-primitive `Output`-array store (`getDynamicVertexIndexedAccess`'s
      // own store-side counterpart to H5b's `Input`-side one) threads its
      // own per-vertex/per-primitive index through as the `Vertex` operand
      // the same way the load path above already does, in place of the
      // ordinary constant `Zero` every other stage-IO store still uses.
      Value *Vertex = Access->Vertex ? Access->Vertex : Zero;
      storeStageIOBlockValue(B, Val, Val->getType(), Access->ElementIDs, Row,
                             Component, Vertex, &ShadowValues);
      SI->eraseFromParent();
      Changed = true;
    }
  }

  // Every read-back load above still points at its own leaf's shadow
  // alloca; `PromoteMemToReg` resolves each to the dominance-correct
  // reaching store now that every instruction has been rewritten,
  // inserting a `phi` for any real control-flow join the source's own
  // read-modify-write straddles (e.g. `gl_Position.y += 1.0f` guarded by an
  // `if`) -- exactly the SSA construction a compiler's own `mem2reg` does
  // for a local variable, which a linear "last stored value" scan could
  // not do correctly in general.
  if (!ShadowValues.empty()) {
    DominatorTree DT(F);
    PromoteMemToReg(ShadowValues.takeAllocas(), DT);
    Changed = true;
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

bool canonicalizeSPIRVHullStage(Function &F) {
  if (!usesSPIRVStageIO(F))
    return false;
  Function *PatchConstantPhase = nullptr;
  if (!splitTessellationControlEntry(F, PatchConstantPhase))
    return false;
  bool Changed = canonicalizeSPIRVStage(F, ShaderStage::Hull,
                                        SPIRVCanonicalPhase::HullControlPoint);
  if (PatchConstantPhase)
    Changed |= canonicalizeSPIRVStage(*PatchConstantPhase, ShaderStage::Hull,
                                      SPIRVCanonicalPhase::HullPatchConstant);
  return Changed || PatchConstantPhase;
}

} // namespace

PreservedAnalyses CanonicalizeStagePass::run(Module &M,
                                             ModuleAnalysisManager &AM) {
  bool Changed = false;
  SmallVector<Function *, 16> WorkList;
  for (Function &F : M)
    WorkList.push_back(&F);
  for (Function *F : WorkList) {
    std::optional<ShaderStage> Stage = getShaderStage(*F);
    if (!Stage ||
        (*Stage != ShaderStage::Vertex && *Stage != ShaderStage::Fragment &&
         *Stage != ShaderStage::Hull && *Stage != ShaderStage::Domain &&
         *Stage != ShaderStage::Geometry && *Stage != ShaderStage::Mesh &&
         *Stage != ShaderStage::Amplification))
      continue;

    // An absent signature (e.g. a hand-written test exercising only the
    // signature-independent rewrites below) is treated as an empty one:
    // `loadInput`/`storeOutput` then simply fail to resolve (left
    // unmodified, for `feme::graphics::ValidateStagePass` to diagnose),
    // while discard/derivative/quad-read/helper-lane rewriting -- which
    // needs no signature at all -- still proceeds.
    EntrySignature Sig = dxil::getEntrySignature(*F).value_or(EntrySignature{});
    if (*Stage == ShaderStage::Vertex || *Stage == ShaderStage::Fragment)
      Changed |= canonicalizeDXILStage(*F, Sig);
    if (*Stage == ShaderStage::Hull)
      Changed |= canonicalizeSPIRVHullStage(*F);
    else
      Changed |=
          canonicalizeSPIRVStage(*F, *Stage, SPIRVCanonicalPhase::Ordinary);
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

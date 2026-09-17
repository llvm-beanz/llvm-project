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
#include "llvm/ADT/STLFunctionalExtras.h"
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
#include "llvm/Transforms/Utils/Local.h"
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
  SPIRVDecorationOffset = 35,
  SPIRVDecorationXfbBuffer = 36,
  SPIRVDecorationXfbStride = 37,
  SPIRVDecorationLocation = 30,
  SPIRVDecorationComponent = 31,
  SPIRVDecorationIndex = 32,
  SPIRVDecorationPerPrimitiveEXT = 5271,
  SPIRVDecorationStream = 29,
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
  /// (Roadmap H21a) `VK_EXT_transform_feedback`'s own three per-variable
  /// decorations, feeding `SignatureElement::XfbBuffer`/`XfbOffset`/
  /// `XfbStride` (see `buildStageIODecorationsAttr` in
  /// SPIRVToLLVMPatterns.cpp, the writer side of this same encoding, and
  /// `feme::SignatureElement`'s own field-level documentation for what
  /// each means).
  std::optional<uint32_t> XfbBuffer;
  std::optional<uint32_t> XfbOffset;
  std::optional<uint32_t> XfbStride;
  /// (Roadmap H173) The `Stream` decoration -- a geometry entry point's
  /// own per-output-variable stream assignment (`SignatureElement::
  /// Stream`, "always 0 for every non-geometry stage" default when
  /// absent), fed by `spirv.EmitStreamVertex`/`spirv.EndStreamPrimitive`
  /// (roadmap H39)'s own paired output-variable decoration.
  std::optional<uint32_t> Stream;
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
    case SPIRVDecorationOffset:
      if (Arg)
        Result.XfbOffset = static_cast<uint32_t>(*Arg);
      break;
    case SPIRVDecorationXfbBuffer:
      if (Arg)
        Result.XfbBuffer = static_cast<uint32_t>(*Arg);
      break;
    case SPIRVDecorationXfbStride:
      if (Arg)
        Result.XfbStride = static_cast<uint32_t>(*Arg);
      break;
    case SPIRVDecorationStream:
      if (Arg)
        Result.Stream = static_cast<uint32_t>(*Arg);
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
/// representation for yet (`PointCoord`, `SamplePosition`, `DeviceIndex`,
/// ...), which is then treated as an
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
/// (Roadmap H7h) `ClipDistance`/`CullDistance` (`gl_ClipDistance`/
/// `gl_CullDistance`) now map to `SignatureSystemValue::ClipDistance`/
/// `CullDistance`: `Executor.cpp`'s `clipTriangle`/`RasterizePrimitives`
/// consume each as a real per-vertex `[8 x float]`-shaped output
/// (`SignatureElement::RowCount` gives how many of the up to
/// `maxClipDistances`/`maxCullDistances` (8) planes the shader actually
/// declared), lowering `shaderClipDistance`/`shaderCullDistance`
/// (`PhysicalDeviceInfo.cpp`) to `VK_TRUE`. Before this milestone these two
/// SPIR-V `BuiltIn`s were unreachable in practice -- GLSL/SPIR-V never
/// declares either as a standalone variable, only as `gl_PerVertex`
/// interface-block members (H2a/H2c), which this function never saw until
/// H2d's own per-member decomposition -- so mapping them here is a change
/// in *what* gets produced, not in any previously-observable behavior.
///
/// (Roadmap H7e) `PointSize` (`gl_PointSize`) maps to
/// `SignatureSystemValue::PointSize`: the last pre-rasterization stage's
/// own vertex output the executor's point-topology quad expansion reads
/// to derive a point primitive's screen-space size (`largePoints`).
///
/// (Roadmap H29r) The three `VK_EXT_mesh_shader` primitive-index builtins
/// (`PrimitiveTriangleIndicesEXT`/`PrimitiveLineIndicesEXT`/
/// `PrimitivePointIndicesEXT`) all map to
/// `SignatureSystemValue::PrimitiveIndices`, which the topology already
/// disambiguates (`FemeMeshArgs::OutputTopology`) -- a mesh entry declares
/// exactly one of the three, matching its own `OutputTriangles`/
/// `OutputLinesEXT`/`OutputPoints` execution mode. Before this row these
/// mapped to `None`, making a real `gl_PrimitiveTriangleIndicesEXT[i] =
/// uvec3(...)` write an ordinary, `Location`-less output element that
/// `MeshOutputWrapperPass` then stored into the per-vertex attribute
/// block instead of `FemeMeshArgs::PrimitiveIndices` -- leaving every
/// meshlet's real index list at its zero-initialized default, so every
/// emitted primitive degenerated to "all three vertices are vertex 0" and
/// never rasterized.
///
/// (Roadmap H106) `CullPrimitiveEXT` (`gl_CullPrimitiveEXT`) now maps to
/// `SignatureSystemValue::CullPrimitive`: `Executor.cpp`'s
/// `resolvePrimitiveState` reads a mesh entry's own authored value back
/// (when present) and skips rasterizing that one primitive outright,
/// mirroring `PrimitiveID`'s already-existing authored-value path. Before
/// this row it mapped to `None`, making a real `gl_CullPrimitiveEXT =
/// true` write an ordinary, unlinkable output that
/// `ValidateStagePass`/the executor simply ignored -- every primitive
/// rasterized regardless of the shader's own culling intent.
SignatureSystemValue getSystemValueForBuiltIn(uint32_t BuiltIn) {
  switch (BuiltIn) {
  case 0:  // Position
  case 15: // FragCoord
    return SignatureSystemValue::Position;
  case 1: // PointSize
    return SignatureSystemValue::PointSize;
  case 3: // ClipDistance
    return SignatureSystemValue::ClipDistance;
  case 4: // CullDistance
    return SignatureSystemValue::CullDistance;
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
  case 5294: // PrimitiveTriangleIndicesEXT
  case 5295: // PrimitiveLineIndicesEXT
  case 5296: // PrimitivePointIndicesEXT
    return SignatureSystemValue::PrimitiveIndices;
  case 5299: // CullPrimitiveEXT
    return SignatureSystemValue::CullPrimitive;
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
  // (Roadmap H6m) SPIR-V's `OpTypeBool` (LLVM `i1`) -- e.g.
  // `gl_CullPrimitiveEXT`, `VK_EXT_mesh_shader`'s per-primitive cull
  // builtin -- canonicalizes to an ordinary 32-bit element here rather
  // than surviving through to `StageStorage::buildStageStorage` at its
  // true 1-bit width, which that host-owned structure-of-arrays layout
  // has no addressable representation for (its per-element strides are
  // all hard-coded to a 4-byte scalar). This mirrors how a real GPU
  // driver typically represents a shader-visible `bool` as a 32-bit
  // value in memory; `loadStageIOValue`/`storeStageIOValue` below widen/
  // narrow the actual value at the same boundary.
  if (Scalar->isIntegerTy(1))
    return {SignatureComponentType::Bool, 32};
  if (auto *IntTy = dyn_cast<IntegerType>(Scalar))
    return {SignatureComponentType::SInt, IntTy->getBitWidth()};
  // FeMe's model has no representation for anything else (aggregates,
  // pointers, ...) yet; default to a plain `f32` rather than crashing, for
  // `feme::graphics::ValidateStagePass` to flag as a mismatch instead.
  return {SignatureComponentType::Float, 32};
}

/// (Roadmap H101j) Name prefix `SPIRVToLLVMPatterns.cpp`'s own
/// `getTightVectorArrayType` wraps its tight-substituted-vector marker
/// struct in (mirrored there as `kTightVectorMarkerName`) -- kept as a
/// single shared literal (duplicated, not included from a common header,
/// since these two files belong to different, independently-linked
/// components with no existing shared-header precedent for this small a
/// constant) so a rename of one side is caught by the other's own tests
/// failing to recognize the marker at all, rather than silently
/// mismatching.
constexpr StringLiteral TightVectorMarkerName = "feme.tight_vector";

/// If \p Ty is one of `SPIRVToLLVMPatterns.cpp`'s own tight-vector marker
/// structs -- wrapping a matrix column, or an array-of-vectors element,
/// that member's own declared offset could only be reproduced by
/// substituting a tightly-packed `array<M x Scalar>` for what would
/// otherwise be a `vector<M x Scalar>` -- returns that inner array;
/// otherwise returns null. Bit-for-bit, a tight-substituted
/// `array<M x Scalar>` is indistinguishable from a genuinely-declared,
/// directly-authored multi-dimensional scalar array (e.g.
/// `dEQP-VK.transform_feedback.fuzz.2_level_array.float`'s own `float
/// xs[2][2]`, which needs the *opposite* row/component classification --
/// each dimension its own row, never a component axis) -- `DataLayout`
/// reports identical size/alignment for both, so this marker is the only
/// positive, unambiguous signal available to tell the two apart.
Type *getTightVectorMarkerInnerType(Type *Ty) {
  auto *ST = dyn_cast<StructType>(Ty);
  if (!ST || ST->getNumElements() != 1 || !ST->hasName() ||
      !ST->getName().starts_with(TightVectorMarkerName))
    return nullptr;
  return ST->getElementType(0);
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
///
/// (Roadmap H101j) Deliberately does *not* peel through a tight-vector
/// marker struct (see `getTightVectorMarkerInnerType` above) the same
/// generic way: unlike an ordinary single-member wrapper, a marker's own
/// member needs special (component-axis, not row-axis) handling by this
/// function's own callers, which must be able to tell "the type I have
/// left is a marker" apart from "the type I have left is some other
/// single-member struct" -- impossible if this already silently unwrapped
/// it first.
Type *peelSingleMemberStruct(Type *Ty) {
  while (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->getNumElements() != 1 || getTightVectorMarkerInnerType(ST))
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
/// (Roadmap H101b) Single-member struct and array wrappers are peeled off
/// in alternation, multiplying \p RowCount at each array level, until a
/// genuine scalar/vector leaf is reached -- not just once each, as before
/// this milestone. This matters for a genuine *array-of-block-instances*
/// whose one member is itself a matrix (e.g. `layout(...) out Block {
/// mat4 var; } block[3];`, GLSL's syntax for 3 independently-captured XFB
/// streams): its LLVM shape is `[3 x { [4 x <4 x float>] }]`, an
/// `ArrayType` of a single-member `StructType` of *another* `ArrayType` --
/// peeling only one of each level left the inner matrix's own `[4 x <4 x
/// float>]` unresolved (returned as a bogus, unrepresentable "scalar"
/// type with `ComponentCount=1`), silently under-sizing this element's
/// storage relative to what the compiled stores it decomposes into
/// actually write -- a `double free or corruption` heap-overflow this
/// milestone's own regression sweep newly exposed. Treating the 3
/// instances and the matrix's own 4 columns as one flat, 12-row element
/// (rather than a nested 3-of-4 shape) matches the back-to-back row-
/// packing already assumed by `resolveOffsetWithinElement`'s own byte-
/// offset arithmetic and `captureTransformFeedback`'s row loop.
struct StageIORowShape {
  Type *Scalar;
  unsigned ComponentCount;
  unsigned RowCount;
};

StageIORowShape getStageIORowShape(Type *ValueTy) {
  uint64_t RowCount = 1;
  Type *PerRowTy = ValueTy;
  while (true) {
    Type *Peeled = peelSingleMemberStruct(PerRowTy);
    // (Roadmap H101j) `SPIRVToLLVMPatterns.cpp`'s
    // `convertOffsetStructTypeIgnoringDecorations` may substitute a
    // struct member's own matrix-column or array-of-vectors element with
    // a "tight" `array<M x Scalar>` in place of the ordinary
    // `vector<M x Scalar>` (see its own "tight vector" retry comment),
    // whenever the ABI-aligned real vector's own placement can't
    // reproduce the member's real, tightly-packed declared offset -- e.g.
    // a `mat3x4` member becomes `[3 x Marker<[4 x float]>]` rather than
    // `[3 x <4 x float>]`. `getTightVectorMarkerInnerType` recognizes this
    // positively (rather than guessing from a bare `[4 x float]`'s own
    // position, which cannot be told apart from a genuinely-declared,
    // directly-authored multi-dimensional scalar array -- see that
    // function's own comment) -- treat the marked inner array's own
    // element count as this row's component axis, exactly like a
    // `FixedVectorType` below.
    if (Type *Inner = getTightVectorMarkerInnerType(Peeled)) {
      auto *ArrTy = cast<ArrayType>(Inner);
      return {ArrTy->getElementType(),
              static_cast<unsigned>(ArrTy->getNumElements()),
              static_cast<unsigned>(RowCount)};
    }
    if (auto *ArrTy = dyn_cast<ArrayType>(Peeled)) {
      RowCount *= ArrTy->getNumElements();
      PerRowTy = ArrTy->getElementType();
      continue;
    }
    PerRowTy = Peeled;
    break;
  }
  if (auto *VecTy = dyn_cast<FixedVectorType>(PerRowTy))
    return {VecTy->getElementType(), VecTy->getNumElements(),
            static_cast<unsigned>(RowCount)};
  return {PerRowTy, /*ComponentCount=*/1, static_cast<unsigned>(RowCount)};
}

/// (Roadmap H101t) Whether \p Ty is a genuine multi-member nested struct
/// -- one whose own real members (e.g. `all_unordered_and_instance_array
/// .2`'s own `!spirv.struct<(mat3x3 [RelaxedPrecision], vector<4xsi32>)>`
/// nested-struct member, which H101s's own `getTightNestedStructType`
/// legalizes to a genuine two-member LLVM struct) `getStageIORowShape`
/// cannot represent as one `SignatureElement`: unlike a matrix/array's
/// own single, uniformly-typed row axis, two distinct real members can
/// have two distinct scalar types (here, a matrix's own `float` rows
/// alongside a vector's own `sint32` row), which `SignatureElement`'s
/// single `ComponentType`/`BitWidth` pair has no room for. Excludes a
/// single-member wrapper (already peeled transparently by
/// `peelSingleMemberStruct`) and a tight-vector marker struct (which
/// `getStageIORowShape` already recognizes positively as a row's own
/// component axis, not a genuine aggregate of independent members).
/// (Roadmap H115) Recurses through any outer `ArrayType` wrapping (e.g.
/// `S blockSa[2];`, an *array* of a genuine multi-member nested struct --
/// as opposed to a lone instance) so this array-of-struct shape is
/// recognized exactly like a lone instance is, everywhere this function
/// already gates "does this member need per-leaf `SignatureElement`
/// expansion instead of one flat, opaque row": `getStageIORowShape`
/// cannot represent a multi-member struct's own leaves as one row
/// regardless of how many array dimensions wrap it, and previously
/// silently mis-collapsed it into one bogus row (mixing every leaf's own
/// distinct scalar type and every array element's own store onto one
/// shared `ElementID`) instead of ever reaching the per-leaf expansion
/// path below.
bool isGenuineMultiMemberNestedStruct(Type *Ty) {
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty))
    return isGenuineMultiMemberNestedStruct(ArrTy->getElementType());
  auto *ST = dyn_cast<StructType>(Ty);
  return ST && ST->getNumElements() > 1 && !getTightVectorMarkerInnerType(ST);
}

/// (Roadmap H101t) The total number of `Location`-consuming rows \p Ty
/// occupies, recursing into any genuine multi-member nested struct (see
/// `isGenuineMultiMemberNestedStruct` above) to sum each of its own real
/// members' own row counts, rather than `getStageIORowShape`'s own
/// single-element answer of 1 (which silently treats the whole nested
/// struct as if it were one opaque, un-decomposed scalar row) -- the
/// same per-member `Location` bookkeeping `addStageIOStructMembers`
/// below performs when it actually emits one `SignatureElement` per
/// leaf, kept in its own function so the first (declared-order,
/// `Location`-computing) `addElements` pass can call it before any
/// `SignatureElement` actually exists yet.
uint32_t getStageIOFlattenedRowCount(Type *Ty) {
  if (!isGenuineMultiMemberNestedStruct(Ty))
    return getStageIORowShape(Ty).RowCount;
  // (Roadmap H115) An array of a genuine multi-member nested struct
  // folds its own array dimension into the total the same way an
  // ordinary scalar/vector member's own outer array dimension already
  // does (see `getStageIORowShape`'s own array-peeling loop): each of
  // the \p Ty array's own instances contributes its element type's own
  // (recursively computed) flattened row count.
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty))
    return static_cast<uint32_t>(ArrTy->getNumElements()) *
           getStageIOFlattenedRowCount(ArrTy->getElementType());
  uint32_t Total = 0;
  for (Type *FieldTy : cast<StructType>(Ty)->elements())
    Total += getStageIOFlattenedRowCount(FieldTy);
  return Total;
}

/// (Roadmap H101t) The number of leaf `SignatureElement`s (and thus the
/// number of consecutive `ElementIDs[GV]` entries) \p Ty contributes as a
/// stage-IO block member, recursing into any genuine multi-member nested
/// struct (see `isGenuineMultiMemberNestedStruct` above) to sum each of
/// its own real members' own leaf counts, rather than the always-1 answer
/// every other member shape has. `resolveOffsetWithinElement` uses this
/// to walk a block's own physical fields and find the right starting
/// index into its `IDs` for a given physical field, now that a genuine
/// multi-member nested-struct field can contribute more than one
/// `ElementID` (unlike every other field, which still contributes
/// exactly one).
uint32_t getStageIOLeafElementCount(Type *Ty) {
  if (!isGenuineMultiMemberNestedStruct(Ty))
    return 1;
  // (Roadmap H115) An array of a genuine multi-member nested struct
  // contributes exactly as many leaf `SignatureElement`s as a lone
  // instance would -- the array dimension is folded into each leaf's
  // own `RowCount` (see `getStageIOFlattenedRowCount` above), not
  // multiplied into the leaf *count*.
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty))
    return getStageIOLeafElementCount(ArrTy->getElementType());
  uint32_t Total = 0;
  for (Type *FieldTy : cast<StructType>(Ty)->elements())
    Total += getStageIOLeafElementCount(FieldTy);
  return Total;
}

/// (Roadmap H101t) Decomposes a genuine multi-member nested-struct
/// stage-IO member (see `isGenuineMultiMemberNestedStruct` above) into
/// one `AddElement` call per leaf field, recursing into any
/// further-nested genuine multi-member struct. Mirrors GLSL's own
/// implicit block layout rule for a nested struct member that carries no
/// `Offset` decoration of its own (case `.2`'s own repro shape has
/// none): each real member gets the next sequential `Location` (one per
/// row, exactly like `TakeBlockPath`'s own top-level per-member loop
/// already computes via `RowCount`), and its own `XfbOffset` is
/// \p BaseD's own `XfbOffset` plus this field's byte offset within
/// \p ST, read from \p DL since there is no explicit per-member `Offset`
/// decoration to read instead -- exactly the byte position H101s's own
/// tight-vector substitution inside \p ST was designed to make match a
/// real SPIR-V-declared layout. \p NextLocation is threaded through (not
/// recomputed locally) so a struct with more than one genuine
/// multi-member nested-struct field in a row still assigns strictly
/// increasing `Location`s across all of them.
///
/// (Roadmap H115) \p Ty may also be an *array* of a genuine multi-member
/// nested struct (e.g. `S blockSa[2];`) rather than a lone instance: each
/// leaf field emitted for one array instance is re-wrapped in the same
/// outer array dimension before being handed to \p AddElement, so
/// `addElement`'s own `getStageIORowShape` call folds this array
/// dimension into that leaf's `RowCount` the same way it already folds
/// an ordinary scalar/vector member's own outer array dimension --
/// rather than the whole array-of-struct collapsing onto one bogus,
/// opaque row (mixing every leaf's own distinct type and every array
/// instance's own store onto a single shared `ElementID`).
/// \p NextLocation is corrected for the array multiplier once the
/// single-instance recursive pass completes, so a following sibling
/// member still gets a `Location` past every row this array-of-struct
/// member actually spans.
void addStageIOStructMembers(
    function_ref<void(GlobalVariable *, unsigned,
                      const ParsedSPIRVDecorations &, Type *)>
        AddElement,
    GlobalVariable *GV, unsigned AddrSpace,
    const ParsedSPIRVDecorations &BaseD, Type *Ty,
    const DataLayout &DL, uint32_t &NextLocation) {
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    uint32_t LocationBeforeInstance = NextLocation;
    auto WrappedAddElement = [&](GlobalVariable *WrappedGV,
                                 unsigned WrappedAddrSpace,
                                 const ParsedSPIRVDecorations &WrappedD,
                                 Type *FieldTy) {
      AddElement(WrappedGV, WrappedAddrSpace, WrappedD,
                ArrayType::get(FieldTy, ArrTy->getNumElements()));
    };
    addStageIOStructMembers(WrappedAddElement, GV, AddrSpace, BaseD,
                            ArrTy->getElementType(), DL, NextLocation);
    uint32_t PerInstanceLocationSpan = NextLocation - LocationBeforeInstance;
    NextLocation = LocationBeforeInstance +
                   PerInstanceLocationSpan *
                       static_cast<uint32_t>(ArrTy->getNumElements());
    return;
  }
  auto *ST = cast<StructType>(Ty);
  const StructLayout *SL = DL.getStructLayout(ST);
  for (unsigned I = 0, E = ST->getNumElements(); I != E; ++I) {
    Type *FieldTy = ST->getElementType(I);
    if (isGenuineMultiMemberNestedStruct(FieldTy)) {
      addStageIOStructMembers(AddElement, GV, AddrSpace, BaseD, FieldTy, DL,
                              NextLocation);
      continue;
    }
    ParsedSPIRVDecorations FieldD = BaseD;
    FieldD.Location = NextLocation;
    FieldD.XfbOffset =
        BaseD.XfbOffset.value_or(0) + SL->getElementOffset(I);
    AddElement(GV, AddrSpace, FieldD, FieldTy);
    NextLocation += getStageIOFlattenedRowCount(FieldTy);
  }
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

/// (Roadmap L39) The load-side mirror of `storeTaskPayloadValue` just
/// below: decomposes a task-payload read's own \p Ty into one scalar
/// `feme.stage.task.payload.load` per (struct member, array index, vector
/// lane), rebuilt with `insertvalue`/`insertelement` -- the same shape
/// `loadStageIOValue` already decomposes an ordinary stage-IO element
/// into, but addressed by a plain byte \p Offset (via \p DL) rather than
/// (ElementID, Row, Component), since a task payload carries no signature
/// element of its own. A task-payload load's/store's value previously
/// reached `feme.stage.task.payload.load`/`.store` with its real,
/// possibly-aggregate SPIR-V-derived type wrapped verbatim (unlike an
/// ordinary stage-IO access, which is always pre-decomposed to a scalar
/// leaf before reaching any `feme.stage.*` call) -- a shape
/// `feme::cpu::SIMDizePass`'s own generic per-lane widening was never
/// designed to accept (its `FunctionWidener::getWidened` asserts against
/// a vector-typed operand). Decomposing here, at the same boundary every
/// other stage-IO access is already decomposed at, keeps every
/// `feme.stage.*` call's own operand/result scalar-only -- the invariant
/// `feme::cpu::SIMDize.cpp`'s widening is designed around -- rather than
/// teaching that pass to special-case a vector-typed payload operand.
///
/// (Roadmap L47) \p Offset is a `Value*` (always `i32`) rather than a
/// plain `uint64_t`: a task/mesh payload's own array member can be
/// addressed by a genuinely dynamic per-invocation index (e.g.
/// `payload.branch[gl_LocalInvocationIndex]`, see
/// `getTaskPayloadDynamicOffsetAccess` below), not just the
/// compile-time-constant offset every caller before this row ever passed.
/// Every recursive add below goes through `IRBuilder`'s own constant
/// folder, so a genuinely constant \p Offset (the overwhelmingly common
/// case) still folds down to a single `ConstantInt` at each step, exactly
/// as if this were still plain `uint64_t` arithmetic -- only a real
/// dynamic \p Offset actually emits an `add` instruction.
llvm::Value *loadTaskPayloadValue(IRBuilderBase &B, Type *Ty, Value *Offset,
                                  const DataLayout &DL) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(ST);
    Value *New = PoisonValue::get(ST);
    for (unsigned I = 0, E = ST->getNumElements(); I != E; ++I) {
      Value *MemberOffset =
          B.CreateAdd(Offset, B.getInt32(SL->getElementOffset(I)));
      Value *MemberVal =
          loadTaskPayloadValue(B, ST->getElementType(I), MemberOffset, DL);
      New = B.CreateInsertValue(New, MemberVal, I);
    }
    return New;
  }
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    uint64_t ElemSize = DL.getTypeAllocSize(ArrTy->getElementType());
    Value *New = PoisonValue::get(ArrTy);
    for (unsigned I = 0, E = ArrTy->getNumElements(); I != E; ++I) {
      Value *ElemOffset = B.CreateAdd(Offset, B.getInt32(I * ElemSize));
      Value *ElemVal =
          loadTaskPayloadValue(B, ArrTy->getElementType(), ElemOffset, DL);
      New = B.CreateInsertValue(New, ElemVal, I);
    }
    return New;
  }
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    uint64_t ElemSize = DL.getTypeAllocSize(VecTy->getElementType());
    Value *New = PoisonValue::get(VecTy);
    for (unsigned I = 0, E = VecTy->getNumElements(); I != E; ++I) {
      Value *ElemOffset = B.CreateAdd(Offset, B.getInt32(I * ElemSize));
      Value *ElemVal =
          loadTaskPayloadValue(B, VecTy->getElementType(), ElemOffset, DL);
      New = B.CreateInsertElement(New, ElemVal, I);
    }
    return New;
  }
  return createStageTaskPayloadLoad(B, Ty, Offset);
}

/// (Roadmap L39) The store-side mirror of `loadTaskPayloadValue` above:
/// decomposes \p Val (of type \p Ty) into one scalar
/// `feme.stage.task.payload.store` per (struct member, array index,
/// vector lane), the same recursion in reverse (`extractvalue`/
/// `extractelement` instead of `insertvalue`/`insertelement`), addressed
/// by a plain byte \p Offset rather than (ElementID, Row, Component). See
/// `loadTaskPayloadValue`'s own comment for why this decomposition
/// happens here rather than in `feme::cpu::SIMDize.cpp`. (Roadmap L47)
/// \p Offset is a `Value*` for the same reason `loadTaskPayloadValue`'s
/// own \p Offset is -- see its comment.
void storeTaskPayloadValue(IRBuilderBase &B, Value *Val, Type *Ty,
                           Value *Offset, const DataLayout &DL) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(ST);
    for (unsigned I = 0, E = ST->getNumElements(); I != E; ++I) {
      Value *MemberOffset =
          B.CreateAdd(Offset, B.getInt32(SL->getElementOffset(I)));
      storeTaskPayloadValue(B, B.CreateExtractValue(Val, I),
                            ST->getElementType(I), MemberOffset, DL);
    }
    return;
  }
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    uint64_t ElemSize = DL.getTypeAllocSize(ArrTy->getElementType());
    for (unsigned I = 0, E = ArrTy->getNumElements(); I != E; ++I) {
      Value *ElemOffset = B.CreateAdd(Offset, B.getInt32(I * ElemSize));
      storeTaskPayloadValue(B, B.CreateExtractValue(Val, I),
                            ArrTy->getElementType(), ElemOffset, DL);
    }
    return;
  }
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    uint64_t ElemSize = DL.getTypeAllocSize(VecTy->getElementType());
    for (unsigned I = 0, E = VecTy->getNumElements(); I != E; ++I) {
      Value *ElemOffset = B.CreateAdd(Offset, B.getInt32(I * ElemSize));
      storeTaskPayloadValue(B, B.CreateExtractElement(Val, I),
                            VecTy->getElementType(), ElemOffset, DL);
    }
    return;
  }
  createStageTaskPayloadStore(B, Offset, Val);
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
///
/// (Roadmap H7w) That per-leaf-scalar scheme only works when `Row` is a
/// compile-time constant -- a distinct alloca per (ElementID, Row,
/// Component) triple, one per array element, is exactly how a
/// `gl_ClipDistance`/`gl_CullDistance`-shaped *constant*-indexed write
/// (e.g. a compile-time-unrolled loop) already gets handled. A
/// non-constant `Row` (a genuinely loop-carried index) has no compile-time
/// value to key such an alloca on, so it instead gets one `RowCount`-sized
/// array alloca per (ElementID, Component), GEP'd by the runtime `Row`
/// value for both its read-back load and its store.
/// `llvm::isAllocaPromotable` does not accept a variable-index GEP into an
/// alloca, so this array alloca is deliberately left out of
/// `takeAllocas()`'s own list: `PromoteMemToReg` never sees it, and it
/// stays ordinary stack memory rather than being lifted to SSA registers
/// -- correct, if a little less optimized, and the only option a
/// genuinely dynamic index leaves open (mirroring how a real GPU driver's
/// own dynamically indexed local array likewise cannot live in
/// registers).
class ShadowValueMap {
public:
  ShadowValueMap(Function &F, const EntrySignature &Sig) : F(F), Sig(Sig) {}

  Value *getOrCreate(uint32_t ElementID, Value *Row, Value *Component, Type *Ty,
                     IRBuilderBase &B) {
    uint64_t ComponentVal = cast<ConstantInt>(Component)->getZExtValue();
    if (auto *RowC = dyn_cast<ConstantInt>(Row)) {
      Key K{ElementID, RowC->getZExtValue(), ComponentVal};
      AllocaInst *&Slot = ScalarAllocas[K];
      if (!Slot) {
        IRBuilder<> EntryBuilder(&F.getEntryBlock(),
                                 F.getEntryBlock().getFirstInsertionPt());
        Slot =
            EntryBuilder.CreateAlloca(Ty, nullptr, "feme.stage.output.shadow");
      }
      return Slot;
    }

    DynamicKey DK{ElementID, ComponentVal};
    AllocaInst *&Slot = DynamicAllocas[DK];
    if (!Slot) {
      uint32_t RowCount = ElementID < Sig.Elements.size()
                              ? Sig.Elements[ElementID].RowCount
                              : 1;
      IRBuilder<> EntryBuilder(&F.getEntryBlock(),
                               F.getEntryBlock().getFirstInsertionPt());
      Slot = EntryBuilder.CreateAlloca(ArrayType::get(Ty, RowCount), nullptr,
                                       "feme.stage.output.shadow.dyn");
    }
    return B.CreateInBoundsGEP(Slot->getAllocatedType(), Slot,
                               {B.getInt32(0), Row});
  }

  bool empty() const { return ScalarAllocas.empty(); }

  /// Every promotable (constant-`Row`) shadow alloca created so far, for
  /// `PromoteMemToReg` to convert to SSA form once every instruction has
  /// been rewritten. A dynamic-`Row` array alloca (see this class's own
  /// comment) is never included: it is not promotable, and stays ordinary
  /// stack memory instead.
  SmallVector<AllocaInst *, 8> takeAllocas() const {
    SmallVector<AllocaInst *, 8> Result;
    for (const auto &KV : ScalarAllocas)
      Result.push_back(KV.second);
    return Result;
  }

private:
  using Key = std::tuple<uint32_t, uint64_t, uint64_t>;
  using DynamicKey = std::pair<uint32_t, uint64_t>;
  Function &F;
  const EntrySignature &Sig;
  DenseMap<Key, AllocaInst *> ScalarAllocas;
  DenseMap<DynamicKey, AllocaInst *> DynamicAllocas;
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
    unsigned RE = ArrTy->getNumElements();
    for (unsigned R = 0; R != RE; ++R) {
      // (Roadmap H101c) `Row` may already be a non-zero base when this
      // array is nested one level inside an outer one (GLSL's own "array
      // of block instances" syntax, `layout(...) out Block { mat4 var; }
      // block[3];`, reaches here with `Row` seeded to the outer instance
      // index, decomposing the matrix's own 4 columns on top of it) --
      // combine (`Row * RE + R`), rather than discard the incoming `Row`
      // outright the way overwriting it with a bare `B.getInt32(R)`
      // always did before, which silently collapsed every instance's own
      // inner rows onto the same absolute row 0..RE-1 regardless of which
      // instance was being written/read.
      Value *CombinedRow =
          Row ? B.CreateAdd(B.CreateMul(Row, B.getInt32(RE)), B.getInt32(R))
              : B.getInt32(R);
      Value *RowVal =
          loadStageIOValue(B, ArrTy->getElementType(), ElementID, CombinedRow,
                           Component, Zero, Name, Shadow);
      New = B.CreateInsertValue(New, RowVal, R);
    }
    return New;
  } else if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    Value *New = PoisonValue::get(VecTy);
    for (unsigned C = 0, CE = VecTy->getNumElements(); C != CE; ++C) {
      // (Roadmap L94(h)) Combine (`Component + C`), rather than discard
      // an incoming non-zero base `Component` outright the way
      // overwriting it with a bare `B.getInt32(C)` always did before --
      // mirrors `CombinedRow`'s own fix just above for the analogous
      // array-nesting case. A whole-vector access at a non-zero
      // `FirstComponent` (SPIR-V's `Component` decoration) reaches here
      // with `Component` already seeded to that base (see
      // `resolveElementBaseComponent`), so decomposing it one component
      // at a time must still land on the variable's own real components,
      // not silently restart from 0.
      Value *CombinedComponent =
          Component ? B.CreateAdd(Component, B.getInt32(C)) : B.getInt32(C);
      Value *Elt = loadStageIOValue(B, VecTy->getElementType(), ElementID, Row,
                                    CombinedComponent, Zero, Name, Shadow);
      New = B.CreateInsertElement(New, Elt, C);
    }
    return New;
  }
  if (Shadow)
    return B.CreateLoad(
        Ty, Shadow->getOrCreate(ElementID, Row, Component, Ty, B), Name);
  // (Roadmap H6m) A `bool` (`i1`) leaf scalar was canonicalized to a
  // 32-bit element by `getComponentType` above, so the `feme.stage.
  // input.load` this element actually reads is `i32`-typed; narrow the
  // loaded value back to `i1` here, at the same boundary, rather than
  // leaving `Ty`'s two callers (the recursive cases above and
  // `resolveOffsetWithinElement`'s own single-scalar caller) to each
  // know about the widening.
  if (Ty->isIntegerTy(1)) {
    Value *Wide = createStageInputLoad(B, B.getInt32Ty(), ElementID, Row,
                                       Component, Zero, Name);
    return B.CreateTrunc(Wide, Ty);
  }
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
    unsigned RE = ArrTy->getNumElements();
    for (unsigned R = 0; R != RE; ++R) {
      // (Roadmap H101c) See `loadStageIOValue`'s own mirrored comment:
      // combine, rather than overwrite, an incoming non-zero base `Row`.
      Value *CombinedRow =
          Row ? B.CreateAdd(B.CreateMul(Row, B.getInt32(RE)), B.getInt32(R))
              : B.getInt32(R);
      storeStageIOValue(B, B.CreateExtractValue(Val, R),
                        ArrTy->getElementType(), ElementID, CombinedRow,
                        Component, Zero, Shadow);
    }
    return;
  } else if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    for (unsigned C = 0, CE = VecTy->getNumElements(); C != CE; ++C) {
      // (Roadmap L94(h)) See `loadStageIOValue`'s own mirrored comment:
      // combine, rather than overwrite, an incoming non-zero base
      // `Component`.
      Value *CombinedComponent =
          Component ? B.CreateAdd(Component, B.getInt32(C)) : B.getInt32(C);
      storeStageIOValue(B, B.CreateExtractElement(Val, C),
                        VecTy->getElementType(), ElementID, Row,
                        CombinedComponent, Zero, Shadow);
    }
    return;
  }
  // (Roadmap H6m) The store-side mirror of `loadStageIOValue`'s own `i1`
  // widening: `getComponentType` canonicalized this leaf's element to a
  // 32-bit scalar, so the `feme.stage.output.store` this emits must widen
  // \p Val to match, not store the bare `i1` `StageStorage` has no
  // addressable representation for. The shadow alloca (read-back within
  // this same invocation) keeps \p Val's own `i1` type, matching
  // `loadStageIOValue`'s `Shadow` load above, which is narrowed back down
  // from `i32` only on the non-shadow (real stage-IO) path.
  Value *StoredVal =
      Ty->isIntegerTy(1) ? B.CreateZExt(Val, B.getInt32Ty()) : Val;
  createStageOutputStore(B, ElementID, Row, Component, StoredVal, Zero);
  if (Shadow)
    B.CreateStore(Val, Shadow->getOrCreate(ElementID, Row, Component, Ty, B));
}

/// (Roadmap L94(h)) \p MemberIDs' own base `Component` (SPIR-V's
/// `Component` decoration, `SignatureElement::FirstComponent`), for a
/// whole-variable stage-IO access that carries no `Access->Component` of
/// its own (i.e. `resolveOffsetWithinElement`'s `ValueTy == ElemTy` case,
/// which never needs to peel into a sub-range and so never computes one).
/// Before this helper, every such access's `Component` operand defaulted
/// to a plain `Zero` regardless of the element's own `FirstComponent`,
/// silently addressing component 0 of `StageStorage` even for a
/// `Component`-packed variable declared to start at, say, component 2 --
/// this is a single-member (non-block) access only (a builtin interface
/// block's own per-member elements are never `Component`-packed), so
/// returns 0 for any multi-member \p MemberIDs.
uint32_t resolveElementBaseComponent(const EntrySignature &Sig,
                                     ArrayRef<uint32_t> MemberIDs) {
  if (MemberIDs.size() != 1)
    return 0;
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == MemberIDs[0])
      return Elt.FirstComponent;
  return 0;
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
/// is directly an `ArrayType`. Shared between `getDynamicVertexIndexedAccess`'s
/// own non-constant-index recognition and `resolveStageIOAccess`'s ordinary
/// constant-offset path, so a *constant* `gl_in[k]` index is folded into
/// the same `Vertex` operand a non-constant one is (roadmap H5f), not
/// into `Row`. Sets \p AddrSpace to \p GV's address space when true.
///
/// (Roadmap L24(a)) Restricted to \p Stage `== Hull || Domain ||
/// Geometry`: a hull entry's own `InputPatch<T, N>` (each invocation
/// legitimately reads any control point's own attribute -- see
/// `HullWrapper.cpp`'s `lowerHullInputLoad`) and a domain entry's own
/// `OutputPatch<T, N>` (`DomainWrapper.cpp`'s `lowerDomainControlPointLoad`)
/// are addressed through this exact same shape, one array dimension
/// wrapping the control-point count instead of geometry's own
/// `VerticesPerPrimitive`, so both need the identical fold. But *every*
/// other stage's own `Input`-storage array-typed global is a plain,
/// ordinary multi-element varying with no such per-invocation-selectable
/// vertex/control-point dimension at all (e.g. a fragment or vertex
/// entry's own `float arr[4] : MY_ARRAY`) -- an earlier, unrestricted
/// version of this check misrouted exactly that shape's own *constant*
/// array index (e.g. `arr[1]`) into the `Vertex` operand instead of `Row`,
/// which happened to still produce a constant value, so
/// `feme::graphics::ValidateStagePass`'s `validateVertex` (whose own
/// non-Geometry/Mesh restriction only fires for a *non*-constant vertex
/// operand) never caught it -- it surfaced only much later, and
/// confusingly, as `feme-cpu-wrap-fragment`'s own generic "synthetic
/// fragment layouts only support vertex operand 0" diagnostic, for
/// whichever array element's misfolded index happened to be nonzero
/// (`ArraySemantics.test`'s own `arr[1]`/`arr[2]`/`arr[3]` reads). Fixing
/// this check itself, rather than teaching `validateVertex` to also flag a
/// wrong-but-constant operand, keeps the fold from happening at all for
/// the stages that never legitimately need it.
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
/// found, and why it stops here" in VulkanCTSReport.md. (Roadmap H6k) That
/// reasoning holds for every stage this implementation gives a genuine
/// per-row matrix output to *except* mesh: see
/// `isPerVertexArrayMeshOutputGlobal` below for the narrower, `Mesh`-only
/// exception this row adds instead of loosening this check itself.
///
/// (Roadmap L24(b)) Excludes a `Patch`-decorated global -- above all
/// `gl_TessLevelOuter`/`gl_TessLevelInner` (`BuiltIn TessLevelOuter`/
/// `TessLevelInner`, always `Patch`, always declared `[4 x float]`/
/// `[2 x float]` regardless of tess domain) read back as a Domain stage's
/// own `Input` -- from this same per-vertex-array classification. Those
/// two are not per-vertex-arrayed at all: every row is a genuine,
/// independently meaningful whole-patch tess factor, the exact shape the
/// `Output`-side `PerInvocationOutputArray` peeling in `addElements`
/// already excludes them from (see that check's own `D.Patch` comment).
/// Before this fix, a Domain stage's own `gl_TessLevelOuter`/
/// `gl_TessLevelInner` `Input` element was wrongly flagged
/// `RowCountIsVertexArray`, so `StageLink.cpp`'s `effectiveRowCount`
/// folded its real `RowCount` (4/2) down to `1` when linking against the
/// Hull stage's own (correctly unfolded, roadmap L24(b)) patch-constant
/// producer element, a spurious `vkQueueSubmit`-time "disagree on
/// component/row count or type" -- `Feature/Semantics/HullSystemValues.
/// test`'s own regression while fixing that Hull-side bug, since before
/// it, both sides happened to independently collapse to `RowCount == 1`
/// and coincidentally agreed.
bool isPerVertexArrayInputGlobal(const GlobalVariable *GV,
                                 unsigned &AddrSpace, ShaderStage Stage) {
  if (Stage != ShaderStage::Hull && Stage != ShaderStage::Domain &&
      Stage != ShaderStage::Geometry)
    return false;
  if (!isSPIRVStageIOGlobal(GV, AddrSpace) || AddrSpace != 7)
    return false;
  if (!isa<ArrayType>(GV->getValueType()))
    return false;
  ParsedSPIRVDecorations D =
      parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
  return !D.Patch;
}

/// (Roadmap H6k) Whether \p GV is a mesh entry's own per-vertex/
/// per-primitive `Output` array (address space 8) -- the `Output`-side
/// counterpart to `isPerVertexArrayInputGlobal` above, deliberately scoped
/// to \p Stage `== ShaderStage::Mesh` rather than every stage: unlike a
/// mesh entry, a non-mesh stage's own `Output`-storage array can be a real
/// matrix's own per-row storage (`isPerVertexArrayInputGlobal`'s own
/// comment on why the constant-index fold below stays unsafe there), so
/// this must not misroute a `Vertex`/`Geometry`/`Domain`/`Hull` stage's own
/// constant-indexed matrix output store into `Vertex`. A mesh entry has no
/// such ordinary matrix output to begin with -- every plain `Output` write
/// it makes is through this same per-vertex/per-primitive array (see
/// H6j's own `addElements` comment) -- so the fold is safe precisely
/// because \p Stage narrows it to that one case. Unlike an earlier version
/// of this helper, a builtin interface block (its own
/// `feme.spirv.MemberDecorations`, e.g. an arrayed `gl_MeshVerticesEXT`/
/// `gl_MeshPrimitivesEXT`) is *not* excluded: `resolveOffsetWithinElement`
/// already knows how to pick the right member's own `ElementID` out of a
/// struct-shaped element type (`addElements`'s own per-member block path
/// assigns one `ElementID` per member), the exact same way
/// `getDynamicVertexIndexedAccess`'s own dynamic-index counterpart already
/// relies on it doing for a *non*-constant vertex index
/// (`ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberStore` covers
/// that side) -- excluding the block shape here only for a *constant*
/// index left it the one shape this fold still routed through the wrong
/// (matrix-`Row`) path instead, corrupting `StageStorage` for a real
/// `gl_MeshVerticesEXT[k].gl_Position = ...`-shaped, compile-time-unrolled
/// store (this row's own crash). Sets \p AddrSpace to \p GV's address
/// space when true.
bool isPerVertexArrayMeshOutputGlobal(const GlobalVariable *GV,
                                      unsigned &AddrSpace, ShaderStage Stage) {
  if (Stage != ShaderStage::Mesh || !isSPIRVStageIOGlobal(GV, AddrSpace) ||
      AddrSpace != 8)
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
///
/// (Roadmap H117/H118) Excludes a `patch`-decorated global -- mirroring
/// `isPerVertexArrayInputGlobal`'s own identical `!D.Patch` exclusion --
/// since a `patch`-qualified, genuinely multi-member `Block`-decorated
/// interface block wrapped in an outer array (glslang's "array of block
/// instances" syntax, e.g. `patch out TheBlock {...} tcBlock[2];`) has
/// the exact same structural shape (an `ArrayType` in address space 7/8)
/// as a real per-vertex/per-primitive array, but is a fundamentally
/// different thing: the array dimension selects one of several
/// independently-captured, non-interpolated *patch* instances, not one of
/// a stage's own fixed per-vertex/per-primitive slots.
/// `getDynamicRowIndexedAccess`'s own generic, recursive row-folding
/// (`collectDynamicRowTerms`) already models this shape correctly (each
/// array level's own instance index folds into `Row`, exactly like an
/// ordinary array-of-struct member's own array dimension one level
/// further in) -- but only for a global this function does *not* also
/// claim, since `getDynamicRowIndexedAccess` deliberately defers to
/// `getDynamicVertexIndexedAccess` for every global this function does
/// match, to avoid double-recognizing the same shape two different ways.
/// Before this fix, a `patch`-qualified block-array global was wrongly
/// claimed here instead, threading its own instance index through as a
/// bogus `Vertex` operand -- a dimension this element's own storage has
/// no room for -- leaving the block's own backing global still
/// `external`/unresolved at JIT-link time (`"Symbols not found: [
/// spirv_var_N ]"`, `dEQP-VK.tessellation.user_defined_io.
/// per_patch_block_array`/`per_vertex_block`'s own crash).
bool isDynamicIndexedArrayGlobal(const GlobalVariable *GV,
                                 unsigned &AddrSpace) {
  if (!isSPIRVStageIOGlobal(GV, AddrSpace) ||
      (AddrSpace != 7 && AddrSpace != 8))
    return false;
  if (!isa<ArrayType>(GV->getValueType()))
    return false;
  // (Roadmap H117/H118) GLSL's `patch` qualifier only ever applies to a
  // whole interface block, never to one member individually, so glslang
  // never emits a `Patch` decoration on the block-array variable itself
  // (only an ordinary `Location`) -- it instead emits `OpMemberDecorate
  // ... Patch` on every one of the block *type*'s own members, captured
  // here the same way `classifyTessControlOutputStoreFrequency` already
  // reads it (checking the first member reflects the whole block, since
  // every member of a genuine `patch out` block carries an identical
  // `Patch` decoration). A plain (non-block) `Patch`-decorated global
  // still carries its own `Patch` decoration directly, handled by the
  // fallback below.
  if (const MDNode *MemberMD = GV->getMetadata("feme.spirv.MemberDecorations")) {
    for (const auto &KV : parseSPIRVMemberDecorations(MemberMD))
      return !KV.second.Patch;
  }
  ParsedSPIRVDecorations D =
      parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
  return !D.Patch;
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
/// (the vertex/primitive index) -- with every further index, if any but
/// (roadmap H92) the last, constant (a builtin interface block's own
/// member, or a matrix row within that one vertex's own value), resolved
/// into a byte offset the same way `resolveRowComponent` already does for
/// the ordinary constant-offset path, just starting one array dimension
/// in. (Roadmap H5f) A constant vertex index is *not* left unresolved
/// here: `resolveStageIOAccess`'s own ordinary constant-offset path
/// (`getStageIOBaseAndOffset`) folds it in too, using
/// `isPerVertexArrayInputGlobal` (below) to recognize the same global
/// shape and route that constant index through `Vertex` there as well,
/// for consistency with the dynamic case this function handles. (Roadmap
/// H92) A *second* genuinely non-constant index -- e.g.
/// `loc[pointIdx].elements[elemIdx]`, a mesh entry's own per-vertex output
/// block whose one array member is itself dynamically indexed by a
/// second loop variable -- is threaded through as \p RowIndex instead of
/// folding into \p ByteOffset, mirroring `getDynamicRowIndexedAccess`'s
/// own single-non-constant-row-index shape (see that function's own
/// comment), just with a per-vertex/per-primitive array dimension already
/// peeled off first. Before this, `resolveOffsetWithinElement`'s own
/// byte-offset-based recursion had no way to represent a second
/// non-constant index at all, so a doubly-dynamic-indexed access like
/// this one was rejected outright by this function's own "only the
/// vertex index may be non-constant" check, surfacing later as
/// `feme-graphics-validate-stage`'s "unresolved stage-IO global-variable
/// access" diagnostic.
///
/// The result of `getDynamicVertexIndexedAccess`: \p GV's own per-vertex/
/// per-primitive array dimension peeled into \p VertexIndex, plus whatever
/// is left to resolve one vertex's own value with. Ordinarily that
/// remainder is a plain, fully-constant \p ByteOffset (the common case
/// `resolveOffsetWithinElement`'s own byte-offset-based recursion
/// resolves from there) -- but (roadmap H92) \p RowIndex is set instead,
/// with \p ByteOffset left at the constant prefix consumed before it, when
/// exactly one more, genuinely non-constant index follows the vertex one,
/// directly selecting a row within an array (mirroring
/// `getDynamicRowIndexedAccess`'s own single-non-constant-index shape, one
/// per-vertex array dimension in). \p RowIndex is `nullptr` for the
/// ordinary, fully-constant-remainder case.
///
/// (Roadmap H110) \p VertexIndex itself may also be a compile-time
/// constant when \p RowIndex is set -- e.g. `ls[0].location_var[i]`, a
/// per-primitive output block with exactly one primitive (so the outer
/// index is always the constant `0`) whose one struct member is itself an
/// array written through a loop-carried `i`. This function is still the
/// only one that resolves that shape: `getDynamicRowIndexedAccess`
/// explicitly excludes any `isDynamicIndexedArrayGlobal` global (to avoid
/// double-recognizing this function's own genuinely-dynamic-vertex-index
/// shape), so it never gets a chance to see this one either, even though
/// its own outer index happens to be constant this time. When \p
/// RowIndex is null, \p VertexIndex is never constant (the "ordinary
/// constant-offset path" caveat above), since that fully-constant case is
/// left for `getStageIOBaseAndOffset` to resolve as before.
///
/// (Roadmap H7w) \p Member is only meaningful alongside \p RowIndex: which
/// of \p GV's own per-vertex struct members (if it has more than one --
/// e.g. `gl_PerVertex`'s `{Position, ClipDistance, CullDistance}`) the row
/// index selects a row within, since \p ByteOffset alone cannot tell
/// `resolveStageIOAccess` which of \p GV's several `ElementIDs` to use the
/// way it can for the ordinary, fully-constant-remainder case. Before this
/// field existed, `resolveStageIOAccess`'s own `RowIndex` branch assumed
/// \p GV had exactly one signature element and \p ByteOffset was always
/// `0` -- true for a plain, single-member global like `ls[0].location_
/// var[i]` above, but false for `gl_in[vertNdx].gl_ClipDistance[i]`/
/// `gl_CullDistance[i]` (a real `dEQP-VK.clipping.user_defined.
/// {clip_distance,clip_cull_distance}_dynamic_index.{vert_geom,vert_tess,
/// vert_tess_geom}.*` shape, `ClipDistance`/`CullDistance` never being
/// `gl_PerVertex`'s first member) -- silently leaving that access
/// unrewritten, which surfaced downstream not as a
/// `feme-graphics-validate-stage` diagnostic (its own "unresolved
/// stage-IO global-variable access" check has this exact gap too, see
/// that pass's own fix) but as a JIT-link-time `"Symbols not found: [
/// spirv_varN ]"` failure once the leftover raw load/GEP reached
/// `feme::cpu`'s JIT with no real definition for the SPIR-V-derived global
/// it still referenced.
///
/// (Roadmap H117/H118) Forward-declared here so this struct's own
/// `getDynamicVertexIndexedAccess` (defined immediately below) can
/// delegate to it directly, rather than duplicating its recursive
/// struct/array-walking logic -- see `collectDynamicRowTerms`'s own
/// definition further below for the full recursion.
std::optional<uint32_t>
collectDynamicRowTerms(Type *Ty, User::op_iterator &It, User::op_iterator End,
                       uint32_t &IDStart,
                       SmallVectorImpl<std::pair<Value *, uint64_t>> &Terms);

struct DynamicVertexIndexedAccess {
  GlobalVariable *GV;
  Value *VertexIndex;
  uint64_t ByteOffset;
  Value *RowIndex = nullptr;
  // (Roadmap H7w) Which of \p GV's own per-vertex struct members \p
  // RowIndex selects a row within -- e.g. `1` for `gl_ClipDistance` in a
  // `gl_PerVertex` block (`{Position, ClipDistance, CullDistance}`).
  // Meaningless when \p RowIndex is null (the ordinary constant-offset
  // path resolves that case without ever consulting it). Always `0` for a
  // plain, single-member global (a non-block per-vertex-arrayed output/
  // input with nothing but its one row-indexed array), matching the
  // implicit assumption every caller made before this field existed.
  unsigned Member = 0;
  // (Roadmap H117/H118) One (index, multiplier) pair per dynamic index
  // found among the indices *following* a genuine multi-member nested
  // struct member select within this per-vertex instance (e.g.
  // `blockSa[gl_InvocationID].blockSa[j].x`, `getDynamicVertexIndexedAccess`'s
  // own per-vertex-array counterpart of `DynamicRowIndexedAccess::Terms`
  // above) -- populated instead of (never alongside) \p RowIndex, by
  // delegating to the same `collectDynamicRowTerms` this struct's sibling
  // uses, whenever a non-constant index is found anywhere in the
  // remaining index walk (`RowIndex`'s own single-terminal-index
  // restriction cannot represent one more constant member-select after
  // it). Empty for every other shape, including the plain
  // single-non-constant-terminal-index one `RowIndex` already covers.
  SmallVector<std::pair<Value *, uint64_t>, 2> RowTerms;
};

/// Returns `std::nullopt` if \p Ptr is not this exact shape.
std::optional<DynamicVertexIndexedAccess>
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
  // (Roadmap H110) A *constant* vertex index is not rejected outright here
  // anymore: whether the ordinary constant-offset path
  // (`getStageIOBaseAndOffset`) can fully handle it instead depends on
  // whether a further, genuinely non-constant `RowIndex` also turns up
  // below (that path cannot fold a non-constant index at all) -- deferred
  // until after the walk below determines that.
  bool ConstantVertexIndex = isa<Constant>(VertexIndex);

  Type *CurTy = ArrTy->getElementType();

  // (Roadmap H117/H118) Try the same generic recursive resolver
  // `getDynamicRowIndexedAccess` uses first, on a *copy* of the iterator
  // so the ordinary walk below still runs unmodified if this finds no
  // dynamic index at all (a fully constant remainder, e.g. `gl_Position.x`,
  // still needs that walk's own vector-lane-select support this recursion
  // does not have -- see `collectDynamicRowTerms`'s own comment). Only
  // used when it finds at least one non-constant index (`Terms` non-empty)
  // -- e.g. `blockSa[gl_InvocationID].blockSa[j].x`, a genuine multi-member
  // nested struct's own array dimension indexed dynamically *and* followed
  // by one more constant member-select, a shape the walk below cannot
  // represent at all (its own `RowIndex` must be the very last index).
  {
    auto ProbeIt = IdxIt;
    ++ProbeIt;
    uint32_t IDStart = 0;
    SmallVector<std::pair<Value *, uint64_t>, 2> Terms;
    std::optional<uint32_t> RowCount =
        collectDynamicRowTerms(CurTy, ProbeIt, GEP->idx_end(), IDStart, Terms);
    if (RowCount && !Terms.empty())
      return DynamicVertexIndexedAccess{GV, VertexIndex, 0, nullptr,
                                        IDStart, std::move(Terms)};
  }

  uint64_t ByteOffset = 0;
  unsigned Member = 0;
  Value *RowIndex = nullptr;
  for (++IdxIt; IdxIt != GEP->idx_end(); ++IdxIt) {
    if (auto *CI = dyn_cast<ConstantInt>(*IdxIt)) {
      uint64_t Idx = CI->getZExtValue();
      if (auto *ST = dyn_cast<StructType>(CurTy)) {
        const StructLayout *SL = DL.getStructLayout(ST);
        ByteOffset += SL->getElementOffset(Idx);
        // (Roadmap H7w) Only meaningful once `RowIndex` is also set below
        // (see `DynamicVertexIndexedAccess::Member`'s own comment); simply
        // records the *last* struct member index seen, mirroring
        // `getDynamicRowIndexedAccess`'s own identical, single-level
        // tracking -- no real shape nests a struct within a struct here.
        Member = Idx;
        CurTy = ST->getElementType(Idx);
      } else if (auto *InnerArrTy = dyn_cast<ArrayType>(CurTy)) {
        ByteOffset += Idx * DL.getTypeAllocSize(InnerArrTy->getElementType());
        CurTy = InnerArrTy->getElementType();
      } else if (auto *VecTy = dyn_cast<FixedVectorType>(CurTy)) {
        // (Roadmap H76) A per-component write into a dynamically
        // vertex-indexed stage-IO member (`gl_MeshVerticesEXT[outIndex].
        // gl_Position.x = ...`, the real shape a real
        // `dEQP-VK.mesh_shader.ext.smoke.fast_lib.depth_only_*_position_
        // components` mesh entry's own per-component position write
        // compiles into, once `outIndex` itself -- unlike every other
        // constant-vertex-index test in this file -- is a genuinely
        // dynamic per-invocation value): one more constant index beyond
        // the struct member already peeled above, selecting a lane within
        // that member's own vector type. Missing this case fell through
        // to the `return std::nullopt` below, leaving the whole access
        // unrewritten and surfacing later as
        // `feme-graphics-validate-stage`'s "unresolved stage-IO
        // global-variable access" diagnostic -- exactly what this
        // milestone's own real CTS failures hit.
        ByteOffset += Idx * DL.getTypeAllocSize(VecTy->getElementType());
        CurTy = VecTy->getElementType();
      } else {
        return std::nullopt;
      }
      continue;
    }
    // (Roadmap H92) The one non-constant index left, other than the
    // vertex index already peeled above: must directly select a row
    // within an array, and must be the final index -- a component-level
    // index after it is not modeled, mirroring
    // `getDynamicRowIndexedAccess`'s own identical constraint.
    if (RowIndex || !isa<ArrayType>(CurTy) ||
        std::next(IdxIt) != GEP->idx_end())
      return std::nullopt;
    RowIndex = *IdxIt;
  }
  // (Roadmap H110) A constant vertex index with no further non-constant
  // `RowIndex` is a fully constant-offset access after all -- leave it for
  // the ordinary constant-offset path (`getStageIOBaseAndOffset`) to
  // resolve, exactly as before this roadmap entry. Only a constant vertex
  // index *paired with* a genuinely dynamic `RowIndex` (e.g.
  // `ls[0].location_var[i]`, a per-primitive output block with exactly one
  // primitive -- so the outer index is always the compile-time constant
  // `0` -- whose one struct member is itself an array written through a
  // loop-carried `i`) needs this function's own result: that shape's
  // `RowIndex` is a non-constant index `getStageIOBaseAndOffset` cannot
  // fold at all, but the outer per-primitive dimension is not itself
  // dynamic, so it was never `getDynamicRowIndexedAccess`'s shape either
  // (that function explicitly excludes any `isDynamicIndexedArrayGlobal`
  // global, to avoid double-recognizing this function's own genuinely
  // dynamic-vertex-index shape) -- previously falling through both
  // functions entirely unresolved.
  if (ConstantVertexIndex && !RowIndex)
    return std::nullopt;
  return DynamicVertexIndexedAccess{GV, VertexIndex, ByteOffset, RowIndex,
                                    Member, {}};
}

/// (Roadmap H7w) `gl_ClipDistance`/`gl_CullDistance` (or any other
/// stage-IO element whose own declared type is an array) addressed
/// through a non-constant index into that array's own row dimension --
/// e.g. `gl_ClipDistance[i]` with a loop-carried `i`, the shape a real
/// `dEQP-VK.clipping.user_defined.clip_distance_dynamic_index.*` vertex
/// shader compiles into -- as opposed to `getDynamicVertexIndexedAccess`'s
/// own non-constant *outer* per-vertex/per-primitive array dimension
/// (roadmap H5b/H6b). This shape is rooted directly at the stage-IO
/// global itself (no per-vertex array dimension to peel first), with
/// every index up to the array's own row dimension constant and only that
/// one final index non-constant. `getStageIOBaseAndOffset`'s
/// `stripAndAccumulateConstantOffsets` walk cannot fold a non-constant
/// index at all, so a `GetElementPtrInst` with one is left entirely
/// unresolved by every path that predates this one -- exactly the shape
/// that first hit `ValidateStagePass`'s "unresolved stage-IO
/// global-variable access" diagnostic in that real CTS case.
///
/// Deliberately excludes any global `getDynamicVertexIndexedAccess`
/// already recognizes (`isDynamicIndexedArrayGlobal`), so a genuinely
/// per-vertex-/per-primitive-arrayed global's own outer dimension is
/// never double-recognized as a member's own row dimension instead.
///
/// (Roadmap H115/H117/H118) A genuine multi-member nested struct (or
/// array-of-struct) reached through the array's own row dimension is
/// modeled too, via `collectDynamicRowTerms` below: one further constant
/// index selecting a member within it (e.g. `blockSa[i].x`), or -- if
/// that member is itself array-typed -- one further *non-constant* index
/// into it too (`blockSa[i].z[j]`, two independently dynamic indices
/// combining into one flattened `Row`). Returns `std::nullopt` if \p Ptr
/// is not one of these shapes.
/// (Roadmap H115/H117/H118) Recursively walks the GEP indices from \p It
/// to \p End against \p Ty, mirroring `resolveNestedStageIOField`'s own
/// compile-time-`Residual`-based recursion but over a GEP's own index
/// sequence instead, to support a genuine multi-member nested struct (or
/// array-of-struct) member reached through one or more genuinely
/// non-constant (dynamic) indices -- e.g. `blockSa[gl_InvocationID].z[j]`,
/// the real shape a `dEQP-VK.tessellation.user_defined_io.per_patch_block`
/// tessellation-control shader's own per-invocation array-of-struct member
/// write takes: `gl_InvocationID` selects which `blockSa` instance, and a
/// second, independently loop-carried `j` selects a row within that
/// instance's own `z` member, which is itself a two-element array. Every
/// non-constant index found is appended to \p Terms as an (index,
/// multiplier) pair, where the multiplier is that index's own leaf-
/// specific `RowCount` -- the same quantity `resolveNestedStageIOField`
/// computes to scale an *enclosing* array-of-struct level's own instance
/// index -- so that summing `Terms[i].first * Terms[i].second` (order
/// irrelevant, addition being commutative) over every term yields the
/// final flattened `Row`, without this function itself needing to build
/// any `mul`/`add` IR -- `combineDynamicRowTerms` below does that once, at
/// the one call site that actually needs a materialized `Value*`. \p
/// IDStart accumulates this leaf's own starting `ElementID` offset,
/// exactly like `resolveNestedStageIOField`'s identically-named field.
/// Returns this leaf's own `RowCount` (unused by the top-level caller, but
/// needed by an enclosing recursive call the same way
/// `resolveNestedStageIOField` needs it), or `std::nullopt` if the
/// remaining indices are not a supported shape (a non-constant index into
/// anything but an array, or a constant index into anything but a struct
/// or array).
std::optional<uint32_t> collectDynamicRowTerms(
    Type *Ty, User::op_iterator &It, User::op_iterator End,
    uint32_t &IDStart, SmallVectorImpl<std::pair<Value *, uint64_t>> &Terms) {
  if (It == End)
    return getStageIORowShape(Ty).RowCount;
  Value *Idx = It->get();
  if (auto *CI = dyn_cast<ConstantInt>(Idx)) {
    uint64_t I = CI->getZExtValue();
    ++It;
    if (auto *ST = dyn_cast<StructType>(Ty)) {
      if (I >= ST->getNumElements())
        return std::nullopt;
      for (unsigned J = 0; J != I; ++J)
        IDStart += getStageIOLeafElementCount(ST->getElementType(J));
      return collectDynamicRowTerms(ST->getElementType(I), It, End, IDStart,
                                    Terms);
    }
    if (auto *ArrTy = dyn_cast<ArrayType>(Ty))
      return collectDynamicRowTerms(ArrTy->getElementType(), It, End, IDStart,
                                    Terms);
    return std::nullopt;
  }
  // A non-constant index must directly select a row within an array.
  auto *ArrTy = dyn_cast<ArrayType>(Ty);
  if (!ArrTy)
    return std::nullopt;
  ++It;
  std::optional<uint32_t> InnerRowCount = collectDynamicRowTerms(
      ArrTy->getElementType(), It, End, IDStart, Terms);
  if (!InnerRowCount)
    return std::nullopt;
  Terms.emplace_back(Idx, *InnerRowCount);
  return static_cast<uint32_t>(ArrTy->getNumElements()) * *InnerRowCount;
}

/// Materializes `collectDynamicRowTerms`'s own flat (index, multiplier)
/// list into a single `Value*` via \p B: `Terms[0].first *
/// Terms[0].second + Terms[1].first * Terms[1].second + ...`, skipping
/// the multiply entirely for a unit multiplier (the common single-term
/// case, e.g. plain `gl_ClipDistance[i]`, matching the plain `RowIndex =
/// *IdxIt` this function's own predecessor used before roadmap
/// H115/H117/H118 taught it to combine more than one dynamic index).
Value *combineDynamicRowTerms(
    IRBuilderBase &B, ArrayRef<std::pair<Value *, uint64_t>> Terms) {
  Value *Row = nullptr;
  for (auto &[Idx, Multiplier] : Terms) {
    Value *Term = B.CreateZExtOrTrunc(Idx, B.getInt32Ty());
    if (Multiplier != 1)
      Term = B.CreateMul(Term, B.getInt32(Multiplier));
    Row = Row ? B.CreateAdd(Row, Term) : Term;
  }
  return Row;
}

struct DynamicRowIndexedAccess {
  GlobalVariable *GV;
  /// The index into \p GV's own per-member `ElementIDs` slice (in
  /// `resolveStageIOAccess`'s own `ElementIDs` map) the constant/struct
  /// prefix and any genuine multi-member nested struct member selects --
  /// `0` for a plain, non-block stage-IO global, which only ever has one.
  unsigned Member;
  /// One (index, multiplier) pair per dynamic index found, in whatever
  /// order `collectDynamicRowTerms` happened to append them -- see its
  /// own comment for why summing `Terms[i].first * Terms[i].second` is
  /// order-independent and always yields the correct flattened `Row`.
  SmallVector<std::pair<Value *, uint64_t>, 2> Terms;
};

std::optional<DynamicRowIndexedAccess>
getDynamicRowIndexedAccess(Value *Ptr, const DataLayout &DL) {
  auto *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  if (!GEP)
    return std::nullopt;
  auto *GV = dyn_cast<GlobalVariable>(GEP->getPointerOperand());
  unsigned AddrSpace = 0;
  if (!isSPIRVStageIOGlobal(GV, AddrSpace) ||
      isDynamicIndexedArrayGlobal(GV, AddrSpace))
    return std::nullopt;

  auto IdxIt = GEP->idx_begin();
  auto *OuterIdx = dyn_cast<ConstantInt>(*IdxIt);
  if (!OuterIdx || !OuterIdx->isZero())
    return std::nullopt;
  ++IdxIt;

  uint32_t IDStart = 0;
  SmallVector<std::pair<Value *, uint64_t>, 2> Terms;
  std::optional<uint32_t> RowCount = collectDynamicRowTerms(
      GV->getValueType(), IdxIt, GEP->idx_end(), IDStart, Terms);
  if (!RowCount || Terms.empty())
    return std::nullopt;
  return DynamicRowIndexedAccess{GV, IDStart, std::move(Terms)};
}

/// (Roadmap L47) A task/mesh entry's own bounded payload
/// (`TaskPayloadWorkgroupEXT`, address space 14, see `isTaskPayloadGlobal`)
/// accessed through a genuinely dynamic index into one of its own array
/// members -- e.g. `payload.branch[gl_LocalInvocationIndex]`, the shape a
/// real `dEQP-VK.mesh_shader.ext.query.*.task_mesh.*` CTS case's own
/// per-invocation payload write takes -- rather than the
/// compile-time-constant offset `getStageIOBaseAndOffset` resolves. Mirrors
/// `getDynamicRowIndexedAccess`'s own shape (a `GetElementPtrInst` rooted
/// at the global, first index constant zero, every index up to one final
/// array dimension constant, and exactly that one final index
/// non-constant, which must be the last index of the GEP) but, unlike
/// that function, computes a real dynamic *byte* offset `Value*` directly
/// instead of returning a (global, member, index) tuple: a task payload
/// has no per-member `ElementIDs` slice of its own to select into the way
/// an ordinary stage-IO block does -- `loadTaskPayloadValue`/
/// `storeTaskPayloadValue` address it by plain byte offset alone (see
/// their own comments), so the byte offset is what every caller actually
/// needs. \p B is used to build the (constant-folding, when every operand
/// happens to be constant) multiply/add math this needs -- it is expected
/// to be positioned at the load/store instruction that will consume the
/// result, which the GEP \p Ptr resolves through always dominates.
/// Returns `std::nullopt` if \p Ptr is not this exact shape (not a
/// task-payload global at all, has more than one non-constant index, or
/// that index is not the array-selecting one / not the final index).
std::optional<std::pair<GlobalVariable *, Value *>>
getTaskPayloadDynamicOffsetAccess(IRBuilderBase &B, Value *Ptr,
                                  const DataLayout &DL) {
  auto *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  if (!GEP)
    return std::nullopt;
  auto *GV = dyn_cast<GlobalVariable>(GEP->getPointerOperand());
  if (!isTaskPayloadGlobal(GV))
    return std::nullopt;

  auto IdxIt = GEP->idx_begin();
  auto *OuterIdx = dyn_cast<ConstantInt>(*IdxIt);
  if (!OuterIdx || !OuterIdx->isZero())
    return std::nullopt;

  // The resulting byte offset is always `i32`, matching every other
  // `feme.stage.task.payload.{load,store}` offset operand's own
  // convention (`createStageTaskPayloadLoad`/`Store`'s constant-offset
  // overloads build an `i32` too) -- computing directly in `i32` here,
  // rather than round-tripping through the GEP's own (possibly wider)
  // pointer index type, keeps the emitted `mul`/`add` chain minimal (no
  // otherwise-unnecessary intermediate `zext`/`trunc` obscuring it).
  Type *CurTy = GV->getValueType();
  uint64_t ConstOffset = 0;
  Value *DynamicOffset = nullptr;
  for (++IdxIt; IdxIt != GEP->idx_end(); ++IdxIt) {
    if (auto *CI = dyn_cast<ConstantInt>(*IdxIt)) {
      uint64_t Idx = CI->getZExtValue();
      if (auto *ST = dyn_cast<StructType>(CurTy)) {
        const StructLayout *SL = DL.getStructLayout(ST);
        ConstOffset += SL->getElementOffset(Idx);
        CurTy = ST->getElementType(Idx);
        continue;
      }
      if (auto *ArrTy = dyn_cast<ArrayType>(CurTy)) {
        ConstOffset += Idx * DL.getTypeAllocSize(ArrTy->getElementType());
        CurTy = ArrTy->getElementType();
        continue;
      }
      return std::nullopt;
    }
    // The one non-constant index: must directly select an element within
    // an array, and must be the final index -- a component-level index
    // after it is not modeled (matching `getDynamicRowIndexedAccess`'s own
    // narrowing).
    if (DynamicOffset || !isa<ArrayType>(CurTy) ||
        std::next(IdxIt) != GEP->idx_end())
      return std::nullopt;
    auto *ArrTy = cast<ArrayType>(CurTy);
    uint64_t ElemSize = DL.getTypeAllocSize(ArrTy->getElementType());
    Value *Index = B.CreateZExtOrTrunc(*IdxIt, B.getInt32Ty());
    DynamicOffset = B.CreateMul(Index, B.getInt32(ElemSize));
    CurTy = ArrTy->getElementType();
  }
  if (!DynamicOffset)
    return std::nullopt;
  Value *ByteOffset = DynamicOffset;
  if (ConstOffset)
    ByteOffset = B.CreateAdd(ByteOffset, B.getInt32(ConstOffset));
  return std::make_pair(GV, ByteOffset);
}

/// The stage-IO global \p Ptr addresses, trying
/// `getStageIOBaseAndOffset`'s constant-offset resolution, (roadmap H5b)
/// `getDynamicVertexIndexedAccess`'s dynamic-vertex-indexed one, and
/// (roadmap H7w) `getDynamicRowIndexedAccess`'s dynamic-row-indexed one --
/// every place that only needs to discover *which* global a load/store
/// touches (as opposed to `resolveStageIOAccess`'s full per-instruction
/// resolution) goes through this so no discovery loop below misses a
/// geometry entry's own `gl_in[i]`-shaped access or a
/// `gl_ClipDistance[i]`-shaped one.
GlobalVariable *getStageIOGlobal(Value *Ptr, const DataLayout &DL) {
  if (auto BaseAndOffset = getStageIOBaseAndOffset(Ptr, DL))
    return BaseAndOffset->first;
  if (auto Dyn = getDynamicVertexIndexedAccess(Ptr, DL))
    return Dyn->GV;
  if (auto Dyn = getDynamicRowIndexedAccess(Ptr, DL))
    return Dyn->GV;
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
  // (Roadmap H29r) The mesh primitive-index builtins are per-primitive by
  // definition, whether or not the producing SPIR-V bothered to also
  // decorate them `PerPrimitiveEXT` (glslang does not for these three).
  // Nothing may misread this element as per-vertex: it is not attribute
  // storage at all, but its own flat `FemeMeshArgs::PrimitiveIndices`
  // array, keyed off this system value by `MeshOutputWrapperPass`.
  if (Sys == SignatureSystemValue::PrimitiveIndices)
    Info.Frequency = SignatureFrequency::PerPrimitive;
  if (Stage == ShaderStage::Hull &&
      Phase == SPIRVCanonicalPhase::HullPatchConstant) {
    if (AddrSpace == 7) {
      Info.Direction = SignatureDirection::Input;
      // (roadmap H13b) Every genuine per-control-point-addressable input
      // read here -- an ordinary varying (`Sys == None`), or a builtin
      // that is itself an array indexed by control point (`Position`,
      // `ClipDistance`, `CullDistance`; unlike `Location`-linked ordinary
      // varyings, these carry no `Location` of their own, but are read
      // from `gl_in[]`/`InputPatch` the same way) -- is `FromInputPatch`,
      // addressed by `PatchConstantWrapper.cpp`'s `lowerPatchConstantInput
      // Load` the same way regardless of `Location` vs. `SystemValue`
      // linkage. `OutputControlPointID` is the only system value this
      // phase reads that is *not* addressable this way: it is the
      // single, current-invocation-implicit scalar `lowerPatchConstant
      // SystemValue` special-cases (always `0`), never one this phase
      // could read a *different* control point's own copy of.
      Info.FromInputPatch = Sys != SignatureSystemValue::OutputControlPointID;
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
        Sys == SignatureSystemValue::PatchVertices ||
        // (Roadmap L81) `SV_PrimitiveID`/`gl_PrimitiveID` is a genuine,
        // pipeline-supplied system value -- this patch's own index within
        // the draw -- never data the patch-constant function computed and
        // forwarded. A real DXC/SPIR-V compile decorates it `Patch`
        // (uniform across the whole patch, like a true patch-constant
        // output), which would otherwise satisfy `isPatchOutputDecoration`
        // below and wrongly demand a `PatchOutput`-direction producer from
        // the patch-constant phase that never exists for it (mirroring
        // roadmap L80's identical Hull-stage-input mistake). Recognized
        // here, alongside `DomainLocation`/`PatchVertices`, the two other
        // domain-stage inputs this pass already knows are synthesized
        // rather than forwarded.
        Sys == SignatureSystemValue::PrimitiveID) {
      Info.Direction = SignatureDirection::Input;
      if (Sys == SignatureSystemValue::PatchVertices ||
          Sys == SignatureSystemValue::PrimitiveID)
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

/// One address-space-8 stage-IO store's own patch-vs-vertex frequency, or
/// `std::nullopt` if \p SI's target cannot be resolved as a stage-IO
/// global store at all (see `getStageIOGlobal`'s own comment on the three
/// shapes it resolves). (Roadmap H121) An interface block's own members
/// are checked via `parseSPIRVMemberDecorations` rather than
/// unconditionally treated as vertex-frequency: GLSL only lets the
/// `patch` qualifier apply to a whole block, never to one of its members
/// individually, so every member of a genuine `patch out` block (e.g.
/// this milestone's own `TheBlock`-shaped multi-member interface block)
/// carries an identical `Patch` decoration, and checking any one of them
/// (the first found) reflects the whole block's real frequency. A
/// builtin interface block (e.g. `gl_PerVertex`) has no `Patch`-decorated
/// member of its own either way, so this still falls through to the same
/// vertex-frequency answer the old, unconditional `false` gave it --
/// `gl_TessLevelInner`/`gl_TessLevelOuter` are plain globals, never
/// interface-block members, and so never reach this branch at all.
std::optional<bool>
classifyTessControlOutputStoreFrequency(StoreInst &SI, const DataLayout &DL) {
  GlobalVariable *GV = getStageIOGlobal(SI.getPointerOperand(), DL);
  if (!GV)
    return std::nullopt;
  unsigned AddrSpace = 0;
  if (!isSPIRVStageIOGlobal(GV, AddrSpace) || AddrSpace != 8)
    return std::nullopt;
  if (const MDNode *MemberMD = GV->getMetadata("feme.spirv.MemberDecorations")) {
    for (const auto &KV : parseSPIRVMemberDecorations(MemberMD))
      return isPatchOutputDecoration(KV.second);
    return false;
  }
  ParsedSPIRVDecorations D =
      parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
  return isPatchOutputDecoration(D);
}

/// Whether \p F's own address-space-8 stage-IO stores are patch-frequency
/// (`Patch`-decorated or a tess-factor `BuiltIn`), vertex-frequency
/// (everything else), or both -- the classification
/// `splitBarrierlessTessellationControlEntry` needs to decide both
/// *whether* a no-barrier entry needs splitting at all (`SawPatchOutput`)
/// and *how* (`SawNonPatchOutput`, whether it is purely patch-constant or
/// a mix of the two). (Roadmap H9c) Resolves each store's target via
/// `getStageIOGlobal` -- not `getStageIOBaseAndOffset` alone, which only
/// resolves a *constant*-offset access and so misses a genuine per-vertex
/// output written through a dynamic index
/// (`out_color[gl_InvocationID] = ...`,
/// `gl_out[gl_InvocationID].gl_Position = ...`; every real GLSL
/// tessellation-control shader's own per-vertex write takes this shape) --
/// matching every other stage-IO-global-discovery scan in this file.
struct TessControlOutputFrequencies {
  bool SawPatchOutput = false;
  bool SawNonPatchOutput = false;
};

TessControlOutputFrequencies classifyTessControlOutputs(Function &F) {
  TessControlOutputFrequencies Result;
  const DataLayout &DL = F.getParent()->getDataLayout();
  for (Instruction &I : instructions(F)) {
    auto *SI = dyn_cast<StoreInst>(&I);
    if (!SI)
      continue;
    std::optional<bool> IsPatch =
        classifyTessControlOutputStoreFrequency(*SI, DL);
    if (!IsPatch)
      continue;
    (*IsPatch ? Result.SawPatchOutput : Result.SawNonPatchOutput) = true;
  }
  return Result;
}

/// (Roadmap H9c) Erases every address-space-8 stage-IO store in \p Fn
/// whose own patch-vs-vertex frequency (`classifyTessControlOutputStore
/// Frequency`) does not match \p KeepPatch, so \p Fn -- one of the two
/// per-frequency-pruned clones `splitBarrierlessTessellationControlEntry`
/// produces for a mixed-frequency, no-barrier entry -- ends up producing
/// only the stage-IO writes that belong to its own phase. A store this
/// cannot resolve as a stage-IO global at all is conservatively left
/// alone. The erased store's own address/value computation is left in
/// place as dead code rather than swept up here too: it has no other
/// side effect, and every downstream pass in this pipeline already
/// tolerates ordinary dead code same as any other LLVM IR.
void pruneStageIOStoresByFrequency(Function &Fn, bool KeepPatch) {
  const DataLayout &DL = Fn.getParent()->getDataLayout();
  SmallVector<StoreInst *, 8> ToErase;
  for (Instruction &I : instructions(Fn)) {
    auto *SI = dyn_cast<StoreInst>(&I);
    if (!SI)
      continue;
    std::optional<bool> IsPatch =
        classifyTessControlOutputStoreFrequency(*SI, DL);
    if (IsPatch && *IsPatch != KeepPatch)
      ToErase.push_back(SI);
  }
  for (StoreInst *SI : ToErase)
    SI->eraseFromParent();
}

/// (Roadmap H4f/H9c) `splitTessellationControlEntry`'s own barrier-based
/// split has nothing to split when \p F has no group-sync barrier at all,
/// but `compileAndValidateStages` (GraphicsPipeline.cpp) unconditionally
/// expects a `<entry>.patchconstant` sibling to exist regardless.
/// `classifyTessControlOutputs` distinguishes three shapes: (1) no
/// patch-frequency write at all -- nothing to split out, \p F is left
/// untouched (`HullStageWithNoBarrierIsNotSplit`); (2) every stage-IO
/// write is patch-frequency (`isPatchConstantOnlyEntry`) -- \p F is
/// semantically already "the patch-constant phase", so its whole body is
/// moved into a new `<entry>.patchconstant` clone, and \p F itself is
/// replaced with a trivial, empty control-point phase, having no
/// per-vertex output of its own to produce (roadmap H4f,
/// `NoBarrierPatchConstantOnlyEntryIsSplitWhole`); (3) a genuine mix of
/// patch- and vertex-frequency writes -- only legal (as shapes (1)/(2)
/// above already are too) when no invocation's own patch- or
/// vertex-frequency write ever depends on another's, the only
/// synchronization a group-sync barrier could otherwise provide, which by
/// definition cannot happen here since there is none. Unlike shape (2),
/// simply cloning \p F's whole body into the patch-constant phase and
/// leaving \p F otherwise unpruned is *not* sound for this shape (see the
/// roadmap's own H4f history): `classifySPIRVElement`'s
/// `HullPatchConstant`-phase branch treats any non-`patch`-decorated
/// address-space-8 write as a captured cross-barrier value's read-back
/// (`Direction::Input`, never a store), so an unpruned vertex-frequency
/// store surviving into the clone is misclassified -- exactly roadmap
/// H9c's own defect. Instead, both clones are pruned by
/// `pruneStageIOStoresByFrequency` to keep only the stage-IO writes that
/// belong to their own phase: \p F keeps only its vertex-frequency
/// writes (becoming the real control-point phase, still executed once
/// per control point), and the new clone keeps only the patch-frequency
/// ones (becoming the real patch-constant phase, conceptually executed
/// once per patch -- redundantly recomputing whatever inputs it needs
/// from scratch, same as \p F does for its own, since neither can read
/// the other's own values without a barrier anyway).
bool splitBarrierlessTessellationControlEntry(Function &F,
                                              Function *&PatchConstantPhase) {
  TessControlOutputFrequencies Freq = classifyTessControlOutputs(F);
  if (!Freq.SawPatchOutput)
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

  if (Freq.SawNonPatchOutput) {
    // A genuine mix (shape (3) above): prune each clone down to its own
    // phase's own writes.
    pruneStageIOStoresByFrequency(F, /*KeepPatch=*/false);
    pruneStageIOStoresByFrequency(*PatchConstantPhase, /*KeepPatch=*/true);
    return true;
  }

  // Purely patch-constant (shape (2) above, roadmap H4f): `F` itself
  // becomes the trivial control-point phase, with every one of its
  // stage-IO writes already moved to the patch-constant clone above. It
  // is left with no `!feme.signature` metadata of its own --
  // `feme::cpu::CompiledStage::create` already treats an entirely absent
  // signature identically to an explicitly empty one (roadmap H4g).
  F.deleteBody();
  ReturnInst::Create(F.getContext(),
                     BasicBlock::Create(F.getContext(), "", &F));
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
    BasicBlock *CaptureEntry = BasicBlock::Create(
        F.getContext(), "patchconst.captures", PatchConstantPhase);
    IRBuilder<> CaptureBuilder(CaptureEntry);
    unsigned NextLocation = computeNextSyntheticLocation(*F.getParent());
    for (Instruction *V : Captured) {
      Type *Ty = V->getType();
      unsigned Location = NextLocation++;
      MDNode *Decoration = createLocationDecoration(F.getContext(), Location);
      auto *GV = new GlobalVariable(
          *F.getParent(), Ty, /*isConstant=*/false, GlobalValue::PrivateLinkage,
          UndefValue::get(Ty),
          F.getName() + ".patchconst.capture." + Twine(Location),
          /*InsertBefore=*/nullptr, GlobalValue::NotThreadLocal,
          /*AddressSpace=*/8);
      GV->setMetadata("spirv.Decorations", Decoration);
      // (Roadmap L82) Marks this global as one `lowerPatchConstantInputLoad`
      // must address by this lane's own flat invocation index rather than
      // by whatever `ControlPoint` operand `resolveStageIOAccess` resolves
      // for its (unindexed, GEP-less) read -- see `SignatureElement::
      // CapturedSelfIndex`'s own comment for the full addressing bug this
      // fixes.
      GV->setMetadata("feme.captured.self.index",
                      MDNode::get(F.getContext(), {}));

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

/// The size, in bytes, of one element of a constant-indexed `Output`
/// array, matching the *tightly packed* stride the SPIR-V-to-LLVM
/// conversion actually bakes into that access's own constant byte
/// offset -- NOT `DataLayout::getTypeAllocSize`'s ABI-alignment-padded
/// size, which a genuine SPIR-V/GLSL interface block's own layout rules
/// need not agree with (roadmap H6l). A `{<4 x float>, float, [1 x
/// float], [1 x float]}` `gl_PerVertex`-shaped element, for instance,
/// ends at byte 28 (its own last member's offset plus that member's own
/// size) but *allocates* to 32 -- rounded up to the struct's own 16-byte
/// alignment, driven by its leading `vec4` member -- so array elements
/// after the first are addressed 28 bytes apart, not 32. A `<3 x i32>`
/// (`uvec3`/`ivec3`) element is a narrower instance of the same gap: it
/// allocates to 16 (LLVM pads a 3-wide vector up to its own 4-wide SIMD
/// register size) but is addressed 12 bytes apart, matching `uvec3`'s
/// own tightly packed 3-`i32` size. Originally used only for a mesh
/// entry's own per-vertex/per-primitive array (roadmap H6k) -- but
/// (roadmap H101h) `resolveRowComponent`'s own array-peeling loop just
/// below hits exactly the same gap for an *ordinary* stage-IO member's
/// row shape once that shape's element type is itself a narrow (3-wide,
/// non-power-of-two) vector or an under-aligned struct: GLSL's "array of
/// block instances" syntax (`layout(...) out Block { ivec3 var; }
/// block[3];`) addresses each `block[k]`'s own tightly-packed 12-byte
/// slot, but `getTypeAllocSize` on that same `{<3 x i32>}` element
/// reports 16 -- an prior comment here claimed this row shape "has no
/// equivalent trailing-alignment gap to correct for", which this fixes
/// (`dEQP-VK.transform_feedback.fuzz.instance_array_basic_type.ivec3.*`
/// received a different row's own value instead of its own, since
/// `Residual / RowSize`'s integer division rounded every row index after
/// the first down to a smaller one whenever `RowSize` overstated the
/// real per-row stride).
uint64_t getPackedElementSize(Type *Ty, const DataLayout &DL) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->getNumElements() == 0)
      return 0;
    unsigned Last = ST->getNumElements() - 1;
    return DL.getStructLayout(ST)->getElementOffset(Last) +
           getPackedElementSize(ST->getElementType(Last), DL);
  }
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty))
    return VecTy->getNumElements() *
           getPackedElementSize(VecTy->getElementType(), DL);
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty))
    return ArrTy->getNumElements() *
           getPackedElementSize(ArrTy->getElementType(), DL);
  return DL.getTypeAllocSize(Ty);
}


/// The (row, component) pair `loadStageIOValue`/`storeStageIOValue` need to
/// seed their own recursion with, from \p Residual -- a byte offset within
/// one stage-IO member's own declared type \p MemberTy -- mirroring
/// `getStageIORowShape`'s own type recursion (a single-member struct
/// peeled, then each array dimension in turn, accumulating \p Row as a
/// single flattened, outer-dimension-major index -- \p Row = \p Idx_outer
/// * InnerRowCount + \p Idx_inner for two nested array dimensions, and so
/// on -- exactly the same flattening `getStageIORowShape`'s own
/// `RowCount *= ArrTy->getNumElements()` product already assumes) -- but
/// only as far down as \p ValueTy, the type of the value actually being
/// loaded/stored at this access: once the type remaining to peel equals
/// \p ValueTy, this stops descending and leaves the rest of the
/// decomposition to `loadStageIOValue`/`storeStageIOValue`'s own
/// recursion, which starts from the `(Row, Component)` this returns as
/// its own base and decomposes \p ValueTy's own shape on top of it.
///
/// (Roadmap H101c) Before this, this had no \p ValueTy to stop at, and
/// instead always fully descended to a vector (or scalar) leaf -- correct
/// only when \p ValueTy itself was already that same leaf (every
/// previously-reachable shape: a single scalar/vector member, or one
/// already-selected row of a matrix, e.g. `gl_TessLevelOuter[i]`). Once
/// GLSL's own "array of block instances" syntax (`layout(...) out Block {
/// mat4 var; } block[3];`) let a single whole-matrix store's own \p
/// ValueTy be the matrix's own `[Columns x VectorType]` array (not a
/// leaf) -- one instance's whole `mat4` assigned in one SPIR-V `OpStore`
/// -- descending all the way to a vector leaf here computed a `Row`
/// (`Idx_outer * ColumnCount + Idx_inner`) that already accounted for the
/// matrix's own column index, which `storeStageIOValue`'s own array
/// recursion (decomposing that same `[Columns x VectorType]` \p ValueTy)
/// then multiplied and added a *second* time -- double-counting the
/// column dimension into far-out-of-range absolute rows (observed via
/// `dEQP-VK.transform_feedback.fuzz.instance_array_basic_type.mat4.
/// vertex`'s own `feme-graphics-validate-stage` failure, rows 16-19/32-35
/// instead of the correct 4-7/8-11 for instances 1 and 2). Stopping at \p
/// ValueTy fixes this by leaving exactly the outer (instance) index in
/// \p Row and letting `storeStageIOValue` decompose the matrix's own
/// column dimension exactly once, matching how the geometry variant of
/// this same shape's own capture bytes already showed (columns captured,
/// just to the wrong absolute row) versus the vertex variant's own harder
/// failure (a `Row` so far out of range `PromoteMemToReg`'s
/// `validateRow`-checked signature rejected it outright).
/// (Roadmap H101j) True when \p Declared -- a type reached while peeling a
/// struct member's own *declared* type in `resolveRowComponent`'s loop
/// below -- describes the same shape as \p Actual -- the real value type
/// of the load/store instruction actually being rewritten -- even when
/// they are not the identical `Type *`. `SPIRVToLLVMPatterns.cpp`'s
/// "tight vector" retry (see `getStageIORowShape`'s own comment above)
/// only ever changes a struct member's own *declared* LLVM type to
/// reproduce a tightly-packed offset; it never changes what a SPIR-V
/// `spirv.Load`/`spirv.Store`'s own operand actually converts to
/// elsewhere, since that goes through the ordinary, unmodified
/// `spirv::MatrixType`/`spirv::VectorType` `TypeConverter` registrations.
/// So a whole-matrix or whole-array-of-vectors store's own \p Actual is
/// always the *real* `array<N x vector<M x Scalar>>` shape, even when the
/// member it is stored into declares the *tight*, marker-wrapped
/// `array<N x Marker<array<M x Scalar>>>` stand-in -- this recurses
/// through matching array levels (unwrapping a marker on \p Declared's
/// own side first), treating a declared `array<M x Scalar>` leaf as
/// compatible with an actual `vector<M x Scalar>` leaf.
bool isShapeCompatible(Type *Declared, Type *Actual) {
  if (Type *Inner = getTightVectorMarkerInnerType(Declared))
    Declared = Inner;
  if (Declared == Actual)
    return true;
  auto *DeclArrTy = dyn_cast<ArrayType>(Declared);
  if (!DeclArrTy)
    return false;
  if (auto *ActualArrTy = dyn_cast<ArrayType>(Actual))
    return DeclArrTy->getNumElements() == ActualArrTy->getNumElements() &&
           isShapeCompatible(DeclArrTy->getElementType(),
                            ActualArrTy->getElementType());
  if (auto *ActualVecTy = dyn_cast<FixedVectorType>(Actual))
    return DeclArrTy->getNumElements() == ActualVecTy->getNumElements() &&
           DeclArrTy->getElementType() == ActualVecTy->getElementType();
  return false;
}

std::pair<uint64_t, uint64_t>
resolveRowComponent(Type *MemberTy, uint64_t Residual, Type *ValueTy,
                   const DataLayout &DL) {
  Type *PerRowTy = MemberTy;
  uint64_t Row = 0;
  // (Roadmap H101j) Set once the loop below stops at a tight-vector
  // marker (rather than at an ordinary `isShapeCompatible` match): the
  // Component computation after the loop then needs to index into
  // `PerRowTy`'s own (unwrapped) tight array -- via `getPackedElementSize`
  // -- instead of a `FixedVectorType`'s own per-lane `DataLayout` size,
  // since the marker's own inner shape is a real LLVM array, not a
  // vector.
  bool StoppedAtMarker = false;
  while (true) {
    PerRowTy = peelSingleMemberStruct(PerRowTy);
    if (isShapeCompatible(PerRowTy, ValueTy))
      break;
    // \p ValueTy can never itself be shape-compatible with a marker's own
    // *wrapper* (a real SPIR-V value never converts to this codebase's
    // own synthetic marker struct -- only a struct member's declared type
    // is ever substituted this way), so once `isShapeCompatible` above
    // has already failed to match at this level, a marker here can only
    // mean \p ValueTy needs one further (component-axis) dereference this
    // loop does not itself perform -- e.g. a single scalar-component
    // store into one lane of a matrix column/array-of-vectors element,
    // narrower than even the marker's own row-of-components shape.
    if (Type *Inner = getTightVectorMarkerInnerType(PerRowTy)) {
      PerRowTy = Inner;
      StoppedAtMarker = true;
      break;
    }
    auto *ArrTy = dyn_cast<ArrayType>(PerRowTy);
    if (!ArrTy)
      break;
    uint64_t RowSize = getPackedElementSize(ArrTy->getElementType(), DL);
    uint64_t Idx = 0;
    if (RowSize) {
      Idx = Residual / RowSize;
      Residual -= Idx * RowSize;
    }
    Row = Row * ArrTy->getNumElements() + Idx;
    PerRowTy = ArrTy->getElementType();
  }
  uint64_t Component = 0;
  if (StoppedAtMarker) {
    auto *ArrTy = cast<ArrayType>(PerRowTy);
    uint64_t CompSize = getPackedElementSize(ArrTy->getElementType(), DL);
    if (CompSize)
      Component = Residual / CompSize;
  } else if (!isShapeCompatible(PerRowTy, ValueTy)) {
    if (auto *VecTy = dyn_cast<FixedVectorType>(PerRowTy)) {
      uint64_t CompSize = DL.getTypeAllocSize(VecTy->getElementType());
      if (CompSize)
        Component = Residual / CompSize;
    }
  }
  return {Row, Component};
}

/// (Roadmap H101t, extended by H115) The result of descending from a
/// struct member's own declared type, through zero or more further
/// levels of genuine multi-member nested-struct/array-of-struct nesting
/// (see `isGenuineMultiMemberNestedStruct`), down to the genuine leaf
/// field (a plain scalar/vector/matrix/single-member-wrapper, or a
/// tight-vector marker) a byte offset actually lands in.
struct NestedStageIOField {
  /// This leaf's own starting offset into the enclosing block's own
  /// `IDs`, relative to whatever base index the caller already
  /// accounted for (e.g. `LLVMMember`'s own leading siblings).
  uint32_t IDStart;
  uint64_t Row;
  uint64_t Component;
  /// This leaf's own total `RowCount` accumulated so far (i.e. within
  /// however many array-of-struct levels have already been folded in
  /// below the current recursion level) -- needed by an *enclosing*
  /// array-of-struct level to correctly scale its own instance index
  /// when combining `Row` (see below), since each leaf field can have a
  /// different per-instance `RowCount` of its own (e.g. `blockSa`'s own
  /// `x`/`y` members have `RowCount=1` each per instance, but its `z`
  /// member -- itself a two-element array -- has `RowCount=2`).
  uint32_t RowCount;
};

/// (Roadmap H115) Generalizes the old (roadmap H101t) "genuine
/// multi-member nested struct directly" recursion to also handle that
/// same shape wrapped in one or more outer array dimensions (e.g. `S
/// blockSa[2];`, an *array* of a genuine multi-member nested struct,
/// rather than a lone instance) -- the shape `addStageIOStructMembers`
/// above now also expands, folding each array level's own instance index
/// into the eventual leaf's own `Row` rather than the whole array-of-
/// struct collapsing onto one bogus, opaque row (mixing every leaf's own
/// distinct type and every array instance's own store onto a single
/// shared `ElementID`) the way it did before this milestone.
///
/// For an ordinary array-of-struct level, \p Residual is split into
/// this level's own instance index (\p Residual / one instance's own
/// `DataLayout` size) and the residual *within* that one instance, and
/// the recursive result one level down is combined by folding the
/// instance index into `Row` scaled by that same recursive call's own
/// *leaf-specific* `RowCount` (`Instance * Inner.RowCount + Inner.Row`)
/// -- exactly standard row-major multi-dimensional index flattening.
/// (Using the whole struct's own combined flattened row count here
/// instead of this one leaf's own -- e.g. `getStageIOFlattenedRowCount`
/// applied to the whole `ElemTy` -- would double-count every other
/// sibling leaf's own rows into this one's `Row` value; the leaf-
/// specific `RowCount` returned by the recursive call itself is the only
/// correct scale factor.)
NestedStageIOField resolveNestedStageIOField(Type *Ty, uint64_t Residual,
                                             Type *ValueTy,
                                             const DataLayout &DL) {
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    if (isGenuineMultiMemberNestedStruct(ArrTy->getElementType())) {
      Type *ElemTy = ArrTy->getElementType();
      uint64_t InstanceSize = DL.getTypeAllocSize(ElemTy).getFixedValue();
      uint64_t Instance = InstanceSize ? Residual / InstanceSize : 0;
      uint64_t InnerResidual = Residual - Instance * InstanceSize;
      NestedStageIOField Inner =
          resolveNestedStageIOField(ElemTy, InnerResidual, ValueTy, DL);
      return {Inner.IDStart, Instance * Inner.RowCount + Inner.Row,
              Inner.Component,
              static_cast<uint32_t>(ArrTy->getNumElements()) * Inner.RowCount};
    }
    // An ordinary (non-genuine-multi-member) array member -- not this
    // shape; fall through to the plain-leaf path below, which already
    // handles an ordinary array via `resolveRowComponent`'s own
    // array-peeling loop.
  } else if (auto *ST = dyn_cast<StructType>(Ty)) {
    if (isGenuineMultiMemberNestedStruct(ST)) {
      const StructLayout *SL = DL.getStructLayout(ST);
      unsigned Member = SL->getElementContainingOffset(Residual);
      uint32_t IDStart = 0;
      for (unsigned I = 0; I != Member; ++I)
        IDStart += getStageIOLeafElementCount(ST->getElementType(I));
      uint64_t InnerResidual = Residual - SL->getElementOffset(Member);
      NestedStageIOField Inner = resolveNestedStageIOField(
          ST->getElementType(Member), InnerResidual, ValueTy, DL);
      Inner.IDStart += IDStart;
      return Inner;
    }
  }
  auto [Row, Component] = resolveRowComponent(Ty, Residual, ValueTy, DL);
  return {0, Row, Component, getStageIORowShape(Ty).RowCount};
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
                           const DataLayout &DL, bool IsOutput, Value *Vertex,
                           bool AllowBlockArrayInstanceFold = false) {
  LLVMContext &Ctx = ElemTy->getContext();
  auto AsConstant = [&](uint64_t V) -> Value * {
    return V ? ConstantInt::get(Type::getInt32Ty(Ctx), V) : nullptr;
  };
  if (IDs.size() == 1) {
    if (ValueTy == ElemTy)
      return StageIOAccess{IDs, nullptr, nullptr, Vertex, IsOutput};
    auto [Row, Component] = resolveRowComponent(ElemTy, ByteOffset, ValueTy, DL);
    return StageIOAccess{IDs, AsConstant(Row), AsConstant(Component), Vertex,
                         IsOutput};
  }

  auto *ST = dyn_cast<StructType>(ElemTy);
  uint64_t InstanceResidual = ByteOffset;
  uint64_t BlockInstance = 0;
  if (ST) {
    if (ValueTy == ST)
      return StageIOAccess{IDs, nullptr, nullptr, Vertex, IsOutput};
  } else if (isGenuineMultiMemberNestedStruct(ElemTy)) {
    // (Roadmap L94(i)) `ElemTy` may be a plain array of a genuine
    // multi-member nested struct with *no* block-array-instance folding
    // at all involved: `addElements`' own single-member-`Block` path
    // (`TakeBlockPath` false, since there is only one top-level block
    // member) reaches `addStageIOStructMembers` whenever that one real
    // member is itself such an array (e.g. `struct TestStruct { vec4 a;
    // ivec3 b; }; layout(location=0) out block { TestStruct s[3]; }
    // blk;`'s own `s` member, `dEQP-VK.pipeline.pipeline_library.
    // interface_matching.vector_length.*member_of_array_of_structures_
    // in_block.*`'s own shape), unlike the `Patch`-array-of-whole-block-
    // instances shape the `AllowBlockArrayInstanceFold` branch below
    // models. Each leaf field there already got its own per-instance
    // `RowCount` widened to include this array dimension (roadmap H115's
    // own `addStageIOStructMembers` re-wrapping), so `IDs` here needs no
    // `BlockInstance` splitting first: `resolveNestedStageIOField` (the
    // exact recursion that widening's own doc comment points at) walks
    // `ElemTy`'s array-then-struct levels and this ByteOffset directly
    // into the right leaf `ElementID`/`Row`/`Component` in one call.
    if (ValueTy == ElemTy)
      return StageIOAccess{IDs, nullptr, nullptr, Vertex, IsOutput};
    NestedStageIOField Nested =
        resolveNestedStageIOField(ElemTy, ByteOffset, ValueTy, DL);
    return StageIOAccess{IDs.slice(Nested.IDStart, 1), AsConstant(Nested.Row),
                         AsConstant(Nested.Component), Vertex, IsOutput};
  } else {
    // (Roadmap H117/H118) `ElemTy` may also be an outer *array* of a
    // genuine multi-member `Block`-decorated struct -- glslang's "array
    // of block instances" syntax for a `patch`-qualified block (e.g.
    // `patch out TheBlock {...} tcBlock[2];`), one array dimension
    // further out than the lone (non-arrayed) instance this function
    // handled before this milestone (see the H6c-a-a-iii comment this
    // generalizes). Each instance occupies its own contiguous
    // `DL.getTypeAllocSize(ST)`-sized span; \p ByteOffset is split into
    // this instance's own index (`BlockInstance`) and the byte offset
    // *within* that one instance (`InstanceResidual`), and
    // `BlockInstance` is folded into the eventual leaf's own `Row`
    // below, scaled by that leaf's own per-instance `RowCount` --
    // exactly the same row-major flattening `resolveNestedStageIOField`'s
    // own array-of-struct branch already applies one level deeper (for a
    // block *member's* own array-of-struct field). A whole-array-of-
    // block-instances aggregate access (\p ValueTy naming the whole
    // array) is not modeled: glslang always compile-time unrolls block-
    // array stores into one per-instance member store at a time in
    // practice, so this shape gracefully falls back to `std::nullopt`,
    // exactly like any other unrecognized pointer this function rejects.
    // (Roadmap H117/H118) This array-of-instances flattening must only
    // run when \p AllowBlockArrayInstanceFold's own caller has confirmed
    // `addElements`' own `TakeBlockPath` construction-side logic actually
    // folded this same array dimension into each member's `RowCount`
    // (only a genuinely `patch`-qualified block array does, see that
    // logic's own comment) -- otherwise this access-side `BlockInstance`
    // math silently disagrees with the (unwidened) storage construction
    // actually allocated, producing a `Row` past the end of that
    // storage's own bounds (`dEQP-VK.transform_feedback.fuzz.
    // random_geometry.all_instance_array.12`'s own out-of-bounds
    // `StageStorage` write/heap corruption, exposed when this whole
    // flattening was unconditional). Falling back to `std::nullopt` here
    // for the non-`Patch` case leaves the access unrewritten, exactly
    // matching this shape's own pre-H117/H118 (imperfect, but non-
    // corrupting) behavior.
    if (!AllowBlockArrayInstanceFold)
      return std::nullopt;
    auto *ArrTy = dyn_cast<ArrayType>(ElemTy);
    auto *InnerST = ArrTy ? dyn_cast<StructType>(ArrTy->getElementType())
                          : nullptr;
    if (!InnerST)
      return std::nullopt;
    if (ValueTy == ElemTy)
      return std::nullopt;
    ST = InnerST;
    uint64_t InstanceSize = DL.getTypeAllocSize(ST).getFixedValue();
    BlockInstance = InstanceSize ? ByteOffset / InstanceSize : 0;
    InstanceResidual = InstanceSize ? ByteOffset % InstanceSize : ByteOffset;
  }

  const StructLayout *SL = DL.getStructLayout(ST);
  unsigned LLVMMember = SL->getElementContainingOffset(InstanceResidual);
  // (Roadmap H101m) `IDs` (one per real, SPIR-V-declared member --
  // `addElements`' own `TakeBlockPath` loop, `CanonicalizeStage.cpp`)
  // does not carry an entry for a leading `[N x i8]` pad field
  // `layOutStructIfOffsetsMatch` (SPIRVToLLVMPatterns.cpp) may have
  // prepended to \p ST itself, whenever this block's own first declared
  // member has a nonzero offset -- exactly the same pad
  // `getEffectiveStageIOValueType`'s own comment documents for the
  // single-real-member case, just here on a genuinely multi-member
  // block instead.
  //
  // (Roadmap H101t) Detected directly by \p ST's own field-0 type (a
  // synthetic pad is always `[N x i8]` -- see `structHasLeadingOffsetPad`
  // /SPIRVToLLVMPatterns.cpp) rather than by comparing `ST->
  // getNumElements()` against `IDs.size() + 1`, unlike `addElements`' own
  // construction-side `HasLeadingPad` (still correct there, since it
  // compares against `MemberDecorations.size()`, the real *declared*
  // top-level member count, never affected by this): a genuine
  // multi-member nested-struct member (`isGenuineMultiMemberNestedStruct`)
  // now contributes more than one entry to \p IDs (see
  // `addStageIOStructMembers`), so \p IDs.size() is this block's own
  // *leaf* element count, not its top-level physical field count, and the
  // old `+ 1` comparison no longer reliably detects a pad once any
  // top-level member has this shape.
  bool HasLeadingPad = false;
  if (auto *PadArr = dyn_cast<ArrayType>(ST->getElementType(0)))
    HasLeadingPad = PadArr->getElementType()->isIntegerTy(8);
  assert((!HasLeadingPad || LLVMMember != 0) &&
        "store/load into a struct's own leading pad");
  // (Roadmap H101p) \p IDs is populated in `addElements`' own *physical*
  // (ascending-byte-offset) order, not necessarily each member's own
  // *declared* SPIR-V order, whenever a block's members are declared out
  // of ascending-offset order -- `IDs[k]` (for `k` past any leading pad
  // shift) names whichever member occupies physical LLVM field `k`, not
  // necessarily the member SPIR-V declared at that position.
  //
  // (Roadmap H101t) Each physical field before `LLVMMember` may itself
  // have contributed more than one entry to \p IDs (a genuine
  // multi-member nested-struct field -- see `addStageIOStructMembers`),
  // so the right starting index into \p IDs for `LLVMMember` is the sum
  // of every earlier (non-pad) physical field's own leaf element count,
  // not simply `LLVMMember` (or `LLVMMember - 1`, once a leading pad also
  // shifts everything) as when every field contributed exactly one.
  uint32_t IDStart = 0;
  for (unsigned I = HasLeadingPad ? 1 : 0; I != LLVMMember; ++I)
    IDStart += getStageIOLeafElementCount(ST->getElementType(I));
  Type *FieldTy = ST->getElementType(LLVMMember);
  uint64_t Residual = InstanceResidual - SL->getElementOffset(LLVMMember);
  // (Roadmap H101t, extended by H115) `FieldTy` may itself be a genuine
  // multi-member nested struct (case `.2`'s own shape), or (roadmap
  // H115) an *array* of one (e.g. `S blockSa[2];`): recurse via
  // `resolveNestedStageIOField` into its own layout, sliced to just its
  // own leaf `IDs` range and (for an array-of-struct level) with that
  // level's own instance index folded into the eventual leaf's own
  // `Row`, until a genuine leaf field (a plain scalar/vector/matrix/
  // single-member-wrapper, or a tight-vector marker) is reached.
  NestedStageIOField Nested =
      resolveNestedStageIOField(FieldTy, Residual, ValueTy, DL);
  IDStart += Nested.IDStart;
  // (Roadmap H117/H118) Folds this block-array instance's own index
  // into the eventual leaf's own `Row`, scaled by that leaf's own
  // per-instance `RowCount` -- `BlockInstance` is 0 for the (far more
  // common) non-arrayed-block case, leaving `Nested.Row` unchanged.
  uint64_t Row = BlockInstance * Nested.RowCount + Nested.Row;
  return StageIOAccess{IDs.slice(IDStart, 1), AsConstant(Row),
                       AsConstant(Nested.Component), Vertex, IsOutput};
}

/// (Roadmap H101k) Reproduces `addElements`' own "array of block
/// instances (or a lone block instance) with exactly one real, non-
/// `BuiltIn`-decorated member" recognition -- see the much longer comment
/// on `addElements`' own `XfbBufferArrayStride` computation for the full
/// story -- purely from \p GV's LLVM type and `feme.spirv.
/// MemberDecorations` metadata, to substitute away any leading `[N x i8]`
/// pad `layOutStructIfOffsetsMatch` (SPIRVToLLVMPatterns.cpp) may have
/// synthesized ahead of that one real member, whenever its own declared
/// `Offset` needed a "tight vector" ABI substitution and wasn't already 0.
/// Both `addElements`' own `addElement` call (building this element's
/// `SignatureElement`) and `resolveStageIOAccess`'s own byte-offset
/// resolution (rewriting every actual load/store into it) must agree on
/// the exact same pad-free shape, or the two independently and
/// differently miscount this element's rows -- see
/// `resolveOffsetWithinElement`'s own recursion, which has no way to
/// peel a *2*-member struct the way `peelSingleMemberStruct` peels a
/// genuine 1-member one, and so previously stopped cold on the padded
/// shape, leaving the store's pointer operand unrewritten and the whole
/// global still referenced -- and thus still `external`, still
/// unresolved -- at JIT-link time (the `Symbols not found: [
/// spirv_var_N ]` crash `dEQP-VK.transform_feedback.fuzz.random_geometry.
/// all_instance_array.11` exposed). Returns \p GV's own, unmodified value
/// type for every other global, including a genuinely multi-member block
/// (`TakeBlockPath`'s own concern, entirely unaffected by this).
Type *getEffectiveStageIOValueType(GlobalVariable *GV) {
  Type *ValueTy = GV->getValueType();
  const MDNode *MemberMD = GV->getMetadata("feme.spirv.MemberDecorations");
  if (!MemberMD)
    return ValueTy;
  auto *ArrTy = dyn_cast<ArrayType>(ValueTy);
  auto *PeekedST =
      dyn_cast<StructType>(ArrTy ? ArrTy->getElementType() : ValueTy);
  if (!PeekedST)
    return ValueTy;
  DenseMap<unsigned, ParsedSPIRVDecorations> MemberDecorations =
      parseSPIRVMemberDecorations(MemberMD);
  if (MemberDecorations.size() != 1 ||
      MemberDecorations.lookup(0).BuiltIn.has_value())
    return ValueTy;
  Type *RealMemberTy = PeekedST->getElementType(PeekedST->getNumElements() - 1);
  if (RealMemberTy == PeekedST)
    return ValueTy; // No leading pad -- already the real member's type.
  return ArrTy ? static_cast<Type *>(
                     ArrayType::get(RealMemberTy, ArrTy->getNumElements()))
               : RealMemberTy;
}

/// (Roadmap H101n) Remaps \p ByteOffset -- a byte offset \p Ptr's own
/// access chain actually resolved against \p GV's *real* (possibly
/// leading-pad'd) LLVM type -- into the corresponding byte offset within
/// `getEffectiveStageIOValueType(GV)`'s own, pad-stripped type, so the two
/// stay consistent with each other the same way `getEffectiveStageIOValueType`
/// already keeps the *type* consistent with `addElements`' own identical
/// substitution.
///
/// Needed only for an *array*-of-block-instances shape (\p EffectiveTy is
/// itself an `ArrayType`): each instance's own leading-pad struct is
/// `Gap` bytes wider than its pad-stripped, tightly-packed replacement, so
/// a raw byte offset from the real (wider, padded) global -- e.g. `k *
/// RealStride + Gap` for instance \p k's own real member -- must become
/// `k * PackedStride` before `resolveRowComponent`'s own array-peeling
/// loop (which walks the *pad-stripped* `EffectiveTy`, one `PackedStride`-
/// wide element per instance) can divide it into the right row at all.
/// Before this, that loop instead divided the *real*, still-padded offset
/// by the *packed* per-row size, since `resolveOffsetWithinElement` is
/// only ever handed `getEffectiveStageIOValueType`'s own pad-stripped
/// type, not \p GV's real one -- rounding every instance past the first to
/// a `Row` far outside the element's own `RowCount`, corrupting host
/// memory beyond `StageStorage.cpp`'s own allocated bounds for that
/// element (`dEQP-VK.transform_feedback.fuzz.random_geometry.
/// nested_structs_instance_arrays.44`'s own out-of-bounds
/// `buildStageStorage` write, found via valgrind, followed by case `.45`'s
/// own heap-corruption crash on the very next allocation).
///
/// A *lone* (non-array) leading-pad instance needs no such remapping:
/// `resolveRowComponent`'s own loop, given a scalar/vector \p ElemTy that
/// already matches the load/store's own value type exactly, never
/// descends into `Residual` at all -- see that function's own
/// `isShapeCompatible` check, which fires immediately for a pad-free
/// single-member element -- so \p ByteOffset never needs remapping there;
/// this function is only ever called for the array case, and asserts that
/// \p EffectiveTy is one.
uint64_t remapByteOffsetPastLeadingPad(GlobalVariable *GV, ArrayType *EffectiveTy,
                                       uint64_t ByteOffset,
                                       const DataLayout &DL) {
  auto *RealArrTy = cast<ArrayType>(GV->getValueType());
  auto *RealST = cast<StructType>(RealArrTy->getElementType());
  // `getPackedElementSize`, not `DL.getTypeAllocSize`, for both the real
  // struct's own per-instance stride and \p Gap: a genuine SPIR-V access
  // chain into this array addresses each instance `getPackedElementSize`
  // bytes apart, the same ABI-alignment-ignoring stride
  // `getPackedElementSize`'s own comment documents for a narrow-vector
  // element -- not `getTypeAllocSize`'s rounded-up one, which
  // over-estimates the real gap between instances whenever \p RealST's
  // own alignment exceeds its packed size (e.g. a `{ <3 x i32> }` single-
  // member wrapper, `getTypeAllocSize` 16 but really addressed 12 bytes
  // apart).
  uint64_t Gap = DL.getStructLayout(RealST)->getElementOffset(
      RealST->getNumElements() - 1);
  uint64_t RealStride = getPackedElementSize(RealST, DL);
  uint64_t PackedStride = getPackedElementSize(EffectiveTy->getElementType(), DL);
  if (!RealStride)
    return ByteOffset;
  uint64_t InstanceIdx = ByteOffset / RealStride;
  uint64_t Residual = ByteOffset % RealStride;
  if (Residual < Gap)
    return ByteOffset; // Should not happen for a real, legalized access.
  return InstanceIdx * PackedStride + (Residual - Gap);
}

/// Resolves \p Ptr -- a load/store's pointer operand -- against \p
/// ElementIDs (one entry per stage-IO global, one `ElementID` per struct
/// member for a builtin interface block, a single one for everything
/// else) into a `StageIOAccess`, or `std::nullopt` if \p Ptr does not
/// address a recognized stage-IO global at all. Tries
/// `getDynamicVertexIndexedAccess`'s dynamically-vertex-indexed shape
/// (roadmap H5b) first, falling back to `getStageIOBaseAndOffset`'s
/// ordinary constant-byte-offset one, and (roadmap H7w)
/// `getDynamicRowIndexedAccess`'s dynamically-row-indexed one last -- see
/// `resolveOffsetWithinElement`'s own comment for how each of the first
/// two resolves from there. (Roadmap H5f) The constant-offset fallback
/// itself peels a per-vertex-arrayed `Input` global's own outer array
/// dimension into `Vertex` too, via
/// `isPerVertexArrayInputGlobal`, so a *constant* `gl_in[k]` index is
/// routed the same way a non-constant one already is, rather than folding
/// into `Row` -- deliberately `Input`-only for every stage but `Mesh`
/// (see that helper's own comment on why a *constant* `Output`-array
/// index is not folded the same way there, roadmap H6b). (Roadmap H6k) A
/// mesh entry's own constant-indexed per-vertex/per-primitive `Output`
/// array access -- e.g. a small, compile-time-unrolled per-vertex output
/// loop, which is exactly the shape real `dEQP-VK.mesh_shader.ext.in_out.*`
/// cases compile mesh entries into -- gets the identical treatment via
/// `isPerVertexArrayMeshOutputGlobal`, using \p Stage to keep the fold
/// scoped to mesh alone. \p OutputGlobals (roadmap H2e) is checked to set
/// the result's `IsOutput`, so a caller can tell a genuinely-input load
/// from an `Output`-direction read-back.
std::optional<StageIOAccess> resolveStageIOAccess(
    IRBuilderBase &B, Value *Ptr, Type *ValueTy, const DataLayout &DL,
    const DenseMap<GlobalVariable *, SmallVector<uint32_t, 1>> &ElementIDs,
    const DenseSet<GlobalVariable *> &OutputGlobals, ShaderStage Stage) {
  if (std::optional<DynamicVertexIndexedAccess> Dyn =
          getDynamicVertexIndexedAccess(Ptr, DL)) {
    auto It = ElementIDs.find(Dyn->GV);
    if (It == ElementIDs.end())
      return std::nullopt;
    // (Roadmap H92) A second, genuinely dynamic row index (e.g.
    // `loc[pointIdx].elements[elemIdx]`) has nowhere to go in
    // `resolveOffsetWithinElement`'s own byte-offset-based recursion, so
    // build the `StageIOAccess` directly instead: `Dyn->Member` (roadmap
    // H7w) selects which of `It->second`'s signature elements the row
    // index applies to, so a real per-member builtin interface block
    // (e.g. `gl_in[vertNdx].gl_ClipDistance[i]`, `ClipDistance` being
    // `gl_PerVertex`'s second member, not its only one) resolves here too,
    // not just a plain single-element global.
    if (!Dyn->RowTerms.empty()) {
      // (Roadmap H117/H118) The `collectDynamicRowTerms`-delegated shape
      // (e.g. `blockSa[gl_InvocationID].blockSa[j].x`, a genuine
      // multi-member nested struct's own array dimension indexed
      // dynamically *and* followed by one more constant member-select):
      // `Dyn->Member` here is already the flattened leaf `IDStart`
      // `collectDynamicRowTerms` computed, exactly like
      // `DynamicRowIndexedAccess::Member`'s identically-named field.
      if (Dyn->Member >= It->second.size())
        return std::nullopt;
      Value *Row = combineDynamicRowTerms(B, Dyn->RowTerms);
      return StageIOAccess{ArrayRef(It->second).slice(Dyn->Member, 1), Row,
                           nullptr, Dyn->VertexIndex,
                           OutputGlobals.contains(Dyn->GV)};
    }
    if (Dyn->RowIndex) {
      if (Dyn->Member >= It->second.size())
        return std::nullopt;
      return StageIOAccess{ArrayRef(It->second).slice(Dyn->Member, 1),
                           Dyn->RowIndex, nullptr, Dyn->VertexIndex,
                           OutputGlobals.contains(Dyn->GV)};
    }
    Type *ElemTy = cast<ArrayType>(Dyn->GV->getValueType())->getElementType();
    return resolveOffsetWithinElement(ElemTy, It->second, Dyn->ByteOffset,
                                      ValueTy, DL,
                                      OutputGlobals.contains(Dyn->GV),
                                      Dyn->VertexIndex);
  }

  std::optional<std::pair<GlobalVariable *, uint64_t>> BaseAndOffset =
      getStageIOBaseAndOffset(Ptr, DL);
  if (!BaseAndOffset) {
    // (Roadmap H7w) Neither of the above matched -- try a non-constant
    // index into a stage-IO element's own row dimension instead (e.g.
    // `gl_ClipDistance[i]` with a loop-carried `i`, or, roadmap
    // H115/H117/H118, a genuine multi-member nested struct member reached
    // through one or more such indices, e.g. `blockSa[i].z[j]`). Unlike
    // the two paths above, this one resolves directly to a single leaf
    // element, so it builds its own `StageIOAccess` rather than routing
    // through `resolveOffsetWithinElement`'s byte-offset-based recursion,
    // which has no way to represent a non-constant `Row` mid-recursion.
    if (auto Dyn = getDynamicRowIndexedAccess(Ptr, DL)) {
      auto It = ElementIDs.find(Dyn->GV);
      if (It == ElementIDs.end())
        return std::nullopt;
      if (Dyn->Member >= It->second.size())
        return std::nullopt;
      Value *RowIndex = combineDynamicRowTerms(B, Dyn->Terms);
      return StageIOAccess{ArrayRef(It->second).slice(Dyn->Member, 1),
                           RowIndex, nullptr, nullptr,
                           OutputGlobals.contains(Dyn->GV)};
    }
    return std::nullopt;
  }
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
  // this row). (Roadmap H6k) A mesh entry's own constant-indexed
  // per-vertex/per-primitive `Output` array access takes the identical
  // fold, via `isPerVertexArrayMeshOutputGlobal` -- both a plain
  // user-defined varying array *and* a builtin interface block
  // (`gl_MeshVerticesEXT`/`gl_MeshPrimitivesEXT`, e.g. a real
  // `gl_MeshVerticesEXT[k].gl_Position = ...`, glslang's own
  // compile-time-unrolled shape for a small, fixed `max_vertices`):
  // `resolveOffsetWithinElement` below already knows how to pick the
  // right member's own `ElementID` out of a struct-shaped element type,
  // exactly the same way it does for `getDynamicVertexIndexedAccess`'s own
  // non-constant-index counterpart just above, so there is nothing
  // block-specific left to do here. Without this fold, a
  // compile-time-unrolled per-vertex output store's constant index folds
  // into `Row` instead, past that (now correctly peeled, roadmap H6j)
  // element's own `RowCount == 1`, corrupting host memory beyond its
  // storage's own bounds. Left alone for a whole-global aggregate access
  // (\p ValueTy names the entire array, e.g. copying every vertex's value
  // at once), which has no single vertex to peel out.
  unsigned AddrSpace = 0;
  if ((isPerVertexArrayInputGlobal(GV, AddrSpace, Stage) ||
       isPerVertexArrayMeshOutputGlobal(GV, AddrSpace, Stage)) &&
      ValueTy != GV->getValueType()) {
    auto *ArrTy = cast<ArrayType>(GV->getValueType());
    Type *ElemTy = ArrTy->getElementType();
    uint64_t VertexSize = getPackedElementSize(ElemTy, DL);
    if (VertexSize) {
      uint64_t VertexIdx = ByteOffset / VertexSize;
      uint64_t Residual = ByteOffset % VertexSize;
      Value *Vertex =
          ConstantInt::get(Type::getInt32Ty(GV->getContext()), VertexIdx);
      return resolveOffsetWithinElement(ElemTy, It->second, Residual, ValueTy,
                                        DL, OutputGlobals.contains(GV), Vertex);
    }
  }

  // (Roadmap H101k) `getEffectiveStageIOValueType` -- rather than `GV->
  // getValueType()` directly -- strips away any leading `[N x i8]` pad an
  // array-of-block-instances (or lone-instance) single-real-member
  // global's own LLVM type may have gained from a nonzero-offset "tight
  // vector" member substitution, so this agrees with `addElements`' own
  // identical substitution when it built this global's `SignatureElement`
  // in the first place -- see that helper's own comment for why the two
  // must match. (Roadmap H101n) An array-of-instances shape also needs
  // `ByteOffset` itself remapped past that same pad -- see
  // `remapByteOffsetPastLeadingPad`'s own comment for why.
  Type *EffectiveTy = getEffectiveStageIOValueType(GV);
  if (auto *EffectiveArrTy = dyn_cast<ArrayType>(EffectiveTy);
      EffectiveArrTy && isa<ArrayType>(GV->getValueType()) &&
      EffectiveTy != GV->getValueType())
    ByteOffset =
        remapByteOffsetPastLeadingPad(GV, EffectiveArrTy, ByteOffset, DL);
  // (Roadmap H117/H118) Mirrors `addElements`' own `TakeBlockPath`
  // construction-side check exactly: only a genuinely `patch`-qualified
  // block's own members fold an outer array-of-instances dimension into
  // `RowCount` -- see `resolveOffsetWithinElement`'s own
  // `AllowBlockArrayInstanceFold` parameter comment for why passing this
  // unconditionally corrupted `StageStorage` for a non-`Patch` multi-
  // member XFB "array of block instances".
  bool AllowBlockArrayInstanceFold = false;
  if (const MDNode *MemberMD = GV->getMetadata("feme.spirv.MemberDecorations")) {
    for (const auto &KV : parseSPIRVMemberDecorations(MemberMD)) {
      AllowBlockArrayInstanceFold = KV.second.Patch;
      break;
    }
  }
  return resolveOffsetWithinElement(EffectiveTy, It->second, ByteOffset,
                                    ValueTy, DL, OutputGlobals.contains(GV),
                                    /*Vertex=*/nullptr,
                                    AllowBlockArrayInstanceFold);
}

/// Rewrites \p F's already-legalized `llvm.spv.discard`/`.demote.to.helper.
/// invocation`/derivative/quad-read intrinsic calls into their `feme.
/// stage.*` peers (mirroring `canonicalizeDXILStage`'s handling of the same
/// operations' DXIL-derived forms). Unlike the rest of
/// `canonicalizeSPIRVStage`, this rewrite needs no stage-IO/signature
/// context at all -- it is purely a per-intrinsic-call rewrite -- so it is
/// both a part of that function (for a real stage entry point) and, on its
/// own, safe and correct to run on any function whatsoever, including a
/// helper function with no shader-stage attribute of its own (see
/// `CanonicalizeStagePass::run`'s own comment on why that case matters: a
/// GLSL/glslang-sourced helper function containing a discard reached via a
/// function call, rather than inlined into the entry point, would otherwise
/// never have its `llvm.spv.discard` rewritten at all, since
/// `CanonicalizeStagePass::run`'s own dispatch loop only ever visits a
/// function carrying a recognized `feme::getShaderStage` attribute).
bool rewriteSPIRVDiscardAndDerivativeIntrinsics(Function &F) {
  bool Changed = false;

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

/// (Roadmap H170) Every non-entry, non-declaration function transitively
/// reachable from \p Entry's own direct/indirect calls: a GLSL/glslang-
/// sourced helper function `feme::cpu::InlineHelperFunctionsPass` has not
/// yet inlined away when `canonicalizeSPIRVStage` (below) runs (it runs
/// *before* that pass -- see this pipeline's own ordering comment in
/// `feme::cpu::runPipeline`, Pipeline.cpp), which -- unlike an HLSL/DXIL-
/// sourced entry, always fully inlined by `dxc` well before this pipeline
/// ever sees it -- can itself directly reference a module-scope `Input`/
/// `Output`-storage-class global (GLSL has no notion of passing such a
/// variable "by value" the way `dxc` lowers an HLSL derivative helper's
/// parameter; it simply names the global directly from inside the
/// callee). Without this, `canonicalizeSPIRVStage`'s own single-function
/// walk over \p Entry's instructions never sees such a helper's own direct
/// load/store at all, leaving it a raw, never-converted access to a
/// SPIR-V-derived global with no definition anywhere -- surviving,
/// unconverted, straight through `InlineHelperFunctionsPass`'s later
/// inlining to reach the CPU JIT as a truly external symbol: `JIT session
/// error: Symbols not found: [ spirv_varN ]` at `vkCreateGraphicsPipelines`
/// time (confirmed via `dEQP-VK.glsl.derivate.dfdx.in_function.
/// vec4_highp`, whose helper function reads its `in` varying directly).
/// A depth-first walk of direct call sites, stopping at any recognized
/// entry point (`getShaderStage` -- this milestone assumes a helper is
/// only ever reachable from one real entry per compiled module, the only
/// shape GLSL's own module-scope-global-capturing helpers can take in
/// practice) or a declaration (an external function has no body of its
/// own to scan), and never revisiting the same callee twice (`Seen`
/// guards both correctness for a mutually-recursive-looking call graph a
/// real shader never has and pointless repeat work for a helper called
/// from more than one site).
void collectReachableHelperFunctions(Function &Entry,
                                     SmallVectorImpl<Function *> &Helpers) {
  SmallPtrSet<Function *, 8> Seen;
  SmallVector<Function *, 8> WorkList{&Entry};
  while (!WorkList.empty()) {
    Function *Cur = WorkList.pop_back_val();
    for (Instruction &I : instructions(Cur)) {
      auto *CI = dyn_cast<CallInst>(&I);
      Function *Callee = CI ? CI->getCalledFunction() : nullptr;
      if (!Callee || Callee->isDeclaration() || isShaderEntryPoint(*Callee) ||
          !Seen.insert(Callee).second)
        continue;
      Helpers.push_back(Callee);
      WorkList.push_back(Callee);
    }
  }
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

  // (Roadmap H170) A GLSL/glslang-sourced entry's own helper function(s)
  // -- unlike an HLSL/DXIL-sourced entry, always fully inlined by `dxc`
  // well before this pipeline ever sees it -- can themselves directly
  // reference a module-scope stage-IO global; every function below
  // (`F` plus any such reachable helper) is walked for discovery and,
  // further down, for the actual load/store rewrite, so a helper's own
  // direct access is converted here rather than surviving, raw, into
  // `feme::cpu::InlineHelperFunctionsPass`'s later inlining (see
  // `collectReachableHelperFunctions`'s own comment for the full story).
  SmallVector<Function *, 4> Helpers;
  collectReachableHelperFunctions(F, Helpers);
  SmallVector<Function *, 4> Functions{&F};
  Functions.append(Helpers.begin(), Helpers.end());

  // Discover this entry's stage-IO globals in two passes -- inputs, then
  // outputs -- so their assigned `ElementID`s land in the same
  // inputs-before-outputs order `feme::dxil::convertEntrySignature` already
  // uses (see `collectElementIDsByDirection`'s comment), keeping the two
  // formats' numbering conventions consistent.
  SmallVector<GlobalVariable *> InputGlobals, OutputGlobals;
  DenseSet<GlobalVariable *> Seen;
  const DataLayout &DL = F.getParent()->getDataLayout();
  for (Function *Fn : Functions) {
    for (Instruction &I : instructions(Fn)) {
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
  }

  DenseMap<GlobalVariable *, SmallVector<uint32_t, 1>> ElementIDs;
  // Hoisted out of the `if` below (rather than left a block-local, as
  // every other `Sig` in this file is) so the element-building loop
  // further down can append to it directly.
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
                          bool RowCountIsVertexArray = false,
                          uint32_t XfbBufferArrayStride = 0) {
      SPIRVElementInfo Info = classifySPIRVElement(Stage, Phase, AddrSpace, D);
      SignatureElement Elt;
      Elt.ElementID = NextID;
      Elt.Direction = Info.Direction;
      Elt.Location = D.Location;
      Elt.Index = D.Index;
      if (D.BuiltIn)
        Elt.SystemValue = getSystemValueForBuiltIn(*D.BuiltIn);

      // (Roadmap H21a) `XfbBuffer`'s presence is what makes `XfbOffset`/
      // `XfbStride` meaningful (see `verifySignature`'s own
      // `checkXfbCapture`); a real SPIR-V module always decorates all
      // three together for a captured variable (the Vulkan spec requires
      // it), so defaulting the latter two to 0 when `XfbBuffer` is absent
      // matches every element that captures nothing.
      Elt.XfbBuffer = D.XfbBuffer;
      Elt.XfbOffset = D.XfbOffset.value_or(0);
      Elt.XfbStride = D.XfbStride.value_or(0);
      // (Roadmap H173) A geometry entry point's own `Stream` decoration
      // (absent -- and every non-geometry stage's own element -- means
      // stream 0, `SignatureElement::Stream`'s own documented default).
      Elt.Stream = D.Stream.value_or(0);

      StageIORowShape Shape = getStageIORowShape(ValueTy);
      std::tie(Elt.ComponentType, Elt.BitWidth) =
          getComponentType(Shape.Scalar);
      Elt.FirstComponent = D.Component.value_or(0);
      Elt.ComponentCount = Shape.ComponentCount;
      Elt.RowCount = Shape.RowCount;
      Elt.RowCountIsVertexArray = RowCountIsVertexArray;
      Elt.XfbBufferArrayStride = XfbBufferArrayStride;
      Elt.Interpolation = getInterpolationMode(D);
      Elt.Frequency = Info.Frequency;
      Elt.FromInputPatch = Info.FromInputPatch;
      Elt.CapturedSelfIndex =
          GV->getMetadata("feme.captured.self.index") != nullptr;

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
        const MDNode *MemberMD =
            GV->getMetadata("feme.spirv.MemberDecorations");
        Type *PeekedBlockTy = MemberMD ? GV->getValueType() : nullptr;
        // (Roadmap H117/H118) `BlockArrayCount`, when non-zero, records
        // this outer array's own element count -- glslang's "array of
        // block instances" syntax for a genuinely multi-member block
        // (e.g. `patch out TheBlock {...} tcBlock[2];`, or a plain
        // XFB-captured `out BlockB {...} blockB[3];`, as opposed to a
        // lone, non-arrayed instance) -- so each member's own storage
        // below can be widened to hold every instance's own copy,
        // back-to-back, the same way `addStageIOStructMembers`'s own
        // `ArrayType` branch already widens a *member*'s array-of-struct
        // storage one level further in. Every block-array instance
        // shares the same base `Location`(s) (SPIR-V attaches only one
        // `Location` decoration to the whole variable regardless of this
        // array dimension, exactly like a lone instance would), so
        // `NextMemberLocation`'s own per-member bookkeeping below is
        // deliberately left unmultiplied.
        //
        // A genuine per-vertex/per-primitive dynamically-indexed I/O
        // array (address space 7's `gl_in[]`-shaped `Input` -- never
        // ambiguous with a static "array of block instances", since
        // transform feedback only ever captures an `Output` variable --
        // `isPerVertexArrayMeshOutputGlobal`'s own
        // `gl_MeshVerticesEXT[]`/`gl_MeshPrimitivesEXT[]`-shaped `Output`,
        // or -- this milestone's own gap -- a Hull control-point stage's
        // own per-invocation `Output` block, e.g. `per_vertex_block`,
        // indexed by `gl_InvocationID`) wraps the *same* genuine
        // multi-member struct shape in an outer array too, but that
        // array is the dynamically-indexed `Vertex` operand the big
        // comment above already describes -- never a `BlockArrayCount`
        // to fold into `RowCount`. Folding it here regardless (this
        // check's own original gap) wrongly multiplied every member's
        // storage by the per-vertex array's own size, desynchronizing
        // this stage's own element/row count from the consuming stage's
        // (which never folds a per-vertex array this way at all), and
        // surfaced downstream as `vkQueueSubmit`'s own "producer/consumer
        // element disagree on component/row count" rejection.
        //
        // A Hull `Output` block array is otherwise indistinguishable
        // from this same genuinely dynamic shape by address space and
        // stage alone (both are address space 8, and a genuine
        // `patch`-qualified block-array can occur in a Hull entry too),
        // so it's disambiguated the same way `isDynamicIndexedArrayGlobal`
        // already does: only a genuinely `patch`-qualified block's own
        // members ever carry a `Patch` decoration (GLSL's `patch`
        // qualifier applies to a whole block, so every member carries an
        // identical one) -- a Hull `Output` array without one is the
        // dynamically-indexed per-invocation shape instead.
        //
        // The outer `ArrayType` layer is still peeled off `PeekedBlockTy`
        // in *every* case below, though -- only whether `BlockArrayCount`
        // itself gets set (widening each member's own storage) differs.
        // `TakeBlockPath`'s own per-member decomposition (multiple
        // `ElementID`s, one per real struct member) is needed regardless
        // of whether the array folds into `RowCount` or is left to the
        // dynamically-indexed `Vertex` operand instead: leaving
        // `PeekedBlockTy` as the *array* type (this fix's own original
        // mistake, skipping `TakeBlockPath` entirely for the dynamic
        // case) fell through to the plain (non-block) path instead, which
        // has no notion of a genuine multi-member nested struct and
        // silently collapsed every member onto one shared, wrongly-typed
        // shadow value -- the `isAllocaPromotable`/`PromoteMemToReg`
        // assertion crash `dEQP-VK.tessellation.user_defined_io.
        // per_vertex_block` hit.
        uint32_t BlockArrayCount = 0;
        if (PeekedBlockTy) {
          if (auto *ArrTy = dyn_cast<ArrayType>(PeekedBlockTy)) {
            bool ArrayMembersArePatch = false;
            if (MemberMD) {
              DenseMap<unsigned, ParsedSPIRVDecorations>
                  ProbeMemberDecorations = parseSPIRVMemberDecorations(MemberMD);
              ArrayMembersArePatch = !ProbeMemberDecorations.empty() &&
                                     ProbeMemberDecorations.begin()->second.Patch;
            }
            // (Roadmap H117/H118) Only a genuinely `patch`-qualified
            // multi-member block array (H117's own `per_patch_block_
            // array`, and its Domain-side mirror reading a Hull
            // patch-constant function's own matching `Output`) folds
            // this outer array dimension into `BlockArrayCount`
            // (widening each member's own storage to hold every
            // instance's own copy back-to-back) here. Every other
            // multi-member block array -- a genuine per-vertex/
            // per-primitive dynamically-indexed array (`gl_in[]`-shaped
            // `Input`, a Mesh entry's own per-vertex/per-primitive
            // `Output`, or this milestone's own Hull per-invocation
            // `Output`, e.g. `per_vertex_block`) *and* a genuine,
            // non-`Patch` XFB "array of block instances" (glslang's
            // `layout(xfb_buffer=0, ...) out Block {...} block[3];` for
            // a *multi*-member `Block`, as opposed to the single-member
            // sub-case `XfbBufferArrayStride` already models below) --
            // leaves `BlockArrayCount` at 0 (no fold) here, exactly
            // matching this array dimension's own pre-H117/H118
            // handling (this whole `BlockArrayCount`/`AddBlockElement`
            // mechanism is new to this milestone; only the `Patch` case
            // ever needed it). Folding it for the latter, non-`Patch`
            // multi-member XFB case too (this fix's own first attempt)
            // wrongly conflated `BlockArrayCount`'s "pack every instance's
            // rows back-to-back" semantics with a *separate*, pre-
            // existing, single-member-only mechanism
            // (`XfbBufferArrayStride`, below) that instead routes each
            // instance to its own `XfbBuffer + k` -- the two disagreed on
            // this element's own storage size, silently corrupting
            // `StageStorage`'s heap allocation (`dEQP-VK.
            // transform_feedback.fuzz.random_geometry.all_instance_
            // array.12`'s own "corrupted size vs. prev_size while
            // consolidating" crash, newly exposed by this fix touching
            // this shape for the first time). A genuine multi-member
            // XFB "array of block instances" is therefore left with the
            // same (pre-existing, imperfect) handling it had before this
            // milestone -- fixing it a proper `XfbBufferArrayStride`-like
            // mechanism for the multi-member case is out of scope here.
            //
            // (Roadmap L94(j)) A fourth shape needs the same fold, not
            // covered by any of the three above: an *ordinary*, entirely
            // undecorated-beyond-`Location` genuine multi-member struct
            // array declared directly as a whole stage-IO variable's own
            // type -- no `Block`/`Patch` (not a block at all -- glslang
            // emits this for a plain `layout(location=0) in/out struct {
            // float dummy; vec4 v; } testStructArray[3];`, `dEQP-VK.
            // pipeline.pipeline_library.interface_matching.vector_length.
            // *member_of_array_of_structures.*`'s own non-`_in_block`
            // shape's Fragment/Vertex-side declaration), no `XfbBuffer`,
            // and not a per-vertex/per-primitive dynamically-indexed
            // array either. Every stage-IO global lives in address space
            // 7 (`Input`) or 8 (`Output`) regardless of stage
            // (`isSPIRVStageIOGlobal`'s own contract), so telling this
            // ordinary shape apart from a genuine per-vertex/per-
            // invocation block needs two complementary checks, not one:
            // `isPerVertexArrayInputGlobal`'s/`isPerVertexArrayMeshOutputGlobal`'s/
            // this same `PerInvocationOutputArray` condition (below)'s
            // own `Stage`-based recognition of every *real* per-vertex/
            // per-invocation shape this compiler emits (Hull/Domain/
            // Geometry `Input`, Hull/Mesh `Output`) -- covering even a
            // compile-time-*constant* vertex index into one of those
            // (`FoldsConstantVertexIndexIntoInterfaceBlockArrayMemberVertexOperand`'s
            // own `gl_in[2]`-shaped access, a real, valid GLSL pattern
            // that must still resolve through the `Vertex` operand,
            // never folded into `Row`) -- *and* a scan of `GV`'s own
            // actual accesses for a non-constant outer array index, to
            // also catch a unit test that tags a synthetic per-vertex-
            // shaped global with an unrelated `Stage` purely to exercise
            // this resolution logic in isolation
            // (`ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberLoad`'s
            // own `"vertex"`-tagged `gl_in[]`-shaped global). Before this
            // fix, this ordinary shape's outer array dimension was
            // silently dropped entirely (`BlockArrayCount` left at 0,
            // `AddBlockElement` leaving each leaf's own type unwrapped),
            // undersizing every leaf's own `RowCount` to 1 instead of the
            // real array extent -- surfaced as `ValidateStage.cpp`'s own
            // "row N is out of range for element M" once a later
            // store/load actually indexed a non-zero array element.
            unsigned ProbeAddrSpace = AddrSpace;
            ParsedSPIRVDecorations ProbeD =
                parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
            bool IsPerVertexDynamicShape =
                isPerVertexArrayInputGlobal(GV, ProbeAddrSpace, Stage) ||
                isPerVertexArrayMeshOutputGlobal(GV, ProbeAddrSpace, Stage) ||
                (Stage == ShaderStage::Hull && AddrSpace == 8 && !ProbeD.Patch);
            bool HasDynamicOuterIndex = false;
            for (const User *U : GV->users()) {
              const auto *GEP = dyn_cast<GetElementPtrInst>(U);
              if (!GEP || GEP->getNumIndices() < 2)
                continue;
              if (!isa<ConstantInt>(GEP->getOperand(2))) {
                HasDynamicOuterIndex = true;
                break;
              }
            }
            bool FoldOrdinaryArray = !ArrayMembersArePatch &&
                                     !IsPerVertexDynamicShape &&
                                     !HasDynamicOuterIndex && !ProbeD.XfbBuffer;
            PeekedBlockTy = ArrTy->getElementType();
            if (ArrayMembersArePatch || FoldOrdinaryArray)
              BlockArrayCount = static_cast<uint32_t>(ArrTy->getNumElements());
          }
        }
        auto *PeekedST =
            PeekedBlockTy ? dyn_cast<StructType>(PeekedBlockTy) : nullptr;
        DenseMap<unsigned, ParsedSPIRVDecorations> PeekedMemberDecorations;
        if (PeekedST)
          PeekedMemberDecorations = parseSPIRVMemberDecorations(MemberMD);
        // (Roadmap H101b) A *single*-member struct whose one member has no
        // `BuiltIn` of its own -- whether or not the struct is wrapped in
        // an outer array dimension -- is left to the plain (non-block)
        // path below exactly as before this milestone's own
        // `buildMemberDecorationsAttr` change (which now attaches
        // `feme.spirv.MemberDecorations` unconditionally whenever a
        // struct has any `Offset`, single-member or not). There's nothing
        // to disambiguate between multiple members when there's only one,
        // and taking this branch for one would wrongly treat a genuine
        // *array-of-block-instances* shape (e.g. `layout(...) out BlockB {
        // uvec4 a; } blockB[3];`, GLSL's own syntax for 3 independently-
        // captured XFB streams sharing one interface-block type) as if the
        // array dimension were the same `gl_in[]`/`gl_MeshVerticesEXT[]`-
        // style per-vertex dimension H5b/H6b peel off above -- silently
        // discarding it instead, producing a single, wrongly-shaped 1-row
        // element and (observed via this milestone's own regression sweep)
        // heap corruption downstream in `Executor.cpp`'s XFB capture. This
        // shape isn't otherwise modeled by this milestone; leaving it on
        // the plain path preserves this fix's own pre-existing (if
        // imperfect) behavior for it rather than introducing a new crash.
        // A single-member struct whose one member *is* `BuiltIn`-decorated
        // (e.g. a `gl_PerVertex`-shaped block carrying only `gl_Position`,
        // as
        // `FoldsConstantVertexIndexIntoSingleMemberInterfaceBlockOutputStore`
        // exercises) still needs this branch -- only it maps `SystemValue`
        // from a member's own `BuiltIn`, never the plain path -- so the
        // single-member exclusion above only applies when there's no
        // `BuiltIn` to preserve.
        //
        // (Roadmap H101k) `PeekedST->getNumElements()` counts the
        // *LLVM*-level struct's own fields, which is not always the same
        // as the real, SPIR-V-declared member count: a single real
        // member whose own `Offset` isn't a multiple of its natural ABI
        // alignment (e.g. a `mat3x4` member declared at `xfb_offset =
        // 44`, requiring the H101i/H101j "tight vector" column
        // substitution) gets an LLVM-level leading `[N x i8]` padding
        // field synthesized ahead of it by
        // `convertOffsetStructTypeIgnoringDecorations` purely to
        // reproduce that offset in a real, naturally-laid-out LLVM
        // struct -- a compiler artifact, not a second declared GLSL
        // member. Counting *that* padded field count here wrongly took
        // this branch for a genuinely single-member, array-of-block-
        // instances-shaped block (e.g. `layout(...) out BlockC { mat3x4
        // d; } blockC[2];`), whose real member count is 1: the per-
        // member loop below then walked LLVM field index 0 (the padding)
        // and 1 (the real member) against `MemberDecorations`, itself
        // keyed by the *real* SPIR-V member index (always 0 for a
        // single-member struct) -- so the padding field spuriously
        // picked up the real member's own decorations at index 0 while
        // the real member's own lookup at index 1 found nothing, and
        // neither dead-ends into a store rewrite `resolveStageIOAccess`
        // can recognize, leaving every store to the global unrewritten
        // and the global itself live (and undefined) at JIT-link time --
        // the `Symbols not found: [ spirv_var_N ]` crash `dEQP-VK.
        // transform_feedback.fuzz.random_geometry.all_instance_array.11`
        // exposed. `PeekedMemberDecorations`'s own entry count is the
        // right proxy instead: it's keyed by real SPIR-V member index,
        // and (per H101b's own `buildMemberDecorationsAttr` fix) every
        // real member of an explicitly-offset (`Block`-decorated) struct
        // gets at least its own `Offset` entry, so its size always
        // matches the true declared member count regardless of any
        // LLVM-level padding fields the real members' own types needed.
        bool TakeBlockPath =
            MemberMD && PeekedST &&
            (PeekedMemberDecorations.size() > 1 ||
             PeekedMemberDecorations.lookup(0).BuiltIn.has_value());
        if (TakeBlockPath) {
          StructType *ST = PeekedST;
          DenseMap<unsigned, ParsedSPIRVDecorations> MemberDecorations =
              std::move(PeekedMemberDecorations);
          // (Roadmap H101b) A builtin interface block (e.g.
          // `gl_PerVertex`) carries no whole-variable `Location`/
          // `XfbBuffer`/`XfbOffset`/`XfbStride` of its own -- SPIR-V
          // matches those members by `BuiltIn` instead -- but a plain
          // user-defined XFB-captured block (e.g. `layout(location = 0,
          // xfb_buffer = 0, xfb_offset = 0) out BlockB { vec2 a; ivec2
          // b[3]; } blockB;`) decorates *only* the block variable itself
          // with `Location`/`XfbBuffer`/`XfbOffset`/`XfbStride`; its
          // members carry nothing but their own `Offset` (byte position
          // within the block, reused by `parseSPIRVDecorations` as
          // `XfbOffset` since SPIR-V shares decoration code 35 between
          // the two uses) and are never individually `Location`-
          // decorated at all. Every such member's real `Location` is the
          // block's own base `Location` plus however many locations the
          // *preceding* members already consumed (`getStageIORowShape`'s
          // own `RowCount`, one location per row, mirroring GLSL's own
          // sequential interface-location assignment for block members),
          // and its real `XfbOffset` is the block's own base `XfbOffset`
          // plus its own relative `Offset` -- both computed below only as
          // a fallback when the member has no explicit decoration of its
          // own, so a real per-member-decorated block (e.g. a mesh
          // entry's `PerVertexEXT`/`PerPrimitiveEXT` output block, which
          // SPIR-V does decorate per member with `Location`) is
          // unaffected. A genuinely `BuiltIn`-decorated member (e.g.
          // `gl_PerVertex`'s own `gl_Position`/`gl_PointSize`/...) is
          // matched downstream by `SystemValue`, never `Location`, and
          // never carries a real `Offset`/`XfbBuffer`/... of its own in
          // practice -- skip all of this synthesis for it, so this
          // milestone leaves every existing builtin-block element exactly
          // as before (an unset `Location`/`XfbBuffer`/`XfbOffset`/
          // `XfbStride`), rather than fabricating a `Location` that could
          // collide with a real, separately-declared user-defined
          // variable's own `Location` 0.
          ParsedSPIRVDecorations WholeVarD =
              parseSPIRVDecorations(GV->getMetadata("spirv.Decorations"));
          uint32_t NextMemberLocation = WholeVarD.Location.value_or(0);
          // (Roadmap H101m) `structHasLeadingOffsetPad`
          // (SPIRVToLLVMPatterns.cpp) synthesizes its leading `[N x i8]`
          // pad whenever a struct's first *declared* member has a nonzero
          // offset -- entirely independent of how many real members the
          // struct has. `getEffectiveStageIOValueType`'s own comment
          // documents this same pad for the single-real-member case (left
          // to the plain path below, never `TakeBlockPath`); a genuinely
          // multi-member block (e.g. `layout(location = 4, xfb_buffer =
          // 0, xfb_offset = 28) out BlockC { uvec3 c; uint d; }
          // blockC;`, whose `xfb_offset` is likewise encoded as member
          // 0's own nonzero `Offset` decoration rather than repeated on
          // the whole variable) can have the very same pad, and
          // `TakeBlockPath` was never taught to expect it: `ST->
          // getNumElements()` (the *LLVM* struct's own field count) then
          // exceeds `MemberDecorations.size()` (the real, SPIR-V-declared
          // member count) by exactly one, and walking every LLVM field as
          // if it were its own same-numbered real member wrongly turned
          // the pad itself into a spurious element (an 8-bit-scalar
          // array `StageStorage.cpp` rejects outright) while shifting
          // every real member's own decorations one index off from its
          // real LLVM type. `HasLeadingPad` recovers the same shift
          // `OffsetStructLeadingPadAccessChainPattern` already applied to
          // every access chain into this same global (SPIRVToLLVMPatterns
          // .cpp) -- skip LLVM field 0 entirely here, and read real
          // member `I`'s own decorations/location bookkeeping from LLVM
          // field `I + 1`.
          bool HasLeadingPad =
              ST->getNumElements() == MemberDecorations.size() + 1;
          // (Roadmap H101p) `layOutStructIfOffsetsMatch`
          // (SPIRVToLLVMPatterns.cpp) now lays a struct's members out in
          // *physical* (ascending byte-offset) order, which need not
          // match their own *declared* SPIR-V member order once a block's
          // members are declared out of ascending-offset order (GLSL's
          // own `all_unordered_and_instance_array` fuzz-test family
          // deliberately emits exactly this shape). `MemberDecorations`
          // is keyed by declared member index, and its own `XfbOffset`
          // (parsed from that member's `Offset` decoration) directly
          // gives its true declared byte offset, so
          // `DL.getStructLayout(ST)->getElementContainingOffset` finds
          // its real physical LLVM field without needing to replicate
          // MLIR's own permutation logic at all -- this sidesteps
          // declared-order assumptions entirely, unlike the (still
          // correct, for the no-reordering case) `HasLeadingPad ? I + 1
          // : I` positional fallback below, which this generalizes rather
          // than replaces (kept for `BuiltIn` members, which carry no
          // `Offset` decoration in practice, so have no offset to look
          // up).
          //
          // `addElement`'s own call order populates `ElementIDs[GV]`
          // (read back by `resolveOffsetWithinElement`'s own positional
          // `IDs.slice(Member, 1)`), so this loop is split into two
          // passes: pass 1 (declared order) computes each member's
          // decorations/type/physical-index -- required, since GLSL's
          // sequential `Location` assignment for undecorated block
          // members counts *preceding declared* members' rows, not
          // preceding *physical* ones -- and pass 2 (sorted by physical
          // index) actually calls `addElement`, so `ElementIDs[GV]` ends
          // up indexed by physical struct-field position, matching what
          // `resolveOffsetWithinElement`'s own positional slice assumes.
          // This reordering is a no-op (byte-for-byte identical call
          // order) for every already-ascending-offset-declared shape.
          struct PendingMember {
            ParsedSPIRVDecorations D;
            Type *Ty;
            unsigned PhysicalIndex;
          };
          const DataLayout &DL = GV->getDataLayout();
          SmallVector<PendingMember, 8> Pending;
          Pending.reserve(MemberDecorations.size());
          for (unsigned I = 0, E = MemberDecorations.size(); I != E; ++I) {
            ParsedSPIRVDecorations MemberD = MemberDecorations.lookup(I);
            unsigned PhysicalIndex = HasLeadingPad ? I + 1 : I;
            if (MemberD.XfbOffset && !MemberD.BuiltIn)
              PhysicalIndex =
                  DL.getStructLayout(ST)->getElementContainingOffset(
                      *MemberD.XfbOffset);
            Type *MemberTy = ST->getElementType(PhysicalIndex);
            // (Roadmap H101t) A genuine multi-member nested struct (see
            // `isGenuineMultiMemberNestedStruct`) expands to more than
            // one `Location`-consuming `SignatureElement` below --
            // `getStageIOFlattenedRowCount` sums each of its own real
            // members' own row counts instead of `getStageIORowShape`'s
            // single-element answer of 1, so a later block member's own
            // `Location` still starts where this member's own leaves
            // actually end.
            uint32_t RowCount = getStageIOFlattenedRowCount(MemberTy);
            if (!MemberD.BuiltIn) {
              if (!MemberD.Location)
                MemberD.Location = NextMemberLocation;
              if (!MemberD.XfbBuffer)
                MemberD.XfbBuffer = WholeVarD.XfbBuffer;
              MemberD.XfbOffset = WholeVarD.XfbOffset.value_or(0) +
                                  MemberD.XfbOffset.value_or(0);
              if (!MemberD.XfbStride)
                MemberD.XfbStride = WholeVarD.XfbStride;
            }
            Pending.push_back({MemberD, MemberTy, PhysicalIndex});
            NextMemberLocation += RowCount;
          }
          llvm::stable_sort(Pending, [](const PendingMember &A,
                                        const PendingMember &B) {
            return A.PhysicalIndex < B.PhysicalIndex;
          });
          for (const PendingMember &PM : Pending) {
            // (Roadmap H101t) A genuine multi-member nested struct
            // member (e.g. `all_unordered_and_instance_array.2`'s own
            // `!spirv.struct<(mat3x3, vector<4xsi32>)>`) cannot become
            // one `SignatureElement` -- its own real members can have
            // distinct scalar types `SignatureElement`'s single
            // `ComponentType`/`BitWidth` pair has no room for -- so
            // expand it into one `addElement` call per leaf field
            // instead, mirroring this same loop's own top-level
            // per-member decomposition one level deeper.
            // (Roadmap H117/H118) When the whole block itself is
            // further wrapped in an outer array (`BlockArrayCount > 1`,
            // e.g. `patch out TheBlock {...} tcBlock[2];`), each leaf's
            // own type is re-wrapped in that same outer array dimension
            // before reaching `addElement`, so its own
            // `getStageIORowShape` call folds this array level into the
            // leaf's `RowCount` -- widening its storage to hold every
            // block-array instance's own copy back-to-back -- exactly
            // mirroring `addStageIOStructMembers`'s own `ArrayType`
            // branch one level further in (for a *member's* own array-
            // of-struct field, e.g. `S blockSa[2]`).
            auto AddBlockElement = [&](GlobalVariable *EltGV,
                                       unsigned EltAddrSpace,
                                       const ParsedSPIRVDecorations &EltD,
                                       Type *EltTy) {
              Type *WrappedTy =
                  BlockArrayCount > 1
                      ? ArrayType::get(EltTy, BlockArrayCount)
                      : EltTy;
              addElement(EltGV, EltAddrSpace, EltD, WrappedTy);
            };
            if (isGenuineMultiMemberNestedStruct(PM.Ty)) {
              uint32_t NestedLocation = PM.D.Location.value_or(0);
              addStageIOStructMembers(AddBlockElement, GV, AddrSpace, PM.D,
                                      PM.Ty, DL, NestedLocation);
              continue;
            }
            AddBlockElement(GV, AddrSpace, PM.D, PM.Ty);
          }
          continue;
        }
        // (Roadmap H5f/L94(h)) A plain (non-block) per-vertex-arrayed
        // `Input` global's outer per-vertex array dimension is peeled off
        // (see this function's own `RowCountIsVertexArray` peeling, below)
        // before `getStageIORowShape` computes this element's `RowCount`:
        // `Elt.RowCountIsVertexArray` marks that this element's real
        // per-vertex value is addressed separately, through the `Vertex`
        // operand (`resolveOffsetWithinElement`'s own threading, whether
        // constant-folded the way H5b originally read or genuinely
        // dynamic), rather than folded into `RowCount` as one more row.
        // `RowCount` here is therefore always this element's own real
        // (per-vertex) shape, matching what a cross-stage link
        // (`StageLink.cpp`'s `linkStageElements`) compares it against --
        // unlike H5b's original design, where a genuinely arrayed
        // per-vertex varying's real array extent got silently folded
        // together with the per-vertex dimension into one combined
        // `RowCount`, only correct by coincidence for an unarrayed one.
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
        // (Roadmap H101l) A single-real-member `Block`-decorated struct
        // (`PeekedST`/`MemberMD`, computed above, before `TakeBlockPath`
        // was checked false) whose one member's own `xfb_offset` is
        // nonzero has that offset encoded *only* as the member's own
        // `Offset` decoration (SPIR-V decoration code 35, reused by
        // `parseSPIRVDecorations` as `XfbOffset` for either use) --
        // glslang never repeats it as a second, whole-variable-level
        // `Offset`/`XfbOffset` decoration of its own for this shape,
        // unlike `XfbBuffer`/`XfbStride`/`Location`, which it always
        // attaches directly to the variable regardless of member count.
        // `D.XfbOffset` above, sourced only from the whole-variable's own
        // decorations, therefore silently defaulted to 0 for any such
        // block whose real `xfb_offset` isn't 0 (e.g. `layout(location =
        // 4, xfb_buffer = 0, xfb_offset = 44, xfb_stride = 92) out BlockC
        // { mat3x4 d; } blockC[2];`) -- wrongly capturing this element at
        // byte 0 of its buffer instead of byte 44, silently overwriting
        // whatever a *different*, genuinely offset-0 element sharing the
        // same `XfbBuffer` (here, `BlockB`, this shader's own sibling
        // block) had already captured there. Folding in
        // `PeekedMemberDecorations.lookup(0).XfbOffset` -- present
        // whenever this member has its own `Offset` decoration, which
        // roadmap H101b's own `buildMemberDecorationsAttr` fix guarantees
        // for any real member of an explicitly-offset struct -- exactly
        // mirrors `TakeBlockPath`'s own analogous per-member `XfbOffset`
        // synthesis just above, applied here to this element as a whole
        // rather than to one of several sibling members (there being only
        // one). Left untouched (the `!D.XfbOffset` guard) whenever the
        // whole variable already carries its own explicit `Offset`
        // decoration, so a shape that *does* redundantly repeat it there
        // is never double-counted.
        if (PeekedST && MemberMD && !D.XfbOffset)
          D.XfbOffset = PeekedMemberDecorations.lookup(0).XfbOffset;
        Type *ValueTy = GV->getValueType();
        bool RowCountIsVertexArray =
            isPerVertexArrayInputGlobal(GV, UnusedAddrSpace, Stage);
        // (Roadmap L94(h)) Peel exactly the outer per-vertex array
        // dimension off \p ValueTy here, before `getStageIORowShape`
        // (inside `addElement`, below) folds every remaining array
        // dimension into `RowCount`: that outer dimension is already
        // addressed separately, as `feme.stage.input.load`/`.store`'s own
        // `Vertex` operand (`resolveOffsetWithinElement`'s
        // `getDynamicVertexIndexedAccess` path, roadmap H5b) and
        // `StageStorage::readRaw`/`writeRaw`'s own `Invocation` parameter,
        // not as one more `Row`. Before this peel, an ordinary per-vertex
        // scalar/vector/matrix varying's `RowCount` still came out
        // correct by coincidence (`StageLink.cpp`'s `effectiveRowCount`
        // simply flattened any per-vertex-arrayed consumer down to `1`,
        // which happened to already equal that varying's own real row
        // count), but a genuinely arrayed one -- e.g. `layout(location=1)
        // in float looseVar[3];`, one of `dEQP-VK.pipeline.
        // pipeline_library.interface_matching.shader_layout_component_
        // matching.*.multiple_locations.*`'s own shapes -- got its real
        // 3-row shape folded together with the outer per-vertex dimension
        // into a bogus combined `RowCount` (e.g. 32 (patch control
        // points) * 3 = 96), which then disagreed with its producer's own
        // genuine, unfolded `RowCount` (3) at `vkQueueSubmit` time
        // ("disagree on component/row count or type").
        if (RowCountIsVertexArray)
          if (auto *ArrTy = dyn_cast<ArrayType>(ValueTy))
            ValueTy = ArrTy->getElementType();
        // (Roadmap H101g) `PeekedST`/`MemberMD` (computed above, before
        // `TakeBlockPath` was checked false) being non-null means `GV`'s
        // whole value type is a single-member, non-`BuiltIn` `Block`-
        // decorated struct -- optionally wrapped in one outer array
        // dimension, already peeled off into `PeekedST` above. When that
        // outer array dimension is genuinely present (`ValueTy` really is
        // `ArrayType` of `PeekedST`, as opposed to `PeekedST` itself with
        // no array at all) and this element is transform-feedback-
        // captured (`D.XfbBuffer` present), that array dimension is GLSL's
        // own "array of block instances" syntax (`layout(...) out BlockB
        // { ... } blockB[N];`): each `blockB[k]` is an independently-
        // captured stream, one per `XfbBuffer + k` (the GLSL/SPIR-V
        // transform-feedback spec's own model for this shape), not
        // `RowCount`-many rows packed back-to-back within one buffer the
        // way a real matrix or a block member's own array member already
        // is. `XfbBufferArrayStride` -- the block's one member's own row
        // count (`getStageIORowShape` on `PeekedST`'s own single member,
        // ignoring the outer array `PeekedST` itself already had peeled
        // off; 1 for a plain scalar/vector member, or a real matrix's own
        // row count, e.g. 4 for `mat4`) -- records this so `Executor.cpp`'s
        // `captureTransformFeedback` can recover each instance's own
        // `(Instance, InnerRow)` split and route it to its own buffer
        // instead of packing every row into one -- see that field's own
        // comment (`Signature.h`) for the full story, including the
        // `dEQP-VK.transform_feedback.fuzz.random_geometry.
        // all_unordered_and_instance_array.28` and `dEQP-VK.
        // transform_feedback.fuzz.instance_array_basic_type.mat4.*` cases
        // that exposed this.
        // (Roadmap H101k) `PeekedMemberDecorations.size() == 1` (rather
        // than `PeekedST->getNumElements() == 1`) is the correct "one
        // real declared member" test here for the same reason it is for
        // `TakeBlockPath` above: `PeekedST`'s own LLVM field count grows
        // to 2 (a leading `[N x i8]` pad plus the real member) whenever
        // that one real member's own `Offset` needed a "tight vector"
        // ABI substitution and isn't naturally aligned to 0 -- e.g.
        // `layout(..., xfb_offset = 44) out BlockC { mat3x4 d; }
        // blockC[2];` -- without ever gaining a second genuine GLSL
        // member. `PeekedST->getElementType(PeekedST->getNumElements() -
        // 1)` (the *last* field, rather than always field 0) recovers
        // that one real member's own type either way: with no leading
        // pad, it is (and always was) the struct's only field; with one,
        // `layOutStructIfOffsetsMatch`'s own comment (SPIRVToLLVMPatterns.
        // cpp) guarantees it is prepended, never appended, so the real
        // member is always the last (and, but for the pad, only) field.
        uint32_t XfbBufferArrayStride = 0;
        if (PeekedST && PeekedMemberDecorations.size() == 1 && MemberMD &&
            D.XfbBuffer && !RowCountIsVertexArray) {
          if (auto *ArrTy = dyn_cast<ArrayType>(ValueTy))
            if (ArrTy->getElementType() == PeekedST) {
              Type *RealMemberTy =
                  PeekedST->getElementType(PeekedST->getNumElements() - 1);
              XfbBufferArrayStride = getStageIORowShape(RealMemberTy).RowCount;
              // (Roadmap H101k) `getStageIORowShape`/`peelSingleMemberStruct`
              // below (via the `addElement` call closing this block) only
              // know how to peel through a genuine single-member
              // `StructType`; `PeekedST` itself no longer looks like one
              // once a leading `[N x i8]` pad field is present (2 elements
              // total, not 1), so passing `ValueTy` -- the *whole*,
              // still-padded `[NumInstances x PeekedST]` -- through
              // unchanged left the real member's own row/component shape
              // unrecognized (a bogus, unrepresentable "scalar" leaf,
              // exactly the under-sizing `peelSingleMemberStruct`'s own
              // comment already describes for a related shape), silently
              // corrupting `StageStorage`'s allocation for this element.
              // Rebuilding `ValueTy` here as a pad-free `[NumInstances x
              // RealMemberTy]` -- bit-for-bit the same shape this branch
              // already produced before any leading pad was possible --
              // sidesteps that without teaching the lower-level, widely-
              // shared `peelSingleMemberStruct` helper a new, narrowly-
              // scoped padding convention it has no other reason to know
              // about.
              if (RealMemberTy != PeekedST)
                ValueTy = ArrayType::get(RealMemberTy, ArrTy->getNumElements());
            }
        }
        //
        // (Roadmap H29g) A hull entry's own plain per-control-point
        // `Output` global (e.g. `layout(location=0) out vec4 vtxColor[];`
        // against `layout(vertices = N) out;`) is the same shape for the
        // same reason: its outer array dimension is the output patch's own
        // control point count, not a matrix row count, and the domain
        // stage links against it by `Location` expecting the single
        // control point each of its own inputs describes.
        //
        // (Roadmap L24(b)) `D.Patch` excludes a genuinely patch-frequency
        // output -- above all `gl_TessLevelOuter`/`gl_TessLevelInner`
        // (`BuiltIn TessLevelOuter`/`TessLevelInner`, always `Patch`-
        // decorated, always a `[4 x float]` global regardless of tess
        // domain) -- from this same peeling. Those two are not shaped
        // like a per-control-point array at all: every one of their (up
        // to four) rows is a genuine, independently meaningful tess
        // factor for the whole patch, and `extractTessFactors` reads them
        // back by `Row` (`feme::graphics::TessFactors::Edges`/`Inside`).
        // Before this fix, an isoline patch constant function's second
        // `gl_TessLevelOuter[1]` store (`SV_TessFactor[1]`, the per-line
        // detail/segment-count factor) was silently unreadable: this
        // peeling collapsed the whole 4-element builtin down to
        // `RowCount == 1`, so `extractTessFactors`'s `Row != Elt.RowCount`
        // loop bound only ever visited `Row == 0`, leaving
        // `TessFactors::Edges[1]` at its untouched default of `1.0`
        // (one segment) instead of the real authored value -- exactly
        // the "masked"-looking gap `Graphics/IsolineDomainTessellation.
        // test` (Roadmap L24(b)) surfaced.
        bool PerInvocationOutputArray =
            (Stage == ShaderStage::Mesh || Stage == ShaderStage::Hull) &&
            AddrSpace == 8 && !D.Patch;
        if (PerInvocationOutputArray) {
          if (auto *ArrTy = dyn_cast<ArrayType>(ValueTy))
            ValueTy = ArrTy->getElementType();
          RowCountIsVertexArray = false;
        }
        // (Roadmap L94(i)) A single-member `Block` (`PeekedST`/`MemberMD`
        // above, `TakeBlockPath` false since there is only one top-level
        // member) whose one real member is itself a genuine multi-member
        // nested struct -- optionally wrapped in one or more array
        // dimensions, e.g. `struct TestStruct { vec4 a; ivec3 b; };
        // layout(location=0) out block { TestStruct s[3]; } blk;`'s own
        // `structArrayInBlock` member (`dEQP-VK.pipeline.pipeline_
        // library.interface_matching.vector_length.*member_of_array_of_
        // structures_in_block.*`'s own shape) -- cannot become one
        // `SignatureElement` any more than `TakeBlockPath`'s own
        // multi-member case can: `TestStruct`'s two real fields have
        // distinct scalar types no single `ComponentType`/`ComponentCount`
        // pair can hold. Left on the plain `addElement` path below (this
        // gap's own original bug), `getStageIORowShape`'s `peelSingleMember
        // Struct`/`ArrayType` peeling ran out of both single-member
        // wrappers and array levels once it reached `TestStruct` itself (2
        // real members, not 1), so it silently treated the whole struct as
        // an opaque scalar `ComponentCount=1` leaf -- one`SignatureElement`
        // undersized relative to what the compiled stores into it actually
        // write, surfaced as `ValidateStage.cpp`'s own "component N is out
        // of range" (or, once past that, `StageStorage`-level corruption).
        // `peelSingleMemberStruct` recovers the same real content
        // `PeekedST`/`PeekedBlockTy` above already peeled through single-
        // member wrapping to find (re-derived here rather than reusing
        // `PeekedBlockTy` directly, since that variable is only set when
        // `MemberMD` names a `Block`-decorated struct at all, whereas this
        // check must apply to a plain, non-`Block` single-member wrapper
        // too); `addStageIOStructMembers` (this same milestone's own
        // pre-existing per-member decomposition, already used by
        // `TakeBlockPath`'s per-member loop above) is reused unchanged: it
        // already understands the array-of-struct wrapping this shape's
        // own outer `[3 x TestStruct]` dimension needs (Roadmap H115).
        //
        // (Roadmap L94(j)) This decomposition also applies when
        // `RowCountIsVertexArray` is set (e.g. a Domain-stage `Input`
        // reading a Hull-stage per-control-point `Output` array of this
        // same genuine multi-member struct shape,
        // `dEQP-VK.pipeline.pipeline_library.interface_matching.
        // vector_length.out_vec4_in_vec4_member_of_array_of_structures_
        // vert_tesc_out_tese_in_frag`'s own consuming side) -- excluding
        // it here (this guard's own original condition) instead fell
        // through to the single opaque `addElement` call below,
        // undersizing/mistyping this element the same way excluding
        // `XfbBufferArrayStride != 0` does. `addStageIOStructMembers`'s
        // own generic `AddElement` callback signature has no room for
        // either flag, silently defaulting every leaf it adds to
        // `RowCountIsVertexArray=false`/`XfbBufferArrayStride=0` -- wrong
        // for this element's own leaves, which must each inherit this
        // whole element's own values for both (the two are mutually
        // exclusive by construction, so at most one is ever non-default
        // for a given element). `AddDecomposedElement` below forwards
        // both through to every leaf `addElement` call in place of the
        // bare `addElement` reference `TakeBlockPath`'s own call above
        // gets away with passing directly (its own leaves are never
        // vertex-array/XFB-array elements to begin with).
        Type *PeeledContent = peelSingleMemberStruct(ValueTy);
        if (XfbBufferArrayStride == 0 &&
            isGenuineMultiMemberNestedStruct(PeeledContent)) {
          uint32_t NestedLocation = D.Location.value_or(0);
          auto AddDecomposedElement =
              [&](GlobalVariable *EltGV, unsigned EltAddrSpace,
                  const ParsedSPIRVDecorations &EltD, Type *EltTy) {
                addElement(EltGV, EltAddrSpace, EltD, EltTy,
                           /*RowCountIsVertexArray=*/RowCountIsVertexArray,
                           /*XfbBufferArrayStride=*/0);
              };
          addStageIOStructMembers(AddDecomposedElement, GV, AddrSpace, D,
                                  PeeledContent, GV->getDataLayout(),
                                  NestedLocation);
        } else {
          addElement(GV, AddrSpace, D, ValueTy,
                     /*RowCountIsVertexArray=*/RowCountIsVertexArray,
                     /*XfbBufferArrayStride=*/XfbBufferArrayStride);
        }
      }
    };
    addElements(InputGlobals);
    addElements(OutputGlobals);
    dxil::setEntrySignature(F, Sig);
    Changed = true;
  } else if (Stage == ShaderStage::Geometry || Stage == ShaderStage::Mesh) {
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
    // scoped to `Geometry`/`Mesh` only: unlike `Vertex`/`Fragment`
    // (dispatched to both `canonicalizeDXILStage` and this function by
    // `CanonicalizeStagePass::run`, so an empty-globals function reaching
    // here could still be a DXIL-origin entry deliberately left
    // signature-less, exactly the ambiguity roadmap H4g's own rejected fix
    // ran into), a geometry or mesh entry is only ever routed through this
    // (SPIR-V-only) function, so there is no DXIL-origin ambiguity to
    // preserve.
    //
    // (Roadmap H91) A mesh entry that calls `SetMeshOutputsEXT(0, 0)` --
    // i.e. genuinely emits zero vertices/primitives, the real shape a CTS
    // `properties.{mesh,task}_{payload,shared_memory,payload_and_shared_
    // memory}_size` case's own mesh shader takes when its only real work
    // is reading a task payload and/or shared memory back and writing a
    // pass/fail flag into an ordinary storage-buffer resource, never a
    // single per-vertex/per-primitive `Output` -- hits the exact same
    // "discovery loop found nothing, branch above never ran" gap
    // `GeometryStreamCutOnlyEntryStillGetsASignature` already covers for
    // geometry's own analogous stream-cut-only shape.
    // `feme::cpu::MeshOutputWrapperPass` (`MeshOutputWrapper.cpp`) is
    // exactly as strict about requiring an attached signature as
    // `GeometryWrapperPass` is (any mesh entry it wraps, `SetMeshOutputsEXT`
    // included, hard-requires one to look element IDs up in), so it hits
    // the identical "requires attached feme.signature metadata" diagnostic
    // this row's own missing-mesh-case left unhandled.
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

  // (Roadmap H7w) Every successful rewrite below erases the load/store it
  // replaces, but not the `GetElementPtrInst` that computed its pointer
  // operand: that GEP's own result is now unused, but -- unlike a
  // constant-index GEP, which `SPIRVToLLVMTranslator` folds into a
  // `ConstantExpr` with no separate instruction of its own to begin with
  // -- a GEP with a genuinely non-constant index (`getDynamicVertexIndexed
  // Access`/`getDynamicRowIndexedAccess`'s own shapes) must be a real
  // `Instruction`, so it survives as dead code in its own basic block
  // unless explicitly cleaned up. Left alone, `feme::cpu`'s JIT still has
  // to resolve the external SPIR-V-derived global that dead GEP
  // references -- which is never actually defined anywhere (canonical-
  // ization having just proven no real memory access to it remains) --
  // failing with a raw `"Symbols not found: [ spirv_varN ]"` link error.
  auto EraseIfNowDead = [](Value *V) {
    if (auto *I = dyn_cast<Instruction>(V))
      RecursivelyDeleteTriviallyDeadInstructions(I);
  };

  // (Roadmap H170) The actual load/store rewrite -- and its own
  // `ShadowValueMap`/dead-GEP sweep/`PromoteMemToReg` cleanup -- runs once
  // per function in `Functions` (the entry, plus any reachable helper):
  // unlike `ElementIDs`/`OutputGlobalSet`/`Sig` (module-/entry-wide state
  // shared across every function), `ShadowValueMap` places its own shadow
  // allocas in *its* function's entry block (see its own comment), so a
  // helper's own `Output` read-back must get one scoped to the helper
  // itself -- reusing the entry's own `ShadowValueMap` for a helper's
  // instructions would leave a helper-local load/store referencing an
  // alloca that lives in a wholly different function, invalid IR
  // `verifyModule` would reject outright.
  for (Function *Fn : Functions) {
    ShadowValueMap ShadowValues(*Fn, Sig);

    for (Instruction &I : llvm::make_early_inc_range(instructions(Fn))) {
      IRBuilder<> B(&I);
      Value *Zero = B.getInt32(0);
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        Value *Ptr = LI->getPointerOperand();
        std::optional<StageIOAccess> Access = resolveStageIOAccess(
            B, Ptr, LI->getType(), DL, ElementIDs, OutputGlobalSet, Stage);
        if (!Access) {
          // (Roadmap L30) A mesh entry's bounded payload read -- the
          // load-side counterpart of the task entry's own payload write
          // fallback below -- an ordinary load through a (possibly GEP'd)
          // address-space-14 global resolves no `StageIOAccess` either, for
          // the same reason (it is raw task-defined memory, not a signature
          // element). `getStageIOBaseAndOffset` still recovers its constant
          // byte offset, letting it canonicalize into
          // `feme.stage.task.payload.load` by that offset directly, rather
          // than being left an unrewritten raw load referencing a SPIR-V-
          // derived global name feme's own host runtime never defines (the
          // JIT-link failure this fixes). (Roadmap L39)
          // `loadTaskPayloadValue` fully decomposes a struct/array/vector-
          // typed read (e.g. a `float3`/`float4` payload member) into one
          // scalar load per leaf first, matching every other `feme.stage.*`
          // call's scalar-only operand/result convention -- see its own
          // comment for why. (Roadmap L47) A payload read through one
          // dynamically-indexed array member (e.g.
          // `payload.branch[gl_LocalInvocationIndex]`) resolves no constant
          // offset either -- `getTaskPayloadDynamicOffsetAccess` recognizes
          // that one additional shape instead, computing a real dynamic
          // byte-offset `Value*` in its place.
          if (auto BaseAndOffset = getStageIOBaseAndOffset(Ptr, DL)) {
            if (isTaskPayloadGlobal(BaseAndOffset->first)) {
              Value *New = loadTaskPayloadValue(
                  B, LI->getType(), B.getInt32(BaseAndOffset->second), DL);
              LI->replaceAllUsesWith(New);
              LI->eraseFromParent();
              EraseIfNowDead(Ptr);
              Changed = true;
            }
          } else if (auto Dyn = getTaskPayloadDynamicOffsetAccess(B, Ptr, DL)) {
            Value *New =
                loadTaskPayloadValue(B, LI->getType(), Dyn->second, DL);
            LI->replaceAllUsesWith(New);
            LI->eraseFromParent();
            EraseIfNowDead(Ptr);
            Changed = true;
          }
          continue;
        }
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
        // (Roadmap L94(h)) `Access->Component` is only ever set when
        // `resolveOffsetWithinElement` peels into a sub-range of the
        // element (e.g. one row of a matrix or vector); a whole-variable
        // access carries none, but must still seed its own `FirstComponent`
        // (SPIR-V's `Component` decoration) rather than assuming 0 -- see
        // `resolveElementBaseComponent`'s own comment.
        Value *Component = Access->Component
                               ? Access->Component
                               : B.getInt32(resolveElementBaseComponent(
                                     Sig, Access->ElementIDs));
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
        EraseIfNowDead(Ptr);
        Changed = true;
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        Value *Ptr = SI->getPointerOperand();
        Value *Val = SI->getValueOperand();
        std::optional<StageIOAccess> Access = resolveStageIOAccess(
            B, Ptr, Val->getType(), DL, ElementIDs, OutputGlobalSet, Stage);
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
          // unresolvable stage-IO access is. (Roadmap L39)
          // `storeTaskPayloadValue` fully decomposes a struct/array/vector-
          // typed write (e.g. a `float3`/`float4` payload member) into one
          // scalar store per leaf first, matching every other
          // `feme.stage.*` call's scalar-only operand/result convention --
          // see its own comment for why. (Roadmap L47) A payload write
          // through one dynamically-indexed array member (e.g.
          // `payload.branch[gl_LocalInvocationIndex] = ...`) resolves no
          // constant offset either -- `getTaskPayloadDynamicOffsetAccess`
          // recognizes that one additional shape instead, computing a real
          // dynamic byte-offset `Value*` in its place. This is the exact
          // shape that used to leave the raw `addrspace(14)` store on the
          // imported global entirely unconverted, surviving all the way to
          // JIT link time as an unresolved external symbol reference.
          if (auto BaseAndOffset = getStageIOBaseAndOffset(Ptr, DL)) {
            if (isTaskPayloadGlobal(BaseAndOffset->first)) {
              storeTaskPayloadValue(B, Val, Val->getType(),
                                    B.getInt32(BaseAndOffset->second), DL);
              SI->eraseFromParent();
              EraseIfNowDead(Ptr);
              Changed = true;
            }
          } else if (auto Dyn = getTaskPayloadDynamicOffsetAccess(B, Ptr, DL)) {
            storeTaskPayloadValue(B, Val, Val->getType(), Dyn->second, DL);
            SI->eraseFromParent();
            EraseIfNowDead(Ptr);
            Changed = true;
          }
          continue;
        }
        Value *Row = Access->Row ? Access->Row : Zero;
        // (Roadmap L94(h)) See the load-side mirror of this above.
        Value *Component = Access->Component
                               ? Access->Component
                               : B.getInt32(resolveElementBaseComponent(
                                     Sig, Access->ElementIDs));
        // (Roadmap L24) A `SignatureSystemValue::Position` output (`gl_
        // Position`/`SV_POSITION`) is stored as-is here, with no compensating
        // Y negation: `feme::graphics::Executor::executeDraws`'s viewport
        // transform (`projectVertex`) already implements the Vulkan-spec
        // NDC-to-window-space mapping directly (roadmap L24's own fix), so
        // every producer's raw clip-space Y reaches it unmodified. This pass
        // used to negate a single-element (but, inconsistently, not a
        // whole-`gl_PerVertex`-block) Position store here to compensate for
        // `projectVertex`'s own extra, erroneous flip -- a real `offloader`
        // re-run of `Graphics/QuadDomainTessellation.test` (a genuine
        // per-element domain-stage `SV_POSITION` store, unlike most simple
        // vertex shaders' whole-`gl_PerVertex`-block `return o;` idiom, which
        // this negation never actually reached) confirmed that compensating
        // negation is itself now the bug, doubly wrong once `projectVertex`'s
        // own flip is corrected.
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
        EraseIfNowDead(Ptr);
        Changed = true;
      }
    }

    // (Roadmap H115/H117/H118) A `GetElementPtrInst` addressing a stage-IO
    // global that never had a load/store consumer at all -- e.g. a real
    // compiled tessellation-control shader's own dead per-invocation address
    // computation into a `Block` member on some unreachable-in-practice (but
    // not dead-code-eliminated by the SPIR-V producer) control-flow path --
    // is never visited by the loop above (which only ever calls
    // `EraseIfNowDead` on a load/store's own pointer operand immediately
    // after successfully rewriting that exact load/store), so it survives
    // as a genuinely unused instruction that still references the same
    // never-actually-defined SPIR-V-derived global at JIT-link time,
    // surfacing as a raw `"Symbols not found: [ spirv_varN ]"` link failure
    // with no compile-time diagnostic at all -- exactly the shape a real
    // `dEQP-VK.tessellation.user_defined_io.per_patch_block` (and sibling
    // `per_patch_block_array`/`per_vertex_block`) case's own array-of-struct
    // `Block` member compiled into. Sweep once more for any such GEP left
    // with no uses at all once every load/store above has been rewritten.
    for (Instruction &I : llvm::make_early_inc_range(instructions(Fn))) {
      auto *GEP = dyn_cast<GetElementPtrInst>(&I);
      if (!GEP || !GEP->use_empty())
        continue;
      unsigned AddrSpace = 0;
      if (isSPIRVStageIOGlobal(
              dyn_cast<GlobalVariable>(GEP->getPointerOperand()), AddrSpace)) {
        GEP->eraseFromParent();
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
      DominatorTree DT(*Fn);
      SmallVector<AllocaInst *, 8> Allocas = ShadowValues.takeAllocas();
      PromoteMemToReg(Allocas, DT);
      Changed = true;
    }
  }

  Changed |= rewriteSPIRVDiscardAndDerivativeIntrinsics(F);

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
         *Stage != ShaderStage::Amplification &&
         *Stage != ShaderStage::Compute)) {
      // Not a recognized shader-stage entry point -- most commonly a
      // GLSL/glslang-sourced helper function a real entry point calls
      // (rather than has inlined into it, unlike `dxc`'s own HLSL/DXIL
      // output, which always fully inlines helpers before this pipeline
      // ever sees it -- see `feme::cpu::InlineHelperFunctionsPass`'s own
      // header comment for that difference, and roadmap H101a for the
      // bug this exact gap caused: a helper function's own
      // `llvm.spv.discard`, reached only via a function call, was never
      // rewritten at all, since this loop skipped straight past it,
      // leaving the raw, un-legalized intrinsic to survive
      // `InlineHelperFunctionsPass`'s later inlining and reach
      // instruction selection unconverted). This rewrite needs no
      // stage-IO/signature context (see
      // `rewriteSPIRVDiscardAndDerivativeIntrinsics`'s own comment), so
      // it is correct and safe to run on any non-declaration function,
      // recognized entry point or not.
      if (!F->isDeclaration())
        Changed |= rewriteSPIRVDiscardAndDerivativeIntrinsics(*F);
      continue;
    }

    // (roadmap L69) `ShaderStage::Compute` joins this list so a compute
    // entry's own `llvm.spv.ddx`/`.ddy`/discard/quad-read intrinsics get
    // the same `feme.stage.derivative.*`/`feme.stage.discard`/... rewrite
    // below that every other stage already gets -- previously, a compute
    // entry using `VK_KHR_compute_shader_derivatives` (`dFdx`/`dFdy`, or
    // any implicit-LOD `texture()` call that needs one internally) left
    // its raw `llvm.spv.ddx`/`.ddy` calls unconverted, which nothing later
    // in this CPU target's own pipeline (`feme::cpu::SIMDizePass`/
    // `WaveLowering.cpp`) recognizes, since both only ever look for the
    // canonical `feme.stage.derivative.*` call this rewrite produces --
    // the actual cause of `vkCreateComputePipelines` failing outright on
    // any such shader, not a gap in the derivative math itself (see
    // `WaveLowering.cpp`'s `lowerDerivative`, which is already stage-
    // agnostic). A compute entry has no stage-IO globals of its own
    // (`InputGlobals`/`OutputGlobals` below are always empty for one), so
    // every other branch this loop takes for a "real" graphics stage
    // simply never fires for it; only the signature-independent discard/
    // derivative/quad-read/helper-lane rewrite at the bottom of
    // `canonicalizeSPIRVStage` applies.
    //
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

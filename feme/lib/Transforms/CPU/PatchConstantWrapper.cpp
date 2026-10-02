//===- PatchConstantWrapper.cpp - CPU target patch-constant wrapper -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap R34's continuation, closing its "patch-constant function" open
// item (see HullWrapper.cpp's file comment and agent_thoughts.md's prior
// session): compiling a hull shader's patch-constant function -- the
// *second* phase, distinct from the control-point phase `HullWrapperPass`
// already handles -- through the CPU lowering pipeline into an invokable
// `feme::cpu::CompiledStage` batch.
//
// Unlike the control-point phase (one invocation per output control point,
// batched like a vertex wave), the patch-constant function is a single,
// non-batched invocation per patch: it reads the *whole* completed
// `OutputPatch` the control-point phase produced (potentially every one of
// its control points, not just "its own" the way a control point's own
// input load is restricted to) and writes the patch's tessellation factors
// and patch constants. "Workgroup barrier semantics" needs nothing here
// either, for a much simpler reason than the control-point phase's own
// (see that file's comment): there is only one invocation, so there is no
// sibling invocation to synchronize with in the first place -- a
// group-sync barrier reaching this phase is therefore diagnosed as an
// unsupported shape, not silently accepted as a no-op, exactly as
// `HullWrapperPass` already does for its own (structurally different)
// reason.
//
// This pass is, like `HullWrapperPass`, a close mirror of the general
// wrapper shape the rest of feme::cpu's stage wrappers share
// (`FemeStageLayout`-addressed stage storage, `feme.stage.input.load`/
// `output.store` lowering) rather than a generalization of any of them,
// duplicating its own small address-computation helpers per this
// codebase's own convention (see HullWrapper.cpp's file comment for the
// same note). The wrapper it builds still goes through the same general
// SIMDize/WaveLowering machinery every other stage does (see
// feme/lib/Target/CPU/Pipeline.cpp) -- widening the compiled body into a
// `<WaveSize x T>` wave, even though only one invocation is ever wanted --
// but calls that widened body exactly once, with only lane 0 marked active
// (`buildWrapper` below), rather than looping over waves of some batch
// count the way every other stage's wrapper does. That is what "a single,
// non-batched invocation ... rather than a wave loop"
// (`FemePatchConstantArgs`'s own comment) means concretely: the *host*-visible
// ABI is one invocation, regardless of how many SIMD lanes the compiled body
// happens to use internally.
//
// Two things follow from "a single invocation reads a whole patch":
//
//  - **No self-indexing restriction.** Unlike `lowerHullInputLoad`, an
//    ordinary (non-system-value) `feme.stage.input.load`'s control-point-
//    index operand here may be *any* value in `[0, OutputControlPointCount)`
//    -- reading more than one control point (e.g. two adjacent corners, to
//    compute the edge between them) is the whole point of this phase.
//  - **Output storage is not batched.** A tessellation-factor/patch-constant
//    `feme.stage.output.store` addresses `FemePatchConstantArgs::Outputs` by
//    row/component alone (`lowerPatchConstantOutputStore` always uses
//    invocation index 0), not per-control-point the way
//    `lowerHullOutputStore` does -- there is exactly one patch's worth of
//    storage, not one slot per output control point.
//
// `feme::cpu::isPatchConstantPhase` (HullPhase.h) is the discriminator this
// pass and `HullWrapperPass` both use to agree on which of a module's
// `feme::ShaderStage::Hull` functions each of them wraps -- see that file's
// own comment for why one stage tag alone cannot tell the two phases apart.
//
// Added in a further follow-up, closing this milestone's own "InputPatch
// parameter" deferral: a patch-constant function may declare a second
// parameter, an `InputPatch<T, M>` naming the *original*, pre-control-stage
// input control points (a hull shader's own input, not its output) -- e.g.
// to compute a tessellation factor from an edge's undisplaced length before
// any control-point-phase displacement. This is a second, independent
// structure-of-arrays input block from the `OutputPatch` one
// (`FemePatchConstantArgs::InputPatch`/`InputPatchLayout`, distinct from
// `Inputs`/`InputLayout`), addressed the same way but with its own control
// point count. `SignatureElement::FromInputPatch`, set on a `Direction::
// Input` element that is authored against the `InputPatch` parameter rather
// than the `OutputPatch` one, is what `lowerPatchConstantInputLoad` below
// switches on to pick which of the two blocks a given
// `feme.stage.input.load` addresses -- the two parameters may otherwise
// share overlapping `ElementID`s' *row* shape (both are per-control-point
// blocks of the same general shape), so the direction alone does not tell
// them apart.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/PatchConstantWrapper.h"

#include "BarrierCalls.h"
#include "HullPhase.h"
#include "StageArgsLayout.h"
#include "StageMaskCalls.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Target/CPU/RuntimeABI.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme;
using namespace feme::cpu;

namespace {

constexpr StringLiteral InputLayoutParamName = "stage_input_layout";
constexpr StringLiteral InputsParamName = "stage_inputs";
constexpr StringLiteral InputPatchLayoutParamName = "stage_input_patch_layout";
constexpr StringLiteral InputPatchParamName = "stage_input_patch";
constexpr StringLiteral OutputLayoutParamName = "stage_output_layout";
constexpr StringLiteral OutputsParamName = "stage_outputs";
constexpr StringLiteral InputPatchControlPointCountParamName =
    "stage_input_patch_control_point_count";
constexpr StringLiteral PrimitiveIDParamName = "stage_primitive_id";
constexpr StringLiteral ViewIndexParamName = "stage_view_index";
// (Roadmap L339) A genuine per-control-point output's own storage/layout --
// see `FemePatchConstantArgs::PerVertexOutputLayout`'s own comment.
constexpr StringLiteral PerVertexOutputLayoutParamName =
    "stage_per_vertex_output_layout";
constexpr StringLiteral PerVertexOutputsParamName = "stage_per_vertex_outputs";

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

struct PatchConstantStageEnv {
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *InputPatchLayout = nullptr;
  Value *InputPatch = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *InputPatchControlPointCount = nullptr;
  Value *PrimitiveID = nullptr;
  Value *ViewIndex = nullptr;
  // (Roadmap L339) Present only when this phase's signature declares a
  // genuine per-control-point output; see `buildWrapperEnv`'s own comment.
  Value *PerVertexOutputLayout = nullptr;
  Value *PerVertexOutputs = nullptr;
};

std::optional<PatchConstantStageEnv> getPatchConstantStageEnv(Function &F) {
  PatchConstantStageEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == InputLayoutParamName)
      Env.InputLayout = &Arg, Found = true;
    else if (Arg.getName() == InputsParamName)
      Env.Inputs = &Arg, Found = true;
    else if (Arg.getName() == InputPatchLayoutParamName)
      Env.InputPatchLayout = &Arg, Found = true;
    else if (Arg.getName() == InputPatchParamName)
      Env.InputPatch = &Arg, Found = true;
    else if (Arg.getName() == OutputLayoutParamName)
      Env.OutputLayout = &Arg, Found = true;
    else if (Arg.getName() == OutputsParamName)
      Env.Outputs = &Arg, Found = true;
    else if (Arg.getName() == InputPatchControlPointCountParamName)
      Env.InputPatchControlPointCount = &Arg, Found = true;
    else if (Arg.getName() == PrimitiveIDParamName)
      Env.PrimitiveID = &Arg, Found = true;
    else if (Arg.getName() == ViewIndexParamName)
      Env.ViewIndex = &Arg, Found = true;
    else if (Arg.getName() == PerVertexOutputLayoutParamName)
      Env.PerVertexOutputLayout = &Arg, Found = true;
    else if (Arg.getName() == PerVertexOutputsParamName)
      Env.PerVertexOutputs = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

Function *appendPatchConstantStageParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Type *, 14> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, I32Ty, I32Ty,
                     I32Ty, PtrTy, PtrTy});

  FunctionType *NewTy =
      FunctionType::get(F.getReturnType(), ParamTypes, F.isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
  NewF->setComdat(F.getComdat());
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto [Kind, Node] : MDs)
    NewF->setMetadata(Kind, Node);
  NewF->splice(NewF->begin(), &F);

  for (auto [OldArg, NewArg] : zip(F.args(), NewF->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  auto ArgIt = NewF->arg_begin() + F.arg_size();
  (&*ArgIt++)->setName(InputLayoutParamName);
  (&*ArgIt++)->setName(InputsParamName);
  (&*ArgIt++)->setName(InputPatchLayoutParamName);
  (&*ArgIt++)->setName(InputPatchParamName);
  (&*ArgIt++)->setName(OutputLayoutParamName);
  (&*ArgIt++)->setName(OutputsParamName);
  (&*ArgIt++)->setName(InputPatchControlPointCountParamName);
  (&*ArgIt++)->setName(PrimitiveIDParamName);
  (&*ArgIt++)->setName(ViewIndexParamName);
  (&*ArgIt++)->setName(PerVertexOutputLayoutParamName);
  (&*ArgIt++)->setName(PerVertexOutputsParamName);

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

Value *extractLaneOrScalar(IRBuilder<> &Builder, Value *V, unsigned Lane) {
  if (isa<FixedVectorType>(V->getType()))
    return Builder.CreateExtractElement(V, Builder.getInt32(Lane));
  return V;
}

/// (Roadmap L98(a)) `StageStorage` only ever holds a genuine `float32` bit
/// pattern for a `Float`-`ComponentType` element -- a 16-bit `half` leaf is
/// widened to `float` immediately before every store here, and narrowed
/// back immediately after every load, so the storage layer itself (and,
/// downstream, `Executor.cpp`'s `lerpVertex`, which interpolates any
/// `Float`-typed varying as a real `float32` value) never observes a
/// genuine 16-bit value. Returns \p ScalarTy unchanged for anything else.
Type *stageStorageLoadType(Type *ScalarTy) {
  return ScalarTy->isHalfTy() ? Type::getFloatTy(ScalarTy->getContext())
                              : ScalarTy;
}

/// The load-side mirror of `stageStorageLoadType`: narrows a `float` value
/// just loaded from `StageStorage` back to \p ScalarTy if it is `half`
/// (the load itself already used `stageStorageLoadType(ScalarTy)`, so
/// \p Loaded is `float`-typed exactly when this narrowing is needed).
Value *narrowStageStorageLoad(IRBuilder<> &Builder, Value *Loaded,
                              Type *ScalarTy) {
  return ScalarTy->isHalfTy() ? Builder.CreateFPTrunc(Loaded, ScalarTy)
                              : Loaded;
}

/// The store-side mirror: widens \p Val to `float` if it is `half`-typed,
/// so every write to `StageStorage` is a genuine `float32` value.
Value *widenForStageStorageStore(IRBuilder<> &Builder, Value *Val) {
  return Val->getType()->isHalfTy()
             ? Builder.CreateFPExt(Val, Type::getFloatTy(Builder.getContext()))
             : Val;
}

/// Mirrors `feme::cpu::HullWrapperPass`'s own (identically-named, file-local)
/// helper: this lane's flat invocation index within the whole dispatch,
/// derived purely from the wave's own index and this lane's position within
/// it -- never from any call operand. `lowerPatchConstantInputLoad` (roadmap
/// L82) needs this to address a `SignatureElement::CapturedSelfIndex`
/// element by *this* invocation's own storage slot, exactly like
/// `HullWrapper.cpp`'s `lowerHullOutputStore` already addresses that same
/// slot on the write side.
Value *getFlatInvocationIndex(IRBuilder<> &Builder, const WaveBodyEnv &WEnv,
                              unsigned WaveSize, unsigned Lane) {
  Value *Base = Builder.CreateMul(WEnv.WaveIndex, Builder.getInt32(WaveSize),
                                  "flat.base");
  return Builder.CreateAdd(Base, Builder.getInt32(Lane), "flat.index");
}

Value *loadLayoutField(IRBuilder<> &Builder, Value *LayoutArg,
                       unsigned ElementID, unsigned Field, Type *FieldTy) {
  LLVMContext &Ctx = Builder.getContext();
  StructType *LayoutTy = getStageLayoutType(Ctx);
  StructType *ElementTy = getStageElementType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Value *ElementsRaw = loadStructField(Builder, LayoutTy, LayoutArg,
                                       StageLayoutFieldElements, PtrTy);
  Value *Elements =
      Builder.CreateBitCast(ElementsRaw, PointerType::get(Ctx, 0));
  Value *EntryPtr = Builder.CreateInBoundsGEP(ElementTy, Elements,
                                              Builder.getInt32(ElementID));
  Value *FieldPtr = Builder.CreateStructGEP(ElementTy, EntryPtr, Field);
  return Builder.CreateLoad(FieldTy, FieldPtr);
}

Value *computeStageStorageAddress(IRBuilder<> &Builder, Value *LayoutArg,
                                  Value *BasePtr, unsigned ElementID,
                                  const SignatureElement &Elt, Value *Row,
                                  Value *Component, Value *InvocationIndex) {
  LLVMContext &Ctx = Builder.getContext();
  Type *I32Ty = Builder.getInt32Ty();
  Type *I64Ty = Builder.getInt64Ty();
  Value *DataOffset = loadLayoutField(Builder, LayoutArg, ElementID,
                                      StageElementFieldDataOffset, I64Ty);
  Value *RowStride = loadLayoutField(Builder, LayoutArg, ElementID,
                                     StageElementFieldRowStride, I32Ty);
  Value *ComponentStride = loadLayoutField(
      Builder, LayoutArg, ElementID, StageElementFieldComponentStride, I32Ty);
  Value *InvocationStride = loadLayoutField(
      Builder, LayoutArg, ElementID, StageElementFieldInvocationStride, I32Ty);
  Value *RelComponent = Builder.CreateSub(
      Component, Builder.getInt32(Elt.FirstComponent), "component.rel");
  Value *ByteOffset = DataOffset;
  ByteOffset = Builder.CreateAdd(
      ByteOffset, Builder.CreateMul(Builder.CreateZExt(Row, I64Ty),
                                    Builder.CreateZExt(RowStride, I64Ty)));
  ByteOffset = Builder.CreateAdd(
      ByteOffset,
      Builder.CreateMul(Builder.CreateZExt(RelComponent, I64Ty),
                        Builder.CreateZExt(ComponentStride, I64Ty)));
  ByteOffset = Builder.CreateAdd(
      ByteOffset,
      Builder.CreateMul(Builder.CreateZExt(InvocationIndex, I64Ty),
                        Builder.CreateZExt(InvocationStride, I64Ty)));
  Value *Bytes = Builder.CreateBitCast(BasePtr, PointerType::get(Ctx, 0));
  return Builder.CreateInBoundsGEP(Builder.getInt8Ty(), Bytes, ByteOffset);
}

/// Lowers an ordinary `feme.stage.input.load` reading either the completed
/// `OutputPatch` or, when `Elt.FromInputPatch` is set, the original
/// `InputPatch` (see this file's comment). Unlike `HullWrapper.cpp`'s
/// `lowerHullInputLoad`, the control-point-index operand (`CI`'s 4th
/// argument) may be any value -- this phase's whole point is reading more
/// than one control point.
Value *lowerPatchConstantInputLoad(CallInst &CI, const SignatureElement &Elt,
                                   const WaveBodyEnv &WEnv,
                                   const PatchConstantStageEnv &PEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);
  Value *LayoutArg =
      Elt.FromInputPatch ? PEnv.InputPatchLayout : PEnv.InputLayout;
  Value *StorageArg = Elt.FromInputPatch ? PEnv.InputPatch : PEnv.Inputs;

  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    // (Roadmap L82) A captured cross-barrier value (see
    // `SignatureElement::CapturedSelfIndex`'s own comment) has no real
    // "which control point" to address other than this lane's own -- the
    // call's own `ControlPoint` operand is only ever a constant `0`
    // (`resolveStageIOAccess` has no dynamic-or-constant vertex index to
    // recover from the capture global's unindexed load/store pair), which
    // would otherwise make every invocation re-read invocation 0's own
    // captured value instead of its own.
    Value *ControlPoint =
        Elt.CapturedSelfIndex
            ? getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane)
            : extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    Value *Addr = computeStageStorageAddress(Builder, LayoutArg, StorageArg,
                                             Elt.ElementID, Elt, Row, Component,
                                             ControlPoint);
    Value *LaneResult =
        Builder.CreateLoad(stageStorageLoadType(ScalarTy), Addr);
    LaneResult = narrowStageStorageLoad(Builder, LaneResult, ScalarTy);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

Value *lowerPatchConstantSystemValue(CallInst &CI, const SignatureElement &Elt,
                                     const WaveBodyEnv &WEnv,
                                     const PatchConstantStageEnv &PEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  IRBuilder<> Builder(&CI);
  // (Roadmap L339) `OutputControlPointID` (`gl_InvocationID`) is this
  // lane's own flat invocation index, exactly like `lowerPatchConstant
  // InputLoad`'s `CapturedSelfIndex` case just above -- not the constant
  // `0` every prior (single-invocation-only) shape here read. In the
  // legacy single-invocation case this still always evaluates to `0`
  // (`WEnv.WaveIndex` is always `0` and only lane `0` is ever active
  // there -- see `buildWrapper`'s own comment), so this is a pure
  // generalization, not a behavior change, for every shape that does not
  // declare a genuine per-control-point output.
  if (Elt.SystemValue == SignatureSystemValue::OutputControlPointID) {
    Value *Result = PoisonValue::get(CI.getType());
    for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
      Value *Active =
          Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
      Value *FlatIndex = getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
      Value *LaneResult =
          Builder.CreateSelect(Active, FlatIndex, Builder.getInt32(0));
      Result = Builder.CreateInsertElement(Result, LaneResult,
                                           Builder.getInt32(Lane));
    }
    return Result;
  }
  Value *Scalar = Elt.SystemValue == SignatureSystemValue::PatchVertices &&
                          Elt.FromInputPatch
                      ? PEnv.InputPatchControlPointCount
                  // (Roadmap L82) A patch-constant function's own
                  // `SV_PrimitiveID` parameter, independent of any
                  // control-point-phase read of the same builtin (which a
                  // barrier-based split's cross-barrier capture forwards on
                  // its own via `SignatureElement::CapturedSelfIndex`): not
                  // storage-backed, so it must come from `PEnv.PrimitiveID`
                  // rather than falling into `lowerPatchConstantInputLoad`'s
                  // generic storage-address computation (see
                  // `HullWrapper.cpp`'s `lowerHullPrimitiveID`, the same
                  // mistake this mirrors on the control-point side).
                  : Elt.SystemValue == SignatureSystemValue::PrimitiveID
                      ? PEnv.PrimitiveID
                  // (Roadmap H51/L109) `gl_ViewIndex` read from the
                  // patch-constant phase: same rationale as `PrimitiveID`
                  // just above -- pipeline-supplied and uniform for the
                  // whole batch, not storage-backed.
                  : Elt.SystemValue == SignatureSystemValue::ViewIndex
                      ? PEnv.ViewIndex
                      : nullptr;
  if (!Scalar)
    return nullptr;
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *LaneResult =
        Builder.CreateSelect(Active, Scalar, Builder.getInt32(0));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers a `feme.stage.output.store` writing either a tessellation
/// factor/patch constant (\p PerVertex false: storage is per-patch, every
/// lane's write uses invocation index 0, addressing the same single patch
/// record) or a genuine per-control-point output (\p PerVertex true,
/// roadmap L339: storage is structure-of-arrays, each lane's write uses
/// *its own* flat invocation index -- see `buildWrapper`'s own comment on
/// why this phase's compiled body is re-invoked once per control point
/// whenever such an element exists, exactly like `HullWrapper.cpp`'s own
/// per-control-point output store).
void lowerPatchConstantOutputStore(CallInst &CI, const SignatureElement &Elt,
                                   const WaveBodyEnv &WEnv,
                                   const PatchConstantStageEnv &PEnv,
                                   bool PerVertex) {
  IRBuilder<> Builder(&CI);
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(3)->getType())->getNumElements();
  Value *OutputLayout = PerVertex ? PEnv.PerVertexOutputLayout : PEnv.OutputLayout;
  Value *Outputs = PerVertex ? PEnv.PerVertexOutputs : PEnv.Outputs;
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(5), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *InvocationIndex =
        PerVertex ? getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane)
                  : Builder.getInt32(0);
    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *Addr = computeStageStorageAddress(Builder, OutputLayout, Outputs,
                                             Elt.ElementID, Elt, Row, Component,
                                             InvocationIndex);
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    LaneVal = widenForStageStorageStore(Builder, LaneVal);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(LaneVal->getType(), Addr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

/// (Roadmap L347) The load-side counterpart of `lowerPatchConstantOutputStore`
/// just above, for `StageOpKind::OutputLoad`'s own self-read-back shape: a
/// genuine per-vertex `Output`-direction element's own already-written
/// value, read back by this same invocation within the same patch-constant
/// phase (e.g. GLSL's `gl_out[gl_InvocationID].gl_Position.xy = ... +
/// gl_out[gl_InvocationID].gl_Position.xy`, see `CanonicalizeStage.cpp`'s
/// own `ShadowValueMap` seeding comment). Unlike `lowerPatchConstantInput
/// Load`, \p CI's own `vertex` operand (always a constant `0`, per
/// `createStageOutputLoad`'s own comment) is never used for addressing --
/// every lane reads back its *own* flat invocation index's value, the
/// same `PerVertexOutputs`/`PerVertexOutputLayout` storage
/// `lowerPatchConstantOutputStore`'s own `PerVertex` case writes.
Value *lowerPatchConstantOutputLoad(CallInst &CI, const SignatureElement &Elt,
                                    const WaveBodyEnv &WEnv,
                                    const PatchConstantStageEnv &PEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *Addr = computeStageStorageAddress(
        Builder, PEnv.PerVertexOutputLayout, PEnv.PerVertexOutputs,
        Elt.ElementID, Elt, Row, Component, InvocationIndex);
    Value *LaneResult =
        Builder.CreateLoad(stageStorageLoadType(ScalarTy), Addr);
    LaneResult = narrowStageStorageLoad(Builder, LaneResult, ScalarTy);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

bool lowerPatchConstantStageOps(Function &F) {
  // A single invocation has no sibling to synchronize with; see this file's
  // comment for why a group-sync barrier here is diagnosed rather than
  // treated as a (structurally meaningless) no-op.
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI)) {
      if (Matched->GroupSync) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-patch-constant: a group-sync barrier is not "
                "supported in the single-invocation patch-constant phase");
        return false;
      }
    }
  }

  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(F);
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedOutputStoreCall(*CI);
  if (!UsesStageOps)
    return true;
  if (!Sig) {
    F.getContext().emitError(
        "feme-cpu-wrap-patch-constant: patch-constant wrapper requires "
        "attached feme.signature metadata");
    return false;
  }

  std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(F);
  std::optional<PatchConstantStageEnv> PEnv = getPatchConstantStageEnv(F);
  if (!WEnv || !PEnv)
    return false;

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (isMaskedOutputStoreCall(*CI)) {
      auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      // (Roadmap L339) A masked output store addresses either the
      // existing per-patch `PatchOutput`-direction element or a genuine
      // per-control-point `Output`-direction one (`classifySPIRVElement`
      // now distinguishes the two -- see its own comment); try both,
      // dispatching `lowerPatchConstantOutputStore`'s `PerVertex` flag
      // accordingly.
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::PatchOutput)
              : nullptr;
      bool PerVertex = false;
      if (!Elt && EltID) {
        Elt = findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                          SignatureDirection::Output);
        PerVertex = Elt != nullptr;
      }
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-patch-constant: masked output store "
                "references an unknown patch-output signature element");
        return false;
      }
      lowerPatchConstantOutputStore(*CI, *Elt, *WEnv, *PEnv, PerVertex);
      CI->eraseFromParent();
      continue;
    }

    StageOpKind Kind;
    if (!isStageOpCall(*CI, &Kind))
      continue;
    auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    if (!EltID) {
      F.getContext().emitError(
          CI, "feme-cpu-wrap-patch-constant: stage IO requires a constant "
              "element ID");
      return false;
    }

    switch (Kind) {
    case StageOpKind::InputLoad: {
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::Input);
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-patch-constant: input load refers to an "
                "unknown signature element");
        return false;
      }
      // (roadmap H13b) A per-control-point-addressable builtin input --
      // `Position`, `ClipDistance`, `CullDistance` -- read from the
      // *original* input patch by a barrier-less tessellation-control
      // entry point whose mixed control-point/patch-constant body ends up
      // compiled as this phase too (see this file's own comment on
      // `lowerPatchConstantInputLoad` and `CanonicalizeStage.cpp`'s
      // `isPatchConstantPhase`/`splitBarrierlessTessellationControlEntry`/
      // `classifySPIRVElement`): despite carrying a `SystemValue` (these
      // builtins have no `Location` of their own -- see
      // `FragmentWrapper.cpp`'s analogous roadmap H7x fix), only
      // `OutputControlPointID` (the current invocation's own index, never
      // addressable to a *different* control point), `PatchVertices`
      // (a true per-patch scalar count), and `PrimitiveID` (roadmap L82:
      // this patch's own index, uniform for the whole invocation, with no
      // per-control-point storage at all -- see `lowerPatchConstantSystemValue`'s
      // own comment) are the genuinely non-addressable system values
      // `lowerPatchConstantSystemValue` below handles -- any other system
      // value here (or none at all) is an ordinary array element read the
      // same `InputPatch`-addressed way as any other linked input, keyed by
      // `Elt.ElementID` the same way.
      bool IsScalarSystemValue =
          Elt->SystemValue == SignatureSystemValue::OutputControlPointID ||
          Elt->SystemValue == SignatureSystemValue::PatchVertices ||
          Elt->SystemValue == SignatureSystemValue::PrimitiveID;
      Value *Lowered = IsScalarSystemValue
                           ? lowerPatchConstantSystemValue(*CI, *Elt, *WEnv,
                                                           *PEnv)
                           : lowerPatchConstantInputLoad(*CI, *Elt, *WEnv,
                                                         *PEnv);
      if (IsScalarSystemValue && !Lowered) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-patch-constant: unsupported patch-constant "
                "input system value");
        return false;
      }
      CI->replaceAllUsesWith(Lowered);
      CI->eraseFromParent();
      break;
    }
    case StageOpKind::OutputStore: {
      // (Roadmap L339) Same `PatchOutput`-or-`Output` dispatch as the
      // masked-store case just above.
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::PatchOutput);
      bool PerVertex = false;
      if (!Elt) {
        Elt = findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                          SignatureDirection::Output);
        PerVertex = Elt != nullptr;
      }
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-patch-constant: output store refers to an "
                "unknown patch-output signature element");
        return false;
      }
      lowerPatchConstantOutputStore(*CI, *Elt, *WEnv, *PEnv, PerVertex);
      CI->eraseFromParent();
      break;
    }
    case StageOpKind::OutputLoad: {
      // (Roadmap L347) Always the genuine per-vertex `Output`-direction
      // element -- `OutputLoad` is only ever emitted by
      // `CanonicalizeStage.cpp`'s own `ShadowValueMap` seeding for exactly
      // that shape (see `StageOpKind::OutputLoad`'s own comment), never
      // for a `PatchOutput`-direction (tess-factor/patch-constant) one.
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::Output);
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-patch-constant: output load refers to an "
                "unknown patch-output signature element");
        return false;
      }
      Value *Lowered = lowerPatchConstantOutputLoad(*CI, *Elt, *WEnv, *PEnv);
      CI->replaceAllUsesWith(Lowered);
      CI->eraseFromParent();
      break;
    }
    default:
      F.getContext().emitError(
          CI, "feme-cpu-wrap-patch-constant: unexpected stage op left for "
              "the patch-constant wrapper");
      return false;
    }
  }
  return true;
}

struct WrapperEnv {
  Value *ResourceHeap = nullptr;
  Value *ResourceHeapCount = nullptr;
  Value *SamplerHeap = nullptr;
  Value *SamplerHeapCount = nullptr;
  Value *RootConstants = nullptr;
  Value *RootConstantSize = nullptr;
  Value *ImageHeap = nullptr;
  Value *ImageHeapCount = nullptr;
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *InputPatchLayout = nullptr;
  Value *InputPatch = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *InputPatchControlPointCount = nullptr;
  Value *PrimitiveID = nullptr;
  Value *ViewIndex = nullptr;
  // (Roadmap L339) Loaded unconditionally now -- previously present in the
  // ABI but never read, since nothing here needed a runtime trip count
  // before a genuine per-control-point output existed. See `buildWrapper`'s
  // own comment for why this is now the wave loop's trip count.
  Value *OutputControlPointCount = nullptr;
  Value *PerVertexOutputLayout = nullptr;
  Value *PerVertexOutputs = nullptr;
};

WrapperEnv buildWrapperEnv(IRBuilder<> &Builder, StructType *ArgsTy,
                           Value *Args) {
  LLVMContext &Ctx = Builder.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Builder.getInt32Ty();
  WrapperEnv Env;
  Env.InputLayout = loadStructField(Builder, ArgsTy, Args,
                                    PatchConstantArgsFieldInputLayout, PtrTy);
  Env.Inputs = loadStructField(Builder, ArgsTy, Args,
                               PatchConstantArgsFieldInputs, PtrTy);
  Env.InputPatchLayout = loadStructField(
      Builder, ArgsTy, Args, PatchConstantArgsFieldInputPatchLayout, PtrTy);
  Env.InputPatch = loadStructField(Builder, ArgsTy, Args,
                                   PatchConstantArgsFieldInputPatch, PtrTy);
  Env.OutputLayout = loadStructField(Builder, ArgsTy, Args,
                                     PatchConstantArgsFieldOutputLayout, PtrTy);
  Env.Outputs = loadStructField(Builder, ArgsTy, Args,
                                PatchConstantArgsFieldOutputs, PtrTy);
  Env.InputPatchControlPointCount =
      loadStructField(Builder, ArgsTy, Args,
                      PatchConstantArgsFieldInputPatchControlPointCount, I32Ty);
  Env.PrimitiveID = loadStructField(
      Builder, ArgsTy, Args, PatchConstantArgsFieldPrimitiveID, I32Ty);
  Env.ViewIndex = loadStructField(
      Builder, ArgsTy, Args, PatchConstantArgsFieldViewIndex, I32Ty);
  Env.OutputControlPointCount = loadStructField(
      Builder, ArgsTy, Args, PatchConstantArgsFieldOutputControlPointCount,
      I32Ty);
  Env.PerVertexOutputLayout =
      loadStructField(Builder, ArgsTy, Args,
                      PatchConstantArgsFieldPerVertexOutputLayout, PtrTy);
  Env.PerVertexOutputs = loadStructField(
      Builder, ArgsTy, Args, PatchConstantArgsFieldPerVertexOutputs, PtrTy);

  Value *ResourcesRaw = loadStructField(Builder, ArgsTy, Args,
                                        PatchConstantArgsFieldResources, PtrTy);
  StructType *ResourcesTy = getShaderResourcesType(Ctx);
  Value *Resources =
      Builder.CreateBitCast(ResourcesRaw, PointerType::get(Ctx, 0));
  Env.ResourceHeap = loadStructField(Builder, ResourcesTy, Resources,
                                     ShaderResourcesFieldResourceHeap, PtrTy);
  Env.ResourceHeapCount =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldResourceHeapCount, I32Ty);
  Env.SamplerHeap = loadStructField(Builder, ResourcesTy, Resources,
                                    ShaderResourcesFieldSamplerHeap, PtrTy);
  Env.SamplerHeapCount =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldSamplerHeapCount, I32Ty);
  Env.RootConstants = loadStructField(Builder, ResourcesTy, Resources,
                                      ShaderResourcesFieldRootConstants, PtrTy);
  Env.RootConstantSize =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldRootConstantSize, I32Ty);
  Env.ImageHeap = loadStructField(Builder, ResourcesTy, Resources,
                                  ShaderResourcesFieldImageHeap, PtrTy);
  Env.ImageHeapCount =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldImageHeapCount, I32Ty);
  return Env;
}

Function *buildWrapper(Function &Body) {
  if (!getWaveBodyEnv(Body))
    return nullptr;
  Module &M = *Body.getParent();
  LLVMContext &Ctx = M.getContext();
  unsigned WaveSize =
      cast<FixedVectorType>(getWaveBodyEnv(Body)->EntryMask->getType())
          ->getNumElements();

  StructType *ArgsTy = getPatchConstantArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);

  // (Roadmap L339) A genuine per-control-point output forces this phase's
  // compiled body to be re-invoked once per output control point --
  // `HullWrapper.cpp`'s own wave loop, mirrored below -- rather than the
  // single, lane-0-only call every other (patch-frequency-only) shape
  // still gets. Detected from the body's own attached signature: a
  // `PatchOutput`-direction element never needs this (patch-frequency
  // values are safely rewritten redundantly by every invocation, see this
  // file's own top comment), only a true `Output`-direction one does.
  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(Body);
  bool HasPerVertexOutput =
      Sig && any_of(Sig->Elements, [](const SignatureElement &Elt) {
        return Elt.Direction == SignatureDirection::Output;
      });

  std::string WrapperName = getEntrySymbolName(Body.getName());
  Function *Wrapper =
      Function::Create(FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false),
                       GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  if (!HasPerVertexOutput) {
    // A single, non-batched invocation (this file's own comment): one call
    // to the widened body, with only lane 0 marked active -- there is no
    // wave loop over some batch count the way every other stage's wrapper
    // has.
    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
    IRBuilder<> Entry(EntryBB);
    WrapperEnv Env = buildWrapperEnv(Entry, ArgsTy, Args);

    SmallVector<Constant *, 8> LaneIsZero;
    for (unsigned I = 0; I != WaveSize; ++I)
      LaneIsZero.push_back(Entry.getInt1(I == 0));
    Value *Mask = ConstantVector::get(LaneIsZero);

    SmallVector<Value *, 16> CallArgs;
    for (const Argument &Arg : Body.args()) {
      if (Arg.getName() == "resource_heap")
        CallArgs.push_back(Env.ResourceHeap);
      else if (Arg.getName() == "resource_heap_count")
        CallArgs.push_back(Env.ResourceHeapCount);
      else if (Arg.getName() == "sampler_heap")
        CallArgs.push_back(Env.SamplerHeap);
      else if (Arg.getName() == "sampler_heap_count")
        CallArgs.push_back(Env.SamplerHeapCount);
      else if (Arg.getName() == "root_constants")
        CallArgs.push_back(Env.RootConstants);
      else if (Arg.getName() == "root_constant_size")
        CallArgs.push_back(Env.RootConstantSize);
      else if (Arg.getName() == "image_heap")
        CallArgs.push_back(Env.ImageHeap);
      else if (Arg.getName() == "image_heap_count")
        CallArgs.push_back(Env.ImageHeapCount);
      else if (Arg.getName() == "wave_group_id_x" ||
               Arg.getName() == "wave_group_id_y" ||
               Arg.getName() == "wave_group_id_z")
        CallArgs.push_back(Entry.getInt32(0));
      else if (Arg.getName() == "wave_group_count_x" ||
               Arg.getName() == "wave_group_count_y" ||
               Arg.getName() == "wave_group_count_z")
        CallArgs.push_back(Entry.getInt32(1));
      else if (Arg.getName() == "wave_index")
        CallArgs.push_back(Entry.getInt32(0));
      else if (Arg.getName() == "wave_entry_mask" ||
               Arg.getName() == "wave_sideeffect_mask")
        CallArgs.push_back(Mask);
      else if (Arg.getName() == "wave_groupshared")
        CallArgs.push_back(
            ConstantPointerNull::get(cast<PointerType>(Arg.getType())));
      else if (Arg.getName() == InputLayoutParamName)
        CallArgs.push_back(Env.InputLayout);
      else if (Arg.getName() == InputsParamName)
        CallArgs.push_back(Env.Inputs);
      else if (Arg.getName() == InputPatchLayoutParamName)
        CallArgs.push_back(Env.InputPatchLayout);
      else if (Arg.getName() == InputPatchParamName)
        CallArgs.push_back(Env.InputPatch);
      else if (Arg.getName() == OutputLayoutParamName)
        CallArgs.push_back(Env.OutputLayout);
      else if (Arg.getName() == OutputsParamName)
        CallArgs.push_back(Env.Outputs);
      else if (Arg.getName() == InputPatchControlPointCountParamName)
        CallArgs.push_back(Env.InputPatchControlPointCount);
      else if (Arg.getName() == PrimitiveIDParamName)
        CallArgs.push_back(Env.PrimitiveID);
      else if (Arg.getName() == ViewIndexParamName)
        CallArgs.push_back(Env.ViewIndex);
      else if (Arg.getName() == PerVertexOutputLayoutParamName)
        CallArgs.push_back(Env.PerVertexOutputLayout);
      else if (Arg.getName() == PerVertexOutputsParamName)
        CallArgs.push_back(Env.PerVertexOutputs);
      else
        llvm_unreachable("unexpected parameter for PatchConstantWrapperPass");
    }
    Entry.CreateCall(&Body, CallArgs);
    Entry.CreateRetVoid();

    Body.setLinkage(GlobalValue::InternalLinkage);
    return Wrapper;
  }

  // (Roadmap L339) `HullWrapper.cpp`'s own wave loop, mirrored verbatim:
  // re-invoke the widened body `ceil(OutputControlPointCount/WaveSize)`
  // times, each iteration computing this wave's flat invocation indices
  // and an in-bounds mask from them.
  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  BasicBlock *HeaderBB = BasicBlock::Create(Ctx, "wave.loop.header", Wrapper);
  BasicBlock *BodyBB = BasicBlock::Create(Ctx, "wave.loop.body", Wrapper);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "wave.loop.exit", Wrapper);

  IRBuilder<> Entry(EntryBB);
  WrapperEnv Env = buildWrapperEnv(Entry, ArgsTy, Args);
  Value *Waves = Entry.CreateUDiv(Entry.CreateAdd(Env.OutputControlPointCount,
                                                  Entry.getInt32(WaveSize - 1)),
                                  Entry.getInt32(WaveSize), "waves");
  Entry.CreateBr(HeaderBB);

  IRBuilder<> Header(HeaderBB);
  PHINode *W = Header.CreatePHI(I32Ty, 2, "w");
  W->addIncoming(Header.getInt32(0), EntryBB);
  Value *Cond = Header.CreateICmpULT(W, Waves, "wave.cond");
  Header.CreateCondBr(Cond, BodyBB, ExitBB);

  IRBuilder<> BodyIR(BodyBB);
  Value *Base = BodyIR.CreateMul(W, BodyIR.getInt32(WaveSize));
  Value *WideBase = BodyIR.CreateVectorSplat(WaveSize, Base);
  SmallVector<Constant *, 8> Lanes;
  for (unsigned I = 0; I != WaveSize; ++I)
    Lanes.push_back(BodyIR.getInt32(I));
  Value *Indices = BodyIR.CreateAdd(WideBase, ConstantVector::get(Lanes));
  Value *WideCount =
      BodyIR.CreateVectorSplat(WaveSize, Env.OutputControlPointCount);
  Value *Mask = BodyIR.CreateICmpULT(Indices, WideCount, "wave.mask");

  SmallVector<Value *, 16> CallArgs;
  for (const Argument &Arg : Body.args()) {
    if (Arg.getName() == "resource_heap")
      CallArgs.push_back(Env.ResourceHeap);
    else if (Arg.getName() == "resource_heap_count")
      CallArgs.push_back(Env.ResourceHeapCount);
    else if (Arg.getName() == "sampler_heap")
      CallArgs.push_back(Env.SamplerHeap);
    else if (Arg.getName() == "sampler_heap_count")
      CallArgs.push_back(Env.SamplerHeapCount);
    else if (Arg.getName() == "root_constants")
      CallArgs.push_back(Env.RootConstants);
    else if (Arg.getName() == "root_constant_size")
      CallArgs.push_back(Env.RootConstantSize);
    else if (Arg.getName() == "image_heap")
      CallArgs.push_back(Env.ImageHeap);
    else if (Arg.getName() == "image_heap_count")
      CallArgs.push_back(Env.ImageHeapCount);
    else if (Arg.getName() == "wave_group_id_x" ||
             Arg.getName() == "wave_group_id_y" ||
             Arg.getName() == "wave_group_id_z")
      CallArgs.push_back(BodyIR.getInt32(0));
    else if (Arg.getName() == "wave_group_count_x" ||
             Arg.getName() == "wave_group_count_y" ||
             Arg.getName() == "wave_group_count_z")
      CallArgs.push_back(BodyIR.getInt32(1));
    else if (Arg.getName() == "wave_index")
      CallArgs.push_back(W);
    else if (Arg.getName() == "wave_entry_mask" ||
             Arg.getName() == "wave_sideeffect_mask")
      CallArgs.push_back(Mask);
    else if (Arg.getName() == "wave_groupshared")
      CallArgs.push_back(
          ConstantPointerNull::get(cast<PointerType>(Arg.getType())));
    else if (Arg.getName() == InputLayoutParamName)
      CallArgs.push_back(Env.InputLayout);
    else if (Arg.getName() == InputsParamName)
      CallArgs.push_back(Env.Inputs);
    else if (Arg.getName() == InputPatchLayoutParamName)
      CallArgs.push_back(Env.InputPatchLayout);
    else if (Arg.getName() == InputPatchParamName)
      CallArgs.push_back(Env.InputPatch);
    else if (Arg.getName() == OutputLayoutParamName)
      CallArgs.push_back(Env.OutputLayout);
    else if (Arg.getName() == OutputsParamName)
      CallArgs.push_back(Env.Outputs);
    else if (Arg.getName() == InputPatchControlPointCountParamName)
      CallArgs.push_back(Env.InputPatchControlPointCount);
    else if (Arg.getName() == PrimitiveIDParamName)
      CallArgs.push_back(Env.PrimitiveID);
    else if (Arg.getName() == ViewIndexParamName)
      CallArgs.push_back(Env.ViewIndex);
    else if (Arg.getName() == PerVertexOutputLayoutParamName)
      CallArgs.push_back(Env.PerVertexOutputLayout);
    else if (Arg.getName() == PerVertexOutputsParamName)
      CallArgs.push_back(Env.PerVertexOutputs);
    else
      llvm_unreachable("unexpected parameter for PatchConstantWrapperPass");
  }
  BodyIR.CreateCall(&Body, CallArgs);
  Value *WNext = BodyIR.CreateAdd(W, BodyIR.getInt32(1), "w.next");
  BodyIR.CreateBr(HeaderBB);
  W->addIncoming(WNext, BodyBB);

  IRBuilder<>(ExitBB).CreateRetVoid();
  Body.setLinkage(GlobalValue::InternalLinkage);
  return Wrapper;
}

} // namespace

PreservedAnalyses PatchConstantWrapperPass::run(Module &M,
                                                ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Hull &&
        isPatchConstantPhase(F))
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    Function *Body = appendPatchConstantStageParams(*F);
    if (!lowerPatchConstantStageOps(*Body))
      continue;
    if (buildWrapper(*Body))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

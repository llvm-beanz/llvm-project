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
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

Function *appendPatchConstantStageParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Type *, 12> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, I32Ty});

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
    Value *ControlPoint =
        extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    Value *Addr = computeStageStorageAddress(Builder, LayoutArg, StorageArg,
                                             Elt.ElementID, Elt, Row, Component,
                                             ControlPoint);
    Value *LaneResult = Builder.CreateLoad(ScalarTy, Addr);
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
  Value *Scalar = Elt.SystemValue == SignatureSystemValue::OutputControlPointID
                      ? Builder.getInt32(0)
                  : Elt.SystemValue == SignatureSystemValue::PatchVertices &&
                          Elt.FromInputPatch
                      ? PEnv.InputPatchControlPointCount
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

/// Lowers a `feme.stage.output.store` writing a tessellation factor or patch
/// constant. Storage is per-patch, not per-control-point (see this file's
/// comment): every lane's write always uses invocation index 0, addressing
/// the same single patch record rather than a structure-of-arrays slot of
/// its own.
void lowerPatchConstantOutputStore(CallInst &CI, const SignatureElement &Elt,
                                   const WaveBodyEnv &WEnv,
                                   const PatchConstantStageEnv &PEnv) {
  IRBuilder<> Builder(&CI);
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(3)->getType())->getNumElements();
  Value *InvocationIndex = Builder.getInt32(0);
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(5), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *Addr = computeStageStorageAddress(Builder, PEnv.OutputLayout,
                                             PEnv.Outputs, Elt.ElementID, Elt,
                                             Row, Component, InvocationIndex);
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(LaneVal->getType(), Addr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
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
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::PatchOutput)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-patch-constant: masked output store "
                "references an unknown patch-output signature element");
        return false;
      }
      lowerPatchConstantOutputStore(*CI, *Elt, *WEnv, *PEnv);
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
      // addressable to a *different* control point) and `PatchVertices`
      // (a true per-patch scalar count) are the genuinely non-addressable
      // system values `lowerPatchConstantSystemValue` below handles -- any
      // other system value here (or none at all) is an ordinary array
      // element read the same `InputPatch`-addressed way as any other
      // linked input, keyed by `Elt.ElementID` the same way.
      bool IsScalarSystemValue =
          Elt->SystemValue == SignatureSystemValue::OutputControlPointID ||
          Elt->SystemValue == SignatureSystemValue::PatchVertices;
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
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::PatchOutput);
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-patch-constant: output store refers to an "
                "unknown patch-output signature element");
        return false;
      }
      lowerPatchConstantOutputStore(*CI, *Elt, *WEnv, *PEnv);
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

  std::string WrapperName = getEntrySymbolName(Body.getName());
  Function *Wrapper =
      Function::Create(FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false),
                       GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  IRBuilder<> Entry(EntryBB);
  WrapperEnv Env = buildWrapperEnv(Entry, ArgsTy, Args);

  // A single, non-batched invocation (this file's own comment): one call to
  // the widened body, with only lane 0 marked active -- there is no wave
  // loop over some batch count the way every other stage's wrapper has.
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
    else
      llvm_unreachable("unexpected parameter for PatchConstantWrapperPass");
  }
  Entry.CreateCall(&Body, CallArgs);
  Entry.CreateRetVoid();

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

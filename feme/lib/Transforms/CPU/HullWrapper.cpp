//===- HullWrapper.cpp - CPU target hull control-point wrapper -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap R34's continuation ("Deferred" list in Roadmap.md's R34 entry and
// Patch.h's file comment): compiling a real hull entry point through the CPU
// lowering pipeline into an invokable `feme::cpu::CompiledStage` batch. This
// file lands the *control-point phase* of that work -- the part of a hull
// shader structurally closest to a vertex shader (see the prior session's
// own recommendation in agent_thoughts.md): one invocation per output
// control point, each independently computing that control point's output
// attributes from its own input control point's attributes, with no
// dependency on any sibling control point's result.
//
// This pass is deliberately a close mirror of `feme::cpu::VertexWrapperPass`
// rather than a generalization of it: `FemeStageLayout`-addressed stage
// storage, `feme.stage.input.load`/`output.store` lowering, and the same
// "loop over `<W x T>` waves of independent invocations" wrapper shape,
// batching over output control points (`FemePatchArgs::
// OutputControlPointCount`) instead of vertices. See RuntimeABI.h's
// `FemePatchArgs` comment for the ABI this produces.
//
// Two real hull-shader shapes are deliberately out of scope, and diagnosed
// rather than silently mishandled:
//
//  - **A control-point index other than the invocation's own.** A hull main
//    function's `InputPatch<T, N>` parameter may legally be indexed by any
//    expression, not just `SV_OutputControlPointID` -- but this wrapper
//    always addresses stage storage using the invoking lane's own flat
//    invocation index (exactly like `VertexWrapperPass`'s single-vertex-per-
//    invocation model), so a control point reading a *different* control
//    point's input needs an addressing model this milestone does not build.
//    `lowerHullInputLoad` requires the load's control-point-index operand to
//    be either the invocation's own `OutputControlPointID` value (the
//    common, and structurally required for embarrassingly-parallel
//    per-control-point processing, case) or, when the function never reads
//    that system value at all, the constant `0` -- matching
//    `VertexWrapperPass`'s own precedent for its analogous "vertex" operand
//    -- and is diagnosed otherwise instead of silently reading the wrong
//    control point.
//  - **A group-sync barrier inside the control-point phase.** A control
//    point that must read a *sibling* control point's output (rather than
//    only its own input) needs one after writing its own output and before
//    reading another's -- exactly the barrier `feme::cpu::EntryWrapperPass`
//    already knows how to split a compute wave body around. Generalizing
//    that barrier-region-splitting machinery to this batch-over-control-
//    points ABI (rather than duplicating close to 1000 lines of it) is the
//    right next step, and is left as this milestone's own documented
//    follow-up -- see `lowerHullStageOps`'s barrier check below, which
//    diagnoses any surviving `..._with_group_sync` call rather than
//    dropping it silently.
//
// The patch-constant function -- a second, separate compiled entry that
// receives the *completed* `OutputPatch` this phase produces (already
// requiring the phase to run to completion first, which
// `feme::cpu::CompiledStage::invokePatch` naturally provides simply by
// running this phase's whole wave loop before returning) and writes
// tessellation factors/patch constants -- is now modeled by
// `feme::cpu::PatchConstantWrapperPass` (PatchConstantWrapper.h/.cpp, added
// after this milestone's initial landing). `isPatchConstantPhase`
// (HullPhase.h) is the discriminator that keeps the two wrappers from both
// claiming the same hull-stage function: this pass now skips any candidate
// it identifies as the patch-constant phase, leaving it entirely to that
// pass. That pass's own `InputPatch` parameter deferral (a patch-constant
// function reading the original, pre-control-stage input control points) is
// now closed too, in a further follow-up -- see PatchConstantWrapper.cpp's
// own comment. Still deferred alongside the domain and geometry wrappers:
// generalizing `EntryWrapperPass`'s barrier-region-splitting machinery to
// this batch-over-control-points ABI (this file's own "group-sync barrier"
// bullet above).
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/HullWrapper.h"

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
constexpr StringLiteral OutputLayoutParamName = "stage_output_layout";
constexpr StringLiteral OutputsParamName = "stage_outputs";

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

struct HullStageEnv {
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
};

std::optional<HullStageEnv> getHullStageEnv(Function &F) {
  HullStageEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == InputLayoutParamName)
      Env.InputLayout = &Arg, Found = true;
    else if (Arg.getName() == InputsParamName)
      Env.Inputs = &Arg, Found = true;
    else if (Arg.getName() == OutputLayoutParamName)
      Env.OutputLayout = &Arg, Found = true;
    else if (Arg.getName() == OutputsParamName)
      Env.Outputs = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

Function *appendHullStageParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  SmallVector<Type *, 12> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, PtrTy, PtrTy, PtrTy});

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
  (&*ArgIt++)->setName(OutputLayoutParamName);
  (&*ArgIt++)->setName(OutputsParamName);

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

/// Lowers a `feme.stage.input.load` of the `OutputControlPointID` system
/// value to the invoking lane's own flat invocation index. Also records that
/// index's `<W x i32>` vector so `lowerHullInputLoad` below can recognize a
/// later ordinary attribute load that indexes by it (see the file comment's
/// "control-point index other than the invocation's own" scope note).
Value *lowerOutputControlPointID(CallInst &CI, const WaveBodyEnv &WEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *Index = getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *LaneResult =
        Builder.CreateSelect(Active, Index, Builder.getInt32(0));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers an ordinary (non-system-value) `feme.stage.input.load`, requiring
/// the load's control-point-index operand (`CI`'s 4th argument) to be either
/// \p SelfIndex (the invocation's own `OutputControlPointID`, already
/// lowered by `lowerOutputControlPointID` above and therefore already
/// present at every use by the time this runs) or the constant `0` if the
/// function never reads that system value at all -- see the file comment's
/// scope note. Returns null (having emitted a diagnostic) for any other
/// operand.
Value *lowerHullInputLoad(CallInst &CI, const SignatureElement &Elt,
                          const WaveBodyEnv &WEnv, const HullStageEnv &HEnv,
                          Value *SelfIndex) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);

  Value *ControlPoint = CI.getArgOperand(3);
  bool SelfReference = ControlPoint == SelfIndex;
  auto *ControlPointConst = dyn_cast<ConstantInt>(ControlPoint);
  bool ZeroConstant = ControlPointConst && ControlPointConst->isZero();
  if (!SelfReference && !(ZeroConstant && !SelfIndex)) {
    CI.getContext().emitError(
        &CI, "feme-cpu-wrap-hull: control-point phase only supports a "
             "control point reading its own input control point's "
             "attributes");
    return nullptr;
  }

  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *Addr = computeStageStorageAddress(Builder, HEnv.InputLayout,
                                             HEnv.Inputs, Elt.ElementID, Elt,
                                             Row, Component, InvocationIndex);
    Value *LaneResult = Builder.CreateLoad(ScalarTy, Addr);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

void lowerHullOutputStore(CallInst &CI, const SignatureElement &Elt,
                          const WaveBodyEnv &WEnv, const HullStageEnv &HEnv) {
  IRBuilder<> Builder(&CI);
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(3)->getType())->getNumElements();
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(5), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *Addr = computeStageStorageAddress(Builder, HEnv.OutputLayout,
                                             HEnv.Outputs, Elt.ElementID, Elt,
                                             Row, Component, InvocationIndex);
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(LaneVal->getType(), Addr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

bool lowerHullStageOps(Function &F) {
  // A group-sync barrier needs the same region-splitting machinery
  // `feme::cpu::EntryWrapperPass` already implements for compute; this
  // milestone does not yet generalize it to the control-point batch ABI
  // (see the file comment above), so diagnose rather than silently drop it.
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI)) {
      if (Matched->GroupSync) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-hull: a group-sync barrier inside the "
                "control-point phase is not yet supported");
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
        "feme-cpu-wrap-hull: hull stage wrapper requires attached "
        "feme.signature metadata");
    return false;
  }

  std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(F);
  std::optional<HullStageEnv> HEnv = getHullStageEnv(F);
  if (!WEnv || !HEnv)
    return false;

  // Lower the `OutputControlPointID` input load(s) first (see
  // `lowerHullInputLoad`'s comment): any later ordinary attribute load
  // indexed by that same value already sees the replacement, since a
  // forward pass over `instructions(F)` visits a def before any use that
  // followed it in the source.
  Value *SelfIndex = nullptr;
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    StageOpKind Kind;
    if (!isStageOpCall(*CI, &Kind) || Kind != StageOpKind::InputLoad)
      continue;
    auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    if (!EltID)
      continue;
    const SignatureElement *Elt =
        findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                    SignatureDirection::Input);
    if (!Elt || Elt->SystemValue != SignatureSystemValue::OutputControlPointID)
      continue;
    SelfIndex = lowerOutputControlPointID(*CI, *WEnv);
    CI->replaceAllUsesWith(SelfIndex);
    CI->eraseFromParent();
  }

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (isMaskedOutputStoreCall(*CI)) {
      auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::Output)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-hull: masked output store references an "
                "unknown signature element");
        return false;
      }
      lowerHullOutputStore(*CI, *Elt, *WEnv, *HEnv);
      CI->eraseFromParent();
      continue;
    }

    StageOpKind Kind;
    if (!isStageOpCall(*CI, &Kind))
      continue;
    auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    if (!EltID) {
      F.getContext().emitError(
          CI, "feme-cpu-wrap-hull: stage IO requires a constant element ID");
      return false;
    }

    switch (Kind) {
    case StageOpKind::InputLoad: {
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::Input);
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-hull: input load refers to an unknown "
                "signature element");
        return false;
      }
      Value *Lowered = lowerHullInputLoad(*CI, *Elt, *WEnv, *HEnv, SelfIndex);
      if (!Lowered)
        return false;
      CI->replaceAllUsesWith(Lowered);
      CI->eraseFromParent();
      break;
    }
    default:
      F.getContext().emitError(
          CI, "feme-cpu-wrap-hull: unexpected stage op left for the hull "
              "wrapper");
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
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *OutputControlPointCount = nullptr;
};

WrapperEnv buildWrapperEnv(IRBuilder<> &Builder, StructType *ArgsTy,
                           Value *Args) {
  LLVMContext &Ctx = Builder.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Builder.getInt32Ty();
  WrapperEnv Env;
  Env.OutputControlPointCount = loadStructField(
      Builder, ArgsTy, Args, PatchArgsFieldOutputControlPointCount, I32Ty);
  Env.InputLayout =
      loadStructField(Builder, ArgsTy, Args, PatchArgsFieldInputLayout, PtrTy);
  Env.Inputs =
      loadStructField(Builder, ArgsTy, Args, PatchArgsFieldInputs, PtrTy);
  Env.OutputLayout =
      loadStructField(Builder, ArgsTy, Args, PatchArgsFieldOutputLayout, PtrTy);
  Env.Outputs =
      loadStructField(Builder, ArgsTy, Args, PatchArgsFieldOutputs, PtrTy);

  Value *ResourcesRaw =
      loadStructField(Builder, ArgsTy, Args, PatchArgsFieldResources, PtrTy);
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

  StructType *ArgsTy = getPatchArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);

  std::string WrapperName = getEntrySymbolName(Body.getName());
  Function *Wrapper =
      Function::Create(FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false),
                       GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

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
    else if (Arg.getName() == OutputLayoutParamName)
      CallArgs.push_back(Env.OutputLayout);
    else if (Arg.getName() == OutputsParamName)
      CallArgs.push_back(Env.Outputs);
    else
      llvm_unreachable("unexpected parameter for HullWrapperPass");
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

PreservedAnalyses HullWrapperPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Hull &&
        !isPatchConstantPhase(F))
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    Function *Body = appendHullStageParams(*F);
    if (!lowerHullStageOps(*Body))
      continue;
    if (buildWrapper(*Body))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

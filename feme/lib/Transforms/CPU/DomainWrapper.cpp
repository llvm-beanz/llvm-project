//===- DomainWrapper.cpp - CPU target domain stage wrapper ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap R34's continuation, closing its "domain wrapper" open item (see
// HullWrapper.cpp's file comment and agent_thoughts.md's prior session):
// compiling a real domain (evaluation) entry point through the CPU lowering
// pipeline into an invokable `feme::cpu::CompiledStage` batch.
//
// A domain shader is, in its wrapper shape, a vertex shader: one independent
// invocation per tessellator-generated domain point, each producing one
// vertex, batched over `FemeDomainArgs::DomainPointCount` in waves of
// `<W x T>` exactly the way `feme::cpu::VertexWrapperPass` batches vertices.
// What is new is that three distinct input sources meet in one entry point,
// and the pass routes each `feme.stage.input.load` to the right one by the
// signature element it names:
//
//  - **The completed patch's control points** (`SignatureDirection::Input`,
//    no system value): structure-of-arrays storage indexed by control point,
//    read at *any* index in `[0, OutputControlPointCount)` -- evaluating a
//    patch means blending its control points, so this pass has no
//    "self-indexing" restriction of the kind `HullWrapperPass` needs, and
//    addresses `FemeDomainArgs::Inputs` the same way
//    `PatchConstantWrapperPass` addresses its own `OutputPatch` block.
//  - **The patch's tessellation factors and patch constants**
//    (`SignatureDirection::PatchInput`): per-patch, not per-invocation, so
//    `lowerDomainPatchConstantLoad` always addresses
//    `FemeDomainArgs::PatchConstants` with invocation index 0 -- the mirror
//    image of `PatchConstantWrapperPass`'s own unbatched *output* store.
//  - **`SV_DomainLocation`** (`SignatureSystemValue::DomainLocation`): the
//    invocation's own generated (u, v, w) coordinate, read from
//    `FemeDomainInvocation::DomainLocation` rather than from any stage
//    storage block, exactly as `VertexWrapperPass` reads `SV_VertexID` from
//    its own per-invocation record.
//
// Outputs are ordinary per-vertex outputs, addressed per invocation just
// like a vertex batch's: a domain shader's result *is* a vertex.
//
// Deliberately out of scope, and diagnosed rather than silently
// mishandled:
//
//  - **A dynamically indexed `SV_DomainLocation` component.** The domain
//    coordinate is a 2- or 3-component system value read component by
//    component; `lowerDomainLocation` requires the load's component operand
//    to be a constant naming one of the record's three components, rather
//    than emitting an unbounded dynamic index into a fixed-size ABI record.
//  - **A group-sync barrier.** Domain invocations are independent (there is
//    no groupshared cooperation model for this stage), so a surviving
//    `..._with_group_sync` call means a shape this wrapper's
//    invocation-per-domain-point model does not implement; it is diagnosed
//    for the same reason `HullWrapperPass` diagnoses its own.
//
// Still deferred alongside this file, documented in HullWrapper.cpp: the
// geometry wrapper (`CompiledStage::invokeGeometry` does not exist),
// generalizing `EntryWrapperPass`'s barrier-region splitting to the
// control-point batch ABI, and wiring any compiled tessellation stage into
// `executeDraws`/`feme-render`.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/DomainWrapper.h"

#include "BarrierCalls.h"
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
constexpr StringLiteral PatchConstantLayoutParamName =
    "stage_patch_constant_layout";
constexpr StringLiteral PatchConstantsParamName = "stage_patch_constants";
constexpr StringLiteral OutputLayoutParamName = "stage_output_layout";
constexpr StringLiteral OutputsParamName = "stage_outputs";
constexpr StringLiteral InvocationsParamName = "stage_domain_invocations";
constexpr StringLiteral OutputControlPointCountParamName =
    "stage_output_control_point_count";

/// The number of components `FemeDomainInvocation::DomainLocation` holds.
constexpr unsigned DomainLocationComponentCount = 3;

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

/// Finds the element a `feme.stage.input.load` names, which for this stage
/// may be either a per-control-point patch input or a per-patch constant.
const SignatureElement *findInputElement(const EntrySignature &Sig,
                                         uint32_t ElementID) {
  if (const SignatureElement *Elt =
          findElement(Sig, ElementID, SignatureDirection::Input))
    return Elt;
  return findElement(Sig, ElementID, SignatureDirection::PatchInput);
}

struct DomainStageEnv {
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *PatchConstantLayout = nullptr;
  Value *PatchConstants = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *Invocations = nullptr;
  Value *OutputControlPointCount = nullptr;
};

std::optional<DomainStageEnv> getDomainStageEnv(Function &F) {
  DomainStageEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == InputLayoutParamName)
      Env.InputLayout = &Arg, Found = true;
    else if (Arg.getName() == InputsParamName)
      Env.Inputs = &Arg, Found = true;
    else if (Arg.getName() == PatchConstantLayoutParamName)
      Env.PatchConstantLayout = &Arg, Found = true;
    else if (Arg.getName() == PatchConstantsParamName)
      Env.PatchConstants = &Arg, Found = true;
    else if (Arg.getName() == OutputLayoutParamName)
      Env.OutputLayout = &Arg, Found = true;
    else if (Arg.getName() == OutputsParamName)
      Env.Outputs = &Arg, Found = true;
    else if (Arg.getName() == InvocationsParamName)
      Env.Invocations = &Arg, Found = true;
    else if (Arg.getName() == OutputControlPointCountParamName)
      Env.OutputControlPointCount = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

Function *appendDomainStageParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Type *, 16> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, I32Ty});

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
  (&*ArgIt++)->setName(PatchConstantLayoutParamName);
  (&*ArgIt++)->setName(PatchConstantsParamName);
  (&*ArgIt++)->setName(OutputLayoutParamName);
  (&*ArgIt++)->setName(OutputsParamName);
  (&*ArgIt++)->setName(InvocationsParamName);
  (&*ArgIt++)->setName(OutputControlPointCountParamName);

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

/// The constant \p Lane's component of \p V, for an operand a widened wave
/// body may present either as a scalar or as a lane-wise vector: the
/// domain-location record is a fixed-size ABI struct, so the component
/// selecting one of its fields has to be resolvable at compile time (see the
/// file comment's scope note).
std::optional<uint64_t> getLaneConstantInt(Value *V, unsigned Lane) {
  if (auto *ScalarConst = dyn_cast<ConstantInt>(V))
    return ScalarConst->getZExtValue();
  auto *C = dyn_cast<Constant>(V);
  if (!C || !isa<FixedVectorType>(V->getType()))
    return std::nullopt;
  auto *LaneConst = dyn_cast_or_null<ConstantInt>(C->getAggregateElement(Lane));
  if (!LaneConst)
    return std::nullopt;
  return LaneConst->getZExtValue();
}

/// Lowers a `feme.stage.input.load` of the `DomainLocation` system value to
/// a read of this invocation's own `FemeDomainInvocation` record. Requires a
/// constant component operand naming one of the record's components (see the
/// file comment's scope note); returns null, having emitted a diagnostic,
/// otherwise.
Value *lowerDomainLocation(CallInst &CI, const SignatureElement &Elt,
                           const WaveBodyEnv &WEnv,
                           const DomainStageEnv &DEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  LLVMContext &Ctx = CI.getContext();
  IRBuilder<> Builder(&CI);

  StructType *InvocationTy = getDomainInvocationType(Ctx);
  Type *CoordTy = Type::getFloatTy(Ctx);
  Value *InvocationBase =
      Builder.CreateBitCast(DEnv.Invocations, PointerType::get(Ctx, 0));
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    std::optional<uint64_t> RawComponent =
        getLaneConstantInt(CI.getArgOperand(2), Lane);
    if (!RawComponent || *RawComponent < Elt.FirstComponent ||
        *RawComponent - Elt.FirstComponent >= DomainLocationComponentCount) {
      Ctx.emitError(&CI,
                    "feme-cpu-wrap-domain: the domain location's component "
                    "operand must be a constant naming one of its three "
                    "components");
      return nullptr;
    }
    unsigned Component =
        static_cast<unsigned>(*RawComponent - Elt.FirstComponent);

    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *InvocationPtr = Builder.CreateInBoundsGEP(
        InvocationTy, InvocationBase, InvocationIndex);
    Value *CoordsPtr = Builder.CreateStructGEP(
        InvocationTy, InvocationPtr, DomainInvocationFieldDomainLocation);
    Value *CoordPtr = Builder.CreateInBoundsGEP(
        ArrayType::get(CoordTy, DomainLocationComponentCount), CoordsPtr,
        {Builder.getInt32(0), Builder.getInt32(Component)});
    Value *LaneResult = Builder.CreateLoad(ScalarTy, CoordPtr);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers an ordinary `feme.stage.input.load` reading the completed patch's
/// control points. The control-point-index operand (`CI`'s 4th argument) may
/// be any value: evaluating a patch means blending its control points (see
/// the file comment).
Value *lowerDomainControlPointLoad(CallInst &CI, const SignatureElement &Elt,
                                   const WaveBodyEnv &WEnv,
                                   const DomainStageEnv &DEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);

  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *ControlPoint =
        extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    Value *Addr = computeStageStorageAddress(Builder, DEnv.InputLayout,
                                             DEnv.Inputs, Elt.ElementID, Elt,
                                             Row, Component, ControlPoint);
    Value *LaneResult = Builder.CreateLoad(ScalarTy, Addr);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers a `feme.stage.input.load` of a tessellation factor or patch
/// constant. That storage is per-patch, not per-invocation (see the file
/// comment): every lane reads invocation index 0.
Value *lowerDomainPatchConstantLoad(CallInst &CI, const SignatureElement &Elt,
                                    const WaveBodyEnv &WEnv,
                                    const DomainStageEnv &DEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);
  Value *InvocationIndex = Builder.getInt32(0);

  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *Addr = computeStageStorageAddress(
        Builder, DEnv.PatchConstantLayout, DEnv.PatchConstants, Elt.ElementID,
        Elt, Row, Component, InvocationIndex);
    Value *LaneResult = Builder.CreateLoad(ScalarTy, Addr);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

Value *lowerDomainPatchVertices(CallInst &CI, const WaveBodyEnv &WEnv,
                                const DomainStageEnv &DEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *LaneResult = Builder.CreateSelect(
        Active, DEnv.OutputControlPointCount, Builder.getInt32(0));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

void lowerDomainOutputStore(CallInst &CI, const SignatureElement &Elt,
                            const WaveBodyEnv &WEnv,
                            const DomainStageEnv &DEnv) {
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
    Value *Addr = computeStageStorageAddress(Builder, DEnv.OutputLayout,
                                             DEnv.Outputs, Elt.ElementID, Elt,
                                             Row, Component, InvocationIndex);
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(LaneVal->getType(), Addr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

/// Lowers one `feme.stage.input.load` to whichever of this stage's three
/// input sources \p Elt names (see the file comment).
Value *lowerDomainInputLoad(CallInst &CI, const SignatureElement &Elt,
                            const WaveBodyEnv &WEnv,
                            const DomainStageEnv &DEnv) {
  if (Elt.Direction == SignatureDirection::PatchInput)
    return lowerDomainPatchConstantLoad(CI, Elt, WEnv, DEnv);
  switch (Elt.SystemValue) {
  case SignatureSystemValue::None:
    return lowerDomainControlPointLoad(CI, Elt, WEnv, DEnv);
  case SignatureSystemValue::DomainLocation:
    return lowerDomainLocation(CI, Elt, WEnv, DEnv);
  case SignatureSystemValue::PatchVertices:
    return lowerDomainPatchVertices(CI, WEnv, DEnv);
  default:
    CI.getContext().emitError(
        &CI, "feme-cpu-wrap-domain: unsupported domain system value");
    return nullptr;
  }
}

bool lowerDomainStageOps(Function &F) {
  // Domain invocations are independent, so a surviving group-sync barrier
  // means a shape this wrapper does not implement (see the file comment).
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI)) {
      if (Matched->GroupSync) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-domain: a group-sync barrier is not supported "
                "in the domain stage's independent per-domain-point "
                "invocations");
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
        "feme-cpu-wrap-domain: domain stage wrapper requires attached "
        "feme.signature metadata");
    return false;
  }

  std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(F);
  std::optional<DomainStageEnv> DEnv = getDomainStageEnv(F);
  if (!WEnv || !DEnv)
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
                            SignatureDirection::Output)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-domain: masked output store references an "
                "unknown signature element");
        return false;
      }
      lowerDomainOutputStore(*CI, *Elt, *WEnv, *DEnv);
      CI->eraseFromParent();
      continue;
    }

    StageOpKind Kind;
    if (!isStageOpCall(*CI, &Kind))
      continue;
    auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    if (!EltID) {
      F.getContext().emitError(
          CI, "feme-cpu-wrap-domain: stage IO requires a constant element ID");
      return false;
    }

    switch (Kind) {
    case StageOpKind::InputLoad: {
      const SignatureElement *Elt =
          findInputElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()));
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-domain: input load refers to an unknown "
                "signature element");
        return false;
      }
      Value *Lowered = lowerDomainInputLoad(*CI, *Elt, *WEnv, *DEnv);
      if (!Lowered)
        return false;
      CI->replaceAllUsesWith(Lowered);
      CI->eraseFromParent();
      break;
    }
    case StageOpKind::OutputStore: {
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::Output);
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-domain: output store refers to an unknown "
                "signature element");
        return false;
      }
      lowerDomainOutputStore(*CI, *Elt, *WEnv, *DEnv);
      CI->eraseFromParent();
      break;
    }
    default:
      F.getContext().emitError(
          CI, "feme-cpu-wrap-domain: unexpected stage op left for the domain "
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
  Value *PatchConstantLayout = nullptr;
  Value *PatchConstants = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *Invocations = nullptr;
  Value *DomainPointCount = nullptr;
  Value *OutputControlPointCount = nullptr;
};

WrapperEnv buildWrapperEnv(IRBuilder<> &Builder, StructType *ArgsTy,
                           Value *Args) {
  LLVMContext &Ctx = Builder.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Builder.getInt32Ty();
  WrapperEnv Env;
  Env.DomainPointCount = loadStructField(
      Builder, ArgsTy, Args, DomainArgsFieldDomainPointCount, I32Ty);
  Env.OutputControlPointCount = loadStructField(
      Builder, ArgsTy, Args, DomainArgsFieldOutputControlPointCount, I32Ty);
  Env.InputLayout =
      loadStructField(Builder, ArgsTy, Args, DomainArgsFieldInputLayout, PtrTy);
  Env.Inputs =
      loadStructField(Builder, ArgsTy, Args, DomainArgsFieldInputs, PtrTy);
  Env.PatchConstantLayout = loadStructField(
      Builder, ArgsTy, Args, DomainArgsFieldPatchConstantLayout, PtrTy);
  Env.PatchConstants = loadStructField(Builder, ArgsTy, Args,
                                       DomainArgsFieldPatchConstants, PtrTy);
  Env.OutputLayout = loadStructField(Builder, ArgsTy, Args,
                                     DomainArgsFieldOutputLayout, PtrTy);
  Env.Outputs =
      loadStructField(Builder, ArgsTy, Args, DomainArgsFieldOutputs, PtrTy);
  Env.Invocations =
      loadStructField(Builder, ArgsTy, Args, DomainArgsFieldInvocations, PtrTy);

  Value *ResourcesRaw =
      loadStructField(Builder, ArgsTy, Args, DomainArgsFieldResources, PtrTy);
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

  StructType *ArgsTy = getDomainArgsType(Ctx);
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
  Value *Waves = Entry.CreateUDiv(
      Entry.CreateAdd(Env.DomainPointCount, Entry.getInt32(WaveSize - 1)),
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
  Value *WideCount = BodyIR.CreateVectorSplat(WaveSize, Env.DomainPointCount);
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
    else if (Arg.getName() == PatchConstantLayoutParamName)
      CallArgs.push_back(Env.PatchConstantLayout);
    else if (Arg.getName() == PatchConstantsParamName)
      CallArgs.push_back(Env.PatchConstants);
    else if (Arg.getName() == OutputLayoutParamName)
      CallArgs.push_back(Env.OutputLayout);
    else if (Arg.getName() == OutputsParamName)
      CallArgs.push_back(Env.Outputs);
    else if (Arg.getName() == InvocationsParamName)
      CallArgs.push_back(Env.Invocations);
    else if (Arg.getName() == OutputControlPointCountParamName)
      CallArgs.push_back(Env.OutputControlPointCount);
    else
      llvm_unreachable("unexpected parameter for DomainWrapperPass");
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

PreservedAnalyses DomainWrapperPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Domain)
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    Function *Body = appendDomainStageParams(*F);
    if (!lowerDomainStageOps(*Body))
      continue;
    if (buildWrapper(*Body))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

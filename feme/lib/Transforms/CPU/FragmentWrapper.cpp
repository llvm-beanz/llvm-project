//===- FragmentWrapper.cpp - CPU target fragment entry wrapper ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/FragmentWrapper.h"

#include "StageArgsLayout.h"
#include "StageMaskCalls.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Target/CPU/RuntimeABI.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/ImageCalls.h"
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
constexpr StringLiteral InvocationsParamName = "stage_fragment_invocations";
constexpr StringLiteral ResultsParamName = "stage_fragment_results";

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

struct FragmentStageEnv {
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *Invocations = nullptr;
  Value *Results = nullptr;
};

std::optional<FragmentStageEnv> getFragmentStageEnv(Function &F) {
  FragmentStageEnv Env;
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
    else if (Arg.getName() == InvocationsParamName)
      Env.Invocations = &Arg, Found = true;
    else if (Arg.getName() == ResultsParamName)
      Env.Results = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

Function *appendFragmentStageParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  SmallVector<Type *, 12> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, PtrTy});

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
  (&*ArgIt++)->setName(InvocationsParamName);
  (&*ArgIt++)->setName(ResultsParamName);

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
  Value *TypedElements =
      Builder.CreateBitCast(ElementsRaw, PointerType::get(Ctx, 0));
  Value *EntryPtr = Builder.CreateInBoundsGEP(ElementTy, TypedElements,
                                              Builder.getInt32(ElementID));
  Value *FieldPtr = Builder.CreateStructGEP(ElementTy, EntryPtr, Field);
  return Builder.CreateLoad(FieldTy, FieldPtr);
}

Value *computeStageStorageAddress(IRBuilder<> &Builder, Value *LayoutArg,
                                  Value *BasePtr, unsigned ElementID,
                                  const SignatureElement &Elt, Value *Row,
                                  Value *Component, Value *InvocationIndex) {
  Type *I64Ty = Builder.getInt64Ty();
  Type *I32Ty = Builder.getInt32Ty();
  Value *DataOffset = loadLayoutField(Builder, LayoutArg, ElementID,
                                      StageElementFieldDataOffset, I64Ty);
  Value *RowStride = loadLayoutField(Builder, LayoutArg, ElementID,
                                     StageElementFieldRowStride, I32Ty);
  Value *ComponentStride = loadLayoutField(
      Builder, LayoutArg, ElementID, StageElementFieldComponentStride, I32Ty);
  Value *InvocationStride = loadLayoutField(
      Builder, LayoutArg, ElementID, StageElementFieldInvocationStride, I32Ty);
  Value *RelComponent =
      Builder.CreateSub(Component, Builder.getInt32(Elt.FirstComponent));
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
  Value *Bytes =
      Builder.CreateBitCast(BasePtr, PointerType::get(Builder.getContext(), 0));
  return Builder.CreateInBoundsGEP(Builder.getInt8Ty(), Bytes, ByteOffset);
}

Value *getFragmentInvocationPtr(IRBuilder<> &Builder, Value *InvocationsArg,
                                Value *InvocationIndex) {
  StructType *InvocationTy = getFragmentInvocationType(Builder.getContext());
  Value *Base = Builder.CreateBitCast(
      InvocationsArg, PointerType::get(Builder.getContext(), 0));
  Value *QuadIndex = Builder.CreateUDiv(InvocationIndex, Builder.getInt32(4));
  return Builder.CreateInBoundsGEP(InvocationTy, Base, QuadIndex);
}

Value *loadFragmentSystemValue(IRBuilder<> &Builder,
                               const SignatureElement &Elt,
                               Value *InvocationsArg, Value *InvocationIndex,
                               unsigned Lane) {
  StructType *InvocationTy = getFragmentInvocationType(Builder.getContext());
  Value *InvocationPtr =
      getFragmentInvocationPtr(Builder, InvocationsArg, InvocationIndex);
  unsigned QuadLane = Lane & 3u;
  switch (Elt.SystemValue) {
  case SignatureSystemValue::Position: {
    Value *PosPtr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                            FragmentInvocationFieldPosition);
    Value *LanePtr = Builder.CreateInBoundsGEP(
        cast<ArrayType>(
            InvocationTy->getElementType(FragmentInvocationFieldPosition)),
        PosPtr, {Builder.getInt32(0), Builder.getInt32(QuadLane)});
    auto *LaneTy =
        cast<ArrayType>(cast<ArrayType>(InvocationTy->getElementType(
                                            FragmentInvocationFieldPosition))
                            ->getElementType());
    Value *ComponentPtr = Builder.CreateInBoundsGEP(
        LaneTy, LanePtr,
        {Builder.getInt32(0), Builder.getInt32(Elt.FirstComponent)});
    return Builder.CreateLoad(Builder.getFloatTy(), ComponentPtr);
  }
  case SignatureSystemValue::PrimitiveID: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         FragmentInvocationFieldPrimitiveID);
    Value *LanePtr = Builder.CreateInBoundsGEP(
        ArrayType::get(Builder.getInt32Ty(), 4), Ptr,
        {Builder.getInt32(0), Builder.getInt32(QuadLane)});
    return Builder.CreateLoad(Builder.getInt32Ty(), LanePtr);
  }
  case SignatureSystemValue::SampleIndex: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         FragmentInvocationFieldSampleIndex);
    Value *LanePtr = Builder.CreateInBoundsGEP(
        ArrayType::get(Builder.getInt32Ty(), 4), Ptr,
        {Builder.getInt32(0), Builder.getInt32(QuadLane)});
    return Builder.CreateLoad(Builder.getInt32Ty(), LanePtr);
  }
  case SignatureSystemValue::Coverage: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         FragmentInvocationFieldCoverage);
    Value *LanePtr = Builder.CreateInBoundsGEP(
        ArrayType::get(Builder.getInt32Ty(), 4), Ptr,
        {Builder.getInt32(0), Builder.getInt32(QuadLane)});
    return Builder.CreateLoad(Builder.getInt32Ty(), LanePtr);
  }
  case SignatureSystemValue::IsFrontFace: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         FragmentInvocationFieldIsFrontFace);
    Value *LanePtr = Builder.CreateInBoundsGEP(
        ArrayType::get(Builder.getInt32Ty(), 4), Ptr,
        {Builder.getInt32(0), Builder.getInt32(QuadLane)});
    return Builder.CreateLoad(Builder.getInt32Ty(), LanePtr);
  }
  case SignatureSystemValue::ViewIndex: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         FragmentInvocationFieldViewIndex);
    return Builder.CreateLoad(Builder.getInt32Ty(), Ptr);
  }
  case SignatureSystemValue::ViewportArrayIndex: {
    Value *Ptr = Builder.CreateStructGEP(
        InvocationTy, InvocationPtr, FragmentInvocationFieldViewportIndex);
    Value *LanePtr = Builder.CreateInBoundsGEP(
        ArrayType::get(Builder.getInt32Ty(), 4), Ptr,
        {Builder.getInt32(0), Builder.getInt32(QuadLane)});
    return Builder.CreateLoad(Builder.getInt32Ty(), LanePtr);
  }
  default:
    Builder.getContext().emitError(
        Twine("feme-cpu-wrap-fragment: unsupported fragment system value for "
              "element ") +
        Twine(Elt.ElementID));
    return UndefValue::get(Builder.getInt32Ty());
  }
}

Value *lowerFragmentInputLoad(CallInst &CI, const SignatureElement &Elt,
                              const WaveBodyEnv &WEnv,
                              const FragmentStageEnv &FEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *LaneResult = Constant::getNullValue(ScalarTy);
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    if (Elt.SystemValue != SignatureSystemValue::None) {
      // (roadmap H7d) A `Position`/`FragCoord` element's `Elt` always
      // describes the *whole* `vec4` builtin (`Elt.FirstComponent == 0`,
      // `Elt.ComponentCount == 4`) -- unlike an ordinary varying, whose
      // per-call "which component does *this* load want" is threaded
      // through `CI`'s own `Component` operand (see the `else` branch's
      // `computeStageStorageAddress` call), not baked into `Elt` itself.
      // `loadFragmentSystemValue`'s `Position` case indexes by
      // `Elt.FirstComponent`, so that operand must be resolved here and
      // substituted in, or every system-value vector component read
      // (`gl_FragCoord.z`, `.w`, ...) would silently collapse to
      // `Elt.FirstComponent`'s default of `0` (`.x`) instead -- exactly
      // what a real GPU's own `gl_FragCoord.z` consumer (e.g.
      // `dEQP-VK.clipping.clip_volume.depth_clamp.*`) surfaced first,
      // since nothing before read any component but `.x`/`.y` (see
      // `loadFragmentPositionComponent`'s own pre-existing, unaffected
      // single-purpose helper below) of a fragment system value.
      Value *RequestedComponent =
          extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
      auto *ComponentConst = dyn_cast<ConstantInt>(RequestedComponent);
      if (!ComponentConst) {
        CI.getContext().emitError(
            &CI, "feme-cpu-wrap-fragment: fragment system-value component "
                 "must be constant");
        return nullptr;
      }
      SignatureElement ResolvedElt = Elt;
      ResolvedElt.FirstComponent =
          static_cast<uint32_t>(ComponentConst->getZExtValue()) -
          Elt.FirstComponent;
      LaneResult = loadFragmentSystemValue(Builder, ResolvedElt,
                                           FEnv.Invocations, InvocationIndex,
                                           Lane);
    } else {
      Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
      Value *Component =
          extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
      Value *Vertex = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
      auto *VertexConst = dyn_cast<ConstantInt>(Vertex);
      if (!VertexConst || VertexConst->getZExtValue() != 0) {
        CI.getContext().emitError(
            &CI,
            "feme-cpu-wrap-fragment: synthetic fragment layouts only support "
            "vertex operand 0");
        return nullptr;
      }
      Value *Addr = computeStageStorageAddress(Builder, FEnv.InputLayout,
                                               FEnv.Inputs, Elt.ElementID, Elt,
                                               Row, Component, InvocationIndex);
      Value *TypedPtr = Builder.CreateBitCast(
          Addr, PointerType::get(Builder.getContext(), 0));
      LaneResult = Builder.CreateLoad(ScalarTy, TypedPtr);
    }
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Reads scalar fragment-invocation `Position` component \p Component
/// (0 for X, 1 for Y) for \p Lane, by building a throwaway `SignatureElement`
/// with `SystemValue = Position` -- `loadFragmentSystemValue`'s existing
/// `Position` case reads exactly `Elt.FirstComponent`, so this needs no
/// dedicated helper of its own.
Value *loadFragmentPositionComponent(IRBuilder<> &Builder,
                                     Value *InvocationsArg,
                                     Value *InvocationIndex, unsigned Lane,
                                     uint32_t Component) {
  SignatureElement PositionElt;
  PositionElt.SystemValue = SignatureSystemValue::Position;
  PositionElt.FirstComponent = Component;
  return loadFragmentSystemValue(Builder, PositionElt, InvocationsArg,
                                 InvocationIndex, Lane);
}

/// Roadmap F8a: lowers `feme.stage.subpass.load(attachment_index,
/// component, sample)` (see `feme::StageOpKind::SubpassLoad`) into a
/// `feme.cpu.image.load.2d.v4f32` call against \p F's own
/// `subpass_input_heap`/`subpass_input_heap_count` parameters (added by
/// `feme::cpu::SPIRVSubpassLoweringPass`, surviving `feme::cpu::SIMDizePass`
/// widening unchanged, exactly like `image_heap`), at the invocation's own
/// fragment location -- `subpassLoad`'s coordinate is always relative to the
/// current fragment (see SPIRVToLLVMPatterns.cpp's `SubpassLoadPattern`,
/// which never threads a coordinate operand through at all), truncated
/// towards zero: `FemeFragmentInvocation::Position` is always a pixel
/// center (`x + 0.5`, `y + 0.5`), so simple truncation recovers the integer
/// texel address, matching every other fragment-position-derived texel
/// address in this file. Mip is always 0 (a render-target attachment has no
/// mip chain of its own); \p CI's own `sample` operand (roadmap F8c) is
/// threaded straight through to `createLoad2D`'s own `Sample` parameter,
/// rather than the constant 0 every other caller of it still passes,
/// since this is the one caller that can genuinely address another sample
/// of a multisampled attachment. The returned `<4 x float>` texel is
/// narrowed to the requested component -- mirroring
/// `lowerFragmentInputLoad`'s own per-lane, masked-select shape above.
Value *lowerFragmentSubpassLoad(CallInst &CI, Function &F,
                                const WaveBodyEnv &WEnv,
                                const FragmentStageEnv &FEnv) {
  Value *SubpassInputHeap = nullptr;
  Value *SubpassInputHeapCount = nullptr;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == "subpass_input_heap")
      SubpassInputHeap = &Arg;
    else if (Arg.getName() == "subpass_input_heap_count")
      SubpassInputHeapCount = &Arg;
  }
  if (!SubpassInputHeap || !SubpassInputHeapCount) {
    F.getContext().emitError(
        &CI, "feme-cpu-wrap-fragment: subpass load with no subpass input "
             "heap parameter (feme::cpu::SPIRVSubpassLoweringPass did not "
             "run?)");
    return nullptr;
  }

  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *AttachmentIndex =
        extractLaneOrScalar(Builder, CI.getArgOperand(0), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Sample = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *PosX = loadFragmentPositionComponent(Builder, FEnv.Invocations,
                                                InvocationIndex, Lane, 0);
    Value *PosY = loadFragmentPositionComponent(Builder, FEnv.Invocations,
                                                InvocationIndex, Lane, 1);
    Value *X = Builder.CreateFPToSI(PosX, Builder.getInt32Ty());
    Value *Y = Builder.CreateFPToSI(PosY, Builder.getInt32Ty());
    feme::cpu::ImageCallEnv ImgEnv;
    ImgEnv.ImageHeap = SubpassInputHeap;
    ImgEnv.ImageHeapCount = SubpassInputHeapCount;
    CallInst *Texel = feme::cpu::createLoad2D(
        Builder, ImgEnv, AttachmentIndex, X, Y, Builder.getInt32(0), Sample,
        Active);
    auto *IndexConst = dyn_cast<ConstantInt>(Component);
    Value *LaneResult =
        IndexConst
            ? Builder.CreateExtractElement(Texel, IndexConst->getZExtValue())
            : Builder.CreateExtractElement(Texel, Component);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(LaneResult->getType()));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

void lowerFragmentOutputStore(CallInst &CI, const SignatureElement &Elt,
                              const WaveBodyEnv &WEnv,
                              const FragmentStageEnv &FEnv) {
  IRBuilder<> Builder(&CI);
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(3)->getType())->getNumElements();
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(5), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *Vertex = extractLaneOrScalar(Builder, CI.getArgOperand(4), Lane);
    auto *VertexConst = dyn_cast<ConstantInt>(Vertex);
    if (!VertexConst || VertexConst->getZExtValue() != 0) {
      CI.getContext().emitError(
          &CI,
          "feme-cpu-wrap-fragment: synthetic fragment layouts only support "
          "vertex operand 0");
      return;
    }

    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *Addr = computeStageStorageAddress(Builder, FEnv.OutputLayout,
                                             FEnv.Outputs, Elt.ElementID, Elt,
                                             Row, Component, InvocationIndex);
    Value *TypedPtr =
        Builder.CreateBitCast(Addr, PointerType::get(Builder.getContext(), 0));
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(LaneVal->getType(), TypedPtr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, TypedPtr);
  }
}

void lowerReturnMasks(CallInst &CI, const WaveBodyEnv &WEnv,
                      const FragmentStageEnv &FEnv) {
  IRBuilder<> Builder(&CI);
  StructType *ResultTy = getFragmentResultType(Builder.getContext());
  Value *Results = Builder.CreateBitCast(
      FEnv.Results, PointerType::get(Builder.getContext(), 0));
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(0)->getType())->getNumElements();
  unsigned QuadsPerWave = WaveSize / 4;
  for (unsigned Quad = 0; Quad != QuadsPerWave; ++Quad) {
    Value *LiveBits = Builder.getInt32(0);
    Value *SideBits = Builder.getInt32(0);
    for (unsigned Lane = 0; Lane != 4; ++Lane) {
      unsigned WaveLane = Quad * 4 + Lane;
      Value *Live = Builder.CreateExtractElement(CI.getArgOperand(0),
                                                 Builder.getInt32(WaveLane));
      Value *Side = Builder.CreateExtractElement(CI.getArgOperand(1),
                                                 Builder.getInt32(WaveLane));
      LiveBits = Builder.CreateOr(
          LiveBits,
          Builder.CreateShl(Builder.CreateZExt(Live, Builder.getInt32Ty()),
                            Builder.getInt32(Lane)));
      SideBits = Builder.CreateOr(
          SideBits,
          Builder.CreateShl(Builder.CreateZExt(Side, Builder.getInt32Ty()),
                            Builder.getInt32(Lane)));
    }
    Value *QuadIndex = Builder.CreateAdd(
        Builder.CreateMul(WEnv.WaveIndex, Builder.getInt32(QuadsPerWave)),
        Builder.getInt32(Quad));
    Value *ResultPtr = Builder.CreateInBoundsGEP(ResultTy, Results, QuadIndex);
    Value *LivePtr = Builder.CreateStructGEP(ResultTy, ResultPtr,
                                             FragmentResultFieldLiveMask);
    Builder.CreateStore(LiveBits, LivePtr);
    Value *SidePtr = Builder.CreateStructGEP(ResultTy, ResultPtr,
                                             FragmentResultFieldSideEffectMask);
    Builder.CreateStore(SideBits, SidePtr);
  }
}

bool lowerFragmentStageOps(Function &F) {
  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(F);
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedOutputStoreCall(*CI) ||
                      isReturnMasksCall(*CI);
  if (!UsesStageOps)
    return true;
  if (!Sig) {
    F.getContext().emitError(
        "feme-cpu-wrap-fragment: fragment stage wrapper requires attached "
        "feme.signature metadata");
    return false;
  }

  std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(F);
  std::optional<FragmentStageEnv> FEnv = getFragmentStageEnv(F);
  if (!WEnv || !FEnv)
    return false;

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (isReturnMasksCall(*CI)) {
      lowerReturnMasks(*CI, *WEnv, *FEnv);
      CI->eraseFromParent();
      continue;
    }
    if (isMaskedOutputStoreCall(*CI)) {
      auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::Output)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(CI,
                                 "feme-cpu-wrap-fragment: masked output store "
                                 "references an unknown signature element");
        return false;
      }
      lowerFragmentOutputStore(*CI, *Elt, *WEnv, *FEnv);
      CI->eraseFromParent();
      continue;
    }

    StageOpKind Kind;
    if (!isStageOpCall(*CI, &Kind))
      continue;
    auto *EltID = CI->arg_size() != 0
                      ? dyn_cast<ConstantInt>(CI->getArgOperand(0))
                      : nullptr;
    switch (Kind) {
    case StageOpKind::InputLoad: {
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::Input)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(CI,
                                 "feme-cpu-wrap-fragment: input load refers "
                                 "to an unknown signature element");
        return false;
      }
      Value *Lowered = lowerFragmentInputLoad(*CI, *Elt, *WEnv, *FEnv);
      if (!Lowered)
        return false;
      CI->replaceAllUsesWith(Lowered);
      CI->eraseFromParent();
      break;
    }
    case StageOpKind::SubpassLoad: {
      Value *Lowered = lowerFragmentSubpassLoad(*CI, F, *WEnv, *FEnv);
      if (!Lowered)
        return false;
      CI->replaceAllUsesWith(Lowered);
      CI->eraseFromParent();
      break;
    }
    case StageOpKind::InterpolateAtCentroid:
    case StageOpKind::InterpolateAtSample:
    case StageOpKind::InterpolateAtOffset:
      F.getContext().emitError(
          CI, "feme-cpu-wrap-fragment: pull-model interpolation "
              "is not implemented yet");
      return false;
    default:
      F.getContext().emitError(
          CI, "feme-cpu-wrap-fragment: unexpected stage op left "
              "for the fragment wrapper");
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
  /// (roadmap F8a) See `FemeShaderResources::SubpassInputHeap`'s comment.
  Value *SubpassInputHeap = nullptr;
  Value *SubpassInputHeapCount = nullptr;
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *Invocations = nullptr;
  Value *Results = nullptr;
  Value *QuadCount = nullptr;
};

WrapperEnv buildWrapperEnv(IRBuilder<> &Builder, StructType *ArgsTy,
                           Value *Args) {
  LLVMContext &Ctx = Builder.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Builder.getInt32Ty();
  WrapperEnv Env;
  Env.QuadCount =
      loadStructField(Builder, ArgsTy, Args, FragmentArgsFieldQuadCount, I32Ty);
  Env.InputLayout = loadStructField(Builder, ArgsTy, Args,
                                    FragmentArgsFieldInputLayout, PtrTy);
  Env.Inputs =
      loadStructField(Builder, ArgsTy, Args, FragmentArgsFieldInputs, PtrTy);
  Env.OutputLayout = loadStructField(Builder, ArgsTy, Args,
                                     FragmentArgsFieldOutputLayout, PtrTy);
  Env.Outputs =
      loadStructField(Builder, ArgsTy, Args, FragmentArgsFieldOutputs, PtrTy);
  Env.Invocations = loadStructField(Builder, ArgsTy, Args,
                                    FragmentArgsFieldInvocations, PtrTy);
  Env.Results =
      loadStructField(Builder, ArgsTy, Args, FragmentArgsFieldResults, PtrTy);

  Value *ResourcesRaw =
      loadStructField(Builder, ArgsTy, Args, FragmentArgsFieldResources, PtrTy);
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
  Env.SubpassInputHeap =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldSubpassInputHeap, PtrTy);
  Env.SubpassInputHeapCount =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldSubpassInputHeapCount, I32Ty);
  return Env;
}

Value *buildQuadMaskValue(IRBuilder<> &Builder, Value *InvocationsArg,
                          Value *QuadIndex, unsigned Field, unsigned WaveSize) {
  StructType *InvocationTy = getFragmentInvocationType(Builder.getContext());
  Value *Base = Builder.CreateBitCast(
      InvocationsArg, PointerType::get(Builder.getContext(), 0));
  Value *InvocationPtr =
      Builder.CreateInBoundsGEP(InvocationTy, Base, QuadIndex);
  Value *MaskPtr = Builder.CreateStructGEP(InvocationTy, InvocationPtr, Field);
  return Builder.CreateLoad(Builder.getInt32Ty(), MaskPtr);
}

Function *buildWrapper(Function &Body) {
  std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(Body);
  if (!WEnv)
    return nullptr;
  Module &M = *Body.getParent();
  LLVMContext &Ctx = M.getContext();
  unsigned WaveSize =
      cast<FixedVectorType>(WEnv->EntryMask->getType())->getNumElements();
  unsigned QuadsPerWave = WaveSize / 4;

  StructType *ArgsTy = getFragmentArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);

  Function *Wrapper = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false),
      GlobalValue::ExternalLinkage, getEntrySymbolName(Body.getName()), M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  BasicBlock *HeaderBB = BasicBlock::Create(Ctx, "wave.loop.header", Wrapper);
  BasicBlock *BodyBB = BasicBlock::Create(Ctx, "wave.loop.body", Wrapper);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "wave.loop.exit", Wrapper);

  IRBuilder<> Entry(EntryBB);
  WrapperEnv Env = buildWrapperEnv(Entry, ArgsTy, Args);
  Value *Waves = Entry.CreateUDiv(
      Entry.CreateAdd(Env.QuadCount, Entry.getInt32(QuadsPerWave - 1)),
      Entry.getInt32(QuadsPerWave), "waves");
  Entry.CreateBr(HeaderBB);

  IRBuilder<> Header(HeaderBB);
  PHINode *W = Header.CreatePHI(I32Ty, 2, "w");
  W->addIncoming(Header.getInt32(0), EntryBB);
  Value *Cond = Header.CreateICmpULT(W, Waves, "wave.cond");
  Header.CreateCondBr(Cond, BodyBB, ExitBB);

  IRBuilder<> BodyIR(BodyBB);
  Value *LiveMask =
      PoisonValue::get(FixedVectorType::get(Type::getInt1Ty(Ctx), WaveSize));
  Value *SideMask =
      PoisonValue::get(FixedVectorType::get(Type::getInt1Ty(Ctx), WaveSize));
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    unsigned QuadInWave = Lane / 4;
    unsigned LaneInQuad = Lane & 3u;
    Value *QuadIndex =
        BodyIR.CreateAdd(BodyIR.CreateMul(W, BodyIR.getInt32(QuadsPerWave)),
                         BodyIR.getInt32(QuadInWave));
    Value *QuadActive = BodyIR.CreateICmpULT(QuadIndex, Env.QuadCount);
    Value *LiveBits =
        buildQuadMaskValue(BodyIR, Env.Invocations, QuadIndex,
                           FragmentInvocationFieldLiveMask, WaveSize);
    Value *SideBits =
        buildQuadMaskValue(BodyIR, Env.Invocations, QuadIndex,
                           FragmentInvocationFieldSideEffectMask, WaveSize);
    Value *LaneLive = BodyIR.CreateAnd(
        QuadActive,
        BodyIR.CreateTrunc(
            BodyIR.CreateAnd(
                BodyIR.CreateLShr(LiveBits, BodyIR.getInt32(LaneInQuad)),
                BodyIR.getInt32(1)),
            BodyIR.getInt1Ty()));
    Value *LaneSide = BodyIR.CreateAnd(
        QuadActive,
        BodyIR.CreateTrunc(
            BodyIR.CreateAnd(
                BodyIR.CreateLShr(SideBits, BodyIR.getInt32(LaneInQuad)),
                BodyIR.getInt32(1)),
            BodyIR.getInt1Ty()));
    LiveMask =
        BodyIR.CreateInsertElement(LiveMask, LaneLive, BodyIR.getInt32(Lane));
    SideMask =
        BodyIR.CreateInsertElement(SideMask, LaneSide, BodyIR.getInt32(Lane));
  }

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
    else if (Arg.getName() == "subpass_input_heap")
      CallArgs.push_back(Env.SubpassInputHeap);
    else if (Arg.getName() == "subpass_input_heap_count")
      CallArgs.push_back(Env.SubpassInputHeapCount);
    else if (Arg.getName() == "wave_group_id_x" ||
             Arg.getName() == "wave_group_id_y" ||
             Arg.getName() == "wave_group_id_z")
      CallArgs.push_back(BodyIR.getInt32(0));
    else if (Arg.getName() == "wave_index")
      CallArgs.push_back(W);
    else if (Arg.getName() == "wave_entry_mask")
      CallArgs.push_back(LiveMask);
    else if (Arg.getName() == "wave_sideeffect_mask")
      CallArgs.push_back(SideMask);
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
    else if (Arg.getName() == InvocationsParamName)
      CallArgs.push_back(Env.Invocations);
    else if (Arg.getName() == ResultsParamName)
      CallArgs.push_back(Env.Results);
    else
      llvm_unreachable("unexpected parameter for FragmentWrapperPass");
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

PreservedAnalyses FragmentWrapperPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Fragment)
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    Function *Body = appendFragmentStageParams(*F);
    if (!lowerFragmentStageOps(*Body))
      continue;
    if (buildWrapper(*Body))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

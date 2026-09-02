//===- VertexWrapper.cpp - CPU target vertex entry wrapper ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/VertexWrapper.h"

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
constexpr StringLiteral InvocationsParamName = "stage_vertex_invocations";

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

struct VertexStageEnv {
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *Invocations = nullptr;
};

std::optional<VertexStageEnv> getVertexStageEnv(Function &F) {
  VertexStageEnv Env;
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
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

Function *appendVertexStageParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  SmallVector<Type *, 12> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, PtrTy, PtrTy, PtrTy, PtrTy});

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
  Value *TypedElements =
      Builder.CreateBitCast(Elements, PointerType::get(Ctx, 0));
  Value *EntryPtr = Builder.CreateInBoundsGEP(ElementTy, TypedElements,
                                              Builder.getInt32(ElementID));
  Value *FieldPtr = Builder.CreateStructGEP(ElementTy, EntryPtr, Field);
  return Builder.CreateLoad(FieldTy, FieldPtr);
}

Value *computeStageStorageAddress(IRBuilder<> &Builder, Value *LayoutArg,
                                  Value *BasePtr, unsigned ElementID,
                                  const SignatureElement &Elt, Value *Row,
                                  Value *Component, Value *InvocationIndex) {
  LLVMContext &Ctx = Builder.getContext();
  Type *I8PtrTy = PointerType::get(Ctx, 0);
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
  Value *Bytes = Builder.CreateBitCast(BasePtr, I8PtrTy);
  return Builder.CreateInBoundsGEP(Builder.getInt8Ty(), Bytes, ByteOffset);
}

Value *loadVertexSystemValue(IRBuilder<> &Builder, const SignatureElement &Elt,
                             Value *InvocationsArg, Value *InvocationIndex,
                             unsigned Lane) {
  LLVMContext &Ctx = Builder.getContext();
  StructType *InvocationTy = getVertexInvocationType(Ctx);
  Value *InvocationBase =
      Builder.CreateBitCast(InvocationsArg, PointerType::get(Ctx, 0));
  Value *InvocationPtr =
      Builder.CreateInBoundsGEP(InvocationTy, InvocationBase, InvocationIndex);
  switch (Elt.SystemValue) {
  case SignatureSystemValue::VertexID: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         VertexInvocationFieldVertexID);
    return Builder.CreateLoad(Builder.getInt32Ty(), Ptr);
  }
  case SignatureSystemValue::InstanceID: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         VertexInvocationFieldInstanceID);
    return Builder.CreateLoad(Builder.getInt32Ty(), Ptr);
  }
  case SignatureSystemValue::BaseVertex: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         VertexInvocationFieldBaseVertex);
    return Builder.CreateLoad(Builder.getInt32Ty(), Ptr);
  }
  case SignatureSystemValue::BaseInstance: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         VertexInvocationFieldBaseInstance);
    return Builder.CreateLoad(Builder.getInt32Ty(), Ptr);
  }
  case SignatureSystemValue::DrawID: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         VertexInvocationFieldDrawID);
    return Builder.CreateLoad(Builder.getInt32Ty(), Ptr);
  }
  case SignatureSystemValue::ViewIndex: {
    Value *Ptr = Builder.CreateStructGEP(InvocationTy, InvocationPtr,
                                         VertexInvocationFieldViewIndex);
    return Builder.CreateLoad(Builder.getInt32Ty(), Ptr);
  }
  default:
    Builder.getContext().emitError(
        Twine("feme-cpu-wrap-vertex: unsupported vertex system value for "
              "element ") +
        Twine(Elt.ElementID));
    return UndefValue::get(Builder.getInt32Ty());
  }
}

Value *lowerVertexInputLoad(CallInst &CI, const SignatureElement &Elt,
                            const WaveBodyEnv &WEnv,
                            const VertexStageEnv &VEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *LaneResult = Constant::getNullValue(ScalarTy);
    if (Elt.SystemValue != SignatureSystemValue::None) {
      Value *InvocationIndex =
          getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
      LaneResult = loadVertexSystemValue(Builder, Elt, VEnv.Invocations,
                                         InvocationIndex, Lane);
    } else {
      Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
      Value *Component =
          extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
      Value *Vertex = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
      auto *VertexConst = dyn_cast<ConstantInt>(Vertex);
      if (!VertexConst || VertexConst->getZExtValue() != 0) {
        CI.getContext().emitError(
            &CI, "feme-cpu-wrap-vertex: synthetic vertex layouts only support "
                 "vertex operand 0");
        return nullptr;
      }
      Value *InvocationIndex =
          getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
      Value *Addr = computeStageStorageAddress(Builder, VEnv.InputLayout,
                                               VEnv.Inputs, Elt.ElementID, Elt,
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

void lowerVertexOutputStore(CallInst &CI, const SignatureElement &Elt,
                            const WaveBodyEnv &WEnv,
                            const VertexStageEnv &VEnv) {
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
          &CI, "feme-cpu-wrap-vertex: synthetic vertex layouts only support "
               "vertex operand 0");
      return;
    }

    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *Addr = computeStageStorageAddress(Builder, VEnv.OutputLayout,
                                             VEnv.Outputs, Elt.ElementID, Elt,
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

bool lowerVertexStageOps(Function &F) {
  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(F);
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedOutputStoreCall(*CI);
  if (!UsesStageOps)
    return true;
  if (!Sig) {
    F.getContext().emitError(
        "feme-cpu-wrap-vertex: vertex stage wrapper requires attached "
        "feme.signature metadata");
    return false;
  }

  std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(F);
  std::optional<VertexStageEnv> VEnv = getVertexStageEnv(F);
  if (!WEnv || !VEnv)
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
        F.getContext().emitError(CI,
                                 "feme-cpu-wrap-vertex: masked output store "
                                 "references an unknown signature element");
        return false;
      }
      lowerVertexOutputStore(*CI, *Elt, *WEnv, *VEnv);
      CI->eraseFromParent();
      continue;
    }

    StageOpKind Kind;
    if (!isStageOpCall(*CI, &Kind))
      continue;
    auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    if (!EltID) {
      F.getContext().emitError(CI, "feme-cpu-wrap-vertex: stage IO requires a "
                                   "constant element ID");
      return false;
    }

    switch (Kind) {
    case StageOpKind::InputLoad: {
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::Input);
      if (!Elt) {
        F.getContext().emitError(CI, "feme-cpu-wrap-vertex: input load refers "
                                     "to an unknown signature element");
        return false;
      }
      Value *Lowered = lowerVertexInputLoad(*CI, *Elt, *WEnv, *VEnv);
      if (!Lowered)
        return false;
      CI->replaceAllUsesWith(Lowered);
      CI->eraseFromParent();
      break;
    }
    default:
      F.getContext().emitError(CI,
                               "feme-cpu-wrap-vertex: unexpected stage op left "
                               "for the vertex wrapper");
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
  Value *Invocations = nullptr;
  Value *InvocationCount = nullptr;
};

WrapperEnv buildWrapperEnv(IRBuilder<> &Builder, StructType *ArgsTy,
                           Value *Args) {
  LLVMContext &Ctx = Builder.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Builder.getInt32Ty();
  WrapperEnv Env;
  Env.InvocationCount = loadStructField(Builder, ArgsTy, Args,
                                        VertexArgsFieldInvocationCount, I32Ty);
  Env.InputLayout =
      loadStructField(Builder, ArgsTy, Args, VertexArgsFieldInputLayout, PtrTy);
  Env.Inputs =
      loadStructField(Builder, ArgsTy, Args, VertexArgsFieldInputs, PtrTy);
  Env.OutputLayout = loadStructField(Builder, ArgsTy, Args,
                                     VertexArgsFieldOutputLayout, PtrTy);
  Env.Outputs =
      loadStructField(Builder, ArgsTy, Args, VertexArgsFieldOutputs, PtrTy);
  Env.Invocations =
      loadStructField(Builder, ArgsTy, Args, VertexArgsFieldInvocations, PtrTy);

  Value *ResourcesRaw =
      loadStructField(Builder, ArgsTy, Args, VertexArgsFieldResources, PtrTy);
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

  StructType *ArgsTy = getVertexArgsType(Ctx);
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
      Entry.CreateAdd(Env.InvocationCount, Entry.getInt32(WaveSize - 1)),
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
  Value *WideCount = BodyIR.CreateVectorSplat(WaveSize, Env.InvocationCount);
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
    // Roadmap H6o: NumWorkgroups is meaningless for a non-compute-
    // family stage (SPIR-V does not permit this builtin outside
    // compute/mesh/task), so this widened function's own
    // wave_group_count_x/y/z parameters are dead here -- 1 rather
    // than 0 avoids encoding a nonsensical "0 workgroups" default,
    // mirroring this same file's own wave_group_id_x/y/z dummy above.
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
    else if (Arg.getName() == OutputLayoutParamName)
      CallArgs.push_back(Env.OutputLayout);
    else if (Arg.getName() == OutputsParamName)
      CallArgs.push_back(Env.Outputs);
    else if (Arg.getName() == InvocationsParamName)
      CallArgs.push_back(Env.Invocations);
    else
      llvm_unreachable("unexpected parameter for VertexWrapperPass");
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

PreservedAnalyses VertexWrapperPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Vertex)
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    Function *Body = appendVertexStageParams(*F);
    if (!lowerVertexStageOps(*Body))
      continue;
    if (buildWrapper(*Body))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

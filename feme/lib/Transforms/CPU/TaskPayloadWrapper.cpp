//===- TaskPayloadWrapper.cpp - Task stage payload store lowering --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See TaskPayloadWrapper.h for this pass's scope and roadmap H6c-a-b's own
// design notes.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/TaskPayloadWrapper.h"

#include "StageArgsLayout.h"
#include "StageMaskCalls.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/SIMDize.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme;
using namespace feme::cpu;

namespace {

constexpr StringLiteral PayloadParamName = "task_payload";
constexpr StringLiteral MaxPayloadBytesParamName = "task_max_payload_bytes";

/// This pass's own trailing wave-body parameters (see the file comment):
/// the payload's base pointer and its runtime-bound byte count.
struct TaskPayloadStageEnv {
  Value *Payload = nullptr;
  Value *MaxPayloadBytes = nullptr;
};

std::optional<TaskPayloadStageEnv> getTaskPayloadStageEnv(Function &F) {
  TaskPayloadStageEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == PayloadParamName)
      Env.Payload = &Arg, Found = true;
    else if (Arg.getName() == MaxPayloadBytesParamName)
      Env.MaxPayloadBytes = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

/// Appends this pass's own trailing parameters to \p F, splicing its body
/// into a freshly-created function -- the same "grow the signature,
/// preserve the body" shape every other stage wrapper's own
/// `append*StageParams` already uses (see e.g. `MeshOutputWrapper.cpp`'s
/// `appendMeshOutputParams`).
Function *appendTaskPayloadParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Type *, 8> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, I32Ty});

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
  (&*ArgIt++)->setName(PayloadParamName);
  (&*ArgIt++)->setName(MaxPayloadBytesParamName);

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

/// Lowers one `feme.cpu.masked.task.payload.store` call: every active lane
/// stores its own value at the same `Env.Payload + Offset` address every
/// lane of this call shares (`Offset` is a single compile-time constant,
/// per `StageOpKind::TaskPayloadStore`'s own comment -- not a per-lane
/// value the way a mesh output store's `Vertex` operand is), the same
/// "every lane may write, the mask decides whose value survives" shape
/// `MeshOutputWrapper.cpp`'s `lowerMeshOutputStore` already uses, just
/// against one fixed address instead of one address per output slot.
///
/// Defensively skips the store entirely (leaving `Env.Payload` at that
/// offset untouched) if `Offset` plus this value's own byte size would
/// exceed `Env.MaxPayloadBytes`: `Offset` is resolved against the
/// *shader's* own declared payload type at canonicalization time (roadmap
/// H6h/H6i), not against this particular dispatch's own bound, so an
/// oversized payload type is still caught here rather than corrupting
/// host memory beyond `Env.Payload`'s own allocation -- mirroring
/// `MeshOutputWrapper.cpp`'s own `clampSlotIndex` precedent for the same
/// "a canonicalized store's own operand is not yet validated against this
/// dispatch's real runtime bound" gap.
void lowerTaskPayloadStore(CallInst &CI, const TaskPayloadStageEnv &Env,
                          const DataLayout &DL) {
  IRBuilder<> Builder(&CI);
  uint64_t Offset = cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue();
  Value *ValueArg = CI.getArgOperand(1);
  Value *MaskArg = CI.getArgOperand(2);

  auto *WideTy = dyn_cast<FixedVectorType>(ValueArg->getType());
  unsigned WaveSize = WideTy ? WideTy->getNumElements() : 1;
  Type *ScalarTy = WideTy ? WideTy->getElementType() : ValueArg->getType();
  uint64_t ByteSize = DL.getTypeStoreSize(ScalarTy).getFixedValue();

  Value *End =
      Builder.getInt32(static_cast<uint32_t>(Offset + ByteSize));
  Value *InBounds =
      Builder.CreateICmpULE(End, Env.MaxPayloadBytes, "payload.inbounds");
  Value *Addr = Builder.CreateInBoundsGEP(
      Builder.getInt8Ty(), Env.Payload,
      Builder.getInt32(static_cast<uint32_t>(Offset)), "payload.addr");

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, MaskArg, Lane);
    Value *EffectiveMask = Builder.CreateAnd(Mask, InBounds, "payload.mask");
    auto *MaskConst = dyn_cast<ConstantInt>(EffectiveMask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *LaneVal = extractLaneOrScalar(Builder, ValueArg, Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(ScalarTy, Addr);
      LaneVal = Builder.CreateSelect(EffectiveMask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

/// Lowers every masked task payload store in \p F, or diagnoses and
/// returns false if \p F uses a `feme.stage.*` op this pass does not
/// support (any op other than `TaskPayloadStore` -- a task entry point has
/// no ordinary stage-IO input to read, and `EmitMeshTasksEXT` has no
/// canonicalized form yet, roadmap H6d's own scope note).
bool lowerTaskPayloadStageOps(Function &F, const DataLayout &DL) {
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedTaskPayloadStoreCall(*CI);
  if (!UsesStageOps)
    return true;

  std::optional<TaskPayloadStageEnv> Env = getTaskPayloadStageEnv(F);
  if (!Env)
    return false;

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (isMaskedTaskPayloadStoreCall(*CI)) {
      lowerTaskPayloadStore(*CI, *Env, DL);
      CI->eraseFromParent();
      continue;
    }
    F.getContext().emitError(
        CI, "feme-cpu-wrap-task-payload: unexpected stage op left for the "
            "task payload wrapper");
    return false;
  }
  return true;
}

} // namespace

PreservedAnalyses TaskPayloadWrapperPass::run(Module &M,
                                              ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Amplification)
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    const DataLayout &DL = F->getDataLayout();
    Function *Body = appendTaskPayloadParams(*F);
    if (lowerTaskPayloadStageOps(*Body, DL))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

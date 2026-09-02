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
#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

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
/// (Roadmap H6s) `FemeTaskArgs::MeshGroupCount`, the trailing parameter
/// this pass appends alongside `Payload`/`MaxPayloadBytes` for a
/// canonicalized `feme.stage.emit_mesh_tasks` call to write through.
constexpr StringLiteral MeshGroupCountParamName = "task_mesh_group_count";
/// (Roadmap H6t) `FemeTaskArgs::DrawID`, SPIR-V's `DrawIndex` builtin
/// (`gl_DrawID`): a task entry's one legitimate ordinary stage-IO input,
/// mirroring `MeshOutputWrapper.cpp`'s own `DrawIDParamName` (roadmap
/// H6p) exactly -- see that file's own comment for why this builtin is
/// workgroup-uniform rather than per-lane.
constexpr StringLiteral DrawIDParamName = "task_draw_id";

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

/// This pass's own trailing wave-body parameters (see the file comment):
/// the payload's base pointer and its runtime-bound byte count.
struct TaskPayloadStageEnv {
  Value *Payload = nullptr;
  Value *MaxPayloadBytes = nullptr;
  /// (Roadmap H6s) `FemeTaskArgs::MeshGroupCount`: the `uint32_t*` this
  /// pass's `lowerEmitMeshTasks` writes the requested mesh dispatch's 3D
  /// group count through, addressing one contiguous 3-element block (see
  /// `RuntimeABI.h`'s own comment on the field).
  Value *MeshGroupCount = nullptr;
  /// (Roadmap H6t) `FemeTaskArgs::DrawID`, workgroup-uniform, threaded
  /// through unchanged from `EntryWrapper.cpp`'s own `Env.TaskDrawID`.
  Value *DrawID = nullptr;
};

std::optional<TaskPayloadStageEnv> getTaskPayloadStageEnv(Function &F) {
  TaskPayloadStageEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == PayloadParamName)
      Env.Payload = &Arg, Found = true;
    else if (Arg.getName() == MaxPayloadBytesParamName)
      Env.MaxPayloadBytes = &Arg, Found = true;
    else if (Arg.getName() == MeshGroupCountParamName)
      Env.MeshGroupCount = &Arg, Found = true;
    else if (Arg.getName() == DrawIDParamName)
      Env.DrawID = &Arg, Found = true;
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
  ParamTypes.append({PtrTy, I32Ty, PtrTy, I32Ty});

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
  (&*ArgIt++)->setName(MeshGroupCountParamName);
  (&*ArgIt++)->setName(DrawIDParamName);

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

/// Lowers one `feme.cpu.masked.emit_mesh_tasks` call (roadmap H6s): every
/// active lane writes its own `(groupCountX, groupCountY, groupCountZ)` to
/// `Env.MeshGroupCount`'s three contiguous slots, the same
/// "every lane may write, the mask decides whose value survives, repeated
/// writes of a spec-identical value are idempotent" shape
/// `MeshOutputWrapper.cpp`'s `lowerSetMeshOutputs` already uses for its own
/// workgroup-uniform pair -- just three slots instead of two, and no
/// per-slot addressing since there is only ever one dispatch request per
/// workgroup.
void lowerEmitMeshTasks(CallInst &CI, const TaskPayloadStageEnv &Env) {
  IRBuilder<> Builder(&CI);
  Value *GroupCountXArg = CI.getArgOperand(0);
  auto *WideTy = dyn_cast<FixedVectorType>(GroupCountXArg->getType());
  unsigned WaveSize = WideTy ? WideTy->getNumElements() : 1;
  Type *ScalarTy = WideTy ? WideTy->getElementType() : GroupCountXArg->getType();

  Value *Addrs[3];
  for (unsigned Dim = 0; Dim != 3; ++Dim)
    Addrs[Dim] = Builder.CreateInBoundsGEP(
        ScalarTy, Env.MeshGroupCount, Builder.getInt32(Dim),
        "mesh.group.count.addr");

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *LaneCounts[3];
    for (unsigned Dim = 0; Dim != 3; ++Dim)
      LaneCounts[Dim] =
          extractLaneOrScalar(Builder, CI.getArgOperand(Dim), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      for (unsigned Dim = 0; Dim != 3; ++Dim) {
        Value *OldCount = Builder.CreateLoad(ScalarTy, Addrs[Dim]);
        LaneCounts[Dim] =
            Builder.CreateSelect(Mask, LaneCounts[Dim], OldCount);
      }
    }
    for (unsigned Dim = 0; Dim != 3; ++Dim)
      Builder.CreateStore(LaneCounts[Dim], Addrs[Dim]);
  }
}

/// Lowers `feme.stage.input.load` for a task entry's one legitimate
/// stage-IO input, SPIR-V's `DrawIndex` builtin (`gl_DrawID`,
/// `SignatureSystemValue::DrawID`, roadmap H6t): workgroup-uniform,
/// mirroring `MeshOutputWrapper.cpp`'s own `lowerMeshInputLoad` (roadmap
/// H6p) exactly, including that `Row`/`Component`/`Vertex` (`CI`'s other
/// operands) carry no meaning for a whole-builtin scalar like this one and
/// are intentionally left unread.
Value *lowerTaskInputLoad(CallInst &CI, const WaveBodyEnv &WEnv,
                          const TaskPayloadStageEnv &Env) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *LaneResult =
        Builder.CreateSelect(Active, Env.DrawID, Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers every masked task payload store, `emit_mesh_tasks` call, and
/// `gl_DrawID` input load in \p F, or diagnoses and returns false if \p F
/// uses a `feme.stage.*` op this pass does not support (any op other than
/// `TaskPayloadStore`/`EmitMeshTasks`/an `InputLoad` of `gl_DrawID` --
/// roadmap H6t found that, mirroring `MeshOutputWrapper.cpp`'s own H6p
/// finding, a task entry point *does* have one legitimate ordinary
/// stage-IO input to read after all).
bool lowerTaskPayloadStageOps(Function &F, const WaveBodyEnv &WEnv,
                              const DataLayout &DL) {
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedTaskPayloadStoreCall(*CI) ||
                      isMaskedEmitMeshTasksCall(*CI);
  if (!UsesStageOps)
    return true;

  std::optional<TaskPayloadStageEnv> Env = getTaskPayloadStageEnv(F);
  if (!Env)
    return false;

  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(F);

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (isMaskedTaskPayloadStoreCall(*CI)) {
      lowerTaskPayloadStore(*CI, *Env, DL);
      CI->eraseFromParent();
      continue;
    }
    if (isMaskedEmitMeshTasksCall(*CI)) {
      lowerEmitMeshTasks(*CI, *Env);
      CI->eraseFromParent();
      continue;
    }
    StageOpKind Kind;
    if (isStageOpCall(*CI, &Kind) && Kind == StageOpKind::InputLoad) {
      if (!Sig) {
        F.getContext().emitError(
            "feme-cpu-wrap-task-payload: task payload wrapper requires "
            "attached feme.signature metadata to lower an input load");
        return false;
      }
      auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::Input)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-task-payload: input load references an "
                "unknown signature element");
        return false;
      }
      if (Elt->SystemValue != SignatureSystemValue::DrawID) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-task-payload: unsupported task stage input "
                "system value");
        return false;
      }
      Value *Result = lowerTaskInputLoad(*CI, WEnv, *Env);
      CI->replaceAllUsesWith(Result);
      CI->eraseFromParent();
      continue;
    }
    // Only a genuinely unlowered `feme.stage.*` call is this pass's own
    // problem to diagnose (see the function comment) -- everything else
    // still calling through `F` at this point (resource loads/stores,
    // ordinary masked memory ops, arithmetic feeding a payload store's or
    // `EmitMeshTasksEXT`'s own operands, etc.) is unrelated to task
    // payload/mesh-dispatch lowering and must be left alone rather than
    // rejected outright (roadmap H6s, mirroring `MeshOutputWrapperPass`'s
    // own H6g-b-d precedent exactly): the `UsesStageOps` gate above only
    // established that *some* call in `F` needs this pass's attention, not
    // that *every* call does.
    if (!isStageOpCall(*CI))
      continue;
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
    // `appendTaskPayloadParams` splices `F`'s body into a brand-new
    // function and erases `F`, so any `WaveBodyEnv` captured against the
    // old function's now-destroyed `Argument`s would dangle -- re-derive
    // it against `Body`, whose spliced-in parameters keep every original
    // `WaveBodyEnv`-recognized name (`wave_entry_mask` et al.) intact
    // (mirroring `MeshOutputWrapperPass::run`'s own precedent exactly).
    std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(*Body);
    assert(WEnv && "getWaveBodyEnv succeeded before appendTaskPayloadParams "
                   "but failed after");
    if (lowerTaskPayloadStageOps(*Body, *WEnv, DL))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
